# SPDX-License-Identifier: AGPL-3.0-or-later
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
                # `ssh -- host`: what follows is the destination, not an option.
                --) [[ -n $host || $# -eq 0 ]] || host=$1
                    break ;;
                -?*) ;;
                *)
                    # ssh takes options after the destination too, up to the remote command. A lone
                    # `-` is not a destination (and not an option either): ssh itself rejects it.
                    [[ $arg == - ]] && return 1
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
                            # ssh accepts "-o ' ControlMaster=auto'", so the value is trimmed
                            # before it is read: Relay's own options go first and first wins.
                            o) case ${value,,} in [[:space:]]*) value=${value#"${value%%[![:space:]]*}"} ;; esac
                               case ${value,,} in control[mp]*) return 1 ;; esac ;;
                        esac
                        ;;
                    *) return 1 ;;
                esac
            done
        done
        [[ -n $host ]] || return 1
        # `ssh -G` prints the configuration ssh would use without connecting — but it is not free:
        # it runs the user's `Match exec` hooks (a VPN probe, a token touch) and canonicalisation
        # can make it resolve names, so asking on every ssh would double their hooks and add their
        # latency. It is only needed to find a ControlMaster or ControlPath the user set, so it is
        # asked only when their configuration mentions one at all, under a timeout, and the answer
        # is remembered for the rest of this shell.
        # A configuration file named on the command line can say anything, so it is always read.
        local named=
        for arg in "${args[@]}"; do [[ $arg == -F* ]] && named=1; done
        if [[ -z $named && -z ${__relay_ssh_configured+set} ]]; then
            __relay_ssh_configured=
            local file line pattern
            local -a files=("$HOME/.ssh/config" /etc/ssh/ssh_config)
            # An Include can hold the keywords too, so the files it names join the list; one level
            # is enough in practice and keeps this to a couple of greps (/etc/ssh/ssh_config.d/*).
            for file in "${files[@]}"; do
                [[ -r $file ]] || continue
                while read -r _ line; do
                    for pattern in $line; do
                        [[ $pattern == ~* ]] && pattern=$HOME${pattern#\~}
                        [[ $pattern == /* ]] || pattern=$HOME/.ssh/$pattern
                        files+=($pattern)
                    done
                done < <(grep -iE '^[[:space:]]*include[[:space:]]' "$file" 2>/dev/null)
            done
            for file in "${files[@]}"; do
                [[ -r $file ]] || continue
                if grep -qiE '^[[:space:]]*(controlmaster|controlpath|controlpersist|match)' "$file" 2>/dev/null; then
                    __relay_ssh_configured=1
                    break
                fi
            done
        fi
        [[ -n $named || -n $__relay_ssh_configured ]] || return 0
        if [[ -z $named && -n ${__relay_ssh_asked[$host]+set} ]]; then
            return "${__relay_ssh_asked[$host]}"
        fi
        # A hook that never returns must not take the shell with it; without `timeout`, no ssh -G.
        if ! type -P timeout > /dev/null; then
            __relay_ssh_asked[$host]=0
            return 0
        fi
        config=$(command timeout 5 ssh -G "${args[@]}" 2>/dev/null < /dev/null) || {
            __relay_ssh_asked[$host]=1
            return 1
        }
        while read -r opt value; do
            case $opt in
                # `ssh -G` normalises: a master is `false` when off, and `controlpath` is printed
                # only when the user set one (as `none` when they set it to none).
                controlmaster) [[ $value == false ]] || { __relay_ssh_asked[$host]=1; return 1; } ;;
                controlpath) [[ $value == none ]] || { __relay_ssh_asked[$host]=1; return 1; } ;;
                controlpersist) [[ $value == no || $value == false ]] || { __relay_ssh_asked[$host]=1; return 1; } ;;
            esac
        done <<< "$config"
        __relay_ssh_asked[$host]=0
        return 0
    }

    declare -A __relay_ssh_asked=()

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
    #
    # `remote` is the server's own idea of its address, which is not reachable from here when the
    # host is behind NAT or reached through a forwarded port: mosh then sits waiting for UDP that
    # never comes. Sharing is not worth breaking a session that worked before Relay, so a shared
    # mosh that dies quickly is run again exactly as the user typed it, with a line saying so.
    function mosh {
        local -a extra=()
        local arg next= mode=
        if __relay_ssh_dir_ok; then
            extra=("--ssh=ssh -o ControlMaster=auto -o ControlPath=$RELAY_SSH_DIR/%C -o ControlPersist=600")
            for arg in "$@"; do
                if [[ -n $next ]]; then
                    [[ $next == mode ]] && mode=$arg
                    next=
                    continue
                fi
                case $arg in
                    # Past the destination everything belongs to the remote command: `mosh host
                    # --ssh=x` runs `--ssh=x` over there and says nothing about mosh's own ssh.
                    --) break ;;
                    --ssh | --ssh=*) extra=() && break ;;
                    --experimental-remote-ip) next=mode ;;
                    --experimental-remote-ip=*) mode=${arg#*=} ;;
                    # mosh's own options that take a separate value; the value is not a destination.
                    -p | --port | --client | --server | --predict | --family | --bind-server) next=skip ;;
                    -*) ;;
                    *) break ;;
                esac
            done
            if [[ ${#extra[@]} -gt 0 ]]; then
                case $mode in
                    '') extra+=(--experimental-remote-ip=remote) ;;
                    proxy) extra=() ;;
                esac
            fi
        fi
        if [[ ${#extra[@]} -eq 0 ]]; then
            command mosh "$@"
            return
        fi
        local started=$SECONDS status=0
        command mosh "${extra[@]}" "$@" || status=$?
        # A session that lived a while and then ended is the user's business, whatever its status.
        if (( status != 0 && SECONDS - started < 20 )); then
            printf 'relay: mosh could not use the shared connection; retrying as you typed it\n' >&2
            command mosh "$@"
            return
        fi
        return "$status"
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
