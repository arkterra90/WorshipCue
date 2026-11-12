# Rich slide editing and global transitions

Research and implementation: November 11, 2026.

## ProPresenter reference workflow

Renewed Vision describes a slide navigator, a central editing canvas, and contextual
text formatting in the editor. Its documentation also distinguishes global
transition defaults from presentation-specific overrides. WorshipCue adopts these
interaction patterns for its existing single text layer and audience compositor.

- [ProPresenter editor and formatting model](https://support.renewedvision.com/hc/en-us/articles/11910559859603-Themes-in-ProPresenter): navigator, canvas, and contextual text properties.
- [ProPresenter transition hierarchy](https://support.renewedvision.com/hc/en-us/articles/360041342354-Guide-to-Using-Transitions-in-ProPresenter): global defaults, presentation overrides, and adjustable duration.
- [ProPresenter user interface](https://support.renewedvision.com/hc/en-us/articles/360041345954-Understanding-The-ProPresenter-User-Interface): editing modes and text-formatting access.

This is a documentation-based reference, not a claim to reproduce ProPresenter's
complete object model, themes, independent media layers, or object animation tools.

## Where the controls are

Use **Edit Slide Text…** above the slides, the slide context menu, or double-click a
slide in **Edit** mode. Library **Edit** opens the same editor. Click into the 16:9
canvas to edit; select words to style them. With no text selected, formatting
controls apply to the full slide. Font family/size, bold, italic, underline, color,
alignment, and undo/redo are available. Navigate between slides without losing
draft text or each slide's undo history. Save commits all drafts; Cancel discards
them. New presentations also offer Add, Copy, and Remove draft-slide controls.

**Global transition** and **Time** are visible beneath the slide workspace. The
full dialog is also under **Settings → Global Slide Transitions…**, and in File.
Choose Cut, Fade, Slide Left, or Slide Right and a duration from 0 to 10 seconds.
Zero duration cuts immediately. Preferences survive restarting the app.

Presentation **Timing & Transitions…** can override the global default. Uncheck
**Override global transition for this presentation** to inherit it again. Global
changes apply to the next inherited cue without restarting the current video,
animation, or countdown. Editing saved presentation-specific settings or slide
content still requires an explicit Take Live to replace the running copy.

## Persistence and output

Schema 5 adds optional per-slide HTML alongside searchable plain text. Old slides
retain their original appearance. Rich text saves keep source-slide IDs, media,
background assignments, and repeated/custom arrangements. Audience, thumbnails,
selected preview, and next preview use the saved formatting; Stage keeps plain
text. A formatted slide that has changed since going live is not mislabeled LIVE
in the saved workspace.

Rich text is limited to supported text and paragraph formatting. Embedded images,
files, and external resources are stripped from pasted content; media continues
through WorshipCue's managed media workflow. Editor and renderer use the same
logical text area, font sizes, and proportional shrink-to-fit behavior.

Existing presentations keep their source-slide count during rich editing. New
drafts can change their structure before first save. Full multi-object layout,
arbitrary positioned text boxes, and reusable themes remain future editor work.
