# WorshipCue development roadmap

## Current milestone: pictures, video, and batch naming

Following the saved-service/operator milestone, this user-requested slice adds media presentations without replacing the working song workflow.

Implemented:

- Import multiple pictures and supported video containers into a reusable presentation and the chosen service.
- Natural filename order at import; saved service-local drag ordering afterward.
- A naming preview with Name and Number, Replace Text, Add Text, and Keep Current Names; extensions remain intact and duplicate/invalid filenames are rejected.
- Original files remain untouched. Initial import copies files into a unique managed media directory using the previewed names.
- Batch rename existing presentation slide labels while keeping stored paths stable for live output and shared copies.
- Persistent media types/paths/names/loop flags through a schema-v2 migration, retaining earlier songs/services.
- Picture thumbnails, filename search, missing-media warnings, and image/video slide navigation.
- One shared live decoder/frame, pause/resume, restart, mute, optional video looping, and runtime error reporting.
- Asynchronous copies and image decoding with progress/cancel and partial-batch cleanup.
- Audience Clear keeps media playing; Blackout hides the audience and mutes video audio while playback continues. Stage remains separate.

The current import builds a new presentation. A full media editor for adding/replacing/removing individual source slides, relinking files, extracting video poster frames, and selecting per-clip playback options is future work. Pictures/videos are presentation slides; motion backgrounds behind edited lyrics, fades, and theme controls are not included.

Initial import filenames and post-import labels deliberately differ: the import dialog chooses actual copied filenames, while a later batch rename changes labels only. Neither operation renames the user's original source files. Closed-library backups/moves need both `library.sqlite3` and the managed `media` directory.

## Validation boundary

The application built on macOS 15.7.4 with Qt 6.12 and AppleClang 17. All nine CTest suites passed, including media import/naming, migrations and rollback, real H.264 playback, and native operator picture/video workflows. Captured native picture, video-control, and rename-dialog layouts were inspected and the rename table contrast corrected. On macOS the operator/playback suites use the Cocoa dispatcher; offscreen AVFoundation callbacks do not decode video. See [MILESTONE_0.3.md](MILESTONE_0.3.md) for the full report and file list.

Container extensions do not guarantee codec support; playback depends on the installed Qt Multimedia backend and OS. Windows, physical projectors/hot-plug, fullscreen video/audio behavior, and physical mouse dragging require rehearsal on target systems. Automated tests do not establish support for every video encoding.

## Next: finish everyday song preparation

1. Explicit slides with add/duplicate/delete/reorder, undo/redo, immediate preview, and safe draft persistence.
2. Reflow pasted lyrics with split/join, section labels, keyboard operations, and preview before applying.
3. Separate content from reusable themes: font, alignment, color, positioning, margins, and backgrounds.
4. Stable slide/section identities and named arrangements selected per service item, surviving source additions/deletions.

Acceptance: a volunteer can paste a song, divide slides, apply a theme, choose an arrangement, save/reopen the service, and present while preparation leaves the existing live output intact.

The original song editor still uses `---` separators and Save/Cancel. Service changes save immediately; unsaved dialog text does not. Existing source-index arrangements reset to natural order when a text song's slide count changes; the running service snapshot remains unchanged.

## Reliability work alongside the next milestone

- Add backup/restore for database plus managed media, editor draft recovery, import journaling, and orphan-media cleanup. Deleting presentations currently retains their files so copies/live snapshots remain safe.
- Test unexpected termination and migration recovery with real files.
- Rehearse macOS and Windows projectors, high DPI, monitor identity changes, hot-plug, Spaces, audio devices, supported codecs, and long/large videos.
- Profile large libraries/images, thumbnail caching, rapid cue changes, and UI responsiveness.
- Add missing-media relinking and portable service export/import. The older audience background still uses an absolute external path.
- Add Windows build/test automation before claiming Windows verification.

Current safeguards include transactional SQLite operations, version checks, deletion confirmation, stable entry IDs, immutable live service copies, unique import folders, atomic individual copies, cancellation cleanup, and playback error reporting. Comprehensive crash recovery, automatic backups, and packaging are not complete.

## Later milestones

| Order | Milestone | Completion target |
|---|---|---|
| 1 | Media editing and backgrounds | Add/replace/remove source media, relink missing files, video posters, background layering, portable export, dependable cut/fade |
| 2 | Stage display | Countdown, useful configuration, and separate profiles without audience changes |
| 3 | Scripture and sermon preparation | Legally available translations, passage generation, text/image/title editor, reusable templates |
| 4 | Production integrations | Evaluate NDI, livestream layouts, control surfaces, Planning Center, and remote control after core reliability |

Library collections, a dedicated sermon-presentation editor, Reflow, themes, motion lyric backgrounds, advanced stage layouts, Scripture generation, integrations, signed installers, and update distribution remain outside the current milestone.

The prior completed work and its validation are recorded in [MILESTONE_0.2.md](MILESTONE_0.2.md).
