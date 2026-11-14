#pragma once
#include "DisplayWindow.h"
#include "NativeDisplayModes.h"
#include <QObject>
#include <QPointer>
#include <QSet>
#include <array>

class QScreen;
class QEvent;
struct OutputTarget { QString id; QString label; };

class OutputManager : public QObject {
    Q_OBJECT
public:
    explicit OutputManager(PresentationController *controller, QObject *parent = nullptr,
                           std::shared_ptr<DisplayModeProvider> modes = {});
    ~OutputManager() override;
    DisplayWindow &audience() { return audience_; }
    DisplayWindow &stage() { return stage_; }
    const DisplayWindow &audience() const { return audience_; }
    const DisplayWindow &stage() const { return stage_; }
    DisplayWindow &output(OutputId id);
    const DisplayWindow &output(OutputId id) const;
    QVector<OutputTarget> availableTargets() const;
    OutputConfiguration configuration(OutputId id) const { return configurations_.at(int(id)); }
    QString target(OutputId id) const { return configuration(id).target; }
    bool setTarget(OutputId id, const QString &target, QString *error = nullptr);
    bool setConfiguration(OutputId id, const OutputConfiguration &configuration, QString *error = nullptr);
    QString status(OutputId id) const;
    DisplayCapabilities capabilities(const QString &target) const;
    QVector<StageLayout> stageLayouts() const { return layouts_; }
    StageLayout stageLayout(const QString &id) const;
    bool saveStageLayout(StageLayout layout, QString *error = nullptr);
    bool deleteStageLayout(const QString &id, QString *error = nullptr);
    bool ownsWindow(const QWidget *window) const;
    void restoreTargets();
    void shutdown();
    // Compatibility for existing operator shortcuts and the first pair of outputs.
    QString target(DisplayWindow::Kind kind) const { return target(first(kind)); }
    bool setTarget(DisplayWindow::Kind kind, const QString &id, QString *error = nullptr) { return setTarget(first(kind), id, error); }
    QString status(DisplayWindow::Kind kind) const { return status(first(kind)); }
    static QString screenId(const QScreen *screen);

signals:
    void targetsChanged();
    void statusChanged();
    void layoutsChanged();
    void operatorFocusRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    DisplayWindow audience_;
    DisplayWindow audience2_;
    DisplayWindow stage_;
    DisplayWindow stage2_;
    std::array<OutputConfiguration, 4> configurations_;
    std::array<QPointer<QScreen>, 4> activeScreens_;
    struct ModeLease { QPointer<QScreen> screen; QString original; QString applied; };
    std::array<ModeLease, 4> modeLeases_;
    std::array<QString, 4> errors_;
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
    void applyTarget(OutputId id);
    bool applySignal(OutputId id, const OutputConfiguration &configuration, QString *error);
    bool releaseMode(ModeLease &lease, QString *error = nullptr);
    void persist(OutputId id);
    void persistLayouts();
    void watchScreen(QScreen *screen);
    void scheduleRefresh();
    void scheduleStatusRefresh();
};
