#include "../src/core/PresentationController.h"
#include <QSignalSpy>
#include <QtTest/QtTest>

class PresentationControllerTest : public QObject {
    Q_OBJECT
private slots:
    void upcomingPreviewFollowsTimedLoopAndManualNextCanLeave() {
        PresentationController controller;
        const PresentationSettings settings{1000, true, SlideTransitionType::Fade, 300};
        const SlideMedia first{MediaKind::Image, QStringLiteral("/first.png"), QStringLiteral("First"), false};
        const SlideMedia last{MediaKind::Image, QStringLiteral("/last.png"), QStringLiteral("Last"), false};
        controller.takeServiceLive(1, {{11, "Loop", {"First", "Last"}, {first, last}, settings},
                                       {12, "Later", {"Next presentation"}}}, 11, 1);
        controller.setAutoAdvancePaused(true);
        QCOMPARE(controller.state().nextTitle(), QStringLiteral("Loop"));
        QCOMPARE(controller.state().nextText(), QStringLiteral("First"));
        QCOMPARE(controller.state().nextMedia(), first);
        QVERIFY(controller.next());
        QCOMPARE(controller.state().entryId, 12);
        QVERIFY(controller.state().nextText().isEmpty());
    }

    void autoAdvanceStopsAtPresentationBoundary() {
        PresentationController controller;
        PresentationSettings settings;
        settings.autoAdvanceMs = 100;
        controller.takeServiceLive(1, {{11, "Announcements", {"First", "Last"}, {}, settings},
                                       {12, "Song", {"Verse"}, {}, settings}}, 11);
        QVERIFY(controller.autoAdvanceActive());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state().index, 1, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.autoAdvanceActive(), 1000);
        QCOMPARE(controller.state().entryId, 11);
        QCOMPARE(controller.autoAdvanceRemainingMs(), 0);
        QVERIFY(controller.next());
        QCOMPARE(controller.state().entryId, 12);
        QVERIFY(controller.autoAdvanceActive());
    }

    void autoAdvanceLoopsOnlyTheCurrentPresentation() {
        PresentationController controller;
        PresentationSettings settings;
        settings.autoAdvanceMs = 100;
        settings.loop = true;
        controller.takeServiceLive(1, {{11, "Loop", {"Only"}, {}, settings},
                                       {12, "Later", {"Never automatic"}}}, 11);
        QSignalSpy cues(&controller, &PresentationController::stateChanged);
        QTRY_VERIFY_WITH_TIMEOUT(cues.count() >= 2, 1000);
        controller.setAutoAdvancePaused(true);
        QCOMPARE(controller.state().entryId, 11);
        QCOMPARE(controller.state().index, 0);
        QVERIFY(controller.autoAdvanceActive());
        QVERIFY(controller.autoAdvancePaused());
    }

    void pausingPreservesTheRemainingCountdown() {
        PresentationController controller;
        PresentationSettings settings;
        settings.autoAdvanceMs = 300;
        controller.takeLive("Timed", {"One", "Two"}, 0, settings);
        QTest::qWait(80);
        controller.setAutoAdvancePaused(true);
        const int remaining = controller.autoAdvanceRemainingMs();
        QVERIFY(remaining > 0 && remaining < settings.autoAdvanceMs);
        QVERIFY(controller.autoAdvanceManuallyPaused());
        QTest::qWait(350);
        QCOMPARE(controller.state().index, 0);
        QCOMPARE(controller.autoAdvanceRemainingMs(), remaining);
        controller.toggleAutoAdvancePaused();
        QVERIFY(!controller.autoAdvancePaused());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state().index, 1, 1000);
    }

    void hidingAudienceSuspendsTimingWithoutLosingManualPause() {
        PresentationController controller;
        PresentationSettings settings;
        settings.autoAdvanceMs = 200;
        controller.takeLive("Timed", {"One", "Two"}, 0, settings);
        QTest::qWait(30);
        controller.toggleBlackout();
        const int remaining = controller.autoAdvanceRemainingMs();
        QVERIFY(controller.autoAdvancePaused());
        QVERIFY(!controller.autoAdvanceManuallyPaused());
        QTest::qWait(250);
        QCOMPARE(controller.state().index, 0);
        QCOMPARE(controller.autoAdvanceRemainingMs(), remaining);
        controller.toggleClear();
        QVERIFY(!controller.state().blackout);
        QVERIFY(controller.state().clear);
        QTest::qWait(250);
        QCOMPARE(controller.autoAdvanceRemainingMs(), remaining);
        controller.restoreOutput();
        QVERIFY(!controller.autoAdvancePaused());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state().index, 1, 1000);
        QVERIFY(controller.setSlide(0));
        controller.setAutoAdvancePaused(true);
        controller.toggleBlackout();
        controller.restoreOutput();
        QVERIFY(controller.autoAdvancePaused());
        QTest::qWait(250);
        QCOMPARE(controller.state().index, 0);
        controller.setAutoAdvancePaused(false);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state().index, 1, 1000);
        QVERIFY(!controller.autoAdvancePaused());
    }

    void manualNavigationRestartsTheIntervalAndExplicitTakeResumes() {
        PresentationController controller;
        PresentationSettings settings;
        settings.autoAdvanceMs = 300;
        controller.takeLive("Timed", {"One", "Two", "Three"}, 0, settings);
        QTest::qWait(120);
        QVERIFY(controller.next());
        QVERIFY(controller.autoAdvanceRemainingMs() > 250);
        QTest::qWait(200);
        QCOMPARE(controller.state().index, 1);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state().index, 2, 1000);
        controller.setAutoAdvancePaused(true);
        QVERIFY(controller.previous());
        QVERIFY(controller.autoAdvancePaused());
        QCOMPARE(controller.autoAdvanceRemainingMs(), settings.autoAdvanceMs);
        // References to live state are safe even when takeLive replaces it.
        controller.takeLive(controller.state().title, controller.state().slides, 0, controller.state().settings);
        QVERIFY(!controller.autoAdvancePaused());
        QVERIFY(controller.autoAdvanceRemainingMs() > 250);
    }

    void timingAndTransitionsUseTheLiveSnapshotAndValidateSettings() {
        PresentationController controller;
        PresentationSettings settings;
        settings.autoAdvanceMs = 100;
        settings.transition = SlideTransitionType::Fade;
        settings.transitionDurationMs = 750;
        QVector<PresentationItem> items{{11, "Timed", {"One", "Two"}, {}, settings},
                                        {12, "Manual", {"Next"}}};
        controller.takeServiceLive(1, items, 11);
        items[0].settings = {};
        items[1].settings = settings;
        QCOMPARE(controller.state().settings, settings);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state().index, 1, 1000);
        QVERIFY(controller.next());
        QCOMPARE(controller.state().entryId, 12);
        QCOMPARE(controller.state().settings, PresentationSettings{});
        QVERIFY(!controller.autoAdvanceActive());
        settings.autoAdvanceMs = -1;
        controller.takeLive("Invalid", {"One"}, 0, settings);
        QCOMPARE(controller.state().settings, PresentationSettings{});
        QVERIFY(!controller.autoAdvanceActive());
        settings.autoAdvanceMs = 100;
        controller.takeLive("Empty", {}, 0, settings);
        QVERIFY(!controller.autoAdvanceActive());
        QCOMPARE(controller.autoAdvanceRemainingMs(), 0);
    }

    void mediaFollowsLiveSnapshotAndUpcomingNavigation() {
        PresentationController controller;
        QVector<PresentationItem> items{
            {81, QStringLiteral("Pictures"), {QString(), QString()},
             {{MediaKind::Image, QStringLiteral("/media/one.jpg"), QStringLiteral("Welcome 01.jpg"), false},
              {MediaKind::Video, QStringLiteral("/media/two.mp4"), QStringLiteral("Welcome 02.mp4"), true}}},
            {82, QStringLiteral("Sermon"), {QString()},
             {{MediaKind::Image, QStringLiteral("/media/sermon.jpg"), QStringLiteral("Sermon title.jpg"), false}}}
        };
        controller.takeServiceLive(8, items, 81);
        QCOMPARE(controller.state().currentMedia().kind, MediaKind::Image);
        QCOMPARE(controller.state().nextMedia().kind, MediaKind::Video);
        QVERIFY(controller.state().nextMedia().loop);
        items[0].media[1].name = QStringLiteral("Edited filename");
        QVERIFY(controller.next());
        QCOMPARE(controller.state().currentMedia().name, QStringLiteral("Welcome 02.mp4"));
        QCOMPARE(controller.state().nextMedia().path, QStringLiteral("/media/sermon.jpg"));
        QVERIFY(controller.next());
        QCOMPARE(controller.state().entryId, 82);
        QCOMPARE(controller.state().currentMedia().kind, MediaKind::Image);
        QCOMPARE(controller.state().nextMedia().kind, MediaKind::None);
        controller.takeLive(QStringLiteral("Song"), {QStringLiteral("Lyric")});
        QCOMPARE(controller.state().currentMedia().kind, MediaKind::None);
    }

    void cueRevisionChangesOnlyForPresentationCommands() {
        PresentationController controller;
        controller.takeLive(QStringLiteral("Song"), {QStringLiteral("One"), QStringLiteral("Two")});
        const auto first = controller.state().cueRevision;
        controller.toggleBlackout();
        controller.toggleClear();
        controller.restoreOutput();
        QCOMPARE(controller.state().cueRevision, first);
        QVERIFY(controller.next());
        QVERIFY(controller.state().cueRevision > first);
        const auto second = controller.state().cueRevision;
        QVERIFY(!controller.next());
        QCOMPARE(controller.state().cueRevision, second);
        QVERIFY(controller.setSlide(1));
        QVERIFY(controller.state().cueRevision > second);
    }

    void navigationBoundaries() {
        PresentationController controller;
        controller.takeLive("Welcome", {"One", "Two", "Three"});
        QCOMPARE(controller.state().index, 0);
        QCOMPARE(controller.state().currentText(), QStringLiteral("One"));
        QCOMPARE(controller.state().nextText(), QStringLiteral("Two"));
        QVERIFY(!controller.previous());
        QVERIFY(controller.next());
        QCOMPARE(controller.state().currentText(), QStringLiteral("Two"));
        QVERIFY(controller.next());
        QCOMPARE(controller.state().index, 2);
        QVERIFY(!controller.next());
        QCOMPARE(controller.state().index, 2);
    }

    void blackoutAndClearAreExclusive() {
        PresentationController controller;
        controller.takeLive("Test", {"First", "Second"});
        controller.toggleBlackout();
        QVERIFY(controller.state().blackout);
        QVERIFY(!controller.state().clear);
        controller.toggleClear();
        QVERIFY(!controller.state().blackout);
        QVERIFY(controller.state().clear);
        QVERIFY(controller.next());
        QVERIFY(!controller.state().clear);
        QCOMPARE(controller.state().currentText(), QStringLiteral("Second"));
    }

    void goingLiveEmitsChange() {
        PresentationController controller;
        QSignalSpy spy(&controller, &PresentationController::stateChanged);
        controller.takeLive("Example", {"Hello"});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(controller.state().title, QStringLiteral("Example"));
        QVERIFY(!controller.state().blackout);
    }

    void serviceNavigationSkipsEmptyEntriesAndStopsAtEnds() {
        PresentationController controller;
        const QVector<PresentationItem> service{
            {11, "Welcome", {"Good morning", "Welcome everyone"}},
            {12, "Empty", {}},
            {13, "Song", {"Verse", "Chorus"}},
            {14, "Empty end", {}}
        };
        controller.takeServiceLive(7, service, 11);
        QCOMPARE(controller.state().serviceId, 7);
        QCOMPARE(controller.state().entryId, 11);
        QCOMPARE(controller.state().nextTitle(), QStringLiteral("Welcome"));
        QVERIFY(!controller.previous());
        QVERIFY(controller.next());
        QCOMPARE(controller.state().nextText(), QStringLiteral("Verse"));
        QCOMPARE(controller.state().nextTitle(), QStringLiteral("Song"));
        QVERIFY(controller.next());
        QCOMPARE(controller.state().entryId, 13);
        QCOMPARE(controller.state().index, 0);
        QVERIFY(controller.previous());
        QCOMPARE(controller.state().entryId, 11);
        QCOMPARE(controller.state().index, 1);
        QVERIFY(controller.next());
        QVERIFY(controller.next());
        QCOMPARE(controller.state().index, 1);
        QVERIFY(controller.state().nextText().isEmpty());
        QVERIFY(controller.state().nextTitle().isEmpty());
        QSignalSpy spy(&controller, &PresentationController::stateChanged);
        QVERIFY(!controller.next());
        QCOMPARE(spy.count(), 0);
        QCOMPARE(controller.state().entryId, 13);
    }

    void repeatedSongsRemainDistinctEntries() {
        PresentationController controller;
        controller.takeServiceLive(1, {{20, "Song", {"Verse"}},
                                       {21, "Song", {"Verse"}}}, 20);
        QVERIFY(controller.next());
        QCOMPARE(controller.state().entryId, 21);
        QVERIFY(controller.previous());
        QCOMPARE(controller.state().entryId, 20);
    }

    void liveServiceIsIsolatedFromLaterEdits() {
        PresentationController controller;
        QVector<PresentationItem> service{{31, "First", {"Original"}},
                                          {32, "Second", {"Upcoming"}}};
        controller.takeServiceLive(3, service, 31);
        service[0].slides[0] = "Edited";
        service[1].slides[0] = "Changed";
        service.removeFirst();
        QCOMPARE(controller.state().currentText(), QStringLiteral("Original"));
        QCOMPARE(controller.state().nextText(), QStringLiteral("Upcoming"));
        QVERIFY(controller.next());
        QCOMPARE(controller.state().currentText(), QStringLiteral("Upcoming"));
        QCOMPARE(controller.state().entryId, 32);
        QCOMPARE(controller.state().serviceId, 3);
    }

    void invalidTakeDoesNotReplaceLiveOutput() {
        PresentationController controller;
        controller.takeServiceLive(2, {{41, "Live", {"Keep this"}}}, 41);
        controller.toggleBlackout();
        QSignalSpy spy(&controller, &PresentationController::stateChanged);
        controller.takeServiceLive(4, {{51, "Other", {"Other text"}}}, 99);
        controller.takeServiceLive(4, {{51, "Empty", {}}}, 51);
        controller.takeServiceLive(0, {{51, "Other", {"Text"}}}, 51);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(controller.state().serviceId, 2);
        QCOMPARE(controller.state().entryId, 41);
        QCOMPARE(controller.state().currentText(), QStringLiteral("Keep this"));
        QVERIFY(controller.state().blackout);
        QVERIFY(!controller.setSlide(-1));
        QVERIFY(!controller.setSlide(10));
        QVERIFY(controller.state().blackout);
    }

    void restoreAndCrossItemNavigationRestoreAudience() {
        PresentationController controller;
        controller.takeServiceLive(9, {{61, "First", {"One"}},
                                      {62, "Second", {"Two"}}}, 61);
        controller.toggleBlackout();
        controller.restoreOutput();
        QVERIFY(!controller.state().blackout);
        QVERIFY(!controller.state().clear);
        controller.toggleClear();
        controller.restoreOutput();
        QVERIFY(!controller.state().clear);
        QCOMPARE(controller.state().currentText(), QStringLiteral("One"));
        controller.toggleBlackout();
        QVERIFY(controller.next());
        QVERIFY(!controller.state().blackout);
        controller.toggleClear();
        QVERIFY(controller.previous());
        QVERIFY(!controller.state().clear);
        QCOMPARE(controller.state().currentText(), QStringLiteral("One"));
    }

    void standaloneTakeClearsServiceSnapshot() {
        PresentationController controller;
        controller.takeServiceLive(1, {{1, "One", {"One"}},
                                      {2, "Two", {"Two"}}}, 1);
        controller.takeLive("Standalone", {"Only"}, 100);
        QCOMPARE(controller.state().serviceId, 0);
        QCOMPARE(controller.state().entryId, 0);
        QCOMPARE(controller.state().index, 0);
        QVERIFY(controller.state().nextText().isEmpty());
        QVERIFY(!controller.next());
        controller.takeLive("Empty", {});
        QCOMPARE(controller.state().index, -1);
        QVERIFY(controller.state().currentText().isEmpty());
        QVERIFY(controller.state().nextText().isEmpty());
        QVERIFY(!controller.next());
        QVERIFY(!controller.previous());
    }
};

QTEST_GUILESS_MAIN(PresentationControllerTest)
#include "presentation_controller_test.moc"
