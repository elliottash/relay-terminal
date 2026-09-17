# SPDX-License-Identifier: GPL-3.0-or-later
# Passed to bash --noprofile --rcfile ... -i; never edits ~/.bashrc.
# A clean-shell launch is available for debugging incompatible prompt plugins.
if [[ ${RELAY_CLEAN_SHELL:-0} != 1 && -f $HOME/.bashrc ]]; then
    source "$HOME/.bashrc"
fi

# Make pane shells (and the commands they start) preferred OOM victims over the Relay window.
# Raising oom_score_adj needs no privileges; failures are ignored.
if [[ -w /proc/$$/oom_score_adj ]]; then
    printf '300\n' > /proc/$$/oom_score_adj 2>/dev/null || :
fi

# Required values are set by the parent before Konsole starts this shell.
if [[ -z ${RELAY_RUNTIME_DIR:-} || -z ${RELAY_SHELL_EVENT:-} || -z ${RELAY_SESSION_TOKEN:-} ]]; then
    return
fi

# Each Relay pane starts in its own directory (new tab, split, or restored pane).
if [[ -n ${RELAY_START_DIR:-} && -d $RELAY_START_DIR ]]; then
    builtin cd -- "$RELAY_START_DIR" || :
fi
unset RELAY_START_DIR

# `relay open [PATH]` opens a folder in Relay's explorer pane or a file in its preview pane.
relay() {
    case ${1:-} in
        open)
            local target=${2:-.}
            RELAY_OPEN_FROM_SHELL=1 command "${RELAY_PYTHON:-python3}" "${RELAY_OPEN_HELPER:-relay-open}" "$target"
            ;;
        *)
            printf 'usage: relay open [PATH]\n' >&2
            return 2
            ;;
    esac
}

__relay_event() {
    command "${RELAY_PYTHON:-python3}" -S "$RELAY_SHELL_EVENT" "$1" "${2:-0}" "$PWD" "$$"
}

# Replacing an existing DEBUG trap breaks several prompt/preexec frameworks.
# Fail into native mode instead of silently replacing user shell behavior.
if [[ -n $(trap -p DEBUG) ]]; then
    __relay_event unsupported 0 < /dev/null
    return
fi

__relay_at_prompt=0
__relay_in_prompt=0
__relay_status=0

__relay_prompt_begin() {
    __relay_status=$?
    __relay_in_prompt=1
    return "$__relay_status"
}

__relay_prompt_end() {
    compgen -A alias -A function | __relay_event ready "$__relay_status"
    __relay_at_prompt=1
    __relay_in_prompt=0
}

__relay_debug() {
    if [[ $__relay_at_prompt == 1 && $__relay_in_prompt == 0 && $BASH_COMMAND != __relay_* ]]; then
        __relay_at_prompt=0
        __relay_event running 0 < /dev/null
    fi
    return 0
}

__relay_load() {
    if [[ ! -f "$RELAY_RUNTIME_DIR/input.txt" ]]; then
        return
    fi
    # read -d '' preserves embedded/trailing newlines, unlike command substitution.
    IFS= read -r -d '' READLINE_LINE < "$RELAY_RUNTIME_DIR/input.txt" || :
    READLINE_POINT=${#READLINE_LINE}
    __relay_event loaded 0 < /dev/null
}

# Keep user-defined PROMPT_COMMAND entries and preserve the original command exit code.
if declare -p PROMPT_COMMAND 2>/dev/null | grep -q 'declare -a'; then
    PROMPT_COMMAND=(__relay_prompt_begin "${PROMPT_COMMAND[@]}" __relay_prompt_end)
else
    PROMPT_COMMAND=(__relay_prompt_begin "${PROMPT_COMMAND:-:}" __relay_prompt_end)
fi

# Relay prints agent output directly to the terminal screen, then sends Ctrl+X Ctrl+P.
# Running any bind -x function makes Readline redraw the prompt and current line.
__relay_redraw() {
    :
}

# Bind in all common Readline keymaps, without changing the user's editing mode.
bind -m emacs-standard -x '"\C-x\C-r":__relay_load'
bind -m vi-insert -x '"\C-x\C-r":__relay_load'
bind -m vi-move -x '"\C-x\C-r":__relay_load'
bind -m emacs-standard -x '"\C-x\C-p":__relay_redraw'
bind -m vi-insert -x '"\C-x\C-p":__relay_redraw'
bind -m vi-move -x '"\C-x\C-p":__relay_redraw'
trap '__relay_debug' DEBUG

# What you type belongs on its own line, not tacked onto the end of the folder: a command staged by
# Relay, and the agent's inline output (which erases only the line it opens on), both start below the
# prompt instead of running into it. Appended to the user's own PS1, whatever it is; never doubled,
# and skipped for a prompt that already ends in a newline.
if [[ -n ${PS1:-} && $PS1 != *$'\n' ]]; then
    PS1=$PS1$'\n'
fi

# Opt-in OSC 7 / OSC 133 marks (palette: "Shell integration (OSC 7/133)", or
# RELAY_SHELL_INTEGRATION=1). Sourced last so it wraps the final PS1/PROMPT_COMMAND.
# Relay's own engine uses the marks; KonsolePart ignores them. See docs/ARCHITECTURE.md.
if [[ ${RELAY_SHELL_INTEGRATION:-0} == 1 && -r ${BASH_SOURCE[0]%/*}/relay-integration.bash ]]; then
    source "${BASH_SOURCE[0]%/*}/relay-integration.bash"
fi
