# CMake generated Testfile for 
# Source directory: /Users/jeremykuehn/Downloads/WorshipCue
# Build directory: /Users/jeremykuehn/Downloads/WorshipCue/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("presentation_controller" "/Users/jeremykuehn/Downloads/WorshipCue/build/WorshipCueCoreTests")
set_tests_properties("presentation_controller" PROPERTIES  _BACKTRACE_TRIPLES "/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;68;add_test;/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;0;")
add_test("library_repository" "/Users/jeremykuehn/Downloads/WorshipCue/build/WorshipCueRepositoryTests")
set_tests_properties("library_repository" PROPERTIES  _BACKTRACE_TRIPLES "/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;72;add_test;/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;0;")
add_test("slide_renderer" "/Users/jeremykuehn/Downloads/WorshipCue/build/WorshipCueRendererTests")
set_tests_properties("slide_renderer" PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" _BACKTRACE_TRIPLES "/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;78;add_test;/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;0;")
add_test("output_manager" "/Users/jeremykuehn/Downloads/WorshipCue/build/WorshipCueOutputTests")
set_tests_properties("output_manager" PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" _BACKTRACE_TRIPLES "/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;83;add_test;/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;0;")
add_test("operator_workflow" "/Users/jeremykuehn/Downloads/WorshipCue/build/WorshipCueOperatorTests")
set_tests_properties("operator_workflow" PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=cocoa" _BACKTRACE_TRIPLES "/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;89;add_test;/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;0;")
add_test("reorder_list" "/Users/jeremykuehn/Downloads/WorshipCue/build/WorshipCueReorderTests")
set_tests_properties("reorder_list" PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" _BACKTRACE_TRIPLES "/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;94;add_test;/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;0;")
add_test("batch_renamer" "/Users/jeremykuehn/Downloads/WorshipCue/build/WorshipCueBatchRenameTests")
set_tests_properties("batch_renamer" PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" _BACKTRACE_TRIPLES "/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;99;add_test;/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;0;")
add_test("media_import" "/Users/jeremykuehn/Downloads/WorshipCue/build/WorshipCueMediaImportTests")
set_tests_properties("media_import" PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" _BACKTRACE_TRIPLES "/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;104;add_test;/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;0;")
add_test("media_playback" "/Users/jeremykuehn/Downloads/WorshipCue/build/WorshipCueMediaPlaybackTests")
set_tests_properties("media_playback" PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=cocoa" _BACKTRACE_TRIPLES "/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;110;add_test;/Users/jeremykuehn/Downloads/WorshipCue/CMakeLists.txt;0;")
