#include "DisplayWindow.h"
#include "SlideRenderer.h"
#include "../media/MediaPlayback.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImageReader>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWindow>

DisplayWindow::DisplayWindow(Kind kind, PresentationController *controller, QWidget *parent)
    : QWidget(parent), kind_(kind), controller_(controller) {
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowTitle(kind == Kind::Audience ? QStringLiteral("WorshipCue — Audience")
                                            : QStringLiteral("WorshipCue — Stage"));
    resize(960, 540);
    setMinimumSize(320, 180);
    setAutoFillBackground(false);
    if (kind_ == Kind::Audience) {
        audienceState_ = controller_->state();
        syncBackground();
        rebuildAudienceCanvas();
        transitionTimer_.setTimerType(Qt::PreciseTimer);
        transitionTimer_.setInterval(16);
        connect(&transitionTimer_, &QTimer::timeout, this, [this] {
            transitionProgress_ = qBound(qreal(0), transitionClock_.elapsed()
                / qreal(qMax(1, audienceState_.settings.transitionDurationMs)), qreal(1));
            compositeDirty_ = true;
            if (transitionProgress_ >= 1) finishTransition();
            audienceChanged();
        });
        connect(controller_, &PresentationController::stateChanged, this, &DisplayWindow::syncAudienceState);
    } else {
        connect(controller_, &PresentationController::stateChanged, this,
                QOverload<>::of(&QWidget::update));
    }
    if (kind_ == Kind::Stage) {
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
        timer->start(1000);
    }
}

void DisplayWindow::showPreview() {
    if (isFullScreen()) showNormal();
    resize(960, 540);
    show();
    raise();
}

void DisplayWindow::showFullscreenOn(QScreen *screen) {
    if (!screen) return;
    if (isFullScreen()) showNormal();
    winId(); // Ensure there is a native QWindow to assign to the display.
    if (windowHandle()) windowHandle()->setScreen(screen);
    setGeometry(screen->geometry());
    showFullScreen();
}

void DisplayWindow::setBackgroundImage(const QString &path) {
    background_ = QPixmap(path);
    if (kind_ == Kind::Audience) {
        syncBackground();
        finishTransition();
        rebuildAudienceCanvas();
        audienceChanged();
    } else update();
}

void DisplayWindow::setMediaPlayback(MediaPlayback *playback) {
    if (playback_) disconnect(playback_, nullptr, this, nullptr);
    playback_ = playback;
    if (kind_ != Kind::Audience) return;
    if (playback_) {
        connect(playback_, &MediaPlayback::frameChanged, this, &DisplayWindow::syncMediaFrame);
        connect(playback_, &MediaPlayback::statusChanged, this, [this] {
            if (playback_ && !playback_->errorString().isEmpty()) {
                finishTransition();
                audienceChanged();
            }
        });
    }
    syncMediaFrame();
}

void DisplayWindow::audienceChanged() {
    update();
    emit audienceFrameChanged();
}

void DisplayWindow::syncBackground() {
    const QString path = audienceState_.backgroundPath;
    if (path.isEmpty()) {
        activeBackgroundPath_.clear();
        activeBackground_ = background_;
    } else if (path != activeBackgroundPath_) {
        activeBackgroundPath_ = path;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize source = reader.size();
        if (source.isValid() && (source.width() > 1920 || source.height() > 1080))
            reader.setScaledSize(source.scaled(QSize(1920, 1080), Qt::KeepAspectRatio));
        QImage image = reader.read();
        if (image.width() > 1920 || image.height() > 1080)
            image = image.scaled(QSize(1920, 1080), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        activeBackground_ = QPixmap::fromImage(image);
    }
}

void DisplayWindow::rebuildAudienceCanvas() {
    // Reuse the video canvas between decoded frames. QPixmap detaches only
    // when a newly started transition still owns the previous canvas.
    if (incomingCanvas_.isNull()) incomingCanvas_ = QPixmap(1920, 1080);
    QPainter painter(&incomingCanvas_);
    SlideRenderer::paintAudience(painter, incomingCanvas_.rect(), audienceState_, activeBackground_, mediaFrame_);
    compositeDirty_ = true;
}

void DisplayWindow::finishTransition() {
    transitionTimer_.stop();
    transitionProgress_ = 1;
    waitingForMedia_ = false;
    outgoingCanvas_ = {};
    compositeDirty_ = true;
}

void DisplayWindow::startTransition() {
    waitingForMedia_ = false;
    transitionProgress_ = 0;
    compositeDirty_ = true;
    transitionClock_.start();
    transitionTimer_.start();
}

QPixmap DisplayWindow::audienceCanvas() {
    if (waitingForMedia_) return outgoingCanvas_;
    if (!transitionTimer_.isActive()) return incomingCanvas_;
    if (compositeDirty_) {
        if (compositedCanvas_.isNull()) compositedCanvas_ = QPixmap(1920, 1080);
        QPainter painter(&compositedCanvas_);
        SlideRenderer::paintTransition(painter, compositedCanvas_.rect(), outgoingCanvas_, incomingCanvas_,
                                        audienceState_.settings.transition, transitionProgress_);
        compositeDirty_ = false;
    }
    return compositedCanvas_;
}

void DisplayWindow::syncAudienceState() {
    const auto &state = controller_->state();
    const bool newCue = state.cueRevision != audienceState_.cueRevision;
    const bool outputChanged = state.blackout != audienceState_.blackout || state.clear != audienceState_.clear;
    if (!newCue && !outputChanged) return;

    // Capture the visible composite before the media decoder clears its old
    // frame. Repeated navigation therefore starts at the current visual.
    const bool animate = newCue && !state.blackout && !state.clear && state.index >= 0
        && state.settings.isValid() && state.settings.transition != SlideTransitionType::Cut
        && state.settings.transitionDurationMs > 0;
    const QPixmap outgoing = animate ? audienceCanvas() : QPixmap{};
    finishTransition();
    audienceState_ = state;
    syncBackground();
    if (newCue) mediaFrame_ = {};
    rebuildAudienceCanvas();
    if (animate) {
        outgoingCanvas_ = outgoing;
        // Keep the outgoing visual until an asynchronous image/video decoder
        // supplies the incoming frame, avoiding a fade to an empty media slide.
        if (audienceState_.currentMedia().kind != MediaKind::None && playback_)
            waitingForMedia_ = true;
        else startTransition();
    }
    audienceChanged();
}

void DisplayWindow::syncMediaFrame() {
    // Playback may have been constructed before this window. In that case its
    // frameChanged signal arrives before our controller stateChanged callback.
    syncAudienceState();
    mediaFrame_ = playback_ ? playback_->frame() : QPixmap{};
    rebuildAudienceCanvas();
    if (waitingForMedia_ && !mediaFrame_.isNull()) startTransition();
    audienceChanged();
}

void DisplayWindow::paintAudience(QPainter &painter, const QRect &destination) {
    // Blend once in the shared logical canvas before scaling either surface.
    // Capturing this exact canvas also prevents rasterization jumps on re-cue.
    SlideRenderer::paintTransition(painter, destination, {}, audienceCanvas(), SlideTransitionType::Cut, 1);
}

void DisplayWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        emit operatorFocusRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void DisplayWindow::closeEvent(QCloseEvent *event) {
    QWidget::closeEvent(event);
    if (event->isAccepted()) emit closedByOperator();
}

void DisplayWindow::drawFittedText(QPainter &p, const QRect &rect, const QString &text,
                                   int maxPx, int minPx, const QColor &color, bool bold) {
    if (text.trimmed().isEmpty() || rect.isEmpty()) return;
    p.save();
    const int flags = Qt::AlignCenter | Qt::TextWordWrap;
    QFont font = p.font();
    font.setBold(bold);
    for (int px = maxPx; px >= minPx; px -= 2) {
        font.setPixelSize(px);
        const QRect bounds = QFontMetrics(font).boundingRect(rect, flags, text);
        if (bounds.height() <= rect.height() && bounds.width() <= rect.width()) break;
    }
    p.setFont(font);
    p.setPen(color);
    p.drawText(rect, flags, text);
    p.restore();
}

void DisplayWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (kind_ == Kind::Audience)
        paintAudience(p, rect());
    else paintStage(p);
}

void DisplayWindow::paintStage(QPainter &p) {
    const QRect r = rect();
    const auto &state = controller_->state();
    p.fillRect(r, QColor(12, 15, 21));
    const int margin = qMax(16, width() / 35);
    p.setPen(QColor(155, 165, 180));
    QFont headerFont = p.font();
    headerFont.setPixelSize(qMax(13, height() / 29));
    headerFont.setBold(true);
    p.setFont(headerFont);
    const QRect titleArea(margin, margin, int(width() * .65) - margin, height() / 12);
    p.drawText(titleArea, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(headerFont).elidedText(
                   state.title.isEmpty() ? QStringLiteral("READY") : state.title,
                   Qt::ElideRight, titleArea.width()));
    p.drawText(QRect(margin, margin, width() - 2 * margin, height() / 12),
               Qt::AlignRight | Qt::AlignVCenter,
               QDateTime::currentDateTime().toString(QStringLiteral("h:mm:ss AP")));

    p.setPen(QColor(88, 227, 143));
    p.drawText(QRect(margin, int(height() * .18), width() - 2 * margin, height() / 10),
               QStringLiteral("CURRENT"));
    // Clear and blackout apply to the audience only. Musicians keep their
    // current and next lyrics, with the audience state explicitly identified.
    const auto stageText = [](const QString &text, const SlideMedia &media) {
        if (media.kind == MediaKind::None) return text;
        const QString kind = media.kind == MediaKind::Image ? QStringLiteral("PICTURE") : QStringLiteral("VIDEO");
        return text.isEmpty() ? QStringLiteral("%1\n%2").arg(kind, media.name)
                              : QStringLiteral("%1 • %2\n%3").arg(kind, media.name, text);
    };
    const QString now = stageText(state.currentText(), state.currentMedia());
    p.setPen(state.blackout ? QColor(255, 119, 119)
                           : state.clear ? QColor(245, 194, 82) : QColor(155, 165, 180));
    p.drawText(QRect(margin, int(height() * .12), width() - 2 * margin, height() / 16),
               Qt::AlignRight | Qt::AlignVCenter,
               state.blackout ? QStringLiteral("AUDIENCE CONTENT: BLACKOUT")
                   : state.clear ? QStringLiteral("AUDIENCE CONTENT: BACKGROUND ONLY")
                                 : state.index < 0 ? QStringLiteral("AUDIENCE CONTENT: READY")
                                     : QStringLiteral("AUDIENCE CONTENT: NORMAL"));
    drawFittedText(p, QRect(margin, int(height() * .28), width() - 2 * margin, int(height() * .32)),
                   now, qMax(24, height() / 11), 18, Qt::white, true);

    p.setPen(QColor(82, 89, 100));
    p.drawLine(margin, int(height() * .68), width() - margin, int(height() * .68));
    p.setPen(QColor(245, 194, 82));
    p.drawText(QRect(margin, int(height() * .71), width() - 2 * margin, height() / 11),
               QFontMetrics(headerFont).elidedText(
                   state.nextTitle().isEmpty() || state.nextTitle() == state.title
                       ? QStringLiteral("NEXT")
                       : QStringLiteral("NEXT • %1").arg(state.nextTitle()),
                   Qt::ElideRight, width() - 2 * margin));
    drawFittedText(p, QRect(margin, int(height() * .79), width() - 2 * margin, int(height() * .17)),
                   state.nextText().isEmpty() && state.nextMedia().kind == MediaKind::None
                       ? QStringLiteral("—") : stageText(state.nextText(), state.nextMedia()),
                   qMax(20, height() / 17), 14, QColor(245, 194, 82));
}
