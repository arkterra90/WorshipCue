#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Exercises the same stdio startup path as an AI client. Only discovery and
// library reads are sent; no model connection or credentials are required.
class McpConnectionProbe : public QObject {
    Q_OBJECT
public:
    explicit McpConnectionProbe(QObject *parent = nullptr);
    ~McpConnectionProbe() override;
    void start(const QString &executable, const QString &databasePath);
    bool isRunning() const { return running_; }
signals:
    void finished(bool success, const QString &report);
private:
    QProcess process_;
    QTimer timer_;
    QByteArray output_;
    QString failure_;
    bool running_ = false;
    bool complete_ = false;
    int expectedId_ = 1;
    void send(int id, const QString &method, const QJsonObject &params = {});
    void readOutput();
    void fail(const QString &message);
    void finish();
};
