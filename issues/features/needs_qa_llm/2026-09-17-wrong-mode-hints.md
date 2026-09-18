---
id: 94V5
type: work
status: needs-qa-llm
component: [gui, router]
milestone: desktop-alpha
workstream: routing
assignee: implemented by Warp agent (auto), 2026-09-17/18
rank: gu
created: '2026-09-17'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: 'owner request: terminal-mode clear agent prompts and agent-mode failing shell commands should flash the mode chip and toast Ctrl+I'
links: {plans: [a4ca95f7-d369-4f06-a7a3-eab133c6999d], commits: [], evidence: [docs/qa_evidence/2026-09-17-wrong-mode-hints/], related: [issues/features/needs_qa_llm/2026-09-17-ctrl-i-input-toggle.md], github: null}
---
# Wrong-mode error hints (flash mode chip + Ctrl+I toast)

## Behavior as implemented

When a submission errors and clearly belongs in the other input mode, the pane flashes the mode chip in the destination colour and shows a 5 s toast naming `input.toggle` (live Keymap text). Both go through the shortcut-hint gates (global setting, per-id limit 3, per-id cooldown 600 s, global 20 s gap). Unbound `input.toggle` stays quiet.

- **Terminal mode, clear request** (invalid + `agent_signal`): nothing runs, composer keeps the text, one ✗ line ("this reads like a request for the agent, not a command"), toast `mode.requestInTerminal` ("That read like a request, not a command · Ctrl+I switches to agent mode"), violet chip flash. No fix loop.
- **Terminal mode, natural command that runs and fails** (valid + `agent_signal`, non-zero exit ≠ 130): same toast/flash alongside the usual fix attempt (`m_commandNatural` carried through `submitTerminal` → `runInTerminal` → ready handler).
- **Agent mode, shell command whose `run_command` fails**: toast `mode.commandInAgent` ("Shell command in agent mode · Ctrl+I cycles input modes · ! runs one line in the terminal"), cyan chip flash, at most once per turn. Match is `commandMatchesPrompt` after stripping a leading `cd <dir> && `.
- **Cooldown**: a second wrong-mode hit of the same hint id within 600 s still prints the ✗ line (or runs the agent turn) but shows neither toast nor flash.

Router (`backend/relay_core/router.py`): `Decision.agent_signal` is computed from NATURAL/assist signals in every mode; forced agent mode still runs `check_runnable`.

Toast placement: `Pane::placeToast` anchors to the terminal host's bottom-right and re-anchors on host resize so a fix-loop agent transcript that grows the composer does not bury the toast.

## Implementer check (not a QA verdict)

Xvfb drive (`docs/qa_evidence/2026-09-17-wrong-mode-hints/drive.sh`) with isolated `XDG_*`, loopback `stub-provider.py` (no network, no keys):

- **A1** ✓ shot `implementer-01-…`: ✗ line + toast "That read like a request… Ctrl+I switches to agent mode"; text kept.
- **A2** ✓ product path: stub skips the `[Relay context…]` block and runs `cd … && git stauts`; `tool_result` matches `m_turnShellPrompt` and fires `mode.commandInAgent` (log-proven). Drive harness can flake when the BYOK dialog stays open after Save (OCR-click + Return); `configure_pane` now verifies Cancel is gone and retries.
- **B1** ✓ shot `implementer-07-…` crop: toast "That read like a request… Ctrl+I switches to agent mode" while the fix attempt runs (placeToast re-anchor).
- **B2** ✓ log + shot `implementer-09-…`: ✗ line prints, `mode.requestInTerminal: gated` (cooldown from B1).

The temporary `[wmh]` diagnostic lines from the instrumented diagnosis run have been removed from the stderr logs.

Evidence: `docs/qa_evidence/2026-09-17-wrong-mode-hints/` (`drive.sh`, `stub-provider.py`, `verify.sh`, `implementer-*.png`, stderr logs).

## QA checklist

1. Terminal mode, type a clear request ("how do I list files by size"), Enter: nothing runs, text stays, one ✗ line, toast names Ctrl+I, mode chip flashes violet.
2. Ctrl+I to agent mode, type a misspelled shell command the agent will re-run ("git stauts"), Enter: when that `run_command` fails, toast names Ctrl+I / `!`, chip flashes cyan; a second failing command in the same turn does not toast again.
3. Fresh session, terminal mode, type a natural-language command that still runs ("find the largest files"), Enter: command fails, toast + flash appear beside the fix attempt.
4. Immediately submit another clear request: ✗ line still prints, but no toast and no flash (per-hint cooldown).
5. With Shortcut hints disabled (or `input.toggle` unbound): neither toast nor flash on any of the above; the ✗ line / fix loop still behave.

## Owner decision and review (2026-09-18)

- **Rule change, owner:** an invalid line that reads like a request, submitted in Terminal mode,
  runs nothing and is not handed to the fix loop; the ✗ line and the Ctrl+I hint replace it. This
  supersedes the earlier "Ctrl+Shift+Enter always sends an invalid command to the fix loop".
- **Reviewed** (Claude, 2026-09-18). The product code arrived through two WIP snapshot commits
  (`c9c88aa`, `598f3ea`) rather than its own commit; it is now reviewed. No correctness bugs found.
  Follow-ups applied: the agent-mode match moved to `relay::input::commandMatchesPrompt`, which also
  strips a quoted `cd "<dir>"`, a `cd <dir>;` form and a trailing `2>&1`, with unit tests.
- QA note: run A2 and B2 independently — B2 only passes after B1 because it relies on B1's cooldown.
