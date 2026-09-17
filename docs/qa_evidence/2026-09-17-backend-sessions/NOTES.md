# Backend sessions: implementer live check (2026-09-17)

Implementer evidence, not a QA verdict. Claude Opus 5 (backend workstream A).

## Setup

- Driver: `live_check.py` (this folder), run from the worktree against the real worker
  (`backend/worker.py`) with keys from `relay_core.keystore` (presets `kimi` and `glm-coding`).
- Scratch workspace with `hello.py` and a project `AGENTS.md`; `session_dir` in the scratch folder.
- Output: `live-events.jsonl` (every event except `delta`/`tool_output`), `live-results.json`, and the
  plan file the agent wrote (`live-plan.md`).

## Results (second run, after the summary-prompt fix)

| Check | Result |
|---|---|
| `configure` (kimi) | `configured` with `context_window` 1,048,576, `limit_tokens` 838,860, `effort` high, `mode` build, `instructions` = [workspace AGENTS.md], `session_id` |
| Turn 1 (Kimi K3): "Remember TANGERINE-42" | done, "OK" |
| `set_model` → glm-coding mid-conversation | `model_changed {model: glm-5.3, preset: glm-coding, context_window: 1000000, effort: high}` |
| Turn 2 (GLM-5.3): "What codeword?" | "TANGERINE-42": conversation kept across the provider switch |
| `context` after turn 2 | `used_tokens` 2262 from provider usage, `estimated: false` |
| `set_effort low` (GLM) | `applied {thinking: {type: enabled}, reasoning_effort: low}` |
| Plan mode turn | tools used: `list_directory`, `read_file`, `run_command` (ran `python3 hello.py`), `write_plan`; `plan_written` → `<workspace>/.relay/plans/2026-09-17-0946-add-verbose-python-version-flag.md`; `hello.py` unchanged |
| `compact {focus}` (manual) | `compaction_started {reason: manual}`, `compacted {before_tokens: 3632, after_tokens: 3645, summary_chars: 786}` (tiny conversation, so the summary is not smaller) |
| Turn after compaction | "TANGERINE-42" recalled from the summary |
| `suggest next_prompt` | "Implement the plan now" |
| `suggest next_command` after `python3 hello.py` exit 0 | empty text, reason "Script ran successfully with no follow-up needed." |
| New worker (Kimi) `sessions` | one item, title = first prompt, turns 4, model glm-5.3 |
| `resume {id}` | `state_loaded {turns: 4}` then `recap {text, next_action, turns_covered: 4, reason: resume}`; text mentions the codeword recall, the plan path, "not been implemented or tested yet" |
| Turn after resume (Kimi on GLM history) | "TANGERINE-42"; Kimi accepted GLM's assistant messages |

## Re-run after rebasing onto main with the subagents backend (dec2d2d)

Same script, same checks, all passed again: Kimi → GLM switch recalled TANGERINE-42; plan mode wrote
`.relay/plans/2026-09-17-0954-…md` using `read_file`, `run_command`, `write_plan` with `hello.py` unchanged;
manual compaction (`summary_chars` 734) kept the codeword; resume on Kimi gave `state_loaded {turns: 4}`, a recap
with `next_action`, and recalled the codeword. `configured` reported 45 skills. The committed `live-*` files are from
the pre-rebase run.

## Bug found and fixed during the check

First run: after manual compaction GLM-5.3 (effort low) wrote a summary saying the codeword "should not be
recalled" because the summary prompt called the transcript "untrusted", and the marker said
"model-generated, untrusted". The agent then refused to recall it (also after resume). Fixed in
`backend/relay_core/context.py`: the summary prompt now asks to record user-provided facts verbatim and not
comment on trust; the marker tells the agent to treat the summary as its memory (quoted tool output stays
untrusted). Second run recalls correctly.

## Other probes

- Usage reporting without `stream_options` (raw requests, 2026-09-17): Kimi puts `usage` inside
  `choices[0]` of the final chunk; GLM and OpenRouter send top-level `usage`. `provider.py` now reads both
  and emits one `usage` event per response.
- `instructions.scan` on this repo with the real home: 27 items, 1 existing (`~/.warp/WARP.md`, 28,329 bytes);
  loading it fits the 32 KiB default cap untruncated.
- Default skill discovery on the real home: 45 skills (user `~/.warp/skills` plus bundled Warp skills and
  bundled Figma MCP skills); 7 Warp-app skills excluded and reported; prompt section 6,107 bytes.
