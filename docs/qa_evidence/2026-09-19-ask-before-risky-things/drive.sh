#!/usr/bin/env bash
# Ask before the risky things (#K2FV): implementer screenshots under Xvfb with an isolated
# HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME, XDG_RUNTIME_DIR and TMPDIR. No
# provider account: the profile points a local model endpoint at stub-provider.py on
# 127.0.0.1, whose tour draws one approval card at a time and blocks on each until the
# pane answers. Nothing leaves the machine: the one network call of the tour is denied
# at its card, and its host does not exist anyway.
#
#   docs/qa_evidence/2026-09-19-ask-before-risky-things/drive.sh [build-dir]
#
# Two runs, each with its own sandbox (so run A really is a first launch):
#   RELAY_QA_ONLY=a  run A only — the fresh profile: first-launch choice, the cautious
#                    five cards, the turn allowance, Esc-does-not-deny, a deny's wording,
#                    the unticked row after "Always allow", and the row that re-runs the
#                    first-launch choice.
#   RELAY_QA_ONLY=b  run B only — the checklist pre-seeded with all seven rows ticked:
#                    the two cards the cautious set never draws (create, network).
#   (default)        both, A then B.
#
# The first-launch pane and Options are driven by keyboard wherever focus lands on its
# own: Options' search box takes Left/Right (tabs) and Down (rows), and the pane's
# focusView() lands on its first button — but a pane that has just been split open does
# not have the focus yet (focusView fires before the layout settles), so the drive clicks
# the pane's body first (which activates it and focuses the first button), then Tab
# reaches the second. Three mouse clicks in all: that one, and the one into the prompt
# box, and nothing else.
#
# Run A shots (implementer-a-*.png):
#   01-first-launch      the ApprovalsPane over a freshly configured pane, before any choice
#   02-options           Options › Security after "Choose what needs approval": the cautious
#                        five ticked, create and network not
#   03-card-edit         the Edit a file card, the turn clock stopped on it
#   04-card-delete       the Delete or move card
#   05-no-second-card    the move back, same turn: it asked nothing — the read card is next
#                        (the timing proof is stub-a.jsonl: its result arrived with no
#                        answer in between, and the tour only advances on an answer)
#   06-card-read         the Read outside card, naming a file under the sandbox home
#   07-card-terminal    the Run in your terminal card
#   08-card-program      the Type into your program card
#   09-esc               Esc on it: the card stays, the line says it does not deny
#   10-denied            answered 4: the refusal wording in the transcript, turn carried on
#   11-row-unticked      Options › Security after "Always allow" on the read: the row is off
#   12-show-it-again-source  the "Show it again" row — Enter on it re-runs the first-launch
#                        choice; QA re-drives that press
#
# Run B shots (implementer-b-*.png):
#   01-card-create       the Create a new file card
#   02-card-network      the Reach the network card
#   03-denied            denied: nothing was fetched
#
# Textual evidence copied out beside the shots (the screenshots say what it looked like;
# these say what actually happened): state-<run>.conf is the profile's relay.conf after
# the run (the first-launch choice's writes, the row an "Always allow" unticks),
# stub-<run>.jsonl is one line per model request the stub saw (each carries the newest
# tool result — an allow's output or a deny's refusal wording), sessions-<run>.tgz is the
# profile's saved session data.
#
# The keyboard walks are tuned for a 1280x860 window and this build's section order
# (Options › Security is the 8th tab); a different build needs new counts. Needs Xvfb,
# xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1280 height=860
only=${RELAY_QA_ONLY:-ab}
port=${RELAY_QA_PORT:-8803}
# Options › Security is the 8th tab from the left (General is 1), so seven Rights walk a
# fresh Options pane onto it. The checklist sits about nine rows down the Security page,
# the "Show it again" row two below the checklist's last row.
security_rights=7
checklist_downs=9
again_downs=11

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }
shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
         import -window "$win" "$out/implementer-$1.png"; }
click() { eval "$(xdotool getwindowgeometry --shell "$win")"
          xdotool mousemove $((X + $1)) $((Y + $2)) click 1; sleep 0.6; }
walk_security() {   # a fresh Options pane onto Security, scrolled to $1 rows down
    k ctrl+shift+o; sleep 2
    for ((i = 0; i < security_rights; i++)); do k Right; sleep 0.4; done
    for ((i = 0; i < $1; i++)); do k Down; sleep 0.25; done
    sleep 1
}

run_tour() {   # run_tour <a|b> <prompt>
    local which=$1 prompt=$2 display= sandbox= stub_pid= xvfb_pid= relay_pid= win=
    local home= work=
    for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
    [[ -z $display ]] && { echo "no free display" >&2; return 1; }

    mkdir -p "$root/tmp"
    sandbox=$(mktemp -d "$root/tmp/relay-approvals-$which.XXXXXX")
    cleanup() {
        local pid
        # Let the debounced layout save land first: the pane transcripts (state/scrollback)
        # are the drive's textual evidence of what each card printed, and they are written
        # on a timer, not on exit.
        sleep 3
        for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
        sleep 1
        [[ -f $sandbox/relay.log ]] && cp "$sandbox/relay.log" "$out/relay-$which.log"
        [[ -f $sandbox/stub.jsonl ]] && cp "$sandbox/stub.jsonl" "$out/stub-$which.jsonl"
        [[ -f $home/.config/RelayTerminal/relay.conf ]] && \
            cp "$home/.config/RelayTerminal/relay.conf" "$out/state-$which.conf"
        tar -C "$home" -czf "$out/sessions-$which.tgz" .local/share .config/relay 2>/dev/null
        rm -rf "$sandbox"
    }
    trap cleanup RETURN

    Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 & xvfb_pid=$!
    sleep 2
    kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display" >&2; return 1; }
    export DISPLAY=$display RELAY_KEYRING=off

    home=$sandbox/home
    work=$home/project
    mkdir -p "$home" "$sandbox/run" "$sandbox/tmp" "$home/.config/RelayTerminal" \
             "$home/.config/relay" "$home/.local/share" "$home/.cache" "$work"
    chmod 700 "$sandbox/run"
    export HOME=$home XDG_CONFIG_HOME=$home/.config XDG_DATA_HOME=$home/.local/share \
           XDG_CACHE_HOME=$home/.cache XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$home/.bashrc"
    printf 'launch checklist: answer every card\n' >"$home/secret-notes.txt"
    printf 'def parse(text):\n    return text\n' >"$work/parser.py"
    {   printf '[instructions]\nonboarded=true\n[provider]\npreset=local:stub\n'
        printf '[isolation]\nenabled=false\n[terminal]\nshell_integration=true\n'
        printf '[hints]\nenabled=false\n[agent]\nterminal_handoff=agent\n'
        printf '[security]\nreadable_roots=%s\n' "$home"
        [[ $which == b ]] && printf 'approvals_ask=edit, create, delete_or_move, read_outside, terminal, program, network\napprovals_chosen=true\n'
    } >"$home/.config/RelayTerminal/relay.conf"
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$home/.config/relay/local-models.json"

    # The stub is started only now, when $home exists: its tour bakes the home into the
    # read-the-file-outside step, and a stub from an earlier drive still holding the port
    # would answer for this one with the WRONG home (how the first drive's read refusal
    # happened) — so kill any straggler first and prove this one is up before relay starts.
    pkill -f "$out/stub-provider.py" 2>/dev/null; sleep 0.5
    python3 "$out/stub-provider.py" "$port" "$home" "$sandbox/stub.jsonl" >/dev/null 2>&1 & stub_pid=$!
    sleep 1
    kill -0 "$stub_pid" 2>/dev/null || { echo "stub died (port $port busy?)" >&2; return 1; }

    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 & relay_pid=$!
    sleep 8
    local best=0
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window ($which)" >&2; cat "$sandbox/relay.log" >&2; return 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height windowfocus "$win"; sleep 2

    if [[ $which == a ]]; then
        sleep 3                                   # the choice lands 0.8 s after configure
        shot a-01-first-launch
        click 800 150; sleep 0.6                  # the pane's body: activates it, focus on
                                                  # its first button (Allow everything)
        k Tab; sleep 0.6; k Return; sleep 2.5     # "Choose what needs approval": cautious
                                                  # list written, Options opens on Security
        for ((i = 0; i < checklist_downs; i++)); do k Down; sleep 0.25; done
        sleep 1; shot a-02-options                # the cautious five ticked
        k Escape; sleep 1.5                       # focus returns to the composer
    fi

    click 420 792
    t "$prompt"; k Return; sleep 6                # the turn starts; the first card comes up

    if [[ $which == a ]]; then
        shot a-03-card-edit;      t 1; k Return; sleep 5   # allow once
        shot a-04-card-delete;    t 2; k Return; sleep 1.2 # allow this turn
        shot a-05-no-second-card; sleep 4                  # the move back asked nothing; the
                                                           # read card is already the next one
        shot a-06-card-read;      t 3; k Return; sleep 5   # always allow — unticks the row
        shot a-07-card-terminal;  t 1; k Return; sleep 6
        shot a-08-card-program
        k Escape; sleep 1.5; shot a-09-esc                 # the card stays up
        t 4; k Return; sleep 5
        shot a-10-denied                                    # the refusal's wording
        walk_security $checklist_downs
        shot a-11-row-unticked                              # the read row is off now
        k Escape; sleep 1.5
        walk_security $again_downs
        shot a-12-show-it-again-source                      # Enter here re-runs the choice
        k Escape; sleep 1.5
    else
        shot b-01-card-create;    t 1; k Return; sleep 5
        shot b-02-card-network
        t 4; k Return; sleep 5
        shot b-03-denied
    fi
    echo "run $which done"
}

[[ $only == *a* ]] && run_tour a "walk the cautious tour"
[[ $only == *b* ]] && run_tour b "the seven tour"
printf 'shots in %s\n' "$out"
