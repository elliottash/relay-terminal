# Changing the model while the agent is working (card 3ES1): implementer evidence

Implementer runs, not QA verdicts. Relay built from this commit, run under Xvfb by
`implementer-driver.sh` with an isolated HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME,
XDG_RUNTIME_DIR and TMPDIR, on the real stored keys (keyring enabled; no key is printed or stored
here). One turn per case: `sleep 25; echo alpha`, then `echo beta`, then a one-line answer; the model
is switched 12 s into the turn, while the first command sleeps.

| Case | Switch | Result |
|---|---|---|
| `flash` | Alt+F: glm-5.3 → glm-5.3-flash (same provider, the Flash agent) | `model_applied at=step step=2 from_model=glm-5.3 to_model=glm-5.3-flash host=api.z.ai`, turn `done` |
| `kimi` | `/kimi`: glm-5.3 (Z.AI) → kimi-k3 (Moonshot) | `model_applied at=step step=2 from_model=glm-5.3 to_model=kimi-k3 host=api.moonshot.ai`, turn `done` |

Files per case:

- `implementer-<case>-01-ready.png` — the pane before the turn.
- `implementer-<case>-02-first-command-running.png` — step 1 answered by glm-5.3, `sleep 25` running.
- `implementer-<case>-03-switched-mid-turn.png` — right after the switch: the chip already shows the
  new model and `↻ … takes over at the next step · glm-5.3 is not interrupted` is in the transcript.
- `implementer-<case>-04-turn-finished.png` — `→ now on …` right after the first command's `exit 0`,
  the second command and the answer from the new model.
- `implementer-<case>-log-lines.txt` — `turn_start`, `model_applied`, `turn_end` from the worker log
  and `agent_finished` from the GUI log (model names and hosts only).
- `implementer-<case>-provider-settings.txt` — the pane's `[provider]` settings after the run (card
  WFJM): after `/kimi` they name Moonshot's endpoint, not the Z.AI one the pane started on.

Six small paid requests in total (three per case).
