#include "PresentationController.h"
#include <QtGlobal>

PresentationController::PresentationController(QObject *parent) : QObject(parent) {
    autoAdvanceTimer_.setSingleShot(true);
    autoAdvanceTimer_.setTimerType(Qt::PreciseTimer);
    connect(&autoAdvanceTimer_, &QTimer::timeout, this, &PresentationController::advanceAutomatically);
}

void PresentationController::setGlobalTransitionDefaults(const PresentationSettings &defaults) {
    if (!defaults.isValid()) return;
    globalTransitionDefaults_ = defaults;
    hasGlobalTransitionDefaults_ = true;
}

PresentationSettings PresentationController::effectiveSettings(const PresentationSettings &settings) const {
    const PresentationSettings valid = settings.isValid() ? settings : PresentationSettings{};
    return hasGlobalTransitionDefaults_ ? valid.resolvedWithDefaults(globalTransitionDefaults_) : valid;
}

void PresentationController::takeLive(const QString &title, const QStringList &slides, int slideIndex,
                                      const PresentationSettings &settings) {
    const QString titleCopy = title;
    const QStringList slidesCopy = slides;
    const PresentationSettings settingsCopy = settings.isValid() ? settings : PresentationSettings{};
    serviceSnapshot_.clear();
    itemIndex_ = -1;
    state_ = {};
    state_.title = titleCopy;
    state_.slides = slidesCopy;
    state_.settings = effectiveSettings(settingsCopy);
    state_.index = slidesCopy.isEmpty() ? -1 : qBound(0, slideIndex, int(slidesCopy.size()) - 1);
    state_.blackout = false;
    state_.clear = false;
    state_.cueRevision = ++cueSerial_;
    autoAdvanceManuallyPaused_ = false;
    resetAutoAdvance();
    emit stateChanged();
}

void PresentationController::takeServiceLive(int serviceId, const QVector<PresentationItem> &items,
                                              int entryId, int slideIndex) {
    if (serviceId <= 0 || entryId <= 0) return;
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).entryId != entryId || items.at(i).slides.isEmpty()) continue;
        // Copy the complete service before going live. Library edits, service
        // switching, and reordering cannot silently redirect live navigation.
        serviceSnapshot_ = items;
        state_.serviceId = serviceId;
        autoAdvanceManuallyPaused_ = false;
        activateItem(i, slideIndex);
        return;
    }
}

void PresentationController::activateItem(int itemIndex, int slideIndex) {
    itemIndex_ = itemIndex;
    const auto &item = serviceSnapshot_.at(itemIndex_);
    state_.entryId = item.entryId;
    state_.title = item.title;
    state_.slides = item.slides;
    state_.media = item.media;
    state_.settings = effectiveSettings(item.settings);
    state_.backgroundPath = item.backgroundPath;
    state_.richText = item.richText;
    state_.index = qBound(0, slideIndex, int(item.slides.size()) - 1);
    state_.blackout = false;
    state_.clear = false;
    state_.cueRevision = ++cueSerial_;
    updateUpcoming();
    resetAutoAdvance();
    emit stateChanged();
}

void PresentationController::updateUpcoming() {
    state_.upcomingTitle.clear();
    state_.upcomingText.clear();
    state_.upcomingMedia = {};
    state_.upcomingBackgroundPath.clear();
    state_.upcomingRichText.clear();
    for (int i = itemIndex_ + 1; i < serviceSnapshot_.size(); ++i) {
        const auto &item = serviceSnapshot_.at(i);
        if (item.slides.isEmpty()) continue;
        state_.upcomingTitle = item.title;
        state_.upcomingText = item.slides.first();
        state_.upcomingMedia = item.media.isEmpty() ? SlideMedia{} : item.media.first();
        state_.upcomingBackgroundPath = item.backgroundPath;
        state_.upcomingRichText = item.richText.value(0);
        return;
    }
}

bool PresentationController::setSlide(int index) {
    if (index < 0 || index >= state_.slides.size()) return false;
    state_.settings = effectiveSettings(state_.settings);
    state_.index = index;
    state_.blackout = false;
    state_.clear = false;
    state_.cueRevision = ++cueSerial_;
    resetAutoAdvance();
    emit stateChanged();
    return true;
}

bool PresentationController::next() {
    if (setSlide(state_.index + 1)) return true;
    for (int i = itemIndex_ + 1; i < serviceSnapshot_.size(); ++i) {
        if (serviceSnapshot_.at(i).slides.isEmpty()) continue;
        activateItem(i, 0);
        return true;
    }
    return false;
}

bool PresentationController::previous() {
    if (setSlide(state_.index - 1)) return true;
    for (int i = itemIndex_ - 1; i >= 0; --i) {
        const auto &item = serviceSnapshot_.at(i);
        if (item.slides.isEmpty()) continue;
        activateItem(i, int(item.slides.size()) - 1);
        return true;
    }
    return false;
}

void PresentationController::toggleBlackout() {
    state_.blackout = !state_.blackout;
    if (state_.blackout) state_.clear = false;
    syncAutoAdvancePause();
    emit stateChanged();
}

void PresentationController::toggleClear() {
    state_.clear = !state_.clear;
    if (state_.clear) state_.blackout = false;
    syncAutoAdvancePause();
    emit stateChanged();
}

void PresentationController::restoreOutput() {
    if (!state_.blackout && !state_.clear) return;
    state_.blackout = false;
    state_.clear = false;
    syncAutoAdvancePause();
    emit stateChanged();
}

bool PresentationController::autoAdvancePaused() const {
    return autoAdvancePending_ && (autoAdvanceManuallyPaused_ || state_.blackout || state_.clear);
}

int PresentationController::autoAdvanceRemainingMs() const {
    if (!autoAdvancePending_) return 0;
    return autoAdvanceTimer_.isActive() ? qMax(0, autoAdvanceTimer_.remainingTime())
                                         : autoAdvanceRemainingMs_;
}

void PresentationController::resetAutoAdvance() {
    autoAdvanceTimer_.stop();
    autoAdvancePending_ = state_.index >= 0 && state_.settings.autoAdvanceMs > 0;
    autoAdvanceRemainingMs_ = autoAdvancePending_ ? state_.settings.autoAdvanceMs : 0;
    if (autoAdvancePending_ && !autoAdvancePaused()) autoAdvanceTimer_.start(autoAdvanceRemainingMs_);
    emit autoAdvanceChanged();
}

void PresentationController::syncAutoAdvancePause() {
    if (!autoAdvancePending_) return;
    if (autoAdvancePaused()) {
        if (autoAdvanceTimer_.isActive()) {
            // Keep a due countdown pending while hidden; restoring the
            // audience can deliver its next cue on a subsequent event turn.
            autoAdvanceRemainingMs_ = qMax(1, autoAdvanceTimer_.remainingTime());
            autoAdvanceTimer_.stop();
        }
    } else if (!autoAdvanceTimer_.isActive()) {
        autoAdvanceTimer_.start(qMax(1, autoAdvanceRemainingMs_));
    }
    emit autoAdvanceChanged();
}

void PresentationController::setAutoAdvancePaused(bool paused) {
    if (!autoAdvancePending_ || autoAdvanceManuallyPaused_ == paused) return;
    autoAdvanceManuallyPaused_ = paused;
    syncAutoAdvancePause();
}

void PresentationController::toggleAutoAdvancePaused() {
    setAutoAdvancePaused(!autoAdvanceManuallyPaused_);
}

void PresentationController::advanceAutomatically() {
    if (!autoAdvancePending_ || autoAdvancePaused()) return;
    if (state_.index + 1 < state_.slides.size()) {
        setSlide(state_.index + 1);
    } else if (state_.settings.loop) {
        setSlide(0);
    } else {
        // Automatic playback stops at this presentation's boundary. Manual
        // Next remains free to enter the next item in the live service.
        autoAdvancePending_ = false;
        autoAdvanceRemainingMs_ = 0;
        emit autoAdvanceChanged();
    }
}
