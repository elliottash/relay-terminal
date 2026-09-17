# SPDX-License-Identifier: GPL-3.0-or-later
# Passed to bash --noprofile --rcfile ... -i; never edits ~/.bashrc.
# A clean-shell launch is available for debugging incompatible prompt plugins.
if [[ ${RELAY_CLEAN_SHELL:-0} != 1 && -f $HOME/.bashrc ]]; then
    source "$HOME/.bashrc"
fi

# Required values are set by the parent before Konsole starts this shell.
if [[ -z ${RELAY_RUNTIME_DIR:-} || -z ${RELAY_SHELL_EVENT:-} || -z ${RELAY_SESSION_TOKEN:-} ]]; then
    return
fi

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

# Bind in all common Readline keymaps, without changing the user's editing mode.
bind -m emacs-standard -x '"\C-x\C-r":__relay_load'
bind -m vi-insert -x '"\C-x\C-r":__relay_load'
bind -m vi-move -x '"\C-x\C-r":__relay_load'
trap '__relay_debug' DEBUG
