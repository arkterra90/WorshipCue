#include "MediaPlayback.h"

#include <QDebug>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QUrl>
#include <QVideoFrame>
#include <QtConcurrent/QtConcurrentRun>

namespace {
QImage readPresentationImage(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    if (source.isValid() && (source.width() > 1920 || source.height() > 1080))
        reader.setScaledSize(source.scaled(QSize(1920, 1080), Qt::KeepAspectRatio));
    QImage image = reader.read();
    // EXIF rotation can exchange width/height after the decoder's scaled size.
    if (image.width() > 1920 || image.height() > 1080)
        image = image.scaled(QSize(1920, 1080), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}
}

MediaPlayback::MediaPlayback(PresentationController *controller, QObject *parent)
    : QObject(parent), controller_(controller), player_(this), audio_(this), sink_(this) {
    audio_.setVolume(1.0);
    player_.setAudioOutput(&audio_);
    player_.setVideoSink(&sink_);
    loadTimeout_.setSingleShot(true);
    loadTimeout_.setInterval(10000);
    connect(&loadTimeout_, &QTimer::timeout, this, [this] {
        if (isVideo() && frame_.isNull() && error_.isEmpty())
            fail(QStringLiteral("%1 produced no video frames. Check its codec or try another format.").arg(label()));
    });
    connect(controller_, &PresentationController::stateChanged, this, &MediaPlayback::syncCue);
    connect(&player_, &QMediaPlayer::playbackStateChanged, this, [this] {
        if (!switching_) emit statusChanged();
    });
    connect(&player_, &QMediaPlayer::positionChanged, this, [this] { emit statusChanged(); });
    connect(&player_, &QMediaPlayer::durationChanged, this, [this] { emit statusChanged(); });
    syncCue();
}

MediaPlayback::~MediaPlayback() {
    disconnect(&player_, nullptr, this, nullptr);
    disconnect(&sink_, nullptr, this, nullptr);
    player_.stop();
    player_.setVideoSink(nullptr);
    player_.setAudioOutput(nullptr);
}

QString MediaPlayback::label() const {
    return activeMedia_.name.isEmpty() ? QFileInfo(activeMedia_.path).fileName() : activeMedia_.name;
}

void MediaPlayback::syncCue() {
    const auto &state = controller_->state();
    // Blackout mutes the soundtrack but keeps its playhead moving. Clear only
    // hides text and leaves the movie and its audio playing.
    audio_.setMuted(state.blackout || userMuted_);
    const SlideMedia media = state.currentMedia();
    if (state.cueRevision == cueRevision_ && media == activeMedia_) return;
    cueRevision_ = state.cueRevision;
    const quint64 generation = ++loadGeneration_;
    switching_ = true;
    loadTimeout_.stop();
    disconnect(videoFramesConnection_);
    disconnect(videoErrorConnection_);
    disconnect(mediaStatusConnection_);
    player_.stop();
    player_.setSource({});
    activeMedia_ = media;
    frame_ = {};
    error_.clear();
    finished_ = false;
    loadingImage_ = false;
    switching_ = false;
    emit frameChanged();

    if (media.kind == MediaKind::None) { emit statusChanged(); return; }
    const QFileInfo info(media.path);
    if (!info.isAbsolute() || !info.isFile() || !info.isReadable()) {
        fail(QStringLiteral("Media is missing or unreadable: %1").arg(label()));
        return;
    }
    if (media.kind == MediaKind::Image) {
        loadingImage_ = true;
        emit statusChanged();
        loadImage(media.path, generation);
        return;
    }
    if (!player_.isAvailable()) {
        fail(QStringLiteral("Media playback is unavailable. Check the Qt Multimedia installation."));
        return;
    }
    player_.setLoops(media.loop ? QMediaPlayer::Infinite : QMediaPlayer::Once);
    videoErrorConnection_ = connect(&player_, &QMediaPlayer::errorOccurred, this,
        [this, generation](QMediaPlayer::Error error, const QString &message) {
            if (generation == loadGeneration_ && !switching_ && isPlayable() && error != QMediaPlayer::NoError)
                fail(QStringLiteral("Could not play %1: %2").arg(label(), message));
        });
    mediaStatusConnection_ = connect(&player_, &QMediaPlayer::mediaStatusChanged, this,
        [this, generation](QMediaPlayer::MediaStatus status) {
            if (generation != loadGeneration_ || switching_ || !isPlayable()) return;
            if (status == QMediaPlayer::InvalidMedia && error_.isEmpty())
                fail(QStringLiteral("%1 could not be decoded. Try a different media format.").arg(label()));
            if (isAudio() && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia))
                loadTimeout_.stop();
            if (status == QMediaPlayer::EndOfMedia) finished_ = true;
            emit statusChanged();
        });
    videoFramesConnection_ = connect(&sink_, &QVideoSink::videoFrameChanged, this,
        [this, generation](const QVideoFrame &videoFrame) {
            // Queued frames from a previous cue must not flash over the next.
            if (generation != loadGeneration_ || switching_ || !isVideo()
                || !error_.isEmpty() || !videoFrame.isValid()) return;
            const QImage image = videoFrame.toImage();
            if (image.isNull()) return;
            loadTimeout_.stop();
            frame_ = QPixmap::fromImage(image);
            emit frameChanged();
        });
    player_.setSource(QUrl::fromLocalFile(media.path));
    if (isVideo()) loadTimeout_.start();
    player_.play();
    emit statusChanged();
}

void MediaPlayback::loadImage(const QString &path, quint64 generation) {
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, generation] {
        const QImage image = watcher->result();
        watcher->deleteLater();
        if (generation != loadGeneration_ || activeMedia_.kind != MediaKind::Image) return;
        loadingImage_ = false;
        if (image.isNull()) {
            fail(QStringLiteral("The picture could not be decoded: %1").arg(label()));
            return;
        }
        frame_ = QPixmap::fromImage(image);
        emit frameChanged();
        emit statusChanged();
    });
    watcher->setFuture(QtConcurrent::run([path] { return readPresentationImage(path); }));
}

void MediaPlayback::fail(const QString &message) {
    error_ = message;
    loadTimeout_.stop();
    qWarning().noquote() << "WorshipCue media:" << message;
    player_.stop();
    frame_ = {};
    loadingImage_ = false;
    emit frameChanged();
    emit statusChanged();
}

QString MediaPlayback::status() const {
    if (!error_.isEmpty()) return error_;
    if (activeMedia_.kind == MediaKind::None) return QStringLiteral("No media live");
    if (activeMedia_.kind == MediaKind::Image)
        return QStringLiteral("%1 • %2").arg(loadingImage_ ? QStringLiteral("Loading picture") : QStringLiteral("Picture"), label());
    const QString state = finished_ ? (isAudio() ? QStringLiteral("Finished") : QStringLiteral("Finished — last frame held"))
        : player_.playbackState() == QMediaPlayer::PausedState ? QStringLiteral("Paused")
        : player_.mediaStatus() == QMediaPlayer::LoadingMedia ? QStringLiteral("Loading media")
        : player_.playbackState() == QMediaPlayer::PlayingState ? QStringLiteral("Playing")
        : QStringLiteral("Stopped");
    return QStringLiteral("%1 • %2%3").arg(state, label(), activeMedia_.loop ? QStringLiteral(" • Loop") : QString());
}

void MediaPlayback::togglePause() {
    if (!isPlayable() || !error_.isEmpty()) return;
    if (player_.playbackState() == QMediaPlayer::PlayingState) player_.pause();
    else {
        if (finished_) { finished_ = false; player_.setPosition(0); }
        player_.play();
    }
}

void MediaPlayback::restart() {
    if (!isPlayable() || !error_.isEmpty()) return;
    finished_ = false;
    player_.setPosition(0);
    player_.play();
}

void MediaPlayback::toggleMute() {
    userMuted_ = !userMuted_;
    audio_.setMuted(userMuted_ || controller_->state().blackout);
    emit statusChanged();
}
