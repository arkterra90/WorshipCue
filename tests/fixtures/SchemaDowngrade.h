#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
// Fixtures that start from the latest schema remove this increment before simulating an older one.
inline bool removeCleanupSchema(const QString &path) {
    const auto name=QUuid::createUuid().toString();bool ok=true;
    {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);ok=db.open();
        if(ok){QSqlQuery q(db);ok=q.exec("SELECT name FROM sqlite_master WHERE type='trigger' AND name GLOB 'cleanup_*'");QStringList triggers;while(q.next())triggers.append(q.value(0).toString());q.finish();
            for(const auto &trigger:triggers)if(!q.exec("DROP TRIGGER \""+trigger+"\""))ok=false;
            for(const auto &table:{"cleanup_lease_refs","cleanup_leases","cleanup_files","content_usage","managed_files","cleanup_batches"})if(!q.exec("DROP TABLE IF EXISTS "+QString::fromLatin1(table)))ok=false;
        }db.close();}
    QSqlDatabase::removeDatabase(name);return ok;
}
