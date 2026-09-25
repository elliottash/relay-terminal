# Relay: host-side holder for a persistent remote pane (Board card #XQ8F).
# The ssh/mosh wrapper in shell/integration.bash sends this file to the host as ONE
# single-quoted word, so it contains no single quote, no exclamation mark and no
# backslash: fish and csh remote login shells must pass it through unchanged. Comment
# and blank lines are stripped before it travels and the rest is joined with a
# semicolon and a space, so every line below is one complete statement. $1 is the
# session name; $2 is an optional start directory, used only when it is one; $3 is
# an optional command a NEW session runs instead of the login shell (the local
# holder panes of card #87HB pass the integration shell Relay ships here). tmux ignores
# $2 and $3 when the session already exists, which is the whole of re-attaching.
d=${XDG_CACHE_HOME:-$HOME/.cache}/relay
if command -v tmux >/dev/null 2>&1; then mkdir -p "$d" && echo set -g status off > "$d/tmux.conf" && echo set -g prefix None >> "$d/tmux.conf" && echo set -g prefix2 None >> "$d/tmux.conf" && echo set -g mouse off >> "$d/tmux.conf" && echo set -s set-clipboard on >> "$d/tmux.conf" && echo set -g allow-passthrough on >> "$d/tmux.conf" && echo set -g window-size latest >> "$d/tmux.conf" && echo set -g history-limit 50000 >> "$d/tmux.conf" && echo set -s escape-time 10 >> "$d/tmux.conf" && echo set -g focus-events on >> "$d/tmux.conf" && { [ -d "$2" ] && [ $# -ge 3 ] && exec tmux -L "${RELAY_HOLDER_SOCK:-relay}" -f "$d/tmux.conf" new-session -A -D -s "$1" -c "$2" "$3"; [ $# -ge 3 ] && exec tmux -L "${RELAY_HOLDER_SOCK:-relay}" -f "$d/tmux.conf" new-session -A -D -s "$1" "$3"; [ -d "$2" ] && exec tmux -L "${RELAY_HOLDER_SOCK:-relay}" -f "$d/tmux.conf" new-session -A -D -s "$1" -c "$2"; exec tmux -L "${RELAY_HOLDER_SOCK:-relay}" -f "$d/tmux.conf" new-session -A -D -s "$1"; }; fi
if command -v screen >/dev/null 2>&1; then mkdir -p "$d" && echo startup_message off > "$d/screenrc" && echo defscrollback 50000 >> "$d/screenrc" && { [ -d "$2" ] && cd "$2"; exec screen -c "$d/screenrc" -D -R -S "$1"; }; fi
echo relay: $(hostname) has neither tmux nor screen, so this session will not persist
[ -d "$2" ] && cd "$2"
exec "${SHELL:-/bin/sh}" -l
