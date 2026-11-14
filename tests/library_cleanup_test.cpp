#include "../src/data/LibraryRepository.h"
#include "fixtures/SchemaDowngrade.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QTemporaryDir>

namespace {
bool sql(const QString &path,const QString &statement) {
    const auto name=QUuid::createUuid().toString();bool ok=false;
    {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);if(db.open()){QSqlQuery q(db);ok=q.exec(statement);if(!ok)qWarning()<<q.lastError();}db.close();}QSqlDatabase::removeDatabase(name);return ok;
}
QVariant value(const QString &path,const QString &statement) {
    const auto name=QUuid::createUuid().toString();QVariant result;
    {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);if(db.open()){QSqlQuery q(db);if(q.exec(statement)&&q.next())result=q.value(0);}db.close();}QSqlDatabase::removeDatabase(name);return result;
}
qint64 past(int days=200){return QDateTime::currentSecsSinceEpoch()-qint64(days)*86400;}
bool age(const QString &path) {
    const auto time=QString::number(past());
    return sql(path,"UPDATE content_usage SET created_at="+time+",imported_at="+time+",modified_at="+time+",last_used_at="+time)
        &&sql(path,"UPDATE managed_files SET created_at="+time+",imported_at="+time+",modified_at="+time+",last_used_at="+time);
}
bool file(const QString &root,const QString &relative,int size=123) {
    const auto path=QDir(root).filePath(relative);if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(QByteArray(size,'x'))!=size||!f.flush())return false;
    return f.setFileTime(QDateTime::fromSecsSinceEpoch(past()),QFileDevice::FileModificationTime);
}
CleanupItem item(const QVector<CleanupItem> &items,CleanupKind kind,int id=0,const QString &path={}) {
    for(const auto &i:items)if(i.key.kind==kind&&i.key.id==id&&i.key.path==path)return i;return {};
}
QString staged(const QString &root,const QString &batch,const QString &path){return QDir(root).filePath(".cleanup-recovery/"+batch+"/"+QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(),QCryptographicHash::Sha256).toHex())+".bin");}
}
class LibraryCleanupTest:public QObject {
 Q_OBJECT
private slots:
 void migrationKeepsLegacyDatesUnknownAndRollsBackPartialSchema(){
    for(bool conflict:{false,true}){QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString error;int id; qint64 slide;
        {LibraryRepository r;QVERIFY(r.open(&error,path));id=r.createTextPresentation("Preserve",{"Text"},&error);slide=r.song(id).slideIds[0];}
        QVERIFY(removeCleanupSchema(path));QVERIFY(sql(path,"PRAGMA user_version=10"));if(conflict)QVERIFY(sql(path,"CREATE TABLE managed_files(blocker TEXT)"));
        {LibraryRepository r;QCOMPARE(r.open(&error,path),!conflict);if(!conflict){QCOMPARE(r.song(id).slideIds[0],slide);auto d=r.usageDates(CleanupKind::Presentation,id);QCOMPARE(d.createdAt,0);QCOMPARE(d.importedAt,0);QCOMPARE(d.modifiedAt,0);QCOMPARE(d.lastUsedAt,0);auto candidate=item(r.cleanupCandidates(90),CleanupKind::Presentation,id);QVERIFY(!candidate.eligible);QVERIFY(candidate.reason.contains("Unknown"));}}
        QCOMPARE(value(path,"PRAGMA user_version").toInt(),conflict?10:11);if(conflict){QCOMPARE(value(path,"SELECT count(*) FROM sqlite_master WHERE name='content_usage'").toInt(),0);QCOMPARE(value(path,"SELECT title FROM songs WHERE id="+QString::number(id)).toString(),QString("Preserve"));}
    }
 }
 void creationImportModificationAndActualUseRemainDistinct(){
    QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString error;LibraryRepository r;QVERIFY(r.open(&error,path));const int id=r.createTextPresentation("Dates",{"One"},&error);auto d=r.usageDates(CleanupKind::Presentation,id);QVERIFY(d.createdAt>0);QCOMPARE(d.importedAt,0);QCOMPARE(d.lastUsedAt,0);QVERIFY(age(path));d=r.usageDates(CleanupKind::Presentation,id);const auto modified=d.modifiedAt;QVERIFY(r.recordUsage(id,{},{},&error));d=r.usageDates(CleanupKind::Presentation,id);QVERIFY(d.lastUsedAt>past(1));QCOMPARE(d.modifiedAt,modified);QVERIFY(r.recordPresentationImport(id,&error));QVERIFY(r.usageDates(CleanupKind::Presentation,id).importedAt>past(1));QVERIFY(age(path));QVERIFY(r.renamePresentation(id,"Edited",&error));QVERIFY(r.usageDates(CleanupKind::Presentation,id).modifiedAt>past(1));
    QVERIFY(file(dir.path(),"media/shared.png"));const auto assets=r.registerMediaAssets({{MediaKind::Image,"media/shared.png","Image",false}},&error);QCOMPARE(assets.size(),1);auto assetDates=r.usageDates(CleanupKind::MediaAsset,assets[0]);QVERIFY(assetDates.importedAt>0);QCOMPARE(assetDates.lastUsedAt,0);QVERIFY(r.recordUsage(0,{}, {"media/shared.png"},&error));QVERIFY(r.usageDates(CleanupKind::MediaAsset,assets[0]).lastUsedAt>past(1));QVERIFY(r.usageDates(CleanupKind::File,0,"media/shared.png").lastUsedAt>past(1));
 }
 void customDaysUnknownUsageAndSavedServicesAreProtected(){
    QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString error;LibraryRepository r;QVERIFY(r.open(&error,path));const int id=r.createTextPresentation("Old",{"One"},&error);QVERIFY(age(path));QVERIFY(sql(path,"UPDATE content_usage SET last_used_at="+QString::number(past(100))+" WHERE song_id="+QString::number(id)));
    QVERIFY(item(r.cleanupCandidates(90),CleanupKind::Presentation,id).eligible);QVERIFY(!item(r.cleanupCandidates(120),CleanupKind::Presentation,id).eligible);QVERIFY(r.cleanupCandidates(0).isEmpty());QVERIFY(!r.lastError().isEmpty());QVERIFY(!item(r.cleanupCandidates(36500),CleanupKind::Presentation,id).eligible);
    const int service=r.createService("Protected service",&error);QVERIFY(r.appendToService(service,id,&error));auto candidate=item(r.cleanupCandidates(1),CleanupKind::Presentation,id);QVERIFY(!candidate.eligible);QVERIFY(candidate.reason.contains("Protected service"));QVERIFY(r.archiveCleanup({candidate.key},1,{},&error).isEmpty());QVERIFY(r.removeFromService(r.service(service)[0].id,&error));QVERIFY(item(r.cleanupCandidates(90),CleanupKind::Presentation,id).eligible);QVERIFY(sql(path,"UPDATE content_usage SET last_used_at=0 WHERE song_id="+QString::number(id)));QVERIFY(!item(r.cleanupCandidates(1),CleanupKind::Presentation,id).eligible);
 }
 void sharedPathsCountOnceAndRecoveryPreservesIdentitiesAndMidi(){
    QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString error;LibraryRepository r;QVERIFY(r.open(&error,path));QVERIFY(file(dir.path(),"media/shared.png"));SlideMedia media{MediaKind::Image,"media/shared.png","Image",false};const int first=r.createPresentation("First",{""},{media},&error),second=r.createPresentation("Second",{""},{media},&error);QVERIFY(first&&second);const auto original=r.song(first);MidiOutputAction output;output.destination="out";QVERIFY(r.setMidiActions(MidiOwnerType::Slide,original.slideIds[0],{},{output},&error));MidiBinding binding;binding.name="Take";binding.command=MidiCommand::TakeSlide;binding.songId=first;binding.slideId=original.slideIds[0];QVERIFY(r.saveMidiBinding(binding,&error));const auto bindingId=r.midiBindings()[0].id;QVERIFY(age(path));
    const auto plan=r.previewCleanup({{CleanupKind::Presentation,first,{}},{CleanupKind::Presentation,second,{}}},90);QVERIFY2(r.lastError().isEmpty(),qPrintable(r.lastError()));QCOMPARE(plan.files.size(),1);QCOMPARE(plan.recoverableBytes,0);QVERIFY(!plan.files[0].removable);QVERIFY(plan.files[0].reason.contains("media asset"));const int asset=r.mediaAssets()[0].id;QVERIFY(!item(r.cleanupCandidates(90),CleanupKind::MediaAsset,asset).eligible);
    const auto batch=r.archiveCleanup({{CleanupKind::Presentation,first,{}},{CleanupKind::Presentation,second,{}}},90,{},&error);QVERIFY2(!batch.isEmpty(),qPrintable(error));QCOMPARE(r.song(first).id,0);QCOMPARE(value(path,"SELECT count(*) FROM songs WHERE id="+QString::number(first)).toInt(),1);QVERIFY(QFileInfo::exists(dir.filePath("media/shared.png")));QVERIFY(!r.renamePresentation(first,"Stale",&error));QVERIFY(!r.appendToService(1,first,&error));QVERIFY(r.restoreCleanup(batch,&error));QCOMPARE(r.song(first).slideIds,original.slideIds);QCOMPARE(r.song(first).midiActions[0],(QVector<MidiOutputAction>{output}));QCOMPARE(r.midiBinding(bindingId).slideId,original.slideIds[0]);
 }
 void assetCleanupFreesOnlyReviewedUnsharedFilesAndRevalidates(){
    QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString error;LibraryRepository r;QVERIFY(r.open(&error,path));QVERIFY(file(dir.path(),"media/remove.png",321));const int id=r.registerMediaAssets({{MediaKind::Image,"media/remove.png","Remove",false}},&error)[0];QVERIFY(age(path));CleanupKey key{CleanupKind::MediaAsset,id,{}};auto plan=r.previewCleanup({key},90);QCOMPARE(plan.recoverableBytes,321);QVERIFY(r.recordUsage(0,{id},{"media/remove.png"},&error));QVERIFY(r.archiveCleanup({key},90,{},&error).isEmpty());QVERIFY(age(path));const auto batch=r.archiveCleanup({key},90,{},&error);QVERIFY(!batch.isEmpty());QCOMPARE(r.mediaAsset(id).id,0);QVERIFY(QFileInfo::exists(dir.filePath("media/remove.png")));plan=r.previewCleanupPurge(batch);QCOMPARE(plan.recoverableBytes,321);
    QVERIFY(file(dir.path(),"media/remove.png",322));QVERIFY(!r.purgeCleanup(batch,plan,{},&error));QVERIFY(error.contains("changed"));QVERIFY(QFileInfo::exists(dir.filePath("media/remove.png")));plan=r.previewCleanupPurge(batch);QCOMPARE(plan.recoverableBytes,322);QVERIFY2(r.purgeCleanup(batch,plan,{},&error),qPrintable(error));QVERIFY(!QFileInfo::exists(dir.filePath("media/remove.png")));QCOMPARE(value(path,"SELECT count(*) FROM media_assets WHERE id="+QString::number(id)).toInt(),0);QVERIFY(!r.restoreCleanup(batch,&error));
 }
 void recoveryCannotPurgeFilesUsedAfterArchiving(){
    QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString error;LibraryRepository r;QVERIFY(r.open(&error,path));QVERIFY(file(dir.path(),"media/reused.png"));const int id=r.registerMediaAssets({{MediaKind::Image,"media/reused.png","Reused",false}},&error)[0];QVERIFY(age(path));const auto batch=r.archiveCleanup({{CleanupKind::MediaAsset,id,{}}},90,{},&error);QVERIFY(!batch.isEmpty());auto plan=r.previewCleanupPurge(batch);QCOMPARE(plan.recoverableBytes,123);QVERIFY(r.recordUsage(0,{}, {"media/reused.png"},&error));QVERIFY(!r.purgeCleanup(batch,plan,{},&error));QVERIFY(error.contains("after archiving"));QVERIFY(QFileInfo::exists(dir.filePath("media/reused.png")));QVERIFY(r.restoreCleanup(batch,&error));QVERIFY(!item(r.cleanupCandidates(90),CleanupKind::MediaAsset,id).eligible);
 }
 void snapshotsAndCrossConnectionLeasesProtectDeletedLibraryMedia(){
    QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString error;LibraryRepository r,other;QVERIFY(r.open(&error,path));QVERIFY(other.open(&error,path));QVERIFY(file(dir.path(),"media/live.png"));const int id=r.registerMediaAssets({{MediaKind::Image,"media/live.png","Live",false}},&error)[0];QVERIFY(age(path));CleanupProtection p;p.assets.insert(id);p.paths.insert("media/live.png");QVERIFY(other.replaceCleanupLease("other-session",p,&error));auto candidate=item(r.cleanupCandidates(90),CleanupKind::MediaAsset,id);QVERIFY(!candidate.eligible);QVERIFY(candidate.reason.contains("snapshot"));QVERIFY(r.deleteMediaAsset(id,&error));candidate=item(r.cleanupCandidates(90),CleanupKind::File,0,"media/live.png");QVERIFY(!candidate.eligible);QVERIFY(candidate.reason.contains("snapshot"));QVERIFY(sql(path,"UPDATE cleanup_leases SET expires_at=0 WHERE owner='other-session'"));QVERIFY(!item(r.cleanupCandidates(90),CleanupKind::File,0,"media/live.png").eligible);other.clearCleanupLease("other-session");QVERIFY(item(r.cleanupCandidates(90),CleanupKind::File,0,"media/live.png").eligible);QVERIFY(!item(r.cleanupCandidates(90,p),CleanupKind::File,0,"media/live.png").eligible);
 }
 void orphanUnknownDatesUnsafePathsAndSavedLookReferencesStayProtected(){
    QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString error;LibraryRepository r;QVERIFY(r.open(&error,path));QVERIFY(file(dir.path(),"media/orphan.png"));auto candidate=item(r.cleanupCandidates(90),CleanupKind::File,0,"media/orphan.png");QVERIFY(!candidate.eligible);QVERIFY(candidate.reason.contains("Unknown"));QVERIFY(r.recordUsage(0,{}, {"media/orphan.png"},&error));QVERIFY(age(path));candidate=item(r.cleanupCandidates(90),CleanupKind::File,0,"media/orphan.png");QVERIFY(candidate.eligible);QVERIFY(!r.recordUsage(0,{}, {"media/../../outside"},&error));
    QVERIFY(file(dir.path(),"backgrounds/look.png"));QVERIFY(r.recordUsage(0,{}, {"backgrounds/look.png"},&error));PresentationLook look;look.background=PresentationLook::Background::Image;look.backgroundPath="backgrounds/look.png";QVERIFY(r.createLook("Saved look",look,&error));QVERIFY(age(path));candidate=item(r.cleanupCandidates(90),CleanupKind::File,0,"backgrounds/look.png");QVERIFY(!candidate.eligible);QVERIFY(candidate.reason.contains("Saved look"));
    QTemporaryDir outside;QVERIFY(file(outside.path(),"outside.png"));QVERIFY(QFile::link(outside.filePath("outside.png"),dir.filePath("media/link.png")));QVERIFY(r.recordUsage(0,{}, {"media/link.png"},&error));QVERIFY(age(path));candidate=item(r.cleanupCandidates(90),CleanupKind::File,0,"media/link.png");QVERIFY(!candidate.eligible);QVERIFY(candidate.reason.contains("symlink"));QVERIFY(QFileInfo::exists(outside.filePath("outside.png")));
 }
 void databaseFailureAfterStagingRestoresFilesAndWholeBatch(){
    QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString error;LibraryRepository r;QVERIFY(r.open(&error,path));QVERIFY(file(dir.path(),"media/rollback.png"));const int id=r.registerMediaAssets({{MediaKind::Image,"media/rollback.png","Rollback",false}},&error)[0];QVERIFY(age(path));const auto batch=r.archiveCleanup({{CleanupKind::MediaAsset,id,{}}},90,{},&error);QVERIFY(!batch.isEmpty());const auto plan=r.previewCleanupPurge(batch);QVERIFY(sql(path,"CREATE TRIGGER fail_cleanup_delete BEFORE DELETE ON media_assets BEGIN SELECT RAISE(ABORT,'Injected delete failure'); END"));QVERIFY(!r.purgeCleanup(batch,plan,{},&error));QVERIFY(QFileInfo::exists(dir.filePath("media/rollback.png")));QVERIFY(!QFileInfo::exists(staged(dir.path(),batch,"media/rollback.png")));QCOMPARE(r.cleanupBatches()[0].state,QString("archived"));QVERIFY(r.restoreCleanup(batch,&error));QCOMPARE(r.mediaAsset(id).id,id);
 }
 void reopeningRecoversInterruptedStagingAndFinishesOnlyCommittedDeletion(){
    for(bool committed:{false,true}){QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString error;QString batch,target;int id;
        {LibraryRepository r;QVERIFY(r.open(&error,path));QVERIFY(file(dir.path(),"media/interrupted.png"));id=r.registerMediaAssets({{MediaKind::Image,"media/interrupted.png","Interrupted",false}},&error)[0];QVERIFY(age(path));batch=r.archiveCleanup({{CleanupKind::MediaAsset,id,{}}},90,{},&error);auto plan=r.previewCleanupPurge(batch);QCOMPARE(plan.files.size(),1);const auto f=plan.files[0];QVERIFY(sql(path,"UPDATE cleanup_batches SET state='purging' WHERE id='"+batch+"'"));QVERIFY(sql(path,"INSERT INTO cleanup_files VALUES('"+batch+"','media/interrupted.png',"+QString::number(f.bytes)+","+QString::number(f.modifiedMs)+")"));target=staged(dir.path(),batch,f.path);QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));QVERIFY(QFile::rename(dir.filePath(f.path),target));if(committed){QVERIFY(sql(path,"PRAGMA foreign_keys=OFF"));QVERIFY(sql(path,"DELETE FROM content_usage WHERE asset_id="+QString::number(id)));QVERIFY(sql(path,"DELETE FROM media_assets WHERE id="+QString::number(id)));QVERIFY(sql(path,"UPDATE cleanup_batches SET state='purged' WHERE id='"+batch+"'"));}}
        const auto unrelated=QFileInfo(target).dir().filePath("unrelated.bin");QFile untouched(unrelated);QVERIFY(untouched.open(QIODevice::WriteOnly));untouched.write("keep");untouched.close();LibraryRepository reopened;QVERIFY2(reopened.open(&error,path),qPrintable(error));QCOMPARE(QFileInfo::exists(dir.filePath("media/interrupted.png")),!committed);QVERIFY(!QFileInfo::exists(target));QVERIFY(QFileInfo::exists(unrelated));if(!committed){QVERIFY(reopened.restoreCleanup(batch,&error));QCOMPARE(reopened.mediaAsset(id).id,id);}
    }
 }
};
QTEST_GUILESS_MAIN(LibraryCleanupTest)
#include "library_cleanup_test.moc"
