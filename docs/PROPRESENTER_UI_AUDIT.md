# ProPresenter UI audit and WorshipCue application

Reviewed **November 10, 2026**, with implementation completed and primary-source links rechecked **November 11, 2026**, against Renewed Vision's public documentation and the local WorshipCue source. This is a documentation and source audit, not a hands-on review of an installed ProPresenter release.

## Findings and changes

| Operator need | Reference finding | WorshipCue baseline finding | Applied change |
|---|---|---|---|
| Read lyrics quickly while cueing | ProPresenter offers thumbnail, outline/table, and readable-text views. [Slide view options](https://support.renewedvision.com/hc/en-us/articles/360041344174-Slide-View-Options-in-ProPresenter). | Fixed 256×144 thumbnails make long lyrics difficult to scan; operators cannot choose density. | Offer **Grid** and **Text** views plus a size slider. Text view wraps full lyrics and identifies media; both preserve slide numbering and live/missing markers. These preferences affect the operator display only. |
| Allocate workspace to the current task | ProPresenter separates libraries/playlists, slide browsing, and live monitoring, and offers a slide-size slider. [Interface guide](https://support.renewedvision.com/hc/en-us/articles/360041345954-Understanding-The-ProPresenter-User-Interface). | The library and service share one fixed vertical layout; horizontal panel sizes are not remembered. | Add a vertical library/service divider and persist both divider positions, slide view, size, and preview tab. Retain separate content-library and service identities. |
| Find urgent audience controls | ProPresenter places layer-clearing controls beside its live preview and transport nearby. [Interface guide](https://support.renewedvision.com/hc/en-us/articles/360041345954-Understanding-The-ProPresenter-User-Interface). | Blackout, Clear Text, and Restore appear below three stacked previews and other controls. This crowds the advertised minimum window height. | Put audience controls directly beneath live output. Use **Up Next / Selected** preview tabs and show timing/video controls when applicable. Keep audience routing visible and distinct from content state. |
| Find content without leaving the keyboard | ProPresenter's Quick Search uses **Command+F** on macOS and **Control+F** on Windows. [Quick Search](https://support.renewedvision.com/hc/en-us/articles/9808905773715-How-to-Use-Quick-Search-within-ProPresenter). | WorshipCue already searches titles, lyrics, and media names, but lacks a standard search-focus shortcut. | Focus the existing library search and select its current query with the platform Find shortcut. Keep current selection and output unchanged. |
| Recover orientation after browsing | Distinguishing content browsing from live output is an operator requirement inferred from this audit. | WorshipCue intentionally runs a copied service while the operator browses or edits saved content. Returning to its saved cue requires manual navigation. | Add **Return to Live** to reveal the corresponding service/cue without taking it live again. Report missing or changed saved content rather than implying the saved slide matches the live copy. |

## Behavior to preserve

WorshipCue already provides useful operator protections: Show/Edit modes, selected and live previews, numbered service order, current/next monitoring across service boundaries, independent audience/stage routes, saved arrangements, and live snapshots. Retain these behaviors while improving layout.

Changing view, size, dividers, preview tabs, or search focus must not cue a slide, change service order, restart media/timing, or restore hidden audience content. Show mode still cues with a click; Edit mode selects for preview and permits reordering. The existing source-slide indices and raw content remain the source of truth in either view.

**Clear Text**, **Blackout**, and **Restore Audience** retain WorshipCue's existing meanings. Clear Text leaves visual media and audio; Blackout hides audience output and mutes video; Stage remains independent. These are not presented as equivalents of ProPresenter's full layer-clearing system.

## Deferred work

- **Section labels and named arrangements:** ProPresenter builds arrangements from labeled groups. WorshipCue currently stores source-slide index orders per service occurrence. Stable slide identities, group metadata, migration, and editing behavior should precede that workflow. [Groups and arrangements](https://support.renewedvision.com/hc/en-us/articles/360041809973-Using-Groups-and-Arrangements-for-Presentation-Organization).
- **Continuous playlist:** ProPresenter can show multiple presentations together. WorshipCue's current workspace represents one selected service entry; a continuous view needs explicit entry/slide mapping and careful selection, reordering, and live-copy handling. [Full playlist view](https://support.renewedvision.com/hc/en-us/articles/360041348254-Viewing-My-Full-Playlist).
- **Themes, reflow, timers, messages, and macros:** These have dedicated editing or control surfaces in ProPresenter. They require underlying content/settings models and complete workflows before adding toolbar buttons. [Interface guide](https://support.renewedvision.com/hc/en-us/articles/360041345954-Understanding-The-ProPresenter-User-Interface).
- **Additional output layers and integrations:** Audience Looks, independent audio/announcement layers, streaming, and online song services exceed this UI pass. Keep WorshipCue's existing offline library and audience/stage rendering contracts intact.

## Verification results — November 11, 2026

The native macOS build completed with Qt **6.12.0**, and **all 11 CTest suites passed** in 22.10 seconds. The local `build-local/WorshipCue.app` was rebuilt with these changes.

Operator regression checks cover view/size changes during blackout without changing the live copy or saved order, Text-view cueing and arrangement changes, Return to Live after browsing another service and after saved content changes, the platform Find shortcut, and workspace preference restoration. The size-slider test checks that the maximum thumbnail actually renders at the requested size.

Grid and Text screenshots were visually reviewed. Additional native-window checks and screenshots verified that both previews and the audience, timing, and video controls remain visible without clipping at **1080×680**, including during decoded timed-video playback and blackout. Return to Live now sits in the top toolbar, leaving more monitor space for active playback controls.

Physical projector routing, hot-plug behavior, and Windows layout were not validated in this UI pass and still require rehearsal on those systems.
