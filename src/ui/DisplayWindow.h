#pragma once

#include "../core/PresentationController.h"
#include "OutputConfiguration.h"
#include "StageLayout.h"
#include "../core/StageTimer.h"

#include <QPainter>
#include <QElapsedTimer>
#include <QPixmap>
#include <QPointer>
#include <QWidget>
#include <QTimer>

class QScreen;
class QCloseEvent;
class MediaPlayback;

class DisplayWindow : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Audience, Stage };
    DisplayWindow(Kind kind, PresentationController *controller, QWidget *parent = nullptr, DisplayWindow *sharedAudience = nullptr);
    void setLiveAuthorizationCheck(std::function<bool()> check) { authorizationCheck_ = std::move(check); }
    void showPreview();
    void showFullscreenOn(QScreen *screen);
    void setBackgroundImage(const QString &path);
    const QPixmap &backgroundPixmap() const { return background_; }
    void setMediaPlayback(MediaPlayback *playback);
    // The operator's live monitor paints this same compositor and animation.
    void paintAudience(QPainter &painter, const QRect &destination);
    void paintOutput(QPainter &painter, const QRect &destination, const OutputConfiguration *preview = nullptr, const StageLayout *layoutPreview = nullptr);
    bool setConfiguration(const OutputConfiguration &configuration, QString *error = nullptr);
    const OutputConfiguration &configuration() const { return configuration_; }
    void setStageLayout(const StageLayout &layout);
    const StageLayout &stageLayout() const { return stageLayout_; }
    bool setStageMessage(const QString &text);
    void setCountdownSeconds(int seconds);
    void startCountdown();
    void pauseCountdown();
    void resumeCountdown();
    void resetCountdown();
    StageRuntime stageRuntime() const;
    int countdownSeconds() const { return stageTimer_.configuration().durationSeconds; }
    const StageTimerConfiguration &stageTimerConfiguration() const { return stageTimer_.configuration(); }
    StageTimerReading stageTimerReading() const { return stageTimer_.reading(); }
    bool configureStageTimer(const StageTimerConfiguration &, QString *error = nullptr);
    void setStageTimerVisible(bool visible, bool overlay = true);
    bool stageTimerVisible() const;
    bool stageTimerOverlayEnabled() const { return timerOverlay_; }
    bool stageTimerElementsVisible() const { return timerVisible_; }
    const LivePresentation &presentationState() const { return controller_->state(); }
    bool sharesAudienceWith(const DisplayWindow *window) const { return sharedAudience_ == window; }

signals:
    void operatorFocusRequested();
    void closedByOperator();
    void audienceFrameChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    std::function<bool()> authorizationCheck_;
    bool canShowLive() const { return authorizationCheck_ && authorizationCheck_(); }
    Kind kind_;
    OutputConfiguration configuration_;
    StageLayout stageLayout_ = StageLayout::standard();
    QPointer<DisplayWindow> sharedAudience_;
    QString stageMessage_;
    StageTimer stageTimer_;
    bool timerVisible_ = true;
    bool timerOverlay_ = false;
    PresentationController *controller_;
    QPixmap background_;
    QPixmap activeBackground_;
    QString activeBackgroundPath_;
    QPointer<MediaPlayback> playback_;
    LivePresentation audienceState_;
    QPixmap mediaFrame_;
    QPixmap incomingCanvas_;
    QPixmap outgoingCanvas_;
    QPixmap compositedCanvas_;
    bool compositeDirty_ = true;
    QTimer transitionTimer_;
    QElapsedTimer transitionClock_;
    qreal transitionProgress_ = 1;
    bool waitingForMedia_ = false;
    SlideTransitionType transitionType_ = SlideTransitionType::Cut;
    int transitionDurationMs_ = 0;
    bool dynamicOutgoing_ = false;
    LivePresentation outgoingState_;
    void syncAudienceState();
    void syncBackground();
    void syncMediaFrame();
    void rebuildAudienceCanvas();
    void startTransition();
    void finishTransition();
    void audienceChanged();
    QPixmap audienceCanvas();
    void paintStage(QPainter &p, const QRect &canvas);
    void drawFittedText(QPainter &p, const QRect &rect, const QString &text,
                        int maxPx, int minPx, const QColor &color, bool bold = false);
};
