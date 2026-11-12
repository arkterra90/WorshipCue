#pragma once

#include <QString>
#include <QVector>

// Database paths share the managed-background namespace while the original
// artwork lives in the executable, so every library can use it offline.
namespace BuiltInBackgrounds {
struct Background {
    QString title;
    QString path;
};

inline const QVector<Background> &catalog() {
    static const QVector<Background> backgrounds{
        {QStringLiteral("Aurora Veil"), QStringLiteral("backgrounds/builtin/aurora-veil.jpg")},
        {QStringLiteral("Midnight Tide"), QStringLiteral("backgrounds/builtin/midnight-tide.jpg")},
        {QStringLiteral("Indigo Flow"), QStringLiteral("backgrounds/builtin/indigo-flow.jpg")},
        {QStringLiteral("Ember Glow"), QStringLiteral("backgrounds/builtin/ember-glow.jpg")},
        {QStringLiteral("Jade Mist"), QStringLiteral("backgrounds/builtin/jade-mist.jpg")},
        {QStringLiteral("Violet Halo"), QStringLiteral("backgrounds/builtin/violet-halo.jpg")},
        {QStringLiteral("Blue Hour"), QStringLiteral("backgrounds/builtin/blue-hour.jpg")},
        {QStringLiteral("Rose Dusk"), QStringLiteral("backgrounds/builtin/rose-dusk.jpg")},
        {QStringLiteral("Golden Hour"), QStringLiteral("backgrounds/builtin/golden-hour.jpg")},
        {QStringLiteral("Arctic Light"), QStringLiteral("backgrounds/builtin/arctic-light.jpg")},
        {QStringLiteral("Graphite Silk"), QStringLiteral("backgrounds/builtin/graphite-silk.jpg")},
        {QStringLiteral("Ocean Depths"), QStringLiteral("backgrounds/builtin/ocean-depths.jpg")},
        {QStringLiteral("Plum Arc"), QStringLiteral("backgrounds/builtin/plum-arc.jpg")},
        {QStringLiteral("Copper Lines"), QStringLiteral("backgrounds/builtin/copper-lines.jpg")},
        {QStringLiteral("Forest Light"), QStringLiteral("backgrounds/builtin/forest-light.jpg")},
        {QStringLiteral("Cobalt Ribbon"), QStringLiteral("backgrounds/builtin/cobalt-ribbon.jpg")},
        {QStringLiteral("Desert Dusk"), QStringLiteral("backgrounds/builtin/desert-dusk.jpg")},
        {QStringLiteral("Lilac Drift"), QStringLiteral("backgrounds/builtin/lilac-drift.jpg")},
        {QStringLiteral("Teal Contours"), QStringLiteral("backgrounds/builtin/teal-contours.jpg")},
        {QStringLiteral("Midnight Prism"), QStringLiteral("backgrounds/builtin/midnight-prism.jpg")}
    };
    return backgrounds;
}

// Recognize exact catalogue entries rather than treating arbitrary paths as
// resources. Existing imported background paths retain their original meaning.
inline QString resourcePath(const QString &path) {
    for (const auto &background : catalog())
        if (background.path == path) return QStringLiteral(":/") + path;
    return {};
}

inline QString title(const QString &path) {
    for (const auto &background : catalog())
        if (background.path == path) return background.title;
    return {};
}
}
