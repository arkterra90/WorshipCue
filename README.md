# WorshipCue

WorshipCue is a native C++17 / Qt 6 Widgets church presentation application with an offline SQLite library, picture/video presentations, batch naming, automatic slide advancement, and slide transitions.

## Run a service

1. Use **+ New** to create slides in the rich text editor, or **Import Song Lyrics…** for text/ChordPro import. Use **Edit** in the library or **Edit Slide Text…** above the slide workspace to edit an existing presentation. Save commits the draft; Cancel discards it.
2. Choose a saved service, or use **New** to create one. **Rename** changes its name; **Copy** makes a new plan from a previous service, including its order and slide arrangements.
3. Search the library by title or lyric text. Double-click a library song or press **Add →** to add it to the selected service. Repeated songs are separate service entries.
4. Drag service items into order. The arrow buttons and right-click menu also move items. **Remove** removes only that service entry; **Duplicate** inserts another occurrence after it. Library deletion requires confirmation and removes the song from every saved service.
5. Select a service item to preview its slides. In **Edit** mode, drag slide cards to arrange that occurrence of the song. The right-click menu also moves slides or resets the song order. Changes save immediately; reopening the app restores the selected service and its saved order.
6. Set **Audience** and **Stage** independently to a display or **Windowed Preview**. Their routing status stays visible above the workspace. Two fullscreen outputs cannot share one display.
7. In **Show** mode, click a slide to take it live. A green border and LIVE marker identify the live slide. Double-clicking a service item takes its selected/first slide live. In **Edit**, selecting a slide previews it and double-clicking opens its rich text editor. **TAKE SELECTED SLIDE LIVE** presents it explicitly.
8. Use Next/Previous or keyboard navigation to run the service. The right side shows rendered audience content, with **Up Next / Selected** tabs underneath. Up Next includes slides across song boundaries, or the first slide when looping.

Switching modes, browsing another service, changing saved order, or saving song edits keeps the live output. Taking a slide live captures a new **copy of the entire service**. Next/Previous continue through that copy until you explicitly take another slide live. This also protects the running service when library songs are removed. At the beginning/end, navigation holds the first/last slide.

The live preview uses the same 16:9 renderer as audience output; non-16:9 windows letterbox the canvas. It shows audience content even when the physical output is Off; check the separate routing status to see whether a window is active.

Choose **Grid** for audience thumbnails or **Text** for readable, wrapped lyrics. **Slide size** adjusts the thumbnails or text size for the operator. Drag the dividers between the three columns or between the library and service playlist to allocate space. View, size, divider positions, and preview tab are remembered when you close the app.

Use **Command+F** on macOS or **Ctrl+F** on Windows to focus the library search. **Return to Live** reveals the saved service and cue corresponding to the running copy without cueing it again. If that saved content has changed or been removed, WorshipCue reports the difference and keeps the live output. Audience controls remain directly beneath the live preview; timing and video controls appear when applicable.

See [the ProPresenter UI audit](docs/PROPRESENTER_UI_AUDIT.md) for the comparison, applied changes, and deferred workflows.

## Edit slide text

Select a slide and click **Edit Slide Text…**, right-click it and choose the same action, or double-click it in **Edit** mode. Library **Edit** opens the whole presentation. The slide navigator selects which source slide you are editing; the central 16:9 canvas lets you click directly into the text.

Use the formatting toolbar for font family, font size, bold, italic, underline, text color, and paragraph alignment. Select words to format those words; with no selection, formatting applies to the whole slide. Undo/Redo works separately for each slide as you move through the navigator. Text shrinks proportionally when necessary to fit the audience text area. Thumbnail, selected/next preview, and audience output use the same formatting; stage keeps plain lyrics for readability.

Save preserves slide IDs, media, backgrounds, and service arrangements. Repeated occurrences of a source slide receive the same saved edit. The currently live copy keeps its original text and formatting until you explicitly take a slide live. Cancel discards all draft changes. New presentations can add, copy, and remove draft slides; existing presentations keep their slide structure while editing text. Media presentations can edit text overlays without replacing imported files.

See [the editor research and implementation notes](docs/SLIDE_EDITOR_AND_GLOBAL_TRANSITIONS.md).

## Import song lyrics

Choose **Import Song Lyrics…** in the sidebar or File menu to open a UTF-8 text or ChordPro file. **Paste Lyrics** in the song editor imports clipboard text. Blank lines, `---` separators, and section headings such as `[Verse 1]` or `Chorus:` create slide breaks. Chord annotations are removed, and ChordPro title metadata supplies the title when present. Imported slides remain editable before Save; import does not commit the song automatically.

## Presentation backgrounds

Select a library presentation and open **Background Library…**, or right-click a library presentation or service item and choose **Assign Background…**. Use **Import Backgrounds…** to copy still images into the reusable library, select a thumbnail, then **Assign Background**. Choose **Default background** to restore the built-in background for that presentation.

Assignments save with the library presentation, apply to all its service entries, and are copied when the presentation is duplicated. Take a slide live to apply a changed assignment. The background forms a layer beneath lyrics and media. Pictures and video remain undimmed and aspect-fitted above it: their unused edges reveal the background, while a full-canvas JPEG covers it. Clear Text retains these visual layers.

## Import pictures and video

Use **Import Pictures / Videos…** in the sidebar or File menu. Select one or several files to create a reusable presentation and add it to the current service. Files initially follow natural filename order (`1`, `2`, `10`); use Edit mode to change that service occurrence’s order afterward.

The import dialog previews every filename before copying. Its Finder-style naming options are:

- **Name and Number:** `Sunday Announcements 01.jpg`, `Sunday Announcements 02.jpg`, with a configurable starting number and number of digits.
- **Replace Text:** replace part of each filename.
- **Add Text:** add a prefix or suffix before the extension.
- **Keep Current Names:** retain unique filenames.

Extensions stay intact. Duplicate names, reserved characters, and Windows-incompatible names are rejected. The originals remain untouched; WorshipCue copies files into its own media folder using the reviewed names. Copying and image decoding run in background workers, with import progress and Cancel. Browsing another service during import does not change its original destination.

Imported pictures appear as undimmed, aspect-fitted slide thumbnails and audience content. Imported videos show a VIDEO/name card until taken live. **Pause / Play Video**, **Restart**, and **Mute / Unmute** control the live clip; the operator’s live preview and audience share one playhead. Check **Loop videos** during import to repeat clips. Non-looping clips hold their final frame unless presentation auto-advance is enabled. Re-taking a clip restarts it.

Right-click a library presentation and choose **Batch Rename Media…**, or use the File menu, to rename its slide labels later. This later operation keeps managed storage paths stable so active playback and shared copies continue working. It changes names displayed in WorshipCue; it does not rename original source files or relocate already imported copies. **Edit** on a media presentation opens its title and text overlays in the rich text slide editor. Full source-media editing is future work.

Pictures use Qt’s installed image readers (typically JPEG, PNG, BMP, WebP, and others). Video import accepts MP4, MOV, M4V, WebM, AVI, and MKV containers; actual codec support depends on Qt Multimedia and the operating system. Invalid or missing media produces an operator error. Missing files are marked on slide cards. Animated image imports are still images. These are media slides; motion backgrounds behind edited lyrics are not included yet. See [media details and validation](docs/MILESTONE_0.3.md).

## Presentation timing and transitions

Use the always-visible **Global transition** controls below the slides, or **Settings → Global Slide Transitions…** (also available in File). Choose **Cut**, **Fade**, **Slide Left**, or **Slide Right**, with a duration from **0 to 10 seconds**. These defaults apply to every presentation that does not have an override; zero seconds gives an instant cut.

Click **Timing & Transitions…** above the slides, or right-click a library presentation or service item and choose it, to configure its slide show. The dialog is also in the File menu for the selected library item. Settings save with the library presentation, apply to all its service entries, and are preserved when it is duplicated. New presentations default to manual advancement and inherit the global transition.

- **Automatically advance slides:** choose an interval from **0.1 to 3600 seconds**. The live monitor shows the remaining time and **Pause / Resume Auto-Advance**. Manual navigation restarts the interval; explicitly taking a slide live also resumes timing.
- **Loop back to the first slide:** repeat only this presentation. Otherwise, hold its last slide when the final interval expires. Automatic playback never enters the next service item; manual Next still does.
- **Override global transition:** enable this to give the presentation its own transition type and duration. Its override takes priority over the global defaults. The incoming presentation supplies the resolved transition settings.

The audience and operator live monitor share the same animation, including while the audience route is Off. Stage lyrics and the upcoming slide update immediately. Blackout, Clear Text, and Restore apply immediately; blackout and clear suspend auto-advance with the remaining time preserved. Restore resumes unless you also paused timing manually.

The interval starts at the cue and includes transition and media-loading time. Incoming picture/video transitions hold the outgoing visual until the first decoded frame is ready. Videos follow the configured slide interval, independently of their clip duration, loop flag, and Pause Video control. Use **Pause Auto-Advance** to hold a timed video slide. Presentation-specific changes apply after you explicitly take a slide live again. Global type/time changes apply to the next inherited cue, including in a running service, while leaving the current animation, timer, and video uninterrupted.

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

Qt Test is required when `WORSHIPCUE_BUILD_TESTS=ON`. Tests use temporary databases and isolated preferences. Most widget tests use Qt’s offscreen backend. On macOS, the operator and media-playback suites use native Cocoa windows in a logged-in desktop session because AVFoundation needs its event dispatcher to decode video. The eleven suites cover migrations and rollback, persistence, service snapshots, timed advancement and pause/loop boundaries, transition defaults and overrides, background layering, rich text editing/rendering and undo, lyric import, routing, reordering, batch naming/import, real H.264 playback, and operator interactions. Windows commands are provided for development; Windows has **not** been tested in this milestone.

Use `macdeployqt` or `windeployqt` from the matching Qt installation to package runtime libraries and plugins. Signing, notarization, native installers, and distribution updates remain future work.

## Storage and compatibility

The default database is `library.sqlite3` under Qt's `QStandardPaths::AppDataLocation` for organization/application **WorshipCue**. On macOS this is normally `~/Library/Application Support/WorshipCue/WorshipCue/`. Geometry, shortcuts, selected service, output routes, and global transition defaults use QSettings. Background assignments are stored in SQLite; the former global background preference is no longer loaded.

The old unversioned database migrates transactionally to schema **5** using `PRAGMA user_version`. Existing songs, slides, service-entry IDs, and order are preserved in **My Service**. Version-1 libraries gain media metadata; version-2 libraries gain timing and transitions; version-3 libraries gain transition overrides and background assignments; version-4 libraries gain per-slide rich text with empty defaults so existing slides retain their appearance. Customized legacy transitions are preserved as overrides when their type is not Cut or their duration is not 500 ms; the previous Cut/500 ms defaults inherit global settings. Each migration is transactional. Newer unknown schema versions are refused. Tables are `songs`, `slides`, `services`, and `service_entries`. Fresh libraries receive two sample items; an intentionally emptied library remains empty after restart.

A service entry references a library song and stores an optional order of source-slide indices. Repeated entries can have different orders, and copying a service copies those orders. Same-count lyric edits preserve indices. **Changing the number of slides resets that song's saved custom orders to natural order** because stable slide identities are not yet implemented. The currently running service copy is unaffected. Song editing requires Save; service construction and arrangements save after every successful operation.

Back up the database **and its sibling `media` and `backgrounds` directories** while the app is closed. Managed paths are relative to the database directory, so these files can move together. Duplicate presentations share managed files; deleting a presentation currently retains files to protect copies and active snapshots. Automatic backups, recovery of unsaved editor dialogs, service export/import, and portable media packaging are not implemented. A missing background falls back to the built-in background and is identified in the operator UI. Live state is intentionally not restored on restart.

## Scope and next work

The current application includes a searchable content library, lyric import/paste, named saved services, picture/video import with batch naming, presentation backgrounds, global transition defaults with presentation overrides, presentation auto-advance/looping, service and slide reordering, Show/Edit operation, shared audience-style previews, live/next monitoring, keyboard controls, and output recovery handling. The rich text slide editor supports direct canvas editing, formatting, per-slide undo/redo, and preview. The bulk lyrics importer retains editable `---` separators.

The next editor work includes Reflow, section labels, stable slide identities, named arrangements, reusable themes, and freeform multi-object slide layouts. Library collections, full media source editing, motion lyric backgrounds, countdown timers, Scripture generation, dedicated sermon presentations, external integrations, and installers remain unimplemented.

See [the roadmap](docs/ROADMAP.md) and [architecture](docs/ARCHITECTURE.md) for the inspection findings, boundaries, and remaining risks. Automated macOS checks do not replace rehearsal with real projectors, physical hot-plug, macOS Spaces, high DPI, and a Windows build before production use.

WorshipCue is a working project name and is not affiliated with Renewed Vision or ProPresenter. The included example text is original placeholder content.
