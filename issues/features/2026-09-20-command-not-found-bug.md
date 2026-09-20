---
id: EB4A
type: work
status: needs-verification
assignee: agent
implemented_by: kimi/kimi-k3
session: b2bc87a9-d7f0-49f9-a9d9-15e6b0768418
rank: zzzzzzzzzzzzzzz
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-command-not-found-agent-prompts/], related: [], github: null}
---
# command not found bug

## Issue
when i type agent prompts, the terminal is still often saying "command not found". here is an example:

✦ another session like that, same issue?

19c1ca7ec71d47019abe9aa536469582
command not found: another

session: 798d406f5d60481a89cd76b2c0ac7b0c

thats a bug

## Plan
**Goal.** A prompt typed for the agent — prose like "another session like that, same issue?" — must never print `command not found: …` under its ✦ echo, in any composer mode. The note stays only where a command was plausibly meant and mistyped ("gti status").

**Findings.**

- The note is the route decision's `invalid_reason`, printed as `prompt.why` under the ✦ echo when the agent turn starts (`src/Pane.h`, the prompt-send path: `printInline("✦ " + prompt.text …, Ink::UserAgent)` then `if (!prompt.why.isEmpty()) printInline(prompt.why + '\n', Ink::Note)`). The wording "command not found: another" is the router's own (`_resolve` in `backend/relay_core/router.py`); bash would say `another: command not found` — so this is Relay's note, not shell output. The other lines in the owner's paste (bare hex id, `session: …`) are the pane's turn/session bookkeeping.
- `backend/worker.py`'s `route` handler passes `classify()`'s Decision through unchanged; all gating is `router.py` + `Pane::dispatch()`.
- At HEAD, the owner's exact line routes correctly in **auto** mode: `classify()` → route `agent`, `valid: false`, and `explain_invalid()` returns False (traced: "another" is in `SIGNAL_WORDS`, so `_meant_as_command` returns False via `FUNCTION_WORDS`; no ambiguity → note suppressed). The 2026-09-18 fixes (card #W954, `docs/qa_evidence/2026-09-18-command-not-found-sentence-punctuation/`) cover this class — the owner's pane may predate them.
- Two latent gaps remain at HEAD, either of which re-opens the bug:
  1. `classify()`'s forced-agent branch (`if forced == "agent":`, composer chip on AGENT) returns the Decision **without setting `explain_invalid`**, so it ships as the default `True` — untrue for prose. Only the GUI-side `mode != "agent"` condition in `dispatch()`'s `why` gate keeps it quiet; any caller that trusts the field, or a mode mismatch, prints the note.
  2. `dispatch()`'s `route == "shell"` branch has an ungated `if (!valid) { submitAgent(text, true, problem); return; }` — it forwards `invalid_reason` as the note without consulting `explain_invalid` at all (reachable with a route-assist-rewritten or legacy decision).
- `src/Pane.h` is being edited by another session right now (dispatch moved ~74 lines during planning) — re-locate these by content, not line number.

**Steps.**

1. Reproduce at HEAD before changing anything: `pytest tests/test_router.py -k "sentence or punctuation or explains or agent_mode"`. Feed the owner's line verbatim through `classify()` in modes `auto` and `agent` and record what `explain_invalid` / the decision dict say. If auto mode already suppresses the note (expected), the work below is hardening + regression tests; if not, the trace above is wrong — find the real path first and fix that.
2. `backend/relay_core/router.py`, forced-agent branch of `classify()`: pass `explain_invalid=explain_invalid(text, reason, known, commands, cwd) if reason else True`, and extend the field's comment ("auto-routed" → any line sent to the agent). Valid text keeps today's dict byte-for-byte; invalid prose now says `False`.
3. `src/Pane.h`, `dispatch()`'s `route == "shell"` branch: gate the note — `submitAgent(text, true, decision.value(QStringLiteral("explain_invalid")).toBool(true) ? problem : QString())` in the `!valid` path, so an assisted/legacy shell-routed prose line cannot print the note either.
4. `tests/test_router.py`: add the owner's line and same-shape variants ("another session like that, same issue?", "same issue as before?", "the other session too?") asserting route `agent` and `explain_invalid` False in both `auto` and `agent` modes; keep an assertion that a real typo ("gti status" in agent mode) still explains itself. Update `test_agent_mode_reports_runnability` (and any exact-dict comparison) for the new field value.

**Risks.**

- **Owner question:** the example is already handled at HEAD in auto mode — was that pane running a build older than the 2026-09-18 fixes? Recommendation: rebuild and retype the line before assuming code is still wrong; the fixes above are worth landing either way.
- Changing the forced-agent Decision changes the decision dict; exact-dict tests and any GUI code reading `explain_invalid` from agent-mode replies must be checked (only `dispatch()` reads it today).
- The GUI's `mode != "agent"` gate stays: agent-chip submissions never print notes even for real typos (the `shellText` hint covers valid commands). Dropping that gate would be a product change — not this card unless the owner asks.

**Verify.**

- Targeted router tests only (house rule): `pytest tests/test_router.py -k "sentence or punctuation or agent_mode or explains"` — all green with the new cases.
- Build with `scripts/relay-build` (never bare cmake). The C++ change has no unit-test harness (`dispatch()` is untested), so verify live under Xvfb with an isolated `XDG_CONFIG_HOME`: type the owner's line with the chip on AUTO and on AGENT, and confirm no `command not found` note under the ✦ echo; type `gti status` in AUTO and confirm the note still appears.
- Land in `needs-verification` with evidence under `docs/qa_evidence/2026-09-20-command-not-found-agent-prompts/` (before/after router outputs, Xvfb transcript) and a QA checklist.

*Small plan — no subagents; all writes stay with the executing agent.*

## Tests
tests/test_router.py::WrongModeSignalTests::test_agent_mode_prose_does_not_explain_itself
tests/test_router.py::WrongModeSignalTests::test_agent_mode_reports_runnability
tests/test_router.py
manual: docs/qa_evidence/2026-09-20-command-not-found-agent-prompts/

## QA checklist
1. **Quiet, AUTO.** Submit `another session like that, same issue?` and `same issue as before?` with the chip on AUTO: each goes to the agent with the ✦ echo and **no** `command not found` note under it.
2. **Quiet, AGENT.** The same two lines with the chip on AGENT (Ctrl+I twice from auto): no note. Router level: `classify(<prose>, "agent").explain_invalid` is `False`.
3. **Explained.** `gti status` in AUTO keeps `command not found: gti` under the echo; `classify("gti status", "agent").explain_invalid` stays `True`, and a valid command's agent-mode decision dict is byte-for-byte what it was before the change (`explain_invalid` default `True`).
4. **Tests.** The three invocations in `## Tests` pass; `drive.sh` in the evidence dir re-runs the live check and reproduces `implementer-01..04`.
5. **No regression in fixed modes.** `tests.test_ssh_remote` and `tests.test_routing_thinking_skills` (the other `classify` consumers) pass.
