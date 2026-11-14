#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MidiBackend.h"
#include <QCryptographicHash>
#include <QHash>
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
namespace {
QString identity(bool input,const QString &name,WORD manufacturer,WORD product,DWORD version){
    const auto text=QStringLiteral("%1|%2|%3|%4").arg(name).arg(manufacturer).arg(product).arg(version);
    return (input?QStringLiteral("winmm:in:"):QStringLiteral("winmm:out:"))+QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha256).toHex());
}
class WindowsMidiBackend final : public MidiBackend {
    struct Input {HMIDIIN handle=nullptr;QString id;quint64 generation=0;std::shared_ptr<ReceiveGate> gate;std::atomic<bool> active{true};};
    QHash<QString,UINT> inputIds_,outputIds_;
    QHash<QString,std::shared_ptr<Input>> inputs_;
    QHash<QString,HMIDIOUT> outputs_;
    static void CALLBACK read(HMIDIIN,UINT message,DWORD_PTR instance,DWORD_PTR data,DWORD_PTR){
        auto *input=reinterpret_cast<Input *>(instance);if(!input||!input->active.load()||message!=MIM_DATA)return;
        const auto status=quint8(data&0xff);const int kind=status&0xf0;
        if(kind!=0x80&&kind!=0x90&&kind!=0xb0)return;
        QByteArray bytes;bytes.append(char(status));bytes.append(char((data>>8)&0x7f));bytes.append(char((data>>16)&0x7f));
        MidiBackend::deliver(input->gate,input->id,std::move(bytes),input->generation);
    }
    bool failure(MMRESULT result,bool input,QString *error){
        if(result==MMSYSERR_NOERROR)return true;WCHAR message[MAXERRORLENGTH]{};
        if(input)midiInGetErrorTextW(result,message,MAXERRORLENGTH);else midiOutGetErrorTextW(result,message,MAXERRORLENGTH);
        if(error)*error=QStringLiteral("Windows MIDI: %1 (%2)").arg(QString::fromWCharArray(message)).arg(result);return false;
    }
public:
    ~WindowsMidiBackend() override {for(const auto &id:inputs_.keys())closeInput(id);for(const auto &id:outputs_.keys())closeOutput(id);}
    QString setupHelp() const override {return "Windows: hardware and OS-installed virtual/network MIDI ports are listed. Create loopMIDI ports or configure rtpMIDI sessions in their own software first. WinMM cannot create virtual ports itself. Identical driver identities are disabled rather than guessed by device index.";}
    QVector<MidiPort> ports(QString *error) override {
        if(error)error->clear();QVector<MidiPort> result;inputIds_.clear();outputIds_.clear();QHash<QString,int> counts;
        for(UINT index=0;index<midiInGetNumDevs();++index){MIDIINCAPSW caps{};if(midiInGetDevCapsW(index,&caps,sizeof(caps))!=MMSYSERR_NOERROR)continue;
            auto name=QString::fromWCharArray(caps.szPname),id=identity(true,name,caps.wMid,caps.wPid,caps.vDriverVersion);inputIds_[id]=index;++counts[id];result.append({id,name,true,true,"Windows MIDI input"});}
        for(UINT index=0;index<midiOutGetNumDevs();++index){MIDIOUTCAPSW caps{};if(midiOutGetDevCapsW(index,&caps,sizeof(caps))!=MMSYSERR_NOERROR)continue;
            auto name=QString::fromWCharArray(caps.szPname),id=identity(false,name,caps.wMid,caps.wPid,caps.vDriverVersion);outputIds_[id]=index;++counts[id];result.append({id,name,false,true,"Windows MIDI output"});}
        for(auto &port:result)if(counts.value(port.id)>1){port.available=false;port.detail="Ambiguous driver identity; rename or reconfigure the OS ports.";inputIds_.remove(port.id);outputIds_.remove(port.id);}
        return result;
    }
    bool openInput(const QString &id,quint64 generation,QString *error) override {
        if(error)error->clear();if(inputs_.contains(id))return true;
        if(!inputIds_.contains(id)){if(error)*error="MIDI input is unavailable or ambiguous.";return false;}
        auto input=std::make_shared<Input>();input->id=id;input->generation=generation;input->gate=receiveGate();
        auto status=midiInOpen(&input->handle,inputIds_.value(id),reinterpret_cast<DWORD_PTR>(&read),reinterpret_cast<DWORD_PTR>(input.get()),CALLBACK_FUNCTION);
        if(status==MMSYSERR_NOERROR)status=midiInStart(input->handle);
        if(!failure(status,true,error)){input->active=false;if(input->handle)midiInClose(input->handle);return false;}
        inputs_[id]=input;return true;
    }
    void closeInput(const QString &id) override {auto input=inputs_.take(id);if(!input)return;input->active=false;midiInStop(input->handle);midiInReset(input->handle);midiInClose(input->handle);}
    bool openOutput(const QString &id,QString *error) override {
        if(error)error->clear();if(outputs_.contains(id))return true;
        if(!outputIds_.contains(id)){if(error)*error="MIDI output is unavailable or ambiguous.";return false;}
        HMIDIOUT output=nullptr;if(!failure(midiOutOpen(&output,outputIds_.value(id),0,0,CALLBACK_NULL),false,error))return false;outputs_[id]=output;return true;
    }
    void closeOutput(const QString &id) override {auto output=outputs_.take(id);if(output)midiOutClose(output);}
    bool send(const QString &id,const QByteArray &bytes,QString *error) override {
        if(error)error->clear();if(!outputs_.contains(id)||bytes.size()!=3){if(error)*error="MIDI output is disconnected.";return false;}
        const DWORD data=quint8(bytes[0])|(DWORD(quint8(bytes[1]))<<8)|(DWORD(quint8(bytes[2]))<<16);
        return failure(midiOutShortMsg(outputs_.value(id),data),false,error);
    }
};
}
std::unique_ptr<MidiBackend> nativeMidiBackend(const QString &){return std::make_unique<WindowsMidiBackend>();}
