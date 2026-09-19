# F-keys: a scheme, proposed

Owner report, 2026-09-18: *"need a keyboard shortcut for showing / hiding the reasoning traces,
maybe an F# key -- we can think about, how can we assign some things to F keys that are thematically
/ UX-coherent. or alt+R."* The shortcut shipped as **Alt+R** (`agent.thinkingPanel`). This is the
answer to the second half: what belongs on an F-key at all.

## The principle

**An F-key toggles what is on screen. A Ctrl chord acts.** A toggle is reversible, takes no
argument, and changes nothing outside this window — cheap to press by accident, so it can be one
key. Anything that sends, closes, restarts or writes stays a chord, where the modifier is the
deliberation.

## What an F-key costs here

Under the default `program_keys: "shift-only"`, **F-keys always act** — they never reach the program
in the terminal (`Keymap::actsInsidePrograms`). Every F-key Relay claims is taken from vim, nano and
Midnight Commander *permanently*. mc, nano and htop own F1–F10 as their whole UI (F10 quits); vim
leaves F2–F8 to plugins; less uses none. That is the budget.

Taken today: **F1** shortcuts list (dropped in the VS Code preset), **F12** native terminal input
(all four presets, and drop-down terminals such as Yakuake grab it globally), **F9** one of three
choices for the voice push-to-talk hold key (Options › Voice; the default is Right Alt).

## What I would assign

As *alternates*, never replacing the Ctrl chords people already have:

| Key | Action | Displaces |
|---|---|---|
| F2 | `agent.thinkingPanel` — reasoning fold (Alt+R) | mc rename, nano write-out |
| F3 | `agent.requests` — tasks panel (Ctrl+Shift+K) | mc view, nano find |
| F4 | `files.explorer` — explorer pane (Ctrl+B) | mc edit, nano replace |
| F6 | `palette.open` — the Actions pane (Ctrl+Shift+A) | mc move |

One row of keys, one idea: each shows or hides a surface — a panel, or (F2) the reasoning fold in
the grid — in the order those surfaces sit in the window.
F12 already fits — native input is a toggle.

## What I would not assign

**F5** (run/refresh everywhere else — a habit worth not breaking), **F7/F8** (debugger step, mc
search/mkdir), **F9** (voice), **F10** (quit in mc and nano; taking it would eat a program's exit
key), **F11** (the window manager's full screen). **F1** stays help.

## The condition

Do not ship the table above until F-keys respect `program_keys`. Today "shift-only" lets them all
through unconditionally, so claiming F2–F6 would break mc and nano for good. The honest order is:
make F-keys pass through to a running program under "shift-only" first, then hand out the four keys.
