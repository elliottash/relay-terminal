# Relay — project instructions for agents

Relay is a cross-platform C++/Qt workspace for terminal and agentic coding, with its own terminal engine, a composer,
per-pane BYOK agents, tabs/panes, file panes and an actions palette. Read `docs/README.md`
(index), `docs/ARCHITECTURE.md` and `docs/ROADMAP.md` before large changes.

## Working rules

- **Issues:** file-based tracker in `.board/` (conventions in `.board/README.md`, based on the
  global issue-tracking skill). The folder is hidden, so a plain project-wide `rg` skips it:
  search with `rg --hidden` or name `.board/` explicitly. Sections are the stage list — inbox, discussing, planning, planned,
  executing, needs verification, needs QA, done — and Relay makes each stage move itself at the
  event that earns it; a section may also collect nothing and be filled by hand (`section:`).
  Implemented work lands in `needs-verification` (then QA) with implementer evidence under
  `docs/qa_evidence/YYYY-MM-DD-<slug>/` and a QA checklist. `.board/POLICY.md` is the generated
  copy of the rules Relay's own pane agents get in their system prompt — read it when you have no
  `board_*` tools, because it also says how to make each of those calls by editing files.
- **Build:** `scripts/relay-build`, never `cmake --build` by hand: it locks `build/`
  against the other sessions and stamps the objects it made back to the build's start, so a
  header edited while a compile was running is recompiled instead of silently missed
  (`CLAUDE.md`, "Build through `scripts/relay-build`"). Once a session has `begin`-claimed its
  paths, it builds and tests its own change through `python3 scripts/land.py try <me>
  [--tests <regex>]` — tip plus its hunks in its own verify slot, so no other session's edit can
  break it. Read `docs/BUILDING.md` before any
  developer, package or release build; it is the canonical Linux, Windows and macOS build map.
- **Tests:** do not run the full test suites unless the owner asks. Run targeted tests for the
  code you changed instead — a single `ctest --test-dir build -R <name>` case, or one
  `pytest`/`scripts/test.sh` subset. (`./scripts/test.sh` and a full `ctest --test-dir build`
  run are for when the owner asks, or a release-scale change.) Verify GUI changes live under
  Xvfb with an isolated `XDG_CONFIG_HOME`.
- **Other agents:** several agents edit this checkout at once, often the same files. That is intended and
  expected: work alongside their changes, never revert them, and do not complain about them or report them
  as a problem. Mention another agent's edit only when it actually blocks your task.
- **Commits:** land through `python3 scripts/land.py begin <me> <paths>` before editing and
  `python3 scripts/land.py commit <me> -m …` afterwards; several sessions share this checkout
  and a plain `git commit` from the shared index reverts them (`CLAUDE.md`).
- **Crashes:** a fatal signal writes its frames into `relay.log` (`gui_crash …`) and a worker's
  into `worker-faults.log`; `scripts/relay-debug` runs Relay under gdb when that is not enough.
  This machine keeps no cores — apport drops unpackaged binaries — so read
  `docs/CRASH-DIAGNOSIS.md` before hunting for one.
- **Protocol:** GUI ↔ worker messages are specified in `docs/AGENT-SESSIONS-PROTOCOL.md`;
  update it when adding messages or events.
- **Decisions already made:** no Konsole fork (and KonsolePart itself retired 2026-09-18);
  no per-action tool approvals; BYOK first (Relay Free is an included, quota-limited hosted
  provider since 2026-09-18, `docs/RELAY-FREE.md`). See `docs/ROADMAP.md`.

## Words

"card" had grown eight meanings, two of them inside one parenthesis in `src/Pane.h`, so the word is
now spent where it is listed here and nowhere else.

- **card** is a Board record, of any of its four types (`CARD_TYPES = ("work", "plan",
  "memory", "alias")`, `backend/relay_core/board.py`). Say *work card*, *plan card*, *memory card*
  or *alias card* when the type matters. It is not narrowed to issue cards: that would leave the
  other three types with no noun.
- **ask** is what the agent puts up when a turn blocks on a question or an approval (`ask_user`,
  protocol 27, approvals 27.6). It is not a card and never was — it draws no surface, it is inline
  terminal text in `Ink::Ask`, and the code around it already says `m_ask`, `ask_user`,
  `approvals_ask`. Not "prompt", which is the prompt box; not "question", because approvals are not
  questions.
- **popup** is a floating panel over a pane: the help cheat-sheet `?` shows, the notifications list.
- **turn** is what the phone thread view renders. Not a "turn card".
- **model card** is the vendor's documentation page for a model, and is always written in full.
- A spec citation is `card #ID`, written in full every time and never shortened to "the card" in a
  later sentence — `app/meet.js` and `remote/cpace.py` did that, and the word then points at
  nothing.

## Shortcut hints (standing rule)

Relay teaches shortcuts Superhuman-style: when the user does something the slow way (mouse,
palette, typing a long form) and a faster keyboard path exists, it shows a brief hint.

**Whenever you add or change a feature that has a shortcut, slash command, prefix or other fast
path, add a hint for it** in the shortcut-hint registry (see `docs/ARCHITECTURE.md`, "Shortcut
hints"), covering the slow path that should trigger it. Keep hints short ("Next time: Ctrl+T"),
use the live Keymap text rather than hard-coded keys, and respect the per-hint show limit and
the global "Shortcut hints" setting.

<!-- relay:switchboard-policy start -->
## Board (Relay)

This project has a Relay board in `.board/`: its cards are the record of what was asked and
what was done, in plain Markdown in git. Read them there or in `.board/BOARD.md`. To search its cards with `rg`, name `.board/` explicitly or use `rg --hidden`; a plain project-wide `rg` skips hidden folders.

**Before doing work, read `.board/POLICY.md`** and follow it: check whether the request is already
done, find the card that asks for it or file one, claim it, plan on it if it needs a plan, do the
work, then land it in needs-verification with its evidence. The policy is the same one Relay's own
agents get in their system prompt; `.board/POLICY.md` also says how to do each of their `board_*`
tool calls by editing files, which is what you have.

<!-- Generated by Relay (relay_core.board.pointer_text): this block is replaced whenever the
     board scaffold runs. Edit around it, not inside it. -->
<!-- relay:switchboard-policy end -->
