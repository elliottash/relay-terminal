Sealed expected result for #T4VK — do not read before answering.

- Within a moment of "you type … and press Enter" (about half a second in the demo), agent_wait
  returns with stopped_for_user_message=true, timed_out=false, and a1 still `running` — the wait
  ended for the message; the background agent was not stopped, cancelled or interrupted.
- The model's next request ends with the steered text as its last user message, prefixed with a
  "Sent by the user while you were working" header — the agent sees the wait result, then your
  message, in that order.
- Exactly one turn starts (turns started: 1): nothing was interrupted or restarted.
- The pane side of the same change (typing + Enter steers in during such a wait, and only when
  nothing is queued ahead of it) is covered by tests/queuesubmit_test.cpp, which stage.sh does
  not run.
