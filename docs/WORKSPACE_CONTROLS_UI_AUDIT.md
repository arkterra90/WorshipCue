# Presentation workspace controls

Reviewed November 13, 2026 against WorshipCue's source and rendered native macOS operator window. The goal is to keep attention on the presentation during a service.

## Findings

| Observation | Effect during a service | Change |
|---|---|---|
| Five editing actions filled two rows immediately above the slide grid. Each used a large, bold, filled button. | Infrequent setup tasks competed with the slide content and live controls for attention. | Consolidate them into one flat **Presentation** dropdown at the upper-right of the workspace header. |
| Long editing labels were clipped at the 1080×680 window size. | Operators had to infer the action from incomplete text. The button rows also reduced the visible slide area. | Put full action names in the dropdown, grouping selected-slide actions and whole-presentation actions. |
| The full-width green Take Selected Slide Live button occupied the bottom of the center column. | A live-output action drew attention away from the slide grid while being separated from Previous/Next. | Place **Take Live** between Previous and Next below the live monitor, with a tooltip and accessible name identifying its selected-slide target. |
| Two sets of static usage instructions and prominent saved-settings text surrounded the grid. | Repeated explanations added scanning work on every cue. | Put interaction guidance in the slide workspace and transport tooltips. Move saved timing/background details below the grid with secondary text styling. |

## Placement and behavior

The workspace begins with its presentation title and the quiet Presentation menu. Grid/Text and slide-size controls follow, then the slide content. These view controls support the frequent task of reading and cueing the presentation.

The menu groups **Edit Slide Text…** and **MIDI Actions / Input Trigger…** under Selected slide. **Song Sections / Arrangements…**, **Looks / Reflow…**, and **Timing & Transitions…** belong to the presentation group. Actions are enabled according to the selected content. The slide right-click menu uses the same slide-editing actions, and Edit-mode double-click continues to open the text editor.

The live monitor groups Previous, Take Live, and Next in one transport row. Blackout, Clear Text, and Restore Audience remain immediately beneath the live preview for direct access during a service. Take Live uses the green live-action treatment at a smaller width and a sentence-case label.

Opening the Presentation menu or canceling an editor preserves the live cue, service position, and Blackout state. The menu can be reached with keyboard focus, and native menu behavior provides keyboard selection. Tooltips explain navigation keys and the scope of Take Live.

## Verification

Operator coverage opens the actual dropdown, selects Timing & Transitions, cancels it, and checks cue/Blackout preservation. It checks access to all five actions, disabled actions for an empty selection, and the location of live transport controls. Compact and standard-size checks verify that the grid occupies more than half the window height and that the controls fit within the window.

The existing editor, Looks, arrangements, MIDI, timing, navigation, and timed-video operator tests exercise their normal handlers after the relocation. All 20 regression suites passed, and the local macOS application was rebuilt. The slide grid occupies more than half the window height at 1080×680 and 1280×760. The offline guide and README describe the new menu and live-control locations. Native macOS screenshots support visual review. Windows runtime and physical-output checks require their respective environments.
