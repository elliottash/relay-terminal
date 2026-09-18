# Switchboard phase 1 — implementer evidence (2026-09-17)

Agent tools and policy, the worker protocol (section 17), the Switchboard pane, and the
terminal's `#` references. Card: `issues/features/needs_qa_llm/2026-09-17-switchboard-phase1.md`.

## How this was run

Relay built from this branch (`cmake --build build`, RelWithDebInfo, Qt 5.15.13 / KF5), started
under **Xvfb on `:117`** (chosen from the free displays in `/tmp/.X11-unix`) with an isolated
`XDG_CONFIG_HOME` and `XDG_DATA_HOME` under the session scratchpad. Captures are of the **window
id** (`import -window 2097162`); a root capture comes back black because Relay draws its own
frame. There is no window manager on the display, so `xdotool windowfocus` + a global `key` is
used instead of `windowactivate`.

The workspace is a **throwaway git repo holding a copy of this repository's `issues/` tree** (86
cards), so the live pass exercises the real data and the real 86 cards without writing anything
into the repository's own tracker. Model: `deepseek/deepseek-v4.1-flash` through OpenRouter, with
the key read from the desktop keyring by the worker. **No key was printed, logged or captured.**

## What the screenshots show

| File | What it proves |
|---|---|
| `01-switchboard-opens.png` | Ctrl+Shift+S splits the Switchboard in beside the terminal pane. Tabs with counts (Features 70, Bugs 14, Design, Marketing, Plans, …), columns Inbox / Discussing / Ready, the real 86 cards, the tab title "Switchboard · 86". |
| `02-card-detail.png` | Enter opens the card detail: `#C1HH`, status and tab pickers, the meta line (milestone, component, acceptance, file path), the rendered Markdown body, "Thread · 0 entries", the reply box with **Ask the agent** / **Comment only**. |
| `03-comment-appended.png` | A comment typed in the pane and sent with **Comment only** lands in the thread as `owner · note`, and the thread count goes to 1. No model call. |
| `04-slash-card-quick-add.png` | `/card <text>` from the terminal composer creates a card without opening anything: the inline line and toast `◆ #6DDX · created · Inbox`. (From the first run; the workspace was rebuilt before the agent pass.) |
| `05-agent-card-writes.png` | A real agent turn in a terminal pane using the tools: `board_list` ×2 (searching before creating), `board_create_card`, `board_move_card`, `board_comment`, `board_read`, `board_update_card`. |
| `06-bugs-tab-after-agent.png` | The pane picked the change up **by itself** through the `QFileSystemWatcher` → `board_refresh`: title "Switchboard · 87", "Bugs 15", `#WC2X` sitting in **Discussing** with `waiting: owner · 4 ✎`, and the inline activity line `◆ #WC2X · waiting_on: (unset) → owner` in the pane that caused it. |
| `07-agent-card-thread.png` | The agent's card opened in the detail view: request verbatim, thread of 4 entries including the numbered question with its recommendation. |
| `08-switchboard-agent-reply.png` | `board_ask`: a reply typed on the card is answered by the **Switchboard agent** (the per-window worker on the `switchboard` role) and streams into the thread; 7 entries. |
| `agent-written-card.md`, `agent-written-thread.md` | The bytes the agent wrote, copied out of the live workspace. |

## The card and thread the agent wrote

`agent-written-card.md` — the request is the user's words verbatim, `status: discussing`, and the
file sits in `issues/changes/` (the Bugs tab's folder).

`agent-written-thread.md` — one entry per write, each naming the actor, the model, the pane and
the `session/turn`:

- `kind=event` — created in Inbox, with the path.
- `kind=event` — `Inbox → Discussing` with the reason the tool required.
- `kind=question` — numbered, with a recommendation, exactly as policy rule 2 asks.
- `kind=event` — `waiting_on: (unset) → owner`.
- `author=owner kind=comment` — the question typed in the pane, recorded **before** the model was
  called.
- `kind=decision` from the Switchboard agent (`pane=switchboard`), quoting the owner verbatim.
- `kind=comment` — the agent's answer, appended by the worker when the turn finished.

## Format check

```
$ python3 scripts/relay-board.py --issues <live workspace>/issues check
87 card(s) checked: 0 error(s), 0 warning(s)
```

and, on this repository's own tracker (untouched by the run):

```
$ python3 scripts/relay-board.py check
86 card(s) checked: 0 error(s), 0 warning(s)
```

## Automated tests

- `./scripts/test.sh` — 623 tests, all passing (`tests/test_board_tools.py` 78,
  `tests/test_board_protocol.py` 37, plus the existing suite).
- `ctest --test-dir build` — 17/17 groups, including the new `board` group
  (`tests/boardmodel_test.cpp`, 16 cases).
- `cmake --build build` — no new warnings (`-Wall -Wextra -Wpedantic` on every target).

## Known gaps, deliberately left for phase 2

See the card's "Not in this change" section. In short: no scan/convert of non-compliant notes, no
`suggest` proposal UI, the `## Tasks` checklist is read-only in the pane, no eval harness, no
labels/assignee editors, and the Undo toast is protocol-complete (`board_undo`) but has no button
in the GUI yet.
