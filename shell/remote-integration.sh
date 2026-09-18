# Relay's remote shell integration, typed once per login (docs/SSH-AND-MOSH.md §3). bash/zsh only.
if [ -n "${BASH_VERSION-}${ZSH_VERSION-}" ]; then case $- in *i*)
case ${RELAY_R-} in ''|0*|*[!0-9]*) ;; *) printf '\033[%sA\r\033[J' "$RELAY_R";; esac
if [ -z "${__relay_r-}" ]; then
__relay_r=1
__relay_r_h=${HOSTNAME:-${HOST:-$(hostname 2>/dev/null)}}
__relay_r_7() {
local s="$PWD" o= c i=0 LC_ALL=C
case $s in *[!_.~a-zA-Z0-9/-]*)
while [ $i -lt ${#s} ]; do c=${s:$i:1}
case $c in [_.~a-zA-Z0-9/-]) o=$o$c;; *) o=$o$(printf '%%%02X' "'$c");; esac
i=$((i+1)); done;; *) o=$s;; esac
printf '\033]7;file://%s%s\007' "$__relay_r_h" "$o"
}
if [ -n "${BASH_VERSION-}" ]; then
__relay_r_pc() { local s=$?; [ -n "${__relay_r_d-}" ] && printf '\033]133;D;%s\007' $s; __relay_r_d=1; __relay_r_7; return $s; }
__relay_r_ps() { local s=$?; case $PS1 in *133\;A*) ;; *) PS1='\[\033]133;A\007\]'$PS1'\[\033]133;B\007\]';; esac; __relay_r_a=1; return $s; }
__relay_r_c() { [ -n "${__relay_r_a-}" ] && case $BASH_COMMAND in __relay_r_*) ;; *) __relay_r_a=; printf '\033]133;C\007';; esac; }
__relay_r_redraw() { :; }
case $(declare -p PROMPT_COMMAND 2>/dev/null) in 'declare -a'*) eval 'PROMPT_COMMAND=(__relay_r_pc "${PROMPT_COMMAND[@]}" __relay_r_ps)';;
*) PROMPT_COMMAND="__relay_r_pc
${PROMPT_COMMAND-}
__relay_r_ps";; esac
case :${HISTCONTROL-}: in *:ignorespace:*|*:ignoreboth:*) ;; *) HISTCONTROL=${HISTCONTROL:+$HISTCONTROL:}ignorespace;; esac
__relay_r_n=$(HISTTIMEFORMAT= history 1)
case $__relay_r_n in *RELAY_R=*) history -d ${__relay_r_n%%[!0-9 ]*};; esac
case $BASH_VERSION in [123].*|4.[0-3].*) [ -z "$(trap -p DEBUG)" ] && trap __relay_r_c DEBUG;;
*) case ${PS0-} in *133\;C*) ;; *) PS0='\033]133;C\007'${PS0-};; esac;; esac
for __relay_r_m in emacs-standard vi-insert vi-move; do bind -m $__relay_r_m -x '"\C-x\C-p":__relay_r_redraw' 2>/dev/null; done
else
setopt HIST_IGNORE_SPACE
__relay_r_pc() { local s=$?; [ -n "${__relay_r_x-}" ] && printf '\033]133;D;%s\007' $s; __relay_r_x=; __relay_r_7
case $PS1 in *133\;A*) ;; *) PS1=$'%{\e]133;A\a%}'$PS1$'%{\e]133;B\a%}';; esac; return $s; }
__relay_r_pre() { printf '\033]133;C\007'; __relay_r_x=1; }
__relay_r_redraw() { zle reset-prompt; }
eval 'precmd_functions+=(__relay_r_pc); preexec_functions+=(__relay_r_pre)'
zle -N __relay_r_redraw
for __relay_r_m in emacs viins vicmd; do bindkey -M $__relay_r_m '^X^P' __relay_r_redraw; done
fi
unset __relay_r_m __relay_r_n
__relay_r_7
fi;; esac; fi
unset RELAY_R
