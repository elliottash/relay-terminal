#!/usr/bin/env bash
# Build probe.cpp exactly the way CMake builds the relay target's main.cpp (flags and include
# paths from build/CMakeFiles/relay.dir/flags.make, libraries from its link.txt, with the app's
# own object files swapped for the probe's).    ./build-probe.sh [build-dir]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
here=$PWD
build=${1:-$(cd ../../.. && pwd)/build}
cd "$build"
/usr/bin/c++ -O2 -g -DNDEBUG -std=gnu++17 -Wall -Wextra -Wpedantic -fPIC \
    -I"$build/relay_autogen/include" -I"$build/../src" -I"$build/../engine" \
    -isystem /usr/include/aarch64-linux-gnu/qt5 \
    -isystem /usr/include/aarch64-linux-gnu/qt5/QtCore \
    -isystem /usr/lib/aarch64-linux-gnu/qt5/mkspecs/linux-g++ \
    -isystem /usr/include/aarch64-linux-gnu/qt5/QtWidgets \
    -isystem /usr/include/aarch64-linux-gnu/qt5/QtGui \
    -isystem /usr/include/aarch64-linux-gnu/qt5/QtNetwork \
    -isystem /usr/include/aarch64-linux-gnu/qt5/QtDBus \
    "$here/probe.cpp" -o "$here/probe" \
    librelay-closedlist.a librelay-localmodels.a librelay-projects.a librelay-hints.a \
    librelay-modelsettings.a librelay-settings.a librelay-outputlinks.a librelay-markdown.a \
    librelay-highlight.a librelay-editor.a librelay-prompthistory.a librelay-filepanes.a \
    librelay-subagents.a librelay-requests.a librelay-conversations.a librelay-completion.a \
    librelay-fileindex.a librelay-backends.a librelay-windowstate.a librelay-runtimedirs.a \
    librelay-input.a librelay-notifications.a librelay-panes.a librelay-queuenav.a \
    librelay-logging.a librelay-voice.a librelay-images.a librelay-titles.a \
    librelay-panestatus.a librelay-remotesession.a librelay-remotefiles.a librelay-sshconfig.a \
    engine/librelay-terminal-engine.a librelay-screen.a librelay-slash.a librelay-board.a \
    librelay-aliases.a librelay-calllines.a librelay-diffview.a librelay-sharing.a \
    librelay-remotepane.a /usr/lib/aarch64-linux-gnu/libQt5DBus.so.5 \
    librelay-panestate.a /usr/lib/aarch64-linux-gnu/libKF5SyntaxHighlighting.so.5 \
    /usr/lib/aarch64-linux-gnu/libQt5Network.so.5 librelay-outputlinks.a \
    engine/librelay-vterm-c.a -lutil librelay-input.a librelay-editor.a librelay-prompthistory.a \
    librelay-toollabel.a librelay-hints.a librelay-highlight.a librelay-theme.a \
    /usr/lib/aarch64-linux-gnu/libQt5Widgets.so.5 /usr/lib/aarch64-linux-gnu/libQt5Gui.so.5 \
    /usr/lib/aarch64-linux-gnu/libQt5Core.so.5
echo "built: $here/probe"
