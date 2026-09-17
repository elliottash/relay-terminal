source /tmp/claude-1000/sp/env.sh
NAME=$1; shift
cat > /tmp/claude-1000/sp/inner-intr.sh <<EOF
cd /tmp/claude-1000/sp
exec bash --norc -i
EOF
"$@" bash /tmp/claude-1000/sp/inner-intr.sh >/tmp/claude-1000/sp/intr-$NAME.log 2>&1 &
LPID=$!
sleep 2
W=$(xdotool search --pid $LPID | tail -1)
xdotool windowfocus $W; xdotool mousemove 300 200
xdotool type 'cat big200.txt'; xdotool key Return
sleep 1.5
CATPID=$(pgrep -n -x cat); echo catpid=$CATPID
t0=$(date +%s.%N); echo sent $(date +%s%3N)
xdotool key ctrl+c
for i in $(seq 3000); do [ -d /proc/$CATPID ] || break; sleep 0.01; done
t1=$(date +%s.%N)
sleep 1
t2=$(date +%s.%N)
xdotool type 'echo typed-after-interrupt'; xdotool key Return
sleep 0.5
import -window root $EV/14-interrupt-$NAME.png
echo "$NAME ctrl+c->cat exit: $(echo "$t1 - $t0" | bc) s"
kill $LPID; sleep 0.3; kill -9 $LPID 2>/dev/null
