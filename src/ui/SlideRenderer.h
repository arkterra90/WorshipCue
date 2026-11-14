#pragma once

#include "../core/PresentationController.h"

#include <QPixmap>
#include <QRect>
#include <QSize>

class QPainter;

// All audience surfaces use the same 16:9 canvas, including operator thumbnails.
// Rendering in fixed logical coordinates keeps line wrapping and margins identical
// at projection, preview, and thumbnail sizes.
class SlideRenderer {
public:
    static QPixmap backgroundImage(const QString &path);
    static void paintAudience(QPainter &painter, const QRect &destination,
                              const LivePresentation &state,
                              const QPixmap &background = {},
                              const QPixmap &mediaFrame = {});
    // Composite already-rendered 16:9 canvases. Both live surfaces share the
    // same progress value; letterbox bars never move with a slide transition.
    static void paintTransition(QPainter &painter, const QRect &destination,
                                const QPixmap &outgoing, const QPixmap &incoming,
                                SlideTransitionType type, qreal progress);
    static QPixmap thumbnail(const QString &text, const QSize &size,
                             const QPixmap &background = {}, const QString &richText = {}, const SlideLook &look = {});
    // For thumbnails only: a bounded, cached still decode or a video label.
    // paintAudience never accesses the filesystem.
    static QPixmap thumbnail(const QString &text, const SlideMedia &media,
                             const QSize &size, const QPixmap &background = {},
                             const QString &richText = {}, const SlideLook &look = {});
};
