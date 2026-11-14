#include "../src/core/StageTimer.h"
#include <QtTest>

class StageTimerTest : public QObject {
    Q_OBJECT
private slots:
    void configurationValidatesAndRoundTrips() {
        StageTimerConfiguration configuration;
        configuration.name = "Sermon";
        configuration.mode = StageTimerConfiguration::Mode::CountdownToTime;
        configuration.target = QDateTime::currentDateTime().addSecs(1800);
        configuration.allowOverrun = true;
        StageTimerConfiguration parsed;
        QString error;
        QVERIFY(StageTimerConfiguration::fromJson(configuration.toJson(), &parsed, &error));
        QCOMPARE(parsed.toJson(), configuration.toJson());
        for (const auto &change : {QJsonObject{{"mode", 3}}, QJsonObject{{"duration", 0}},
                                  QJsonObject{{"warning", -1}}, QJsonObject{{"duration", 1.5}},
                                  QJsonObject{{"name", ""}}, QJsonObject{{"target", "bad"}}, QJsonObject{{"overrun", 1}}}) {
            auto json = configuration.toJson();
            for (auto it = change.begin(); it != change.end(); ++it) json[it.key()] = it.value();
            QVERIFY(!StageTimerConfiguration::fromJson(json, &parsed, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(parsed.toJson(), configuration.toJson());
        }
        QCOMPARE(StageTimer::format(5500), QString("00:06"));
        QCOMPARE(StageTimer::format(5500, true), QString("00:05"));
        QCOMPARE(StageTimer::format(3600000), QString("01:00:00"));
        QCOMPARE(StageTimer::format(-1), QString("-00:01"));
    }
    void countdownPausesResumesResetsAndFinishes() {
        StageTimer timer;
        StageTimerConfiguration configuration;
        configuration.durationSeconds = 1;
        configuration.warningSeconds = 1;
        QVERIFY(timer.configure(configuration));
        QCOMPARE(timer.reading().state, StageTimerReading::State::Ready);
        timer.start(); QTest::qWait(30); timer.pause();
        const auto paused = timer.reading().valueMs;
        QVERIFY(paused > 0 && paused < 1000);
        QTest::qWait(40);
        QCOMPARE(timer.reading().valueMs, paused);
        QCOMPARE(timer.reading().state, StageTimerReading::State::Paused);
        timer.resume();
        QTRY_COMPARE_WITH_TIMEOUT(timer.reading().state, StageTimerReading::State::Finished, 2000);
        QCOMPARE(timer.reading().valueMs, qint64(0));
        timer.resume();
        QCOMPARE(timer.reading().state, StageTimerReading::State::Finished);
        timer.reset();
        QCOMPARE(timer.reading().state, StageTimerReading::State::Ready);
        QCOMPARE(timer.reading().valueMs, qint64(1000));
    }
    void scheduledTimeCatchesUpOnResumeAndSupportsNegativeOverrun() {
        StageTimer timer;
        StageTimerConfiguration configuration;
        configuration.mode = StageTimerConfiguration::Mode::CountdownToTime;
        configuration.target = QDateTime::currentDateTimeUtc().addSecs(5);
        configuration.allowOverrun = true;
        QVERIFY(timer.configure(configuration));
        timer.start(); timer.pause();
        const auto paused = timer.reading().valueMs;
        QTest::qWait(60);
        QCOMPARE(timer.reading().valueMs, paused);
        timer.resume();
        QVERIFY(timer.reading().valueMs < paused - 30);
        configuration.target = QDateTime::currentDateTimeUtc().addSecs(-2);
        QVERIFY(timer.configure(configuration)); timer.start();
        QVERIFY(timer.reading().overrun);
        QVERIFY(timer.reading().text.startsWith('-'));
        QCOMPARE(timer.reading().state, StageTimerReading::State::Running);
        configuration.allowOverrun = false;
        QVERIFY(timer.configure(configuration)); timer.start();
        QCOMPARE(timer.reading().state, StageTimerReading::State::Finished);
        QCOMPARE(timer.reading().text, QString("00:00"));
    }
    void elapsedTimerCanRunFreelyOrStopAtItsLimit() {
        StageTimer timer;
        StageTimerConfiguration configuration;
        configuration.mode = StageTimerConfiguration::Mode::Elapsed;
        QVERIFY(timer.configure(configuration)); timer.start(); QTest::qWait(40);
        QVERIFY(timer.reading().valueMs > 0);
        QVERIFY(!timer.reading().warning);
        timer.pause(); const auto paused = timer.reading().valueMs; QTest::qWait(30);
        QCOMPARE(timer.reading().valueMs, paused);
        configuration.elapsedLimitSeconds = 1;
        QVERIFY(timer.configure(configuration)); timer.start();
        QTRY_COMPARE_WITH_TIMEOUT(timer.reading().state, StageTimerReading::State::Finished, 2000);
        QCOMPARE(timer.reading().valueMs, qint64(1000));
        configuration.allowOverrun = true;
        QVERIFY(timer.configure(configuration)); timer.start();
        QTRY_VERIFY_WITH_TIMEOUT(timer.reading().overrun, 2000);
        QVERIFY(timer.reading().valueMs > 1000);
        QCOMPARE(timer.reading().state, StageTimerReading::State::Running);
    }
};
QTEST_GUILESS_MAIN(StageTimerTest)
#include "stage_timer_test.moc"
