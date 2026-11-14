#pragma once
#include <QSet>
#include <QStringList>
#include <QVector>

enum class CleanupKind { Presentation, MediaAsset, File };
struct CleanupKey {
    CleanupKind kind = CleanupKind::Presentation;
    int id = 0;
    QString path;
    QString token() const { return QString::number(int(kind)) + ":" + (kind == CleanupKind::File ? path : QString::number(id)); }
};
struct UsageDates {
    qint64 createdAt = 0;
    qint64 importedAt = 0;
    qint64 modifiedAt = 0;
    qint64 lastUsedAt = 0;
};
struct CleanupProtection {
    QSet<int> songs, assets;
    QSet<QString> paths; // managed relative paths
};
struct CleanupItem {
    CleanupKey key;
    QString name;
    UsageDates dates;
    QStringList paths;
    bool eligible = false;
    QString reason;
};
struct CleanupFile {
    QString path;
    qint64 bytes = 0;
    qint64 modifiedMs = 0;
    bool removable = false;
    QString reason;
};
struct CleanupPlan {
    QVector<CleanupItem> items;
    QVector<CleanupFile> files;
    qint64 recoverableBytes = 0;
};
struct CleanupBatch {
    QString id;
    qint64 createdAt = 0;
    int itemCount = 0;
    QString state;
};
