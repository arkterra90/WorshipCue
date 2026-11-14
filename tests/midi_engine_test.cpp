#include <QtTest>
#include <QSettings>
#include <QTemporaryDir>
#include "../src/midi/MidiEngine.h"
#include "fixtures/FakeMidiBackend.h"
class MidiEngineTest : public QObject {
    Q_OBJECT
    static MidiSettings settings(){MidiSettings s;s.enabled=true;s.autoReconnect=false;s.echoWindowMs=0;s.minimumIntervalMs=0;s.inputs={{"in-a",true,0}};s.outputs={{"out-a",true,3}};return s;}
    static MidiBinding binding(qint64 id=1,int note=60){MidiBinding b;b.id=id;b.name="Next";b.trigger.number=note;return b;}
    static MidiOutputAction action(){MidiOutputAction a;a.destination="out-a";return a;}
    static void flush(){QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();}
private slots:
    void initTestCase(){QCoreApplication::setOrganizationName("WorshipCueTests");QCoreApplication::setApplicationName("MidiEngine");}
    void decoderHandlesPacketBoundariesRunningStatusAndSystemData(){
        MidiStreamDecoder d;QVERIFY(d.feed(QByteArray::fromHex("923c")).isEmpty());auto m=d.feed(QByteArray::fromHex("f87f3d00b20a40823c05"));QCOMPARE(m.size(),4);QCOMPARE(m[0].channel,3);QCOMPARE(m[0].type,MidiMessageType::NoteOn);QCOMPARE(m[1].type,MidiMessageType::NoteOff);QCOMPARE(m[2].type,MidiMessageType::ControlChange);QCOMPARE(m[3].value,5);
        QVERIFY(d.feed(QByteArray::fromHex("f00102f803f7f20102c101e10040")).isEmpty());m=d.feed(QByteArray::fromHex("903c7f"));QCOMPARE(m.size(),1);
    }
    void validationRejectsAmbiguousMappingsAndInvalidBytes(){
        QString error;auto a=binding(),b=binding(2);b.trigger.source="in-a";b.trigger.channel=1;b.trigger.value=100;QVERIFY(!validateMidiBindings({a,b},&error));QVERIFY(error.contains("conflict",Qt::CaseInsensitive));b.enabled=false;QVERIFY(validateMidiBindings({a,b},&error));b.enabled=true;b.trigger.number=61;QVERIFY(validateMidiBindings({a,b},&error));a.trigger.value=0;QVERIFY(!a.valid());auto out=action();out.message.channel=17;QVERIFY(!out.valid());MidiBinding parsed;auto json=b.toJson();QVERIFY(MidiBinding::fromJson(json,&parsed));json["slide"]=1.5;QVERIFY(!MidiBinding::fromJson(json,&parsed));
    }
    void cueCommandsFireOnceAndReleaseOwnedNotes(){
        auto backend=std::make_unique<FakeMidiBackend>();auto *fake=backend.get();MidiEngine e(nullptr,std::move(backend));QString error;QVERIFY(e.setSettings(settings(),&error,false));
        e.activateCue(1,{action(),action()});QCOMPARE(fake->sent.size(),2);e.activateCue(1,{action()});QCOMPARE(fake->sent.size(),2);e.activateCue(2,{action()});QCOMPARE(fake->sent.size(),3);e.releaseNotes();QCOMPARE(fake->sent.size(),4);QCOMPARE(fake->sent.last().second,QByteArray::fromHex("803c00"));e.releaseNotes();QCOMPARE(fake->sent.size(),4);
        e.activateCue(3,{action()});auto s=settings();s.enabled=false;QVERIFY(e.setSettings(s,&error,false));QCOMPARE(fake->sent.last().second,QByteArray::fromHex("803c00"));QVERIFY(fake->outputs.isEmpty());e.activateCue(4,{action()});auto count=fake->sent.size();QVERIFY(e.setSettings(settings(),&error,false));e.activateCue(4,{action()});QCOMPARE(fake->sent.size(),count);
    }
    void inputEdgesChannelFiltersAndModalGate(){
        auto backend=std::make_unique<FakeMidiBackend>();auto *f=backend.get();MidiEngine e(nullptr,std::move(backend));QString error;auto s=settings();s.inputs[0].channel=2;QVERIFY(e.setSettings(s,&error,false));QVERIFY(e.setBindings({binding()},&error));QSignalSpy command(&e,&MidiEngine::commandRequested);QSignalSpy monitor(&e,&MidiEngine::messageReceived);
        f->inject(QByteArray::fromHex("903c7f913c7f913c7f"));flush();QCOMPARE(command.count(),1);QCOMPARE(monitor.count(),3);
        f->inject(QByteArray::fromHex("813c00913c70"));flush();QCOMPARE(command.count(),2);
        bool gate=false;e.setDispatchGate([&]{return gate;});f->inject(QByteArray::fromHex("813c00913c7f"));flush();QCOMPARE(command.count(),2);gate=true;f->inject(QByteArray::fromHex("913c7f"));flush();QCOMPARE(command.count(),2);f->inject(QByteArray::fromHex("813c00913c7f"));flush();QCOMPARE(command.count(),3);
    }
    void controllerChangesRearmAndDebounceApplies(){
        auto backend=std::make_unique<FakeMidiBackend>();auto *f=backend.get();MidiEngine e(nullptr,std::move(backend));QString error;auto s=settings();s.minimumIntervalMs=50;QVERIFY(e.setSettings(s,&error,false));auto b=binding();b.trigger.type=MidiMessageType::ControlChange;b.trigger.number=10;b.trigger.value=127;QVERIFY(e.setBindings({b},&error));QSignalSpy command(&e,&MidiEngine::commandRequested);
        f->inject(QByteArray::fromHex("b00a7fb00a7fb00a00b00a7f"));flush();QCOMPARE(command.count(),1);QTest::qWait(60);f->inject(QByteArray::fromHex("b00a00b00a7f"));flush();QCOMPARE(command.count(),2);
    }
    void feedbackSuppressionCoversReturnedNoteRelease(){
        auto backend=std::make_unique<FakeMidiBackend>();auto *f=backend.get();MidiEngine e(nullptr,std::move(backend));QString error;auto s=settings();s.echoWindowMs=30;QVERIFY(e.setSettings(s,&error,false));auto off=binding(2);off.trigger.type=MidiMessageType::NoteOff;QVERIFY(e.setBindings({binding(),off},&error));QSignalSpy commands(&e,&MidiEngine::commandRequested);QVERIFY(e.sendAction(action(),&error));f->inject(QByteArray::fromHex("903c60803c00"));flush();QCOMPARE(commands.count(),0);QTest::qWait(40);f->inject(QByteArray::fromHex("903c7f803c00"));flush();QCOMPARE(commands.count(),2);
    }
    void reconnectUsesStableIdentityAndRejectsOldGeneration(){
        auto backend=std::make_unique<FakeMidiBackend>();auto *f=backend.get();MidiEngine e(nullptr,std::move(backend));QString error;QVERIFY(e.setSettings(settings(),&error,false));QVERIFY(e.setBindings({binding()},&error));QSignalSpy commands(&e,&MidiEngine::commandRequested);auto old=f->inputs.value("in-a");f->devices.clear();e.refreshPorts();f->inject(QByteArray::fromHex("903c7f"),"in-a",old);flush();QCOMPARE(commands.count(),0);f->devices={{"other","Same display name",true,true,{}},{"in-a","Renamed pad",true,true,{}},{"out-a","Lights",false,true,{}}};e.refreshPorts();QVERIFY(f->inputs.contains("in-a"));QVERIFY(!f->inputs.contains("other"));QVERIFY(f->inputs.value("in-a")!=old);f->inject(QByteArray::fromHex("903c7f"),"in-a",old);flush();QCOMPARE(commands.count(),0);f->inject(QByteArray::fromHex("903c7f"));flush();QCOMPARE(commands.count(),1);
    }
    void failedOutputsReconnectAndSkippedCuesNeverReplay(){
        auto backend=std::make_unique<FakeMidiBackend>();auto *f=backend.get();MidiEngine e(nullptr,std::move(backend));QString error;QVERIFY(e.setSettings(settings(),&error,false));f->failSend=true;e.activateCue(1,{action()});QVERIFY(f->outputs.isEmpty());f->failSend=false;e.refreshPorts();e.activateCue(1,{action()});QVERIFY(f->sent.isEmpty());e.activateCue(2,{action()});QCOMPARE(f->sent.size(),1);auto s=settings();s.outputs[0].enabled=false;QVERIFY(e.setSettings(s,&error,false));QCOMPARE(f->sent.last().second,QByteArray::fromHex("803c00"));
    }
    void settingsPersistValidateAndUnsupportedVirtualFails(){
        QTemporaryDir dir;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());auto backend=std::make_unique<FakeMidiBackend>();auto *f=backend.get();MidiEngine e(nullptr,std::move(backend));QString error;auto s=settings();s.virtualPorts=true;QVERIFY(e.setSettings(s,&error));QVERIFY(f->virtualEnabled);e.shutdown();e.restoreSettings();QVERIFY(e.settings().enabled);QCOMPARE(e.outputChannel("out-a"),3);s.outputs.append(s.outputs[0]);QVERIFY(!e.setSettings(s,&error,false));f->virtualSupported=false;s=settings();s.virtualPorts=true;QVERIFY(!e.setSettings(s,&error,false));QSettings().clear();
    }
    void packetQueueAndActivityAreBounded(){
        auto backend=std::make_unique<FakeMidiBackend>();auto *f=backend.get();MidiEngine e(nullptr,std::move(backend));QString error;QVERIFY(e.setSettings(settings(),&error,false));QSignalSpy overflow(f,&MidiBackend::inputOverflow);for(int i=0;i<1000;++i)f->inject(QByteArray::fromHex("903c7f803c00"));QTRY_VERIFY(overflow.count()>0);QTRY_COMPARE(e.activity().size(),500);QVERIFY(e.setBindings({},&error));auto unsaved=binding();unsaved.id=0;QVERIFY(!e.setBindings({unsaved},&error));
    }
};
QTEST_GUILESS_MAIN(MidiEngineTest)
#include "midi_engine_test.moc"
