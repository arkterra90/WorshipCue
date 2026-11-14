# Confidence monitor timers

## ProPresenter workflow research

ProPresenter creates and operates timers in a dedicated show-control panel. Timers can count down from a duration, count toward a scheduled time, or count elapsed time. Controls reset the timer or start/stop it. An overrun option continues beyond the endpoint. [Official timer setup](https://support.renewedvision.com/hc/en-us/articles/360050782494-Setting-up-Timers-in-ProPresenter-7).

Stage layouts display timers through linked text and predefined timer objects. The layout controls placement and appearance. Color triggers can warn a presenter as the timer approaches its endpoint and indicate overtime. [Official stage-timer guide](https://support.renewedvision.com/hc/en-us/articles/360053250613-Using-Timers-on-Stage-Screens).

## WorshipCue implementation

**Timers…** in the operator toolbar and **Settings → Stage Messages / Timers…** open one reusable modeless panel. Each of the two stage outputs has an independent named timer. Select the output, set its mode and duration or target date/time, then choose **Start & Show**. The panel shows the current reading, state, routing status, and an actual rendered confidence-monitor preview.

**Pause**, **Resume**, **Reset**, and **Restart & Show** provide explicit control. Edited fields apply on Start/Restart or Reset. Countdown and elapsed timers use a monotonic elapsed clock. Scheduled timers track the selected absolute time and catch up to that deadline when resumed. Reset returns a timer to Ready. Elapsed timers start at zero and support an optional end limit.

Visibility is independent of running state. Starting enables display. The standard stage layout reserves a footer for the timer while keeping current/next content above it. Existing custom Timer elements retain their positions and fonts. A custom layout without a Timer element receives the footer. The configured warning period uses amber, and reaching the endpoint uses red. Optional overrun produces negative countdowns or continued elapsed time.

Configuration and visibility persist per stage output in QSettings. Application restarts initialize timers in Ready state. Closing the control panel leaves a running timer active. Slide cues, audience output controls, and timer controls operate independently.

`StageTimer` provides validated configuration, serialization, formatting, and runtime readings. `DisplayWindow` consumes that model through its existing stage runtime and rendering paths. The existing countdown methods remain available to their callers. `OutputManager` persists settings and limits these operations to stage outputs. Library schema 11 remains compatible.

Tests cover all three modes, pause/resume/reset, limits, overrun, formatting, invalid configuration, independent outputs, persistence, standard/custom layout rendering, warning colors, toolbar operation, modeless lifecycle, and live-cue preservation. Native macOS screenshots support visual review. Physical confidence monitors and Windows runtime require platform validation.
