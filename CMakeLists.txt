cmake_minimum_required(VERSION 3.22)
project(Relay VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CTest)
option(RELAY_BUILD_APP "Build the Relay application" ON)
set(RELAY_QT_MAJOR "AUTO" CACHE STRING "Qt/KDE Frameworks major version: AUTO, 6, or 5")
set_property(CACHE RELAY_QT_MAJOR PROPERTY STRINGS AUTO 6 5)
if(RELAY_QT_MAJOR STREQUAL "AUTO")
    # Probe config files without find_package(), which would fix QT_MAJOR_VERSION too early.
    #
    # Qt 5 wins when it is installed. Until KonsolePart was retired (2026-09-18) that was decided
    # for us — Qt 6 was only chosen when KF6Parts was there too. Qt 6 builds, tests and runs as of
    # 2026-09-18 (card #WV4V; Ubuntu 26.04 x86_64 and aarch64), so -DRELAY_QT_MAJOR=6 is a real
    # choice — but Qt 5 stays the default here because that is what the desktop has been run on.
    find_file(RELAY_QT5_CONFIG Qt5Config.cmake PATH_SUFFIXES cmake/Qt5 lib/cmake/Qt5 lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/Qt5)
    find_file(RELAY_QT6_CONFIG Qt6Config.cmake PATH_SUFFIXES cmake/Qt6 lib/cmake/Qt6 lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/Qt6)
    if(RELAY_QT5_CONFIG OR NOT RELAY_QT6_CONFIG)
        set(RELAY_QT_MAJOR 5)
    else()
        set(RELAY_QT_MAJOR 6)
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
# KDE Frameworks is no longer required — KonsolePart was retired on 2026-09-18 — but the
# optional file-preview highlighter still comes from it when it happens to be installed.
set(RELAY_KF KF${RELAY_QT_MAJOR})
message(STATUS "Relay: building against ${RELAY_QT}")
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

# The prompt box's history files: `$XDG_DATA_HOME/relay/state/prompt-history/<pane-id>.txt`, one
# per pane and kept across restarts. QtCore only, so it is tested headless.
add_library(relay-prompthistory STATIC src/PromptHistory.cpp src/PromptHistory.h)
target_include_directories(relay-prompthistory PUBLIC src)
target_link_libraries(relay-prompthistory PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-prompthistory PRIVATE -Wall -Wextra -Wpedantic)

add_library(relay-editor STATIC src/RichEditor.cpp src/RichEditor.h)
target_include_directories(relay-editor PUBLIC src)
target_link_libraries(relay-editor PUBLIC relay-prompthistory ${RELAY_QT}::Widgets)
target_compile_options(relay-editor PRIVATE -Wall -Wextra -Wpedantic)

# Switchboard pane: the card board, its detail view and the per-window Switchboard worker.
# BoardModel holds the logic (tabs, columns, filters, search) so it can be tested headless.
add_library(relay-board STATIC src/BoardModel.cpp src/BoardModel.h src/BoardPane.cpp src/BoardPane.h
    src/BoardWorker.cpp src/BoardWorker.h src/BoardWorkspace.cpp src/BoardWorkspace.h)
target_include_directories(relay-board PUBLIC src)
# relay-toollabel: the cleanup run's progress line is the call's concise line (protocol section 23).
# relay-projects: boardRootFor() asks projects::boardDirOf() which folder of a directory is a board,
# so the GUI's walk and the registry's can never disagree (#JN7X). The dependency only goes this
# way; relay-projects stays QtCore-only and knows nothing about panes.
target_link_libraries(relay-board PUBLIC relay-editor relay-toollabel relay-projects ${RELAY_QT}::Widgets)
target_compile_options(relay-board PRIVATE -Wall -Wextra -Wpedantic)

# Which project a pane is in, where that project's Switchboard folder is (or would be), and the
# removable registry of known projects under `$XDG_DATA_HOME/relay/state/projects.json`. QtCore only
# and deliberately not part of relay-board, which links Widgets: candidateFor() is called on every
# cwd change and must stay a filesystem walk with no git subprocess and no GUI behind it.
add_library(relay-projects STATIC src/Projects.cpp src/Projects.h src/ProjectInit.cpp src/ProjectInit.h)
target_include_directories(relay-projects PUBLIC src)
target_link_libraries(relay-projects PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-projects PRIVATE -Wall -Wextra -Wpedantic)

# Folder explorer and file preview panes: plain Qt, with optional syntax highlighting and PDF.
# The two provider modals (API keys, model roles): a static library so the roles dialog's lists can be
# tested headlessly, exactly like the settings pane below.
add_library(relay-modelsettings STATIC src/ModelSettings.cpp src/ModelSettings.h)
target_include_directories(relay-modelsettings PUBLIC src)
target_link_libraries(relay-modelsettings PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-modelsettings PRIVATE -Wall -Wextra -Wpedantic)

add_library(relay-settings STATIC src/SettingsPane.cpp src/SettingsPane.h)
target_include_directories(relay-settings PUBLIC src)
target_link_libraries(relay-settings PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-settings PRIVATE -Wall -Wextra -Wpedantic)

# Settings › Local models (card #24XJ): the section's rows and the worker traffic behind them,
# with no widgets of its own, so the same headless test can drive it.
add_library(relay-localmodels STATIC src/LocalModelsSettings.cpp src/LocalModelsSettings.h)
target_include_directories(relay-localmodels PUBLIC src)
target_link_libraries(relay-localmodels PUBLIC relay-settings ${RELAY_QT}::Widgets)
target_compile_options(relay-localmodels PRIVATE -Wall -Wextra -Wpedantic)

add_library(relay-filepanes STATIC src/FilePanes.cpp src/FilePanes.h)
target_include_directories(relay-filepanes PUBLIC src)
target_link_libraries(relay-filepanes PUBLIC relay-remotefiles relay-hints ${RELAY_QT}::Widgets)
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
add_library(relay-subagents STATIC src/SubagentsPanel.cpp src/SubagentsPanel.h src/SubagentTranscript.cpp src/SubagentTranscript.h
            src/JobsPanel.cpp src/JobsPanel.h)
target_include_directories(relay-subagents PUBLIC src)
# relay-toollabel: the transcript draws one concise line per tool call (protocol section 23).
# relay-requests: the strip under the composer pairs the running agents with the open task list
# (owner, 2026-09-19), so it reads the same RequestLedgerModel the task list panel does.
target_link_libraries(relay-subagents PUBLIC relay-toollabel relay-requests ${RELAY_QT}::Widgets)
target_compile_options(relay-subagents PRIVATE -Wall -Wextra -Wpedantic)

# Terminal engine settings (--engine-core / RELAY_ENGINE_CORE) and the pane's right-click menu:
# plain Qt, no engine linked in, so the rules can be tested on their own.
# src/BackendFactory.cpp adds the implementation to the app.
add_library(relay-backends STATIC src/TerminalBackends.cpp src/TerminalBackends.h)
target_include_directories(relay-backends PUBLIC src)
target_link_libraries(relay-backends PUBLIC ${RELAY_QT}::Core)
# The engine library is always built (see add_subdirectory(engine) below), so every build of
# the app has a terminal.
target_compile_definitions(relay-backends PUBLIC RELAY_HAVE_ENGINE)
target_compile_options(relay-backends PRIVATE -Wall -Wextra -Wpedantic)

# Saved window layout ("reopen where I left off"): reading/writing state/windows.json, screen
# clamping and directory fallbacks. Plain QtCore so the rules can be tested without a window.
# src/ClosedStack.* is the recently-closed list (state/closed.json): same directory, same node
# shapes, same atomic write, so it lives in the same library.
add_library(relay-windowstate STATIC src/WindowState.cpp src/WindowState.h src/ClosedStack.cpp src/ClosedStack.h)
target_include_directories(relay-windowstate PUBLIC src)
target_link_libraries(relay-windowstate PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-windowstate PRIVATE -Wall -Wextra -Wpedantic)

# The "Recently closed" list widget (the session manager's second tab). Fed records and callbacks,
# so it is tested offscreen without a window.
add_library(relay-closedlist STATIC src/ClosedList.cpp src/ClosedList.h)
target_include_directories(relay-closedlist PUBLIC src)
target_link_libraries(relay-closedlist PUBLIC relay-windowstate ${RELAY_QT}::Widgets)
target_compile_options(relay-closedlist PRIVATE -Wall -Wextra -Wpedantic)

# Private runtime directories (/tmp/relay-XXXXXX, /tmp/relay-open-XXXXXX): the owner mark that says
# which Relay is using one, and the startup sweep that removes the ones a crash left behind.
add_library(relay-runtimedirs STATIC src/RuntimeDirs.cpp src/RuntimeDirs.h)
target_include_directories(relay-runtimedirs PUBLIC src)
target_link_libraries(relay-runtimedirs PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-runtimedirs PRIVATE -Wall -Wextra -Wpedantic)

# Request ledger UI: model for requests/todos events and the expanded request list (plain Qt).
add_library(relay-completion STATIC src/Completion.cpp src/Completion.h)
target_include_directories(relay-completion PUBLIC src)
target_link_libraries(relay-completion PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-completion PRIVATE -Wall -Wextra -Wpedantic)

# The `@` file picker's index: the git listing behind it, run asynchronously so typing `@` in a
# large repository never blocks the window. Plain QtCore, driven from a test without a pane.
add_library(relay-fileindex STATIC src/FileIndex.cpp src/FileIndex.h)
target_include_directories(relay-fileindex PUBLIC src)
target_link_libraries(relay-fileindex PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-fileindex PRIVATE -Wall -Wextra -Wpedantic)

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

# Slash commands: whether a submitted line is an attempt at a Relay command (and not a path the
# shell should keep), and what Relay answers when the command does not exist. Pure rules, with an
# injectable filesystem probe so `/tmp` and `/usr/bin/foo` are testable anywhere.
add_library(relay-slash STATIC src/SlashCommands.cpp src/SlashCommands.h)
target_include_directories(relay-slash PUBLIC src)
target_link_libraries(relay-slash PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-slash PRIVATE -Wall -Wextra -Wpedantic)

# Screen-text input detection: "is the program waiting for a line?" read off the last rows of
# the screen (issues/features/2026-09-17-screen-text-input-detection.md). Pure text rules.
add_library(relay-screen STATIC src/ScreenPrompt.cpp src/ScreenPrompt.h)
target_include_directories(relay-screen PUBLIC src)
target_link_libraries(relay-screen PUBLIC relay-input ${RELAY_QT}::Core)
target_compile_options(relay-screen PRIVATE -Wall -Wextra -Wpedantic)

# The session manager pane (/resume, Ctrl+Shift+Y: every session and, on request, every subagent
# thread, searchable), the Ctrl+F find bar and the ⓘ conversation info view. The worker does the
# searching (docs/AGENT-SESSIONS-PROTOCOL.md sections 14 and 25); these are the views and helpers.
add_library(relay-conversations STATIC src/Conversations.cpp src/Conversations.h
    src/SessionInfo.cpp src/SessionInfo.h src/PaneView.h)
target_include_directories(relay-conversations PUBLIC src)
target_link_libraries(relay-conversations PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-conversations PRIVATE -Wall -Wextra -Wpedantic)

# Notification centre: the list behind the bell in the window header. Plain QtCore so the
# rules (order, unread count, cap) can be tested without a window.
add_library(relay-notifications STATIC src/Notifications.cpp src/Notifications.h)
target_include_directories(relay-notifications PUBLIC src)
target_link_libraries(relay-notifications PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-notifications PRIVATE -Wall -Wextra -Wpedantic)

# Queue navigation: when Up and Down move between queued items and when they belong to the text of
# the item being edited in the prompt box. Pure rules, no widgets.
add_library(relay-queuenav STATIC src/QueueNav.cpp src/QueueNav.h)
target_include_directories(relay-queuenav PUBLIC src)
target_link_libraries(relay-queuenav PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-queuenav PRIVATE -Wall -Wextra -Wpedantic)

# Queue submit policy (#N8VK): when a submitted agent prompt starts its turn at once and when it
# queues. Pure rules, no widgets.
add_library(relay-queuesubmit STATIC src/QueueSubmit.cpp src/QueueSubmit.h)
target_include_directories(relay-queuesubmit PUBLIC src)
target_link_libraries(relay-queuesubmit PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-queuesubmit PRIVATE -Wall -Wextra -Wpedantic)

# pane_state (relay-terminal-71): a pane as a paired phone draws it, and the publisher that
# coalesces its changes. Plain inputs in, the v1 JSON out; QtCore only, so it is tested headless.
add_library(relay-panestate STATIC src/PaneState.cpp src/PaneState.h)
target_include_directories(relay-panestate PUBLIC src)
target_link_libraries(relay-panestate PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-panestate PRIVATE -Wall -Wextra -Wpedantic)

# The opt-in cap on the programs that leave their pane (card #Y4RX): tmux and Chrome move
# themselves into their own systemd scope under app.slice, so per-pane limits never see them. The
# drop-in files this writes (and removes) are the owner's opt-in mitigation, and are machine-wide
# for those two programs. QtCore only, so the install/remove logic is tested headless against a
# temporary config root.
add_library(relay-escapees STATIC src/EscapeeCaps.cpp src/EscapeeCaps.h)
target_include_directories(relay-escapees PUBLIC src)
target_link_libraries(relay-escapees PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-escapees PRIVATE -Wall -Wextra -Wpedantic)

# Pane movement: which pane is the neighbour in a direction, how two panes swap inside their
# splitter, and which edge a dropped pane lands on. Plain Qt widgets, no terminal.
add_library(relay-panes STATIC src/PaneLayout.cpp src/PaneLayout.h)
target_include_directories(relay-panes PUBLIC src)
target_link_libraries(relay-panes PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-panes PRIVATE -Wall -Wextra -Wpedantic)

# Image context (issue EM1E): where a pasted, dropped or captured image is written, what counts as
# an image, the `@` token the composer gets and the cache sweep. Plain Qt Gui so every rule can be
# tested without a window; the worker attaches the file and picks the model (protocol 17).
add_library(relay-images STATIC src/Images.cpp src/Images.h)
target_include_directories(relay-images PUBLIC src)
target_link_libraries(relay-images PUBLIC ${RELAY_QT}::Gui)
target_compile_options(relay-images PRIVATE -Wall -Wextra -Wpedantic)

# Pane titles and tab labels (issue JRWQ): tidying a model-written title, deciding whether two
# panes are on the same work without a model, and joining/shortening a tab label. Plain QtCore so
# the rules can be tested without a window; the worker writes the titles (protocol 18).
add_library(relay-titles STATIC src/PaneTitles.cpp src/PaneTitles.h)
target_include_directories(relay-titles PUBLIC src)
target_link_libraries(relay-titles PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-titles PRIVATE -Wall -Wextra -Wpedantic)
# Pane types and pane states (#SPBN, #XM0T): the header band's tints, the state a pane is in, the
# most urgent one a tab shows, and the host of an ssh/mosh/telnet session. Rules only; the painting
# is in src/PaneChrome.h.
add_library(relay-panestatus STATIC src/PaneStatus.cpp src/PaneStatus.h)
target_include_directories(relay-panestatus PUBLIC src)
target_link_libraries(relay-panestatus PUBLIC ${RELAY_QT}::Gui)
target_compile_options(relay-panestatus PRIVATE -Wall -Wextra -Wpedantic)
# A pane's share of the machine (issue #D03W): /proc counters of the shell's and the worker's
# process trees, turned into CPU / memory percentages. QtCore only, so tests need no window.
add_library(relay-paneusage STATIC src/PaneUsage.cpp src/PaneUsage.h)
target_include_directories(relay-paneusage PUBLIC src)
target_link_libraries(relay-paneusage PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-paneusage PRIVATE -Wall -Wextra -Wpedantic)

# SSH and mosh sessions (card #S5SH, docs/SSH-AND-MOSH.md): which host a login reached, per
# `ssh -G`, and the line that loads shell/remote-integration.sh into the remote shell.
add_library(relay-remotesession STATIC src/RemoteSession.cpp src/RemoteSession.h)
target_include_directories(relay-remotesession PUBLIC src)
target_link_libraries(relay-remotesession PUBLIC ${RELAY_QT}::Core ${RELAY_QT}::Network)
target_compile_options(relay-remotesession PRIVATE -Wall -Wextra -Wpedantic)
# The shortcut-hint registry (WARP.md, "Shortcut hints"): how often a hint has been shown, when it
# may be shown again, and the "Next time: …" wording. Plain QtCore over QSettings — a library
# rather than a file in the app, so a pane widget can teach its own shortcut too (#S5SH).
add_library(relay-hints STATIC src/Hints.cpp src/Hints.h)
target_include_directories(relay-hints PUBLIC src)
target_link_libraries(relay-hints PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-hints PRIVATE -Wall -Wextra -Wpedantic)
# Files on the host a pane is logged into (#S5SH, docs/SSH-AND-MOSH.md section 9): the shell
# quoting and the remote scripts that fetch and save one file over the user's own ssh connection,
# and the batched `test -e` cache that says which paths in the output are the host's. The quoting,
# the scripts and the "it changed under us" decision are plain rules; only QProcess touches ssh.
add_library(relay-remotefiles STATIC src/RemoteFiles.cpp src/RemoteFiles.h)
target_include_directories(relay-remotefiles PUBLIC src)
target_link_libraries(relay-remotefiles PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-remotefiles PRIVATE -Wall -Wextra -Wpedantic)
# SSH (#S5SH, docs/SSH-AND-MOSH.md section 8): the concrete hosts of ~/.ssh/config and its
# Includes for "Connect to host…", the recently used hosts, and the ssh/mosh command line that
# "Split on the same host" runs again. Plain QtCore, tested against a temporary ~/.ssh.
add_library(relay-sshconfig STATIC src/SshConfig.cpp src/SshConfig.h)
target_include_directories(relay-sshconfig PUBLIC src)
target_link_libraries(relay-sshconfig PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-sshconfig PRIVATE -Wall -Wextra -Wpedantic)
# Clickable paths in terminal output (#YZTK) and the keyboard walk over them (#GWXM):
# which spans of a line are files, folders or URLs, what they resolve to against a pane's
# working directory, and the cursor that steps through the ordered list. Plain QtCore with
# an injectable filesystem probe, so every format and every false positive is testable.
add_library(relay-outputlinks STATIC src/OutputLinks.cpp src/OutputLinks.h)
target_include_directories(relay-outputlinks PUBLIC src)
target_link_libraries(relay-outputlinks PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-outputlinks PRIVATE -Wall -Wextra -Wpedantic)

# Agent replies in the terminal: streaming Markdown to ANSI (headings, emphasis, code, lists,
# quotes, links, tables), word-wrapped to the pane's width. Plain QtCore so chunk boundaries can
# be tested without a terminal.
add_library(relay-markdown STATIC src/MarkdownAnsi.cpp src/MarkdownAnsi.h src/WordWrap.cpp src/WordWrap.h)
target_include_directories(relay-markdown PUBLIC src)
target_link_libraries(relay-markdown PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-markdown PRIVATE -Wall -Wextra -Wpedantic)

# Aliases: saved commands and prompts (issue G8DK): turning an alias's template into composer
# fields, walking them with Tab, and deciding whether a typed line names an alias. Plain QtCore so
# every rule is testable without a window; the worker owns the files and the shell-safe parameter
# substitution (protocol 20).
add_library(relay-aliases STATIC src/Aliases.cpp src/Aliases.h)
target_include_directories(relay-aliases PUBLIC src)
target_link_libraries(relay-aliases PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-aliases PRIVATE -Wall -Wextra -Wpedantic)

# Voice transcription (issue NY7Z): which capture tool to run, which key event is the hold key,
# where the transcript lands in the composer, and WAV repair. Plain QtCore so every rule can be
# tested without a microphone or a window; the worker does the transcribing (protocol 16).
add_library(relay-voice STATIC src/Voice.cpp src/Voice.h)
target_include_directories(relay-voice PUBLIC src)
target_link_libraries(relay-voice PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-voice PRIVATE -Wall -Wextra -Wpedantic)

# Colour themes (issue 0JA7): reading a theme file (TOML) into a ThemeSpec, the tokens a theme
# must carry, and discovery across the built-in and the user folder. Plain Qt Gui so every rule
# can be tested without a window;
# src/Theme.cpp turns the result into the live palette, stylesheet and terminal colours.
add_library(relay-theme STATIC src/ThemeFile.cpp src/ThemeFile.h)
target_include_directories(relay-theme PUBLIC src)
target_link_libraries(relay-theme PUBLIC ${RELAY_QT}::Gui)
target_compile_options(relay-theme PRIVATE -Wall -Wextra -Wpedantic)

# Prompt-box colouring: shell syntax when the line is going to the shell, plain text for an agent
# prompt, and the caret colour that shows which (owner design, 2026-09-17).
add_library(relay-highlight STATIC src/ShellHighlighter.cpp src/ShellHighlighter.h src/Theme.cpp src/Theme.h)
target_include_directories(relay-highlight PUBLIC src)
target_link_libraries(relay-highlight PUBLIC ${RELAY_QT}::Gui ${RELAY_QT}::Widgets relay-theme)
target_compile_options(relay-highlight PRIVATE -Wall -Wextra -Wpedantic)

# Unified diffs from the agent's file tools: the parser the concise tool-call notification and the
# diff pane share, and the read-only viewer. relay-highlight carries src/Theme.cpp, so the add and
# remove tints are derived from the live tokens and follow a theme switch.
add_library(relay-diffview STATIC src/DiffView.cpp src/DiffView.h)
target_include_directories(relay-diffview PUBLIC src)
target_link_libraries(relay-diffview PUBLIC relay-highlight ${RELAY_QT}::Widgets)
target_compile_options(relay-diffview PRIVATE -Wall -Wextra -Wpedantic)

# The tool-call `label` (docs/AGENT-SESSIONS-PROTOCOL.md section 23) turned into the one concise
# line every surface prints: the terminal pane, the subagent transcript and the turn pane share it.
# QtCore only -- no widget, no theme -- so the rules are tested headless.
add_library(relay-toollabel STATIC src/ToolLabel.cpp src/ToolLabel.h)
target_include_directories(relay-toollabel PUBLIC src)
target_link_libraries(relay-toollabel PUBLIC ${RELAY_QT}::Core)
target_compile_options(relay-toollabel PRIVATE -Wall -Wextra -Wpedantic)

# The terminal pane's tool-call lines (card #TK9C): the anchor URI, the row and how far it may be
# cut, the rewrite-or-new-row state machine, and a fold's rows. src/Pane.h is the glue that writes
# the bytes; everything it decides first lives here, so it is tested without a terminal. engine/ is
# on the include path for relay::FoldLine alone (a declaration -- the engine is not linked in).
add_library(relay-calllines STATIC src/CallLines.cpp src/CallLines.h)
target_include_directories(relay-calllines PUBLIC src ${CMAKE_CURRENT_SOURCE_DIR}/engine)
target_link_libraries(relay-calllines PUBLIC relay-toollabel relay-diffview relay-markdown)
target_compile_options(relay-calllines PRIVATE -Wall -Wextra -Wpedantic)

add_library(relay-requests STATIC src/RequestLedger.cpp src/RequestLedger.h src/RequestsPanel.cpp src/RequestsPanel.h)
target_include_directories(relay-requests PUBLIC src)
# relay-calllines for the one status-glyph table the fold and this panel share (card #BDXG).
target_link_libraries(relay-requests PUBLIC ${RELAY_QT}::Widgets relay-calllines)
target_compile_options(relay-requests PRIVATE -Wall -Wextra -Wpedantic)

# Multiplayer, the owner's side (#W5N2): the Sharing pane and the model behind it. The model eats
# the sidecar's line JSON and holds no widgets, so tests/sharingpane_test.cpp runs it without a hub.
add_library(relay-sharing STATIC src/SharingPane.cpp src/SharingPane.h)
target_include_directories(relay-sharing PUBLIC src)
target_link_libraries(relay-sharing PUBLIC ${RELAY_QT}::Widgets)
target_compile_options(relay-sharing PRIVATE -Wall -Wextra -Wpedantic)

# Relay-to-Relay: a pane shared by another desktop, opened here as the owner's own device — the
# screen painter, the pane_state view, and the `remote/viewer.py` sidecar that holds the session.
add_library(relay-remotepane STATIC src/RemotePane.cpp src/RemotePane.h)
target_include_directories(relay-remotepane PUBLIC src)
target_link_libraries(relay-remotepane PUBLIC relay-highlight relay-hints ${RELAY_QT}::Widgets)
target_compile_definitions(relay-remotepane PRIVATE
    RELAY_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/relay" RELAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(relay-remotepane PRIVATE -Wall -Wextra -Wpedantic)

# Relay's own terminal engine (engine/, docs/ENGINE.md). The library is built and linked
# into the app (RELAY_HAVE_ENGINE, --engine=relay); the manual harness and the benchmark
# are extra targets behind RELAY_BUILD_ENGINE.
option(RELAY_BUILD_ENGINE "Also build the engine harness (relay-vterm-spike) and benchmark" OFF)
option(RELAY_BUILD_ENGINE_SPIKE "Deprecated alias for RELAY_BUILD_ENGINE" OFF)
add_subdirectory(engine)

if(RELAY_BUILD_APP)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Relay v0.1 targets Linux (/proc and the Bash integration).")
    endif()
    # main.cpp was split into one header per unit on 2026-09-18 (scripts/split-main.py); it is
    # still one translation unit -- main.cpp includes them -- and they are listed so AUTOMOC and
    # IDEs see them.
    add_executable(relay src/main.cpp
        src/AppPaths.h src/CopyOnSelect.h src/Keymap.h src/Isolation.h src/GuestBridge.h src/Pane.h src/PaneChrome.h
        src/WindowChrome.h src/RelayWindow.h src/WindowManagerImpl.h
        src/Theme.cpp src/Theme.h src/AgentUi.cpp src/AgentUi.h src/TurnTranscript.cpp src/TurnTranscript.h src/SkillsDialog.cpp src/SkillsDialog.h
        src/EngineBackend.cpp src/EngineBackend.h src/BackendFactory.cpp
        src/RemoteShare.cpp src/RemoteShare.h)
    find_package(${RELAY_QT} REQUIRED COMPONENTS DBus Network)
    target_link_libraries(relay PRIVATE relay-closedlist relay-localmodels relay-projects relay-escapees)
    target_link_libraries(relay PRIVATE relay-hints relay-modelsettings relay-settings relay-outputlinks relay-markdown relay-highlight relay-editor relay-prompthistory relay-filepanes relay-subagents relay-requests relay-conversations relay-completion relay-fileindex relay-backends relay-windowstate relay-runtimedirs relay-input relay-notifications relay-panes relay-queuenav relay-queuesubmit relay-logging relay-voice relay-images relay-titles relay-panestatus relay-paneusage relay-remotesession relay-remotefiles relay-sshconfig relay-terminal-engine relay-screen relay-slash relay-board relay-aliases relay-calllines relay-diffview relay-sharing relay-remotepane ${RELAY_QT}::DBus ${RELAY_QT}::Network)
    target_compile_definitions(relay PRIVATE
        RELAY_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/relay"
        RELAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        RELAY_VERSION="${PROJECT_VERSION}")
    target_compile_options(relay PRIVATE -Wall -Wextra -Wpedantic)
    install(TARGETS relay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    target_link_libraries(relay PRIVATE relay-panestate)   # pane_state (relay-terminal-71)
    # remote/, rendezvous/ and app/ are the phone-sharing sidecar, its relay and the web client.
    install(DIRECTORY backend shell remote rendezvous app DESTINATION ${CMAKE_INSTALL_DATADIR}/relay
        PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
    install(DIRECTORY data/theme DESTINATION ${CMAKE_INSTALL_DATADIR}/relay)
    install(FILES org.relayterminal.Relay.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(DIRECTORY data/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
    install(FILES data/icons/org.relayterminal.Relay.svg data/icons/org.relayterminal.Relay-symbolic.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    # dataRoot() prepends <data>/scripts to PATH and points RELAY_OPEN_HELPER at relay-open.
    install(PROGRAMS scripts/relay-open scripts/relay-agent.py scripts/relay-local.py scripts/relay-update.py DESTINATION ${CMAKE_INSTALL_DATADIR}/relay/scripts)
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
    add_executable(relay-prompthistory-tests tests/prompthistory_test.cpp)
    target_link_libraries(relay-prompthistory-tests PRIVATE relay-prompthistory ${RELAY_QT}::Test)
    add_test(NAME prompthistory COMMAND relay-prompthistory-tests)
    add_executable(relay-modelsettings-tests tests/modelsettings_test.cpp)
    target_link_libraries(relay-modelsettings-tests PRIVATE relay-modelsettings ${RELAY_QT}::Test)
    add_test(NAME modelsettings COMMAND relay-modelsettings-tests)
    set_tests_properties(modelsettings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-settings-tests tests/settingspane_test.cpp)
    target_link_libraries(relay-settings-tests PRIVATE relay-settings relay-localmodels ${RELAY_QT}::Test)
    # The Local models section is placed by RelayWindow::settingsSections(), which needs a whole
    # window to run; the test reads that one line as text instead (as tests/test_presets.py does).
    target_compile_definitions(relay-settings-tests PRIVATE RELAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME settings COMMAND relay-settings-tests)
    set_tests_properties(settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-filepanes-tests tests/filepanes_test.cpp)
    target_link_libraries(relay-filepanes-tests PRIVATE relay-filepanes ${RELAY_QT}::Test)
    add_test(NAME filepanes COMMAND relay-filepanes-tests)
    set_tests_properties(filepanes PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-subagents-tests tests/subagents_test.cpp)
    target_link_libraries(relay-subagents-tests PRIVATE relay-subagents ${RELAY_QT}::Test)
    add_test(NAME subagents COMMAND relay-subagents-tests)
    add_executable(relay-jobs-tests tests/jobs_test.cpp)
    target_link_libraries(relay-jobs-tests PRIVATE relay-subagents ${RELAY_QT}::Test)
    add_test(NAME jobs COMMAND relay-jobs-tests)
    set_tests_properties(jobs PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # The strip under the composer pairs subagents with the open tasks (owner, 2026-09-19): the
    # window centred on the marginal task and the row pairing are pure functions, tested without a
    # window.
    add_executable(relay-striplayout-tests tests/striplayout_test.cpp)
    target_link_libraries(relay-striplayout-tests PRIVATE relay-subagents ${RELAY_QT}::Test)
    add_test(NAME striplayout COMMAND relay-striplayout-tests)
    set_tests_properties(striplayout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(subagents PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-completion-tests tests/completion_test.cpp)
    target_link_libraries(relay-completion-tests PRIVATE relay-completion ${RELAY_QT}::Test)
    add_test(NAME completion COMMAND relay-completion-tests)
    add_executable(relay-fileindex-tests tests/fileindex_test.cpp)
    target_link_libraries(relay-fileindex-tests PRIVATE relay-fileindex ${RELAY_QT}::Test)
    add_test(NAME fileindex COMMAND relay-fileindex-tests)

    add_executable(relay-requests-tests tests/requests_test.cpp)
    target_link_libraries(relay-requests-tests PRIVATE relay-requests ${RELAY_QT}::Test)
    add_test(NAME requests COMMAND relay-requests-tests)
    add_executable(relay-sharing-tests tests/sharingpane_test.cpp)
    target_link_libraries(relay-sharing-tests PRIVATE relay-sharing ${RELAY_QT}::Test)
    add_test(NAME sharing COMMAND relay-sharing-tests)
    set_tests_properties(sharing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-remotepane-tests tests/remotepane_test.cpp)
    target_link_libraries(relay-remotepane-tests PRIVATE relay-remotepane ${RELAY_QT}::Test)
    add_test(NAME remotepane COMMAND relay-remotepane-tests)
    set_tests_properties(remotepane PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RELAY_KEYRING=off")
    set_tests_properties(requests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-windowstate-tests tests/windowstate_test.cpp)
    target_link_libraries(relay-windowstate-tests PRIVATE relay-windowstate ${RELAY_QT}::Test)
    add_test(NAME windowstate COMMAND relay-windowstate-tests)
    add_executable(relay-closedstack-tests tests/closedstack_test.cpp)
    target_link_libraries(relay-closedstack-tests PRIVATE relay-windowstate ${RELAY_QT}::Test)
    add_test(NAME closedstack COMMAND relay-closedstack-tests)
    add_executable(relay-closedlist-tests tests/closedlist_test.cpp)
    target_link_libraries(relay-closedlist-tests PRIVATE relay-closedlist ${RELAY_QT}::Test)
    add_test(NAME closedlist COMMAND relay-closedlist-tests)
    set_tests_properties(closedlist PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-runtimedirs-tests tests/runtimedirs_test.cpp)
    target_link_libraries(relay-runtimedirs-tests PRIVATE relay-runtimedirs ${RELAY_QT}::Test)
    add_test(NAME runtimedirs COMMAND relay-runtimedirs-tests)
    # The IDE bridge's C++ rules (GT7X, 26.5), header-only beside the pane: which reply files the
    # GUI may write, and the shell environment a claude pane injects.
    add_executable(relay-guestbridge-tests tests/guestbridge_test.cpp)
    target_link_libraries(relay-guestbridge-tests PRIVATE relay-logging relay-runtimedirs ${RELAY_QT}::Test)
    add_test(NAME guestbridge COMMAND relay-guestbridge-tests)
    add_executable(relay-slash-tests tests/slashcommands_test.cpp)
    target_link_libraries(relay-slash-tests PRIVATE relay-slash ${RELAY_QT}::Test)
    add_test(NAME slash COMMAND relay-slash-tests)
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
    add_executable(relay-queuenav-tests tests/queuenav_test.cpp)
    target_link_libraries(relay-queuenav-tests PRIVATE relay-queuenav ${RELAY_QT}::Test)
    add_test(NAME queuenav COMMAND relay-queuenav-tests)
    add_executable(relay-queuesubmit-tests tests/queuesubmit_test.cpp)
    target_link_libraries(relay-queuesubmit-tests PRIVATE relay-queuesubmit ${RELAY_QT}::Test)
    add_test(NAME queuesubmit COMMAND relay-queuesubmit-tests)
    add_executable(relay-panestate-tests tests/panestate_test.cpp)
    target_link_libraries(relay-panestate-tests PRIVATE relay-panestate ${RELAY_QT}::Test)
    add_test(NAME panestate COMMAND relay-panestate-tests)
    add_executable(relay-escapees-tests tests/escapeecaps_test.cpp)
    target_link_libraries(relay-escapees-tests PRIVATE relay-escapees ${RELAY_QT}::Test)
    add_test(NAME escapees COMMAND relay-escapees-tests)
    add_executable(relay-panes-tests tests/panelayout_test.cpp)
    target_link_libraries(relay-panes-tests PRIVATE relay-panes ${RELAY_QT}::Test)
    add_test(NAME panes COMMAND relay-panes-tests)
    set_tests_properties(panes PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-outputlinks-tests tests/outputlinks_test.cpp)
    target_link_libraries(relay-outputlinks-tests PRIVATE relay-outputlinks ${RELAY_QT}::Test)
    add_test(NAME outputlinks COMMAND relay-outputlinks-tests)
    add_executable(relay-markdown-tests tests/markdownansi_test.cpp)
    target_link_libraries(relay-markdown-tests PRIVATE relay-markdown ${RELAY_QT}::Test)
    add_test(NAME markdown COMMAND relay-markdown-tests)
    add_executable(relay-wordwrap-tests tests/wordwrap_test.cpp)
    target_link_libraries(relay-wordwrap-tests PRIVATE relay-markdown ${RELAY_QT}::Test)
    add_test(NAME wordwrap COMMAND relay-wordwrap-tests)
    add_executable(relay-backends-tests tests/backends_test.cpp)
    target_link_libraries(relay-backends-tests PRIVATE relay-backends ${RELAY_QT}::Test)
    add_test(NAME backends COMMAND relay-backends-tests)
    set_tests_properties(backends PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-notifications-tests tests/notifications_test.cpp)
    target_link_libraries(relay-notifications-tests PRIVATE relay-notifications ${RELAY_QT}::Test)
    add_test(NAME notifications COMMAND relay-notifications-tests)
    set_tests_properties(notifications PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-theme-tests tests/theme_test.cpp)
    target_link_libraries(relay-theme-tests PRIVATE relay-theme ${RELAY_QT}::Test)
    add_test(NAME theme COMMAND relay-theme-tests)

    # The running application's end of the theme: what `theme/name` does when the theme it names is
    # gone (Solarized Dark was removed 2026-09-18), which needs Theme.cpp — relay-highlight carries it.
    add_executable(relay-themeswitch-tests tests/themeswitch_test.cpp)
    target_link_libraries(relay-themeswitch-tests PRIVATE relay-highlight ${RELAY_QT}::Test ${RELAY_QT}::Widgets)
    target_compile_definitions(relay-themeswitch-tests PRIVATE RELAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME themeswitch COMMAND relay-themeswitch-tests)
    # It builds a QApplication and real widgets, so it needs a platform plugin: without one it does
    # not fail, it aborts before the first test ("Subprocess aborted", 0.07 s in CI). A developer
    # running ctest under xvfb-run never sees it; CI has no display at all.
    set_tests_properties(themeswitch PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # The test also reads the shipped theme files, so it runs from the source tree.
    set_tests_properties(theme PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Renders every button label in the live stylesheet and fails on one that paints past its edge.
    add_executable(relay-buttonfit-tests tests/buttonfit_test.cpp)
    target_link_libraries(relay-buttonfit-tests PRIVATE relay-highlight ${RELAY_QT}::Test)
    target_compile_definitions(relay-buttonfit-tests PRIVATE RELAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME buttonfit COMMAND relay-buttonfit-tests)
    # Reads the shipped theme files (and the sources it scans) from the source tree.
    set_tests_properties(buttonfit PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Card #V8KT: renders the live pane-state marks (relay::chrome::tabIcon, paintStateGlyph)
    # at every step of the pulse and fails when a step stops changing the painted pixels. It
    # includes PaneChrome.h, which is the application's own translation unit, so it links the
    # app's libraries rather than one small one.
    add_executable(relay-pulsepaint-tests tests/pulsepaint_test.cpp tests/pulsepaint_paint.cpp)
    target_link_libraries(relay-pulsepaint-tests PRIVATE relay-closedlist relay-localmodels
        relay-projects relay-hints relay-modelsettings relay-settings relay-outputlinks relay-markdown
        relay-highlight relay-editor relay-prompthistory relay-filepanes relay-subagents
        relay-requests relay-conversations relay-completion relay-fileindex relay-backends
        relay-windowstate relay-runtimedirs relay-input relay-notifications relay-panes
        relay-queuenav relay-queuesubmit relay-logging relay-voice relay-images relay-titles relay-panestatus relay-paneusage
        relay-remotesession relay-remotefiles relay-sshconfig relay-terminal-engine relay-screen
        relay-slash relay-board relay-aliases relay-calllines relay-diffview relay-sharing
        relay-remotepane relay-panestate ${RELAY_QT}::Widgets ${RELAY_QT}::Test)
    add_test(NAME pulsepaint COMMAND relay-pulsepaint-tests)
    # Renders into pixmaps, so it needs a platform plugin like buttonfit does.
    set_tests_properties(pulsepaint PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-titles-tests tests/panetitles_test.cpp)
    target_link_libraries(relay-titles-tests PRIVATE relay-titles ${RELAY_QT}::Test)
    add_test(NAME titles COMMAND relay-titles-tests)
    add_executable(relay-panestatus-tests tests/panestatus_test.cpp)
    target_link_libraries(relay-panestatus-tests PRIVATE relay-panestatus relay-theme ${RELAY_QT}::Test)
    add_test(NAME panestatus COMMAND relay-panestatus-tests)
    add_executable(relay-paneusage-tests tests/paneusage_test.cpp)
    target_link_libraries(relay-paneusage-tests PRIVATE relay-paneusage ${RELAY_QT}::Test)
    add_test(NAME paneusage COMMAND relay-paneusage-tests)
    # Reads the shipped theme files from the source tree.
    set_tests_properties(panestatus PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(relay-remotesession-tests tests/remotesession_test.cpp)
    target_link_libraries(relay-remotesession-tests PRIVATE relay-remotesession ${RELAY_QT}::Test)
    add_test(NAME remotesession COMMAND relay-remotesession-tests)
    add_executable(relay-remotefiles-tests tests/remotefiles_test.cpp)
    target_link_libraries(relay-remotefiles-tests PRIVATE relay-remotefiles ${RELAY_QT}::Test)
    add_test(NAME remotefiles COMMAND relay-remotefiles-tests)
    add_executable(relay-sshconfig-tests tests/sshconfig_test.cpp)
    target_link_libraries(relay-sshconfig-tests PRIVATE relay-sshconfig ${RELAY_QT}::Test)
    add_test(NAME sshconfig COMMAND relay-sshconfig-tests)
    add_executable(relay-voice-tests tests/voice_test.cpp)
    target_link_libraries(relay-voice-tests PRIVATE relay-voice ${RELAY_QT}::Test)
    add_test(NAME voice COMMAND relay-voice-tests)
    set_tests_properties(voice PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-aliases-tests tests/aliases_test.cpp)
    target_link_libraries(relay-aliases-tests PRIVATE relay-aliases ${RELAY_QT}::Test)
    add_test(NAME aliases COMMAND relay-aliases-tests)
    add_executable(relay-images-tests tests/images_test.cpp)
    target_link_libraries(relay-images-tests PRIVATE relay-images ${RELAY_QT}::Test)
    add_test(NAME images COMMAND relay-images-tests)
    set_tests_properties(images PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-hints-tests tests/hints_test.cpp)
    target_link_libraries(relay-hints-tests PRIVATE relay-hints ${RELAY_QT}::Test)
    add_test(NAME hints COMMAND relay-hints-tests)
    set_tests_properties(hints PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-board-tests tests/boardmodel_test.cpp)
    target_link_libraries(relay-board-tests PRIVATE relay-board ${RELAY_QT}::Test)
    add_test(NAME board COMMAND relay-board-tests)
    set_tests_properties(board PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Which project's Switchboard a pane is looking at. The rule itself is pure (QTemporaryDir);
    # RelayWindow::boardWorkspace() needs a whole window, so the test reads that one function as
    # text, exactly as tests/settingspane_test.cpp reads settingsSections().
    add_executable(relay-boardworkspace-tests tests/boardworkspace_test.cpp)
    target_link_libraries(relay-boardworkspace-tests PRIVATE relay-board ${RELAY_QT}::Test)
    target_compile_definitions(relay-boardworkspace-tests PRIVATE RELAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME boardworkspace COMMAND relay-boardworkspace-tests)
    set_tests_properties(boardworkspace PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # The project model: candidate projects, the board folder, the key the backend's workspace
    # digest has to agree with, and the registry file. No widgets, so no offscreen platform.
    add_executable(relay-projects-tests tests/projects_test.cpp)
    target_link_libraries(relay-projects-tests PRIVATE relay-projects ${RELAY_QT}::Test)
    # It reads backend/relay_core/board.py to check that both languages list the board folders in
    # the same order (`boardFolders()` / `BOARD_FOLDERS`).
    target_compile_definitions(relay-projects-tests PRIVATE RELAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME projects COMMAND relay-projects-tests)
    # "Initialize a project and create a Switchboard here?": when it is asked, what the probe's
    # findings read as, and which import kinds a ticked box asks for. Pure (src/ProjectInit.h); the
    # pane and window wiring behind it is read as text, the way boardworkspace_test.cpp does.
    add_executable(relay-projectinit-tests tests/projectinit_test.cpp)
    target_link_libraries(relay-projectinit-tests PRIVATE relay-projects ${RELAY_QT}::Test)
    target_compile_definitions(relay-projectinit-tests PRIVATE RELAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME projectinit COMMAND relay-projectinit-tests)
    # Copy on highlight is header-only (src/CopyOnSelect.h); relay-diffview gives the test one
    # real pane surface that installs it.
    add_executable(relay-copyonselect-tests tests/copyonselect_test.cpp)
    target_include_directories(relay-copyonselect-tests PRIVATE src)
    target_link_libraries(relay-copyonselect-tests PRIVATE relay-diffview ${RELAY_QT}::Test)
    add_test(NAME copyonselect COMMAND relay-copyonselect-tests)
    set_tests_properties(copyonselect PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-diffview-tests tests/diffview_test.cpp)
    target_link_libraries(relay-diffview-tests PRIVATE relay-diffview ${RELAY_QT}::Test)
    add_test(NAME diffview COMMAND relay-diffview-tests)
    set_tests_properties(diffview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-toollabel-tests tests/toollabel_test.cpp)
    target_link_libraries(relay-toollabel-tests PRIVATE relay-toollabel ${RELAY_QT}::Test)
    add_test(NAME toollabel COMMAND relay-toollabel-tests)
    set_tests_properties(toollabel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(relay-calllines-tests tests/calllines_test.cpp)
    target_link_libraries(relay-calllines-tests PRIVATE relay-calllines ${RELAY_QT}::Test)
    add_test(NAME calllines COMMAND relay-calllines-tests)
    set_tests_properties(calllines PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # The turn pane is part of the application rather than a library; the test compiles it directly.
    add_executable(relay-turntranscript-tests tests/turntranscript_test.cpp src/TurnTranscript.cpp)
    target_link_libraries(relay-turntranscript-tests PRIVATE relay-toollabel relay-highlight ${RELAY_QT}::Test)
    add_test(NAME turntranscript COMMAND relay-turntranscript-tests)
    set_tests_properties(turntranscript PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME backend-and-bash COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -v)
    # The suite takes ~150 s on this machine (2026-09-18); 120 s made every ctest run report it red.
    # XDG_DATA_HOME keeps session autosaves and the conversation index out of the real profile, and
    # XDG_CONFIG_HOME keeps the developer's own settings out of the tests: with a local model
    # configured, test_roles read it and the `local` role stopped falling back to main.
    set_tests_properties(backend-and-bash PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/backend;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-data-home;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config-home;RELAY_KEYRING=off" TIMEOUT 600)
endif()
