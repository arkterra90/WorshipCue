#include "fixtures/SchemaDowngrade.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QUuid>
#include "../src/data/LibraryRepository.h"
namespace {
QVariant sql(const QString &path,const QString &statement){const auto name=QUuid::createUuid().toString();QVariant value;{auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);if(db.open()){QSqlQuery q(db);if(q.exec(statement)){value=true;if(q.next())value=q.value(0);}}db.close();}QSqlDatabase::removeDatabase(name);return value;}
MidiOutputAction action(int note=60){MidiOutputAction a;a.destination="destination";a.message.number=note;return a;}
MidiBinding mapping(int song=0,qint64 slide=0){MidiBinding b;b.name="Cue";if(song){b.command=MidiCommand::TakeSlide;b.songId=song;b.slideId=slide;}return b;}
}
class MidiRepositoryTest:public QObject {
 Q_OBJECT
private slots:
 void actionsPersistInOrderAndRejectStaleDrafts(){
  QTemporaryDir dir;auto path=dir.filePath("db.sqlite");QString error;qint64 page;
  {LibraryRepository r;QVERIFY(r.open(&error,path));auto id=r.createTextPresentation("MIDI",{"One","Two"},&error);page=r.song(id).slideIds[0];QVERIFY(r.setMidiActions(MidiOwnerType::Slide,page,{}, {action(60),action(61)},&error));QCOMPARE(r.song(id).midiActions[0],(QVector<MidiOutputAction>{action(60),action(61)}));QVERIFY(!r.setMidiActions(MidiOwnerType::Slide,page,{}, {action(62)},&error));QCOMPARE(r.midiActions(MidiOwnerType::Slide,page).size(),2);auto bad=action();bad.message.value=0;QVERIFY(!r.setMidiActions(MidiOwnerType::Slide,page,{action(60),action(61)},{bad},&error));}
  LibraryRepository r;QVERIFY(r.open(&error,path));QCOMPARE(r.midiActions(MidiOwnerType::Slide,page),(QVector<MidiOutputAction>{action(60),action(61)}));
 }
 void conflictsTargetsAndOptimisticUpdatesAreAtomic(){
  LibraryRepository r;QString error;QVERIFY(r.open(&error,":memory:"));const auto id=r.createTextPresentation("Target",{"One","Two"},&error);auto page=r.song(id).slideIds[1];auto b=mapping(id,page);QVERIFY(r.saveMidiBinding(b,&error));auto saved=r.midiBindings().last();QVERIFY(saved.id>0);auto overlap=mapping();overlap.trigger.source="specific";overlap.trigger.channel=3;overlap.trigger.value=90;QVERIFY(!r.saveMidiBinding(overlap,&error));QCOMPARE(r.midiBindings().size(),1);overlap.enabled=false;QVERIFY(r.saveMidiBinding(overlap,&error));auto stale=saved;saved.name="Updated";QVERIFY(r.saveMidiBinding(saved,&error,&stale));stale.name="Stale overwrite";QVERIFY(!r.saveMidiBinding(stale,&error,&stale));QCOMPARE(r.midiBinding(saved.id).name,QString("Updated"));b.trigger.number=61;b.slideId=999999;QVERIFY(!r.saveMidiBinding(b,&error));b.slideId=page;b.occurrence=1;QVERIFY(!r.saveMidiBinding(b,&error));QVERIFY(r.deleteSong(id,&error));QCOMPARE(r.midiBindings().size(),1);QCOMPARE(r.midiBinding(saved.id).id,0);
 }
 void arrangementReorderAndReflowRetainOnlyOriginalPageAssignments(){
  LibraryRepository r;QString error;QVERIFY(r.open(&error,":memory:"));SongStructure structure=SongStructure::fromSlides(2);structure.arrangements[0].sectionIds.append(structure.sections[0].id);const int id=r.createTextPresentation("Structured",{"A1A2","B"},&error,{}, {},{},structure);QVERIFY(id);auto original=r.song(id);const auto page=original.slideIds[0];QVERIFY(r.setMidiActions(MidiOwnerType::Slide,page,{}, {action()},&error));auto b=mapping(id,page);b.occurrence=1;QVERIFY(r.saveMidiBinding(b,&error));original=r.song(id);auto draft=original;draft.slides={"B","A1A2"};draft.richText={"",""};draft.structure.sections.move(0,1);QVERIFY2(r.updateSongStructure(original,draft,&error),qPrintable(error));auto moved=r.song(id);QCOMPARE(moved.slideIds[1],page);QCOMPARE(moved.midiActions[1],(QVector<MidiOutputAction>{action()}));QCOMPARE(r.midiBindings()[0].slideId,page);
  auto split=moved;split.slides={"B","A1","A2"};split.richText={"","",""};split.structure.sections[1].reflow({2});split.media.resize(3);split.appearance.slideLookIds.resize(3);split.appearance.richTextOverrides.resize(3);split.appearance.fitText.resize(3);QVERIFY2(r.updatePresentationDraft(moved,split,{0,1,1},&error),qPrintable(error));const auto after=r.song(id);QCOMPARE(after.slideIds[1],page);QCOMPARE(after.midiActions[1].size(),1);QVERIFY(after.midiActions[2].isEmpty());QCOMPARE(r.midiBindings()[0].slideId,page);
 }
 void directMediaDefaultsAndEntryCommandsSurviveServiceCopies(){
  LibraryRepository r;QString error;QVERIFY(r.open(&error,":memory:"));auto ids=r.registerMediaAssets({{MediaKind::Image,"media/test.png","Test",false}},&error);QCOMPARE(ids.size(),1);const int service=r.createService("MIDI",&error);const int entry=r.appendMediaToService(service,ids[0],&error);QVERIFY(entry);QVERIFY(r.setMidiActions(MidiOwnerType::Asset,ids[0],{},{action(60)},&error));QVERIFY(r.setMidiActions(MidiOwnerType::Entry,entry,{},{action(61)},&error));QCOMPARE(r.entrySource(entry).midiActions[0],(QVector<MidiOutputAction>{action(60),action(61)}));QVERIFY(r.duplicateServiceEntry(entry,&error));auto entries=r.service(service);QCOMPARE(entries.size(),2);QCOMPARE(r.midiActions(MidiOwnerType::Entry,entries[1].id),(QVector<MidiOutputAction>{action(61)}));const int copy=r.duplicateService(service,"Copy",&error);QVERIFY(copy);QCOMPARE(r.entrySource(r.service(copy)[0].id).midiActions[0].size(),2);
 }
 void serviceOccurrenceAndFlatEditsPreserveStableTargets(){
  LibraryRepository r;QString error;QVERIFY(r.open(&error,":memory:"));auto id=r.createTextPresentation("Flat",{"One","Two"},&error);auto page=r.song(id).slideIds[0];const int service=r.createService("Service",&error);QVERIFY(r.appendToService(service,id,&error));const int entry=r.service(service)[0].id;QVERIFY(r.setEntrySlideOrder(entry,{1,0,0},&error));auto b=mapping(id,page);b.entryId=entry;b.occurrence=1;QVERIFY(r.saveMidiBinding(b,&error));QVERIFY(r.setMidiActions(MidiOwnerType::Slide,page,{},{action()},&error));QVERIFY(r.updateSong(id,"Flat updated",{"Edited","Two edited"},&error));QCOMPARE(r.song(id).slideIds[0],page);QVERIFY(!r.updateSong(id,"Wrong count",{"Only one"},&error));QCOMPARE(r.song(id).slides.size(),2);QVERIFY(r.setEntrySlideOrder(entry,{1},&error));QVERIFY(!r.saveMidiBinding(b,&error));QVERIFY(r.removeFromService(entry,&error));QVERIFY(r.midiBindings().isEmpty());
 }
 void deletedHighestIdsAreNeverReused(){
  LibraryRepository r;QString error;QVERIFY(r.open(&error,":memory:"));auto id=r.createTextPresentation("Old",{"Old"},&error);auto page=r.song(id).slideIds[0];QVERIFY(r.deleteSong(id,&error));auto next=r.createTextPresentation("New",{"New"},&error);QVERIFY(next>id);QVERIFY(r.song(next).slideIds[0]>page);
 }
 void versionNineMigrationPreservesContentReferencesAndRollsBackOnConflict(){
  for(bool conflict:{false,true}){QTemporaryDir dir;auto path=dir.filePath("migration.sqlite");QString error;int id,service,look;qint64 page;
   {LibraryRepository r;QVERIFY(r.open(&error,path));id=r.createTextPresentation("Keep",{"Keep page"},&error);page=r.song(id).slideIds[0];service=r.createService("Keep service",&error);QVERIFY(r.appendToService(service,id,&error));look=r.createLook("Keep look",{},&error);QVERIFY(look);}
   QVERIFY(removeCleanupSchema(path));
   QVERIFY(sql(path,"DROP TABLE midi_bindings").isValid());QVERIFY(sql(path,"DROP TABLE midi_slide_actions").isValid());QVERIFY(sql(path,"PRAGMA user_version=9").isValid());if(conflict)QVERIFY(sql(path,"CREATE TABLE midi_bindings(blocker TEXT)").isValid());
   {LibraryRepository r;QCOMPARE(r.open(&error,path),!conflict);if(!conflict){QCOMPARE(r.song(id).slides,QStringList{"Keep page"});QCOMPARE(r.song(id).slideIds[0],page);QCOMPARE(r.service(service)[0].songId,id);QCOMPARE(r.look(look).id,look);QVERIFY(r.deleteSong(id,&error));QVERIFY(r.service(service).isEmpty());}}
   QCOMPARE(sql(path,"PRAGMA user_version").toInt(),conflict?9:11);if(conflict){QCOMPARE(sql(path,"SELECT title FROM songs WHERE id="+QString::number(id)).toString(),QString("Keep"));QCOMPARE(sql(path,"SELECT count(*) FROM sqlite_master WHERE name='songs_v10'").toInt(),0);}
  }
 }
};
QTEST_GUILESS_MAIN(MidiRepositoryTest)
#include "midi_repository_test.moc"
