# Relay terminal engine. Built only with -DRELAY_BUILD_ENGINE=ON (default OFF);
# the relay app target does not depend on it yet.
#
# Targets:
#   relay-vterm-c           vendored + patched libvterm 0.3.3 (MIT), C99
#   relay-terminal-engine   static library: VtCore (ghostty / libvterm), Pty, session, view, backend
#   relay-engine-bench      headless core throughput
#   relay-vterm-spike       manual GUI harness
#   relay-engine-tests      unit tests (ctest), with BUILD_TESTING
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
    add_library(relay-ghostty-vt STATIC IMPORTED GLOBAL)
    set_target_properties(relay-ghostty-vt PROPERTIES
        IMPORTED_LOCATION ${RELAY_GHOSTTY_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${RELAY_GHOSTTY_INCLUDE_DIR})
    message(STATUS "Relay engine: libghostty-vt core enabled (${RELAY_GHOSTTY_LIBRARY})")
endif()

# ---- engine library
set(RELAY_ENGINE_SOURCES
    core/CellTypes.h core/CellTypes.cpp
    core/VtCore.h core/VtCoreFactory.cpp
    core/LibVtermCore.h core/LibVtermCore.cpp
    core/Osc133Scanner.h
)
if(RELAY_ENGINE_WITH_GHOSTTY)
    list(APPEND RELAY_ENGINE_SOURCES core/GhosttyCore.h core/GhosttyCore.cpp)
endif()

add_library(relay-terminal-engine STATIC ${RELAY_ENGINE_SOURCES})
target_include_directories(relay-terminal-engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(relay-terminal-engine PUBLIC ${RELAY_QT}::Widgets PRIVATE relay-vterm-c)
target_compile_options(relay-terminal-engine PRIVATE -Wall -Wextra)
if(RELAY_ENGINE_WITH_GHOSTTY)
    target_link_libraries(relay-terminal-engine PRIVATE relay-ghostty-vt)
    target_compile_definitions(relay-terminal-engine PUBLIC RELAY_HAVE_GHOSTTY)
endif()

add_executable(relay-engine-bench bench/EngineBench.cpp)
target_link_libraries(relay-engine-bench PRIVATE relay-terminal-engine)
