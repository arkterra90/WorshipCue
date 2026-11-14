#include "../src/ui/MainWindow.h"
#include "../src/ui/MidiDialogs.h"
#include "fixtures/FakeMidiBackend.h"
#include <QTableWidget>
#include "../src/ui/ReorderListWidget.h"
#include "../src/ui/MediaImportDialog.h"
#include "../src/ui/MediaAssetListWidget.h"
#include "../src/ui/PresentationSettingsDialog.h"
#include "../src/ui/SlideEditorDialog.h"
#include "../src/ui/PresentationLookDialog.h"
#include "../src/ui/SongStructureDialog.h"
#include "../src/ui/BackgroundLibraryDialog.h"
#include "../src/ui/DisplayWindow.h"
#include "../src/ui/OutputSettingsDialog.h"
#include "../src/ui/StageLayoutDialog.h"
#include "../src/core/BuiltInBackgrounds.h"
#include <QAction>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QTextDocument>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
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

    void midiInputsResolveStableSavedTargetsAndCueOutputsOnce() {
        QTemporaryDir dir;const auto path=dir.filePath("midi.sqlite");QString error;LibraryRepository repo;QVERIFY(repo.open(&error,path));
        const int song=repo.createTextPresentation("MIDI Song",{"First","Second"},&error);const auto page=repo.song(song).slideIds[0];const int service=repo.createService("MIDI Service",&error);QVERIFY(repo.appendToService(service,song,&error));const int entry=repo.service(service)[0].id;QVERIFY(repo.setEntrySlideOrder(entry,{1,0,0},&error));
        MidiOutputAction output;output.destination="out-a";output.message.number=64;QVERIFY(repo.setMidiActions(MidiOwnerType::Slide,page,{},{output},&error));
        MidiBinding take;take.name="Take repeated first page";take.command=MidiCommand::TakeSlide;take.songId=song;take.slideId=page;take.entryId=entry;take.occurrence=1;QVERIFY(repo.saveMidiBinding(take,&error));
        MidiBinding blackout;blackout.name="Blackout";blackout.command=MidiCommand::Blackout;blackout.trigger.number=61;QVERIFY(repo.saveMidiBinding(blackout,&error));
        auto backend=std::make_unique<FakeMidiBackend>();auto *fake=backend.get();MainWindow window(nullptr,std::move(backend));QVERIFY(window.initialize(&error,path));window.show();QTest::qWait(40);
        auto *engine=control<MidiEngine>(window,"midiEngine");auto *controller=control<PresentationController>(window,"presentationController");MidiSettings settings;settings.enabled=true;settings.autoReconnect=false;settings.echoWindowMs=0;settings.minimumIntervalMs=0;settings.inputs={{"in-a",true,0}};settings.outputs={{"out-a",true,1}};QVERIFY(engine->setSettings(settings,&error,false));
        fake->inject(QByteArray::fromHex("903c7f"));QTRY_COMPARE(controller->state().entryId,entry);QCOMPARE(controller->state().index,2);QCOMPARE(controller->state().currentText(),QString("First"));QCOMPARE(fake->sent.size(),1);
        const auto revision=controller->state().cueRevision;fake->inject(QByteArray::fromHex("903c7f903d7f"));QTRY_VERIFY(controller->state().blackout);QCOMPARE(controller->state().cueRevision,revision);QCOMPARE(fake->sent.size(),1);
        controller->restoreOutput();QCOMPARE(fake->sent.size(),1);QVERIFY(repo.setEntrySlideOrder(entry,{0,0,1},&error));fake->inject(QByteArray::fromHex("803c00903c7f"));QTRY_COMPARE(controller->state().index,1);QCOMPARE(fake->sent.size(),2);
        // A changed saved action applies on the next take, not during snapshot navigation.
        auto changed=output;changed.message.number=65;QVERIFY(repo.setMidiActions(MidiOwnerType::Slide,page,{output},{changed},&error));QVERIFY(controller->previous());QCOMPARE(fake->sent.last().second,QByteArray::fromHex("90407f"));
        fake->inject(QByteArray::fromHex("803c00903c7f"));QTRY_COMPARE(fake->sent.last().second,QByteArray::fromHex("90417f"));
        auto *mode=control<QComboBox>(window,"operatorMode");mode->setCurrentIndex(1);const auto held=controller->state().cueRevision;fake->inject(QByteArray::fromHex("803c00903c7f"));QTest::qWait(20);QCOMPARE(controller->state().cueRevision,held);mode->setCurrentIndex(0);
        const auto saved=repo.midiBindings()[0];QVERIFY(repo.deleteMidiBinding(saved.id,&error));fake->inject(QByteArray::fromHex("803c00903c7f"));QTest::qWait(20);QCOMPARE(controller->state().cueRevision,held);
    }
    void midiPresentationSelectionAndGlobalControlsUseSavedArrangements() {
        QTemporaryDir dir;const auto path=dir.filePath("midi-commands.sqlite");QString error;LibraryRepository repo;QVERIFY(repo.open(&error,path));
        auto structure=SongStructure::fromSlides(2);structure.arrangements[0].sectionIds={structure.sections[1].id,structure.sections[0].id};const int song=repo.createTextPresentation("MIDI library target",{"Source first","Arranged first"},&error,{}, {},{},structure);
        QVector<MidiCommand> commands{MidiCommand::SelectPresentation,MidiCommand::TakePresentation,MidiCommand::Next,MidiCommand::Previous,MidiCommand::Blackout,MidiCommand::ClearText,MidiCommand::Restore};
        for(int i=0;i<commands.size();++i){MidiBinding b;b.name=MidiBinding::commandName(commands[i]);b.command=commands[i];b.trigger.number=70+i;if(i<2)b.songId=song;QVERIFY(repo.saveMidiBinding(b,&error));}
        const int service=repo.createService("Selected MIDI service",&error);QVERIFY(repo.appendToService(service,song,&error));const int entry=repo.service(service)[0].id;QVERIFY(repo.setEntrySlideOrder(entry,{0},&error));MidiBinding select;select.name="Select saved service item";select.command=MidiCommand::SelectPresentation;select.songId=song;select.entryId=entry;select.trigger.number=77;QVERIFY(repo.saveMidiBinding(select,&error));
        QDir().mkpath(dir.filePath("media"));QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(dir.filePath("media/midi-test.png")));const auto assets=repo.registerMediaAssets({{MediaKind::Image,"media/midi-test.png","MIDI image",false}},&error);QCOMPARE(assets.size(),1);MidiBinding media;media.name="Take library media";media.command=MidiCommand::TakeSlide;media.assetId=assets[0];media.trigger.number=78;QVERIFY(repo.saveMidiBinding(media,&error));
        auto backend=std::make_unique<FakeMidiBackend>();auto *fake=backend.get();MainWindow window(nullptr,std::move(backend));QVERIFY(window.initialize(&error,path));window.show();QTest::qWait(30);auto *engine=control<MidiEngine>(window,"midiEngine");auto *controller=control<PresentationController>(window,"presentationController");MidiSettings settings;settings.enabled=true;settings.autoReconnect=false;settings.minimumIntervalMs=0;settings.inputs={{"in-a",true,0}};QVERIFY(engine->setSettings(settings,&error,false));
        auto send=[&](int note){MidiMessage m;m.number=note;fake->inject(m.bytes());QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();};
        const auto revision=controller->state().cueRevision;send(70);QCOMPARE(controller->state().cueRevision,revision);QCOMPARE(control<QListWidget>(window,"slideWorkspace")->count(),2);send(71);QCOMPARE(controller->state().currentText(),QString("Arranged first"));QCOMPARE(controller->state().serviceId,0);send(72);QCOMPARE(controller->state().currentText(),QString("Source first"));send(73);QCOMPARE(controller->state().currentText(),QString("Arranged first"));send(74);QVERIFY(controller->state().blackout);send(75);QVERIFY(controller->state().clear);send(76);QVERIFY(!controller->state().blackout);QVERIFY(!controller->state().clear);
        const auto beforeSelect=controller->state().cueRevision;send(77);QCOMPARE(controller->state().cueRevision,beforeSelect);QCOMPARE(control<QListWidget>(window,"slideWorkspace")->count(),1);QCOMPARE(control<QTabWidget>(window,"sidebarTabs")->currentIndex(),1);
        send(78);QCOMPARE(controller->state().currentMedia().kind,MediaKind::Image);QCOMPARE(controller->state().title,QString("MIDI image"));QCOMPARE(controller->state().serviceId,0);
    }
    void midiSettingsSlideActionsLearningAndConflictUseOperatorDialogs() {
        QTemporaryDir dir;const auto path=dir.filePath("midi-ui.sqlite");QString error;auto backend=std::make_unique<FakeMidiBackend>();auto *fake=backend.get();MainWindow window(nullptr,std::move(backend));QVERIFY(window.initialize(&error,path));window.show();QTest::qWait(40);
        auto *engine=control<MidiEngine>(window,"midiEngine");
        QTimer::singleShot(0,[&]{auto *d=QApplication::activeModalWidget();QVERIFY(d);QCOMPARE(d->objectName(),QString("midiSettingsDialog"));d->findChild<QCheckBox*>("midiEnabled")->setChecked(true);auto *table=d->findChild<QTableWidget*>("midiDevicesTable");QCOMPARE(table->rowCount(),2);for(int r=0;r<2;++r)qobject_cast<QCheckBox*>(table->cellWidget(r,0))->setChecked(true);d->findChild<QPushButton*>("midiApply")->click();QVERIFY(engine->settings().enabled);QCOMPARE(fake->inputs.size(),1);if(qEnvironmentVariableIsSet("WORSHIPCUE_MIDI_SCREENSHOT"))QVERIFY(d->grab().save(qEnvironmentVariable("WORSHIPCUE_MIDI_SCREENSHOT")));qobject_cast<QDialog*>(d)->accept();});
        control<QAction>(window,"midiSettingsAction")->trigger();
        auto *slides=control<QListWidget>(window,"slideWorkspace");slides->setCurrentRow(0);
        QTimer::singleShot(0,[&]{auto *d=QApplication::activeModalWidget();QVERIFY(d);QCOMPARE(d->objectName(),QString("midiSlideDialog"));auto *add=d->findChild<QPushButton*>("midiAddAction");add->click();add->click();auto *table=d->findChild<QTableWidget*>("midiActionsTable");QCOMPARE(table->rowCount(),2);qobject_cast<QSpinBox*>(table->cellWidget(0,4))->setValue(70);qobject_cast<QComboBox*>(table->cellWidget(1,3))->setCurrentIndex(2);qobject_cast<QSpinBox*>(table->cellWidget(1,4))->setValue(10);qobject_cast<QSpinBox*>(table->cellWidget(1,5))->setValue(100);d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();});
        control<QPushButton>(window,"configureSlideMidi")->click();
        auto *controller=control<PresentationController>(window,"presentationController");control<QPushButton>(window,"takeLive")->click();QCOMPARE(controller->state().currentMidiActions().size(),2);QCOMPARE(fake->sent.size(),2);QCOMPARE(fake->sent[0].second,QByteArray::fromHex("90467f"));QCOMPARE(fake->sent[1].second,QByteArray::fromHex("b00a64"));
        LibraryRepository repo;QVERIFY(repo.open(&error,path));
        QTimer::singleShot(0,[&]{auto *d=QApplication::activeModalWidget();QVERIFY(d);d->findChild<QLineEdit*>("midiBindingName")->setText("Learned next");d->findChild<QPushButton*>("midiLearn")->click();fake->inject(QByteArray::fromHex("923f64"));QTimer::singleShot(20,d,[&,d]{QCOMPARE(d->findChild<QSpinBox*>("midiTriggerChannel")->value(),3);QCOMPARE(d->findChild<QSpinBox*>("midiTriggerNumber")->value(),63);d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();});});
        QVERIFY(editMidiBinding(&window,*engine,repo));QCOMPARE(repo.midiBindings().size(),1);
        QTimer::singleShot(0,[&]{auto *d=QApplication::activeModalWidget();d->findChild<QLineEdit*>("midiBindingName")->setText("Conflicting mapping");d->findChild<QSpinBox*>("midiTriggerNumber")->setValue(63);d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();QVERIFY(d->findChild<QLabel*>("midiBindingError")->text().contains("conflict",Qt::CaseInsensitive));qobject_cast<QDialog*>(d)->reject();});
        QVERIFY(!editMidiBinding(&window,*engine,repo));QCOMPARE(repo.midiBindings().size(),1);
    }

    void cleanupUsageFollowsCuesAndProtectsTheWholeLiveSnapshot() {
        QTemporaryDir dir;const auto path=dir.filePath("cleanup-usage.sqlite");QString error;LibraryRepository repo;QVERIFY(repo.open(&error,path));
        QDir().mkpath(dir.filePath("media"));QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(dir.filePath("media/future.png")));
        const int first=repo.createTextPresentation("Usage first",{"Current"},&error);const int future=repo.createPresentation("Usage future",{""},{{MediaKind::Image,"media/future.png","Future",false}},&error);const int asset=repo.mediaAssets()[0].id;const int service=repo.createService("Usage service",&error);QVERIFY(repo.appendToService(service,first,&error));QVERIFY(repo.appendToService(service,future,&error));
        const auto sqlName=QUuid::createUuid().toString();{auto db=QSqlDatabase::addDatabase("QSQLITE",sqlName);db.setDatabaseName(path);QVERIFY(db.open());QSqlQuery q(db);const auto old=QString::number(QDateTime::currentSecsSinceEpoch()-200*86400);for(const auto &table:{"content_usage","managed_files"})QVERIFY(q.exec("UPDATE "+QString(table)+" SET created_at="+old+",imported_at="+old+",modified_at="+old+",last_used_at="+old));}QSqlDatabase::removeDatabase(sqlName);
        QSettings().setValue("service/selected",service);MainWindow window;QVERIFY(window.initialize(&error,path));window.show();QTest::qWait(30);control<QListWidget>(window,"serviceOrder")->setCurrentRow(0);control<QPushButton>(window,"takeLive")->click();auto *controller=control<PresentationController>(window,"presentationController");QCOMPARE(controller->state().currentText(),QString("Current"));QVERIFY(repo.usageDates(CleanupKind::Presentation,first).lastUsedAt>QDateTime::currentSecsSinceEpoch()-60);const auto futureUse=repo.usageDates(CleanupKind::Presentation,future).lastUsedAt;QVERIFY(futureUse<QDateTime::currentSecsSinceEpoch()-86400);
        // Saved entries can disappear while the live service still holds its captured next item.
        QVERIFY(repo.deleteSong(future,&error));QVERIFY(repo.deleteMediaAsset(asset,&error));bool protectedFile=false;for(const auto &item:repo.cleanupCandidates(90))if(item.key.path=="media/future.png"){protectedFile=!item.eligible&&item.reason.contains("snapshot");}QVERIFY(protectedFile);
        QVERIFY(controller->next());QCOMPARE(controller->state().title,QString("Usage future"));QVERIFY(repo.usageDates(CleanupKind::File,0,"media/future.png").lastUsedAt>QDateTime::currentSecsSinceEpoch()-60);QVERIFY(QFileInfo::exists(dir.filePath("media/future.png")));
    }
    void libraryCleanupAndRecoveryRequireReviewedConfirmations() {
        QTemporaryDir dir;const auto path=dir.filePath("cleanup-ui.sqlite");QString error;LibraryRepository repo;QVERIFY(repo.open(&error,path));const int song=repo.createTextPresentation("Cleanup fixture",{"Old text"},&error);
        QDir().mkpath(dir.filePath("media"));QFile file(dir.filePath("media/cleanup.bin"));QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(QByteArray(4096,'x')),qint64(4096));QVERIFY(file.flush());QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-200),QFileDevice::FileModificationTime));file.close();const int asset=repo.registerMediaAssets({{MediaKind::Audio,"media/cleanup.bin","Cleanup media",false}},&error)[0];
        const auto sqlName=QUuid::createUuid().toString();{auto db=QSqlDatabase::addDatabase("QSQLITE",sqlName);db.setDatabaseName(path);QVERIFY(db.open());QSqlQuery q(db);const auto old=QString::number(QDateTime::currentSecsSinceEpoch()-200*86400);for(const auto &table:{"content_usage","managed_files"})QVERIFY(q.exec("UPDATE "+QString(table)+" SET created_at="+old+",imported_at="+old+",modified_at="+old+",last_used_at="+old));}QSqlDatabase::removeDatabase(sqlName);
        const int unknown=repo.createTextPresentation("Unknown usage",{"Keep"},&error);MainWindow window;QVERIFY(window.initialize(&error,path));window.show();QTest::qWait(30);
        auto *running=control<PresentationController>(window,"presentationController");PresentationSettings timed;timed.autoAdvanceMs=100;running->takeLive("Continuous output",{"First","Second"},0,timed);
        QTimer::singleShot(0,[&]{auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(d);QCOMPARE(d->objectName(),QString("libraryCleanupDialog"));QTimer::singleShot(15000,d,&QDialog::reject);auto *days=d->findChild<QSpinBox*>("cleanupDays");days->setValue(365);d->findChild<QPushButton*>("cleanupScan")->click();QTRY_VERIFY(d->findChild<QPushButton*>("cleanupScan")->isEnabled());d->findChild<QPushButton*>("cleanupSelectAll")->click();QTest::qWait(100);QVERIFY(!d->findChild<QPushButton*>("cleanupArchive")->isEnabled());days->setValue(90);d->findChild<QPushButton*>("cleanupScan")->click();QTRY_VERIFY(d->findChild<QPushButton*>("cleanupScan")->isEnabled());auto *table=d->findChild<QTableWidget*>("cleanupCandidates");int protectedCount=0;for(int r=0;r<table->rowCount();++r)if(!(table->item(r,0)->flags()&Qt::ItemIsUserCheckable))++protectedCount;QVERIFY(protectedCount>=3);QTRY_COMPARE(running->state().index,1);d->findChild<QPushButton*>("cleanupSelectAll")->click();QTest::qWait(110);QVERIFY(d->findChild<QLabel*>("cleanupSummary")->text().contains("2 selected"));QVERIFY(d->findChild<QPushButton*>("cleanupArchive")->isEnabled());
            if(qEnvironmentVariableIsSet("WORSHIPCUE_CLEANUP_SCREENSHOT"))QVERIFY(d->grab().save(qEnvironmentVariable("WORSHIPCUE_CLEANUP_SCREENSHOT")));
            auto cancelPreview=[&]{auto *p=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(p);p->reject();};
            QTimer::singleShot(0,cancelPreview);d->findChild<QPushButton*>("cleanupArchive")->click();QTRY_VERIFY(d->findChild<QPushButton*>("cleanupScan")->isEnabled());QCOMPARE(repo.song(song).id,song);QVERIFY(repo.cleanupBatches().isEmpty());
            auto confirmArchive=[&]{auto *p=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(p);QCOMPARE(p->objectName(),QString("cleanupArchivePreview"));QCOMPARE(p->findChild<QTableWidget*>("cleanupReviewItems")->rowCount(),2);p->findChild<QPushButton*>("confirmCleanup")->click();};
            QTimer::singleShot(0,confirmArchive);d->findChild<QPushButton*>("cleanupArchive")->click();QTRY_VERIFY(d->findChild<QPushButton*>("cleanupScan")->isEnabled());QCOMPARE(repo.song(song).id,0);QCOMPARE(repo.mediaAsset(asset).id,0);QVERIFY(QFileInfo::exists(file.fileName()));auto *batches=d->findChild<QTableWidget*>("cleanupBatches");QCOMPARE(batches->rowCount(),1);batches->setCurrentCell(0,0);d->findChild<QPushButton*>("cleanupRestore")->click();QTRY_VERIFY(d->findChild<QPushButton*>("cleanupScan")->isEnabled());QCOMPARE(repo.song(song).id,song);QCOMPARE(repo.mediaAsset(asset).id,asset);
            d->findChild<QTabWidget*>("cleanupTabs")->setCurrentIndex(0);d->findChild<QPushButton*>("cleanupSelectAll")->click();QTest::qWait(110);QTimer::singleShot(0,confirmArchive);d->findChild<QPushButton*>("cleanupArchive")->click();QTRY_VERIFY(d->findChild<QPushButton*>("cleanupScan")->isEnabled());batches->setCurrentCell(0,0);QTimer::singleShot(0,cancelPreview);d->findChild<QPushButton*>("cleanupPurge")->click();QVERIFY(QFileInfo::exists(file.fileName()));QCOMPARE(repo.cleanupBatches().size(),1);
            QTimer::singleShot(0,[&]{auto *p=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(p);QCOMPARE(p->objectName(),QString("cleanupPurgePreview"));QVERIFY(p->findChild<QLabel*>("cleanupReviewSummary")->text().contains("4"));if(qEnvironmentVariableIsSet("WORSHIPCUE_CLEANUP_REVIEW_SCREENSHOT"))QVERIFY(p->grab().save(qEnvironmentVariable("WORSHIPCUE_CLEANUP_REVIEW_SCREENSHOT")));p->findChild<QPushButton*>("confirmCleanup")->click();});d->findChild<QPushButton*>("cleanupPurge")->click();QVERIFY2(d->findChild<QLabel*>("cleanupError")->text().isEmpty(),qPrintable(d->findChild<QLabel*>("cleanupError")->text()));QVERIFY(!QFileInfo::exists(file.fileName()));QTRY_VERIFY(d->findChild<QPushButton*>("cleanupScan")->isEnabled());QCOMPARE(batches->rowCount(),0);QCOMPARE(repo.song(unknown).id,unknown);d->accept();});
        control<QAction>(window,"libraryCleanupAction")->trigger();QCOMPARE(repo.song(song).id,0);QCOMPARE(repo.mediaAsset(asset).id,0);QCOMPARE(repo.song(unknown).id,unknown);
    }

    void fourOutputSettingsAndStageLayoutsUseOperatorEntryPoints() {
        QTemporaryDir directory;
        MainWindow window; QString error;
        QVERIFY(window.initialize(&error, directory.filePath("outputs.sqlite3")));
        window.show();
        auto *outputs = window.findChild<OutputManager *>(); QVERIFY(outputs);
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        auto *controller = control<PresentationController>(window, "presentationController");
        const auto revision = controller->state().cueRevision;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<OutputSettingsDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto *tabs = dialog->findChild<QTabWidget *>("outputSettingsTabs"); QCOMPARE(tabs->count(), 4);
            tabs->setCurrentIndex(1);
            auto *display = dialog->findChild<QComboBox *>("outputDisplay1"); display->setCurrentIndex(display->findData("preview"));
            dialog->findChild<QSpinBox *>("outputWidth1")->setValue(1280);
            dialog->findChild<QSpinBox *>("outputHeight1")->setValue(1024);
            dialog->findChild<QSpinBox *>("outputAspectWidth1")->setValue(4);
            dialog->findChild<QSpinBox *>("outputAspectHeight1")->setValue(3);
            dialog->findChild<QComboBox *>("outputRotation1")->setCurrentIndex(1);
            dialog->findChild<QPushButton *>("saveOutput1")->click();
            QCOMPARE(outputs->configuration(OutputId::Audience2).canvas, QSize(1280,1024));
            QCOMPARE(outputs->configuration(OutputId::Audience2).rotation, 90);
            QCOMPARE(outputs->target(OutputId::Audience1), QStringLiteral("off"));
            tabs->setCurrentIndex(3);
            display = dialog->findChild<QComboBox *>("outputDisplay3"); display->setCurrentIndex(display->findData("preview"));
            dialog->findChild<QSpinBox *>("outputWidth3")->setValue(720);
            dialog->findChild<QSpinBox *>("outputHeight3")->setValue(1280);
            dialog->findChild<QSpinBox *>("outputAspectWidth3")->setValue(9);
            dialog->findChild<QSpinBox *>("outputAspectHeight3")->setValue(16);
            dialog->findChild<QPushButton *>("saveOutput3")->click();
            if (qEnvironmentVariableIsSet("WORSHIPCUE_OUTPUT_SETTINGS_SCREENSHOT"))
                QVERIFY(dialog->grab().save(qEnvironmentVariable("WORSHIPCUE_OUTPUT_SETTINGS_SCREENSHOT")));
            // Closing discards unsaved preview-only changes.
            dialog->findChild<QSpinBox *>("outputWidth3")->setValue(900);
            dialog->reject();
        });
        control<QAction>(window, "outputSettingsAction")->trigger();
        QCOMPARE(outputs->configuration(OutputId::Stage2).canvas, QSize(720,1280));
        QCOMPARE(controller->state().cueRevision, revision);
        QVERIFY(outputs->output(OutputId::Audience2).isVisible());
        QVERIFY(outputs->output(OutputId::Stage2).isVisible());
        QString layoutId;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<StageLayoutDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            dialog->findChild<QPushButton *>("copyStageLayout")->click();
            dialog->findChild<QLineEdit *>("stageLayoutName")->setText("Sunday stage");
            dialog->findChild<QLineEdit *>("stageLayoutBackground")->setText("#122536");
            dialog->findChild<QPushButton *>("addStageElement")->click();
            dialog->findChild<QComboBox *>("stageElementKind")->setCurrentIndex(int(StageElement::Kind::Message));
            dialog->findChild<QDoubleSpinBox *>("stageElementY")->setValue(10);
            dialog->findChild<QDoubleSpinBox *>("stageElementHeight")->setValue(10);
            dialog->findChild<QPushButton *>("addStageElement")->click();
            dialog->findChild<QComboBox *>("stageElementKind")->setCurrentIndex(int(StageElement::Kind::Countdown));
            dialog->findChild<QDoubleSpinBox *>("stageElementY")->setValue(94);
            dialog->findChild<QDoubleSpinBox *>("stageElementHeight")->setValue(10);
            dialog->findChild<QDoubleSpinBox *>("stageElementHeight")->setValue(5);
            dialog->findChild<QLineEdit *>("stageElementColor")->setText("#00ff00");
            dialog->findChild<QPushButton *>("saveStageLayout")->click();
            QCOMPARE(outputs->stageLayouts().size(), 2); layoutId = outputs->stageLayouts().last().id;
            QVERIFY(layoutId != "standard");
            QVERIFY(outputs->stageLayouts().last().elements.first().fontFamily.isEmpty());
            if (qEnvironmentVariableIsSet("WORSHIPCUE_STAGE_LAYOUT_SCREENSHOT"))
                QVERIFY(dialog->grab().save(qEnvironmentVariable("WORSHIPCUE_STAGE_LAYOUT_SCREENSHOT")));
            dialog->reject();
        });
        control<QAction>(window, "stageLayoutsAction")->trigger();
        QVERIFY(!layoutId.isEmpty());
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<OutputSettingsDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            dialog->findChild<QTabWidget *>("outputSettingsTabs")->setCurrentIndex(3);
            auto *layout = dialog->findChild<QComboBox *>("outputStageLayout3"); layout->setCurrentIndex(layout->findData(layoutId));
            dialog->findChild<QPushButton *>("saveOutput3")->click(); dialog->reject();
        });
        control<QAction>(window, "outputSettingsAction")->trigger();
        QCOMPARE(outputs->output(OutputId::Stage2).stageLayout().id, layoutId);
        QCOMPARE(outputs->stage().stageLayout().id, QStringLiteral("standard"));
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<StageControlsDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            dialog->findChild<QComboBox *>("stageControlOutput")->setCurrentIndex(1);
            dialog->findChild<QPlainTextEdit *>("stageMessage")->setPlainText("Wrap up after chorus");
            dialog->findChild<QPushButton *>("sendStageMessage")->click();
            dialog->findChild<QSpinBox *>("stageCountdownSeconds")->setValue(2);
            dialog->findChild<QPushButton *>("stageCountdownStart")->click();
            QVERIFY(outputs->output(OutputId::Stage2).stageRuntime().running);
            dialog->findChild<QPushButton *>("stageCountdownPause")->click();
            QVERIFY(!outputs->output(OutputId::Stage2).stageRuntime().running);
            dialog->reject();
        });
        control<QAction>(window, "stageControlsAction")->trigger();
        QCOMPARE(outputs->output(OutputId::Stage2).stageRuntime().message, QStringLiteral("Wrap up after chorus"));
        QVERIFY(outputs->stage().stageRuntime().message.isEmpty());
        QCOMPARE(controller->state().cueRevision, revision);
        QTest::keyClick(&outputs->output(OutputId::Audience2), Qt::Key_Down);
        QCOMPARE(controller->state().index, 1);
        QTest::keyClick(&outputs->output(OutputId::Stage2), Qt::Key_Up);
        QCOMPARE(controller->state().index, 0);
        window.close();
        for (auto id : outputIds) QVERIFY(!outputs->output(id).isVisible());
    }

    void bulkTransitionsSupportMixedSelectionUndoRedoAndIndividualOverride() {
        QTemporaryDir directory;
        const auto path = directory.filePath("library.sqlite3");
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        PresentationSettings a{5000, true, SlideTransitionType::Cut, 500, false};
        PresentationSettings b{0, false, SlideTransitionType::SlideRight, 1200, true};
        const int first = observer.createTextPresentation("First", {"One", "Two"}, &error, a);
        const int second = observer.createTextPresentation("Second", {"Three"}, &error, b);
        const int plan = observer.createServiceOrder("Bulk service", {{first, {}, {}, {}}, {second, {}, {}, {}}, {first, {}, {}, {1,0}}}, &error);
        const auto assets = observer.registerMediaAssets({{MediaKind::Image, "media/still.jpg", "Still", false}}, &error);
        const int mediaEntry = observer.appendMediaToService(plan, assets.first(), &error);
        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        auto *plans = control<QComboBox>(window, "savedServices");
        plans->setCurrentIndex(plans->findData(plan));
        control<QTabWidget>(window, "sidebarTabs")->setCurrentIndex(1);
        auto *list = control<ReorderListWidget>(window, "serviceOrder");
        QCOMPARE(list->selectionMode(), QAbstractItemView::ExtendedSelection);
        list->setCurrentRow(0);
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        auto *controller = control<PresentationController>(window, "presentationController");
        controller->setAutoAdvancePaused(true);
        const auto live = controller->state();
        const auto apply = [&](int type, double seconds, bool inherit, bool save) {
            QTimer::singleShot(0, [=] {
                auto *dialog = qobject_cast<PresentationSettingsDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                QCOMPARE(dialog->objectName(), QStringLiteral("bulkTransitionSettingsDialog"));
                QVERIFY(!dialog->findChild<QCheckBox *>("autoAdvanceEnabled")->isVisible());
                dialog->findChild<QComboBox *>("slideTransitionType")->setCurrentIndex(type);
                dialog->findChild<QDoubleSpinBox *>("slideTransitionDuration")->setValue(seconds);
                dialog->findChild<QCheckBox *>("overrideProgramTransition")->setChecked(!inherit);
                if (qEnvironmentVariableIsSet("WORSHIPCUE_BULK_TRANSITIONS_SCREENSHOT"))
                    QVERIFY(dialog->grab().save(qEnvironmentVariable("WORSHIPCUE_BULK_TRANSITIONS_SCREENSHOT")));
                if (save) dialog->accept(); else dialog->reject();
            });
            control<QAction>(window, "bulkTransitions")->trigger();
        };
        // Normal modifier selection and Select All both work in the playlist.
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, list->visualItemRect(list->item(1)).center());
        QCOMPARE(list->selectedItems().size(), 2);
        apply(1, .65, false, false);
        QCOMPARE(observer.song(first).settings, a);
        QVERIFY(!control<QAction>(window, "undoBulkTransitions")->isEnabled());
        control<QAction>(window, "selectAllServiceItems")->trigger();
        QCOMPARE(list->selectedItems().size(), 4);
        apply(1, .65, false, true);
        const auto checkFade = [&](const PresentationSettings &settings) {
            QCOMPARE(settings.transition, SlideTransitionType::Fade);
            QCOMPARE(settings.transitionDurationMs, 650);
            QVERIFY(settings.overrideTransition);
        };
        checkFade(observer.song(first).settings);
        checkFade(observer.song(second).settings);
        checkFade(observer.entrySource(mediaEntry).settings);
        QCOMPARE(observer.song(first).settings.autoAdvanceMs, 5000);
        QVERIFY(observer.song(first).settings.loop);
        QCOMPARE(list->selectedItems().size(), 4);
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            QCOMPARE(list->selectedItems().size(), 4);
            QVERIFY(menu->actions().contains(control<QAction>(window, "bulkTransitions")));
            menu->close();
        });
        list->customContextMenuRequested(list->visualItemRect(list->item(2)).center());
        QCOMPARE(list->selectedItems().size(), 4);
        if (qEnvironmentVariableIsSet("WORSHIPCUE_BULK_PLAYLIST_SCREENSHOT"))
            QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_BULK_PLAYLIST_SCREENSHOT")));
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        QCOMPARE(controller->state().entryId, live.entryId);
        QCOMPARE(controller->state().index, live.index);
        QCOMPARE(controller->state().settings, live.settings);
        list->setFocus();
        QTest::keySequence(list, QKeySequence::Undo);
        QCOMPARE(observer.song(first).settings, a);
        QCOMPARE(observer.song(second).settings, b);
        QVERIFY(!observer.entrySource(mediaEntry).settings.overrideTransition);
        QTest::keySequence(list, QKeySequence::Redo);
        checkFade(observer.song(second).settings);
        // Single selections can change a presentation after the bulk edit.
        list->setCurrentRow(1, QItemSelectionModel::ClearAndSelect);
        apply(2, .25, false, true);
        QCOMPARE(observer.song(second).settings.transition, SlideTransitionType::SlideLeft);
        checkFade(observer.song(first).settings);
        control<QAction>(window, "undoBulkTransitions")->trigger();
        checkFade(observer.song(second).settings);
        control<QAction>(window, "undoBulkTransitions")->trigger();
        QCOMPARE(observer.song(first).settings, a);
        QCOMPARE(observer.song(second).settings, b);
        // A new edit clears redo; inheritance is an explicit bulk operation too.
        control<QAction>(window, "selectAllServiceItems")->trigger();
        apply(0, 0, true, true);
        QVERIFY(!observer.song(second).settings.overrideTransition);
        QVERIFY(!control<QAction>(window, "redoBulkTransitions")->isEnabled());
        QCOMPARE(observer.service(plan).at(2).slideOrder, (QVector<int>{1,0}));
        // A single media entry can receive its own override.
        list->setCurrentRow(3, QItemSelectionModel::ClearAndSelect);
        apply(3, .4, false, true);
        QCOMPARE(observer.entrySource(mediaEntry).settings.transition, SlideTransitionType::SlideRight);
        QVERIFY(observer.entrySource(mediaEntry).settings.overrideTransition);
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
    }

    void looksUseTheNormalEditorPreviewAndLiveEntryPoints() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        PresentationLook blue;
        blue.fontSize = 72; blue.background = PresentationLook::Background::Color;
        blue.backgroundColor = QStringLiteral("#123456");
        const int lookId = observer.createLook(QStringLiteral("Blue service"), blue, &error);
        QVERIFY(lookId > 0);
        SongStructure structure;
        structure.sections = {{"v", "Verse", 1}, {"c", "Chorus", 1}};
        structure.arrangements = {{"full", "Full", {"v", "c", "c"}}}; structure.activeArrangementId = QStringLiteral("full");
        const int id = observer.createTextPresentation(QStringLiteral("Styled song"), {"Verse words", "Chorus words"}, &error, {}, {}, {"", ""}, structure);
        const int plan = observer.createService(QStringLiteral("Looks service"), &error);
        QVERIFY(observer.appendToService(plan, id, &error));
        MainWindow window;
        QVERIFY(window.initialize(&error, path));
        window.show();
        auto *plans = control<QComboBox>(window, "savedServices");
        plans->setCurrentIndex(plans->findData(plan));
        auto *slides = control<QListWidget>(window, "slideWorkspace");
        auto *controller = control<PresentationController>(window, "presentationController");
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        const auto revision = controller->state().cueRevision;
        QTimer::singleShot(0, [lookId] {
            auto *dialog = qobject_cast<PresentationLookDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            auto *combo = dialog->findChild<QComboBox *>(QStringLiteral("savedLooks"));
            combo->setCurrentIndex(combo->findData(lookId));
            dialog->findChild<QPushButton *>(QStringLiteral("applyLookToPresentation"))->click();
            dialog->findChild<QDialogButtonBox *>(QStringLiteral("lookDialogActions"))->button(QDialogButtonBox::Save)->click();
        });
        QTest::mouseClick(control<QPushButton>(window, "configureLooks"), Qt::LeftButton);
        QCOMPARE(observer.song(id).appearance.lookId, lookId);
        QCOMPARE(observer.song(id).slides, QStringList({"Verse words", "Chorus words"}));
        QCOMPARE(controller->state().cueRevision, revision);
        QCOMPARE(controller->state().currentLook().id, 0);
        const auto thumbnail = slides->item(0)->data(Qt::UserRole + 2).value<QPixmap>();
        QCOMPARE(thumbnail.toImage().pixelColor(10, 10), QColor(QStringLiteral("#123456")));
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        QCOMPARE(controller->state().currentLook().id, lookId);
        QCOMPARE(controller->state().nextLook().id, lookId);
        DisplayWindow audience(DisplayWindow::Kind::Audience, controller);
        QImage frame(1920, 1080, QImage::Format_ARGB32_Premultiplied);
        { QPainter painter(&frame); audience.paintAudience(painter, frame.rect()); }
        QCOMPARE(frame.pixelColor(10, 10), QColor(QStringLiteral("#123456")));
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<SlideEditorDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            dialog->findChild<QSpinBox *>(QStringLiteral("slideEditorFontSize"))->setValue(48);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        QTest::mouseClick(control<QPushButton>(window, "editSlideText"), Qt::LeftButton);
        QVERIFY(observer.song(id).appearance.richTextOverrides.first());
        QVERIFY(controller->state().currentLook().richTextOverrides);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Verse words"));
        PresentationLook imageLook = blue;
        imageLook.background = PresentationLook::Background::Image;
        imageLook.backgroundPath = BuiltInBackgrounds::catalog().first().path;
        const int imageId = observer.createLook(QStringLiteral("Image look"), imageLook, &error);
        QVERIFY(imageId > 0);
        slides->setCurrentRow(1);
        QTimer::singleShot(0, [imageId] {
            auto *dialog = qobject_cast<PresentationLookDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            QString error;
            if (!dialog->applyLook(imageId, {1}, false, PresentationLookDialog::OverflowHandling::Cancel, &error)) {
                dialog->reject();
                QFAIL(qPrintable(error));
            }
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        QTest::mouseClick(control<QPushButton>(window, "configureLooks"), Qt::LeftButton);
        QCOMPARE(observer.song(id).appearance.slideLookIds, QVector<int>({0, imageId}));
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        QCOMPARE(controller->state().currentBackgroundPath(), BuiltInBackgrounds::resourcePath(imageLook.backgroundPath));
        QCOMPARE(controller->state().nextBackgroundPath(), controller->state().currentBackgroundPath());
        QVERIFY(controller->next());
        QCOMPARE(controller->state().currentLook().id, imageId);
        if (qEnvironmentVariableIsSet("WORSHIPCUE_LOOK_OPERATOR_SCREENSHOT"))
            QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_LOOK_OPERATOR_SCREENSHOT")));
    }

    void keyboardNavigationAcrossArrangementsAndMedia_data() {
        QTest::addColumn<int>("key");
        QTest::addColumn<bool>("forward");
        QTest::newRow("space") << int(Qt::Key_Space) << true;
        QTest::newRow("right") << int(Qt::Key_Right) << true;
        QTest::newRow("down") << int(Qt::Key_Down) << true;
        QTest::newRow("left") << int(Qt::Key_Left) << false;
        QTest::newRow("up") << int(Qt::Key_Up) << false;
    }

    void keyboardNavigationAcrossArrangementsAndMedia() {
        QFETCH(int, key);
        QFETCH(bool, forward);
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository library;
        QVERIFY(library.open(&error, path));
        SongStructure structure;
        structure.sections = {{"a", "Verse", 1}, {"b", "Chorus", 1}};
        structure.arrangements = {{"full", "Full", {"b", "a", "b"}}}; structure.activeArrangementId = QStringLiteral("full");
        const int song = library.createTextPresentation(QStringLiteral("Arranged"), {"Verse", "Chorus"}, &error, {}, {}, {"", ""}, structure);
        const int end = library.createTextPresentation(QStringLiteral("Closing"), {"End"}, &error);
        const QString imagePath = directory.filePath(QStringLiteral("media/still.png"));
        QVERIFY(QDir().mkpath(QFileInfo(imagePath).absolutePath()));
        QImage image(32, 32, QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(imagePath));
        const auto media = library.registerMediaAssets({{MediaKind::Image, "media/still.png", "Still.png", false}}, &error);
        const int plan = library.createService(QStringLiteral("Keys"), &error);
        QVERIFY(library.appendToService(plan, song, &error));
        QVERIFY(library.appendMediaToService(plan, media.first(), &error));
        QVERIFY(library.appendToService(plan, end, &error));
        const auto entries = library.service(plan);
        MainWindow window;
        QVERIFY(window.initialize(&error, path));
        window.show(); window.raise(); window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *plans = control<QComboBox>(window, "savedServices");
        plans->setCurrentIndex(plans->findData(plan));
        auto *order = control<QListWidget>(window, "serviceOrder");
        auto *slides = control<QListWidget>(window, "slideWorkspace");
        order->setCurrentRow(forward ? 0 : 2);
        slides->setCurrentRow(forward ? 2 : 0);
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        auto *controller = control<PresentationController>(window, "presentationController");
        slides->setFocus();
        QTest::keyClick(slides, Qt::Key(key));
        QCOMPARE(controller->state().entryId, entries.at(1).id);
        QCOMPARE(controller->state().currentMedia().kind, MediaKind::Image);
        QTest::keyClick(slides, Qt::Key(key));
        QCOMPARE(controller->state().entryId, entries.at(forward ? 2 : 0).id);
        QCOMPARE(controller->state().index, forward ? 0 : 2);
        QCOMPARE(controller->state().currentText(), forward ? QStringLiteral("End") : QStringLiteral("Chorus"));
        const auto revision = controller->state().cueRevision;
        auto *search = control<QLineEdit>(window, "songSearch");
        search->setFocus(); QTRY_VERIFY(search->hasFocus());
        QTest::keyClick(search, Qt::Key(key));
        QCOMPARE(controller->state().cueRevision, revision);
        auto *time = control<QDoubleSpinBox>(window, "globalTransitionTime");
        // Enable the existing global duration editor, then verify its keys remain editor input.
        control<QComboBox>(window, "globalTransitionType")->setCurrentIndex(1);
        time->setFocus(); QTRY_VERIFY(time->hasFocus());
        QTest::keyClick(time, Qt::Key(key));
        QCOMPARE(controller->state().cueRevision, revision);
        control<QComboBox>(window, "operatorMode")->setCurrentIndex(1);
        slides->setFocus(); QTest::keyClick(slides, Qt::Key(key));
        QCOMPARE(controller->state().cueRevision, revision);
        QTimer::singleShot(0, [key, revision, controller] {
            auto *dialog = qobject_cast<SlideEditorDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            auto *canvas = dialog->findChild<QGraphicsView *>(QStringLiteral("slideTextCanvas"));
            canvas->setFocus();
            QTest::keyClick(canvas->viewport(), Qt::Key(key));
            QCOMPARE(controller->state().cueRevision, revision);
            dialog->reject();
        });
        QTest::mouseClick(control<QPushButton>(window, "editSlideText"), Qt::LeftButton);
        auto *outputs = window.findChild<OutputManager *>();
        QVERIFY(outputs);
        for (const auto kind : {DisplayWindow::Kind::Audience, DisplayWindow::Kind::Stage}) {
            window.raise(); window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            control<QComboBox>(window, "operatorMode")->setCurrentIndex(0);
            order->setCurrentRow(forward ? 0 : 2);
            slides->setCurrentRow(forward ? 2 : 0);
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            QVERIFY(outputs->setTarget(kind, QStringLiteral("preview"), &error));
            auto *output = kind == DisplayWindow::Kind::Audience ? &outputs->audience() : &outputs->stage();
            output->raise(); output->activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(output));
            QTest::keyClick(output, Qt::Key(key));
            QCOMPARE(controller->state().entryId, entries.at(1).id);
            QVERIFY(outputs->setTarget(kind, QStringLiteral("off"), &error));
        }
    }

    void structuredSongEditingRefreshesSharedLiveLyricsAndNavigation() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        SongStructure structure;
        structure.sections = {{"verse", "Verse 1", 1}, {"chorus", "Chorus", 2}};
        structure.arrangements = {{"full", "Full", {"verse", "chorus", "verse", "chorus", "chorus"}}};
        structure.activeArrangementId = QStringLiteral("full");
        const int id = observer.createTextPresentation(QStringLiteral("Shared song"), {"Verse", "Chorus first", "Chorus second"},
                                                       &error, {}, {}, {"", "", ""}, structure);
        QVERIFY(id > 0);
        const int plan = observer.createService(QStringLiteral("Structured service"), &error);
        QVERIFY(observer.appendToService(plan, id, &error));
        MainWindow window;
        QVERIFY(window.initialize(&error, path));
        window.show();
        auto *plans = control<QComboBox>(window, "savedServices");
        plans->setCurrentIndex(plans->findData(plan));
        auto *slides = control<QListWidget>(window, "slideWorkspace");
        QCOMPARE(slides->count(), 8);
        slides->setCurrentRow(1);
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        auto *controller = control<PresentationController>(window, "presentationController");
        QCOMPARE(controller->state().currentText(), QStringLiteral("Chorus first"));
        const auto revision = controller->state().cueRevision;
        QTimer::singleShot(0, [] {
            auto *editor = qobject_cast<SongStructureDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(editor);
            editor->findChild<QListWidget *>(QStringLiteral("songSections"))->setCurrentRow(1);
            editor->findChild<QPlainTextEdit *>(QStringLiteral("songSectionLyrics"))->setPlainText(QStringLiteral("Changed chorus"));
            editor->findChild<QListWidget *>(QStringLiteral("songSectionSlides"))->setCurrentRow(1);
            editor->findChild<QPlainTextEdit *>(QStringLiteral("songSectionLyrics"))->setPlainText(QStringLiteral("Second changed page"));
            editor->findChild<QDialogButtonBox *>(QStringLiteral("songStructureActions"))->button(QDialogButtonBox::Save)->click();
        });
        QTest::mouseClick(control<QPushButton>(window, "editSongStructure"), Qt::LeftButton);
        QCOMPARE(controller->state().cueRevision, revision);
        QCOMPARE(controller->state().index, 1);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Changed chorus"));
        QCOMPARE(controller->state().slides.count(QStringLiteral("Changed chorus")), 3);
        QCOMPARE(observer.song(id).slides.size(), 3);
        QCOMPARE(slides->item(1)->data(Qt::UserRole + 5).toString(), QStringLiteral("Chorus • Slide 1"));
        QCOMPARE(slides->item(2)->data(Qt::UserRole + 5).toString(), QStringLiteral("Chorus • Slide 2"));
        for (int row : {1, 4, 6}) QCOMPARE(slides->item(row)->data(Qt::UserRole + 1).toString(), QStringLiteral("Changed chorus"));
        slides->setFocus();
        QTest::keyClick(slides, Qt::Key_Down);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Second changed page"));
        QTest::keyClick(slides, Qt::Key_Down);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Verse"));
        QTest::keyClick(slides, Qt::Key_Up);
        QCOMPARE(controller->state().currentText(), QStringLiteral("Second changed page"));
        QTest::keyClick(slides, Qt::Key_Up);
        QCOMPARE(controller->state().index, 1);
        const auto beforeRichEdit = controller->state().cueRevision;
        QTimer::singleShot(0, [] {
            auto *editor = qobject_cast<SlideEditorDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(editor);
            auto *text = editorTextItem(editor);
            Q_ASSERT(text);
            text->document()->setPlainText(QStringLiteral("Rich editor shared edit"));
            editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        QTest::mouseClick(control<QPushButton>(window, "editSlideText"), Qt::LeftButton);
        QCOMPARE(controller->state().cueRevision, beforeRichEdit);
        QCOMPARE(controller->state().slides.count(QStringLiteral("Rich editor shared edit")), 3);
        QTimer::singleShot(0, [] {
            auto *editor = qobject_cast<SongStructureDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(editor);
            editor->findChild<QLineEdit *>(QStringLiteral("structuredSongTitle"))->setText(QStringLiteral("Created with sections"));
            editor->findChild<QPlainTextEdit *>(QStringLiteral("songSectionLyrics"))->setPlainText(QStringLiteral("New verse"));
            editor->findChild<QPushButton *>(QStringLiteral("addSectionSlide"))->click();
            editor->findChild<QPlainTextEdit *>(QStringLiteral("songSectionLyrics"))->setPlainText(QStringLiteral("New verse second slide"));
            editor->findChild<QPushButton *>(QStringLiteral("appendSongSection"))->click();
            if (qEnvironmentVariableIsSet("WORSHIPCUE_SECTION_EDITOR_SCREENSHOT"))
                QVERIFY(editor->grab().save(qEnvironmentVariable("WORSHIPCUE_SECTION_EDITOR_SCREENSHOT")));
            editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        QTest::mouseClick(control<QPushButton>(window, "newSong"), Qt::LeftButton);
        const auto created = observer.songs(QStringLiteral("Created with sections"));
        QCOMPARE(created.size(), 1);
        QCOMPARE(created.first().structure.sections.first().name, QStringLiteral("Verse 1"));
        QCOMPARE(created.first().slides, (QStringList{"New verse", "New verse second slide"}));
        QCOMPARE(created.first().structure.sections.first().slideCount, 2);
        QCOMPARE(created.first().structure.slideOrder(), (QVector<int>{0,1,0,1}));
    }

    void standaloneMediaImportPreviewOrganizeAndMixedServiceNavigation() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
        const QString source = directory.filePath(QStringLiteral("Still.png"));
        QImage image(640, 360, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        QVERIFY(image.save(source));
        QString error;
        MainWindow window;
        QVERIFY(window.initialize(&error, path));
        window.show();
        LibraryRepository observer;
        QVERIFY(observer.open(&error, path));
        const auto presentationCount = observer.songs().size();
        const auto serviceId = observer.services().first().id;
        const auto oldEntries = observer.service(serviceId);
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<MediaImportDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            Q_ASSERT(!dialog->createPresentation());
            dialog->findChild<QDialogButtonBox *>(QStringLiteral("mediaImportActions"))->button(QDialogButtonBox::Ok)->click();
        });
        QVERIFY(window.importMediaFiles({source}));
        QTRY_VERIFY_WITH_TIMEOUT(control<QPushButton>(window, "importMediaAssets")->isEnabled(), 10000);
        QCOMPARE(observer.songs().size(), presentationCount);
        QCOMPARE(observer.service(serviceId).size(), oldEntries.size());
        QCOMPARE(observer.mediaAssets().size(), 1);
        const auto asset = observer.mediaAssets().first();
        auto *media = control<QListWidget>(window, "mediaLibrary");
        QCOMPARE(media->count(), 1);
        auto *tabs = control<QTabWidget>(window, "sidebarTabs");
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->tabText(2), QStringLiteral("Media Library"));
        QCOMPARE(tabs->currentIndex(), 2);
        QVERIFY(control<QLabel>(window, "mediaDetails")->text().contains(QStringLiteral("640 × 360")));
        auto *controller = control<PresentationController>(window, "presentationController");
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog && dialog->objectName() == QStringLiteral("mediaAssetPreview"));
            dialog->reject();
        });
        QTest::mouseClick(control<QPushButton>(window, "previewMediaAsset"), Qt::LeftButton);
        QCOMPARE(controller->state().index, -1);
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog && dialog->objectName() == QStringLiteral("mediaAssetEditor"));
            dialog->findChild<QLineEdit *>(QStringLiteral("mediaAssetName"))->setText(QStringLiteral("Sunday Still.png"));
            dialog->findChild<QComboBox *>(QStringLiteral("mediaAssetFolder"))->setCurrentText(QStringLiteral("Sunday"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        QTest::mouseClick(control<QPushButton>(window, "editMediaAsset"), Qt::LeftButton);
        QCOMPARE(observer.mediaAsset(asset.id).folder, QStringLiteral("Sunday"));
        QCOMPARE(observer.mediaAsset(asset.id).media.name, QStringLiteral("Sunday Still.png"));
        QCOMPARE(observer.mediaAsset(asset.id).media.path, asset.media.path);
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        QCOMPARE(controller->state().currentMedia().kind, MediaKind::Image);
        QCOMPARE(controller->state().serviceId, 0);
        QVERIFY(control<QPushButton>(window, "revealLiveCue")->isEnabled());
        auto *order = control<QListWidget>(window, "serviceOrder");
        tabs->setCurrentIndex(1);
        QMimeData data;
        data.setData(QString::fromLatin1(mediaAssetMimeType), QByteArray::number(asset.id));
        const QPoint point = order->visualItemRect(order->item(1)).center();
        QDragEnterEvent enter(point, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(order->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(point, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(order->viewport(), &drop);
        QVERIFY(drop.isAccepted());
        const auto entries = observer.service(serviceId);
        QCOMPARE(entries.size(), oldEntries.size() + 1);
        QCOMPARE(entries.at(1).mediaAssetId, asset.id);
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        QCOMPARE(controller->state().entryId, entries.at(1).id);
        auto *slides = control<QListWidget>(window, "slideWorkspace");
        slides->setFocus();
        QTest::keyClick(slides, Qt::Key_Down);
        QCOMPARE(controller->state().entryId, entries.at(2).id);
        QCOMPARE(controller->state().index, 0);
        QTest::keyClick(slides, Qt::Key_Up);
        QCOMPARE(controller->state().entryId, entries.at(1).id);
        QCOMPARE(controller->state().currentMedia().kind, MediaKind::Image);
        QTest::keyClick(slides, Qt::Key_Left);
        QCOMPARE(controller->state().entryId, entries.at(0).id);
        QCOMPARE(controller->state().index, observer.entrySlides(entries.first().id).size() - 1);
        tabs->setCurrentIndex(2);
        window.activateWindow();
        QApplication::processEvents();
        control<QLineEdit>(window, "mediaSearch")->setFocus();
        QVERIFY(control<QLineEdit>(window, "mediaSearch")->hasFocus());
        const auto revision = controller->state().cueRevision;
        QTest::keyClick(control<QLineEdit>(window, "mediaSearch"), Qt::Key_Down);
        QTest::keyClick(control<QLineEdit>(window, "mediaSearch"), Qt::Key_Space);
        QCOMPARE(controller->state().cueRevision, revision);
        control<QLineEdit>(window, "mediaSearch")->clear();
        QTest::mouseClick(media->viewport(), Qt::LeftButton, Qt::NoModifier, media->visualItemRect(media->item(0)).center());
        if (qEnvironmentVariableIsSet("WORSHIPCUE_MEDIA_LIBRARY_SCREENSHOT"))
            QVERIFY(window.grab().save(qEnvironmentVariable("WORSHIPCUE_MEDIA_LIBRARY_SCREENSHOT")));
    }

    void libraryClickLoadsCompletePresentationAndTargetsWorkspaceTools() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY2(observer.open(&error, path), qPrintable(error));
        QImage picture(32, 18, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        QVERIFY(QDir().mkpath(temp.filePath(QStringLiteral("media"))));
        QVERIFY(picture.save(temp.filePath(QStringLiteral("media/library-picture.png"))));
        const QString background = BuiltInBackgrounds::catalog().first().path;
        const QStringList text{QStringLiteral("Library picture overlay"), QStringLiteral("Library second slide")};
        const QStringList rich{QStringLiteral("<p><b>Library picture overlay</b></p>"),
                               QStringLiteral("<p><i>Library second slide</i></p>")};
        const QVector<SlideMedia> media{{MediaKind::Image, QStringLiteral("media/library-picture.png"),
                                         QStringLiteral("Library picture"), false}, {}};
        const int libraryId = observer.createPresentation(QStringLiteral("AAA Library only"), text, media,
                                                           &error, {}, background, rich);
        QVERIFY2(libraryId > 0, qPrintable(error));
        const int serviceId = observer.services().first().id;
        const auto serviceBefore = observer.service(serviceId);
        const Song serviceSong = observer.song(serviceBefore.first().songId);

        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *library = control<QListWidget>(window, "songLibrary");
        auto *slides = control<ReorderListWidget>(window, "slideWorkspace");
        auto *controller = control<PresentationController>(window, "presentationController");
        QCOMPARE(library->currentItem()->data(Qt::UserRole).toInt(), libraryId);
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        const auto live = controller->state();
        QVERIFY(live.serviceId > 0);
        // The library row is already selected while the service is displayed.
        // Clicking it must still switch the workspace to that library item.
        control<QTabWidget>(window, "sidebarTabs")->setCurrentIndex(0);
        QTest::mouseClick(library->viewport(), Qt::LeftButton, Qt::NoModifier,
                         library->visualItemRect(library->currentItem()).center());
        QCOMPARE(slides->count(), 2);
        for (int i = 0; i < slides->count(); ++i) {
            QCOMPARE(slides->item(i)->data(Qt::UserRole).toInt(), i);
            QCOMPARE(slides->item(i)->data(Qt::UserRole + 1).toString(), text.at(i));
            QCOMPARE(slides->item(i)->data(Qt::UserRole + 3).value<SlideMedia>(), media.at(i));
            QCOMPARE(slides->item(i)->data(Qt::UserRole + 4).toString(), rich.at(i));
        }
        QVERIFY(control<QLabel>(window, "selectedInfo")->text().contains(QStringLiteral("AAA Library only")));
        QVERIFY(control<QLabel>(window, "selectedTimingInfo")->text().contains(BuiltInBackgrounds::title(background)));
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        QCOMPARE(controller->state().slides, live.slides);

        slides->setCurrentRow(1);
        bool editedLibrarySlide = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<SlideEditorDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            editedLibrarySlide = dialog->title() == QStringLiteral("AAA Library only")
                && dialog->selectedSlideIndex() == 1;
            editorTextItem(dialog)->setHtml(QStringLiteral("<p><b>Edited library second slide</b></p>"));
            dialog->accept();
        });
        QTest::mouseClick(control<QPushButton>(window, "editSlideText"), Qt::LeftButton);
        QVERIFY(editedLibrarySlide);
        QCOMPARE(observer.song(libraryId).slides.at(1), QStringLiteral("Edited library second slide"));
        QCOMPARE(observer.song(serviceSong.id).slides, serviceSong.slides);
        QCOMPARE(slides->currentRow(), 1);
        QCOMPARE(slides->item(1)->data(Qt::UserRole + 1).toString(), observer.song(libraryId).slides.at(1));

        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<PresentationSettingsDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            dialog->findChild<QCheckBox *>(QStringLiteral("overrideProgramTransition"))->setChecked(true);
            dialog->findChild<QComboBox *>(QStringLiteral("slideTransitionType"))->setCurrentIndex(1);
            dialog->findChild<QDoubleSpinBox *>(QStringLiteral("slideTransitionDuration"))->setValue(.35);
            dialog->accept();
        });
        QTest::mouseClick(control<QPushButton>(window, "presentationSettings"), Qt::LeftButton);
        QCOMPARE(observer.song(libraryId).settings, (PresentationSettings{0, false, SlideTransitionType::Fade, 350, true}));
        QCOMPARE(observer.song(serviceSong.id).settings, serviceSong.settings);

        Song updated = observer.song(libraryId);
        updated.slides[1] = QStringLiteral("External library edit");
        updated.richText[1] = QStringLiteral("<p><i>External library edit</i></p>");
        QVERIFY(observer.updatePresentationText(libraryId, updated.title, updated.slides, updated.richText, &error));
        QVERIFY(QMetaObject::invokeMethod(control<QTimer>(window, "externalLibraryRefresh"), "timeout", Qt::DirectConnection));
        QCOMPARE(library->currentItem()->data(Qt::UserRole).toInt(), libraryId);
        QCOMPARE(slides->currentRow(), 1);
        QCOMPARE(slides->item(1)->data(Qt::UserRole + 1).toString(), updated.slides.at(1));
        QCOMPARE(slides->item(1)->data(Qt::UserRole + 4).toString(), updated.richText.at(1));
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        QCOMPARE(controller->state().slides, live.slides);

        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        QCOMPARE(controller->state().serviceId, 0);
        QCOMPARE(controller->state().entryId, 0);
        QCOMPARE(controller->state().title, updated.title);
        QCOMPARE(controller->state().slides, updated.slides);
        QCOMPARE(controller->state().currentText(), updated.slides.at(1));
        QCOMPARE(controller->state().currentRichText(), updated.richText.at(1));
        QCOMPARE(controller->state().media.first().path, temp.filePath(QStringLiteral("media/library-picture.png")));
        QCOMPARE(controller->state().backgroundPath, BuiltInBackgrounds::resourcePath(background));
        QVERIFY(!controller->next());
        QCOMPARE(observer.service(serviceId).size(), serviceBefore.size());
        for (int i = 0; i < serviceBefore.size(); ++i)
            QCOMPARE(observer.service(serviceId).at(i).id, serviceBefore.at(i).id);
    }

    void libraryBrowsingKeepsNaturalOrderAndServiceArrangementSeparate() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository observer;
        QVERIFY2(observer.open(&error, path), qPrintable(error));
        const QStringList text{QStringLiteral("First"), QStringLiteral("Second"), QStringLiteral("Third")};
        const int songId = observer.createTextPresentation(QStringLiteral("AAA Arranged song"), text, &error);
        QVERIFY(songId > 0);
        const int serviceId = observer.createService(QStringLiteral("Arranged service"), &error);
        QVERIFY(observer.appendToService(serviceId, songId, &error));
        const int entryId = observer.service(serviceId).first().id;
        const QVector<int> arrangement{2, 0, 2};
        QVERIFY(observer.setEntrySlideOrder(entryId, arrangement, &error));

        MainWindow window;
        QVERIFY2(window.initialize(&error, path), qPrintable(error));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *plans = control<QComboBox>(window, "savedServices");
        plans->setCurrentIndex(plans->findData(serviceId));
        auto *library = control<QListWidget>(window, "songLibrary");
        auto *entries = control<QListWidget>(window, "serviceOrder");
        auto *slides = control<ReorderListWidget>(window, "slideWorkspace");
        auto *tabs = control<QTabWidget>(window, "sidebarTabs");
        auto *controller = control<PresentationController>(window, "presentationController");
        QCOMPARE(slides->item(0)->data(Qt::UserRole).toInt(), 2);
        QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
        const auto live = controller->state();
        tabs->setCurrentIndex(0);
        QTest::mouseClick(library->viewport(), Qt::LeftButton, Qt::NoModifier,
                         library->visualItemRect(library->currentItem()).center());
        QCOMPARE(library->currentItem()->data(Qt::UserRole).toInt(), songId);
        for (int i = 0; i < slides->count(); ++i) {
            QCOMPARE(slides->item(i)->data(Qt::UserRole).toInt(), i);
            QCOMPARE(slides->item(i)->data(Qt::UserRole + 1).toString(), text.at(i));
        }
        control<QComboBox>(window, "operatorMode")->setCurrentIndex(1);
        QCOMPARE(slides->dragDropMode(), QAbstractItemView::NoDragDrop);
        QVERIFY(slides->moveItem(0, 2));
        for (int i = 0; i < slides->count(); ++i)
            QCOMPARE(slides->item(i)->data(Qt::UserRole).toInt(), i);
        QCOMPARE(observer.service(serviceId).first().slideOrder, arrangement);
        QCOMPARE(controller->state().cueRevision, live.cueRevision);

        // Returning to the already-selected service row must restore its order.
        tabs->setCurrentIndex(1);
        QTest::mouseClick(entries->viewport(), Qt::LeftButton, Qt::NoModifier,
                         entries->visualItemRect(entries->currentItem()).center());
        QCOMPARE(slides->dragDropMode(), QAbstractItemView::InternalMove);
        for (int i = 0; i < slides->count(); ++i)
            QCOMPARE(slides->item(i)->data(Qt::UserRole).toInt(), arrangement.at(i));
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
        tabs->setCurrentIndex(0);
        QTest::mouseClick(library->viewport(), Qt::LeftButton, Qt::NoModifier,
                         library->visualItemRect(library->currentItem()).center());
        QTest::mouseClick(control<QPushButton>(window, "revealLiveCue"), Qt::LeftButton);
        QCOMPARE(tabs->currentIndex(), 1);
        QCOMPARE(entries->currentItem()->data(Qt::UserRole).toInt(), entryId);
        for (int i = 0; i < slides->count(); ++i)
            QCOMPARE(slides->item(i)->data(Qt::UserRole).toInt(), arrangement.at(i));
        QCOMPARE(controller->state().cueRevision, live.cueRevision);
    }

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
        window.raise();
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

    void presentationSettingsDefaultsAllowDirectValueEditing_data() {
        QTest::addColumn<bool>("programMode");
        QTest::addColumn<bool>("bulkMode");
        QTest::newRow("presentation") << false << false;
        QTest::newRow("bulk-selection") << false << true;
        QTest::newRow("global-defaults") << true << false;
    }

    void presentationSettingsDefaultsAllowDirectValueEditing() {
        QFETCH(bool, programMode);
        QFETCH(bool, bulkMode);
        const PresentationSettings original;
        const PresentationSettings defaults{0, false, SlideTransitionType::Cut, 800};
        PresentationSettingsDialog dialog("Direct editing", original, nullptr, defaults, programMode, bulkMode);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        if (!programMode && !bulkMode && qEnvironmentVariableIsSet("WORSHIPCUE_DEFAULT_TIMING_SCREENSHOT"))
            QVERIFY(dialog.grab().save(qEnvironmentVariable("WORSHIPCUE_DEFAULT_TIMING_SCREENSHOT")));
        auto *interval = dialog.findChild<QDoubleSpinBox *>("autoAdvanceInterval");
        auto *automatic = dialog.findChild<QCheckBox *>("autoAdvanceEnabled");
        auto *loop = dialog.findChild<QCheckBox *>("loopPresentation");
        auto *type = dialog.findChild<QComboBox *>("slideTransitionType");
        auto *duration = dialog.findChild<QDoubleSpinBox *>("slideTransitionDuration");
        auto *override = dialog.findChild<QCheckBox *>("overrideProgramTransition");
        QVERIFY(type->isEnabled());
        QVERIFY(duration->isEnabled());
        QVERIFY(!automatic->isChecked());
        QVERIFY(!override->isChecked());
        QCOMPARE(dialog.settings().overrideTransition, programMode);
        if (!programMode) QCOMPARE(dialog.settings(), original);
        type->setFocus();
        QTest::keyClick(type, Qt::Key_End);
        QCOMPARE(type->currentData().toInt(), int(SlideTransitionType::SlideRight));
        if (!programMode) QVERIFY(override->isChecked());
        duration->setFocus(); duration->selectAll();
        QTest::keyClicks(duration, "1.25");
        QTest::keyClick(duration, Qt::Key_Tab);
        QCOMPARE(dialog.settings().transitionDurationMs, 1250);
        QCOMPARE(dialog.settings().transition, SlideTransitionType::SlideRight);
        if (!programMode) {
            // Restoring inheritance displays actual global values without locking edits.
            QTest::mouseClick(override, Qt::LeftButton, Qt::NoModifier, QPoint(10, override->height() / 2));
            QVERIFY(!override->isChecked());
            QCOMPARE(type->currentData().toInt(), int(defaults.transition));
            QCOMPARE(duration->value(), .8);
            QVERIFY(type->isEnabled());
            QVERIFY(duration->isEnabled());
            QCOMPARE(dialog.settings().overrideTransition, false);
            duration->setFocus(); duration->selectAll();
            QTest::keyClicks(duration, "0.65");
            QTest::keyClick(duration, Qt::Key_Tab);
            QVERIFY(override->isChecked());
            QCOMPARE(dialog.settings().transitionDurationMs, 650);
        }
        if (!programMode && !bulkMode) {
            QVERIFY(interval->isEnabled());
            QVERIFY(loop->isEnabled());
            interval->setFocus(); interval->selectAll();
            QTest::keyClicks(interval, "3.5");
            QTest::keyClick(interval, Qt::Key_Tab);
            QVERIFY(automatic->isChecked());
            QCOMPARE(dialog.settings().autoAdvanceMs, 3500);
            QTest::mouseClick(automatic, Qt::LeftButton, Qt::NoModifier, QPoint(10, automatic->height() / 2));
            QCOMPARE(dialog.settings().autoAdvanceMs, 0);
            QVERIFY(interval->isEnabled());
            QTest::mouseClick(loop, Qt::LeftButton, Qt::NoModifier, QPoint(10, loop->height() / 2));
            QVERIFY(automatic->isChecked());
            QVERIFY(dialog.settings().loop);
        }
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
                QVERIFY(dialog->findChild<QDoubleSpinBox *>(QStringLiteral("autoAdvanceInterval"))->isEnabled());
                dialog->findChild<QDoubleSpinBox *>(QStringLiteral("autoAdvanceInterval"))->setValue(1.0);
                dialog->findChild<QCheckBox *>(QStringLiteral("loopPresentation"))->setChecked(true);
                QVERIFY(dialog->findChild<QComboBox *>(QStringLiteral("slideTransitionType"))->isEnabled());
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
            auto *service = control<QListWidget>(window, "serviceOrder");
            control<QTabWidget>(window, "sidebarTabs")->setCurrentIndex(1);
            QTest::mouseClick(service->viewport(), Qt::LeftButton, Qt::NoModifier, service->visualItemRect(service->item(0)).center());
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

    void builtInBackgroundAssignmentPersistsAndReachesAudience() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("library.sqlite3"));
        const QString assigned = BuiltInBackgrounds::catalog().last().path;
        const QString resource = BuiltInBackgrounds::resourcePath(assigned);
        QString error;
        LibraryRepository observer;
        QVERIFY2(observer.open(&error, path), qPrintable(error));
        const int songId = observer.service(observer.services().first().id).first().songId;
        const auto audienceImage = [](DisplayWindow &audience) {
            QImage image(320, 180, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::black);
            QPainter painter(&image);
            audience.paintAudience(painter, image.rect());
            return image;
        };
        {
            MainWindow window;
            QVERIFY2(window.initialize(&error, path), qPrintable(error));
            window.show();
            auto *library = control<QListWidget>(window, "songLibrary");
            for (int i = 0; i < library->count(); ++i)
                if (library->item(i)->data(Qt::UserRole).toInt() == songId) library->setCurrentRow(i);
            auto *controller = control<PresentationController>(window, "presentationController");
            DisplayWindow audience(DisplayWindow::Kind::Audience, controller);
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            const QImage before = audienceImage(audience);
            auto *slides = control<QListWidget>(window, "slideWorkspace");
            const QImage thumbnailBefore = slides->item(0)->data(Qt::UserRole + 2).value<QPixmap>().toImage();
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<BackgroundLibraryDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                auto *items = dialog->findChild<QListWidget *>(QStringLiteral("backgroundLibraryItems"));
                for (int i = 0; i < items->count(); ++i)
                    if (items->item(i)->data(Qt::UserRole).toString() == assigned) items->setCurrentRow(i);
                dialog->accept();
            });
            control<QAction>(window, "assignBackgroundAction")->trigger();
            QCOMPARE(observer.song(songId).backgroundPath, assigned);
            QVERIFY(controller->state().backgroundPath.isEmpty());
            QCOMPARE(audienceImage(audience), before);
            const QImage thumbnailAfter = slides->item(0)->data(Qt::UserRole + 2).value<QPixmap>().toImage();
            QVERIFY(!thumbnailAfter.isNull());
            QVERIFY(thumbnailAfter != thumbnailBefore);
            QTest::mouseClick(control<QPushButton>(window, "takeLive"), Qt::LeftButton);
            QCOMPARE(controller->state().backgroundPath, resource);
            QVERIFY(audienceImage(audience) != before);
            QVERIFY(!QFileInfo::exists(temp.filePath(QStringLiteral("backgrounds"))));
            window.close();
        }
        MainWindow reopened;
        QVERIFY2(reopened.initialize(&error, path), qPrintable(error));
        QTest::mouseClick(control<QPushButton>(reopened, "takeLive"), Qt::LeftButton);
        QCOMPARE(control<PresentationController>(reopened, "presentationController")->state().backgroundPath, resource);
        BackgroundLibraryDialog backgroundLibrary(temp.path(), observer.song(songId).backgroundPath);
        QCOMPARE(backgroundLibrary.selectedPath(), assigned);
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
                dialog->findChild<QCheckBox *>(QStringLiteral("createMediaPresentation"))->setChecked(true);
            dialog->findChild<QCheckBox *>(QStringLiteral("addMediaToService"))->setChecked(true);
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
            dialog->findChild<QCheckBox *>(QStringLiteral("createMediaPresentation"))->setChecked(true);
            dialog->findChild<QCheckBox *>(QStringLiteral("addMediaToService"))->setChecked(true);
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
        window.raise();
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
