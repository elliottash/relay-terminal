# Sealed expected result — card #12JX (do not put this on the card)

Run, in order:

```
bash docs/qa_evidence/2026-09-25-subagent-restore/stage.sh
bash docs/qa_evidence/2026-09-25-subagent-restore/check.sh
XDG_DATA_HOME="$PWD/docs/qa_evidence/2026-09-25-subagent-restore/sandbox/home" \
  RELAY_KEYRING=off RELAY_MEMORY_IMPORT=off ./build/relay
```

The staged situation: Relay was killed outright (no clean stop) while subagent `a1`
("Count the pelicans") ran. Its thread file says `running`, which before this card meant
"running forever, roster gone".

Pass — ALL of these hold:

1. `check.py` prints four PASS lines and exits 0.
2. The app window opens with one tab restored to the "Pelican survey" conversation
   (the pane resumes its session; no model is configured in the sandbox, and none is needed).
3. The pane's subagents pane (the agents button on the pane's toolbar) lists `a1
   Count the pelicans` with the interrupted glyph `⊘` — not `running`, not absent — and its
   transcript shows the task history that was on disk.
4. `sandbox/home/relay/sessions/<digest>/12jxrestore….threads/<thread>.json` now reads
   `"status": "interrupted"` (the app rewrites it on restore).
5. Nothing spends money or calls a model: restoring is file work; the subagent sits idle.

Fail — any of: no row in the subagents pane; the row shows running; the thread file still
says `running` after the app opened; the app window opens empty (staging bug — see
stage_files.py output); any trace/log error mentioning restore.

Close the app, then `bash docs/qa_evidence/2026-09-25-subagent-restore/unstage.sh`.
