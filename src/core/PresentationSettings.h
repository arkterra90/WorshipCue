#pragma once

#include <QMetaType>

enum class SlideTransitionType { Cut = 0, Fade = 1, SlideLeft = 2, SlideRight = 3 };

struct PresentationSettings {
    // Zero disables timed advancement. Timing belongs to a presentation, not
    // to the surrounding service, and is copied when that service goes live.
    int autoAdvanceMs = 0;
    bool loop = false;
    SlideTransitionType transition = SlideTransitionType::Cut;
    int transitionDurationMs = 500;
    // Presentations inherit the program transition until explicitly overridden.
    bool overrideTransition = false;

    PresentationSettings resolvedWithDefaults(const PresentationSettings &defaults) const {
        PresentationSettings result = *this;
        if (!overrideTransition) {
            const PresentationSettings validDefaults = defaults.isValid() ? defaults : PresentationSettings{};
            result.transition = validDefaults.transition;
            result.transitionDurationMs = validDefaults.transitionDurationMs;
        }
        return result;
    }

    bool isValid() const {
        return (autoAdvanceMs == 0 || (autoAdvanceMs >= 100 && autoAdvanceMs <= 3600000))
            && transitionDurationMs >= 0 && transitionDurationMs <= 10000
            && int(transition) >= int(SlideTransitionType::Cut)
            && int(transition) <= int(SlideTransitionType::SlideRight);
    }
    bool sameTransition(const PresentationSettings &other) const {
        return transition == other.transition && transitionDurationMs == other.transitionDurationMs
            && overrideTransition == other.overrideTransition;
    }
    bool operator==(const PresentationSettings &other) const {
        return autoAdvanceMs == other.autoAdvanceMs && loop == other.loop
            && transition == other.transition && transitionDurationMs == other.transitionDurationMs
            && overrideTransition == other.overrideTransition;
    }
    bool operator!=(const PresentationSettings &other) const { return !(*this == other); }
};

Q_DECLARE_METATYPE(SlideTransitionType)
Q_DECLARE_METATYPE(PresentationSettings)
