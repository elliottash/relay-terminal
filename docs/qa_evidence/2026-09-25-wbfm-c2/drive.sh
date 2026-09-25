#!/bin/sh
# #WBFM: compile the ModelsPane screenshot driver against the built relay libs and run it under
# Xvfb with an isolated XDG_CONFIG_HOME. Usage: drive.sh <label>   (before|after)
set -eu
cd "$(dirname "$0")"
ROOT=../../..
BUILD=$ROOT/build
LABEL=${1:-shot}

c++ -O2 -g -DNDEBUG -std=gnu++17 -fPIC \
    -DQT_CORE_LIB -DQT_GUI_LIB -DQT_NO_DEBUG -DQT_TESTLIB_LIB -DQT_WIDGETS_LIB \
    -I"$ROOT/src" \
    -isystem /usr/include/aarch64-linux-gnu/qt5 \
    -isystem /usr/include/aarch64-linux-gnu/qt5/QtCore \
    -isystem /usr/include/aarch64-linux-gnu/qt5/QtWidgets \
    -isystem /usr/include/aarch64-linux-gnu/qt5/QtGui \
    -isystem /usr/include/aarch64-linux-gnu/qt5/QtTest \
    -isystem /usr/lib/aarch64-linux-gnu/qt5/mkspecs/linux-g++ \
    drive.cpp -o /tmp/wbfm-drive \
    "$BUILD/librelay-modelspane.a" \
    "$BUILD/librelay-modelpicker.a" "$BUILD/librelay-hints.a" "$BUILD/librelay-jobstab.a" \
    "$BUILD/librelay-modelrows.a" "$BUILD/librelay-modelcatalog.a" "$BUILD/librelay-filterpopup.a" \
    "$BUILD/librelay-sourcedir.a" "$BUILD/librelay-theme.a" "$BUILD/librelay-settings.a" \
    "$BUILD/librelay-agentcontext.a" "$BUILD/librelay-outputlinks.a" "$BUILD/librelay-settingscache.a" \
    /usr/lib/aarch64-linux-gnu/libQt5Test.so.5.15.13 \
    /usr/lib/aarch64-linux-gnu/libQt5Widgets.so.5.15.13 \
    /usr/lib/aarch64-linux-gnu/libQt5Gui.so.5.15.13 \
    /usr/lib/aarch64-linux-gnu/libQt5Core.so.5.15.13

CONF=$(mktemp -d)
WORK=$(mktemp -d)
xvfb-run -a env XDG_CONFIG_HOME="$CONF" RELAY_WORKSPACE="$WORK" WBFM_DUMP="${WBFM_DUMP:-}" /tmp/wbfm-drive . "$LABEL"
rm -rf "$CONF" "$WORK"
ls -la ./*.png
