# Tracker portability for multiple subscriptions

## What does the tracker actually collect, and how are accounts identified and refreshed?

### Takeaway
The tracker proves that separate Claude Code and Codex config directories can hold simultaneous logins on this Linux machine. It is a personal quota exporter, not a general identity or credential service: labels and sheet rows are manually assigned, and CLI token files are read directly.

### Cited Findings
- The account table pins three Codex homes and three nondefault Claude config directories to absolute `/home/elliott` paths; one Claude account uses the default directory. Emails and row numbers are literals, with no check that the credential belongs to the stated email. [Tracker lines 45–70](/home/elliott/data/usage_tracker/check_usage.py:45).
- Claude Code officially documents `CLAUDE_CONFIG_DIR` as an override useful for simultaneous accounts. It relocates settings and session history as well as credentials, and project/local settings cannot set it. [Claude environment variables](https://code.claude.com/docs/en/env-vars). [Tracker lines 99–105](/home/elliott/data/usage_tracker/check_usage.py:99).
- Claude Code credentials are normally `.credentials.json` on Linux and Windows, but macOS normally uses Keychain; `CLAUDE_CONFIG_DIR` also keys a distinct Keychain entry. Direct file reading therefore fails for the normal macOS case. [Claude authentication](https://code.claude.com/docs/en/authentication). [Tracker lines 99–105](/home/elliott/data/usage_tracker/check_usage.py:99).
- Codex officially allows `file`, `keyring`, `auto`, and `ephemeral` credential stores; only file storage guarantees `CODEX_HOME/auth.json`. The tracker assumes that file and reads token fields from it. [OpenAI configuration reference](https://developers.openai.com/codex/config-reference). [Tracker lines 108–114](/home/elliott/data/usage_tracker/check_usage.py:108).
- On a 401, `api_json` calls the owning CLI once and retries. Claude refresh invokes `claude -p /usage`; Codex invokes `codex login status`. `run_cli` ignores return code and output, sets a Linux-specific `PATH`, and has a 120-second timeout. It is not established that `codex login status` refreshes a token or that `/usage` is a side-effect-free refresh command. [Tracker lines 89–126](/home/elliott/data/usage_tracker/check_usage.py:89).
- The tracker does not ask Claude or Codex to identify the logged-in account before using a manually attached email label. Claude Code documents `/status` as showing login organization and email for the active credential; it also documents precedence under which API keys and other credentials can outrank subscription OAuth. [Claude authentication](https://code.claude.com/docs/en/authentication). [Tracker lines 45–65](/home/elliott/data/usage_tracker/check_usage.py:45).
- Claude and Codex web calls use OAuth bearer tokens taken from CLI stores. Kimi and GLM instead use named API keys loaded from a local `.env`. Legacy cookie functions can read Brave, Chrome, or Firefox cookies but are not selected by the current account table. [Tracker lines 39–40, 63–81, 149–164, 207–242, 249–285, 331–337](/home/elliott/data/usage_tracker/check_usage.py:39).

### Inferences
- Relay should store an account's stable internal ID, vendor, display label, and config-home pointer separately; verify the active account through the owning CLI when possible. A label alone does not prove which subscription will run.
- Reuse the per-account directory idea, while letting CLIs own OAuth and refresh. A consumer of private token files must account for OS credential stores, login changes, and token rotation.

### Gaps
- No official public documentation found for the tracker's Claude and Codex quota HTTP endpoints, their response schemas, or whether direct bearer-token use is supported for third-party clients.
- No read-only evidence here verifies actual on-disk credential contents, account identities, or whether the refresh subprocesses work today; credentials were deliberately not opened and tracker main was not run.

## How do endpoint, quota, polling, and sheet assumptions compare with Relay?

### Takeaway
Relay already has a safer normalized quota path for the same Kimi and Z.ai endpoints and direct guest-reported limits for Claude and Codex. The tracker's four-column weekly snapshot would discard useful windows and couple routing to a Google Sheet.

### Cited Findings
- The tracker polls undocumented `api.anthropic.com/api/oauth/usage` and `chatgpt.com/backend-api/wham/usage`, plus Kimi `/coding/v1/usages` and Z.ai `/api/monitor/usage/quota/limit`. Claude parses `seven_day` and a model named `fable`; Codex chooses a seven-day window if duration is exactly 604800 seconds, otherwise a fallback window. [Tracker lines 13–24, 133–164, 191–225, 249–285](/home/elliott/data/usage_tracker/check_usage.py:13).
- Kimi parsing assumes a top-level `usage` with `limit`, `remaining`, and `resetTime`; Z.ai parsing takes the first `unit == 6` row regardless of type. Relay's `provider_limits.py` also accepts newer Kimi ratio pools and five-hour/monthly windows, filters Z.ai rows by credit/token type, validates finite percentages, and normalizes reset epochs. [Tracker lines 249–285](/home/elliott/data/usage_tracker/check_usage.py:249). [Relay parser lines 43–132](/home/elliott/repos/relay-terminal/backend/relay_core/provider_limits.py:43).
- Relay polls Kimi and Z.ai every 15 minutes on a daemon thread using the existing keystore; it emits `usage_limits` windows and preserves the last good snapshot on failure without logging credential-bearing exception text. The tracker runs each account sequentially, sleeps 1.5 seconds, and catches failures into `ERR` sheet cells; its own script has no recurring scheduler. [Relay polling lines 135–176](/home/elliott/repos/relay-terminal/backend/relay_core/provider_limits.py:135). [Tracker lines 341–375](/home/elliott/data/usage_tracker/check_usage.py:341).
- Relay's Claude harness turns `rate_limit_event.unifiedWindows` into five-hour and weekly `limits` windows. Codex app-server reads `account/rateLimits/read` at start and merges sparse update notifications. The guest provider then emits `usage_limits` and caches one snapshot per guest ID. [Claude adapter lines 1225–1273](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_claude.py:1225). [Codex adapter lines 49–69, 303–367](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_codex.py:49). [Guest cache lines 532–572](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:532).
- Relay exposes provider snapshots in preset rows and only uses quota data at most 30 minutes old to weight tied-rank model selection. The cache is currently per preset/guest, so multiple accounts would require per-account preset identity and cache keys. [Worker lines 153–205](/home/elliott/repos/relay-terminal/backend/worker.py:153). [Model selection lines 1226–1254](/home/elliott/repos/relay-terminal/src/ModelCatalog.cpp:1226). [Guest cache lines 541–563](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness_provider.py:541).
- Sheet output assumes a fixed spreadsheet ID, `Sheet1`, rows 2–10 in account order, columns D:G as available percent/Fable/reset credits/reset date, and a timestamp in row 12. It imports a private helper through an absolute `/home/elliott/alfred` path; that helper stores Google OAuth files in its own `memory` directory and may refresh/write tokens. Google documents `values.batchUpdate` as requiring a spreadsheet ID, input option, and value ranges. [Tracker lines 36–65, 341–370](/home/elliott/data/usage_tracker/check_usage.py:36). [Google helper lines 20–37, 66–122](/home/elliott/alfred/google_utils.py:20). [Google Sheets batchUpdate](https://developers.google.com/workspace/sheets/api/reference/rest/v4/spreadsheets.values/batchUpdate).
- A separate Bash wrapper calls the tracker from cron using a fixed virtualenv, path, UID 1000 DBus socket, and X display for cookie decryption. The wrapper does not state the cron interval. [Cron wrapper lines 1–14](/home/elliott/data/usage_tracker/run_check_usage.sh:1).

### Inferences
- Extend Relay's existing normalized `windows` event and per-account cache, rather than ingesting the sheet's presentation columns. Guest events can supply data while an account is active; independent polling, if wanted for inactive accounts, needs its own source-specific adapter and freshness policy.
- Keep Google Sheets as an optional export outside the routing core. Its write requires another user's spreadsheet and Google OAuth consent, and its fixed row layout cannot represent arbitrary numbers of subscriptions.
- A quota fetch failure should preserve last good data with a timestamp and leave routing conservative. Both Relay's provider poller and selection freshness check already express this behavior.

### Gaps
- No installed cron schedule was inspected, so the tracker's actual polling frequency is unknown.
- The official vendor status of the Kimi and Z.ai usage paths and schema versions was not established; the comparison above is based on local code, not a guaranteed API contract.

## What must change for Linux, macOS, Windows, and other users?

### Takeaway
The isolation mechanism is portable in principle, but the script is Linux and owner specific. A general Relay pipeline needs OS-aware CLI launch and credential handling, configurable account IDs and labels, normalized windows, bounded background polling, and a clear distinction between observed limits and verified account identity.

### Cited Findings
- Absolute Linux paths appear in imports, account config directories, CLI binaries, and the subprocess `PATH`. The cron wrapper additionally uses Bash, `/run/user/1000/bus`, `DISPLAY=:0`, and a fixed venv. [Tracker lines 36–70, 89–96](/home/elliott/data/usage_tracker/check_usage.py:36). [Cron wrapper lines 1–14](/home/elliott/data/usage_tracker/run_check_usage.sh:1).
- Claude documents configuration and credential isolation across platforms, including Windows `%USERPROFILE%` and macOS Keychain behavior. OpenAI documents credential-store choices that prevent assuming a Codex auth file exists. [Claude environment variables](https://code.claude.com/docs/en/env-vars). [Claude authentication](https://code.claude.com/docs/en/authentication). [OpenAI configuration reference](https://developers.openai.com/codex/config-reference).
- The tracker injects Linux user-agent strings for browser cookie fallback, and its error paths can include response excerpts or provider bodies in printed output and the sheet. That is a privacy issue for a productized collector, even though this investigation did not inspect or expose any such responses. [Tracker lines 78–81, 117–126, 228–242, 259–280, 301–312, 341–356](/home/elliott/data/usage_tracker/check_usage.py:78).
- The tracker's timestamp formatting uses the local timezone without recording it, and its output rounds remaining percentage to integers and returns only one reset date. Relay's normalized events retain epoch reset times and one row per quota window. [Tracker lines 315–328, 348–366](/home/elliott/data/usage_tracker/check_usage.py:315). [Relay guest event contract lines 83–97](/home/elliott/repos/relay-terminal/backend/relay_core/guest_harness.py:83).

### Inferences
- Minimum reusable design: per-user account records with stable IDs and user-editable labels; platform-resolved CLI executable and config home; environment isolation on each launch; a source adapter yielding normalized windows with source and observation time; and a failure state that never logs token, body, or cookie values. The selected account ID must travel through guest launch, persisted session, preset, limit cache, and model selection.
- For Claude and Codex, prefer native guest limit events where a guest is active, since Relay already has them. Direct polling of private web endpoints for idle accounts would be an explicitly best-effort fallback and should be separately gated and tested across vendor CLI versions.
- Cross-platform tests should cover file versus OS credential store, two simultaneous accounts with swapped labels, stale quota, login expiry, refresh failure, and restart/resume on the intended account.

### Gaps
- Official documentation reviewed here does not establish a public account enumeration API or guaranteed offline quota API for all four vendors.
- This read-only task did not validate launch behavior on macOS or Windows, inspect credentials, invoke any quota endpoint, or execute tracker main.
