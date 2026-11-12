#include "../src/ui/MainWindow.h"
#include "../src/ui/ReorderListWidget.h"
#include "../src/ui/MediaImportDialog.h"
#include "../src/ui/PresentationSettingsDialog.h"
#include "../src/ui/SlideEditorDialog.h"
#include "../src/ui/BackgroundLibraryDialog.h"
#include <QAction>
#include <QFileInfo>
#include <QImage>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QTextDocument>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTabWidget>
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
    static QGraphicsTextItem *editorTextItem(SlideEditorDialog *dialog) {
        auto *view = dialog->findChild<QGraphicsView *>(QStringLiteral("slideTextCanvas"));
        if (!view) return nullptr;
        for (auto *item : view->scene()->items())
            if (auto *text = dynamic_cast<QGraphicsTextItem *>(item)) return text;
        return nullptr;
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

    void externalServiceOrderRefreshPreservesSelectionAndLiveOutput() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository external;
        QVERIFY2(external.open(&error, path), qPrintable(error));
        const int songId = external.songs(QStringLiteral("Sample Song")).first().id;
        const int planId = external.createService(QStringLiteral("Current service"), &error);
        QVERIFY(external.appendToService(planId, songId, &error));
        QVERIFY(external.appendToService(planId, songId, &error));
        const int entryId = external.service(planId).last().id;

        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        auto *plans = control<QComboBox>(window, "savedServices");
        auto *entries = control<QListWidget>(window, "serviceOrder");
        auto *slides = control<QListWidget>(window, "slideWorkspace");
        auto *songs = control<QListWidget>(window, "songLibrary");
        auto *controller = control<PresentationController>(window, "presentationController");
        plans->setCurrentIndex(plans->findData(planId));
        entries->setCurrentRow(1);
        slides->setCurrentRow(1);
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        controller->toggleBlackout();
        const auto live = controller->state();
        const int selectedSong = songs->currentItem()->data(Qt::UserRole).toInt();

        ServiceOrderItem welcome;
        welcome.title = QStringLiteral("AI Welcome");
        welcome.slides = {QStringLiteral("Welcome to Sunday worship")};
        ServiceOrderItem existing;
        existing.songId = songId;
        existing.slideOrder = {1, 0, 1};
        control<QTabWidget>(window, "sidebarTabs")->setCurrentIndex(1);
        plans->showPopup();
        QVERIFY(QApplication::activePopupWidget());
        const int generatedId = external.createServiceOrder(QStringLiteral("AI Sunday"), {welcome, existing}, &error);
        QVERIFY2(generatedId > 0, qPrintable(error));
        // Poll synchronously while the popup is observed open; native window activation
        // events can otherwise dismiss it during a timed wait on macOS.
        QVERIFY(QApplication::activePopupWidget());
        QVERIFY(QMetaObject::invokeMethod(control<QTimer>(window, "externalLibraryRefresh"), "timeout", Qt::DirectConnection));
        QVERIFY(QApplication::activePopupWidget());
        QVERIFY(plans->findData(generatedId) < 0);
        plans->hidePopup();
        QTRY_VERIFY_WITH_TIMEOUT(plans->findData(generatedId) >= 0, 5000);
        QCOMPARE(plans->currentData().toInt(), planId);
        QCOMPARE(entries->currentItem()->data(Qt::UserRole).toInt(), entryId);
        QCOMPARE(slides->currentRow(), 1);
        QCOMPARE(songs->currentItem()->data(Qt::UserRole).toInt(), selectedSong);
        bool welcomeVisible = false;
        for (int i = 0; i < songs->count(); ++i)
            if (songs->item(i)->text() == welcome.title) welcomeVisible = true;
        QVERIFY(welcomeVisible);
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        QCOMPARE(controller->state().slides, live.slides);
        QCOMPARE(controller->state().entryId, live.entryId);
        QCOMPARE(controller->state().index, live.index);
        QVERIFY(controller->state().blackout);

        plans->setCurrentIndex(plans->findData(generatedId));
        QCOMPARE(entries->count(), 2);
        QCOMPARE(entries->item(0)->data(Qt::UserRole + 2).toString(), welcome.title);
        QCOMPARE(slides->item(0)->data(Qt::UserRole + 1).toString(), welcome.slides.first());
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        QVERIFY(controller->state().blackout);
    }

    void mcpSetupUsesActualDatabasePath() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("custom service library.sqlite3"));
        QString error;
        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        QJsonObject config;
        QString status;
        bool foundDialog = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            foundDialog = dialog->objectName() == QStringLiteral("mcpSetupDialog");
            auto *editor = dialog->findChild<QPlainTextEdit *>(QStringLiteral("mcpConfiguration"));
            auto *label = dialog->findChild<QLabel *>(QStringLiteral("mcpSetupStatus"));
            if (editor) config = QJsonDocument::fromJson(editor->toPlainText().toUtf8()).object();
            if (label) status = label->text();
            if (qEnvironmentVariableIsSet("WORSHIPCUE_MCP_UI_SCREENSHOT"))
                dialog->grab().save(qEnvironmentVariable("WORSHIPCUE_MCP_UI_SCREENSHOT"));
            dialog->reject();
        });
        control<QAction>(window, "mcpSetupAction")->trigger();
        QVERIFY(foundDialog);
        QVERIFY(!status.isEmpty());
        const auto server = config.value(QStringLiteral("mcpServers")).toObject().value(QStringLiteral("worshipcue")).toObject();
        const QString executable = server.value(QStringLiteral("command")).toString();
        QVERIFY(QFileInfo(executable).isAbsolute());
        QVERIFY(executable.contains(QStringLiteral("WorshipCueMcp")));
        QCOMPARE(server.value(QStringLiteral("args")).toArray(), QJsonArray({QStringLiteral("--database"), path}));
    }

    void failedExternalRefreshKeepsLibraryAndRetriesSameVersion() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository external;
        QVERIFY2(external.open(&error, path), qPrintable(error));
        const auto previousConnections = QSqlDatabase::connectionNames();
        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        QString appConnection;
        for (const auto &name : QSqlDatabase::connectionNames())
            if (!previousConnections.contains(name)) appConnection = name;
        QVERIFY(!appConnection.isEmpty());
        auto *plans = control<QComboBox>(window, "savedServices");
        auto *songs = control<QListWidget>(window, "songLibrary");
        const int songCount = songs->count();
        const int selectedSong = songs->currentItem()->data(Qt::UserRole).toInt();
        const int newService = external.createService(QStringLiteral("Refresh recovery"), &error);
        QVERIFY2(newService > 0, qPrintable(error));

        // Make the next library read fail after an external commit. Repair through the
        // app's own connection so its data_version does not change a second time.
        QString externalConnection;
        for (const auto &name : previousConnections) {
            if (QSqlDatabase::database(name).databaseName() == path) externalConnection = name;
        }
        QVERIFY(!externalConnection.isEmpty());
        {
            QSqlQuery query(QSqlDatabase::database(externalConnection));
            QVERIFY(query.exec(QStringLiteral("ALTER TABLE songs RENAME TO songs_temporarily_unavailable")));
        }
        QTRY_VERIFY_WITH_TIMEOUT(window.statusBar()->currentMessage().contains(QStringLiteral("Retrying automatically")), 5000);
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(songs->count(), songCount);
        QCOMPARE(songs->currentItem()->data(Qt::UserRole).toInt(), selectedSong);
        QVERIFY(plans->findData(newService) < 0);
        {
            QSqlQuery query(QSqlDatabase::database(appConnection));
            QVERIFY(query.exec(QStringLiteral("PRAGMA data_version")));
            QVERIFY(query.next());
            const qint64 version = query.value(0).toLongLong();
            query.finish();
            QVERIFY(query.exec(QStringLiteral("ALTER TABLE songs_temporarily_unavailable RENAME TO songs")));
            QVERIFY(query.exec(QStringLiteral("PRAGMA data_version")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toLongLong(), version);
        }
        QTRY_VERIFY_WITH_TIMEOUT(plans->findData(newService) >= 0, 5000);
        QCOMPARE(songs->count(), songCount);
        QCOMPARE(songs->currentItem()->data(Qt::UserRole).toInt(), selectedSong);
    }

    void richSlideEditingUsesSourceIndexAndKeepsLiveCopy() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        const int songId = observer.songs(QStringLiteral("Sample Song")).first().id;
        const int planId = observer.createService(QStringLiteral("Rich editing"), &error);
        QVERIFY(observer.appendToService(planId, songId, &error));
        const int entryId = observer.service(planId).first().id;
        QVERIFY(observer.setEntrySlideOrder(entryId, {2, 0, 2}, &error));
        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        auto *plans = control<QComboBox>(window, "savedServices");
        plans->setCurrentIndex(plans->findData(planId));
        auto *slides = control<QListWidget>(window, "slideWorkspace");
        auto *controller = control<PresentationController>(window, "presentationController");
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        const LivePresentation original = controller->state();
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<SlideEditorDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog && dialog->selectedSlideIndex() == 2);
            auto *text = editorTextItem(dialog);
            Q_ASSERT(text);
            text->setHtml(QStringLiteral("<p align='center'><span style='font-size:96px;color:#ffcc00;font-weight:700'>Revised last slide</span></p>"));
            if (qEnvironmentVariableIsSet("WORSHIPCUE_RICH_EDITOR_SCREENSHOT"))
                dialog->grab().save(qEnvironmentVariable("WORSHIPCUE_RICH_EDITOR_SCREENSHOT"));
            auto *actions = dialog->findChild<QDialogButtonBox *>(QStringLiteral("slideEditorActions"));
            QTest::mouseClick(actions->button(QDialogButtonBox::Save), Qt::LeftButton);
        });
        QTest::mouseClick(control<QPushButton>(window, "editSlideText"), Qt::LeftButton);
        const auto saved = observer.song(songId);
        QCOMPARE(saved.slides.at(2), QStringLiteral("Revised last slide"));
        QVERIFY(saved.richText.at(2).contains(QStringLiteral("#ffcc00")));
        QCOMPARE(observer.service(planId).first().slideOrder, QVector<int>({2, 0, 2}));
        QCOMPARE(slides->item(0)->data(Qt::UserRole + 1).toString(), QStringLiteral("Revised last slide"));
        QCOMPARE(slides->item(2)->data(Qt::UserRole + 4).toString(), saved.richText.at(2));
        QCOMPARE(controller->state().slides, original.slides);
        QCOMPARE(controller->state().richText, original.richText);
        QCOMPARE(controller->state().cueRevision, original.cueRevision);
        QVERIFY(!slides->item(0)->text().contains(QStringLiteral("LIVE")));
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        QCOMPARE(controller->state().currentRichText(), saved.richText.at(2));
        QCOMPARE(controller->state().currentText(), QStringLiteral("Revised last slide"));
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<SlideEditorDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            editorTextItem(dialog)->setPlainText(QStringLiteral("Discard this"));
            dialog->reject();
        });
        QTest::mouseClick(control<QPushButton>(window, "editSlideText"), Qt::LeftButton);
        QCOMPARE(observer.song(songId).richText, saved.richText);
        QVERIFY(!observer.songs(QStringLiteral("Revised last slide")).isEmpty());
    }

    void visibleGlobalTransitionsApplyToNextCueAndPersist() {
        QTemporaryDir temp;
        QString error;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        {
            MainWindow window;
            QVERIFY2(window.initialize(&error, path), qPrintable(error));
            window.show();
            auto *type = control<QComboBox>(window, "globalTransitionType");
            auto *duration = control<QDoubleSpinBox>(window, "globalTransitionTime");
            QVERIFY(type->isVisible());
            auto *controller = control<PresentationController>(window, "presentationController");
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            const auto revision = controller->state().cueRevision;
            type->setCurrentIndex(type->findData(int(SlideTransitionType::Fade)));
            duration->setValue(.75);
            QCOMPARE(controller->state().cueRevision, revision);
            QCOMPARE(controller->state().settings.transition, SlideTransitionType::Cut);
            QTest::mouseClick(control<QPushButton>(window, "nextSlide"), Qt::LeftButton);
            QCOMPARE(controller->state().settings.transition, SlideTransitionType::Fade);
            QCOMPARE(controller->state().settings.transitionDurationMs, 750);
            QVERIFY(control<QLabel>(window, "selectedTimingInfo")->text().contains(QStringLiteral("global default")));
            window.close();
        }
        MainWindow reopened;
        QVERIFY2(reopened.initialize(&error, path), qPrintable(error));
        QCOMPARE(control<QComboBox>(reopened, "globalTransitionType")->currentData().toInt(), int(SlideTransitionType::Fade));
        QCOMPARE(control<QDoubleSpinBox>(reopened, "globalTransitionTime")->value(), .75);
    }

    void slideViewsKeepLiveSnapshotAndAllowTextViewArrangement() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        QVERIFY(observer.createSong(QStringLiteral("Readable song"),
            {QStringLiteral("First verse\nA second line"), QStringLiteral("Second verse"), QStringLiteral("Closing chorus")}, &error));
        const int songId = observer.songs(QStringLiteral("Readable song")).first().id;
        const int planId = observer.createService(QStringLiteral("Readable service"), &error);
        QVERIFY(observer.appendToService(planId, songId, &error));
        const int entryId = observer.service(planId).first().id;

        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *plans = control<QComboBox>(window, "savedServices");
        plans->setCurrentIndex(plans->findData(planId));
        auto *slides = control<ReorderListWidget>(window, "slideWorkspace");
        auto *view = control<QComboBox>(window, "slideViewMode");
        auto *size = control<QSlider>(window, "slideSize");
        auto *controller = control<PresentationController>(window, "presentationController");
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        QVERIFY(controller->setSlide(1));
        controller->toggleBlackout();
        const LivePresentation live = controller->state();
        const auto originalOrder = observer.service(planId).first().slideOrder;

        view->setCurrentIndex(1);
        QCOMPARE(slides->viewMode(), QListView::ListMode);
        const QSize smallHint = slides->sizeHintForIndex(slides->model()->index(0, 0));
        size->setValue(size->maximum());
        const QSize largeHint = slides->sizeHintForIndex(slides->model()->index(0, 0));
        QVERIFY(largeHint.height() >= smallHint.height());
        view->setCurrentIndex(0);
        const QSize largeIcon = slides->iconSize();
        // Enlarging the grid must enlarge the rendered thumbnail, not only its cell.
        QVERIFY(slides->item(0)->icon().availableSizes().contains(largeIcon));
        QCOMPARE(slides->item(0)->icon().actualSize(largeIcon), largeIcon);
        size->setValue(size->minimum());
        QVERIFY(slides->iconSize().width() < largeIcon.width());
        view->setCurrentIndex(1);
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        QCOMPARE(controller->state().slides, live.slides);
        QCOMPARE(controller->state().index, live.index);
        QVERIFY(controller->state().blackout);
        QCOMPARE(observer.service(planId).first().slideOrder, originalOrder);

        control<QComboBox>(window, "operatorMode")->setCurrentIndex(1);
        QVERIFY(slides->moveItem(0, 2));
        QCOMPARE(observer.service(planId).first().slideOrder, QVector<int>({1, 2, 0}));
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        QCOMPARE(controller->state().slides, live.slides);
        QVERIFY(controller->state().blackout);
        slides->scrollToItem(slides->item(0));
        QTest::mouseClick(slides->viewport(), Qt::LeftButton, Qt::NoModifier,
            slides->visualItemRect(slides->item(0)).center());
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        control<QComboBox>(window, "operatorMode")->setCurrentIndex(0);
        QTest::mouseClick(slides->viewport(), Qt::LeftButton, Qt::NoModifier,
            slides->visualItemRect(slides->item(0)).center());
        QCOMPARE(controller->state().entryId, entryId);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Second verse"));
        QCOMPARE(controller->state().slides, observer.entrySlides(entryId));
        QVERIFY(controller->state().cueRevision > live.cueRevision);
        QVERIFY(!controller->state().blackout);
        if (qEnvironmentVariableIsSet("WORSHIPCUE_TEXT_UI_SCREENSHOT")) {
            QCoreApplication::processEvents();
            QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_TEXT_UI_SCREENSHOT")));
        }
    }

    void revealLiveCueReturnsToLiveServiceWithoutChangingOutput() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        QVERIFY(observer.createSong(QStringLiteral("Live song"),
            {QStringLiteral("Live first"), QStringLiteral("Live second")}, &error));
        const int songId = observer.songs(QStringLiteral("Live song")).first().id;
        const int livePlan = observer.createService(QStringLiteral("Current service"), &error);
        const int otherPlan = observer.createService(QStringLiteral("Future service"), &error);
        QVERIFY(observer.appendToService(livePlan, songId, &error));
        QVERIFY(observer.appendToService(livePlan, songId, &error));
        QVERIFY(observer.appendToService(otherPlan, songId, &error));
        const int liveEntry = observer.service(livePlan).last().id;

        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        auto *reveal = control<QPushButton>(window, "revealLiveCue");
        QVERIFY(!reveal->isEnabled());
        auto *plans = control<QComboBox>(window, "savedServices");
        auto *entries = control<QListWidget>(window, "serviceOrder");
        auto *slides = control<ReorderListWidget>(window, "slideWorkspace");
        auto *controller = control<PresentationController>(window, "presentationController");
        plans->setCurrentIndex(plans->findData(livePlan));
        entries->setCurrentRow(1);
        slides->setCurrentRow(1);
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        controller->toggleBlackout();
        const LivePresentation live = controller->state();
        plans->setCurrentIndex(plans->findData(otherPlan));
        QVERIFY(reveal->isEnabled());
        QTest::mouseClick(reveal, Qt::LeftButton);
        QCOMPARE(plans->currentData().toInt(), livePlan);
        QCOMPARE(entries->currentItem()->data(Qt::UserRole).toInt(), liveEntry);
        QCOMPARE(slides->currentRow(), 1);
        QVERIFY(slides->currentItem()->text().contains(QStringLiteral("LIVE")));
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        QCOMPARE(controller->state().index, live.index);
        QVERIFY(controller->state().blackout);

        // A saved arrangement may change while its original copy is live.
        control<QComboBox>(window, "operatorMode")->setCurrentIndex(1);
        QVERIFY(slides->moveItem(0, 1));
        plans->setCurrentIndex(plans->findData(otherPlan));
        QTest::mouseClick(reveal, Qt::LeftButton);
        QCOMPARE(plans->currentData().toInt(), livePlan);
        QCOMPARE(entries->currentItem()->data(Qt::UserRole).toInt(), liveEntry);
        for (int i = 0; i < slides->count(); ++i)
            QVERIFY(!slides->item(i)->text().contains(QStringLiteral("LIVE")));
        QCOMPARE(controller->state().slides, live.slides);
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        QVERIFY(controller->state().blackout);
    }

    void standardFindShortcutSelectsLibraryQueryWithoutCueing() {
        QTemporaryDir temp;
        QString error;
        MainWindow window;
        QVERIFY2(window.initialize(&error, temp.filePath(QStringLiteral("library.sqlite3"))), qPrintable(error));
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        auto *controller = control<PresentationController>(window, "presentationController");
        const LivePresentation live = controller->state();
        auto *search = control<QLineEdit>(window, "songSearch");
        search->setText(QStringLiteral("a saved search"));
        auto *slides = control<QListWidget>(window, "slideWorkspace");
        slides->setFocus();
        QTRY_VERIFY(slides->hasFocus());
        auto *find = control<QAction>(window, "focusLibrarySearch");
        QVERIFY(!find->shortcuts().isEmpty());
        control<QTabWidget>(window, "sidebarTabs")->setCurrentIndex(1);
        QTest::keySequence(slides, QKeySequence(QKeySequence::Find));
        QTRY_VERIFY(search->hasFocus());
        QCOMPARE(control<QTabWidget>(window, "sidebarTabs")->currentIndex(), 0);
        QCOMPARE(search->text(), QStringLiteral("a saved search"));
        QCOMPARE(search->selectedText(), search->text());
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        QCOMPARE(controller->state().currentText(), live.currentText());
    }

    void compactWorkspaceKeepsShowControlsVisibleAndRestoresLayout() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        QList<int> panelSizes;
        {
            MainWindow window;
            QVERIFY2(window.initialize(&error, path), qPrintable(error));
            window.resize(1080, 680);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QCoreApplication::processEvents();
            QCOMPARE(window.size(), QSize(1080, 680));
            for (const char *name : {"blackout", "clearText", "restoreAudience", "nextSlide", "previousSlide", "revealLiveCue"}) {
                auto *widget = control<QPushButton>(window, name);
                QVERIFY2(widget->isVisible(), name);
                QVERIFY2(window.rect().contains(QRect(widget->mapTo(&window, QPoint()), widget->size())), name);
            }
            auto *livePreview = control<QWidget>(window, "livePreview");
            auto *blackout = control<QPushButton>(window, "blackout");
            QVERIFY(blackout->mapTo(&window, QPoint()).y() >= livePreview->mapTo(&window, QPoint(0, livePreview->height())).y());
            QVERIFY(control<QPushButton>(window, "pauseVideo")->isHidden());
            QVERIFY(control<QPushButton>(window, "pauseAutoAdvance")->isHidden());
            if (qEnvironmentVariableIsSet("WORSHIPCUE_UI_SCREENSHOT"))
                QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_UI_SCREENSHOT")));

            window.resize(1280, 800);
            control<QComboBox>(window, "slideViewMode")->setCurrentIndex(1);
            control<QSlider>(window, "slideSize")->setValue(272);
            control<QTabWidget>(window, "previewTabs")->setCurrentIndex(1);
            auto *panels = control<QSplitter>(window, "operatorSplitter");
            auto *sidebar = control<QTabWidget>(window, "sidebarTabs");
            auto *library = control<QListWidget>(window, "songLibrary");
            auto *playlist = control<QListWidget>(window, "serviceOrder");
            sidebar->setCurrentIndex(0);
            QVERIFY(library->isVisible());
            QVERIFY(!playlist->isVisible());
            const auto revision = control<PresentationController>(window, "presentationController")->state().cueRevision;
            sidebar->setCurrentIndex(1);
            QVERIFY(playlist->isVisible());
            QVERIFY(!library->isVisible());
            QCOMPARE(control<PresentationController>(window, "presentationController")->state().cueRevision, revision);
            panels->setSizes({310, 620, 330});
            QCoreApplication::processEvents();
            panelSizes = panels->sizes();
            window.close();
        }
        MainWindow reopened;
        QVERIFY2(reopened.initialize(&error, path), qPrintable(error));
        reopened.show();
        QVERIFY(QTest::qWaitForWindowExposed(&reopened));
        QCOMPARE(control<QComboBox>(reopened, "slideViewMode")->currentIndex(), 1);
        QCOMPARE(control<QSlider>(reopened, "slideSize")->value(), 272);
        QCOMPARE(control<QTabWidget>(reopened, "previewTabs")->currentIndex(), 1);
        const auto restoredPanels = control<QSplitter>(reopened, "operatorSplitter")->sizes();
        QCOMPARE(control<QTabWidget>(reopened, "sidebarTabs")->currentIndex(), 1);
        QCOMPARE(restoredPanels.size(), panelSizes.size());
        for (int i = 0; i < panelSizes.size(); ++i)
            QVERIFY(qAbs(restoredPanels.at(i) - panelSizes.at(i)) <= 5);
    }

    void compactWorkspaceKeepsTimedVideoControlsVisible() {
        QTemporaryDir temp;
        const QString fixture = QFINDTESTDATA("fixtures/media-colors.mov");
        QVERIFY(!fixture.isEmpty());
        QString error;
        MainWindow window;
        QVERIFY2(window.initialize(&error, temp.filePath(QStringLiteral("library.sqlite3"))), qPrintable(error));
        window.resize(1080, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = control<PresentationController>(window, "presentationController");
        const PresentationSettings timing{60000, true, SlideTransitionType::Cut, 0, false};
        const SlideMedia video{MediaKind::Video, fixture, QStringLiteral("Welcome video.mov"), true};
        controller->takeServiceLive(1, {{1, QStringLiteral("Welcome Video"), {QStringLiteral("")}, {video}, timing}}, 1);
        auto *playback = control<MediaPlayback>(window, "mediaPlayback");
        QTRY_VERIFY_WITH_TIMEOUT(!playback->frame().isNull() || !playback->errorString().isEmpty(), 12000);
        QVERIFY2(playback->errorString().isEmpty(), qPrintable(playback->errorString()));
        QCoreApplication::processEvents();

        QCOMPARE(window.size(), QSize(1080, 680));
        for (const char *name : {"blackout", "clearText", "restoreAudience", "previousSlide", "nextSlide",
                                "revealLiveCue", "pauseAutoAdvance", "pauseVideo", "restartVideo", "muteVideo",
                                "autoAdvanceStatus", "mediaStatus", "livePreview", "previewTabs"}) {
            auto *widget = control<QWidget>(window, name);
            QVERIFY2(widget->isVisible(), name);
            QVERIFY2(window.rect().contains(QRect(widget->mapTo(&window, QPoint()), widget->size())), name);
        }
        auto *tabs = control<QTabWidget>(window, "previewTabs");
        QVERIFY(tabs->currentWidget()->height() >= 124);
        auto *nextPreview = control<QWidget>(window, "nextPreview");
        QVERIFY(nextPreview->height() >= 124);
        QVERIFY(tabs->currentWidget()->rect().contains(QRect(nextPreview->mapTo(tabs->currentWidget(), QPoint()), nextPreview->size())));
        QTest::mouseClick(control<QPushButton>(window, "blackout"), Qt::LeftButton);
        QCoreApplication::processEvents();
        QCOMPARE(window.size(), QSize(1080, 680));
        QVERIFY(controller->state().blackout);
        auto *timingStatus = control<QLabel>(window, "autoAdvanceStatus");
        QVERIFY(timingStatus->text().contains(QStringLiteral("Audience hidden")));
        QVERIFY(window.rect().contains(QRect(timingStatus->mapTo(&window, QPoint()), timingStatus->size())));
        QVERIFY(tabs->currentWidget()->rect().contains(QRect(nextPreview->mapTo(tabs->currentWidget(), QPoint()), nextPreview->size())));
        if (qEnvironmentVariableIsSet("WORSHIPCUE_TIMED_VIDEO_UI_SCREENSHOT"))
            QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_TIMED_VIDEO_UI_SCREENSHOT")));
    }

    void presentationSettingsSaveCancelAndKeepLiveSnapshot() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        const int planId = observer.services().first().id;
        const int songId = observer.service(planId).first().songId;
        const int entryId = observer.service(planId).first().id;
        const PresentationSettings saved{1000, true, SlideTransitionType::Fade, 250, true};
        {
            MainWindow window;
            QVERIFY2(window.initialize(&error, path), qPrintable(error));
            window.show();
            control<QComboBox>(window, "savedServices")->setCurrentIndex(0);
            control<QListWidget>(window, "serviceOrder")->setCurrentRow(0);
            auto *library = control<QListWidget>(window, "songLibrary");
            for (int i = 0; i < library->count(); ++i)
                if (library->item(i)->data(Qt::UserRole).toInt() == songId) library->setCurrentRow(i);
            QVERIFY(control<QPushButton>(window, "presentationSettings")->isVisible());
            auto *settings = control<QAction>(window, "presentationSettingsAction");
            QVERIFY(settings->isEnabled());
            QTimer::singleShot(0, [] {
                auto *dialog = qobject_cast<PresentationSettingsDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                dialog->findChild<QCheckBox *>(QStringLiteral("autoAdvanceEnabled"))->setChecked(true);
                dialog->findChild<QDoubleSpinBox *>(QStringLiteral("autoAdvanceInterval"))->setValue(1.0);
                dialog->findChild<QCheckBox *>(QStringLiteral("loopPresentation"))->setChecked(true);
                dialog->findChild<QCheckBox *>(QStringLiteral("overrideProgramTransition"))->setChecked(true);
                dialog->findChild<QComboBox *>(QStringLiteral("slideTransitionType"))->setCurrentIndex(1);
                dialog->findChild<QDoubleSpinBox *>(QStringLiteral("slideTransitionDuration"))->setValue(.25);
                if (qEnvironmentVariableIsSet("WORSHIPCUE_TIMING_DIALOG_SCREENSHOT"))
                    dialog->grab().save(qEnvironmentVariable("WORSHIPCUE_TIMING_DIALOG_SCREENSHOT"));
                auto *buttons = dialog->findChild<QDialogButtonBox *>(QStringLiteral("presentationSettingsActions"));
                QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
            });
            settings->trigger();
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
            settings->trigger();
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
            settings->trigger();
            QCOMPARE(controller->state().settings, saved);
            QVERIFY(controller->autoAdvanceManuallyPaused());
            QCOMPARE(observer.song(songId).settings, (PresentationSettings{1500, true, SlideTransitionType::SlideLeft, 400, true}));
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
        QCOMPARE(controller->state().settings, (PresentationSettings{1500, true, SlideTransitionType::SlideLeft, 400, true}));
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

    void programTransitionsApplyToAllPresentationsWithExplicitOverrides() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        const auto entries = observer.service(observer.services().first().id);
        QVERIFY(entries.size() >= 2);
        const auto changeDefaults = [](MainWindow &window, int type, double duration, bool save) {
            QTimer::singleShot(0, [=] {
                auto *dialog = qobject_cast<PresentationSettingsDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                dialog->findChild<QComboBox *>(QStringLiteral("slideTransitionType"))->setCurrentIndex(type);
                dialog->findChild<QDoubleSpinBox *>(QStringLiteral("slideTransitionDuration"))->setValue(duration);
                auto *actions = dialog->findChild<QDialogButtonBox *>(QStringLiteral("presentationSettingsActions"));
                QTest::mouseClick(actions->button(save ? QDialogButtonBox::Save : QDialogButtonBox::Cancel), Qt::LeftButton);
            });
            window.findChild<QAction *>(QStringLiteral("programTransitionsAction"))->trigger();
        };
        {
            MainWindow window;
            QVERIFY2(window.initialize(&error, path), qPrintable(error));
            window.show();
            auto *controller = control<PresentationController>(window, "presentationController");
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            QCOMPARE(controller->state().settings.transition, SlideTransitionType::Cut);
            changeDefaults(window, 1, .8, true);
            // Saving preferences keeps the running snapshot intact.
            QCOMPARE(controller->state().settings.transition, SlideTransitionType::Cut);
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            QCOMPARE(controller->state().settings.transition, SlideTransitionType::Fade);
            QCOMPARE(controller->state().settings.transitionDurationMs, 800);
            control<QListWidget>(window, "serviceOrder")->setCurrentRow(1);
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            QCOMPARE(controller->state().settings.transition, SlideTransitionType::Fade);
            changeDefaults(window, 3, .2, false);
            QCOMPARE(QSettings().value(QStringLiteral("presentation/transitionType")).toInt(), 1);
            const PresentationSettings explicitCut{0, false, SlideTransitionType::Cut, 0, true};
            QVERIFY(observer.updatePresentationSettings(entries.at(1).songId, explicitCut, &error));
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            QCOMPARE(controller->state().settings, explicitCut);
            window.close();
        }
        MainWindow reopened;
        QVERIFY2(reopened.initialize(&error, path), qPrintable(error));
        control<QListWidget>(reopened, "serviceOrder")->setCurrentRow(0);
        QTest::mouseClick(control<QPushButton>(reopened, "takeLive"), Qt::LeftButton);
        const auto *controller = control<PresentationController>(reopened, "presentationController");
        QCOMPARE(controller->state().settings.transition, SlideTransitionType::Fade);
        QCOMPARE(controller->state().settings.transitionDurationMs, 800);
    }

    void backgroundAssignmentIsReusableAndKeepsLiveSnapshot() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        const QString picture = temp.filePath(QStringLiteral("Blue.jpg"));
        QImage image(192, 108, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(picture));
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        const int songId = observer.service(observer.services().first().id).first().songId;
        QString assigned;
        {
            MainWindow window;
            QVERIFY2(window.initialize(&error, path), qPrintable(error));
            window.show();
            auto *library = control<QListWidget>(window, "songLibrary");
            for (int i = 0; i < library->count(); ++i)
                if (library->item(i)->data(Qt::UserRole).toInt() == songId) library->setCurrentRow(i);
            auto *controller = control<PresentationController>(window, "presentationController");
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<BackgroundLibraryDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                QString importError;
                QVERIFY2(dialog->importFiles({picture}, &importError), qPrintable(importError));
                assigned = dialog->selectedPath();
                dialog->accept();
            });
            control<QAction>(window, "assignBackgroundAction")->trigger();
            QVERIFY(assigned.startsWith(QStringLiteral("backgrounds/")));
            QCOMPARE(observer.song(songId).backgroundPath, assigned);
            QVERIFY(controller->state().backgroundPath.isEmpty());
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            QCOMPARE(controller->state().backgroundPath, temp.filePath(assigned));
            QTimer::singleShot(0, [] {
                auto *dialog = qobject_cast<BackgroundLibraryDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                dialog->findChild<QListWidget *>(QStringLiteral("backgroundLibraryItems"))->setCurrentRow(0);
                dialog->reject();
            });
            control<QAction>(window, "assignBackgroundAction")->trigger();
            QCOMPARE(observer.song(songId).backgroundPath, assigned);
            if (qEnvironmentVariableIsSet("WORSHIPCUE_CORRECTIONS_SCREENSHOT"))
                QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_CORRECTIONS_SCREENSHOT")));
            window.close();
        }
        MainWindow reopened;
        QVERIFY2(reopened.initialize(&error, path), qPrintable(error));
        QTest::mouseClick(control<QPushButton>(reopened, "takeLive"), Qt::LeftButton);
        QCOMPARE(control<PresentationController>(reopened, "presentationController")->state().backgroundPath, temp.filePath(assigned));
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
            auto *dialog = qobject_cast<SlideEditorDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            dialog->findChild<QLineEdit *>(QStringLiteral("slideEditorTitle"))->setText(QStringLiteral("Alpha revised"));
            editorTextItem(dialog)->setPlainText(QStringLiteral("Revised first"));
            dialog->findChild<QListWidget *>(QStringLiteral("slideEditorSlides"))->setCurrentRow(1);
            editorTextItem(dialog)->setPlainText(QStringLiteral("Revised second"));
            dialog->accept();
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
