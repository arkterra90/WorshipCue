# WorshipCue architecture

## Scope and ownership

WorshipCue remains a native C++17 / Qt 6 Widgets application with local SQLite storage, managed media/background assets, and saved presentation settings.

```text
MainWindow ───────── LibraryRepository ─── SQLite library
    │                       └── songs/presentations, slides, services, entries
    ├── MediaManager ─────── library-owned media files and image thumbnails
    ├── BackgroundLibraryDialog ─── reusable library-owned still backgrounds
    ├── LyricsImporter ──── text/ChordPro to editable song slides
    ├── PresentationController ─── immutable live service snapshot
    ├── MediaPlayback ────── one Qt Multimedia decoder / shared live frame
    └── OutputManager ────── audience and stage DisplayWindows

SlideRenderer ─── audience window, live/selected/next previews, slide thumbnails
BatchRenamer ─── naming rules and collision validation
```

`MainWindow` owns the repository, presentation controller, output manager, media playback, and media manager. `OutputManager` owns the two top-level output windows. Windows borrow the controller; the audience also borrows the shared playback frame. Outputs do not query SQLite. Qt parent ownership manages operator widgets and connections.

## PresentationController: a stable live service

Taking a slide live copies the complete service into an in-memory snapshot: entry IDs, titles, slide text, media metadata, order, background assignments, and presentation timing/resolved transition settings. Next/Previous navigate that snapshot across entries and hold the first/last slide at service boundaries. Text lists remain present for compatibility; a media slide can have empty text and a valid image/video descriptor. An empty text string does not make that slide an empty service item.

Library edits, service switching, deletions, and reordering leave the live snapshot intact. Taking another slide live explicitly captures the current saved service. Media and background paths become absolute when constructing the live snapshot, while persistent references stay relative to the library directory. Upcoming previews carry the next presentation's background, including presentation-loop boundaries. A cue revision increments on deliberate takes/navigation so retaking the same video restarts it. Clear/Blackout/Restore do not change that revision.

Clear hides audience text and retains its image/video and audio. Blackout paints the audience black and temporarily mutes video audio while its playhead continues. Restore removes either state without restarting playback. Taking or advancing to a valid slide restores normal audience content. Stage retains useful lyrics, media labels, and its clock. An invalid boundary-navigation request holds both content and output state.

The controller owns a single-shot precise auto-advance timer. `PresentationSettings` supplies an interval (zero disables timing), a presentation loop flag, a transition type, and transition duration. Timer expiry advances only within the current presentation, loops to its first slide if requested, or completes after its last interval. Manual Next/Previous can still cross service items. Each successful cue resets the interval; explicit takes clear manual pause. Pausing stores remaining milliseconds. Blackout/clear also suspend timing, independently of manual pause. Timer status has its own signal, so countdown updates do not restart media or issue a slide cue. The operator polls remaining time every 100 ms.

Auto-advance and looping belong to the library presentation. Program transition defaults live in QSettings (`presentation/transitionType` and `presentation/transitionDurationMs`). `PresentationSettings::resolvedWithDefaults()` uses those defaults unless the presentation's `overrideTransition` flag is enabled. `MainWindow` resolves each item when capturing the live service; changing defaults or an override takes effect on the next explicit take. The incoming item supplies the transition type/duration.

## LibraryRepository: versioned content and services

The default database is `library.sqlite3` in Qt's `QStandardPaths::AppDataLocation`. Tests can supply a path or `:memory:`. Each repository owns a unique SQL connection and removes it on close.

Schema version **5**, recorded in `PRAGMA user_version`, contains:

| Table | Purpose |
|---|---|
| `songs` | Shared title/ID, auto-advance interval/loop, transition type/duration/override, relative background path |
| `slides` | Ordered text plus media kind, relative path, display name, and video loop flag |
| `services` | Named saved plans |
| `service_entries` | Independent IDs, service/content references, position, optional slide order |

Version 0 migrates to version 1 by preserving songs, slides, service-entry IDs, and order in **My Service**. Version 1 then adds media columns; version 2 adds presentation timing/transitions with manual/Cut defaults. Version 3 adds `transition_override` and `background_path`: existing non-Cut transitions or durations other than 500 ms become explicit overrides, while the previous default inherits program settings. Version 4 adds optional rich text per slide, defaulting to empty metadata for legacy appearance. Settings and managed background paths are validated on reads and writes; create operations include them in their transaction. Each migration is transactional; a failed migration rolls back its own changes. Unknown newer schema versions are rejected. Samples are seeded only for a new library, so intentionally deleting all songs stays effective after restart.

Media presentation creation validates aligned text/media lists, valid kinds, normalized managed `media/...` references, unique media names, and video-only looping. Song text editing rejects media presentations to prevent replacing imported assets accidentally; a separate operation renames their presentation title. Media-name updates are transactional and preserve stored paths. Search includes titles, lyrics, and media names using bound parameters with literal LIKE-wildcard escaping.

Song edits, service copies, entry duplication, and ordering use transactions. Foreign keys cascade library deletion into saved service entries; removing an entry leaves its library content intact. Copies share underlying content and media. Service and entry IDs use `AUTOINCREMENT` to avoid identifying a newly inserted entry as a deleted live entry. Deleting a presentation does not currently reclaim media files, preserving files used by copies or active snapshots.

Arrangements store source-slide indices in JSON. Null/empty means natural order. Per-entry arrangements may repeat or omit slides; the UI exposes ordering and reset. Plain text, rich HTML, and media arrays follow the same indices. A text song's slide-count change resets affected saved arrangements to natural order transactionally; same-count edits preserve them. Stable slide/section identities are future editor work.

Writes return errors; read failures are logged and exposed through `lastError()`. Callers must inspect a read error before issuing another repository operation.

## MediaManager: managed imports and thumbnails

Importing copies selected files into `media/<batch UUID>/<chosen filename>` beside the database. Original files are unchanged. Stored paths are relative, allowing the closed database and its media directory to move together. A batch UUID isolates import destinations from existing content; files are never overwritten.

`MediaManager` copies in a Qt Concurrent worker, validates readable images, preserves extensions, checks names, and reports progress and cancellation. Each file uses `QSaveFile`; a failed/canceled batch cleans its directory. `MainWindow` commits the presentation after successful copying, discards the batch if database creation fails, and adds the result to the service captured when import began. Browsing another service during import does not change the destination. If adding the entry fails, the imported library item remains available and the operator receives an error.

Image thumbnails decode asynchronously and are cached. Managed-path resolution rejects paths outside the library media directory. Videos are accepted by supported container extension and validated by the playback backend when cued; extension support is not a codec guarantee. Animated image imports are treated as still images rather than motion backgrounds.

A crash between file copying and database commit can leave an unreferenced import directory. Garbage collection, import journaling, media relinking, and a complete backup/export workflow are not implemented. Backups must include the closed database and its sibling `media` and `backgrounds` directories.

## Background library and lyric import

`BackgroundLibraryDialog` scans the managed `backgrounds` directory beside the database, previews reusable still images, and copies imports with unique filenames while preserving originals. Assignment stores a normalized `backgrounds/...` path on the library presentation; an empty path selects the built-in background. Duplicating a presentation copies its assignment and shares the asset. The selected/next previews resolve their own backgrounds; the audience loads the live snapshot's assignment when the cue changes. Missing files fall back to the built-in background and appear in the operator's background status. The former global audience-background QSettings preference is no longer loaded by `MainWindow`.

`LyricsImporter` normalizes UTF-8 text, CRLF/BOM, paragraphs, explicit `---` separators, and section headings into ordinary editable song slides. It removes chord annotations and reads supported ChordPro metadata. `SongEditorDialog` offers file import and clipboard paste, validates UTF-8 files, and leaves existing editor content intact on failed imports. Imports populate the Save/Cancel editor; they do not write the repository until Save. Section headings identify import boundaries; stable section metadata and a full Reflow editor remain future work.

## Batch renaming

`BatchRenamer` implements Name and Number, Replace Text, Add Text, and Keep Current Names. It preserves extensions and validates duplicate names case-insensitively with Unicode normalization. It rejects path separators, invalid Windows/macOS filename characters, reserved Windows names, trailing dots/spaces, excessive length, and invalid numbering ranges.

`MediaImportDialog` previews every current/new name before applying. Initial import uses those names for the copied physical files. Batch renaming an existing presentation changes its slide labels in SQLite while keeping physical storage paths stable; this protects live snapshots and duplicate presentations. It does not rename the user's original files or relocate managed files. The dialog explicitly distinguishes these operations.

## Operator interaction and rendering

The left sidebar contains the searchable content library and named services, separated by a vertical splitter. The center offers Grid thumbnails or a Text list with wrapped lyrics; a size slider adjusts thumbnails or operator text. `SlideTextDelegate` reads the existing slide roles without modifying presentation content. Thumbnails are cached at 320×180 to support the full size range. The right shows Live with audience controls immediately underneath, Up Next/Selected preview tabs, transport, and contextual timing/media controls. Both splitter positions, slide view/size, and preview tab persist in QSettings. Preview tabs retain a minimum height that fits their child preview, including during timed video playback at 1080×680.

Lyrics/media import and the background library are available from the sidebar. Presentation timing/transitions and background assignment are available in library/service context menus; global transition defaults are visible below the slide workspace and in Settings/File. Media presentation titles, labels, and arrangements persist. Picture cards show decoded thumbnails; video cards identify the filename/type. Missing media receives an operator warning.

Show mode takes a clicked slide live. Edit mode selects for preview and enables service-local reordering. Explicit Take Live remains available. Selecting another entry previews it without changing the live service. `ReorderListWidget` explicitly changes model row order for list/grid drags and saves after a complete reorder. The rich text slide editor supports direct canvas editing and per-slide undo/redo; bulk lyric import retains the Save/Cancel separator editor. Draft changes are not autosaved.

Keyboard handling excludes modal dialogs and text-entry controls. Escape returns focus to operator controls while preserving fullscreen output. The platform Find shortcut focuses the existing library search and selects its query. Return to Live in the toolbar reveals the corresponding saved service/entry through the snapshot-matching selection logic, without recapturing the snapshot, restarting playback, or restoring hidden audience content. Changed or removed saved cues receive a status message. Safety shortcuts, selected service, output routes, program transition defaults, and geometry use QSettings. See the [ProPresenter UI audit](PROPRESENTER_UI_AUDIT.md) for the comparison and verification.

`SlideRenderer` paints a fixed 1920×1080 logical canvas at each audience/preview surface. Text uses identical margins/wrapping/auto-fit. The presentation background is the base layer, using cover/crop and dimming for readable lyrics. Imported pictures and video frames sit above it, fitted without cropping or dimming; unused space reveals the background. A full-canvas JPEG covers that layer. Clear hides text while retaining background and media. Reusable themes are not implemented.

The audience `DisplayWindow` owns the transition compositor. Cut, Fade, Slide Left, and Slide Right combine cached outgoing/incoming canvases; a 16 ms timer updates one shared progress value. The operator live monitor paints through that same compositor, including when the audience window is hidden. Re-cueing during an animation captures its current composite as the new outgoing frame. Incoming media waits for the first decoded frame before its transition begins, retaining the outgoing visual during loading. Output clear/blackout/restore cancel animation immediately. Stage, selected/next previews, and thumbnails remain static. No transition paint operation reads files or queries SQLite.

## MediaPlayback: one audience decoder

`MediaPlayback` owns one `QMediaPlayer`, `QAudioOutput`, and `QVideoSink`. The audience and operator live monitor draw the same decoded frame, avoiding separate decoders drifting apart. Pictures decode asynchronously; generation checks discard stale completions after rapid navigation. Playback errors and missing files are reported in the operator UI. A timeout catches video sources that produce no frames.

Video controls pause/resume, restart, and mute. An import checkbox sets looping for videos in that presentation. Non-looping videos hold their last frame unless the independent presentation timer advances the slide. Auto-advance counts from the cue, including load/transition time, and does not follow clip completion or Pause Video. Re-cueing deliberately restarts a clip. Audio uses the system output device; independent audio-device routing is not implemented. Runtime Qt Multimedia/OS codec support determines which files actually play.

## Display routing

`OutputManager` routes each output to Off, Windowed Preview, or a fullscreen screen and persists desired identities. It rejects sending audience and stage to the same fullscreen identity. A missing target stays selected as Disconnected while hidden; screen-added notifications retry it. Geometry changes refresh routing without cycling a healthy fullscreen window. Closing sets Off; Escape requests operator focus. Visibility/minimization changes update status without reopening a deliberately hidden window.

Screen identities combine OS name, manufacturer, model, and serial number. Identical identities are offered as one route. Projector identity changes, macOS Spaces, and real display reconnection need physical validation. The stage output is separate and does not play audience media/audio.

## Verification and remaining work

The media milestone built on macOS 15.7.4 with Qt 6.12 and AppleClang 17 and passed its nine CTest suites; its historical validation is recorded in [MILESTONE_0.3.md](MILESTONE_0.3.md). The current eleven suites additionally cover lyric parsing/import, schema-v5 migration, rich text editing/rendering, global transition inheritance/overrides, and presentation background layering. Operator and video-playback tests use Cocoa on macOS because AVFoundation needs its native event dispatcher; other widget suites use offscreen rendering.

Offscreen tests do not establish real projector routing, physical mouse dragging, codec coverage, audio-device behavior, or hardware-accelerated fullscreen video. Windows and physical multi-monitor testing remain outstanding. Keep backup/restore, real-output rehearsal, large-media profiling, and bounded cache/memory checks alongside the next explicit slide-editor/Reflow/themes milestone.

## Rich slide text and global transition controls

Schema 5 adds `slides.rich_text`, defaulting to an empty string so existing slides keep their original renderer. `Song`, `PresentationItem`, and `LivePresentation` carry aligned rich-text arrays, and upcoming/loop previews carry the corresponding rich text. Plain text remains separately searchable and readable on Stage. `updatePresentationText` saves the title, plain text, and HTML transactionally without replacing slide IDs, media, background assignments, or source-slide arrangements. Legacy plain-text edits preserve HTML for unchanged slides and clear it where the text changes.

`SlideEditorDialog` uses a 1920×1080 graphics scene with an editable `QGraphicsTextItem`, the shared rendered background/media layer, and a thumbnail navigator. Documents retain independent undo/redo history across navigation. `RichTextSupport` canonicalizes supported text/paragraph formatting and blocks embedded resources. The same font sizes, text area, and proportional fit are used by editor and audience. A bounded cache avoids re-parsing identical HTML on every video frame. New presentation drafts allow adding/copying/removing slides; editing existing presentations preserves their structure.

The global transition bar is visible below the slide workspace, with an additional Settings-menu dialog. QSettings stores default type and duration. `setGlobalTransitionDefaults` updates the controller's pending defaults without emitting a cue or restarting timing/media. Subsequent inherited cues resolve the latest global default; explicit presentation overrides remain fixed. Saved slide content and presentation-specific settings retain live-snapshot protection.
