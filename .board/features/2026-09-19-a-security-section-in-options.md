---
id: 3KB7
type: work
status: executing
labels: [feature]
component: [gui, worker]
milestone: beta
workstream: agent
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 91b2ca54-3939-4ab8-819e-fd2e3d26efbe
rank: zzzzzzm
created: '2026-09-19'
acceptance: Options has a Security section that gathers every setting governing what the agent may reach, adds the ones Relay has no control for today, and states in one place what is allowed by default and what is never allowed
verify: {artifact: system, primary: script, also: [probe], human: optional, criteria: Security shows the cross-pane and unattended controls together; the documented defaults match the controls., sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'conversation, 2026-09-19: "add a security options menu with various secruity options like that, not just relay - relay, but more of the approvals options on warp. look at warp options for advice on that."'
links: {plans: [], commits: [ecf74b8b, 2068bc90, df732939, 6f8aa86f8b78, 2378fe77, cb8e6624, abb6d402da71], evidence: [docs/qa_evidence/2026-09-19-security-section/], related: [R5TC, V2HM, C1HH, D8J3, S5SH, SSRQ, JN7X, K2FV, 9M96], github: null}
---
# A Security section in Options, gathering what the agent may reach

## Issue
add a security options menu with various secruity options like that, not just relay - relay, but
more of the approvals options on warp. look at warp options for advice on that

## Decisions

- 2026-09-19, agent: settings **move** rather than being duplicated. A security section that
  mirrors rows living elsewhere is a second place to change them and a second place to be wrong.
- 2026-09-19, agent: two-state rows, not Warp's three. `always_ask` is what
  `docs/ROADMAP.md:43` settled against, and the owner's standing position is that tool calls run
  unapproved. Where Warp asks, Relay denies, confines or bounds.
- 2026-09-19, agent: an allowlist for commands is not offered, only a denylist. An allowlist is
  only meaningful when the default is "ask" or "deny", which Relay's is not.
- 2026-09-26 — Owner: “yes to all.” Move “Let panes message each other” to Security. #K2FV’s opt-in Ask before supersedes this card’s older two-state/no-ask decision; the command denylist decision remains.

## Discussion points

### Open question (owner), as written 2026-09-19


**Does any capability get "ask" back?** The request said "more of the approvals options on warp",
and Warp's own default for `run_agents` on this machine is `always_ask` — the same capability as
#R5TC's cross-pane messaging. Relay has no ask-mechanism at all, by decision. Adding one for even a
single capability reverses `docs/ROADMAP.md:43` and needs a question card, a blocked turn and a
default, so it is worth being explicit rather than assumed. The section above is built without it;
adding one capability's ask later is additive.

*Freshened 2026-09-26:* answered in the tree by #K2FV (owner, 2026-09-19: "approvals come back as an
opt-in checklist; allow-everything stays recommended but must be explicitly chosen on the first
launch"). Ask came back per capability as Options › Security › Ask before
(`src/RelayWindowSettings.cpp:918-936`). The two-state entries in `## Decisions` above predate it;
whether to record the supersession there is question 2 in the thread.

## Planning notes

The survey the plan was built from, as written on 2026-09-19. The "today" table is a snapshot of
that day, not of now — most of its "no setting" rows have since become rows (see `## Plan`).

### What Warp does

Read from this machine's own `~/.config/warp-terminal/settings.toml` (option names and values only;
nothing from the endpoint blocks was read into the session):

**Named execution profiles.** `[agents.execution_profiles.default]` is a *set* of per-capability
permissions you can switch between, not a flat list of toggles. Each capability takes one of
`always_allow`, `always_ask`, `never`, or `ask_except_in_auto_approve`:

| Capability | This machine's value | Relay's equivalent |
|---|---|---|
| `execute_commands` | `always_allow` | `run_command` — always on |
| `read_files` | `always_allow` | `read_file`, `list_directory` — always on, workspace-confined |
| `apply_code_diffs` | `always_allow` | `write_file`, `edit_file` — always on, workspace-confined |
| `run_agents` | **`always_ask`** | subagents (#2JY7), and now `pane_send` (#R5TC) |
| `computer_use` | `never` | `type_into_program` under a per-turn grant (#C1HH) |
| `mcp_permissions` | `always_allow` | nothing — MCP is #SSRQ, unbuilt |
| `ask_user_question` | `ask_except_in_auto_approve` | `ask_user` (#MQ9C) |

**Lists, beside the capabilities:** `command_allowlist`, `command_denylist`, `directory_allowlist`,
`mcp_allowlist`, `mcp_denylist` — all empty here.

**Elsewhere in the file, and just as much security surface:** `[privacy] custom_secret_regex_list`
(user-defined patterns for what to redact), `[terminal] osc52_clipboard_access = "read_write"`,
`[agents] cloud_conversation_storage_enabled`, `[code.indexing]
agent_mode_codebase_context_auto_indexing`.

### What Relay had on 2026-09-19, and where it was

Every one of these exists; none of them is in one place, and several have no control at all.

| | Today |
|---|---|
| Commands, file reads, file writes | Always allowed, no setting. **No per-action approvals** is a standing decision (`docs/ROADMAP.md:43`, `WARP.md`). |
| Workspace confinement | Stronger than Warp's: the file tools refuse any path outside the pane's workspace (`backend/relay_core/tools.py:5-7`), and the same guard travels to an ssh host (#S5SH). One directory, not a list. |
| Secret files | `looks_secret()` (`tools.py:50-55`): a fixed set — `.ssh`, `.gnupg`, `.git`, `id_rsa`, `id_ed25519`, `.env*`, `*.pem`, `*.key` — plus a `KEY|TOKEN|SECRET|PASSWORD|CREDENTIAL|COOKIE` name regex. **Not user-extensible.** |
| Handing commands to the real shell | `agent/terminal_handoff`, a ceiling rather than a toggle (`src/RelayWindow.h:1869`), plus `kMaxHandoffChain = 3` (`src/InputPolicy.h:135`). In **Agent**. |
| Driving an interactive program | A per-turn human grant, `programGrant()` (`src/Pane.h:1680`), refused while the screen is masked. No setting. |
| Password prompts | Refused twice over today; #V2HM proposes a named-secret path. |
| Isolation | `isolation/enabled` and the memory limits (`src/Isolation.h`). In **Terminal**. |
| Turn bounds | `agent/max_auto_turns`, `agent/max_steps`, `agent/max_tool_calls`, `agent/stall_timeout_s`. In **Agent**. |
| Audit | `agent/audit_requests` (`src/RelayWindow.h:1928`). In **Agent**. |
| Clipboard writes from output (OSC 52) | `setClipboardWriteAllowed()`, **off**, and reads are never answered (`engine/view/TerminalView.h:111`, `engine/core/GhosttyCore.cpp:436`). **No setting — an invisible default.** |
| Where prompts go | Relay Free is hosted inference; `discloseHosted()` says so once per installation. No telemetry. |
| Cross-pane messaging | `agent/cross_pane` and the kill switch, proposed by #R5TC. |

## Plan

**Goal.** Options › Security holds every setting that governs what the agent may reach, with the
posture stated at its head. Freshened 2026-09-26 against the tree at `6514be0c`: the section and
nearly all its rows are landed. What is left is two owner calls, one doc, one test and a comment
tidy — one short Execute turn once the questions are answered.

**Where each planned row stands** (the 13 rows of the 2026-09-19 plan):

| # | Row | State | Evidence |
|---|---|---|---|
| 1 | Unattended turns get the full tool set (`security/unattended_full_tools`, on) | landed with #R5TC | predicate `src/Pane.h:10647`, applied `:13957` (`8d67ff77`); row `src/RelayWindowSettings.cpp:768-774` (`cb8e6624`) |
| 2 | Other panes may message this pane (`agent/cross_pane`) | landed, **on the Agent page** | `src/RelayWindowSettings.cpp:659-664` (`8d67ff77`); palette kill switch `c4f87618` — placement is question 1 |
| 3 | Hand commands to your terminal, chain limit named | landed | `6f8aa86f`; `src/RelayWindowSettings.cpp:694-710` |
| 4 | Commands the agent never runs (denylist) | landed | `ecf74b8b` (worker, `backend/relay_core/security.py`), `2068bc90` (row, end-to-end) |
| 5 | Folders readable outside the workspace | landed | `ecf74b8b`, `2068bc90`; `:730-737` |
| 6 | Files the agent never reads (extend-only) | landed | `ecf74b8b`, `2068bc90`; `:738-745` |
| 7 | OSC 52 clipboard writes, default off | landed | `df732939`; `:746-751` |
| 8 | Isolation / memory limits, moved from Terminal | landed | `6f8aa86f`; `:775-` |
| 9 | Turn bounds, moved from Agent | landed | `6f8aa86f`; `:894-911` |
| 10 | Audit requests after each turn, moved | landed | `6f8aa86f`; `:913` |
| 11 | Where your prompts go | landed in **Privacy**, beside hosted inference | `src/RelayWindowSettings.cpp:1069-1082` |
| 12 | Password prompts | #V2HM's row (card `planned`, 0/9) — not this card's to build | — |
| 13 | MCP servers | landed by #9M96 | `abb6d402`; `:766` |
| — | Ask before (not in the original plan) | landed by #K2FV | `9c9fb1b9`; `:918-936` |

The list rows are single-line with their own separators — commas for commands and folders (a rule
may hold spaces), whitespace for secret patterns (a regex may hold a comma) — because there is no
multi-line row kind (design note from the `2068bc90` landing).

**Steps left.**
1. `docs/VALIDATION.md:329-337` "Security boundaries": rewrite as the written statement of this
   section — allow by default with the opt-in Ask before group, the denylist as a guardrail not a
   sandbox, workspace confinement plus readable folders, secret-file patterns, the OSC 52 default,
   unattended turns, the cross-pane switch. Scope is that section only; the historical "no
   per-action approvals" lines in `docs/AGENT-SESSIONS-PROTOCOL.md`, `ARCHITECTURE.md`,
   `SCRATCHPAD-DESIGN.md` and `MODEL-PICKING-DESIGN.md` describe other surfaces and stay theirs.
2. A test that `security/unattended_full_tools` off sets `noHandoff` on a woken turn's entry
   (`src/Pane.h:13957`) and on leaves it clear. Nothing covers it today: `rg -i unattended tests`
   finds only `tests/test_remote_security.py:222`, which is about a phone, not this switch.
3. Comment tidy in the Security block, no behaviour change: the header at
   `src/RelayWindowSettings.cpp:667-673` still says `docs/ROADMAP.md` "settled against per-action
   approvals", and the isolation (`:752-757`) and unattended (`:758-763`) comments sit above the
   MCP rows instead of their own rows.
4. Per question 1: move the `agent/cross_pane` row onto Security beside Unattended turns (key
   unchanged — every reader is key-based), or leave it on Agent and record why.
5. Per question 2: the owner's answer recorded in `## Decisions` via a `decision` comment.

**Risks.** Moving `agent/cross_pane` changes where a visible row lives (Options search finds it by
key either way). The VALIDATION.md rewrite must not overstate the denylist or classifiers — both
are guardrails a shell line can spell around, as the in-app text already says.

**Verify.** Step 2's test passes; `tests/test_security.py` and the `settings`/`settingsexport`
ctests stay green; one Options screenshot of Security showing the unattended and cross-pane rows
together (if moved); a read of the new VALIDATION.md section against the rows it names.
**2026-09-26 scope update.** Finish the hand-off-tool restriction test, refresh `docs/VALIDATION.md`, move the cross-pane-message control to Security, and tidy stale comments. #K2FV's opt-in Ask before is the current approval rule.

## Tasks

- [x] The `security` section between Agent and Privacy, with the posture sentence at its head — `2068bc90` <!-- t:a3 -->
- [x] `security/unattended_full_tools` (default on) and the predicate it drives — landed with #R5TC, `8d67ff77` (predicate) and `cb8e6624` (row) <!-- t:b5 -->
- [x] `agent/cross_pane` switch and the Stop cross-pane messaging action — landed with #R5TC, `8d67ff77`, `c4f87618`, on the Agent page (placement: question 1) <!-- t:m5 -->
- [x] Move, not copy: `agent/terminal_handoff`, `isolation/*`, the turn bounds, `agent/audit_requests`; chain limit named on the hand-off row — `6f8aa86f` <!-- t:c7 -->
- [x] The command denylist: setting, match before execution, the refusal the model sees — `ecf74b8b`, `2068bc90` <!-- t:d9 -->
- [x] Readable folders outside the workspace, including the ssh-host path — `ecf74b8b`, `2068bc90` <!-- t:e2 -->
- [x] User patterns added to `looks_secret()`, extend-only — `ecf74b8b`, `2068bc90` <!-- t:f4 -->
- [x] OSC 52 row driving `setClipboardWriteAllowed()`, default off — `df732939` <!-- t:g6 -->
- [x] "Where your prompts go" — landed as the Privacy section's statement (`src/RelayWindowSettings.cpp:1069-1082`) <!-- t:h8 -->
- [x] MCP servers rows (reserved for #SSRQ) — `abb6d402` (#9M96) <!-- t:q2 -->
- [x] Tests: denylist refusal, readable folders, secret patterns extend-only, moved keys read from their new home — `tests/test_security.py`, `ecf74b8b`, `2378fe77` <!-- t:k3 -->
- [ ] Test: `security/unattended_full_tools` off withholds `run_in_terminal`/program control from a woken turn (`src/Pane.h:13957`); on keeps them <!-- t:r4 -->
- [ ] Rewrite `docs/VALIDATION.md:329-337` "Security boundaries" as the written statement of this section, Ask before included <!-- t:j1 -->
- [ ] Comment tidy in the Security block (`src/RelayWindowSettings.cpp:667-673` stale ROADMAP line; isolation/unattended comments stranded above the MCP rows) <!-- t:s6 -->
- [ ] Question 1's answer: move the `agent/cross_pane` row onto Security, or record why it stays on Agent <!-- t:t8 s=blocked -->

## Done means
- Options › Security contains the existing cross-pane messaging control beside the unattended-turn control, with each setting stored in one place.
- A focused test proves a woken turn loses terminal handoff and program-control tools when `security/unattended_full_tools` is off and retains them when on.
- `docs/VALIDATION.md` states the current opt-in Ask before posture and the real security boundaries; stale no-ask comments in the Security UI are corrected.
- Targeted settings and security checks pass, and a live Options capture shows the controls.
