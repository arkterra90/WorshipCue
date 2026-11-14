#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>

struct StageTimerConfiguration {
    enum class Mode { Countdown, CountdownToTime, Elapsed };
    QString name = QStringLiteral("Service timer");
    Mode mode = Mode::Countdown;
    int durationSeconds = 300;
    QDateTime target = QDateTime::currentDateTime().addSecs(3600);
    int elapsedLimitSeconds = 0;
    bool allowOverrun = false;
    int warningSeconds = 60;

    bool validate(QString *error = nullptr) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, StageTimerConfiguration *, QString *error = nullptr);
};

struct StageTimerReading {
    enum class State { Ready, Running, Paused, Finished };
    qint64 valueMs = 300000;
    State state = State::Ready;
    bool warning = false;
    bool overrun = false;
    bool atEnd = false;
    QString text;
};

class StageTimer {
public:
    const StageTimerConfiguration &configuration() const { return configuration_; }
    bool configure(const StageTimerConfiguration &, QString *error = nullptr);
    void start();
    void pause();
    void resume();
    void reset();
    StageTimerReading reading() const;
    static QString format(qint64 milliseconds, bool countUp = false);
private:
    StageTimerConfiguration configuration_;
    QElapsedTimer clock_;
    qint64 elapsedMs_ = 0;
    qint64 frozenTargetMs_ = 0;
    bool started_ = false;
    bool ticking_ = false;
};
