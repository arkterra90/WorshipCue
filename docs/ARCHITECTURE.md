# WorshipCue architecture

## Scope and ownership

WorshipCue remains a native C++17 / Qt 6 Widgets application with local SQLite storage. The media milestone extends the existing saved-service application.

```text
MainWindow ───────── LibraryRepository ─── SQLite library
    │                       └── songs/presentations, slides, services, entries
    ├── MediaManager ─────── library-owned media files and image thumbnails
    ├── PresentationController ─── immutable live service snapshot
    ├── MediaPlayback ────── one Qt Multimedia decoder / shared live frame
    └── OutputManager ────── audience and stage DisplayWindows

SlideRenderer ─── audience window, live/selected/next previews, slide thumbnails
BatchRenamer ─── naming rules and collision validation
```

`MainWindow` owns the repository, presentation controller, output manager, media playback, and media manager. `OutputManager` owns the two top-level output windows. Windows borrow the controller; the audience also borrows the shared playback frame. Outputs do not query SQLite. Qt parent ownership manages operator widgets and connections.

## PresentationController: a stable live service

Taking a slide live copies the complete service into an in-memory snapshot: entry IDs, titles, slide text, media metadata, order, and presentation timing/transition settings. Next/Previous navigate that snapshot across entries and hold the first/last slide at service boundaries. Text lists remain present for compatibility; a media slide can have empty text and a valid image/video descriptor. An empty text string does not make that slide an empty service item.

Library edits, service switching, deletions, and reordering leave the live snapshot intact. Taking another slide live explicitly captures the current saved service. Media paths become absolute when constructing the live snapshot, while persistent references stay relative to the library directory. A cue revision increments on deliberate takes/navigation so retaking the same video restarts it. Clear/Blackout/Restore do not change that revision.

Clear hides audience text and retains its image/video and audio. Blackout paints the audience black and temporarily mutes video audio while its playhead continues. Restore removes either state without restarting playback. Taking or advancing to a valid slide restores normal audience content. Stage retains useful lyrics, media labels, and its clock. An invalid boundary-navigation request holds both content and output state.

The controller owns a single-shot precise auto-advance timer. `PresentationSettings` supplies an interval (zero disables timing), a presentation loop flag, a transition type, and transition duration. Timer expiry advances only within the current presentation, loops to its first slide if requested, or completes after its last interval. Manual Next/Previous can still cross service items. Each successful cue resets the interval; explicit takes clear manual pause. Pausing stores remaining milliseconds. Blackout/clear also suspend timing, independently of manual pause. Timer status has its own signal, so countdown updates do not restart media or issue a slide cue. The operator polls remaining time every 100 ms.

## LibraryRepository: versioned content and services

The default database is `library.sqlite3` in Qt's `QStandardPaths::AppDataLocation`. Tests can supply a path or `:memory:`. Each repository owns a unique SQL connection and removes it on close.

Schema version **3**, recorded in `PRAGMA user_version`, contains:

| Table | Purpose |
|---|---|
| `songs` | Shared title/ID, auto-advance interval/loop, transition type/duration |
| `slides` | Ordered text plus media kind, relative path, display name, and video loop flag |
| `services` | Named saved plans |
| `service_entries` | Independent IDs, service/content references, position, optional slide order |

Version 0 migrates to version 1 by preserving songs, slides, service-entry IDs, and order in **My Service**. Version 1 then migrates to version 2 by adding the media columns with text-compatible defaults. Version 2 migrates to version 3 by adding timing/transition columns to songs, defaulting to manual advancement and instant cuts. Settings are validated on both reads and writes; create operations include settings in their transaction. Each migration is transactional; a failed migration rolls back its own changes. Unknown newer schema versions are rejected. Samples are seeded only for a new library, so intentionally deleting all songs stays effective after restart.

Media presentation creation validates aligned text/media lists, valid kinds, normalized managed `media/...` references, unique media names, and video-only looping. Song text editing rejects media presentations to prevent replacing imported assets accidentally; a separate operation renames their presentation title. Media-name updates are transactional and preserve stored paths. Search includes titles, lyrics, and media names using bound parameters with literal LIKE-wildcard escaping.

Song edits, service copies, entry duplication, and ordering use transactions. Foreign keys cascade library deletion into saved service entries; removing an entry leaves its library content intact. Copies share underlying content and media. Service and entry IDs use `AUTOINCREMENT` to avoid identifying a newly inserted entry as a deleted live entry. Deleting a presentation does not currently reclaim media files, preserving files used by copies or active snapshots.

Arrangements store source-slide indices in JSON. Null/empty means natural order. Per-entry arrangements may repeat or omit slides; the UI exposes ordering and reset. Text and media arrays follow the same indices. A text song's slide-count change resets affected saved arrangements to natural order transactionally; same-count edits preserve them. Stable slide/section identities are future editor work.

Writes return errors; read failures are logged and exposed through `lastError()`. Callers must inspect a read error before issuing another repository operation.

## MediaManager: managed imports and thumbnails

Importing copies selected files into `media/<batch UUID>/<chosen filename>` beside the database. Original files are unchanged. Stored paths are relative, allowing the closed database and its media directory to move together. A batch UUID isolates import destinations from existing content; files are never overwritten.

`MediaManager` copies in a Qt Concurrent worker, validates readable images, preserves extensions, checks names, and reports progress and cancellation. Each file uses `QSaveFile`; a failed/canceled batch cleans its directory. `MainWindow` commits the presentation after successful copying, discards the batch if database creation fails, and adds the result to the service captured when import began. Browsing another service during import does not change the destination. If adding the entry fails, the imported library item remains available and the operator receives an error.

Image thumbnails decode asynchronously and are cached. Managed-path resolution rejects paths outside the library media directory. Videos are accepted by supported container extension and validated by the playback backend when cued; extension support is not a codec guarantee. Animated image imports are treated as still images rather than motion backgrounds.

A crash between file copying and database commit can leave an unreferenced import directory. Garbage collection, import journaling, media relinking, and a complete backup/export workflow are not implemented. Backups must include both the closed database and its `media` directory. The older global audience background remains an external absolute image path.

## Batch renaming

`BatchRenamer` implements Name and Number, Replace Text, Add Text, and Keep Current Names. It preserves extensions and validates duplicate names case-insensitively with Unicode normalization. It rejects path separators, invalid Windows/macOS filename characters, reserved Windows names, trailing dots/spaces, excessive length, and invalid numbering ranges.

`MediaImportDialog` previews every current/new name before applying. Initial import uses those names for the copied physical files. Batch renaming an existing presentation changes its slide labels in SQLite while keeping physical storage paths stable; this protects live snapshots and duplicate presentations. It does not rename the user's original files or relocate managed files. The dialog explicitly distinguishes these operations.

## Operator interaction and rendering

The left sidebar contains the searchable content library and named services; the center contains slide cards; the right shows Live, Up Next, Selected Preview, transport, media status, and audience safety controls. Import is available from the sidebar and File menu. Media presentation titles, labels, and arrangements persist. Picture cards show decoded thumbnails; video cards identify the filename/type. Missing media receives an operator warning.

Show mode takes a clicked slide live. Edit mode selects for preview and enables service-local reordering. Explicit Take Live remains available. Selecting another entry previews it without changing the live service. `ReorderListWidget` explicitly changes model row order for list/grid drags and saves after a complete reorder. The original song dialog remains a Save/Cancel `---` separator editor; open draft text is not autosaved.

Keyboard handling excludes modal dialogs and text-entry controls. Escape returns focus to operator controls while preserving fullscreen output. Safety shortcuts, selected service, output routes, background path, and geometry use QSettings.

`SlideRenderer` paints a fixed 1920×1080 logical canvas at each audience/preview surface. Text uses identical margins/wrapping/auto-fit. Imported pictures and video frames fit within the canvas without cropping or lyric-background dimming; unused space is black. Clear leaves media visible. The global song background retains its existing cover/crop behavior. Reusable themes are not implemented.

The audience `DisplayWindow` owns the transition compositor. Cut, Fade, Slide Left, and Slide Right combine cached outgoing/incoming canvases; a 16 ms timer updates one shared progress value. The operator live monitor paints through that same compositor, including when the audience window is hidden. Re-cueing during an animation captures its current composite as the new outgoing frame. Incoming media waits for the first decoded frame before its transition begins, retaining the outgoing visual during loading. Output clear/blackout/restore cancel animation immediately. Stage, selected/next previews, and thumbnails remain static. No transition paint operation reads files or queries SQLite.

## MediaPlayback: one audience decoder

`MediaPlayback` owns one `QMediaPlayer`, `QAudioOutput`, and `QVideoSink`. The audience and operator live monitor draw the same decoded frame, avoiding separate decoders drifting apart. Pictures decode asynchronously; generation checks discard stale completions after rapid navigation. Playback errors and missing files are reported in the operator UI. A timeout catches video sources that produce no frames.

Video controls pause/resume, restart, and mute. An import checkbox sets looping for videos in that presentation. Non-looping videos hold their last frame unless the independent presentation timer advances the slide. Auto-advance counts from the cue, including load/transition time, and does not follow clip completion or Pause Video. Re-cueing deliberately restarts a clip. Audio uses the system output device; independent audio-device routing is not implemented. Runtime Qt Multimedia/OS codec support determines which files actually play.

## Display routing

`OutputManager` routes each output to Off, Windowed Preview, or a fullscreen screen and persists desired identities. It rejects sending audience and stage to the same fullscreen identity. A missing target stays selected as Disconnected while hidden; screen-added notifications retry it. Geometry changes refresh routing without cycling a healthy fullscreen window. Closing sets Off; Escape requests operator focus. Visibility/minimization changes update status without reopening a deliberately hidden window.

Screen identities combine OS name, manufacturer, model, and serial number. Identical identities are offered as one route. Projector identity changes, macOS Spaces, and real display reconnection need physical validation. The stage output is separate and does not play audience media/audio.

## Verification and remaining work

The application built on macOS 15.7.4 with Qt 6.12 and AppleClang 17. All nine CTest suites passed, including version-0/version-1 migrations and rollback, batch naming/import, real H.264 decoding, and native operator workflows for imported pictures/videos. The native operator suite passed again after the final dialog contrast adjustment. Operator and video-playback tests use Cocoa on macOS because AVFoundation needs its native event dispatcher; other widget suites use offscreen rendering. See [MILESTONE_0.3.md](MILESTONE_0.3.md) for commands, changed files, and the validation boundary.

Offscreen tests do not establish real projector routing, physical mouse dragging, codec coverage, audio-device behavior, or hardware-accelerated fullscreen video. Windows and physical multi-monitor testing remain outstanding. Keep backup/restore, real-output rehearsal, large-media profiling, and bounded cache/memory checks alongside the next explicit slide-editor/Reflow/themes milestone.
