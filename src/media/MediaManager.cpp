#include "MediaManager.h"
#include "BatchRenamer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

namespace {
struct BatchCleanup {
    QString path;
    bool keep = false;
    ~BatchCleanup() { if (!keep && !path.isEmpty()) QDir(path).removeRecursively(); }
};
}

MediaManager::MediaManager(const QString &databaseDirectory, QObject *parent)
    : QObject(parent), directory_(QDir(databaseDirectory).absolutePath()) {
    qRegisterMetaType<MediaImportResult>();
    connect(&watcher_, &QFutureWatcher<MediaImportResult>::finished, this, [this] {
        busy_ = false;
        delivered_ = true;
        emit importFinished(watcher_.result());
    });
}

MediaManager::~MediaManager() {
    disconnect(&watcher_, nullptr, this, nullptr);
    cancelImport();
    // Only shutdown waits: imports themselves run off the GUI thread. Worker
    // callbacks retain this receiver only until this wait completes.
    watcher_.waitForFinished();
    if (watcher_.future().isValid() && watcher_.future().resultCount()) {
        const auto result = watcher_.result();
        if (result.canceled || !delivered_) discardImport(result.directory);
    }
}

QStringList MediaManager::supportedImageExtensions() {
    QStringList result;
    for (const auto &format : QImageReader::supportedImageFormats()) result.append(QString::fromLatin1(format).toLower());
    result.removeDuplicates();
    return result;
}
QStringList MediaManager::supportedVideoExtensions() {
    return {QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("m4v"), QStringLiteral("webm"), QStringLiteral("avi"), QStringLiteral("mkv")};
}

QString MediaManager::absolutePath(const QString &relativePath) const {
    if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath)) return {};
    if (QFileInfo(QDir(directory_).filePath(QStringLiteral("media"))).isSymLink()) return {};
    const QString clean = QDir::cleanPath(relativePath);
    if (!clean.startsWith(QStringLiteral("media/")) || clean.contains(QLatin1Char('\\'))
        || clean == QStringLiteral("..") || clean.startsWith(QStringLiteral("../"))) return {};
    const QString absolute = QDir(directory_).absoluteFilePath(clean);
    const QString canonical = QFileInfo(absolute).canonicalFilePath();
    const QString root = QFileInfo(directory_).canonicalFilePath();
    if (!canonical.isEmpty() && !root.isEmpty() && !canonical.startsWith(root + QStringLiteral("/media/"))) return {};
    return absolute;
}

bool MediaManager::discardImport(const QString &directory) {
    if (directory.isEmpty()) return false;
    const QFileInfo info(directory);
    const QDir media(QDir(directory_).filePath(QStringLiteral("media")));
    const QString expectedParent = media.absolutePath();
    if (info.isSymLink() || info.absolutePath() != expectedParent
        || QUuid(info.fileName()).isNull()) return false;
    return !info.exists() || QDir(info.absoluteFilePath()).removeRecursively();
}

QPixmap MediaManager::thumbnail(const QString &relativePath) {
    if (const auto *cached = thumbnails_.object(relativePath)) return *cached;
    if (pendingThumbnails_.contains(relativePath)) return {};
    const QString path = absolutePath(relativePath);
    if (path.isEmpty() || !supportedImageExtensions().contains(QFileInfo(path).suffix().toLower())) return {};
    pendingThumbnails_.insert(relativePath);
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, relativePath] {
        thumbnails_.insert(relativePath, new QPixmap(QPixmap::fromImage(watcher->result())));
        pendingThumbnails_.remove(relativePath);
        watcher->deleteLater();
        emit thumbnailReady(relativePath);
    });
    watcher->setFuture(QtConcurrent::run([path] {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize dimensions = reader.size();
        if (dimensions.isValid()) reader.setScaledSize(dimensions.scaled(QSize(512, 288), Qt::KeepAspectRatio));
        return reader.read();
    }));
    return {};
}

void MediaManager::cancelImport() {
    if (canceled_) canceled_->store(true);
}

bool MediaManager::importFiles(const QStringList &sourcePaths, const QStringList &targetNames, bool loopVideos) {
    if (busy()) return false;
    busy_ = true;
    delivered_ = false;
    const QString root = directory_;
    const QStringList imageExtensions = supportedImageExtensions();
    const QStringList videoExtensions = supportedVideoExtensions();
    canceled_ = std::make_shared<std::atomic_bool>(false);
    const auto canceled = canceled_;
    watcher_.setFuture(QtConcurrent::run([this, root, sourcePaths, targetNames, loopVideos,
                                        imageExtensions, videoExtensions, canceled] {
        MediaImportResult result;
        BatchCleanup cleanup;
        auto fail = [&](const QString &message) {
            result.error = message;
            result.media.clear();
            return result;
        };
        auto checkCanceled = [&] {
            if (!canceled->load()) return false;
            result.canceled = true;
            return true;
        };
        if (sourcePaths.size() != targetNames.size())
            return fail(QStringLiteral("The file list and naming preview no longer match."));
        QString validation;
        if (!BatchRenamer::validateNames(targetNames, &validation)) return fail(validation);
        if (checkCanceled()) return fail(QStringLiteral("Import canceled."));
        if (QFileInfo(QDir(root).filePath(QStringLiteral("media"))).isSymLink())
            return fail(QStringLiteral("The media folder must be a folder inside this library, not a symbolic link."));
        const QString batch = QUuid::createUuid().toString(QUuid::WithoutBraces);
        result.directory = QDir(root).filePath(QStringLiteral("media/") + batch);
        if (QFileInfo::exists(result.directory) || !QDir().mkpath(result.directory))
            return fail(QStringLiteral("Could not create the media folder."));
        cleanup.path = result.directory;
        for (int index = 0; index < sourcePaths.size(); ++index) {
            if (checkCanceled()) return fail(QStringLiteral("Import canceled."));
            const QFileInfo sourceInfo(sourcePaths.at(index));
            if (!sourceInfo.isFile() || !sourceInfo.isReadable() || sourceInfo.size() <= 0)
                return fail(QStringLiteral("“%1” is missing, empty, or cannot be read.").arg(sourceInfo.fileName()));
            const QString extension = sourceInfo.suffix().toLower();
            MediaKind kind = MediaKind::None;
            if (imageExtensions.contains(extension)) {
                QImageReader reader(sourceInfo.absoluteFilePath());
                reader.setAutoTransform(true);
                const QSize dimensions = reader.size();
                if (dimensions.isValid()) reader.setScaledSize(dimensions.scaled(QSize(256, 256), Qt::KeepAspectRatio));
                if (!reader.canRead() || reader.read().isNull())
                    return fail(QStringLiteral("“%1” is not a readable image: %2").arg(sourceInfo.fileName(), reader.errorString()));
                kind = MediaKind::Image;
            } else if (videoExtensions.contains(extension)) {
                kind = MediaKind::Video;
            } else {
                return fail(QStringLiteral("“%1” is not a supported image or video file.").arg(sourceInfo.fileName()));
            }
            // Preserve the original encoding suffix; renaming never converts a file.
            if (QFileInfo(targetNames.at(index)).suffix().compare(sourceInfo.suffix(), Qt::CaseInsensitive) != 0)
                return fail(QStringLiteral("Keep the original extension when naming “%1”.").arg(sourceInfo.fileName()));
            const QString target = QDir(result.directory).filePath(targetNames.at(index));
            if (QFileInfo::exists(target)) return fail(QStringLiteral("A media filename already exists. Nothing was overwritten."));
            QFile source(sourceInfo.absoluteFilePath());
            QSaveFile destination(target);
            destination.setDirectWriteFallback(false);
            if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly))
                return fail(QStringLiteral("Could not copy “%1”: %2").arg(sourceInfo.fileName(), destination.errorString()));
            while (!source.atEnd()) {
                if (checkCanceled()) { destination.cancelWriting(); return fail(QStringLiteral("Import canceled.")); }
                const QByteArray chunk = source.read(1024 * 1024);
                if (chunk.isEmpty() && source.error() != QFileDevice::NoError)
                    return fail(QStringLiteral("Could not read “%1”: %2").arg(sourceInfo.fileName(), source.errorString()));
                if (destination.write(chunk) != chunk.size())
                    return fail(QStringLiteral("Could not save “%1”: %2").arg(targetNames.at(index), destination.errorString()));
            }
            if (!destination.commit()) return fail(QStringLiteral("Could not finish saving “%1”: %2").arg(targetNames.at(index), destination.errorString()));
            result.media.append({kind, QStringLiteral("media/") + batch + QLatin1Char('/') + targetNames.at(index),
                                 targetNames.at(index), kind == MediaKind::Video && loopVideos});
            QMetaObject::invokeMethod(this, [this, index, sourcePaths] {
                emit progressChanged(index + 1, sourcePaths.size());
            }, Qt::QueuedConnection);
        }
        if (checkCanceled()) return fail(QStringLiteral("Import canceled."));
        cleanup.keep = true;
        return result;
    }));
    return true;
}
