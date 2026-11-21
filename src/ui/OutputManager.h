#pragma once
#include "DisplayWindow.h"
#include "NativeDisplayModes.h"
#include <QObject>
#include <QPointer>
#include <QSet>
#include <map>
#include <optional>

class QScreen;
class QEvent;
struct OutputTarget { QString id; QString label; };

class OutputManager : public QObject {
    Q_OBJECT
public:
    explicit OutputManager(PresentationController *controller, QObject *parent = nullptr,
                           std::shared_ptr<DisplayModeProvider> modes = {});
    ~OutputManager() override;
    DisplayWindow &audience() { return output(OutputId::Audience1); }
    DisplayWindow &stage() { return output(OutputId::Stage1); }
    const DisplayWindow &audience() const { return output(OutputId::Audience1); }
    const DisplayWindow &stage() const { return output(OutputId::Stage1); }
    DisplayWindow &output(OutputId id);
    const DisplayWindow &output(OutputId id) const;
    QVector<OutputId> outputIds() const;
    OutputId addOutput(DisplayWindow::Kind kind);
    QVector<OutputTarget> availableTargets() const;
    OutputConfiguration configuration(OutputId id) const { return outputs_.at(id).configuration; }
    QString target(OutputId id) const { return configuration(id).target; }
    bool setTarget(OutputId id, const QString &target, QString *error = nullptr);
    bool setConfiguration(OutputId id, const OutputConfiguration &configuration, QString *error = nullptr);
    bool beginModeTrial(OutputId id, const OutputConfiguration &configuration, QString *error = nullptr);
    bool finishModeTrial(bool keep, QString *error = nullptr);
    QString status(OutputId id) const;
    DisplayCapabilities capabilities(const QString &target) const;
    QVector<StageLayout> stageLayouts() const { return layouts_; }
    StageLayout stageLayout(const QString &id) const;
    bool saveStageLayout(StageLayout layout, QString *error = nullptr);
    bool deleteStageLayout(const QString &id, QString *error = nullptr);
    bool configureStageTimer(OutputId, const StageTimerConfiguration &, QString *error = nullptr);
    bool showStageTimer(OutputId, bool visible, QString *error = nullptr);
    bool ownsWindow(const QWidget *window) const;
    void setLiveAuthorizationCheck(std::function<bool()> check);
    void refreshAuthorization();
    bool liveAuthorized() const { return authorizationCheck_ && authorizationCheck_(); }
    void restoreTargets();
    void shutdown();
    // Compatibility for existing operator shortcuts and the first pair of outputs.
    QString target(DisplayWindow::Kind kind) const { return target(first(kind)); }
    bool setTarget(DisplayWindow::Kind kind, const QString &id, QString *error = nullptr) { return setTarget(first(kind), id, error); }
    QString status(DisplayWindow::Kind kind) const { return status(first(kind)); }
    static QString screenId(const QScreen *screen);

signals:
    void outputsChanged();
    void targetsChanged();
    void statusChanged();
    void layoutsChanged();
    void operatorFocusRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    std::function<bool()> authorizationCheck_;
    PresentationController *controller_;
    struct ModeLease { QPointer<QScreen> screen; QString original; QString applied; };
    struct Output {
        std::unique_ptr<DisplayWindow> window;
        OutputConfiguration configuration;
        QPointer<QScreen> activeScreen;
        ModeLease modeLease;
        QString error;
    };
    std::map<OutputId, Output> outputs_;
    struct ModeTrial { OutputId id; OutputConfiguration previous; QPointer<QScreen> screen; };
    std::optional<ModeTrial> modeTrial_;
    std::shared_ptr<DisplayModeProvider> modes_;
    QVector<StageLayout> layouts_{StageLayout::standard()};
    QSet<QScreen *> watchedScreens_;
    bool refreshPending_ = false;
    bool statusPending_ = false;
    bool shuttingDown_ = false;

    static OutputId first(DisplayWindow::Kind kind) { return kind == DisplayWindow::Kind::Audience ? OutputId::Audience1 : OutputId::Stage1; }
    static QScreen *findScreen(const QString &id);
    static QString settingsKey(OutputId id);
    static bool isDisplayTarget(const QString &id);
    bool hasFullscreenConflict(OutputId id, const QString &target, const QScreen *screen) const;
    void createOutput(OutputId id);
    bool applyConfiguration(OutputId id, const OutputConfiguration &configuration, bool save, QString *error);
    void applyTarget(OutputId id);
    bool applySignal(OutputId id, const OutputConfiguration &configuration, QString *error);
    bool releaseMode(ModeLease &lease, QString *error = nullptr);
    bool persist(OutputId id);
    void persistLayouts();
    void persistStageTimer(OutputId);
    void watchScreen(QScreen *screen);
    void scheduleRefresh();
    void scheduleStatusRefresh();
};
