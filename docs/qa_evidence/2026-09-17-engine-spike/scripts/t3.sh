source /tmp/claude-1000/sp/env.sh
W=$(xdotool search --pid $(pgrep -f relay-vterm-spike | head -1) | tail -1)
click() { xdotool mousemove $1 $2; xdotool keydown ctrl; xdotool click 1; xdotool keyup ctrl; sleep 0.3; }
click 20 351
click 256 351
click 480 351
click 60 116
# selection drag over "OSC8 link plain" then middle-click paste into prompt
xdotool mousemove 2 351 mousedown 1 mousemove 60 351 mousemove 150 351 mouseup 1
sleep 0.3
xdotool mousemove 500 369 click 2
sleep 0.5
shot 03-selection-paste
cat /tmp/claude-1000/sp/spike.log
