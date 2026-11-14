#include "fixtures/SchemaDowngrade.h"
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
    void bulkTransitionsAreAtomicPersistentAndPreserveUnrelatedSettings() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("transitions.sqlite3"));
        QString error;
        LibraryRepository repository;
        QVERIFY(repository.open(&error, path));
        const PresentationSettings first{1200, true, SlideTransitionType::Cut, 500, false};
        const PresentationSettings second{2100, false, SlideTransitionType::SlideRight, 900, true};
        const int a = repository.createTextPresentation("A", {"one", "two"}, &error, first, "backgrounds/a.jpg");
        const int b = repository.createTextPresentation("B", {"three"}, &error, second);
        const int plan = repository.createServiceOrder("Bulk", {{a, {}, {}, {1,0,1}}, {b, {}, {}, {}}}, &error);
        const auto assetIds = repository.registerMediaAssets({{MediaKind::Video, "media/clip.mov", "Clip", true}}, &error);
        QCOMPARE(assetIds.size(), 1);
        const int mediaEntry = repository.appendMediaToService(plan, assetIds.first(), &error);
        const int otherMediaEntry = repository.appendMediaToService(plan, assetIds.first(), &error);
        const auto entries = repository.service(plan);
        const auto fade = [](PresentationSettings settings) {
            settings.transition = SlideTransitionType::Fade;
            settings.transitionDurationMs = 750;
            settings.overrideTransition = true;
            return settings;
        };
        const auto originalMedia = repository.entrySource(mediaEntry);
        QVector<TransitionChange> changes{
            {TransitionChange::Target::Presentation, a, first, fade(first)},
            {TransitionChange::Target::Presentation, b, second, fade(second)},
            {TransitionChange::Target::MediaEntry, mediaEntry, originalMedia.settings, fade(originalMedia.settings)}};
        QVERIFY2(repository.updateTransitions(changes, &error), qPrintable(error));
        QCOMPARE(repository.song(a).settings, fade(first));
        QCOMPARE(repository.song(b).settings, fade(second));
        QCOMPARE(repository.song(a).backgroundPath, QStringLiteral("backgrounds/a.jpg"));
        QCOMPARE(repository.song(a).slides, (QStringList{"one", "two"}));
        QCOMPARE(repository.service(plan).first().slideOrder, (QVector<int>{1,0,1}));
        QCOMPARE(entryIds(repository.service(plan)), entryIds(entries));
        QCOMPARE(repository.entrySource(mediaEntry).media, originalMedia.media);
        QVERIFY(repository.entrySource(mediaEntry).settings.sameTransition(fade(originalMedia.settings)));
        QVERIFY(!repository.entrySource(otherMediaEntry).settings.overrideTransition);
        // A later timer edit survives undo and redo.
        auto changedTiming = repository.song(a).settings;
        changedTiming.autoAdvanceMs = 4300; changedTiming.loop = false;
        QVERIFY(repository.updatePresentationSettings(a, changedTiming, &error));
        auto inverse = changes;
        for (auto &change : inverse) std::swap(change.before, change.after);
        QVERIFY2(repository.updateTransitions(inverse, &error), qPrintable(error));
        QVERIFY(repository.song(a).settings.sameTransition(first));
        QCOMPARE(repository.song(a).settings.autoAdvanceMs, 4300);
        QVERIFY(!repository.song(a).settings.loop);
        QVERIFY(repository.updateTransitions(changes, &error));
        // A later individual override prevents the whole undo, including earlier rows.
        auto individual = repository.song(b).settings;
        individual.transition = SlideTransitionType::SlideLeft;
        QVERIFY(repository.updatePresentationSettings(b, individual, &error));
        QVERIFY(!repository.updateTransitions(inverse, &error));
        QVERIFY(error.contains(QStringLiteral("changed")));
        QVERIFY(repository.song(a).settings.sameTransition(fade(first)));
        QVERIFY(repository.entrySource(mediaEntry).settings.sameTransition(fade(originalMedia.settings)));
        auto missing = changes; missing[1].id = 999999;
        for (auto &change : missing) { change.before = fade(change.before); change.after.transition = SlideTransitionType::Cut; }
        QVERIFY(!repository.updateTransitions(missing, &error));
        QVERIFY(repository.song(a).settings.sameTransition(fade(first)));
        auto invalid = inverse; invalid.last().after.transitionDurationMs = -1;
        QVERIFY(!repository.updateTransitions(invalid, &error));
        QVERIFY(repository.song(a).settings.sameTransition(fade(first)));
        QVERIFY(!repository.updateTransitions({changes.first(), changes.first()}, &error));
        // Media copies keep their overrides, and a reopened connection sees persisted values.
        QVERIFY(repository.duplicateServiceEntry(mediaEntry, &error));
        const auto copiedEntries = repository.service(plan);
        QVERIFY(repository.entrySource(copiedEntries.at(3).id).settings.sameTransition(fade(originalMedia.settings)));
        const int copiedPlan = repository.duplicateService(plan, "Copy", &error);
        QVERIFY(copiedPlan > 0);
        QVERIFY(repository.entrySource(repository.service(copiedPlan).at(2).id).settings.sameTransition(fade(originalMedia.settings)));
        LibraryRepository reopened;
        QVERIFY(reopened.open(&error, path));
        QCOMPARE(reopened.song(b).settings, individual);
        QVERIFY(reopened.entrySource(mediaEntry).settings.sameTransition(fade(originalMedia.settings)));
    }

    void versionEightTransitionMigrationPreservesEntriesAndRollsBackFailure() {
        QTemporaryDir directory;
        for (bool conflict : {false, true}) {
            const auto path = directory.filePath(conflict ? "bad.sqlite3" : "good.sqlite3");
            QString error;
            QVector<int> ids;
            int plan;
            {
                LibraryRepository repository;
                QVERIFY(repository.open(&error, path));
                plan = repository.services().first().id;
                const auto assets = repository.registerMediaAssets({{MediaKind::Image, "media/picture.jpg", "Picture", false}}, &error);
                QVERIFY(repository.appendMediaToService(plan, assets.first(), &error));
                ids = entryIds(repository.service(plan));
            }
            QStringList statements{
                "ALTER TABLE service_entries DROP COLUMN media_transition_type",
                "ALTER TABLE service_entries DROP COLUMN media_transition_duration_ms"};
            if (!conflict) statements.append("ALTER TABLE service_entries DROP COLUMN media_transition_override");
            QVERIFY(removeCleanupSchema(path));
            statements.prepend("DROP TABLE midi_slide_actions");
            statements.prepend("DROP TABLE midi_bindings");
            statements.append("PRAGMA user_version=8");
            QVERIFY2(rawSql(path, statements, &error), qPrintable(error));
            LibraryRepository migrated;
            QCOMPARE(migrated.open(&error, path), !conflict);
            QCOMPARE(rawValue(path, "PRAGMA user_version").toInt(), conflict ? 8 : 11);
            if (conflict) {
                QVERIFY(!error.isEmpty());
                QCOMPARE(rawValue(path, "SELECT COUNT(*) FROM pragma_table_info('service_entries') WHERE name='media_transition_type'").toInt(), 0);
                QCOMPARE(rawValue(path, "SELECT COUNT(*) FROM pragma_table_info('service_entries') WHERE name='media_transition_duration_ms'").toInt(), 0);
            } else {
                QCOMPARE(entryIds(migrated.service(plan)), ids);
                QCOMPARE(migrated.entrySource(ids.last()).settings, PresentationSettings{});
            }
        }
    }

    void completeServiceOrdersPersistWithNewAndRepeatedPresentations() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("service-order.sqlite3"));
        QString error;
        int serviceId = 0;
        QVector<int> ids;
        {
            LibraryRepository repository;
            QVERIFY2(repository.open(&error, path), qPrintable(error));
            const Song original = repository.song(2);
            const int oldSongs = repository.songs().size();
            serviceId = repository.createServiceOrder(QStringLiteral("  Sunday 10:30  "), {
                {0, QStringLiteral("  Welcome & Prayer  "), {QStringLiteral(" Welcome everyone "), QStringLiteral("Let us pray")}, {1, 0, 1}},
                {2, {}, {}, {2, 0, 2, 1}},
                {2, {}, {}, {}}
            }, &error);
            QVERIFY2(serviceId > 0, qPrintable(error));
            QCOMPARE(repository.songs().size(), oldSongs + 1);
            const auto entries = repository.service(serviceId);
            QCOMPARE(entries.size(), 3);
            ids = entryIds(entries);
            QVERIFY(entries[0].id != entries[1].id);
            QVERIFY(entries[1].id != entries[2].id);
            QVERIFY(entries[0].songId != 2);
            QCOMPARE(entries[1].songId, 2);
            QCOMPARE(entries[2].songId, 2);
            const Song created = repository.song(entries[0].songId);
            QCOMPARE(created.title, QStringLiteral("Welcome & Prayer"));
            QCOMPARE(created.slides, (QStringList{QStringLiteral("Welcome everyone"), QStringLiteral("Let us pray")}));
            QCOMPARE(created.settings, PresentationSettings{});
            QVERIFY(created.backgroundPath.isEmpty());
            QCOMPARE(repository.entrySlides(entries[0].id),
                (QStringList{QStringLiteral("Let us pray"), QStringLiteral("Welcome everyone"), QStringLiteral("Let us pray")}));
            QCOMPARE(repository.entrySlides(entries[1].id),
                (QStringList{original.slides[2], original.slides[0], original.slides[2], original.slides[1]}));
            QCOMPARE(repository.entrySlides(entries[2].id), original.slides);
            QCOMPARE(repository.song(2).slides, original.slides);
        }
        LibraryRepository reopened;
        QVERIFY2(reopened.open(&error, path), qPrintable(error));
        QCOMPARE(entryIds(reopened.service(serviceId)), ids);
        bool found = false;
        for (const auto &plan : reopened.services())
            if (plan.id == serviceId) found = plan.name == QStringLiteral("Sunday 10:30");
        QVERIFY(found);
        QCOMPARE(reopened.service(1).size(), 2);
    }

    void completeServiceOrderRejectsInvalidRequestsWithoutChanges() {
        LibraryRepository repository;
        QString error;
        QVERIFY2(repository.open(&error, QStringLiteral(":memory:")), qPrintable(error));
        const auto songCount = repository.songs().size();
        const auto serviceCount = repository.services().size();
        const ServiceOrderItem validNew{0, QStringLiteral("Prayer"), {QStringLiteral("Let us pray")}, {}};
        const auto reject = [&](const QString &name, const QVector<ServiceOrderItem> &items) {
            const int id = repository.createServiceOrder(name, items, &error);
            if (id != 0 || error.isEmpty() || repository.songs().size() != songCount
                || repository.services().size() != serviceCount) return false;
            return true;
        };
        QVERIFY(reject(QStringLiteral(" "), {validNew}));
        QVERIFY(reject(QString(257, QLatin1Char('x')), {validNew}));
        QVERIFY(reject(QStringLiteral("Order"), {}));
        QVERIFY(reject(QStringLiteral("Order"), QVector<ServiceOrderItem>(501, {2, {}, {}, {}})));
        QVERIFY(reject(QStringLiteral("Order"), {validNew, {999999, {}, {}, {}}}));
        QVERIFY(reject(QStringLiteral("Order"), {validNew, {-1, {}, {}, {}}}));
        QVERIFY(reject(QStringLiteral("Order"), {validNew, {2, QStringLiteral("Ambiguous"), {}, {}}}));
        QVERIFY(reject(QStringLiteral("Order"), {{0, QStringLiteral("Empty"), {}, {}}}));
        QVERIFY(reject(QStringLiteral("Order"), {{0, QStringLiteral("Blank"), {QStringLiteral(" \n\t ")}, {}}}));
        QVERIFY(reject(QStringLiteral("Order"), {{0, QStringLiteral("Null"), {QString(QChar::Null)}, {}}}));
        QVERIFY(reject(QStringLiteral("Order"), {{0, QStringLiteral("Oversize"), {QString(64 * 1024 + 1, QLatin1Char('x'))}, {}}}));
        QVERIFY(reject(QStringLiteral("Order"), {validNew, {2, {}, {}, {-1}}}));
        QVERIFY(reject(QStringLiteral("Order"), {validNew, {2, {}, {}, {3}}}));
        QVERIFY(reject(QStringLiteral("Order"), {{0, QStringLiteral("Prayer"), {QStringLiteral("Text")}, {1}}}));
        QVERIFY(reject(QStringLiteral("Order"), {{2, {}, {}, QVector<int>(10001, 0)}}));
        QVERIFY(repository.createServiceOrder(QStringLiteral("Valid afterward"), {validNew}, &error) > 0);
    }

    void completeServiceOrderRollsBackDatabaseFailures() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("service-order-rollback.sqlite3"));
        LibraryRepository repository;
        QString error;
        QVERIFY2(repository.open(&error, path), qPrintable(error));
        const auto songCount = repository.songs().size();
        const auto serviceCount = repository.services().size();
        const auto entryCount = rawValue(path, QStringLiteral("SELECT COUNT(*) FROM service_entries")).toInt();
        const QVector<ServiceOrderItem> order{
            {0, QStringLiteral("Welcome"), {QStringLiteral("Hello"), QStringLiteral("Prayer")}, {}},
            {2, {}, {}, {2, 0, 1}}
        };
        QVERIFY(rawSql(path, {QStringLiteral("CREATE TRIGGER reject_service_entry BEFORE INSERT ON service_entries "
            "WHEN NEW.position=1 BEGIN SELECT RAISE(ABORT,'reject entry'); END")}, &error));
        QCOMPARE(repository.createServiceOrder(QStringLiteral("Rejected service"), order, &error), 0);
        QVERIFY(!error.isEmpty());
        QCOMPARE(repository.songs().size(), songCount);
        QCOMPARE(repository.services().size(), serviceCount);
        QCOMPARE(rawValue(path, QStringLiteral("SELECT COUNT(*) FROM service_entries")).toInt(), entryCount);
        QVERIFY(rawSql(path, {QStringLiteral("DROP TRIGGER reject_service_entry"),
            QStringLiteral("CREATE TRIGGER reject_order_slide BEFORE INSERT ON slides "
                "WHEN NEW.position=1 BEGIN SELECT RAISE(ABORT,'reject slide'); END")}, &error));
        QCOMPARE(repository.createServiceOrder(QStringLiteral("Rejected slide"), order, &error), 0);
        QVERIFY(!error.isEmpty());
        QCOMPARE(repository.songs().size(), songCount);
        QCOMPARE(repository.services().size(), serviceCount);
        QCOMPARE(rawValue(path, QStringLiteral("SELECT COUNT(*) FROM service_entries")).toInt(), entryCount);
        QVERIFY(rawSql(path, {QStringLiteral("DROP TRIGGER reject_order_slide")}, &error));
        const int saved = repository.createServiceOrder(QStringLiteral("Saved after failure"), order, &error);
        QVERIFY2(saved > 0, qPrintable(error));
        QCOMPARE(repository.service(saved).size(), 2);
    }

    void dataVersionDetectsOtherConnectionCommits() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("version.sqlite3"));
        LibraryRepository operatorRepository;
        LibraryRepository aiRepository;
        QString error;
        QVERIFY(operatorRepository.open(&error, path));
        QVERIFY(aiRepository.open(&error, path));
        const qint64 version = operatorRepository.dataVersion();
        QVERIFY(version >= 0);
        QVERIFY(operatorRepository.createService(QStringLiteral("Local edit"), &error) > 0);
        QCOMPARE(operatorRepository.dataVersion(), version);
        QVERIFY(aiRepository.createServiceOrder(QStringLiteral("AI order"), {{2, {}, {}, {}}}, &error) > 0);
        QVERIFY(operatorRepository.dataVersion() != version);
        QCOMPARE(operatorRepository.services().size(), 3);
    }

    void richTextSavesInPlaceAndFollowsArrangementsAndCopies() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("rich.sqlite3"));
        QString error;
        int presentationId = 0;
        int copyId = 0;
        int entryId = 0;
        const QStringList texts{QStringLiteral("Welcome"), QString(), QStringLiteral("Closing")};
        const QStringList html{QStringLiteral("<p><b>Welcome</b></p>"), QStringLiteral("<p></p>"),
                               QStringLiteral("<p style='color:#00ff99'>Closing</p>")};
        const QVector<SlideMedia> media{
            {MediaKind::Image, QStringLiteral("media/session/a.jpg"), QStringLiteral("a.jpg"), false},
            {},
            {MediaKind::Video, QStringLiteral("media/session/c.mov"), QStringLiteral("c.mov"), true}};
        PresentationSettings settings{3000, true, SlideTransitionType::Fade, 800};
        settings.overrideTransition = true;
        {
            LibraryRepository repository;
            QVERIFY(repository.open(&error, path));
            presentationId = repository.createPresentation(QStringLiteral("Before"),
                {QString(), QStringLiteral("Text slide"), QString()}, media, &error, settings,
                QStringLiteral("backgrounds/service.jpg"));
            QVERIFY2(presentationId, qPrintable(error));
            const int plan = repository.services().first().id;
            QVERIFY(repository.appendToService(plan, presentationId, &error));
            entryId = repository.service(plan).last().id;
            QVERIFY(repository.setEntrySlideOrder(entryId, {2, 0, 2, 1}, &error));
            const auto ids = rawValue(path, QStringLiteral("SELECT group_concat(id,',') FROM "
                "(SELECT id FROM slides WHERE song_id=%1 ORDER BY position,id)").arg(presentationId));
            QVERIFY2(repository.updatePresentationText(presentationId, QStringLiteral("After"), texts, html, &error), qPrintable(error));
            const Song saved = repository.song(presentationId);
            QCOMPARE(saved.title, QStringLiteral("After"));
            QCOMPARE(saved.slides, texts);
            QCOMPARE(saved.richText, html);
            QCOMPARE(saved.media, media);
            QCOMPARE(saved.settings, settings);
            QCOMPARE(saved.backgroundPath, QStringLiteral("backgrounds/service.jpg"));
            QCOMPARE(repository.service(plan).last().slideOrder, (QVector<int>{2, 0, 2, 1}));
            QCOMPARE(repository.entryRichText(entryId), (QStringList{html[2], html[0], html[2], html[1]}));
            QCOMPARE(rawValue(path, QStringLiteral("SELECT group_concat(id,',') FROM "
                "(SELECT id FROM slides WHERE song_id=%1 ORDER BY position,id)").arg(presentationId)), ids);
            // A formatted blank text slide remains a valid slide on duplication.
            copyId = repository.createPresentation(QStringLiteral("Copy"), texts, media, &error,
                settings, saved.backgroundPath, html);
            QVERIFY2(copyId, qPrintable(error));
        }
        LibraryRepository reopened;
        QVERIFY(reopened.open(&error, path));
        QCOMPARE(reopened.song(presentationId).richText, html);
        QCOMPARE(reopened.song(copyId).richText, html);
        QCOMPARE(reopened.song(copyId).media, media);
        QCOMPARE(reopened.entryRichText(entryId), (QStringList{html[2], html[0], html[2], html[1]}));
        QCOMPARE(reopened.songs(QStringLiteral("Closing")).size(), 2);
    }

    void richTextValidationAndDatabaseFailureDoNotPartiallySave() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("rollback.sqlite3"));
        QString error;
        LibraryRepository repository;
        QVERIFY(repository.open(&error, path));
        const Song original = repository.song(1);
        QVERIFY(!repository.updatePresentationText(1, QStringLiteral("Wrong count"),
            {QStringLiteral("Only")}, {QStringLiteral("<p>Only</p>")}, &error));
        QVERIFY(!repository.updatePresentationText(1, QStringLiteral("Wrong HTML count"),
            original.slides, {QStringLiteral("<p>Only</p>")}, &error));
        QVERIFY(!repository.createSong(QStringLiteral("Oversize"), {QStringLiteral("Text")}, &error,
            {}, {}, {QString(1024 * 1024 + 1, QLatin1Char('x'))}));
        QVERIFY(repository.songs(QStringLiteral("Oversize")).isEmpty());
        const QStringList changed{QStringLiteral("New first"), QStringLiteral("New last")};
        const QStringList html{QStringLiteral("<p>New first</p>"), QStringLiteral("<p>New last</p>")};
        QVERIFY(rawSql(path, {QStringLiteral("CREATE TRIGGER reject_second BEFORE UPDATE OF rich_text ON slides "
            "WHEN NEW.song_id=1 AND NEW.position=1 BEGIN SELECT RAISE(ABORT,'reject second'); END")}, &error));
        QVERIFY(!repository.updatePresentationText(1, QStringLiteral("Changed"), changed, html, &error));
        QCOMPARE(repository.song(1).title, original.title);
        QCOMPARE(repository.song(1).slides, original.slides);
        QCOMPARE(repository.song(1).richText, original.richText);
        QVERIFY(rawSql(path, {QStringLiteral("DROP TRIGGER reject_second")}, &error));
        QVERIFY(repository.updatePresentationText(1, QStringLiteral("Changed"), changed, html, &error));
        QCOMPARE(repository.song(1).richText, html);
    }

    void plainTextEditsPreserveOnlyUnchangedSlideFormatting() {
        LibraryRepository repository;
        QString error;
        QVERIFY(repository.open(&error, QStringLiteral(":memory:")));
        const QStringList texts{QStringLiteral("One"), QStringLiteral("Two")};
        const QStringList html{QStringLiteral("<p><b>One</b></p>"), QStringLiteral("<p><i>Two</i></p>")};
        QVERIFY(repository.createSong(QStringLiteral("Formatted"), texts, &error, {}, {}, html));
        const int id = repository.songs(QStringLiteral("Formatted")).first().id;
        QVERIFY(repository.updateSong(id, QStringLiteral("Formatted"), {QStringLiteral("One"), QStringLiteral("Changed")}, &error));
        QCOMPARE(repository.song(id).richText, (QStringList{html.first(), QString()}));
        QVERIFY(repository.updateSong(id, QStringLiteral("Formatted"), {QStringLiteral("One")}, &error));
        QCOMPARE(repository.song(id).richText, (QStringList{QString()}));
    }

    void migratesVersionFourRichTextAndRollsBackDuplicateColumn() {
        QTemporaryDir directory;
        QString error;
        const auto createV4 = [](const QString &path, bool duplicateRichTextColumn) {
            return rawSql(path, {
                QStringLiteral("CREATE TABLE songs(id INTEGER PRIMARY KEY,title TEXT NOT NULL,auto_advance_ms INTEGER NOT NULL DEFAULT 0,"
                    "presentation_loop INTEGER NOT NULL DEFAULT 0,transition_type INTEGER NOT NULL DEFAULT 0,"
                    "transition_duration_ms INTEGER NOT NULL DEFAULT 500,transition_override INTEGER NOT NULL DEFAULT 0,background_path TEXT NOT NULL DEFAULT '')"),
                QStringLiteral("CREATE TABLE slides(id INTEGER PRIMARY KEY,song_id INTEGER NOT NULL REFERENCES songs(id),position INTEGER NOT NULL,text TEXT NOT NULL,"
                    "media_kind TEXT NOT NULL DEFAULT '',media_path TEXT NOT NULL DEFAULT '',media_name TEXT NOT NULL DEFAULT '',media_loop INTEGER NOT NULL DEFAULT 0%1)")
                    .arg(duplicateRichTextColumn ? QStringLiteral(",rich_text TEXT NOT NULL DEFAULT ''") : QString()),
                QStringLiteral("CREATE TABLE services(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL)"),
                QStringLiteral("CREATE TABLE service_entries(id INTEGER PRIMARY KEY AUTOINCREMENT,service_id INTEGER NOT NULL REFERENCES services(id),song_id INTEGER NOT NULL REFERENCES songs(id),position INTEGER NOT NULL,slide_order TEXT)"),
                QStringLiteral("INSERT INTO songs(id,title) VALUES(8,'Existing')"),
                QStringLiteral("INSERT INTO slides(id,song_id,position,text) VALUES(41,8,0,'Old text'),(42,8,1,'Last text')"),
                QStringLiteral("INSERT INTO services(id,name) VALUES(2,'Existing Service')"),
                QStringLiteral("INSERT INTO service_entries VALUES(91,2,8,0,'[1,0,1]')"),
                QStringLiteral("PRAGMA user_version=4")
            });
        };
        const QString path = directory.filePath(QStringLiteral("v4.sqlite3"));
        QVERIFY(createV4(path, false));
        {
            LibraryRepository repository;
            QVERIFY2(repository.open(&error, path), qPrintable(error));
            QCOMPARE(repository.song(8).slides, (QStringList{QStringLiteral("Old text"), QStringLiteral("Last text")}));
            QCOMPARE(repository.song(8).richText, (QStringList{QString(), QString()}));
            QCOMPARE(repository.entryRichText(91), (QStringList{QString(), QString(), QString()}));
            QCOMPARE(repository.service(2).first().id, 91);
        }
        QCOMPARE(rawValue(path, QStringLiteral("PRAGMA user_version")).toInt(), 11);
        QCOMPARE(rawValue(path, QStringLiteral("SELECT id FROM slides WHERE position=0")).toInt(), 41);
        const QString badPath = directory.filePath(QStringLiteral("duplicate.sqlite3"));
        QVERIFY(createV4(badPath, true));
        {
            LibraryRepository repository;
            QVERIFY(!repository.open(&error, badPath));
            QVERIFY(!error.isEmpty());
        }
        QCOMPARE(rawValue(badPath, QStringLiteral("PRAGMA user_version")).toInt(), 4);
        QCOMPARE(rawValue(badPath, QStringLiteral("SELECT text FROM slides WHERE id=41")).toString(), QStringLiteral("Old text"));
    }

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
        QCOMPARE(rawValue(path, QStringLiteral("PRAGMA user_version")).toInt(), 11);
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
        QCOMPARE(rawValue(path, QStringLiteral("PRAGMA user_version")).toInt(), 11);
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
        const PresentationSettings settings{7250, true, SlideTransitionType::Fade, 1250, true};
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
            const PresentationSettings changed{9500, false, SlideTransitionType::SlideRight, 300, true};
            QVERIFY(repository.updatePresentationSettings(textId, changed, &error));
            QCOMPARE(repository.song(textId).settings, changed);
        }
        LibraryRepository reopened;
        QVERIFY2(reopened.open(&error, path), qPrintable(error));
        QCOMPARE(reopened.song(textId).settings, (PresentationSettings{9500, false, SlideTransitionType::SlideRight, 300, true}));
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
        QCOMPARE(rawValue(path, QStringLiteral("PRAGMA user_version")).toInt(), 11);
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

    void programTransitionsInheritWithoutChangingPresentationTiming() {
        const PresentationSettings program{0, false, SlideTransitionType::Fade, 1200};
        const PresentationSettings inherited{5500, true, SlideTransitionType::Cut, 500};
        const auto resolved = inherited.resolvedWithDefaults(program);
        QCOMPARE(resolved.autoAdvanceMs, 5500);
        QVERIFY(resolved.loop);
        QCOMPARE(resolved.transition, SlideTransitionType::Fade);
        QCOMPARE(resolved.transitionDurationMs, 1200);
        QVERIFY(!resolved.overrideTransition);
        const PresentationSettings overridden{5500, true, SlideTransitionType::Cut, 0, true};
        QCOMPARE(overridden.resolvedWithDefaults(program), overridden);
        auto differentProgram = program;
        differentProgram.transition = SlideTransitionType::SlideRight;
        differentProgram.transitionDurationMs = 400;
        QCOMPARE(inherited.resolvedWithDefaults(differentProgram).transition, SlideTransitionType::SlideRight);
        QCOMPARE(overridden.resolvedWithDefaults(differentProgram), overridden);
    }

    void backgroundsPersistWithPresentationsAndValidateManagedPaths() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("backgrounds.sqlite3"));
        QString error;
        int copyId = 0;
        const QString background = QStringLiteral("backgrounds/Sunday blue.jpg");
        const PresentationSettings settings{6000, true, SlideTransitionType::Fade, 800, true};
        {
            LibraryRepository repository;
            QVERIFY(repository.open(&error, path));
            QVERIFY(repository.updatePresentationBackground(1, background, &error));
            QVERIFY(repository.updatePresentationSettings(1, settings, &error));
            const Song source = repository.song(1);
            QCOMPARE(source.backgroundPath, background);
            QVERIFY(repository.updateSong(1, QStringLiteral("Welcome edited"), {QStringLiteral("Welcome")}, &error));
            QCOMPARE(repository.song(1).backgroundPath, background);
            QCOMPARE(repository.song(1).settings, settings);
            QVERIFY(repository.createSong(QStringLiteral("Copied song"), source.slides, &error, source.settings, source.backgroundPath));
            copyId = repository.songs(QStringLiteral("Copied song")).first().id;
            const QVector<SlideMedia> media{{MediaKind::Image, QStringLiteral("media/session/a.jpg"), QStringLiteral("a.jpg"), false}};
            const int mediaId = repository.createPresentation(QStringLiteral("Picture"), {QStringLiteral("")}, media, &error, {}, background);
            QVERIFY(mediaId > 0);
            QCOMPARE(repository.song(mediaId).backgroundPath, background);
            const QStringList invalidPaths{QStringLiteral("/tmp/a.jpg"), QStringLiteral("backgrounds/../a.jpg"),
                QStringLiteral("backgrounds//a.jpg"), QStringLiteral("media/a.jpg"), QStringLiteral("backgrounds/"),
                QStringLiteral("backgrounds/a.jpg/"), QStringLiteral("backgrounds\\a.jpg")};
            for (const QString &invalid : invalidPaths) {
                QVERIFY(!repository.updatePresentationBackground(1, invalid, &error));
                QVERIFY(!error.isEmpty());
                QCOMPARE(repository.song(1).backgroundPath, background);
                QVERIFY(!repository.createSong(QStringLiteral("Invalid"), {QStringLiteral("Text")}, &error, {}, invalid));
                QCOMPARE(repository.createPresentation(QStringLiteral("Invalid Picture"), {QStringLiteral("")}, media, &error, {}, invalid), 0);
            }
            QVERIFY(!repository.updatePresentationBackground(999, background, &error));
            QVERIFY(repository.updatePresentationBackground(1, {}, &error));
            QVERIFY(repository.song(1).backgroundPath.isEmpty());
        }
        LibraryRepository reopened;
        QVERIFY(reopened.open(&error, path));
        QCOMPARE(reopened.song(copyId).backgroundPath, background);
        QCOMPARE(reopened.song(copyId).settings, settings);
        QVERIFY(reopened.song(1).backgroundPath.isEmpty());
        QVERIFY(rawSql(path, {QStringLiteral("UPDATE songs SET background_path='../outside.jpg' WHERE id=1")}, &error));
        QCOMPARE(reopened.song(1).id, 0);
        QVERIFY(reopened.lastError().contains(QStringLiteral("background")));
    }

    void migratesVersionThreeOverridesAndRollsBackPartialBackgroundMigration() {
        QTemporaryDir directory;
        QString error;
        const auto createV3 = [](const QString &path, bool duplicateBackgroundColumn) {
            return rawSql(path, {
                QStringLiteral("CREATE TABLE songs(id INTEGER PRIMARY KEY,title TEXT NOT NULL,auto_advance_ms INTEGER NOT NULL DEFAULT 0,"
                               "presentation_loop INTEGER NOT NULL DEFAULT 0,transition_type INTEGER NOT NULL DEFAULT 0,"
                               "transition_duration_ms INTEGER NOT NULL DEFAULT 500%1)")
                    .arg(duplicateBackgroundColumn ? QStringLiteral(",background_path TEXT") : QString()),
                QStringLiteral("CREATE TABLE slides(id INTEGER PRIMARY KEY,song_id INTEGER NOT NULL REFERENCES songs(id),position INTEGER NOT NULL,text TEXT NOT NULL,"
                               "media_kind TEXT NOT NULL DEFAULT '',media_path TEXT NOT NULL DEFAULT '',media_name TEXT NOT NULL DEFAULT '',media_loop INTEGER NOT NULL DEFAULT 0)"),
                QStringLiteral("CREATE TABLE services(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL)"),
                QStringLiteral("CREATE TABLE service_entries(id INTEGER PRIMARY KEY AUTOINCREMENT,service_id INTEGER NOT NULL REFERENCES services(id),song_id INTEGER NOT NULL REFERENCES songs(id),position INTEGER NOT NULL,slide_order TEXT)"),
                QStringLiteral("INSERT INTO songs(id,title,auto_advance_ms,presentation_loop,transition_type,transition_duration_ms) "
                               "VALUES(1,'Inherited',6000,1,0,500),(2,'Fade',0,0,1,800),(3,'Custom cut',0,0,0,0)"),
                QStringLiteral("INSERT INTO slides(id,song_id,position,text) VALUES(11,1,0,'First'),(12,2,0,'Second'),(13,3,0,'Third')"),
                QStringLiteral("INSERT INTO services(id,name) VALUES(4,'Existing Service')"),
                QStringLiteral("INSERT INTO service_entries VALUES(29,4,1,0,'[0,0]')"),
                QStringLiteral("PRAGMA user_version=3")
            });
        };
        const QString path = directory.filePath(QStringLiteral("v3.sqlite3"));
        QVERIFY(createV3(path, false));
        {
            LibraryRepository repository;
            QVERIFY2(repository.open(&error, path), qPrintable(error));
            const Song inherited = repository.song(1);
            QVERIFY(!inherited.settings.overrideTransition);
            QCOMPARE(inherited.settings.autoAdvanceMs, 6000);
            QVERIFY(inherited.settings.loop);
            QVERIFY(inherited.backgroundPath.isEmpty());
            QVERIFY(repository.song(2).settings.overrideTransition);
            QCOMPARE(repository.song(2).settings.transition, SlideTransitionType::Fade);
            QCOMPARE(repository.song(2).settings.transitionDurationMs, 800);
            QVERIFY(repository.song(3).settings.overrideTransition);
            QCOMPARE(repository.song(3).settings.transitionDurationMs, 0);
            QCOMPARE(repository.service(4).first().id, 29);
            QCOMPARE(repository.service(4).first().slideOrder, (QVector<int>{0, 0}));
        }
        QCOMPARE(rawValue(path, QStringLiteral("PRAGMA user_version")).toInt(), 11);
        const QString badPath = directory.filePath(QStringLiteral("bad-v3.sqlite3"));
        QVERIFY(createV3(badPath, true));
        {
            LibraryRepository repository;
            QVERIFY(!repository.open(&error, badPath));
            QVERIFY(!error.isEmpty());
        }
        QCOMPARE(rawValue(badPath, QStringLiteral("PRAGMA user_version")).toInt(), 3);
        QCOMPARE(rawValue(badPath, QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('songs') WHERE name='transition_override'")).toInt(), 0);
        QCOMPARE(rawValue(badPath, QStringLiteral("SELECT title FROM songs WHERE id=1")).toString(), QStringLiteral("Inherited"));
    }

    void standaloneMediaReferencesStaySeparateAndSurviveCopies() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("media.sqlite3"));
        QString error;
        int assetId = 0;
        int serviceId = 0;
        {
            LibraryRepository repository;
            QVERIFY(repository.open(&error, path));
            const auto presentationCount = repository.songs().size();
            const SlideMedia media{MediaKind::Audio, QStringLiteral("media/session/music.wav"), QStringLiteral("Music.wav"), true};
            const auto ids = repository.registerMediaAssets({media, media}, &error);
            QCOMPARE(ids.size(), 2);
            QCOMPARE(ids.first(), ids.last());
            assetId = ids.first();
            QCOMPARE(repository.songs().size(), presentationCount);
            QCOMPARE(repository.mediaAssets().size(), 1);
            QVERIFY(repository.mediaAsset(assetId).importedAt > 0);
            serviceId = repository.createService(QStringLiteral("Mixed service"), &error);
            QVERIFY(repository.appendToService(serviceId, 1, &error));
            const int firstEntry = repository.service(serviceId).first().id;
            const int inserted = repository.appendMediaToService(serviceId, assetId, &error, firstEntry);
            QVERIFY(inserted > 0);
            const auto entry = repository.service(serviceId).first();
            QCOMPARE(entry.id, inserted);
            QCOMPARE(entry.songId, 0);
            QCOMPARE(entry.mediaAssetId, assetId);
            QCOMPARE(repository.entrySlides(entry.id), QStringList({QStringLiteral("")}));
            QCOMPARE(repository.entryMedia(entry.id), QVector<SlideMedia>({media}));
            QVERIFY(repository.setEntrySlideOrder(entry.id, {0}, &error));
            QVERIFY(!repository.setEntrySlideOrder(entry.id, {0, 0}, &error));
            QVERIFY(!repository.setEntrySlideOrder(entry.id, {1}, &error));
            QVERIFY(repository.updateMediaAsset(assetId, QStringLiteral("Prelude.wav"), QStringLiteral("Sunday"), false, &error));
            QCOMPARE(repository.entrySource(entry.id).title, QStringLiteral("Prelude.wav"));
            QVERIFY(!repository.entryMedia(entry.id).first().loop);
            QCOMPARE(repository.mediaAssets(QStringLiteral("Sunday")).first().id, assetId);
            QVERIFY(!repository.deleteMediaAsset(assetId, &error));
            QVERIFY(error.contains(QStringLiteral("referenced")));
            QVERIFY(repository.duplicateServiceEntry(entry.id, &error));
            QCOMPARE(repository.service(serviceId).at(1).mediaAssetId, assetId);
            const int copy = repository.duplicateService(serviceId, QStringLiteral("Copy"), &error);
            QVERIFY(copy > 0);
            QCOMPARE(repository.service(copy).first().mediaAssetId, assetId);
            QCOMPARE(repository.mediaAssets().size(), 1);
            QVERIFY(repository.deleteSong(1, &error));
            QCOMPARE(repository.service(serviceId).size(), 2);
        }
        LibraryRepository reopened;
        QVERIFY(reopened.open(&error, path));
        QCOMPARE(reopened.mediaAsset(assetId).folder, QStringLiteral("Sunday"));
        QCOMPARE(reopened.entrySource(reopened.service(serviceId).first().id).mediaAssetId, assetId);
        for (const auto &plan : reopened.services())
            for (const auto &entry : reopened.service(plan.id))
                if (entry.mediaAssetId == assetId) QVERIFY(reopened.removeFromService(entry.id, &error));
        QVERIFY(reopened.deleteMediaAsset(assetId, &error));
        QCOMPARE(reopened.mediaAsset(assetId).id, 0);
    }

    void mediaBatchRollbackAndPresentationProtection() {
        QTemporaryDir directory;
        QString error;
        LibraryRepository repository;
        QVERIFY(repository.open(&error, directory.filePath(QStringLiteral("library.sqlite3"))));
        const SlideMedia image{MediaKind::Image, QStringLiteral("media/session/image.jpg"), QStringLiteral("Image.jpg"), false};
        auto invalid = image;
        invalid.path = QStringLiteral("media/../../outside.jpg");
        QVERIFY(repository.registerMediaAssets({image, invalid}, &error).isEmpty());
        QVERIFY(repository.mediaAssets().isEmpty());
        const int presentation = repository.createPresentation(QStringLiteral("Legacy batch"), {QStringLiteral("")}, {image}, &error);
        QVERIFY2(presentation > 0, qPrintable(error));
        const int assetId = repository.mediaAssets().first().id;
        QCOMPARE(repository.registerMediaAssets({image}, &error), QVector<int>({assetId}));
        QVERIFY(!repository.deleteMediaAsset(assetId, &error));
        auto conflict = image;
        conflict.kind = MediaKind::Video;
        QVERIFY(repository.registerMediaAssets({conflict}, &error).isEmpty());
        QVERIFY(!repository.updateMediaAsset(assetId, QStringLiteral("../bad"), {}, false, &error));
        QVERIFY(!repository.updateMediaAsset(assetId, image.name, {}, true, &error));
        QCOMPARE(repository.mediaAsset(assetId).media, image);
        QVERIFY(!repository.appendMediaToService(999, assetId, &error));
        const int plan = repository.createService(QStringLiteral("Media"), &error);
        QVERIFY(!repository.appendMediaToService(plan, assetId, &error, 999));
        QVERIFY(repository.service(plan).isEmpty());
        QVERIFY(repository.deleteSong(presentation, &error));
        QVERIFY(repository.deleteMediaAsset(assetId, &error));
    }

    void versionFiveMigrationPreservesMediaAndEntrySequence() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("v5.sqlite3"));
        QString error;
        {
            LibraryRepository repository;
            QVERIFY(repository.open(&error, path));
            QVERIFY(repository.createPresentation(QStringLiteral("Pictures"), {QStringLiteral("")},
                {{MediaKind::Image, QStringLiteral("media/legacy/a.jpg"), QStringLiteral("a.jpg"), false}}, &error));
        }
        // Reconstruct the v5 entry table and remove the new registry to exercise a real upgrade.
        QVERIFY(removeCleanupSchema(path));
        QVERIFY(rawSql(path, {QStringLiteral("DROP TABLE midi_bindings"), QStringLiteral("DROP TABLE midi_slide_actions"),
            QStringLiteral("CREATE TABLE service_entries_v5(id INTEGER PRIMARY KEY AUTOINCREMENT,service_id INTEGER NOT NULL REFERENCES services(id),song_id INTEGER NOT NULL REFERENCES songs(id),position INTEGER NOT NULL,slide_order TEXT)"),
            QStringLiteral("INSERT INTO service_entries_v5 SELECT id,service_id,song_id,position,slide_order FROM service_entries"),
            QStringLiteral("INSERT INTO service_entries_v5(id,service_id,song_id,position) VALUES(800,1,1,9)"),
            QStringLiteral("DELETE FROM service_entries_v5 WHERE id=800"),
            QStringLiteral("DROP TABLE service_entries"),
            QStringLiteral("ALTER TABLE service_entries_v5 RENAME TO service_entries"),
            QStringLiteral("CREATE INDEX service_entries_order ON service_entries(service_id,position,id)"),
            QStringLiteral("ALTER TABLE songs DROP COLUMN look_id"),
            QStringLiteral("ALTER TABLE slides DROP COLUMN look_id"),
            QStringLiteral("ALTER TABLE slides DROP COLUMN look_rich_override"),
            QStringLiteral("ALTER TABLE slides DROP COLUMN look_fit"),
            QStringLiteral("DROP TABLE looks"),
            QStringLiteral("ALTER TABLE songs DROP COLUMN song_structure"),
            QStringLiteral("DROP INDEX slides_media_path"),
            QStringLiteral("DROP TABLE media_assets"),
            QStringLiteral("PRAGMA user_version=5")}, &error));
        LibraryRepository migrated;
        QVERIFY2(migrated.open(&error, path), qPrintable(error));
        QCOMPARE(migrated.mediaAssets().size(), 1);
        const auto asset = migrated.mediaAssets().first();
        QCOMPARE(asset.media.path, QStringLiteral("media/legacy/a.jpg"));
        QCOMPARE(asset.importedAt, 0);
        QCOMPARE(migrated.service(1).first().id, 1);
        QVERIFY(migrated.appendMediaToService(1, asset.id, &error));
        QVERIFY(migrated.service(1).last().id > 800);
        QCOMPARE(rawValue(path, QStringLiteral("PRAGMA user_version")).toInt(), 11);
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
