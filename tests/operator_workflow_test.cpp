#include "../src/ui/MainWindow.h"
#include "../src/ui/ReorderListWidget.h"
#include "../src/ui/MediaImportDialog.h"
#include "../src/ui/PresentationSettingsDialog.h"
#include <QAction>
#include <QFileInfo>
#include <QImage>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class OperatorWorkflowTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    template<class T> T *control(MainWindow &window, const char *name) {
        auto *found = window.findChild<T *>(QString::fromLatin1(name));
        Q_ASSERT(found);
        return found;
    }
    void answerTextDialog(const QString &text) {
        QTimer::singleShot(0, [text] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            dialog->setTextValue(text);
            dialog->accept();
        });
    }
private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("WorshipCueTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Operator"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
    }
    void init() { QSettings().clear(); }

    void presentationSettingsSaveCancelAndKeepLiveSnapshot() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        const int planId = observer.services().first().id;
        const int songId = observer.service(planId).first().songId;
        const int entryId = observer.service(planId).first().id;
        const PresentationSettings saved{1000, true, SlideTransitionType::Fade, 250};
        {
            MainWindow window;
            QVERIFY2(window.initialize(&error, path), qPrintable(error));
            window.show();
            control<QComboBox>(window, "savedServices")->setCurrentIndex(0);
            control<QListWidget>(window, "serviceOrder")->setCurrentRow(0);
            auto *settings = control<QPushButton>(window, "presentationSettings");
            QVERIFY(settings->isEnabled());
            QTimer::singleShot(0, [] {
                auto *dialog = qobject_cast<PresentationSettingsDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                dialog->findChild<QCheckBox *>(QStringLiteral("autoAdvanceEnabled"))->setChecked(true);
                dialog->findChild<QDoubleSpinBox *>(QStringLiteral("autoAdvanceInterval"))->setValue(1.0);
                dialog->findChild<QCheckBox *>(QStringLiteral("loopPresentation"))->setChecked(true);
                dialog->findChild<QComboBox *>(QStringLiteral("slideTransitionType"))->setCurrentIndex(1);
                dialog->findChild<QDoubleSpinBox *>(QStringLiteral("slideTransitionDuration"))->setValue(.25);
                if (qEnvironmentVariableIsSet("WORSHIPCUE_TIMING_DIALOG_SCREENSHOT"))
                    dialog->grab().save(qEnvironmentVariable("WORSHIPCUE_TIMING_DIALOG_SCREENSHOT"));
                auto *buttons = dialog->findChild<QDialogButtonBox *>(QStringLiteral("presentationSettingsActions"));
                QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
            });
            QTest::mouseClick(settings, Qt::LeftButton);
            QCOMPARE(observer.song(songId).settings, saved);
            QVERIFY(control<QLabel>(window, "selectedTimingInfo")->text().contains(QStringLiteral("Fade")));
            auto *controller = control<PresentationController>(window, "presentationController");
            QCOMPARE(controller->state().index, -1);
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            QCOMPARE(controller->state().settings, saved);
            QCOMPARE(controller->state().entryId, entryId);
            auto *pause = control<QPushButton>(window, "pauseAutoAdvance");
            QVERIFY(pause->isEnabled());
            QTest::mouseClick(pause, Qt::LeftButton);
            QVERIFY(controller->autoAdvanceManuallyPaused());
            QCOMPARE(pause->text(), QStringLiteral("Resume Auto-Advance"));
            const int remaining = controller->autoAdvanceRemainingMs();
            QTest::qWait(1100);
            QCOMPARE(controller->state().index, 0);
            QCOMPARE(controller->autoAdvanceRemainingMs(), remaining);
            QTimer::singleShot(0, [] {
                auto *dialog = qobject_cast<PresentationSettingsDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                dialog->findChild<QDoubleSpinBox *>(QStringLiteral("autoAdvanceInterval"))->setValue(2.0);
                dialog->findChild<QComboBox *>(QStringLiteral("slideTransitionType"))->setCurrentIndex(3);
                auto *buttons = dialog->findChild<QDialogButtonBox *>(QStringLiteral("presentationSettingsActions"));
                QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton);
            });
            QTest::mouseClick(settings, Qt::LeftButton);
            QCOMPARE(observer.song(songId).settings, saved);
            QTimer::singleShot(0, [] {
                auto *dialog = qobject_cast<PresentationSettingsDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                dialog->findChild<QDoubleSpinBox *>(QStringLiteral("autoAdvanceInterval"))->setValue(1.5);
                dialog->findChild<QComboBox *>(QStringLiteral("slideTransitionType"))->setCurrentIndex(2);
                dialog->findChild<QDoubleSpinBox *>(QStringLiteral("slideTransitionDuration"))->setValue(.4);
                auto *buttons = dialog->findChild<QDialogButtonBox *>(QStringLiteral("presentationSettingsActions"));
                QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
            });
            QTest::mouseClick(settings, Qt::LeftButton);
            QCOMPARE(controller->state().settings, saved);
            QVERIFY(controller->autoAdvanceManuallyPaused());
            QCOMPARE(observer.song(songId).settings, (PresentationSettings{1500, true, SlideTransitionType::SlideLeft, 400}));
            QVERIFY(controller->setSlide(1));
            QCOMPARE(control<QLabel>(window, "nextInfo")->text(), QStringLiteral("UP NEXT • Welcome"));
            QCOMPARE(controller->state().nextText(), controller->state().slides.first());
            QVERIFY(controller->setSlide(0));
            if (qEnvironmentVariableIsSet("WORSHIPCUE_TIMING_SCREENSHOT"))
                QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_TIMING_SCREENSHOT")));
            window.close();
        }
        MainWindow reopened;
        QVERIFY2(reopened.initialize(&error, path), qPrintable(error));
        QVERIFY(control<QLabel>(reopened, "selectedTimingInfo")->text().contains(QStringLiteral("Slide Left")));
        QTest::mouseClick(control<QPushButton>(reopened, "takeLive"), Qt::LeftButton);
        auto *controller = control<PresentationController>(reopened, "presentationController");
        QCOMPARE(controller->state().settings, (PresentationSettings{1500, true, SlideTransitionType::SlideLeft, 400}));
        QVERIFY(!controller->autoAdvancePaused());
    }

    void automaticPlaybackUpdatesMonitorAndHoldsPresentationEnd() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        const int planId = observer.services().first().id;
        const auto entries = observer.service(planId);
        QVERIFY(observer.updatePresentationSettings(entries.first().songId,
            {200, false, SlideTransitionType::Cut, 0}, &error));
        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        auto *controller = control<PresentationController>(window, "presentationController");
        QTRY_VERIFY_WITH_TIMEOUT(!controller->autoAdvanceActive(), 3000);
        QCOMPARE(controller->state().entryId, entries.first().id);
        QCOMPARE(controller->state().index, 1);
        QVERIFY(control<QLabel>(window, "liveInfo")->text().contains(QStringLiteral("2/2")));
        QVERIFY(control<QLabel>(window, "autoAdvanceStatus")->text().contains(QStringLiteral("Complete")));
        QVERIFY(!control<QPushButton>(window, "pauseAutoAdvance")->isEnabled());
        QTest::mouseClick(control<QPushButton>(window, "nextSlide"), Qt::LeftButton);
        QCOMPARE(controller->state().entryId, entries.at(1).id);
        QVERIFY(control<QLabel>(window, "autoAdvanceStatus")->text().contains(QStringLiteral("Off")));
    }

    void buildSaveReopenService() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        int planId = 0;
        {
            MainWindow window;
            QVERIFY2(window.initialize(&error, path), qPrintable(error));
            window.show();
            auto *services = control<QComboBox>(window, "savedServices");
            answerTextDialog(QStringLiteral("Sunday Morning"));
            QTest::mouseClick(control<QPushButton>(window, "newService"), Qt::LeftButton);
            QCOMPARE(services->currentText(), QStringLiteral("Sunday Morning"));
            planId = services->currentData().toInt();
            auto *songs = control<QListWidget>(window, "songLibrary");
            auto *order = control<ReorderListWidget>(window, "serviceOrder");
            QCOMPARE(order->count(), 0);
            songs->setCurrentRow(0);
            QTest::mouseClick(control<QPushButton>(window, "addSong"), Qt::LeftButton);
            songs->setCurrentRow(1);
            QTest::mouseClick(control<QPushButton>(window, "addSong"), Qt::LeftButton);
            QTest::mouseClick(control<QPushButton>(window, "duplicateEntry"), Qt::LeftButton);
            QCOMPARE(order->count(), 3);
            const int last = order->item(2)->data(Qt::UserRole).toInt();
            QVERIFY(order->moveItem(2, 0));
            QTRY_COMPARE(order->item(0)->data(Qt::UserRole).toInt(), last);
            auto *slides = control<ReorderListWidget>(window, "slideWorkspace");
            order->setCurrentRow(0);
            control<QComboBox>(window, "operatorMode")->setCurrentIndex(1);
            const QString original = slides->item(0)->data(Qt::UserRole + 1).toString();
            QVERIFY(slides->moveItem(0, slides->count() - 1));
            QCOMPARE(slides->item(slides->count() - 1)->data(Qt::UserRole + 1).toString(), original);
            answerTextDialog(QStringLiteral("Sunday 9 AM"));
            QTest::mouseClick(control<QPushButton>(window, "renameService"), Qt::LeftButton);
            QCOMPARE(services->currentText(), QStringLiteral("Sunday 9 AM"));
            answerTextDialog(QStringLiteral("Next Sunday"));
            QTest::mouseClick(control<QPushButton>(window, "copyService"), Qt::LeftButton);
            QCOMPARE(services->currentText(), QStringLiteral("Next Sunday"));
            QCOMPARE(order->count(), 3);
            services->setCurrentIndex(services->findData(planId));
            window.close();
        }
        {
            MainWindow reopened;
            QVERIFY2(reopened.initialize(&error, path), qPrintable(error));
            auto *services = control<QComboBox>(reopened, "savedServices");
            QCOMPARE(services->currentData().toInt(), planId);
            QCOMPARE(services->currentText(), QStringLiteral("Sunday 9 AM"));
            QCOMPARE(control<QListWidget>(reopened, "serviceOrder")->count(), 3);
            LibraryRepository observer;
            QVERIFY(observer.open(&error, path));
            const auto entries = observer.service(planId);
            QCOMPARE(entries.size(), 3);
            QVERIFY(!entries.first().slideOrder.isEmpty());
            QCOMPARE(entries.first().slideOrder.last(), 0);
        }
    }

    void importRenameAndReopenPicturePresentation() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        const QStringList originals = {temp.filePath(QStringLiteral("1.png")), temp.filePath(QStringLiteral("2.png")), temp.filePath(QStringLiteral("10.png"))};
        const QList<QColor> colors = {Qt::red, Qt::blue, Qt::green};
        for (int i = 0; i < originals.size(); ++i) {
            QImage image(160, 90, QImage::Format_RGB32);
            image.fill(colors.at(i));
            QVERIFY(image.save(originals.at(i)));
        }
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        const int originalPlan = observer.services().first().id;
        const int otherPlan = observer.createService(QStringLiteral("Other Service"), &error);
        int importedId = 0;
        QStringList stablePaths;
        {
            MainWindow window;
            QVERIFY2(window.initialize(&error, path), qPrintable(error));
            window.show();
            auto *plans = control<QComboBox>(window, "savedServices");
            plans->setCurrentIndex(plans->findData(originalPlan));
            auto *controller = control<PresentationController>(window, "presentationController");
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            const QString liveBefore = controller->state().currentText();
            QTimer::singleShot(0, [] {
                auto *dialog = qobject_cast<MediaImportDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                dialog->setPresentationTitle(QStringLiteral("Sunday Announcements"));
                auto *buttons = dialog->findChild<QDialogButtonBox *>(QStringLiteral("mediaImportActions"));
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            });
            QVERIFY(window.importMediaFiles({originals.at(2), originals.at(0), originals.at(1)}));
            plans->setCurrentIndex(plans->findData(otherPlan));
            QTRY_VERIFY_WITH_TIMEOUT(control<QPushButton>(window, "importMedia")->isEnabled(), 10000);
            QCOMPARE(controller->state().currentText(), liveBefore);
            QCOMPARE(plans->currentData().toInt(), otherPlan);
            const auto imported = observer.songs(QStringLiteral("Sunday Announcements"));
            QCOMPARE(imported.size(), 1);
            importedId = imported.first().id;
            QCOMPARE(imported.first().slides.size(), 3);
            QCOMPARE(observer.service(originalPlan).size(), 3);
            QCOMPARE(observer.service(otherPlan).size(), 0);
            for (int i = 0; i < 3; ++i) {
                const auto media = imported.first().media.at(i);
                QCOMPARE(media.name, QStringLiteral("Sunday Announcements %1.png").arg(i + 1, 2, 10, QLatin1Char('0')));
                QVERIFY(QFileInfo::exists(temp.filePath(media.path)));
                QVERIFY(QFileInfo::exists(originals.at(i)));
                stablePaths.append(media.path);
                QImage copied(temp.filePath(media.path));
                QCOMPARE(copied.pixelColor(10, 10), colors.at(i));
            }
            plans->setCurrentIndex(plans->findData(originalPlan));
            auto *order = control<QListWidget>(window, "serviceOrder");
            order->setCurrentRow(order->count() - 1);
            auto *slides = control<QListWidget>(window, "slideWorkspace");
            QCOMPARE(slides->count(), 3);
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            auto *playback = control<MediaPlayback>(window, "mediaPlayback");
            QTRY_VERIFY_WITH_TIMEOUT(!playback->frame().isNull(), 5000);
            QCOMPARE(playback->frame().toImage().pixelColor(10, 10), QColor(Qt::red));
            QVERIFY(!control<QLabel>(window, "nextInfo")->text().contains(QStringLiteral("End of service")));
            QTimer::singleShot(0, [] {
                auto *dialog = qobject_cast<MediaImportDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                dialog->findChild<QLineEdit *>(QStringLiteral("renameText"))->setText(QStringLiteral("Christmas Welcome"));
                if (qEnvironmentVariableIsSet("WORSHIPCUE_RENAME_SCREENSHOT"))
                    dialog->grab().save(qEnvironmentVariable("WORSHIPCUE_RENAME_SCREENSHOT"));
                auto *buttons = dialog->findChild<QDialogButtonBox *>(QStringLiteral("mediaImportActions"));
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            });
            control<QAction>(window, "renameMediaAction")->trigger();
            QCOMPARE(observer.song(importedId).media.first().name, QStringLiteral("Christmas Welcome 01.png"));
            QCOMPARE(observer.song(importedId).media.first().path, stablePaths.first());
            QCOMPARE(controller->state().currentMedia().name, QStringLiteral("Sunday Announcements 01.png"));
            QVERIFY(slides->item(0)->text().contains(QStringLiteral("LIVE")));
            QTest::mouseClick(control<QPushButton>(window, "nextSlide"), Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(!playback->frame().isNull(), 5000);
            QCOMPARE(playback->frame().toImage().pixelColor(10, 10), QColor(Qt::blue));
            QCOMPARE(slides->currentRow(), 1);
            if (qEnvironmentVariableIsSet("WORSHIPCUE_MEDIA_SCREENSHOT"))
                QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_MEDIA_SCREENSHOT")));
            window.close();
        }
        MainWindow reopened;
        QVERIFY(reopened.initialize(&error, path));
        const auto media = observer.song(importedId).media;
        QCOMPARE(media.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(media.at(i).path, stablePaths.at(i));
            QCOMPARE(media.at(i).name, QStringLiteral("Christmas Welcome %1.png").arg(i + 1, 2, 10, QLatin1Char('0')));
        }
        auto *order = control<QListWidget>(reopened, "serviceOrder");
        order->setCurrentRow(order->count() - 1);
        QCOMPARE(control<QListWidget>(reopened, "slideWorkspace")->count(), 3);
    }

    void importVideoAndUseOperatorPlaybackControls() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        const QString fixture = QFINDTESTDATA("fixtures/media-colors.mov");
        QVERIFY(!fixture.isEmpty());
        QString error;
        MainWindow window;
        QVERIFY(window.initialize(&error, path));
        window.show();
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<MediaImportDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            dialog->setPresentationTitle(QStringLiteral("Welcome Video"));
            dialog->findChild<QCheckBox *>(QStringLiteral("loopVideos"))->setChecked(true);
            auto *buttons = dialog->findChild<QDialogButtonBox *>(QStringLiteral("mediaImportActions"));
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
        });
        QVERIFY(window.importMediaFiles({fixture}));
        QTRY_VERIFY_WITH_TIMEOUT(control<QPushButton>(window, "importMedia")->isEnabled(), 10000);
        auto *slides = control<QListWidget>(window, "slideWorkspace");
        QCOMPARE(slides->count(), 1);
        QVERIFY(slides->item(0)->text().contains(QStringLiteral("Welcome Video 01.mov")));
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        auto *controller = control<PresentationController>(window, "presentationController");
        QCOMPARE(controller->state().currentMedia().kind, MediaKind::Video);
        QVERIFY(controller->state().currentMedia().loop);
        auto *playback = control<MediaPlayback>(window, "mediaPlayback");
        QTRY_VERIFY_WITH_TIMEOUT(!playback->frame().isNull() || !playback->errorString().isEmpty(), 12000);
        QVERIFY2(playback->errorString().isEmpty(), qPrintable(playback->errorString()));
        auto *pause = control<QPushButton>(window, "pauseVideo");
        QVERIFY(pause->isVisible());
        QTest::mouseClick(pause, Qt::LeftButton);
        QTRY_VERIFY(playback->isPaused());
        QCOMPARE(pause->text(), QStringLiteral("Play Video"));
        QTest::mouseClick(control<QPushButton>(window, "muteVideo"), Qt::LeftButton);
        QVERIFY(playback->muted());
        QTest::mouseClick(control<QPushButton>(window, "restartVideo"), Qt::LeftButton);
        QTRY_VERIFY(!playback->isPaused());
        QTest::mouseClick(control<QPushButton>(window, "blackout"), Qt::LeftButton);
        QVERIFY(controller->state().blackout);
        QTest::mouseClick(control<QPushButton>(window, "restoreAudience"), Qt::LeftButton);
        QVERIFY(!controller->state().blackout);
        QVERIFY(playback->muted());
        QVERIFY(!playback->frame().isNull());
        if (qEnvironmentVariableIsSet("WORSHIPCUE_VIDEO_SCREENSHOT"))
            QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_VIDEO_SCREENSHOT")));
    }

    void liveNavigationIsIndependentOfBrowsingAndTyping() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository seed;
        QVERIFY(seed.open(&error, path));
        QVERIFY(seed.createSong(QStringLiteral("Alpha"), {QStringLiteral("First lyric"), QStringLiteral("Second lyric")}, &error));
        QVERIFY(seed.createSong(QStringLiteral("Beta"), {QStringLiteral("Final lyric")}, &error));
        const auto alpha = seed.songs(QStringLiteral("Alpha")).first();
        const auto beta = seed.songs(QStringLiteral("Beta")).first();
        const int livePlan = seed.createService(QStringLiteral("Live Service"), &error);
        const int otherPlan = seed.createService(QStringLiteral("Next Service"), &error);
        QVERIFY(seed.appendToService(livePlan, alpha.id, &error));
        QVERIFY(seed.appendToService(livePlan, beta.id, &error));
        QVERIFY(seed.appendToService(livePlan, alpha.id, &error));
        QVERIFY(seed.appendToService(otherPlan, beta.id, &error));
        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *plans = control<QComboBox>(window, "savedServices");
        plans->setCurrentIndex(plans->findData(livePlan));
        auto *slides = control<QListWidget>(window, "slideWorkspace");
        auto *controller = control<PresentationController>(window, "presentationController");
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        QCOMPARE(controller->state().currentText(), QStringLiteral("First lyric"));
        QTest::keyClick(slides, Qt::Key_Right);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Second lyric"));
        QCOMPARE(controller->state().nextText(), QStringLiteral("Final lyric"));
        QCOMPARE(slides->currentRow(), 1);
        auto *search = control<QLineEdit>(window, "songSearch");
        search->setFocus();
        QTRY_VERIFY(search->hasFocus());
        QTest::keyClicks(search, "First lyric");
        QCOMPARE(control<QListWidget>(window, "songLibrary")->count(), 1);
        QTest::keyClick(search, Qt::Key_Space);
        QTest::keyClick(search, Qt::Key_Right);
        QTest::keyClick(search, Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(controller->state().index, 1);
        QVERIFY(!controller->state().blackout);
        QTest::keyClick(search, Qt::Key_Escape);
        QTRY_VERIFY(slides->hasFocus());
        QTest::keyClick(slides, Qt::Key_B, Qt::ControlModifier);
        QVERIFY(controller->state().blackout);
        QTest::mouseClick(control<QPushButton>(window, "restoreAudience"), Qt::LeftButton);
        QVERIFY(!controller->state().blackout);
        plans->setCurrentIndex(plans->findData(otherPlan));
        QTest::mouseClick(control<QPushButton>(window, "nextSlide"), Qt::LeftButton);
        QCOMPARE(controller->state().serviceId, livePlan);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Final lyric"));
        QCOMPARE(plans->currentData().toInt(), otherPlan);
        QTest::mouseClick(control<QPushButton>(window, "nextSlide"), Qt::LeftButton);
        QCOMPARE(controller->state().currentText(), QStringLiteral("First lyric"));
        QTest::mouseClick(control<QPushButton>(window, "previousSlide"), Qt::LeftButton);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Final lyric"));
        plans->setCurrentIndex(plans->findData(livePlan));
        control<QComboBox>(window, "operatorMode")->setCurrentIndex(1);
        QTest::keyClick(slides, Qt::Key_Left);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Final lyric"));
        control<QComboBox>(window, "operatorMode")->setCurrentIndex(0);
        // Show mode is single-click; the live identity follows the clicked entry.
        control<QListWidget>(window, "serviceOrder")->setCurrentRow(0);
        QTest::mouseClick(slides->viewport(), Qt::LeftButton, Qt::NoModifier, slides->visualItemRect(slides->item(0)).center());
        QCOMPARE(controller->state().currentText(), QStringLiteral("First lyric"));
        QCOMPARE(controller->state().entryId, seed.service(livePlan).first().id);
        if (qEnvironmentVariableIsSet("WORSHIPCUE_TEST_SCREENSHOT"))
            QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_TEST_SCREENSHOT")));
        // Save edits through the real dialog while the original service copy runs.
        QTimer::singleShot(0, [] {
            auto *dialog = QApplication::activeModalWidget();
            Q_ASSERT(dialog);
            dialog->findChild<QLineEdit *>()->setText(QStringLiteral("Alpha revised"));
            dialog->findChild<QPlainTextEdit *>()->setPlainText(QStringLiteral("Revised first\n---\nRevised second"));
            qobject_cast<QDialog *>(dialog)->accept();
        });
        QTest::mouseClick(control<QPushButton>(window, "editSong"), Qt::LeftButton);
        QCOMPARE(controller->state().currentText(), QStringLiteral("First lyric"));
        QTest::mouseClick(control<QPushButton>(window, "nextSlide"), Qt::LeftButton);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Second lyric"));
        QCOMPARE(seed.song(alpha.id).slides.first(), QStringLiteral("Revised first"));
    }
};

QTEST_MAIN(OperatorWorkflowTest)
#include "operator_workflow_test.moc"
