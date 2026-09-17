# Evidence — Tasks are the model's todos only (`H3QW`)

Implementer evidence for
[`issues/changes/needs_qa_llm/2026-09-17-tasks-are-todos-only.md`](../../../issues/changes/needs_qa_llm/2026-09-17-tasks-are-todos-only.md).
**Not a QA verdict.** Files prefixed `implementer-` were produced by the implementing model.

| File | What it shows |
|---|---|
| `implementer-requests-tests.txt` | `relay-requests-tests` under offscreen Qt: 14 passed, 0 failed. Includes `aPromptIsNeverATask`, `panelShowsTasks` and `panelWithoutATaskList`, which assert on the real `RequestsPanel` widget. |
| `implementer-01-no-chip-on-startup.png` | Relay under `xvfb-run` with isolated `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME`: starts with no Tasks chip in the composer strip. |

| `eval/RESULTS.md` | Todo-tool uptake per preset from `scripts/eval-requests.py` — the number card `H3QW` depends on. Raw summaries alongside it. |

## Gap for QA to close

No live agent turn was run. The isolated profile has no provider key, and the implementer did not
run against the owner's own credentials. The two behaviours that matter most — the chip staying
hidden through a simple turn, and the panel listing todos during a multi-step turn — are covered
only by the widget-level tests. Checklist items 1–3 in the card are the priority.

`scripts/eval-requests.py` **was** run — see `eval/RESULTS.md`. All three keyed presets write a
complete todo list for a multi-ask prompt and none for a single simple ask, so the split has a real
list to show. One finding came out of it and has its own card: GLM-5.3 drops the list when a steer
arrives mid-turn (`D8VN`).
