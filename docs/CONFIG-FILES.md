# Configuration files

Everything Relay keeps on disk outside a workspace. `$XDG_CONFIG_HOME` defaults
to `~/.config`; `RELAY_CONFIG_HOME` overrides it.

## Preferences and state

| path | what |
| --- | --- |
| `$XDG_CONFIG_HOME/RelayTerminal/relay.conf` | GUI preferences (QSettings, org `RelayTerminal`, app `relay`). Presence of a key = a customization; absence = the built-in default. |
| `$XDG_CONFIG_HOME/RelayTerminal/relay/keybindings.json` | keyboard preset, program-keys mode, per-action binding overrides (`docs/KEYBINDING-PRESETS.md`) |
| `$XDG_CONFIG_HOME/RelayTerminal/relay/backups/settings-backup-<stamp>.json` | automatic profile backups written before a settings import applies (`docs/BACKUP-AND-RESTORE.md`) |
| `$XDG_CONFIG_HOME/relay/relay.md` | global instructions, sent to every worker |
| `$XDG_CONFIG_HOME/relay/switchboard/aliases/*.md` | global alias cards (one markdown file per alias) |
| `$XDG_CONFIG_HOME/relay/switchboard/memory/*.md` | global memory cards |
| `$XDG_CONFIG_HOME/relay/local-models.json` | local model endpoints: `{"version":1,"endpoints":[…]}`; loopback `http://` URLs only |
| `$XDG_CONFIG_HOME/relay/themes/<id>.toml` | user theme files |
| `$XDG_CONFIG_HOME/relay/custom-providers.json` | custom provider definitions (backend) |

## Never moved between machines

The settings bundle (`docs/USER-SETTINGS.md`, `docs/SETTINGS-SYNC-DESIGN.md`)
exports customized preferences only. It never carries credentials, remote
pairing identity or pinned devices, usage/recent/availability state,
onboarding markers, machine-specific paths (agent CLIs, `models_json`, plans
dir, login shell) or memory limits — those either live only in the backend
keystore or describe this machine.
