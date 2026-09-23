# SPDX-License-Identifier: AGPL-3.0-or-later
# Relay shell integration for Zsh: OSC 7 (working directory) and OSC 133 (prompt,
# command and output marks). Opt-in — Relay never edits your shell configuration.
#
#     source /usr/share/relay/shell/relay-integration.zsh   # in ~/.zshrc
#
# The Bash version and the sequences it emits are documented in relay-integration.bash.
# Relay's own panes run Bash; this file is for a Zsh started inside one, or for using
# Relay's engine with Zsh elsewhere.

[[ -o interactive ]] || return 0
[[ -n ${__relay_integration_loaded:-} ]] && return 0
__relay_integration_loaded=1

autoload -Uz add-zsh-hook

# Percent-encode $PWD for the file:// URL of OSC 7 (RFC 3986 unreserved set, plus "/").
__relay_url_encode() {
    local string=$1 out= i char
    local LC_ALL=C   # iterate over bytes, so UTF-8 names encode byte by byte
    for (( i = 1; i <= ${#string}; i++ )); do
        char=$string[i]
        case $char in
            [-_.~a-zA-Z0-9/]) out+=$char ;;
            *) out+=$(printf '%%%02X' "'$char") ;;
        esac
    done
    printf '%s' "$out"
}

__relay_osc7() {
    printf '\033]7;file://%s%s\007' "${HOST:-localhost}" "$(__relay_url_encode "$PWD")"
}

__relay_mark_precmd() {
    local command_status=$?
    if (( ${__relay_mark_ran:-0} )); then
        printf '\033]133;D;%s\007' "$command_status"
    fi
    __relay_mark_ran=0
    __relay_osc7
    printf '\033]133;A\007'
}

__relay_mark_preexec() {
    __relay_mark_ran=1
    printf '\033]133;C\007'
    if [[ -n ${RELAY_SESSION_TOKEN:-} ]]; then
        printf '\033]777;notify;relay-command;%s;%s;%s\007' "$RELAY_SESSION_TOKEN" \
            "$(printf %s "$1" | base64 | tr -d '\n')" \
            "$(printf %s "$PWD" | base64 | tr -d '\n')"
    fi
}

add-zsh-hook precmd __relay_mark_precmd
add-zsh-hook preexec __relay_mark_preexec
