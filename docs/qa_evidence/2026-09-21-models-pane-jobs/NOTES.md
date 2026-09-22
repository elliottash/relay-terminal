# #MDL1 — the models pane's fourth tab: **jobs**

Owner, 2026-09-21: *"for the per-job models, i think that should be reviewed and improved and made
a 4th tab. take a careful look at it to see how to improve it for that."*

The review of the retired modal and the design of the tab are in `docs/MODEL-PICKING-DESIGN.md`
§5.9. This directory is the driven run: `drive.sh` under Xvfb on `:840`, an isolated
`HOME`/`XDG_*`/`TMPDIR` under `/tmp/claude-1000/mpj` (short — `XDG_RUNTIME_DIR` holds a unix
socket), `RELAY_KEYRING=off`, fake provider keys in the environment and **no network turn at all**:
a role resolves out of the keystore, so `model_roles` arrives with `configured`.

Built from a **clean `git archive main` export** (`cmake` + `--target relay`), not from the shared
`build/`: another session's uncommitted `src/BoardPane.cpp` would not compile at the time of the
run. `strings -el` on that binary finds the tab's own text, so the shots are of this code.

## What each shot shows

| shot | what it proves |
| --- | --- |
| `a-jobs-runs-on.png` | the tab: fifteen jobs in five tier groups plus the two fixed ones, and **"runs on" filled from a live worker** — `glm-5.3 · high` for the main tier, `glm-5.3 · max` for high, `glm-5.3-flash · low` for flash, `relay-lite · low` for lite. The group heading carries what the *tier* resolves to, which is the only place **lite** is visible now that the priorities tab has no lite section. |
| `b-summaries-highlighted.png` | seven Downs from "agent turns" land on "summaries": the five group headings are `NoItemFlags`, so Up and Down step over them. |
| `b1-model-list.png` | Enter drops the same filter list the model box uses, **over the row's own override cell**: "follows flash" first, then one row per model name with the provider in the via column. No `claude code` row — summaries is a background role. |
| `c0-filtered.png` | typing narrows it (`kimi-k3`). |
| `c1-level-list.png` | a model with levels of its own then drops its level list, in the model's own words. |
| `c-override-set.png` | the override cell reads `kimi-k3 · low ×`, **and "runs on" has already followed** — the write went to the worker and its `model_roles` came back. |
| `d-runs-on-followed.png` | the same, after a further four seconds: it is the report, not an animation. |
| `e-override-cleared.png` | Delete: "follows flash" again, and "runs on" back to `glm-5.3-flash · low`. `conf-after.txt` has no `[roles]` section at all. |
| `e1-models-pane-on-providers.png` | the pane moved to providers, so the next step is a change and not a coincidence. |
| `f-options-per-job-row.png` | Options › Models, the **per-job models** row with its `jobs…` button, found by its own search. |
| `g-row-lands-on-jobs.png` | that row pressed: the models pane is on **jobs**. The old modal is not opened by anything any more. |

`conf-before.txt` and `conf-after.txt` are the profile's settings file either side of the run.

## Two things the first runs found, and what changed

**Every "runs on" cell read an em dash.** Rank 1 of the main list on this machine is the guest
harness Relay finds on `PATH`, and a guest pane starts its process on the **first turn** — so
nothing was ever configured, no `model_roles` was ever sent, and the column had nothing to show.
That is the tab telling the truth, and it is worth knowing that a never-configured pane reads that
way; but it is not what the column is for, so `drive.sh` pre-seeds the five lists on a cloud
provider in `relay.conf`.

**The list dropped in the wrong place, and the columns did not fit.** `FilterPopup::openFor` drops
under its anchor and the anchor was the whole tree, so Enter on a row two-thirds down opened the
list at the top-left of the list, off the pane. And the four columns were fixed widths adding up to
more than a models pane is wide, so `kimi-k3 · low` read `kimi-k3 · lov` behind a horizontal
scrollbar. Both fixed in `8fb5250` before these shots were kept: the popup anchors on the row's
override cell, and "what it does" takes the slack while the other three keep a width.

A third came out of the script rather than the code: typing `kimi` matched `kimi-for-coding`
first, which has no reasoning knob, so no level list opened and the keys meant for it went into the
tree. The script types `kimi-k3`, and the comment says why.

## Tests

`ctest --test-dir build -R "jobstab|modelspane|modelsettings"` — jobstab 20, modelspane 18,
modelsettings 6, all green. `PYTHONPATH=$PWD/backend RELAY_KEYRING=off python3 -m unittest discover
-s tests -p test_roles.py` — 76 green, including the new `BackgroundRoleTests`, which pins
`BACKGROUND_ROLES` to the set the tab derives for the guest rule.
