#pragma once
#include "MidiBackend.h"
#include "../core/MidiTypes.h"
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <functional>
struct MidiDeviceSetting {
    QString id;
    bool enabled = true;
    int channel = 0;
};
struct MidiSettings {
    bool enabled = false;
    bool autoReconnect = true;
    bool virtualPorts = false;
    int echoWindowMs = 250;
    int minimumIntervalMs = 100;
    QVector<MidiDeviceSetting> inputs;
    QVector<MidiDeviceSetting> outputs;
    bool valid(QString *error = nullptr) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, MidiSettings *, QString *error = nullptr);
};
class MidiEngine : public QObject {
    Q_OBJECT
public:
    explicit MidiEngine(QObject *parent = nullptr, std::unique_ptr<MidiBackend> backend = {});
    ~MidiEngine() override;
    const MidiSettings &settings() const { return settings_; }
    const QVector<MidiPort> &ports() const { return ports_; }
    const QVector<MidiBinding> &bindings() const { return bindings_; }
    QStringList activity() const { return activity_; }
    QString setupHelp() const { return backend_->setupHelp(); }
    bool supportsVirtualPorts() const { return backend_->supportsVirtualPorts(); }
    bool setSettings(const MidiSettings &, QString *error = nullptr, bool persist = true);
    void restoreSettings();
    bool setBindings(const QVector<MidiBinding> &, QString *error = nullptr);
    void setDispatchGate(std::function<bool()> gate) { dispatchGate_ = std::move(gate); }
    void refreshPorts();
    void activateCue(quint64 revision, const QVector<MidiOutputAction> &actions);
    bool sendAction(const MidiOutputAction &, QString *error = nullptr);
    void releaseNotes();
    void shutdown();
    QString portStatus(const QString &id, bool input) const;
    int outputChannel(const QString &id) const;
    void record(const QString &text);

signals:
    void portsChanged();
    void settingsChanged();
    void activityAdded(const QString &line);
    void messageReceived(const QString &source, const MidiMessage &message);
    void commandRequested(const MidiBinding &binding);

private:
    std::unique_ptr<MidiBackend> backend_;
    MidiSettings settings_;
    QVector<MidiPort> ports_;
    QVector<MidiBinding> bindings_;
    QHash<QString,MidiBinding> bindingIndex_;
    QHash<QString, quint64> activeInputs_;
    QSet<QString> activeOutputs_;
    QHash<QString, QString> portErrors_;
    QHash<QString, MidiStreamDecoder> decoders_;
    QHash<QString, int> edgeStates_;
    QHash<QString, qint64> recentOutput_;
    QHash<qint64, qint64> lastTrigger_;
    QHash<QString, MidiOutputAction> heldOutputNotes_;
    QStringList activity_;
    QTimer reconnect_;
    QElapsedTimer clock_;
    quint64 connectionSerial_ = 0;
    quint64 lastCue_ = 0;
    QString lastDiscoveryError_;
    std::function<bool()> dispatchGate_;
    void receive(const QString &, const QByteArray &, quint64);
    void closeConnections();
    void releaseDestination(const QString &id);
};
