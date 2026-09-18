cmake_minimum_required(VERSION 3.22)
project(Relay VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CTest)
option(RELAY_BUILD_APP "Build the native Konsole-based Relay application" ON)
set(RELAY_QT_MAJOR "AUTO" CACHE STRING "Qt/KDE Frameworks major version: AUTO, 6, or 5")
set_property(CACHE RELAY_QT_MAJOR PROPERTY STRINGS AUTO 6 5)
if(RELAY_QT_MAJOR STREQUAL "AUTO")
    # Probe config files without find_package(): loading Qt6 packages sets
    # QT_MAJOR_VERSION, which later breaks KF5's Qt5 dependency resolution.
    find_file(RELAY_QT6_CONFIG Qt6Config.cmake PATH_SUFFIXES cmake/Qt6 lib/cmake/Qt6 lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/Qt6)
    find_file(RELAY_KF6PARTS_CONFIG KF6PartsConfig.cmake PATH_SUFFIXES cmake/KF6Parts lib/cmake/KF6Parts lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/KF6Parts)
    if(RELAY_QT6_CONFIG AND (RELAY_KF6PARTS_CONFIG OR NOT RELAY_BUILD_APP))
        set(RELAY_QT_MAJOR 6)
    else()
        set(RELAY_QT_MAJOR 5)
    endif()
endif()
if(RELAY_QT_MAJOR STREQUAL "6")
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
elseif(RELAY_QT_MAJOR STREQUAL "5")
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
else()
    message(FATAL_ERROR "RELAY_QT_MAJOR must be AUTO, 6, or 5.")
endif()
set(RELAY_QT Qt${RELAY_QT_MAJOR})
set(RELAY_KF KF${RELAY_QT_MAJOR})
message(STATUS "Relay: building against ${RELAY_QT} / ${RELAY_KF}")
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

add_library(relay-editor STATIC src/RichEditor.cpp src/RichEditor.h)
target_include_directories(relay-editor PUBLIC src)
target_link_libraries(relay-editor PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-editor PRIVATE -Wall -Wextra -Wpedantic)

# Folder explorer and file preview panes: plain Qt, with optional syntax highlighting and PDF.
add_library(relay-filepanes STATIC src/FilePanes.cpp src/FilePanes.h)
target_include_directories(relay-filepanes PUBLIC src)
target_link_libraries(relay-filepanes PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-filepanes PRIVATE -Wall -Wextra -Wpedantic)
find_package(${RELAY_KF}SyntaxHighlighting QUIET)
if(TARGET ${RELAY_KF}::SyntaxHighlighting)
    target_link_libraries(relay-filepanes PRIVATE ${RELAY_KF}::SyntaxHighlighting)
    target_compile_definitions(relay-filepanes PRIVATE RELAY_HAVE_SYNTAX_HIGHLIGHTING)
    message(STATUS "Relay: file preview syntax highlighting enabled (${RELAY_KF}SyntaxHighlighting)")
else()
    message(STATUS "Relay: file preview syntax highlighting disabled (${RELAY_KF}SyntaxHighlighting not found)")
endif()
find_package(${RELAY_QT} QUIET COMPONENTS Pdf PdfWidgets)
if(TARGET ${RELAY_QT}::Pdf AND TARGET ${RELAY_QT}::PdfWidgets)
    target_link_libraries(relay-filepanes PRIVATE ${RELAY_QT}::Pdf ${RELAY_QT}::PdfWidgets)
    target_compile_definitions(relay-filepanes PRIVATE RELAY_HAVE_QTPDF)
    message(STATUS "Relay: PDF preview enabled (${RELAY_QT} Pdf)")
else()
    message(STATUS "Relay: PDF preview disabled (${RELAY_QT} Pdf not found)")
endif()

# Subagents UI: running-agents list and live transcript view (plain Qt).
add_library(relay-subagents STATIC src/SubagentsPanel.cpp src/SubagentsPanel.h src/SubagentTranscript.cpp src/SubagentTranscript.h)
target_include_directories(relay-subagents PUBLIC src)
target_link_libraries(relay-subagents PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-subagents PRIVATE -Wall -Wextra -Wpedantic)

# Terminal engine selection (--engine / RELAY_ENGINE): plain Qt, no KParts and no engine,
# so the parsing rules can be tested on their own. src/BackendFactory.cpp adds the two
# implementations to the app.
add_library(relay-backends STATIC src/TerminalBackends.cpp src/TerminalBackends.h)
target_include_directories(relay-backends PUBLIC src)
target_link_libraries(relay-backends PUBLIC ${RELAY_QT}::Core)
# The engine library is always built (see add_subdirectory(engine) below), so
# --engine=relay is available in every build.
target_compile_definitions(relay-backends PUBLIC RELAY_HAVE_ENGINE)
target_compile_options(relay-backends PRIVATE -Wall -Wextra -Wpedantic)

# Saved window layout ("reopen where I left off"): reading/writing state/windows.json, screen
# clamping and directory fallbacks. Plain QtCore so the rules can be tested without a window.
add_library(relay-windowstate STATIC src/WindowState.cpp src/WindowState.h)
target_include_directories(relay-windowstate PUBLIC src)
target_link_libraries(relay-windowstate PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-windowstate PRIVATE -Wall -Wextra -Wpedantic)

# Request ledger UI: model for requests/todos events and the expanded request list (plain Qt).
add_library(relay-completion STATIC src/Completion.cpp src/Completion.h)
target_include_directories(relay-completion PUBLIC src)
target_link_libraries(relay-completion PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-completion PRIVATE -Wall -Wextra -Wpedantic)

# Rotating diagnostics log (~/.local/share/relay/logs/relay.log). Plain QtCore so the rotation,
# level and redaction rules can be tested without a window.
add_library(relay-logging STATIC src/Logging.cpp src/Logging.h)
target_include_directories(relay-logging PUBLIC src)
target_link_libraries(relay-logging PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-logging PRIVATE -Wall -Wextra -Wpedantic)

# Prompt-box-only input: where a submitted line goes, when "Take control" is offered, and how
# a password typed into the masked prompt box is wiped. Pure rules, no terminal.
add_library(relay-input STATIC src/InputPolicy.cpp src/InputPolicy.h)
target_include_directories(relay-input PUBLIC src)
target_link_libraries(relay-input PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-input PRIVATE -Wall -Wextra -Wpedantic)

# Screen-text input detection: "is the program waiting for a line?" read off the last rows of
# the screen (issues/features/2026-09-17-screen-text-input-detection.md). Pure text rules.
add_library(relay-screen STATIC src/ScreenPrompt.cpp src/ScreenPrompt.h)
target_include_directories(relay-screen PUBLIC src)
target_link_libraries(relay-screen PUBLIC relay-input ${RELAY_QT}::Core)
target_compile_options(relay-screen PRIVATE -Wall -Wextra -Wpedantic)

# Conversation list with full-text search (Ctrl+Shift+O) and the Ctrl+F find bar. The worker does
# the searching (docs/AGENT-SESSIONS-PROTOCOL.md section 14); this is the dialog and its helpers.
add_library(relay-conversations STATIC src/Conversations.cpp src/Conversations.h)
target_include_directories(relay-conversations PUBLIC src)
target_link_libraries(relay-conversations PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-conversations PRIVATE -Wall -Wextra -Wpedantic)

# Notification centre: the list behind the bell in the window header. Plain QtCore so the
# rules (order, unread count, cap) can be tested without a window.
add_library(relay-notifications STATIC src/Notifications.cpp src/Notifications.h)
target_include_directories(relay-notifications PUBLIC src)
target_link_libraries(relay-notifications PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-notifications PRIVATE -Wall -Wextra -Wpedantic)

# Pane movement: which pane is the neighbour in a direction, how two panes swap inside their
# splitter, and which edge a dropped pane lands on. Plain Qt widgets, no terminal.
add_library(relay-panes STATIC src/PaneLayout.cpp src/PaneLayout.h)
target_include_directories(relay-panes PUBLIC src)
target_link_libraries(relay-panes PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-panes PRIVATE -Wall -Wextra -Wpedantic)

# Voice transcription (issue NY7Z): which capture tool to run, which key event is the hold key,
# where the transcript lands in the composer, and WAV repair. Plain QtCore so every rule can be
# tested without a microphone or a window; the worker does the transcribing (protocol 16).
add_library(relay-voice STATIC src/Voice.cpp src/Voice.h)
target_include_directories(relay-voice PUBLIC src)
target_link_libraries(relay-voice PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-voice PRIVATE -Wall -Wextra -Wpedantic)

# Prompt-box colouring: shell syntax when the line is going to the shell, plain text for an agent
# prompt, and the caret colour that shows which (owner design, 2026-09-17).
add_library(relay-highlight STATIC src/ShellHighlighter.cpp src/ShellHighlighter.h src/Theme.cpp src/Theme.h)
target_include_directories(relay-highlight PUBLIC src)
target_link_libraries(relay-highlight PUBLIC ${RELAY_QT}::Gui ${RELAY_QT}::Widgets)
target_compile_options(relay-highlight PRIVATE -Wall -Wextra -Wpedantic)

add_library(relay-requests STATIC src/RequestLedger.cpp src/RequestLedger.h src/RequestsPanel.cpp src/RequestsPanel.h)
target_include_directories(relay-requests PUBLIC src)
target_link_libraries(relay-requests PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-requests PRIVATE -Wall -Wextra -Wpedantic)

# Relay's own terminal engine (engine/, docs/ENGINE.md). The library is built and linked
# into the app (RELAY_HAVE_ENGINE, --engine=relay); the manual harness and the benchmark
# are extra targets behind RELAY_BUILD_ENGINE.
option(RELAY_BUILD_ENGINE "Also build the engine harness (relay-vterm-spike) and benchmark" OFF)
option(RELAY_BUILD_ENGINE_SPIKE "Deprecated alias for RELAY_BUILD_ENGINE" OFF)
add_subdirectory(engine)

if(RELAY_BUILD_APP)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Relay v0.1 targets Linux (KonsolePart, /proc, and Bash integration).")
    endif()
    find_package(${RELAY_KF}CoreAddons REQUIRED)
    find_package(${RELAY_KF}Parts REQUIRED)
    add_executable(relay src/main.cpp src/Theme.cpp src/Theme.h src/AgentUi.cpp src/AgentUi.h src/Hints.cpp src/Hints.h src/TurnTranscript.cpp src/TurnTranscript.h src/SkillsDialog.cpp src/SkillsDialog.h
        src/ModelSettings.cpp src/ModelSettings.h
        src/KonsoleBackend.cpp src/KonsoleBackend.h src/EngineBackend.cpp src/EngineBackend.h src/BackendFactory.cpp)
    find_package(${RELAY_QT} REQUIRED COMPONENTS DBus)
    target_link_libraries(relay PRIVATE relay-highlight relay-editor relay-filepanes relay-subagents relay-requests relay-conversations relay-completion relay-backends relay-windowstate relay-input relay-screen relay-notifications relay-panes relay-logging relay-voice relay-terminal-engine ${RELAY_KF}::Parts ${RELAY_KF}::CoreAddons ${RELAY_QT}::DBus)
    target_compile_definitions(relay PRIVATE
        RELAY_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/relay"
        RELAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        RELAY_VERSION="${PROJECT_VERSION}")
    target_compile_options(relay PRIVATE -Wall -Wextra -Wpedantic)
    install(TARGETS relay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY backend shell DESTINATION ${CMAKE_INSTALL_DATADIR}/relay
        PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
    install(DIRECTORY data/theme DESTINATION ${CMAKE_INSTALL_DATADIR}/relay)
    install(FILES org.relayterminal.Relay.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(DIRECTORY data/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
    install(FILES data/icons/org.relayterminal.Relay.svg data/icons/org.relayterminal.Relay-symbolic.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    # dataRoot() prepends <data>/scripts to PATH and points RELAY_OPEN_HELPER at relay-open.
    install(PROGRAMS scripts/relay-open scripts/relay-agent.py DESTINATION ${CMAKE_INSTALL_DATADIR}/relay/scripts)
    install(FILES packaging/org.relayterminal.Relay.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES README.md packaging/copyright DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/relay)
    include(packaging/cpack.cmake)
endif()

if(BUILD_TESTING)
    find_package(${RELAY_QT} REQUIRED COMPONENTS Test)
    add_executable(relay-editor-tests tests/editor_test.cpp)
    target_link_libraries(relay-editor-tests PRIVATE relay-editor ${RELAY_QT}::Test)
    add_test(NAME editor COMMAND relay-editor-tests)
    set_tests_properties(editor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-filepanes-tests tests/filepanes_test.cpp)
    target_link_libraries(relay-filepanes-tests PRIVATE relay-filepanes ${RELAY_QT}::Test)
    add_test(NAME filepanes COMMAND relay-filepanes-tests)
    set_tests_properties(filepanes PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-subagents-tests tests/subagents_test.cpp)
    target_link_libraries(relay-subagents-tests PRIVATE relay-subagents ${RELAY_QT}::Test)
    add_test(NAME subagents COMMAND relay-subagents-tests)
    set_tests_properties(subagents PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-completion-tests tests/completion_test.cpp)
    target_link_libraries(relay-completion-tests PRIVATE relay-completion ${RELAY_QT}::Test)
    add_test(NAME completion COMMAND relay-completion-tests)

    add_executable(relay-requests-tests tests/requests_test.cpp)
    target_link_libraries(relay-requests-tests PRIVATE relay-requests ${RELAY_QT}::Test)
    add_test(NAME requests COMMAND relay-requests-tests)
    set_tests_properties(requests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-windowstate-tests tests/windowstate_test.cpp)
    target_link_libraries(relay-windowstate-tests PRIVATE relay-windowstate ${RELAY_QT}::Test)
    add_test(NAME windowstate COMMAND relay-windowstate-tests)
    add_executable(relay-input-tests tests/inputpolicy_test.cpp)
    target_link_libraries(relay-input-tests PRIVATE relay-input ${RELAY_QT}::Test)
    add_test(NAME input COMMAND relay-input-tests)
    add_executable(relay-screen-tests tests/screenprompt_test.cpp)
    target_link_libraries(relay-screen-tests PRIVATE relay-screen ${RELAY_QT}::Test)
    target_compile_definitions(relay-screen-tests PRIVATE
        RELAY_SCREEN_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/screen")
    add_test(NAME screen COMMAND relay-screen-tests)
    add_executable(relay-logging-tests tests/logging_test.cpp)
    target_link_libraries(relay-logging-tests PRIVATE relay-logging ${RELAY_QT}::Test)
    add_test(NAME logging COMMAND relay-logging-tests)
    add_executable(relay-conversations-tests tests/conversations_test.cpp)
    target_link_libraries(relay-conversations-tests PRIVATE relay-conversations ${RELAY_QT}::Test)
    add_test(NAME conversations COMMAND relay-conversations-tests)
    set_tests_properties(conversations PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-panes-tests tests/panelayout_test.cpp)
    target_link_libraries(relay-panes-tests PRIVATE relay-panes ${RELAY_QT}::Test)
    add_test(NAME panes COMMAND relay-panes-tests)
    set_tests_properties(panes PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-backends-tests tests/backends_test.cpp)
    target_link_libraries(relay-backends-tests PRIVATE relay-backends ${RELAY_QT}::Test)
    add_test(NAME backends COMMAND relay-backends-tests)
    set_tests_properties(backends PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-notifications-tests tests/notifications_test.cpp)
    target_link_libraries(relay-notifications-tests PRIVATE relay-notifications ${RELAY_QT}::Test)
    add_test(NAME notifications COMMAND relay-notifications-tests)
    set_tests_properties(notifications PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-voice-tests tests/voice_test.cpp)
    target_link_libraries(relay-voice-tests PRIVATE relay-voice ${RELAY_QT}::Test)
    add_test(NAME voice COMMAND relay-voice-tests)
    set_tests_properties(voice PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-hints-tests tests/hints_test.cpp src/Hints.cpp)
    target_include_directories(relay-hints-tests PRIVATE src)
    target_link_libraries(relay-hints-tests PRIVATE ${RELAY_QT}::Test)
    add_test(NAME hints COMMAND relay-hints-tests)
    set_tests_properties(hints PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME backend-and-bash COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -v)
    # XDG_DATA_HOME keeps session autosaves and the conversation index out of the real profile.
    set_tests_properties(backend-and-bash PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/backend;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-data-home;RELAY_KEYRING=off" TIMEOUT 120)
endif()
