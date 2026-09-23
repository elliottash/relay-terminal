# Several Claude Code / Codex logins side by side (#M8S2)

## Driven in the app (`drive.sh`)

Built `relay` and drove it under Xvfb in an isolated home. The `claude` and `codex` on PATH are the
fakes from `tests/test_guest_accounts.py`: they answer the status commands from a `signed-in`
marker in the account's directory, and `claude auth login` writes that marker. No real login is
read and no model is asked anything. The registry starts with Claude Code `work` (signed in) and
Codex `eth` (not signed in).

- `02-providers.png`: Options › Models › Guest Agents. The default "claude code" row has
  **add account…**. "claude code (work)" is a row of its own, with **sign in / test / remove** and
  its own login state.
- `03-signed-out-account.png`: "codex (eth)" says it is not logged in, and that **sign in** runs
  the CLI's sign-in for that account.
- `04-add-account-dialog.png`: the add-account dialog: a name, and an optional existing directory.
- `05-account-added.png`: after "personal" + OK, the worker registered it
  (`guest-accounts-after-add.json`) and typed
  `CLAUDE_CONFIG_DIR=…/claude-personal claude auth login` into the pane.
- `05b-signed-in-row.png`: the sign-in exited 0, so the pane sent `guest_logins_refresh`, and
  "claude code (personal)" reads *logged in* without anyone pressing test.
- `07-available-via.png`: the models pane's available tab. `claude-opus-5.5` is one row whose
  **via** list names "claude code" and "claude code (work)", so a model is picked per login.

## Against the real CLIs on this machine (2026-09-23)

A throwaway registry (`RELAY_GUEST_ACCOUNTS=/tmp/m8s2-live/accounts.json`) listed the usage
tracker's existing homes under `~/.config/usage-tracker/`, reused as they are.

- The status commands, through Relay's own code (`_read_login_status` with each account's
  environment): all five homes and both default logins answered *logged in*. `preset_rows()`
  listed `guest:claude:gmail`, `guest:claude:e`, `guest:codex:eth`, `guest:codex:gmail` and
  `guest:codex:e` as separate rows, each with the CLI's models.
- Identity: `claude auth status` under `CLAUDE_CONFIG_DIR` shows `elliott.t.ash@gmail.com` for
  `claude-gmail`, and `e@elliottash.com` for both `claude-e` and the default login. The three Codex
  homes hold three different ChatGPT account ids (compared by hash). The `-e` homes turn out to be
  the same accounts as the default logins.
- One real test turn per account (`keytest.run`, the Options "test" button):
  - `guest:codex:gmail`: **ok**, gpt-6-astra, reply "ok", 6.9 s.
  - `guest:codex:eth`: that account's own answer: "You've hit your usage limit…". The account is
    out of quota, which is itself per-account attribution working.
  - `guest:claude:gmail`: Claude Code refused to refresh the token: "another Claude Code process is
    refreshing it or exited mid-refresh". The directory holds an `.oauth_refresh.lock` from 19:00,
    matching its `.last-cleanup` stamp from the tracker's hourly run, not from Relay. It was left
    alone because it is the tracker's directory.

## Tests

`tests/test_guest_accounts.py` (20 tests), including end-to-end `test_key` runs on the fake CLIs
that record the environment each process got. The affected existing suites
(`test_guest_harness_provider`, `_claude`, `_codex`, `test_model_switch`, `test_guest_delegation`,
`test_guest_handover`, `test_keytest`, `test_keytest_guest`, `test_model_ranking`, `test_roles`,
`test_plan_turns`, `test_presets`, `test_guest_sessions`, `test_conv_index`) have no new failures.
The five failures they do show (test_plan_turns ×3, test_presets GuiMirror ×2) fail identically
on a clean export of `HEAD` without this change.
