# hetzner — dossier

## What it is and what we were trying to do

`~/admin/hetzner/` (inside `~/Dropbox/_Ash_Admin`, verified with `readlink`) is the runbook folder for seven Hetzner assets: three Cloud VPSs (`recode-1`, `elliott-main-1`, `trace-law`), a Mattermost VM (`lexchat-1-mattermost`), a Storage Box, a Cloud Volume and the dedicated `filesync-hel1` (`README.md` "Inventory at a glance", with cost, location and a Verification column per row). The purpose is to keep several unrelated services alive and recoverable: static sites, a course chat, a research application, encrypted backups. Domain: server administration. State: live; the newest runbook is dated 2026-09-22 (`ai-econ-lab-mattermost.md`). It is not a git repository. 14 Markdown files, 2,808 lines, of which `filesync-hel1.md` is 899.

## How the work was actually done

- **Runbooks, one per machine**: `elliott-main-1.md`, `recode-1.md`, `filesync-hel1.md`, `lexchat-mattermost-setup.md`, `zrh-ai-econ-mattermost-setup.md`, `ai-econ-lab-mattermost.md`, plus topic files (`recode-backups.md`, `zrh-ai-econ-email-sending-instructions.md`, `zrh-ai-econ-course-site.md`) and a handoff (`benjamin-recode-1-handoff.md`). `README.md` is the index and inventory; `WARP.md` is the agent entry point with operational rules.
- **AI tools**: inferred from prose written to an agent; no tool is named. `zrh-ai-econ-email-sending-instructions.md` splits a task into the step only Elliott can do in the AWS console and the steps an agent then did.
- **Scripted vs manual**: two programs in `scripts/` (`create_zrh_mattermost_accounts.py` 164 lines, `send_zrh_student_onboarding.py` 262 lines) built for one onboarding of 23 students, with state in `.zrh-onboarding-state.json` (a dict keyed by SHA-256 of the recipient, holding `announcement_sent`, `reset_requested`, `ses_message_id`). `ops/` holds two dated one-offs for `filesync-hel1` (`filly-disk-20260913/`, `filly-storage-policy-20260913/`) with the systemd units, scripts, a README and `test-results.txt` (9 unittest cases OK). `configs/` mirrors five server config files. Everything else is a paste block: "Routine health check", "nginx -t && systemctl reload nginx".
- **How server state is tracked versus doc state**: the server is the truth and the doc is a dated snapshot. Each runbook opens with `Last verified:` (recode-1 2026-08-10, lexchat 2026-08-10, elliott-main-1 2026-08-12, zrh-mattermost 2026-08-31, filesync 2026-09-17), `README.md` has a "Verification snapshot — 2026-08-10", and facts are re-verified by running SSH commands and writing the result into prose. `filesync-hel1.md` "Change discipline" makes this a rule: capture health, state the change and rollback, change one layer, verify, "update this document with the date and result". Nothing re-verifies automatically. Drift is visible: `elliott-main-1.md` says "Last verified 2026-08-12" yet contains a section dated 2026-09-18 and a vhost added 2026-09-17; `README.md` describes two Mattermost stacks on lexchat, `WARP.md` three.
- **Sync conflicts (verified by diff)**: `WARP.sync-conflict-20260916-154839-JGJ6GBC.md` and `zrh-ai-econ-mattermost-setup.sync-conflict-20260916-154838-JGJ6GBC.md` are the *older* side (mtimes 2026-09-13 and 2026-08-31 against 2026-09-17 for the kept files). The WARP conflict still points at `~/admin/web-site/` (singular) and lists two Mattermost stacks; the kept file has `web-sites/` and three stacks with the lab instance on port 8067. The zrh conflict predates the `summer-chat.` rename. The conflict stamp, 2026-09-16 15:48, is the day `filesync-hel1.md` says the Dropbox bridge for `_Ash_Admin` went live; I infer the bridge's first pass collided with an edit on another device. The newer text won and nothing was lost, but the copies were never deleted, so any grep reads both.

## Cases and servers

- *Health check a host*: a paste block per runbook (`systemctl --failed`, `nginx -t`, `df -h`); served by the person or an agent each time, never a program.
- *Add a site to the shared host*: prose in `elliott-main-1.md` plus `~/admin/web-sites/scripts/setup_site_dns.py`; the listener rule became a guard on the server (`/usr/local/sbin/check-shared-nginx-listeners`) after the 2026-08-12 incident.
- *Restore a backup*: `filesync-hel1.md` "Recovery procedures" (ZFS snapshot, Borg, total loss, disk failure) as prose; each was exercised once ("Validated tests": seven-file Borg extraction with SHA-256 check). `recode-backups.md` explains the `age` keypair and bucket rules.
- *Onboard a cohort to Mattermost*: a real server built for one run (two scripts, a state file, an SES instructions doc).
- *Hand a server to a collaborator*: `benjamin-recode-1-handoff.md` with fingerprints and a revocation procedure.
- **Redone**: three runbooks describe the one lexchat VM and each restates the Caddy routing; `zrh-ai-econ-course-site.md` carries two H1 documents, current and "historical", in one file.

## Verification

- **Probe**: `curl -fsSI https://recode.ink/`, `/v1/health` on the rendezvous, `systemctl status`; API readback ("Verified by authenticated REST readback", `ai-econ-lab-mattermost.md`); Hetzner API via `hcloud` where a token covers the project.
- **Restore drills**: done once per layer and listed in `filesync-hel1.md`; a one-time metric ("141,308 files / 94.8 GiB manifests match", `ops/filly-disk-20260913/README.md`).
- **Script**: only `ops/filly-storage-policy-20260913/test_storage.py`.
- **Explicitly unverified, recorded as such**: lexchat "API-side backups, billing, and protection flags are unverified"; Mattermost "uploads and off-site/disk-loss recovery are not covered"; `trace-law-restart.service` failed and left; the 52.5 GB `private-wondernauts` sync "not yet in the Borg allowlist".
- **Cost visible**: the shared-host listener incident; the ETH VPN stealing the Storage Box route until a route guard was added (`filesync-hel1.md`).

## Strengths

- Every runbook has a `Last verified` date, a "Pending work" or "Known gaps" list, and a rollback statement; the writing is honest about what was not checked.
- Secret values are absent from prose; secret files are mode 0600 (`.env`, `.tracelaw-storagebox.env`, `recode-backup-age-identity`); the trade-off of keeping a private key in Dropbox is argued in `recode-backups.md` rather than hidden.
- Costs, IDs, ports and fingerprints are recorded, so a rebuild is possible from the folder.
- One-offs that had to be safe were built with tests and receipts (`ops/`).

## Weaknesses and limitations of the ad-hoc workflow

- **No version control**: the only diff history is the two Syncthing-style conflict copies. Nobody can see what a doc said before a verification pass overwrote it.
- **Doc state drifts from server state** by construction: `Last verified` headers stop being true the day a section is appended (`elliott-main-1.md`), and the inventory in `README.md` disagrees with `WARP.md` on lexchat.
- **Stale conflict copies** left beside the originals; a search reads the old `web-site/` path.
- **Cross-tree coupling**: `scripts/send_zrh_student_onboarding.py` reads `~/admin/web-sites/.env`, and runbooks link into `~/overleafs_repos/reproduction-checker/docs/` and `~/tracelaw/docs/`.
- **Secrets and state beside prose**: `.env`, the age identity, `.zrh-onboarding-state.json` (hashed student IDs) in the same synced folder as the runbooks; `ops/filly-disk-20260913/__pycache__/` and `scripts/__pycache__/` committed to Dropbox.
- **Health checks are paste blocks**, so "is it still up" is answered only when someone thinks to ask.

## What Relay would have to support here

- **A host object** per server holding the facts the runbooks repeat (IP, aliases, services, ports, backup window, token scope) with a `last verified` that a probe can refresh, separate from the prose.
- **Runbook as skill with embedded probes**: the paste blocks in "Routine health check" should be runnable steps whose output is stored as evidence on the card, over `run_command` with the host explicit.
- **Server-state ledger versus doc**: a doctor check that flags a `Last verified` older than the newest section, and flags `sync-conflict` files.
- **Secrets by reference** with file-mode checks, and a store outside the synced tree for the age identity and the onboarding state.
- **One-off cards that produce a folder like `ops/<date>/`**: scripts, units, tests, receipts, linked from the runbook rather than pasted into it.
- **Handoff and revocation as objects**: who has which key on which host, with fingerprints, so `benjamin-recode-1-handoff.md` is data rather than a document.
