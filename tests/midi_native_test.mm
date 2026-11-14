#include <QtTest>
#include <QSignalSpy>
#include <QUuid>
#include <QMutex>
#include <QMutexLocker>
#include <CoreMIDI/CoreMIDI.h>
#include "../src/midi/MidiBackend.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
class MidiNativeTest:public QObject {
 Q_OBJECT
private slots:
 void isolatedCoreMidiInputAndOutputRoundTrip(){
  const auto label="WorshipCue Test "+QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto utf=label.toUtf8();CFStringRef name=CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8*>(utf.constData()),utf.size(),kCFStringEncodingUTF8,false);
  MIDIClientRef client=0;QCOMPARE(MIDIClientCreate(name,nullptr,nullptr,&client),OSStatus(noErr));
  auto backend=nativeMidiBackend(label);QString error;QVERIFY2(backend->setVirtualPorts(true,&error),qPrintable(error));
  auto ports=backend->ports(&error);QVERIFY(error.isEmpty());QVERIFY(backend->openInput("virtual:in",42,&error));QSignalSpy received(backend.get(),&MidiBackend::packetReceived);
  MIDIEndpointRef ownInput=0;
  for(ItemCount i=0;i<MIDIGetNumberOfDestinations();++i){auto endpoint=MIDIGetDestination(i);CFStringRef endpointName=nullptr;MIDIObjectGetStringProperty(endpoint,kMIDIPropertyName,&endpointName);if(endpointName){QString text=QString::fromCFString(endpointName);if(text==label+" Input")ownInput=endpoint;CFRelease(endpointName);}}
  QVERIFY(ownInput);MIDIPortRef output=0;QCOMPARE(MIDIOutputPortCreate(client,name,&output),OSStatus(noErr));
  MIDIPacketList packet;auto *next=MIDIPacketListInit(&packet);const Byte note[]{0x92,60,100};QVERIFY(MIDIPacketListAdd(&packet,sizeof(packet),next,0,3,note));QCOMPARE(MIDISend(output,ownInput,&packet),OSStatus(noErr));QTRY_COMPARE(received.count(),1);QCOMPARE(received.at(0).at(0).toString(),QString("virtual:in"));QCOMPARE(received.at(0).at(1).toByteArray(),QByteArray::fromHex("923c64"));QCOMPARE(received.at(0).at(2).toULongLong(),quint64(42));
  // Native output goes only to this test-owned destination, never to hardware.
  struct State { QMutex mutex; QByteArray bytes; };auto state=std::make_shared<State>();MIDIEndpointRef destination=0;
  QCOMPARE(MIDIDestinationCreateWithBlock(client,name,&destination,^(const MIDIPacketList *list,void *){QMutexLocker lock(&state->mutex);const MIDIPacket *p=&list->packet[0];for(UInt32 i=0;i<list->numPackets;++i){state->bytes.append(reinterpret_cast<const char*>(p->data),p->length);p=MIDIPacketNext(p);}}),OSStatus(noErr));
  MIDIUniqueID uid=0;QCOMPARE(MIDIObjectGetIntegerProperty(destination,kMIDIPropertyUniqueID,&uid),OSStatus(noErr));auto id="coremidi:out:"+QString::number(uid);ports=backend->ports(&error);bool found=false;for(const auto &p:ports)if(p.id==id)found=true;QVERIFY(found);QVERIFY2(backend->openOutput(id,&error),qPrintable(error));QVERIFY2(backend->send(id,QByteArray::fromHex("b2097f"),&error),qPrintable(error));
  auto bytes=[&]{QMutexLocker lock(&state->mutex);return state->bytes;};QTRY_COMPARE(bytes(),QByteArray::fromHex("b2097f"));
  backend->closeInput("virtual:in");QCOMPARE(MIDISend(output,ownInput,&packet),OSStatus(noErr));QTest::qWait(30);QCOMPARE(received.count(),1);
  backend->closeOutput(id);QVERIFY(backend->setVirtualPorts(false,&error));backend.reset();MIDIPortDispose(output);MIDIEndpointDispose(destination);MIDIClientDispose(client);CFRelease(name);
 }
};
QTEST_GUILESS_MAIN(MidiNativeTest)
#include "midi_native_test.moc"
#pragma clang diagnostic pop
