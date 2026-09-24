# Expected — #J0VY Try it (sealed until you answer)

With eight panes open and the app left alone for a minute:

- The printed count of `app_catalog_updated` is **0** (a handful at most is fine — a real
  setting change legitimately emits one batch). The pre-fix behaviour was a batch every few
  seconds, hundreds per minute, without you touching anything.
- Typing in a pane and scrolling it feels the same as a single-pane window; nothing queues
  or stutters.
- Re-running the printed `grep -c` after you have played with the app should still show
  only a small number: interacting with panes is not a settings change, so nothing should
  flood.
- Every pane's worker still works: the pane prompts are alive, and (had a model been
  configured) the catalog would still reach them on a real settings change — that path is
  covered by the unit test `catalogChangedGatesIdenticalResends`.
