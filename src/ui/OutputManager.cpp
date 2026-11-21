#include "OutputManager.h"
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QSettings>
#include <QTimer>

OutputManager::OutputManager(PresentationController *controller, QObject *parent, std::shared_ptr<DisplayModeProvider> modes)
    : QObject(parent), controller_(controller),
      modes_(modes ? std::move(modes) : nativeDisplayModeProvider()) {
    for (const auto id : defaultOutputIds) createOutput(id);
    for (auto *screen : QGuiApplication::screens()) watchScreen(screen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) { watchScreen(screen); scheduleRefresh(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        for (auto &[id, route] : outputs_) {
            if (route.activeScreen == screen || route.configuration.target == screenId(screen)) {
                route.window->hide();
                route.activeScreen.clear();
            }
            // Keep the requested mode so it can be reapplied when the display returns.
            if (route.modeLease.screen == screen) route.modeLease = {};
        }
        watchedScreens_.remove(screen); disconnect(screen, nullptr, this, nullptr); scheduleRefresh();
    });
}
OutputManager::~OutputManager() {
    shutdown();
    for (const auto &[id, route] : outputs_) route.window->removeEventFilter(this);
}

void OutputManager::createOutput(OutputId id) {
    const auto kind = isStageOutput(id) ? DisplayWindow::Kind::Stage : DisplayWindow::Kind::Audience;
    auto *source = kind == DisplayWindow::Kind::Audience && id != OutputId::Audience1 ? &audience() : nullptr;
    auto &route = outputs_[id];
    route.window = std::make_unique<DisplayWindow>(kind, controller_, nullptr, source);
    auto &window = *route.window;
    window.setLiveAuthorizationCheck([this] { return liveAuthorized(); });
    window.setWindowTitle(QStringLiteral("WorshipCue — %1").arg(outputName(id)));
    window.installEventFilter(this);
    connect(&window, &DisplayWindow::operatorFocusRequested, this, &OutputManager::operatorFocusRequested);
    connect(&window, &DisplayWindow::closedByOperator, this, [this, id] {
        if (shuttingDown_) return;
        if (modeTrial_ && modeTrial_->id == id) {
            finishModeTrial(false);
            return;
        }
        QString error;
        if (setTarget(id, "off", &error)) return;
        // Keep the output closed and retain its mode lease for a later restoration retry.
        auto &closed = outputs_.at(id);
        closed.configuration.target = "off";
        closed.configuration.signalMode.clear();
        closed.error = "desktop mode restore failed: " + error;
        closed.activeScreen.clear();
        closed.window->setConfiguration(closed.configuration);
        persist(id);
        emit targetsChanged();
        emit statusChanged();
    });
}

OutputId OutputManager::addOutput(DisplayWindow::Kind kind) {
    int next = kind == DisplayWindow::Kind::Stage ? 5 : 4;
    while (outputs_.count(OutputId(next))) next += 2;
    const auto id = OutputId(next);
    createOutput(id);
    persist(id);
    emit outputsChanged();
    return id;
}

QVector<OutputId> OutputManager::outputIds() const {
    QVector<OutputId> ids;
    ids.reserve(outputs_.size());
    for (const auto &[id, route] : outputs_) ids.append(id);
    return ids;
}

DisplayWindow &OutputManager::output(OutputId id) { return *outputs_.at(id).window; }
const DisplayWindow &OutputManager::output(OutputId id) const { return *outputs_.at(id).window; }
bool OutputManager::ownsWindow(const QWidget *window) const {
    for (const auto &[id, route] : outputs_) if (route.window.get() == window) return true;
    return false;
}
QString OutputManager::screenId(const QScreen *screen) {
    return QStringLiteral("screen:%1|%2|%3|%4").arg(screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber());
}
QScreen *OutputManager::findScreen(const QString &id) {
    if (!isDisplayTarget(id)) return nullptr;
    QVector<QScreen *> matches;
    for (auto *screen : QGuiApplication::screens()) if (screenId(screen) == id) matches.append(screen);
    if (matches.size() == 1) return matches.first();
    if (!matches.isEmpty()) return nullptr; // Ambiguous identities must not jump between projectors.
    const auto identity = id.section('|', 1);
    if (id.section('|', 3).isEmpty()) return nullptr;
    for (auto *screen : QGuiApplication::screens()) if (screenId(screen).section('|', 1) == identity) matches.append(screen);
    return matches.size() == 1 ? matches.first() : nullptr;
}
QString OutputManager::settingsKey(OutputId id) { return QStringLiteral("outputs/%1/configuration").arg(int(id)); }
bool OutputManager::isDisplayTarget(const QString &id) { return id.startsWith("screen:"); }
bool OutputManager::hasFullscreenConflict(OutputId id, const QString &target, const QScreen *screen) const {
    for (const auto &[otherId, other] : outputs_) {
        const auto &otherTarget = other.configuration.target;
        if (otherId != id && isDisplayTarget(otherTarget)
            && (otherTarget == target || (screen && findScreen(otherTarget) == screen))) return true;
    }
    return false;
}

QVector<OutputTarget> OutputManager::availableTargets() const {
    QVector<OutputTarget> result{{"off", "Off"}, {"preview", "Windowed Preview"}};
    QSet<QString> included{"off", "preview"};
    const auto screens = QGuiApplication::screens();
    for (int index = 0; index < screens.size(); ++index) {
        auto *screen = screens.at(index); const auto id = screenId(screen);
        if (included.contains(id) || !findScreen(id)) continue;
        const auto pixels = screen->size() * screen->devicePixelRatio();
        result.append({id, QStringLiteral("%1: %2 (%3×%4 desktop pixels)").arg(index + 1).arg(screen->name()).arg(pixels.width()).arg(pixels.height())});
        included.insert(id);
    }
    for (const auto &[id, route] : outputs_) if (!included.contains(route.configuration.target)) {
        const auto &target = route.configuration.target;
        auto *screen = findScreen(target);
        result.append({target, screen ? QStringLiteral("Reconnected: %1").arg(screen->name())
            : QStringLiteral("Disconnected or ambiguous: %1").arg(target.mid(7).section('|', 0, 0))});
        included.insert(target);
    }
    return result;
}
DisplayCapabilities OutputManager::capabilities(const QString &target) const {
    auto *screen = findScreen(target);
    if (!screen) return {{}, {}, false, "Assign a connected fullscreen display to inspect native modes. Off and preview do not change the physical signal."};
    return modes_->capabilities(screen);
}
bool OutputManager::releaseMode(ModeLease &lease, QString *error) {
    if (lease.screen && !lease.original.isEmpty() && modes_->capabilities(lease.screen).currentMode == lease.applied) {
        if (!modes_->apply(lease.screen, lease.original, error)) return false;
    }
    lease = {}; return true;
}
bool OutputManager::applySignal(OutputId id, const OutputConfiguration &configuration, QString *error) {
    auto &lease = outputs_.at(id).modeLease;
    auto *screen = findScreen(configuration.target);
    if (!screen || configuration.signalMode.isEmpty()) return releaseMode(lease, error);
    const auto caps = modes_->capabilities(screen);
    bool found = false;
    for (const auto &mode : caps.modes) found = found || mode.id == configuration.signalMode;
    if (!found || (!caps.canSetMode && caps.currentMode != configuration.signalMode)) {
        if (error) *error = "This resolution/refresh/scan combination is unavailable. Select an OS-reported mode or retain the desktop mode.";
        return false;
    }
    if (caps.currentMode == configuration.signalMode) {
        if (lease.screen && lease.screen != screen) return releaseMode(lease, error);
        if (lease.screen == screen) lease.applied = configuration.signalMode;
        return true;
    }
    const auto previous = lease;
    if (!modes_->apply(screen, configuration.signalMode, error)) return false;
    if (previous.screen && previous.screen != screen) {
        auto old = previous;
        if (!releaseMode(old, error)) {
            QString rollbackError;
            if (!modes_->apply(screen, caps.currentMode, &rollbackError)) qWarning() << "Display rollback failed:" << rollbackError;
            return false;
        }
    }
    lease = {screen, previous.screen == screen ? previous.original : caps.currentMode, configuration.signalMode};
    return true;
}
bool OutputManager::setTarget(OutputId id, const QString &target, QString *error) {
    auto config = configuration(id);
    if (config.target != target) config.signalMode.clear();
    config.target = target; return setConfiguration(id, config, error);
}
bool OutputManager::setConfiguration(OutputId id, const OutputConfiguration &config, QString *error) {
    if (modeTrial_ && modeTrial_->id == id) {
        if (error) *error = "Finish the physical display mode review first.";
        return false;
    }
    return applyConfiguration(id, config, true, error);
}

bool OutputManager::beginModeTrial(OutputId id, const OutputConfiguration &config, QString *error) {
    auto *screen = findScreen(config.target);
    if (modeTrial_ || !screen || config.signalMode.isEmpty() || !liveAuthorized()) {
        if (error) *error = "A physical mode trial requires a connected display and live authorization.";
        return false;
    }
    modeTrial_ = ModeTrial{id, configuration(id), screen};
    if (applyConfiguration(id, config, false, error)) return true;
    modeTrial_.reset();
    return false;
}

bool OutputManager::finishModeTrial(bool keep, QString *error) {
    if (error) error->clear();
    if (!modeTrial_ || shuttingDown_) {
        if (error) *error = "The physical mode trial is no longer active.";
        return false;
    }
    const auto trial = *modeTrial_;
    modeTrial_.reset();
    auto &route = outputs_.at(trial.id);
    if (keep && liveAuthorized() && trial.screen && route.activeScreen == trial.screen
        && modes_->capabilities(trial.screen).currentMode == route.configuration.signalMode) {
        if (persist(trial.id)) return true;
        if (error) *error = "Could not save the confirmed display mode.";
    } else if (keep && error) {
        *error = "The display or trial mode is no longer available.";
    }

    // The saved settings still describe the prior route, even if restoring the
    // native mode fails or that display was unplugged during confirmation.
    route.configuration = trial.previous;
    route.window->setConfiguration(trial.previous);
    if (isStageOutput(trial.id)) route.window->setStageLayout(stageLayout(trial.previous.stageLayout));
    route.error.clear();
    const bool restored = applySignal(trial.id, trial.previous, &route.error);
    if (restored) {
        applyTarget(trial.id);
    } else {
        route.window->hide();
        route.activeScreen.clear();
        if (error) *error = "Could not restore the physical display mode: " + route.error;
    }
    emit targetsChanged();
    emit statusChanged();
    return restored && !keep;
}

bool OutputManager::applyConfiguration(OutputId id, const OutputConfiguration &config, bool save, QString *error) {
    if (error) error->clear();
    if (!config.validate(error)) return false;
    auto *screen = findScreen(config.target);
    if (isDisplayTarget(config.target)) {
        if (hasFullscreenConflict(id, config.target, screen)) {
            if (error) *error = "Each fullscreen output must use a different physical display."; return false;
        }
        if (!screen && config.target != target(id)) {
            if (error) *error = "That display is unavailable or has an ambiguous identity. Choose a connected display or Windowed Preview."; return false;
        }
    }
    if (isStageOutput(id) && stageLayout(config.stageLayout).id != config.stageLayout) {
        if (error) *error = "Choose an existing stage layout."; return false;
    }
    if (liveAuthorized() && !applySignal(id, config, error)) return false;
    auto &route = outputs_.at(id);
    const auto &oldTarget = route.configuration.target;
    auto *oldScreen = findScreen(oldTarget);
    const bool resolvedConflict = oldTarget != config.target && oldScreen && hasFullscreenConflict(id, oldTarget, oldScreen);
    route.configuration = config;
    route.error.clear();
    shuttingDown_ = false;
    output(id).setConfiguration(config);
    if (isStageOutput(id)) output(id).setStageLayout(stageLayout(config.stageLayout));
    if (save) persist(id);
    applyTarget(id);
    if (resolvedConflict) scheduleRefresh();
    emit targetsChanged();
    emit statusChanged();
    return true;
}
bool OutputManager::persist(OutputId id) {
    QSettings settings;
    settings.setValue(settingsKey(id), QJsonDocument(configuration(id).toJson()).toJson(QJsonDocument::Compact));
    // Retain compatibility with the first-pair route preferences.
    if (id == OutputId::Audience1) settings.setValue("outputs/audienceTarget", target(id));
    if (id == OutputId::Stage1) settings.setValue("outputs/stageTarget", target(id));
    settings.sync();
    if (settings.status() != QSettings::NoError) qWarning() << "Could not save output configuration" << settings.fileName();
    return settings.status() == QSettings::NoError;
}
void OutputManager::restoreTargets() {
    shuttingDown_ = false;
    QSettings settings;
    settings.beginGroup("outputs");
    bool addedOutputs = false;
    for (const auto &group : settings.childGroups()) {
        bool ok = false;
        const int value = group.toInt(&ok);
        if (!ok || value < 4 || group != QString::number(value)
            || !settings.contains(group + "/configuration") || outputs_.count(OutputId(value))) continue;
        createOutput(OutputId(value));
        addedOutputs = true;
    }
    settings.endGroup();
    layouts_ = {StageLayout::standard()};
    const auto storedLayouts = settings.value("outputs/stageLayouts").toByteArray();
    const auto savedLayouts = storedLayouts.size() <= 20 * 1024 * 1024 ? QJsonDocument::fromJson(storedLayouts).array() : QJsonArray{};
    for (const auto &value : savedLayouts) {
        StageLayout layout;
        if (layouts_.size() >= 65) break;
        if (StageLayout::fromJson(value.toObject(), &layout) && layout.id != "standard") {
            bool duplicate = false;
            for (const auto &existing : layouts_) duplicate = duplicate || existing.id == layout.id || existing.name.compare(layout.name, Qt::CaseInsensitive) == 0;
            if (!duplicate) layouts_.append(layout);
        }
    }
    for (auto &[id, route] : outputs_) {
        OutputConfiguration config;
        if (settings.contains(settingsKey(id))) {
            const auto data = settings.value(settingsKey(id)).toByteArray();
            QString error;
            if (data.size() > 16384 || !OutputConfiguration::fromJson(QJsonDocument::fromJson(data).object(), &config, &error))
                qWarning() << "Ignoring invalid output configuration" << int(id) << error;
        } else {
            const QString legacy = id == OutputId::Audience1 ? "outputs/audienceTarget" : id == OutputId::Stage1 ? "outputs/stageTarget" : QString();
            const auto saved = legacy.isEmpty() ? QStringLiteral("off") : settings.value(legacy, "off").toString();
            if (saved == "off" || saved == "preview" || isDisplayTarget(saved)) config.target = saved;
        }
        if (stageLayout(config.stageLayout).id != config.stageLayout) config.stageLayout = "standard";
        route.configuration = config;
        output(id).setConfiguration(config);
        if (isStageOutput(id)) output(id).setStageLayout(stageLayout(config.stageLayout));
        if (isStageOutput(id)) {
            const auto timerData = settings.value(QStringLiteral("outputs/stageTimer%1").arg(int(id))).toByteArray();
            if (!timerData.isEmpty()) {
                const auto timer = timerData.size() <= 16384 ? QJsonDocument::fromJson(timerData).object() : QJsonObject{};
                StageTimerConfiguration timerConfiguration;
                QString error;
                if (timer["visible"].isBool() && timer["overlay"].isBool()
                    && StageTimerConfiguration::fromJson(timer["configuration"].toObject(), &timerConfiguration, &error)) {
                    output(id).configureStageTimer(timerConfiguration);
                    output(id).setStageTimerVisible(timer["visible"].toBool(), timer["overlay"].toBool());
                } else qWarning() << "Ignoring invalid saved stage timer" << int(id);
            }
        }
        persist(id);
    }
    for (const auto id : outputIds()) applyTarget(id);
    if (addedOutputs) emit outputsChanged();
    emit layoutsChanged(); emit targetsChanged(); emit statusChanged();
}
void OutputManager::applyTarget(OutputId id) {
    if (shuttingDown_) return;
    auto &route = outputs_.at(id);
    auto &window = *route.window;
    const auto &config = route.configuration;
    auto &active = route.activeScreen;
    if (!liveAuthorized()) { window.hide(); active.clear(); releaseMode(route.modeLease); return; }
    if (config.target == "off") { window.hide(); active.clear(); return; }
    if (config.target == "preview") { active.clear(); if (!window.isVisible() || window.isFullScreen()) window.showPreview(); return; }
    auto *screen = findScreen(config.target);
    if (!screen) { window.hide(); active.clear(); return; }
    if (modeTrial_ && modeTrial_->id == id && modeTrial_->screen != screen) {
        window.hide();
        active.clear();
        return;
    }
    // Previously separate display identities can resolve to the same screen
    // after a reconnect. Keep both routes hidden until one is reassigned.
    if (hasFullscreenConflict(id, config.target, screen)) {
        window.hide();
        active.clear();
        releaseMode(route.modeLease, &route.error);
        return;
    }
    QString error;
    if (!applySignal(id, config, &error)) { route.error = error; window.hide(); active.clear(); return; }
    route.error.clear();
    if (active == screen && window.isVisible() && window.isFullScreen()) {
        if (window.geometry() != screen->geometry()) window.setGeometry(screen->geometry());
        return;
    }
    active = screen; window.showFullscreenOn(screen);
}
QString OutputManager::status(OutputId id) const {
    if (!liveAuthorized()) return "Editing only — license required";
    const auto desired = target(id);
    const auto *screen = findScreen(desired);
    if (isDisplayTarget(desired) && !screen) return "Disconnected";
    if (screen && hasFullscreenConflict(id, desired, screen)) return "Display assignment conflict";
    const auto &error = outputs_.at(id).error;
    if (!error.isEmpty()) return (desired == "off" ? QStringLiteral("Off — %1") : QStringLiteral("Mode unavailable: %1")).arg(error);
    if (desired == "off") return "Off";
    const auto &window = output(id);
    if (!window.isVisible()) return "Off";
    if (window.isMinimized()) return "Minimized";
    if (desired == "preview" || !window.isFullScreen()) return "Windowed preview";
    return QStringLiteral("Fullscreen — %1").arg(screen->name());
}
StageLayout OutputManager::stageLayout(const QString &id) const {
    for (const auto &layout : layouts_) if (layout.id == id) return layout;
    return StageLayout::standard();
}
void OutputManager::persistLayouts() {
    QJsonArray saved; for (const auto &layout : layouts_) if (layout.id != "standard") saved.append(layout.toJson());
    QSettings settings; settings.setValue("outputs/stageLayouts", QJsonDocument(saved).toJson(QJsonDocument::Compact)); settings.sync();
}
void OutputManager::persistStageTimer(OutputId id) {
    const auto &window = output(id);
    const QJsonObject timer{{"configuration", window.stageTimerConfiguration().toJson()},
        {"visible", window.stageTimerElementsVisible()}, {"overlay", window.stageTimerOverlayEnabled()}};
    QSettings settings;
    settings.setValue(QStringLiteral("outputs/stageTimer%1").arg(int(id)), QJsonDocument(timer).toJson(QJsonDocument::Compact));
    settings.sync();
}
bool OutputManager::configureStageTimer(OutputId id, const StageTimerConfiguration &configuration, QString *error) {
    if (error) error->clear();
    if (!isStageOutput(id)) { if (error) *error = QStringLiteral("Choose a stage screen for this timer."); return false; }
    if (!output(id).configureStageTimer(configuration, error)) return false;
    persistStageTimer(id);
    return true;
}
bool OutputManager::showStageTimer(OutputId id, bool visible, QString *error) {
    if (error) error->clear();
    if (!isStageOutput(id)) { if (error) *error = QStringLiteral("Choose a stage screen for this timer."); return false; }
    output(id).setStageTimerVisible(visible);
    persistStageTimer(id);
    return true;
}
bool OutputManager::saveStageLayout(StageLayout layout, QString *error) {
    if (!layout.validate(error)) return false;
    if (layout.id == "standard") { if (error) *error = "The standard layout is preserved. Save a copy with a new name."; return false; }
    int existing = -1;
    for (int i = 0; i < layouts_.size(); ++i) {
        if (layouts_.at(i).id == layout.id) existing = i;
        else if (layouts_.at(i).name.compare(layout.name, Qt::CaseInsensitive) == 0) { if (error) *error = "Choose a unique layout name."; return false; }
    }
    if (existing < 0 && layouts_.size() >= 65) { if (error) *error = "At most 64 custom stage layouts can be saved."; return false; }
    if (existing < 0) layouts_.append(layout); else layouts_[existing] = layout;
    persistLayouts();
    for (auto &[id, route] : outputs_) if (isStageOutput(id) && route.configuration.stageLayout == layout.id) route.window->setStageLayout(layout);
    emit layoutsChanged(); return true;
}
bool OutputManager::deleteStageLayout(const QString &id, QString *error) {
    if (error) error->clear();
    if (id == "standard") { if (error) *error = "The standard layout cannot be removed."; return false; }
    for (const auto &[outputId, route] : outputs_) if (isStageOutput(outputId) && route.configuration.stageLayout == id) {
        if (error) *error = "This layout is assigned to a stage screen. Assign another layout before removing it."; return false;
    }
    for (int i = 0; i < layouts_.size(); ++i) if (layouts_.at(i).id == id) { layouts_.removeAt(i); persistLayouts(); emit layoutsChanged(); return true; }
    if (error) *error = "This stage layout no longer exists."; return false;
}
void OutputManager::shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    for (auto &[id, route] : outputs_) {
        route.window->hide();
        route.activeScreen.clear();
        QString error;
        if (!releaseMode(route.modeLease, &error)) qWarning() << "Could not restore desktop mode:" << error;
    }
}
bool OutputManager::eventFilter(QObject *watched, QEvent *event) {
    // Only managed output windows install this filter.
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange)
        scheduleStatusRefresh();
    return QObject::eventFilter(watched, event);
}
void OutputManager::scheduleStatusRefresh() {
    if (statusPending_) return; statusPending_ = true;
    QTimer::singleShot(0, this, [this] { statusPending_ = false; emit statusChanged(); });
}
void OutputManager::watchScreen(QScreen *screen) {
    if (watchedScreens_.contains(screen)) return;
    watchedScreens_.insert(screen);
    connect(screen, &QScreen::geometryChanged, this, [this] { scheduleRefresh(); });
    connect(screen, &QScreen::refreshRateChanged, this, [this] { scheduleRefresh(); });
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, [this] { scheduleRefresh(); });
    connect(screen, &QObject::destroyed, this, [this, screen] { watchedScreens_.remove(screen); });
}
void OutputManager::scheduleRefresh() {
    if (refreshPending_ || shuttingDown_) return; refreshPending_ = true;
    QTimer::singleShot(0, this, [this] { refreshPending_ = false; for (auto id : outputIds()) applyTarget(id); emit targetsChanged(); emit statusChanged(); });
}

void OutputManager::setLiveAuthorizationCheck(std::function<bool()> check) {
    authorizationCheck_ = std::move(check); refreshAuthorization();
}
void OutputManager::refreshAuthorization() {
    for (auto id : outputIds()) applyTarget(id);
    emit statusChanged();
}
