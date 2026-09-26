---
id: 05J2
type: work
status: needs-verification
labels: [feature]
component: [gui, worker]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: glm/glm-5.3-flashx
rank: zz05
created: '2026-09-17'
acceptance: settings export to a file and import on another machine; a design for optional encrypted sync is recorded
verify: {artifact: code, primary: script, also: [person], human: optional, criteria: 'export on one machine, import on another: theme, keymap, model roles and global aliases come back; no API key or device identity is in the file', sign_off: none, effort: medium, stakes: reputation, blast: capability}
source: '`issues/feature_intake.txt`, 2026-09-17: "no accounts, but make it where you can export your settings or otherwise make it easy to share across computers, maybe with a brave-like sync system."'
links: {plans: [], commits: [6ee5b1603076, a00a73e41cbb], evidence: [docs/qa_evidence/2026-09-25-05j2-settings-export/], related: [], github: null}
---
# Export settings, and an optional sync across machines

No accounts. Export and import everything that is not a secret: keymap, theme, model roles (not keys),
Agent options, Switchboard preferences, aliases. Then consider a Brave-style sync: a device pairs with a code,
content encrypted end to end, no account, using the same pairing machinery as remote access (#W5N2).

That machinery now exists and was deliberately factored to be reused (2026-09-18): `remote/pairing.py`
is pairing on its own — the QR link with the secret in the fragment, single-use short-lived rooms,
and the five-digit code both ends derive from the handshake — with no knowledge of panes or agents.
`remote/noise.py` and `remote/identity.py` hold the session and the pinned device keys. Sync would
add its own payload over that, not a second way to pair.
API keys stay in the keyring and are never exported unless the user explicitly asks.

## Done means
- Options has **Export settings…** and **Import settings…**; a file exported on one machine and imported on a
  fresh profile on another brings back the theme, keymap, model roles/priorities, Agent and Security options
  and global aliases, and they are in effect without a restart.
- The exported file contains no API key, token, remote device identity or pinned device, and no per-machine
  state (recents, counters, migrations, window layout). A test greps the file for every keyring-held value
  and for `remote/` state and fails if one is there.
- Import previews changes before applying. Different non-default values on both machines are shown side by side and require the user's choice; cancel or an unresolved conflict changes nothing. A malformed, newer-version or foreign file is refused with one sentence and changes nothing.
- A design for optional end-to-end-encrypted sync over the existing pairing machinery is recorded in
  `docs/`, and the card links it. Failure looks like: a secret in the file, a setting silently dropped on
  import, or sync being built with a second pairing mechanism.

## Plan
**Goal.** Ship export/import of every non-secret preference as one versioned JSON file, reachable from
Options and the palette, and record (not build) a design for Brave-style E2E sync that reuses `remote/`
pairing. Sync implementation becomes its own card once the design is accepted.

**Findings** (re-verified 2026-09-25; nothing of this exists yet — no export/import code in `src/`,
`backend/`, `scripts/`; `git log --grep` finds only the intake commit `d8358259` and the 2026-09-19 sweep
`0a676397`, which left #05J2 open; #XZZB's resolution says #05J2 is the only part of the Options work left).

Where the preferences live today:
- **QSettings** `~/.config/RelayTerminal/relay.conf` (org `RelayTerminal`, app `relay`, `src/main.cpp:372`),
  read through the cache `src/SettingsCache.cpp` (`relay::settings::value`, `invalidate()`). Groups in use:
  `agent`, `appearance`, `guests`, `hints`, `hosted`, `instructions`, `isolation`, `logging`, `migrations`,
  `models`, `options`, `palette`, `provider`, `remote`, `roles`, `scratch`, `security`, `suggestions`,
  `terminal`, `theme`, `tiers`, `url_handler`, `voice`. Several are state, not preference:
  `palette/recent`, `models/recent|uses|count|last|collapsed|available`, `options/collapsed`,
  `migrations/*`, `url_handler/announced`, `hosted/disclosed`, `instructions/onboarded`, `hints/*`,
  `scratch/monitor_*`. Some are machine-specific: `isolation/*_memory_max`, `instructions/files` (paths),
  `remote/address`, `remote/alwaysOn`.
- **Keymap**: `~/.config/RelayTerminal/relay/keybindings.json`, `Keymap` in `src/Keymap.h` (path built at
  ~line 534, reload via the `keybindings.reload` action).
- **Themes**: selected id in `theme/name` (`src/Theme.cpp:238`, applied at `:1064`); user theme files in
  `$XDG_CONFIG_HOME/relay/themes/*.toml` (`src/ThemeFile.cpp:631`).
- **Global aliases, memories, board prefs**: `~/.config/relay/switchboard/` (`aliases.global_root()`,
  `backend/relay_core/aliases.py:444`; `memory/`, `board.yaml`).
- **Local model servers**: `~/.config/relay/local-models.json` (`{version, endpoints}`).
- **Secrets**: API keys in the keyring via `backend/relay_core/keystore.py`; rows holding them are marked
  secret on `SettingRow` (`src/SettingsPane.h`, the #FEJQ block after `reset`/`changed`). Remote identity
  and pinned devices: `remote/identity.py` (`identity.key`, keyring attribute `remote-identity`) — never
  exported.
- **Options UI**: sections built in `src/RelayWindowSettings.cpp` (General at :9 … About at :1092; the
  per-page "Reset to defaults" loop at ~:1160 is the precedent for a page-level button). `SettingRow` has
  no QSettings key, so export cannot be driven from the rows — it needs its own allow-list.
- **Pairing for sync**: `remote/pairing.py` (pair/invite URLs with the secret in the fragment, `Room`,
  `RoomBook`), `remote/noise.py`, `remote/identity.py`, `remote/cpace.py`; related card #H0P3 (persistent
  phone identity / possible relay-terminal.ai account) may change the rendezvous story.

**Steps**
1. `src/SettingsExport.{h,cpp}` (new, in `relay-agentcontext`-style small library or the `relay` target;
   add to `CMakeLists.txt`): one table `kExportable` listing QSettings groups/keys that are preferences
   (allow-list, not deny-list, so a new key is private until someone adds it), plus `exportTo(path)` →
   JSON `{format:"relay-settings", version:1, exported_at, relay_version, settings:{key:value},
   files:{"keybindings.json":…, "themes/<name>.toml":…, "local-models.json":…}}` and
   `importFrom(path, Mode)` → a result listing applied/skipped keys. Refuse unknown `format`, higher
   `version`, non-object JSON. After validation and conflict resolution, stage every target file and write the current state to
   `~/.config/RelayTerminal/relay/settings-backup-<stamp>.json` before committing the staged change set. If any validation or write fails, leave the profile unchanged.
2. Global aliases (and, if the owner says so, global memories — Q2): the worker owns those files, so
   add `backend/relay_core/settings_bundle.py` with `export_global()`/`import_global()` over
   `aliases.global_root()`, and one worker message pair (`settings_export_globals` /
   `settings_import_globals`) documented in `docs/AGENT-SESSIONS-PROTOCOL.md`. Can run in parallel with 1.
3. Apply without restart: after import call `relay::settings::invalidate()`, reload the keymap, re-apply
   `theme/name`, and `refreshSettingsPanes()` (as the reset loop does). Depends on 1.
4. UI: in `src/RelayWindowSettings.cpp` General section, a Buttons row "Export settings… · Import
   settings…" (QFileDialog), and two palette actions `settings.export` / `settings.import` registered in
   `src/Keymap.h` with no default keys. Import opens a review screen before applying: summary of added,
   unchanged and conflicting items, with current and incoming values shown side by side. A conflict is
   a different non-default value on both sides; resolve each with Keep current or Use imported, plus
   bulk choices for remaining conflicts. For keybindings, aliases and endpoints, compare by their stable
   item key rather than treating the whole file as one conflict. Preserve the current value unless the
   user explicitly resolves a conflict. Cancel applies nothing. Show that API keys are excluded. Depends on 1–3.
5. A headless CLI path for scripting and tests: `relay --export-settings <file>` / `--import-settings
   <file>` via `QCommandLineOption` in `src/main.cpp:430` (exits without opening a window). Optional if
   the owner prefers UI only.
6. Sync design (docs only, can run in parallel with 1–5): `docs/SETTINGS-SYNC-DESIGN.md` — a "sync chain"
   of devices paired with `remote/pairing.py` links + the five-digit code; the bundle from step 1
   encrypted with a chain key derived during pairing (Noise session, no new crypto); transport options
   (direct LAN/tailnet via `remote/httpd.py`, or an opaque mailbox on the existing rendezvous that stores
   only ciphertext); conflict rule (non-default divergent values require the same user-facing resolution as import; timestamps can order non-conflicting updates but never silently overwrite a conflict); what never syncs (the
   same secret list as step 1); relation to #H0P3. Link it in `links.plans`.
7. Docs: a short "Moving to another machine" section in the user docs next to Options, naming what is
   and is not in the file.

**Risks**
- Defaults can change between versions: keep a source version/schema marker, compare the incoming override with the destination's current defaults, and preview every resulting change rather than writing raw source defaults.
- A new secret-bearing QSettings key slipping into exports — mitigated by the allow-list and the test in
  Verify that fails on any keyring value or `remote/*` key.
- Theme/keymap files referencing things the other machine lacks (a theme name with no file, a local-model
  URL on `localhost`) — import must fall back, not break.
- Cross-platform paths (macOS/Windows QSettings formats): export values as JSON types, never raw INI.
- Owner questions:
  1. **Import semantics** — decided: merge. Imported non-default values fill defaults; identical values are no-ops; when both current and imported values are different non-default customizations, show a conflict and let the user choose. No silent overwrite.
  2. **Scope of "everything"** — include global aliases (recommended yes), global memories and
     instructions under `~/.config/relay/switchboard/memory` (recommended: opt-in checkbox, they can hold
     personal facts), and local model servers (recommended yes; they are URLs, not keys)?
  3. **Machine-specific options** (`isolation/*` memory limits, `instructions/files`, `remote/*`) —
     leave them out (recommended)?
  4. **Sync** — design only on this card and a follow-up card to build it (recommended, and what the
     `acceptance` line asks), or build it here? And should it wait on #H0P3's decision about an account
     on relay-terminal.ai?
  5. Is the CLI flag (step 5) wanted, or UI only? A headless import must stop and report unresolved conflicts, not choose for the user.

**Verify**
- New `tests/settingsexport_test.cpp` (`relay-settings-export-tests` in `CMakeLists.txt`), run with
  `ctest --test-dir build -R settings-export`: round-trip into a temp `QSettings` profile
  (`QStandardPaths::setTestModeEnabled`) — every allow-listed key comes back equal; state keys and
  `remote/*`, `isolation/*` are absent; a fake keyring value and `identity.key` bytes never appear in the
  file; a `version: 99` file and a non-JSON file are refused with nothing written; the backup file exists
  after an import; two different non-default values produce a preview conflict and no write until resolved;
  Keep current and Use imported each produce the selected result; cancel and failed staging leave all settings unchanged.
- `tests/test_settings_bundle.py` (pytest) for the worker half: global aliases round-trip into an empty
  `XDG_CONFIG_HOME`.
- `tests/settingspane_test.cpp`: the General page has the Export/Import row.
- By hand: export on this machine, `XDG_CONFIG_HOME=$(mktemp -d) ./build/relay --fresh`, import, and see
  the theme, a custom shortcut and an alias work immediately; `grep -i key` the exported file.
**Preference coverage and conflict granularity (owner clarification, 2026-09-25).** Ordinary Agent, Security, appearance, theme, provider and Switchboard preferences are included when customized; model role assignments, provider/model priorities and other model choices are included, but usage/recent/availability state and API keys are excluded. Custom hotkeys from `keybindings.json` are included as per-action overrides. The import preview compares each preference key, model role/priority and hotkey action separately, showing Current and Imported values for divergent non-default choices. Thus one conflicting shortcut does not block unrelated shortcuts or the whole keymap; users can keep or import that one binding. Validate imported model identifiers and shortcuts against the destination installation and show unsupported entries as skipped or requiring attention before applying. Add round-trip and conflict tests for one model role, one priority, one ordinary preference and two custom hotkeys where only one conflicts.

## Decisions
Owner, 2026-09-25: “05J2, yes, but help the user resolve conflicts for non defaults.” Import merges profiles. When current and incoming values are distinct non-default customizations, preview both and require an explicit per-item or bulk choice before applying. Defaults and identical values merge automatically. This decision also governs the proposed sync design; sync must not silently choose last-writer-wins for such conflicts.
Owner, 2026-09-25: “deliver 05J@ in a subagent and then lets discuss the next card” (immediately after confirming that settings, model preferences and custom hotkeys are covered). Proceed with the plan as clarified: merge imports with per-item conflict resolution; include customized settings, model preferences and custom hotkeys; keep secrets and machine state out; deliver sync as a design, not an implementation.

## Execution Summary
Landed across four commits. `9f092a3a`: the settings bundle core — `src/SettingsExport.{h,cpp}` (allow-listed QSettings keys, keybindings/themes/local-models files, per-key merge plan with Conflict/Attention/Skip statuses, staged apply with automatic backup, secrets and machine state never exported), `src/SettingsTransferDialog.{h,cpp}` (export dialog and import review screen with side-by-side values, per-row Keep current/Use imported, Apply anyway for attention items), `tests/settingsexport_test.cpp`.

`6ee5b160`: the remainder — Options › General "Settings on other machines" row (Export…/Import…), `settings.export`/`settings.import` palette entries, `src/SettingsCli.{h,cpp}` behind `--export-settings`/`--import-settings`/`--include-memories`/`--import-resolve`/`--import-accept-attention` (headless: unresolved conflicts or unaccepted attention print the plan, change nothing, exit 2), post-import `settings::invalidate()` + keymap reload + theme reapply + Options refresh (no restart), and the docs: `USER-SETTINGS.md` (moving to another machine), `CONFIG-FILES.md`, `BACKUP-AND-RESTORE.md`, `SETTINGS-SYNC-DESIGN.md` (sync stays design-only: E2E over the existing `remote/pairing.py` machinery, same conflict rules as import), plus the docs/README index rows.

A real bug found during verification: the import dialog took its model/hotkey validation hooks as a public member set *after* the constructor had already planned the rows, so unknown model ids never surfaced. Fixed with `SettingsImportDialog::setHooks()` which re-plans; landed with the salvage commits (`def2cf0b`) and re-verified here on a clean archive of `main`.

Evidence `a00a73e4` under `docs/qa_evidence/2026-09-25-05j2-settings-export/`: import-review screenshots (undecided = Apply disabled; decided with an accepted attention row = applied, values land in `relay.conf`) and the headless CLI transcript (rc 0/2/1, backup written, nothing-written-on-unresolved).

## Tests
- pass · `relay-settings-export-tests` (ctest): **14 passed, 0 failed** on a clean `git archive` of `main` — round-trip, state/secret exclusion, version refusal, backup, conflict preview and resolution (`docs/qa_evidence/2026-09-25-05j2-settings-export/README.md`).
- pass · `relay-settings-tests` `settingsTransferRowSitsOnGeneral`: the General page carries the Export/Import row and palette keys (verify-slot build). One pre-existing unrelated failure in that binary (`theHelperConsoleFollowsTheModeAndAsksAsTheOptionsPane`, the #E8V1 label vs a stale test expectation) is another card's, not this change's.
- pass · GUI import review under offscreen: 2 conflicts + 1 attention row, Apply disabled until every row is decided, accepted attention applies and writes `relay.conf` — `review-undecided.png`, `review-decided.png`.
- pass · headless CLI transcript: export rc 0; unresolved/attention import rc 2 with nothing written; `--import-resolve imported` applies and writes `relay/backups/settings-backup-<ts>.json`; bad version rc 1 — `cli-transcript.txt`.
- not applicable · full `ctest`/`scripts/test.sh` suites: owner runs those.
