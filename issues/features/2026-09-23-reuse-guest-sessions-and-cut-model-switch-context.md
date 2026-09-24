---
id: Q8TM
type: work
status: needs-verification
labels: [feature, guests, context, models]
assignee: agent
implemented_by: glm/glm-5.3
session: bd9e4ae0-caa0-4e43-ba0b-f26212835b63
rank: m
created: '2026-09-23'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: owner in Codex, 2026-09-23
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-24-guest-switch-resume-Q8TM/'], related: [1V4F, 0C0V, 3ES1], github: null}
---
# Preserve context while reducing model-switch token use across guest and native models

## Issue
got it. write that into a card, and check that you make it where the same approach assists with token usage for claude, glm, and kimi. 

put any other suggestions you noticed in the card

Earlier requests in this conversation:

check out my codex usage today. can you give me some advice about reducing credit usage

can you tell, are there issues with how relay is interacting with the harness that wastes tokens

can you also research how codex, claude, and warp deal optimize model switches

## Planning notes
- The 2026-09-23 Codex log review found roughly 8,800 model responses across 88 sessions, about 1.18 billion input tokens, approximately 98% cached input, and 25 cross-harness handovers. Handover text had a median near 85 KB and reached about 164 KB. These are token and character counts, **not measured credit charges**. A cached token can still cost credits, and model-call count matters.
- `guest_harness_provider.switch_model()` already reuses a live harness when only its model changes. Codex keeps its thread; Claude Code uses `set_model` or restarts on the same session. Preserve these paths. Switching away from a guest closes it; switching back creates a new guest and `handover_brief()` repeats up to 160 KB of the conversation, including earlier turns that guest already saw. This is the specific repeated-context opportunity left after #1V4F.
- GLM and Kimi use `ChatProvider.complete()`, which sends `wire_messages(messages)` to `/chat/completions` on every call. Relay already retains and converts their conversation on a model switch (#3ES1). There is no guest session ID to resume here. The shared idea is **track what each model has seen and avoid avoidable reintroduction of old material**; the transport differs by provider.
- #0C0V already adds per-turn usage, guest handover size, bounded native tool output, stable prompt prefixes, and optional compaction. Build on those records and controls; do not duplicate their UI or undo their cache-preserving choices.
- Research context: [OpenAI prompt caching](https://developers.openai.com/api/docs/guides/prompt-caching), [Claude Code model configuration](https://code.claude.com/docs/en/model-config), [Claude cache diagnostics](https://platform.claude.com/docs/en/build-with-claude/cache-diagnostics), and [Warp model choice](https://docs.warp.dev/agents/inference/model-choice/). These describe model selection and caching; none establish that a cross-provider switch can share a provider's private cache. Treat any cache saving as a measured outcome, not a guarantee.

## Done means
- Returning to a prior Codex or Claude guest session preserves that guest's own prior turns and gives it every intervening Relay turn and tool result exactly once, without a full replay of turns it has already seen. Restart, stop, account changes, explicit resume/fork, and failed handoffs have safe behavior and no lost context.
- A native GLM/Kimi switch retains the full meaningful conversation and applies the same bounded-context and stable-prefix policy; any provider-specific incremental transport is used only when supported and shown to preserve behavior.
- Usage evidence compares baseline and changed paths for Codex, Claude, GLM, and Kimi: input, cached input, cache writes where reported, calls, handover size, estimated or reported cost, and task outcome. A regression in correctness or total cost fails the proposal even if fewer text bytes are sent by Relay.

## Plan
### Goal
Cut repeated model-switch context while keeping the owner's #1V4F requirement that no conversation context is lost.

### Findings
1. `backend/relay_core/guest_harness_provider.py`: same-guest model changes reuse the harness; cross-preset returns start fresh and prepend `handover_brief()` in `complete()`.
2. `backend/relay_core/session_protocol.py`: switches between guest and native providers replace the provider, closing the previous guest; a smaller target window can trigger compaction before the switch.
3. `backend/relay_core/provider.py`: native GLM/Kimi requests send the Relay message list through the stateless Chat Completions shape. Cache usage fields for Moonshot and Z.AI are already normalized by `cache_counts()`.
4. `backend/relay_core/agent.py`: native conversation conversion and compaction already exist. #0C0V adds observability and native token controls; #3ES1 covers mid-turn native switching.

### Steps
1. Record a per-pane, per-guest-session cursor at each guest turn: guest family/account, session ID, last Relay message or turn delivered, and a durable mapping to the pane conversation. Keep enough state to resume Codex and Claude when a pane returns to that same guest. Invalidate it on explicit new/fork, incompatible account, rewind, or unavailable session; test restart recovery.
2. On return, resume the guest's session and deliver only the intervening Relay turns, including user messages, assistant turns, tool calls and results, in order. Keep a bounded handover for a genuinely new guest or a session that cannot resume. Use a lossless full-context fallback when a compact summary would omit needed facts; never silently trim to save tokens. Deduplicate only material demonstrably present in the resumed session.
3. Reuse the existing native transcript path for GLM/Kimi. Verify `adapt_history()` retains provider-required reasoning/tool-call structure across GLM ↔ Kimi and guest ↔ native switches. Measure whether #0C0V's bounded tool results, stale-result clearing, prefix stability, and opt-in compaction reduce billed input on both providers. Investigate stateful or incremental provider APIs separately and enable them only after capability checks and equivalence tests; the current `/chat/completions` path still requires the full valid history.
4. Add switch-path accounting: fresh guest versus resumed guest, old/new session IDs (redacted in UI), intervening-turn count, handover characters, input/cached/cache-write/output tokens, provider-reported cost or clearly labelled estimate, and cache-prefix changes. Attribute costs across the first several turns after a switch, not just the first handoff.
5. Replay four patterns on each applicable provider: same-guest model change, guest → GLM/Kimi → same guest, guest → other guest, and GLM ↔ Kimi. Compare task success and total cost with the old path. Test long tool-heavy turns, a restart, unavailable resume, compaction at a smaller window, and a switch during an active turn. Add protocol docs for the distinct guest/native paths.

### Other suggestions
- Keep same-guest model switches on the existing live harness; avoid bouncing between model families for small subtasks when it creates fresh sessions or cache misses.
- Use cheap model roles for narrow reading, search, and summarizing tasks as in #0C0V, then measure whether delegation overhead outweighs the saving for short tasks.
- Surface repeated giant handovers and unexpected prefix changes in the existing usage view; make the cause and the charged versus cached counts visible before recommending compaction.
- Consider an explicit compact handover plus retrievable transcript ranges only after measuring answer fidelity against the full handover, especially for tool output and decisions. A 160 KB cap can already omit old context; clearly signal truncation and provide retrieval.
- Do not use raw input token totals as subscription credit totals. Compare provider or Relay credit records when available, and retain cache-read prices in estimates.

### Risks and verification
- Resuming a guest without the intervening delta violates #1V4F. Replaying its own prior turns wastes context and may confuse it. Cursors, deduplication, failed sends, and resume fallback need focused regression tests.
- Cache behavior can vary by model and provider; a shorter prompt may rebuild a cache and cost more. Require before/after usage and outcome evidence for Codex, Claude, GLM, and Kimi before choosing defaults.
- Verify with fake guest harness tests, native provider payload tests, and a small number of explicit live A/B turns. Use #0C0V's usage records as the measurement source. No live subscription calls are required merely to plan this card.

## Execution Summary

Delivered 2026-09-24, pane bd9e4ae0 (GLM 5.3). The plan's five steps landed as:

1. **Cursors** — `guest_harness_provider.cursor_key/guest_cursor/_note_cursor` keep, per pane
   and per (guest, account), the session the guest ran and how far it read (`{session, messages,
   head}` — `head` is an 80-char fingerprint of its last reply). Durable: `attach()`'s session-data
   wrapper writes `guest_cursors` into the session file, and `resume_session()` restores it, so a
   restart still resumes. Invalidated the four ways the plan names: an explicit `guest.resume` /
   `guest.fork` wins over the cursor, another account's cursor is never read (#M8S2), a
   rewind/fork on the pane is caught by the fingerprint (count-or-head mismatch → full brief), and
   a session the guest no longer holds falls back inside `start_provider`.
2. **Resume + delta** — `start_provider(resume_cursor=…)` restarts the harness on the pane's own
   session (Codex `thread/resume`, Claude `--resume`) and `complete()` delivers only the
   intervening Relay turns — user turns, assistant turns, tool calls and results, in order, via
   `render_transcript` (so the #1V4F rendering is the same) inside a `CATCHUP_NOTE` block, capped
   by the same 160 KB handover cap. Nothing already in the resumed session is re-sent; the
   fingerprint check deduplicates exactly, not heuristically. The fallback is the existing
   lossless full handover brief — never a silent trim. A failed send does not consume the
   catch-up: it is retried with the block still in the prompt.
3. **Native path** — untouched by construction: handovers and catch-ups are composed inside
   `complete()` and never appended to `agent.messages`, so GLM/Kimi keep receiving the whole
   conversation with #0C0V's pinned prefix. `test_model_switch`'s conversion tests and the
   handover suite's endpoint round trips (unchanged) are the evidence; live per-provider cache
   A/B is runtime evidence the owner's usage view will now show through the accounting below.
4. **Accounting** — `model_changed` carries `guest_resumed` and `guest_resume_fallback`; the
   first turn's `status` event carries `resumed_session`, `catchup_messages`, `catchup_chars`;
   the turn record carries `guest_resumed`/`catchup_messages`/`catchup_chars` beside #0C0V's
   `handover_*`; and `logs` records `guest_catchup`, `guest_catchup_fallback`,
   `guest_resume_failed` and `cursor_resumed` on `guest_harness_started`. Session ids stay
   redacted in the UI as before (status lines name them, payloads do not).
5. **Replay** — the four patterns and every fallback on the scripted FakeHarness in
   `tests/test_guest_handover.py` (see `## Tests`); protocol doc updated
   (`docs/AGENT-SESSIONS-PROTOCOL.md`, the guest switch bullet).

Evidence: `docs/qa_evidence/2026-09-24-guest-switch-resume-Q8TM/` (README + the 113-test
would-land run: `git archive HEAD` plus exactly this card's files).

Not done here, and why: live cross-provider A/B turns and per-provider billed-input comparison
(run against real subscriptions — the owner's to run; the records to read them are what this
card adds), and the stateful/incremental native APIs (plan step 3 keeps them behind capability
checks; nothing to enable today).

## Tests

`tests/test_guest_handover.py` — the switch replay suite, all on FakeHarness, no real guest:

- `test_switching_back_to_a_guest_resumes_its_session_and_gets_only_what_ran_since` — guest →
  native → guest: the harness restarts on its own session, only the native turn is delivered,
  never the guest's own earlier turns; a second round trip delivers only what ran since.
- `test_a_return_with_nothing_to_deliver_sends_the_prompt_bare` — resume with an empty delta.
- `test_a_cursor_that_no_longer_fits_falls_back_to_the_full_handover` — rewind/fork detection.
- `test_a_guest_that_lost_the_session_starts_fresh_and_is_briefed_in_full` — pruned session:
  `guest_resume_failed` logged, fresh harness, full brief, `switch_metrics` recorded.
- `test_a_failed_send_is_retried_with_the_catch_up_still_in_the_prompt`.
- `test_a_cursor_is_only_resumed_under_the_account_that_ran_it` — account keying.
- `test_cursors_survive_a_restart_in_the_session_file` — session-file round trip + resume.
- Unchanged #1V4F suite neighbours (guest→guest full brief, endpoint round trips) still green.

Would-land run (HEAD + this card's five files): `tests.test_guest_handover` +
`tests.test_guest_harness_provider` + `tests.test_model_switch` = **113/113 OK**
(`docs/qa_evidence/2026-09-24-guest-switch-resume-Q8TM/tests.txt`). The shared working tree
shows two other sessions' in-flight test failures in the same modules; they fail on files this
card does not touch and pass on HEAD.

## Try it

Open it with one command:

```
docs/qa_evidence/2026-09-24-tryit-Q8TM/stage.sh
```

It prints, side by side, what Relay sent to the Codex guest on a switch back **before** this
card (a fresh session and the whole conversation again) and **after** it (its own session
resumed, only the GLM turn that ran while it was away) — the real switch code path, with
scripted model answers so it needs no network. Read the two prompts and the two status lines.

Would you trust the "after" hand-over on a real working session — is anything missing from the
catch-up that you would want re-sent? (~2 min)

Expected: docs/qa_evidence/2026-09-24-tryit-Q8TM/expected.md (sealed until you answer)
