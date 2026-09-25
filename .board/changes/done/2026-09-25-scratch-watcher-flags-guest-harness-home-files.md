---
id: WZ3K
type: work
status: done
labels: [bug, scratch, guest]
assignee: agent
rank: m
created: '2026-09-25'
source: Claude Code guest session, 2026-09-25
links: {plans: [], commits: [02e76d3], evidence: [docs/qa_evidence/2026-09-25-wz3k/README.md], related: [DVV2, B53G], github: null}
---
# Scratch watcher flags the guest harness's own home files (~/.claude, ~/.claude.json) as unledgered scratch

## Issue
delegate a subagent to file the scratch watcher issue

## Done means
A turn served by the Claude Code guest harness ends with no scratch-sweep warning about `~/.claude` or `~/.claude.json` (a Codex guest turn likewise for its `CODEX_HOME`), with no ledger row needed for them: the sweep's skip list comes from the guest profile, so an account whose `CLAUDE_CONFIG_DIR` points somewhere else is skipped there too, not just at the default `~/.claude`.
The ledger gains a supported verb — `relay-scratch own <path>` — that records an existing path as a live install-class, lifetime-user row without creating or moving it, so nobody hand-edits `scratch-ledger.jsonl` again (the two hand-appended rows sc1a8f0/sc1a8f1 stay valid but stop being necessary).
Failure looks like: the sweep still names the harness's own home files at a guest turn's end, or `own` refuses / mis-classes an existing path.
A native Relay agent that itself invokes `claude` under $HOME is still flagged and answered with `own` — that behaviour is wanted, not a regression.

## Plan
**Goal.** The post-turn scratch sweep (#DVV2) must not flag the guest harness's own state — the harness that served the turn, at whichever home its account gives it — and the ledger must gain the supported verb whose absence forced the hand-edited rows.

**Findings.**
- The sweep is `scratch.unledgered_created_since()` (`backend/relay_core/scratch.py:703`): top-level entries of the temp dirs and `$HOME` newer than the turn start, uid-owned, minus paths covered by any ledger row (path-equal or parent). It is called at turn end in the session runner (`backend/relay_core/agent.py:2816`, guarded by `ctx["swept"]`; the note text is `scratch_sweep_note`, `agent.py:715`). Relay's own `~/.config/relay` / `~/.local/state/relay` are never seen only because they are not top-level; `~/.claude` and `~/.claude.json` are, and Claude Code rewrites them every turn.
- The session already knows the turn was guest-served: `self._guest_harness()` (`agent.py:2471`) and `guest_harness_provider.config_guest_id(self.config)` (`backend/relay_core/guest_harness_provider.py:138`). `agent.py` already imports that module lazily where only a guest needs it (`_guest_label`, `agent.py:199`) — copy that pattern so the heavy guest stack is not pulled into non-guest panes.
- The profile knows its own home: `ClaudeHarness._config_dir()` (`backend/relay_core/guest_harness_claude.py:399`) = the account's `CLAUDE_CONFIG_DIR` → `os.environ` → `~/.claude`; `guest_accounts.CONFIG_ENV = {"claude": "CLAUDE_CONFIG_DIR", "codex": "CODEX_HOME"}` and an account's `config_dir` is what the harness env overrides carry (`tests/test_guest_accounts.py:224`). Codex's equivalent chain is `CODEX_HOME` → `~/.codex`. Claude Code also rewrites the top-level file `~/.claude.json` each turn regardless of `CLAUDE_CONFIG_DIR`.
- Why the workarounds existed: `scratch.new_dir()` (`scratch.py:588`) allocates *new* directories (it `mkdir`s its chosen path), and `scratch.adopt()` (`scratch.py:728`) only writes `orphaned` rows for the pre-ledger backlog — neither can say "this existing path is an application's own, forever".

**Steps.**
1. `backend/relay_core/guest_harness_provider.py`: add `own_home_paths(config) -> list[str]` — for `config_guest_id(config)`, the paths that harness itself owns and rewrites each turn: claude → the `CLAUDE_CONFIG_DIR` chain (account override via `config_account(config)`/`guest_accounts`, then `os.environ`, then `~/.claude`) **plus** `~/.claude.json`; codex → the `CODEX_HOME` chain else `~/.codex`. Pure path computation, both candidate locations included unconditionally (matching is by path only, so a non-existent entry is harmless); no filesystem writes, per protocol 26.7.
2. `backend/relay_core/scratch.py`: extend `unledgered_created_since(since, *, ledger, home, tmp, skip=())` — each skip path matches exactly as a covered row does (equal to, or a parent of, the scanned entry). Reuse the same `any(p == path or p in path.parents …)` test so the two lists cannot drift.
3. `backend/relay_core/agent.py` turn-end block (at `agent.py:2816`): when `self._guest_harness()`, lazily import `guest_harness_provider` (the `_guest_label` pattern) and pass `skip=own_home_paths(self.config)` to `unledgered_created_since`.
4. `backend/relay_core/scratch.py`: add `own_path(path, purpose, *, ledger=None)` — refuse a path that does not exist; if `ledger.find(path)` already yields a row, return it unchanged (idempotent); else append `Row(cls="install", lifetime="user", state="live", created_by={"source": "own"}, note="adopted as an application's own state (card #WZ3K)")`. Wire a `own` verb into `main()`'s dispatch beside `new`/`adopt`, printing the row id the way `new` does.
5. Tests (below) and a `scripts/relay-build` build; land through `scripts/land.py` per repo rules.

**Risks.**
- Whether `~/.claude.json` follows `CLAUDE_CONFIG_DIR` varies by Claude Code version; the skip list includes both the config dir and `~/.claude.json` unconditionally, so either answer works.
- The hand-appended rows sc1a8f0/sc1a8f1 are not rewritten (the ledger is append-only history); they remain valid `covered` entries and the new verb is idempotent over them.
- Only the *running* harness's homes are skipped, per the card. A native Relay agent that runs `claude` itself still gets asked — now answerable with `relay-scratch own`. No owner decision is needed.

**Verify.**
- `pytest tests/test_scratch_ledger.py`: `unledgered_created_since` honours `skip` (directory, top-level file, parent-of match, still reports a sibling); `own_path` writes a live install/user row for an existing path, refuses a missing path, and is idempotent over an existing row.
- `pytest tests/test_guest_accounts.py` (new case there): `own_home_paths` returns `~/.claude` + `~/.claude.json` by default, honours an account/env `CLAUDE_CONFIG_DIR`, and `~/.codex` / `CODEX_HOME` for codex.
- Manual: end a turn in a Claude Code guest pane — no sweep note naming `~/.claude` or `~/.claude.json`; `relay-scratch own /tmp/whatever` prints a row id and a later sweep stays quiet about it.

## Tests
- `tests/test_guest_accounts.py` `OwnHomePathsTests`: defaults to `~/.claude` + `~/.claude.json`; honours a `CLAUDE_CONFIG_DIR` override and a registered account's dir; uses `~/.codex` / `CODEX_HOME` for Codex; a non-guest config owns nothing. 25/25 OK.
- `tests/test_scratch_ledger.py`: `test_unledgered_created_since_honours_skip` (a dir, a top-level file, a temp-dir entry, a parent skip; an unlisted sibling is still named), `OwnPathTests` (live install/user row, refuses a missing path, idempotent), and the sweep note names `relay-scratch own`. All new tests pass. The only failure, `test_deliverable_outside_workspace_refused`, also fails at the pre-change HEAD and is filed as #B53G.
- `scripts/relay-build` OK. CLI smoke test against a throwaway `RELAY_LEDGER`: see the evidence README.
- Not run: the live guest-pane turn-end check, which needs a restarted backend on `02e76d3`.
