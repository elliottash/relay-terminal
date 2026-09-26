# SPDX-License-Identifier: AGPL-3.0-or-later
#
# One shared compiler cache for every Relay tree (card #V52P): build/, build-fast/, each land.py
# verify slot. Every session builds the same headers in its turn, so a header state one of them
# compiled should not be compiled again by the next; without a cache each Pane.h touch
# recompiled every window source in every tree.
#
# ccache is preferred, then sccache; -DRELAY_COMPILER_CACHE_TOOL=sccache picks one. A launcher
# given on the command line (CI passes -DCMAKE_CXX_COMPILER_LAUNCHER=ccache) is left alone, and
# -DRELAY_COMPILER_CACHE=OFF turns the cache off.
#
# ccache runs with its own config file in the cache dir (CCACHE_CONFIGPATH), so the size cap is
# the same whoever builds: $RELAY_CCACHE_MAX, default 10G, never lowering a larger cap
# already in the file. CCACHE_BASEDIR is the common parent
# of the source and build dirs, so every path on a compile line is relative, and two trees laid
# out alike (two verify slots) produce the same hash for the same sources and flags.
option(RELAY_COMPILER_CACHE "Compile through ccache or sccache when one is installed" ON)

# "40G", "10.5 GB", "512M", "2Gi" -> whole megabytes in `out` (0 when unreadable), enough to
# compare two ccache max_size values. K/M/G/T are ccache's decimal units, Ki/Mi/Gi/Ti binary.
function(relay_size_bytes text out)
    set(mb 0)
    if(text MATCHES "^ *([0-9]+)(\\.[0-9]+)? ?([KMGT]?)(i?)B? *$")
        set(whole "${CMAKE_MATCH_1}")
        set(frac "${CMAKE_MATCH_2}")
        set(unit "${CMAKE_MATCH_3}")
        string(SUBSTRING "${frac}0" 1 1 tenth)      # first decimal digit, "0" when none
        if(tenth STREQUAL "")
            set(tenth 0)
        endif()
        if(unit STREQUAL "T")
            math(EXPR mb "${whole} * 1000000 + ${tenth} * 100000")
        elseif(unit STREQUAL "G" OR unit STREQUAL "")
            math(EXPR mb "${whole} * 1000 + ${tenth} * 100")
        elseif(unit STREQUAL "M")
            set(mb "${whole}")
        else()
            math(EXPR mb "${whole} / 1000")
        endif()
    endif()
    set(${out} "${mb}" PARENT_SCOPE)
endfunction()
set(RELAY_COMPILER_CACHE_TOOL "AUTO" CACHE STRING "Compiler cache to use: AUTO, ccache or sccache")
set_property(CACHE RELAY_COMPILER_CACHE_TOOL PROPERTY STRINGS AUTO ccache sccache)
set(RELAY_COMPILER_CACHE_DIR "" CACHE PATH
    "Shared compiler cache dir (empty: $RELAY_CCACHE_DIR, else $XDG_CACHE_HOME/relay/ccache)")

if(NOT RELAY_COMPILER_CACHE)
    message(STATUS "Relay: compiler cache off (RELAY_COMPILER_CACHE=OFF)")
elseif(CMAKE_CXX_COMPILER_LAUNCHER)
    message(STATUS "Relay: compiler launcher given: ${CMAKE_CXX_COMPILER_LAUNCHER}")
else()
    # A tool found once and uninstalled since would fail every compile: look again.
    foreach(_relay_tool CCACHE SCCACHE)
        if(RELAY_${_relay_tool}_PROGRAM AND NOT EXISTS "${RELAY_${_relay_tool}_PROGRAM}")
            unset(RELAY_${_relay_tool}_PROGRAM CACHE)
        endif()
    endforeach()
    set(RELAY_CCACHE_PROGRAM_USE "")
    set(RELAY_SCCACHE_PROGRAM_USE "")
    if(NOT RELAY_COMPILER_CACHE_TOOL STREQUAL "sccache")
        find_program(RELAY_CCACHE_PROGRAM ccache)
        set(RELAY_CCACHE_PROGRAM_USE "${RELAY_CCACHE_PROGRAM}")
    endif()
    if(NOT RELAY_COMPILER_CACHE_TOOL STREQUAL "ccache")
        find_program(RELAY_SCCACHE_PROGRAM sccache)
        set(RELAY_SCCACHE_PROGRAM_USE "${RELAY_SCCACHE_PROGRAM}")
    endif()

    set(_relay_cache_dir "${RELAY_COMPILER_CACHE_DIR}")
    if(NOT _relay_cache_dir AND DEFINED ENV{RELAY_CCACHE_DIR})
        set(_relay_cache_dir "$ENV{RELAY_CCACHE_DIR}")
    endif()
    if(NOT _relay_cache_dir)
        if(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
            set(_relay_cache_dir "$ENV{XDG_CACHE_HOME}/relay/ccache")
        elseif(WIN32 AND DEFINED ENV{LOCALAPPDATA})
            set(_relay_cache_dir "$ENV{LOCALAPPDATA}/relay/ccache")
        else()
            set(_relay_cache_dir "$ENV{HOME}/.cache/relay/ccache")
        endif()
    endif()
    file(TO_CMAKE_PATH "${_relay_cache_dir}" _relay_cache_dir)

    if(RELAY_CCACHE_PROGRAM_USE)
        # The deepest directory holding both trees: the checkout for build/ and build-fast/,
        # <slot> for a verify slot's <slot>/src and <slot>/build.
        set(_relay_base "${CMAKE_SOURCE_DIR}")
        while(TRUE)
            string(FIND "${CMAKE_BINARY_DIR}/" "${_relay_base}/" _relay_at)
            if(_relay_at EQUAL 0)
                break()
            endif()
            get_filename_component(_relay_parent "${_relay_base}" DIRECTORY)
            if(_relay_parent STREQUAL _relay_base)
                break()
            endif()
            set(_relay_base "${_relay_parent}")
        endwhile()

        set(_relay_max "10G")
        if(DEFINED ENV{RELAY_CCACHE_MAX} AND NOT "$ENV{RELAY_CCACHE_MAX}" STREQUAL "")
            set(_relay_max "$ENV{RELAY_CCACHE_MAX}")
        endif()
        file(MAKE_DIRECTORY "${_relay_cache_dir}")
        set(_relay_conf "${_relay_cache_dir}/ccache.conf")
        set(_relay_old_conf "")
        if(EXISTS "${_relay_conf}")
            file(READ "${_relay_conf}" _relay_old_conf)
        endif()
        # Never shrink the cap (card #3MH4): a limit raised by hand (`ccache -M 40G`) or by
        # another configure's RELAY_CCACHE_MAX stays, instead of the next verify slot's
        # configure writing the 10G default back over it and evicting half the cache.
        relay_size_bytes("${_relay_max}" _relay_new_bytes)
        if(_relay_old_conf MATCHES "max_size = ([0-9.]+ ?[KMGT]?i?B?)")
            set(_relay_old_max "${CMAKE_MATCH_1}")
            relay_size_bytes("${_relay_old_max}" _relay_old_bytes)
            if(_relay_old_bytes GREATER _relay_new_bytes)
                string(REPLACE " " "" _relay_max "${_relay_old_max}")
            endif()
        endif()
        set(_relay_conf_text "# Written by Relay's cmake/CompilerCache.cmake (card #V52P).\nmax_size = ${_relay_max}\ncompression = true\n")
        # Written only when it differs, so parallel configures of several trees do not race
        # on a file ccache is reading.
        if(NOT _relay_old_conf STREQUAL _relay_conf_text)
            file(WRITE "${_relay_conf}" "${_relay_conf_text}")
        endif()

        set(_relay_env "CCACHE_DIR=${_relay_cache_dir}" "CCACHE_CONFIGPATH=${_relay_conf}")
        # base_dir "/" would make every absolute path relative; ccache refuses it, so do we.
        if(NOT _relay_base STREQUAL "/" AND NOT _relay_base MATCHES "^[A-Za-z]:/?$")
            list(APPEND _relay_env "CCACHE_BASEDIR=${_relay_base}")
        endif()
        set(CMAKE_CXX_COMPILER_LAUNCHER
            "${CMAKE_COMMAND};-E;env;${_relay_env};${RELAY_CCACHE_PROGRAM}")
        message(STATUS "Relay: compiler cache ccache (${RELAY_CCACHE_PROGRAM}), dir ${_relay_cache_dir}, base ${_relay_base}, max ${_relay_max}")
    elseif(RELAY_SCCACHE_PROGRAM_USE)
        set(CMAKE_CXX_COMPILER_LAUNCHER
            "${CMAKE_COMMAND};-E;env;SCCACHE_DIR=${_relay_cache_dir};${RELAY_SCCACHE_PROGRAM}")
        message(STATUS "Relay: compiler cache sccache (${RELAY_SCCACHE_PROGRAM}), dir ${_relay_cache_dir}")
    else()
        message(STATUS "Relay: no compiler cache (install ccache to share objects between build trees)")
    endif()
endif()
