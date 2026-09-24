# Q8TM — reuse guest sessions, cut model-switch context

Evidence for the switch-path rework on card `issues/features/2026-09-23-reuse-guest-sessions-and-cut-model-switch-context.md`.

## What was checked

The would-land tree — `git archive HEAD` plus exactly the five files this card changes
(`backend/relay_core/guest_harness_provider.py`, `backend/relay_core/session_protocol.py`,
`tests/guest_harness_fake.py`, `tests/test_guest_handover.py`, `docs/AGENT-SESSIONS-PROTOCOL.md`)
— run under `PYTHONPATH=backend python3 -m unittest`:

- `tests.test_guest_handover` — the switch replay suite, 8 new/changed tests
  (`tests.txt`, 113 tests across the three modules, all green).
- `tests.test_guest_harness_provider`, `tests.test_model_switch` — the provider and
  mid-turn-switch neighbourhood, unchanged counts, all green.

## The four patterns (plan step 5)

| Pattern | Test | What it proves |
| --- | --- | --- |
| guest → native → guest | `test_switching_back_to_a_guest_resumes_its_session_and_gets_only_what_ran_since` | the harness restarts on its own session (`starts[0]["resume"] == "codex-1"`), the prompt carries only the native turn that ran while away, never "Start on codex."; a second round trip delivers only what ran since |
| nothing between | `test_a_return_with_nothing_to_deliver_sends_the_prompt_bare` | resume happens, prompt goes bare, status says "nothing has happened since it left" |
| guest → other guest | `test_one_guest_hands_over_to_another_including_its_tool_results` (unchanged #1V4F test) | a guest that never saw the conversation still gets the full bounded brief |
| restart | `test_cursors_survive_a_restart_in_the_session_file` | cursors ride the session file (`guest_cursors`), `resume_session` restores them, the next switch resumes codex-1 with only the post-restart turn |

Fallbacks: `test_a_guest_that_lost_the_session_starts_fresh_and_is_briefed_in_full`
(session pruned → fresh harness, full brief, `guest_resume_failed` logged, metrics say
`guest_resume_fallback`), `test_a_cursor_that_no_longer_fits_falls_back_to_the_full_handover`
(rewind/fork detected by the cursor fingerprint), `test_a_failed_send_is_retried_with_the_catch_up_still_in_the_prompt`
(a failed send does not consume the catch-up), `test_a_cursor_is_only_resumed_under_the_account_that_ran_it`
(account keying, #M8S2).

## The native path (plan step 3)

Guests' handovers are composed inside `HarnessProvider.complete()` and never appended to
`agent.messages`; the existing suite continues to assert the endpoint conversation arrives
whole after a guest round trip (`test_a_pane_that_starts_on_an_endpoint_and_switches_to_a_guest…`),
and it is unchanged by this work — no rebuild of the transcript, the #0C0V pinned prefix
untouched.

## Not mine (measured, separate cards)

Both fail on plain `git archive HEAD`, before any of this card's files:

- `tests.test_session_protocol…test_compact_resume_recap_and_plan_execute`
- `tests.test_guest.MirroredInCxx…test_the_backend_directory_is_appended_to_pythonpath_only_once`

In the shared working tree two tests from another session's in-flight work
(`test_usage_reset_goes_to_the_harness…`, `test_the_scan_landing_is_what_the_worker_pushes_on`)
fail against code that session has not finished; they pass on HEAD and on the would-land tree.
