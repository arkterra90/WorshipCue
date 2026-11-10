#include "../src/data/LibraryRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QtTest>

namespace {
bool rawSql(const QString &path, const QStringList &statements, QString *error = nullptr) {
    const QString connection = QUuid::createUuid().toString();
    bool success = true;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        success = db.open();
        if (!success && error) *error = db.lastError().text();
        if (success) {
            QSqlQuery query(db);
            for (const auto &statement : statements) {
                if (!query.exec(statement)) {
                    if (error) *error = query.lastError().text();
                    success = false;
                    break;
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return success;
}

QVariant rawValue(const QString &path, const QString &sql) {
    const QString connection = QUuid::createUuid().toString();
    QVariant result;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec(sql) && query.next()) result = query.value(0);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}

QVector<int> entryIds(const QVector<ServiceEntry> &entries) {
    QVector<int> result;
    for (const auto &entry : entries) result.append(entry.id);
    return result;
}
}

class LibraryRepositoryTest : public QObject {
    Q_OBJECT
private slots:
    void savedServicesPersistAndKeepIndependentArrangements() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        int sunday = 0;
        int midweek = 0;
        int sourceId = 0;
        int firstEntry = 0;
        int copy = 0;
        QVector<int> savedOrder;
        {
            LibraryRepository repository;
            QVERIFY2(repository.open(&error, path), qPrintable(error));
            QCOMPARE(repository.services().size(), 1);
            sourceId = repository.songs(QStringLiteral("Sample Song")).first().id;
            sunday = repository.createService(QStringLiteral("Sunday Morning"), &error);
            midweek = repository.createService(QStringLiteral("Wednesday"), &error);
            QVERIFY(sunday > 0);
            QVERIFY(midweek > sunday);
            QVERIFY(repository.appendToService(sunday, sourceId, &error));
            QVERIFY(repository.appendToService(sunday, sourceId, &error));
            QVERIFY(repository.appendToService(midweek, sourceId, &error));
            const auto entries = repository.service(sunday);
            QCOMPARE(entries.size(), 2);
            firstEntry = entries.first().id;
            QVERIFY(firstEntry != entries.last().id);
            QVERIFY(repository.setEntrySlideOrder(firstEntry, {2, 0, 2, 1}, &error));
            const auto lyrics = repository.song(sourceId).slides;
            QCOMPARE(repository.entrySlides(firstEntry), (QStringList{lyrics[2], lyrics[0], lyrics[2], lyrics[1]}));
            QCOMPARE(repository.entrySlides(repository.service(midweek).first().id), lyrics);
            QVERIFY(repository.duplicateServiceEntry(firstEntry, &error));
            auto duplicatedEntries = repository.service(sunday);
            QCOMPARE(duplicatedEntries.size(), 3);
            QCOMPARE(duplicatedEntries[1].slideOrder, (QVector<int>{2, 0, 2, 1}));
            QCOMPARE(repository.entrySlides(duplicatedEntries[1].id), repository.entrySlides(firstEntry));
            QVERIFY(repository.moveServiceEntry(sunday, 0, 2, &error));
            savedOrder = entryIds(repository.service(sunday));
            QCOMPARE(savedOrder.last(), firstEntry);
            QVERIFY(repository.renameService(sunday, QStringLiteral("Sunday 10:30"), &error));
            copy = repository.duplicateService(sunday, QStringLiteral("Next Sunday"), &error);
            QVERIFY(copy > 0);
            const auto copied = repository.service(copy);
            QCOMPARE(copied.size(), 3);
            QVERIFY(copied[0].id != savedOrder[0]);
            QCOMPARE(copied[2].slideOrder, (QVector<int>{2, 0, 2, 1}));
            QVERIFY(repository.removeFromService(copied[0].id, &error));
            QCOMPARE(repository.service(sunday).size(), 3);
            QVERIFY(repository.song(sourceId).id);
        }
        {
            LibraryRepository reopened;
            QVERIFY2(reopened.open(&error, path), qPrintable(error));
            QCOMPARE(entryIds(reopened.service(sunday)), savedOrder);
            QCOMPARE(reopened.service(sunday).last().slideOrder, (QVector<int>{2, 0, 2, 1}));
            QCOMPARE(reopened.service(midweek).size(), 1);
            QCOMPARE(reopened.service(copy).size(), 2);
            bool foundName = false;
            for (const auto &service : reopened.services())
                if (service.id == sunday) foundName = service.name == QStringLiteral("Sunday 10:30");
            QVERIFY(foundName);
        }
        QCOMPARE(rawValue(path, QStringLiteral("PRAGMA user_version")).toInt(), 3);
    }

    void migratesLegacyLibraryWithoutChangingIdsOrOrder() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("legacy.sqlite3"));
        QString error;
        QVERIFY2(rawSql(path, {
            QStringLiteral("CREATE TABLE songs(id INTEGER PRIMARY KEY,title TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE slides(id INTEGER PRIMARY KEY,song_id INTEGER NOT NULL REFERENCES songs(id) ON DELETE CASCADE,position INTEGER NOT NULL,text TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE service_entries(id INTEGER PRIMARY KEY,song_id INTEGER NOT NULL REFERENCES songs(id) ON DELETE CASCADE,position INTEGER NOT NULL)"),
            QStringLiteral("INSERT INTO songs VALUES(7,'Legacy Song')"),
            QStringLiteral("INSERT INTO slides VALUES(21,7,1,'Second'),(22,7,0,'First')"),
            QStringLiteral("INSERT INTO service_entries VALUES(12,7,3),(15,7,1)")
        }, &error), qPrintable(error));
        {
            LibraryRepository repository;
            QVERIFY2(repository.open(&error, path), qPrintable(error));
            QCOMPARE(repository.songs().size(), 1);
            QCOMPARE(repository.song(7).slides, (QStringList{"First", "Second"}));
            QCOMPARE(repository.song(7).settings, PresentationSettings{});
            QCOMPARE(repository.services().size(), 1);
            QCOMPARE(entryIds(repository.service(1)), (QVector<int>{15, 12}));
            QCOMPARE(repository.entrySlides(15), (QStringList{"First", "Second"}));
            QVERIFY(repository.appendToService(1, 7, &error));
            QVERIFY(repository.service(1).last().id > 15);
        }
        LibraryRepository reopened;
        QVERIFY2(reopened.open(&error, path), qPrintable(error));
        QCOMPARE(reopened.service(1).size(), 3);
    }

    void failedMigrationRollsBackAndFutureSchemaIsRejected() {
        QTemporaryDir directory;
        const QString badPath = directory.filePath(QStringLiteral("broken.sqlite3"));
        QString error;
        QVERIFY(rawSql(badPath, {
            QStringLiteral("CREATE TABLE songs(id INTEGER PRIMARY KEY,title TEXT NOT NULL)"),
            QStringLiteral("INSERT INTO songs VALUES(9,'Keep this')"),
            QStringLiteral("CREATE TABLE service_entries(id INTEGER PRIMARY KEY,song_id INTEGER)")
        }, &error));
        {
            LibraryRepository repository;
            QVERIFY(!repository.open(&error, badPath));
            QVERIFY(!error.isEmpty());
        }
        QCOMPARE(rawValue(badPath, QStringLiteral("SELECT title FROM songs WHERE id=9")).toString(), QStringLiteral("Keep this"));
        QCOMPARE(rawValue(badPath, QStringLiteral("PRAGMA user_version")).toInt(), 0);
        QCOMPARE(rawValue(badPath, QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE name='services'")).toInt(), 0);

        const QString futurePath = directory.filePath(QStringLiteral("future.sqlite3"));
        QVERIFY(rawSql(futurePath, {QStringLiteral("PRAGMA user_version=999")}, &error));
        LibraryRepository repository;
        QVERIFY(!repository.open(&error, futurePath));
        QVERIFY(error.contains(QStringLiteral("newer")));
        QCOMPARE(rawValue(futurePath, QStringLiteral("PRAGMA user_version")).toInt(), 999);
        QCOMPARE(rawValue(futurePath, QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='table'")).toInt(), 0);
    }

    void validatesReorderAndRollsBackPartialWrites() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
        LibraryRepository repository;
        QString error;
        QVERIFY(repository.open(&error, path));
        const auto original = entryIds(repository.service(1));
        QCOMPARE(original.size(), 2);
        const int other = repository.createService(QStringLiteral("Other"), &error);
        QVERIFY(repository.appendToService(other, repository.songs().first().id, &error));
        const int foreignEntry = repository.service(other).first().id;
        QVERIFY(!repository.reorderServiceEntries(1, {original[0], foreignEntry}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!repository.reorderServiceEntries(1, {original[0], original[0]}, &error));
        QVERIFY(!repository.reorderServiceEntries(1, {original[0]}, &error));
        QVERIFY(!repository.moveServiceEntry(1, -1, 0, &error));
        QVERIFY(!repository.reorderServiceEntries(999, {}, &error));
        QCOMPARE(entryIds(repository.service(1)), original);

        QVERIFY(rawSql(path, {QStringLiteral("CREATE TRIGGER reject_reorder BEFORE UPDATE OF position ON service_entries "
            "WHEN OLD.id=%1 BEGIN SELECT RAISE(ABORT,'test write failure'); END").arg(original[0])}, &error));
        QVERIFY(!repository.reorderServiceEntries(1, {original[1], original[0]}, &error));
        QVERIFY(error.contains(QStringLiteral("test write failure")));
        QCOMPARE(entryIds(repository.service(1)), original);
        QCOMPARE(rawValue(path, QStringLiteral("SELECT position FROM service_entries WHERE id=%1").arg(original[1])).toInt(), 1);
        QVERIFY(rawSql(path, {QStringLiteral("DROP TRIGGER reject_reorder")}, &error));
        QVERIFY(repository.reorderServiceEntries(1, {original[1], original[0]}, &error));
        QCOMPARE(entryIds(repository.service(1)), (QVector<int>{original[1], original[0]}));

        const int count = repository.services().size();
        QVERIFY(rawSql(path, {QStringLiteral("CREATE TRIGGER reject_copy BEFORE INSERT ON service_entries BEGIN SELECT RAISE(ABORT,'test copy failure'); END")}, &error));
        QCOMPARE(repository.duplicateService(1, QStringLiteral("Should roll back"), &error), 0);
        QCOMPARE(repository.services().size(), count);
        const auto beforeDuplicate = entryIds(repository.service(1));
        QVERIFY(!repository.duplicateServiceEntry(beforeDuplicate[0], &error));
        QCOMPARE(entryIds(repository.service(1)), beforeDuplicate);
        QCOMPARE(rawValue(path, QStringLiteral("SELECT position FROM service_entries WHERE id=%1").arg(beforeDuplicate[1])).toInt(), 1);
    }

    void lyricSearchUsesLiteralWildcardsAndSongDeletionCascades() {
        QTemporaryDir directory;
        LibraryRepository repository;
        QString error;
        QVERIFY(repository.open(&error, directory.filePath(QStringLiteral("library.sqlite3"))));
        QVERIFY(repository.createSong(QStringLiteral("100% Real_Song\\Name"), {QStringLiteral("A quiet promise\nStill shines")}, &error));
        QCOMPARE(repository.songs(QStringLiteral("quiet promise")).size(), 1);
        QCOMPARE(repository.songs(QStringLiteral("100% real")).size(), 1);
        QCOMPARE(repository.songs(QStringLiteral("%" )).size(), 1);
        QCOMPARE(repository.songs(QStringLiteral("_" )).size(), 1);
        QCOMPARE(repository.songs(QStringLiteral("\\")).size(), 1);
        QVERIFY(repository.songs(QStringLiteral("no matching words")).isEmpty());
        const int songId = repository.songs(QStringLiteral("quiet promise")).first().id;
        QVERIFY(repository.appendToService(1, songId, &error));
        const int another = repository.createService(QStringLiteral("Another"), &error);
        QVERIFY(repository.appendToService(another, songId, &error));
        QVERIFY(repository.deleteSong(songId, &error));
        QCOMPARE(repository.service(1).size(), 2);
        QVERIFY(repository.service(another).isEmpty());
        QVERIFY(repository.songs(QStringLiteral("quiet promise")).isEmpty());
        QVERIFY(!repository.deleteSong(songId, &error));
    }

    void arrangementValidationAndSongEditsAreAtomic() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
        LibraryRepository repository;
        QString error;
        QVERIFY(repository.open(&error, path));
        const auto entries = repository.service(1);
        const int entry = entries[1].id;
        const int songId = entries[1].songId;
        QVERIFY(repository.setEntrySlideOrder(entry, {2, 0, 2}, &error));
        QVERIFY(!repository.setEntrySlideOrder(entry, {3}, &error));
        QVERIFY(!repository.setEntrySlideOrder(entry, {-1}, &error));
        QVERIFY(!repository.setEntrySlideOrder(999, {0}, &error));
        QCOMPARE(repository.service(1)[1].slideOrder, (QVector<int>{2, 0, 2}));
        QVERIFY(repository.updateSong(songId, QStringLiteral("Edited"), {"A", "B", "C"}, &error));
        QCOMPARE(repository.entrySlides(entry), (QStringList{"C", "A", "C"}));
        QVERIFY(repository.updateSong(songId, QStringLiteral("Shorter"), {"A", "B"}, &error));
        QVERIFY(repository.service(1)[1].slideOrder.isEmpty());
        QCOMPARE(repository.entrySlides(entry), (QStringList{"A", "B"}));
        QVERIFY(repository.setEntrySlideOrder(entry, {1, 0}, &error));
        QVERIFY(rawSql(path, {QStringLiteral("CREATE TRIGGER reject_slide BEFORE INSERT ON slides WHEN NEW.text='Fail' "
            "BEGIN SELECT RAISE(ABORT,'test slide failure'); END")}, &error));
        QVERIFY(!repository.updateSong(songId, QStringLiteral("Do not save"), {"New", "Fail", "Third"}, &error));
        QCOMPARE(repository.song(songId).title, QStringLiteral("Shorter"));
        QCOMPARE(repository.song(songId).slides, (QStringList{"A", "B"}));
        QCOMPARE(repository.service(1)[1].slideOrder, (QVector<int>{1, 0}));
        QVERIFY(repository.setEntrySlideOrder(entry, {}, &error));
        QCOMPARE(repository.entrySlides(entry), (QStringList{"A", "B"}));
        QVERIFY(!repository.createSong(QStringLiteral("  "), {"Text"}, &error));
        QVERIFY(!repository.updateSong(999, QStringLiteral("Missing"), {"Text"}, &error));
        QCOMPARE(repository.createService(QStringLiteral("  "), &error), 0);
        QVERIFY(!repository.renameService(999, QStringLiteral("Missing"), &error));
        QCOMPARE(repository.duplicateService(999, QStringLiteral("Missing"), &error), 0);
        QVERIFY(!repository.appendToService(999, songId, &error));
        QVERIFY(!repository.appendToService(1, 999, &error));
        QVERIFY(!repository.removeFromService(999, &error));
        QVERIFY(!repository.duplicateServiceEntry(999, &error));
    }

    void emptyLibraryStaysEmptyAndConnectionsAreIndependent() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        {
            LibraryRepository repository;
            QVERIFY(repository.open(&error, path));
            for (const auto &song : repository.songs()) QVERIFY(repository.deleteSong(song.id, &error));
        }
        LibraryRepository first;
        LibraryRepository second;
        QVERIFY(first.open(&error, path));
        QVERIFY(second.open(&error, path));
        QVERIFY(first.songs().isEmpty());
        QVERIFY(second.songs().isEmpty());
        QVERIFY(second.createSong(QStringLiteral("New"), {"Text"}, &error));
        QCOMPARE(first.songs().size(), 1);
        QVERIFY(first.open(&error, QStringLiteral(":memory:")));
        QCOMPARE(first.songs().size(), 2);
        QCOMPARE(second.songs().size(), 1);
    }

    void mediaPresentationsPersistAndArrangementsFollowSourceMedia() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("media.sqlite3"));
        QString error;
        int presentationId = 0;
        int entryId = 0;
        int copiedService = 0;
        const QVector<SlideMedia> media = {
            {MediaKind::Image, QStringLiteral("media/import1/Sunday 01.jpg"), QStringLiteral("Sunday 01.jpg"), false},
            {MediaKind::Video, QStringLiteral("media/import1/Welcome.mp4"), QStringLiteral("Welcome.mp4"), true},
            {}
        };
        const QStringList texts = {QStringLiteral(""), QStringLiteral(""), QStringLiteral("Welcome text")};
        {
            LibraryRepository repository;
            QVERIFY2(repository.open(&error, path), qPrintable(error));
            QCOMPARE(repository.databaseDirectory(), directory.path());
            presentationId = repository.createPresentation(QStringLiteral("Media Service"), texts, media, &error);
            QVERIFY2(presentationId > 0, qPrintable(error));
            const Song loaded = repository.song(presentationId);
            QCOMPARE(loaded.slides, texts);
            QCOMPARE(loaded.media, media);
            QCOMPARE(repository.songs(QStringLiteral("Sunday 01.jpg")).size(), 1);
            QVERIFY(repository.appendToService(1, presentationId, &error));
            entryId = repository.service(1).last().id;
            QVERIFY(repository.setEntrySlideOrder(entryId, {1, 0, 1, 2}, &error));
            QCOMPARE(repository.entryMedia(entryId), (QVector<SlideMedia>{media[1], media[0], media[1], media[2]}));
            QCOMPARE(repository.entrySlides(entryId), (QStringList{QStringLiteral(""), QStringLiteral(""), QStringLiteral(""), QStringLiteral("Welcome text")}));
            QVERIFY(repository.duplicateServiceEntry(entryId, &error));
            QCOMPARE(repository.entryMedia(repository.service(1).last().id), repository.entryMedia(entryId));
            copiedService = repository.duplicateService(1, QStringLiteral("Next Week"), &error);
            QVERIFY(copiedService > 0);
            QCOMPARE(repository.entryMedia(repository.service(copiedService)[2].id), repository.entryMedia(entryId));
            QVERIFY(!repository.updateSong(presentationId, QStringLiteral("Overwrite"), {QStringLiteral("Deleted media")}, &error));
            QVERIFY(error.contains(QStringLiteral("media")));
            QCOMPARE(repository.song(presentationId).media, media);
        }
        LibraryRepository reopened;
        QVERIFY2(reopened.open(&error, path), qPrintable(error));
        QCOMPARE(reopened.song(presentationId).media, media);
        QCOMPARE(reopened.entryMedia(entryId)[0].kind, MediaKind::Video);
        QVERIFY(reopened.entryMedia(entryId)[0].loop);
        QCOMPARE(reopened.service(copiedService)[2].slideOrder, (QVector<int>{1, 0, 1, 2}));
        const auto textSong = reopened.songs(QStringLiteral("Sample Song")).first();
        QCOMPARE(textSong.media.size(), textSong.slides.size());
        for (const auto &item : textSong.media) QCOMPARE(item.kind, MediaKind::None);
    }

    void mediaNamesRenameAtomicallyAndPathsStayStable() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("rename.sqlite3"));
        LibraryRepository repository;
        QString error;
        QVERIFY(repository.open(&error, path));
        const QVector<SlideMedia> media = {
            {MediaKind::Image, QStringLiteral("media/session/a.jpg"), QStringLiteral("a.jpg"), false},
            {MediaKind::Image, QStringLiteral("media/session/b.jpg"), QStringLiteral("b.jpg"), false}
        };
        const int id = repository.createPresentation(QStringLiteral("Pictures"), {QStringLiteral(""), QStringLiteral("")}, media, &error);
        QVERIFY2(id > 0, qPrintable(error));
        QVERIFY(repository.updatePresentationMediaNames(id, {QStringLiteral("Offering 01.jpg"), QStringLiteral("Offering 02.jpg")}, &error));
        auto loaded = repository.song(id);
        QCOMPARE(loaded.media[0].name, QStringLiteral("Offering 01.jpg"));
        QCOMPARE(loaded.media[1].name, QStringLiteral("Offering 02.jpg"));
        QCOMPARE(loaded.media[0].path, media[0].path);
        QCOMPARE(loaded.media[1].path, media[1].path);
        QCOMPARE(repository.songs(QStringLiteral("Offering 02")).size(), 1);
        QVERIFY(repository.renamePresentation(id, QStringLiteral("Offering Pictures"), &error));
        QCOMPARE(repository.song(id).title, QStringLiteral("Offering Pictures"));
        QCOMPARE(repository.song(id).media, loaded.media);
        QVERIFY(!repository.renamePresentation(id, QStringLiteral("  "), &error));
        QVERIFY(!repository.renamePresentation(999, QStringLiteral("Missing"), &error));
        QVERIFY(repository.songs(QStringLiteral("b.jpg")).isEmpty());
        QVERIFY(!repository.updatePresentationMediaNames(id, {QStringLiteral("Same.jpg"), QStringLiteral("same.JPG")}, &error));
        QVERIFY(!repository.updatePresentationMediaNames(id, {QStringLiteral("Only one.jpg")}, &error));
        QVERIFY(!repository.updatePresentationMediaNames(id, {QStringLiteral("../a.jpg"), QStringLiteral("b.jpg")}, &error));
        QVERIFY(rawSql(path, {QStringLiteral("CREATE TRIGGER reject_rename BEFORE UPDATE OF media_name ON slides "
            "WHEN OLD.media_name='Offering 02.jpg' BEGIN SELECT RAISE(ABORT,'test rename failure'); END")}, &error));
        QVERIFY(!repository.updatePresentationMediaNames(id, {QStringLiteral("New 01.jpg"), QStringLiteral("New 02.jpg")}, &error));
        QCOMPARE(repository.song(id).media, loaded.media);
        QVERIFY(!repository.updatePresentationMediaNames(999, {}, &error));
    }

    void rejectsInvalidMediaAndRollsBackFailedImport() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("invalid.sqlite3"));
        LibraryRepository repository;
        QString error;
        QVERIFY(repository.open(&error, path));
        const int before = repository.songs().size();
        const QStringList texts = {QStringLiteral("")};
        QVERIFY(!repository.createPresentation(QStringLiteral("Empty"), {}, {}, &error));
        QVERIFY(!repository.createPresentation(QStringLiteral("Text"), {QStringLiteral("Only text")}, {{}}, &error));
        QVERIFY(!repository.createPresentation(QStringLiteral("Missing"), texts, {}, &error));
        QVERIFY(!repository.createPresentation(QStringLiteral("Absolute"), texts,
            {{MediaKind::Image, QStringLiteral("/tmp/a.jpg"), QStringLiteral("a.jpg"), false}}, &error));
        QVERIFY(!repository.createPresentation(QStringLiteral("Traversal"), texts,
            {{MediaKind::Image, QStringLiteral("media/../a.jpg"), QStringLiteral("a.jpg"), false}}, &error));
        QVERIFY(!repository.createPresentation(QStringLiteral("Empty name"), texts,
            {{MediaKind::Image, QStringLiteral("media/a.jpg"), QString(), false}}, &error));
        QVERIFY(!repository.createPresentation(QStringLiteral("Loop image"), texts,
            {{MediaKind::Image, QStringLiteral("media/a.jpg"), QStringLiteral("a.jpg"), true}}, &error));
        const QVector<SlideMedia> duplicateNames = {
            {MediaKind::Image, QStringLiteral("media/a.jpg"), QStringLiteral("Same.jpg"), false},
            {MediaKind::Image, QStringLiteral("media/b.jpg"), QStringLiteral("same.JPG"), false}
        };
        QVERIFY(!repository.createPresentation(QStringLiteral("Duplicate"), {QStringLiteral(""), QStringLiteral("")}, duplicateNames, &error));
        QCOMPARE(repository.songs().size(), before);
        QVERIFY(rawSql(path, {QStringLiteral("CREATE TRIGGER reject_import BEFORE INSERT ON slides "
            "WHEN NEW.media_name='Fail.jpg' BEGIN SELECT RAISE(ABORT,'test media failure'); END")}, &error));
        const QVector<SlideMedia> media = {
            {MediaKind::Image, QStringLiteral("media/a.jpg"), QStringLiteral("Good.jpg"), false},
            {MediaKind::Image, QStringLiteral("media/b.jpg"), QStringLiteral("Fail.jpg"), false}
        };
        QVERIFY(!repository.createPresentation(QStringLiteral("Rollback"), {QStringLiteral(""), QStringLiteral("")}, media, &error));
        QVERIFY(error.contains(QStringLiteral("test media failure")));
        QCOMPARE(repository.songs().size(), before);
        QCOMPARE(rawValue(path, QStringLiteral("SELECT COUNT(*) FROM slides WHERE media_kind<>''")).toInt(), 0);
    }

    void migratesVersionOneAndRollsBackPartialMediaMigration() {
        QTemporaryDir directory;
        QString error;
        const auto createV1 = [](const QString &path, bool existingPathColumn) {
            return rawSql(path, {
                QStringLiteral("CREATE TABLE songs(id INTEGER PRIMARY KEY,title TEXT NOT NULL)"),
                QStringLiteral("CREATE TABLE slides(id INTEGER PRIMARY KEY,song_id INTEGER NOT NULL REFERENCES songs(id),position INTEGER NOT NULL,text TEXT NOT NULL%1)")
                    .arg(existingPathColumn ? QStringLiteral(",media_path TEXT") : QString()),
                QStringLiteral("CREATE TABLE services(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL)"),
                QStringLiteral("CREATE TABLE service_entries(id INTEGER PRIMARY KEY AUTOINCREMENT,service_id INTEGER NOT NULL REFERENCES services(id),song_id INTEGER NOT NULL REFERENCES songs(id),position INTEGER NOT NULL,slide_order TEXT)"),
                QStringLiteral("INSERT INTO songs VALUES(17,'Existing Song')"),
                QStringLiteral("INSERT INTO slides(id,song_id,position,text) VALUES(23,17,0,'Keep lyric')"),
                QStringLiteral("INSERT INTO services(id,name) VALUES(4,'Existing Service')"),
                QStringLiteral("INSERT INTO service_entries VALUES(29,4,17,0,'[0,0]')"),
                QStringLiteral("PRAGMA user_version=1")
            });
        };
        const QString path = directory.filePath(QStringLiteral("v1.sqlite3"));
        QVERIFY(createV1(path, false));
        {
            LibraryRepository repository;
            QVERIFY2(repository.open(&error, path), qPrintable(error));
            QCOMPARE(repository.song(17).slides, (QStringList{QStringLiteral("Keep lyric")}));
            QCOMPARE(repository.song(17).media.size(), 1);
            QCOMPARE(repository.song(17).media.first().kind, MediaKind::None);
            QCOMPARE(repository.entrySlides(29), (QStringList{QStringLiteral("Keep lyric"), QStringLiteral("Keep lyric")}));
            QCOMPARE(repository.service(4).first().id, 29);
        }
        QCOMPARE(rawValue(path, QStringLiteral("PRAGMA user_version")).toInt(), 3);
        const QString badPath = directory.filePath(QStringLiteral("bad-v1.sqlite3"));
        QVERIFY(createV1(badPath, true));
        {
            LibraryRepository repository;
            QVERIFY(!repository.open(&error, badPath));
            QVERIFY(!error.isEmpty());
        }
        QCOMPARE(rawValue(badPath, QStringLiteral("PRAGMA user_version")).toInt(), 1);
        QCOMPARE(rawValue(badPath, QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('slides') WHERE name='media_kind'")).toInt(), 0);
        QCOMPARE(rawValue(badPath, QStringLiteral("SELECT text FROM slides WHERE id=23")).toString(), QStringLiteral("Keep lyric"));
    }

    void presentationSettingsPersistAndSurviveContentEdits() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("timing.sqlite3"));
        const PresentationSettings settings{7250, true, SlideTransitionType::Fade, 1250};
        QString error;
        int textId = 0;
        int mediaId = 0;
        {
            LibraryRepository repository;
            QVERIFY2(repository.open(&error, path), qPrintable(error));
            QCOMPARE(repository.song(1).settings, PresentationSettings{});
            QVERIFY(repository.createSong(QStringLiteral("Timed Song"), {QStringLiteral("One"), QStringLiteral("Two")}, &error, settings));
            textId = repository.songs(QStringLiteral("Timed Song")).first().id;
            QCOMPARE(repository.song(textId).settings, settings);
            QVERIFY(repository.updateSong(textId, QStringLiteral("Edited Timed Song"), {QStringLiteral("New lyric")}, &error));
            QCOMPARE(repository.song(textId).settings, settings);
            const QVector<SlideMedia> media{{MediaKind::Image, QStringLiteral("media/import/a.jpg"), QStringLiteral("a.jpg"), false}};
            mediaId = repository.createPresentation(QStringLiteral("Timed Picture"), {QStringLiteral("")}, media, &error, settings);
            QVERIFY2(mediaId > 0, qPrintable(error));
            QCOMPARE(repository.song(mediaId).settings, settings);
            QVERIFY(repository.renamePresentation(mediaId, QStringLiteral("Renamed Picture"), &error));
            QVERIFY(repository.updatePresentationMediaNames(mediaId, {QStringLiteral("new.jpg")}, &error));
            QCOMPARE(repository.song(mediaId).settings, settings);
            QVERIFY(repository.appendToService(1, mediaId, &error));
            const int copied = repository.duplicateService(1, QStringLiteral("Copied Service"), &error);
            QVERIFY(copied > 0);
            QCOMPARE(repository.song(repository.service(copied).last().songId).settings, settings);
            const PresentationSettings changed{9500, false, SlideTransitionType::SlideRight, 300};
            QVERIFY(repository.updatePresentationSettings(textId, changed, &error));
            QCOMPARE(repository.song(textId).settings, changed);
        }
        LibraryRepository reopened;
        QVERIFY2(reopened.open(&error, path), qPrintable(error));
        QCOMPARE(reopened.song(textId).settings, (PresentationSettings{9500, false, SlideTransitionType::SlideRight, 300}));
        QCOMPARE(reopened.song(mediaId).settings, settings);
    }

    void validatesSettingsAndKeepsUpdatesAtomic() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("invalid-timing.sqlite3"));
        LibraryRepository repository;
        QString error;
        QVERIFY(repository.open(&error, path));
        const PresentationSettings valid{4000, true, SlideTransitionType::SlideLeft, 700};
        QVERIFY(repository.updatePresentationSettings(1, valid, &error));
        QCOMPARE(repository.song(1).settings, valid);
        const QVector<PresentationSettings> invalid{
            {-1, false, SlideTransitionType::Cut, 500},
            {99, false, SlideTransitionType::Cut, 500},
            {3600001, false, SlideTransitionType::Cut, 500},
            {0, false, SlideTransitionType(-1), 500},
            {0, false, SlideTransitionType(4), 500},
            {0, false, SlideTransitionType::Fade, -1},
            {0, false, SlideTransitionType::Fade, 10001}
        };
        for (const auto &settings : invalid) {
            QVERIFY(!repository.updatePresentationSettings(1, settings, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(repository.song(1).settings, valid);
            QVERIFY(!repository.createSong(QStringLiteral("Invalid Song"), {QStringLiteral("Lyric")}, &error, settings));
            QVERIFY(!repository.createPresentation(QStringLiteral("Invalid Picture"), {QStringLiteral("")},
                {{MediaKind::Image, QStringLiteral("media/a.jpg"), QStringLiteral("a.jpg"), false}}, &error, settings));
        }
        QVERIFY(!repository.updatePresentationSettings(999, valid, &error));
        QVERIFY(error.contains(QStringLiteral("no longer exists")));
        QVERIFY(rawSql(path, {QStringLiteral("CREATE TRIGGER reject_settings BEFORE UPDATE OF transition_type ON songs "
            "BEGIN SELECT RAISE(ABORT,'test settings failure'); END")}, &error));
        QVERIFY(!repository.updatePresentationSettings(1, PresentationSettings{}, &error));
        QVERIFY(error.contains(QStringLiteral("test settings failure")));
        QCOMPARE(repository.song(1).settings, valid);
        QVERIFY(rawSql(path, {
            QStringLiteral("DROP TRIGGER reject_settings"),
            QStringLiteral("PRAGMA ignore_check_constraints=ON"),
            QStringLiteral("UPDATE songs SET transition_type=99 WHERE id=1")
        }, &error));
        QCOMPARE(repository.song(1).id, 0);
        QVERIFY(repository.lastError().contains(QStringLiteral("settings")));
    }

    void migratesVersionTwoSettingsAndRollsBackPartialMigration() {
        QTemporaryDir directory;
        QString error;
        const auto createV2 = [](const QString &path, bool duplicateTransitionColumn) {
            return rawSql(path, {
                QStringLiteral("CREATE TABLE songs(id INTEGER PRIMARY KEY,title TEXT NOT NULL%1)")
                    .arg(duplicateTransitionColumn ? QStringLiteral(",transition_type INTEGER") : QString()),
                QStringLiteral("CREATE TABLE slides(id INTEGER PRIMARY KEY,song_id INTEGER NOT NULL REFERENCES songs(id),position INTEGER NOT NULL,text TEXT NOT NULL,"
                               "media_kind TEXT NOT NULL DEFAULT '',media_path TEXT NOT NULL DEFAULT '',media_name TEXT NOT NULL DEFAULT '',media_loop INTEGER NOT NULL DEFAULT 0)"),
                QStringLiteral("CREATE TABLE services(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL)"),
                QStringLiteral("CREATE TABLE service_entries(id INTEGER PRIMARY KEY AUTOINCREMENT,service_id INTEGER NOT NULL REFERENCES services(id),song_id INTEGER NOT NULL REFERENCES songs(id),position INTEGER NOT NULL,slide_order TEXT)"),
                QStringLiteral("INSERT INTO songs(id,title) VALUES(17,'Existing Picture')"),
                QStringLiteral("INSERT INTO slides(id,song_id,position,text,media_kind,media_path,media_name) VALUES(23,17,0,'','image','media/session/a.jpg','a.jpg')"),
                QStringLiteral("INSERT INTO services(id,name) VALUES(4,'Existing Service')"),
                QStringLiteral("INSERT INTO service_entries VALUES(29,4,17,0,'[0,0]')"),
                QStringLiteral("PRAGMA user_version=2")
            });
        };
        const QString path = directory.filePath(QStringLiteral("v2.sqlite3"));
        QVERIFY(createV2(path, false));
        {
            LibraryRepository repository;
            QVERIFY2(repository.open(&error, path), qPrintable(error));
            const Song song = repository.song(17);
            QCOMPARE(song.settings, PresentationSettings{});
            QCOMPARE(song.media.first().path, QStringLiteral("media/session/a.jpg"));
            QCOMPARE(repository.service(4).first().id, 29);
            QCOMPARE(repository.service(4).first().slideOrder, (QVector<int>{0, 0}));
            QVERIFY(repository.updatePresentationSettings(17, {100, false, SlideTransitionType::Fade, 0}, &error));
        }
        QCOMPARE(rawValue(path, QStringLiteral("PRAGMA user_version")).toInt(), 3);
        const QString badPath = directory.filePath(QStringLiteral("bad-v2.sqlite3"));
        QVERIFY(createV2(badPath, true));
        {
            LibraryRepository repository;
            QVERIFY(!repository.open(&error, badPath));
            QVERIFY(!error.isEmpty());
        }
        QCOMPARE(rawValue(badPath, QStringLiteral("PRAGMA user_version")).toInt(), 2);
        QCOMPARE(rawValue(badPath, QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('songs') WHERE name IN ('auto_advance_ms','presentation_loop','transition_duration_ms')")).toInt(), 0);
        QCOMPARE(rawValue(badPath, QStringLiteral("SELECT title FROM songs WHERE id=17")).toString(), QStringLiteral("Existing Picture"));
        QCOMPARE(rawValue(badPath, QStringLiteral("SELECT media_path FROM slides WHERE id=23")).toString(), QStringLiteral("media/session/a.jpg"));
    }

    void readErrorsAreAvailableToOperator() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
        QString error;
        LibraryRepository repository;
        QVERIFY(repository.open(&error, path));
        QVERIFY(rawSql(path, {QStringLiteral("DROP TABLE slides")}, &error));
        QVERIFY(repository.songs().isEmpty());
        QVERIFY(!repository.lastError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(LibraryRepositoryTest)
#include "library_repository_test.moc"
