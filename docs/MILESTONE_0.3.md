# Picture/video presentations and batch naming

Implemented November 10, 2026, on the existing C++17/Qt application.

## Operator workflow

1. Choose **Import Pictures / Videos…** in the sidebar or File menu and select multiple files.
2. Review the natural filename order and enter the presentation title. Use Name and Number, Replace Text, Add Text, or Keep Current Names. Numbering supports a starting number and minimum digit count; file extensions are preserved.
3. Review the complete before/after table. Invalid or duplicate filenames prevent import. Check **Loop videos** if desired, then import.
4. WorshipCue copies files in the background into `media/<batch UUID>/<chosen filename>` alongside its database. Progress/Cancel are available. Originals remain unchanged. The new presentation is saved in the library and added to the service chosen when import began, even if the operator browses elsewhere before it finishes.
5. Run image/video slides through the existing Show/Edit, service arrangement, Take Live, and Next/Previous workflow. Pictures are fitted without dimming/cropping. Video controls provide pause/play, restart, and mute/unmute. The audience and operator live monitor share one decoder/playhead. Non-looping videos hold the final frame instead of automatically advancing.
6. Use **Batch Rename Media…** from the library context menu/File menu, or the slide context menu, to rename existing presentation labels. This later operation preserves physical storage paths and the running snapshot. It does not rename original source files or already imported copies.

For example, `1.jpg`, `2.jpg`, and `10.jpg` import in numeric order as `Sunday Announcements 01.jpg`, `Sunday Announcements 02.jpg`, and `Sunday Announcements 03.jpg`. Other naming modes support literal text replacement and prefixes/suffixes before the extension. Validation considers case-insensitive/Unicode collisions and Windows-reserved names and characters.

Clear hides text and keeps picture/video/audio; Blackout hides audience content and temporarily mutes the soundtrack while the playhead continues. Restore does not restart playback and respects the user's mute choice. Stage remains separate, showing current/upcoming media names or lyrics and its clock. Missing/invalid media reports an operator error; file paths and errors are not projected to the audience.

## Storage and reliability

Schema 2 adds slide media kind, relative path, display name, and loop metadata. Existing version-0 and version-1 libraries migrate transactionally, preserving songs and services. Media and text follow the same saved arrangement indices. Text editing refuses to overwrite a media presentation; Edit renames its title instead.

Imports use unique batch directories and atomic individual writes. Failed/canceled batches are cleaned; database-creation failure discards the copied batch. Thumbnail/still decoding and copies run in workers. Generation checks discard stale decoding callbacks after rapid navigation. Live snapshots continue across renamed labels and saved-service edits. Duplicate presentations share files; deletion retains managed media to protect copies and active playback.

Backups and moves need both the closed `library.sqlite3` and its sibling `media` directory. A crash between copying and database commit may leave an orphan import folder; automatic cleanup/journaling and complete backup/restore are future work.

## Changed files

| Files | Purpose |
|---|---|
| `CMakeLists.txt`, `src/main.cpp` | Version 0.3.0; Qt Multimedia/Concurrent; nine test targets |
| `src/core/MediaSlide.h` (new) | Shared typed image/video metadata |
| `src/core/PresentationController.{h,cpp}` | Media-aware live snapshots, upcoming media, cue revisions |
| `src/data/LibraryRepository.{h,cpp}` | Schema 2, media persistence, ordered media, title/label renaming, search and validation |
| `src/media/MediaManager.{h,cpp}` (new) | Async managed copy, cancel/rollback, path resolution, bounded thumbnails |
| `src/media/BatchRenamer.{h,cpp}` (new) | Naming rules, extension preservation, collision and filename checks |
| `src/media/MediaPlayback.{h,cpp}` (new) | Shared decoder, async stills, transport, mute/blackout, looping and errors |
| `src/ui/MediaImportDialog.{h,cpp}` (new) | Reviewable import and later label-renaming plans |
| `src/ui/MainWindow.{h,cpp}` | Import workflow, media cards/previews, playback controls, batch naming, missing-file indicators |
| `src/ui/DisplayWindow.{h,cpp}`, `src/ui/SlideRenderer.{h,cpp}` | Shared media frames, fitted image/video rendering, stage media labels |
| `tests/presentation_controller_test.cpp`, `tests/library_repository_test.cpp`, `tests/slide_renderer_test.cpp`, `tests/operator_workflow_test.cpp` | Expanded persistence, rendering, navigation and native operator coverage |
| `tests/batch_renamer_test.cpp`, `tests/media_import_test.cpp`, `tests/media_playback_test.cpp` (new) | Naming/dialog, copy/cancel/rollback, real decoder and playback behavior |
| `tests/fixtures/` (new) | Original silent 3-second H.264 movie, native regeneration source, fixture documentation |
| `README.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, this report | Operation, semantics, architecture, tests and remaining work |

The existing song editor, service database content, and output routing architecture are preserved. Multimedia and Concurrent are standard Qt modules; no separate FFmpeg dependency was introduced.

## Build and verification

Environment: macOS 15.7.4, arm64, Qt 6.12.0, AppleClang 17.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DWORSHIPCUE_BUILD_TESTS=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Application build passed. **All nine CTest suites passed, zero failures**: controller, repository, rendering, routing, operator workflow, reordering, batch naming, media import, and media playback. The final UI contrast adjustment was rebuilt and the native operator suite passed again.

On macOS the operator and playback suites use Cocoa in a logged-in desktop session. AVFoundation did not deliver decoder callbacks through Qt's offscreen dispatcher; native-window tests successfully decoded the real H.264 fixture. Native tests required access outside the shell sandbox so Cocoa could enumerate screens and use OS media services. Other widget suites remain offscreen. Tests use temporary libraries, imported copies, and isolated preferences.

Coverage includes version-0/version-1 migrations and injected rollback, media metadata reopening and copied arrangements, filename rules and preview collisions, import cancellation/cleanup, real H.264 frames, pause/restart/re-take, true looping, final-frame hold, retained mute through blackout, missing/invalid media, and shared audience rendering. Native operator cases cover initial numeric order, actual renamed copies, importing while browsing another service, later label renaming without interrupting the live snapshot, reopening, and imported-video controls. Picture, rename-dialog, and video-control screenshots were visually inspected; the rename table's alternate-row contrast was corrected.

## Known limits and next work

- Video containers accepted for import are MP4/MOV/M4V/WebM/AVI/MKV. Actual decoding depends on Qt Multimedia and the OS; importing by extension does not guarantee that every codec plays. The test fixture is H.264 in MOV and has no soundtrack. Speaker/device behavior and all codec combinations are not verified.
- Windows, actual projectors, fullscreen/Spaces playback, hot-plug, physical mouse drags, and long/large-media performance remain untested.
- Imported media currently builds a new presentation. Adding/replacing/removing individual source media, relinking missing files, and changing per-clip settings after import need a dedicated media editor.
- Video preview cards use a VIDEO/name placeholder; extracted poster frames are future work. Animated image imports use a still frame.
- Picture/video slides are implemented. Motion backgrounds under edited lyrics, fades, automatic picture advance, audio-device routing, portable service export, storage reclamation, and backups remain future work.

Recommended next media milestone: a source-media editor to append/replace/remove and relink slides, followed by poster frames and dependable background layering/cut/fade. Keep the explicit song editor/Reflow/themes and production-rehearsal work on the broader roadmap.
