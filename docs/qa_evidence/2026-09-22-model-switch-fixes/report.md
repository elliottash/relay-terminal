# Model selection fixes — #MSW7 and #YJG7

Implemented 2026-09-22. Original incident: session ef7ab5c3ef21445ab80dea50fedfbb29.

## Changes

- Role picks and direct picks now share transport installation, including guest startup and
  return to an API provider. A High guest chosen before the first prompt also starts correctly.
- A deferred guest is started at the step boundary, not while the old provider answers;
  a rejected startup preserves the old provider and allows its turn to continue.
- Role state is committed after transport installation. The GUI retains its current mode until
  acknowledgment, and failed switches use the explicit refusal path and actual active model.
- Main-page picks send one model switch instead of restoring old Main and then switching again.
- GUI `model_picker` info logs capture initial/changed display, opened rows and picked row data,
  alongside model, preset, role and effort. Worker `model_selection` records target and outcome.
- Before the catalog arrives, the label is `Loading models…`, replacing the misleading temporary
  `Claude Code` label found during the first drive (about 0.1 seconds in that fixture).
- Fallback errors retain the original reason and each fallback's own reason. Z.AI Coding Plan
  429/code 1310 stops immediately and reports its recognised reset timestamp in provider time;
  unrecognised 429s retain normal backoff. Arbitrary response bodies are not displayed.

## Verification

`tests.txt`: 299 targeted unittest tests passed across model switching, guest harness/worker,
failover, provider, roles and Board model selection. The final diagnostics-only change was
also checked with all 22 model-switch tests. New regressions cover initial High guest startup,
API → High guest → API, refused guest startup, deferred guest installation, a failed deferred
startup continuing the old provider's tool loop, exhausted quota and transient 429 behavior.

`build.txt`: `scripts/relay-build --target relay` passed. Earlier attempts encountered unfinished
changes from concurrent SSH/rank work; those sessions corrected them. Their changes are excluded
from this task's commit.

`drive.py`: real compiled Relay GUI and real backend worker under Xvfb, isolated XDG config/data,
controlled HTTP opener and guest harness only. No production credentials or paid model requests.
The fixture deliberately makes one guest startup fail and makes GLM quota/Muse authentication fail.
This proves UI/protocol/transport-selection behavior, not external provider availability.

The complete repeatable drive passed (`drive-result.txt`):

1. New pane and opened picker show Kimi as Main (01–02). Initial log says Loading models before
   catalog readiness, then Kimi; no transient claim that Claude is running.
2. Pick Codex / gpt-6-astra under High, then submit a prompt: guest actually answers (03–05).
3. Pick glm-5.3-flash under Flash and submit: API fixture answers on Flash (06).
4. Pick Kimi under Main and submit: API fixture answers on Kimi (07–08).
5. Pick deliberately unavailable gpt-5.6-sol under High: explicit startup refusal; reopened picker
   and next answer remain Kimi (09–11).
6. Pick GLM Main and submit: exactly one GLM HTTP request, then one Muse fallback request; error
   contains the GLM quota reset and Muse HTTP 401 separately (12). No transient retry waits.
7. Open a second tab/pane and its picker: it follows Main rank 1 (Kimi), not the first pane's GLM
   override; startup and open-row logs captured for that new pane (13–14).

`relay.log` contains exactly four successful model_changed events for the four successful picks,
in order: gpt-6-astra, glm-5.3-flash, kimi-k3, glm-5.3. The failed pick has model_switch_refused.
`worker.log` confirms the actual turn models. `transport.jsonl` records the fixture boundaries.
Screenshots and OCR text accompany every numbered stage.

`board-check.txt`: no diagnostics for these cards or threads; the board-wide checker still has
12 existing errors elsewhere. `tests-check.txt` records the card evidence check.

## Reproduce

```
scripts/relay-build --target relay
PYTHONPATH=backend:tests python3 -m unittest test_model_switch test_guest_harness_provider test_failover test_provider test_roles test_card_model_selection -q
python3 docs/qa_evidence/2026-09-22-model-switch-fixes/drive.py
```

The default drive runs all steps, asserts outcomes, saves evidence and terminates its isolated
processes. `--interactive` leaves that test instance running for command-file exploration; create
`stop` in the printed temporary directory to close it.
