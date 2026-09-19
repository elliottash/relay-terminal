# SPDX-License-Identifier: AGPL-3.0-or-later
# Relay shell integration for Bash: OSC 7 (working directory) and OSC 133 (prompt,
# command and output marks). Opt-in — Relay never edits your shell configuration.
#
# Enable it for every shell by adding this to ~/.bashrc:
#
#     source /usr/share/relay/shell/relay-integration.bash
#
# or, from a source checkout, shell/relay-integration.bash. Inside Relay you can also
# set RELAY_SHELL_INTEGRATION=1 (palette: "Shell integration (OSC 7/133)"), which makes
# the pane shell source this file after your own configuration.
#
# What the marks are used for: Relay's own terminal engine (--engine=relay) tracks the
# working directory from OSC 7 and command boundaries from OSC 133, which give it
# "jump to previous/next prompt". Other terminals that
# implement the same sequences (kitty, WezTerm, Ghostty, VS Code, iTerm2) understand
# them too, so enabling this is not Relay-specific.
#
#   OSC 7  ;file://HOST/PATH        current directory
#   OSC 133;A                       start of the prompt
#   OSC 133;B                       end of the prompt / start of the typed command
#   OSC 133;C                       command output starts
#   OSC 133;D;EXITCODE              command finished

# Interactive Bash only, and only once per shell.
case $- in
    *i*) ;;
    *) return 0 ;;
esac
if [[ -n ${__relay_integration_loaded:-} || -z ${BASH_VERSION:-} ]]; then
    return 0
fi
__relay_integration_loaded=1

# Percent-encode $PWD for the file:// URL of OSC 7. Everything outside the unreserved
# set of RFC 3986 (plus "/") is escaped byte by byte, so non-UTF-8 names survive.
__relay_url_encode() {
    local string=$1 out= i char
    local LC_ALL=C   # iterate over bytes, so UTF-8 names encode byte by byte
    for ((i = 0; i < ${#string}; i++)); do
        char=${string:i:1}
        case $char in
            [-_.~a-zA-Z0-9/]) out+=$char ;;
            *) out+=$(printf '%%%02X' "'$char") ;;
        esac
    done
    printf '%s' "$out"
}

__relay_osc7() {
    printf '\033]7;file://%s%s\007' "${HOSTNAME:-localhost}" "$(__relay_url_encode "$PWD")"
}

# Runs first in PROMPT_COMMAND: the previous command has finished, and the exit status
# is passed on unchanged so other PROMPT_COMMAND entries still see it.
__relay_mark_prompt() {
    local status=$?
    if [[ ${__relay_mark_ran:-0} == 1 ]]; then
        printf '\033]133;D;%s\007' "$status"
    fi
    __relay_mark_ran=1
    __relay_osc7
    return "$status"
}

if declare -p PROMPT_COMMAND >/dev/null 2>&1 && [[ $(declare -p PROMPT_COMMAND) == 'declare -a'* ]]; then
    PROMPT_COMMAND=(__relay_mark_prompt "${PROMPT_COMMAND[@]}")
else
    PROMPT_COMMAND=(__relay_mark_prompt "${PROMPT_COMMAND:-:}")
fi

# A and B bracket the prompt itself; \[ \] keep the escapes out of Readline's width
# calculation. PS0 is printed after Enter and before the command runs.
if [[ $PS1 != *'133;A'* ]]; then
    PS1='\[\033]133;A\007\]'"$PS1"'\[\033]133;B\007\]'
fi
if [[ ${PS0-} != *'133;C'* ]]; then
    PS0=$'\033]133;C\007'"${PS0-}"
fi
