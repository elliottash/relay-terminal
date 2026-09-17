# Todo-tool uptake per preset — `scripts/eval-requests.py`, 2026-09-17

Run to settle the risk recorded in card `H3QW`: with the request-as-task backfill removed, the Tasks
panel is empty whenever the model writes no todo list, so **how often the model actually calls
`update_todos` is now the thing the surface depends on.** `docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md`
section 2 had this as UNVERIFIED for open-weight models.

Presets with a stored key: `openrouter` (deepseek/deepseek-v4.1-flash), `kimi` (kimi-k3),
`glm-coding` (glm-5.3). `kimi-code` and `glm` have no key and were not run. Default limits, default
`todo_tool: true`, fresh temp workspace per scenario. Summaries in this folder.

## Scenarios 1–3: multi-ask prompts (existing scenarios)

`todo_items` is the size of the largest list the model wrote; `asks` is what the prompt contained.

| Preset | S1 (5 asks) | S2 (3 asks) | S3 (1 ask + 3 steers) | Files correct | Silent drops |
|---|---|---|---|---|---|
| openrouter | 5 todos | 3 todos | 4 todos | 12/12 | 0 |
| kimi | 5 todos | 3 todos | 4 todos | 12/12 | 0 |
| glm-coding | 5 todos | 3 todos | 4 todos | 12/12 | 0 |

**Every preset wrote a complete list, one todo per ask, in every run.** No completion-check
re-prompts and no step limits were needed. The split is safe: a multi-ask prompt produces a real
task list to show.

## Scenario 8 (new): a single simple ask

One ask ("create answer.txt containing the number README.md says is the answer").

| Preset | Wrote no list | File correct |
|---|---|---|
| openrouter | yes | yes |
| kimi | yes | yes |
| glm-coding | yes | yes |

**3/3 wrote no list**, which is what `todos.RULES` asks for ("Skip the list for a single simple
ask"). This is the case that used to produce `Tasks 0/1` → `Tasks 1/1` with the user's own command
as the task text; the chip is now correctly absent.

## Scenario 9 (new): a refinement arriving mid-turn

Scenario 1's five asks, then a steer part-way through: "wait - c.txt should say CHARLIE in capitals,
not charlie". The rule added to `todos.RULES` in this change says to add the steer's request id to
the todo it refines instead of adding a sixth todo. `merged` = some todo carries both `R2` and
another request id.

| Preset | Runs | Wrote a list | Merged when it did | Files correct |
|---|---|---|---|---|
| openrouter | 1 | 1/1 | 1/1 | 5/5 |
| kimi | 1 | 1/1 | 1/1 | 5/5 |
| glm-coding | 3 | 1/3 | 1/1 | 5/5 every run |

**When a model keeps a list through a steer, it follows the new merge rule — 3/3 presets, and the
list stayed at five items.** The rewritten todo text shows the reasoning surviving into the list:

- openrouter: `create c.txt containing charlie — corrected by R2 to CHARLIE (capitals)`
- kimi: `create c.txt containing CHARLIE (corrected from charlie)`

### The GLM result is not about the steer

An earlier version of this file, and of card `D8VN`, read GLM's 1/3 as "GLM abandons the list when a
steer arrives". **The event logs disprove that.** The scenario waits for `tool_started` and then
sleeps 1 s, so the steer is always submitted about a second *after* the model's first tool call — that
is, after the response that decided whether to write a list.

In both GLM no-list runs the first tool call was `write_file`: the list was already skipped before the
steer was submitted. In every kimi and openrouter run the first tool call was `update_todos`, which is
why they always had something to merge into.

Scenarios 1 and 9 send the identical five-ask prompt. GLM wrote a list in 3/3 runs of scenario 1 and
1/3 of scenario 9, so across **six runs of one prompt it wrote a list four times**. That is sampling
variance in the model, not an effect of the steer. No mechanism was identified; batching was checked
and ruled out (GLM issues all five `write_file` calls in one parallel batch in the list-writing runs
too). Six runs is a thin sample — `D8VN` item 3 is to run ten.

## Scenario 10 (new): one instruction, many tool calls — the no-list nudge

Six .txt files without trailing newlines, one instruction to fix them all. Models reasonably skip the
todo list here, which makes this the only scenario that reliably trips
`agent.NO_LIST_TOOL_CALLS` (card `D8VN`). `no_list_nudges` counts the note in the message history,
since it is a prompt note rather than an event.

| Preset | Nudge fired | Wrote a list | Files correct | Tool calls used |
|---|---|---|---|---|
| openrouter | 1 | no | 6/6 | 4 |
| glm-coding | 1 | no | 6/6 | 3, then a later call tripped it |
| kimi | (pre-instrumentation run) | no | 6/6 | 3 |

**The nudge fires once and real models honour its escape clause** — for something that genuinely is
one instruction they ignored it and finished correctly. Note that all three solved six files in three
or four tool calls by using a shell loop, so the four-call threshold only just trips; whether it fires
often enough to matter in real use is `D8VN` item 2.

## GLM list-writing, all runs of the five-ask prompt

Collected across both batches, since the rate moved a lot between them:

| Batch | Runs | Wrote a list |
|---|---|---|
| scenario 1, before the nudge | 3 | 3 |
| scenario 9, before the nudge | 3 | 1 |
| scenarios 1 and 9, after the nudge (it never fired — the models listed first) | 6 | 6 |
| **total** | **12** | **10** |

Two skips in twelve runs. The 1/3 that prompted card `D8VN` did not reproduce, so that card's
GLM-specific claim was withdrawn; what stands is the code-level hole it uncovered.

## What this does not cover

- No GUI was driven. These are worker-level runs; the chip, panel and end-of-turn line are covered
  by `tests/requests_test.cpp` and still need the live Xvfb pass in the card's QA checklist.
- Scenarios 4 and 6 (interrupt-then-continue, forced compaction) were not re-run for this card.
- `--no-todos` comparison runs were not made; the question here was uptake with the tool on.
- One run per preset for scenarios 8 and 9 except GLM (three). The research doc asks for 3 runs per
  preset; GLM got them because its first result disagreed with scenario 1.
