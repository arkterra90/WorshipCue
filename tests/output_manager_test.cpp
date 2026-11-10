#include "../src/ui/OutputManager.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

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

    void previewsAreIndependentAndClosingPersistsOff() {
        PresentationController controller;
        {
            OutputManager outputs(&controller);
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
        OutputManager reopened(&controller);
        reopened.restoreTargets();
        QCOMPARE(reopened.target(DisplayWindow::Kind::Audience), QStringLiteral("off"));
        QCOMPARE(reopened.target(DisplayWindow::Kind::Stage), QStringLiteral("preview"));
        QVERIFY(reopened.stage().isVisible());
    }

    void disconnectedDesiredTargetRemainsAvailable() {
        const QString missing = QStringLiteral("screen:Missing projector|Maker|Model|12345");
        QSettings().setValue(QStringLiteral("outputs/audienceTarget"), missing);
        PresentationController controller;
        OutputManager outputs(&controller);
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
        OutputManager outputs(&controller);
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
        OutputManager outputs(&controller);
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
        OutputManager outputs(&controller);
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
        OutputManager outputs(&controller);
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
