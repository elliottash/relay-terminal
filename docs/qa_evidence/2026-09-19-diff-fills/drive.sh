#!/bin/sh
# The diff-fill evidence drive (card #BH3R): builds drive.cpp against the checkout's own
# relay-diffview / relay-highlight / relay-theme static libraries — the same objects the app and
# its tests link — and renders one DiffView in every shipped theme under Xvfb, with an isolated
# XDG_CONFIG_HOME so the drive cannot touch the owner's settings.
#
# Run from the repo root:  sh docs/qa_evidence/2026-09-19-diff-fills/drive.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

g++ -std=c++17 -fPIC \
    $(pkg-config --cflags Qt5Widgets) \
    -Isrc -I"$tmp" \
    "$here/drive.cpp" \
    build/librelay-diffview.a build/librelay-highlight.a build/librelay-theme.a \
    $(pkg-config --libs Qt5Widgets) \
    -o "$tmp/drive"

XDG_CONFIG_HOME="$tmp/config" xvfb-run -a "$tmp/drive" "$here"
echo "wrote $(ls "$here"/diff-*.png | wc -l) PNGs in $here"
