---
id: P9ZA
type: work
status: needs-verification
labels: [feature, workflow, land]
assignee: claude-code
parent: 3MH4
discovered_from: 3MH4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-text], human: none, sign_off: none, effort: high, stakes: rework, blast: capability, criteria: 'tests/test_reconcile.py passes on the landed tree: the weighted High draw, durable case/day budgets, the two-attempt loop and every conservative refusal are each proved by a test against real Git worktrees'}
source: 'Approved #3MH4 implementation workstream, 2026-09-26'
links: {plans: [], commits: [bdb8c79683df], evidence: [], related: [RT3B, FW1C, ASQ4], github: null}
---
# A4: Automatic AI reconciliation with weighted high models

## Issue
Resolve conflicts through the existing weighted high-tier model routing, bounded tokens and retries; hand failed or unsafe patches to the author agent.

## Done means
- `relay_core.reconcile.Reconciler(state_root=...).reconcile(context, model_call=None)` (async, plus a sync `reconcile_sync`) takes the A2 context (`repo, repo_id, job_id, base_sha, target_sha, submitted_sha, candidate_path, cards, intents, conflicts, diagnostics, policy`) and returns `status` (`resolved` | `author_required`), `patch`, `resolved_paths`, `model`, `preset`, `account`, `effort`, `tokens`, `attempts`, `reason`, `trailer` (`Reconciled-From: <target-sha> <submitted-sha>`) and `card_notes`.
- Production draws the model from the configured `high` list (context policy → the GUI's stored `tier/high` list → `presets.tier_list_defaults` from what can take a turn now) through `roles.RoleResolver.choose_role("high")`, i.e. `ordered_candidates(choose=True)` with the subscription weighting and guest-account expansion; guest entries run through `guest_harness_provider.start_provider` (read-only permissions, cwd = candidate), API entries through `provider.make_provider`. No vendor model id is hard-coded; the effort is the entry's own or the model's top level.
- Budgets are durable in `state_root/integration/reconcile.sqlite3`: a case/day reservation is written before every call, settled with input/output usage afterwards, and a reservation left open by a crash keeps counting at its reserved size. Exceeding `tokens_per_case` / `tokens_per_day` refuses the call and returns the conflict to the author agent. At most `max_attempts` (≤ 2) model calls per case.
- A proposed patch is rejected when it touches a path outside the conflicted set, leaves conflict markers, removes or weakens tests (dropped test functions/assertions, added skips), or drops either side's added lines / both sides' kept lines beyond a small tolerance. Rejection never asks the owner: `author_required` carries the diagnostics and the rejected patch for the author agent. The reconciler only writes into `candidate_path`.
- Failure is recognised by `tests/test_reconcile.py` (real temporary Git repos, injected `model_call`, `FakeHarness` for the guest path, a loopback OpenAI-compatible server for the API path) failing, or by any of the above returning `resolved` for a patch a check should refuse.

## Plan
**Goal.** A4 of `docs/TREES-AND-LANDING.md`: automatic, gate-bound conflict reconciliation on the weighted High tier with bounded tokens and conservative refusal.

**Findings.** `backend/relay_core/roles.py` already owns the weighted draw (`ordered_candidates(choose=True)`, `_usage_weight`, guest/key account expansion) and role resolution (`RoleResolver.choose_role`, `_tier`, `_guest`). `presets.tier_list_defaults` builds the default lists from usable providers; the GUI stores the user's lists in `$XDG_CONFIG_HOME/RelayTerminal/relay.conf` `[models] tier\high` as `preset|model|effort|rank=N` strings. `guest_harness_provider.start_provider` / `HarnessProvider.complete` run one guest turn and emit `usage`; `provider.make_provider` + a no-tools `complete` is the API path (`sidecall.call`). No `landq`/`trees` module exists yet, so the context shape is the contract's.

**Steps.**
1. `backend/relay_core/reconcile.py`: `ReconcileError`, policy normalisation (`enabled`, `max_attempts`, `tokens_per_case`, `tokens_per_day`, caps), `BudgetLedger` (sqlite WAL, reservations, settle, uncertain sweep, day/case totals).
2. Candidate inspection: conflicted paths from `conflicts` or `git ls-files -u` in `candidate_path`; base/ours/theirs from the three SHAs; capped prompt assembly with cards and intents.
3. Model routing: `high_entries()` (policy → stored list → defaults), `RoleResolver.choose_role("high")` per attempt, `call_model()` adapters for API and guest; injectable `model_call`, `key_lookup`, `guest_check`, `tiers`.
4. Patch parsing (JSON files or a `give_up`), safety review (`review_patch`), write into the candidate, `git add`, unified `patch`, trailer and idempotent card notes (`append_card_notes` through `board.Board.append_thread`, keyed by job id).
5. Attempt loop: redraw excluding a failed provider on transport/quota errors, feed review diagnostics into the second attempt, return `author_required` with the handoff otherwise.
6. `tests/test_reconcile.py` covering each Done-means line.

**Risks.** The stored-list reader parses a QSettings ini by hand (tolerant, falls back to defaults). The review is heuristic by design; the project gate stays mandatory. Guest turns cannot be limited to a token count from outside; the completion cap applies to API calls and the guest's usage is settled after the fact.

**Verify.** `PYTHONPATH=backend python3 -m pytest tests/test_reconcile.py -q`.

## Execution Summary
- `backend/relay_core/reconcile.py` (new): `Reconciler(state_root=None)` with async `reconcile(context, model_call=None, board_root=None, cancel=None)` and the sync `reconcile_sync` the A2 queue can pass as its `reconcile=` callback. Returns `status`, `patch`, `resolved_paths`, `files` (sha256), `model`, `preset`, `account`, `effort`, `tokens`, `attempts`, `reason`, `trailer`, `card_notes`, `handoff`, `conflicts`, `list_source`, `policy`.
- Routing: `high_entries()` takes the list from the context policy (`tiers.high`), else the GUI's stored Options › Models list (`$XDG_CONFIG_HOME/RelayTerminal/relay.conf`, `[models] tier\high`, minus un-ticked models), else `presets.tier_list_defaults` from what can take a turn now. `draw_high()` resolves through `roles.RoleResolver.choose_role("high")`, i.e. `ordered_candidates(choose=True)`: ranks, the subscription-weighted draw, guest and plan accounts expanded from their family rows. `with_high_effort()` fills a missing level with the model's top one. A provider that errors is excluded from the redraw.
- Adapters: API entries through `provider.make_provider` with `max_tokens` capped at the policy's completion budget and a no-tools completion; guest entries through `guest_harness_provider.start_provider` with `permissions: deny`, the candidate as cwd and no Board bridge; the guest's reported model and session id go into the record.
- Budgets: `BudgetLedger` in `state_root/integration/reconcile.sqlite3` (WAL, IMMEDIATE reservations). A reservation of estimated prompt + completion is written before each call and charged per case and per UTC day; settled with input/output usage; `uncertain` (still charged at the reserved size) when usage is unknown or the holding process died; `released` only when the guest never started. `history(job_id)` and `budget_status()` read it back.
- Review (`review_patch`): only conflicted paths, every conflicted path returned, no conflict markers, no elision placeholder, not emptied or halved, neither side's added lines dropped (one in ten allowed above ten lines), no line both sides kept dropped, and on test-like files no test function lost, no assertion count drop, no skip added. Refusals return `author_required` with diagnostics, the rejected patch and an author handoff; the candidate is left as it was.
- Card notes: `card_notes()` and `append_card_notes(board_root, notes)` add one `note` entry per card through `board.Board.append_thread`, keyed by `reconcile:<job>:<status>` so a retry adds nothing twice.
- Integration hooks for B1/A2: `Queue.process_one(verifier, reconcile=Reconciler().reconcile_sync)`; pass `policy` as the accepted config (its `[reconcile]` table and, optionally, `tiers.high`); on `resolved` commit the staged candidate with `result["trailer"]` appended to the message and call `append_card_notes(board_root, result["card_notes"])`; on `author_required` deliver `result["handoff"]` and `result["patch"]` to the author agent.

## Tests
- `PYTHONPATH=backend python3 -m pytest tests/test_reconcile.py -q` — 25 passed (also green under `python3 -m unittest tests.test_reconcile`).
- Real temporary Git repos with a stopped merge in a worktree candidate; injected `model_call` for the resolve/refuse/budget/attempt paths; `tests/guest_harness_fake.FakeHarness` behind `guest_harness_provider.make_harness` for the guest adapter (family row and a registered account); a loopback OpenAI-compatible SSE server for the API adapter; a hand-written `relay.conf` for the stored list; a minimal board for the idempotent notes.
