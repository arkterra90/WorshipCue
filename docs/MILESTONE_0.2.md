# Saved-service and operator-control milestone

Completed November 10, 2026, extending the existing C++17/Qt application.

## Initial assessment and result

The initial app had working song creation/editing/deletion, a persistent single service order, basic transport, still backgrounds, and separate audience/stage windows. Saved named services, lyric search, readable thumbnails, live highlighting, and familiar keyboard workflows were absent. Navigation depended on the currently browsed service list, creating a risk of redirecting the live presentation during preparation. Output Escape left fullscreen; removed-display identities were discarded.

This milestone adds named service creation, rename, copy, reopening, repeat/duplicate items, immediate service persistence, and independent saved slide order per entry. The library searches titles and lyrics and supports song duplication. The operator has a left library/service sidebar, center slide cards, right Live/Up Next/Selected monitors, Show/Edit behavior, context menus, navigation keys, and configurable safety shortcuts. List/grid dragging uses an explicit row reorder rather than relying on icon positions; thumbnails cache their base rendering during navigation.

The controller snapshots the complete service when taking a slide live. Browsing, editing, reordering, and deleting saved content leave that copy intact. Stage lyrics remain visible during audience-only Clear/Blackout; Restore and taking a new slide return normal content. Shared rendering keeps audience output and previews consistent. OutputManager owns routing, persisted targets, disconnected placeholders/recovery hooks, visibility status, and fullscreen assignment conflict checks.

The old database migrates transactionally to schema 1 without discarding the original service. Read errors are surfaced, writes are validated, and partial transaction failure rolls back.

## Changed files

| Files | Change |
|---|---|
| `CMakeLists.txt`, `src/main.cpp` | Version 0.2.0, source integration, six test targets |
| `src/core/PresentationController.h`, `.cpp` | Service snapshot ownership, boundary navigation, upcoming content, restore |
| `src/data/LibraryRepository.h`, `.cpp` | Named services, schema migration, copying, ordering, search, error handling |
| `src/ui/MainWindow.h`, `.cpp` | Operator layout, saved-service workflow, rendered monitors, keyboard/mouse interactions |
| `src/ui/DisplayWindow.h`, `.cpp` | Shared audience rendering, independent stage lyrics, focus/close signals |
| `src/ui/SlideRenderer.h`, `.cpp` (new) | Shared fixed-canvas rendering and thumbnails |
| `src/ui/OutputManager.h`, `.cpp` (new) | Output ownership, routing, status, preference persistence, recovery hooks |
| `src/ui/ReorderListWidget.h`, `.cpp` (new) | Explicit item reordering for list/grid drags |
| `tests/presentation_controller_test.cpp` | Expanded controller behavior tests |
| `tests/library_repository_test.cpp` (new) | Persistence, migration, validation, rollback and connection tests |
| `tests/slide_renderer_test.cpp` (new) | Rendering and stage/output state tests |
| `tests/output_manager_test.cpp` (new) | Routing, visibility, preference, unavailable-target and conflict tests |
| `tests/operator_workflow_test.cpp` (new) | Build/save/copy/reopen service; navigation, search, typing and live editing |
| `tests/reorder_list_test.cpp` (new) | List/grid identity ordering, selection, signals and external-drop rejection |
| `README.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, this report | Operation, data behavior, implementation boundaries, remaining work |

The existing `SongEditorDialog` is preserved.

## Validation

Environment: macOS 15.7.4, arm64, AppleClang 17, Qt 6.12.0.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DWORSHIPCUE_BUILD_TESTS=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Application and test build passed. CTest: **6/6 suites passed, 0 failed**. Output/widget tests use the offscreen backend. The optional Vulkan-header discovery message is nonfatal; this Widgets application does not require Vulkan.

The final operator suite also passed with native macOS windows:

```sh
QT_QPA_PLATFORM=cocoa ./build/WorshipCueOperatorTests
```

Both workflow cases passed, using temporary databases and preferences. A native operator screenshot was captured and visually inspected. Native execution required access outside the shell sandbox because the sandboxed Cocoa backend could not enumerate screens. No Windows testing was performed.

## Limits and next milestone

- Physical mouse dragging, projectors, fullscreen Spaces, real monitor hot-plug, resolution changes, and identical monitor identities still require native hardware rehearsal. Automated reorder tests cover the shared mutation path and persistence, not a real OS drag gesture.
- The song editor remains a Save/Cancel separator-based dialog. Reflow, sections, full slide editing, undo/redo, themes, and named arrangements are not implemented.
- Arrangements store source indices. Changing a song's slide count resets its saved custom orders; the live service copy remains unchanged.
- Backgrounds remain external absolute image paths. Video, managed media, portable services, backups, draft recovery, and comprehensive crash recovery remain future work.
- Large-library/image responsiveness needs profiling. Automatic saving applies to committed song edits and service changes, not unsaved editor-dialog text.
- Windows and distributable application packaging remain unverified.

Next: complete everyday song preparation with explicit slide editing, preview, undo/redo, Reflow, section labels, stable slide identities, and reusable themes. Add backup/restore and real output rehearsals alongside that work.
