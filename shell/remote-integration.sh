# Relay's remote shell integration, typed once per login (docs/SSH-AND-MOSH.md §3). bash/zsh only.
if [ -n "${BASH_VERSION-}${ZSH_VERSION-}" ]; then case $- in *i*)
case ${RELAY_R-} in ''|0*|*[!0-9]*) ;; *) printf '\033[%sA\r\033[J' "$RELAY_R";; esac
if [ -z "${__relay_r-}" ]; then
__relay_r=1
__relay_r_h=${HOSTNAME:-${HOST:-$(hostname 2>/dev/null)}}
# The host names itself; a name with an escape in it would close the OSC early and hand the
# terminal a sequence of the host's choosing, so only a host name's own characters survive.
case $__relay_r_h in *[!A-Za-z0-9.-]*) __relay_r_h=$(printf %s "$__relay_r_h" | tr -cd 'A-Za-z0-9.-');; esac
[ -n "$__relay_r_h" ] || __relay_r_h=remote
# A multiplexer eats an unwrapped OSC. All we send is ESC+OSC+BEL, so tmux's ESC doubling is one
# more ESC in the prefix, decided once here, spelt as printf, PS1 and PS0 all expand it.
__relay_r_e= __relay_r_f= __relay_r_l=
if [ -n "${TMUX-}" ]; then
__relay_r_e='\033Ptmux;\033' __relay_r_f='\033\\'
case $(tmux show -gv allow-passthrough 2>/dev/null) in on|all) ;;
*) printf 'relay: tmux needs "set -g allow-passthrough on" for prompt marks\n';; esac
else case ${STY:+screen}${TERM-} in screen*) __relay_r_e='\033P' __relay_r_f='\033\\' __relay_r_l=200;; esac
fi
__relay_r_o() { printf "$__relay_r_e\033]%s\007$__relay_r_f" "$1"; }
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
__relay_r_pc() { local s=$?; [ -n "${__relay_r_d-}" ] && __relay_r_o "133;D;$s"; __relay_r_d=1; __relay_r_7; return $s; }
__relay_r_ps() { local s=$?; case $PS1 in *133\;A*) ;; *) PS1='\['$__relay_r_e'\033]133;A\007'$__relay_r_f'\]'$PS1'\['$__relay_r_e'\033]133;B\007'$__relay_r_f'\]';; esac; __relay_r_a=1; return $s; }
__relay_r_c() { [ -n "${__relay_r_a-}" ] && case $BASH_COMMAND in __relay_r_*) ;; *) __relay_r_a=; __relay_r_o '133;C';; esac; }
__relay_r_redraw() { :; }
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
*) case ${PS0-} in *133\;C*) ;; *) PS0=$__relay_r_e'\033]133;C\007'$__relay_r_f${PS0-};; esac;; esac
for __relay_r_m in emacs-standard vi-insert vi-move; do bind -m $__relay_r_m -x '"\C-x\C-p":__relay_r_redraw' 2>/dev/null; done
else
setopt HIST_IGNORE_SPACE
# The line was read before HIST_IGNORE_SPACE existed, and zsh cannot delete an entry: keep it out
# of the file on the host instead, where it would otherwise sit in every Ctrl+R for good.
case ${HISTORY_IGNORE-} in '') HISTORY_IGNORE='*| gzip -dc)*';; *) HISTORY_IGNORE="(${HISTORY_IGNORE})|(*| gzip -dc)*)";; esac
__relay_r_p=$(printf "$__relay_r_e") __relay_r_s=$(printf "$__relay_r_f")  # zsh's PS1 wants bytes
__relay_r_pc() { local s=$?; [ -n "${__relay_r_x-}" ] && __relay_r_o "133;D;$s"; __relay_r_x=; __relay_r_7
case $PS1 in *133\;A*) ;; *) PS1="%{$__relay_r_p"$'\e]133;A\a'"$__relay_r_s%}"$PS1"%{$__relay_r_p"$'\e]133;B\a'"$__relay_r_s%}";; esac; return $s; }
__relay_r_pre() { __relay_r_o '133;C'; __relay_r_x=1; }
__relay_r_redraw() { zle reset-prompt; }
eval 'precmd_functions+=(__relay_r_pc); preexec_functions+=(__relay_r_pre)'
zle -N __relay_r_redraw
for __relay_r_m in emacs viins vicmd; do bindkey -M $__relay_r_m '^X^P' __relay_r_redraw; done
fi
unset __relay_r_m __relay_r_n __relay_r_q
__relay_r_7
fi;; esac; fi
unset RELAY_R
