source /tmp/claude-1000/sp/env.sh
cd /tmp/claude-1000/sp/work
rm -f /tmp/claude-1000/sp/dump.txt vimtest.txt
seq 1 5000 | sed 's/^/line number /' > big.txt
startspike
# vim
t 'vim -u NONE -N vimtest.txt'; k Return; sleep 1
k i; t 'hello from relay-vterm-spike'; k Return; t 'second line with tab'; k Tab; t 'end'; k Escape
sleep 0.4
shot 04-vim-insert
k ctrl+shift+d; sleep 0.2
# resize while vim open
xdotool windowsize $W 600 300; sleep 1
shot 05-vim-resized-small
k ctrl+shift+d; sleep 0.2
xdotool windowsize $W 1100 600; sleep 1
shot 06-vim-resized-large
t ':set ruler'; k Return; sleep 0.3
t ':wq'; k Return; sleep 0.8
t 'cat -A vimtest.txt'; k Return; sleep 0.5
k ctrl+shift+d; sleep 0.2
shot 07-vim-saved
# less
t 'less big.txt'; k Return; sleep 0.8
k space space; t '/line number 4242'; k Return; sleep 0.6
shot 08-less-search
k ctrl+shift+d; sleep 0.2
k q; sleep 0.3
# htop
t 'htop -F htop'; k Return; sleep 2.5
shot 09-htop-filtered
k ctrl+shift+d; sleep 0.2
k F10; sleep 0.5; k q; sleep 0.3
t 'clear'; k Return; sleep 0.3
# scrollback
t 'seq 1 300'; k Return; sleep 0.6
k shift+Prior shift+Prior; sleep 0.4
shot 10-scrollback-shift-pageup
k shift+End
k ctrl+shift+d; sleep 0.2
