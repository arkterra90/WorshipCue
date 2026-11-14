#include "MidiBackend.h"
#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <atomic>
#import <CoreMIDI/CoreMIDI.h>
// MIDI 1.0 packet APIs retain compatibility with the application's older macOS
// deployment targets. Incoming data is decoded and bounded by the shared engine.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
namespace {
QString property(MIDIObjectRef object, CFStringRef key) {
    CFStringRef text=nullptr;
    if(MIDIObjectGetStringProperty(object,key,&text)!=noErr||!text)return {};
    QString result; result.resize(CFStringGetLength(text));
    CFStringGetCharacters(text,CFRangeMake(0,result.size()),reinterpret_cast<UniChar *>(result.data())); CFRelease(text); return result;
}
QString endpointName(MIDIEndpointRef endpoint) { auto name=property(endpoint,kMIDIPropertyDisplayName);return name.isEmpty()?property(endpoint,kMIDIPropertyName):name; }
QString endpointId(MIDIEndpointRef endpoint,bool input) {
    SInt32 id=0; MIDIObjectGetIntegerProperty(endpoint,kMIDIPropertyUniqueID,&id);
    const auto identity=id?QString::number(id):QString::fromLatin1(QCryptographicHash::hash(endpointName(endpoint).toUtf8(),QCryptographicHash::Sha256).toHex());
    return (input?QStringLiteral("coremidi:in:"):QStringLiteral("coremidi:out:"))+identity;
}
bool online(MIDIEndpointRef endpoint) {
    SInt32 offline=0; MIDIObjectGetIntegerProperty(endpoint,kMIDIPropertyOffline,&offline);
    MIDIEntityRef entity=0; MIDIDeviceRef device=0;
    if(!offline&&MIDIEndpointGetEntity(endpoint,&entity)==noErr&&entity&&MIDIEntityGetDevice(entity,&device)==noErr&&device)
        MIDIObjectGetIntegerProperty(device,kMIDIPropertyOffline,&offline);
    return !offline;
}
void packets(const std::shared_ptr<MidiBackend::ReceiveGate> &gate,const QString &id,quint64 generation,const MIDIPacketList *list) {
    if(!generation)return;
    const MIDIPacket *packet=&list->packet[0];
    for(UInt32 i=0;i<list->numPackets;++i){MidiBackend::deliver(gate,id,QByteArray(reinterpret_cast<const char *>(packet->data),packet->length),generation);packet=MIDIPacketNext(packet);}
}
class CoreMidiBackend final : public MidiBackend {
    QString name_;
    MIDIClientRef client_=0;
    MIDIPortRef output_=0;
    QHash<QString,MIDIPortRef> inputs_;
    QHash<QString,MIDIEndpointRef> endpoints_;
    QSet<QString> outputs_;
    MIDIEndpointRef virtualIn_=0,virtualOut_=0;
    std::shared_ptr<std::atomic<quint64>> virtualGeneration_=std::make_shared<std::atomic<quint64>>(0);
    bool client(QString *error) {
        if(client_)return true;
        const auto text=CFStringCreateWithCharacters(nullptr,reinterpret_cast<const UniChar *>(name_.utf16()),name_.size());
        const auto result=MIDIClientCreate(text,nullptr,nullptr,&client_);CFRelease(text);
        if(result==noErr)return true;if(error)*error=QStringLiteral("CoreMIDI client is unavailable (error %1).").arg(result);return false;
    }
    bool failure(OSStatus status,QString *error,const QString &operation){if(status==noErr)return true;if(error)*error=QStringLiteral("CoreMIDI %1 failed (error %2).").arg(operation).arg(status);return false;}
public:
    explicit CoreMidiBackend(QString name):name_(std::move(name)){}
    ~CoreMidiBackend() override {
        for(const auto &id:inputs_.keys())closeInput(id);
        QString ignored;setVirtualPorts(false,&ignored);
        if(output_)MIDIPortDispose(output_);if(client_)MIDIClientDispose(client_);
    }
    QString setupHelp() const override {return "macOS: enable IAC ports or network sessions in Audio MIDI Setup → MIDI Studio. Configured CoreMIDI hardware, IAC, and network endpoints appear here. WorshipCue can also publish its own virtual input/output.";}
    bool supportsVirtualPorts() const override {return true;}
    QVector<MidiPort> ports(QString *error) override {
        if(error)error->clear();QVector<MidiPort> result;if(!client(error))return result;
        endpoints_.clear();QHash<QString,int> counts;
        for(bool input:{true,false}){
            const auto count=input?MIDIGetNumberOfSources():MIDIGetNumberOfDestinations();
            for(ItemCount i=0;i<count;++i){auto endpoint=input?MIDIGetSource(i):MIDIGetDestination(i);if(endpoint==virtualIn_||endpoint==virtualOut_)continue;
                const auto id=endpointId(endpoint,input);const bool available=online(endpoint);
                result.append({id,endpointName(endpoint),input,available,available?QStringLiteral("CoreMIDI endpoint"):QStringLiteral("Device is offline")});endpoints_[id]=endpoint;++counts[id];}
        }
        for(auto &port:result)if(counts.value(port.id)>1){port.available=false;port.detail="Ambiguous endpoint identity; reconnect or rename the OS ports.";endpoints_.remove(port.id);}
        result.append({"virtual:in",name_+" Input (virtual)",true,virtualIn_!=0,"Enable WorshipCue virtual ports to publish this input."});
        result.append({"virtual:out",name_+" Output (virtual)",false,virtualOut_!=0,"Enable WorshipCue virtual ports to publish this output."});
        return result;
    }
    bool setVirtualPorts(bool enabled,QString *error) override {
        if(error)error->clear();
        if(!enabled){virtualGeneration_->store(0);if(virtualIn_)MIDIEndpointDispose(virtualIn_);if(virtualOut_)MIDIEndpointDispose(virtualOut_);virtualIn_=virtualOut_=0;outputs_.remove("virtual:out");return true;}
        if(virtualIn_&&virtualOut_)return true;if(!client(error))return false;
        const auto gate=receiveGate();const auto generation=virtualGeneration_;
        const auto inputName=name_+" Input",outputName=name_+" Output";
        auto inText=CFStringCreateWithCharacters(nullptr,reinterpret_cast<const UniChar *>(inputName.utf16()),inputName.size());
        auto outText=CFStringCreateWithCharacters(nullptr,reinterpret_cast<const UniChar *>(outputName.utf16()),outputName.size());
        OSStatus status=MIDIDestinationCreateWithBlock(client_,inText,&virtualIn_,^(const MIDIPacketList *list,void *){packets(gate,"virtual:in",generation->load(),list);});
        if(status==noErr)status=MIDISourceCreate(client_,outText,&virtualOut_);CFRelease(inText);CFRelease(outText);
        if(!failure(status,error,"virtual port creation")){QString ignored;setVirtualPorts(false,&ignored);return false;}return true;
    }
    bool openInput(const QString &id,quint64 generation,QString *error) override {
        if(error)error->clear();
        if(id=="virtual:in"){if(!virtualIn_){if(error)*error="Enable the virtual input first.";return false;}virtualGeneration_->store(generation);return true;}
        const auto endpoint=endpoints_.value(id);if(!endpoint||!online(endpoint)){if(error)*error="MIDI input is disconnected.";return false;}
        if(inputs_.contains(id))return true;
        const auto gate=receiveGate();MIDIPortRef port=0;
        auto status=MIDIInputPortCreateWithBlock(client_,CFSTR("WorshipCue input connection"),&port,^(const MIDIPacketList *list,void *){packets(gate,id,generation,list);});
        if(status==noErr)status=MIDIPortConnectSource(port,endpoint,nullptr);
        if(!failure(status,error,"input connection")){if(port)MIDIPortDispose(port);return false;}inputs_[id]=port;return true;
    }
    void closeInput(const QString &id) override {if(id=="virtual:in"){virtualGeneration_->store(0);return;}const auto port=inputs_.take(id);if(port)MIDIPortDispose(port);}
    bool openOutput(const QString &id,QString *error) override {
        if(error)error->clear();if(!client(error))return false;
        if(id=="virtual:out"?!virtualOut_:!endpoints_.contains(id)){if(error)*error="MIDI output is disconnected.";return false;}
        if(!output_&&!failure(MIDIOutputPortCreate(client_,CFSTR("WorshipCue output connection"),&output_),error,"output port"))return false;
        outputs_.insert(id);return true;
    }
    void closeOutput(const QString &id) override {outputs_.remove(id);}
    bool send(const QString &id,const QByteArray &bytes,QString *error) override {
        if(error)error->clear();if(!outputs_.contains(id)||bytes.size()!=3){if(error)*error="MIDI output is not connected.";return false;}
        MIDIPacketList list{};auto *packet=MIDIPacketListInit(&list);
        if(!MIDIPacketListAdd(&list,sizeof(list),packet,0,3,reinterpret_cast<const Byte *>(bytes.constData()))){if(error)*error="Invalid MIDI packet.";return false;}
        const auto endpoint=id=="virtual:out"?virtualOut_:endpoints_.value(id);
        if(!endpoint){if(error)*error="MIDI output disappeared.";return false;}
        return failure(id=="virtual:out"?MIDIReceived(endpoint,&list):MIDISend(output_,endpoint,&list),error,"send");
    }
};
}
std::unique_ptr<MidiBackend> nativeMidiBackend(const QString &name){return std::make_unique<CoreMidiBackend>(name);}
#pragma clang diagnostic pop
