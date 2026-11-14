#include "MidiEngine.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

namespace {
QString triggerKey(const QString &source,int channel,MidiMessageType type,int number,int value) {
    return source+QStringLiteral("|%1|%2|%3|%4").arg(channel).arg(int(type)).arg(number).arg(value);
}
}
bool MidiSettings::valid(QString *error) const {
    if (error) error->clear();
    const auto fail = [error](const QString &text) { if (error) *error = text; return false; };
    if (echoWindowMs < 0 || echoWindowMs > 5000 || minimumIntervalMs < 0 || minimumIntervalMs > 5000 || inputs.size() > 128 || outputs.size() > 128)
        return fail("Use echo/debounce windows of 0–5000 ms and at most 128 input/output devices each.");
    for (bool input : {true, false}) {
        QSet<QString> ids;
        for (const auto &device : input ? inputs : outputs) {
            if (device.id.isEmpty() || device.id.size() > 256 || device.id == "*" || ids.contains(device.id)
                || device.channel < (input ? 0 : 1) || device.channel > 16) return fail("Use unique device identities and channels 1–16 (or Any for input).");
            ids.insert(device.id);
        }
    }
    return true;
}
QJsonObject MidiSettings::toJson() const {
    const auto devices = [](const QVector<MidiDeviceSetting> &items) { QJsonArray array; for (const auto &d : items) array.append(QJsonObject{{"id",d.id},{"enabled",d.enabled},{"channel",d.channel}}); return array; };
    return {{"enabled",enabled},{"reconnect",autoReconnect},{"virtual",virtualPorts},{"echo",echoWindowMs},{"debounce",minimumIntervalMs},{"inputs",devices(inputs)},{"outputs",devices(outputs)}};
}
bool MidiSettings::fromJson(const QJsonObject &json, MidiSettings *result, QString *error) {
    const auto fail = [error] { if (error) *error = "Invalid saved MIDI device settings."; return false; };
    for (const auto &key : {"enabled","reconnect","virtual"}) if (!json[key].isBool()) return fail();
    for (const auto &key : {"echo","debounce"}) if (!json[key].isDouble() || json[key].toDouble() != json[key].toInt(-1)) return fail();
    MidiSettings parsed; parsed.enabled=json["enabled"].toBool(); parsed.autoReconnect=json["reconnect"].toBool(); parsed.virtualPorts=json["virtual"].toBool();
    parsed.echoWindowMs=json["echo"].toInt(); parsed.minimumIntervalMs=json["debounce"].toInt();
    for (bool input : {true,false}) {
        const auto value=json[input ? "inputs":"outputs"]; if (!value.isArray() || value.toArray().size()>128) return fail();
        for (const auto &item : value.toArray()) {
            const auto d=item.toObject(); if (!d["id"].isString() || !d["enabled"].isBool() || !d["channel"].isDouble() || d["channel"].toInt(-1)!=d["channel"].toDouble()) return fail();
            (input ? parsed.inputs:parsed.outputs).append({d["id"].toString(),d["enabled"].toBool(),d["channel"].toInt()});
        }
    }
    if (!parsed.valid(error)) return false; *result=parsed; return true;
}
MidiEngine::MidiEngine(QObject *parent, std::unique_ptr<MidiBackend> backend) : QObject(parent), backend_(backend ? std::move(backend) : nativeMidiBackend()) {
    setObjectName("midiEngine"); qRegisterMetaType<MidiMessage>(); qRegisterMetaType<MidiBinding>(); clock_.start();
    reconnect_.setInterval(1000); connect(&reconnect_, &QTimer::timeout, this, &MidiEngine::refreshPorts);
    connect(backend_.get(), &MidiBackend::packetReceived, this, &MidiEngine::receive);
    connect(backend_.get(), &MidiBackend::inputOverflow, this, [this] { decoders_.clear(); edgeStates_.clear(); record("Input overflow: data was dropped; input edge state reset."); });
}
MidiEngine::~MidiEngine() { shutdown(); }
void MidiEngine::record(const QString &text) {
    const auto line=QDateTime::currentDateTime().toString("hh:mm:ss.zzz ")+text.left(1500);
    activity_.append(line); while(activity_.size()>500) activity_.removeFirst(); emit activityAdded(line);
}
bool MidiEngine::setSettings(const MidiSettings &settings, QString *error, bool persist) {
    if (!settings.valid(error)) return false;
    if (settings.virtualPorts && !backend_->supportsVirtualPorts()) { if(error)*error=backend_->setupHelp(); return false; }
    if (!settings.enabled) {
        shutdown(); settings_=settings;
    } else {
        QString backendError;
        if(settings_.virtualPorts&&!settings.virtualPorts){releaseDestination("virtual:out");backend_->closeOutput("virtual:out");activeOutputs_.remove("virtual:out");}
        if (!backend_->setVirtualPorts(settings.virtualPorts, &backendError)) { if(error)*error=backendError; return false; }
        settings_=settings;
        refreshPorts(); if(settings.autoReconnect) reconnect_.start(); else reconnect_.stop();
    }
    if(persist) { QSettings prefs; prefs.setValue("midi/devices",QJsonDocument(settings_.toJson()).toJson(QJsonDocument::Compact)); prefs.sync();
        if(prefs.status()!=QSettings::NoError) record("Device settings applied, but preferences could not be saved."); }
    emit settingsChanged(); return true;
}
void MidiEngine::restoreSettings() {
    const auto data=QSettings().value("midi/devices").toByteArray(); MidiSettings settings; QString error;
    if(!data.isEmpty() && (data.size()>256*1024 || !MidiSettings::fromJson(QJsonDocument::fromJson(data).object(),&settings,&error))) record("Ignoring invalid MIDI preferences: "+error);
    if(!setSettings(settings,&error,false)) { record("MIDI setup failed: "+error); shutdown(); }
}
bool MidiEngine::setBindings(const QVector<MidiBinding> &bindings, QString *error) {
    bool saved=true; for(const auto &binding:bindings)if(binding.id<=0)saved=false;
    bindingIndex_.clear();
    if(!saved||!validateMidiBindings(bindings,error)) { if(!saved&&error)*error="Save MIDI mappings before enabling them."; bindings_.clear(); record("Input mappings disabled because validation failed."); return false; }
    bool unchanged = bindings.size() == bindings_.size();
    for (int i = 0; unchanged && i < bindings.size(); ++i)
        unchanged = bindings.at(i).id == bindings_.at(i).id && bindings.at(i).toJson() == bindings_.at(i).toJson();
    bindings_=bindings; if (!unchanged) lastTrigger_.clear();
    for(const auto &binding:bindings_)if(binding.enabled)bindingIndex_.insert(triggerKey(binding.trigger.source,binding.trigger.channel,binding.trigger.type,binding.trigger.number,binding.trigger.value),binding);
    return true;
}
void MidiEngine::refreshPorts() {
    QString error; const auto discovered=backend_->ports(&error);
    if(error!=lastDiscoveryError_) { if(!error.isEmpty())record("Discovery: "+error); lastDiscoveryError_=error; }
    if(discovered!=ports_) { ports_=discovered; emit portsChanged(); }
    QSet<QString> availableInputs,availableOutputs,desiredInputs,desiredOutputs;
    for(const auto &port:ports_) if(port.available) (port.input?availableInputs:availableOutputs).insert(port.id);
    if(settings_.enabled) {
        for(const auto &device:settings_.inputs) if(device.enabled) desiredInputs.insert(device.id);
        for(const auto &device:settings_.outputs) if(device.enabled) desiredOutputs.insert(device.id);
    }
    for(const auto &id:activeInputs_.keys()) if(!desiredInputs.contains(id)||!availableInputs.contains(id)) {
        backend_->closeInput(id); activeInputs_.remove(id); decoders_.remove(id);
        for(auto it=edgeStates_.begin();it!=edgeStates_.end();) if(it.key().startsWith(id+"|"))it=edgeStates_.erase(it);else++it;
        record("Input disconnected: "+id);
    }
    for(const auto &id:activeOutputs_.values()) if(!desiredOutputs.contains(id)||!availableOutputs.contains(id)) {
        releaseDestination(id); backend_->closeOutput(id); activeOutputs_.remove(id); record("Output disconnected: "+id);
    }
    for(const auto &id:desiredInputs) if(availableInputs.contains(id)&&!activeInputs_.contains(id)) {
        const auto generation=++connectionSerial_;
        if(backend_->openInput(id,generation,&error)) { activeInputs_.insert(id,generation); portErrors_.remove(id); record("Input connected: "+id); }
        else if(portErrors_.value(id)!=error) { portErrors_[id]=error;record("Input connection failed: "+error); }
    }
    for(const auto &id:desiredOutputs) if(availableOutputs.contains(id)&&!activeOutputs_.contains(id)) {
        if(backend_->openOutput(id,&error)) {activeOutputs_.insert(id);portErrors_.remove(id);record("Output connected: "+id);}
        else if(portErrors_.value(id)!=error){portErrors_[id]=error;record("Output connection failed: "+error);}
    }
    emit portsChanged();
}
QString MidiEngine::portStatus(const QString &id, bool input) const {
    if(portErrors_.contains(id))return portErrors_.value(id);
    if(input?activeInputs_.contains(id):activeOutputs_.contains(id))return "Connected";
    for(const auto &port:ports_)if(port.id==id&&port.input==input)return port.available?QStringLiteral("Disabled / not connected"):port.detail;
    return "Disconnected — saved identity retained";
}
int MidiEngine::outputChannel(const QString &id) const { for(const auto &d:settings_.outputs)if(d.id==id)return d.channel; return 1; }
void MidiEngine::receive(const QString &source, const QByteArray &data, quint64 generation) {
    if(!settings_.enabled||activeInputs_.value(source)!=generation||!generation)return;
    const auto messages=decoders_[source].feed(data);
    if(messages.size()>256){edgeStates_.clear();record("Input burst exceeded 256 messages per packet; excess messages ignored.");}
    for(int messageIndex=0;messageIndex<qMin(256,int(messages.size()));++messageIndex) {
        const auto message=messages.at(messageIndex);
        record("RX "+source+" · "+message.description()); emit messageReceived(source,message);
        const auto noteKey=source+"|"+QString::number(message.channel)+":"+QString::number(message.number)+(message.type==MidiMessageType::ControlChange?":cc":":note");
        const int edge=message.type==MidiMessageType::NoteOn?1:message.type==MidiMessageType::NoteOff?0:message.value;
        const bool duplicate=edgeStates_.contains(noteKey)&&edgeStates_.value(noteKey)==edge;
        edgeStates_[noteKey]=edge;
        bool channelAllowed=false;
        for(const auto &device:settings_.inputs)if(device.enabled&&device.id==source&&(!device.channel||device.channel==message.channel))channelAllowed=true;
        if(!channelAllowed){record("Ignored: input channel filter.");continue;}
        const auto now=clock_.elapsed(); const auto echo=recentOutput_.constFind(message.edgeKey());
        if(settings_.echoWindowMs>0&&echo!=recentOutput_.cend()&&now-echo.value()<=settings_.echoWindowMs){record("Ignored: possible MIDI feedback/echo.");continue;}
        if(duplicate){record("Ignored: held note or unchanged controller value.");continue;}
        if(dispatchGate_&&!dispatchGate_()){record("Ignored: MIDI input control is suspended in Edit mode or a dialog.");continue;}
        MidiBinding matched;
        for(const auto &input:QStringList{source,QStringLiteral("*")})for(int channel:{message.channel,0})for(int value:{message.value,-1}){
            const auto found=bindingIndex_.constFind(triggerKey(input,channel,message.type,message.number,value));
            if(found!=bindingIndex_.cend())matched=found.value();
        }
        if(matched.id){
            if(lastTrigger_.contains(matched.id)&&now-lastTrigger_.value(matched.id)<settings_.minimumIntervalMs){record("Ignored: mapping debounce.");continue;}
            lastTrigger_[matched.id]=now;record("Mapped: "+matched.name);emit commandRequested(matched);
        }
    }
}
bool MidiEngine::sendAction(const MidiOutputAction &action, QString *error) {
    if(error)error->clear();
    if(!settings_.enabled||!action.enabled||!action.valid()||!activeOutputs_.contains(action.destination)) {
        if(error)*error="MIDI output is disabled, disconnected, or the action is invalid."; return false;
    }
    const auto key=action.message.edgeKey(); const auto old=recentOutput_.value(key,-1); recentOutput_[key]=clock_.elapsed();
    if(!backend_->send(action.destination,action.message.bytes(),error)) {
        if(old<0)recentOutput_.remove(key);else recentOutput_[key]=old;
        record("TX failed: "+(error?*error:action.destination));
        backend_->closeOutput(action.destination);activeOutputs_.remove(action.destination);
        for(auto it=heldOutputNotes_.begin();it!=heldOutputNotes_.end();)if(it.value().destination==action.destination)it=heldOutputNotes_.erase(it);else++it;
        emit portsChanged();return false;
    }
    record("TX "+action.destination+" · "+action.message.description());
    const auto noteKey=action.destination+"|"+QString::number(action.message.channel)+":"+QString::number(action.message.number);
    if(action.message.type==MidiMessageType::NoteOn)heldOutputNotes_[noteKey]=action;
    if(action.message.type==MidiMessageType::NoteOff)heldOutputNotes_.remove(noteKey);
    return true;
}
void MidiEngine::activateCue(quint64 revision, const QVector<MidiOutputAction> &actions) {
    if(!revision||revision==lastCue_)return; lastCue_=revision;
    for(const auto &action:actions)if(action.enabled&&settings_.enabled){QString error;if(!sendAction(action,&error))record("Slide MIDI action skipped: "+error);}
    if(recentOutput_.size()>8192)recentOutput_.clear();
}
void MidiEngine::releaseDestination(const QString &id) {
    const auto notes=heldOutputNotes_.values();
    for(auto action:notes)if(action.destination==id){action.message.type=MidiMessageType::NoteOff;action.message.value=0;QString error;sendAction(action,&error);}
    for(auto it=heldOutputNotes_.begin();it!=heldOutputNotes_.end();)if(it.value().destination==id)it=heldOutputNotes_.erase(it);else++it;
}
void MidiEngine::releaseNotes(){for(const auto &id:activeOutputs_.values())releaseDestination(id);}
void MidiEngine::closeConnections(){for(const auto &id:activeInputs_.keys())backend_->closeInput(id);activeInputs_.clear();for(const auto &id:activeOutputs_)backend_->closeOutput(id);activeOutputs_.clear();decoders_.clear();edgeStates_.clear();}
void MidiEngine::shutdown(){reconnect_.stop();releaseNotes();closeConnections();QString error;backend_->setVirtualPorts(false,&error);settings_.enabled=false;recentOutput_.clear();lastTrigger_.clear();}
