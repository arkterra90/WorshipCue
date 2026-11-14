# Corrections 2 implementation status

## Delivered in this increment

Priorities 1–6 are implemented with feature-specific tests. The ordered remaining work begins with Priority 7.

### 1.1 Media Library

- Three primary sidebar sections provide Content Library, Service Playlist, and Media Library.
- Standalone images, videos, and audio have independent database identities. Playlist entries reference presentations or media assets directly.
- Import supports reviewed naming, asynchronous copying, cancellation, optional looping, optional batch presentations, and optional insertion into the captured service.
- Media supports search, folder labels, metadata inspection, independent preview, rename/organize, confirmed removal, direct live triggering, and drag/drop insertion into services.
- Copies and repeated playlist entries share asset paths. Presentation references and saved service references protect assets from removal.
- Managed files are retained when an unreferenced asset record is removed. Ordinary removal recovers no disk space; the Priority 6 cleanup workflow now provides reviewed Recovery and permanent reclamation.
- Legacy media presentations, service entry IDs, custom orders, files, and paths survive migration.
- Audio uses the existing shared playback transport, including pause, restart, mute, looping, and blackout behavior. Audio transitions start without waiting for a video frame.

### 1.2 Song sections and arrangements

- New songs, lyric import, and MCP song creation support unique named sections, suggested section types, custom names, multi-slide groups, and repeated references. The section editor now exposes individually labelled slides with add, duplicate, split-at-cursor, reorder, and confirmed removal controls.
- Sections can be edited and reordered. Named arrangements can be created, copied, renamed, selected, deleted, saved, and reused.
- Text and rich HTML have one canonical source per section page. Repeated arrangement occurrences reference that source.
- Saving a shared edit updates saved arrangements, operator previews, and active audience text. Cue position, timing, and playback are preserved. Explicit takes capture structural and order changes.
- Legacy presentations acquire section metadata through a Save/Cancel editor conversion.
- Section reordering preserves surviving slide IDs. Complete section occurrences expand together when pages are added. Referenced removals and stale editor saves produce actionable errors.
- Structural saves, slide updates, and custom-service remapping are transactional. Stable page IDs preserve row identity, Looks, HTML, partial service references, and live updates when slides move within a section; legacy numeric keys remain compatible. Multiple Looks/Reflow passes preserve those identities.
- Matching repeated imported headings and lyrics share a section. Different lyrics under the same heading receive distinct names.

### 2.1 Looks and text overflow

- Saved Looks support font family/size/weight/style, text color, horizontal/vertical alignment, margins, padding, and presentation/color/image backgrounds.
- Users can capture a formatted source page, save a named reusable Look, apply it to selected source pages or a presentation, and keep explicit rich text overrides.
- QTextDocument/QTextLayout measurements drive overflow feedback, preview, rendering, and splitting. Line/sentence boundaries and rich document fragments preserve every source character in reading order.
- Overflow application offers a reviewed automatic split, explicit fitting, or cancel. Draft operations support undo/redo. Save commits atomically.
- Split pages retain song section IDs and arrangement references, expand even individual source occurrences in custom service orders, preserve original first-page IDs, and share media references.
- The canvas, previews, thumbnails, upcoming/loop previews, and live output consume the same resolved styles. Saved appearance changes take effect on the next explicit take.
- Later text changes receive safety fitting and editor/operator overflow feedback. Repeated section occurrences share source-page formatting.

### 2.2 Keyboard navigation

- Space, Right, and Down advance one cue. Left and Up restore the previous cue across presentation/media boundaries.
- A data-driven matrix covers each key through the operator, audience, and stage windows, plus text entry, duration controls, modal canvas editing, and Edit mode.
- Central controller navigation and existing mouse workflows remain in use.

### 3.1 Global and presentation-level transitions

- Blackout, Clear Text, and Restore use the active presentation override, then the latest global defaults. Cut, Fade, Slide Left/Right, and zero-duration transitions follow the same rules.
- Output controls preserve service/entry/slide position, cue revision, shared media source, playhead, and remaining auto-advance time. Output-state pause and blackout mute take effect immediately while visuals animate.
- The audience and operator share the compositor. Repeated controls continue from the visible composite. Uninterrupted same-cue fades keep both canvases on the current decoded media frame.

### 3.2 Bulk transition editing

- Extended playlist selection, platform Select All, context-menu actions, and a visible selection button support mixed presentations and standalone media.
- Bulk editing changes type/duration/inheritance only; repeated shared presentations are deduplicated. Presentation scope remains library-wide; media scope is per service entry. The dialog explains that scope and mixed values.
- Session undo/redo uses atomic inverse/forward transactions, preserves later unrelated edits, and refuses stale/missing transition targets without moving history. New edits clear redo.
- Single-item edits can override bulk settings. Active snapshots apply saved changes on the next explicit take.
- Selection survives reloads and right-clicks; multi-selection drag moves the selected group in source order.

### 4.1 Four logical outputs

- Audience Screen 1/2 and Stage Screen 1/2 have independent routes, visibility, configuration, persistence, and status. Fullscreen routes cannot collide.
- Both audience outputs share one compositor/transition clock and one media decoder. Independent output mappings preserve the cue and playhead.
- Closing, disconnecting, reconnecting, and geometry changes are handled per output. Unique serial identities recover renamed displays; ambiguous identities are withheld.

### 4.2 Output formats and capabilities

- Screens exposes per-output logical dimensions, custom aspect, rotation, Fit/Fill/Stretch, and native resolution/refresh/scan choices when available.
- Core Graphics (including expanded Retina/HiDPI enumeration) and Windows display-settings backends enumerate and apply matching OS modes. Read-only native validation reports eight modes and an included current HiDPI restoration mode on this desktop. Missing information remains unknown; custom logical rendering does not invent hardware timings.
- Validation, backend errors, temporary mode leases, conditional desktop restoration, unavailable-mode recovery, and a 15-second keep/revert UI are implemented. No automated test switches a real physical signal.

### 4.3 Custom stage layouts

- The original standard stage is preserved as the default. Named custom layouts support current/next content, title, clock, countdown, message, custom text, backgrounds, fonts, colors, alignment, and normalized layout settings.
- Definitions can be created/copied/edited/saved and independently assigned. Assigned layouts are protected from deletion.
- Each stage screen has independent session-local message and countdown controls. Output assignments and reusable layouts persist.

## Important modified files

| Files | Purpose |
|---|---|
| `src/data/LibraryRepository.*` | Versioned migration, standalone assets, mixed playlist references, canonical section persistence, transactional order remapping |
| `src/core/PresentationLook.*` | Validated Look definitions and appearance references, shared with the Qt Core MCP process |
| `src/ui/PresentationLookDialog.*`, `src/ui/RichTextSupport.*` | Reusable Look editing, actual layout measurements, split review, fitting, and draft undo/redo |
| `tests/presentation_look_test.cpp` | Look persistence/validation, schema upgrade rollback, exact reflow, media sharing, rendering, UI review, and undo |
| `src/core/SongStructure.*` | Section/arrangement validation, serialization, expansion, stable section/page keys |
| `src/core/LyricsImporter.*` | Preserve imported section groups and share matching repeated sections |
| `src/core/PresentationController.*` | Refresh shared lyrics and resolve output-control transitions without issuing another cue |
| `src/core/MediaSlide.h`, `src/media/MediaManager.*`, `src/media/MediaPlayback.*` | Audio assets, import extensions, shared audio/video transport |
| `src/ui/SongStructureDialog.*` | Section and named-arrangement editor with immediate repeated previews |
| `src/ui/MainWindow.*`, `src/ui/MediaAssetListWidget.h`, `src/ui/ReorderListWidget.*` | Media Library workflow, mixed playlists, drag/drop, structured-song entry points, keyboard bindings |
| `src/ui/MediaImportDialog.*`, `src/ui/SongEditorDialog.*` | Import choices and preservation of reviewed lyric groups |
| `src/ui/DisplayWindow.cpp`, `src/ui/SlideRenderer.cpp`, `src/ui/SlideTextDelegate.cpp` | Shared-lyric repainting, shared output-state compositor, moving-media continuity, audio behavior and labels |
| `src/ui/PresentationSettingsDialog.*`, `src/ui/MainWindow.*` | Mixed-selection transition dialog, scoped actions, transactional bulk undo/redo |
| `src/mcp/McpServer.cpp`, `src/mcp/McpCreationTools.cpp` | Compatible mixed-service inspection and structured song creation |
| `tests/song_structure_test.cpp`, updated repository/controller/operator/import/MCP/render/playback tests | Feature-specific and regression coverage |
| `src/ui/OutputConfiguration.*`, `src/ui/NativeDisplayModes*`, `src/ui/OutputManager.*` | Four-route format/capability model, native backends, recovery, leases, and persistence |
| `src/ui/StageLayout.*`, `src/ui/StageLayoutDialog.*`, `src/ui/OutputSettingsDialog.*` | Stage rendering/editor/runtime controls and four-tab output configuration |
| `tests/stage_layout_test.cpp`, `tests/output_manager_test.cpp` | Format and stage rendering, serialization, routing, recovery, native-mode test backend, Keep/Revert/timeout confirmation paths, and independent stage runtime |
| `CMakeLists.txt` | Include the shared model/editor and dedicated song-structure test suite |
| `README.md`, `docs/ARCHITECTURE.md`, `docs/MCP.md` | Updated user and developer instructions |

## Database changes

Schema 5 upgrades transactionally through versions 6, 7, 8, and 9.

1. Version 6 adds `media_assets` and registers existing managed media paths. Existing import times remain unknown. It rebuilds `service_entries` with mutually exclusive `song_id` and `media_asset_id` references, protects referenced assets, and preserves the service entry sequence high-water mark. It adds media lookup indexes.
2. Version 7 adds `songs.song_structure` with an empty default. The validated JSON contains section groups, named arrangement references, and the selected arrangement. Lyrics and HTML remain in the existing unique slide rows.

3. Version 8 adds `looks`, presentation/source-page Look references, rich-text override flags, and explicit fit flags. Legacy appearance defaults are preserved. Tests exercise version-7 upgrade and rollback after a partial migration failure.

4. Version 9 adds type/duration/inheritance fields for standalone media service entries. Existing entries inherit defaults; copies preserve overrides. Tests cover version-8 migration and rollback after a partial failure.

Older migration paths are exercised by the repository tests. Unknown newer versions remain rejected. Backups include the closed database and sibling `media` and `backgrounds` folders. Old application versions reject the upgraded library.

## Build and test validation

The macOS GUI application, bundled MCP executable, and all test targets build using AppleClang 17 and Qt 6.12. The isolated build directory is `/private/tmp/worshipcue-c02-20261112`.

```sh
cmake -S . -B /private/tmp/worshipcue-c02-20261112 \
  -DWORSHIPCUE_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build /private/tmp/worshipcue-c02-20261112 -j 6
ctest --test-dir /private/tmp/worshipcue-c02-20261112 --output-on-failure --timeout 90
```

The nineteen-suite regression run passes. Native macOS access is required for the Cocoa operator tests, AVFoundation playback tests, and isolated CoreMIDI round-trip test. The restricted sandbox cannot supply all of those native services.

| Feature | Automated verification |
|---|---|
| Media/presentation separation | Repository and operator tests verify imports create asset records and direct playlist references |
| Shared paths and persistence | Repository tests verify repeated registration, service copies, duplicate entries, restart, and protected removal |
| Import and preview | Operator tests exercise import, local preview, rename/folder editing, metadata, and direct take. Import tests cover copying, rollback, and cancellation |
| Audio | A generated silent PCM WAV exercises real import and decoding, duration/position, pause/restart/mute, blackout, and frame-independent transitions |
| Mixed service navigation | Operator tests cross text/media boundaries forward and backward and verify text-entry guards |
| Shared lyrics | Controller, repository, renderer, and operator tests verify every repeated occurrence and active audience text update |
| Named arrangements | Dedicated tests cover switching, copying, independent source groups, repeated references, editor previews, and persistence |
| Multi-slide sections | Tests cover expansion, sequential navigation, source reordering, page growth, surviving row IDs, protected removals, and stale editor rejection |
| Transaction safety | Repository/section tests inject SQL failures and verify rollback |
| Imports and MCP | Tests cover repeated heading deduplication, differing lyrics, canonical versus expanded counts, song creation, and mixed service inspection |
| Looks and overflow | Dedicated tests verify style capture, uniform and rich-override rendering, selected/all application, exact Unicode/text preservation, preview/cancel/undo/redo, source IDs, section/custom-order mapping, media sharing, and safety fit |
| Keyboard navigation | All five required keys are tested through operator and audience/stage windows, arranged songs and media, and editor/focus guards |
| Output controls | Data-driven renderer tests cover all types, global/explicit precedence, zero duration, visible-composite interruption, and cue/timer preservation. Native H.264 playback verifies Clear fades keep the shared moving frame and source |
| Bulk transitions | Repository/operator tests cover mixed and repeated selection, Select All, per-entry media, cancel, inheritance, individual overrides, keyboard undo/redo, persistence/copy, stale/missing targets, atomic rollback, and unrelated-setting preservation. Reorder tests verify group order/selection |
| Outputs and stages | Routing/model/native-operator tests cover all four outputs, configuration persistence and conflicts, capability failures/recovery/restore via a test backend, custom stage layouts, runtime controls, and HD/4:3/portrait/wide rendering |
| MIDI | Engine/repository/native/operator tests cover routing, parsing, reconnection, conflicts, retrigger/feedback gates, action persistence, stable targets, migration rollback, learning, cue snapshots, and actual isolated CoreMIDI input/output |
| Cleanup and storage | Dedicated repository and native operator tests verify dates/eligibility, shared/active references, estimates, review/cancel, Recovery, permanent reclamation, migration rollback, and interrupted removal |
| Compatibility | Existing suites exercise prior migrations, media presentations, background assignment, timing, rich text, routing, and MCP transport |

Screenshots of the song editor, Media Library, Looks / Reflow editor, and bulk-transition dialog/playlist, MIDI device settings, and cleanup/review dialogs are visually inspected during verification. Physical pointer dragging, projector hot-plug, audio hardware output, and Windows behavior require platform/hardware validation. The PCM fixture verifies the transport with silence.

## Inspection and ordered remaining implementation plan

### Priority 2 — Completed

Reusable Looks, measured overflow handling, reviewed splitting, explicit fit, draft undo/redo, shared rendering, and the keyboard matrix are implemented and tested. Rich text canvas editing and source-page overrides remain available. Windows and physical-output validation remain outstanding.

### Priority 3 — Completed

Output-control transition inheritance and transactional bulk playlist transitions with session undo/redo are implemented and tested as described above. Physical-display and Windows validation remain outstanding. Undo history is intentionally session-local.

### Priority 4 — Completed

Four outputs, independent rendering formats, native capability backends, temporary mode handling, reusable stage layouts, and independent stage runtime controls are implemented and tested. Native macOS capability discovery is read-only in automated validation. Physical mode switching/hot-plug/rearrangement and Windows runtime validation remain outstanding.

### Priority 5 — Completed

- Native CoreMIDI (macOS) and WinMM (Windows) transports discover configured hardware/virtual/network endpoints, save per-device enable/channel preferences, reconnect by identity, and report incoming/outgoing activity. MIDI defaults off. macOS can publish WorshipCue virtual Input/Output ports; Windows uses installed virtual/network drivers.
- Settings provides Devices, central Input Mappings, and a bounded Activity monitor. Mapping editors support input learning, Note On/Off and CC, exact/wildcard source/channel/value, global navigation/audience/playback commands, select/take presentation, and stable source-page/asset/service-occurrence targets.
- Right-click **MIDI Actions / Input Trigger…** and **Slide MIDI…** provide multiple ordered output commands with independent destination/channel/message/number/value and explicit Save/Cancel. Shared source assignments follow repeated arrangements. Direct media runs asset defaults followed by entry commands; service copies retain entry outputs.
- Conflicting enabled inputs are rejected atomically. Held notes, unchanged CC values, debounce, outgoing echo suppression, connection generations, Show/dialog gates, and bounded native delivery prevent accidental control/retrigger. Disabled/disconnected outputs skip cues without replay. Owned notes receive release attempts on disable/shutdown.
- Live snapshots retain their action lists; only cue activation emits them. Source IDs survive reordering, same-count text editing, and the first reflow fragment. New/copied source pages/presentations start without commands. Dispatch revalidates saved mappings and fresh target orders. Deleted targets cascade their mappings.
- Schema 10 preserves existing content/IDs/references while adding action/mapping storage and non-reusable song/slide identities. Migration failure and stale writes roll back safely.
- Three dedicated MIDI suites cover transport/engine, repository/migration, and an actual isolated native CoreMIDI input/output round trip. Operator tests cover settings, slide actions, learning, conflicts, target resolution, snapshot behavior, selection/take/global commands, and Edit gates. Device UI was visually inspected. Full validation now comprises 18 CTest suites.
- Windows compilation/runtime, physical devices, and OS network-session interoperability still require platform/hardware validation. No physical MIDI destination receives automated test messages.

Workflow references: official [ProPresenter MIDI setup](https://support.renewedvision.com/hc/en-us/articles/1500000020301-Devices-MIDI-ProPresenter-Setup) and [system MIDI setup](https://support.renewedvision.com/hc/en-us/articles/4412376777491-Devices-MIDI-System-Setup). Implementation and assets are WorshipCue's own. See [README MIDI instructions](../README.md#midi-control) and [architecture](ARCHITECTURE.md#midi-integration-corrections-2-priority-5).

### Priority 6 — Completed

- **Settings → Library Cleanup / Storage…** offers custom 1–36500 days, background scanning, separate date columns, protected/unknown-age explanations, individual or all-eligible selection, unique-file storage estimates, and an explicit record/file review.
- Schema 11 adds separate creation/import/modification/actual-use tracking. Live takes/navigation and actual media previews record the current source/assets/backgrounds, while future service items and browsing do not acquire false usage dates. Legacy dates remain unknown unless genuinely recorded.
- Saved services, presentation references, saved Looks, shared files, full live snapshots, and other running sessions protect content. Unknown usage and unsafe/symlink paths are ineligible. Scans refuse oversized partial results.
- Confirmed **Move to Recovery** preserves all original records, source IDs, Looks, arrangements, MIDI assignments, and files. Restore reverses it. Recovery frees no disk space; a separate reviewed **Permanently Delete** operation rechecks references/usage and reclaims only unshared managed files.
- Durable staging/journaling restores uncommitted interrupted removals and completes only already-confirmed committed removals. Conflicting files are left untouched; blocked unlinks remain available for Retry. Content reused after archiving requires Restore and a fresh age scan.
- A dedicated cleanup suite covers migration/unknown dates, actual usage, custom periods, shared references/estimates, stale data, live/cross-connection leases, Recovery/IDs/MIDI, unsafe files, post-archive use, SQL failure after staging, and interrupted/committed operations. Native operator tests cover selection, both cancellation points, restoration, permanent removal, live usage/upcoming protection, and continuous timed output during scans. Both cleanup dialogs are visually inspected.
- The regression now comprises 19 suites. Native Windows filesystem and process-liveness behavior still requires platform validation.

See [cleanup instructions](../README.md#library-cleanup-and-recovery) and [architecture](ARCHITECTURE.md#library-cleanup-usage-and-recovery-corrections-2-priority-6).

### Priority 7 — MCP integration

The stdio companion, setup dialog, client configuration generation, protocol tests, development path, and bundle inclusion already exist. This increment preserves compatibility with media entries and shared song arrangements.

Add dedicated standalone media operations and structured section/arrangement mutations, bounded validation, expanded diagnostics, and packaged execution checks. Test each mutation's integrity and failure behavior. Test the packaged companion from a clean installed environment.

### Priority 8 — Offline help

Documentation currently lives in repository Markdown files. A packaged offline documentation hub is unimplemented.

Add a visible Help entry, navigable/searchable local content, shortcuts, and resource packaging covering implemented workflows. Test resource inclusion, offline navigation/search, and UI wording accuracy. Add licensing/MIDI instructions as those features become available.

### Priority 9 — Licensing

The licensing client, Cloudflare Worker/D1 service, signing/verifying leases, secure credential storage, live-output authorization, activation UI, and developer-license issuance are unimplemented.

Implement the authoritative backend and migrations, a five-installation transactional activation limit, signed expiring offline leases, replay-resistant transfer/deactivation, OS credential storage, and authorization enforcement across all live-output entry points. Add tests for invalid/expired/revoked leases, duplicated installations, concurrent sixth activations, transfer, replay, and editing-only mode. Provision a developer license through the deployed real backend after Cloudflare account/deployment access is configured. Keep all credentials out of source and logs.

### Priority 10 — Distribution

CMake currently builds a macOS application bundle with icons/background resources and its MCP companion and provides basic install targets. Repeatable DMG/Windows installer pipelines, dependency deployment validation, signing, and notarization are unimplemented.

Add release scripts and CI/build targets for deployed Qt dependencies, DMG and Windows installer creation, production MCP/help/migration/license resource checks, and optional signing/notarization using supplied credentials. Test installation/uninstallation and launch on clean macOS and Windows systems. Mark unsigned development builds explicitly. Distribution readiness remains unverified.

## External setup and limits

- A Windows Qt/compiler environment and native Windows runtime are required for Windows validation.
- Real displays, physical MIDI devices/network sessions, and audio hardware are required for their respective integration checks. Isolated native CoreMIDI virtual-port I/O is tested.
- Cloudflare account/deployment access is required when implementing and deploying the authoritative licensing backend and issuing the real developer license.
- Apple signing/notarization credentials and Windows signing credentials are required for signed release validation.
- Later priorities have implementation and test plans above. They follow the completed Priority 1–6 increments.
