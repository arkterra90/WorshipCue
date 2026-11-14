#include "LibraryRepository.h"

#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

namespace {
constexpr int schemaVersion = 11;

bool validRichText(const QStringList &texts, const QStringList &richText) {
    if (!richText.isEmpty() && richText.size() != texts.size()) return false;
    // Keep malformed imported payloads from producing unbounded documents.
    qsizetype total = 0;
    for (const auto &html : richText) {
        if (html.size() > 1024 * 1024 || html.contains(QChar::Null)) return false;
        total += html.size();
        if (total > 16 * 1024 * 1024) return false;
    }
    return true;
}

class Transaction {
public:
    explicit Transaction(QSqlDatabase &database) : database_(database), active_(database.transaction()) {}
    ~Transaction() { if (active_) database_.rollback(); }
    bool active() const { return active_; }
    bool commit() {
        if (!active_ || !database_.commit()) return false;
        active_ = false;
        return true;
    }
private:
    QSqlDatabase &database_;
    bool active_;
};

class ForeignKeyPause {
public:
    explicit ForeignKeyPause(QSqlDatabase &db) : db_(db) { QSqlQuery query(db_); active_ = query.exec("PRAGMA foreign_keys=OFF"); }
    ~ForeignKeyPause() { QSqlQuery query(db_); query.exec("PRAGMA foreign_keys=ON"); }
    bool active() const { return active_; }
private:
    QSqlDatabase &db_;
    bool active_ = false;
};

QString midiOwnerColumn(MidiOwnerType type) {
    if(type==MidiOwnerType::Slide)return "slide_id";
    if(type==MidiOwnerType::Entry)return "entry_id";
    if(type==MidiOwnerType::Asset)return "asset_id";
    return {};
}
QString midiOwnerTable(MidiOwnerType type) { return type==MidiOwnerType::Slide?QStringLiteral("slides"):type==MidiOwnerType::Entry?QStringLiteral("service_entries"):QStringLiteral("media_assets"); }

bool validBackgroundPath(const QString &path) {
    return path.isEmpty() || (!QDir::isAbsolutePath(path)
        && path.startsWith(QStringLiteral("backgrounds/"))
        && path.size() > QStringLiteral("backgrounds/").size()
        && !path.endsWith(QLatin1Char('/'))
        && QDir::cleanPath(path) == path
        && !path.contains(QLatin1Char('\\')) && !path.contains(QChar::Null));
}

bool validAsset(const SlideMedia &media) {
    return (media.kind == MediaKind::Image || media.kind == MediaKind::Video || media.kind == MediaKind::Audio)
        && !QDir::isAbsolutePath(media.path) && media.path.startsWith(QStringLiteral("media/"))
        && media.path.size() > 6 && !media.path.endsWith(QLatin1Char('/'))
        && QDir::cleanPath(media.path) == media.path && !media.path.contains(QLatin1Char('\\'))
        && !media.path.contains(QChar::Null) && !media.name.trimmed().isEmpty()
        && media.name.size() <= 256 && !media.name.contains(QLatin1Char('/'))
        && !media.name.contains(QLatin1Char('\\')) && !media.name.contains(QChar::Null)
        && (media.kind != MediaKind::Image || !media.loop);
}

QString storedMediaKind(MediaKind kind) {
    if (kind == MediaKind::Image) return QStringLiteral("image");
    if (kind == MediaKind::Video) return QStringLiteral("video");
    if (kind == MediaKind::Audio) return QStringLiteral("audio");
    return QStringLiteral("");
}

QString encodedOrder(const QVector<int> &order) {
    QJsonArray array;
    for (int index : order) array.append(index);
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

bool decodeOrder(const QVariant &stored, QVector<int> *order) {
    if (stored.isNull()) return true;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(stored.toString().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) return false;
    for (const auto &value : document.array()) {
        if (!value.isDouble() || value.toInt(-1) < 0 || value.toDouble() != value.toInt()) return false;
        order->append(value.toInt());
    }
    return true;
}
}

LibraryRepository::~LibraryRepository() { close(); }

void LibraryRepository::close() {
    if (!db_.isValid()) return;
    const QString connectionName = db_.connectionName();
    db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

void LibraryRepository::clearError(QString *error) const {
    lastError_.clear();
    if (error) error->clear();
}

bool LibraryRepository::fail(const QString &message, QString *error) const {
    lastError_ = message;
    if (error) *error = message;
    qWarning().noquote() << "WorshipCue database:" << message;
    return false;
}

bool LibraryRepository::execute(const QString &sql, QString *error) const {
    QSqlQuery query(db_);
    if (!query.exec(sql)) return fail(query.lastError().text(), error);
    return true;
}

bool LibraryRepository::exists(const QString &table, int id, QString *error) const {
    QSqlQuery query(db_);
    // Table names are private constants supplied by this implementation.
    query.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE id=?").arg(table));
    query.addBindValue(id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.next()) return fail(QStringLiteral("The requested %1 item no longer exists.").arg(table), error);
    return true;
}

bool LibraryRepository::open(QString *error, const QString &databasePath) {
    clearError(error);
    close();
    QString path = databasePath;
    if (path.isEmpty()) {
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (directory.isEmpty() || !QDir().mkpath(directory))
            return fail(QStringLiteral("Could not create application data directory: ") + directory, error);
        path = QDir(directory).filePath(QStringLiteral("library.sqlite3"));
    } else if (path != QStringLiteral(":memory:") && !QDir().mkpath(QFileInfo(path).absolutePath())) {
        return fail(QStringLiteral("Could not create database directory."), error);
    }

    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
        QStringLiteral("worshipcue-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    db_.setDatabaseName(path);
    if (!db_.open()) return fail(db_.lastError().text(), error);
    if (!execute(QStringLiteral("PRAGMA foreign_keys=ON"), error) ||
        !execute(QStringLiteral("PRAGMA busy_timeout=3000"), error) ||
        !execute(QStringLiteral("PRAGMA journal_mode=WAL"), error) ||
        !execute(QStringLiteral("PRAGMA synchronous=FULL"), error)) return false;

    int version = 0;
    {
        QSqlQuery query(db_);
        if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next())
            return fail(query.lastError().text(), error);
        version = query.value(0).toInt();
    }
    if (version > schemaVersion)
        return fail(QStringLiteral("This library was saved by a newer WorshipCue version. Update WorshipCue to open it."), error);
    const bool fresh = !db_.tables().contains(QStringLiteral("songs"));
    if (version < 1) {
        Transaction transaction(db_);
        if (!transaction.active()) return fail(db_.lastError().text(), error);
        const QStringList migration = {
            QStringLiteral("CREATE TABLE IF NOT EXISTS songs (id INTEGER PRIMARY KEY, title TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS slides (id INTEGER PRIMARY KEY, "
                           "song_id INTEGER NOT NULL REFERENCES songs(id) ON DELETE CASCADE, "
                           "position INTEGER NOT NULL, text TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS service_entries (id INTEGER PRIMARY KEY, "
                           "song_id INTEGER NOT NULL REFERENCES songs(id) ON DELETE CASCADE, position INTEGER NOT NULL)"),
            QStringLiteral("CREATE TABLE services (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL)"),
            QStringLiteral("INSERT INTO services(id,name) VALUES(1,'My Service')"),
            QStringLiteral("CREATE TABLE service_entries_v1 (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                           "service_id INTEGER NOT NULL REFERENCES services(id) ON DELETE CASCADE, "
                           "song_id INTEGER NOT NULL REFERENCES songs(id) ON DELETE CASCADE, "
                           "position INTEGER NOT NULL, slide_order TEXT)"),
            QStringLiteral("INSERT INTO service_entries_v1(id,service_id,song_id,position) "
                           "SELECT id,1,song_id,position FROM service_entries"),
            QStringLiteral("DROP TABLE service_entries"),
            QStringLiteral("ALTER TABLE service_entries_v1 RENAME TO service_entries"),
            QStringLiteral("CREATE INDEX service_entries_order ON service_entries(service_id,position,id)"),
            QStringLiteral("CREATE INDEX slides_order ON slides(song_id,position,id)"),
            QStringLiteral("PRAGMA user_version=1")
        };
        for (const QString &sql : migration) if (!execute(sql, error)) return false;
        if (fresh) {
            if (!execute(QStringLiteral("INSERT INTO songs(id,title) VALUES(1,'Welcome'),(2,'Sample Song (replace me)')"), error) ||
                !storeSlides(1, {QStringLiteral("Welcome to worship!"), QStringLiteral("We are glad you're here.")}, error) ||
                !storeSlides(2, {QStringLiteral("Verse 1\nAdd your own song lyrics here"),
                                QStringLiteral("Chorus\nEach section becomes one slide"),
                                QStringLiteral("Bridge\nEdit this example to get started")}, error) ||
                !execute(QStringLiteral("INSERT INTO service_entries(service_id,song_id,position) VALUES(1,1,0),(1,2,1)"), error))
                return false;
        }
        if (!transaction.commit()) return fail(db_.lastError().text(), error);
    }
    if (version < 2) {
        Transaction transaction(db_);
        if (!transaction.active()) return fail(db_.lastError().text(), error);
        const QStringList migration = {
            QStringLiteral("ALTER TABLE slides ADD COLUMN media_kind TEXT NOT NULL DEFAULT ''"),
            QStringLiteral("ALTER TABLE slides ADD COLUMN media_path TEXT NOT NULL DEFAULT ''"),
            QStringLiteral("ALTER TABLE slides ADD COLUMN media_name TEXT NOT NULL DEFAULT ''"),
            QStringLiteral("ALTER TABLE slides ADD COLUMN media_loop INTEGER NOT NULL DEFAULT 0"),
            QStringLiteral("PRAGMA user_version=2")
        };
        for (const auto &sql : migration) if (!execute(sql, error)) return false;
        if (!transaction.commit()) return fail(db_.lastError().text(), error);
    }
    if (version < 3) {
        Transaction transaction(db_);
        if (!transaction.active()) return fail(db_.lastError().text(), error);
        const QStringList migration = {
            QStringLiteral("ALTER TABLE songs ADD COLUMN auto_advance_ms INTEGER NOT NULL DEFAULT 0 "
                           "CHECK(auto_advance_ms=0 OR auto_advance_ms BETWEEN 100 AND 3600000)"),
            QStringLiteral("ALTER TABLE songs ADD COLUMN presentation_loop INTEGER NOT NULL DEFAULT 0 "
                           "CHECK(presentation_loop IN (0,1))"),
            QStringLiteral("ALTER TABLE songs ADD COLUMN transition_type INTEGER NOT NULL DEFAULT 0 "
                           "CHECK(transition_type BETWEEN 0 AND 3)"),
            QStringLiteral("ALTER TABLE songs ADD COLUMN transition_duration_ms INTEGER NOT NULL DEFAULT 500 "
                           "CHECK(transition_duration_ms BETWEEN 0 AND 10000)"),
            QStringLiteral("PRAGMA user_version=3")
        };
        for (const auto &sql : migration) if (!execute(sql, error)) return false;
        if (!transaction.commit()) return fail(db_.lastError().text(), error);
    }
    if (version < 4) {
        Transaction transaction(db_);
        if (!transaction.active()) return fail(db_.lastError().text(), error);
        const QStringList migration = {
            QStringLiteral("ALTER TABLE songs ADD COLUMN transition_override INTEGER NOT NULL DEFAULT 0 "
                           "CHECK(transition_override IN (0,1))"),
            QStringLiteral("ALTER TABLE songs ADD COLUMN background_path TEXT NOT NULL DEFAULT ''"),
            // Preserve deliberate choices in older libraries; untouched presentations inherit.
            QStringLiteral("UPDATE songs SET transition_override=1 WHERE transition_type<>0 OR transition_duration_ms<>500"),
            QStringLiteral("PRAGMA user_version=4")
        };
        for (const auto &sql : migration) if (!execute(sql, error)) return false;
        if (!transaction.commit()) return fail(db_.lastError().text(), error);
    }
    if (version < 5) {
        Transaction transaction(db_);
        if (!transaction.active()) return fail(db_.lastError().text(), error);
        if (!execute(QStringLiteral("ALTER TABLE slides ADD COLUMN rich_text TEXT NOT NULL DEFAULT ''"), error)
            || !execute(QStringLiteral("PRAGMA user_version=5"), error)) return false;
        if (!transaction.commit()) return fail(db_.lastError().text(), error);
    }
    if (version < 6) {
        Transaction transaction(db_);
        if (!transaction.active()) return fail(db_.lastError().text(), error);
        const QStringList migration = {
            QStringLiteral("CREATE TABLE media_assets (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                           "name TEXT NOT NULL, kind TEXT NOT NULL CHECK(kind IN ('image','video','audio')), "
                           "path TEXT NOT NULL UNIQUE, folder TEXT NOT NULL DEFAULT '', "
                           "loop INTEGER NOT NULL DEFAULT 0 CHECK(loop IN (0,1)), imported_at INTEGER)"),
            // Legacy imports remain presentations, sharing their original files with the Media Library.
            // Their original import time is unknown.
            QStringLiteral("INSERT INTO media_assets(name,kind,path,loop) "
                           "SELECT MIN(media_name),media_kind,media_path,MAX(media_loop) FROM slides "
                           "WHERE media_kind IN ('image','video') GROUP BY media_path,media_kind"),
            QStringLiteral("CREATE TABLE service_entries_v6 (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                           "service_id INTEGER NOT NULL REFERENCES services(id) ON DELETE CASCADE, "
                           "song_id INTEGER REFERENCES songs(id) ON DELETE CASCADE, "
                           "position INTEGER NOT NULL, slide_order TEXT, "
                           "media_asset_id INTEGER REFERENCES media_assets(id) ON DELETE RESTRICT, "
                           "CHECK((song_id IS NOT NULL AND media_asset_id IS NULL) OR "
                           "(song_id IS NULL AND media_asset_id IS NOT NULL AND slide_order IS NULL)))"),
            QStringLiteral("INSERT INTO service_entries_v6(id,service_id,song_id,position,slide_order) "
                           "SELECT id,service_id,song_id,position,slide_order FROM service_entries"),
            QStringLiteral("INSERT INTO sqlite_sequence(name,seq) SELECT 'service_entries_v6',seq FROM sqlite_sequence "
                           "WHERE name='service_entries' AND NOT EXISTS (SELECT 1 FROM sqlite_sequence WHERE name='service_entries_v6')"),
            QStringLiteral("UPDATE sqlite_sequence SET seq=MAX(seq,COALESCE((SELECT seq FROM sqlite_sequence "
                           "WHERE name='service_entries'),0)) WHERE name='service_entries_v6'"),
            QStringLiteral("DROP TABLE service_entries"),
            QStringLiteral("ALTER TABLE service_entries_v6 RENAME TO service_entries"),
            QStringLiteral("CREATE INDEX service_entries_order ON service_entries(service_id,position,id)"),
            QStringLiteral("CREATE INDEX service_entries_media ON service_entries(media_asset_id)"),
            QStringLiteral("CREATE INDEX slides_media_path ON slides(media_path)"),
            QStringLiteral("PRAGMA user_version=6")
        };
        for (const auto &sql : migration) if (!execute(sql, error)) return false;
        if (!transaction.commit()) return fail(db_.lastError().text(), error);
    }
    if (version < 7) {
        Transaction transaction(db_);
        if (!transaction.active()) return fail(db_.lastError().text(), error);
        if (!execute(QStringLiteral("ALTER TABLE songs ADD COLUMN song_structure TEXT NOT NULL DEFAULT ''"), error)
            || !execute(QStringLiteral("PRAGMA user_version=7"), error)) return false;
        if (!transaction.commit()) return fail(db_.lastError().text(), error);
    }
    if (version < 8) {
        Transaction transaction(db_);
        if (!transaction.active()) return fail(db_.lastError().text(), error);
        const QStringList migration = {
            QStringLiteral("CREATE TABLE looks(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL COLLATE NOCASE UNIQUE,settings TEXT NOT NULL)"),
            QStringLiteral("ALTER TABLE songs ADD COLUMN look_id INTEGER REFERENCES looks(id) ON DELETE SET NULL"),
            QStringLiteral("ALTER TABLE slides ADD COLUMN look_id INTEGER REFERENCES looks(id) ON DELETE SET NULL"),
            QStringLiteral("ALTER TABLE slides ADD COLUMN look_rich_override INTEGER NOT NULL DEFAULT 0 CHECK(look_rich_override IN (0,1))"),
            QStringLiteral("ALTER TABLE slides ADD COLUMN look_fit INTEGER NOT NULL DEFAULT 0 CHECK(look_fit IN (0,1))"),
            QStringLiteral("PRAGMA user_version=8")
        };
        for (const auto &sql : migration) if (!execute(sql, error)) return false;
        if (!transaction.commit()) return fail(db_.lastError().text(), error);
    }
    if (version < 9) {
        Transaction transaction(db_);
        if (!transaction.active()) return fail(db_.lastError().text(), error);
        const QStringList migration = {
            QStringLiteral("ALTER TABLE service_entries ADD COLUMN media_transition_type INTEGER NOT NULL DEFAULT 0 CHECK(media_transition_type BETWEEN 0 AND 3)"),
            QStringLiteral("ALTER TABLE service_entries ADD COLUMN media_transition_duration_ms INTEGER NOT NULL DEFAULT 500 CHECK(media_transition_duration_ms BETWEEN 0 AND 10000)"),
            QStringLiteral("ALTER TABLE service_entries ADD COLUMN media_transition_override INTEGER NOT NULL DEFAULT 0 CHECK(media_transition_override IN (0,1))"),
            QStringLiteral("PRAGMA user_version=9")
        };
        for (const auto &sql : migration) if (!execute(sql, error)) return false;
        if (!transaction.commit()) return fail(db_.lastError().text(), error);
    }
    if (version < 10) {
        // Rebuild the two legacy rowid tables with non-reusable identities.
        // Disable FKs outside the transaction, then verify every reference before committing.
        ForeignKeyPause foreignKeys(db_);
        if (!foreignKeys.active()) return fail("Could not prepare MIDI identity migration.", error);
        Transaction transaction(db_);
        if (!transaction.active()) return fail(db_.lastError().text(), error);
        const QStringList migration = {
            "CREATE TABLE songs_v10(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT NOT NULL,auto_advance_ms INTEGER NOT NULL DEFAULT 0 CHECK(auto_advance_ms=0 OR auto_advance_ms BETWEEN 100 AND 3600000),presentation_loop INTEGER NOT NULL DEFAULT 0 CHECK(presentation_loop IN(0,1)),transition_type INTEGER NOT NULL DEFAULT 0 CHECK(transition_type BETWEEN 0 AND 3),transition_duration_ms INTEGER NOT NULL DEFAULT 500 CHECK(transition_duration_ms BETWEEN 0 AND 10000),transition_override INTEGER NOT NULL DEFAULT 0 CHECK(transition_override IN(0,1)),background_path TEXT NOT NULL DEFAULT '',song_structure TEXT NOT NULL DEFAULT '',look_id INTEGER REFERENCES looks(id) ON DELETE SET NULL)",
            "INSERT INTO songs_v10 SELECT id,title,auto_advance_ms,presentation_loop,transition_type,transition_duration_ms,transition_override,background_path,song_structure,look_id FROM songs",
            "INSERT INTO sqlite_sequence(name,seq) SELECT 'songs_v10',0 WHERE NOT EXISTS(SELECT 1 FROM sqlite_sequence WHERE name='songs_v10')",
            "UPDATE sqlite_sequence SET seq=MAX(seq,COALESCE((SELECT seq FROM sqlite_sequence WHERE name='songs'),0)) WHERE name='songs_v10'",
            "CREATE TABLE slides_v10(id INTEGER PRIMARY KEY AUTOINCREMENT,song_id INTEGER NOT NULL REFERENCES songs(id) ON DELETE CASCADE,position INTEGER NOT NULL,text TEXT NOT NULL,media_kind TEXT NOT NULL DEFAULT '',media_path TEXT NOT NULL DEFAULT '',media_name TEXT NOT NULL DEFAULT '',media_loop INTEGER NOT NULL DEFAULT 0,rich_text TEXT NOT NULL DEFAULT '',look_id INTEGER REFERENCES looks(id) ON DELETE SET NULL,look_rich_override INTEGER NOT NULL DEFAULT 0 CHECK(look_rich_override IN(0,1)),look_fit INTEGER NOT NULL DEFAULT 0 CHECK(look_fit IN(0,1)))",
            "INSERT INTO slides_v10 SELECT id,song_id,position,text,media_kind,media_path,media_name,media_loop,rich_text,look_id,look_rich_override,look_fit FROM slides",
            "INSERT INTO sqlite_sequence(name,seq) SELECT 'slides_v10',0 WHERE NOT EXISTS(SELECT 1 FROM sqlite_sequence WHERE name='slides_v10')",
            "UPDATE sqlite_sequence SET seq=MAX(seq,COALESCE((SELECT seq FROM sqlite_sequence WHERE name='slides'),0)) WHERE name='slides_v10'",
            "DROP TABLE slides", "DROP TABLE songs", "ALTER TABLE songs_v10 RENAME TO songs", "ALTER TABLE slides_v10 RENAME TO slides",
            "CREATE INDEX slides_order ON slides(song_id,position,id)",
            "CREATE INDEX slides_media_path ON slides(media_path)",
            "CREATE TABLE midi_slide_actions(id INTEGER PRIMARY KEY AUTOINCREMENT,slide_id INTEGER REFERENCES slides(id) ON DELETE CASCADE,entry_id INTEGER REFERENCES service_entries(id) ON DELETE CASCADE,asset_id INTEGER REFERENCES media_assets(id) ON DELETE CASCADE,position INTEGER NOT NULL,payload TEXT NOT NULL,CHECK((slide_id IS NOT NULL)+(entry_id IS NOT NULL)+(asset_id IS NOT NULL)=1))",
            "CREATE INDEX midi_actions_slide ON midi_slide_actions(slide_id,position,id)",
            "CREATE INDEX midi_actions_entry ON midi_slide_actions(entry_id,position,id)",
            "CREATE INDEX midi_actions_asset ON midi_slide_actions(asset_id,position,id)",
            "CREATE TABLE midi_bindings(id INTEGER PRIMARY KEY AUTOINCREMENT,payload TEXT NOT NULL,song_id INTEGER REFERENCES songs(id) ON DELETE CASCADE,slide_id INTEGER REFERENCES slides(id) ON DELETE CASCADE,entry_id INTEGER REFERENCES service_entries(id) ON DELETE CASCADE,asset_id INTEGER REFERENCES media_assets(id) ON DELETE CASCADE)",
            "PRAGMA user_version=10"
        };
        for (const auto &sql : migration) if (!execute(sql, error)) return false;
        QSqlQuery check(db_);
        if(!check.exec("PRAGMA foreign_key_check"))return fail(check.lastError().text(),error);
        if(check.next())return fail("Existing library references failed the MIDI migration integrity check. No migration changes were committed.",error);
        if(!transaction.commit())return fail(db_.lastError().text(),error);
    }
    if (version < 11 && !migrateCleanup(error)) return false;
    if (!recoverCleanup(error)) return false;
    return true;
}

QString LibraryRepository::databaseDirectory() const {
    const QString path = db_.databaseName();
    return path.isEmpty() || path == QStringLiteral(":memory:") ? QString() : QFileInfo(path).absolutePath();
}

qint64 LibraryRepository::dataVersion() const {
    clearError();
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("PRAGMA data_version")) || !query.next()) {
        fail(query.lastError().text());
        return -1;
    }
    return query.value(0).toLongLong();
}

QVector<Song> LibraryRepository::songs(const QString &search) const {
    clearError();
    QVector<Song> result;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id FROM songs WHERE NOT EXISTS(SELECT 1 FROM content_usage u WHERE u.song_id=songs.id AND u.archived_batch IS NOT NULL) AND (title LIKE ? ESCAPE '\\' OR EXISTS "
        "(SELECT 1 FROM slides WHERE song_id=songs.id AND (text LIKE ? ESCAPE '\\' OR media_name LIKE ? ESCAPE '\\'))) ORDER BY title COLLATE NOCASE,id"));
    QString escaped = search;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("%"), QStringLiteral("\\%"));
    escaped.replace(QStringLiteral("_"), QStringLiteral("\\_"));
    const QString pattern = QStringLiteral("%") + escaped + QStringLiteral("%");
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    while (query.next()) {
        const Song value = song(query.value(0).toInt());
        if (!lastError_.isEmpty()) return {};
        result.append(value);
    }
    return result;
}

Song LibraryRepository::song(int id) const {
    clearError();
    Song result;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,title,auto_advance_ms,presentation_loop,transition_type,transition_duration_ms,transition_override,background_path,song_structure,look_id "
                                 "FROM songs WHERE id=? AND NOT EXISTS(SELECT 1 FROM content_usage u WHERE u.song_id=songs.id AND u.archived_batch IS NOT NULL)"));
    query.addBindValue(id);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    if (!query.next()) return {};
    result.id = query.value(0).toInt();
    result.title = query.value(1).toString();
    // Refuse malformed stored settings rather than silently changing a live
    // presentation's timing or interpreting a future transition as a cut.
    for (int column = 2; column <= 6; ++column) {
        bool valid = false;
        const int value = query.value(column).toInt(&valid);
        if (!valid || query.value(column).toDouble() != value) {
            fail(QStringLiteral("A presentation contains invalid timing or transition settings."));
            return {};
        }
    }
    result.settings.autoAdvanceMs = query.value(2).toInt();
    const int loop = query.value(3).toInt();
    result.settings.loop = loop != 0;
    result.settings.transition = SlideTransitionType(query.value(4).toInt());
    result.settings.transitionDurationMs = query.value(5).toInt();
    const int overrideTransition = query.value(6).toInt();
    result.settings.overrideTransition = overrideTransition != 0;
    result.backgroundPath = query.value(7).toString();
    result.appearance.lookId = query.value(9).toInt();
    if (!result.settings.isValid() || (loop != 0 && loop != 1)
        || (overrideTransition != 0 && overrideTransition != 1)) {
        fail(QStringLiteral("A presentation contains invalid timing or transition settings."));
        return {};
    }
    if (!validBackgroundPath(result.backgroundPath)) {
        fail(QStringLiteral("A presentation contains an invalid background path."));
        return {};
    }
    QSqlQuery slides(db_);
    slides.prepare(QStringLiteral("SELECT text,media_kind,media_path,media_name,media_loop,rich_text,look_id,look_rich_override,look_fit,id FROM slides WHERE song_id=? ORDER BY position,id"));
    slides.addBindValue(id);
    if (!slides.exec()) { fail(slides.lastError().text()); return {}; }
    while (slides.next()) {
        result.slideIds.append(slides.value(9).toLongLong());
        result.slides << slides.value(0).toString();
        result.richText << slides.value(5).toString();
        result.appearance.slideLookIds.append(slides.value(6).toInt());
        result.appearance.richTextOverrides.append(slides.value(7).toBool());
        result.appearance.fitText.append(slides.value(8).toBool());
        const QString kind = slides.value(1).toString();
        if (!kind.isEmpty() && kind != QStringLiteral("image") && kind != QStringLiteral("video") && kind != QStringLiteral("audio")) {
            fail(QStringLiteral("A presentation contains an unknown media type."));
            return {};
        }
        result.media.append({kind == QStringLiteral("image") ? MediaKind::Image
                            : kind == QStringLiteral("video") ? MediaKind::Video
                            : kind == QStringLiteral("audio") ? MediaKind::Audio : MediaKind::None,
                             slides.value(2).toString(), slides.value(3).toString(), slides.value(4).toBool()});
    }
    QString structureError;
    if (!SongStructure::fromJson(query.value(8).toString(), int(result.slides.size()), &result.structure, &structureError)) {
        fail(structureError);
        return {};
    }
    if (!result.structure.isEmpty()) {
        for (const auto &media : result.media) if (media.kind != MediaKind::None) {
            fail(QStringLiteral("Song sections cannot contain media slides."));
            return {};
        }
    }
    if (!validRichText(result.slides, result.richText)) {
        fail(QStringLiteral("A presentation contains invalid or oversized rich text."));
        return {};
    }
    result.midiActions.resize(result.slides.size());
    QHash<qint64,int> positions;
    for(int i=0;i<result.slideIds.size();++i)positions.insert(result.slideIds.at(i),i);
    QSqlQuery actions(db_);
    actions.prepare("SELECT slide_id,payload FROM midi_slide_actions WHERE slide_id IN(SELECT id FROM slides WHERE song_id=?) ORDER BY position,id");
    actions.addBindValue(id);
    if(!actions.exec()){fail(actions.lastError().text());return {};}
    while(actions.next()){
        const auto payload=actions.value(1).toString();MidiOutputAction action;
        const int position=positions.value(actions.value(0).toLongLong(),-1);
        if(position<0||payload.size()>65536||!MidiOutputAction::fromJson(QJsonDocument::fromJson(payload.toUtf8()).object(),&action)
            ||result.midiActions.at(position).size()>=64){fail("A slide contains invalid or oversized MIDI actions.");return {};}
        result.midiActions[position].append(action);
    }
    QHash<int, SavedLook> savedLooks;
    for (int i = 0; i < result.slides.size(); ++i) {
        const int id = result.appearance.slideLookIds.value(i) > 0 ? result.appearance.slideLookIds.at(i) : result.appearance.lookId;
        SlideLook style;
        style.id = id;
        if (id > 0) {
            if (!savedLooks.contains(id)) {
                const auto saved = look(id);
                if (!lastError_.isEmpty()) return {};
                if (!saved.id) { fail(QStringLiteral("A presentation references a missing Look.")); return {}; }
                savedLooks.insert(id, saved);
            }
            style.settings = savedLooks.value(id).settings;
        }
        style.richTextOverrides = result.appearance.richTextOverrides.value(i);
        style.fitText = result.appearance.fitText.value(i);
        result.looks.append(style);
    }
    return result;
}

bool LibraryRepository::storeSlides(int songId, const QStringList &slides, QString *error,
                                     const QStringList &richText, const PresentationAppearance &appearance) {
    QSqlQuery query(db_);
    // The text-only form is also used before rich_text exists during version-0 seeding.
    const bool styled = appearance.lookId > 0 || !appearance.slideLookIds.isEmpty();
    query.prepare(styled
        ? QStringLiteral("INSERT INTO slides(song_id,position,text,rich_text,look_id,look_rich_override,look_fit) VALUES(?,?,?,?,?,?,?)")
        : richText.isEmpty()
        ? QStringLiteral("INSERT INTO slides(song_id,position,text) VALUES(?,?,?)")
        : QStringLiteral("INSERT INTO slides(song_id,position,text,rich_text) VALUES(?,?,?,?)"));
    for (int i = 0; i < slides.size(); ++i) {
        query.bindValue(0, songId);
        query.bindValue(1, i);
        const QString text = richText.value(i).isEmpty() ? slides.at(i).trimmed() : slides.at(i);
        query.bindValue(2, text.isNull() ? QStringLiteral("") : text);
        if (styled || !richText.isEmpty()) query.bindValue(3, richText.value(i).isNull() ? QStringLiteral("") : richText.value(i));
        if (styled) {
            query.bindValue(4, appearance.slideLookIds.value(i) > 0 ? QVariant(appearance.slideLookIds.at(i)) : QVariant());
            query.bindValue(5, appearance.richTextOverrides.value(i));
            query.bindValue(6, appearance.fitText.value(i));
        }
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    return true;
}

bool LibraryRepository::createSong(const QString &title, const QStringList &slides, QString *error,
                                   const PresentationSettings &settings, const QString &backgroundPath,
                                   const QStringList &richText, const SongStructure &structure, const PresentationAppearance &appearance) {
    return createTextPresentation(title, slides, error, settings, backgroundPath, richText, structure, appearance) > 0;
}

int LibraryRepository::createTextPresentation(const QString &title, const QStringList &slides, QString *error,
                                   const PresentationSettings &settings, const QString &backgroundPath,
                                   const QStringList &richText, const SongStructure &structure, const PresentationAppearance &appearance) {
    clearError(error);
    if (title.trimmed().isEmpty() || slides.isEmpty())
        return fail(QStringLiteral("A song must have a title and at least one slide."), error);
    if (!settings.isValid()) return fail(QStringLiteral("The presentation timing or transition settings are invalid."), error);
    if (!validBackgroundPath(backgroundPath))
        return fail(QStringLiteral("Choose a managed image from the background library."), error);
    if (!validRichText(slides, richText))
        return fail(QStringLiteral("Provide one rich-text document per slide, no larger than 1 MB each (16 MB total)."), error);
    QString structureError;
    if (!structure.validate(int(slides.size()), &structureError)) return fail(structureError, error);
    if (!appearance.validate(int(slides.size()))) return fail(QStringLiteral("Invalid slide Look assignments."), error);
    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO songs(title,auto_advance_ms,presentation_loop,transition_type,transition_duration_ms,transition_override,background_path,song_structure,look_id) VALUES(?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(title.trimmed());
    query.addBindValue(settings.autoAdvanceMs);
    query.addBindValue(settings.loop);
    query.addBindValue(int(settings.transition));
    query.addBindValue(settings.transitionDurationMs);
    query.addBindValue(settings.overrideTransition);
    query.addBindValue(backgroundPath.isNull() ? QStringLiteral("") : backgroundPath);
    query.addBindValue(structure.toJson());
    query.addBindValue(appearance.lookId > 0 ? QVariant(appearance.lookId) : QVariant());
    if (!query.exec()) return fail(query.lastError().text(), error);
    const int id = query.lastInsertId().toInt();
    if (!storeSlides(id, slides, error, richText, appearance)) return 0;
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return id;
}

int LibraryRepository::createPresentation(const QString &title, const QStringList &texts,
                                          const QVector<SlideMedia> &media, QString *error,
                                          const PresentationSettings &settings, const QString &backgroundPath,
                                          const QStringList &richText, const PresentationAppearance &appearance) {
    clearError(error);
    if (title.trimmed().isEmpty() || texts.isEmpty() || texts.size() != media.size()) {
        fail(QStringLiteral("A presentation needs a title and matching slide and media lists."), error);
        return 0;
    }
    if (!settings.isValid()) {
        fail(QStringLiteral("The presentation timing or transition settings are invalid."), error);
        return 0;
    }
    if (!validRichText(texts, richText)) {
        fail(QStringLiteral("Provide one rich-text document per slide, no larger than 1 MB each (16 MB total)."), error);
        return 0;
    }
    bool hasMedia = false;
    QSet<QString> names;
    for (int i = 0; i < media.size(); ++i) {
        const auto &item = media.at(i);
        if (item.kind == MediaKind::None) {
            if ((texts.at(i).trimmed().isEmpty() && richText.isEmpty())
                || !item.path.isEmpty() || !item.name.isEmpty() || item.loop) {
                fail(QStringLiteral("A text slide needs content and cannot contain media settings."), error);
                return 0;
            }
            continue;
        }
        if (item.kind != MediaKind::Image && item.kind != MediaKind::Video && item.kind != MediaKind::Audio) {
            fail(QStringLiteral("The presentation contains an unsupported media type."), error);
            return 0;
        }
        const QString cleanPath = QDir::cleanPath(item.path);
        if (!validAsset(item) || QDir::isAbsolutePath(item.path) || !cleanPath.startsWith(QStringLiteral("media/"))
            || cleanPath != item.path || item.path.contains(QLatin1Char('\\'))
            || item.path.contains(QChar::Null) || item.name.trimmed().isEmpty()
            || item.name.contains(QLatin1Char('/')) || item.name.contains(QLatin1Char('\\'))
            || item.name.contains(QChar::Null) || (item.kind == MediaKind::Image && item.loop)) {
            fail(QStringLiteral("Media slides need a managed relative path and a valid filename; only videos can loop."), error);
            return 0;
        }
        const QString key = item.name.trimmed().toCaseFolded();
        if (names.contains(key)) {
            fail(QStringLiteral("Each media slide in a presentation must have a unique name."), error);
            return 0;
        }
        names.insert(key);
        hasMedia = true;
    }
    if (!hasMedia) {
        fail(QStringLiteral("Use the song editor to create presentations containing only text."), error);
        return 0;
    }
    if (!validBackgroundPath(backgroundPath)) {
        fail(QStringLiteral("Choose a managed image from the background library."), error);
        return 0;
    }
    if (!appearance.validate(int(texts.size()))) { fail(QStringLiteral("Invalid slide Look assignments."), error); return 0; }
    Transaction transaction(db_);
    if (!transaction.active()) { fail(db_.lastError().text(), error); return 0; }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO songs(title,auto_advance_ms,presentation_loop,transition_type,transition_duration_ms,transition_override,background_path,look_id) VALUES(?,?,?,?,?,?,?,?)"));
    query.addBindValue(title.trimmed());
    query.addBindValue(settings.autoAdvanceMs);
    query.addBindValue(settings.loop);
    query.addBindValue(int(settings.transition));
    query.addBindValue(settings.transitionDurationMs);
    query.addBindValue(settings.overrideTransition);
    query.addBindValue(backgroundPath.isNull() ? QStringLiteral("") : backgroundPath);
    query.addBindValue(appearance.lookId > 0 ? QVariant(appearance.lookId) : QVariant());
    if (!query.exec()) { fail(query.lastError().text(), error); return 0; }
    const int id = query.lastInsertId().toInt();
    query.prepare(QStringLiteral("INSERT INTO slides(song_id,position,text,media_kind,media_path,media_name,media_loop,rich_text,look_id,look_rich_override,look_fit) "
                                 "VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
    for (int i = 0; i < texts.size(); ++i) {
        const auto &item = media.at(i);
        query.bindValue(0, id);
        query.bindValue(1, i);
        const QString text = richText.value(i).isEmpty() ? texts.at(i).trimmed() : texts.at(i);
        query.bindValue(2, text.isNull() ? QStringLiteral("") : text);
        query.bindValue(3, storedMediaKind(item.kind));
        query.bindValue(4, item.path.isNull() ? QStringLiteral("") : item.path);
        query.bindValue(5, item.name.isNull() ? QStringLiteral("") : item.name.trimmed());
        query.bindValue(6, item.loop);
        query.bindValue(7, richText.value(i).isNull() ? QStringLiteral("") : richText.value(i));
        query.bindValue(8, appearance.slideLookIds.value(i) > 0 ? QVariant(appearance.slideLookIds.at(i)) : QVariant());
        query.bindValue(9, appearance.richTextOverrides.value(i));
        query.bindValue(10, appearance.fitText.value(i));
        if (!query.exec()) { fail(query.lastError().text(), error); return 0; }
        if (item.kind != MediaKind::None && !registerMediaAsset(item, error)) return 0;
    }
    if (!transaction.commit()) { fail(db_.lastError().text(), error); return 0; }
    return id;
}

bool LibraryRepository::renamePresentation(int songId, const QString &title, QString *error) {
    clearError(error);
    if (title.trimmed().isEmpty()) return fail(QStringLiteral("A presentation must have a title."), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE songs SET title=? WHERE id=?"));
    query.addBindValue(title.trimmed());
    query.addBindValue(songId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.numRowsAffected()) return fail(QStringLiteral("This presentation no longer exists."), error);
    return true;
}

bool LibraryRepository::updatePresentationSettings(int songId, const PresentationSettings &settings, QString *error) {
    clearError(error);
    if (!settings.isValid())
        return fail(QStringLiteral("Use an auto-advance interval of 0.1–3600 seconds (or turn it off), "
                                   "a supported transition, and a transition duration of 0–10 seconds."), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE songs SET auto_advance_ms=?,presentation_loop=?,transition_type=?,transition_duration_ms=?,transition_override=? WHERE id=?"));
    query.addBindValue(settings.autoAdvanceMs);
    query.addBindValue(settings.loop);
    query.addBindValue(int(settings.transition));
    query.addBindValue(settings.transitionDurationMs);
    query.addBindValue(settings.overrideTransition);
    query.addBindValue(songId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.numRowsAffected()) return fail(QStringLiteral("This presentation no longer exists."), error);
    return true;
}

bool LibraryRepository::updateTransitions(const QVector<TransitionChange> &changes, QString *error) {
    clearError(error);
    if (changes.isEmpty()) return fail(QStringLiteral("Select service items to edit transitions."), error);
    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    QSet<QString> targets;
    for (const auto &change : changes) {
        if (change.id <= 0 || !change.before.isValid() || !change.after.isValid()
            || (change.target != TransitionChange::Target::Presentation && change.target != TransitionChange::Target::MediaEntry))
            return fail(QStringLiteral("Invalid transition change."), error);
        const bool media = change.target == TransitionChange::Target::MediaEntry;
        const QString key = QString::number(int(change.target)) + QLatin1Char(':') + QString::number(change.id);
        if (targets.contains(key)) return fail(QStringLiteral("A transition target was selected twice."), error);
        targets.insert(key);
        const QString table = media ? QStringLiteral("service_entries") : QStringLiteral("songs");
        const QString prefix = media ? QStringLiteral("media_") : QString();
        const QString condition = media ? QStringLiteral(" AND media_asset_id IS NOT NULL") : QString();
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("SELECT %1transition_type,%1transition_duration_ms,%1transition_override FROM %2 WHERE id=?%3")
                      .arg(prefix, table, condition));
        query.addBindValue(change.id);
        if (!query.exec()) return fail(query.lastError().text(), error);
        if (!query.next()) return fail(QStringLiteral("A selected transition target no longer exists. No changes were saved."), error);
        PresentationSettings current;
        current.transition = SlideTransitionType(query.value(0).toInt());
        current.transitionDurationMs = query.value(1).toInt();
        current.overrideTransition = query.value(2).toBool();
        if (!current.sameTransition(change.before))
            return fail(QStringLiteral("Transitions changed since this edit. No changes were saved; review the current settings."), error);
        query.finish();
        query.prepare(QStringLiteral("UPDATE %1 SET %2transition_type=?,%2transition_duration_ms=?,%2transition_override=? WHERE id=?%3")
                      .arg(table, prefix, condition));
        query.addBindValue(int(change.after.transition));
        query.addBindValue(change.after.transitionDurationMs);
        query.addBindValue(change.after.overrideTransition);
        query.addBindValue(change.id);
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return true;
}

bool LibraryRepository::updatePresentationBackground(int songId, const QString &path, QString *error) {
    clearError(error);
    if (!validBackgroundPath(path))
        return fail(QStringLiteral("Choose a managed image from the background library."), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE songs SET background_path=? WHERE id=?"));
    query.addBindValue(path.isNull() ? QStringLiteral("") : path);
    query.addBindValue(songId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.numRowsAffected()) return fail(QStringLiteral("This presentation no longer exists."), error);
    return true;
}

bool LibraryRepository::updatePresentationMediaNames(int songId, const QStringList &names, QString *error) {
    clearError(error);
    const Song source = song(songId);
    if (!lastError_.isEmpty()) return fail(lastError_, error);
    if (!source.id || names.size() != source.media.size())
        return fail(QStringLiteral("A rename must provide one name for each slide in the presentation."), error);
    bool hasMedia = false;
    QSet<QString> unique;
    for (int i = 0; i < names.size(); ++i) {
        if (source.media.at(i).kind == MediaKind::None) {
            if (!names.at(i).isEmpty()) return fail(QStringLiteral("Text slides do not have media filenames."), error);
            continue;
        }
        const QString name = names.at(i).trimmed();
        if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
            || name.contains(QChar::Null))
            return fail(QStringLiteral("Each media slide needs a filename without path separators."), error);
        const QString key = name.toCaseFolded();
        if (unique.contains(key)) return fail(QStringLiteral("The new media names must be unique within the presentation."), error);
        unique.insert(key);
        hasMedia = true;
    }
    if (!hasMedia) return fail(QStringLiteral("This song contains no media to rename."), error);
    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    QSqlQuery ids(db_);
    ids.prepare(QStringLiteral("SELECT id FROM slides WHERE song_id=? ORDER BY position,id"));
    ids.addBindValue(songId);
    if (!ids.exec()) return fail(ids.lastError().text(), error);
    QVector<int> slideIds;
    while (ids.next()) slideIds.append(ids.value(0).toInt());
    ids.finish();
    if (slideIds.size() != names.size()) return fail(QStringLiteral("The presentation changed before it could be renamed. Try again."), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE slides SET media_name=? WHERE id=?"));
    for (int i = 0; i < slideIds.size(); ++i) {
        query.bindValue(0, names.at(i).isNull() ? QStringLiteral("") : names.at(i).trimmed());
        query.bindValue(1, slideIds.at(i));
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return true;
}

bool LibraryRepository::updateSong(int id, const QString &title, const QStringList &slides, QString *error) {
    clearError(error);
    if (title.trimmed().isEmpty() || slides.isEmpty())
        return fail(QStringLiteral("A song must have a title and at least one slide."), error);
    const Song original = song(id);
    if (!lastError_.isEmpty()) return fail(lastError_, error);
    if (!original.id) return fail(QStringLiteral("This song no longer exists."), error);
    for (const auto &media : original.media)
        if (media.kind != MediaKind::None)
            return fail(QStringLiteral("Use media presentation controls to edit this item; the song text editor cannot replace media slides."), error);
    if (!original.structure.isEmpty() || original.slides.size() == slides.size()) {
        if (original.slides.size() != slides.size())
            return fail(QStringLiteral("Use Song Sections / Arrangements to change this song's slide groups."), error);
        QStringList texts, rich;
        for (int i = 0; i < slides.size(); ++i) {
            texts.append(slides.at(i).trimmed());
            rich.append(texts.last() == original.slides.at(i) ? original.richText.value(i) : QStringLiteral(""));
        }
        auto overrides = original.appearance.richTextOverrides;
        for (int i = 0; i < rich.size(); ++i) if (rich.at(i).isEmpty()) overrides[i] = false;
        return updatePresentationText(id, title, texts, rich, error, overrides);
    }
    QSqlQuery midiCheck(db_);
    midiCheck.prepare("SELECT 1 FROM midi_bindings WHERE song_id=? AND slide_id IS NOT NULL UNION ALL SELECT 1 FROM midi_slide_actions WHERE slide_id IN(SELECT id FROM slides WHERE song_id=?) LIMIT 1");
    midiCheck.addBindValue(id); midiCheck.addBindValue(id);
    if(!midiCheck.exec())return fail(midiCheck.lastError().text(),error);
    if(midiCheck.next())return fail("Use Song Sections / Arrangements or Looks / Reflow to change pages with MIDI assignments while preserving their identities.",error);
    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE songs SET title=? WHERE id=?"));
    query.addBindValue(title.trimmed());
    query.addBindValue(id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    query.prepare(QStringLiteral("DELETE FROM slides WHERE song_id=?"));
    query.addBindValue(id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    QStringList preservedRichText;
    if (original.slides.size() == slides.size()) {
        for (int i = 0; i < slides.size(); ++i)
            preservedRichText.append(original.slides.at(i) == slides.at(i).trimmed()
                ? original.richText.value(i) : QString());
    }
    auto appearance = original.appearance;
    appearance.slideLookIds.resize(slides.size());
    appearance.richTextOverrides.resize(slides.size());
    appearance.fitText.resize(slides.size());
    for (int i = 0; i < slides.size(); ++i) if (preservedRichText.value(i).isEmpty()) appearance.richTextOverrides[i] = false;
    if (!storeSlides(id, slides, error, preservedRichText, appearance)) return false;
    // Source indices no longer identify the same arrangement after a slide-count change.
    if (original.slides.size() != slides.size()) {
        query.prepare(QStringLiteral("UPDATE service_entries SET slide_order=NULL WHERE song_id=?"));
        query.addBindValue(id);
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return true;
}

bool LibraryRepository::updatePresentationText(int songId, const QString &title,
    const QStringList &texts, const QStringList &richText, QString *error, const QVector<bool> &lookOverrides) {
    clearError(error);
    if (title.trimmed().isEmpty() || texts.isEmpty())
        return fail(QStringLiteral("A presentation needs a title and at least one slide."), error);
    if ((!lookOverrides.isEmpty() && lookOverrides.size() != texts.size())
        || richText.size() != texts.size() || !validRichText(texts, richText))
        return fail(QStringLiteral("Provide one rich-text document per slide, no larger than 1 MB each (16 MB total)."), error);

    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id FROM slides WHERE song_id=? ORDER BY position,id"));
    query.addBindValue(songId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    QVector<int> slideIds;
    while (query.next()) slideIds.append(query.value(0).toInt());
    query.finish();
    if (slideIds.size() != texts.size())
        return fail(QStringLiteral("The presentation's slides changed while editing. Reopen the editor before saving."), error);
    query.prepare(QStringLiteral("UPDATE songs SET title=? WHERE id=?"));
    query.addBindValue(title.trimmed());
    query.addBindValue(songId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.numRowsAffected()) return fail(QStringLiteral("This presentation no longer exists."), error);
    query.prepare(QStringLiteral("UPDATE slides SET text=?,rich_text=?,look_rich_override=COALESCE(?,look_rich_override) WHERE id=? AND song_id=?"));
    for (int i = 0; i < texts.size(); ++i) {
        query.bindValue(0, texts.at(i).isNull() ? QStringLiteral("") : texts.at(i));
        query.bindValue(1, richText.at(i).isNull() ? QStringLiteral("") : richText.at(i));
        query.bindValue(2, lookOverrides.isEmpty() ? QVariant() : QVariant(lookOverrides.at(i)));
        query.bindValue(3, slideIds.at(i));
        query.bindValue(4, songId);
        if (!query.exec()) return fail(query.lastError().text(), error);
        if (query.numRowsAffected() != 1)
            return fail(QStringLiteral("A slide could not be saved. Reopen the editor and try again."), error);
    }
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return true;
}

bool LibraryRepository::updateSongStructure(const Song &original, const Song &draft, QString *error) {
    clearError(error);
    QString validation;
    if (draft.id != original.id || draft.title.trimmed().isEmpty() || draft.title.size() > 256
        || draft.slides.isEmpty() || draft.structure.isEmpty()
        || draft.richText.size() != draft.slides.size() || !validRichText(draft.slides, draft.richText)
        || !draft.structure.validate(int(draft.slides.size()), &validation)
        || !original.structure.validate(int(original.slides.size()), &validation))
        return fail(validation.isEmpty() ? QStringLiteral("Provide a complete song with valid sections, arrangements, and slide text.") : validation, error);
    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    const Song current = song(original.id);
    if (!lastError_.isEmpty()) return fail(lastError_, error);
    if (!current.id || current.title != original.title || current.slides != original.slides
        || current.richText != original.richText || current.appearance != original.appearance
        || (!current.structure.isEmpty() && current.structure.toJson() != original.structure.toJson()))
        return fail(QStringLiteral("This song changed while editing. Reopen Song Sections / Arrangements before saving."), error);
    for (const auto &media : current.media) if (media.kind != MediaKind::None)
        return fail(QStringLiteral("Song sections support text presentations. Use the media editor for this presentation."), error);
    const auto oldKeys = original.structure.slideKeys();
    const auto newKeys = draft.structure.slideKeys();
    if (oldKeys.size() != original.slides.size())
        return fail(QStringLiteral("Open the section editor to group this song's existing slides before changing its structure."), error);

    QHash<QString, int> newPositions;
    for (int i = 0; i < newKeys.size(); ++i) newPositions.insert(newKeys.at(i), i);
    QHash<int, SongSection> oldSectionsByStart;
    int sectionStart = 0;
    for (const auto &section : original.structure.sections) {
        oldSectionsByStart.insert(sectionStart, section);
        sectionStart += section.slideCount;
    }
    QHash<QString, QPair<int, int>> newSectionRanges;
    sectionStart = 0;
    for (const auto &section : draft.structure.sections) {
        newSectionRanges.insert(section.id, {sectionStart, section.slideCount});
        sectionStart += section.slideCount;
    }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,slide_order FROM service_entries WHERE song_id=? AND slide_order IS NOT NULL"));
    query.addBindValue(original.id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    QVector<QPair<int, QVector<int>>> remapped;
    while (query.next()) {
        QVector<int> oldOrder, newOrder;
        if (!decodeOrder(query.value(1), &oldOrder)) return fail(QStringLiteral("A saved service has an invalid arrangement."), error);
        for (int position = 0; position < oldOrder.size();) {
            const int index = oldOrder.at(position);
            if (index < 0 || index >= oldKeys.size()) return fail(QStringLiteral("A saved service references a missing slide."), error);
            // Whole-section occurrences expand together when the section gains slides.
            int groupCount = 1;
            QString sectionId;
            const auto oldSection = oldSectionsByStart.constFind(index);
            if (oldSection != oldSectionsByStart.cend()) {
                const auto &section = oldSection.value();
                bool wholeSection = position + section.slideCount <= oldOrder.size();
                for (int offset = 0; wholeSection && offset < section.slideCount; ++offset)
                    wholeSection = oldOrder.at(position + offset) == index + offset;
                if (wholeSection) { groupCount = section.slideCount; sectionId = section.id; }
            }
            if (!sectionId.isEmpty()) {
                const auto found = newSectionRanges.constFind(sectionId);
                if (found == newSectionRanges.cend()) return fail(QStringLiteral("A saved service uses a section you removed. Reset that service arrangement before removing the section."), error);
                const auto range = found.value();
                for (int offset = 0; offset < range.second; ++offset) newOrder.append(range.first + offset);
            } else {
                const auto found = newPositions.constFind(oldKeys.at(index));
                if (found == newPositions.cend()) return fail(QStringLiteral("A saved service uses a slide you removed. Reset that service arrangement before removing the slide."), error);
                newOrder.append(found.value());
            }
            if (newOrder.size() > 10000) return fail(QStringLiteral("A saved service arrangement would exceed 10000 slides."), error);
            position += groupCount;
        }
        remapped.append({query.value(0).toInt(), newOrder});
    }
    query.finish();
    query.prepare(QStringLiteral("SELECT id FROM slides WHERE song_id=? ORDER BY position,id"));
    query.addBindValue(original.id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    QHash<QString, int> slideIds;
    int index = 0;
    while (query.next()) slideIds.insert(oldKeys.at(index++), query.value(0).toInt());
    query.finish();
    for (int i = 0; i < draft.slides.size(); ++i) {
        const int slideId = slideIds.take(newKeys.at(i));
        query.prepare(slideId
            ? QStringLiteral("UPDATE slides SET position=?,text=?,rich_text=?,look_rich_override=CASE WHEN ? THEN 0 ELSE look_rich_override END WHERE song_id=? AND id=?")
            : QStringLiteral("INSERT INTO slides(position,text,rich_text,look_id,look_rich_override,look_fit,song_id) VALUES(?,?,?,?,?,?,?)"));
        query.addBindValue(i);
        query.addBindValue(draft.slides.at(i).isNull() ? QStringLiteral("") : draft.slides.at(i));
        query.addBindValue(draft.richText.at(i).isNull() ? QStringLiteral("") : draft.richText.at(i));
        if (slideId) query.addBindValue(draft.richText.at(i).isEmpty());
        else {
            const bool aligned = draft.appearance.slideLookIds.size() == draft.slides.size();
            const int lookId = aligned ? draft.appearance.slideLookIds.value(i) : 0;
            query.addBindValue(lookId > 0 ? QVariant(lookId) : QVariant());
            query.addBindValue(aligned && draft.appearance.richTextOverrides.value(i));
            query.addBindValue(aligned && draft.appearance.fitText.value(i));
        }
        query.addBindValue(original.id);
        if (slideId) query.addBindValue(slideId);
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    query.prepare(QStringLiteral("DELETE FROM slides WHERE song_id=? AND id=?"));
    for (int slideId : slideIds) {
        query.bindValue(0, original.id);
        query.bindValue(1, slideId);
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    query.prepare(QStringLiteral("UPDATE service_entries SET slide_order=? WHERE id=? AND song_id=?"));
    for (const auto &entry : remapped) {
        query.bindValue(0, entry.second.isEmpty() ? QVariant() : QVariant(encodedOrder(entry.second)));
        query.bindValue(1, entry.first);
        query.bindValue(2, original.id);
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    query.prepare(QStringLiteral("UPDATE songs SET title=?,song_structure=? WHERE id=?"));
    query.addBindValue(draft.title.trimmed());
    query.addBindValue(draft.structure.toJson());
    query.addBindValue(original.id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return true;
}

bool LibraryRepository::deleteSong(int id, QString *error) {
    clearError(error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM songs WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.numRowsAffected()) return fail(QStringLiteral("This song no longer exists."), error);
    return true;
}

QVector<SavedLook> LibraryRepository::looks() const {
    clearError();
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT id FROM looks ORDER BY name COLLATE NOCASE,id"))) { fail(query.lastError().text()); return {}; }
    QVector<SavedLook> result;
    while (query.next()) {
        const auto saved = look(query.value(0).toInt());
        if (!lastError_.isEmpty()) return {};
        result.append(saved);
    }
    return result;
}

SavedLook LibraryRepository::look(int id) const {
    clearError();
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,name,settings FROM looks WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    if (!query.next()) return {};
    SavedLook result;
    result.id = query.value(0).toInt();
    result.name = query.value(1).toString();
    QString error;
    if (!PresentationLook::fromJson(query.value(2).toString(), &result.settings, &error)) { fail(error); return {}; }
    return result;
}

int LibraryRepository::createLook(const QString &name, const PresentationLook &settings, QString *error) {
    clearError(error);
    QString validation;
    if (name.trimmed().isEmpty() || name.size() > 100 || name.contains(QChar::Null))
        return fail(QStringLiteral("Give this Look a name of 1–100 characters."), error);
    if (!settings.validate(&validation)) return fail(validation, error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO looks(name,settings) VALUES(?,?)"));
    query.addBindValue(name.trimmed());
    query.addBindValue(settings.toJson());
    if (!query.exec()) return fail(query.lastError().text(), error);
    return query.lastInsertId().toInt();
}

bool LibraryRepository::updatePresentationDraft(const Song &original, const Song &draft,
    const QVector<int> &sourceIndices, QString *error) {
    clearError(error);
    if (original.id <= 0 || draft.id != original.id || draft.title != original.title
        || draft.slides.isEmpty() || draft.slides.size() > 10000 || sourceIndices.size() != draft.slides.size()
        || draft.richText.size() != draft.slides.size() || !validRichText(draft.slides, draft.richText)
        || !draft.appearance.validate(int(draft.slides.size())))
        return fail(QStringLiteral("The Look/reflow draft has invalid source pages or styling."), error);
    QVector<QVector<int>> replacements(original.slides.size());
    QStringList reconstructed;
    reconstructed.resize(original.slides.size());
    int previous = -1;
    for (int i = 0; i < sourceIndices.size(); ++i) {
        const int source = sourceIndices.at(i);
        if (source < 0 || source >= original.slides.size() || source < previous
            || draft.media.value(i) != original.media.value(source))
            return fail(QStringLiteral("Reflow must preserve source order and media references."), error);
        previous = source;
        replacements[source].append(i);
        reconstructed[source].append(draft.slides.at(i));
    }
    for (int i = 0; i < original.slides.size(); ++i)
        if (replacements.at(i).isEmpty() || reconstructed.at(i) != original.slides.at(i))
            return fail(QStringLiteral("Reflow must preserve every source character and page in reading order."), error);
    SongStructure expected = original.structure;
    int sourceStart = 0;
    for (auto &section : expected.sections) {
        const int oldCount = section.slideCount;
        QVector<int> counts;
        for (int offset = 0; offset < oldCount; ++offset) counts.append(int(replacements.at(sourceStart + offset).size()));
        section.reflow(counts);
        sourceStart += oldCount;
    }
    QString validation;
    if (expected.toJson() != draft.structure.toJson() || !draft.structure.validate(int(draft.slides.size()), &validation))
        return fail(QStringLiteral("Reflow must retain song section and arrangement associations. %1").arg(validation), error);

    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    const auto current = song(original.id);
    if (!lastError_.isEmpty()) return fail(lastError_, error);
    if (!current.id || current.title != original.title || current.slides != original.slides
        || current.richText != original.richText || current.appearance != original.appearance
        || current.structure.toJson() != original.structure.toJson() || current.media != original.media)
        return fail(QStringLiteral("This presentation changed while editing. Reopen Looks / Reflow before saving."), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,slide_order FROM service_entries WHERE song_id=? AND slide_order IS NOT NULL"));
    query.addBindValue(original.id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    QVector<QPair<int, QVector<int>>> orders;
    while (query.next()) {
        QVector<int> oldOrder, newOrder;
        if (!decodeOrder(query.value(1), &oldOrder)) return fail(QStringLiteral("A service arrangement is invalid."), error);
        for (int index : oldOrder) {
            if (index < 0 || index >= replacements.size()) return fail(QStringLiteral("A service references a missing source slide."), error);
            newOrder.append(replacements.at(index));
            if (newOrder.size() > 10000) return fail(QStringLiteral("Reflow would exceed the 10000-slide service arrangement limit."), error);
        }
        orders.append({query.value(0).toInt(), newOrder});
    }
    query.finish();
    query.prepare(QStringLiteral("SELECT id FROM slides WHERE song_id=? ORDER BY position,id"));
    query.addBindValue(original.id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    QVector<int> ids;
    while (query.next()) ids.append(query.value(0).toInt());
    query.finish();
    for (int i = 0; i < draft.slides.size(); ++i) {
        const int source = sourceIndices.at(i);
        const bool existing = replacements.at(source).first() == i;
        query.prepare(existing
            ? QStringLiteral("UPDATE slides SET position=?,text=?,rich_text=?,look_id=?,look_rich_override=?,look_fit=? WHERE id=? AND song_id=?")
            : QStringLiteral("INSERT INTO slides(position,text,rich_text,look_id,look_rich_override,look_fit,song_id,media_kind,media_path,media_name,media_loop) VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(i);
        query.addBindValue(draft.slides.at(i).isNull() ? QStringLiteral("") : draft.slides.at(i));
        query.addBindValue(draft.richText.at(i).isNull() ? QStringLiteral("") : draft.richText.at(i));
        query.addBindValue(draft.appearance.slideLookIds.value(i) > 0 ? QVariant(draft.appearance.slideLookIds.at(i)) : QVariant());
        query.addBindValue(draft.appearance.richTextOverrides.value(i));
        query.addBindValue(draft.appearance.fitText.value(i));
        if (existing) {
            query.addBindValue(ids.at(source));
            query.addBindValue(original.id);
        } else {
            const auto media = draft.media.value(i);
            query.addBindValue(original.id);
            query.addBindValue(storedMediaKind(media.kind));
            query.addBindValue(media.path.isNull() ? QStringLiteral("") : media.path);
            query.addBindValue(media.name.isNull() ? QStringLiteral("") : media.name);
            query.addBindValue(media.loop);
        }
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    query.prepare(QStringLiteral("UPDATE service_entries SET slide_order=? WHERE id=? AND song_id=?"));
    for (const auto &entry : orders) {
        query.bindValue(0, entry.second.isEmpty() ? QVariant() : QVariant(encodedOrder(entry.second)));
        query.bindValue(1, entry.first);
        query.bindValue(2, original.id);
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    query.prepare(QStringLiteral("UPDATE songs SET look_id=?,song_structure=? WHERE id=?"));
    query.addBindValue(draft.appearance.lookId > 0 ? QVariant(draft.appearance.lookId) : QVariant());
    query.addBindValue(draft.structure.toJson());
    query.addBindValue(original.id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return true;
}

QVector<MediaAsset> LibraryRepository::mediaAssets(const QString &search) const {
    clearError();
    QVector<MediaAsset> result;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id FROM media_assets WHERE NOT EXISTS(SELECT 1 FROM content_usage u WHERE u.asset_id=media_assets.id AND u.archived_batch IS NOT NULL) AND (instr(lower(name),lower(?))>0 "
                                 "OR instr(lower(folder),lower(?))>0) ORDER BY folder COLLATE NOCASE,name COLLATE NOCASE,id"));
    query.addBindValue(search.isNull() ? QStringLiteral("") : search);
    query.addBindValue(search.isNull() ? QStringLiteral("") : search);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    while (query.next()) {
        const auto asset = mediaAsset(query.value(0).toInt());
        if (!lastError_.isEmpty()) return {};
        result.append(asset);
    }
    return result;
}

MediaAsset LibraryRepository::mediaAsset(int id) const {
    clearError();
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,name,kind,path,folder,loop,imported_at FROM media_assets WHERE id=? AND NOT EXISTS(SELECT 1 FROM content_usage u WHERE u.asset_id=media_assets.id AND u.archived_batch IS NOT NULL)"));
    query.addBindValue(id);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    if (!query.next()) return {};
    MediaAsset asset;
    asset.id = query.value(0).toInt();
    asset.media.name = query.value(1).toString();
    const QString kind = query.value(2).toString();
    asset.media.kind = kind == QStringLiteral("image") ? MediaKind::Image
        : kind == QStringLiteral("video") ? MediaKind::Video
        : kind == QStringLiteral("audio") ? MediaKind::Audio : MediaKind::None;
    asset.media.path = query.value(3).toString();
    asset.folder = query.value(4).toString();
    asset.media.loop = query.value(5).toBool();
    asset.importedAt = query.value(6).toLongLong();
    if (!validAsset(asset.media)) {
        fail(QStringLiteral("A Media Library item contains invalid metadata."));
        return {};
    }
    return asset;
}

int LibraryRepository::registerMediaAsset(const SlideMedia &media, QString *error) {
    if (!validAsset(media)) {
        fail(QStringLiteral("Media needs a supported type, a managed relative path, and a valid name. Only audio and video can loop."), error);
        return 0;
    }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,kind,(SELECT archived_batch FROM content_usage u WHERE u.asset_id=media_assets.id) FROM media_assets WHERE path=?"));
    query.addBindValue(media.path);
    if (!query.exec()) { fail(query.lastError().text(), error); return 0; }
    if (query.next()) {
        if (!query.value(2).isNull()) { fail(QStringLiteral("This media asset is in Cleanup Recovery. Restore it before reusing this path."), error); return 0; }
        if (query.value(1).toString() != storedMediaKind(media.kind)) {
            fail(QStringLiteral("This managed path is already registered with a different media type."), error);
            return 0;
        }
        return query.value(0).toInt();
    }
    query.finish();
    query.prepare(QStringLiteral("INSERT INTO media_assets(name,kind,path,loop,imported_at) VALUES(?,?,?,?,?)"));
    query.addBindValue(media.name.trimmed());
    query.addBindValue(storedMediaKind(media.kind));
    query.addBindValue(media.path);
    query.addBindValue(media.loop);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec()) { fail(query.lastError().text(), error); return 0; }
    return query.lastInsertId().toInt();
}

QVector<int> LibraryRepository::registerMediaAssets(const QVector<SlideMedia> &media, QString *error) {
    clearError(error);
    if (media.isEmpty() || media.size() > 10000) {
        fail(QStringLiteral("Import between 1 and 10000 media items per batch."), error);
        return {};
    }
    Transaction transaction(db_);
    if (!transaction.active()) { fail(db_.lastError().text(), error); return {}; }
    QVector<int> ids;
    for (const auto &item : media) {
        const int id = registerMediaAsset(item, error);
        if (!id) return {};
        ids.append(id);
    }
    if (!transaction.commit()) { fail(db_.lastError().text(), error); return {}; }
    return ids;
}

bool LibraryRepository::updateMediaAsset(int id, const QString &name, const QString &folder,
                                        bool loop, QString *error) {
    clearError(error);
    auto asset = mediaAsset(id);
    if (!lastError_.isEmpty()) return fail(lastError_, error);
    if (!asset.id) return fail(QStringLiteral("This media item no longer exists."), error);
    asset.media.name = name.trimmed();
    asset.media.loop = loop;
    if (!validAsset(asset.media) || folder.size() > 256 || folder.contains(QChar::Null)
        || folder.contains(QLatin1Char('\n')) || folder.contains(QLatin1Char('\r')))
        return fail(QStringLiteral("Use a valid media name and a folder label of up to 256 characters. Only audio and video can loop."), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE media_assets SET name=?,folder=?,loop=? WHERE id=?"));
    query.addBindValue(asset.media.name);
    query.addBindValue(folder.trimmed());
    query.addBindValue(loop);
    query.addBindValue(id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.numRowsAffected()) return fail(QStringLiteral("This media item no longer exists."), error);
    return true;
}

bool LibraryRepository::deleteMediaAsset(int id, QString *error) {
    clearError(error);
    // One statement protects against a concurrently created presentation or playlist reference.
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM media_assets WHERE id=? AND NOT EXISTS "
        "(SELECT 1 FROM slides WHERE media_path=media_assets.path AND media_kind<>'') AND NOT EXISTS "
        "(SELECT 1 FROM service_entries WHERE media_asset_id=media_assets.id)"));
    query.addBindValue(id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.numRowsAffected())
        return fail(QStringLiteral("This media item is referenced by a presentation or saved service, or was already removed. Remove those references before deleting it."), error);
    return true;
}

int LibraryRepository::appendMediaToService(int serviceId, int mediaAssetId, QString *error, int beforeEntryId) {
    clearError(error);
    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    if (!exists(QStringLiteral("services"), serviceId, error)
        || !exists(QStringLiteral("media_assets"), mediaAssetId, error)) return false;
    auto entries = service(serviceId);
    if (!lastError_.isEmpty()) return fail(lastError_, error);
    int position = entries.size();
    if (beforeEntryId) {
        position = -1;
        for (int i = 0; i < entries.size(); ++i)
            if (entries.at(i).id == beforeEntryId) position = i;
        if (position < 0) return fail(QStringLiteral("The service changed during the drop. Try again."), error);
    }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE service_entries SET position=? WHERE id=? AND service_id=?"));
    for (int i = 0; i < entries.size(); ++i) {
        query.bindValue(0, i >= position ? i + 1 : i);
        query.bindValue(1, entries.at(i).id);
        query.bindValue(2, serviceId);
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    query.prepare(QStringLiteral("INSERT INTO service_entries(service_id,media_asset_id,position) VALUES(?,?,?)"));
    query.addBindValue(serviceId);
    query.addBindValue(mediaAssetId);
    query.addBindValue(position);
    if (!query.exec()) return fail(query.lastError().text(), error);
    const int entryId = query.lastInsertId().toInt();
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return entryId;
}

QVector<MidiOutputAction> LibraryRepository::midiActions(MidiOwnerType type,qint64 id) const {
    clearError();QVector<MidiOutputAction> result;const auto column=midiOwnerColumn(type);
    if(column.isEmpty()||id<=0){fail("Choose a valid slide or media cue.");return {};}
    QSqlQuery query(db_);query.prepare("SELECT payload FROM midi_slide_actions WHERE "+column+"=? ORDER BY position,id LIMIT 65");query.addBindValue(id);
    if(!query.exec()){fail(query.lastError().text());return {};}
    while(query.next()){
        const auto payload=query.value(0).toString();MidiOutputAction action;
        if(payload.size()>65536||result.size()>=64||!MidiOutputAction::fromJson(QJsonDocument::fromJson(payload.toUtf8()).object(),&action)){fail("Invalid saved MIDI action data.");return {};}
        result.append(action);
    }
    return result;
}
bool LibraryRepository::setMidiActions(MidiOwnerType type,qint64 id,const QVector<MidiOutputAction> &original,const QVector<MidiOutputAction> &actions,QString *error) {
    clearError(error);const auto column=midiOwnerColumn(type);
    if(column.isEmpty()||id<=0||actions.size()>64)return fail("Choose a valid cue and at most 64 MIDI output actions.",error);
    for(const auto &action:actions)if(!action.valid())return fail("Use a destination, channel 1–16, number/value 0–127, and positive Note On velocity.",error);
    Transaction transaction(db_);if(!transaction.active())return fail(db_.lastError().text(),error);
    QSqlQuery query(db_);query.prepare("SELECT 1 FROM "+midiOwnerTable(type)+" WHERE id=?"+(type==MidiOwnerType::Entry?QStringLiteral(" AND media_asset_id IS NOT NULL"):QString()));query.addBindValue(id);
    if(!query.exec())return fail(query.lastError().text(),error);
    if(!query.next())return fail("This slide or media cue no longer exists.",error);query.finish();
    const auto current=midiActions(type,id);if(!lastError_.isEmpty())return fail(lastError_,error);
    if(current!=original)return fail("MIDI actions changed while editing. Reopen the action editor before saving.",error);
    query.prepare("DELETE FROM midi_slide_actions WHERE "+column+"=?");query.addBindValue(id);if(!query.exec())return fail(query.lastError().text(),error);
    query.prepare("INSERT INTO midi_slide_actions("+column+",position,payload) VALUES(?,?,?)");
    for(int i=0;i<actions.size();++i){query.bindValue(0,id);query.bindValue(1,i);query.bindValue(2,QString::fromUtf8(QJsonDocument(actions.at(i).toJson()).toJson(QJsonDocument::Compact)));if(!query.exec())return fail(query.lastError().text(),error);}
    if(!transaction.commit())return fail(db_.lastError().text(),error);return true;
}
QVector<MidiBinding> LibraryRepository::midiBindings() const {
    clearError();QVector<MidiBinding> result;QSqlQuery query(db_);
    if(!query.exec("SELECT id,payload,song_id,slide_id,entry_id,asset_id FROM midi_bindings ORDER BY id LIMIT 2001")){fail(query.lastError().text());return {};}
    while(query.next()){
        const auto payload=query.value(1).toString();MidiBinding binding;
        if(payload.size()>8192||result.size()>=2000||!MidiBinding::fromJson(QJsonDocument::fromJson(payload.toUtf8()).object(),&binding)
            ||binding.songId!=query.value(2).toInt()||binding.slideId!=query.value(3).toLongLong()||binding.entryId!=query.value(4).toInt()||binding.assetId!=query.value(5).toInt()){
            fail("A saved MIDI mapping is malformed. Input mapping control has been disabled.");return {};
        }
        binding.id=query.value(0).toLongLong();result.append(binding);
    }
    QString error;if(!validateMidiBindings(result,&error)){fail(error);return {};}
    return result;
}
MidiBinding LibraryRepository::midiBinding(qint64 id) const {
    clearError();QSqlQuery query(db_);query.prepare("SELECT payload FROM midi_bindings WHERE id=?");query.addBindValue(id);
    if(!query.exec()){fail(query.lastError().text());return {};}
    if(!query.next())return {};
    const auto payload=query.value(0).toString();MidiBinding binding;
    if(payload.size()>8192||!MidiBinding::fromJson(QJsonDocument::fromJson(payload.toUtf8()).object(),&binding)){fail("Invalid MIDI mapping data.");return {};}
    binding.id=id;return binding;
}
int LibraryRepository::entryServiceId(int entryId) const {
    clearError();QSqlQuery query(db_);query.prepare("SELECT service_id FROM service_entries WHERE id=?");query.addBindValue(entryId);
    if(!query.exec()){fail(query.lastError().text());return 0;}return query.next()?query.value(0).toInt():0;
}
bool LibraryRepository::validateMidiTarget(const MidiBinding &binding,QString *error) const {
    if(!binding.valid())return fail("Choose a valid mapping name, trigger, command, and target.",error);
    if(binding.command!=MidiCommand::TakeSlide&&binding.command!=MidiCommand::TakePresentation&&binding.command!=MidiCommand::SelectPresentation)return true;
    Song source;
    if(binding.entryId){source=entrySource(binding.entryId);if(!lastError_.isEmpty())return fail(lastError_,error);
        if((binding.songId&&source.id!=binding.songId)||(binding.assetId&&source.mediaAssetId!=binding.assetId))return fail("This service item no longer matches the MIDI target.",error);
    }else if(binding.songId){source=song(binding.songId);if(!lastError_.isEmpty())return fail(lastError_,error);}
    else {const auto asset=mediaAsset(binding.assetId);if(!asset.id)return fail("This media asset no longer exists.",error);source.mediaAssetId=asset.id;source.slides={QStringLiteral("")};}
    if(!source.id&&!source.mediaAssetId)return fail("This MIDI target no longer exists.",error);
    if(binding.command!=MidiCommand::TakeSlide)return true;
    if(binding.assetId)return binding.occurrence==0?true:fail("A standalone media cue has one slide.",error);
    const int page=source.slideIds.indexOf(binding.slideId);if(page<0)return fail("This canonical slide no longer belongs to the selected presentation.",error);
    QVector<int> order;
    if(binding.entryId){QSqlQuery query(db_);query.prepare("SELECT slide_order FROM service_entries WHERE id=?");query.addBindValue(binding.entryId);
        if(!query.exec()||!query.next())return fail("This service item no longer exists.",error);
        if(!decodeOrder(query.value(0),&order))return fail("This service arrangement is invalid.",error);}
    if(order.isEmpty())order=source.playbackOrder();
    if(order.count(page)<=binding.occurrence)return fail("The selected occurrence is not in this saved arrangement. Choose an existing occurrence or update the service order.",error);
    return true;
}
bool LibraryRepository::saveMidiBinding(MidiBinding binding,QString *error,const MidiBinding *original) {
    clearError(error);binding.name=binding.name.trimmed();
    Transaction transaction(db_);if(!transaction.active())return fail(db_.lastError().text(),error);
    if(!validateMidiTarget(binding,error))return false;
    auto all=midiBindings();if(!lastError_.isEmpty())return fail(lastError_,error);
    bool found=!binding.id;
    for(int i=0;i<all.size();++i)if(all.at(i).id==binding.id&&binding.id){
        if(original&&all.at(i).toJson()!=original->toJson())return fail("This MIDI mapping changed while editing. Reopen it before saving.",error);
        all.removeAt(i);found=true;break;}
    if(!found)return fail("This MIDI mapping was removed while editing.",error);
    all.append(binding);QString validation;if(!validateMidiBindings(all,&validation))return fail(validation,error);
    QSqlQuery query(db_);query.prepare(binding.id?"UPDATE midi_bindings SET payload=?,song_id=?,slide_id=?,entry_id=?,asset_id=? WHERE id=?":"INSERT INTO midi_bindings(payload,song_id,slide_id,entry_id,asset_id) VALUES(?,?,?,?,?)");
    query.addBindValue(QString::fromUtf8(QJsonDocument(binding.toJson()).toJson(QJsonDocument::Compact)));
    query.addBindValue(binding.songId?QVariant(binding.songId):QVariant());query.addBindValue(binding.slideId?QVariant(binding.slideId):QVariant());
    query.addBindValue(binding.entryId?QVariant(binding.entryId):QVariant());query.addBindValue(binding.assetId?QVariant(binding.assetId):QVariant());if(binding.id)query.addBindValue(binding.id);
    if(!query.exec())return fail(query.lastError().text(),error);if(!transaction.commit())return fail(db_.lastError().text(),error);return true;
}
bool LibraryRepository::deleteMidiBinding(qint64 id,QString *error) {
    clearError(error);QSqlQuery query(db_);query.prepare("DELETE FROM midi_bindings WHERE id=?");query.addBindValue(id);
    if(!query.exec())return fail(query.lastError().text(),error);if(!query.numRowsAffected())return fail("This MIDI mapping no longer exists.",error);return true;
}
bool LibraryRepository::copyEntryMidiActions(int source,int target,QString *error){
    QSqlQuery query(db_);query.prepare("INSERT INTO midi_slide_actions(entry_id,position,payload) SELECT ?,position,payload FROM midi_slide_actions WHERE entry_id=? ORDER BY position,id");query.addBindValue(target);query.addBindValue(source);return query.exec()?true:fail(query.lastError().text(),error);
}

QVector<ServicePlan> LibraryRepository::services() const {
    clearError();
    QVector<ServicePlan> result;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT id,name FROM services ORDER BY name COLLATE NOCASE,id"))) {
        fail(query.lastError().text());
        return {};
    }
    while (query.next()) result.append({query.value(0).toInt(), query.value(1).toString()});
    return result;
}

int LibraryRepository::createService(const QString &name, QString *error) {
    clearError(error);
    if (name.trimmed().isEmpty()) { fail(QStringLiteral("A service must have a name."), error); return 0; }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO services(name) VALUES(?)"));
    query.addBindValue(name.trimmed());
    if (!query.exec()) { fail(query.lastError().text(), error); return 0; }
    return query.lastInsertId().toInt();
}

int LibraryRepository::createServiceOrder(const QString &name, const QVector<ServiceOrderItem> &items,
                                          QString *error) {
    clearError(error);
    const auto reject = [this, error](const QString &message) {
        fail(message, error);
        return 0;
    };
    if (name.trimmed().isEmpty() || name.size() > 256 || name.contains(QChar::Null))
        return reject(QStringLiteral("A service needs a name of 1 to 256 characters without null characters."));
    if (items.isEmpty() || items.size() > 500)
        return reject(QStringLiteral("A service order must contain 1 to 500 items."));

    qsizetype totalText = 0;
    qsizetype totalSlides = 0;
    qsizetype totalOrder = 0;
    for (const auto &item : items) {
        if (item.songId < 0)
            return reject(QStringLiteral("An existing presentation ID must be a positive integer."));
        if (item.songId > 0) {
            if (!item.title.isEmpty() || !item.slides.isEmpty())
                return reject(QStringLiteral("Use an existing presentation ID or a new title and slides, not both."));
        } else {
            if (item.title.trimmed().isEmpty() || item.title.size() > 256 || item.title.contains(QChar::Null))
                return reject(QStringLiteral("A new presentation needs a title of 1 to 256 characters without null characters."));
            if (item.slides.isEmpty() || item.slides.size() > 1000)
                return reject(QStringLiteral("A new presentation must contain 1 to 1000 text slides."));
            totalSlides += item.slides.size();
            if (totalSlides > 5000)
                return reject(QStringLiteral("A service order can create at most 5000 text slides."));
            for (const auto &text : item.slides) {
                if (text.trimmed().isEmpty() || text.size() > 64 * 1024 || text.contains(QChar::Null))
                    return reject(QStringLiteral("Each new slide needs nonblank text of at most 64 Ki characters without null characters."));
                totalText += text.size();
                if (totalText > 4 * 1024 * 1024)
                    return reject(QStringLiteral("A service order can create at most 4 Mi characters of slide text."));
            }
        }
        totalOrder += item.slideOrder.size();
        if (item.slideOrder.size() > 10000 || totalOrder > 50000)
            return reject(QStringLiteral("Slide arrangements are limited to 10000 indices per item and 50000 per service order."));
    }

    Transaction transaction(db_);
    if (!transaction.active()) return reject(db_.lastError().text());
    // Validate every reference inside the transaction, including arrangements,
    // before creating the service or any new library presentations.
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT COUNT(slides.id) FROM songs LEFT JOIN slides ON slides.song_id=songs.id "
                                 "WHERE songs.id=? GROUP BY songs.id"));
    for (const auto &item : items) {
        int count = int(item.slides.size());
        if (item.songId > 0) {
            query.bindValue(0, item.songId);
            if (!query.exec()) return reject(query.lastError().text());
            if (!query.next() || query.value(0).toInt() < 1)
                return reject(QStringLiteral("Presentation %1 no longer exists or has no slides.").arg(item.songId));
            count = query.value(0).toInt();
            query.finish();
        }
        for (int index : item.slideOrder)
            if (index < 0 || index >= count)
                return reject(QStringLiteral("The arrangement contains an invalid source slide."));
    }
    query.prepare(QStringLiteral("INSERT INTO services(name) VALUES(?)"));
    query.addBindValue(name.trimmed());
    if (!query.exec()) return reject(query.lastError().text());
    const int serviceId = query.lastInsertId().toInt();
    for (int position = 0; position < items.size(); ++position) {
        const auto &item = items.at(position);
        int songId = item.songId;
        if (songId == 0) {
            query.prepare(QStringLiteral("INSERT INTO songs(title) VALUES(?)"));
            query.addBindValue(item.title.trimmed());
            if (!query.exec()) return reject(query.lastError().text());
            songId = query.lastInsertId().toInt();
            if (!storeSlides(songId, item.slides, error)) return 0;
        }
        query.prepare(QStringLiteral("INSERT INTO service_entries(service_id,song_id,position,slide_order) VALUES(?,?,?,?)"));
        query.addBindValue(serviceId);
        query.addBindValue(songId);
        query.addBindValue(position);
        query.addBindValue(item.slideOrder.isEmpty() ? QVariant() : QVariant(encodedOrder(item.slideOrder)));
        if (!query.exec()) return reject(query.lastError().text());
    }
    if (!transaction.commit()) return reject(db_.lastError().text());
    return serviceId;
}

bool LibraryRepository::renameService(int id, const QString &name, QString *error) {
    clearError(error);
    if (name.trimmed().isEmpty()) return fail(QStringLiteral("A service must have a name."), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE services SET name=? WHERE id=?"));
    query.addBindValue(name.trimmed());
    query.addBindValue(id);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.numRowsAffected()) return fail(QStringLiteral("This service no longer exists."), error);
    return true;
}

int LibraryRepository::duplicateService(int id, const QString &name, QString *error) {
    clearError(error);
    if (name.trimmed().isEmpty()) { fail(QStringLiteral("A service must have a name."), error); return 0; }
    Transaction transaction(db_);
    if (!transaction.active()) { fail(db_.lastError().text(), error); return 0; }
    if (!exists(QStringLiteral("services"), id, error)) return 0;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO services(name) VALUES(?)"));
    query.addBindValue(name.trimmed());
    if (!query.exec()) { fail(query.lastError().text(), error); return 0; }
    const int newId = query.lastInsertId().toInt();
    query.prepare(QStringLiteral("INSERT INTO service_entries(service_id,song_id,position,slide_order,media_asset_id,media_transition_type,media_transition_duration_ms,media_transition_override) "
                                 "SELECT ?,song_id,position,slide_order,media_asset_id,media_transition_type,media_transition_duration_ms,media_transition_override FROM service_entries WHERE service_id=? ORDER BY position,id"));
    query.addBindValue(newId);
    query.addBindValue(id);
    if (!query.exec()) { fail(query.lastError().text(), error); return 0; }
    const auto originals=service(id);if(!lastError_.isEmpty()){fail(lastError_,error);return 0;}
    const auto copies=service(newId);if(!lastError_.isEmpty()){fail(lastError_,error);return 0;}
    for(int i=0;i<originals.size();++i)if(!copyEntryMidiActions(originals.at(i).id,copies.at(i).id,error))return 0;
    if (!transaction.commit()) { fail(db_.lastError().text(), error); return 0; }
    return newId;
}

QVector<ServiceEntry> LibraryRepository::service(int serviceId) const {
    clearError();
    QVector<ServiceEntry> result;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,song_id,slide_order,media_asset_id FROM service_entries WHERE service_id=? ORDER BY position,id"));
    query.addBindValue(serviceId);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    while (query.next()) {
        ServiceEntry entry{query.value(0).toInt(), query.value(1).toInt(), {}};
        if (!decodeOrder(query.value(2), &entry.slideOrder)) {
            fail(QStringLiteral("A service item contains an invalid slide arrangement."));
            return {};
        }
        entry.mediaAssetId = query.value(3).toInt();
        result.append(entry);
    }
    return result;
}

bool LibraryRepository::appendToService(int serviceId, int songId, QString *error) {
    clearError(error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO service_entries(service_id,song_id,position) "
                                 "VALUES(?,?,(SELECT COALESCE(MAX(position),-1)+1 FROM service_entries WHERE service_id=?))"));
    query.addBindValue(serviceId);
    query.addBindValue(songId);
    query.addBindValue(serviceId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    return true;
}

bool LibraryRepository::removeFromService(int entryId, QString *error) {
    clearError(error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM service_entries WHERE id=?"));
    query.addBindValue(entryId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.numRowsAffected()) return fail(QStringLiteral("This service item no longer exists."), error);
    return true;
}

bool LibraryRepository::moveServiceEntry(int serviceId, int from, int to, QString *error) {
    clearError(error);
    auto entries = service(serviceId);
    if (!lastError_.isEmpty()) return fail(lastError_, error);
    if (from < 0 || to < 0 || from >= entries.size() || to >= entries.size())
        return fail(QStringLiteral("The service item move is outside the service order."), error);
    entries.move(from, to);
    QVector<int> ids;
    for (const auto &entry : entries) ids.append(entry.id);
    return reorderServiceEntries(serviceId, ids, error);
}

bool LibraryRepository::reorderServiceEntries(int serviceId, const QVector<int> &ids, QString *error) {
    clearError(error);
    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    if (!exists(QStringLiteral("services"), serviceId, error)) return false;
    const auto entries = service(serviceId);
    if (!lastError_.isEmpty()) return fail(lastError_, error);
    QSet<int> expected;
    for (const auto &entry : entries) expected.insert(entry.id);
    const QSet<int> requested(ids.cbegin(), ids.cend());
    if (ids.size() != entries.size() || requested.size() != ids.size() || requested != expected)
        return fail(QStringLiteral("A reordered service must contain each existing item exactly once."), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE service_entries SET position=? WHERE id=? AND service_id=?"));
    for (int i = 0; i < ids.size(); ++i) {
        query.bindValue(0, i);
        query.bindValue(1, ids.at(i));
        query.bindValue(2, serviceId);
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return true;
}

bool LibraryRepository::duplicateServiceEntry(int entryId, QString *error) {
    clearError(error);
    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT service_id FROM service_entries WHERE id=?"));
    query.addBindValue(entryId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.next()) return fail(QStringLiteral("This service item no longer exists."), error);
    const int serviceId = query.value(0).toInt();
    query.finish();
    const auto entries = service(serviceId);
    if (!lastError_.isEmpty()) return fail(lastError_, error);
    int selectedRow = -1;
    for (int i = 0; i < entries.size(); ++i)
        if (entries.at(i).id == entryId) selectedRow = i;
    if (selectedRow < 0) return fail(QStringLiteral("This service item no longer exists."), error);
    // Normalize old/gapped positions so a copy is always immediately after its source,
    // even when a migrated service contained tied positions.
    query.prepare(QStringLiteral("UPDATE service_entries SET position=? WHERE id=?"));
    for (int i = 0; i < entries.size(); ++i) {
        query.bindValue(0, i > selectedRow ? i + 1 : i);
        query.bindValue(1, entries.at(i).id);
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    query.prepare(QStringLiteral("INSERT INTO service_entries(service_id,song_id,position,slide_order,media_asset_id,media_transition_type,media_transition_duration_ms,media_transition_override) "
                                 "SELECT service_id,song_id,position+1,slide_order,media_asset_id,media_transition_type,media_transition_duration_ms,media_transition_override FROM service_entries WHERE id=?"));
    query.addBindValue(entryId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    const int duplicateId=query.lastInsertId().toInt();
    if(!copyEntryMidiActions(entryId,duplicateId,error))return false;
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return true;
}

Song LibraryRepository::entrySource(int entryId) const {
    clearError();
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT song_id,media_asset_id,media_transition_type,media_transition_duration_ms,media_transition_override FROM service_entries WHERE id=?"));
    query.addBindValue(entryId);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    if (!query.next()) return {};
    if (!query.value(0).isNull()) return song(query.value(0).toInt());
    const auto asset = mediaAsset(query.value(1).toInt());
    if (!asset.id) return {};
    Song source;
    source.title = asset.media.name;
    source.slides = {QStringLiteral("")};
    source.richText = {QStringLiteral("")};
    source.media = {asset.media};
    source.mediaAssetId = asset.id;
    source.slideIds = {0};
    auto actions = midiActions(MidiOwnerType::Asset, asset.id);
    if(!lastError_.isEmpty())return {};
    actions.append(midiActions(MidiOwnerType::Entry, entryId));
    if(!lastError_.isEmpty())return {};
    source.midiActions = {actions};
    source.settings.transition = SlideTransitionType(query.value(2).toInt());
    source.settings.transitionDurationMs = query.value(3).toInt();
    source.settings.overrideTransition = query.value(4).toBool();
    return source;
}

Song LibraryRepository::entryContent(int entryId) const {
    const Song source = entrySource(entryId);
    if (!lastError_.isEmpty()) return {};
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT slide_order FROM service_entries WHERE id=?"));
    query.addBindValue(entryId);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    if (!query.next()) return {};
    QVector<int> order;
    if (!decodeOrder(query.value(0), &order)) {
        fail(QStringLiteral("A service item contains an invalid slide arrangement."));
        return {};
    }
    if (order.isEmpty()) order = source.playbackOrder();
    Song arranged = source;
    arranged.slides.clear();
    arranged.richText.clear();
    arranged.media.clear();
    arranged.looks.clear();
    for (int index : order) {
        if (index < 0 || index >= source.slides.size()) {
            fail(QStringLiteral("A service item's slide arrangement references a missing slide."));
            return {};
        }
        arranged.slides.append(source.slides.at(index));
        arranged.richText.append(source.richText.value(index));
        arranged.media.append(source.media.value(index));
        arranged.looks.append(source.looks.value(index));
    }
    return arranged;
}

QStringList LibraryRepository::entrySlides(int entryId) const { return entryContent(entryId).slides; }
QStringList LibraryRepository::entryRichText(int entryId) const { return entryContent(entryId).richText; }
QVector<SlideMedia> LibraryRepository::entryMedia(int entryId) const { return entryContent(entryId).media; }
QVector<SlideLook> LibraryRepository::entryLooks(int entryId) const { return entryContent(entryId).looks; }

bool LibraryRepository::setEntrySlideOrder(int entryId, const QVector<int> &indices, QString *error) {
    clearError(error);
    const auto source = entrySource(entryId);
    if (!lastError_.isEmpty()) return fail(lastError_, error);
    if (source.mediaAssetId) {
        if (!indices.isEmpty() && indices != QVector<int>{0})
            return fail(QStringLiteral("A standalone media item has one cue. Duplicate the service item to repeat it."), error);
        return true;
    }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT COUNT(slides.id) FROM service_entries "
        "JOIN slides ON slides.song_id=service_entries.song_id WHERE service_entries.id=? GROUP BY service_entries.id"));
    query.addBindValue(entryId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!query.next()) return fail(QStringLiteral("This service item no longer exists or has no slides."), error);
    const int count = query.value(0).toInt();
    for (int index : indices)
        if (index < 0 || index >= count) return fail(QStringLiteral("The arrangement contains an invalid source slide."), error);
    query.finish();
    query.prepare(QStringLiteral("UPDATE service_entries SET slide_order=? WHERE id=?"));
    query.addBindValue(indices.isEmpty() ? QVariant() : QVariant(encodedOrder(indices)));
    query.addBindValue(entryId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    return true;
}
