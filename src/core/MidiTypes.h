#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

enum class MidiMessageType { NoteOff = 0x80, NoteOn = 0x90, ControlChange = 0xb0 };
struct MidiMessage {
    MidiMessageType type = MidiMessageType::NoteOn;
    int channel = 1;
    int number = 60;
    int value = 127;
    bool valid() const;
    QByteArray bytes() const;
    QString description() const;
    QString edgeKey() const;
    static QString typeName(MidiMessageType type);
};
struct MidiTrigger {
    QString source = QStringLiteral("*");
    int channel = 0; // zero: any enabled input channel
    MidiMessageType type = MidiMessageType::NoteOn;
    int number = 60;
    int value = -1; // -1: any value (Note On still requires a positive velocity)
    bool valid() const;
    bool matches(const QString &input, const MidiMessage &message) const;
    bool overlaps(const MidiTrigger &other) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, MidiTrigger *);
};
struct MidiOutputAction {
    bool enabled = true;
    QString destination;
    MidiMessage message;
    bool valid() const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, MidiOutputAction *);
    bool operator==(const MidiOutputAction &other) const { return toJson() == other.toJson(); }
};
enum class MidiCommand {
    Next, Previous, Blackout, ClearText, Restore, SelectPresentation, TakePresentation, TakeSlide,
    PauseAutoAdvance, PauseMedia, RestartMedia, MuteMedia, ReleaseNotes
};
struct MidiBinding {
    qint64 id = 0;
    QString name;
    bool enabled = true;
    MidiTrigger trigger;
    MidiCommand command = MidiCommand::Next;
    int songId = 0;
    qint64 slideId = 0;
    int entryId = 0;
    int assetId = 0;
    int occurrence = 0; // nth occurrence of this canonical page in this saved service entry
    bool valid() const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, MidiBinding *);
    static QString commandName(MidiCommand command);
};
bool validateMidiBindings(const QVector<MidiBinding> &, QString *error = nullptr);

// MIDI 1.0 stream decoder: handles packet boundaries, running status, realtime
// interleaving, velocity-zero Note Off, and ignores unsupported system/SysEx data.
class MidiStreamDecoder {
public:
    QVector<MidiMessage> feed(const QByteArray &bytes);
private:
    int status_ = 0;
    int count_ = 0;
    int first_ = 0;
    int systemRemaining_ = 0;
    bool sysex_ = false;
};
Q_DECLARE_METATYPE(MidiMessage)
Q_DECLARE_METATYPE(MidiBinding)
