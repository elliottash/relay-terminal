# SPDX-License-Identifier: AGPL-3.0-or-later
# Source in an interactive Fish shell; preexec receives the accepted command, not keys.
status is-interactive; or return
set -q __relay_fish_loaded; and return
set -g __relay_fish_loaded 1

function __relay_fish_preexec --on-event fish_preexec
    set -g __relay_fish_active 1
    printf '\033]133;C\007'
    if set -q RELAY_SESSION_TOKEN
        set -l command64 (printf %s "$argv[1]" | base64 | string join '' )
        set -l cwd64 (printf %s "$PWD" | base64 | string join '')
        printf '\033]777;notify;relay-command;%s;%s;%s\007' "$RELAY_SESSION_TOKEN" "$command64" "$cwd64"
    end
end

function __relay_fish_postexec --on-event fish_postexec
    set -l command_status $status
    printf '\033]133;D;%s\007' $command_status
    set -e __relay_fish_active
end

function __relay_fish_prompt --on-event fish_prompt
    printf '\033]133;A\007'
end
