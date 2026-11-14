#pragma once

#include <QMetaType>
#include <QString>

enum class MediaKind { None, Image, Video, Audio };

// Paths in the repository are relative to the library directory. Presentation
// snapshots may resolve them to absolute paths before being taken live.
struct SlideMedia {
    MediaKind kind = MediaKind::None;
    QString path;
    QString name;
    bool loop = false;

    bool operator==(const SlideMedia &other) const {
        return kind == other.kind && path == other.path && name == other.name && loop == other.loop;
    }
    bool operator!=(const SlideMedia &other) const { return !(*this == other); }
};

Q_DECLARE_METATYPE(MediaKind)
Q_DECLARE_METATYPE(SlideMedia)
