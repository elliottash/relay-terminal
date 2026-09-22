# CFG1 verification — 2026-09-22 UTC

PARTIAL PASS / OPTIONS PROPAGATION FAIL. A new Xvfb display, isolated HOME/XDG profile, fixture board and local stub model exercised both Switchboard console contexts. No external model/account was used. A transparent worker stdin tee (`worker-tap.py`) forwarded the original protocol to the real worker and recorded selected messages in `wire.jsonl`.

- `03-card.png` creates the card console after the list console. `idle-start.txt` and `idle-end.txt` bracket 61 seconds with both contexts alive; no helper configure messages occur in that interval.
- `05-card-answer.png` shows the card’s successful discussion. Its configuration carries `name: card`, `surface: card:<fixture ID>`, and the card brief.
- `06-back-list.png` and `07-list-answer.png` show the return to the list and its successful ask. The worker switches to `name: switchboard`, `surface: switchboard`, with the list brief.
- `08-options-search.png` locates the Options “Step limit per turn” row; `09-limit-37.png` shows its value changed to 37. Only the terminal worker receives `set_agent_options` with `max_steps: 37`; the helper remains configured at 500. The driver assertion fails with `AssertionError: 500`. A subsequent context switch had masked this gap in a preliminary manual run.

Reproduce: `bash docs/qa_evidence/2026-09-22-verify-CFG1/drive.sh`. The script reuses fixture/UI helpers from the earlier AGNT drive, not earlier results. `relay-log.txt` and `worker-log.txt` are this run’s fresh logs.

Targeted checks: windowstate, consolemode and agentcontext all passed in recorded run `20260922T011245Z-3882`; no signals opened. CFG1 itself is proved by the live protocol observation, since its window-level routing has no isolated unit seam.

Implementation: `4764200e`. This verification used the current shared checkout, including unrelated uncommitted edits; none were altered or committed here. A scoped fix is needed: the Security step/tool-call limit rows are plain `numberRow` instances and never call the existing `alsoBoardWorkers()` wrapper. `proposed-fix.patch` is a reviewable two-row fix, not applied: the user requires coordination for shared code and `src/RelayWindow.h` is held by mdl1verify and other live sessions. No Relay messaging tools are exposed in this harness. A preliminary fixture attempt was stopped after discovering its old model defaults; the final driver pins the local stub. A preliminary Escape navigation stayed in the card console; the final driver targets the named `boardBack` widget instead (OCR of the Back link was also unreliable).
