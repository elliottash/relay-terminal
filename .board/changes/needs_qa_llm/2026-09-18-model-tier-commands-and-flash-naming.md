---
id: C6YX
type: work
status: needs-qa-llm
labels: [change, feature]
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: '`/main`, `/flash`, `/glm` and `/kimi` switch the pane from the composer; nothing in Relay calls the Flash role "fast"; `./scripts/test.sh` (883) and `ctest` (29) pass'
source: 'owner in chat, 2026-09-18: "add /main to switch to the main model for the main terminal agent. add /flash to switch to the flash model. /glm to switch to your glm coding plan /kimi to switch to your kimi coding plan. yes fix the alt + F terminology"'
links: {plans: [], commits: [bd156a4], evidence: [], related: [4WHD, 9V1F], github: null}
---
# Four model commands in the composer, and the Flash role stops calling itself "fast"

## Request

`/main` and `/flash` for the pane's own agent, `/glm` and `/kimi` for the provider, plus the
terminology fix: the tier layer said Flash while the pane layer said Fast for the same thing.

## Behavior

### The naming

The pane-agent role is `flash` everywhere, matching the Flash tier it has always resolved to.
"Fast" is gone from the product on purpose: in Codex and Claude Code `/fast` means *the same model
served faster at a token premium*, close to the opposite of Relay's trade, and Relay is the terminal
those agents run inside. Leaving `/fast` unclaimed also keeps it free to mean the standard thing if
Relay ever adds it.

Renamed: role `fast` → `flash` (`relay_core.roles.ROLES`, `LABELS`, `ACTIONS`, `ROLE_TIERS`), the
command `agent.fastAgent` → `agent.flashAgent` (Alt+F unchanged — F still fits), `Pane::toggleFastAgent`
→ `toggleFlashAgent`, `newPanesUseFastAgent` → `newPanesUseFlashAgent`, QSettings `agent/panes_fast` →
`agent/panes_flash` and `roles/fast/*` → `roles/flash/*`, and the visible strings ("Flash agent for this
pane", "New panes use the Flash agent").

Nothing writes `fast` any more, but three things on disk still carry it, so every read normalizes:

- **Settings** — `migrateFastRoleSettings()` in `src/main.cpp` moves the keys once at startup, before
  anything reads them; a value already under the new name wins and the old key is removed either way.
- **Saved layouts** — a pane's `agent_role` goes through `Pane::canonicalRole()` on restore.
- **Protocol and subagent definitions** — `roles.DEPRECATED_ROLES` / `canonical_role()` translate the
  name in `validate_role`, `validate_roles`, `RoleResolver.resolve` and subagent model specs, so a
  user definition saying `model: fast` still resolves to the Flash role.

### The commands

- `/main` and `/flash` switch this pane's own agent between the Main and Flash models, keeping the
  conversation — the same switch as Alt+F. Already on that role, the command says so with the model
  rather than looking like it did nothing.
- `/glm` and `/kimi` switch the pane's provider. The Coding Plan preset is tried first (`glm-coding`,
  `kimi-code`) so a subscription is spent before pay-as-you-go credit, falling back to the family's
  other preset (`glm`, `kimi`). With neither key stored the command names the missing provider instead
  of opening a picker. Like any model pick, this puts the pane back on the Main agent.

### Also

`README.md` carried two "Model roles" bullets: the older one was superseded in full by
"Model roles: Main, Flash, Lite" below it and had been damaged in a merge — its last line was an
orphaned fragment of the away-recap sentence that is still intact further up. Removed. The surviving
recap sentence pointed at Actions › Agent options; the toggle is `recap/away` in Settings › General.

## QA checklist

1. **The commands.** In a pane with a GLM or Kimi key: `/flash` → the chip reads `Flash agent · <model>`
   and the status line names the model; a prompt is answered by the Flash model; `/main` returns, with
   the conversation intact both ways. `/flash` twice: the second says "Already on the Flash agent · …".
2. **Providers.** `/glm` switches to Z.AI · GLM-5.3 · Coding Plan (the Coding Plan key is the one stored
   here). `/kimi` — no `kimi-code` key is stored on this machine, so it must land on Kimi · K3 and say so,
   not error. `/glm` again → "Already on …". A provider with no key at all → the "No stored … key" line.
   **Checked 2026-09-18 (not in the GUI):** the stored `kimi` key is a Moonshot pay-as-you-go key —
   it authenticates at `https://api.moonshot.ai/v1` and is refused (401) by the Kimi Code base
   `https://api.kimi.ai/coding/v1` — so the fall-through to `kimi` is the correct landing here, and a
   real Coding Plan key would have to come from the kimi.com/code console. The GUI half of this item
   (the status line, "Already on …") is still to do.
3. **From the Flash agent.** With the pane on `/flash`, run `/glm`: the pane switches provider *and*
   returns to the Main agent (`selectModel` does both).
4. **Popup.** Type `/` — the four new rows appear with their descriptions, and `/m`, `/f`, `/g`, `/k`
   rank them. An alias named `main`, `flash`, `glm` or `kimi` must not shadow them (they are reserved).
5. **Busy pane.** With a turn running, `/flash` refuses with "Stop the current agent turn…" and changes
   nothing.
6. **Migration.** With a pre-2026-09-18 profile: set `agent/panes_fast=true` and `roles/fast/preset=glm`
   in `~/.config/RelayTerminal/relay.conf`, start Relay, quit, and check the file now has
   `agent/panes_flash` and `roles/flash/preset` and neither old key. Run twice: the second start is a
   no-op.
7. **Saved layout.** Hand-edit a saved layout's `agent_role` to `"fast"`, restart, and confirm the pane
   comes back on the Flash agent (chip and menu tick) and saves back as `"flash"`.
8. **Old name over the protocol.** `configure` with `{"roles": {"fast": {"preset": "glm"}}}` is accepted
   and reported back under `flash`; a subagent definition with `model: fast` runs on the Flash model.
9. **Nothing says "fast".** ✅ **Checked 2026-09-18.**
   `grep -rn "fastAgent\|panes_fast\|Fast agent" src backend docs README.md tests` returns only the four
   lines of `migrateFastRoleSettings` in `src/main.cpp`; `docs/qa_evidence/` is excluded on purpose
   (recorded worker output from the 2026-09-17 run, left as the record of what shipped then). Still to
   check in the GUI: the roles modal's Advanced list row reads "New panes (Flash agent)".
10. **Composing with a cross-provider tier** (new since `834ca1b`, which lets a tier follow another
    provider). The owner's setup is Kimi for Main and GLM-5.3-Flash for Flash. **Checked 2026-09-18
    against the worker:** configured that way, `/glm` moves Main to `glm-5.3` and the explicit Flash
    override stays `glm-5.3-flash` — switching provider does not drag the Flash tier with it. Worth one
    pass in the GUI to confirm the chip and the roles modal agree.

## Known gaps

- `/glm` and `/kimi` cover the two providers the owner pays for; other presets still go through
  `/model`. A general `/<preset>` was not added — it would collide with alias names.
- The intake item that prompted this ("selecting model options in the model dropdown didnt do
  anything") is a separate bug and is **not** fixed here. It was picked up independently by another
  session in this checkout — see `issues/changes/needs_qa_llm/2026-09-18-model-dropdown-selection.md`.
