# Corrections 2 implementation status

## Delivered in this increment

Priorities 1–8 are implemented with feature-specific tests. The ordered remaining work begins with Priority 9.

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

### Presentation workspace focus audit

The center's five large editing buttons are consolidated into a flat **Presentation** dropdown in the workspace header. Slide text/MIDI and presentation arrangement/Look/timing actions remain available through their existing handlers. Slide context menus share the same QActions and enabled states. **Take Live** sits between Previous and Next beneath the live monitor, and saved timing/background details use secondary text below the grid.

The macOS local app was rebuilt and all 20 regression suites passed after the change. Native operator checks open the dropdown, select the timing editor, cancel, and verify cue/Blackout preservation. They verify action availability, empty-selection disabling, transport placement, a slide grid taller than half the window at 1080×680 and 1280×760, and controls fitting during timed-video playback. The workspace and menu were visually inspected. The README and embedded Help articles describe the new locations. See [the controls audit](WORKSPACE_CONTROLS_UI_AUDIT.md).

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
| `src/mcp/McpLibraryTools.cpp`, `src/mcp/McpConnectionProbe.*`, `src/mcp/main.cpp` | Media lookup, revision-checked shared-song edits, asynchronous connection diagnostics, and bundled macOS plugin discovery |
| `tools/check-mcp-package.py`, `tests/mcp_server_test.cpp` | Installed-runtime MCP smoke check and protocol/mutation/diagnostic integrity coverage |
| `src/ui/HelpDialog.*`, `docs/help/*`, `tests/help_dialog_test.cpp` | Embedded offline articles, topic navigation/search/history, native keyboard reference, and resource/link/focus coverage |
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

All twenty-one regression suites pass, including confidence monitor timers (62.73 seconds on November 13, 2026). Native macOS access is required for the Cocoa operator tests, AVFoundation playback tests, and isolated CoreMIDI round-trip test. The restricted sandbox cannot supply all of those native services.

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
| Offline Help | Dedicated tests load all 16 embedded topics from an empty working directory, check every related-topic link, search/highlights/history, empty results, shortcut labels, resource boundaries, and Find/Escape. Native operator tests cover Help menu entry points, live cue/timer preservation, and shortcut-reference refresh |
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

### Priority 7 — Completed

- Preserved the existing stdio companion, negotiated legacy and per-request protocol paths, setup/configuration generation, development executable, and bundled install. The tool inventory now has 15 operations.
- `list_media_assets` / `get_media_asset` discover active standalone assets without exposing file bytes. `create_service_order` accepts direct `media_asset_id` items, including repeats, alongside existing presentations and new text. One transaction validates active references and rolls back the entire order on failure; shared files are reused. Recovery content remains excluded.
- `get_presentation` returns an edit revision. `update_song_section` edits the authoritative lyrics of an existing section without changing page count/IDs; changed pages clear rich overrides, while unchanged formatting, Looks, MIDI actions, service orders, and repeated references survive. `save_song_arrangement` creates/updates named ordered section references and optionally selects the active arrangement. Both default to a dry run, enforce bounded typed inputs, reject stale revisions, and reuse the repository's original-state checks and transaction rollback. Live snapshots remain under operator control.
- **Settings → AI / MCP Setup… → Test Connection** asynchronously checks the actual executable, MCP negotiation, current tools, and this window's library. It provides actionable missing/runtime/protocol/library/version/timeout reports, stops on close, and omits raw environment/stderr and credentials.
- Fixed the macOS console companion's deployed plugin lookup: because it is not the bundle's primary executable, it now explicitly adds the enclosing `.app/Contents/PlugIns` before opening SQLite. Standalone development paths are unchanged. The installed-package check caught this failure with developer Qt settings removed.
- `tools/check-mcp-package.py` validates deployed Qt/SQLite/TLS dependencies, then exercises actual installed stdio, invalid-input recovery, media lookup/repeated mixed services, shared edits, arrangements, and persistence in a temporary home/library/working directory. It checks only the companion and its SQL/TLS dependency graph, not GUI-only plugins or an installer.
- The installed macOS check passed against `/private/tmp/worshipcue-p7-final/WorshipCue.app/Contents/MacOS/WorshipCueMcp`, with Qt deployed and developer environment settings removed. The temporary bundle uses ad-hoc signing; it is not a signed/notarized release or a clean-OS certification.
- On November 13, 2026, the macOS GUI/companion and all targets built. All 19 regression suites passed with native macOS access for Cocoa, CoreMIDI, and AVFoundation; restricted-sandbox failures in those services passed after rerunning with native access. The MCP suite also verifies missing/corrupt-library startup, failed-process recovery, stale edits, rich-text/Look/MIDI identity preservation, and injected SQL rollback. The setup dialog and its successful real connection test are visually inspected.
- Schema remains version 11; no migration is required. User instructions, mutation limits, protocol references, and package commands are in [MCP.md](MCP.md). Windows runtime, clean OS images, real AI-client configuration, and live Planning Center accounts remain external checks. Distribution signing/installers and unrelated Qt GUI-plugin deployment remain Priority 10 work.

### Priority 8 — Completed

- **Help → WorshipCue Help…** opens a modeless offline reader. **Keyboard Reference…** opens the shortcut topic directly. The platform Help shortcut also opens the guide, and repeated menu actions reuse the open window.
- Sixteen embedded Markdown articles cover all 15 requested topics plus a keyboard reference. Articles use current UI labels and include links to related local topics. The licensing article now describes activation/device management and offline authorization from Priority 9.
- Search covers article titles and rendered content, matches all case-insensitive words, highlights results, and explains empty results. Topic selection, Home, Back/Forward history, local links, and platform Find/history shortcuts support keyboard and mouse navigation.
- The keyboard reference displays configured audience shortcuts with native platform symbols and refreshes when the shortcut editor saves changes. Markdown punctuation in custom key labels is escaped for literal rendering.
- The Help window stays outside the operator/output keyboard filter. Its reading/search controls preserve the current cue, and automatic presentation timing continues while Help is open.
- CMake embeds the catalogue and every article in the GUI executable. Installation carries the resource collection with that binary. A temporary installed macOS app was checked for the initialized Help collection, catalogue, and all 16 article resource entries.
- The macOS GUI/companion and test targets build. All 20 regression suites pass with native macOS access where required. Dedicated Help tests verify resource and link completeness from an empty working directory, body search, case/multiple terms, highlights, history, empty results, literal shortcut labels, local resource loading, Find, and Escape. Native operator checks verify both menu entries, window reuse/reopening, automatic shortcut-reference updates, and live cue/timer preservation. The Help window is visually inspected.
- Schema remains version 11. Windows runtime and full signed installer verification remain external validation. Help uses Qt Widgets and the application's compiled resources.

### Confidence monitor timers — Completed

- Researched ProPresenter's dedicated timer controls, duration/scheduled/elapsed modes, stage-linked timer objects, overrun, and color warnings. Primary sources and implementation scope are recorded in [CONFIDENCE_MONITOR_TIMERS.md](CONFIDENCE_MONITOR_TIMERS.md).
- The quiet **Timers…** toolbar button opens a reusable modeless panel with independent named timers for Stage Screen 1 and 2. Duration countdown, countdown to a local date/time, and elapsed time support Start/Restart & Show, Pause, Resume, and Reset, plus optional limits, overrun, and warning thresholds.
- **Start & Show** displays a large timer footer on the standard confidence layout. Custom layouts retain their Timer element placement or receive the footer when no Timer element exists. Approaching the endpoint turns amber; reaching it turns red. Hiding the timer or closing the panel leaves its clock running. A rendered preview and routing status explain which screen will show it.
- Timer configuration and visibility persist independently per stage output; app restarts restore Ready state. Runtime control preserves slide cues, media, audience blackout, and automatic presentation timing. Schema remains 11. Offline Help includes the operator workflow.
- All 21 regression suites pass. Core, renderer, output persistence, and native operator tests cover the modes, invalid input, formatting, pause/resume/reset, limits/overrun, warning colors, independent screens, visibility, and modeless lifecycle. The local macOS app is rebuilt, and the panel and rendered confidence screen are visually inspected. Physical confidence monitors and Windows runtime remain external validation.

### Planning Center / SongSelect import readiness

- Verified the current Planning Center Services documentation for Item song/arrangement includes, arrangement `lyrics` and sequence arrays, scoped Plan endpoints and ID filters, and PAT/OAuth authentication. The existing MCP path remains the operator workflow; this increment does not introduce a separate GUI plan browser.
- Fixed browser-plan-link resolution to search documented scoped plan collections, with direct service-type/plan IDs supported and bounded discovery. Requests validate returned plan identity and support either church-local PAT credentials or an externally managed OAuth access token. Auth/permissions/missing-plan failures provide distinct messages. Credentials remain outside SQLite, QSettings, tool arguments, and logs.
- Imported arrangement lyrics now retain reusable named sections, multi-page groups, and canonical repeated lyrics. Custom sequence labels support Unicode. Invalid sequence types, unknown labels, and conflicting text under a label require resolution. Song sections, slides, and the service share the existing atomic transaction; injected database failures verify rollback and a subsequent successful save.
- **Settings → AI / MCP Setup…** can include credential placeholders in the copied client configuration and explains importing SongSelect Lyrics into Planning Center's selected **Lyrics & Chords** arrangement first. An attached PDF alone is insufficient. The local connection test remains explicitly separate from Planning Center account verification.
- `tools/check-planning-center.py` verifies the actual companion's tools and Services access and optionally previews a plan in a temporary library. Four isolated subprocess checks cover connection-only operation, preview-only imports, unresolved-lyrics guidance, missing credentials, credential omission, and cleanup. Setup details are embedded in offline Help and documented in [MCP.md](MCP.md#connect-planning-center-services).
- All 21 CTest suites pass (63.96 seconds on November 13, 2026), as do the checker's four subprocess tests. Native operator tests exercise placeholder generation/copy/reset and the real local companion connection. The updated setup dialog is visually inspected, and the local app/companion are rebuilt.
- Live Planning Center credentials are not configured in this workspace, so a real church account and SongSelect-populated plan have not been exercised. Church-local PAT setup is supported; distributed multi-church authorization requires a registered OAuth integration and token lifecycle management outside the companion.

### Priority 9 — Licensing

Implemented the editing/live split, Qt licensing client and activation/device dialog, protected macOS Keychain/Windows DPAPI state, Ed25519 seven-day leases, renewal and rollback checks, and shared live authorization across controllers, all four output windows, media controls, and MIDI. Unlicensed editing, media inspection, libraries, and saved services remain available. License loss clears live content and closes outputs without deleting editing data or saved routes.

The Cloudflare Worker is deployed at `https://worshipcue-licensing.jerqin.workers.dev`, with its own `worshipcue-licenses` D1 database and two applied migrations. Atomic nonce/mutation batches, primary-consistent reads, SQL activation triggers, and idempotent UPSERT enforce five installations and replay-resistant transfer. Credentials are generated server-side, stored as peppered verifiers, and excluded from desktop/MCP state. Rate limits, bounded HTTPS requests, safe errors, separate admin authentication, issuance/revocation tooling, and ignored protected local secrets are included.

One real developer license was issued by the backend and activated with the actual Qt client in this workstation's Keychain. Its credential remains in ignored `licensing/secrets/developer-license.json`; no code or private signing key is committed or printed. Live Cloudflare/D1 tests verified concurrent cap enforcement, duplicate registration, renewal, transfer/deactivation, and temporary-device cleanup. The production licensing dialog was visually reviewed.

Local Worker tests cover invalid/revoked licenses, stale/tampered/replayed signatures, concurrent sixth attempts and duplicates, transfer, remote release, rate limits, and safe persistence. Qt tests cover lease authenticity/expiration, copied installations, rollback/protected denial state, storage failures, denied output rendering/restoration, editing-only UI/library behavior, and licensed operator regressions. See [LICENSING.md](LICENSING.md) for build/test commands and full operating/recovery details.

All 22 desktop CTest suites pass (24.66 seconds on November 14, 2026), and all five local Worker tests pass. The actual backend reports one issued license and one active workstation after temporary integration-device cleanup.

Windows DPAPI/runtime and physical display hardware remain unverified. Existing offline leases can outlive remote release/revocation by up to seven days; an idle live output is cleared within the 30-second watchdog. Runtime bundling is handled by Priority 10; release signing and clean-machine installer acceptance remain pending. No local library schema change was needed.

### Priority 10 — Distribution

Implemented a native release pipeline in `tools/package-release.py` and a reusable installed-runtime checker. CMake supplies shared version/channel information, macOS Info.plist metadata, Windows icon/version resources, and legal-notice install rules. Development, unsigned, signed-unnotarized, and notarized artifacts are distinguished; unsigned app windows/Finder names are labelled. Normal editor/library behavior remains unchanged.

The macOS path builds Release, stages both GUI and MCP, deploys Qt plus remaining third-party libraries, writes relative Qt configuration, restricts bundled plugin searches, checks dependency closure/architecture/minimum OS, signs inside-out, and produces a DMG with an Applications shortcut. Optional Developer ID, hardened runtime, timestamping, app/image notarization, stapling, and Gatekeeper checks fail closed if requested steps fail. No valid signing identity is configured here, so only ad-hoc integrity signing is tested.

Built `dist/WorshipCue-0.3.0-macOS-arm64-unsigned.dmg` with its SHA-256 checksum and JSON verification report. The bundled Qt 6.12.0/runtime graph contains 69 Mach-O images and requires macOS 15.0+. The image is verified/mounted read-only, its app copied to a fresh path with spaces, and the copied GUI/companion checked with developer environment paths removed. Tests confirm actual H.264 decoding, TLS, 16 Help articles, 20 backgrounds, icon loading, SQLite migrations/persistence, public licensing configuration/invalid-lease rejection, and real MCP library/song/service workflows. User libraries, settings, Keychain and live outputs are untouched by the package probe. A separate visual launch check of the installed DMG copy passed after the user approved macOS Keychain access for the new ad-hoc signature: the unsigned label, existing-license renewal and updated offline Help were verified. Dependency license texts and available original notices/SBOMs are included; private licensing/signing files are excluded.

The Windows recipe deploys Qt, OpenSSL and app-local MSVC runtime DLLs, uses a stable per-user Inno Setup installation ID with shortcuts and uninstall support, and supports Authenticode signing of GUI/MCP/setup/uninstaller. Its native installation test refuses an existing registered WorshipCue installation, installs into a temporary path, checks GUI/MCP, uninstalls and preserves an untracked user file. A manual GitHub Actions workflow prepares macOS arm64 and Windows x64 builds/tests/artifacts without publishing or receiving private license/backend credentials. **Windows compilation/installer execution and this new workflow have not run in this environment.**

All 24 CTest suites pass on macOS (51.61 seconds on November 14, 2026), including the isolated package probe and seven Python packaging safeguards. No database schema change is required. See [RELEASING.md](RELEASING.md) for commands, prerequisites, signing setup, dependency provenance and clean-machine release gates.

Remaining: native Windows/Intel artifacts and upgrade/uninstall validation, real signing/notarization, Gatekeeper/SmartScreen/download checks, and clean OS testing without developer software. Public distribution readiness is not claimed. Website download publication remains a separate authorized release action after these gates pass.

## External setup and limits

- A Windows Qt/compiler environment and native Windows runtime are required for Windows validation.
- Real displays, physical MIDI devices/network sessions, and audio hardware are required for their respective integration checks. Isolated native CoreMIDI virtual-port I/O is tested.
- Cloudflare licensing is deployed and this workstation has a real developer activation. Keep the ignored secret backup secure; future operations require authorized account/admin access.
- Apple signing/notarization credentials and Windows signing credentials are required for signed release validation.
- Priority 10 has a tested unsigned macOS artifact and prepared Windows/signing paths; the remaining native/distribution acceptance gates are documented above.


### Remembered activation without login prompts

Per the user's follow-up, macOS activation persistence now uses an atomic, encrypted `licensing/activation-state.bin` cache with 0700 directory and 0600 file permissions, bound to the current machine/user. Valid saved activation opens immediately and renewal remains silent across unsigned builds. Windows keeps noninteractive DPAPI. Master license codes and OS login credentials are not retained by the desktop.

Legacy Keychain migration explicitly disables interaction for the read (the per-query UI flag alone does not cover legacy items), restores the process policy afterward, and skips denied/locked records without opening a prompt. Existing accessible activations preserve their installation ID. An inaccessible older record needs a one-time activation-code entry and may need its old registration released; future launches reuse the private cache.

Regression tests cover encrypted persistence across two fresh managers, unchanged installation identity, 0600/0700 permissions, wrong binding keys, tampering, symlink rejection, and preservation of prior state on invalid writes. The actual native developer activation and a subsequent fresh-process online verification pass without an OS login prompt. Signed leases, expiration, revocation, five-installation enforcement and editing-only restrictions remain in force.


The updated native activation and fresh-process verification complete without OS authentication, and the updated DMG passes its isolated GUI/resource/TLS/video/MCP checks. The full regression run passes 22/24 suites; native operator-focus and media timing suites require rerunning in an unlocked desktop session. A read-only session check confirmed `CGSSessionScreenIsLocked=true` while those suites and optional visual automation could not obtain active windows. No tests or authorization gates were relaxed. The superseded developer registration was released after the one-time storage upgrade; the remembered installation is retained.
