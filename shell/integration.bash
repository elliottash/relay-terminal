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
    # The GUI sources this file from its data dir; the persistent-holder script is a sibling.
    __relay_shell_dir=.
    case ${BASH_SOURCE[0]} in */*) __relay_shell_dir=${BASH_SOURCE[0]%/*} ;; esac
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

    # The argv walk behind persistence: sets __relay_ssh_dest to the destination and
    # returns 0 only when the words are one plain interactive login - a destination,
    # nothing after it (that would be a remote command) and no option that gives ssh
    # other work: no shell, forwarding, tunnels or control operations. Valued options
    # are consumed the way __relay_ssh_shareable consumes them.
    __relay_ssh_dest=
    __relay_ssh_login_argv() {
        __relay_ssh_dest=
        local arg rest opt
        while (($#)); do
            arg=$1
            shift
            case $arg in
                --)
                    # Only a `--` that introduces the destination is safe to append after.
                    [[ -n $__relay_ssh_dest ]] && return 1
                    (($#)) || return 1
                    __relay_ssh_dest=$1
                    shift
                    (($# == 0)) || return 1
                    ;;
                -?*)
                    rest=${arg#-}
                    while [[ -n $rest ]]; do
                        opt=${rest:0:1}
                        rest=${rest:1}
                        case $opt in
                            [NWOGVQsTfMDLRw]) return 1 ;;
                            [46AaCcgKkntvXxYy]) ;;
                            [BbcEeFIiJLlmOoPpQS])
                                if [[ -n $rest ]]; then rest=
                                elif (($#)); then shift
                                else return 1
                                fi
                                ;;
                            *) return 1 ;;
                        esac
                    done
                    ;;
                -) return 1 ;;
                *) [[ -n $__relay_ssh_dest ]] && return 1; __relay_ssh_dest=$arg ;;
            esac
        done
        [[ -n $__relay_ssh_dest ]]
    }

    # The same walk for mosh's own options: `--` and a word after the destination both
    # mean a remote command there. mosh's valued options are the ones the mosh function
    # knows about.
    __relay_mosh_login_argv() {
        __relay_ssh_dest=
        local arg next=
        while (($#)); do
            arg=$1
            shift
            if [[ -n $next ]]; then
                next=
                continue
            fi
            case $arg in
                --)
                    [[ -n $__relay_ssh_dest ]] && return 1
                    (($#)) || return 1
                    __relay_ssh_dest=$1
                    shift
                    (($# == 0)) || return 1
                    ;;
                --ssh | --experimental-remote-ip | -p | --port | --client | --server | --predict | --family | --bind-server) next=skip ;;
                -*) ;;
                *) [[ -n $__relay_ssh_dest ]] && return 1; __relay_ssh_dest=$arg ;;
            esac
        done
        [[ -n $__relay_ssh_dest ]]
    }

    # The half of the gate both transports share: the NEVER list, the session name and
    # the start directory, kept in __relay_persist_session and __relay_persist_cwd.
    __relay_persist_session=
    __relay_persist_cwd=
    __relay_ssh_persist_prepare() {
        local -a never=()
        local IFS=,
        read -r -a never <<< "${RELAY_SSH_NEVER:-}"
        local host=${__relay_ssh_dest#*@} entry
        host=${host,,}
        for entry in "${never[@]}"; do
            entry=${entry#"${entry%%[![:space:]]*}"}; entry=${entry%"${entry##*[![:space:]]}"}
            [[ ${entry,,} == "$host" ]] && return 1
        done
        local name=${RELAY_SSH_SESSION:-relay-${RELAY_PANE_ID:0:8}}
        # tmux session names must not hold a dot or a colon; screen takes any word.
        name=${name//[!A-Za-z0-9_-]/}
        [[ -n $name ]] || return 1
        __relay_persist_session=$name
        __relay_persist_cwd=
        # The directory travels as an unquoted word in the remote command, so it may
        # hold none of the characters this pattern allows past.
        local re='^[-A-Za-z0-9_./~+@%:]+$'
        [[ ${RELAY_SSH_CWD:-} =~ $re ]] && __relay_persist_cwd=$RELAY_SSH_CWD
        return 0
    }

    # True when this ssh may become a persistent pane login: persistence is on, the
    # pane (or a session name) is known, the words are one interactive login, and the
    # host is not on the NEVER list.
    __relay_ssh_persist_ok() {
        [[ ${RELAY_SSH_PERSIST:-0} == 1 ]] || return 1
        [[ -n ${RELAY_PANE_ID:-} || -n ${RELAY_SSH_SESSION:-} ]] || return 1
        __relay_ssh_login_argv "$@" || return 1
        __relay_ssh_persist_prepare
    }

    __relay_mosh_persist_ok() {
        [[ ${RELAY_SSH_PERSIST:-0} == 1 ]] || return 1
        [[ -n ${RELAY_PANE_ID:-} || -n ${RELAY_SSH_SESSION:-} ]] || return 1
        __relay_mosh_login_argv "$@" || return 1
        __relay_ssh_persist_prepare
    }

    # The holder script, from the shell directory this file was sourced from, as one
    # single-quoted word: comments and blank lines stripped, the rest joined with a
    # semicolon and a space. Cached on the first use; empty, and persistence silently
    # off, when the file is missing or holds a quote, backslash or bang that would not
    # survive the single quotes on its way through the remote login shell.
    __relay_holder_text=
    __relay_holder_loaded=0
    __relay_holder() {
        if (( ! __relay_holder_loaded )); then
            __relay_holder_loaded=1
            local line text= file=$__relay_shell_dir/remote-holder.sh
            if [[ -r $file ]]; then
                while IFS= read -r line || [[ -n $line ]]; do
                    line=${line#"${line%%[![:space:]]*}"}; line=${line%"${line##*[![:space:]]}"}
                    [[ -z $line || $line == \#* ]] && continue
                    text+="${text:+; }$line"
                done < "$file"
            fi
            [[ $text == *\'* || $text == *\\* || $text == *!* ]] && text=
            __relay_holder_text=$text
        fi
        [[ -n $__relay_holder_text ]]
    }

    # The persistent ssh form: the share options when the configuration allows them,
    # -t, the user's own words, then the holder as one word after them. The GUI knows
    # the shape: relay-holder marks the session name, and the directory, when there is
    # one, is the last word.
    __relay_ssh_persist_run() {
        local -a pre=()
        __relay_ssh_shareable "$@" && pre=(-o ControlMaster=auto -o "ControlPath=$RELAY_SSH_DIR/%C" -o ControlPersist=600)
        local remote="sh -c '$__relay_holder_text' relay-holder $__relay_persist_session"
        [[ -n $__relay_persist_cwd ]] && remote="$remote $__relay_persist_cwd"
        command ssh "${pre[@]}" -t "$@" "$remote"
    }

    # mosh's sharing extra: --ssh=ssh with Relay's sharing, plus
    # --experimental-remote-ip=remote unless the user named a mode; an explicit proxy
    # turns sharing off entirely. Kept in __relay_mosh_extra for the mosh function and
    # the RELAY_SSH_LINK=mosh form of ssh.
    __relay_mosh_extra=()
    __relay_mosh_prepare_extra() {
        __relay_mosh_extra=()
        local arg next= mode=
        __relay_ssh_dir_ok || return 0
        __relay_mosh_extra=("--ssh=ssh -o ControlMaster=auto -o ControlPath=$RELAY_SSH_DIR/%C -o ControlPersist=600")
        for arg in "$@"; do
            if [[ -n $next ]]; then
                [[ $next == mode ]] && mode=$arg
                next=
                continue
            fi
            case $arg in
                --) break ;;
                --ssh | --ssh=*) __relay_mosh_extra=() && break ;;
                --experimental-remote-ip) next=mode ;;
                --experimental-remote-ip=*) mode=${arg#*=} ;;
                -p | --port | --client | --server | --predict | --family | --bind-server) next=skip ;;
                -*) ;;
                *) break ;;
            esac
        done
        if (( ${#__relay_mosh_extra[@]} > 0 )); then
            case $mode in
                '') __relay_mosh_extra+=(--experimental-remote-ip=remote) ;;
                proxy) __relay_mosh_extra=() ;;
            esac
        fi
    }

    # The persistent mosh form, for RELAY_SSH_LINK=mosh and for a mosh typed by the
    # user. mosh shell-quotes every word it passes after `--` and mosh-server execs
    # those words verbatim, so the holder must come as bare words of its own - no
    # quotes added here, or the quotes become part of what sh -c runs on the host.
    # A mosh that dies within twenty seconds (no mosh-server there, UDP blocked)
    # falls back to the persistent ssh form, with a line saying so.
    __relay_mosh_persist_run() {
        __relay_mosh_prepare_extra "$@"
        local started=$SECONDS status=0
        local -a after=(-- sh -c "$__relay_holder_text" relay-holder "$__relay_persist_session")
        [[ -n $__relay_persist_cwd ]] && after+=("$__relay_persist_cwd")
        command mosh "${__relay_mosh_extra[@]}" "$@" "${after[@]}" || status=$?
        # A session that lived a while and then ended is the user's business.
        if (( status != 0 && SECONDS - started < 20 )); then
            printf 'relay: mosh could not connect to %s; using ssh\n' "$__relay_ssh_dest" >&2
            __relay_ssh_persist_run "$@"
            return
        fi
        return "$status"
    }

    # True when ssh "$@" may become a mosh: mosh takes none of ssh's options, so only a
    # bare destination (optionally after `--`) can be handed over. An ssh port, a jump
    # host or an identity has no meaning for mosh, and such a login stays on ssh.
    __relay_ssh_mosh_ok() {
        case $# in
            1) [[ $1 != -* ]] ;;
            2) [[ $1 == -- && $2 != -* ]] ;;
            *) return 1 ;;
        esac
    }

    # `function name` rather than `name()`: an alias called ssh must not expand here.
    function ssh {
        if __relay_ssh_persist_ok "$@" && __relay_holder; then
            if [[ ${RELAY_SSH_LINK:-ssh} == mosh ]] && type -P mosh > /dev/null && __relay_ssh_mosh_ok "$@"; then
                __relay_mosh_persist_run "$@"
            else
                __relay_ssh_persist_run "$@"
            fi
            return
        fi
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
        if __relay_mosh_persist_ok "$@" && __relay_holder; then
            __relay_mosh_persist_run "$@"
            return
        fi
        __relay_mosh_prepare_extra "$@"
        if [[ ${#__relay_mosh_extra[@]} -eq 0 ]]; then
            command mosh "$@"
            return
        fi
        local started=$SECONDS status=0
        command mosh "${__relay_mosh_extra[@]}" "$@" || status=$?
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
    command "${RELAY_PYTHON:-python3}" -S "$RELAY_SHELL_EVENT" "$1" "${2:-0}" "$PWD" "$$" "${3-}"
}

# Replacing an existing DEBUG trap breaks several prompt/preexec frameworks.
# Fail into native mode instead of silently replacing user shell behavior.
if [[ -n $(trap -p DEBUG) ]]; then
    __relay_event unsupported 0 < /dev/null
    # PS0 can delimit unavailable native commands without replacing the user's trap
    # or enabling composer control. Older Bash has no safe pre-execution fallback.
    if (( BASH_VERSINFO[0] > 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] >= 4) )); then
        __relay_unavailable_start() {
            printf '\033]777;notify;relay-command;%s;;%s\007' "$RELAY_SESSION_TOKEN" \
                "$(printf %s "$PWD" | base64 | tr -d '\n')"
        }
        __relay_unavailable_prompt() {
            local command_status=$?
            if [[ ${__relay_unavailable_ran:-0} == 1 ]]; then
                printf '\033]133;D;%s\007' "$command_status"
            fi
            __relay_unavailable_ran=1
            printf '\033]133;A\007'
            return "$command_status"
        }
        PS0='$(__relay_unavailable_start)'${PS0-}
        if [[ $(declare -p PROMPT_COMMAND 2>/dev/null) == 'declare -a'* ]]; then
            PROMPT_COMMAND=(__relay_unavailable_prompt "${PROMPT_COMMAND[@]}")
        else
            PROMPT_COMMAND=(__relay_unavailable_prompt "${PROMPT_COMMAND:-:}")
        fi
    fi
    return
fi

__relay_at_prompt=0
__relay_in_prompt=0
__relay_status=0
__relay_staged_rows=

# How many display rows a command line's echo occupies: each line takes at least one row, a
# wrapped one ceil(chars/COLUMNS). Counted in characters, not cells — wide glyphs and tabs can
# miscount by a row at a wrap boundary, which only under/over-marks the band below.
__relay_rows_for() {
    local text=$1 line width rows=0 cols=${COLUMNS:-80}
    (( cols > 0 )) || cols=80
    while IFS= read -r line || [[ -n $line ]]; do
        width=${#line}
        (( width == 0 )) && width=1
        (( rows += (width + cols - 1) / cols ))
    done <<< "$text"
    printf '%s' "$rows"
}

# Only accepted Readline input can supply text; never inspect shell history.
__relay_command_rows() {
    if [[ -n $__relay_staged_rows ]]; then
        printf '%s' "$__relay_staged_rows"
    elif [[ -n $__relay_accepted_line ]]; then
        __relay_rows_for "$__relay_accepted_line"
    else
        printf 1
    fi
}

# Capture at acceptance, never while a foreground program is reading input. Multiple
# acceptances before a prompt (PS2 input) are deliberately unavailable. Syntax checking
# also prevents an incomplete first line being reused if a different binding finishes it.
__relay_accept() {
    ((__relay_accept_count += 1))
    __relay_accepted_line=
    if (( __relay_accept_count == 1 )) && [[ $READLINE_LINE != *\\ ]]; then
        local diagnostics
        if diagnostics=$(printf '%s\n' "$READLINE_LINE" | command bash --noprofile --norc -n 2>&1) && [[ -z $diagnostics ]]; then
            __relay_accepted_line=$READLINE_LINE
        fi
    fi
}

# Wrap only default Enter bindings, and only when both private chords are unused.
# User macros/functions and alternative accept keys remain untouched (unavailable text).
for __relay_map in emacs-standard vi-insert vi-move; do
    __relay_bindings=$(bind -m "$__relay_map" -p; bind -m "$__relay_map" -s; bind -m "$__relay_map" -X)
    if [[ $__relay_bindings != *'"\e[777;1~"'* && $__relay_bindings != *'"\e[777;2~"'* ]]; then
        bind -m "$__relay_map" -x '"\e[777;1~":__relay_accept'
        bind -m "$__relay_map" '"\e[777;2~": accept-line'
        for __relay_enter in '\C-m' '\C-j'; do
            if printf '%s\n' "$__relay_bindings" | command grep -Fqx "\"$__relay_enter\": accept-line"; then
                bind -m "$__relay_map" "\"$__relay_enter\": \"\\e[777;1~\\e[777;2~\""
            fi
        done
    fi
done
unset __relay_map __relay_bindings __relay_enter
__relay_accept_count=0
__relay_accepted_line=

# First simple command of a line just entered: the cursor sits on the fresh row below the
# command's echo, so the echo's rows are directly above. Mark each with Relay's row role
# (OSC 7772;shell) and the engine paints the cyan band behind what was typed, the same block
# an agent prompt wears in violet. CUU/CUD stay inside the screen, so nothing scrolls; past
# the top of the screen only the visible tail is marked.
__relay_mark_typed_rows() {
    local rows up i
    rows=$(__relay_command_rows) || return 0
    [[ $rows =~ ^[0-9]+$ ]] || return 0
    up=$rows
    (( up > ${LINES:-24} - 1 )) && up=$(( ${LINES:-24} - 1 ))
    (( up >= 1 )) || return 0
    printf '\033[%dA' "$up"
    for (( i = 1; i <= up; i++ )); do
        printf '\033]7772;shell\033\\'
        (( i < up )) && printf '\033[B'
    done
    printf '\033[B'
}

__relay_prompt_begin() {
    __relay_status=$?
    __relay_in_prompt=1
    if [[ -n ${__relay_command_active+set} ]]; then
        printf '\033]133;D;%s\007' "$__relay_status"
    fi
    unset __relay_command_active
    __relay_accept_count=0
    __relay_accepted_line=
    __relay_staged_rows=   # a staged line that was cleared never marks a later command
    return "$__relay_status"
}

__relay_prompt_end() {
    compgen -A alias -A function | __relay_event ready "$__relay_status"
    printf '\033]133;A\007'
    __relay_at_prompt=1
    __relay_in_prompt=0
}

__relay_debug() {
    if [[ $__relay_at_prompt == 1 && $__relay_in_prompt == 0 && $BASH_COMMAND != __relay_* ]]; then
        __relay_at_prompt=0
        # Bash >= 4.4 starts at PS0, before even a subshell/pipeline can print.
        if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 4) )); then
            __relay_command_active=1
            __relay_event running 0 "" < /dev/null
        fi
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
    __relay_staged_rows=$(__relay_rows_for "$READLINE_LINE")
    # Bash has printed pending job notifications before entering this binding. Repair the
    # upper gap now, just before Readline echoes the command; late output can consume PS1's
    # original gap. The terminal adds nothing when a blank row is already there.
    printf '\033]7772;input-gap\033\\'
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
__relay_command_start() {
    __relay_event running 0 "$__relay_accepted_line" < /dev/null
    __relay_mark_typed_rows
}
if (( BASH_VERSINFO[0] > 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] >= 4) )); then
    # Assignment expands to an empty string but persists in the parent shell; the
    # command substitution alone could not mark a subshell-only command as active.
    PS0='${__relay_command_active-$(__relay_command_start)'$'\n''}${__relay_command_active=}'${PS0-}
fi
trap '__relay_debug' DEBUG

# Leave a blank row between the folder prompt and what you type, matching agent prompt spacing.
# Preserve the user's PS1 and any extra spacing it already has.
if [[ -n ${PS1:-} ]]; then
    [[ $PS1 == *$'\n' ]] || PS1=$PS1$'\n'
    [[ $PS1 == *$'\n\n' ]] || PS1=$PS1$'\n'
fi

# Opt-in OSC 7 / OSC 133 marks (palette: "Shell integration (OSC 7/133)", or
# RELAY_SHELL_INTEGRATION=1). Sourced last so it wraps the final PS1/PROMPT_COMMAND.
# Relay's engine uses the marks. See docs/ARCHITECTURE.md.
if [[ ${RELAY_SHELL_INTEGRATION:-0} == 1 && -r ${BASH_SOURCE[0]%/*}/relay-integration.bash ]]; then
    source "${BASH_SOURCE[0]%/*}/relay-integration.bash"
fi
