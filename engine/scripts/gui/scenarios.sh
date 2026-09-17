#!/usr/bin/env bash
# Manual / CI-optional GUI scenario run for relay-vterm-spike under Xvfb + xdotool.
#
#   DISPLAY=:78 engine/scripts/gui/scenarios.sh BIN CORE OUTDIR
#
# Produces OUTDIR/<core>-NN-*.png screenshots, OUTDIR/<core>-dump.txt (Ctrl+Shift+D
# dumps and backend callbacks) and OUTDIR/<core>-vimtest.txt. Requires vim, less,
# htop, tmux, xdotool, ImageMagick import. Check the dumps and screenshots by eye;
# the script only drives the programs.
set -uo pipefail
bin=$1 core=$2 out=$3
mkdir -p "$out/work"
work=$(cd "$out/work" && pwd)
dump="$out/$core-dump.txt"
rm -f "$dump" "$work/vimtest.txt"
seq 1 5000 | sed 's/^/line number /' >"$work/big.txt"

shot() { import -window root "$out/$core-$1.png"; }
t() { xdotool type --delay 8 "$1"; }
k() { xdotool key --delay 30 "$@"; }
dumpnow() { k ctrl+shift+d; sleep 0.3; }

"$bin" --core "$core" --cwd "$work" --dump "$dump" --size 110x32 -e bash --norc -i >"$out/$core-stderr.log" 2>&1 &
pid=$!
sleep 1.5
win=$(xdotool search --pid "$pid" | tail -1)
xdotool windowmove "$win" 0 0
xdotool windowfocus "$win"
xdotool mousemove 400 300
sleep 0.3

# 0. Links on row 0: Ctrl+click OSC 8, path:line:col and URL (callbacks land in the dump)
t 'clear; printf "\e]7;file://$HOSTNAME$PWD\a\e]8;;https://example.com/docs\e\\\\OSC8-link\e]8;;\e\\\\ big.txt:42:7 https://relay.test/x\n"'; k Return
sleep 0.6
cw=9; ch=17 # DejaVu Sans Mono 10pt under Xvfb; adjust for other fonts
y=$((2 + ch / 2))
xdotool keydown ctrl
for col in 3 14 30; do xdotool mousemove $((2 + col * cw + 4)) $y click 1; sleep 0.2; done
xdotool keyup ctrl
sleep 0.3

# 1. SGR, 256/truecolor, CJK, emoji, box drawing, OSC 133
t 'printf "\e]133;A\a"; ls -la --color=always'; k Return
t 'printf "\e[1mbold\e[0m \e[3mitalic\e[0m \e[4munder\e[0m \e[4:3mcurly\e[0m \e[7mreverse\e[0m \e[9mstrike\e[0m \e[2mfaint\e[0m \e[38;5;208m256-orange\e[0m \e[48;2;120;40;160mtruecolor-bg\e[0m\n"'; k Return
t 'printf "CJK: 漢字かな한글| emoji: \U1F389\U1F44D\U1F3FD\U1F468‍\U1F469‍\U1F467| flag: \U1F1EF\U1F1F5| é| box: ┌─┬─┐ ╔═╗ ▀▄█░▒▓\n"'; k Return
sleep 0.8
shot 01-colors-unicode-links
dumpnow

# 2. vim: insert, resize while open, save
t 'vim -u NONE -N vimtest.txt'; k Return; sleep 1
k i; t 'hello from relay engine'; k Return; t 'second line with tab'; k Tab; t 'end'; k Escape
sleep 0.4
shot 02-vim-insert
xdotool windowsize "$win" 600 300; sleep 1
shot 03-vim-resized-small
dumpnow
xdotool windowsize "$win" 1100 640; sleep 1
t ':wq'; k Return; sleep 0.8
t 'cat -A vimtest.txt'; k Return; sleep 0.5
dumpnow

# 3. less + search, alt screen
t 'less big.txt'; k Return; sleep 0.8
k space space; t '/line number 4242'; k Return; sleep 0.6
shot 04-less-search
dumpnow
k q; sleep 0.3

# 4. htop (mouse click on the F-key bar: Help), quit
t 'htop'; k Return; sleep 2.5
shot 05-htop
k F10; sleep 0.5

# 5. tmux splits
tmux -L relayengine kill-server 2>/dev/null
t 'tmux -L relayengine -f /dev/null new -s engine'; k Return; sleep 1.2
t 'ls --color=always -la'; k Return; sleep 0.3
k ctrl+b; k percent; sleep 0.6
t 'printf "right pane │ \e[42m green \e[0m\n"; vim -u NONE -N big.txt'; k Return; sleep 1
k ctrl+b; k quotedbl; sleep 0.6
t 'htop'; k Return; sleep 2
shot 06-tmux-splits
dumpnow
k ctrl+b; k d; sleep 0.8
tmux -L relayengine kill-server 2>/dev/null

# 6. scrollback: Shift+PageUp, prompt jump
t 'clear; seq 1 300'; k Return; sleep 0.6
k shift+Prior shift+Prior; sleep 0.4
shot 07-scrollback
k shift+End; sleep 0.2

# 7. keyboard encoding through cat -v, readline editing
t 'echo one two three'; k alt+b; t 'X'; k ctrl+Left ctrl+Left; t 'Y'; k End; k Return
t 'cat -v'; k Return; k F5 F12 Up Home shift+Up ctrl+Right alt+x; k Return; k ctrl+d
sleep 0.4
shot 08-keyboard
dumpnow

# 8. search bar
k ctrl+shift+f; sleep 0.3; t 'line'; sleep 0.4
shot 09-search
k Escape; sleep 0.2

t 'exit'; k Return
sleep 0.5
kill "$pid" 2>/dev/null
cp "$work/vimtest.txt" "$out/$core-vimtest.txt" 2>/dev/null || true
echo "done: $out"
