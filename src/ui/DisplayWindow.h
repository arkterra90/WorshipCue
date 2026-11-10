#pragma once

#include "../core/PresentationController.h"

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
    DisplayWindow(Kind kind, PresentationController *controller, QWidget *parent = nullptr);
    void showPreview();
    void showFullscreenOn(QScreen *screen);
    void setBackgroundImage(const QString &path);
    const QPixmap &backgroundPixmap() const { return background_; }
    void setMediaPlayback(MediaPlayback *playback);
    // The operator's live monitor paints this same compositor and animation.
    void paintAudience(QPainter &painter, const QRect &destination);

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
    PresentationController *controller_;
    QPixmap background_;
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
    void syncAudienceState();
    void syncMediaFrame();
    void rebuildAudienceCanvas();
    void startTransition();
    void finishTransition();
    void audienceChanged();
    QPixmap audienceCanvas();
    void paintStage(QPainter &p);
    void drawFittedText(QPainter &p, const QRect &rect, const QString &text,
                        int maxPx, int minPx, const QColor &color, bool bold = false);
};
