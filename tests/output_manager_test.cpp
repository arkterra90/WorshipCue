#include "../src/ui/OutputManager.h"
#include "../src/ui/OutputSettingsDialog.h"
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include <QCoreApplication>
#include <QScreen>
#include <QScopeGuard>
#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class FakeDisplayModes final : public DisplayModeProvider {
public:
    mutable QString current = "desktop";
    bool supported = true;
    bool available = true;
    bool fail = false;
    QStringList applied;
    DisplayCapabilities capabilities(QScreen *) const override {
        DisplayCapabilities caps;
        caps.currentMode = current; caps.canSetMode = supported;
        caps.modes = {{"desktop", {1920,1080}, {1920,1080}, 60, 0}};
        if (available) caps.modes.append({"interlaced", {1920,1080}, {1920,1080}, 59.94, 1});
        return caps;
    }
    bool apply(QScreen *, const QString &mode, QString *error) override {
        if (fail) { if (error) *error = "Backend rejected mode"; return false; }
        applied.append(mode); current = mode; return true;
    }
};

#include "fixtures/LicenseFixture.h"
class LicensedOutputManager : public OutputManager {
    std::unique_ptr<LicenseManager> license_ = fixtureLicense();
public:
    explicit LicensedOutputManager(PresentationController *controller, QObject *parent=nullptr,
                                  std::shared_ptr<DisplayModeProvider> modes={})
        : OutputManager(controller,parent,std::move(modes)) {
        setLiveAuthorizationCheck([this] { return license_->liveAllowed(); });
    }
    ~LicensedOutputManager() override { shutdown(); setLiveAuthorizationCheck({}); }
};

class OutputManagerTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir settingsDirectory_;

private slots:
    void initTestCase() {
        QVERIFY(settingsDirectory_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("WorshipCueTests"));
        QCoreApplication::setApplicationName(QStringLiteral("OutputManager"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
    }

    void init() { QSettings().clear(); }

    void noAuthorityNeverRestoresOrPaintsLiveOutputs() {
        PresentationController controller;
        controller.takeLive("Unit renderer content", {"Must not be projected"});
        OutputManager denied(&controller);
        QString error;
        for (auto id : defaultOutputIds) {
            QVERIFY(denied.setTarget(id, "preview", &error));
            QVERIFY(!denied.output(id).isVisible());
            denied.output(id).showPreview();
            QVERIFY(!denied.output(id).isVisible());
            auto image = denied.output(id).grab().toImage();
            QCOMPARE(image.pixelColor(image.width()/2, image.height()/2), QColor(Qt::black));
        }
        denied.restoreTargets();
        for (auto id : defaultOutputIds) QVERIFY(!denied.output(id).isVisible());
    }
    void stageTimerSettingsAndVisibilitySurviveRestartWithoutStartingPlayback() {
        PresentationController controller;
        controller.takeLive("Sermon", {"Point one"});
        const auto revision = controller.state().cueRevision;
        StageTimerConfiguration first;
        first.name = "Sermon budget"; first.durationSeconds = 1800;
        StageTimerConfiguration second;
        second.name = "Service start";
        second.mode = StageTimerConfiguration::Mode::CountdownToTime;
        second.target = QDateTime::currentDateTimeUtc().addSecs(3600);
        second.allowOverrun = true;
        {
            LicensedOutputManager outputs(&controller);
            outputs.restoreTargets();
            QVERIFY(outputs.configureStageTimer(OutputId::Stage1, first));
            QVERIFY(outputs.configureStageTimer(OutputId::Stage2, second));
            QVERIFY(outputs.showStageTimer(OutputId::Stage1, true));
            QVERIFY(outputs.showStageTimer(OutputId::Stage2, false));
            outputs.stage().startCountdown();
            QVERIFY(outputs.stage().stageRuntime().running);
            QVERIFY(!outputs.configureStageTimer(OutputId::Audience1, first));
            QVERIFY(!outputs.showStageTimer(OutputId::Audience2, true));
        }
        LicensedOutputManager reopened(&controller);
        reopened.restoreTargets();
        QCOMPARE(reopened.stage().stageTimerConfiguration().toJson(), first.toJson());
        QCOMPARE(reopened.output(OutputId::Stage2).stageTimerConfiguration().toJson(), second.toJson());
        QVERIFY(reopened.stage().stageTimerVisible());
        QVERIFY(!reopened.output(OutputId::Stage2).stageTimerVisible());
        QCOMPARE(reopened.stage().stageTimerReading().state, StageTimerReading::State::Ready);
        QCOMPARE(controller.state().cueRevision, revision);
    }

    void fourOutputsRouteIndependentlyAndShareAudienceCompositor() {
        PresentationController controller;
        {
            LicensedOutputManager outputs(&controller);
            outputs.restoreTargets();
            for (auto id : defaultOutputIds) {
                QVERIFY(outputs.setTarget(id, "preview"));
                QVERIFY(outputs.output(id).isVisible());
                QCOMPARE(outputs.status(id), QStringLiteral("Windowed preview"));
            }
            QVERIFY(outputs.output(OutputId::Audience2).sharesAudienceWith(&outputs.audience()));
            controller.takeLive("Four outputs", {"Current lyrics", "Next lyrics"});
            const auto revision = controller.state().cueRevision;
            outputs.output(OutputId::Audience2).close();
            QCOMPARE(outputs.target(OutputId::Audience2), QStringLiteral("off"));
            for (auto id : {OutputId::Audience1, OutputId::Stage1, OutputId::Stage2}) QVERIFY(outputs.output(id).isVisible());
            QCOMPARE(controller.state().cueRevision, revision);
        }
        LicensedOutputManager restored(&controller);
        restored.restoreTargets();
        QCOMPARE(restored.target(OutputId::Audience2), QStringLiteral("off"));
        for (auto id : {OutputId::Audience1, OutputId::Stage1, OutputId::Stage2}) QCOMPARE(restored.target(id), QStringLiteral("preview"));
        restored.shutdown();
        for (auto id : defaultOutputIds) QVERIFY(!restored.output(id).isVisible());
    }

    void fullscreenConflictsAreCheckedAcrossAdditionalRoutes() {
        PresentationController controller;
        LicensedOutputManager outputs(&controller);
        const auto audience = outputs.addOutput(DisplayWindow::Kind::Audience);
        const auto stage = outputs.addOutput(DisplayWindow::Kind::Stage);
        QString display;
        for (const auto &target : outputs.availableTargets()) if (target.id.startsWith("screen:")) { display = target.id; break; }
        if (display.isEmpty()) QSKIP("No display reported by this backend.");
        QVERIFY(outputs.setTarget(stage, display));
        for (auto id : outputs.outputIds()) {
            if (id == stage) continue;
            QString error;
            QVERIFY(!outputs.setTarget(id, display, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(outputs.target(id), QStringLiteral("off"));
        }
        QVERIFY(outputs.setTarget(stage, "off"));
        QVERIFY(outputs.setTarget(audience, display));
        QVERIFY(outputs.output(audience).isFullScreen());
    }

    void additionalRoutesRestoreWithoutReopeningClosedOutputs() {
        PresentationController controller;
        OutputId extraAudience;
        OutputId extraStage;
        OutputConfiguration stageFormat;
        stageFormat.rotation = 90;
        stageFormat.target = "preview";
        StageTimerConfiguration timer;
        timer.name = "Additional stage timer";
        {
            LicensedOutputManager outputs(&controller);
            extraStage = outputs.addOutput(DisplayWindow::Kind::Stage);
            const auto occupiedStage = outputs.addOutput(DisplayWindow::Kind::Stage);
            extraAudience = outputs.addOutput(DisplayWindow::Kind::Audience);
            QCOMPARE(outputName(extraStage), QStringLiteral("Stage Screen 3"));
            QCOMPARE(outputName(occupiedStage), QStringLiteral("Stage Screen 4"));
            QCOMPARE(outputName(extraAudience), QStringLiteral("Audience Screen 3"));
            QVERIFY(outputs.output(extraAudience).sharesAudienceWith(&outputs.audience()));
            QVERIFY(outputs.setConfiguration(extraStage, stageFormat));
            QVERIFY(outputs.configureStageTimer(extraStage, timer));
            QVERIFY(outputs.setTarget(extraAudience, "preview"));
            outputs.output(extraAudience).close();
            QVERIFY(outputs.output(extraStage).isVisible());
        }
        LicensedOutputManager restored(&controller);
        restored.restoreTargets();
        QCOMPARE(restored.outputIds().size(), 7);
        QCOMPARE(restored.configuration(extraStage).toJson(), stageFormat.toJson());
        QCOMPARE(restored.output(extraStage).stageTimerConfiguration().toJson(), timer.toJson());
        QCOMPARE(restored.target(extraAudience), QStringLiteral("off"));
        QVERIFY(!restored.output(extraAudience).isVisible());
        restored.shutdown();
        QVERIFY(!restored.output(extraStage).isVisible());
    }

    void conflictingSavedRoutesStayHiddenUntilReassigned() {
        if (QGuiApplication::screens().isEmpty()) QSKIP("No screen reported.");
        OutputConfiguration configuration;
        configuration.target = OutputManager::screenId(QGuiApplication::screens().first());
        PresentationController controller;
        LicensedOutputManager outputs(&controller);
        const auto stage = outputs.addOutput(DisplayWindow::Kind::Stage);
        const auto saved = QJsonDocument(configuration.toJson()).toJson();
        QSettings settings;
        settings.setValue("outputs/0/configuration", saved);
        settings.setValue(QStringLiteral("outputs/%1/configuration").arg(int(stage)), saved);
        settings.sync();

        outputs.restoreTargets();
        QCOMPARE(outputs.status(OutputId::Audience1), QStringLiteral("Display assignment conflict"));
        QCOMPARE(outputs.status(stage), QStringLiteral("Display assignment conflict"));
        QVERIFY(!outputs.audience().isVisible());
        QVERIFY(!outputs.output(stage).isVisible());
        QCOMPARE(outputs.target(stage), configuration.target);
        QVERIFY(outputs.setTarget(OutputId::Audience1, "off"));
        QTRY_VERIFY(outputs.output(stage).isFullScreen());
        QVERIFY(outputs.status(stage).startsWith("Fullscreen"));
    }

    void formatsPersistAndInvalidChangesLeaveTheOutputUntouched() {
        PresentationController controller;
        OutputConfiguration format;
        format.target = "preview"; format.canvas = {1080,1920}; format.aspectWidth = 9; format.aspectHeight = 16;
        format.rotation = 90; format.scaling = OutputConfiguration::Scaling::Fill;
        {
            LicensedOutputManager outputs(&controller);
            controller.takeLive("Live", {"Unchanged"});
            const auto revision = controller.state().cueRevision;
            QVERIFY(outputs.setConfiguration(OutputId::Audience2, format));
            QCOMPARE(outputs.configuration(OutputId::Audience2).toJson(), format.toJson());
            auto invalid = format; invalid.canvas = {8192,8192}; QString error;
            QVERIFY(!outputs.setConfiguration(OutputId::Audience2, invalid, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(outputs.configuration(OutputId::Audience2).toJson(), format.toJson());
            QCOMPARE(controller.state().cueRevision, revision);
            QCOMPARE(outputs.configuration(OutputId::Audience1).canvas, QSize(1920,1080));
        }
        LicensedOutputManager reopened(&controller); reopened.restoreTargets();
        QCOMPARE(reopened.configuration(OutputId::Audience2).toJson(), format.toJson());
    }

    void nativeModeValidationRollbackAndDesktopRestorationUseTheBackend() {
        if (QGuiApplication::screens().isEmpty()) QSKIP("No screen reported.");
        auto modes = std::make_shared<FakeDisplayModes>();
        PresentationController controller;
        LicensedOutputManager outputs(&controller, nullptr, modes);
        OutputConfiguration config;
        config.target = OutputManager::screenId(QGuiApplication::screens().first());
        config.signalMode = "not-supported";
        QString error;
        QVERIFY(!outputs.setConfiguration(OutputId::Audience2, config, &error));
        QVERIFY(!error.isEmpty()); QVERIFY(modes->applied.isEmpty());
        QCOMPARE(outputs.target(OutputId::Audience2), QStringLiteral("off"));
        config.signalMode = "interlaced"; modes->fail = true;
        QVERIFY(!outputs.setConfiguration(OutputId::Audience2, config, &error));
        QCOMPARE(outputs.target(OutputId::Audience2), QStringLiteral("off"));
        modes->fail = false; modes->supported = false;
        QVERIFY(!outputs.setConfiguration(OutputId::Audience2, config, &error));
        modes->supported = true;
        QVERIFY2(outputs.setConfiguration(OutputId::Audience2, config, &error), qPrintable(error));
        QCOMPARE(modes->current, QStringLiteral("interlaced"));
        QVERIFY(outputs.setTarget(OutputId::Audience2, "off"));
        QCOMPARE(modes->applied, (QStringList{"interlaced", "desktop"}));
        QVERIFY(outputs.setConfiguration(OutputId::Audience2, config, &error));
        outputs.shutdown();
        QCOMPARE(modes->current, QStringLiteral("desktop"));
        QVERIFY(outputs.setConfiguration(OutputId::Audience2, config, &error));
        modes->fail = true; outputs.output(OutputId::Audience2).close();
        QCOMPARE(outputs.target(OutputId::Audience2), QStringLiteral("off"));
        QVERIFY(!outputs.output(OutputId::Audience2).isVisible());
        QVERIFY(outputs.status(OutputId::Audience2).contains("restore failed"));
        modes->fail = false; outputs.shutdown(); QCOMPARE(modes->current, QStringLiteral("desktop"));
        QVERIFY(outputs.setConfiguration(OutputId::Audience2, config, &error));
        modes->current = "changed-in-OS";
        const auto calls = modes->applied.size(); outputs.shutdown();
        QCOMPARE(modes->applied.size(), calls);
    }

    void unavailableSavedModeHidesOutputUntilCapabilitiesReturn() {
        if (QGuiApplication::screens().isEmpty()) QSKIP("No screen reported.");
        auto *screen = QGuiApplication::screens().first();
        auto modes = std::make_shared<FakeDisplayModes>(); modes->available = false;
        OutputConfiguration config; config.target = OutputManager::screenId(screen); config.signalMode = "interlaced";
        QSettings().setValue("outputs/1/configuration", QJsonDocument(config.toJson()).toJson());
        PresentationController controller; LicensedOutputManager outputs(&controller, nullptr, modes);
        outputs.restoreTargets();
        QCOMPARE(outputs.target(OutputId::Audience2), config.target);
        QVERIFY(!outputs.output(OutputId::Audience2).isVisible());
        QVERIFY(outputs.status(OutputId::Audience2).contains("unavailable"));
        modes->available = true;
        screen->geometryChanged(screen->geometry());
        QTRY_VERIFY(outputs.output(OutputId::Audience2).isVisible());
        QCOMPARE(modes->current, QStringLiteral("interlaced"));
    }

    void stageLayoutsAssignIndependentlyAndProtectAssignedDefinitions() {
        PresentationController controller; LicensedOutputManager outputs(&controller);
        auto layout = StageLayout::standard(); layout.id = "custom"; layout.name = "Custom stage";
        layout.elements.append({StageElement::Kind::Message, {.05,.1,.9,.1}, {}, {}, 64, "#00ff00", true, 1});
        QString error;
        QVERIFY(outputs.saveStageLayout(layout, &error));
        auto stage2 = outputs.configuration(OutputId::Stage2); stage2.stageLayout = layout.id;
        QVERIFY(outputs.setConfiguration(OutputId::Stage2, stage2, &error));
        QCOMPARE(outputs.output(OutputId::Stage1).stageLayout().id, QStringLiteral("standard"));
        QCOMPARE(outputs.output(OutputId::Stage2).stageLayout().id, layout.id);
        QVERIFY(!outputs.deleteStageLayout(layout.id, &error));
        layout.background = "#112233";
        QVERIFY(outputs.saveStageLayout(layout, &error));
        QCOMPARE(outputs.output(OutputId::Stage2).stageLayout().background, layout.background);
        auto duplicate = layout; duplicate.id = "duplicate";
        QVERIFY(!outputs.saveStageLayout(duplicate, &error));
        QVERIFY(!outputs.saveStageLayout(StageLayout::standard(), &error));
        LicensedOutputManager reopened(&controller); reopened.restoreTargets();
        QCOMPARE(reopened.configuration(OutputId::Stage2).stageLayout, layout.id);
        QCOMPARE(reopened.output(OutputId::Stage2).stageLayout().background, layout.background);
        stage2.stageLayout = "standard"; QVERIFY(reopened.setConfiguration(OutputId::Stage2, stage2));
        QVERIFY(reopened.deleteStageLayout(layout.id, &error));
        QVERIFY(!reopened.deleteStageLayout("standard", &error));
    }

    void physicalModeConfirmationKeepsOrRevertsWithoutHardwareChanges_data() {
        QTest::addColumn<int>("answer");
        QTest::newRow("keep") << int(QMessageBox::Yes);
        QTest::newRow("revert") << int(QMessageBox::No);
        QTest::newRow("timeout-close") << int(QDialog::Rejected);
    }
    void physicalModeConfirmationKeepsOrRevertsWithoutHardwareChanges() {
        QFETCH(int, answer);
        if (QGuiApplication::screens().isEmpty()) QSKIP("No screen reported.");
        auto modes = std::make_shared<FakeDisplayModes>();
        PresentationController controller; controller.takeLive("Song", {"Unchanged"});
        const auto revision = controller.state().cueRevision;
        LicensedOutputManager outputs(&controller, nullptr, modes);
        const QString preference = QStringLiteral("outputs/0/configuration");
        QVERIFY(outputs.setTarget(OutputId::Audience1, "off"));
        const auto savedBefore = QSettings().value(preference);
        OutputSettingsDialog dialog(&outputs);
        const auto screen = OutputManager::screenId(QGuiApplication::screens().first());
        auto *display = dialog.findChild<QComboBox *>("outputDisplay0");
        auto *signal = dialog.findChild<QComboBox *>("outputSignal0");
        QVERIFY(display); QVERIFY(signal);
        display->setCurrentIndex(display->findData(screen));
        signal->setCurrentIndex(signal->findData("interlaced"));
        QTimer::singleShot(0, [answer, preference, savedBefore, modes] {
            auto *confirm = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(confirm);
            auto dismiss = qScopeGuard([confirm] { confirm->reject(); });
            QCOMPARE(modes->current, QStringLiteral("interlaced"));
            QCOMPARE(QSettings().value(preference), savedBefore);
            if (answer == QDialog::Rejected) return;
            dismiss.dismiss();
            confirm->button(QMessageBox::StandardButton(answer))->click();
        });
        dialog.findChild<QPushButton *>("saveOutput0")->click();
        if (answer == QMessageBox::Yes) {
            QCOMPARE(outputs.target(OutputId::Audience1), screen);
            QCOMPARE(modes->current, QStringLiteral("interlaced"));
            QCOMPARE(modes->applied, (QStringList{"interlaced"}));
            QCOMPARE(QJsonDocument::fromJson(QSettings().value(preference).toByteArray()).object()["signal"].toString(), QStringLiteral("interlaced"));
        } else {
            QCOMPARE(outputs.target(OutputId::Audience1), QStringLiteral("off"));
            QCOMPARE(modes->current, QStringLiteral("desktop"));
            QCOMPARE(modes->applied, (QStringList{"interlaced", "desktop"}));
            QCOMPARE(QSettings().value(preference), savedBefore);
        }
        QCOMPARE(controller.state().cueRevision, revision);
        outputs.shutdown(); QCOMPARE(modes->current, QStringLiteral("desktop"));
    }

    void unfinishedNativeModeTrialKeepsLastSavedRouteAcrossRestart() {
        if (QGuiApplication::screens().isEmpty()) QSKIP("No screen reported.");
        auto modes = std::make_shared<FakeDisplayModes>();
        PresentationController controller;
        {
            LicensedOutputManager outputs(&controller, nullptr, modes);
            outputs.restoreTargets();
            auto trial = outputs.configuration(OutputId::Audience1);
            trial.target = OutputManager::screenId(QGuiApplication::screens().first());
            trial.signalMode = "interlaced";
            QVERIFY(outputs.beginModeTrial(OutputId::Audience1, trial));
            QCOMPARE(modes->current, QStringLiteral("interlaced"));
        }
        QCOMPARE(modes->current, QStringLiteral("desktop"));
        LicensedOutputManager reopened(&controller, nullptr, modes);
        reopened.restoreTargets();
        QCOMPARE(reopened.target(OutputId::Audience1), QStringLiteral("off"));
        QCOMPARE(modes->current, QStringLiteral("desktop"));
    }

    void nativeCapabilitiesExposeOnlyReportedModes() {
        if (QGuiApplication::screens().isEmpty()) QSKIP("No screen reported.");
        PresentationController controller; LicensedOutputManager outputs(&controller);
        const auto caps = outputs.capabilities(OutputManager::screenId(QGuiApplication::screens().first()));
        qInfo() << "OS-reported display modes:" << caps.modes.size() << "; native mode changes available:" << caps.canSetMode;
        if (qEnvironmentVariableIsSet("WORSHIPCUE_PRINT_DISPLAY_CAPABILITIES")) {
            qInfo().noquote() << "Current:" << caps.currentMode << caps.explanation;
            for (const auto &mode : caps.modes) qInfo().noquote() << mode.id << mode.label();
        }
        QVERIFY(!caps.explanation.isEmpty());
        if (qEnvironmentVariableIsSet("WORSHIPCUE_EXPECT_NATIVE_MODE_SWITCHING")) {
            QVERIFY2(caps.canSetMode, qPrintable(caps.explanation));
            bool currentFound = false;
            for (const auto &mode : caps.modes) currentFound = currentFound || mode.id == caps.currentMode;
            QVERIFY2(currentFound, "Expanded native mode discovery must include the current mode for restoration.");
        }
        if (caps.canSetMode) { QVERIFY(!caps.modes.isEmpty()); QVERIFY(!caps.currentMode.isEmpty()); }
        for (const auto &mode : caps.modes) {
            QVERIFY(mode.pixels.width() > 0); QVERIFY(mode.pixels.height() > 0);
            QVERIFY(mode.refreshHz >= 0); QVERIFY(mode.interlaced >= -1 && mode.interlaced <= 1);
            QVERIFY(!mode.id.isEmpty()); QVERIFY(!mode.label().isEmpty());
        }
        // Discovery is read-only: no physical signal is changed by this test.
    }

    void previewsAreIndependentAndClosingPersistsOff() {
        PresentationController controller;
        {
            LicensedOutputManager outputs(&controller);
            outputs.restoreTargets();
            QCOMPARE(outputs.status(DisplayWindow::Kind::Audience), QStringLiteral("Off"));
            QVERIFY(outputs.setTarget(DisplayWindow::Kind::Audience, QStringLiteral("preview")));
            QVERIFY(outputs.setTarget(DisplayWindow::Kind::Stage, QStringLiteral("preview")));
            QVERIFY(outputs.audience().isVisible());
            QVERIFY(outputs.stage().isVisible());
            QCOMPARE(outputs.status(DisplayWindow::Kind::Audience), QStringLiteral("Windowed preview"));
            outputs.audience().close();
            QCOMPARE(outputs.target(DisplayWindow::Kind::Audience), QStringLiteral("off"));
            QVERIFY(outputs.stage().isVisible());
        }
        LicensedOutputManager reopened(&controller);
        reopened.restoreTargets();
        QCOMPARE(reopened.target(DisplayWindow::Kind::Audience), QStringLiteral("off"));
        QCOMPARE(reopened.target(DisplayWindow::Kind::Stage), QStringLiteral("preview"));
        QVERIFY(reopened.stage().isVisible());
    }

    void disconnectedDesiredTargetRemainsAvailable() {
        const QString missing = QStringLiteral("screen:Missing projector|Maker|Model|12345");
        QSettings().setValue(QStringLiteral("outputs/audienceTarget"), missing);
        PresentationController controller;
        LicensedOutputManager outputs(&controller);
        outputs.restoreTargets();
        QCOMPARE(outputs.target(DisplayWindow::Kind::Audience), missing);
        QCOMPARE(outputs.status(DisplayWindow::Kind::Audience), QStringLiteral("Disconnected"));
        QVERIFY(!outputs.audience().isVisible());
        bool found = false;
        for (const auto &target : outputs.availableTargets()) {
            if (target.id == missing) {
                found = true;
                QVERIFY(target.label.contains(QStringLiteral("Disconnected")));
            }
        }
        QVERIFY(found);
        QVERIFY(outputs.setTarget(DisplayWindow::Kind::Audience, missing));
        QVERIFY(outputs.setTarget(DisplayWindow::Kind::Audience, QStringLiteral("off")));
        QCOMPARE(outputs.status(DisplayWindow::Kind::Audience), QStringLiteral("Off"));
    }

    void sameFullscreenDisplayIsRejected() {
        PresentationController controller;
        LicensedOutputManager outputs(&controller);
        QString display;
        for (const auto &target : outputs.availableTargets())
            if (target.id.startsWith(QStringLiteral("screen:"))) { display = target.id; break; }
        if (display.isEmpty()) QSKIP("No screen is reported by the platform plugin.");
        QVERIFY(outputs.setTarget(DisplayWindow::Kind::Audience, display));
        QString error;
        QVERIFY(!outputs.setTarget(DisplayWindow::Kind::Stage, display, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(outputs.target(DisplayWindow::Kind::Stage), QStringLiteral("off"));
        QVERIFY(outputs.audience().isFullScreen());
        QVERIFY(outputs.setTarget(DisplayWindow::Kind::Audience, QStringLiteral("off")));
        QVERIFY(outputs.setTarget(DisplayWindow::Kind::Stage, display));
    }

    void invalidTargetsDoNotChangeRouting() {
        PresentationController controller;
        LicensedOutputManager outputs(&controller);
        QVERIFY(outputs.setTarget(DisplayWindow::Kind::Audience, QStringLiteral("preview")));
        QString error;
        QVERIFY(!outputs.setTarget(DisplayWindow::Kind::Audience, QStringLiteral("screen:unknown"), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(outputs.target(DisplayWindow::Kind::Audience), QStringLiteral("preview"));
        QVERIFY(outputs.audience().isVisible());
        QSettings().setValue(QStringLiteral("outputs/stageTarget"), QStringLiteral("invalid"));
        outputs.restoreTargets();
        QCOMPARE(outputs.target(DisplayWindow::Kind::Stage), QStringLiteral("off"));
    }

    void nativeVisibilityChangesReportActualStatusWithoutRestoringOutput() {
        PresentationController controller;
        LicensedOutputManager outputs(&controller);
        QVERIFY(outputs.setTarget(DisplayWindow::Kind::Audience, QStringLiteral("preview")));
        QCoreApplication::processEvents();
        QSignalSpy changed(&outputs, &OutputManager::statusChanged);
        outputs.audience().showMinimized();
        QTRY_VERIFY(changed.count() > 0);
        QCOMPARE(outputs.status(DisplayWindow::Kind::Audience), QStringLiteral("Minimized"));
        QCOMPARE(outputs.target(DisplayWindow::Kind::Audience), QStringLiteral("preview"));
        changed.clear();
        outputs.audience().showNormal();
        QTRY_VERIFY(changed.count() > 0);
        QCOMPARE(outputs.status(DisplayWindow::Kind::Audience), QStringLiteral("Windowed preview"));
        changed.clear();
        outputs.audience().hide();
        QTRY_VERIFY(changed.count() > 0);
        QCOMPARE(outputs.status(DisplayWindow::Kind::Audience), QStringLiteral("Off"));
        QVERIFY(!outputs.audience().isVisible());
        QCOMPARE(outputs.target(DisplayWindow::Kind::Audience), QStringLiteral("preview"));
    }

    void escapeFocusesOperatorAndKeepsOutput() {
        PresentationController controller;
        LicensedOutputManager outputs(&controller);
        QVERIFY(outputs.setTarget(DisplayWindow::Kind::Audience, QStringLiteral("preview")));
        QSignalSpy focus(&outputs, &OutputManager::operatorFocusRequested);
        QTest::keyClick(&outputs.audience(), Qt::Key_Escape);
        QCOMPARE(focus.count(), 1);
        QCOMPARE(outputs.target(DisplayWindow::Kind::Audience), QStringLiteral("preview"));
        QVERIFY(outputs.audience().isVisible());
    }
};

QTEST_MAIN(OutputManagerTest)
#include "output_manager_test.moc"
