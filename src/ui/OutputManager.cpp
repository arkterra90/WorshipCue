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
    : QObject(parent), audience_(DisplayWindow::Kind::Audience, controller),
      audience2_(DisplayWindow::Kind::Audience, controller, nullptr, &audience_),
      stage_(DisplayWindow::Kind::Stage, controller), stage2_(DisplayWindow::Kind::Stage, controller),
      modes_(modes ? std::move(modes) : nativeDisplayModeProvider()) {
    for (const auto id : outputIds) {
        auto &window = output(id);
        window.setWindowTitle(QStringLiteral("WorshipCue — %1").arg(outputName(id)));
        window.installEventFilter(this);
        connect(&window, &DisplayWindow::operatorFocusRequested, this, &OutputManager::operatorFocusRequested);
        connect(&window, &DisplayWindow::closedByOperator, this, [this, id] {
            QString error;
            if (setTarget(id, "off", &error)) return;
            // A backend restore failure must never reopen a window the operator
            // explicitly closed. Retain its lease for a later restoration retry.
            auto &config = configurations_[int(id)]; config.target = "off"; config.signalMode.clear();
            errors_[int(id)] = "desktop mode restore failed: " + error;
            activeScreens_[int(id)].clear(); output(id).setConfiguration(config); persist(id);
            emit targetsChanged(); emit statusChanged();
        });
    }
    for (auto *screen : QGuiApplication::screens()) watchScreen(screen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) { watchScreen(screen); scheduleRefresh(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        for (auto id : outputIds) if (activeScreens_[int(id)] == screen || target(id) == screenId(screen)) {
            output(id).hide(); activeScreens_[int(id)].clear();
            // A disconnected display cannot be restored. Its desired route/mode
            // remains saved and is revalidated when a matching display returns.
            modeLeases_[int(id)] = {};
        }
        watchedScreens_.remove(screen); disconnect(screen, nullptr, this, nullptr); scheduleRefresh();
    });
}
OutputManager::~OutputManager() { shutdown(); for (auto id : outputIds) output(id).removeEventFilter(this); }
DisplayWindow &OutputManager::output(OutputId id) {
    switch (id) {
    case OutputId::Audience1: return audience_;
    case OutputId::Audience2: return audience2_;
    case OutputId::Stage1: return stage_;
    case OutputId::Stage2: return stage2_;
    }
    return audience_;
}
const DisplayWindow &OutputManager::output(OutputId id) const { return const_cast<OutputManager *>(this)->output(id); }
bool OutputManager::ownsWindow(const QWidget *window) const {
    for (auto id : outputIds) if (&output(id) == window) return true;
    return false;
}
QString OutputManager::screenId(const QScreen *screen) {
    return QStringLiteral("screen:%1|%2|%3|%4").arg(screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber());
}
QScreen *OutputManager::findScreen(const QString &id) {
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
    for (const auto &configuration : configurations_) if (!included.contains(configuration.target)) {
        auto *screen = findScreen(configuration.target);
        result.append({configuration.target, screen ? QStringLiteral("Reconnected: %1").arg(screen->name())
            : QStringLiteral("Disconnected or ambiguous: %1").arg(configuration.target.mid(7).section('|', 0, 0))});
        included.insert(configuration.target);
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
    auto &lease = modeLeases_[int(id)];
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
    if (error) error->clear();
    if (!config.validate(error)) return false;
    auto *screen = findScreen(config.target);
    if (isDisplayTarget(config.target)) {
        for (const auto other : outputIds) if (other != id && isDisplayTarget(target(other))
            && (target(other) == config.target || (screen && findScreen(target(other)) == screen))) {
            if (error) *error = "Each fullscreen output must use a different physical display."; return false;
        }
        if (!screen && config.target != target(id)) {
            if (error) *error = "That display is unavailable or has an ambiguous identity. Choose a connected display or Windowed Preview."; return false;
        }
    }
    if (isStageOutput(id) && stageLayout(config.stageLayout).id != config.stageLayout) {
        if (error) *error = "Choose an existing stage layout."; return false;
    }
    if (!applySignal(id, config, error)) return false;
    configurations_[int(id)] = config; errors_[int(id)].clear(); shuttingDown_ = false;
    output(id).setConfiguration(config);
    if (isStageOutput(id)) output(id).setStageLayout(stageLayout(config.stageLayout));
    persist(id); applyTarget(id); emit targetsChanged(); emit statusChanged(); return true;
}
void OutputManager::persist(OutputId id) {
    QSettings settings;
    settings.setValue(settingsKey(id), QJsonDocument(configuration(id).toJson()).toJson(QJsonDocument::Compact));
    // Retain compatibility with the first-pair route preferences.
    if (id == OutputId::Audience1) settings.setValue("outputs/audienceTarget", target(id));
    if (id == OutputId::Stage1) settings.setValue("outputs/stageTarget", target(id));
    settings.sync();
    if (settings.status() != QSettings::NoError) qWarning() << "Could not save output configuration" << settings.fileName();
}
void OutputManager::restoreTargets() {
    shuttingDown_ = false;
    QSettings settings;
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
    QSet<QString> assigned;
    QSet<QScreen *> assignedScreens;
    for (const auto id : outputIds) {
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
        auto *screen = findScreen(config.target);
        if (isDisplayTarget(config.target) && (assigned.contains(config.target) || (screen && assignedScreens.contains(screen)))) {
            qWarning() << "Ignoring conflicting fullscreen assignment" << outputName(id); config.target = "off"; config.signalMode.clear();
        }
        if (stageLayout(config.stageLayout).id != config.stageLayout) config.stageLayout = "standard";
        configurations_[int(id)] = config;
        if (isDisplayTarget(config.target)) { assigned.insert(config.target); if (screen) assignedScreens.insert(screen); }
        output(id).setConfiguration(config);
        if (isStageOutput(id)) output(id).setStageLayout(stageLayout(config.stageLayout));
        persist(id); applyTarget(id);
    }
    emit layoutsChanged(); emit targetsChanged(); emit statusChanged();
}
void OutputManager::applyTarget(OutputId id) {
    if (shuttingDown_) return;
    auto &window = output(id); const auto config = configuration(id); auto &active = activeScreens_[int(id)];
    if (config.target == "off") { window.hide(); active.clear(); return; }
    if (config.target == "preview") { active.clear(); if (!window.isVisible() || window.isFullScreen()) window.showPreview(); return; }
    auto *screen = findScreen(config.target);
    if (!screen) { window.hide(); active.clear(); return; }
    QString error;
    if (!applySignal(id, config, &error)) { errors_[int(id)] = error; window.hide(); active.clear(); return; }
    errors_[int(id)].clear();
    if (active == screen && window.isVisible() && window.isFullScreen()) {
        if (window.geometry() != screen->geometry()) window.setGeometry(screen->geometry());
        return;
    }
    active = screen; window.showFullscreenOn(screen);
}
QString OutputManager::status(OutputId id) const {
    if (!errors_[int(id)].isEmpty()) return (target(id) == "off" ? QStringLiteral("Off — %1") : QStringLiteral("Mode unavailable: %1")).arg(errors_[int(id)]);
    const auto desired = target(id);
    if (desired == "off") return "Off";
    const auto *screen = isDisplayTarget(desired) ? findScreen(desired) : nullptr;
    if (isDisplayTarget(desired) && !screen) return "Disconnected";
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
    for (auto id : outputIds) if (isStageOutput(id) && configuration(id).stageLayout == layout.id) output(id).setStageLayout(layout);
    emit layoutsChanged(); return true;
}
bool OutputManager::deleteStageLayout(const QString &id, QString *error) {
    if (error) error->clear();
    if (id == "standard") { if (error) *error = "The standard layout cannot be removed."; return false; }
    for (auto outputId : {OutputId::Stage1, OutputId::Stage2}) if (configuration(outputId).stageLayout == id) {
        if (error) *error = "This layout is assigned to a stage screen. Assign another layout before removing it."; return false;
    }
    for (int i = 0; i < layouts_.size(); ++i) if (layouts_.at(i).id == id) { layouts_.removeAt(i); persistLayouts(); emit layoutsChanged(); return true; }
    if (error) *error = "This stage layout no longer exists."; return false;
}
void OutputManager::shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    for (auto id : outputIds) {
        output(id).hide(); activeScreens_[int(id)].clear(); QString error;
        if (!releaseMode(modeLeases_[int(id)], &error)) qWarning() << "Could not restore desktop mode:" << error;
    }
}
bool OutputManager::eventFilter(QObject *watched, QEvent *event) {
    for (auto id : outputIds) if (watched == &output(id) && (event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange)) scheduleStatusRefresh();
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
    QTimer::singleShot(0, this, [this] { refreshPending_ = false; for (auto id : outputIds) applyTarget(id); emit targetsChanged(); emit statusChanged(); });
}
