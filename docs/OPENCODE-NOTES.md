# opencode notes: what Relay's agent should adopt next

Research date: 2026-09-16. Source: `sst/opencode` branch `dev`, commit
`88c6c7abc7f3` (shallow clone). Paths below are relative to
`packages/opencode/src/` unless stated; links use
`https://github.com/sst/opencode/blob/dev/packages/opencode/src/<path>`.
Relay baseline: `backend/relay_core/agent.py`, `tools.py`, `provider.py`.

opencode is **not installed locally**. `~/.opencode/bin` is on `PATH`, but
`~/.opencode`, `~/.config/opencode` and `~/.local/share/opencode` do not exist.
There is no local provider config for GLM, Kimi or OpenRouter to port. Warp is the
only source of those settings.

> **Update 2026-09-17:** Relay removed per-action approvals; tools now run
> immediately. P2 below and the approval trade-offs throughout describe the
> earlier design.

## Relay today, in one paragraph

A single conversation holds one fixed system prompt and four tools:
`run_command`, `read_file`, `list_directory` and `write_file` (a whole-file
write). **Every** tool call needs a one-shot approval. Limits are 12 model
steps and 24 tool calls per turn. Context is never compacted or counted, and a
request fails once it reaches 8 MiB. Reasoning is preserved as
`reasoning_content`, or as `reasoning` for OpenRouter. The worker rejects `ask`
while a turn is active. Cancelling trims the partial turn from history. There
are no snapshots or undo.

---

## P1. Exact-match `edit_file` tool instead of whole-file rewrites

- **opencode:** `tool/edit.ts` and `tool/edit.txt` take `filePath`, `oldString`
  and `newString`, plus an optional `replaceAll`. The edit fails if the string is
  missing or matches more than once, unless `replaceAll` is set. A chain of
  fallback replacers absorbs model sloppiness: `SimpleReplacer`,
  `LineTrimmedReplacer`, `BlockAnchorReplacer`, `WhitespaceNormalizedReplacer`,
  `IndentationFlexibleReplacer` and others (edit.ts:217–703). The file must have
  been read first. GPT-family models get `tool/apply_patch.ts` instead, selected
  in `tool/registry.ts:298`.
- **Relay:** `write_file` resends the whole file. That is slow and expensive on
  large files. Weaker models also tend to drop content, and the diff review gets
  noisy.
- **Change:** Add `edit_file(path, old, new, replace_all=false)`. Start with
  exact matching and a uniqueness check. Next add line-trimmed and
  indentation-flexible matching, and nothing fuzzier. Reuse the existing diff
  preview and stale-file recheck. Require a prior `read_file` of that path in
  the session, tracked by hash. Keep `write_file` for new files.
- **Size:** M.
- **Security:** Neutral to positive. The diff under review becomes smaller and
  easier to check. Fuzzy matching must never apply to a region other than the
  one shown in the preview, so compute the diff after matching.

## P2. Rule-based permissions: allow/ask/deny per tool and pattern, "always" per session

- **opencode:** In `permission/index.ts`, `evaluate(permission, pattern,
  ...rulesets)` returns the **last** matching wildcard rule and defaults to
  `ask`. The rule sets are agent defaults, then user config, then patterns the
  user approved "always" during the session. The defaults live in
  `agent/agent.ts:115–135`. Most tools default to `allow`. `doom_loop` and
  `external_directory` default to `ask`, and `.env` reads default to `ask` with
  `*.env.example` allowed. For shell commands, `tool/shell.ts` parses the
  command with tree-sitter. It reduces each command to a
  "human-understandable prefix" using the arity table in
  `permission/arity.ts`, so `git checkout main` becomes `git checkout` and
  `npm run dev` stays `npm run dev`. It then asks with patterns such as
  `git checkout *`, which the user can approve "always".
- **Relay:** Every call asks, and there are no "always" answers. The
  architecture doc deliberately rejects first-word allowlists.
- **Change:** Keep the default of **ask for everything**. Add session-scoped
  "always allow this" for:
  1. `read_file` and `list_directory` under the workspace (non-secret paths),
  2. the exact `edit_file` or `write_file` path,
  3. shell commands matched by parsed prefix, only for commands with no
     substitutions, redirections, pipes into interpreters, `eval`, `source`,
     or `sh -c`.

  Reject anything unparseable and fall back to asking. Add an optional
  `deny` list, such as `rm -rf *`, `git push *`, `curl * | sh`, which
  returns an error to the model without prompting. Persist nothing across
  sessions at first.
- **Size:** M. Parsing needs `bashlex` or a small tokenizer, and a vendored
  tree-sitter is L.
- **Security:** This is the main trade-off. The architecture doc is right that a
  prefix is not a safety proof: `make`, `npm run` and `pytest` execute arbitrary
  project code. Mitigations are to never auto-allow build and run entrypoints
  by default, to show the matched rule on every auto-approved call in the agent
  pane, and to keep the rules session-only. Read auto-allow still sends file
  content to the provider, so it needs the same consent wording.

## P3. Context accounting and compaction

- **opencode:** `session/overflow.ts` counts usable context as the model's
  input limit minus a reserve of min(20k, max output tokens). The count uses
  provider usage figures: input, output, cache read and cache write. When a
  turn crosses that limit, `session/compaction.ts` runs a hidden `compaction`
  agent with every tool denied, using the prompt in
  `agent/prompt/compaction.txt`. That agent writes a structured summary with
  exact paths and identifiers preserved, and older messages are replaced by the
  summary. Separately, `prune` walks back through old tool outputs. It protects
  the newest 40k tokens and blanks older tool results once more than 20k tokens
  can be freed (`PRUNE_PROTECT` and `PRUNE_MINIMUM`). OpenRouter requests set
  `usage: {include: true}` (`provider/transform.ts:1236`).
- **Relay:** It emits `usage` events but ignores them. The conversation grows
  until a provider error or the 8 MiB local cap. `/new` is the only remedy.
- **Change:**
  1. Store the last `usage.prompt_tokens` and show "ctx N / limit" in the
     agent header.
  2. Add a per-preset `context_limit`. Kimi K3, GLM-5.3 and DeepSeek V4.1 Flash
     values should be taken from provider docs, not guessed.
  3. Prune first: replace tool results older than the last 2 turns with
     `[output elided, N bytes]` once over about 70%.
  4. Compact at about 85%: make one model call with no tools and a fixed
     summary template (goal, decisions, files touched, commands run and their
     results, open items), then rebuild history from the system prompt, the
     summary and the last user message.

  Send `stream_options: {include_usage: true}` where supported, and
  `usage: {include: true}` on OpenRouter.
- **Size:** M.
- **Security:** Low risk. The summary call sends the same data the provider
  already has. The summary must be marked as untrusted model output, so it
  cannot turn earlier tool text into "instructions".

## P4. Accept messages while busy: queue, then steer at step boundaries

- **opencode:** A prompt sent while a session is running is saved immediately
  and joins the active run. `SessionRunState.ensureRunning` in
  `session/run-state.ts` and `effect/runner.ts:115` return the running handle
  instead of failing. The loop in `session/prompt.ts:~1090` reloads message
  history on **every step**. A queued user message is therefore seen at the
  next model call, after current tool results, without cancelling. The TUI
  tags such messages `QUEUED`
  (`packages/tui/src/routes/session/index.tsx:1450`). Interrupting is a
  separate explicit cancel. It marks in-flight tool parts as interrupted
  orphans, which the loop then ignores (`isOrphanedInterruptedTool`,
  `prompt.ts:99`).
- **Relay:** The worker returns "An agent turn is already active." This is the
  "interrupt vs queue" item in `issues/feature_intake.txt`.
- **Change:** Add a thread-safe `pending_user` list on `Agent`. A worker `ask`
  while busy appends to it and emits `queued`. The loop drains the list into
  history before each model call. Tool-call groups must stay contiguous, so
  never insert between an assistant `tool_calls` message and its `tool`
  results. In the GUI, Enter queues while busy. A separate shortcut or button
  means interrupt and send, which cancels, keeps the existing checkpoint
  truncation, then sends. A queued message must not auto-approve pending
  approvals.
- **Size:** S–M.
- **Security:** Neutral. Approvals stay one-shot, and a queued message must
  never count as a reply to an approval dialog.

## P5. Project instructions (AGENTS.md) and an environment block

- **opencode:** `session/instruction.ts` loads the global
  `~/.config/opencode/AGENTS.md` and optionally `~/.claude/CLAUDE.md`. It then
  loads the **first** project match found walking up from the working
  directory to the worktree root: `AGENTS.md`, then `CLAUDE.md`, then the
  deprecated `CONTEXT.md`. Config `instructions` globs can add more.
  `session/system.ts` adds a block giving the model ID, working directory,
  worktree root, platform and date.
- **Relay:** It uses a static `SYSTEM` string plus "Chosen workspace: …".
- **Change:** On `configure`, read the first `AGENTS.md` or `CLAUDE.md` found
  walking up from the workspace to the git root, capped at 32 KiB. Append it
  under a heading such as "Project instructions (from <path>; lower priority
  than the rules above)". Add an env block with the model, workspace, git
  branch, OS and date. Show in the UI that the file was loaded.
- **Size:** S.
- **Security:** Repository files become part of the system context. A hostile
  repo's AGENTS.md can try to steer the agent. Keep Relay's rules first and
  label the instructions as lower priority. Approvals still gate every action.
  Offer a toggle to disable loading.

## P6. Provider-quirk table for Kimi, GLM (Z.AI) and DeepSeek/OpenRouter

- **opencode:** The quirks live in `provider/transform.ts`:
  - **DeepSeek:** every assistant message must carry reasoning, even an empty
    string (lines 303–319). For "interleaved" models, reasoning is sent back as
    a top-level field, `reasoning_content` or `reasoning_details`, on **all**
    assistant messages, even when empty (lines 321–350). OpenRouter is excluded
    because its SDK handles `reasoning_details` itself.
  - **Z.AI/zhipu via openai-compatible:** requests send
    `thinking: {type: "enabled", clear_thinking: false}` (line ~1251).
  - **GLM-5.2 on OpenRouter:** reasoning effort goes in as
    `reasoning: {effort}` (line ~801).
  - **Kimi family:** detected by model ID or host (`api.moonshot.ai`,
    `api.kimi.com`). Sampling defaults are temperature 1.0 for thinking
    variants and 0.6 otherwise, and top-p 0.95 for k2.5 (lines 527–551).
    Kimi also gets a dedicated system prompt, `session/prompt/kimi.txt`. It
    tells the model to act with tools rather than describe changes, and to
    emit parallel tool calls.
  - **Tool-call repair:** `session/llm.ts:296` lower-cases a mis-cased tool
    name. Otherwise it routes the call to an `invalid` tool whose result carries
    the parse error back to the model.
  - **Doom-loop guard:** `session/processor.ts:29, 356–373` asks the user after
    3 identical consecutive tool calls with the same input.
  - **Finish reasons:** some providers return `stop` with tool calls present,
    so the loop keys on tool calls existing, not on `finish_reason`
    (`prompt.ts:~1103`).
- **Relay:** It preserves `reasoning_content` or `reasoning` only when non-empty
  for `reasoning`. It rejects streams whose `finish_reason` is missing or not
  in {stop, tool_calls} and have no `[DONE]`. An invalid tool name or bad JSON
  already becomes an error result. There is no loop guard.
- **Change:**
  1. Always echo `reasoning_content` on assistant messages for DeepSeek and
     Kimi, even when empty. Relay already does this, so add a regression test.
  2. Add a `reasoning_details` passthrough for OpenRouter: accumulate the
     streamed array verbatim and send it back.
  3. Add `clear_thinking: false` to the GLM preset extras. This needs
     `thinking` sub-key validation.
  4. Add `stream_options.include_usage` and OpenRouter `usage.include`.
  5. Lower-case tool-name repair.
  6. A doom-loop check: 3 identical calls in a row returns an error result
     telling the model to change approach.
  7. Accept `finish_reason == "stop"` when tool calls were assembled and
     `[DONE]` was received.

  Verify each item against live responses before shipping. The live smoke test
  on 2026-09-16 passed on all three presets without these changes.
- **Size:** S each, M together.
- **Security:** Neutral. The doom-loop guard reduces approval fatigue.

## P7. Read-only exploration tools: `grep`, `glob`, ranged `read_file`

- **opencode:** It has `tool/grep.ts` (ripgrep), `tool/glob.ts`, and
  `tool/read.ts` with `offset` and `limit`. Reads default to 2000 lines, cap
  each line at 2000 characters and total output at 50 KiB, and use line-number
  prefixes. Large tool output is truncated with the full text saved to a temp
  file the agent may read (`tool/truncate.ts`: 2000 lines or 50 KiB).
- **Relay:** Searching needs `run_command` (`grep`, `find`), and each search
  needs an approval, which is the dominant friction. `read_file` has no range.
- **Change:** Add `search_files(pattern, path, glob)` using `rg` if present,
  otherwise Python `re`, and `find_files(glob)`. Both are confined to the
  workspace and skip secret paths, `.git` and binaries. Add `offset` and
  `limit` to `read_file`, with numbered lines. These are the natural first
  candidates for P2's session "always allow".
- **Size:** S–M.
- **Security:** Positive. They replace unsandboxed shell commands with
  workspace-confined, secret-guarded tools.

## P8. Plan mode (read-only agent) vs build mode

- **opencode:** `agent/agent.ts:142–180` defines two primary agents. `build`
  uses the configured permissions. `plan` denies edits everywhere except
  `.opencode/plans/*.md`. A synthetic reminder is appended to the user message
  in plan mode (`session/reminders.ts`, `session/prompt/plan.txt`). Switching
  back injects `build-switch.txt` and points at the plan file.
- **Relay:** Only one mode exists.
- **Change:** Add an agent mode selector, Plan or Build, next to the router
  selector. Plan exposes only the read-only tools (P7 plus `list_directory`
  and `read_file`) and adds a short "investigate and propose, do not modify"
  reminder. Switching to Build adds "execute the plan above".
- **Size:** S.
- **Security:** Positive. It gives a no-write mode where auto-allowing reads
  is reasonable.

## Lower priority (P9–P11)

- **P9. Todo tool (S, no risk).** opencode's `tool/todo.ts` and
  `tool/todowrite.txt` keep an in-memory list with pending, in-progress and
  completed states, one item in progress at a time. Relay could add
  `update_todos(items)` without approval, emitted as a `todos` event and
  rendered in the agent pane.
- **P10. Per-turn snapshots and undo (M).** `snapshot/index.ts` uses a
  separate git dir (`--git-dir <data>/snapshot --work-tree <worktree>`), so the
  user's repo is untouched. `session/revert.ts` restores or un-reverts
  snapshots. Relay could snapshot before the first approved write in a turn,
  into a 0700 runtime dir that honors `.gitignore` and the secret guard. Undo
  would restore only files changed by file tools and say plainly that shell
  side effects are not undone.
- **P11. Subagents and web fetch (L, defer).** `tool/task.ts` spawns child
  sessions with narrower permissions, such as the read-only `explore` agent.
  `tool/webfetch.ts` fetches pages. For Relay, both multiply approvals and add
  an exfiltration channel. Revisit after P2, P7 and P8, starting with a
  read-only explore subagent. Web fetch would need approval with the full URL.

**Not recommended:** per-family system prompts, as in `session/system.ts:28–50`
and `session/prompt/kimi.txt`. A per-preset addendum is enough for
OpenAI-compatible providers. Kimi's would be "act with tools, don't just
describe; parallel calls are fine." opencode's default `build` agent allows
nearly everything, which conflicts with approval-first. P2 gets most of the
ergonomics while keeping ask as the default.
