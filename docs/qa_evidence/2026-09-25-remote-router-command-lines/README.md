# #VJX7 — remote router: command-shaped lines typed on the host again

Change: `backend/relay_core/router.py` gains `_remote_assist_signals`, used by `_classify_remote`
for both its agent signal and its assist gate. After a non-English command name (`cp`, `kubectl`,
`claude`) a remote line assists only on sentence punctuation or a trailing "?"; articles, pronouns,
lead-ins and word count alone no longer clear #1ZNS's bar at an ssh prompt. English-command
assists ("make the tests pass") and the local `classify` path are unchanged.

- `tests.txt` — `python3 -m unittest tests.test_ssh_remote.RemoteRouterTests tests.test_router -v`
  (all pass, incl. `NonEnglishCommandNameTests` and the new remote #1ZNS sentence case).
- `remote-classify.txt` — the Done-means lines classified with `remote={"host": "filly"}`.

Before the change `test_command_shaped_lines_are_typed_on_the_host` failed for "cp a b" and
"kubectl get pods in the namespace" (route `agent`).
