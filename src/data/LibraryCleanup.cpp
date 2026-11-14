#include "LibraryRepository.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#endif
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>

namespace {
constexpr int maxCleanupItems = 50000;
class CleanupTransaction {
public:
    explicit CleanupTransaction(QSqlDatabase &db) : db_(db) { QSqlQuery q(db_); active_ = q.exec("BEGIN IMMEDIATE"); }
    ~CleanupTransaction() { if (active_) db_.rollback(); }
    bool active() const { return active_; }
    bool commit() { if (!active_ || !db_.commit()) return false; active_ = false; return true; }
private:
    QSqlDatabase &db_;
    bool active_ = false;
};
// Keep a stalled/suspended running process protected after its heartbeat grace period.
bool processMayBeRunning(qint64 pid) {
    if(pid<=0 || pid>2147483647)return true;
#ifdef Q_OS_WIN
    const auto handle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));
    if(!handle)return GetLastError()!=ERROR_INVALID_PARAMETER;
    DWORD code=STILL_ACTIVE;const bool queried=GetExitCodeProcess(handle,&code);CloseHandle(handle);
    return !queried||code==STILL_ACTIVE;
#else
    if(::kill(pid_t(pid),0)==0)return true;
    return errno!=ESRCH;
#endif
}
QString ownerColumn(CleanupKind kind) { return kind == CleanupKind::Presentation ? "song_id" : "asset_id"; }
QString ids(const QSet<int> &values) {
    QStringList result;
    for (int id : values) if (id > 0) result.append(QString::number(id));
    return result.isEmpty() ? QStringLiteral("0") : result.join(',');
}
bool managedPath(const QString &path) {
    return !path.isEmpty() && path.size() <= 4096 && !path.contains(QChar::Null) && !path.contains('\\')
        && !QDir::isAbsolutePath(path) && QDir::cleanPath(path) == path
        && (path.startsWith("media/") || path.startsWith("backgrounds/")) && !path.endsWith('/');
}
bool batchId(const QString &id) { return id.size() == 36 && QUuid(id).toString(QUuid::WithoutBraces) == id; }
QString quarantinePath(const QString &batch, const QString &path) {
    return ".cleanup-recovery/" + batch + "/" + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".bin";
}
// Inspect every component: even symlinks pointing back inside the library are rejected.
bool safePath(const QString &root, const QString &relative, bool quarantine, QString *error) {
    if (root.isEmpty() || relative.contains(QChar::Null) || relative.contains('\\') || QDir::isAbsolutePath(relative)
        || QDir::cleanPath(relative) != relative || (quarantine ? !relative.startsWith(".cleanup-recovery/") : !managedPath(relative))) {
        if (error) *error = "Only regular files inside this library's managed folders can be removed.";
        return false;
    }
    QString current = QFileInfo(root).canonicalFilePath();
    if (current.isEmpty()) { if(error)*error="The library directory is unavailable."; return false; }
    const auto parts = relative.split('/');
    for (int i = 0; i < parts.size(); ++i) {
        current = QDir(current).filePath(parts.at(i));
        const QFileInfo info(current);
        if (info.isSymLink() || (info.exists() && (i + 1 == parts.size() ? !info.isFile() : !info.isDir()))) {
            if (error) *error = "A managed path is a symlink or is not a regular file: " + relative;
            return false;
        }
    }
    return true;
}
UsageDates dates(const QSqlQuery &q, int offset) { return {q.value(offset).toLongLong(), q.value(offset+1).toLongLong(), q.value(offset+2).toLongLong(), q.value(offset+3).toLongLong()}; }
QString ageReason(const UsageDates &d, qint64 cutoff) {
    if (d.lastUsedAt <= 0) return "Unknown last-used date; absence of history is not evidence of non-use.";
    if (d.lastUsedAt >= cutoff) return "Used within the selected period.";
    if (qMax(d.createdAt, qMax(d.importedAt, d.modifiedAt)) >= cutoff) return "Created, imported, or modified within the selected period.";
    return {};
}
bool samePlan(const CleanupPlan &a, const CleanupPlan &b) {
    if (a.items.size()!=b.items.size() || a.files.size()!=b.files.size() || a.recoverableBytes!=b.recoverableBytes) return false;
    for (int i=0;i<a.items.size();++i) {
        const auto &x=a.items.at(i), &y=b.items.at(i);
        if(x.key.token()!=y.key.token() || x.name!=y.name || x.dates.createdAt!=y.dates.createdAt || x.dates.importedAt!=y.dates.importedAt || x.dates.modifiedAt!=y.dates.modifiedAt || x.dates.lastUsedAt!=y.dates.lastUsedAt)return false;
    }
    for (int i=0;i<a.files.size();++i) {
        const auto &x=a.files.at(i), &y=b.files.at(i);
        if(x.path!=y.path || x.bytes!=y.bytes || x.modifiedMs!=y.modifiedMs || x.removable!=y.removable || x.reason!=y.reason) return false;
    }
    return true;
}
}

bool LibraryRepository::migrateCleanup(QString *error) {
    CleanupTransaction tx(db_);
    if (!tx.active()) return fail(db_.lastError().text(),error);
    const QStringList schema {
        "CREATE TABLE cleanup_batches(id TEXT PRIMARY KEY,created_at INTEGER NOT NULL,item_count INTEGER NOT NULL,state TEXT NOT NULL CHECK(state IN('archived','restored','purging','purged')))",
        "CREATE TABLE content_usage(song_id INTEGER UNIQUE REFERENCES songs(id) ON DELETE CASCADE,asset_id INTEGER UNIQUE REFERENCES media_assets(id) ON DELETE CASCADE,created_at INTEGER NOT NULL DEFAULT 0,imported_at INTEGER NOT NULL DEFAULT 0,modified_at INTEGER NOT NULL DEFAULT 0,last_used_at INTEGER NOT NULL DEFAULT 0,archived_batch TEXT REFERENCES cleanup_batches(id),CHECK((song_id IS NOT NULL)+(asset_id IS NOT NULL)=1))",
        "CREATE TABLE managed_files(path TEXT PRIMARY KEY,created_at INTEGER NOT NULL DEFAULT 0,imported_at INTEGER NOT NULL DEFAULT 0,modified_at INTEGER NOT NULL DEFAULT 0,last_used_at INTEGER NOT NULL DEFAULT 0,archived_batch TEXT REFERENCES cleanup_batches(id))",
        "CREATE TABLE cleanup_files(batch_id TEXT NOT NULL REFERENCES cleanup_batches(id),path TEXT NOT NULL,bytes INTEGER NOT NULL,modified_ms INTEGER NOT NULL,PRIMARY KEY(batch_id,path))",
        "CREATE TABLE cleanup_leases(owner TEXT PRIMARY KEY,expires_at INTEGER NOT NULL,process_id INTEGER NOT NULL)",
        "CREATE TABLE cleanup_lease_refs(owner TEXT NOT NULL REFERENCES cleanup_leases(owner) ON DELETE CASCADE,kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 2),reference TEXT NOT NULL,PRIMARY KEY(owner,kind,reference))",
        "INSERT INTO content_usage(song_id) SELECT id FROM songs",
        "INSERT INTO content_usage(asset_id,imported_at) SELECT id,COALESCE(imported_at,0) FROM media_assets",
        "INSERT INTO managed_files(path,imported_at) SELECT path,COALESCE(imported_at,0) FROM media_assets",
        "CREATE TRIGGER cleanup_song_created AFTER INSERT ON songs BEGIN INSERT INTO content_usage(song_id,created_at,modified_at) VALUES(NEW.id,CAST(strftime('%s','now') AS INTEGER),CAST(strftime('%s','now') AS INTEGER)); END",
        "CREATE TRIGGER cleanup_asset_created AFTER INSERT ON media_assets BEGIN INSERT INTO content_usage(asset_id,created_at,imported_at,modified_at) VALUES(NEW.id,CAST(strftime('%s','now') AS INTEGER),COALESCE(NEW.imported_at,0),CAST(strftime('%s','now') AS INTEGER)); INSERT INTO managed_files(path,created_at,imported_at,modified_at) VALUES(NEW.path,CAST(strftime('%s','now') AS INTEGER),COALESCE(NEW.imported_at,0),CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(path) DO NOTHING; END",
        "CREATE TRIGGER cleanup_song_modified AFTER UPDATE ON songs BEGIN UPDATE content_usage SET modified_at=MAX(modified_at,CAST(strftime('%s','now') AS INTEGER)) WHERE song_id=NEW.id; END",
        "CREATE TRIGGER cleanup_asset_modified AFTER UPDATE ON media_assets BEGIN UPDATE content_usage SET modified_at=MAX(modified_at,CAST(strftime('%s','now') AS INTEGER)) WHERE asset_id=NEW.id; END",
        "PRAGMA user_version=11"
    };
    for (const auto &statement : schema) if (!execute(statement,error)) return false;
    for (const auto &operation : {QStringLiteral("INSERT"),QStringLiteral("UPDATE"),QStringLiteral("DELETE")}) {
        const auto row=operation=="DELETE"?QStringLiteral("OLD"):QStringLiteral("NEW");
        if (!execute("CREATE TRIGGER cleanup_slide_"+operation+" AFTER "+operation+" ON slides BEGIN UPDATE content_usage SET modified_at=MAX(modified_at,CAST(strftime('%s','now') AS INTEGER)) WHERE song_id="+row+".song_id; END",error)) return false;
    }
    for (const auto &table : {QStringLiteral("songs"),QStringLiteral("media_assets")}) {
        const auto column=table=="songs"?QStringLiteral("song_id"):QStringLiteral("asset_id");
        for (const auto &operation : {QStringLiteral("UPDATE"),QStringLiteral("DELETE")}) {
            const auto state=operation=="DELETE"?QStringLiteral(" AND b.state<>'purging'"):QString();
            if (!execute("CREATE TRIGGER cleanup_guard_"+table+"_"+operation+" BEFORE "+operation+" ON "+table+" WHEN EXISTS(SELECT 1 FROM content_usage u JOIN cleanup_batches b ON b.id=u.archived_batch WHERE u."+column+"=OLD.id"+state+") BEGIN SELECT RAISE(ABORT,'This item is in Cleanup Recovery. Restore it before editing or deleting it.'); END",error)) return false;
        }
    }
    for (const auto &operation : {QStringLiteral("INSERT"),QStringLiteral("UPDATE"),QStringLiteral("DELETE")}) {
        const auto row=operation=="DELETE"?QStringLiteral("OLD"):QStringLiteral("NEW");
        const auto state=operation=="DELETE"?QStringLiteral(" AND b.state<>'purging'"):QString();
        if (!execute("CREATE TRIGGER cleanup_guard_slide_"+operation+" BEFORE "+operation+" ON slides WHEN EXISTS(SELECT 1 FROM content_usage u JOIN cleanup_batches b ON b.id=u.archived_batch WHERE u.song_id="+row+".song_id"+state+") BEGIN SELECT RAISE(ABORT,'Restore this presentation from Cleanup Recovery first.'); END",error)) return false;
    }
    for (const auto &operation : {QStringLiteral("INSERT"),QStringLiteral("UPDATE")}) {
        if (!execute("CREATE TRIGGER cleanup_guard_service_"+operation+" BEFORE "+operation+" ON service_entries WHEN EXISTS(SELECT 1 FROM content_usage WHERE (song_id=NEW.song_id OR asset_id=NEW.media_asset_id) AND archived_batch IS NOT NULL) BEGIN SELECT RAISE(ABORT,'Restore this item from Cleanup Recovery before adding it to a service.'); END",error)) return false;
    }
    for(const auto &table:{QStringLiteral("midi_slide_actions"),QStringLiteral("midi_bindings")}) {
        for(const auto &operation:{QStringLiteral("INSERT"),QStringLiteral("UPDATE"),QStringLiteral("DELETE")}) {
            const auto row=operation=="DELETE"?QStringLiteral("OLD"):QStringLiteral("NEW");
            const auto song=table=="midi_bindings"?row+".song_id":"(SELECT song_id FROM slides WHERE id="+row+".slide_id)";
            if(!execute("CREATE TRIGGER cleanup_modified_"+table+"_"+operation+" AFTER "+operation+" ON "+table+" BEGIN UPDATE content_usage SET modified_at=MAX(modified_at,CAST(strftime('%s','now') AS INTEGER)) WHERE song_id="+song+" OR asset_id="+row+".asset_id; END",error))return false;
        }
    }
    if(!execute("CREATE INDEX cleanup_content_batch ON content_usage(archived_batch)",error)||!execute("CREATE INDEX cleanup_managed_batch ON managed_files(archived_batch)",error))return false;
    QSqlQuery integrity(db_);
    if(!integrity.exec("PRAGMA foreign_key_check"))return fail(integrity.lastError().text(),error);
    if(integrity.next())return fail("Existing references failed the cleanup migration integrity check. No migration changes were committed.",error);
    integrity.finish();
    return tx.commit() || fail(db_.lastError().text(),error);
}

bool LibraryRepository::recordPresentationImport(int id,QString *error) {
    clearError(error);QSqlQuery q(db_);q.prepare("UPDATE content_usage SET imported_at=? WHERE song_id=? AND archived_batch IS NULL");q.addBindValue(QDateTime::currentSecsSinceEpoch());q.addBindValue(id);
    if(!q.exec())return fail(q.lastError().text(),error);
    return q.numRowsAffected()==1||fail("The imported presentation no longer exists.",error);
}
UsageDates LibraryRepository::usageDates(CleanupKind kind,int id,const QString &path) const {
    clearError(); QSqlQuery q(db_);
    q.prepare(kind==CleanupKind::File?"SELECT created_at,imported_at,modified_at,last_used_at FROM managed_files WHERE path=?":"SELECT created_at,imported_at,modified_at,last_used_at FROM content_usage WHERE "+ownerColumn(kind)+"=?");
    q.addBindValue(kind==CleanupKind::File?QVariant(path):QVariant(id));
    if(!q.exec()){fail(q.lastError().text());return {};}
    return q.next()?dates(q,0):UsageDates{};
}
bool LibraryRepository::recordManagedImport(const QString &path,QString *error) {
    clearError(error);if(!managedPath(path))return fail("Only managed media/background imports can be recorded.",error);
    QSqlQuery q(db_);q.prepare("INSERT INTO managed_files(path,created_at,imported_at,modified_at) VALUES(?,?,?,?) ON CONFLICT(path) DO NOTHING");q.addBindValue(path);
    const auto now=QDateTime::currentSecsSinceEpoch();for(int i=0;i<3;++i)q.addBindValue(now);
    return q.exec()||fail(q.lastError().text(),error);
}
bool LibraryRepository::recordUsage(int songId,const QSet<int> &assets,const QSet<QString> &paths,QString *error) {
    clearError(error);if(songId<0||assets.size()>10000||paths.size()>50000)return fail("Invalid usage references.",error);
    for(const auto &path:paths)if(!managedPath(path))return fail("Usage must reference managed relative paths.",error);
    CleanupTransaction tx(db_);if(!tx.active())return fail(db_.lastError().text(),error);
    const auto now=QDateTime::currentSecsSinceEpoch();QSqlQuery q(db_);
    if(songId){q.prepare("UPDATE content_usage SET last_used_at=MAX(last_used_at,?) WHERE song_id=? AND archived_batch IS NULL");q.addBindValue(now);q.addBindValue(songId);if(!q.exec())return fail(q.lastError().text(),error);}
    for(int id:assets){q.prepare("UPDATE content_usage SET last_used_at=MAX(last_used_at,?) WHERE asset_id=? AND archived_batch IS NULL");q.addBindValue(now);q.addBindValue(id);if(!q.exec())return fail(q.lastError().text(),error);}
    for(const auto &path:paths){
        q.prepare("INSERT INTO managed_files(path,last_used_at) VALUES(?,?) ON CONFLICT(path) DO UPDATE SET last_used_at=MAX(last_used_at,excluded.last_used_at)");q.addBindValue(path);q.addBindValue(now);if(!q.exec())return fail(q.lastError().text(),error);
        q.prepare("UPDATE content_usage SET last_used_at=MAX(last_used_at,?) WHERE asset_id IN(SELECT id FROM media_assets WHERE path=?) AND archived_batch IS NULL");q.addBindValue(now);q.addBindValue(path);if(!q.exec())return fail(q.lastError().text(),error);
    }
    return tx.commit()||fail(db_.lastError().text(),error);
}
bool LibraryRepository::replaceCleanupLease(const QString &owner,const CleanupProtection &p,QString *error) {
    clearError(error);if(owner.isEmpty()||owner.size()>100||p.songs.size()+p.assets.size()+p.paths.size()>50000)return fail("Invalid live snapshot lease.",error);
    CleanupTransaction tx(db_);if(!tx.active())return fail(db_.lastError().text(),error);QSqlQuery q(db_);
    // A newly captured snapshot cannot take content already archived by another connection.
    for(bool song:{true,false})for(int id:song?p.songs:p.assets){
        q.prepare("SELECT archived_batch FROM content_usage WHERE "+QString(song?"song_id":"asset_id")+"=?");q.addBindValue(id);if(!q.exec())return fail(q.lastError().text(),error);
        if(q.next()&&!q.value(0).isNull())return fail("A requested live item is in Cleanup Recovery. Restore it before taking it live.",error);
    }
    for(const auto &path:p.paths){if(!managedPath(path))return fail("Invalid managed snapshot path.",error);
        q.prepare("SELECT 1 FROM cleanup_files f JOIN cleanup_batches b ON b.id=f.batch_id WHERE f.path=? AND b.state='purging'");q.addBindValue(path);if(!q.exec())return fail(q.lastError().text(),error);if(q.next())return fail("A requested live file is being permanently removed. Refresh the library.",error);
    }
    q.prepare("INSERT INTO cleanup_leases(owner,expires_at,process_id) VALUES(?,?,?) ON CONFLICT(owner) DO UPDATE SET expires_at=excluded.expires_at,process_id=excluded.process_id");q.addBindValue(owner);q.addBindValue(QDateTime::currentSecsSinceEpoch()+60);q.addBindValue(QCoreApplication::applicationPid());if(!q.exec())return fail(q.lastError().text(),error);
    q.prepare("DELETE FROM cleanup_lease_refs WHERE owner=?");q.addBindValue(owner);if(!q.exec())return fail(q.lastError().text(),error);
    q.prepare("INSERT INTO cleanup_lease_refs(owner,kind,reference) VALUES(?,?,?)");
    for(int kind=0;kind<3;++kind){QStringList refs;if(kind<2)for(int id:kind==0?p.songs:p.assets)refs.append(QString::number(id));else refs=p.paths.values();
        for(const auto &ref:refs){q.bindValue(0,owner);q.bindValue(1,kind);q.bindValue(2,ref);if(!q.exec())return fail(q.lastError().text(),error);}}
    return tx.commit()||fail(db_.lastError().text(),error);
}
void LibraryRepository::clearCleanupLease(const QString &owner){QSqlQuery q(db_);q.prepare("DELETE FROM cleanup_leases WHERE owner=?");q.addBindValue(owner);q.exec();}
CleanupProtection LibraryRepository::cleanupLeases(const CleanupProtection &p) const {
    auto result=p;QSqlQuery q(db_);
    if(!q.exec("SELECT r.kind,r.reference,l.expires_at,l.process_id FROM cleanup_lease_refs r JOIN cleanup_leases l ON l.owner=r.owner")){fail(q.lastError().text());return {};}
    const auto now=QDateTime::currentSecsSinceEpoch();QHash<qint64,bool> running;
    while(q.next()){
        const auto pid=q.value(3).toLongLong();
        if(q.value(2).toLongLong()<=now){if(!running.contains(pid))running.insert(pid,processMayBeRunning(pid));if(!running.value(pid))continue;}
        const auto ref=q.value(1).toString();switch(q.value(0).toInt()){case 0:result.songs.insert(ref.toInt());break;case 1:result.assets.insert(ref.toInt());break;case 2:result.paths.insert(ref);break;}
    }
    return result;
}
QStringList LibraryRepository::cleanupPaths(CleanupKind kind,int id) const {
    QSqlQuery q(db_);QStringList paths;
    if(kind==CleanupKind::MediaAsset){q.prepare("SELECT path FROM media_assets WHERE id=?");q.addBindValue(id);if(!q.exec()){fail(q.lastError().text());return {};};if(q.next())paths.append(q.value(0).toString());return paths;}
    q.prepare("SELECT media_path FROM slides WHERE song_id=? AND media_path<>'' UNION SELECT background_path FROM songs WHERE id=? AND background_path<>''");q.addBindValue(id);q.addBindValue(id);if(!q.exec()){fail(q.lastError().text());return {};}
    while(q.next())paths.append(q.value(0).toString());
    q.prepare("SELECT settings FROM looks WHERE id IN(SELECT look_id FROM songs WHERE id=? UNION SELECT look_id FROM slides WHERE song_id=?)");q.addBindValue(id);q.addBindValue(id);if(!q.exec()){fail(q.lastError().text());return {};}
    while(q.next()){PresentationLook look;QString error;if(!PresentationLook::fromJson(q.value(0).toString(),&look,&error)){fail(error);return {};};if(look.background==PresentationLook::Background::Image)paths.append(look.backgroundPath);}
    paths.removeDuplicates();return paths;
}
QString LibraryRepository::cleanupReferenceReason(const QString &path,const QSet<int> &excludeSongs,const QSet<int> &excludeAssets) const {
    QSqlQuery q(db_);
    q.prepare("SELECT title FROM songs WHERE id NOT IN("+ids(excludeSongs)+") AND (background_path=? OR id IN(SELECT song_id FROM slides WHERE media_path=?)) LIMIT 1");q.addBindValue(path);q.addBindValue(path);
    if(!q.exec()){fail(q.lastError().text());return "Reference check failed.";}if(q.next())return "Referenced by presentation (including Recovery): "+q.value(0).toString();
    q.prepare("SELECT name FROM media_assets WHERE path=? AND id NOT IN("+ids(excludeAssets)+") LIMIT 1");q.addBindValue(path);if(!q.exec()){fail(q.lastError().text());return "Reference check failed.";}if(q.next())return "Referenced by media asset (including Recovery): "+q.value(0).toString();
    if(!q.exec("SELECT name,settings FROM looks")){fail(q.lastError().text());return "Reference check failed.";}
    while(q.next()){PresentationLook look;QString error;if(!PresentationLook::fromJson(q.value(1).toString(),&look,&error)){fail(error);return "A saved Look could not be checked.";}if(look.background==PresentationLook::Background::Image&&look.backgroundPath==path)return "Referenced by saved Look: "+q.value(0).toString();}
    return {};
}

QVector<CleanupItem> LibraryRepository::cleanupCandidates(int days,const CleanupProtection &provided) const {
    clearError();
    if(days<1||days>36500){fail("Enter a period of 1–36500 days.");return {};}
    const auto protection=cleanupLeases(provided);if(!lastError_.isEmpty())return {};
    const auto cutoff=QDateTime::currentSecsSinceEpoch()-qint64(days)*86400;
    QVector<CleanupItem> result;QSqlQuery q(db_);
    QSet<QString> registered;
    for(const auto kind:{CleanupKind::Presentation,CleanupKind::MediaAsset}) {
        const bool song=kind==CleanupKind::Presentation;
        const QString table=song?"songs":"media_assets", column=ownerColumn(kind);
        if(!q.exec("SELECT t.id,t."+QString(song?"title":"name")+",u.created_at,u.imported_at,u.modified_at,u.last_used_at FROM "+table+" t JOIN content_usage u ON u."+column+"=t.id WHERE u.archived_batch IS NULL ORDER BY t.id")){fail(q.lastError().text());return {};}
        while(q.next()) {
            if(result.size()>=maxCleanupItems){fail("This scan exceeds 50000 items. No cleanup selection was produced.");return {};}
            CleanupItem item;item.key={kind,q.value(0).toInt(),{}};item.name=q.value(1).toString();item.dates=dates(q,2);item.paths=cleanupPaths(kind,item.key.id);if(!lastError_.isEmpty())return {};
            for(const auto &path:item.paths)registered.insert(path);
            QSqlQuery refs(db_);refs.prepare("SELECT s.name FROM service_entries e JOIN services s ON s.id=e.service_id WHERE e."+QString(song?"song_id":"media_asset_id")+"=? LIMIT 1");refs.addBindValue(item.key.id);
            if(!refs.exec()){fail(refs.lastError().text());return {};}
            if(refs.next())item.reason="Referenced by saved service: "+refs.value(0).toString();
            else if((song?protection.songs:protection.assets).contains(item.key.id))item.reason="Protected by a live presentation/service snapshot.";
            else if(!song)item.reason=cleanupReferenceReason(item.paths.value(0),{},{item.key.id});
            if(!lastError_.isEmpty())return {};
            for(const auto &path:item.paths) {
                if(!item.reason.isEmpty())break;
                if(protection.paths.contains(path))item.reason="A live snapshot retains this managed file.";
                else if(!safePath(databaseDirectory(),path,false,&item.reason))break;
                else {
                    const QFileInfo file(QDir(databaseDirectory()).filePath(path));
                    if(file.exists()&&file.lastModified().toSecsSinceEpoch()>=cutoff)item.reason="A managed file was modified within the selected period.";
                    QSqlQuery shared(db_);shared.prepare("SELECT MAX(created_at,imported_at,modified_at,last_used_at) FROM managed_files WHERE path=?");shared.addBindValue(path);
                    if(!shared.exec()){fail(shared.lastError().text());return {};}
                    if(shared.next()&&shared.value(0).toLongLong()>=cutoff)item.reason="A shared managed file was used, imported, or modified within the selected period.";
                }
            }
            if(item.reason.isEmpty())item.reason=ageReason(item.dates,cutoff);
            item.eligible=item.reason.isEmpty();if(item.eligible)item.reason="Known usage is older than the selected period.";
            result.append(item);
        }
    }
    // Files without records are shown, but cannot qualify without actual usage history.
    QSet<QString> filePaths;
    if(!databaseDirectory().isEmpty())for(const auto &folder:{QStringLiteral("media"),QStringLiteral("backgrounds")}) {
        const auto root=QDir(databaseDirectory()).filePath(folder);
        if(QFileInfo(root).isSymLink()){fail("The managed "+folder+" folder is a symlink. Cleanup cannot safely scan it.");return {};}
        QDirIterator it(root,QDir::Files|QDir::Hidden|QDir::System,QDirIterator::Subdirectories);
        while(it.hasNext()) {
            const auto path=QDir(databaseDirectory()).relativeFilePath(it.next());
            if(filePaths.size()>=maxCleanupItems){fail("The managed-file scan exceeds 50000 files. No cleanup selection was produced.");return {};}
            filePaths.insert(path);
        }
    }
    if(!q.exec("SELECT path FROM managed_files")){fail(q.lastError().text());return {};}
    while(q.next()){filePaths.insert(q.value(0).toString());if(filePaths.size()>maxCleanupItems){fail("This scan exceeds 50000 managed files. No cleanup selection was produced.");return {};}}
    QStringList sorted=filePaths.values();sorted.sort();
    for(const auto &path:sorted) {
        if(registered.contains(path))continue;
        QSqlQuery file(db_);file.prepare("SELECT created_at,imported_at,modified_at,last_used_at,archived_batch FROM managed_files WHERE path=?");file.addBindValue(path);if(!file.exec()){fail(file.lastError().text());return {};}
        CleanupItem item;item.key={CleanupKind::File,0,path};item.name=path;item.paths={path};
        if(file.next()){if(!file.value(4).isNull())continue;item.dates=dates(file,0);}
        item.reason=cleanupReferenceReason(path);if(!lastError_.isEmpty())return {};
        if(item.reason.isEmpty()&&protection.paths.contains(path))item.reason="Protected by a live snapshot.";
        if(item.reason.isEmpty())safePath(databaseDirectory(),path,false,&item.reason);
        if(item.reason.isEmpty())item.reason=ageReason(item.dates,cutoff);
        const QFileInfo info(QDir(databaseDirectory()).filePath(path));
        if(item.reason.isEmpty()&&info.exists()&&info.lastModified().toSecsSinceEpoch()>=cutoff)item.reason="The file was modified within the selected period.";
        item.eligible=item.reason.isEmpty();if(item.eligible)item.reason="Unreferenced file with known old usage.";if(result.size()>=maxCleanupItems){fail("This scan exceeds 50000 items. No cleanup selection was produced.");return {};}result.append(item);
    }
    return result;
}
CleanupPlan LibraryRepository::cleanupPlan(const QVector<CleanupItem> &items,const CleanupProtection &provided) const {
    CleanupPlan plan;plan.items=items;QSet<int> songs,assets;QSet<QString> paths;
    const auto protection=cleanupLeases(provided);if(!lastError_.isEmpty())return {};
    for(const auto &item:items){if(item.key.kind==CleanupKind::Presentation)songs.insert(item.key.id);else if(item.key.kind==CleanupKind::MediaAsset)assets.insert(item.key.id);for(const auto &path:item.paths)paths.insert(path);}
    QStringList sorted=paths.values();sorted.sort();
    for(const auto &path:sorted) {
        CleanupFile file;file.path=path;
        file.reason=cleanupReferenceReason(path,songs,assets);if(!lastError_.isEmpty())return {};
        if(file.reason.isEmpty()&&protection.paths.contains(path))file.reason="Protected by a live snapshot.";
        QString unsafe;
        if(!safePath(databaseDirectory(),path,false,&unsafe)){file.reason=unsafe;}
        const QFileInfo info(QDir(databaseDirectory()).filePath(path));file.bytes=info.exists()&&info.isFile()&&!info.isSymLink()?info.size():0;file.modifiedMs=info.exists()?info.lastModified().toMSecsSinceEpoch():0;
        if(file.reason.isEmpty()&&!info.exists())file.reason="Already missing; metadata only.";
        file.removable=file.reason.isEmpty();if(file.removable)plan.recoverableBytes+=file.bytes;
        plan.files.append(file);
    }
    return plan;
}
CleanupPlan LibraryRepository::estimateCleanup(const QVector<CleanupItem> &items,const CleanupProtection &p) const {
    clearError();return cleanupPlan(items,p);
}
CleanupPlan LibraryRepository::previewCleanup(const QVector<CleanupKey> &keys,int days,const CleanupProtection &p) const {
    clearError();if(keys.isEmpty()||keys.size()>maxCleanupItems){fail("Select between 1 and 50000 eligible items.");return {};}
    const auto all=cleanupCandidates(days,p);if(!lastError_.isEmpty())return {};
    QHash<QString,CleanupItem> found;for(const auto &item:all)found.insert(item.key.token(),item);
    QVector<CleanupItem> selected;QSet<QString> seen;
    for(const auto &key:keys){const auto it=found.constFind(key.token());if(it==found.cend()||!it->eligible||seen.contains(key.token())){fail("The selection changed or contains a protected item. Scan again before cleaning up.");return {};};seen.insert(key.token());selected.append(*it);}
    std::sort(selected.begin(),selected.end(),[](const auto &a,const auto &b){return a.key.token()<b.key.token();});
    return cleanupPlan(selected,p);
}
QString LibraryRepository::archiveCleanup(const QVector<CleanupKey> &keys,int days,const CleanupProtection &p,QString *error) {
    clearError(error);CleanupTransaction tx(db_);if(!tx.active()){fail(db_.lastError().text(),error);return {};}
    const auto plan=previewCleanup(keys,days,p);if(!lastError_.isEmpty()){fail(lastError_,error);return {};}
    const auto batch=QUuid::createUuid().toString(QUuid::WithoutBraces);QSqlQuery q(db_);
    q.prepare("INSERT INTO cleanup_batches(id,created_at,item_count,state) VALUES(?,?,?,'archived')");q.addBindValue(batch);q.addBindValue(QDateTime::currentSecsSinceEpoch());q.addBindValue(plan.items.size());if(!q.exec()){fail(q.lastError().text(),error);return {};}
    for(const auto &item:plan.items){
        q.prepare(item.key.kind==CleanupKind::File?"UPDATE managed_files SET archived_batch=? WHERE path=? AND archived_batch IS NULL":"UPDATE content_usage SET archived_batch=? WHERE "+ownerColumn(item.key.kind)+"=? AND archived_batch IS NULL");q.addBindValue(batch);q.addBindValue(item.key.kind==CleanupKind::File?QVariant(item.key.path):QVariant(item.key.id));
        if(!q.exec()||q.numRowsAffected()!=1){fail("An item changed while moving it to Recovery. No cleanup changes were saved.",error);return {};}
    }
    if(!tx.commit()){fail(db_.lastError().text(),error);return {};}
    return batch;
}
QVector<CleanupBatch> LibraryRepository::cleanupBatches() const {
    clearError();QSqlQuery q(db_);if(!q.exec("SELECT id,created_at,item_count,state FROM cleanup_batches WHERE state IN('archived','purging') OR id IN(SELECT batch_id FROM cleanup_files) ORDER BY created_at DESC,id")){fail(q.lastError().text());return {};}
    QVector<CleanupBatch> result;while(q.next())result.append({q.value(0).toString(),q.value(1).toLongLong(),q.value(2).toInt(),q.value(3).toString()});return result;
}
bool LibraryRepository::restoreCleanup(const QString &batch,QString *error) {
    clearError(error);if(!batchId(batch))return fail("Choose a valid Recovery batch.",error);CleanupTransaction tx(db_);if(!tx.active())return fail(db_.lastError().text(),error);
    QSqlQuery q(db_);q.prepare("UPDATE cleanup_batches SET state='restored' WHERE id=? AND state='archived'");q.addBindValue(batch);if(!q.exec()||q.numRowsAffected()!=1)return fail("This Recovery batch is unavailable or has already been permanently removed.",error);
    for(const auto &table:{QStringLiteral("content_usage"),QStringLiteral("managed_files")}){q.prepare("UPDATE "+table+" SET archived_batch=NULL WHERE archived_batch=?");q.addBindValue(batch);if(!q.exec())return fail(q.lastError().text(),error);}
    return tx.commit()||fail(db_.lastError().text(),error);
}
CleanupPlan LibraryRepository::previewCleanupPurge(const QString &batch,const CleanupProtection &p) const {
    clearError();if(!batchId(batch)){fail("Choose a valid Recovery batch.");return {};}
    QSqlQuery q(db_);q.prepare("SELECT state,created_at FROM cleanup_batches WHERE id=?");q.addBindValue(batch);if(!q.exec()){fail(q.lastError().text());return {};};if(!q.next()||q.value(0).toString()!="archived"){fail("Only archived Recovery batches can be reviewed for permanent removal.");return {};}
    const auto archivedAt=q.value(1).toLongLong();
    const auto protection=cleanupLeases(p);if(!lastError_.isEmpty())return {};QVector<CleanupItem> items;
    q.prepare("SELECT u.song_id,u.asset_id,COALESCE(s.title,a.name),u.created_at,u.imported_at,u.modified_at,u.last_used_at FROM content_usage u LEFT JOIN songs s ON s.id=u.song_id LEFT JOIN media_assets a ON a.id=u.asset_id WHERE archived_batch=? ORDER BY u.song_id,u.asset_id");q.addBindValue(batch);if(!q.exec()){fail(q.lastError().text());return {};}
    while(q.next()) {
        CleanupItem item;item.key={q.value(0).isNull()?CleanupKind::MediaAsset:CleanupKind::Presentation,q.value(0).isNull()?q.value(1).toInt():q.value(0).toInt(),{}};item.name=q.value(2).toString();item.dates=dates(q,3);item.paths=cleanupPaths(item.key.kind,item.key.id);if(!lastError_.isEmpty())return {};
        if((item.key.kind==CleanupKind::Presentation?protection.songs:protection.assets).contains(item.key.id)){fail("A live snapshot now protects an item in this Recovery batch.");return {};}
        QSqlQuery refs(db_);refs.prepare("SELECT 1 FROM service_entries WHERE "+QString(item.key.kind==CleanupKind::Presentation?"song_id":"media_asset_id")+"=? LIMIT 1");refs.addBindValue(item.key.id);if(!refs.exec()){fail(refs.lastError().text());return {};};if(refs.next()){fail("A saved service now references an item in Recovery. Restore the batch before editing that service.");return {};}
        for(const auto &path:item.paths)if(protection.paths.contains(path)){fail("A live snapshot now protects a managed file in this Recovery batch.");return {};}
        items.append(item);
    }
    q.prepare("SELECT path,created_at,imported_at,modified_at,last_used_at FROM managed_files WHERE archived_batch=? ORDER BY path");q.addBindValue(batch);if(!q.exec()){fail(q.lastError().text());return {};}
    while(q.next()){CleanupItem item;item.key={CleanupKind::File,0,q.value(0).toString()};item.name=item.key.path;item.paths={item.key.path};item.dates=dates(q,1);if(protection.paths.contains(item.key.path)){fail("A live snapshot now protects a file in this Recovery batch.");return {};}items.append(item);}
    std::sort(items.begin(),items.end(),[](const auto &a,const auto &b){return a.key.token()<b.key.token();});
    const auto plan=cleanupPlan(items,p);if(!lastError_.isEmpty())return {};
    for(const auto &item:items)if(qMax(qMax(item.dates.createdAt,item.dates.importedAt),qMax(item.dates.modifiedAt,item.dates.lastUsedAt))>=archivedAt){fail("An item changed or was used after archiving. Restore it and run a fresh age scan.");return {};}
    for(const auto &file:plan.files)if(file.removable){
        q.prepare("SELECT MAX(created_at,imported_at,modified_at,last_used_at) FROM managed_files WHERE path=?");q.addBindValue(file.path);if(!q.exec()){fail(q.lastError().text());return {};}
        if((q.next()&&q.value(0).toLongLong()>=archivedAt)||file.modifiedMs/1000>=archivedAt){fail("A file changed or was used after archiving. Restore the batch and run a fresh age scan.");return {};}
    }
    return plan;
}

bool LibraryRepository::purgeCleanup(const QString &batch,const CleanupPlan &reviewed,const CleanupProtection &p,QString *error) {
    clearError(error);
    if(!batchId(batch)||reviewed.items.isEmpty())return fail("Review a nonempty Recovery batch before permanent removal.",error);
    {
        CleanupTransaction tx(db_);if(!tx.active())return fail(db_.lastError().text(),error);
        const auto fresh=previewCleanupPurge(batch,p);if(!lastError_.isEmpty())return fail(lastError_,error);
        if(!samePlan(reviewed,fresh))return fail("The Recovery contents, file sizes, or references changed. Review the permanent-delete preview again.",error);
        QSqlQuery q(db_);q.prepare("UPDATE cleanup_batches SET state='purging' WHERE id=? AND state='archived'");q.addBindValue(batch);if(!q.exec()||q.numRowsAffected()!=1)return fail("This Recovery batch changed. Review it again.",error);
        q.prepare("INSERT INTO cleanup_files(batch_id,path,bytes,modified_ms) VALUES(?,?,?,?)");
        for(const auto &file:fresh.files)if(file.removable){q.bindValue(0,batch);q.bindValue(1,file.path);q.bindValue(2,file.bytes);q.bindValue(3,file.modifiedMs);if(!q.exec())return fail(q.lastError().text(),error);}
        if(!tx.commit())return fail(db_.lastError().text(),error);
    }
    QString operationError;
    bool committed=false;
    {
        CleanupTransaction tx(db_);
        if(!tx.active())operationError=db_.lastError().text();
        else {
            QSqlQuery q(db_);q.prepare("SELECT state,created_at FROM cleanup_batches WHERE id=?");q.addBindValue(batch);
            if(!q.exec()||!q.next()||q.value(0).toString()!="purging")operationError="The pending cleanup was recovered by another connection. Review it again.";
            const auto archivedAt=q.value(1).toLongLong();
            // Recheck every reference under the writer lock before touching any file.
            const auto protection=cleanupLeases(p);if(!lastError_.isEmpty())operationError=lastError_;
            for(const auto &item:reviewed.items) {
                if(!operationError.isEmpty())break;
                if(item.key.kind!=CleanupKind::File){
                    if((item.key.kind==CleanupKind::Presentation?protection.songs:protection.assets).contains(item.key.id)){operationError="An item is now protected by a live snapshot.";break;}
                    q.prepare("SELECT 1 FROM service_entries WHERE "+QString(item.key.kind==CleanupKind::Presentation?"song_id":"media_asset_id")+"=? LIMIT 1");q.addBindValue(item.key.id);if(!q.exec()||q.next()){operationError="A service reference could not be ruled out. No items were permanently removed.";break;}
                }
                for(const auto &path:item.paths)if(protection.paths.contains(path)){operationError="A live snapshot now protects a selected file.";break;}
            }
            QSet<int> songs,assets;for(const auto &item:reviewed.items){if(item.key.kind==CleanupKind::Presentation)songs.insert(item.key.id);if(item.key.kind==CleanupKind::MediaAsset)assets.insert(item.key.id);}
            for(const auto &file:reviewed.files)if(file.removable&&operationError.isEmpty()) {
                const auto reason=cleanupReferenceReason(file.path,songs,assets);
                if(!lastError_.isEmpty()||!reason.isEmpty()){operationError="A file gained a reference after the preview. No items were permanently removed.";break;}
                q.prepare("SELECT MAX(created_at,imported_at,modified_at,last_used_at) FROM managed_files WHERE path=?");q.addBindValue(file.path);
                if(!q.exec()||(q.next()&&q.value(0).toLongLong()>=archivedAt)){operationError="A file was used after archiving. Restore the batch and run a fresh age scan.";break;}
                const auto destination=quarantinePath(batch,file.path);
                if(!safePath(databaseDirectory(),file.path,false,&operationError)||!safePath(databaseDirectory(),destination,true,&operationError))break;
                const QFileInfo original(QDir(databaseDirectory()).filePath(file.path));
                if(!original.exists()||original.size()!=file.bytes||original.lastModified().toMSecsSinceEpoch()!=file.modifiedMs){operationError="A file changed after review. No items were permanently removed.";break;}
                const auto target=QDir(databaseDirectory()).filePath(destination);
                if(QFileInfo::exists(target)||!QDir().mkpath(QFileInfo(target).absolutePath())||!QFile::rename(original.absoluteFilePath(),target)){operationError="Could not stage a file for permanent removal: "+file.path;break;}
            }
            if(operationError.isEmpty())for(const auto &item:reviewed.items)if(item.key.kind!=CleanupKind::File){
                q.prepare("DELETE FROM "+QString(item.key.kind==CleanupKind::Presentation?"songs":"media_assets")+" WHERE id=?");q.addBindValue(item.key.id);if(!q.exec()||q.numRowsAffected()!=1){operationError=q.lastError().text().isEmpty()?QStringLiteral("A Recovery item changed during permanent removal."):q.lastError().text();break;}
            }
            if(operationError.isEmpty()){
                q.prepare("UPDATE managed_files SET archived_batch=NULL WHERE archived_batch=?");q.addBindValue(batch);if(!q.exec())operationError=q.lastError().text();
            }
            if(operationError.isEmpty()){
                q.prepare("UPDATE cleanup_batches SET state='purged' WHERE id=?");q.addBindValue(batch);if(!q.exec())operationError=q.lastError().text();
            }
            if(operationError.isEmpty()){committed=tx.commit();if(!committed)operationError=db_.lastError().text();}
        }
    }
    // The durable journal distinguishes rollback restoration from an authorized committed purge.
    QString recoveryError;
    if(!recoverCleanup(&recoveryError))return fail(operationError+" "+recoveryError,error);
    if(!committed)return fail(operationError.isEmpty()?QStringLiteral("Permanent cleanup was rolled back."):operationError,error);
    return true;
}

bool LibraryRepository::recoverCleanup(QString *error) {
    clearError(error);CleanupTransaction tx(db_);if(!tx.active())return fail(db_.lastError().text(),error);
    QSqlQuery batches(db_);if(!batches.exec("SELECT id,state FROM cleanup_batches WHERE state='purging' OR (state='purged' AND id IN(SELECT batch_id FROM cleanup_files))"))return fail(batches.lastError().text(),error);
    QVector<QPair<QString,QString>> pending;while(batches.next())pending.append({batches.value(0).toString(),batches.value(1).toString()});batches.finish();
    for(const auto &batch:pending) {
        if(!batchId(batch.first))return fail("Invalid cleanup recovery journal identity; no files were changed.",error);
        QSqlQuery q(db_);q.prepare("SELECT path,bytes,modified_ms FROM cleanup_files WHERE batch_id=? ORDER BY path");q.addBindValue(batch.first);if(!q.exec())return fail(q.lastError().text(),error);
        QVector<CleanupFile> files;while(q.next()){CleanupFile file;file.path=q.value(0).toString();file.bytes=q.value(1).toLongLong();file.modifiedMs=q.value(2).toLongLong();files.append(file);}q.finish();
        for(const auto &file:files) {
            const auto staged=quarantinePath(batch.first,file.path);QString safety;
            if(!safePath(databaseDirectory(),staged,true,&safety)||!safePath(databaseDirectory(),file.path,false,&safety))return fail(safety,error);
            const auto original=QDir(databaseDirectory()).filePath(file.path),target=QDir(databaseDirectory()).filePath(staged);const QFileInfo stored(target);
            if(batch.second=="purging") {
                if(stored.exists()){
                    if(QFileInfo::exists(original)||stored.size()!=file.bytes||stored.lastModified().toMSecsSinceEpoch()!=file.modifiedMs)return fail("Recovery found a changed or conflicting file. It was left untouched: "+file.path,error);
                    if(!QDir().mkpath(QFileInfo(original).absolutePath())||!QFile::rename(target,original))return fail("Could not restore an interrupted cleanup file: "+file.path,error);
                }else if(!QFileInfo::exists(original))return fail("An interrupted cleanup file is missing from both original and recovery locations: "+file.path,error);
            } else {
                // This branch only unlinks files after the user-confirmed DB purge committed.
                if(stored.exists()&&(stored.size()!=file.bytes||stored.lastModified().toMSecsSinceEpoch()!=file.modifiedMs))return fail("A staged deletion file changed; it was left untouched: "+file.path,error);
                if(stored.exists()&&!QFile::remove(target))continue; // Keep the journal for an explicit retry.
                if(!QFileInfo::exists(original)&&cleanupReferenceReason(file.path).isEmpty()) {
                    if(!lastError_.isEmpty())return fail(lastError_,error);
                    q.prepare("DELETE FROM managed_files WHERE path=? AND archived_batch IS NULL");q.addBindValue(file.path);if(!q.exec())return fail(q.lastError().text(),error);
                }
            }
            q.prepare("DELETE FROM cleanup_files WHERE batch_id=? AND path=?");q.addBindValue(batch.first);q.addBindValue(file.path);if(!q.exec())return fail(q.lastError().text(),error);
        }
        if(batch.second=="purging"){q.prepare("UPDATE cleanup_batches SET state='archived' WHERE id=?");q.addBindValue(batch.first);if(!q.exec())return fail(q.lastError().text(),error);}
    }
    return tx.commit()||fail(db_.lastError().text(),error);
}
