# 05J2 — settings export/import: verification evidence

Everything here was produced from `main` as landed (the `setHooks` fix and the
UI/CLI/docs remainder went out with `6ee5b160` and the salvage commits
`def2cf0b`/`b52a33ad`).

## Headless tests

`git archive HEAD | tar -x` + configure + `cmake --build b --target
relay-settings-export-tests`: **Totals: 14 passed, 0 failed** — merge plan,
conflict resolution, backup/staged apply, endpoint and model validation,
hotkey conflict handling.

## Import review screen (GUI)

`review-undecided.png` / `review-decided.png`: the landed
`SettingsImportDialog` driven by a throwaway Qt5 harness that links
`src/SettingsTransferDialog.cpp` + `src/SettingsExport.cpp` and imports a real
CLI export whose `models/favorites` carries one id this install does not know
(`nowhere/unknown-model`). Offscreen platform, `QWidget::grab()`. A harness
was used because the import opens from Options/palette, not from an
`app_command` action that `scripts/relay-drive` can trigger.

- Undecided: **2 conflict(s) to decide · 1 needing attention**, the unknown
  model row carries an unticked **Apply anyway** box, Apply is **disabled**
  ("Nothing is written until every conflict is decided — Cancel applies
  nothing.").
- Decided (combos to *Use imported*, *Apply anyway* ticked): Apply **enabled**;
  clicking it wrote all three keys to `relay.conf`
  (`max_steps=77`, `favorites=nowhere/unknown-model, local/sunset`,
  `name=sunset`).
- This run exercises the `setHooks` fix: the hooks are now set before the row
  plan is built, so unknown model ids surface at review time instead of being
  silently applied.

## Headless CLI (`cli-transcript.txt`)

Built `relay` from a clean archive of `main`; isolated `XDG_CONFIG_HOME`:

- `--export-settings`: 0 keys on a pristine config; 3 after seeding, `rc=0`.
- Import with an unverifiable model id and no resolution: plan printed,
  **nothing changed, `rc=2`** (attention needs `--import-accept-attention`).
- Import over local customizations without `--import-resolve`: conflicts
  listed, file byte-identical before/after, `rc=2`.
- `--import-resolve imported`: applied 3 changes and wrote
  `relay/backups/settings-backup-<ts>.json`, `rc=0`.
- Unknown bundle version: `Unsupported bundle version`, `rc=1`.
