# Backups and restore

## Settings import backups

Before a settings import writes anything (Options › General › Import
settings…, or `relay --import-settings`), Relay snapshots everything the import
could touch into one file:

```
$XDG_CONFIG_HOME/RelayTerminal/relay/backups/settings-backup-<yyyyMMdd-HHmmss>.json
```

The backup records, for each affected unit: the prior value (or absence) of
every QSettings key the plan writes, the full prior content of
`keybindings.json` and `relay/local-models.json` (or that they did not exist),
and the prior content of every alias, memory, theme and instructions file the
plan writes. It is written before the first change; if the backup itself
cannot be written, the import applies nothing.

The import result dialog and the CLI both print the backup path. To undo an
import, restore that file:

```sh
# in-process restore (the GUI does this automatically if a write fails midway)
relay --import-settings <backup>.json   # not a bundle: use the restore dialog
```

Headless restore is a small JSON apply (present → restore value, absent →
remove). If a write fails midway through an import, Relay restores the backup
itself and reports the failure; the profile is left exactly as it was.

## What is *not* backed up this way

API keys and tokens live in the backend keystore and are never part of a
settings import, so they are never touched by one. Workspace state, session
transcripts and board files live under each workspace and are not affected.
