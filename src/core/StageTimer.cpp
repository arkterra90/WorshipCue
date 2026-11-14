#include "StageTimer.h"

#include <cmath>

namespace {
constexpr int maximumSeconds = 99 * 3600 + 59 * 60 + 59;
bool integer(const QJsonValue &value, int minimum, int maximum) {
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= minimum
        && value.toDouble() <= maximum && std::floor(value.toDouble()) == value.toDouble();
}
}

bool StageTimerConfiguration::validate(QString *error) const {
    if (error) error->clear();
    if (name.trimmed().isEmpty() || name.size() > 100 || name.contains(QChar::Null)
        || int(mode) < 0 || int(mode) > 2 || durationSeconds < 1 || durationSeconds > maximumSeconds
        || elapsedLimitSeconds < 0 || elapsedLimitSeconds > maximumSeconds
        || warningSeconds < 0 || warningSeconds > maximumSeconds || !target.isValid()
        || target.date().year() < 1752 || target.date().year() > 9999) {
        if (error) *error = QStringLiteral("Enter a timer name, a duration up to 99:59:59, and a valid target date/time.");
        return false;
    }
    return true;
}

QJsonObject StageTimerConfiguration::toJson() const {
    return {{"name", name}, {"mode", int(mode)}, {"duration", durationSeconds},
        {"target", target.toUTC().toString(Qt::ISODateWithMs)}, {"elapsedLimit", elapsedLimitSeconds},
        {"overrun", allowOverrun}, {"warning", warningSeconds}};
}

bool StageTimerConfiguration::fromJson(const QJsonObject &json, StageTimerConfiguration *configuration, QString *error) {
    if (!json["name"].isString() || !integer(json["mode"], 0, 2)
        || !integer(json["duration"], 1, maximumSeconds) || !json["target"].isString()
        || !integer(json["elapsedLimit"], 0, maximumSeconds) || !json["overrun"].isBool()
        || !integer(json["warning"], 0, maximumSeconds)) {
        if (error) *error = QStringLiteral("Invalid saved stage timer settings.");
        return false;
    }
    StageTimerConfiguration parsed;
    parsed.name = json["name"].toString();
    parsed.mode = Mode(json["mode"].toInt());
    parsed.durationSeconds = json["duration"].toInt();
    parsed.target = QDateTime::fromString(json["target"].toString(), Qt::ISODateWithMs);
    parsed.elapsedLimitSeconds = json["elapsedLimit"].toInt();
    parsed.allowOverrun = json["overrun"].toBool();
    parsed.warningSeconds = json["warning"].toInt();
    if (!parsed.validate(error)) return false;
    *configuration = parsed;
    return true;
}

bool StageTimer::configure(const StageTimerConfiguration &configuration, QString *error) {
    if (!configuration.validate(error)) return false;
    configuration_ = configuration;
    reset();
    return true;
}

void StageTimer::reset() {
    elapsedMs_ = 0;
    frozenTargetMs_ = QDateTime::currentDateTimeUtc().msecsTo(configuration_.target);
    ticking_ = false;
    started_ = false;
    clock_.invalidate();
}

void StageTimer::start() {
    reset();
    started_ = true;
    ticking_ = true;
    clock_.start();
}

void StageTimer::pause() {
    if (!ticking_) return;
    elapsedMs_ += clock_.elapsed();
    frozenTargetMs_ = QDateTime::currentDateTimeUtc().msecsTo(configuration_.target);
    ticking_ = false;
}

void StageTimer::resume() {
    if (reading().state != StageTimerReading::State::Paused) return;
    ticking_ = true;
    clock_.start();
}

StageTimerReading StageTimer::reading() const {
    const qint64 elapsed = elapsedMs_ + (ticking_ ? clock_.elapsed() : 0);
    const bool countUp = configuration_.mode == StageTimerConfiguration::Mode::Elapsed;
    qint64 value = qint64(configuration_.durationSeconds) * 1000 - elapsed;
    if (countUp) value = elapsed;
    else if (configuration_.mode == StageTimerConfiguration::Mode::CountdownToTime)
        value = ticking_ ? QDateTime::currentDateTimeUtc().msecsTo(configuration_.target) : frozenTargetMs_;
    const bool limited = !countUp || configuration_.elapsedLimitSeconds > 0;
    const qint64 remaining = countUp ? qint64(configuration_.elapsedLimitSeconds) * 1000 - value : value;
    const bool reachedEnd = limited && remaining <= 0;
    StageTimerReading result;
    result.state = !started_ ? StageTimerReading::State::Ready
        : reachedEnd && !configuration_.allowOverrun ? StageTimerReading::State::Finished
        : ticking_ ? StageTimerReading::State::Running : StageTimerReading::State::Paused;
    result.overrun = limited && remaining < 0 && configuration_.allowOverrun;
    result.atEnd = reachedEnd;
    result.warning = limited && remaining <= qint64(configuration_.warningSeconds) * 1000;
    if (reachedEnd && !configuration_.allowOverrun)
        value = countUp ? qint64(configuration_.elapsedLimitSeconds) * 1000 : 0;
    result.valueMs = value;
    result.text = format(value, countUp);
    return result;
}

QString StageTimer::format(qint64 milliseconds, bool countUp) {
    const bool negative = milliseconds < 0;
    const quint64 magnitude = negative ? quint64(-(milliseconds + 1)) + 1 : quint64(milliseconds);
    const quint64 seconds = countUp ? magnitude / 1000 : (magnitude + 999) / 1000;
    QString result = QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    if (seconds >= 3600)
        result = QStringLiteral("%1:%2:%3").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return negative ? QLatin1Char('-') + result : result;
}
