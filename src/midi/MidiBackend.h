#pragma once
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QVector>
#include <memory>
struct MidiPort {
    QString id;
    QString name;
    bool input = false;
    bool available = true;
    QString detail;
    bool operator==(const MidiPort &p) const { return id == p.id && name == p.name && input == p.input && available == p.available && detail == p.detail; }
};
class MidiBackend : public QObject {
    Q_OBJECT
public:
    explicit MidiBackend(QObject *parent = nullptr);
    ~MidiBackend() override;
    virtual QVector<MidiPort> ports(QString *error) = 0;
    virtual bool openInput(const QString &id, quint64 generation, QString *error) = 0;
    virtual void closeInput(const QString &id) = 0;
    virtual bool openOutput(const QString &id, QString *error) = 0;
    virtual void closeOutput(const QString &id) = 0;
    virtual bool send(const QString &id, const QByteArray &bytes, QString *error) = 0;
    virtual bool supportsVirtualPorts() const { return false; }
    virtual bool setVirtualPorts(bool enabled, QString *error);
    virtual QString setupHelp() const = 0;
    struct ReceiveGate {
        QMutex mutex;
        MidiBackend *receiver = nullptr;
        struct Packet { QString source; QByteArray bytes; quint64 generation; };
        QVector<Packet> pending;
        bool scheduled = false;
        bool overflow = false;
    };
    // Native callbacks retain this gate, not a naked pointer to a dying QObject.
    static void deliver(const std::shared_ptr<ReceiveGate> &, const QString &, QByteArray, quint64 generation);

signals:
    void packetReceived(const QString &source, const QByteArray &bytes, quint64 generation);
    void inputOverflow();

protected:
    std::shared_ptr<ReceiveGate> receiveGate() const { return gate_; }
private:
    std::shared_ptr<ReceiveGate> gate_;
};
std::unique_ptr<MidiBackend> nativeMidiBackend(const QString &clientName = QStringLiteral("WorshipCue"));
