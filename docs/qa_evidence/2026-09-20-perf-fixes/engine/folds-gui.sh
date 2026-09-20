#!/usr/bin/env bash
# The fold GUI script (engine/scripts/gui/folds.sh) against one build, into its own out dir.
#   folds-gui.sh <build dir> <tag>
set -uo pipefail
B=$1; TAG=$2
P=/tmp/claude-1000/pf4k/fix/engine/run
R=/home/elliott/repos/relay-terminal
disp=
for n in $(seq 121 160); do [[ -e /tmp/.X11-unix/X$n ]] || { disp=:$n; break; }; done
Xvfb $disp -screen 0 1200x700x24 >/dev/null 2>&1 & xvfb=$!
sleep 2
export DISPLAY=$disp
out=$P/folds-$TAG; rm -rf $out; mkdir -p $out
DISPLAY=$disp bash $R/engine/scripts/gui/folds.sh "$B/engine/relay-vterm-spike" libvterm "$out" >/dev/null 2>&1
kill $xvfb 2>/dev/null
ls $out | sed "s/^/$TAG /"
