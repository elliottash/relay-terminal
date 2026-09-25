# #9FX8 steps 3–4: Globals › Skills, project memories out of Globals, Re-verify

Screenshots are offscreen grabs (`QT_QPA_PLATFORM=offscreen`, isolated `XDG_CONFIG_HOME`) taken by
the widget tests themselves when `RELAY_SHOT_DIR` is set, on the exact tree `land.py try` built
(tip plus this session's hunks only). The rows are the tests' fixture payloads, not a live
worker's, so every state the page draws (fresh, stale with its reason, no cases yet, a linked
card) appears on one screen.

| File | What it shows |
| --- | --- |
| `board-skills-tab.png` | Board › Skills: project skills only (the global fixture row is filtered out), source · version, cases, pass, last verified, Stale; the fresh skill's page with the profile strip, provenance, stats, cases, one Linked chip, and Load / Re-verify / Exclude / Refine / Open file |
| `board-skill-page-stale.png` | the stale skill's page: "stale — last passed 2026-08-01" beside the Stale column |
| `board-memories-tab.png` | Board › Memories: Expired first, Active, Retired folded |
| `globals-skills.png` | Globals › Skills: the global rows of a mixed payload (the project row is absent), "no cases yet" for the skill with no rows, the stale reason on the page, Import from repository… and Check updates in the toolbar |
| `globals-all-global-records.png` | Globals › All global records: user memories and an alias; the project-scoped memory in the payload is not listed and the intro says so |

`tests.txt` has the commands and results.
