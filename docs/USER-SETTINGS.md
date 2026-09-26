# User settings reference

Where Relay keeps your preferences, which of them move between machines, and
how. File locations are in `docs/CONFIG-FILES.md`.

## Where settings live

* **GUI preferences** — `relay.conf` (QSettings, org `RelayTerminal`, app
  `relay`): everything under Options — General, Appearance, Terminal, Agent,
  Security, Voice, Privacy, model roles and provider/model priorities.
  Every row follows one convention: *a reset forgets the key*. A key present in
  the file is a customization; its absence means the built-in default applies.
* **Keyboard bindings** — `keybindings.json`: the chosen preset, the
  program-keys mode and your per-action overrides (see
  `docs/KEYBINDING-PRESETS.md`).
* **Global aliases and memories** — `relay/switchboard/aliases/*.md` and
  `relay/switchboard/memory/*.md`.
* **Global instructions** — `relay/relay.md`.
* **Local model endpoints** — `relay/local-models.json` (loopback servers only;
  see `docs/LOCAL-MODELS.md`).

## Export settings…

Options › General › *Settings on other machines* › **Export settings…** (also
in the command palette, and headless: `relay --export-settings FILE`).

The bundle carries your customized preferences only: Agent/Security/appearance/
theme/provider/Switchboard settings that differ from the defaults, model role
assignments and provider or model priorities, custom hotkeys, global aliases,
local endpoints, and — only when you tick the box — global memories and
instructions. It **never** carries API keys, tokens, pairing identity or pinned
devices, usage history, availability state, machine paths or memory limits.

## Import settings…

Options › General › **Import settings…** (also in the palette, or headless:
`relay --import-settings FILE`).

Import merges; it never replaces. You always see the review screen first:
current and incoming values side by side, with

* **merges** — identical values, and incoming non-defaults over a local
  default, apply without asking;
* **conflicts** — where both machines customized the same thing differently,
  you choose *Keep current* or *Use imported* per item, with a bulk choice for
  the rest;
* **needs attention** — model ids this install does not know; applied only if
  you accept them;
* **skipped** — entries this build cannot use (unknown action, unreadable
  shortcut, endpoint the worker would reject), shown with the reason.

Nothing is written until every conflict is decided; Cancel writes nothing.
Before anything changes, a timestamped backup is written under
`relay/backups/` (see `docs/BACKUP-AND-RESTORE.md`). Covered surfaces apply
without a restart; a failed write leaves the profile unchanged.

## Command line

```sh
relay --export-settings relay-settings.json                # non-secret preferences
relay --export-settings full.json --include-memories       # + memories, instructions
relay --import-settings relay-settings.json                # review, then apply
relay --import-settings f.json --import-resolve imported   # bulk-resolve conflicts
relay --import-settings f.json --import-resolve keep --import-accept-attention
```

The headless import never chooses for you: with unresolved conflicts it stops
with exit 2 and prints them; nothing on disk changes. Exit codes: 0 applied or
exported, 1 bad invocation or unreadable bundle, 2 unresolved conflicts or
unaccepted attention items.
