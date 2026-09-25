# Expected — #DVV2 Try-it (sealed; the verifier compares, the implementer does not read this aloud)

Running `stage.sh` prints nine steps and exits 0. All of the following hold:

1. Step 1: a plain `mktemp` lands under `<sandbox>/home/.cache/scratch/relay/scratch/tok-tryit/tmp/` — not under `/tmp`.
2. Step 2: `scratch` and `keep` paths are printed; the `keep` path is under `<sandbox>/project/.relay/work/`.
3. Step 4 (`relay-scratch ledger`): three live rows (TMPDIR, build, keep) with classes `scratch`/`keep`, and totals lines showing `class keep 1 rows`, `class scratch 2 rows`, `session tok-tryit`.
4. Step 5: the build row prints `reclaimed` and its directory is gone (`No such file or directory`).
5. Step 6: releasing the keep row without flags **fails** with "never silently deleted"; `--promote-to` then prints `promoted` and the promoted file (`quarterly numbers`) is readable inside the sandbox project.
6. Step 7: `end_session` reclaims the session's remaining scratch row (`reclaimed: ['sc…']`) and `still live` is `[]`.
7. Step 8: the sweep finds exactly the rogue dir: `sweep found: ['/tmp/dvv2-tryit-rogue']` — and nothing else from the real `/tmp` newer than 10 minutes.
8. Step 9: `gc --apply` at 0 h idle on default roots is **refused** with the `force_idle` message, in the library and from the CLI.
9. Nothing outside `sandbox/`, `/tmp/dvv2-tryit-rogue` and `transcript.txt` changed: the real `~/.local/state/relay/scratch-ledger.jsonl` either does not exist or is untouched by the run.
