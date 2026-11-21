# Audience and stage displays

WorshipCue starts with two audience screens and two stage screens. Each has its own route and rendering configuration. Add more audience screens or confidence monitors as needed.

A valid license is required to show live output windows, including **Windowed Preview**. Output configuration and layout editing remain available in editing-only mode. See [License activation and devices](licensing.md).

## Assign an output

1. Open **Screens…** or **Settings → Screens / Output Settings…**.
2. Select an existing output tab or use **Add Output** to create an audience or stage output. New outputs start Off. Choose **Off**, **Windowed Preview**, or any available fullscreen display.
3. Set logical dimensions, aspect ratio, rotation, and **Fit**, **Fill**, or **Stretch** scaling. Fit letterboxes, Fill crops, and Stretch fills the destination.
4. Choose **Save This Output** to save that tab. Repeat for other outputs.

Fullscreen outputs use separate physical displays. Audience outputs share the presentation and media transport while applying their own display formats. Use the selector above the right-hand live preview to monitor any audience or stage output, even while its route is Off. The selection is saved and leaves routing and cue playback unchanged.

Custom logical dimensions set the rendering canvas. The **Physical signal** selector lists modes reported by the OS, including resolution, refresh, and scan type where available. **Keep OS desktop mode** retains the current hardware signal. Select **Keep Mode** in the 15-second review to save the tested mode. The previously saved route is retained until you confirm.

Closing an output window saves its route as Off. Disconnecting a display hides the affected output and retains its assignment for reconnection. Check output status if a display or saved physical mode is unavailable.

## Design stage layouts

Open **Settings → Stage Display Layouts…**. **Copy** the **Standard Stage** layout or choose **New**. Name it and add elements for current/next content, presentation title, clock, countdown, stage message, or custom text. Set percentage-based position/size, font, color, alignment, and background, then **Save Layout**.

Assign a layout to each stage output in Screens. Assigned layouts are protected from deletion.

## Set a confidence monitor timer

1. Open **Timers…** beside the output controls, or **Settings → Stage Messages / Timers…**.
2. Choose a stage screen and give its timer a name, such as Sermon.
3. Choose **Countdown**, **Countdown to Time**, or **Elapsed Time**. Enter hours/minutes/seconds for a countdown, or a local date/time for a scheduled finish. Elapsed timers start at zero and can have an optional duration limit.
4. Set the warning threshold and optional overrun, then choose **Start & Show**. The preview shows the selected confidence monitor. Its routing status identifies the active display or Off state.
5. Use **Pause**, **Resume**, and **Reset** as needed. **Restart & Show** restarts from the configured settings. Edits apply when Start/Restart or Reset is chosen.

Start & Show displays a timer footer with the standard layout. A custom layout with a **Timer** element uses that element's position and font. A custom layout without a Timer element receives a footer beneath its content. **Show on confidence monitor** hides or shows the timer while it continues running.

The timer turns amber inside its warning period and red at the endpoint. Overrun lets countdowns become negative or elapsed timers continue beyond their limit. Countdown and elapsed timers use elapsed time for accuracy. A scheduled countdown resumes against its original date/time, catching up after a pause.

Each stage screen has an independent timer. Settings and visibility are saved, and timers open ready after an application restart. Closing the Timers panel leaves running timers active. The modeless panel can stay open while an operator runs slides. Audience Blackout and Clear Text preserve stage timers.

For stage messages, use the **Stage Message** tab to send/clear a message. Include a Stage Message element in the assigned custom layout. Messages last for the session.

[Presentation navigation](navigation.md) · [Troubleshooting](troubleshooting.md)
