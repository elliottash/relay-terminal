# web-sites — dossier

## What it is and what we were trying to do

`~/admin/web-sites/` is the admin root for domains, DNS and hosting, with one folder per website (`WARP.md` "Layout"). `~/admin` resolves to `~/Dropbox/_Ash_Admin` (verified with `readlink`), so the whole tree is Dropbox-synced. The job it exists for: move about 28 domains off GoDaddy to Porkbun for registration, Cloudflare for DNS and mail routing, and one shared Hetzner host, `elliott-main-1`, behind a Cloudflare Tunnel (`TRANSFER-WORKFLOW.md`, `elliottash/migration-plan.md` dated 2026-05-12), then keep buying and hosting sites. Domain of knowledge work: personal IT administration with money attached. State: live and in use (`relay-terminal/WARP.md` records a domain bought 2026-09-17; `ai-econ-lab/WARP.md` records a cutover on 2026-09-22).

Size (verified): 1.9 GB. `elliottash/` is 1.3 GB, of which `_backups_misc/` is 748 MB of WordPress-era zips and `site/node_modules` 183 MB; `ai-econ-lab/` is 499 MB. `WARP.md` lists 12 site folders; 10 of them hold only a `WARP.md` stub. Git: initialised 2026-09-22 (`.git` birth time), zero commits, 27 untracked entries and no `.gitignore`.

## How the work was actually done

- **Instruction files**: `WARP.md` (layout, which `.env` keys exist, shared-host rules, sites table, "Agent rules"), per-site `WARP.md` files carrying facts such as zone IDs and purchase dates, and `AGENTS.md`, which is only the Relay board pointer block generated 2026-09-23.
- **Workflows as prose**: `DOMAIN-PURCHASE-WORKFLOW.md` (quote, dry-run, purchase with a price ceiling, verify), `TRANSFER-WORKFLOW.md` (per-domain checklist, batch rounds, per-TLD rules).
- **Board**: `.board/` was created by `board_init` on 2026-09-22 (`.board/survey-state.json`) and holds zero cards (`.board/threads/.gitkeep` only, no `BOARD.md`). A `board -> .board` symlink was added 2026-09-23. All the work here predates the board.
- **AI tools**: inferred, not verified. The scripts have step-numbered docstrings and `WARP.md` is written to an agent ("Do not print secret values", "Agent rules"). Which tool is not recorded.
- **Scripted vs manual**: eight programs in `scripts/` (`register_porkbun_domain.py` 197 lines, `transfer_all.py` 690, `phase0_domain_inventory.py` 751, `setup_site_dns.py`, `transfer_to_porkbun.py`, `refresh_auth_codes.py`, `test_email_forward.py`, two shell scripts for email routing). The manual residue is named explicitly in the `transfer_all.py` docstring: GoDaddy outbound approval in the dashboard, `.uk` IPS tags, Porkbun purchases over $100 on the website.
- **State between sessions**: machine-readable in `phase0/transfer_state.json` (27 domains, per-domain timestamps, Cloudflare zone IDs, cost in cents), `phase0/domain_inventory.{csv,json}`, `phase0/raw/<domain>.json`, and `ai-econ-lab/dns-verification-status.json`; prose in each site's `WARP.md`.

## Cases and servers

- *Buy a domain*: a program plus a skill. `scripts/register_porkbun_domain.py` quotes, dry-runs, re-quotes, enforces `--max-price` and sends an idempotency key; `DOMAIN-PURCHASE-WORKFLOW.md` tells the agent when to stop and hand to the person. This is the clearest built server in the tree.
- *Transfer a domain*: `transfer_all.py` classifies each domain into one of seven types and automates what the APIs allow; `TRANSFER-WORKFLOW.md` is the prose form. Three transfers are recorded done (`TRANSFER-WORKFLOW.md`; `ashtwins.com` in `transfer_state.json`).
- *Point a domain at the shared host*: `setup_site_dns.py`, idempotent (zone, CNAMEs, HTTPS, tunnel ingress, nameservers).
- *Deploy a site*: `elliottash/site/deploy.sh` (tests, content audit, build, rsync `--checksum`, six live probes, origin-side hash). Every other site has its own `deploy.sh` in its own repo (`relay-terminal/WARP.md`, `ai-econ-lab/WARP.md`, `~/admin/hetzner/elliott-main-1.md` for theorize and relit). The pattern was re-implemented per site, not built once.
- *Verify a migration*: `ai-econ-lab/verify_migration.py` (20 route checks, 19 download hashes) and `wait_for_dns.py`, written for one cutover.
- **Redone ad hoc**: `load_env` is reimplemented in five of the scripts (`register_porkbun_domain.py`, `phase0_domain_inventory.py`, `refresh_auth_codes.py`, `transfer_all.py`, `transfer_to_porkbun.py`) and a sixth time in `~/admin/hetzner/scripts/send_zrh_student_onboarding.py`. Email routing has three overlapping tools (`fix_email_routing.sh`, `check_email_routing.sh`, `test_email_forward.py`, the last importing from `/home/elliott/alfred`).

## Verification

- **Deployed site**: probe mode, and good. `deploy.sh` requires HTTP 200 on six pages, greps one page for content, and compares the headshot's SHA-256 at origin because Cloudflare's edge can lie. `verify_migration.py` does the same for ai-econ-lab and records "Passed 20 route checks and 19 download hashes" (`dns-verification-status.json`, 2026-09-22).
- **Domain purchase**: verified from API response fields; the person is told to record the order ID.
- **DNS propagation**: a user-level systemd timer retried read-only checks every two minutes for up to 48 hours (`ai-econ-lab/WARP.md`).
- **Nothing**: two pytest files in `elliottash/site/tests/`, no CI; the ten stub sites have no verification record here, and with zero commits there is no record of what changed when.
- **Cost when skipped, on record**: `deploy.sh` "this bit us once with `--size-only`"; `elliott-main-1.md` records the `[::]:80` listener that made every tunnelled hostname serve the Relit page on 2026-08-12.

## Strengths

- The purchase flow is a real card-built server with a refusal path (`--max-price` required, rate-limit handling, idempotent retry) and an explicit hand-off to the person.
- Deploy scripts test the live artifact, not the recipe, and compare hashes at origin.
- `transfer_state.json` is per-domain, timestamped, machine-readable state, which is exactly what the sessions needed between runs.
- Secret values are absent from every `.md` and `.py` I read; keys are named, values live in `.env`.

## Weaknesses and limitations of the ad-hoc workflow

- **Credentials in a synced tree**: `.env` (4,147 bytes, mode 0644) and `credentials.txt` (2020, mode 0644) sit in Dropbox; `phase0/transfer/` holds 16 EPP auth codes (mode 0600) plus an editor swap file `.freerid.es.auth.kate-swp`. Not opened; existence and location only.
- **Zero-commit git with no `.gitignore`**: a `git add .` would sweep in `.env`, `credentials.txt`, 748 MB of backups and `node_modules`.
- **Board created, never used**; and the `board` symlink originates inside a Dropbox tree, which `WARP.md` "Agent rules" forbids.
- **Stale prose**: `TRANSFER-WORKFLOW.md` still says "journo-gpt.ai expires in 2 days — RENEW IMMEDIATELY" (May 2026); `migration-plan.md` says Porkbun credentials are not yet in `.env` while `WARP.md` lists them; `.htaccess` is a WordPress file from November 2020; `CLOUDFLARE_ZONE_ID` names `trace.law`, and two files warn about it.
- **Six copies of `load_env`**, a per-repo `deploy.sh` per site.
- **Strays**: `tmp/` (four HTML pages about a scientific team, 2026-09-15), `uploads/` (PDFs and a CV), `_archive/wombo-art-paper-titles/` (77 AI images of paper titles). None is web administration.
- **Knowledge split three ways**: this tree, `~/admin/hetzner/elliott-main-1.md`, and each site's own repository.

## What Relay would have to support here

- **Secrets by reference**: a `.env` object whose keys can be named in a pane and whose values are never printed, with a home outside Dropbox for auth codes.
- **Remote/SSH as the default target**: every case ends on `deploy@138.201.189.28`; `run_command` with the host explicit, and a policy that runs `nginx -t` and the listener guard before any reload.
- **Probe verification as a first-class mode**: a URL list, expected codes, a content grep and an origin hash, re-runnable from the card.
- **A domain object with state** (registrar, nameservers, zone, expiry, transfer stage), with expiry alerts; `transfer_state.json` is the prototype.
- **One deploy server across sites** instead of a `deploy.sh` per repository.
- **One-off cards that leave a runbook and evidence**: the ai-econ-lab migration produced a verifier, a backup folder and a status JSON; Relay should attach those to a card, and the repository should have commits at all.
