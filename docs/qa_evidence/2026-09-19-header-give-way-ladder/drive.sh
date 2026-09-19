set -e
D=/tmp/claude-1000/v-header/gui
export HOME=$D/home XDG_CONFIG_HOME=$D/home/.config XDG_DATA_HOME=$D/home/.local/share XDG_RUNTIME_DIR=$D/run TMPDIR=$D/tmp RELAY_KEYRING=off
export QT_QPA_PLATFORM=xcb
/tmp/claude-1000/v-header/build/relay >$D/relay3.out 2>&1 &
PID=$!
for i in $(seq 1 40); do xdotool search --pid $PID --name . >/dev/null 2>&1 && break; sleep 0.5; done
sleep 5
WID=$(xdotool search --pid $PID --onlyvisible --name . | tail -1)
xdotool windowsize $WID 900 500; sleep 2
xdotool windowfocus $WID || true; sleep 1
xdotool key ctrl+h; sleep 2
xdotool type --delay 40 'ssh -o StrictHostKeyChecking=no -o BatchMode=yes elliott@localhost sleep 300'
sleep 1; xdotool key Return; sleep 8
for w in 620 600 580 560 540 520; do
  xdotool windowsize $WID $w 500; sleep 2
  import -window $WID $D/ssh3-win-$w.png
  convert $D/ssh3-win-$w.png -crop ${w}x40+0+40 +repage $D/ssh3-$w.png
done
kill $PID 2>/dev/null || true
