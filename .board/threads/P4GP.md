<!-- relay:entry 20260925T120000Z-p4 author=claude-code kind=note -->
### Claude Code · 2026-09-25 12:00
Fresh review (planning pass, no code changed): everything this card asks for has shipped, most of it under #R6BS (needs-verification) and #W9ST (needs-verification). Recommendation: **move to `done`** and point here to #R6BS / #W9ST (or `dropped` as superseded by #R6BS if the owner prefers its verification to be the record). No new plan written.

Evidence against each acceptance item:
- **Public web page with screenshots and install instructions** — `site/index.html` has an `#install` section (lines ~374–410) with direct links to the v0.1.0-beta.4 Windows installer, both macOS DMGs, the six Ubuntu 24.04/26.04 and Debian 13 `.deb`s and `SHA256SUMS`; screenshots in `site/assets/`. Deployment of commit 06418a97 to apex and `www`, byte-matched, is recorded in `docs/qa_evidence/2026-09-21-refresh-public-builds/README.md` and `beta4-publication.json` there. Website refresh: #W9ST (e24a7d06, 347fcf71).
- **Downloadable beta for ≥1 Linux channel, installed and launched on a clean machine** — tags `v0.1.0-beta.1`…`beta.4`; `gh release list` shows all four as pre-releases, beta.4 (2026-09-22) carrying 11 assets. `.github/workflows/release.yml` builds each `.deb` and install/smoke-tests it in a fresh container (`packaging/deb/docker-build-all.sh`, `packaging/smoke-installed.sh`); run 35677749794 passed all six Linux gates (R6BS evidence README).
- **Windows and macOS platform plan recorded** — exceeded: native Windows (ConPTY, `packaging/windows`, `.github/workflows/windows.yml`) and native Apple Silicon + Intel DMGs (`packaging/macos`, `macos.yml`) ship in beta.4; documented in `docs/BUILDING.md`. The old dependency on #portable-terminal-engine (`.board/features/2026-09-17-portable-terminal-engine.md`, still `planned`) no longer blocks this card.

Not re-checked here: the live site itself (no network used beyond read-only `gh`). Screenshot refreshes as the UI changes are ongoing marketing work (#W9ST, #K13B), not this card's scope. Owner decisions still open elsewhere, not here: macOS notarization / Windows code signing (unsigned today, per the install copy), and the disabled AUR job.

<!-- relay:entry 20260925T120739Z-j9 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Dropped · changed in the Board
