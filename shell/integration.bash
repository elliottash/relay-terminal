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

# Required values are set by the parent before the terminal starts this shell.
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

# ssh and mosh share their connection so the pane's agent can reuse the login (docs/SSH-AND-MOSH.md,
# section 1). The GUI sets RELAY_SSH_WRAP=1 and creates RELAY_SSH_DIR. `command ssh` bypasses this.
if [[ ${RELAY_SSH_WRAP:-0} == 1 ]]; then
    # A directory ssh can take in ControlPath (no %, spaces or quotes; room for %C and ssh's
    # temporary suffix in a 108-byte socket path) and mosh can take in --ssh.
    __relay_ssh_dir_ok() {
        local re='^/[-A-Za-z0-9_.+@/]*$'
        [[ -n ${RELAY_SSH_DIR:-} && $RELAY_SSH_DIR =~ $re && ${#RELAY_SSH_DIR} -le 48 && -d $RELAY_SSH_DIR ]]
    }

    # Succeeds when ssh "$@" can take Relay's ControlMaster/ControlPath/ControlPersist options:
    # nothing in the arguments or in the user's ssh config for that host already decides them.
    __relay_ssh_shareable() {
        __relay_ssh_dir_ok || return 1
        local -a args=("$@")
        local arg rest opt value host= config
        while (($#)); do
            arg=$1
            shift
            case $arg in
                --) break ;;
                -?*) ;;
                *)
                    # ssh takes options after the destination too, up to the remote command.
                    [[ -n $host ]] && break
                    host=$arg
                    continue
                    ;;
            esac
            rest=${arg#-}
            while [[ -n $rest ]]; do
                opt=${rest:0:1}
                rest=${rest:1}
                case $opt in
                    [MGV]) return 1 ;;
                    [46AaCfgKkNnqsTtvXxYy]) ;;
                    [BbcDEeFIiJLlmOoPpQRSWw])
                        if [[ -n $rest ]]; then
                            value=$rest
                            rest=
                        elif (($#)); then
                            value=$1
                            shift
                        else
                            return 1
                        fi
                        case $opt in
                            [OSQW]) return 1 ;;
                            o) case ${value,,} in control[mp]*) return 1 ;; esac ;;
                        esac
                        ;;
                    *) return 1 ;;
                esac
            done
        done
        [[ -n $host ]] || return 1
        # Local only: -G prints the configuration ssh would use, without connecting.
        config=$(command ssh -G "${args[@]}" 2>/dev/null < /dev/null) || return 1
        while read -r opt value; do
            case $opt in
                controlmaster) [[ $value == false || $value == no ]] || return 1 ;;
                controlpath) [[ $value == none ]] || return 1 ;;
            esac
        done <<< "$config"
        return 0
    }

    # `function name` rather than `name()`: an alias called ssh must not expand here.
    function ssh {
        if __relay_ssh_shareable "$@"; then
            command ssh -o ControlMaster=auto -o "ControlPath=$RELAY_SSH_DIR/%C" -o ControlPersist=600 "$@"
        else
            command ssh "$@"
        fi
    }

    # mosh's default way of finding the server's address (--experimental-remote-ip=proxy) passes
    # `-S none` to ssh, which turns sharing off, and it cannot work over a shared connection anyway
    # (the proxy that reports the address never runs). So an unspecified mode becomes `remote`,
    # which reads the address from $SSH_CONNECTION on the server; an explicit `proxy` is left alone.
    function mosh {
        local -a extra=()
        local arg next= mode=
        if __relay_ssh_dir_ok; then
            extra=("--ssh=ssh -o ControlMaster=auto -o ControlPath=$RELAY_SSH_DIR/%C -o ControlPersist=600")
            for arg in "$@"; do
                if [[ $next == mode ]]; then
                    mode=$arg
                    next=
                    continue
                fi
                case $arg in
                    --) break ;;
                    --ssh | --ssh=*) extra=() && break ;;
                    --experimental-remote-ip) next=mode ;;
                    --experimental-remote-ip=*) mode=${arg#*=} ;;
                esac
            done
            if [[ ${#extra[@]} -gt 0 ]]; then
                case $mode in
                    '') extra+=(--experimental-remote-ip=remote) ;;
                    proxy) extra=() ;;
                esac
            fi
        fi
        command mosh ${extra[@]+"${extra[@]}"} "$@"
    }
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
# Relay's engine uses the marks. See docs/ARCHITECTURE.md.
if [[ ${RELAY_SHELL_INTEGRATION:-0} == 1 && -r ${BASH_SOURCE[0]%/*}/relay-integration.bash ]]; then
    source "${BASH_SOURCE[0]%/*}/relay-integration.bash"
fi
