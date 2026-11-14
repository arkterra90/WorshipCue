#pragma once

#include "../core/MediaSlide.h"
#include <QFutureWatcher>
#include <QCache>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>

struct MediaImportResult {
    QVector<SlideMedia> media;
    QString error;
    // Absolute directory belonging only to this batch; safe to discard on DB failure.
    QString directory;
    bool canceled = false;
};
Q_DECLARE_METATYPE(MediaImportResult)

// Copies imported assets into library-owned storage. Source files stay untouched.
class MediaManager : public QObject {
    Q_OBJECT
public:
    explicit MediaManager(const QString &databaseDirectory, QObject *parent = nullptr);
    ~MediaManager() override;
    bool importFiles(const QStringList &sourcePaths, const QStringList &targetNames,
                     bool loopVideos = false);
    bool busy() const { return busy_; }
    void cancelImport();
    QString absolutePath(const QString &relativePath) const;
    bool discardImport(const QString &directory);
    QPixmap thumbnail(const QString &relativePath);
    static QStringList supportedImageExtensions();
    static QStringList supportedVideoExtensions();
    static QStringList supportedAudioExtensions();

signals:
    void importFinished(const MediaImportResult &result);
    void progressChanged(int completed, int total);
    void thumbnailReady(const QString &relativePath);

private:
    QString directory_;
    QFutureWatcher<MediaImportResult> watcher_;
    std::shared_ptr<std::atomic_bool> canceled_;
    bool busy_ = false;
    bool delivered_ = true;
    QCache<QString, QPixmap> thumbnails_{100};
    QSet<QString> pendingThumbnails_;
};
