#include "SlideRenderer.h"
#include "RichTextSupport.h"

#include <QFontDatabase>
#include <QDateTime>
#include <QFontMetrics>
#include <QFileInfo>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmapCache>

namespace {
constexpr int CanvasWidth = 1920;
constexpr int CanvasHeight = 1080;

QPixmap thumbnailImage(const QString &path) {
    const QFileInfo info(path);
    const QString key = QStringLiteral("worshipcue-image:%1:%2:%3")
        .arg(info.absoluteFilePath()).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
    QPixmap image;
    if (QPixmapCache::find(key, &image)) return image;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    if (source.isValid() && (source.width() > 1920 || source.height() > 1080))
        reader.setScaledSize(source.scaled(QSize(1920, 1080), Qt::KeepAspectRatio));
    QImage decoded = reader.read();
    if (decoded.width() > 1920 || decoded.height() > 1080)
        decoded = decoded.scaled(QSize(1920, 1080), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image = QPixmap::fromImage(decoded);
    if (!image.isNull()) QPixmapCache::insert(key, image);
    return image;
}

void drawLyrics(QPainter &painter, const QString &text) {
    if (text.trimmed().isEmpty()) return;
    const QRect textArea(154, 130, 1612, 820);
    constexpr int flags = Qt::AlignCenter | Qt::TextWordWrap;
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setBold(true);
    // Evaluate in the same logical coordinates at every surface size. A small
    // thumbnail must never acquire different wrapping from the live output.
    for (int pixels = 112; pixels >= 12; pixels -= 2) {
        font.setPixelSize(pixels);
        const QRect bounds = QFontMetrics(font).boundingRect(textArea, flags, text);
        if (bounds.width() <= textArea.width() && bounds.height() <= textArea.height()) break;
    }
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.save();
    painter.setClipRect(textArea);
    painter.drawText(textArea, flags, text);
    painter.restore();
}
}

QPixmap SlideRenderer::backgroundImage(const QString &path) {
    return path.isEmpty() ? QPixmap{} : thumbnailImage(path);
}

void SlideRenderer::paintAudience(QPainter &painter, const QRect &destination,
                                  const LivePresentation &state,
                                  const QPixmap &background,
                                  const QPixmap &mediaFrame) {
    if (destination.isEmpty()) return;
    painter.save();
    painter.setClipRect(destination, Qt::IntersectClip);
    painter.fillRect(destination, Qt::black);
    if (state.blackout) {
        painter.restore();
        return;
    }

    const qreal scale = qMin(destination.width() / qreal(CanvasWidth),
                             destination.height() / qreal(CanvasHeight));
    painter.translate(destination.x() + (destination.width() - CanvasWidth * scale) / 2,
                      destination.y() + (destination.height() - CanvasHeight * scale) / 2);
    painter.scale(scale, scale);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRect canvas(0, 0, CanvasWidth, CanvasHeight);
    painter.setClipRect(canvas, Qt::IntersectClip);

    // A presentation background is a separate layer. Pictures and video sit
    // above it, so fitted media can reveal the background around its edges.
    const auto look = state.currentLook();
    if (look.id > 0 && look.settings.background == PresentationLook::Background::Color) {
        painter.fillRect(canvas, QColor(look.settings.backgroundColor));
    } else if (!background.isNull()) {
        const qreal coverScale = qMax(CanvasWidth / qreal(background.width()),
                                      CanvasHeight / qreal(background.height()));
        const QSizeF cover(background.width() * coverScale, background.height() * coverScale);
        const QRectF target((CanvasWidth - cover.width()) / 2,
                            (CanvasHeight - cover.height()) / 2,
                            cover.width(), cover.height());
        painter.drawPixmap(target, background, QRectF(background.rect()));
        painter.fillRect(canvas, QColor(0, 0, 0, 100));
    } else {
        QLinearGradient gradient(0, 0, CanvasWidth, CanvasHeight);
        gradient.setColorAt(0, QColor(12, 21, 44));
        gradient.setColorAt(1, QColor(21, 44, 67));
        painter.fillRect(canvas, gradient);
    }

    if (state.currentMedia().kind != MediaKind::None && !mediaFrame.isNull()) {
        // Only the background is dimmed. Full-canvas JPEGs cover it completely
        // and media retains its original brightness and aspect ratio.
        const QSizeF fitted = mediaFrame.size().scaled(canvas.size(), Qt::KeepAspectRatio);
        const QRectF target((CanvasWidth - fitted.width()) / 2,
                            (CanvasHeight - fitted.height()) / 2,
                            fitted.width(), fitted.height());
        painter.drawPixmap(target, mediaFrame, QRectF(mediaFrame.rect()));
    }

    if (!state.clear) {
        if (look.id > 0) RichTextSupport::paint(painter, look.settings.textRect(), state.currentText(), state.currentRichText(), look);
        else if (state.currentRichText().isEmpty()) drawLyrics(painter, state.currentText());
        else RichTextSupport::paint(painter, QRectF(154, 130, 1612, 820), state.currentText(), state.currentRichText());
    }
    painter.restore();
}

QPixmap SlideRenderer::thumbnail(const QString &text, const QSize &size,
                                 const QPixmap &background, const QString &richText, const SlideLook &look) {
    if (size.isEmpty()) return {};
    QPixmap result(size);
    result.fill(Qt::black);
    LivePresentation state;
    state.slides = {text};
    state.richText = {richText};
    state.looks = {look};
    state.index = 0;
    QPainter painter(&result);
    paintAudience(painter, result.rect(), state, background);
    return result;
}

void SlideRenderer::paintTransition(QPainter &painter, const QRect &destination,
                                     const QPixmap &outgoing, const QPixmap &incoming,
                                     SlideTransitionType type, qreal progress) {
    if (destination.isEmpty()) return;
    progress = qBound(qreal(0), progress, qreal(1));
    const qreal scale = qMin(destination.width() / qreal(CanvasWidth),
                             destination.height() / qreal(CanvasHeight));
    const QRectF canvas(destination.x() + (destination.width() - CanvasWidth * scale) / 2,
                        destination.y() + (destination.height() - CanvasHeight * scale) / 2,
                        CanvasWidth * scale, CanvasHeight * scale);
    painter.save();
    painter.setClipRect(destination, Qt::IntersectClip);
    painter.fillRect(destination, Qt::black);
    painter.setClipRect(canvas, Qt::IntersectClip);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto draw = [&](const QPixmap &image, qreal offset = 0) {
        if (!image.isNull()) painter.drawPixmap(canvas.translated(offset, 0), image, QRectF(image.rect()));
    };
    if (type == SlideTransitionType::Cut || progress >= 1) {
        draw(incoming);
    } else if (progress <= 0) {
        draw(outgoing);
    } else if (type == SlideTransitionType::Fade) {
        draw(outgoing);
        painter.setOpacity(painter.opacity() * progress);
        draw(incoming);
    } else {
        const qreal direction = type == SlideTransitionType::SlideLeft ? -1 : 1;
        draw(outgoing, direction * progress * canvas.width());
        draw(incoming, -direction * (1 - progress) * canvas.width());
    }
    painter.restore();
}

QPixmap SlideRenderer::thumbnail(const QString &text, const SlideMedia &media,
                                 const QSize &size, const QPixmap &background,
                                 const QString &richText, const SlideLook &look) {
    if (media.kind == MediaKind::None) return thumbnail(text, size, background, richText, look);
    if (size.isEmpty()) return {};
    QPixmap result(size);
    result.fill(Qt::black);
    LivePresentation state;
    state.slides = {text};
    state.richText = {richText};
    state.looks = {look};
    state.media = {media};
    state.index = 0;
    const QPixmap frame = media.kind == MediaKind::Image ? thumbnailImage(media.path) : QPixmap{};
    QPainter painter(&result);
    paintAudience(painter, result.rect(), state, background, frame);
    if (frame.isNull()) {
        QFont font = painter.font();
        font.setPixelSize(qMax(12, size.height() / 10));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor(180, 195, 214));
        painter.drawText(result.rect().adjusted(10, 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap,
                         QStringLiteral("%1\n%2").arg(media.kind == MediaKind::Audio ? QStringLiteral("AUDIO")
                             : media.kind == MediaKind::Video ? QStringLiteral("VIDEO")
                                                                                  : QStringLiteral("MISSING PICTURE"), media.name));
    }
    return result;
}
