#pragma once
#include "../../src/midi/MidiBackend.h"
#include <QHash>
#include <QSet>
class FakeMidiBackend : public MidiBackend {
public:
    QVector<MidiPort> devices{{"in-a","Pad",true,true,{}},{"out-a","Lights",false,true,{}}};
    QHash<QString,quint64> inputs; QSet<QString> outputs;
    QVector<QPair<QString,QByteArray>> sent;
    bool failSend=false,failOpen=false,virtualSupported=true,virtualEnabled=false;
    QVector<MidiPort> ports(QString *) override { return devices; }
    bool openInput(const QString &id,quint64 gen,QString *error) override {if(failOpen){if(error)*error="Unavailable input";return false;}inputs[id]=gen;return true;}
    void closeInput(const QString &id) override { inputs.remove(id); }
    bool openOutput(const QString &id,QString *error) override {if(failOpen){if(error)*error="Unavailable output";return false;}outputs.insert(id);return true;}
    void closeOutput(const QString &id) override {outputs.remove(id);}
    bool send(const QString &id,const QByteArray &bytes,QString *error) override {if(failSend){if(error)*error="Send failed";return false;}sent.append({id,bytes});return true;}
    bool supportsVirtualPorts() const override {return virtualSupported;}
    bool setVirtualPorts(bool enabled,QString *error) override {if(enabled&&!virtualSupported)return MidiBackend::setVirtualPorts(enabled,error);virtualEnabled=enabled;return true;}
    QString setupHelp() const override {return "Test backend";}
    void inject(const QByteArray &data,const QString &source="in-a",quint64 generation=0){deliver(receiveGate(),source,data,generation?generation:inputs.value(source));}
};
