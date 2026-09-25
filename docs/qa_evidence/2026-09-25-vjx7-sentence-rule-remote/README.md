# #VJX7 revision — remote sessions follow the #1ZNS sentence rule

Owner decision 2026-09-25: "i want the sentence rule for remote sessions — build and test it out".
Supersedes de31a0136f2a (which had made remote mode stricter than local).

Change: `_remote_assist_signals` is removed from `backend/relay_core/router.py`; `_classify_remote`
uses the same `assist_signals` assist gate as the local path. "cp a b" and "kubectl get pods in
the namespace" now route to `agent` with `needs_assist` remotely — they **ask first**, nothing is
silently sent. `tests/test_ssh_remote.py` drops the two lines from
`test_command_shaped_lines_are_typed_on_the_host` and pins the new behaviour in
`test_sentence_shaped_lines_ask_instead_of_running`.

- `tests.txt` — `python3 -m unittest tests.test_ssh_remote tests.test_router -v` (97 passed).
- `remote-classify.txt` — remote routing for the Done-means lines on the revised rule.
