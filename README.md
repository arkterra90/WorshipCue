# WorshipCue

WorshipCue is a native C++17 / Qt 6 Widgets church presentation application with an offline SQLite library, picture/video presentations, batch naming, automatic slide advancement, and slide transitions.

## Run a service

1. Create songs with **+ New**, or edit an existing song. In the song editor, a line containing `---` separates slides. Save commits the song; Cancel discards the dialog changes.
2. Choose a saved service, or use **New** to create one. **Rename** changes its name; **Copy** makes a new plan from a previous service, including its order and slide arrangements.
3. Search the library by title or lyric text. Double-click a library song or press **Add →** to add it to the selected service. Repeated songs are separate service entries.
4. Drag service items into order. The arrow buttons and right-click menu also move items. **Remove** removes only that service entry; **Duplicate** inserts another occurrence after it. Library deletion requires confirmation and removes the song from every saved service.
5. Select a service item to preview its slides. In **Edit** mode, drag slide cards to arrange that occurrence of the song. The right-click menu also moves slides or resets the song order. Changes save immediately; reopening the app restores the selected service and its saved order.
6. Set **Audience** and **Stage** independently to a display or **Windowed Preview**. Their routing status stays visible above the workspace. Two fullscreen outputs cannot share one display.
7. In **Show** mode, click a slide to take it live. A green border and LIVE marker identify the live slide. Double-clicking a service item takes its selected/first slide live. In **Edit**, selecting a slide only previews it; double-click or **TAKE SELECTED SLIDE LIVE** presents it.
8. Use Next/Previous or keyboard navigation to run the service. The right side shows rendered audience content, the upcoming slide (including across song boundaries, or back to the first slide when looping), and the selected preview.

Switching modes, browsing another service, changing saved order, or saving song edits keeps the live output. Taking a slide live captures a new **copy of the entire service**. Next/Previous continue through that copy until you explicitly take another slide live. This also protects the running service when library songs are removed. At the beginning/end, navigation holds the first/last slide.

The live preview uses the same 16:9 renderer as audience output; non-16:9 windows letterbox the canvas. It shows audience content even when the physical output is Off; check the separate routing status to see whether a window is active.

## Import pictures and video

Use **Import Pictures / Videos…** in the sidebar or File menu. Select one or several files to create a reusable presentation and add it to the current service. Files initially follow natural filename order (`1`, `2`, `10`); use Edit mode to change that service occurrence’s order afterward.

The import dialog previews every filename before copying. Its Finder-style naming options are:

- **Name and Number:** `Sunday Announcements 01.jpg`, `Sunday Announcements 02.jpg`, with a configurable starting number and number of digits.
- **Replace Text:** replace part of each filename.
- **Add Text:** add a prefix or suffix before the extension.
- **Keep Current Names:** retain unique filenames.

Extensions stay intact. Duplicate names, reserved characters, and Windows-incompatible names are rejected. The originals remain untouched; WorshipCue copies files into its own media folder using the reviewed names. Copying and image decoding run in background workers, with import progress and Cancel. Browsing another service during import does not change its original destination.

Imported pictures appear as undimmed, aspect-fitted slide thumbnails and audience content. Imported videos show a VIDEO/name card until taken live. **Pause / Play Video**, **Restart**, and **Mute / Unmute** control the live clip; the operator’s live preview and audience share one playhead. Check **Loop videos** during import to repeat clips. Non-looping clips hold their final frame unless presentation auto-advance is enabled. Re-taking a clip restarts it.

Right-click a library presentation and choose **Batch Rename Media…**, or use the File menu, to rename its slide labels later. This later operation keeps managed storage paths stable so active playback and shared copies continue working. It changes names displayed in WorshipCue; it does not rename original source files or relocate already imported copies. **Edit** on a media presentation renames its title. Full source-media editing is future work.

Pictures use Qt’s installed image readers (typically JPEG, PNG, BMP, WebP, and others). Video import accepts MP4, MOV, M4V, WebM, AVI, and MKV containers; actual codec support depends on Qt Multimedia and the operating system. Invalid or missing media produces an operator error. Missing files are marked on slide cards. Animated image imports are still images. These are media slides; motion backgrounds behind edited lyrics are not included yet. See [media details and validation](docs/MILESTONE_0.3.md).

## Presentation timing and transitions

Select a service item and click **Timing & Transitions…** above its slides. The same dialog is available by right-clicking a library presentation or service item, and from the File menu for the selected library item. Settings save with the library presentation, apply to all its service entries, and are preserved when it is duplicated. Existing presentations default to manual advancement and instant cuts.

- **Automatically advance slides:** choose an interval from **0.1 to 3600 seconds**. The live monitor shows the remaining time and **Pause / Resume Auto-Advance**. Manual navigation restarts the interval; explicitly taking a slide live also resumes timing.
- **Loop back to the first slide:** repeat only this presentation. Otherwise, hold its last slide when the final interval expires. Automatic playback never enters the next service item; manual Next still does.
- **Transition:** choose **Cut**, **Fade**, **Slide Left**, or **Slide Right**, with an adjustable transition time from **0 to 10 seconds**. Zero seconds gives an instant cut. The incoming presentation supplies the transition settings.

The audience and operator live monitor share the same animation, including while the audience route is Off. Stage lyrics and the upcoming slide update immediately. Blackout, Clear Text, and Restore apply immediately; blackout and clear suspend auto-advance with the remaining time preserved. Restore resumes unless you also paused timing manually.

The interval starts at the cue and includes transition and media-loading time. Incoming picture/video transitions hold the outgoing visual until the first decoded frame is ready. Videos follow the configured slide interval, independently of their clip duration, loop flag, and Pause Video control. Use **Pause Auto-Advance** to hold a timed video slide. Saved setting changes affect the live snapshot only after you explicitly take a slide live again.

## Keyboard and output controls

| Key/control | Behavior |
|---|---|
| Right Arrow, Space, Page Down | Next live slide in Show mode; starts the selected slide if nothing is live |
| Left Arrow, Page Up | Previous live slide in Show mode |
| Escape | Return focus to operator slide controls; output stays fullscreen and live |
| Ctrl+B (default) | Toggle audience blackout |
| Ctrl+Shift+C (default) | Toggle audience clear text |
| Restore Audience | Remove blackout/clear without changing the slide |
| File → Keyboard Shortcuts | Configure the two audience shortcuts; preferences persist |

Navigation and output shortcuts do not intercept text fields, open dialogs, or combo-box editing. In Edit mode, arrows remain available for slide selection. Defaults use the Control key on macOS; the shortcut dialog also accepts Command combinations.

**Clear Text** hides audience text while keeping the background/picture/video and its audio. **Blackout** makes audience output fully black and mutes video audio while its playhead continues. **Restore Audience** restores normal audience content. These controls are mutually exclusive and affect **audience content only**; stage keeps current/next lyrics or media names and the clock. Taking or advancing a slide also restores normal audience content.

Routing preferences persist. Closing an output window sets its route to Off. A disconnected output is hidden and its desired display identity retained; reconnection attempts to restore it. Escape returns operator focus without changing the route. Identical monitor identities cannot yet be distinguished safely and are offered as one route. Validate this workflow with your actual display setup.

## Build

Install Qt **6.4 or newer**, including Widgets, SQL, the SQLite driver, **Multimedia**, **Concurrent**, CMake, and a compatible C++ compiler. Multimedia and Concurrent are standard Qt modules used for playback and background imports; no separate FFmpeg dependency is introduced. Open `CMakeLists.txt` in Qt Creator and use a matching macOS or Windows kit, or build from the command line.

macOS (replace the example prefix with your Qt installation):

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt" \
  -DWORSHIPCUE_BUILD_TESTS=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
open build/WorshipCue.app
```

Windows PowerShell with the matching MSVC Qt kit:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:\Qt\<version>\msvc2022_64" `
  -DWORSHIPCUE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\WorshipCue.exe
```

Qt Test is required when `WORSHIPCUE_BUILD_TESTS=ON`. Tests use temporary databases and isolated preferences. Most widget tests use Qt’s offscreen backend. On macOS, the operator and media-playback suites use native Cocoa windows in a logged-in desktop session because AVFoundation needs its event dispatcher to decode video. The nine suites cover migrations and rollback, persistence, service snapshots, timed advancement and pause/loop boundaries, transition rendering and interruption, routing, reordering, batch naming/import, real H.264 playback, and operator interactions. Windows commands are provided for development; Windows has **not** been tested in this milestone.

Use `macdeployqt` or `windeployqt` from the matching Qt installation to package runtime libraries and plugins. Signing, notarization, native installers, and distribution updates remain future work.

## Storage and compatibility

The default database is `library.sqlite3` under Qt's `QStandardPaths::AppDataLocation` for organization/application **WorshipCue**. On macOS this is normally `~/Library/Application Support/WorshipCue/WorshipCue/`. Geometry, shortcuts, selected service, output routes, and the still-background path use QSettings.

The old unversioned database migrates transactionally to schema **3** using `PRAGMA user_version`. Existing songs, slides, service-entry IDs, and order are preserved in **My Service**. Existing version-1 libraries gain media metadata columns; version-2 libraries gain presentation timing and transition settings with manual/Cut defaults. Each migration is transactional. Newer unknown schema versions are refused. Tables are `songs`, `slides`, `services`, and `service_entries`. Fresh libraries receive two sample items; an intentionally emptied library remains empty after restart.

A service entry references a library song and stores an optional order of source-slide indices. Repeated entries can have different orders, and copying a service copies those orders. Same-count lyric edits preserve indices. **Changing the number of slides resets that song's saved custom orders to natural order** because stable slide identities are not yet implemented. The currently running service copy is unaffected. Song editing requires Save; service construction and arrangements save after every successful operation.

Back up the database **and its sibling `media` directory** while the app is closed. Managed media references are relative to that directory, so both can move together. Duplicate presentations share managed files; deleting a presentation currently retains files to protect copies and active snapshots. Automatic backups, recovery of unsaved editor dialogs, service export/import, and portable media packaging are not implemented. Background images still use absolute paths; a missing image falls back to the default background with a warning. Live state is intentionally not restored on restart.

## Scope and next work

The current application includes a searchable content library, named saved services, picture/video import with batch naming, service and slide reordering, Show/Edit operation, shared audience-style previews, live/next monitoring, keyboard controls, and output recovery handling. The original separator-based song editor remains in place.

The recommended next milestone is explicit slide editing with undo/redo and preview, Reflow, section labels, stable slide identities, named arrangements, and reusable themes. Library collections, full media source editing, motion lyric backgrounds, countdown timers, Scripture generation, dedicated sermon presentations, external integrations, and installers remain unimplemented.

See [the roadmap](docs/ROADMAP.md) and [architecture](docs/ARCHITECTURE.md) for the inspection findings, boundaries, and remaining risks. Automated macOS checks do not replace rehearsal with real projectors, physical hot-plug, macOS Spaces, high DPI, and a Windows build before production use.

WorshipCue is a working project name and is not affiliated with Renewed Vision or ProPresenter. The included example text is original placeholder content.
