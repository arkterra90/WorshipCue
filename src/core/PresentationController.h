#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include "MediaSlide.h"
#include "PresentationSettings.h"

struct PresentationItem {
    int entryId = 0;
    QString title;
    QStringList slides;
    QVector<SlideMedia> media;
    PresentationSettings settings;
    // Absolute at the playback boundary; the library stores managed relative paths.
    QString backgroundPath;
    QStringList richText;
};

struct LivePresentation {
    QString title;
    QStringList slides;
    int index = -1;
    bool blackout = false;
    bool clear = false;
    int serviceId = 0;
    int entryId = 0;
    // The first slide of the next nonempty service item, when there is one.
    QString upcomingTitle;
    QString upcomingText;
    QVector<SlideMedia> media;
    SlideMedia upcomingMedia;
    quint64 cueRevision = 0;
    PresentationSettings settings;
    QString backgroundPath;
    QString upcomingBackgroundPath;
    QStringList richText;
    QString upcomingRichText;

    QString currentText() const {
        return index >= 0 && index < slides.size() ? slides.at(index) : QString{};
    }
    QString currentRichText() const {
        return index >= 0 && index < richText.size() ? richText.at(index) : QString{};
    }
    // Upcoming previews follow a configured timer loop. Manual Next still
    // allows the operator to leave the presentation at this boundary.
    bool loopsAtEnd() const {
        return settings.autoAdvanceMs > 0 && settings.loop && !slides.isEmpty()
            && index == slides.size() - 1;
    }
    QString nextText() const {
        if (index < 0) return {};
        if (loopsAtEnd()) return slides.first();
        return index + 1 < slides.size() ? slides.at(index + 1) : upcomingText;
    }
    QString nextTitle() const {
        if (index < 0) return {};
        if (loopsAtEnd()) return title;
        return index + 1 < slides.size() ? title : upcomingTitle;
    }
    QString nextRichText() const {
        if (index < 0) return {};
        if (loopsAtEnd()) return richText.value(0);
        return index + 1 < slides.size() ? richText.value(index + 1) : upcomingRichText;
    }
    QString nextBackgroundPath() const {
        if (index < 0) return {};
        return loopsAtEnd() || index + 1 < slides.size() ? backgroundPath : upcomingBackgroundPath;
    }
    SlideMedia currentMedia() const {
        return index >= 0 && index < media.size() ? media.at(index) : SlideMedia{};
    }
    SlideMedia nextMedia() const {
        if (index < 0) return {};
        if (loopsAtEnd()) return media.isEmpty() ? SlideMedia{} : media.first();
        if (index + 1 < slides.size())
            return index + 1 < media.size() ? media.at(index + 1) : SlideMedia{};
        return upcomingMedia;
    }
};

class PresentationController : public QObject {
    Q_OBJECT
public:
    explicit PresentationController(QObject *parent = nullptr);
    const LivePresentation &state() const { return state_; }
    void takeLive(const QString &title, const QStringList &slides, int slideIndex = 0,
                  const PresentationSettings &settings = {});
    void takeServiceLive(int serviceId, const QVector<PresentationItem> &items,
                         int entryId, int slideIndex = 0);
    bool setSlide(int index);
    bool next();
    bool previous();
    void toggleBlackout();
    void toggleClear();
    void restoreOutput();
    // Updates inherited transitions for the next cue without changing current output.
    void setGlobalTransitionDefaults(const PresentationSettings &defaults);
    // Active includes a suspended countdown; completion clears it entirely.
    bool autoAdvanceActive() const { return autoAdvancePending_; }
    bool autoAdvancePaused() const;
    bool autoAdvanceManuallyPaused() const { return autoAdvancePending_ && autoAdvanceManuallyPaused_; }
    int autoAdvanceRemainingMs() const;
    void setAutoAdvancePaused(bool paused);
    void toggleAutoAdvancePaused();

signals:
    void stateChanged();
    void autoAdvanceChanged();

private:
    LivePresentation state_;
    QVector<PresentationItem> serviceSnapshot_;
    int itemIndex_ = -1;
    quint64 cueSerial_ = 0;
    QTimer autoAdvanceTimer_;
    bool autoAdvancePending_ = false;
    bool autoAdvanceManuallyPaused_ = false;
    int autoAdvanceRemainingMs_ = 0;
    PresentationSettings globalTransitionDefaults_;
    bool hasGlobalTransitionDefaults_ = false;
    PresentationSettings effectiveSettings(const PresentationSettings &settings) const;
    void activateItem(int itemIndex, int slideIndex);
    void updateUpcoming();
    void resetAutoAdvance();
    void syncAutoAdvancePause();
    void advanceAutomatically();
};
