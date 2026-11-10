#include "OutputManager.h"

#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QTimer>

OutputManager::OutputManager(PresentationController *controller, QObject *parent)
    : QObject(parent), audience_(DisplayWindow::Kind::Audience, controller),
      stage_(DisplayWindow::Kind::Stage, controller) {
    for (const auto kind : {DisplayWindow::Kind::Audience, DisplayWindow::Kind::Stage}) {
        auto &output = window(kind);
        output.installEventFilter(this);
        connect(&output, &DisplayWindow::operatorFocusRequested,
                this, &OutputManager::operatorFocusRequested);
        connect(&output, &DisplayWindow::closedByOperator, this, [this, kind] {
            setTarget(kind, QStringLiteral("off"));
        });
    }
    for (auto *screen : QGuiApplication::screens()) watchScreen(screen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
        watchScreen(screen);
        scheduleRefresh();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        // Qt may move a disconnected fullscreen window to the operator display.
        // Hide it immediately, while retaining the desired display identity.
        for (const auto kind : {DisplayWindow::Kind::Audience, DisplayWindow::Kind::Stage}) {
            if (activeScreen(kind) == screen || target(kind) == screenId(screen)) {
                window(kind).hide();
                activeScreen(kind).clear();
            }
        }
        watchedScreens_.remove(screen);
        disconnect(screen, nullptr, this, nullptr);
        scheduleRefresh();
    });
}

OutputManager::~OutputManager() {
    audience_.removeEventFilter(this);
    stage_.removeEventFilter(this);
}

DisplayWindow &OutputManager::window(DisplayWindow::Kind kind) {
    return kind == DisplayWindow::Kind::Audience ? audience_ : stage_;
}

const DisplayWindow &OutputManager::window(DisplayWindow::Kind kind) const {
    return kind == DisplayWindow::Kind::Audience ? audience_ : stage_;
}

QString &OutputManager::desiredTarget(DisplayWindow::Kind kind) {
    return kind == DisplayWindow::Kind::Audience ? audienceTarget_ : stageTarget_;
}

QPointer<QScreen> &OutputManager::activeScreen(DisplayWindow::Kind kind) {
    return kind == DisplayWindow::Kind::Audience ? audienceScreen_ : stageScreen_;
}

QString OutputManager::target(DisplayWindow::Kind kind) const {
    return kind == DisplayWindow::Kind::Audience ? audienceTarget_ : stageTarget_;
}

QString OutputManager::screenId(const QScreen *screen) {
    return QStringLiteral("screen:%1|%2|%3|%4")
        .arg(screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber());
}

QScreen *OutputManager::findScreen(const QString &id) {
    for (auto *screen : QGuiApplication::screens())
        if (screenId(screen) == id) return screen;
    return nullptr;
}

QString OutputManager::settingsKey(DisplayWindow::Kind kind) {
    return kind == DisplayWindow::Kind::Audience
        ? QStringLiteral("outputs/audienceTarget") : QStringLiteral("outputs/stageTarget");
}

bool OutputManager::isDisplayTarget(const QString &id) {
    return id.startsWith(QStringLiteral("screen:"));
}

QVector<OutputTarget> OutputManager::availableTargets() const {
    QVector<OutputTarget> result = {
        {QStringLiteral("off"), QStringLiteral("Off")},
        {QStringLiteral("preview"), QStringLiteral("Windowed Preview")}
    };
    QSet<QString> included = {QStringLiteral("off"), QStringLiteral("preview")};
    const auto screens = QGuiApplication::screens();
    for (int index = 0; index < screens.size(); ++index) {
        const auto *screen = screens.at(index);
        const QString id = screenId(screen);
        // Some projectors expose identical identities. Until they can be
        // distinguished safely, offer one route rather than ambiguous routes.
        if (included.contains(id)) continue;
        result.append({id, QStringLiteral("%1: %2 (%3×%4)")
            .arg(index + 1).arg(screen->name())
            .arg(screen->geometry().width()).arg(screen->geometry().height())});
        included.insert(id);
    }
    for (const QString &desired : {audienceTarget_, stageTarget_}) {
        if (!included.contains(desired)) {
            const QString name = desired.mid(7).section(QLatin1Char('|'), 0, 0);
            result.append({desired, QStringLiteral("Disconnected: %1")
                .arg(name.isEmpty() ? QStringLiteral("Display") : name)});
            included.insert(desired);
        }
    }
    return result;
}

bool OutputManager::setTarget(DisplayWindow::Kind kind, const QString &id, QString *error) {
    if (error) error->clear();
    const auto other = kind == DisplayWindow::Kind::Audience
        ? DisplayWindow::Kind::Stage : DisplayWindow::Kind::Audience;
    if (isDisplayTarget(id) && id == target(other)) {
        if (error) *error = QStringLiteral("Audience and stage must use different fullscreen displays.");
        return false;
    }
    if (id != QStringLiteral("off") && id != QStringLiteral("preview")
        && (!isDisplayTarget(id) || (!findScreen(id) && id != target(kind)))) {
        if (error) *error = QStringLiteral("That output display is unavailable. Choose a connected display or Windowed Preview.");
        return false;
    }
    const bool changed = target(kind) != id;
    desiredTarget(kind) = id;
    if (changed) persistTarget(kind);
    applyTarget(kind);
    if (changed) emit targetsChanged();
    emit statusChanged();
    return true;
}

void OutputManager::persistTarget(DisplayWindow::Kind kind) {
    QSettings settings;
    settings.setValue(settingsKey(kind), target(kind));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        qWarning() << "Could not save output routing preferences" << settings.fileName();
}

void OutputManager::restoreTargets() {
    const QSettings settings;
    for (const auto kind : {DisplayWindow::Kind::Audience, DisplayWindow::Kind::Stage}) {
        const QString saved = settings.value(settingsKey(kind), QStringLiteral("off")).toString();
        desiredTarget(kind) = saved == QStringLiteral("off") || saved == QStringLiteral("preview")
            || isDisplayTarget(saved) ? saved : QStringLiteral("off");
    }
    if (isDisplayTarget(audienceTarget_) && audienceTarget_ == stageTarget_) {
        qWarning() << "Ignoring conflicting stage fullscreen assignment";
        stageTarget_ = QStringLiteral("off");
        persistTarget(DisplayWindow::Kind::Stage);
    }
    applyTarget(DisplayWindow::Kind::Audience);
    applyTarget(DisplayWindow::Kind::Stage);
    emit targetsChanged();
    emit statusChanged();
}

void OutputManager::applyTarget(DisplayWindow::Kind kind) {
    auto &output = window(kind);
    const QString desired = target(kind);
    if (desired == QStringLiteral("off")) {
        output.hide();
        activeScreen(kind).clear();
        return;
    }
    if (desired == QStringLiteral("preview")) {
        activeScreen(kind).clear();
        if (!output.isVisible() || output.isFullScreen()) output.showPreview();
        return;
    }
    auto *screen = findScreen(desired);
    if (!screen) {
        output.hide();
        activeScreen(kind).clear();
        return;
    }
    // An unrelated monitor connection must not cycle a healthy fullscreen
    // window through normal mode (which is disruptive with macOS Spaces).
    if (activeScreen(kind) == screen && output.isVisible() && output.isFullScreen()) return;
    activeScreen(kind) = screen;
    output.showFullscreenOn(screen);
}

QString OutputManager::status(DisplayWindow::Kind kind) const {
    const QString desired = target(kind);
    if (desired == QStringLiteral("off")) return QStringLiteral("Off");
    const auto *screen = isDisplayTarget(desired) ? findScreen(desired) : nullptr;
    if (isDisplayTarget(desired) && !screen) return QStringLiteral("Disconnected");
    const auto &output = window(kind);
    if (!output.isVisible()) return QStringLiteral("Off");
    if (output.isMinimized()) return QStringLiteral("Minimized");
    if (desired == QStringLiteral("preview") || !output.isFullScreen())
        return QStringLiteral("Windowed preview");
    return QStringLiteral("Fullscreen — %1").arg(screen->name());
}

bool OutputManager::eventFilter(QObject *watched, QEvent *event) {
    if ((watched == &audience_ || watched == &stage_)
        && (event->type() == QEvent::Show || event->type() == QEvent::Hide
            || event->type() == QEvent::WindowStateChange)) {
        // Observe native visibility changes without reapplying the desired
        // route: a status update must never restore a minimized/hidden output.
        scheduleStatusRefresh();
    }
    return QObject::eventFilter(watched, event);
}

void OutputManager::scheduleStatusRefresh() {
    if (statusPending_) return;
    statusPending_ = true;
    QTimer::singleShot(0, this, [this] {
        statusPending_ = false;
        emit statusChanged();
    });
}

void OutputManager::watchScreen(QScreen *screen) {
    if (watchedScreens_.contains(screen)) return;
    watchedScreens_.insert(screen);
    connect(screen, &QScreen::geometryChanged, this, [this] { scheduleRefresh(); });
    connect(screen, &QObject::destroyed, this, [this, screen] { watchedScreens_.remove(screen); });
}

void OutputManager::scheduleRefresh() {
    if (refreshPending_) return;
    refreshPending_ = true;
    QTimer::singleShot(0, this, [this] {
        refreshPending_ = false;
        applyTarget(DisplayWindow::Kind::Audience);
        applyTarget(DisplayWindow::Kind::Stage);
        emit targetsChanged();
        emit statusChanged();
    });
}
