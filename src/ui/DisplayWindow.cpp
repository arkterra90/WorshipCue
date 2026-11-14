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

DisplayWindow::DisplayWindow(Kind kind, PresentationController *controller, QWidget *parent, DisplayWindow *sharedAudience)
    : QWidget(parent), kind_(kind), sharedAudience_(sharedAudience), controller_(controller) {
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowTitle(kind == Kind::Audience ? QStringLiteral("WorshipCue — Audience")
                                            : QStringLiteral("WorshipCue — Stage"));
    resize(960, 540);
    setMinimumSize(320, 180);
    setAutoFillBackground(false);
    if (kind_ == Kind::Audience && sharedAudience_) {
        connect(sharedAudience_, &DisplayWindow::audienceFrameChanged, this, [this] { audienceChanged(); });
    } else if (kind_ == Kind::Audience) {
        audienceState_ = controller_->state();
        syncBackground();
        rebuildAudienceCanvas();
        transitionTimer_.setTimerType(Qt::PreciseTimer);
        transitionTimer_.setInterval(16);
        connect(&transitionTimer_, &QTimer::timeout, this, [this] {
            transitionProgress_ = qBound(qreal(0), transitionClock_.elapsed()
                / qreal(qMax(1, transitionDurationMs_)), qreal(1));
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
    if (sharedAudience_) return;
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
    const QString path = audienceState_.currentBackgroundPath();
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
    dynamicOutgoing_ = false;
    outgoingState_ = {};
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
                                        transitionType_, transitionProgress_);
        compositeDirty_ = false;
    }
    return compositedCanvas_;
}

void DisplayWindow::syncAudienceState() {
    const auto &state = controller_->state();
    const bool newCue = state.cueRevision != audienceState_.cueRevision;
    const bool outputChanged = state.blackout != audienceState_.blackout || state.clear != audienceState_.clear;
    if (!newCue && !outputChanged) {
        const bool textChanged = state.currentText() != audienceState_.currentText()
            || state.currentRichText() != audienceState_.currentRichText()
            || state.currentLook() != audienceState_.currentLook();
        audienceState_ = state;
        if (textChanged) { rebuildAudienceCanvas(); audienceChanged(); }
        return;
    }

    // Capture the visible composite before the media decoder clears its old
    // frame. Repeated navigation therefore starts at the current visual.
    const bool interrupted = transitionTimer_.isActive() || waitingForMedia_;
    const bool animate = (state.index >= 0 || outputChanged)
        && state.settings.isValid() && state.settings.transition != SlideTransitionType::Cut
        && state.settings.transitionDurationMs > 0;
    const QPixmap outgoing = animate ? audienceCanvas() : QPixmap{};
    const LivePresentation previousState = audienceState_;
    finishTransition();
    audienceState_ = state;
    syncBackground();
    if (newCue) mediaFrame_ = {};
    rebuildAudienceCanvas();
    if (animate) {
        outgoingCanvas_ = outgoing;
        transitionType_ = state.settings.transition;
        transitionDurationMs_ = state.settings.transitionDurationMs;
        dynamicOutgoing_ = outputChanged && !newCue && !interrupted;
        outgoingState_ = previousState;
        // Keep the outgoing visual until an asynchronous image/video decoder
        // supplies the incoming frame, avoiding a fade to an empty media slide.
        if (newCue && (audienceState_.currentMedia().kind == MediaKind::Image
             || audienceState_.currentMedia().kind == MediaKind::Video) && playback_)
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
    if (dynamicOutgoing_) {
        // Output-state fades use the current shared frame on both sides, so
        // clearing lyrics preserves moving media throughout the fade.
        QPainter painter(&outgoingCanvas_);
        SlideRenderer::paintAudience(painter, outgoingCanvas_.rect(), outgoingState_, activeBackground_, mediaFrame_);
        compositeDirty_ = true;
    }
    if (waitingForMedia_ && !mediaFrame_.isNull()) startTransition();
    audienceChanged();
}

void DisplayWindow::paintAudience(QPainter &painter, const QRect &destination) {
    if (sharedAudience_) { sharedAudience_->paintAudience(painter, destination); return; }
    // Blend once in the shared logical canvas before scaling either surface.
    // Capturing this exact canvas also prevents rasterization jumps on re-cue.
    SlideRenderer::paintTransition(painter, destination, {}, audienceCanvas(), SlideTransitionType::Cut, 1);
}

bool DisplayWindow::setConfiguration(const OutputConfiguration &configuration, QString *error) {
    if (!configuration.validate(error)) return false;
    configuration_ = configuration;
    audienceChanged();
    return true;
}

void DisplayWindow::setStageLayout(const StageLayout &layout) {
    if (layout.validate()) { stageLayout_ = layout; update(); }
}
bool DisplayWindow::setStageMessage(const QString &text) {
    if (text.size() > 4000) return false;
    stageMessage_ = text; update(); return true;
}
void DisplayWindow::setCountdownSeconds(int seconds) {
    if (seconds < 1 || seconds > 86400) return;
    countdownSeconds_ = seconds; resetCountdown();
}
void DisplayWindow::startCountdown() { countdownRemainingMs_ = qint64(countdownSeconds_) * 1000; countdownClock_.start(); countdownRunning_ = true; update(); }
void DisplayWindow::pauseCountdown() { countdownRemainingMs_ = stageRuntime().remainingMs; countdownRunning_ = false; update(); }
void DisplayWindow::resumeCountdown() { if (countdownRemainingMs_ > 0) { countdownClock_.start(); countdownRunning_ = true; update(); } }
void DisplayWindow::resetCountdown() { countdownRemainingMs_ = qint64(countdownSeconds_) * 1000; countdownRunning_ = false; update(); }
StageRuntime DisplayWindow::stageRuntime() const {
    const qint64 remaining = qMax<qint64>(0, countdownRemainingMs_ - (countdownRunning_ ? countdownClock_.elapsed() : 0));
    return {stageMessage_, remaining, countdownRunning_ && remaining > 0};
}

void DisplayWindow::paintOutput(QPainter &p, const QRect &destination, const OutputConfiguration *preview, const StageLayout *layoutPreview) {
    const auto &configuration = preview ? *preview : configuration_;
    const auto &layout = layoutPreview ? *layoutPreview : stageLayout_;
    p.save(); p.setClipRect(destination, Qt::IntersectClip); p.fillRect(destination, Qt::black);
    const QSize logical = configuration.canvas;
    const bool quarter = configuration.rotation == 90 || configuration.rotation == 270;
    const QSize rotated = quarter ? logical.transposed() : logical;
    const auto fitted = outputContentRect(destination, qreal(rotated.width()) / rotated.height(), configuration.scaling);
    p.translate(fitted.center());
    p.scale(fitted.width() / rotated.width(), fitted.height() / rotated.height());
    p.rotate(configuration.rotation);
    p.translate(-logical.width() / 2., -logical.height() / 2.);
    const QRectF canvas(0, 0, logical.width(), logical.height());
    p.setClipRect(canvas, Qt::IntersectClip);
    const auto content = outputContentRect(canvas, qreal(configuration.aspectWidth) / configuration.aspectHeight, configuration.scaling);
    p.translate(content.topLeft());
    if (kind_ == Kind::Audience) {
        // The audience source has one shared transition/decode pipeline. Each
        // output maps that canvas independently without restarting playback.
        p.scale(content.width() / 1920., content.height() / 1080.);
        paintAudience(p, QRect(0, 0, 1920, 1080));
    } else if (layout.id == QStringLiteral("standard")) {
        paintStage(p, QRect(0, 0, qRound(content.width()), qRound(content.height())));
    } else {
        paintStageLayout(p, QRectF(0, 0, content.width(), content.height()), layout, controller_->state(), stageRuntime());
    }
    p.restore();
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
    paintOutput(p, rect());
}

void DisplayWindow::paintStage(QPainter &p, const QRect &r) {
    const auto &state = controller_->state();
    p.fillRect(r, QColor(12, 15, 21));
    const int margin = qMax(16, r.width() / 35);
    p.setPen(QColor(155, 165, 180));
    QFont headerFont = p.font();
    headerFont.setPixelSize(qMax(13, r.height() / 29));
    headerFont.setBold(true);
    p.setFont(headerFont);
    const QRect titleArea(margin, margin, int(r.width() * .65) - margin, r.height() / 12);
    p.drawText(titleArea, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(headerFont).elidedText(
                   state.title.isEmpty() ? QStringLiteral("READY") : state.title,
                   Qt::ElideRight, titleArea.width()));
    p.drawText(QRect(margin, margin, r.width() - 2 * margin, r.height() / 12),
               Qt::AlignRight | Qt::AlignVCenter,
               QDateTime::currentDateTime().toString(QStringLiteral("h:mm:ss AP")));

    p.setPen(QColor(88, 227, 143));
    p.drawText(QRect(margin, int(r.height() * .18), r.width() - 2 * margin, r.height() / 10),
               QStringLiteral("CURRENT"));
    // Clear and blackout apply to the audience only. Musicians keep their
    // current and next lyrics, with the audience state explicitly identified.
    const auto stageText = [](const QString &text, const SlideMedia &media) {
        if (media.kind == MediaKind::None) return text;
        const QString kind = media.kind == MediaKind::Image ? QStringLiteral("PICTURE")
            : media.kind == MediaKind::Audio ? QStringLiteral("AUDIO") : QStringLiteral("VIDEO");
        return text.isEmpty() ? QStringLiteral("%1\n%2").arg(kind, media.name)
                              : QStringLiteral("%1 • %2\n%3").arg(kind, media.name, text);
    };
    const QString now = stageText(state.currentText(), state.currentMedia());
    p.setPen(state.blackout ? QColor(255, 119, 119)
                           : state.clear ? QColor(245, 194, 82) : QColor(155, 165, 180));
    p.drawText(QRect(margin, int(r.height() * .12), r.width() - 2 * margin, r.height() / 16),
               Qt::AlignRight | Qt::AlignVCenter,
               state.blackout ? QStringLiteral("AUDIENCE CONTENT: BLACKOUT")
                   : state.clear ? QStringLiteral("AUDIENCE CONTENT: BACKGROUND ONLY")
                                 : state.index < 0 ? QStringLiteral("AUDIENCE CONTENT: READY")
                                     : QStringLiteral("AUDIENCE CONTENT: NORMAL"));
    drawFittedText(p, QRect(margin, int(r.height() * .28), r.width() - 2 * margin, int(r.height() * .32)),
                   now, qMax(24, r.height() / 11), 18, Qt::white, true);

    p.setPen(QColor(82, 89, 100));
    p.drawLine(margin, int(r.height() * .68), r.width() - margin, int(r.height() * .68));
    p.setPen(QColor(245, 194, 82));
    p.drawText(QRect(margin, int(r.height() * .71), r.width() - 2 * margin, r.height() / 11),
               QFontMetrics(headerFont).elidedText(
                   state.nextTitle().isEmpty() || state.nextTitle() == state.title
                       ? QStringLiteral("NEXT")
                       : QStringLiteral("NEXT • %1").arg(state.nextTitle()),
                   Qt::ElideRight, r.width() - 2 * margin));
    drawFittedText(p, QRect(margin, int(r.height() * .79), r.width() - 2 * margin, int(r.height() * .17)),
                   state.nextText().isEmpty() && state.nextMedia().kind == MediaKind::None
                       ? QStringLiteral("—") : stageText(state.nextText(), state.nextMedia()),
                   qMax(20, r.height() / 17), 14, QColor(245, 194, 82));
}
