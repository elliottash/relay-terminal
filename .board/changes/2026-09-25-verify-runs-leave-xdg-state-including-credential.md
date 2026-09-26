---
id: NA0H
type: work
status: inbox
labels: [bug, qa, evidence, security]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: pane p47, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Verify runs leave XDG state, including .credentials.json and relay.conf copies, inside docs/qa_evidence/

## Issue
Measured while landing the working tree at 21:55 on 2026-09-25: 1479 of the 1676 uncommitted paths under `docs/qa_evidence/` are scratch XDG state a verify run dumped inside the evidence directory, and they include credential and config copies — `.credentials.json` and `relay.conf` — in directories the repo's evidence convention would commit. `/xdg/` and `/state-copy/` have zero precedent in the tracked tree (`git ls-files docs/qa_evidence | grep -c '/xdg/'` → 0, `... | grep -c 'state-copy'` → 0). They were left out of the landing rather than committed; the drive scripts should write them outside the repo or `.gitignore` should cover them, and a redaction rule should exist for the credential files either way.
