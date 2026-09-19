# Evidence — Plan-mode turns run on the main model pushed to max reasoning (`Z0VG`)

Implementer evidence for
[`issues/features/needs_qa_llm/2026-09-19-plan-mode-turns-run-on-the-main-model-pushed-to.md`](../../../issues/features/needs_qa_llm/2026-09-19-plan-mode-turns-run-on-the-main-model-pushed-to.md).
**Not a QA verdict.** Files prefixed `implementer-` were produced by the implementing model
(Oz agent, via Warp, 2026-09-19).

| File | What it shows |
|---|---|
| `implementer-pytest.txt` | The targeted pytest suites: 122 passed — `tests/test_plan_turns.py` (7 new: swap and restore, next-turn restore, restore on error, configured planning model, build-mode no-op, already-max no-op, no-effort-knob no-op), `tests/test_roles.py` (incl. 6 new planning-resolution tests), `tests/test_presets.py`, `tests/test_remote_wire.py` (the `plan_route`/`plan_route_ended` forward classification). Offline, against a provider that records what it was sent. |
| `implementer-driver.sh` | The Xvfb driver that produced the screenshots: isolated `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`XDG_CACHE_HOME`, started on the stored `glm-coding` key, Advanced list pre-opened. |
| `implementer-01-startup.png` | Relay under Xvfb in the isolated profile, configured on Z.AI Coding Plan. |
| `implementer-02-model-roles-plan-mode-row.png` | Settings › Models with Advanced options open: the **Plan mode** row right under Agent turns, hint "investigating and writing plans; max reasoning by default". |
| `implementer-03-plan-mode-on.png` | Shift+Tab from the prompt box: the mode chip reads **PLAN**. |
| `implementer-04-prompt-ready.png` | The prompt typed, plan mode still on. |
| `implementer-05-plan-turn-running.png` | The plan turn running, 2.5 s in: the routing line "◆ Plan mode · this turn runs on glm-5.3 at max reasoning." and the model chip reading **◆ glm-5.3 · this turn**. |
| `implementer-06-plan-turn-answer.png` | After the turn: the answer in the transcript and the chip back to **glm-5.3**, mode back to auto. |
| `implementer-relay.log` | The GUI log from that run (`gui_start` → `configured model=glm-5.3` → one turn `done`). The routing line is not logged at info level; it is in screenshots 05/06. |

## The full suites

`./scripts/test.sh` (2554 tests, `OK`) and `ctest --test-dir build` (55/55) were run and green in
the implementing session on 2026-09-19, after the `remote/wire.py` classification fix. Their full
output was not captured to files — both capture runs were cut short — so the counts stand on the
card's Implementer check rather than on a log here. The captured part is the targeted pytest file
above; the suites were re-run whole once more before the branch was committed.

## Gaps for QA to close

- **A pinned planning model on a real endpoint.** Pinning `roles/planning` to another keyed
  provider is unit-tested but was never run live.
- **A provider with no effort knob, live.** The Anthropic/MiniMax no-op (no routing line, no
  swap) is unit-tested only; no such key was exercised.
- **The ◆ line on a phone.** `plan_route`/`plan_route_ended` are classified as forwarded in
  `remote/wire.py`, but no phone render was driven.
