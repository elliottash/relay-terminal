# The AI's pass over the staged #ESDF situation

Played 2026-09-23, `RELAY_BIN` = a clean-export build of `main` at `914adee5` (the shared tree
was mid-edit by another session and did not compile; tip did). Named controls only
(`scripts/relay-drive`), one screenshot per step, all under `captures/`.

| Step | Evidence | What it shows |
| --- | --- | --- |
| Open the Board | `01-board-opens-flat-recent.png` | The pane opens flat: one list, newest first, a stage on every row. The top card is the iPad export bug updated minutes before staging. |
| Click STAGE | `02-stage-click-sections.png`, `02-sections-notice.json` | Sections return; the notice reads "Grouped by stage. Click STAGE for one list of every card." |
| Click STAGE again | `03-stage-click-flat-again.png`, `03-flat-notice.json` | Back to one list; notice reads "One list, recently updated. Click STAGE to group by stage again." |
| Click UPDATED twice | `04-flat-sorted-oldest-updated.png` | The sort cells order the whole flat list, oldest-updated first. |
| Restart the app | `05-after-restart.png` | The board opens on the same flat view after a restart. |

Nothing here answers the judgement; that is the person's one step. The mechanical steps above are
replayed by `ai-pass.sh` and are not asked of them.
