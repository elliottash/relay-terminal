# Independent verification of #7BM4

Date: 2026-09-21 America/New_York (2026-09-22 UTC). Verifier: gpt-6-astra via Codex, Relay a2.
Checked source revision: `0241d05ef19393e84c0ef98655c9687466a92efc`.
Clean `git archive` export at `/tmp/verify-7bm4-fresh/src`; application and four named GUI test targets built with that export's `scripts/relay-build`, its exclusive build lock held. Qt 5, Linux aarch64. No application source edits.

## Tests

All six card-listed backend modules passed together: 237 tests (`backend-tests.log`).
All four card-listed CTest selections passed: testsuites, cardtests, profilepane, windowstate (`gui-tests.log`). Build output: `build.log`.
The card has no `Done means` section: missing evidence, not an inferred acceptance checklist.

## Fresh mechanical simulation

The existing `scenario/stage.py` created a new orders fixture at `/tmp/verify-7bm4-fresh/orders-own`. A separate app ran on Xvfb :173 with isolated HOME, XDG paths and TMPDIR. Inputs were xdotool clicks and typing in GUI fields only.

- `scenario/01-card.png`: 4 tests listed on the short fixture card.
- `scenario/02-checked.png`, `03-done-blocked.png`: Check identifies totals_large_order failed, invoice never run, rounding retired. Done is refused, with failing and missing-evidence tests named. Modern retired-test behavior differs from the stale human walkthrough: retired rounding is not applicable, not a blocker.
- `scenario/04-tests-pane.png`, `05-history.png`: 7 tests; inventory_sync first, 70% reliability over 20 runs, flake score 5.3; history shows desktop/laptop and last failure text. report_export is slow, p95 3.89 s.
- `scenario/06-profile-menu.png`: four targets offered.
- `scenario/08-profile-result.png`: actual clean fixture profile, report.cpp.o 2.7 s / 94.7% of compile time.
- `scenario/09-attach.png`, `10-attached.png`: selected the staged build-slow card; its Profile table and links.evidence were written.

## Findings

1. The real #7BM4 is longer than `board_tools.MAX_TEXT` (16384 characters); its actual Tests heading starts at character 17576 of the 25767-character exported body; `board_read` truncates its body before Tests. The GUI says `Tests 0 listed`, although Check's backend sees the full test list. `10-card.png` and the exact clean source `backend/relay_core/board_tools.py::_read` substantiate this.
2. On that long real card, Check findings render as clipped fragments at 1600x1000 (`05-wait.png`, `08-max.png`); the short fixture renders correctly (`scenario/03-done-blocked.png`).
3. `Done means` is absent. The implementer's QA checklist cannot replace it.
4. Verifier board updates/QA transition overwrite `implemented_by`: the original `anthropic/claude-fable-5.1` became `kimi/kimi-k3`. The board_read immediately before updates still returns Anthropic; the two board_update_card calls supplied only id/base_hash/replace_section, and board_move_card supplied id/reason/status/evidence, never an implementer field (`kimi-board-calls.json`). `board_tools.py` stamps the current signature whenever status is needs-verification, including section-only updates, and again on first entry into QA. The resulting GUI recommends no independent verifier because it now thinks Kimi implemented the card (`20-verify-record.png`).

## Real-model flow

The exported committed card lacks the implementer identity present in the shared working card, disabling Verify. To exercise the flow, the current working card was copied into the disposable export and its status set to needs-verification. This is fixture metadata, not an implementation modification or a new identity claim. Original export card retained at `/tmp/verify-7bm4-fresh/card-before.md`.

GLM-5.3 Verify was actually clicked (`12-verify-launched.png`), but ended with HTTP 429 after six retries and zero tools. Screenshot `13-glm-failed.png` is failure evidence, not completion.

Kimi K3 was available using configured credentials, loaded into the app's environment without printing or storing key values. The second Verify button run started and has made real tool calls (`16-kimi-start.png`, `17-kimi-tools.png`). Verify completed at 01:44:33 UTC: `outcome=done`, 64 tools, 0 open items, 1384.654 seconds. It wrote QA checklist/Verdict, a RESULTS.md and stage.sh, and moved the disposable card to needs-qa-llm. See `kimi-final-card.md`, `kimi-results.md`, `kimi-board-calls.json` and `20-verify-record.png`. Its pass verdict covers its staged short-card simulation; it did not discover the long-card failure reported above. The subsequent Try it click at 01:44:50 UTC started another real Kimi turn. It completed at 01:51:42 UTC (`outcome=done`, 17 tools, 0 open items, 411.887 seconds), wrote its staged handoff and sealed expected result, and reused the verifier staging. The actual Try it heading was at character 27960 of a 28896-character body. `25-try-written.png` shows a bare Try it label and answer field, with no task/question/Open button. The question was only visible in the model narrative. A clearly labeled AUTOMATED VERIFIER TEST was typed into that answer field and submitted by Return; the GUI confirmed Answer recorded (`26-answer-submitted.png`). No owner judgment about trust, speed or usability was supplied. The expected result was revealed on disk, but the long-card GUI still could not show it. Thus the full Verify → Try it → answer sequence was exercised to completion, with a failing UX/preservation verdict rather than a pass. See `flow-summary.json` and `kimi-answer-card.md`.

Human-only judgments and all three original unanswered Human QA questions are preserved. No shared implementation files changed. relay_board tools, including parent messaging/delegation, were not exposed in this harness; file fallback used.

## Additional end-to-end finding

5. Answer submission replaces the existing Human QA section. Compare `kimi-final-card.md` (after Verify, three original unanswered questions present) with `kimi-answer-card.md` (after answer, only one generated question plus the clearly automated response remains). The original three questions were lost in the disposable copy despite the response explicitly saying to preserve them. The shared project card was not the flow target and retains all original questions byte-for-byte. This is an implementation fault reported to the parent; no code fix or synthetic owner answer was made.

## Shared-checkout validation

`python3 scripts/relay-board.py check` checked 454 cards and reported 13 errors / 748 warnings elsewhere; no output names #7BM4 or its card path. No broad fixes attempted.
`land.py commit --dry-run` refused a merge conflict in the shared `issues/threads/7BM4.md`; it wrote no commit and left the working tree intact. Parent session `delivery-cards` also holds this card/thread. Evidence and verifier edits remain available to the parent; do not resolve by replacing the thread.

## Final verifier outcome

**Failed; leave #7BM4 open/executing.** All automated Tests lines passed, the three fresh mechanical scenarios played successfully on the short fixture, and both real-model turns plus GUI answer submission completed. Five findings above prevent a pass. Real-model captures/scripts are copied into `model-verify/`; `kimi-results.md` is the model's report, not a replacement for this verifier's findings.

Final board-format run checked 455 cards: 11 errors / 748 warnings elsewhere, no #7BM4/card-path diagnostic. Fresh stage.sh passed bash syntax checking; both interactive Python drivers byte-compiled. Both isolated driver-owned apps and Xvfb displays were stopped. The shared card's Human QA text matches the pre-edit snapshot and has zero Answer lines. No implementation changes, no commit, no branches/worktrees, no shared checkout cleanup. Parent must reconcile and land card/thread/evidence; the land.py session is `verify-7bm4-fresh`.
