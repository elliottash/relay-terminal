---
id: TCXT
type: work
status: executing
labels: [feature, agent, terminal, context]
assignee: codex
rank: mtcxt
created: '2026-09-22'
source: User conversation with Codex in Relay, 2026-09-22
links: {plans: [], commits: [338342cbc2e032cf7d32dd765e33085b72727872], evidence: [], related: [CCKY, AGNT], github: null}
---
# Give agents recent command and output context from their terminal pane

## Issue

what command did i just run and what did it say

can you research, in warp terminal, does it give the shell commands and output to the agent? think about how to do that in a smooth way and explore what we need to add that in relay. i know claude code does for example. do research and give me your views on the feature

perfect. save the plan in a card and add more detail as appropriate. clarify anything else as needed and include notes on how you will orchestrate with subagents.

## Decisions

2026-09-22, owner: "perfect. save the plan in a card and add more detail as appropriate. clarify anything else as needed and include notes on how you will orchestrate with subagents."

This turn saves the plan. Implementation has not started. The recommendations below carry forward the accepted direction; numeric limits are proposed engineering defaults to validate during implementation.

## Done means

- After a composer command, asking what just ran returns the actual command, host/directory, status and captured output in native and guest agents, without rerunning it or requiring a paste.
- Concurrent panes and SSH reconnects cannot substitute another command; a queued question keeps the terminal snapshot selected at submission, and unavailable or truncated output is explicitly labelled.
- Automatic/manual/off sharing, a preview/removal chip, and bounded read-only retrieval work independently of persistent history indexing; shell completion alone never starts inference.
- Native-shell commands become available in stage 2 when integration can identify their boundaries; unsupported capture and full-screen applications report their limits instead of inventing output.
- Targeted tests and isolated GUI evidence establish the behavior, followed by separate verification before closure. Stage 1 alone does not close this full feature card.

## Plan
**Goal**

Make “what command did I just run and what did it say?” and “why did that fail?” work naturally in the same Relay pane. Supply bounded recent context automatically, with read-only retrieval and explicit attachments for more. Do not start an agent turn merely because a shell command finishes.

**Findings and sources (inspected 2026-09-22)**

- `src/Pane.h`: `beginCommandCapture` / `finishCommandCapture` collect composer-run commands, exit status, directory, timestamp and up to 64 KiB of leading output. `finishHandoff` reports agent-requested terminal commands. The prompt context assembly currently sends cwd, foreground program, remote session and control/handoff capabilities, not recent command records.
- `backend/relay_core/conv_index.py::record_commands` stores terminal history under a synthetic workspace conversation. It cannot safely answer “last command in this pane”; old rows lack sufficient pane identity.
- `backend/relay_core/agent.py::validate_context` / `format_context` need a bounded terminal snapshot contract. `guest_instructions.py` and native instructions currently describe terminal output as unavailable.
- `backend/relay_core/guest_board_bridge.py` needs authenticated read-only terminal tools in its discovery/allowlist; `session_protocol.py` is the existing terminal-history ingestion path. Trace queued turns and guest adapters as well as direct submission.
- `engine/core/SequenceScanner.h`, `VtCore.h`, and `CellTypes.h` already recognize OSC 133 lifecycle markers. Markers alone do not guarantee exact command text; stage 2 must establish shell-specific command reporting.
- [Warp blocks as context](https://docs.warp.dev/agents/local-agents/agent-context/blocks-as-context/): commands inside an agent conversation automatically supply context; ordinary terminal blocks can be explicitly attached.
- [Warp terminal/agent modes](https://docs.warp.dev/agents/local-agents/interacting-with-agents/terminal-and-agent-modes/): conversation scope and failed-command attachment hints.
- [Claude Code shell mode](https://code.claude.com/docs/en/interactive-mode#shell-mode-with-prefix): `!` commands and output enter the conversation. This does not imply visibility into an unrelated terminal.

**Proposed product defaults**

Automatic sharing is on for the attached terminal pane. Send the latest user-run command with a head/tail excerpt and compact metadata for a few preceding commands; aim for 2–4k tokens total and enforce a hard byte ceiling. Keep user-run and agent-run origins separate, so agent tool activity cannot replace “what I just ran.” Include any currently running command as running, without a fabricated exit status.

Show a removable “Terminal: npm test · failed” chip before submission. Its preview must match the actual sanitized payload. Explicit selection wins over automatic selection. Removing the chip excludes that snapshot from the turn, including automatic retrieval of the excluded record. A global setting provides Automatic / Manual / Off with a per-pane override. Manual grants only attached records; Off supplies no terminal metadata or read capability. Changing to Off revokes future reads, but cannot retract content already sent to a provider.

Persistent history indexing and live sharing remain independent. Indexing disabled must not break live context; sharing disabled must not silently enable indexing. Existing history settings continue controlling disk retention. Default new live records to memory only, bounded per pane (initial proposal: 32 commands, 64 KiB each, 2 MiB total). Report eviction explicitly. Do not silently expand existing disk retention. A transcript retains the excerpts actually sent according to existing conversation retention.

**Steps**

1. Freeze the schema and lifecycle before parallel implementation. Record `command_id`, pane identity, shell-generation/login identity, per-pane sequence, origin, cwd, host, start/end times, running/completed/interrupted state, optional exit status, output head/tail, total bytes seen, and availability/truncation reason. IDs are opaque; identity and access scope come from Relay, never model-provided paths or host strings.
2. Implement a small tested command-record component outside the large Pane header where practical. Start with composer commands and existing capture hooks. Keep both leading and trailing output under bounded memory, preserve UTF-8 across chunks, and exclude prompt redraws, control sequences and Relay-rendered agent text. Distinguish empty output from disabled, unsupported, interrupted, evicted and truncated capture. PTY output is merged; do not promise separate stdout/stderr.
3. Snapshot command IDs and output revision at prompt submission, before queueing. Carry that snapshot through native and guest turns, retries and model switches. Automatic context references the latest user command; explicit attachments preserve exact selection. Do not inject identical output repeatedly or double-count handoff reports. A queued prompt retains its captured revision; later output requires an explicit fresh read.
4. Add proposed `terminal_history` and `terminal_read` tools to the native executor and relay_board guest bridge. History returns bounded metadata and stable IDs; read accepts an authorized ID plus bounded ranges/cursor. Authenticate the pane from the connection; deny arbitrary paths and ungranted other panes. Use a common service and response schema for both agent families. Running reads return revision/cursor; unknown, stale, evicted and revoked IDs return clear errors. Tool retrieval must not execute a shell command.
5. Wire the chip, preview, removal, settings and “Ask about this” / “Attach output” actions. Other-pane or historical-session attachment is deliberate and grants only the selected records. Add shortcut-registry hints using the live keymap after checking conflicts. Board/settings consoles with no shell must not inherit terminal data accidentally.
6. Update native/guest instructions and `docs/AGENT-SESSIONS-PROTOCOL.md`. Present records as quoted evidence, never executable instructions or fabricated user requests. Check remote-context branches, which currently return early, so local and SSH prompts get equivalent context. Keep screen-reading/driving permissions separate.
7. Complete stage 1 with composer capture, native/guest delivery, retrieval, sharing controls and the original user acceptance flow. Record evidence and remaining stage-2 tasks; do not mark the full card complete.
8. Stage 2: extend Bash/Zsh/Fish and supported Windows shell integration to report native commands and reliable boundaries. Deduplicate commands already captured by the composer. Record host and cwd at command start, distinguish reconnect generations, preserve interrupted/running state and separate background/unattributed output. Where integration is absent, say command capture is unavailable; do not infer exact commands by scraping prompts or shell history.
9. Stage 2: complete selection/attachment of native command blocks. Full-screen/alternate-screen applications remain labelled as such; any screen snapshot uses the existing separate capability. Restored scrollback is historical, never “just ran.” Document supported shells/platforms and attach QA evidence.
10. Integrate, run targeted checks, land through `scripts/land.py`, and move to needs-verification with evidence. A separate verifier exercises the original user flow and writes QA checklist/verdict.

**Subagent orchestration when implementation starts**

The coordinator owns the card, schema, orchestration, shared integration files and final review. Use only Relay's `agent`, `agent_message` and `agent_wait`; create tasks with `update_todos`, preserve existing tasks, and pass returned `todo_id` to each launch. Read every report with `agent_wait` before incorporating it. If delegation is unavailable, work locally and report that limitation.

After agreeing the contract, two bounded implementation agents can work in parallel:
- Capture agent: new command-record C++ component and dedicated unit tests; later shell-hook/engine changes after stage-1 integration. Do not edit Pane.h or global build files.
- Backend agent: new terminal-record service, validation/access controls, native read tools and dedicated Python tests. Do not edit agent.py, guest_board_bridge.py or session_protocol.py initially; report the exact adapter requirements to the coordinator.

The coordinator alone edits `src/Pane.h`, shared settings/keymap/build files, `agent.py`, `guest_board_bridge.py`, `session_protocol.py`, instructions and protocol documentation. This avoids concurrent edits in the hottest files. Delegate a third read-only review of scope isolation and test coverage only once concrete code exists. Reuse a freed slot for the independent verifier after implementation; prefer a different model family when available, without making availability a blocker.

Each assignment names allowed files, inputs/outputs, tests and exclusions. Agents inspect project instructions, share the existing main checkout, claim paths through land.py before editing, and never commit another session's work. Coordinator reviews reports/diffs and resolves interface mismatches before builds. Build through scripts/relay-build after reading docs/BUILDING.md; serialize shared builds. No native harness subagents or agent CLIs.

**Risks and handling**

- Unbounded logs, progress redraws and malformed bytes: streaming head/tail caps, deterministic sanitization and explicit omitted-byte counts; no per-chunk inference.
- Cross-pane, reconnect and queued-prompt races: stable pane/shell IDs and immutable submission snapshots; never query workspace-wide history to resolve “last.”
- Sensitive output and prompt injection: visible preview/removal, sharing controls, reuse applicable secret filtering before provider delivery, and treat text strictly as data. Do not promise perfect redaction. Never capture password keystrokes or synthesize typed input from PTY input streams.
- Shell commands can emit OSC markers: preserve existing authenticated integration boundaries where available; document remaining attribution limits rather than treating output markers as security authority.
- Background jobs and full-screen apps: explicitly mark ambiguous/unavailable attribution. Do not silently attach unrelated output to the next command.
- Memory expiry, restart and conversation compaction: keep sent excerpts in their normal transcript; retrieval may report expired. A restarted shell receives a fresh generation and no fabricated live history.
- Remote phone/paired sessions and consoles: preserve existing origin/access restrictions. This feature must not grant remote terminal access incidentally.
- No additional owner decision blocks planning. Defaults above are recommendations under the accepted design, not invented quotations. Ask only if implementation reveals a material product tradeoff.

**Verify**

Use deterministic tests for schema/caps, empty vs unavailable output, Unicode/ANSI boundaries, failing/successful/interrupted/running commands, head/tail retention, background ambiguity, and eviction. Test two panes in one workspace, local/SSH identity, reconnect, queued submission followed by another command, explicit selection/removal, manual/off revocation, indexing off, guest discovery and denial of unauthorized IDs, retry/deduplication and model switching.

Exercise native and guest agents with a stub provider and inspect the actual submitted payload, not just widget labels. Run a command emitting recognizable first/last lines and a nonzero exit; ask the original question; prove the response uses that record and no rerun occurs. Repeat with concurrent panes and SSH, plus native-shell modes in stage 2. Confirm ordinary command completion causes no model request.

Extend relevant existing tests in `tests/test_conv_index.py`, `tests/inputpolicy_test.cpp` and engine lifecycle tests where behavior changes; add focused new tests for the record service and guest bridge. Record concrete runnable commands in Tests when targets exist, without claiming planned tests passed. GUI verification uses isolated XDG_CONFIG_HOME and Xvfb, with evidence under `docs/qa_evidence/<implementation-date>-terminal-context/`. Run the card's tests_check before implementation handoff; only the verifier writes QA checklist and Verdict.

## Tasks

- [x] Freeze command-record schema, scope rules and submission snapshot contract <!-- t:x7 -->
- [x] Implement bounded composer capture and per-pane records <!-- t:x8 -->
- [x] Expose native and guest context plus authorized history/read tools <!-- t:4a -->
- [ ] Add sharing controls, preview/removal and explicit attachments <!-- t:4h s=in-progress -->
- [ ] Verify and land stage 1 with protocol documentation <!-- t:4t s=in-progress -->
- [x] Extend native-shell lifecycle capture and block attachment in stage 2 <!-- t:w0 -->
- [ ] Complete cross-pane/SSH/native/guest evidence and independent verification <!-- t:kg s=in-progress -->
