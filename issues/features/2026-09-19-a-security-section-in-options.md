---
id: 3KB7
type: work
status: in-progress
labels: [feature]
component: [gui, worker]
milestone: beta
workstream: agent
assignee: agent
rank: zzzzzzm
created: '2026-09-19'
acceptance: Options has a Security section that gathers every setting governing what the agent may reach, adds the ones Relay has no control for today, and states in one place what is allowed by default and what is never allowed
source: 'conversation, 2026-09-19: "add a security options menu with various secruity options like that, not just relay - relay, but more of the approvals options on warp. look at warp options for advice on that."'
links: {plans: [], commits: [a68712d, 0b30397, 60af091], evidence: ['docs/qa_evidence/2026-09-19-security-section/'], related: [R5TC, V2HM, C1HH, D8J3, S5SH, SSRQ, JN7X], github: null}
---
# A Security section in Options, gathering what the agent may reach

## Issue

add a security options menu with various secruity options like that, not just relay - relay, but
more of the approvals options on warp. look at warp options for advice on that.

## What Warp does

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

## What Relay has today, and where it is

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

## The plan

**A new `security` section in the settings pane**, between `agent` and `privacy`
(`sections << agent;` at `src/RelayWindow.h:1930`). Settings that already exist **move** here
rather than being duplicated — one home each, or the section is just a second place to look.

Relay's posture differs from Warp's in one deliberate way, and the section should say so in its
own words at the top: **Relay allows by default and never interrupts to ask.** So a capability row
is two-state — *allowed* or *never* — and not Warp's three. `always_ask` is the thing
`docs/ROADMAP.md:43` rejected. Where Warp reaches for "ask", Relay's answer is a denylist, a
confinement, or a bound.

### Rows

1. **Unattended turns use the full tool set** — `security/unattended_full_tools`, **default on**.
   Owner, 2026-09-19: "i think unattended turns get the full set -- make that an option that is on
   by default." Governs a turn nobody started: today only a #R5TC wake, later anything similar.
   Off, such a turn falls back to the `entry.noHandoff` predicate (`src/Pane.h:325`, gated `:10402`)
   and loses `run_in_terminal` and `program_control`.
2. **Other panes may message this pane's agent** — `agent/cross_pane`, default on, with the
   *Stop cross-pane messaging* action beside it (#R5TC).
3. **Hand commands to your terminal** — move `agent/terminal_handoff` here from Agent, and show
   the chain limit beside it.
4. **Commands the agent never runs** — a denylist, matched before execution, the useful half of
   Warp's pair. An allowlist is not offered: it only means anything if the default is "ask".
5. **Folders the agent may read outside the workspace** — an opt-in list. Relay confines to one
   workspace today, which is stricter than Warp's `directory_allowlist`; this makes the
   confinement visible and lets it be widened deliberately rather than by moving the pane.
6. **Files the agent never reads** — surface `looks_secret()`'s fixed list and let it be extended,
   the way `custom_secret_regex_list` is. Extend only: the built-ins cannot be removed.
7. **Let programs write your clipboard (OSC 52)** — default off, as it is now. Today that is an
   invisible default; a row makes it a decision. Reads stay unanswered and get no row.
8. **Isolation** — move `isolation/enabled` and the memory limits here from Terminal.
9. **Turn bounds** — move `agent/max_auto_turns`, `max_steps`, `max_tool_calls` here; they are the
   real cost and runaway control, and they read as performance settings where they sit now.
10. **Audit requests after each turn** — move `agent/audit_requests` here.
11. **Where your prompts go** — not a toggle but a statement, with the hosted-inference switch
    beside it: which provider this pane uses, and that Relay Free means the prompt leaves this
    machine (`docs/RELAY-FREE.md`).
12. **Password prompts** — the #V2HM row, once that lands. Named here so the two cards agree on
    where it goes.
13. **MCP servers** — reserved for #SSRQ. Not built, so no row yet.

### Deliberately not in v1

**Named profiles.** Warp's real idea is `[agents.execution_profiles.<name>]` — switch the whole set
at once, per project or per task. It is the right shape eventually and the settings above should be
stored so that a profile can wrap them later (one flat `security/*` namespace, no scattered keys),
but shipping one set first is the smaller step and matches how Relay's other settings work today.

## Tasks

- [x] The `security` section itself, between `agent` and `privacy` (`src/RelayWindow.h:1930`), with <!-- t:a3 -->
      the posture sentence at its head
- [ ] `security/unattended_full_tools` (default on) and the predicate it drives, with #R5TC <!-- t:b5 -->
- [x] Move, do not copy: `agent/terminal_handoff`, `isolation/*`, `agent/max_auto_turns`, <!-- t:c7 -->
      `agent/max_steps`, `agent/max_tool_calls`, `agent/audit_requests`. Check every reader of each
      key still finds it, and that the Agent and Terminal sections do not end up with a hole
- [x] The command denylist: a setting, the match (before execution, on the resolved command), the <!-- t:d9 -->
      refusal the model sees, and a line in the pane saying which rule refused it
- [x] Readable folders outside the workspace: the setting, and the widened check in <!-- t:e2 -->
      `backend/relay_core/tools.py` — including the ssh-host path, where the workspace guard does
      not apply (#S5SH)
- [x] User patterns added to `looks_secret()` (`tools.py:50-55`), extend-only, with the built-ins <!-- t:f4 -->
      shown and not removable
- [x] An OSC 52 row driving `setClipboardWriteAllowed()` (`engine/view/TerminalView.h:111`), <!-- t:g6 -->
      default off
- [ ] "Where your prompts go", beside the hosted-inference switch <!-- t:h8 -->
- [ ] `docs/VALIDATION.md`'s "Security boundaries" section becomes the written statement this <!-- t:j1 -->
      section renders, so the two cannot drift
- [x] Tests: the denylist refuses and says why; a folder outside the workspace is readable only <!-- t:k3 -->
      when listed, on this machine and on an ssh host; a user pattern is honoured and a built-in
      cannot be removed; `security/unattended_full_tools` off actually withholds the two tools;
      and every moved key is still read from its new home

## Landed so far (2026-09-19)

**`a68712d` — the policy the worker enforces.** `backend/relay_core/security.py` (pure) plus its
wiring: the three lists ride protocol 12.1 beside `max_steps`, validated in `validate_turn_options`
and returned under one `security_options` key, and `Agent` hands them to the executor's policy
rather than setting them on itself. 33 cases in `tests/test_security.py`, including a `WiringTests`
class for the half that can silently not be connected. Verified on a clean export of main plus
exactly those hunks: 3044 tests passing.

**This commit — the section.** Options › Security between Agent and Privacy: the posture paragraph
and the three list rows, carried to the worker by `requestOptions()` and applied to a running agent
at once (any `security/` key now re-sends `set_agent_options`, as the turn limits already did).
Evidence in `docs/qa_evidence/2026-09-19-security-section/`, including the end-to-end run where a
`rm -rf` tool call is refused by name and the probe file survives.

One design note worth keeping: there is no multi-line row kind, so each list is one line with its
own separator, and each detail line says which. A command rule may contain spaces
(`git push --force*`) so that list splits on commas; a secret pattern may contain a comma (`x{1,3}`)
so that one splits on whitespace.

## Still to do

- [ ] `security/unattended_full_tools` and `agent/cross_pane`. Both govern something #R5TC has not <!-- t:m5 -->
      built yet — there is no unattended turn in Relay today — and a control that does nothing is
      worse than no control, so they land with that card.
- [x] The OSC 52 row — landed in `60af091`. `setClipboardWriteAllowed()` was on `TerminalView` <!-- t:n7 -->
      but not on the `TerminalBackend` interface the Pane speaks to, so the gate was unreachable;
      a `ClipboardWrite` capability, the `VTermBackend` override and `Pane::applyClipboardPolicy()`
      are the missing rung. Off by default, verified live in both states. Reads stay impossible and
      have no switch. Note `run_command` output never reaches the emulator, so the agent copies by
      running the escape in the pane's own terminal.
- [x] Moving `agent/terminal_handoff`, `isolation/*`, the turn bounds and `agent/audit_requests` <!-- t:p9 -->
      onto this page, done as that quiet pass (2026-09-19): keys unchanged — every reader, the
      notifications and `requestOptions()` are key-based — with the chain limit named on the handoff
      row. The compaction threshold and the two model-call timeouts stay on the Agent page.


## Decisions

- 2026-09-19, agent: settings **move** rather than being duplicated. A security section that
  mirrors rows living elsewhere is a second place to change them and a second place to be wrong.
- 2026-09-19, agent: two-state rows, not Warp's three. `always_ask` is what
  `docs/ROADMAP.md:43` settled against, and the owner's standing position is that tool calls run
  unapproved. Where Warp asks, Relay denies, confines or bounds.
- 2026-09-19, agent: an allowlist for commands is not offered, only a denylist. An allowlist is
  only meaningful when the default is "ask" or "deny", which Relay's is not.

## Open question (owner)

**Does any capability get "ask" back?** The request said "more of the approvals options on warp",
and Warp's own default for `run_agents` on this machine is `always_ask` — the same capability as
#R5TC's cross-pane messaging. Relay has no ask-mechanism at all, by decision. Adding one for even a
single capability reverses `docs/ROADMAP.md:43` and needs a question card, a blocked turn and a
default, so it is worth being explicit rather than assumed. The section above is built without it;
adding one capability's ask later is additive.
