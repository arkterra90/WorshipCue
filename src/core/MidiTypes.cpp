#include "MidiTypes.h"
#include <QSet>

namespace {
bool kind(MidiMessageType type) { return type == MidiMessageType::NoteOn || type == MidiMessageType::NoteOff || type == MidiMessageType::ControlChange; }
bool integer(const QJsonObject &json, const char *key, int *value) {
    const auto field = json.value(QString::fromLatin1(key));
    if (!field.isDouble() || field.toDouble() != field.toInt(-999999)) return false;
    *value = field.toInt(); return true;
}
}
bool MidiMessage::valid() const {
    return kind(type) && channel >= 1 && channel <= 16 && number >= 0 && number <= 127 && value >= 0 && value <= 127
        && (type != MidiMessageType::NoteOn || value > 0);
}
QByteArray MidiMessage::bytes() const {
    if (!valid()) return {};
    QByteArray result; result.append(char(int(type) | (channel - 1))); result.append(char(number)); result.append(char(value)); return result;
}
QString MidiMessage::typeName(MidiMessageType type) {
    if (type == MidiMessageType::NoteOn) return "Note On";
    if (type == MidiMessageType::NoteOff) return "Note Off";
    if (type == MidiMessageType::ControlChange) return "Control Change";
    return "Unsupported";
}
QString MidiMessage::description() const { return QStringLiteral("%1 · channel %2 · number %3 · value %4").arg(typeName(type)).arg(channel).arg(number).arg(value); }
QString MidiMessage::edgeKey() const { return QStringLiteral("%1:%2:%3").arg(type == MidiMessageType::ControlChange ? int(type) : int(MidiMessageType::NoteOn)).arg(channel).arg(number); }
bool MidiTrigger::valid() const {
    return !source.isEmpty() && source.size() <= 256 && channel >= 0 && channel <= 16 && kind(type)
        && number >= 0 && number <= 127 && value >= -1 && value <= 127 && (type != MidiMessageType::NoteOn || value != 0);
}
bool MidiTrigger::matches(const QString &input, const MidiMessage &message) const {
    return (source == "*" || source == input) && (!channel || channel == message.channel) && type == message.type
        && number == message.number && (value < 0 || value == message.value);
}
bool MidiTrigger::overlaps(const MidiTrigger &other) const {
    return (source == "*" || other.source == "*" || source == other.source)
        && (!channel || !other.channel || channel == other.channel) && type == other.type && number == other.number
        && (value < 0 || other.value < 0 || value == other.value);
}
QJsonObject MidiTrigger::toJson() const { return {{"source", source}, {"channel", channel}, {"type", int(type)}, {"number", number}, {"value", value}}; }
bool MidiTrigger::fromJson(const QJsonObject &json, MidiTrigger *result) {
    MidiTrigger parsed; int type;
    if (!json["source"].isString() || !integer(json,"channel",&parsed.channel) || !integer(json,"type",&type)
        || !integer(json,"number",&parsed.number) || !integer(json,"value",&parsed.value)) return false;
    parsed.source = json["source"].toString(); parsed.type = MidiMessageType(type);
    if (!parsed.valid()) return false; *result = parsed; return true;
}
bool MidiOutputAction::valid() const { return !destination.isEmpty() && destination.size() <= 256 && message.valid(); }
QJsonObject MidiOutputAction::toJson() const {
    return {{"enabled", enabled}, {"destination", destination}, {"type", int(message.type)}, {"channel", message.channel}, {"number", message.number}, {"value", message.value}};
}
bool MidiOutputAction::fromJson(const QJsonObject &json, MidiOutputAction *result) {
    MidiOutputAction parsed; int type;
    if (!json["enabled"].isBool() || !json["destination"].isString() || !integer(json,"type",&type)
        || !integer(json,"channel",&parsed.message.channel) || !integer(json,"number",&parsed.message.number) || !integer(json,"value",&parsed.message.value)) return false;
    parsed.enabled = json["enabled"].toBool(); parsed.destination = json["destination"].toString(); parsed.message.type = MidiMessageType(type);
    if (!parsed.valid()) return false; *result = parsed; return true;
}
QString MidiBinding::commandName(MidiCommand command) {
    switch (command) {
    case MidiCommand::Next: return "Next slide"; case MidiCommand::Previous: return "Previous slide";
    case MidiCommand::Blackout: return "Toggle Blackout"; case MidiCommand::ClearText: return "Toggle Clear Text";
    case MidiCommand::Restore: return "Restore audience"; case MidiCommand::SelectPresentation: return "Select presentation";
    case MidiCommand::TakePresentation: return "Take presentation live"; case MidiCommand::TakeSlide: return "Take slide live";
    case MidiCommand::PauseAutoAdvance: return "Pause / resume auto-advance"; case MidiCommand::PauseMedia: return "Pause / resume media";
    case MidiCommand::RestartMedia: return "Restart media"; case MidiCommand::MuteMedia: return "Toggle media mute";
    case MidiCommand::ReleaseNotes: return "Release WorshipCue notes";
    }
    return {};
}
bool MidiBinding::valid() const {
    if (name.trimmed().isEmpty() || name.size() > 100 || !trigger.valid() || int(command) < 0 || int(command) > int(MidiCommand::ReleaseNotes)
        || songId < 0 || slideId < 0 || slideId > 9007199254740991LL || entryId < 0 || assetId < 0 || occurrence < 0 || occurrence > 9999) return false;
    if (command == MidiCommand::TakeSlide) return ((songId > 0 && slideId > 0 && !assetId) || (!songId && !slideId && assetId > 0));
    if (command == MidiCommand::SelectPresentation || command == MidiCommand::TakePresentation) return songId > 0 && !slideId && !assetId && !occurrence;
    return !songId && !slideId && !entryId && !assetId && !occurrence;
}
QJsonObject MidiBinding::toJson() const {
    return {{"name", name}, {"enabled", enabled}, {"trigger", trigger.toJson()}, {"command", int(command)},
        {"song", songId}, {"slide", double(slideId)}, {"entry", entryId}, {"asset", assetId}, {"occurrence", occurrence}};
}
bool MidiBinding::fromJson(const QJsonObject &json, MidiBinding *result) {
    MidiBinding parsed; int command;
    const auto slide = json["slide"];
    if (!json["name"].isString() || !json["enabled"].isBool() || !json["trigger"].isObject() || !MidiTrigger::fromJson(json["trigger"].toObject(), &parsed.trigger)
        || !integer(json,"command",&command) || !integer(json,"song",&parsed.songId) || !integer(json,"entry",&parsed.entryId)
        || !integer(json,"asset",&parsed.assetId) || !integer(json,"occurrence",&parsed.occurrence)
        || !slide.isDouble() || slide.toDouble() < 0 || slide.toDouble() > 9007199254740991. || slide.toDouble() != double(slide.toInteger(-1))) return false;
    parsed.name = json["name"].toString(); parsed.enabled = json["enabled"].toBool(); parsed.command = MidiCommand(command); parsed.slideId = slide.toInteger();
    if (!parsed.valid()) return false; *result = parsed; return true;
}
bool validateMidiBindings(const QVector<MidiBinding> &bindings, QString *error) {
    if (error) error->clear();
    const auto fail = [error](const QString &text) { if (error) *error = text; return false; };
    if (bindings.size() > 2000) return fail("A library supports at most 2000 MIDI input mappings.");
    QSet<qint64> ids;
    for (int i = 0; i < bindings.size(); ++i) {
        const auto &binding = bindings.at(i);
        if (!binding.valid() || binding.id < 0 || (binding.id && ids.contains(binding.id))) return fail("Use a valid MIDI trigger, name, command, and target.");
        if (binding.id) ids.insert(binding.id);
        for (int j = 0; binding.enabled && j < i; ++j) if (bindings.at(j).enabled && binding.trigger.overlaps(bindings.at(j).trigger))
            return fail(QStringLiteral("MIDI trigger conflict: ‘%1’ overlaps ‘%2’. Use a different source, channel, number, or value, or disable a mapping.").arg(binding.name, bindings.at(j).name));
    }
    return true;
}
QVector<MidiMessage> MidiStreamDecoder::feed(const QByteArray &bytes) {
    QVector<MidiMessage> result;
    for (const auto raw : bytes) {
        const int byte = quint8(raw);
        if (byte >= 0xf8) continue;
        if (byte & 0x80) {
            count_ = 0; systemRemaining_ = 0;
            if (byte >= 0xf0) {
                status_ = 0; sysex_ = byte == 0xf0;
                if (byte == 0xf1 || byte == 0xf3) systemRemaining_ = 1;
                if (byte == 0xf2) systemRemaining_ = 2;
            } else { status_ = byte; sysex_ = false; }
            continue;
        }
        if (sysex_) continue;
        if (systemRemaining_) { --systemRemaining_; continue; }
        if (!status_) continue;
        const int kind = status_ & 0xf0;
        if (kind == 0xc0 || kind == 0xd0) continue;
        if (!count_) { first_ = byte; count_ = 1; continue; }
        count_ = 0;
        MidiMessage message{MidiMessageType(kind), (status_ & 0x0f) + 1, first_, byte};
        if (kind == 0x90 && !byte) message.type = MidiMessageType::NoteOff;
        if (message.valid()) result.append(message);
    }
    return result;
}
