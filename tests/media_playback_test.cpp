#include "../src/media/MediaPlayback.h"
#include "../src/ui/DisplayWindow.h"
#include "../src/ui/SlideRenderer.h"

#include <QAudioOutput>
#include <QFile>
#include <QImage>
#include <QMediaPlayer>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class MediaPlaybackTest : public QObject {
    Q_OBJECT
    static void take(PresentationController &controller, const SlideMedia &media) {
        controller.takeServiceLive(1, {{1, QStringLiteral("Media presentation"), {QString{}}, {media}}}, 1);
    }
private slots:
    void stillsDecodeAsynchronouslyAndRespectSnapshotChanges() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("large.png"));
        QImage original(4000, 2400, QImage::Format_RGB32);
        original.fill(Qt::blue);
        QVERIFY(original.save(path));
        PresentationController controller;
        MediaPlayback playback(&controller);
        take(controller, {MediaKind::Image, path, QStringLiteral("Sunday 01.png"), false});
        QTRY_VERIFY_WITH_TIMEOUT(!playback.frame().isNull(), 5000);
        QVERIFY(playback.errorString().isEmpty());
        QVERIFY(playback.frame().width() <= 1920);
        QVERIFY(playback.frame().height() <= 1080);
        QCOMPARE(playback.frame().toImage().pixelColor(10, 10), QColor(Qt::blue));
        const auto key = playback.frame().cacheKey();
        controller.toggleBlackout();
        controller.toggleClear();
        controller.restoreOutput();
        QCOMPARE(playback.frame().cacheKey(), key);
        // A completed worker for an old cue must not replace a newer text cue.
        take(controller, {MediaKind::Image, path, QStringLiteral("Old picture"), false});
        controller.takeLive(QStringLiteral("Song"), {QStringLiteral("Live lyric")});
        QTest::qWait(100);
        QVERIFY(playback.frame().isNull());
        QVERIFY(playback.errorString().isEmpty());
        QCOMPARE(playback.findChildren<QMediaPlayer *>().size(), 1);
    }

    void missingAndInvalidMediaStopPreviousContent() {
        QTemporaryDir directory;
        const QString picture = directory.filePath(QStringLiteral("picture.png"));
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(picture));
        PresentationController controller;
        MediaPlayback playback(&controller);
        take(controller, {MediaKind::Image, picture, QStringLiteral("Picture"), false});
        QTRY_VERIFY(!playback.frame().isNull());
        take(controller, {MediaKind::Video, directory.filePath(QStringLiteral("missing.mov")), QStringLiteral("Missing clip"), false});
        QVERIFY(playback.frame().isNull());
        QVERIFY(playback.errorString().contains(QStringLiteral("missing"), Qt::CaseInsensitive));
        auto *player = playback.findChild<QMediaPlayer *>();
        QVERIFY(player);
        QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState);
        const QString invalid = directory.filePath(QStringLiteral("invalid.mov"));
        QFile file(invalid);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("This is not a movie");
        file.close();
        take(controller, {MediaKind::Video, invalid, QStringLiteral("Invalid clip"), false});
        QTRY_VERIFY_WITH_TIMEOUT(!playback.errorString().isEmpty(), 15000);
        QVERIFY(playback.frame().isNull());
        QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState);
        controller.takeLive(QStringLiteral("Song"), {QStringLiteral("Restored lyric")});
        QVERIFY(playback.errorString().isEmpty());
        QVERIFY(!playback.isVideo());
    }

    void videoDecodesAndControlsShareOnePlayhead() {
        const QString path = QFINDTESTDATA("fixtures/media-colors.mov");
        QVERIFY(!path.isEmpty());
        PresentationController controller;
        MediaPlayback playback(&controller);
        take(controller, {MediaKind::Video, path, QStringLiteral("Test clip.mov"), false});
        QTRY_VERIFY_WITH_TIMEOUT(!playback.frame().isNull() || !playback.errorString().isEmpty(), 10000);
        QVERIFY2(playback.errorString().isEmpty(), qPrintable(playback.errorString()));
        QVERIFY(!playback.frame().isNull());
        const QColor firstColor = playback.frame().toImage().pixelColor(80, 45);
        QVERIFY(firstColor.red() > firstColor.green());
        auto *player = playback.findChild<QMediaPlayer *>();
        auto *audio = playback.findChild<QAudioOutput *>();
        QVERIFY(player && audio);
        QCOMPARE(playback.findChildren<QMediaPlayer *>().size(), 1);
        QTRY_VERIFY(playback.position() > 100);
        const auto revision = controller.state().cueRevision;
        QSignalSpy sourceChanges(player, &QMediaPlayer::sourceChanged);
        controller.toggleBlackout();
        QVERIFY(audio->isMuted());
        controller.toggleClear();
        QVERIFY(!audio->isMuted());
        controller.restoreOutput();
        QCOMPARE(sourceChanges.count(), 0);
        QCOMPARE(controller.state().cueRevision, revision);
        playback.toggleMute();
        QVERIFY(playback.muted());
        QVERIFY(audio->isMuted());
        controller.toggleBlackout();
        controller.restoreOutput();
        QVERIFY(audio->isMuted());
        playback.toggleMute();
        QVERIFY(!playback.muted());
        QVERIFY(!audio->isMuted());
        playback.togglePause();
        QTRY_VERIFY(playback.isPaused());
        const auto paused = playback.position();
        QTest::qWait(120);
        // AVFoundation may round a paused media timestamp to the adjacent
        // millisecond. It must remain paused rather than advance with time.
        QVERIFY(playback.isPaused());
        QVERIFY(qAbs(playback.position() - paused) <= 2);
        playback.restart();
        QTRY_VERIFY(!playback.isPaused());
        QVERIFY(playback.position() <= paused);
        // An explicit Take of the same cue intentionally restarts its movie.
        take(controller, {MediaKind::Video, path, QStringLiteral("Test clip.mov"), false});
        QVERIFY(sourceChanges.count() >= 2);
        QTRY_VERIFY(!playback.frame().isNull());
        const QColor restartedColor = playback.frame().toImage().pixelColor(80, 45);
        QVERIFY(restartedColor.red() > restartedColor.green());
        QTRY_VERIFY_WITH_TIMEOUT(playback.status().contains(QStringLiteral("Finished")), 8000);
        QVERIFY(!playback.frame().isNull());
        const QColor finalColor = playback.frame().toImage().pixelColor(80, 45);
        QVERIFY(finalColor.green() > finalColor.red());
        QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState);
    }

    void loopingMovieKeepsPlayingAcrossItsEnd() {
        const QString path = QFINDTESTDATA("fixtures/media-colors.mov");
        QVERIFY(!path.isEmpty());
        PresentationController controller;
        MediaPlayback playback(&controller);
        take(controller, {MediaKind::Video, path, QStringLiteral("Loop clip.mov"), true});
        QTRY_VERIFY_WITH_TIMEOUT(!playback.frame().isNull() || !playback.errorString().isEmpty(), 10000);
        QVERIFY2(playback.errorString().isEmpty(), qPrintable(playback.errorString()));
        auto *player = playback.findChild<QMediaPlayer *>();
        QCOMPARE(player->loops(), int(QMediaPlayer::Infinite));
        QTest::qWait(3500);
        QCOMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QVERIFY(!playback.frame().isNull());
        QVERIFY(!playback.status().contains(QStringLiteral("Finished")));
    }

    void sharedAudienceRenderMatchesThePlaybackFrame() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("picture.png"));
        QImage image(160, 90, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(path));
        PresentationController controller;
        MediaPlayback playback(&controller);
        DisplayWindow audience(DisplayWindow::Kind::Audience, &controller);
        audience.setMediaPlayback(&playback);
        audience.resize(320, 180);
        take(controller, {MediaKind::Image, path, QStringLiteral("Picture"), false});
        QTRY_VERIFY(!playback.frame().isNull());
        QImage expected(QSize(320, 180), QImage::Format_ARGB32_Premultiplied);
        expected.fill(Qt::black);
        {
            QPainter painter(&expected);
            SlideRenderer::paintAudience(painter, expected.rect(), controller.state(), {}, playback.frame());
        }
        const QPixmap captured = audience.grab();
        const QImage logical = captured.toImage().scaled(QSize(320, 180));
        QCOMPARE(logical.pixelColor(160, 90), expected.pixelColor(160, 90));
        controller.toggleBlackout();
        QCOMPARE(audience.grab().toImage().pixelColor(100, 60), QColor(Qt::black));
    }
};

QTEST_MAIN(MediaPlaybackTest)
#include "media_playback_test.moc"
