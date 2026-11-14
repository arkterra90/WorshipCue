#include "fixtures/SchemaDowngrade.h"
#include "../src/data/LibraryRepository.h"
#include "../src/core/PresentationController.h"
#include "../src/ui/SongStructureDialog.h"
#include "../src/ui/ReorderListWidget.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QInputDialog>
#include <QTimer>
#include <QMessageBox>
#include <QTextCursor>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

namespace {
Song songFixture() {
    Song song;
    song.title = QStringLiteral("Shared sections");
    song.slides = {QStringLiteral("First verse"), QStringLiteral("Second verse"), QStringLiteral("Chorus first page"),
                   QStringLiteral("Chorus second page"), QStringLiteral("Bridge")};
    song.richText = {QStringLiteral(""), QStringLiteral(""), QStringLiteral(""), QStringLiteral(""), QStringLiteral("")};
    song.structure.sections = {{"v1", "Verse 1", 1}, {"v2", "Verse 2", 1}, {"c", "Chorus", 2}, {"b", "Bridge", 1}};
    song.structure.arrangements = {{"full", "Full", {"v1", "c", "v2", "c", "b", "c", "c"}},
                                   {"short", "Acoustic", {"v1", "c"}}};
    song.structure.activeArrangementId = QStringLiteral("full");
    return song;
}
QVariant sqlValue(const QString &path, const QString &statement) {
    const auto name = QUuid::createUuid().toString();
    QVariant value;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec(statement) && query.next()) value = query.value(0);
        }
    }
    QSqlDatabase::removeDatabase(name);
    return value;
}
bool sql(const QString &path, const QString &statement) {
    const auto name = QUuid::createUuid().toString();
    bool result;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        database.setDatabaseName(path);
        QSqlQuery query(database);
        result = database.open() && query.exec(statement);
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}
}

class SongStructureTest : public QObject {
    Q_OBJECT
    template<typename T> T *control(QObject &parent, const char *name) {
        auto *result = parent.findChild<T *>(QString::fromLatin1(name));
        Q_ASSERT(result);
        return result;
    }
private slots:
    void sectionSlidesHaveExplicitAddSplitDuplicateReorderAndRemoveControls() {
        const auto source = songFixture();
        SongStructureDialog editor(source);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        auto *pages = control<ReorderListWidget>(editor, "songSectionSlides");
        auto *lyrics = control<QPlainTextEdit>(editor, "songSectionLyrics");
        QCOMPARE(pages->count(), 1);
        QCOMPARE(pages->item(0)->text(), QStringLiteral("Verse 1 • Slide 1"));
        QVERIFY(!control<QPushButton>(editor, "removeSectionSlide")->isEnabled());
        control<QPushButton>(editor, "addSectionSlide")->click();
        QCOMPARE(pages->count(), 2);
        QCOMPARE(pages->item(1)->text(), QStringLiteral("Verse 1 • Slide 2"));
        QVERIFY(lyrics->toPlainText().isEmpty());
        lyrics->setPlainText("Verse continuation");
        pages->setCurrentRow(0);
        auto cursor = lyrics->textCursor(); cursor.setPosition(5); lyrics->setTextCursor(cursor);
        lyrics->setFocus();
        QTest::keyClick(lyrics, Qt::Key_Return, Qt::AltModifier);
        QCOMPARE(pages->count(), 3);
        QCOMPARE(editor.draft().slides.mid(0, 3), (QStringList{"First", " verse", "Verse continuation"}));
        const auto originalKey = editor.draft().structure.slideKeys().first();
        control<QPushButton>(editor, "duplicateSectionSlide")->click();
        QCOMPARE(pages->count(), 4);
        QCOMPARE(editor.draft().slides.mid(0, 4), (QStringList{"First", " verse", " verse", "Verse continuation"}));
        QVERIFY(pages->moveItem(3, 0));
        QCOMPARE(editor.draft().slides.first(), QStringLiteral("Verse continuation"));
        QCOMPARE(editor.draft().structure.slideKeys().at(1), originalKey);
        pages->setCurrentRow(3);
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            Q_ASSERT(box); box->button(QMessageBox::Yes)->click();
        });
        control<QPushButton>(editor, "removeSectionSlide")->click();
        QCOMPARE(pages->count(), 3);
        QCOMPARE(editor.draft().slides.mid(0, 3), (QStringList{"Verse continuation", "First", " verse"}));
        QCOMPARE(pages->item(2)->text(), QStringLiteral("Verse 1 • Slide 3"));
        QString error;
        QVERIFY2(editor.draft().structure.validate(int(editor.draft().slides.size()), &error), qPrintable(error));
        if (qEnvironmentVariableIsSet("WORSHIPCUE_SECTION_SLIDES_SCREENSHOT"))
            QVERIFY(editor.grab().save(qEnvironmentVariable("WORSHIPCUE_SECTION_SLIDES_SCREENSHOT")));
        editor.reject();
        QCOMPARE(source.slides.first(), QStringLiteral("First verse"));
    }

    void newMultiSlideSectionSavesReopensAndRepeatsAsOneGroup() {
        SongStructureDialog editor;
        control<QLineEdit>(editor, "structuredSongTitle")->setText("Two-page verse");
        control<QPlainTextEdit>(editor, "songSectionLyrics")->setPlainText("Verse first slide");
        control<QPushButton>(editor, "addSectionSlide")->click();
        control<QPlainTextEdit>(editor, "songSectionLyrics")->setPlainText("Verse second slide");
        control<QPushButton>(editor, "appendSongSection")->click();
        const auto draft = editor.draft();
        QCOMPARE(draft.structure.sections.size(), 1);
        QCOMPARE(draft.structure.sections.first().slideCount, 2);
        QCOMPARE(draft.structure.slideOrder(), (QVector<int>{0,1,0,1}));
        control<QDialogButtonBox>(editor, "songStructureActions")->button(QDialogButtonBox::Save)->click();
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        QTemporaryDir directory;
        const auto path = directory.filePath("multi.sqlite3");
        QString error;
        int id;
        {
            LibraryRepository library;
            QVERIFY(library.open(&error, path));
            id = library.createTextPresentation(draft.title, draft.slides, &error, {}, {}, draft.richText, draft.structure);
            QVERIFY2(id > 0, qPrintable(error));
            QVERIFY(library.appendToService(1, id, &error));
        }
        LibraryRepository reopened;
        QVERIFY(reopened.open(&error, path));
        const auto saved = reopened.song(id);
        QCOMPARE(saved.structure.slideKeys(), draft.structure.slideKeys());
        SongStructureDialog again(saved);
        auto *pages = control<ReorderListWidget>(again, "songSectionSlides");
        QCOMPARE(pages->count(), 2);
        QCOMPARE(pages->item(1)->text(), QStringLiteral("Verse 1 • Slide 2"));
        const auto texts = reopened.entrySlides(reopened.service(1).last().id);
        QCOMPARE(texts, (QStringList{"Verse first slide", "Verse second slide", "Verse first slide", "Verse second slide"}));
        PresentationController controller;
        controller.takeLive("Song", texts);
        for (int i = 0; i < texts.size(); ++i) {
            QCOMPARE(controller.state().currentText(), texts.at(i));
            if (i + 1 < texts.size()) QVERIFY(controller.next());
        }
    }

    void slideIdentitySurvivesReorderAndCopiesPreserveFormatting() {
        QTemporaryDir directory;
        const auto path = directory.filePath("identities.sqlite3");
        QString error;
        LibraryRepository library;
        QVERIFY(library.open(&error, path));
        auto fixture = songFixture();
        PresentationLook look;
        look.textColor = "#ff0000";
        const int lookId = library.createLook("Chorus red", look, &error);
        QVERIFY(lookId > 0);
        fixture.richText[2] = "<p><b>Chorus first page</b></p>";
        fixture.appearance.slideLookIds = {0,0,lookId,0,0};
        fixture.appearance.richTextOverrides = {false,false,true,false,false};
        fixture.appearance.fitText = {false,false,true,false,false};
        const int id = library.createTextPresentation(fixture.title, fixture.slides, &error, {}, {}, fixture.richText, fixture.structure, fixture.appearance);
        QVERIFY2(id > 0, qPrintable(error));
        QVERIFY(library.appendToService(1, id, &error));
        const int entry = library.service(1).last().id;
        QVERIFY(library.setEntrySlideOrder(entry, {3,2,3}, &error));
        const auto before = library.song(id);
        const int firstId = sqlValue(path, QStringLiteral("SELECT id FROM slides WHERE song_id=%1 AND position=2").arg(id)).toInt();
        PresentationItem live{entry, "Live chorus", before.slides.mid(2,2)};
        live.songId = id; live.sourceSlideKeys = before.structure.slideKeys().mid(2,2);
        PresentationController controller;
        controller.takeServiceLive(1, {live}, entry);
        const auto revision = controller.state().cueRevision;
        SongStructureDialog editor(before);
        control<ReorderListWidget>(editor, "songSections")->setCurrentRow(2);
        auto *pages = control<ReorderListWidget>(editor, "songSectionSlides");
        QVERIFY(pages->moveItem(1,0));
        pages->setCurrentRow(1);
        control<QPushButton>(editor, "addSectionSlide")->click();
        control<QPlainTextEdit>(editor, "songSectionLyrics")->setPlainText("Extra chorus");
        pages->setCurrentRow(1);
        control<QPushButton>(editor, "duplicateSectionSlide")->click();
        const auto draft = editor.draft();
        QVERIFY2(library.updateSongStructure(before, draft, &error), qPrintable(error));
        const auto saved = library.song(id);
        QCOMPARE(saved.structure.sections.at(2).slideCount, 4);
        QCOMPARE(saved.slides.mid(2,4), (QStringList{"Chorus second page", "Chorus first page", "Chorus first page", "Extra chorus"}));
        QCOMPARE(sqlValue(path, QStringLiteral("SELECT id FROM slides WHERE song_id=%1 AND position=3").arg(id)).toInt(), firstId);
        QCOMPARE(library.service(1).last().slideOrder, (QVector<int>{2,2,3,4,5}));
        for (int index : {3,4}) {
            QCOMPARE(saved.appearance.slideLookIds.at(index), lookId);
            QVERIFY(saved.appearance.richTextOverrides.at(index));
            QVERIFY(saved.appearance.fitText.at(index));
            QCOMPARE(saved.richText.at(index), before.richText.at(2));
        }
        QVERIFY(controller.refreshSongText(id, saved.structure.slideKeys(), saved.slides, saved.richText, saved.looks));
        QCOMPARE(controller.state().currentText(), QStringLiteral("Chorus first page"));
        QCOMPARE(controller.state().nextText(), QStringLiteral("Chorus second page"));
        QCOMPARE(controller.state().cueRevision, revision);
        QCOMPARE(controller.state().entryId, entry);
        QCOMPARE(controller.state().index, 0);
        // A custom partial occurrence protects the specific page it references.
        SongStructureDialog removal(saved);
        control<ReorderListWidget>(removal, "songSections")->setCurrentRow(2);
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            Q_ASSERT(box); box->button(QMessageBox::Yes)->click();
        });
        control<QPushButton>(removal, "removeSectionSlide")->click();
        QVERIFY(!library.updateSongStructure(saved, removal.draft(), &error));
        QVERIFY(error.contains("saved service"));
        QCOMPARE(library.song(id).slides, saved.slides);
    }

    void stablePageMetadataIsValidatedAndLegacyKeysStayCompatible() {
        auto structure = songFixture().structure;
        QCOMPARE(structure.slideKeys().at(2), QStringLiteral("c/0"));
        structure.sections[2].slideIds = {"second", "first"};
        QString error;
        QVERIFY(structure.validate(5, &error));
        SongStructure decoded;
        QVERIFY(SongStructure::fromJson(structure.toJson(), 5, &decoded, &error));
        QCOMPARE(decoded.slideKeys().at(2), QStringLiteral("c/second"));
        QCOMPARE(decoded.toJson(), structure.toJson());
        structure.sections[2].slideIds = {"same", "same"};
        QVERIFY(!structure.validate(5, &error));
        QVERIFY(!SongStructure::fromJson(structure.toJson(), 5, &decoded, &error));
        structure.sections[2].slideIds = {"only-one"};
        QVERIFY(!SongStructure::fromJson(structure.toJson(), 5, &decoded, &error));
    }

    void repeatedMultiSlideSectionsShareOneSourceAndPersist() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        int id = 0;
        int entryId = 0;
        {
            LibraryRepository library;
            QVERIFY(library.open(&error, path));
            auto song = songFixture();
            id = library.createTextPresentation(song.title, song.slides, &error, {}, {}, song.richText, song.structure);
            QVERIFY2(id > 0, qPrintable(error));
            QVERIFY(library.appendToService(1, id, &error));
            entryId = library.service(1).last().id;
            QCOMPARE(library.song(id).slides.size(), 5);
            QCOMPARE(library.entrySlides(entryId).size(), 11);
            song.slides[2] = QStringLiteral("Edited shared chorus");
            song.richText[2] = QStringLiteral("<p><b>Edited shared chorus</b></p>");
            QVERIFY(library.updatePresentationText(id, song.title, song.slides, song.richText, &error));
            const auto texts = library.entrySlides(entryId);
            const auto rich = library.entryRichText(entryId);
            for (int index : {1, 4, 7, 9}) {
                QCOMPARE(texts.at(index), song.slides.at(2));
                QCOMPARE(rich.at(index), song.richText.at(2));
                QCOMPARE(texts.at(index + 1), song.slides.at(3));
            }
            QCOMPARE(sqlValue(path, QStringLiteral("SELECT COUNT(*) FROM slides WHERE song_id=%1").arg(id)).toInt(), 5);
        }
        LibraryRepository reopened;
        QVERIFY(reopened.open(&error, path));
        QCOMPARE(reopened.song(id).structure.arrangements.size(), 2);
        QCOMPARE(reopened.entrySlides(entryId).count(QStringLiteral("Edited shared chorus")), 4);
    }

    void namedArrangementsCanBeSwitchedWithoutEditingLyrics() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        auto fixture = songFixture();
        const int id = library.createTextPresentation(fixture.title, fixture.slides, &error, {}, {}, fixture.richText, fixture.structure);
        QVERIFY(library.appendToService(1, id, &error));
        const int entry = library.service(1).last().id;
        const Song original = library.song(id);
        Song acoustic = original;
        acoustic.structure.activeArrangementId = QStringLiteral("short");
        QVERIFY2(library.updateSongStructure(original, acoustic, &error), qPrintable(error));
        QCOMPARE(library.song(id).slides, original.slides);
        QCOMPARE(library.entrySlides(entry), QStringList({"First verse", "Chorus first page", "Chorus second page"}));
        QVERIFY(library.setEntrySlideOrder(entry, {4, 2, 3, 2, 3}, &error));
        QCOMPARE(library.entrySlides(entry).first(), QStringLiteral("Bridge"));
        QVERIFY(library.setEntrySlideOrder(entry, {}, &error));
        QCOMPARE(library.entrySlides(entry).size(), 3);
    }

    void reorderingAndSplittingSectionsRemapsSavedOccurrencesAndPreservesSlideIds() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, path));
        auto fixture = songFixture();
        const int id = library.createTextPresentation(fixture.title, fixture.slides, &error, {}, {}, fixture.richText, fixture.structure);
        QVERIFY(library.appendToService(1, id, &error));
        const int entry = library.service(1).last().id;
        QVERIFY(library.setEntrySlideOrder(entry, {2, 3, 0, 2, 3}, &error));
        const int chorusId = sqlValue(path, QStringLiteral("SELECT id FROM slides WHERE song_id=%1 AND position=2").arg(id)).toInt();
        const Song original = library.song(id);
        Song draft = original;
        draft.structure.sections = {{"c", "Chorus", 3}, {"v1", "Verse 1", 1}, {"v2", "Verse 2", 1}, {"b", "Bridge", 1}};
        draft.slides = {"Chorus first page", "Chorus second page", "Added chorus page", "First verse", "Second verse", "Bridge"};
        draft.richText = {"", "", "", "", "", ""};
        QVERIFY2(library.updateSongStructure(original, draft, &error), qPrintable(error));
        QCOMPARE(library.service(1).last().slideOrder, QVector<int>({0, 1, 2, 3, 0, 1, 2}));
        QCOMPARE(library.entrySlides(entry).count(QStringLiteral("Added chorus page")), 2);
        QCOMPARE(sqlValue(path, QStringLiteral("SELECT id FROM slides WHERE song_id=%1 AND position=0").arg(id)).toInt(), chorusId);
        QVERIFY(!library.updateSongStructure(original, draft, &error));
        QVERIFY(error.contains(QStringLiteral("changed while editing")));
        const Song beforeRemoval = library.song(id);
        Song removed = beforeRemoval;
        removed.structure.sections.removeFirst();
        removed.slides = {"First verse", "Second verse", "Bridge"};
        removed.richText = {"", "", ""};
        for (auto &arrangement : removed.structure.arrangements) arrangement.sectionIds.removeAll(QStringLiteral("c"));
        QVERIFY(!library.updateSongStructure(beforeRemoval, removed, &error));
        QVERIFY(error.contains(QStringLiteral("saved service")));
        QCOMPARE(library.song(id).slides, beforeRemoval.slides);
    }

    void malformedArrangementsAreRejectedAndVersionSixMigrates() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        {
            LibraryRepository library;
            QVERIFY(library.open(&error, path));
            auto fixture = songFixture();
            fixture.structure.arrangements[0].sectionIds.append(QStringLiteral("missing"));
            QVERIFY(!library.createSong(fixture.title, fixture.slides, &error, {}, {}, fixture.richText, fixture.structure));
            fixture = songFixture();
            fixture.structure.sections[1].name = QStringLiteral("VERSE 1");
            QVERIFY(!fixture.structure.validate(5, &error));
        }
        QVERIFY(removeCleanupSchema(path));
        QVERIFY(sql(path, QStringLiteral("ALTER TABLE songs DROP COLUMN look_id")));
        QVERIFY(sql(path, QStringLiteral("ALTER TABLE slides DROP COLUMN look_id")));
        QVERIFY(sql(path, QStringLiteral("ALTER TABLE slides DROP COLUMN look_rich_override")));
        QVERIFY(sql(path, QStringLiteral("ALTER TABLE slides DROP COLUMN look_fit")));
        QVERIFY(sql(path, QStringLiteral("DROP TABLE looks")));
        QVERIFY(sql(path, QStringLiteral("ALTER TABLE songs DROP COLUMN song_structure")));
        for (const auto &column : {"media_transition_type", "media_transition_duration_ms", "media_transition_override"})
            QVERIFY(sql(path, QStringLiteral("ALTER TABLE service_entries DROP COLUMN %1").arg(QString::fromLatin1(column))));
        QVERIFY(sql(path, QStringLiteral("DROP TABLE midi_bindings")));
        QVERIFY(sql(path, QStringLiteral("DROP TABLE midi_slide_actions")));
        QVERIFY(sql(path, QStringLiteral("PRAGMA user_version=6")));
        LibraryRepository migrated;
        QVERIFY2(migrated.open(&error, path), qPrintable(error));
        QVERIFY(migrated.song(1).structure.isEmpty());
        QCOMPARE(migrated.entrySlides(1).size(), 2);
        QCOMPARE(sqlValue(path, QStringLiteral("PRAGMA user_version")).toInt(), 11);
        QVERIFY(sql(path, QStringLiteral(R"SQL(UPDATE songs SET song_structure='{"broken":true}' WHERE id=1)SQL")));
        QCOMPARE(migrated.song(1).id, 0);
        QVERIFY(!migrated.lastError().isEmpty());
    }

    void failedSectionSaveRollsBackLyricsAndServiceArrangements() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository library;
        QVERIFY(library.open(&error, path));
        const auto fixture = songFixture();
        const int id = library.createTextPresentation(fixture.title, fixture.slides, &error, {}, {}, fixture.richText, fixture.structure);
        QVERIFY(library.appendToService(1, id, &error));
        const int entry = library.service(1).last().id;
        QVERIFY(library.setEntrySlideOrder(entry, {2, 3, 0}, &error));
        const Song original = library.song(id);
        auto draft = original;
        draft.structure.sections[2].slideCount = 3;
        draft.slides.insert(4, QStringLiteral("Extra chorus page"));
        draft.richText.insert(4, QStringLiteral(""));
        QVERIFY(sql(path, QStringLiteral("CREATE TRIGGER reject_structure BEFORE UPDATE OF song_structure ON songs "
                                        "BEGIN SELECT RAISE(ABORT,'reject structure'); END")));
        QVERIFY(!library.updateSongStructure(original, draft, &error));
        QVERIFY(error.contains(QStringLiteral("reject structure")));
        QCOMPARE(library.song(id).slides, original.slides);
        QCOMPARE(library.song(id).structure.toJson(), original.structure.toJson());
        QCOMPARE(library.service(1).last().slideOrder, QVector<int>({2, 3, 0}));
    }

    void sectionEditorUpdatesRepeatedPreviewsAndSavesNamedReferences() {
        auto source = songFixture();
        source.id = 42;
        SongStructureDialog editor(source);
        auto *sections = control<ReorderListWidget>(editor, "songSections");
        auto *sequence = control<ReorderListWidget>(editor, "songArrangementSequence");
        auto *lyrics = control<QPlainTextEdit>(editor, "songSectionLyrics");
        sections->setCurrentRow(2);
        lyrics->setPlainText(QStringLiteral("Updated chorus"));
        control<ReorderListWidget>(editor, "songSectionSlides")->setCurrentRow(1);
        lyrics->setPlainText(QStringLiteral("Second chorus page"));
        QCOMPARE(sequence->count(), 7);
        for (int index : {1, 3, 5, 6}) QVERIFY(sequence->item(index)->toolTip().startsWith(QStringLiteral("Updated chorus")));
        sequence->setCurrentRow(3);
        QVERIFY(control<QPlainTextEdit>(editor, "songSectionPreview")->toPlainText().contains(QStringLiteral("Chorus • Slide 1\nUpdated chorus")));
        control<QPushButton>(editor, "appendSongSection")->click();
        QCOMPARE(sequence->count(), 8);
        QCOMPARE(editor.draft().slides.size(), 5);
        QVERIFY(sequence->moveItem(7, 0));
        QVERIFY(sections->moveItem(2, 0));
        QCOMPARE(editor.draft().structure.sections.first().id, QStringLiteral("c"));
        QTimer::singleShot(0, [] {
            auto *input = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            Q_ASSERT(input);
            input->setTextValue(QStringLiteral("Evening"));
            input->accept();
        });
        control<QPushButton>(editor, "copySongArrangement")->click();
        QCOMPARE(editor.draft().structure.arrangements.size(), 3);
        QCOMPARE(editor.draft().structure.arrangements.last().sectionIds.size(), 8);
        control<QComboBox>(editor, "songArrangements")->setCurrentIndex(1);
        QCOMPARE(sequence->count(), 2);
        const auto draft = editor.draft();
        QCOMPARE(draft.structure.activeArrangementId, QStringLiteral("short"));
        QString error;
        QVERIFY2(draft.structure.validate(int(draft.slides.size()), &error), qPrintable(error));
        if (qEnvironmentVariableIsSet("WORSHIPCUE_SONG_STRUCTURE_SCREENSHOT"))
            QVERIFY(editor.grab().save(qEnvironmentVariable("WORSHIPCUE_SONG_STRUCTURE_SCREENSHOT")));
        editor.reject();
        QCOMPARE(source.slides.at(2), QStringLiteral("Chorus first page"));
    }
};

QTEST_MAIN(SongStructureTest)
#include "song_structure_test.moc"
