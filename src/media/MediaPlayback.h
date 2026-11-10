#pragma once

#include "../core/PresentationController.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>
#include <QPixmap>
#include <QTimer>
#include <QVideoSink>

// One decoder supplies the live audience and operator monitor. Previewing other
// slides never creates another audio player or changes this transport.
class MediaPlayback : public QObject {
    Q_OBJECT
public:
    explicit MediaPlayback(PresentationController *controller, QObject *parent = nullptr);
    ~MediaPlayback() override;
    const QPixmap &frame() const { return frame_; }
    QString status() const;
    QString errorString() const { return error_; }
    bool isVideo() const { return activeMedia_.kind == MediaKind::Video; }
    bool isPaused() const { return player_.playbackState() == QMediaPlayer::PausedState; }
    bool muted() const { return userMuted_; }
    qint64 position() const { return isVideo() ? player_.position() : 0; }
    qint64 duration() const { return isVideo() ? player_.duration() : 0; }
    void togglePause();
    void restart();
    void toggleMute();

signals:
    void frameChanged();
    void statusChanged();

private:
    PresentationController *controller_;
    QMediaPlayer player_;
    QAudioOutput audio_;
    QVideoSink sink_;
    QTimer loadTimeout_;
    QMetaObject::Connection videoFramesConnection_;
    QMetaObject::Connection videoErrorConnection_;
    QMetaObject::Connection mediaStatusConnection_;
    SlideMedia activeMedia_;
    QPixmap frame_;
    QString error_;
    quint64 cueRevision_ = 0;
    quint64 loadGeneration_ = 0;
    bool switching_ = false;
    bool loadingImage_ = false;
    bool finished_ = false;
    bool userMuted_ = false;

    void syncCue();
    void fail(const QString &message);
    void loadImage(const QString &path, quint64 generation);
    QString label() const;
};
