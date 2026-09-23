# Relay architecture for multiple subscription identities

## Where does Relay currently assume one identity per CLI family?

### Takeaway
The backend uses `claude` and `codex` as both product identifiers and account identifiers. Multiple simultaneous subscriptions need a stable account identifier distinct from the CLI family, carried in every preset and launch path.

### Cited Findings
- `GuestSpec` has one `.claude` and one `.codex` config path under the user's home; installation detection and transcript paths derive from those fixed paths. Foreground command classification only returns the family. — [guest.py:41-55](/home/elliott/repos/relay-terminal/backend/relay_core/guest.py:41), [guest.py:193-223](/home/elliott/repos/relay-terminal/backend/relay_core/guest.py:193)
- The harness preset parser accepts only `guest:claude` or `guest:codex`; `harness://<family>` is the provider base URL. The request `guest` block allows model, resume, fork, permissions, effort, and memory, with no identity. — [guest_harness_provider.py:49-103](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:49), [guest_harness_provider.py:191-215](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:191)
- Claude harness inherits the worker environment after stripping `CLAUDECODE`, `CLAUDE_EFFORT`, and `CLAUDE_CODE_*`; Codex harness spawns with `dict(os.environ)`. Neither receives account-specific configuration. — [guest_harness_claude.py:156-174](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_claude.py:156), [guest_harness_claude.py:384-390](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_claude.py:384), [guest_harness_codex.py:1240-1243](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_codex.py:1240)
- Tier B launch builds one command string with inline environment assignments, but currently uses that environment for Claude's IDE bridge only. It reads/cleans user config through `home`, including Codex developer instructions and legacy hook configuration. — [guest_launch.py:210-235](/home/elliott/repos/relay-terminal/backend/relay_core/guest_launch.py:210), [guest_launch.py:368-421](/home/elliott/repos/relay-terminal/backend/relay_core/guest_launch.py:368)

### Inferences
- Use immutable account IDs, for example `guest:claude:<account-id>` and `guest:codex:<account-id>`, while preserving existing `guest:claude` and `guest:codex` as the default account migration. Parse into `{family, account_id}` rather than treating the suffix as a family. Keep `harness://` discrimination, but make the account resolvable from the preset/config or an explicit provider field.
- Store an absolute, validated config directory for each account. Pass `CLAUDE_CONFIG_DIR` or `CODEX_HOME` to each harness subprocess and each Tier B command. This is a proposed isolation mechanism requiring CLI verification; ensure Claude's environment sanitizer does not remove `CLAUDE_CONFIG_DIR` (its current prefix test does not). Apply the same environment to auth probes and Codex catalog scans. Do not change the worker's global environment.
- Refactor `guest_launch.command_line` to accept account identity/config and use the selected directory for settings/statusline, legacy cleanup, developer instructions, memory, transcripts, and resumption. Legacy cleanup should never edit the default account merely because an alternate account launches.

### Gaps
- This repository investigation did not verify vendor support for the two environment variables or how each CLI handles token refresh and plugins under alternate directories; that must be checked against installed CLI behavior and vendor documentation.

## Which catalog, quota, routing, and UI paths need account identity?

### Takeaway
Preset ID is already the best cross-layer routing key, but several guest-specific functions collapse it back to the family. The minimum change is to retain the full account preset ID from catalog through picker, tier lists, weighted draws, worker launch, and quota events.

### Cited Findings
- `preset_rows()` emits one row per `HARNESS_GUESTS`, with `id = guest:<family>`, shared family login state, model catalog, and limits. Login probes and the Codex model scan run once per worker process under the worker environment. — [guest_harness_provider.py:318-324](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:318), [guest_harness_provider.py:381-419](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:381), [guest_harness_provider.py:499-538](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:499)
- `_LAST_LIMITS` is keyed by family; `usage_limits_event()` emits `preset = guest:<family>`. The worker forwards rows in the `presets` event, and the pane stores `usage_limits` by the event's preset. — [guest_harness_provider.py:541-572](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:541), [worker.py:153-195](/home/elliott/repos/relay-terminal/backend/worker.py:153), [Pane.h:8718-8731](/home/elliott/repos/relay-terminal/src/Pane.h:8718)
- Tier list entries preserve `preset`, `model`, `rank`, and `effort`; duplicate detection compares preset, URL, and model. Tied ranks use quota-weighted draws, but quota lookup reduces guest preset to family. A stale or absent window has weight 1. — [roles.py:404-450](/home/elliott/repos/relay-terminal/backend/relay_core/roles.py:404), [roles.py:471-529](/home/elliott/repos/relay-terminal/backend/relay_core/roles.py:471)
- The role resolver's `guest_id_of()` returns the entire text after `guest:`; `guest_runnable()` checks family installation and login; `_guest_target()` builds `harness://` from that suffix. `RoleResolver` only retains a main preset if it is a built-in API preset, which drops guest identity for its main resolver. — [roles.py:332-355](/home/elliott/repos/relay-terminal/backend/relay_core/roles.py:332), [roles.py:635-648](/home/elliott/repos/relay-terminal/backend/relay_core/roles.py:635), [roles.py:800-807](/home/elliott/repos/relay-terminal/backend/relay_core/roles.py:800)
- The C++ model catalog uses exact preset IDs for keys, limits, labels, and reported-model reconciliation, so unique account preset IDs can make multiple account rows distinct. The pane's `guestOfPreset()` and `guestHarnessUsable()` assume the suffix is the family and reconstruct `guest:<family>`. — [ModelCatalog.cpp:204-273](/home/elliott/repos/relay-terminal/src/ModelCatalog.cpp:204), [Pane.h:14115-14135](/home/elliott/repos/relay-terminal/src/Pane.h:14115)

### Inferences
- Add an account registry feeding one catalog row per account, each with stable preset ID, family, human label, config path, login state, and account-specific limits. The default account row retains its legacy ID. Scan auth and Codex catalog with that account's launch environment; cache by account preset ID and invalidate when account configuration changes. A shared family model list is acceptable only if proven identical across plans and feature flags.
- Route using full preset IDs in tier lists and weighted draws. Two accounts on the same model at tied rank must remain separate targets; quota lookup must use the account preset, and exhaustion must skip only that account. For stale/unknown quota, existing weight 1 behavior is a reasonable starting rule; document that the draw is heuristic, not a reservation.
- UI should show one selectable `via` entry and distinct quota/login status per account, expose add/rename/remove account controls and config path or login action, and display the chosen account on a pane/session. Keep the model folded by name only where the `via` list makes the account choice explicit. Update `guestOfPreset`, `guestHarnessUsable`, `pickGuest`, slash-model resolution, deferred startup, and Tier B fallback to pass full identity.

### Gaps
- The present scan does not establish whether Codex catalogs differ by identity, so catalog sharing across accounts should be treated as an optimization after testing.

## What must survive persistence and restoration?

### Takeaway
Both Relay harness sessions and imported CLI sessions need account provenance. A session ID plus CLI family is insufficient to locate transcripts or safely resume under the same subscription.

### Cited Findings
- `attach()` adds only `guest` and `guest_session` to `Agent.session_data()`. `session_guest()` validates only those two; `resume_session()` starts a replacement harness by family when the current provider has the same family. — [guest_harness_provider.py:1626-1666](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:1626), [guest_harness_provider.py:1775-1828](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:1775)
- Base `Agent.session_data()` stores a preset only when `self.preset` resolves to a built-in preset; guest provider's wrapper is therefore the current guest provenance. Session resume invokes that wrapper's restoration path. — [agent.py:4522-4534](/home/elliott/repos/relay-terminal/backend/relay_core/agent.py:4522), [session_protocol.py:490-503](/home/elliott/repos/relay-terminal/backend/relay_core/session_protocol.py:490)
- CLI session indexing reads only default `~/.claude/projects`, `~/.codex/sessions`, and Codex state DB. Resume commands carry family, session ID, and cwd, with no config-directory identity. — [guest_sessions.py:81-115](/home/elliott/repos/relay-terminal/backend/relay_core/guest_sessions.py:81), [guest_sessions.py:886-902](/home/elliott/repos/relay-terminal/backend/relay_core/guest_sessions.py:886), [guest.py:204-223](/home/elliott/repos/relay-terminal/backend/relay_core/guest.py:204)
- The pane persists the chosen preset and model in `QSettings`; its Tier B launch helper currently receives the family and settings, without an account argument. — [Pane.h:1693-1707](/home/elliott/repos/relay-terminal/src/Pane.h:1693), [Pane.h:14325-14345](/home/elliott/repos/relay-terminal/src/Pane.h:14325)

### Inferences
- Save `guest_account` (stable ID or full preset ID) beside `guest` and `guest_session`, and include it in session index records and GUI restore state. On resume, resolve it to a configured account, select that account's config directory, then resume. If the account was removed, show a recoverable choice rather than silently using another account. Map old records with no account to the default account only.
- Index each registered config directory separately, retain source account on every transcript and record, and key deduplication by `(family, account_id, session_id)` to avoid collisions. Resume/fork commands and live tails must use the selected config environment. Make restoration of a running pane compare account IDs as well as family before reusing a harness.

### Gaps
- Session ID uniqueness across separate config directories is not guaranteed by the code; account-scoped keys avoid relying on it.

## What is the minimum viable architecture and test matrix?

### Takeaway
One account registry plus account-scoped process environments and preset IDs is the smallest coherent change. Every path that turns a full preset ID into a family must be audited before routing is safe.

### Cited Findings
- Guest harness startup currently resolves the preset to a family, instantiates an adapter, and starts it without account configuration. Same-family model switching reuses the harness, even if the selected preset changes. — [guest_harness_provider.py:599-657](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:599), [guest_harness_provider.py:1686-1719](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:1686)
- Worker configure and role switching both call guest startup, while planning can start a guest from a tier entry; subagent selection treats only API `PRESETS` as named presets and rejects guest-backed `ProviderConfig` without an injected provider. — [worker.py:281-358](/home/elliott/repos/relay-terminal/backend/worker.py:281), [worker.py:680-703](/home/elliott/repos/relay-terminal/backend/worker.py:680), [subagents.py:165-173](/home/elliott/repos/relay-terminal/backend/relay_core/subagents.py:165), [subagents.py:286-317](/home/elliott/repos/relay-terminal/backend/relay_core/subagents.py:286)
- Quota weighting is applied only within tied ranks, and ordinary failover explicitly excludes guest harnesses as replacement targets for an already-running turn. — [roles.py:500-529](/home/elliott/repos/relay-terminal/backend/relay_core/roles.py:500), [roles.py:1024-1044](/home/elliott/repos/relay-terminal/backend/relay_core/roles.py:1024)

### Inferences
- **MVP components:** (1) versioned account registry with immutable IDs, family, display label, absolute config directory, default-account migration; (2) account-aware preset parser and `HarnessProvider` identity; (3) per-process launch environment for Claude/Codex harness, Tier B, auth probe, and catalog probe; (4) account-keyed login/quota cache and events; (5) picker/tier/UI account rows; (6) account-aware session files/index/resume. Keep credentials in CLI-owned directories, not the Relay registry.
- **Test matrix:** two Claude and two Codex accounts concurrently; separate auth results, env and transcripts; default legacy ID compatibility; model and effort switch within one account; account switch with the same family must start a new harness; restart and restore each account; deleted-account recovery; Tier B launch/resume/fork and index; quota updates/exhaustion/recovery isolated by account; tied-rank weighted draw and untied deterministic rank; unknown/stale quota; plan-tier guest startup; subagent inheritance/fallback; picker folded model with distinct `via` accounts. Use fake spawned CLIs to assert environment and account IDs; run a narrow live smoke test with two actual accounts only after the fake tests pass.

### Gaps
- The current code cannot prove vendor authentication semantics or quota polling frequency for alternate accounts. Live two-account verification remains necessary before treating routing as reliable.
