# Settings export / import and sync design

Owner: card #05J2. Status: export/import shipped; sync is design-only (this document).

Relay keeps a user's preferences in a handful of plain files (see
`docs/CONFIG-FILES.md`). Card #05J2 added a versioned JSON bundle that moves the
**customized, non-secret** parts of that profile between machines, with a
visible review screen for conflicts. This document records the data model, the
merge rules, what is deliberately never moved, and how a Brave-style end-to-end
encrypted sync could ride the existing remote pairing machinery without changing
those rules.

## 1. The bundle

```json
{
  "format": "relay-settings",
  "version": 1,
  "exported_at": "2026-09-25T12:00:00Z",
  "settings":  { "agent/effort": "max", "roles/Deep/model": "openrouter/..." },
  "hotkeys":   { "preset": "vscode", "program_keys": "shift-only",
                 "bindings": { "app.palette": ["Ctrl+P"] } },
  "themes":    [ { "id": "sunset", "content": "# a user theme" } ],
  "aliases":   [ { "name": "standup", "content": "..." } ],
  "endpoints": [ { "id": "llama-box", "server": "llamacpp",
                   "base_url": "http://127.0.0.1:8080/v1" } ],
  "memories":     [ { "name": "...", "content": "..." } ],
  "instructions": "global relay.md contents"
}
```

* `version` is the bundle schema version. A reader must refuse newer or unknown
  versions rather than guessing (`readBundle()` reports and stops).
* `settings` carries only keys whose value differs from the UI default. For
  every row the Options UI writes, "default" is the *absence* of the key ("a
  reset forgets the key"), so presence is the non-default convention.
* `memories` / `instructions` are present only when the export was explicitly
  opted in (`--include-memories`, or the checkbox in the export dialog).

## 2. What is exported (allow-list)

Export is an **allow-list, not a scrub of everything**: a key moves only if it
is in the registry (`relay::settingsexport::registry()`) or one of the reviewed
dynamic families (`roles/<role>/{preset,model,effort}`, `tiers/<tier>`,
`models/...` curated subkeys, `models/profiles/<name>`). A future key that is
not added to the registry simply does not travel — an export gap, never a leak.

Covered by the registry today: Agent (execution, effort, failover, session
limits, terminal context/handoff, QA, skills), Security (clipboard, unattended
tools, command denylist, secret patterns, approvals-ask), Appearance and theme,
Terminal, Switchboard (`board/signals_auto_work`) and suggestions, Voice,
Privacy, General rows, model role assignments, provider/model priority and
profile keys.

## 3. What is never exported

`relay::settingsexport::neverExported()` — enforced *in addition* to the
allow-list, so a family can never drag these along:

* credentials: API keys, tokens (they live in the backend keystore, not in
  these files; endpoint dicts are stripped of anything key/token/secret-shaped
  on export);
* remote pairing identity and pinned devices (`remote/...`);
* live state: usage/recent models, model availability, pane/session state,
  onboarding markers, url-handler announcements;
* machine-specific paths: agent CLIs, `models_json`, extra paths, plans dir,
  login shell;
* memory limits (`isolation/...`, `*_memory_max`) — owner decision on the card.

Global memories and the global instructions file are **opt-in**: personal
enough that the default export leaves them out.

## 4. Merge rules (import)

Import **merges; it never replaces**. Every item is planned per *unit* — one
QSettings key, one role/priority, one hotkey action, one alias or memory card,
one endpoint — never whole-file:

| situation | outcome |
| --- | --- |
| identical on both sides | merges automatically (no-op) |
| incoming non-default, current default/absent | merges automatically |
| incoming is the default | kept current (defaults never overwrite a customization) |
| both sides non-default and different | **conflict**: review screen shows current vs imported side by side, user chooses Keep current / Use imported per item, or one bulk choice for the remaining conflicts |
| model id unknown to this install | **attention**: shown, not silently applied; applies only if explicitly accepted |
| unknown hotkey action, unreadable shortcut, endpoint the worker would reject | **skip**: reported with a reason |

Cancel, or closing the dialog with conflicts undecided, writes nothing — not
even the items that would have merged automatically. The headless CLI behaves
the same: `relay --import-settings f.json` stops with exit 2 and prints the
conflicts unless the caller passes `--import-resolve keep|imported` (the bulk
choice) and `--import-accept-attention`; the CLI never picks for you.

## 5. Staging, backup, failure

`applyImport()`:

1. refuses while any conflict is unresolved or an attention item is unaccepted;
2. snapshots every QSettings key, keybinding file, switchboard card, theme file
   and `local-models.json` the plan could touch into one timestamped backup,
   `…/backups/settings-backup-<stamp>.json`;
3. applies settings → keybindings.json → cards/themes/instructions →
   `local-models.json`;
4. on any failure restores the backup and reports; the profile is unchanged.

If the backup itself cannot be written, nothing is changed. Live state is
re-applied after a successful import (settings cache invalidation, keymap
reload, theme re-apply) — no restart needed for the covered surfaces.

## 6. Sync design (not implemented)

A Brave-style sync over the existing remote pairing/Noise machinery:

* **Transport.** Reuse the remote pairing trust root (`remote/pairing`,
  pinned devices) and the Noise-encrypted channel the remote-control feature
  already establishes between a desktop and its paired devices. Two transport
  shapes fit without new trust code:
  1. *pair-direct*: the phone/desktop pair already shares a Noise session; sync
     frames ride it as a new message family next to remote-control events;
  2. *relay-store-and-forward*: for devices not simultaneously online, an
     encrypted blob (below) is stored on the relay remote host against the
     pairing id and pushed on reconnect. The host stores ciphertext only.
* **Payload.** The same bundle object, but per-category deltas with monotonic
  `revision` counters per category instead of one whole bundle, so a device can
  pull "what changed since revision N" rather than re-deciding the world.
* **Encryption.** The payload is sealed to the pairing's Noise key chain
  (symmetric ratchet derived at pairing time), so the relay host never sees
  preference values. Keys live on the paired devices only.
* **Never synced.** Exactly §3 — plus anything the device cannot validate: a
  model id or endpoint the receiving install does not serve still goes through
  the §4 attention path on arrival (sync **applies nothing silently**).
* **Conflict rule.** The same rule as import: identical or
  incoming-over-default merges; differing non-defaults surface as conflicts on
  the receiving device's review screen with Keep current / Use imported and a
  bulk choice. There is **no silent last-writer-wins**: a sync push that would
  overwrite a local non-default with a different non-default is held as a
  conflict until the device answers. Deletions are tombstoned per unit so a
  device that chose Keep current is not re-offered the item forever.
* **Scope guard.** Machine-specific keys (§3) never enter a sync payload at
  all, so a stolen sync store leaks nothing about paths, identity or limits.
