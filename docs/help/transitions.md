# Transitions and fades

Use **Global transition** below the slide workspace or **Settings → Global Slide Transitions…** to choose **Cut**, **Fade**, **Slide Left**, or **Slide Right**, with a duration from 0 to 10 seconds.

## Override one presentation

Choose **Presentation → Timing & Transitions…** in the workspace header for the selected presentation. Editing transition type or duration enables its override. Uncheck **Override global transition** to inherit the global settings.

Presentation overrides take priority over global defaults. The incoming cue supplies its transition. Cut and a zero-second duration are instant.

## Edit several service items

1. In **Service Playlist**, select a range with Shift-click or separate items with Command-click on macOS / Ctrl-click on Windows. Platform Select All selects the playlist's entries while it has focus.
2. Open **Transitions for Selection…** from its button or context menu.
3. Set a shared override, or restore global inheritance, then save.

For presentations, the shared library setting affects their service occurrences. Standalone media overrides belong to the selected service entries. **Edit → Undo Bulk Transitions / Redo Bulk Transitions** provides session history while preserving unrelated settings.

## Blackout and Clear Text

**Blackout** instantly makes audience output black and mutes live media audio while its playhead continues. Blackout stays active while you navigate or take another cue. Choose **Restore Audience** to release it.

**Clear Text** hides audience text while retaining the background, media, and audio. Clear Text and Restore use the active presentation transition override or current global default. Blackout and Clear Text suspend automatic timing with its remaining time preserved. Restore resumes timing unless it was also paused manually or suspended by a dialog. Stage content continues displaying its current and next cues.

## Automatic advancement

In **Timing & Transitions…**, enable **Automatically advance slides** and choose 0.1–3600 seconds. The interval includes transition and media-loading time. **Pause / Resume Auto-Advance** holds or resumes the countdown.

At the final interval, the presentation holds its last slide or loops when configured. Manual Next crosses into the next service item. Saved presentation-specific changes are captured on the next explicit take.

[Presentation navigation](navigation.md) · [Keyboard reference](shortcuts.md)
