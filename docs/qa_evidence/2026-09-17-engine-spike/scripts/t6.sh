source /tmp/claude-1000/sp/env.sh
cd /tmp/claude-1000/sp/work
rm -f /tmp/claude-1000/sp/dump.txt
startspike
t 'clear'; k Return; sleep 0.3
t 'echo one two three'; k alt+b; t 'X'; k ctrl+Left; k ctrl+Left; t 'Y'; k End; k Return
t 'echo abc'; k ctrl+a; k Delete; t 'e'; k Return
t 'cat -v'; k Return; k F5 F12 Up Home shift+Up ctrl+Right alt+x; k Return; k ctrl+d
sleep 0.5
k ctrl+shift+d; sleep 0.3
