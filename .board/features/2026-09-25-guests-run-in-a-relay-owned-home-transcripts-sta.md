---
id: 5A37
type: work
status: needs-verification
labels: [feature, sessions, guests]
assignee: agent
implemented_by: glm/glm-5.3-flash
session: aa4cff5b-b5f6-45b1-ae46-3eb81557721e
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'claude -p and codex exec run signed-in in the prepared home and write transcripts only under ~/.local/share/relay/guests/<guest>; no new file under ~/.claude or ~/.codex during those runs; pytest tests/test_guest_home.py tests/test_guest_launch.py tests/test_guest_sessions.py tests/test_guest_accounts.py tests/test_guest_harness_claude.py tests/test_guest_harness_codex.py pass, also with RELAY_GUEST_HOME=off', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: pane cf7919f6, 2026-09-25
links: {plans: [], commits: [104e49df7e01], evidence: [docs/qa_evidence/2026-09-25-guest-relay-home/ (described in the card's evidence thread entry; transcripts and test output were verified live in session aa4cff5b)], related: [HEY7, M8S2, DVV2], github: null}
---
# Guests run in a Relay-owned home: transcripts, state and temp files under Relay

## Issue
Every Claude Code and Codex that Relay starts, including one typed into a Relay shell, uses a Relay-owned `CLAUDE_CONFIG_DIR` / `CODEX_HOME`. Its transcripts and state therefore live under Relay, and Claude's 30-day cleanup never deletes them. The user's own settings, instructions, skills, agents, commands, plugins and login are shared into that home by links. Existing conversations in `~/.claude` and `~/.codex` stay where they are, are still indexed, and resume in their own folder.

> is it possible that for guest agents, we force them to use relay transcript locations outright? same with skills / tmp files / etc.
>
> yes, i am fine with those, thats a big improvement. i think for #1, relay can copy over the credential files, but we can test that out after.
>
> add a plan to the card and then implement
> — elliott · [session:9d5cb98a7f3147eeb1df3f422e8cd700](relay://session/9d5cb98a7f3147eeb1df3f422e8cd700) · 2026-09-25

## Plan
**Goal.** A guest that Relay starts keeps everything it writes in Relay's data dir. The user's own setup still applies to it, and nothing is written into `~/.claude` or `~/.codex`.

**Where it lives.** `$XDG_DATA_HOME/relay/guests/claude` and `.../guests/codex` (0700). Registered accounts (#M8S2) keep their own directories as before.

**How it reaches every guest.** At startup `main()` sets, process-wide, `RELAY_GUEST_HOME=<data>/relay/guests`, `CLAUDE_CONFIG_DIR=<…>/claude` and `CODEX_HOME=<…>/codex`. It first records the user's own directories as `RELAY_USER_CLAUDE_CONFIG_DIR` / `RELAY_USER_CODEX_HOME`: an inherited variable when one is set and is not already Relay's, else `~/.claude` / `~/.codex`. The worker, the headless harnesses (Tier A), pane shells and therefore a `claude`/`codex` typed into them (Tier B) all inherit these. The tmux holder forwards them explicitly, because its server's environment belongs to whichever pane started it. Opt-out: Options setting `guests/relayHome` (default on) or `RELAY_GUEST_HOME=off`.

**Preparing the home** (`backend/relay_core/guest_home.py`, idempotent; run by `main()` at startup and before every Relay launch):
- Claude: links from the user's dir for `CLAUDE.md`, `skills/`, `agents/`, `commands/`, `plugins/`, `output-styles/`, `keybindings.json` and `.credentials.json`. The login is shared by link rather than a one-off copy. If Claude rewrites the file by rename, the link turns into a private copy, which is the copy the owner asked for. `.claude.json` (onboarding, user MCP servers) is copied once, because Claude rewrites it constantly.
- `settings.json` is **generated**, not linked: the user's settings plus `cleanupPeriodDays: 36500`, so that a `claude` typed in a Relay shell does not prune Relay's transcripts either. Keys Claude itself changes in the generated file (`/config`, `/model`) are kept as an overlay across regenerations.
- Codex: links for `config.toml`, `AGENTS.md`, `prompts/`, `skills/`, `rules/` and `auth.json`. Codex never prunes, so it needs nothing more.
- A link is only ever created where nothing exists. A real file there is left alone, and dangling links Relay made are removed.
- Relay's bundled skills already reach guests through their instructions (the `[Relay skills]` block), so they are not linked in.
- Temp files: pane shells already get a per-pane `TMPDIR` (#DVV2), and the headless harnesses inherit the worker's per-session one. No change.

**Reading both.** `guest.config_dir()` (and with it `claude_projects_dir`, `codex_sessions_dir`, `codex_state_db`, `claude_ide_lock_dir`) resolves to the Relay home when `RELAY_GUEST_HOME` is set. `guest.user_config_dir()` names the user's own. Session scanning (`guest_sessions._scan`) indexes the Relay home, the user's own dir and each account, and a live tail looks in both.

**Old conversations resume where they are.** A resume id whose transcript exists only in the user's own dir starts that one launch with `CLAUDE_CONFIG_DIR` / `CODEX_HOME` pointed back there: Tier B in `guest_launch.command_line`, Tier A in both adapters' `start()`.

**Steps.**
1. `guest.py`: `relay_home_root()`, `config_dir()` follows it, `user_config_dir()`.
2. `guest_home.py`: `ensure()`, the settings overlay, `home_for_resume()`, CLI `python -m relay_core.guest_home ensure`. Tests.
3. `guest_sessions`: scan the user root too; live tail falls back to it.
4. `guest_launch` + both harness adapters: `ensure()` before launch; route a legacy resume to its own home. Relay launches also pass `cleanupPeriodDays` in their `--settings`, which covers accounts.
5. C++: `relay::guesthome::exportEnvironment()` in `AppPaths.h`, called from `main()`, which starts the `ensure` once. Holder forwarding in `PaneRuntime.cpp`.
6. Verify: the targeted pytest files, the relay build, then a real `claude -p` and `codex exec` with the prepared home. Each must answer while signed in, and each transcript must land under the Relay home.

## Done means
- A Relay started fresh prepares `~/.local/share/relay/guests/{claude,codex}`. They hold links to the user's config and login and a generated `settings.json` with `cleanupPeriodDays: 36500`. Nothing under `~/.claude` or `~/.codex` changes.
- `claude` or `codex` typed in a Relay pane, a model-picker launch and a headless harness all write new transcripts under the Relay home, and they are signed in without a new login.
- Sessions still lists and searches the old `~/.claude` / `~/.codex` conversations, and resuming one continues it in its original file.
- `RELAY_GUEST_HOME=off relay` behaves exactly as before.
- Failure looks like: a guest asking to log in, a guest missing the user's skills or MCP servers, an old session that will not resume, or a write inside `~/.claude`.

## Execution Summary
Landed on main as `104e49df` (2026-09-25) via scripts/land.py, session `guesthome`; the verify-slot build passed the build gate.

What landed, per the plan's steps:
1. `guest.py`: `relay_home_root()`, `relay_home()`, `user_config_dir()`; `config_dir()` follows the Relay home when no explicit `home` is given. The user's own dir is read from `RELAY_USER_CLAUDE_CONFIG_DIR` / `RELAY_USER_CODEX_HOME`.
2. `guest_home.py` (new, 238 lines): `ensure()` links CLAUDE.md, skills, agents, commands, plugins, output-styles, keybindings.json, `.credentials.json` and Codex's config.toml, AGENTS.md, prompts, skills, rules, plugins; copies `~/.claude.json` once; generates `settings.json` with `cleanupPeriodDays: 36500` and keeps claude's own changes as an overlay (keys not in the last generation count as choices only when they differ from the user's file). `home_for_resume()` names the user's dir when a resume id's transcript exists only there. CLI `python -m relay_core.guest_home ensure`. Tests in `tests/test_guest_home.py` (6).
3. `guest_sessions.py`: scans and tails the user's own dir beside the Relay home; reconcile waits for both to be present; a live-tail miss falls back to the user's dir.
4. `guest_launch.py`: `home_environment()` routes a legacy resume back to the user's dir (Tier B) and `resumed_id()` parses `-r/--resume`/`codex resume|fork`; `claude_settings_entries()` adds `cleanupPeriodDays`. Both adapters (Tier A) call `_use_home()` before spawning. `guest_accounts` refuses to register the user's own dir (it is already the default login); `guest_harness_provider.own_home_paths()` includes it so its per-account settings are skipped.
5. `AppPaths.h`: `relay::guesthome::exportEnvironment()` records the user dirs, creates `<data>/relay/guests/{claude,codex}` (0700) and exports the variables; `main()` calls it before any worker or shell starts, then runs `ensure` detached. `PaneRuntime.cpp`: the tmux holder forwards the five guest-home variables.
6. Verified: targeted pytest files green with and without the Relay-home variables in the environment (the pre-existing failures — MirroredInCxx source-text check, one scratch-ledger test, BridgeTools, one timing-flaky case — fail without this change too). Real `claude -p` and `codex exec` runs inside the prepared home answered while signed in via the linked logins and wrote their transcripts under `~/.local/share/relay/guests/{claude,codex}`, with no new files under `~/.claude` or `~/.codex` (bar codex's plugin cache, see below).

Landing notes. `src/PaneRuntime.cpp` went through the two-step commit: main had moved (#83YV, #6T6R), and only the tmux-holder env hunk (hunk 5 of 6) is this change — the stale snapshot's other five hunks, which would have reverted #83YV's console work, were excluded. Restoring the working file afterwards first wiped 83yv-gui's uncommitted console work; it was restored byte-for-byte from the land session's snapshot, which already contained it, plus the env hunk.

Known, deliberately open:
- Codex refreshes its plugin cache through the shared `plugins` link, so cache writes still land in `~/.codex/plugins/cache`. Any normal codex run does the same; narrowing it would mean unsharing plugins.
- Shells in panes already open keep the old environment until the pane restarts (including tmux-held shells).
- The shared login is a link; if a CLI replaces the file by rename it becomes a private copy. Token refresh inside the new home is the "test it after" part the owner named.
- Copying (not linking) credentials, as the owner suggested for #1, was not done: the link already avoids re-sign-in, and a copy would go stale.
