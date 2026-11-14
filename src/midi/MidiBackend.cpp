#include "MidiBackend.h"
#include <QMutexLocker>
#include <QPointer>
MidiBackend::MidiBackend(QObject *parent) : QObject(parent), gate_(std::make_shared<ReceiveGate>()) { gate_->receiver = this; }
MidiBackend::~MidiBackend() { QMutexLocker lock(&gate_->mutex); gate_->receiver = nullptr; gate_->pending.clear(); }
bool MidiBackend::setVirtualPorts(bool enabled, QString *error) {
    if (!enabled) return true;
    if (error) *error = "This backend uses OS-provided virtual ports. Create them in the operating system first."; return false;
}
namespace {
void drain(const std::shared_ptr<MidiBackend::ReceiveGate> &gate) {
    QVector<MidiBackend::ReceiveGate::Packet> packets; bool overflow; MidiBackend *receiver;
    {
        QMutexLocker lock(&gate->mutex); receiver=gate->receiver;
        if(!receiver){gate->pending.clear();gate->scheduled=false;return;}
        int bytes=0,count=0;
        while(count<gate->pending.size()&&count<16&&bytes+gate->pending.at(count).bytes.size()<=8192){bytes+=gate->pending.at(count).bytes.size();++count;}
        packets=gate->pending.mid(0,count);gate->pending.remove(0,count);
        overflow=gate->overflow;gate->overflow=false;gate->scheduled=!gate->pending.isEmpty();
        if(gate->scheduled)QMetaObject::invokeMethod(receiver,[gate]{drain(gate);},Qt::QueuedConnection);
    }
    QPointer<MidiBackend> alive(receiver);
    if(overflow)emit receiver->inputOverflow();
    for(const auto &packet:packets) {
        if (!alive) break;
        emit alive->packetReceived(packet.source,packet.bytes,packet.generation);
    }
}
}
void MidiBackend::deliver(const std::shared_ptr<ReceiveGate> &gate, const QString &source, QByteArray bytes, quint64 generation) {
    if(!generation||bytes.isEmpty())return;
    QMutexLocker lock(&gate->mutex);if(!gate->receiver)return;
    if(bytes.size()>4096||gate->pending.size()>=256)gate->overflow=true;
    else gate->pending.append({source,std::move(bytes),generation});
    if(gate->scheduled)return;gate->scheduled=true;
    QMetaObject::invokeMethod(gate->receiver,[gate]{drain(gate);},Qt::QueuedConnection);
}
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
class UnsupportedMidi final : public MidiBackend {
public:
    QVector<MidiPort> ports(QString *error) override { if (error) *error = setupHelp(); return {}; }
    bool openInput(const QString &, quint64, QString *error) override { if (error) *error = setupHelp(); return false; }
    void closeInput(const QString &) override {}
    bool openOutput(const QString &, QString *error) override { if (error) *error = setupHelp(); return false; }
    void closeOutput(const QString &) override {}
    bool send(const QString &, const QByteArray &, QString *error) override { if (error) *error = setupHelp(); return false; }
    QString setupHelp() const override { return "Native MIDI is implemented for macOS CoreMIDI and Windows MIDI devices."; }
};
std::unique_ptr<MidiBackend> nativeMidiBackend(const QString &) { return std::make_unique<UnsupportedMidi>(); }
#endif
