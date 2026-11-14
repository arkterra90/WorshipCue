cmake_minimum_required(VERSION 3.21)
project(WorshipCue VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Multimedia Concurrent Network)

add_library(WorshipCueMidiModel STATIC src/core/MidiTypes.cpp src/core/MidiTypes.h)
target_link_libraries(WorshipCueMidiModel PUBLIC Qt6::Core)
set(WORSHIPCUE_LOOK_SOURCES src/core/PresentationLook.h src/core/PresentationLook.cpp)

set(WORSHIPCUE_CORE_SOURCES
    ${WORSHIPCUE_LOOK_SOURCES}
    src/core/BuiltInBackgrounds.h
    src/core/MediaSlide.h
    src/core/PresentationSettings.h
    src/core/PresentationController.h
    src/core/PresentationController.cpp
)

# Embed the collection in each consumer so installed apps and isolated tests do
# not depend on the source tree or a writable user-library directory.
function(worshipcue_add_backgrounds target)
    qt_add_resources(${target} ${target}_backgrounds
        PREFIX "/backgrounds/builtin"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/backgrounds"
        FILES
            assets/backgrounds/aurora-veil.jpg
            assets/backgrounds/midnight-tide.jpg
            assets/backgrounds/indigo-flow.jpg
            assets/backgrounds/ember-glow.jpg
            assets/backgrounds/jade-mist.jpg
            assets/backgrounds/violet-halo.jpg
            assets/backgrounds/blue-hour.jpg
            assets/backgrounds/rose-dusk.jpg
            assets/backgrounds/golden-hour.jpg
            assets/backgrounds/arctic-light.jpg
            assets/backgrounds/graphite-silk.jpg
            assets/backgrounds/ocean-depths.jpg
            assets/backgrounds/plum-arc.jpg
            assets/backgrounds/copper-lines.jpg
            assets/backgrounds/forest-light.jpg
            assets/backgrounds/cobalt-ribbon.jpg
            assets/backgrounds/desert-dusk.jpg
            assets/backgrounds/lilac-drift.jpg
            assets/backgrounds/teal-contours.jpg
            assets/backgrounds/midnight-prism.jpg
    )
endfunction()
set(WORSHIPCUE_DATA_SOURCES
    src/data/LibraryCleanup.h src/data/LibraryCleanup.cpp
    src/core/SongStructure.h src/core/SongStructure.cpp
    src/data/LibraryRepository.h
    src/data/LibraryRepository.cpp
)
set(WORSHIPCUE_MCP_SOURCES
    src/mcp/McpServer.h
    src/mcp/McpServer.cpp
    src/mcp/McpCreationTools.cpp
    src/mcp/PlanningCenterClient.h
    src/mcp/PlanningCenterClient.cpp
    src/core/LyricsImporter.h
    src/core/LyricsImporter.cpp
)
# A console companion keeps MCP stdio independent of the GUI subsystem on Windows
# and avoids loading Widgets or Multimedia in AI client subprocesses.
add_executable(WorshipCueMcp src/mcp/main.cpp
    ${WORSHIPCUE_LOOK_SOURCES} ${WORSHIPCUE_DATA_SOURCES} ${WORSHIPCUE_MCP_SOURCES})
target_link_libraries(WorshipCueMcp PRIVATE WorshipCueMidiModel Qt6::Core Qt6::Sql Qt6::Network)
# Output format and stage models are shared by renderer, routing, and UI tests.
set(WORSHIPCUE_NATIVE_MODE_SOURCES src/ui/NativeDisplayModes.cpp)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    list(APPEND WORSHIPCUE_NATIVE_MODE_SOURCES src/ui/NativeDisplayModes_mac.mm)
elseif(WIN32)
    list(APPEND WORSHIPCUE_NATIVE_MODE_SOURCES src/ui/NativeDisplayModes_win.cpp)
endif()
add_library(WorshipCueOutputSupport STATIC
    src/ui/OutputConfiguration.cpp src/ui/StageLayout.cpp ${WORSHIPCUE_NATIVE_MODE_SOURCES})
target_link_libraries(WorshipCueOutputSupport PUBLIC Qt6::Gui Qt6::Core)
if(APPLE)
    target_link_libraries(WorshipCueOutputSupport PUBLIC "-framework AppKit" "-framework CoreGraphics")
elseif(WIN32)
    target_link_libraries(WorshipCueOutputSupport PUBLIC user32)
endif()
set(WORSHIPCUE_MIDI_NATIVE_SOURCES)
if(APPLE)
    list(APPEND WORSHIPCUE_MIDI_NATIVE_SOURCES src/midi/MidiBackend_mac.mm)
elseif(WIN32)
    list(APPEND WORSHIPCUE_MIDI_NATIVE_SOURCES src/midi/MidiBackend_win.cpp)
endif()
add_library(WorshipCueMidi STATIC src/midi/MidiBackend.h src/midi/MidiBackend.cpp
    src/midi/MidiEngine.h src/midi/MidiEngine.cpp ${WORSHIPCUE_MIDI_NATIVE_SOURCES})
target_link_libraries(WorshipCueMidi PUBLIC WorshipCueMidiModel Qt6::Core)
if(APPLE)
    target_link_libraries(WorshipCueMidi PUBLIC "-framework CoreMIDI" "-framework CoreFoundation")
elseif(WIN32)
    target_link_libraries(WorshipCueMidi PUBLIC winmm)
endif()
set(WORSHIPCUE_OUTPUT_SOURCES
    src/ui/RichTextSupport.h
    src/ui/RichTextSupport.cpp
    src/media/MediaPlayback.h
    src/media/MediaPlayback.cpp
    src/ui/SlideRenderer.h
    src/ui/SlideRenderer.cpp
    src/ui/DisplayWindow.h
    src/ui/DisplayWindow.cpp
    src/ui/OutputManager.h
    src/ui/OutputManager.cpp
)
set(WORSHIPCUE_OPERATOR_SOURCES
    src/ui/LibraryCleanupDialog.h src/ui/LibraryCleanupDialog.cpp
    src/ui/MidiDialogs.h src/ui/MidiDialogs.cpp
    src/ui/OutputSettingsDialog.h src/ui/OutputSettingsDialog.cpp
    src/ui/StageLayoutDialog.h src/ui/StageLayoutDialog.cpp
    src/ui/PresentationLookDialog.h src/ui/PresentationLookDialog.cpp
    src/ui/SlideEditorDialog.h
    src/ui/SlideEditorDialog.cpp
    src/media/BatchRenamer.h
    src/media/BatchRenamer.cpp
    src/media/MediaManager.h
    src/media/MediaManager.cpp
    src/ui/MediaImportDialog.h
    src/ui/MediaImportDialog.cpp
    src/ui/ReorderListWidget.h
    src/ui/ReorderListWidget.cpp
    src/ui/SlideTextDelegate.h
    src/ui/SlideTextDelegate.cpp
    src/ui/SlideGridDelegate.h
    src/ui/SlideGridDelegate.cpp
    src/ui/SongStructureDialog.h src/ui/SongStructureDialog.cpp
    src/ui/SongEditorDialog.h
    src/ui/SongEditorDialog.cpp
    src/core/LyricsImporter.h
    src/core/LyricsImporter.cpp
    src/ui/BackgroundLibraryDialog.h
    src/ui/BackgroundLibraryDialog.cpp
    src/ui/PresentationSettingsDialog.h
    src/ui/PresentationSettingsDialog.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)
qt_add_executable(WorshipCue src/main.cpp
    ${WORSHIPCUE_CORE_SOURCES} ${WORSHIPCUE_DATA_SOURCES}
    ${WORSHIPCUE_OUTPUT_SOURCES} ${WORSHIPCUE_OPERATOR_SOURCES}
)
worshipcue_add_backgrounds(WorshipCue)
add_dependencies(WorshipCue WorshipCueMcp)
if(APPLE)
    # Run on every app build, including when only the companion has changed.
    add_custom_target(WorshipCueMcpBundle
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WorshipCue>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WorshipCueMcp>" "$<TARGET_FILE_DIR:WorshipCue>/WorshipCueMcp"
        DEPENDS WorshipCueMcp
        COMMENT "Copying the local MCP server into WorshipCue.app"
        VERBATIM)
    add_dependencies(WorshipCue WorshipCueMcpBundle)
endif()
qt_add_resources(WorshipCue app_icons
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons"
    FILES assets/icons/worshipcue.png
)
if(APPLE)
    target_sources(WorshipCue PRIVATE assets/icons/WorshipCue.icns)
    set_source_files_properties(assets/icons/WorshipCue.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(WorshipCue PROPERTIES
        MACOSX_BUNDLE_ICON_FILE WorshipCue.icns)
endif()
target_link_libraries(WorshipCue PRIVATE WorshipCueMidi WorshipCueMidiModel WorshipCueOutputSupport Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::Concurrent)
set_target_properties(WorshipCue PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "WorshipCue"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.worshipcue.desktop"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

include(GNUInstallDirs)
install(TARGETS WorshipCue BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(APPLE)
    install(TARGETS WorshipCueMcp RUNTIME DESTINATION WorshipCue.app/Contents/MacOS)
else()
    install(TARGETS WorshipCueMcp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

option(WORSHIPCUE_BUILD_TESTS "Build WorshipCue behavior and persistence tests" OFF)
if(WORSHIPCUE_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    if(APPLE)
        qt_add_executable(WorshipCueMidiNativeTests tests/midi_native_test.mm)
        target_link_libraries(WorshipCueMidiNativeTests PRIVATE WorshipCueMidi Qt6::Test)
        add_test(NAME midi_native COMMAND WorshipCueMidiNativeTests)
    endif()
    qt_add_executable(WorshipCueCleanupTests tests/library_cleanup_test.cpp ${WORSHIPCUE_LOOK_SOURCES} ${WORSHIPCUE_DATA_SOURCES})
    target_link_libraries(WorshipCueCleanupTests PRIVATE WorshipCueMidiModel Qt6::Core Qt6::Sql Qt6::Test)
    add_test(NAME library_cleanup COMMAND WorshipCueCleanupTests)
    qt_add_executable(WorshipCueMidiRepositoryTests tests/midi_repository_test.cpp ${WORSHIPCUE_LOOK_SOURCES} ${WORSHIPCUE_DATA_SOURCES})
    target_link_libraries(WorshipCueMidiRepositoryTests PRIVATE WorshipCueMidiModel Qt6::Core Qt6::Sql Qt6::Test)
    add_test(NAME midi_repository COMMAND WorshipCueMidiRepositoryTests)
    qt_add_executable(WorshipCueMidiTests tests/midi_engine_test.cpp)
    target_link_libraries(WorshipCueMidiTests PRIVATE WorshipCueMidi Qt6::Test)
    add_test(NAME midi_engine COMMAND WorshipCueMidiTests)
    qt_add_executable(WorshipCueCoreTests tests/presentation_controller_test.cpp ${WORSHIPCUE_CORE_SOURCES})
    target_link_libraries(WorshipCueCoreTests PRIVATE WorshipCueMidiModel Qt6::Core Qt6::Test)
    add_test(NAME presentation_controller COMMAND WorshipCueCoreTests)

    qt_add_executable(WorshipCueRepositoryTests tests/library_repository_test.cpp ${WORSHIPCUE_LOOK_SOURCES} ${WORSHIPCUE_DATA_SOURCES})
    target_link_libraries(WorshipCueRepositoryTests PRIVATE WorshipCueMidiModel Qt6::Core Qt6::Sql Qt6::Test)
    add_test(NAME library_repository COMMAND WorshipCueRepositoryTests)

    qt_add_executable(WorshipCueSongStructureTests tests/song_structure_test.cpp
        ${WORSHIPCUE_CORE_SOURCES} ${WORSHIPCUE_DATA_SOURCES}
        src/ui/SongStructureDialog.h src/ui/SongStructureDialog.cpp
        src/ui/ReorderListWidget.h src/ui/ReorderListWidget.cpp)
    target_link_libraries(WorshipCueSongStructureTests PRIVATE WorshipCueMidiModel Qt6::Widgets Qt6::Sql Qt6::Test)
    add_test(NAME song_structure COMMAND WorshipCueSongStructureTests)

    qt_add_executable(WorshipCueLookTests tests/presentation_look_test.cpp
        ${WORSHIPCUE_CORE_SOURCES} ${WORSHIPCUE_DATA_SOURCES}
        src/ui/PresentationLookDialog.h src/ui/PresentationLookDialog.cpp
        src/ui/RichTextSupport.h src/ui/RichTextSupport.cpp
        src/ui/SlideRenderer.h src/ui/SlideRenderer.cpp
        src/ui/BackgroundLibraryDialog.h src/ui/BackgroundLibraryDialog.cpp)
    worshipcue_add_backgrounds(WorshipCueLookTests)
    target_link_libraries(WorshipCueLookTests PRIVATE WorshipCueMidiModel Qt6::Widgets Qt6::Sql Qt6::Test)
    add_test(NAME presentation_looks COMMAND WorshipCueLookTests)

    qt_add_executable(WorshipCueMcpTests tests/mcp_server_test.cpp
        ${WORSHIPCUE_LOOK_SOURCES} ${WORSHIPCUE_DATA_SOURCES} ${WORSHIPCUE_MCP_SOURCES})
    target_link_libraries(WorshipCueMcpTests PRIVATE WorshipCueMidiModel Qt6::Core Qt6::Sql Qt6::Network Qt6::Test)
    target_compile_definitions(WorshipCueMcpTests PRIVATE
        WORSHIPCUE_MCP_EXECUTABLE="$<TARGET_FILE:WorshipCueMcp>")
    add_dependencies(WorshipCueMcpTests WorshipCueMcp)
    add_test(NAME mcp_server COMMAND WorshipCueMcpTests)

    qt_add_executable(WorshipCueLyricsImportTests tests/lyrics_import_test.cpp
        src/core/SongStructure.h src/core/SongStructure.cpp
        src/core/LyricsImporter.h src/core/LyricsImporter.cpp
        src/ui/SongEditorDialog.h src/ui/SongEditorDialog.cpp)
    target_link_libraries(WorshipCueLyricsImportTests PRIVATE WorshipCueMidiModel Qt6::Widgets Qt6::Sql Qt6::Test)
    add_test(NAME lyrics_import COMMAND WorshipCueLyricsImportTests)

    qt_add_executable(WorshipCueRendererTests tests/slide_renderer_test.cpp
        src/ui/RichTextSupport.h src/ui/RichTextSupport.cpp
        ${WORSHIPCUE_CORE_SOURCES} src/ui/SlideRenderer.cpp src/ui/SlideRenderer.h
        src/ui/BackgroundLibraryDialog.cpp src/ui/BackgroundLibraryDialog.h
        src/ui/DisplayWindow.cpp src/ui/DisplayWindow.h src/media/MediaPlayback.h src/media/MediaPlayback.cpp)
    worshipcue_add_backgrounds(WorshipCueRendererTests)
    target_link_libraries(WorshipCueRendererTests PRIVATE WorshipCueMidiModel WorshipCueOutputSupport Qt6::Widgets Qt6::Test Qt6::Multimedia Qt6::Concurrent)
    add_test(NAME slide_renderer COMMAND WorshipCueRendererTests)

    qt_add_executable(WorshipCueSlideEditorTests tests/slide_editor_test.cpp
        ${WORSHIPCUE_LOOK_SOURCES}
        src/ui/SlideEditorDialog.h src/ui/SlideEditorDialog.cpp
        src/ui/RichTextSupport.h src/ui/RichTextSupport.cpp
        src/ui/SlideRenderer.h src/ui/SlideRenderer.cpp)
    target_link_libraries(WorshipCueSlideEditorTests PRIVATE WorshipCueMidiModel Qt6::Widgets Qt6::Sql Qt6::Test)
    add_test(NAME slide_editor COMMAND WorshipCueSlideEditorTests)

    qt_add_executable(WorshipCueOutputTests tests/output_manager_test.cpp
        src/ui/OutputSettingsDialog.h src/ui/OutputSettingsDialog.cpp
        src/ui/StageLayoutDialog.h src/ui/StageLayoutDialog.cpp
        ${WORSHIPCUE_CORE_SOURCES} ${WORSHIPCUE_OUTPUT_SOURCES})
    target_link_libraries(WorshipCueOutputTests PRIVATE WorshipCueMidiModel WorshipCueOutputSupport Qt6::Widgets Qt6::Test Qt6::Multimedia Qt6::Concurrent)
    add_test(NAME output_manager COMMAND WorshipCueOutputTests)

    qt_add_executable(WorshipCueStageLayoutTests tests/stage_layout_test.cpp
        ${WORSHIPCUE_CORE_SOURCES} ${WORSHIPCUE_OUTPUT_SOURCES})
    target_link_libraries(WorshipCueStageLayoutTests PRIVATE WorshipCueMidiModel WorshipCueOutputSupport Qt6::Widgets Qt6::Test Qt6::Multimedia Qt6::Concurrent)
    add_test(NAME stage_layout COMMAND WorshipCueStageLayoutTests)

    qt_add_executable(WorshipCueOperatorTests tests/operator_workflow_test.cpp
        ${WORSHIPCUE_CORE_SOURCES} ${WORSHIPCUE_DATA_SOURCES}
        ${WORSHIPCUE_OUTPUT_SOURCES} ${WORSHIPCUE_OPERATOR_SOURCES})
    worshipcue_add_backgrounds(WorshipCueOperatorTests)
    target_link_libraries(WorshipCueOperatorTests PRIVATE WorshipCueMidi WorshipCueMidiModel WorshipCueOutputSupport Qt6::Widgets Qt6::Sql Qt6::Test Qt6::Multimedia Qt6::Concurrent)
    add_test(NAME operator_workflow COMMAND WorshipCueOperatorTests)

    qt_add_executable(WorshipCueReorderTests tests/reorder_list_test.cpp
        src/ui/ReorderListWidget.h src/ui/ReorderListWidget.cpp
        src/ui/SlideGridDelegate.h src/ui/SlideGridDelegate.cpp)
    target_link_libraries(WorshipCueReorderTests PRIVATE WorshipCueMidiModel Qt6::Widgets Qt6::Test)
    add_test(NAME reorder_list COMMAND WorshipCueReorderTests)

    qt_add_executable(WorshipCueBatchRenameTests tests/batch_renamer_test.cpp
        src/media/BatchRenamer.h src/media/BatchRenamer.cpp src/ui/MediaImportDialog.h src/ui/MediaImportDialog.cpp)
    target_link_libraries(WorshipCueBatchRenameTests PRIVATE WorshipCueMidiModel Qt6::Widgets Qt6::Test)
    add_test(NAME batch_renamer COMMAND WorshipCueBatchRenameTests)

    qt_add_executable(WorshipCueMediaImportTests tests/media_import_test.cpp
        src/media/BatchRenamer.h src/media/BatchRenamer.cpp src/media/MediaManager.h src/media/MediaManager.cpp)
    target_link_libraries(WorshipCueMediaImportTests PRIVATE WorshipCueMidiModel Qt6::Widgets Qt6::Concurrent Qt6::Test)
    add_test(NAME media_import COMMAND WorshipCueMediaImportTests)

    qt_add_executable(WorshipCueMediaPlaybackTests tests/media_playback_test.cpp
        src/ui/RichTextSupport.h src/ui/RichTextSupport.cpp
        ${WORSHIPCUE_CORE_SOURCES} src/media/MediaPlayback.h src/media/MediaPlayback.cpp src/ui/SlideRenderer.h src/ui/SlideRenderer.cpp
        src/ui/DisplayWindow.h src/ui/DisplayWindow.cpp)
    target_link_libraries(WorshipCueMediaPlaybackTests PRIVATE WorshipCueMidiModel WorshipCueOutputSupport Qt6::Widgets Qt6::Concurrent Qt6::Multimedia Qt6::Test)
    add_test(NAME media_playback COMMAND WorshipCueMediaPlaybackTests)

    set_tests_properties(stage_layout presentation_looks song_structure slide_renderer slide_editor output_manager operator_workflow reorder_list batch_renamer media_import media_playback lyrics_import
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # AVFoundation needs Cocoa's native event dispatcher to deliver video frames.
    if(APPLE)
        set_tests_properties(media_playback operator_workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
    endif()
endif()
