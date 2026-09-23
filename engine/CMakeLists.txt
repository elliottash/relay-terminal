# Relay terminal engine. The library and its tests are always built; the app links it
# and selects it per pane with --engine=relay (src/EngineBackend). The manual harness
# and the benchmark need -DRELAY_BUILD_ENGINE=ON (default OFF).
#
# Targets:
#   relay-vterm-c           vendored + patched libvterm 0.3.3 (MIT), C99
#   relay-terminal-engine   static library: VtCore (ghostty / libvterm), Pty, session, view, backend
#   relay-engine-tests      unit tests (ctest), with BUILD_TESTING
#   relay-engine-bench      headless core throughput          (RELAY_BUILD_ENGINE)
#   relay-vterm-spike       manual GUI harness                (RELAY_BUILD_ENGINE)
enable_language(C)

option(RELAY_ENGINE_WITH_GHOSTTY "Build the libghostty-vt emulator core (needs a libghostty-vt prefix, see engine/scripts/build-libghostty-vt.sh)" OFF)
set(RELAY_GHOSTTY_VT_PREFIX "" CACHE PATH "Install prefix of libghostty-vt (include/ghostty/vt.h, lib/libghostty-vt.a)")

# ---- libvterm (vendored)
file(GLOB RELAY_VTERM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvterm/src/*.c)
add_library(relay-vterm-c STATIC ${RELAY_VTERM_SOURCES})
target_include_directories(relay-vterm-c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvterm/include)
set_target_properties(relay-vterm-c PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(relay-vterm-c PRIVATE -w)
endif()

# ---- libghostty-vt (prebuilt, optional)
if(RELAY_ENGINE_WITH_GHOSTTY)
    find_path(RELAY_GHOSTTY_INCLUDE_DIR ghostty/vt.h HINTS ${RELAY_GHOSTTY_VT_PREFIX}/include NO_DEFAULT_PATH)
    find_library(RELAY_GHOSTTY_LIBRARY NAMES libghostty-vt.a ghostty-vt HINTS ${RELAY_GHOSTTY_VT_PREFIX}/lib NO_DEFAULT_PATH)
    if(NOT RELAY_GHOSTTY_INCLUDE_DIR OR NOT RELAY_GHOSTTY_LIBRARY)
        message(FATAL_ERROR "RELAY_ENGINE_WITH_GHOSTTY=ON but libghostty-vt was not found under RELAY_GHOSTTY_VT_PREFIX='${RELAY_GHOSTTY_VT_PREFIX}'. Build it with engine/scripts/build-libghostty-vt.sh.")
    endif()
    # The Zig-built archive bundles compiler_rt with *global* memcpy, memset,
    # strlen, __memcpy_chk, __stack_chk_fail, ... which would silently replace
    # glibc's for every object linked after it. On Linux, partially link the
    # archive and keep only the ghostty_* API global.
    set(RELAY_GHOSTTY_LINK_LIBRARY ${RELAY_GHOSTTY_LIBRARY})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_OBJCOPY AND CMAKE_LINKER)
        set(RELAY_GHOSTTY_LINK_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/libghostty-vt-relay.a)
        add_custom_command(OUTPUT ${RELAY_GHOSTTY_LINK_LIBRARY}
            COMMAND ${CMAKE_LINKER} -r --whole-archive ${RELAY_GHOSTTY_LIBRARY} -o ghostty-vt-all.o
            COMMAND ${CMAKE_OBJCOPY} --wildcard --keep-global-symbol=ghostty_* ghostty-vt-all.o ghostty-vt-local.o
            COMMAND ${CMAKE_COMMAND} -E rm -f ${RELAY_GHOSTTY_LINK_LIBRARY} ghostty-vt-all.o
            COMMAND ${CMAKE_AR} rcs ${RELAY_GHOSTTY_LINK_LIBRARY} ghostty-vt-local.o
            DEPENDS ${RELAY_GHOSTTY_LIBRARY}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Localizing non-API symbols of libghostty-vt")
        add_custom_target(relay-ghostty-vt-localize DEPENDS ${RELAY_GHOSTTY_LINK_LIBRARY})
    else()
        message(WARNING "Relay engine: linking libghostty-vt without symbol localization (TODO for this platform)")
    endif()
    add_library(relay-ghostty-vt STATIC IMPORTED GLOBAL)
    set_target_properties(relay-ghostty-vt PROPERTIES
        IMPORTED_LOCATION ${RELAY_GHOSTTY_LINK_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${RELAY_GHOSTTY_INCLUDE_DIR})
    if(TARGET relay-ghostty-vt-localize)
        add_dependencies(relay-ghostty-vt relay-ghostty-vt-localize)
    endif()
    message(STATUS "Relay engine: libghostty-vt core enabled (${RELAY_GHOSTTY_LIBRARY})")
endif()

# ---- engine library
set(RELAY_ENGINE_SOURCES
    TerminalBackend.h
    core/AnsiSerializer.h core/AnsiSerializer.cpp
    core/CellTypes.h core/CellTypes.cpp
    core/VtCore.h core/VtCoreFactory.cpp
    core/LibVtermCore.h core/LibVtermCore.cpp
    core/SequenceScanner.h
    core/InlineImage.h core/InlineImage.cpp
    pty/Pty.h
    session/TerminalSession.h session/TerminalSession.cpp
    session/ImageProtocol.h session/ImageProtocol.cpp
    view/ColorScheme.h
    view/FaintInk.h
    view/BoxDrawing.h view/BoxDrawing.cpp
    view/FoldLayer.h view/FoldLayer.cpp
    view/FoldSearch.h view/FoldSearch.cpp
    view/ImageCache.h view/ImageCache.cpp
    view/ProseSpans.h
    view/KeyMapper.h view/KeyMapper.cpp
    view/TerminalView.h view/TerminalView.cpp
    backend/VTermBackend.h backend/VTermBackend.cpp
)
if(WIN32)
    list(APPEND RELAY_ENGINE_SOURCES pty/PtyWin.cpp)
else()
    list(APPEND RELAY_ENGINE_SOURCES pty/PtyUnix.cpp)
endif()
if(RELAY_ENGINE_WITH_GHOSTTY)
    list(APPEND RELAY_ENGINE_SOURCES core/GhosttyCore.h core/GhosttyCore.cpp)
endif()

add_library(relay-terminal-engine STATIC ${RELAY_ENGINE_SOURCES})
target_include_directories(relay-terminal-engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
# relay-outputlinks is PUBLIC: view/TerminalView.h names relay::links::Cursor.
# relay-markdown is PUBLIC: TerminalBackend.h includes src/WordWrap.h for the
# shared word-aware break rule (#R2WQ), which is QtCore-only with PUBLIC src
# includes, so this is no cycle.
target_link_libraries(relay-terminal-engine PUBLIC ${RELAY_QT}::Widgets relay-outputlinks relay-markdown PRIVATE relay-vterm-c Threads::Threads)
if(UNIX AND NOT APPLE)
    target_link_libraries(relay-terminal-engine PRIVATE util) # forkpty
endif()
if(MSVC)
    target_compile_options(relay-terminal-engine PRIVATE /W3 /utf-8)
else()
    target_compile_options(relay-terminal-engine PRIVATE -Wall -Wextra)
endif()
if(RELAY_ENGINE_WITH_GHOSTTY)
    target_link_libraries(relay-terminal-engine PRIVATE relay-ghostty-vt)
    target_compile_definitions(relay-terminal-engine PUBLIC RELAY_HAVE_GHOSTTY)
endif()

if(RELAY_BUILD_ENGINE OR RELAY_BUILD_ENGINE_SPIKE)
    add_executable(relay-engine-bench bench/EngineBench.cpp)
    target_link_libraries(relay-engine-bench PRIVATE relay-terminal-engine)

    add_executable(relay-vterm-spike main.cpp)
    target_link_libraries(relay-vterm-spike PRIVATE relay-terminal-engine)

    # Headless PTY + emulator, streaming screen state as line JSON. Used by remote access
    # (docs/REMOTE-PROTOCOL.md section 6.5) until the GUI itself carries the hub.
    add_executable(relay-screen-bridge tools/ScreenBridge.cpp)
    target_link_libraries(relay-screen-bridge PRIVATE relay-terminal-engine)
endif()

if(BUILD_TESTING)
    find_package(${RELAY_QT} REQUIRED COMPONENTS Test)
    add_executable(relay-engine-tests
        tests/main.cpp tests/CoreTest.cpp tests/FaintInkTest.cpp tests/FoldLayerTest.cpp
        tests/FoldSearchTest.cpp tests/ImageTest.cpp tests/PtyTest.cpp tests/SessionTest.cpp tests/ViewTest.cpp)
    target_link_libraries(relay-engine-tests PRIVATE relay-terminal-engine ${RELAY_QT}::Test)
    add_test(NAME relay-engine-tests COMMAND relay-engine-tests)
    set_tests_properties(relay-engine-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS engine TIMEOUT 120)
endif()
