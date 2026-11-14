#pragma once

#include "../core/PresentationController.h"
#include "OutputConfiguration.h"
#include "StageLayout.h"

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
    int countdownSeconds() const { return countdownSeconds_; }
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
    Kind kind_;
    OutputConfiguration configuration_;
    StageLayout stageLayout_ = StageLayout::standard();
    QPointer<DisplayWindow> sharedAudience_;
    QString stageMessage_;
    int countdownSeconds_ = 300;
    qint64 countdownRemainingMs_ = 300000;
    bool countdownRunning_ = false;
    QElapsedTimer countdownClock_;
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
