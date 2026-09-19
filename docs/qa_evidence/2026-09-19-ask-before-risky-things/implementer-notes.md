# Ask before the risky things (#K2FV) — implementer evidence

Opt-in approval cards: seven capabilities (edit, create, delete_or_move, read_outside,
terminal, program, network), a first-launch choice (allow everything — recommended — or
the cautious set), and a checklist in Options › Security. A card stops the turn until it
is answered; a deny refuses the tool with wording that says the user denied it.

## What landed

- `backend/relay_core/approvals.py` — capabilities, DECISIONS, CAUTIOUS, the policy
  (`ask` set + `chosen`), `needed()`, and `refusal()`.
- `backend/relay_core/questions.py` — `ask_approval(capability, subject)` on Questions:
  a `question {kind: approval}` out, a `question_answer {decision}` back, per-turn
  allowance cleared in `begin_turn`, `turn_allows()`.
- `backend/relay_core/tools.py` — `ToolExecutor.approvals`, `may_approve`, the
  `_approval()` gate at prepare time (re-checked at execute for `run_command`, minus
  what the same call already asked about), `Prepared.approved`.
- `backend/relay_core/agent.py`, `session_protocol.py`, `worker.py`, `subagents.py` —
  options wiring (`approvals_ask`/`approvals_chosen` under one `approval_options` key,
  like the #3KB7 security lists), subagents inherit at spawn and follow changes.
- `src/Pane.h` — the approval card: 1–4 answers only, no free text, Esc does not deny
  ("Answer 1–4 · Esc does not deny this one"), "Always allow" unticks the row and
  pushes the new policy before the decision is sent.
- `src/ApprovalsPane.h` (new) — the undismissable-until-answered first-launch pane, and
  `relay::approvals::cautious()`, the one list the checklist rows, the card's
  "Always allow" and the pane's choose button share.
- `src/RelayWindow.h` — Options › Security › "Ask before": seven checklist rows over
  `security/approvals_ask`, and the "Show the first-launch choice again" button row.
- `docs/AGENT-SESSIONS-PROTOCOL.md` — §12.1 (the two settings rows) and §27.6 (the
  approval card's question/question_answer shapes).
- `tests/test_approvals.py` — round trip, the four decisions, turn allowance, deny
  wording, subagent forwarding, options wiring, and the source-string tests that pin
  the GUI's readers (including the `rememberAlwaysAllowed` fix below).
  `tests/test_questions.py` — the unreadable-card refactor fix.

## The Xvfb drive

Run `drive.sh [build-dir]`; set `RELAY_QA_ONLY=a` or `RELAY_QA_ONLY=b` for one run.
Two sandboxes under the project's `tmp/` (never `/tmp`), isolated HOME/XDG_*, and a
loopback stub provider draw one card at a time and advance only on an answer. Nothing
leaves the machine: the one network call is denied at its card, and its host does not
exist anyway.

The shots (`implementer-a-*.png`, `implementer-b-*.png`) are described in drive.sh's
header. Textual evidence beside them says what actually happened:

- `state-<run>.conf` — Run A ends with
  `approvals_ask=edit, delete_or_move, terminal, program` and
  `approvals_chosen=true`: the read card's "Always allow" took `read_outside` off the
  cautious set, not off an empty list. Run B leaves all seven on.
- `stub-<run>.jsonl` — one line per model request. Its `results` count is the tour
  step; `last_content` carries the newest allow output or deny wording. Run A ends
  with the `type_into_program` refusal, run B with the network refusal.
- `sessions-<run>.tgz` — contains the pane transcript (every card, decision echo,
  Esc line and refusal), prompt history (one tour prompt, no stray turns), and full
  conversation. Run A's read inside the readable root returned
  `launch checklist: answer every card`.

### Bug found by the drive

The first drive caught `Pane::rememberAlwaysAllowed()` reading
`security/approvals_ask` raw. Before the first-launch choice the key is empty, so one
"Always allow" wrote "ask about nothing" and silently flipped the policy to allow-all.
The root fix starts from `relay::approvals::cautious()` while `approvals_chosen` is
false. The list moved into `src/ApprovalsPane.h` so the rows, card and first-launch
pane cannot disagree. The regression test is
`test_always_before_the_first_launch_choice_starts_from_the_cautious_set`.

### Harness-only phantom, resolved

The first drive's outside read was refused, suggesting `readable_roots` had not
reached the worker. A stale stub held the port and answered with an old sandbox path;
drive.sh also passed `$sandbox` where the stub wants `$sandbox/home`. It now starts the
stub after setup, passes the real home, kills strays first and proves the new stub is
up. The re-run's outside read succeeds; no product change was needed.

## Test results

- `./scripts/test.sh`: **3413 tests, OK** (`test-output.txt`). One earlier run had a
  timing flake in `test_web_meet_code`; it passed standalone and did not recur.
- `ctest --test-dir build`: **62/63**. The one failure is `buttonfit` — another
  session's committed `7e3fb9ff` (`src/Theme.cpp` 8.5pt against the 9pt floor); it
  fails on a pristine export of main too. Recorded in `issues/bug_intake.txt`.
- `relay-theme-tests` was not rebuilt: another session's in-flight
  `tests/theme_test.cpp` references `theme::contrastInk`, absent from their current
  `src/Theme.h`. Every other target was rebuilt and run. A one-line shared-build
  unblock in `src/RelayWindow.h` (`dynamic_cast` on a `QPointer` needs `.data()`)
  belongs to that session's diff and is not part of this card.

## Shortcut hints

None added: this work has no new fast path. Approval cards use the number keys that
question cards already teach; the first-launch pane is two Tab-reachable buttons; the
checklist is ordinary Options rows.

## For QA

Live under Xvfb with isolated `XDG_CONFIG_HOME`: first-launch pane; edit card;
"Always allow" unticking the row; deny wording; Esc retaining the card; and the
all-seven checklist drawing the create and network cards. The drive reproduces all of
it end to end.
