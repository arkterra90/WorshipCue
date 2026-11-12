#include "LibraryRepository.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

namespace {
constexpr int schemaVersion = 5;

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

bool validBackgroundPath(const QString &path) {
    return path.isEmpty() || (!QDir::isAbsolutePath(path)
        && path.startsWith(QStringLiteral("backgrounds/"))
        && path.size() > QStringLiteral("backgrounds/").size()
        && !path.endsWith(QLatin1Char('/'))
        && QDir::cleanPath(path) == path
        && !path.contains(QLatin1Char('\\')) && !path.contains(QChar::Null));
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
        !execute(QStringLiteral("PRAGMA busy_timeout=3000"), error)) return false;

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
    return true;
}

QString LibraryRepository::databaseDirectory() const {
    const QString path = db_.databaseName();
    return path.isEmpty() || path == QStringLiteral(":memory:") ? QString() : QFileInfo(path).absolutePath();
}

QVector<Song> LibraryRepository::songs(const QString &search) const {
    clearError();
    QVector<Song> result;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id FROM songs WHERE title LIKE ? ESCAPE '\\' OR EXISTS "
        "(SELECT 1 FROM slides WHERE song_id=songs.id AND (text LIKE ? ESCAPE '\\' OR media_name LIKE ? ESCAPE '\\')) ORDER BY title COLLATE NOCASE,id"));
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
    query.prepare(QStringLiteral("SELECT id,title,auto_advance_ms,presentation_loop,transition_type,transition_duration_ms,transition_override,background_path "
                                 "FROM songs WHERE id=?"));
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
    slides.prepare(QStringLiteral("SELECT text,media_kind,media_path,media_name,media_loop,rich_text FROM slides WHERE song_id=? ORDER BY position,id"));
    slides.addBindValue(id);
    if (!slides.exec()) { fail(slides.lastError().text()); return {}; }
    while (slides.next()) {
        result.slides << slides.value(0).toString();
        result.richText << slides.value(5).toString();
        const QString kind = slides.value(1).toString();
        if (!kind.isEmpty() && kind != QStringLiteral("image") && kind != QStringLiteral("video")) {
            fail(QStringLiteral("A presentation contains an unknown media type."));
            return {};
        }
        result.media.append({kind == QStringLiteral("image") ? MediaKind::Image
                            : kind == QStringLiteral("video") ? MediaKind::Video : MediaKind::None,
                             slides.value(2).toString(), slides.value(3).toString(), slides.value(4).toBool()});
    }
    if (!validRichText(result.slides, result.richText)) {
        fail(QStringLiteral("A presentation contains invalid or oversized rich text."));
        return {};
    }
    return result;
}

bool LibraryRepository::storeSlides(int songId, const QStringList &slides, QString *error,
                                     const QStringList &richText) {
    QSqlQuery query(db_);
    // The text-only form is also used before rich_text exists during version-0 seeding.
    query.prepare(richText.isEmpty()
        ? QStringLiteral("INSERT INTO slides(song_id,position,text) VALUES(?,?,?)")
        : QStringLiteral("INSERT INTO slides(song_id,position,text,rich_text) VALUES(?,?,?,?)"));
    for (int i = 0; i < slides.size(); ++i) {
        query.bindValue(0, songId);
        query.bindValue(1, i);
        const QString text = richText.value(i).isEmpty() ? slides.at(i).trimmed() : slides.at(i);
        query.bindValue(2, text.isNull() ? QStringLiteral("") : text);
        if (!richText.isEmpty()) query.bindValue(3, richText.at(i).isNull() ? QStringLiteral("") : richText.at(i));
        if (!query.exec()) return fail(query.lastError().text(), error);
    }
    return true;
}

bool LibraryRepository::createSong(const QString &title, const QStringList &slides, QString *error,
                                   const PresentationSettings &settings, const QString &backgroundPath,
                                   const QStringList &richText) {
    clearError(error);
    if (title.trimmed().isEmpty() || slides.isEmpty())
        return fail(QStringLiteral("A song must have a title and at least one slide."), error);
    if (!settings.isValid()) return fail(QStringLiteral("The presentation timing or transition settings are invalid."), error);
    if (!validBackgroundPath(backgroundPath))
        return fail(QStringLiteral("Choose a managed image from the background library."), error);
    if (!validRichText(slides, richText))
        return fail(QStringLiteral("Provide one rich-text document per slide, no larger than 1 MB each (16 MB total)."), error);
    Transaction transaction(db_);
    if (!transaction.active()) return fail(db_.lastError().text(), error);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO songs(title,auto_advance_ms,presentation_loop,transition_type,transition_duration_ms,transition_override,background_path) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(title.trimmed());
    query.addBindValue(settings.autoAdvanceMs);
    query.addBindValue(settings.loop);
    query.addBindValue(int(settings.transition));
    query.addBindValue(settings.transitionDurationMs);
    query.addBindValue(settings.overrideTransition);
    query.addBindValue(backgroundPath.isNull() ? QStringLiteral("") : backgroundPath);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!storeSlides(query.lastInsertId().toInt(), slides, error, richText)) return false;
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return true;
}

int LibraryRepository::createPresentation(const QString &title, const QStringList &texts,
                                          const QVector<SlideMedia> &media, QString *error,
                                          const PresentationSettings &settings, const QString &backgroundPath,
                                          const QStringList &richText) {
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
        if (item.kind != MediaKind::Image && item.kind != MediaKind::Video) {
            fail(QStringLiteral("The presentation contains an unsupported media type."), error);
            return 0;
        }
        const QString cleanPath = QDir::cleanPath(item.path);
        if (QDir::isAbsolutePath(item.path) || !cleanPath.startsWith(QStringLiteral("media/"))
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
    Transaction transaction(db_);
    if (!transaction.active()) { fail(db_.lastError().text(), error); return 0; }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO songs(title,auto_advance_ms,presentation_loop,transition_type,transition_duration_ms,transition_override,background_path) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(title.trimmed());
    query.addBindValue(settings.autoAdvanceMs);
    query.addBindValue(settings.loop);
    query.addBindValue(int(settings.transition));
    query.addBindValue(settings.transitionDurationMs);
    query.addBindValue(settings.overrideTransition);
    query.addBindValue(backgroundPath.isNull() ? QStringLiteral("") : backgroundPath);
    if (!query.exec()) { fail(query.lastError().text(), error); return 0; }
    const int id = query.lastInsertId().toInt();
    query.prepare(QStringLiteral("INSERT INTO slides(song_id,position,text,media_kind,media_path,media_name,media_loop,rich_text) "
                                 "VALUES(?,?,?,?,?,?,?,?)"));
    for (int i = 0; i < texts.size(); ++i) {
        const auto &item = media.at(i);
        query.bindValue(0, id);
        query.bindValue(1, i);
        const QString text = richText.value(i).isEmpty() ? texts.at(i).trimmed() : texts.at(i);
        query.bindValue(2, text.isNull() ? QStringLiteral("") : text);
        query.bindValue(3, item.kind == MediaKind::Image ? QStringLiteral("image")
                           : item.kind == MediaKind::Video ? QStringLiteral("video") : QStringLiteral(""));
        query.bindValue(4, item.path.isNull() ? QStringLiteral("") : item.path);
        query.bindValue(5, item.name.isNull() ? QStringLiteral("") : item.name.trimmed());
        query.bindValue(6, item.loop);
        query.bindValue(7, richText.value(i).isNull() ? QStringLiteral("") : richText.value(i));
        if (!query.exec()) { fail(query.lastError().text(), error); return 0; }
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
    if (!storeSlides(id, slides, error, preservedRichText)) return false;
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
    const QStringList &texts, const QStringList &richText, QString *error) {
    clearError(error);
    if (title.trimmed().isEmpty() || texts.isEmpty())
        return fail(QStringLiteral("A presentation needs a title and at least one slide."), error);
    if (richText.size() != texts.size() || !validRichText(texts, richText))
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
    query.prepare(QStringLiteral("UPDATE slides SET text=?,rich_text=? WHERE id=? AND song_id=?"));
    for (int i = 0; i < texts.size(); ++i) {
        query.bindValue(0, texts.at(i).isNull() ? QStringLiteral("") : texts.at(i));
        query.bindValue(1, richText.at(i).isNull() ? QStringLiteral("") : richText.at(i));
        query.bindValue(2, slideIds.at(i));
        query.bindValue(3, songId);
        if (!query.exec()) return fail(query.lastError().text(), error);
        if (query.numRowsAffected() != 1)
            return fail(QStringLiteral("A slide could not be saved. Reopen the editor and try again."), error);
    }
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
    query.prepare(QStringLiteral("INSERT INTO service_entries(service_id,song_id,position,slide_order) "
                                 "SELECT ?,song_id,position,slide_order FROM service_entries WHERE service_id=? ORDER BY position,id"));
    query.addBindValue(newId);
    query.addBindValue(id);
    if (!query.exec()) { fail(query.lastError().text(), error); return 0; }
    if (!transaction.commit()) { fail(db_.lastError().text(), error); return 0; }
    return newId;
}

QVector<ServiceEntry> LibraryRepository::service(int serviceId) const {
    clearError();
    QVector<ServiceEntry> result;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id,song_id,slide_order FROM service_entries WHERE service_id=? ORDER BY position,id"));
    query.addBindValue(serviceId);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    while (query.next()) {
        ServiceEntry entry{query.value(0).toInt(), query.value(1).toInt(), {}};
        if (!decodeOrder(query.value(2), &entry.slideOrder)) {
            fail(QStringLiteral("A service item contains an invalid slide arrangement."));
            return {};
        }
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
    query.prepare(QStringLiteral("INSERT INTO service_entries(service_id,song_id,position,slide_order) "
                                 "SELECT service_id,song_id,position+1,slide_order FROM service_entries WHERE id=?"));
    query.addBindValue(entryId);
    if (!query.exec()) return fail(query.lastError().text(), error);
    if (!transaction.commit()) return fail(db_.lastError().text(), error);
    return true;
}

QStringList LibraryRepository::entrySlides(int entryId) const {
    clearError();
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT song_id,slide_order FROM service_entries WHERE id=?"));
    query.addBindValue(entryId);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    if (!query.next()) return {};
    QVector<int> order;
    if (!decodeOrder(query.value(1), &order)) {
        fail(QStringLiteral("A service item contains an invalid slide arrangement."));
        return {};
    }
    const Song source = song(query.value(0).toInt());
    if (!lastError_.isEmpty()) return {};
    if (order.isEmpty()) return source.slides;
    QStringList result;
    for (int index : order) {
        if (index >= source.slides.size()) {
            fail(QStringLiteral("A service item's slide arrangement references a missing slide."));
            return {};
        }
        result.append(source.slides.at(index));
    }
    return result;
}

QStringList LibraryRepository::entryRichText(int entryId) const {
    clearError();
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT song_id,slide_order FROM service_entries WHERE id=?"));
    query.addBindValue(entryId);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    if (!query.next()) return {};
    QVector<int> order;
    if (!decodeOrder(query.value(1), &order)) {
        fail(QStringLiteral("A service item contains an invalid slide arrangement."));
        return {};
    }
    const Song source = song(query.value(0).toInt());
    if (!lastError_.isEmpty()) return {};
    if (order.isEmpty()) return source.richText;
    QStringList result;
    for (int index : order) {
        if (index >= source.richText.size()) {
            fail(QStringLiteral("A service item's slide arrangement references missing rich text."));
            return {};
        }
        result.append(source.richText.at(index));
    }
    return result;
}

QVector<SlideMedia> LibraryRepository::entryMedia(int entryId) const {
    clearError();
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT song_id,slide_order FROM service_entries WHERE id=?"));
    query.addBindValue(entryId);
    if (!query.exec()) { fail(query.lastError().text()); return {}; }
    if (!query.next()) return {};
    QVector<int> order;
    if (!decodeOrder(query.value(1), &order)) {
        fail(QStringLiteral("A service item contains an invalid slide arrangement."));
        return {};
    }
    const Song source = song(query.value(0).toInt());
    if (!lastError_.isEmpty()) return {};
    if (order.isEmpty()) return source.media;
    QVector<SlideMedia> result;
    for (int index : order) {
        if (index >= source.media.size()) {
            fail(QStringLiteral("A service item's slide arrangement references missing media."));
            return {};
        }
        result.append(source.media.at(index));
    }
    return result;
}

bool LibraryRepository::setEntrySlideOrder(int entryId, const QVector<int> &indices, QString *error) {
    clearError(error);
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
