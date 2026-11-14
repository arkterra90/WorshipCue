# WorshipCue

WorshipCue is a native C++17 / Qt 6 Widgets church presentation application with an offline SQLite library, structured songs, reusable arrangements, standalone media assets, batch naming, automatic slide advancement, slide transitions, and MIDI control.

## Create service orders with AI

Open **Settings → AI / MCP Setup…** to connect an AI client to WorshipCue's local MCP server. The client can create song and sermon presentations, search your library, inspect past services, and preview/save new service orders. With Planning Center credentials configured, it can read a Services plan and import its order, matching library songs or creating slides from available arrangement lyrics. New services appear in the app automatically while live output continues unchanged. The normal app build includes the `WorshipCueMcp` companion; no model API key is required in WorshipCue.

See [MCP setup, example prompts, and tool details](docs/MCP.md).

## Run a service

1. Use **+ New** to create a song with reusable sections and arrangements, or **Import Song Lyrics…** for text/ChordPro import. Use **Edit** in the library or **Edit Slide Text…** above the slide workspace to edit an existing presentation. Save commits the draft; Cancel discards it.
2. Choose a saved service, or use **New** to create one. **Rename** changes its name; **Copy** makes a new plan from a previous service, including its order and slide arrangements.
3. Search the library by title or lyric text. Double-click a library song or press **Add →** to add it to the selected service. Repeated songs are separate service entries.
4. Drag service items into order. The arrow buttons and right-click menu also move items. **Remove** removes only that service entry; **Duplicate** inserts another occurrence after it. Library deletion requires confirmation and removes the song from every saved service.
5. Select a service item to preview its slides. In **Edit** mode, drag slide cards to arrange that occurrence of the song. The right-click menu also moves slides or resets the song order. Changes save immediately; reopening the app restores the selected service and its saved order.
6. Open **Screens…** to configure **Audience Screen 1/2** and **Stage Screen 1/2** independently. Assign a display or **Windowed Preview** and save each output. The toolbar retains quick routes for the first audience/stage pair. Fullscreen outputs cannot share one physical display.
7. In **Show** mode, click a slide to take it live. A green border and LIVE marker identify the live slide. Double-clicking a service item takes its selected/first slide live. In **Edit**, selecting a slide previews it and double-clicking opens its rich text editor. **TAKE SELECTED SLIDE LIVE** presents it explicitly.
8. Use Next/Previous or keyboard navigation to run the service. The right side shows rendered audience content, with **Up Next / Selected** tabs underneath. Up Next includes slides across song boundaries, or the first slide when looping.

Switching modes, browsing another service, and changing saved order preserve the running service snapshot. Shared text edits to structured songs repaint every live occurrence immediately. Taking a slide live captures a new **copy of the entire service**. Next/Previous continue through that copy until you explicitly take another slide live. This also protects the running service when library songs are removed. At the beginning/end, navigation holds the first/last slide.

The live monitor follows Audience Screen 1’s output format. Both audience outputs share presentation content and transitions while applying their own canvas, aspect, rotation, and scaling. It shows audience content even when the physical output is Off; check the separate routing status to see whether a window is active.

Choose **Grid** for audience thumbnails or **Text** for readable, wrapped lyrics. **Slide size** adjusts the thumbnails or text size for the operator. Choose **Content Library**, **Service Playlist**, or **Media Library** at the top of the left sidebar; the selected view uses its full height. The library shows which saved service receives added items. Drag the dividers between the three columns to allocate space. View, size, column widths, sidebar choice, and preview tab are remembered when you close the app.

Use **Command+F** on macOS or **Ctrl+F** on Windows to open Content Library and focus its search. **Return to Live** reveals the saved service and cue corresponding to the running copy without cueing it again. If that saved content has changed or been removed, WorshipCue reports the difference and keeps the live output. Audience controls remain directly beneath the live preview; timing and video controls appear when applicable.

See [the ProPresenter UI audit](docs/PROPRESENTER_UI_AUDIT.md) for the comparison, applied changes, and deferred workflows.

## MIDI control

Open **Settings → MIDI Devices / Mappings…**. MIDI starts disabled. In **Devices**, enable MIDI, check the input sources/output destinations, choose channels, and press **Apply Device Settings**. Input **Any** accepts all channels; an output's channel is the default for new commands. Existing commands retain their own channel. **Refresh Devices** discovers new ports; automatic reconnect retries saved identities every second. Missing devices remain saved. **Activity** shows the latest 500 receive/transmit, connection, mapping, and ignored-input events.

In **Input Mappings**, add a named mapping for Note On, Note Off, or Control Change, with source, channel, number, and an exact or **Any** value. **Learn next input message** fills those fields from an enabled input. Choose Next/Previous, Blackout, Clear Text, Restore, select/take a presentation, take a source page, pause/resume auto-advance or media, restart/mute media, or release WorshipCue's notes. Select preview changes no live cue; take presentation starts at the first page of its saved arrangement. For repeated pages, **Occurrence** selects that page's first, second, or later appearance in the chosen library/service arrangement. Service targets belong to that specific saved service item. Overlapping enabled triggers are rejected; disabled mappings may overlap. Mapping changes save immediately in the mapping editor.

Right-click a slide and choose **MIDI Actions / Input Trigger…**, or use **Slide MIDI…** above the workspace. Add multiple ordered output commands, each with an enabled flag, destination, channel 1–16, type, number 0–127, and velocity/value 0–127 (Note On requires 1–127). **Assign Input Trigger…** opens a mapping prefilled for that source page and its selected service occurrence. Slide output commands save together with **Save**. An input mapping saved in its own editor remains saved if the output editor is canceled.

Output commands fire once on each explicit take or actual navigation/timed advance. Retaking a page fires them again. Repainting, text edits, Blackout, Clear, Restore, and opening settings do not resend them. Live navigation retains the captured command lists until the next take. Disconnected/disabled commands are skipped without later replay. Source-page assignments survive reordering and repeated arrangements; after reflow only the first fragment retains them. New/duplicated source pages and copied presentations start without MIDI assignments. Direct media service items run the asset's default commands followed by that item's commands; copying those service items preserves their extra output commands. Input mappings remain attached to their original targets. Deleted target mappings are removed automatically.

Input control runs in **Show** mode with dialogs and popup menus closed. Holding a note or repeating an unchanged CC value fires once; release/change rearms it. An **Any value** CC mapping responds to each changed value, including zero. The default 100 ms per-mapping interval suppresses bouncing, and a 250 ms echo window suppresses returned output messages with the same type/channel/number (Note On/Off share the note key; values are ignored). Both windows are adjustable, including 0 to disable. Use distinct input/output numbers or channels when routing between apps. Input learning and monitoring continue while dialogs suspend control. **Release WorshipCue Notes**, disabling a destination/MIDI, and shutdown send Note Off for notes sent by WorshipCue when their destination is still available.

macOS uses CoreMIDI: hardware, IAC, and configured network-session endpoints appear in Devices. Optionally publish **WorshipCue Input / Output** virtual ports. Other apps send to WorshipCue Input and subscribe to WorshipCue Output. On Windows, WinMM exposes hardware and OS-installed virtual/network ports; configure loopMIDI or rtpMIDI separately. Ambiguous duplicate driver identities are disabled rather than selected by enumeration index. Supported messages are MIDI 1.0 Note On/Off and CC; SysEx, MIDI Clock commands, and MIDI 2.0 are not mapped.

The source/destination, central-map, and slide-action workflow follows the official [ProPresenter MIDI setup reference](https://support.renewedvision.com/hc/en-us/articles/1500000020301-Devices-MIDI-ProPresenter-Setup). See its [system MIDI setup guide](https://support.renewedvision.com/hc/en-us/articles/4412376777491-Devices-MIDI-System-Setup) for OS virtual/network routing. Tests verify an isolated native CoreMIDI input/output round trip; Windows runtime, physical controllers, and network sessions still require platform/hardware checks.

## Library cleanup and Recovery

Open **Settings → Library Cleanup / Storage…**. Enter **1–36500 days** and choose **Find Content**. The scan runs in the background. It lists presentations, media assets, and unreferenced files inside the library's managed `media` / `backgrounds` folders. **Show protected / unknown-age items** explains exclusions. Check individual eligible items or **Select All Eligible**; the summary estimates recoverable file storage for the selection.

Creation, import, modification, and last-used dates are separate. Explicit live takes, slide navigation (including timed advance), and actual media preview record use. Browsing or selecting a library row does not. Taking one item does not mark the rest of its service as used. Media used by a presentation updates its shared asset/file history, and live backgrounds record file use. Text/media edits and MIDI assignment changes update modification history. Old libraries retain unknown historical dates, including unknown last-used dates; missing history never qualifies content for age-based cleanup. Recently created, imported, edited, or used content and recently modified physical files stay protected.

Saved services protect their referenced presentations and direct media. Presentation references protect media assets. Every item/file in a live service snapshot is protected, including upcoming items and content removed from the saved library. Other open WorshipCue windows publish the same protection; closing a window releases it. A running process remains protected if its heartbeat is delayed. Saved Looks and other presentations/assets protect shared background/media paths. Symlinks and paths outside managed folders cannot be cleaned up. Scans are bounded to 50000 content items/managed files and report an error rather than returning a partial selection when the limit is exceeded.

Choose **Review Move to Recovery…** to inspect the selected records and file-by-file disposition. **Move to Recovery** hides them from the library while keeping their original IDs, lyrics, arrangements, Looks, MIDI assignments, and files. **This step frees no disk space.** Nothing is moved until you confirm. Cancel leaves the selection intact. In **Recovery**, select a batch and **Restore Selected Batch** to bring it back.

To free space, select a Recovery batch and choose **Review Permanent Removal…**. Review the records, removable files, retained-file reasons, and estimate, then explicitly choose **Permanently Delete**. This step cannot be undone. Shared paths are counted once, and files still referenced by another active or Recovery item or a saved Look are retained. A presentation's files may remain registered in the Media Library; permanently remove the unused presentation first, then rescan to review its now-unreferenced assets. Estimates exclude database space, directories, and filesystem allocation/compression differences.

References, live protections, file sizes/timestamps, and changed usage are checked again before removal. If an archived item/file is used or changed again, restore it and run a fresh age scan. An interrupted removal restores staged files when its database changes did not commit; after a confirmed removal commits, only its recorded files are eligible for completion. **Refresh / Retry Pending Removal** retries already-confirmed file removals that could not finish, such as a file blocked by permissions. Ordinary library Delete operations retain managed files; this cleanup workflow provides reviewed disk reclamation.

## Edit slide text

Select a slide and click **Edit Slide Text…**, right-click it and choose the same action, or double-click it in **Edit** mode. Library **Edit** opens the whole presentation. The slide navigator selects which source slide you are editing; the central 16:9 canvas lets you click directly into the text.

Use the formatting toolbar for font family, font size, bold, italic, underline, text color, and paragraph alignment. Select words to format those words; with no selection, formatting applies to the whole slide. Undo/Redo works separately for each slide as you move through the navigator. Text shrinks proportionally when necessary to fit the audience text area. Thumbnail, selected/next preview, and audience output use the same formatting; stage keeps plain lyrics for readability.

Save preserves slide IDs, media, backgrounds, and service arrangements. Repeated occurrences of a source slide receive the same saved edit. Structured song edits update all arranged occurrences, including the active audience cue. Other presentation edits appear on the next explicit take. Cancel discards all draft changes. New presentations can add, copy, and remove draft slides; existing presentations keep their slide structure while editing text. Media presentations can edit text overlays without replacing imported files.

See [the editor research and implementation notes](docs/SLIDE_EDITOR_AND_GLOBAL_TRANSITIONS.md).

## Looks and text reflow

Select a presentation slide and open **Looks / Reflow…** above the workspace or in the slide context menu. The left list contains unique source pages. Repeated song sections share those pages and their formatting. Ctrl/Command-click or Shift-click to select multiple source pages.

**Capture Slide Formatting** starts from the selected source page's first visible text format. Configure the font, pixel size, weight, italic/underline/strike-out, text color, horizontal and vertical alignment, margins, and padding. The **Layout / Background** tab offers the presentation background, a solid color, or an image from the Background Library. Margins position the text box within the 1920×1080 canvas. **Keep slide text formatting overrides** retains existing rich text styling with the Look's layout and background.

Choose **Save as Look…** and name the Look for reuse. Saved Looks appear in the selector for other presentations. Choose **Apply to Selected Slides** or **Apply to Presentation**. Applying to the presentation establishes its shared Look and resets its source-page Look overrides. Applying to selected pages assigns explicit page-level references. Creating another named Look and applying it changes presentation formatting while preserving lyric/sermon text. Saved Look definitions are retained when the presentation dialog is canceled.

The preview uses the same text layout and renderer as audience output. When a Look exceeds a text box, applying it opens **Text Overflow**. Choose **Preview Automatic Split…** to review the resulting pages before **Apply Split**, choose **Fit Text** to scale within the box, or cancel. Splitting uses rendered line and sentence boundaries, preserves every source character in reading order, retains section/arrangement associations, and reuses media references. Each split page with video is a separate cue of that video.

**Undo Look / Split** and **Redo Look / Split** restore complete draft operations. Save commits the draft atomically. Cancel discards presentation changes. Explicit takes capture saved Look/reflow changes for live output. Text editing in the canvas can create rich text overrides for styled pages. If a later lyric edit overflows, the editor and operator show an overflow warning and the renderer uses safety fitting. Open Looks / Reflow to review an explicit split or fit.

## Song sections and arrangements

**+ New** opens the section editor. For an existing text presentation, select it and choose **Song Sections / Arrangements…** above the workspace or in the Content Library context menu. Existing flat presentations become one named section per source slide when opened here. Saving commits the conversion.

The left list contains unique sections. **Add** creates a section. Select a suggested name such as Verse 1, Chorus, Pre-Chorus, Bridge, Intro, Outro, Tag, Refrain, or Instrumental, or type a custom name. The middle column lists its individual slides as **Verse 1 • Slide 1**, **Verse 1 • Slide 2**, etc. Select a slide to edit its lyrics. **Add Slide** inserts a blank slide after the selection; **Duplicate** copies it with its formatting. **Split at Cursor** (Option/Alt+Return) divides its lyrics into two slides. Drag slides or use ↑/↓ to change their order within the section. **Remove** confirms deletion of a selected slide; each section retains at least one slide. Blank slides are retained. Drag sections to reorder their source groups. Section names must be unique within the song.

Select a section and use **Add Section →** to append a reference to the selected arrangement. Repeating that action adds another occurrence of the same section. Drag the arrangement list to change playback order. Its **Remove** button removes one occurrence. Every occurrence contains all the section’s slides in the same order. The preview labels each slide and updates as its section is edited. The section list's **Remove** button confirms removal from all draft arrangements.

**New**, **Copy**, **Rename**, and **Delete** beside the saved arrangement selector manage named arrangements. Choose an arrangement and save to make it the song's playback order. Default service entries use that saved arrangement. Service-specific slide reordering continues to provide an independent override. Resetting a service item's slide order returns to the saved song arrangement.

**Edit Slide Text…** edits the unique source page for any selected occurrence, including rich text formatting. Saving either editor updates every corresponding occurrence across saved arrangements and active live content. Section-lyrics edits preserve rich formatting on unchanged pages and clear formatting on changed or manually split pages. Reordering retains slide identities and formatting. Cancel discards the draft. Shared text updates preserve cue position, timer state, and media playback. An explicit take captures structural or arrangement-order changes for live navigation.

Complete section occurrences in saved custom service orders follow additions, removals, and internal reordering together. Partial occurrences keep their specific slide identities. Removing a section or a slide explicitly used by a partial custom order requires resetting that order first. A failed save rolls back the entire change. Duplicate presentations have independent section sources.

## Import song lyrics

Choose **Import Song Lyrics…** in the sidebar or File menu to open a UTF-8 text or ChordPro file. **Paste Lyrics** in the song editor imports clipboard text. Blank lines, `---` separators, and section headings such as `[Verse 1]` or `Chorus:` create slide breaks. Chord annotations are removed, and ChordPro title metadata supplies the title when present. Imported slides remain editable before Save. Matching repeated headings and lyrics share one section source when saved. Multiple pages under one heading form one section. Different lyrics under the same heading receive distinct section names. Editing the bulk import text rebuilds sections from the revised slide breaks. Import commits when you save.

## Presentation backgrounds

WorshipCue includes **20 original modern backgrounds** in 4K (3840 × 2160), ready to use offline. The collection combines soft auroras, flowing ribbons, subtle contours, and minimal geometric designs with space for readable lyrics. [Preview the collection](docs/backgrounds-preview.jpg).

Select a library presentation and open **Background Library…**, or right-click a library presentation or service item and choose **Assign Background…**. Select an included background, then **Assign Background**. Use **Import Backgrounds…** to copy your own still images into the same reusable library. Choose **Default background** to restore the app's original gradient for that presentation.

Assignments save with the library presentation, apply to all its service entries, and are copied when the presentation is duplicated. Take a slide live to apply a changed assignment. The background forms a layer beneath lyrics and media. Pictures and video remain undimmed and aspect-fitted above it: their unused edges reveal the background, while a full-canvas JPEG covers it. Clear Text retains these visual layers.

## Media Library

The sidebar has **Content Library**, **Service Playlist**, and **Media Library** tabs. **Import Media…** imports individual images, videos, and audio into managed storage. Review filenames and optional looping, then choose **Import Media**. Select **Also create a slide presentation from this batch** when the files should form one presentation. **Add imported items to the current service** uses the service selected when the import starts.

In **Media Library**, search names or folders and select an item to see its type, size, image dimensions, file modification date, import date, and managed path. Right-click or use **Rename / Organize…** to change its display name, folder label, or audio/video looping. Choose an existing folder label or type a new one. Display-name changes keep the stored path stable. Legacy media has an unknown import date.

**Preview…** opens a local preview. Audio and video require **Play Preview**. Close the dialog to stop preview playback. Select an asset and use **TAKE SELECTED SLIDE LIVE**, or right-click **Take Live**, to trigger it directly. **Add to Service** and dragging an asset onto the **Service Playlist** tab create shared references. Drop on an item to insert before it, or below the list to append. Copies of services and media entries share the same asset and stored file.

Right-click **Delete from Media Library…** to review and confirm removal. Presentations and saved services protect their referenced assets. Removal retains the managed file for recovery and does not reclaim disk space. Existing media presentations remain in Content Library and also expose their files in Media Library. Their slide labels remain presentation-specific.

Batch presentations retain **Batch Rename Media…** in the File and context menus. Name and Number, Replace Text, Add Text, and Keep Current Names offer a preview and preserve extensions. Original source files remain untouched. Importing runs in a background worker with progress and cancellation.

Image support follows Qt's installed image readers. Video containers include MP4, MOV, M4V, WebM, AVI, and MKV. Audio extensions include MP3, WAV, M4A, AAC, FLAC, OGG, AIFF, and AIF. Decoding depends on Qt Multimedia and the operating system. Pause/play, restart, mute, and looping use the same live transport for audio and video. Missing or invalid files produce operator errors. Animated images import as stills.

## Presentation timing and transitions

Use the always-visible **Global transition** controls below the slides, or **Settings → Global Slide Transitions…** (also available in File). Choose **Cut**, **Fade**, **Slide Left**, or **Slide Right**, with a duration from **0 to 10 seconds**. These defaults apply to every presentation that does not have an override; zero seconds gives an instant cut.

Click **Timing & Transitions…** above the slides, or right-click a library presentation or service item and choose it, to configure its slide show. The dialog is also in the File menu for the selected library item. Settings save with the library presentation, apply to all its service entries, and are preserved when it is duplicated. New presentations default to manual advancement and inherit the global transition.

- **Automatically advance slides:** choose an interval from **0.1 to 3600 seconds**. Interval and loop controls stay editable; changing the interval or enabling looping turns on auto-advance. Uncheck auto-advance to return to manual slides. The live monitor shows the remaining time and **Pause / Resume Auto-Advance**. Manual navigation restarts the interval; explicitly taking a slide live also resumes timing.
- **Loop back to the first slide:** repeat only this presentation. Otherwise, hold its last slide when the final interval expires. Automatic playback never enters the next service item; manual Next still does.
- **Override global transition:** edit type or duration directly to create a presentation override, or check this box explicitly. Uncheck it to restore and display the global settings. Both value fields stay editable; Cut remains instant regardless of duration. Its override takes priority over the global defaults. The incoming presentation supplies the resolved transition settings.

The audience and operator live monitor share the same animation, including while the audience route is Off. Stage lyrics and the upcoming slide update immediately. Blackout, Clear Text, and Restore animate using the active presentation’s explicit transition override or the latest global default. They preserve the current slide, service position, and video playhead. Blackout and clear suspend auto-advance immediately with the remaining time preserved; blackout also mutes audio immediately. Restore resumes unless you also paused timing manually.

The interval starts at the cue and includes transition and media-loading time. Incoming picture/video transitions hold the outgoing visual until the first decoded frame is ready. Videos follow the configured slide interval, independently of their clip duration, loop flag, and Pause Video control. Use **Pause Auto-Advance** to hold a timed video slide. Presentation-specific changes apply after you explicitly take a slide live again. Global type/time changes apply to the next inherited cue or Blackout/Clear Text/Restore operation, including in a running service, while leaving the current animation, timer, and video uninterrupted.

In **Service Playlist**, use Shift-click for a range, Command-click (macOS) / Ctrl-click (Windows) for separate items, or **Edit → Select All Service Items** (platform Select All shortcut while the playlist has focus). Choose **Transitions for Selection…** from the button or context menu. Set an override with Cut/Fade/Slide Left/Slide Right and a duration, or uncheck override to restore inheritance. A mixed selection starts with its first item's settings. Selecting one item provides an individual override after a bulk edit.

Bulk edits change only transition type, duration, and inheritance. Shared library presentations change across all their service entries; standalone media overrides belong to each selected service item. Timing, lyrics, Looks, backgrounds, arrangements, and media remain intact. **Edit → Undo Bulk Transitions / Redo Bulk Transitions** also appear in the playlist context menu; platform Undo/Redo shortcuts work while the playlist has focus. History lasts for this application session. Undo preserves later timing/appearance changes and refuses the entire operation if a target was removed or its transitions changed afterward. Saved edits take effect on the next explicit take, preserving the running snapshot. Dragging several selected entries moves them together in service order.

## Keyboard and output controls

| Key/control | Behavior |
|---|---|
| Right Arrow, Down Arrow, Space, Page Down | Next live slide in Show mode; starts the selected slide if nothing is live |
| Left Arrow, Up Arrow, Page Up | Previous live slide in Show mode |
| Escape | Return focus to operator slide controls; output stays fullscreen and live |
| Ctrl+B (default) | Toggle audience blackout |
| Ctrl+Shift+C (default) | Toggle audience clear text |
| Restore Audience | Remove blackout/clear without changing the slide |
| File → Keyboard Shortcuts | Configure the two audience shortcuts; preferences persist |

Navigation and output shortcuts do not intercept text fields, open dialogs, or combo-box editing. In Edit mode, arrows remain available for slide selection. Defaults use the Control key on macOS; the shortcut dialog also accepts Command combinations.

**Clear Text** hides audience text while keeping the background/picture/video and its audio. **Blackout** makes audience output fully black and mutes video audio while its playhead continues. **Restore Audience** restores normal audience content. These controls are mutually exclusive and affect **audience content only**; stage keeps current/next lyrics or media names and the clock. Taking or advancing a slide also restores normal audience content.

Routing preferences persist. Closing an output window sets its route to Off. A disconnected output is hidden and its desired display identity retained; reconnection attempts to restore it. Escape returns operator focus without changing the route. Ambiguous identical monitor identities are withheld from fullscreen assignment. Stable unique serial identities can recover a display even when its OS name changes. Validate this workflow with your actual display setup.

## Screens and stage layouts

**Screens…** or **Settings → Screens / Output Settings…** has a tab for each logical output. Choose **Off**, **Windowed Preview**, or an available fullscreen display. **Save This Output** commits only that tab; closing discards unsaved preview edits. Routes, dimensions, aspect, rotation, scaling, and stage-layout assignments persist. Closing an output window saves Off for that output. Display disconnects hide the affected window while retaining its assignment for reconnection.

Each output supports a logical canvas from 160×90 through 8192×8192 (up to 32 megapixels), a custom integer aspect ratio such as 16:9, 4:3, or 239:100, and 0/90/180/270-degree rendering rotation. **Fit** letterboxes, **Fill** crops, and **Stretch** fills the destination. The preview follows live content with the draft format. Logical dimensions and rotation affect rendering; they do not change the monitor’s electrical signal or OS orientation. Audience content retains its shared 1920×1080 design canvas and is mapped independently into each output; custom stage elements render against that output’s canvas.

For fullscreen displays, the **Physical signal** selector lists OS-reported resolution, refresh, and progressive/interlaced combinations. macOS uses Core Graphics; Windows uses display-settings APIs. HiDPI modes distinguish backing pixels from logical desktop dimensions. Missing refresh or scan information is labelled as unreported. **Keep OS desktop mode** performs no hardware mode change. Custom dimensions are logical rendering settings; arbitrary custom hardware timings are not generated. A selected native mode must be supported by the connected display and accepted by the OS. Mode changes have a 15-second keep/revert dialog. Modes changed by this session are restored when the route is released or the app closes normally, unless the OS/user has since changed that mode. Unsupported or missing saved modes hide the affected output and report the problem. Other outputs and the live cue keep running.

**Settings → Stage Display Layouts…** creates reusable stage layouts. **Copy** the preserved **Standard Stage** layout or choose **New**; name it, set a background color, then add/remove/order elements. Elements include current lyrics/media label, next lyrics/media label, presentation title, clock, countdown, stage message, and custom text. Set each element’s percentage-based position/size, font, size, weight, color, and alignment. **Save Layout** commits the definition. Assign it independently to either stage screen in Screens. Editing a saved layout updates outputs assigned to that layout; assigned layouts cannot be deleted until reassigned.

**Settings → Stage Messages / Timers…** selects Stage Screen 1 or 2. Send/clear its message and start, pause, resume, or reset its countdown. These are independent runtime controls and do not change audience output or cue position. Add Message/Countdown elements to the assigned custom layout to display them. Messages and running timer state are session-local; saved layouts and output assignments persist. The standard stage display remains the default.

Windows runtime and physical multi-projector mode switching/hot-plug still require hardware validation. Automated tests use a mode-provider test double for signal changes; native macOS capability discovery is exercised read-only.

## Build

Install Qt **6.4 or newer**, including Widgets, SQL, the SQLite driver, **Multimedia**, **Concurrent**, **Network**, CMake, and a compatible C++ compiler. Multimedia and Concurrent are standard Qt modules used for playback and background imports; no separate FFmpeg dependency is introduced. Open `CMakeLists.txt` in Qt Creator and use a matching macOS or Windows kit, or build from the command line.

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

Qt Test is required when `WORSHIPCUE_BUILD_TESTS=ON`. Tests use temporary databases and isolated preferences. Most widget tests use Qt’s offscreen backend. On macOS, the operator and media-playback suites use native Cocoa windows in a logged-in desktop session because AVFoundation needs its event dispatcher to decode video. The nineteen suites cover MCP protocol and subprocess transport, atomic service creation, migrations and rollback, persistence, service snapshots, timed advancement and pause/loop boundaries, transition defaults and overrides, output-control fades, bulk transition editing/undo and conflict rollback, background layering, rich text editing/rendering and undo, shared section edits and named arrangements, Looks and measured reflow/undo, standalone media separation and protected deletion, lyric import, four-output routing, native-mode validation/rollback with a test backend, independent format rendering, saved stage layouts/messages/countdowns, reordering, batch naming/import, real H.264 playback, MIDI parsing/configuration/conflicts/retrigger handling, persisted slide actions/targets, isolated native CoreMIDI input/output, usage dates, cleanup eligibility/protection, Recovery and interrupted removal, confirmation/cancel flows, and operator interactions. Windows commands are provided for development; Windows has **not** been tested in this milestone.

Use `macdeployqt` or `windeployqt` from the matching Qt installation to package runtime libraries and plugins. Signing, notarization, native installers, and distribution updates remain future work.

## Storage and compatibility

The default database is `library.sqlite3` under Qt's `QStandardPaths::AppDataLocation` for organization/application **WorshipCue**. On macOS this is normally `~/Library/Application Support/WorshipCue/WorshipCue/`. Geometry, shortcuts, selected service, output routes, and global transition defaults use QSettings. Background assignments are stored in SQLite; the former global background preference is no longer loaded.

The old unversioned database migrates transactionally to schema **11** using `PRAGMA user_version`. Existing songs, slides, service-entry IDs, and order are preserved in **My Service**. Version-1 libraries gain media metadata; version-2 libraries gain timing and transitions; version-3 libraries gain transition overrides and background assignments; version-4 libraries gain per-slide rich text with empty defaults so existing slides retain their appearance. Customized legacy transitions are preserved as overrides when their type is not Cut or their duration is not 500 ms; the previous Cut/500 ms defaults inherit global settings. Each migration is transactional. Newer unknown schema versions are refused. Schema 6 adds the standalone `media_assets` registry and direct media service entries, preserving existing entry IDs, arrangements, and media presentation paths. Schema 7 adds validated song section and arrangement metadata to `songs.song_structure`. Schema 8 adds shared saved Looks and presentation/source-page styling references with legacy defaults. Schema 9 adds transition overrides to standalone media service entries with inherited defaults; copying services or entries preserves them. Schema 10 adds `midi_slide_actions` and `midi_bindings`, preserving song/slide identities while making them non-reusable. It verifies foreign-key integrity before committing. Schema 11 adds separate content/file usage history, Recovery batches, live-session protection leases, and a permanent-removal recovery journal. It preserves existing records and leaves historical usage unknown. Device preferences are stored separately in QSettings. Tables are `looks`, `songs`, `slides`, `services`, `service_entries`, `media_assets`, `midi_slide_actions`, `midi_bindings`, `content_usage`, `managed_files`, `cleanup_batches`, `cleanup_files`, `cleanup_leases`, and `cleanup_lease_refs`. Fresh libraries receive two sample items; an intentionally emptied library remains empty after restart.

A service entry references a presentation or a standalone media asset. Presentation entries store an optional order of source-slide indices. Repeated entries can have different orders, and copying a service copies those orders. Same-count lyric edits preserve indices. For legacy flat presentations, changing the slide count through the bulk repository operation resets custom orders. The structured section editor preserves source identities and remaps complete section occurrences when groups move or gain pages. The currently running service copy is unaffected. Song editing requires Save; service construction and arrangements save after every successful operation.

Back up the entire library folder, including the database, `media`, `backgrounds`, and any `.cleanup-recovery` directory, with all WorshipCue windows and the MCP companion closed. Preserve any SQLite `-wal`/`-shm` sidecars that remain. Managed paths are relative to the database directory, so these files can move together. The 20 included backgrounds are bundled with the app and need no separate image backup; their assignments remain in the database. Duplicate presentations share managed files; deleting a presentation currently retains files to protect copies and active snapshots. Automatic backups, recovery of unsaved editor dialogs, service export/import, and portable media packaging are not implemented. A missing background falls back to the default gradient and is identified in the operator UI. Live state is intentionally not restored on restart.

## Scope and next work

The current application includes a searchable content library, lyric import/paste, named saved services, local MCP service-order creation, picture/video import with batch naming, presentation backgrounds, global transition defaults with presentation overrides, presentation auto-advance/looping, service and slide reordering, Show/Edit operation, shared audience-style previews, live/next monitoring, keyboard controls, and output recovery handling. The rich text slide editor supports direct canvas editing, formatting, per-slide undo/redo, and preview. The bulk lyrics importer retains editable `---` separators.

The next work begins with MCP integration improvements (Corrections 2 Priority 7). Freeform multi-object slide layouts remain future editor work. Library collections, full media source editing, motion lyric backgrounds, audience countdown presentations, Scripture generation, dedicated sermon presentations, additional external integrations, and installers remain unimplemented.

See [the roadmap](docs/ROADMAP.md) and [architecture](docs/ARCHITECTURE.md) for the inspection findings, boundaries, and remaining risks. Automated macOS checks do not replace rehearsal with real projectors, physical hot-plug, macOS Spaces, high DPI, and a Windows build before production use.

WorshipCue is a working project name and is not affiliated with Renewed Vision or ProPresenter. The included example text is original placeholder content.

The ordered Corrections 2 implementation status, test coverage, and remaining plan are recorded in [Corrections 2 progress](docs/CORRECTIONS02_PROGRESS.md).
