source /tmp/claude-1000/sp/env.sh
cd /tmp/claude-1000/sp/work
rm -f /tmp/claude-1000/sp/dump.txt
tmux -L spike kill-server 2>/dev/null
rm -f $EV/09-htop.png
startspike
t 'htop -F htop'; k Return; sleep 2.5
shot 09-htop-filtered
k ctrl+shift+d; sleep 0.2
k q; sleep 0.4
t 'tmux -L spike -f /dev/null new -s spike'; k Return; sleep 1.2
t 'ls --color=always -la'; k Return; sleep 0.3
k ctrl+b; k percent; sleep 0.6
t 'printf "right pane │ \e[42m green \e[0m\n"; vim -u NONE -N big.txt'; k Return; sleep 1
k ctrl+b; k quotedbl; sleep 0.6
t 'htop -F htop'; k Return; sleep 2
shot 11-tmux-splits
k ctrl+shift+d; sleep 0.2
k ctrl+b; k d; sleep 0.8
shot 12-tmux-detached
k ctrl+shift+d; sleep 0.2
tmux -L spike kill-server
