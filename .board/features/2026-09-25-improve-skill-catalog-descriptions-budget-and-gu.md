---
id: G8JN
type: work
status: executing
labels: [feature, skills, worker]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:elliott-t-ash-gmail-com
session: 6fa2d509-138c-4a64-8910-c6cb50031874
parent: SZ1H
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'Catalogue rendering tests show whole-sentence triggers within 8 KiB and a guest block that omits the guest''s own harness tree while keeping Relay and other sources., sign_off: none, effort: low, stakes: rework, blast: capability} source: ''Owner approval on #SZ1H, 2026-09-25'' links: {plans: [], commits: [], evidence: [], related: [9FX8, GSK7], github: null'}
---
# Improve skill catalog descriptions, budget, and guest supplements

## Issue
Use short or the first complete description sentence for each skill, raise the catalog budget to 8 KiB, and avoid repeating the guest harness's own skill tree in Relay's supplemental block. Test the prompt rendering and budget behavior.

## Done means
Catalog rows use `short:` or a complete first sentence within an 8 KiB total budget. Guest supplemental skills do not repeat the harness's own home-tree catalog. Tests cover truncation, over-budget behavior and guest-specific source filtering.

## Plan
Refreshed 2026-09-26 against `053f4458`. No open product decisions.

**Findings.**
- `skills.py:21–34`: `MAX_SHORT=100`, `MIN_SHORT=40`, `MAX_DESCRIPTION=150` (unused in rendering), `MAX_PROMPT_BYTES=5*1024`. A line uses `short:` or a clipped description (160–196); rendering shrinks every trigger together from 100 toward 40 and then moves the overflow to a names-only trailer (273–365).
- The guest block is `guest_instructions.build_instructions` / `_with_skills` (`guest_instructions.py:82–108`): every indexed skill with name, trigger and path. The guest kind is known in `guest_harness_provider.start_provider` (`preset_guest_id`, ≈870; ids in `guest.py:51`) but is not passed to `build_instructions`. #GSK7 (global skills reach guests) is compatible: it decides what is included, this card removes only the harness's own tree.

**Steps.**
1. Trigger = `short:` when present; otherwise the description's first complete sentence. Clip only when that sentence alone exceeds the per-line ceiling, and then at a word boundary with an ellipsis. No uniform shrink of every line.
2. `MAX_PROMPT_BYTES = 8 * 1024`. Over budget: keep whole lines in catalogue order (Relay-owned, project, then external) and put the remainder in the existing names-only trailer.
3. Pass the guest id into `build_instructions`. Omit skills whose manifest resolves under that harness's own home tree: `~/.claude/**` for `claude`, `~/.codex/**` (and `$CODEX_HOME`) for `codex`. Keep Relay's own skills, `~/.config/relay/skills`, project `.relay/skills` and other harnesses' trees.
4. Coordination: #H7NF (executing) names the same guest outcome in its Done means; this card is where the filter lives, and #H7NF's check consumes it. #K26R's hidden skills drop out of both the pane catalogue and the guest block once it lands; this card does not wait for it.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_skills tests.test_guest_instructions -v` (create the latter if absent): sentence selection, `short:` precedence, single over-long sentence clipping, 8 KiB cap with trailer, Claude guest omits a `~/.claude` fixture skill but keeps a `.relay/skills` one, Codex guest likewise for `~/.codex`, a plain Relay pane unchanged.
