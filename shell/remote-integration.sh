# Relay's remote shell integration, typed once per login (docs/SSH-AND-MOSH.md §3). bash/zsh only.
if [ -n "${BASH_VERSION-}${ZSH_VERSION-}" ]; then case $- in *i*)
case ${RELAY_R-} in ''|0*|*[!0-9]*) ;; *) printf '\033[%sA\r\033[J' "$RELAY_R";; esac
if [ -z "${__relay_r-}" ]; then
__relay_r=1
__relay_r_token=${RELAY_REMOTE_TOKEN-}
unset RELAY_REMOTE_TOKEN
__relay_r_h=${HOSTNAME:-${HOST:-$(hostname 2>/dev/null)}}
# Only hostname characters may enter an OSC.
case $__relay_r_h in *[!A-Za-z0-9.-]*) __relay_r_h=$(printf %s "$__relay_r_h" | tr -cd 'A-Za-z0-9.-');; esac
[ -n "$__relay_r_h" ] || __relay_r_h=remote
# Multiplexers require wrapped OSCs.
__relay_r_e= __relay_r_f= __relay_r_l=
if [ -n "${TMUX-}" ]; then
__relay_r_e='\033Ptmux;\033' __relay_r_f='\033\\'
case $(tmux show -gv allow-passthrough 2>/dev/null) in on|all) ;;
*) printf 'relay: tmux needs "set -g allow-passthrough on" for prompt marks\n';; esac
else case ${STY:+screen}${TERM-} in screen*) __relay_r_e='\033P' __relay_r_f='\033\\' __relay_r_l=200;; esac
fi
__relay_r_o() { printf "$__relay_r_e\033]%s\007$__relay_r_f" "$1"; }
__relay_r_confirm() { [ -n "${__relay_r_token-}" ] && __relay_r_o "777;notify;relay-shell;$__relay_r_token"; return 0; }
__relay_r_rows_for() {
local text=$1 line width rows=0 cols=${COLUMNS:-80}
set -- $(stty size 2>/dev/null); cols=${2:-$cols}
[ "$cols" -gt 0 ] || cols=80
while IFS= read -r line || [ -n "$line" ]; do
width=${#line}; [ "$width" -gt 0 ] || width=1
rows=$((rows + (width + cols - 1) / cols))
done <<__RELAY_LINE
$text
__RELAY_LINE
printf %s "$rows"
}
__relay_r_gap() {
local text=${1-} rows=${__relay_r_rows-} i
if [ -z "$rows" ]; then
if [ -n "${BASH_VERSION-}" ]; then
text=$(HISTTIMEFORMAT= builtin history 1); text=${text#*[0-9]  }
fi
rows=$(__relay_r_rows_for "$text")
fi
[ "$rows" -lt "${LINES:-24}" ] || rows=$((${LINES:-24} - 1))
[ "$rows" -gt 0 ] || rows=1
printf '\033[%sA' "$rows"
i=1
while [ "$i" -le "$rows" ]; do
__relay_r_o '7772;shell'; printf '\033[B'; i=$((i+1))
done
printf '\n'; __relay_r_o '133;C'
}
__relay_r_7() {
local s="$PWD" o= c i=0 LC_ALL=C
case $s in *[!_.~a-zA-Z0-9/-]*)
while [ $i -lt ${#s} ]; do c=${s:$i:1}
case $c in [_.~a-zA-Z0-9/-]) o=$o$c;; *) o=$o$(printf '%%%02X' "'$c");; esac
i=$((i+1)); done;; *) o=$s;; esac
o="7;file://$__relay_r_h$o"
case $__relay_r_l in ?*) [ ${#o} -gt $__relay_r_l ] && return 0;; esac
__relay_r_o "$o"
}
if [ -n "${BASH_VERSION-}" ]; then
# Respect existing redraw bindings and read-only prompt hooks.
__relay_r_k=
case $(bind -X 2>/dev/null; bind -p 2>/dev/null) in *'"\C-x\C-p"'*) __relay_r_k=key;; esac
case " $(readonly -p 2>/dev/null | tr '\n' ' ')" in *' PROMPT_COMMAND='*) __relay_r_k=prompt;; esac
if [ -n "$__relay_r_k" ]; then
case $__relay_r_k in
key) printf 'relay: Ctrl+X Ctrl+P is already bound here, so this session is left as it is\n';;
*) printf 'relay: PROMPT_COMMAND is read-only here, so this session is left as it is\n';;
esac
else
__relay_r_pc() { local s=$?; __relay_r_rows=; unset __relay_r_once; [ -n "${__relay_r_d-}" ] && __relay_r_o "133;D;$s"; __relay_r_d=1; __relay_r_7; __relay_r_confirm; return $s; }
__relay_r_ps() { local s=$?; case $PS1 in *133\;A*) ;; *) case $PS1 in *$'\n') ;; *) PS1=$PS1$'\n';; esac; case $PS1 in *$'\n\n') ;; *) PS1=$PS1$'\n';; esac; PS1='\['$__relay_r_e'\033]133;A\007'$__relay_r_f'\]'$PS1'\['$__relay_r_e'\033]133;B\007'$__relay_r_f'\]';; esac; __relay_r_a=1; return $s; }
__relay_r_c() { [ -n "${__relay_r_a-}" ] && case $BASH_COMMAND in __relay_r_*) ;; *) __relay_r_a=; __relay_r_gap;; esac; }
__relay_r_redraw() { __relay_r_rows=$(__relay_r_rows_for "$READLINE_LINE"); }
shopt -s lithist # preserve multiline history for echoed command row counts
__relay_r_q=$(declare -p PROMPT_COMMAND 2>/dev/null)
case $__relay_r_q in 'declare -a'*) eval 'PROMPT_COMMAND=(__relay_r_pc "${PROMPT_COMMAND[@]}" __relay_r_ps)';;
*) PROMPT_COMMAND="__relay_r_pc
${PROMPT_COMMAND-}
__relay_r_ps";; esac
# Exported, it would reach a later tmux, whose shell has no __relay_r_pc: keep it, unexport it.
case ${__relay_r_q%% PROMPT_COMMAND*} in *x*) export -n PROMPT_COMMAND;; esac
case :${HISTCONTROL-}: in *:ignorespace:*|*:ignoreboth:*) ;; *) HISTCONTROL=${HISTCONTROL:+$HISTCONTROL:}ignorespace;; esac
__relay_r_n=$(HISTTIMEFORMAT= history 1)
case $__relay_r_n in *"| gzip -dc)"*) history -d ${__relay_r_n%%[!0-9 ]*};; esac
case $BASH_VERSION in [123].*|4.[0-3].*) [ -z "$(trap -p DEBUG)" ] && trap __relay_r_c DEBUG;;
*) case ${PS0-} in *133\;C*) ;; *) PS0='${__relay_r_once-$( __relay_r_gap )}${__relay_r_once=}'${PS0-};; esac;; esac
for __relay_r_m in emacs-standard vi-insert vi-move; do bind -m $__relay_r_m -x '"\C-x\C-p":__relay_r_redraw' 2>/dev/null; done
fi
else
setopt HIST_IGNORE_SPACE
# Hide bootstrap history.
case ${HISTORY_IGNORE-} in '') HISTORY_IGNORE='*| gzip -dc)*';; *) HISTORY_IGNORE="(${HISTORY_IGNORE})|(*| gzip -dc)*)";; esac
__relay_r_p=$(printf "$__relay_r_e") __relay_r_s=$(printf "$__relay_r_f")  # zsh's PS1 wants bytes
PROMPT_EOL_MARK="%{$__relay_r_p"$'\e]7772;end-output\a'"$__relay_r_s%}"${PROMPT_EOL_MARK-'%B%S%#%s%b'}
__relay_r_pc() { local s=$?; [ -n "${__relay_r_off-}" ] && return $s
[ -n "${__relay_r_x-}" ] && __relay_r_o "133;D;$s"; __relay_r_x=; __relay_r_7; __relay_r_confirm
case $PS1 in *133\;A*) ;; *) case $PS1 in *$'\n') ;; *) PS1=$PS1$'\n';; esac; case $PS1 in *$'\n\n') ;; *) PS1=$PS1$'\n';; esac; PS1="%{$__relay_r_p"$'\e]133;A\a'"$__relay_r_s%}"$PS1"%{$__relay_r_p"$'\e]133;B\a'"$__relay_r_s%}";; esac; return $s; }
__relay_r_pre() { [ -n "${__relay_r_off-}" ] && return; __relay_r_gap "$1"; __relay_r_rows=; __relay_r_x=1; }
__relay_r_redraw() { __relay_r_rows=$(__relay_r_rows_for "$BUFFER"); }
eval 'precmd_functions+=(__relay_r_pc); preexec_functions+=(__relay_r_pre)'
zle -N __relay_r_redraw
case $(bindkey -M emacs '^X^P' 2>/dev/null) in *undefined-key*|'')
for __relay_r_m in emacs viins vicmd; do bindkey -M $__relay_r_m '^X^P' __relay_r_redraw; done;;
*) printf 'relay: Ctrl+X Ctrl+P is already bound here, so this session is left as it is\n'
__relay_r_off=1;; esac
fi
unset __relay_r_m __relay_r_n __relay_r_q
__relay_r_7
fi;; esac; fi
unset RELAY_R
