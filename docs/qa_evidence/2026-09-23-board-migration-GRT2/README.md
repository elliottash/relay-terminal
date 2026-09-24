# `.board/` migration evidence — #GRT2

The setup change landed as `9bd89b7c`: new projects create `.board/`, the backend and GUI
recognize it before older board names, and the Board pane offers to move existing `board/` and
`.switchboard/` boards there. Renaming refreshes `POLICY.md`, agent pointers, and `.gitattributes`.

## Focused verification

- `PYTHONPATH=backend python3 -m unittest tests.test_board tests.test_board_tools tests.test_board_protocol -q`: 574 of 575 passed on the first run; the remaining stale default-folder assertion was corrected and its case passed. Earlier `tests.test_board` passed all 129 cases after the policy/pointer update.
- `QT_QPA_PLATFORM=offscreen build/relay-projects-tests -silent`: 33 passed.
- `QT_QPA_PLATFORM=offscreen build/relay-boardsections-tests -silent`: 19 passed.
- Focused `relay-projectinit-tests` folder and creation cases: 6 passed. Two unrelated source-text assertions in the full target fail because concurrent panes changed their target source files.
- `scripts/relay-build --target relay`: completed. The `land.py` exact-tree verification also built `relay` before commit.

## Other local projects

Each source board was copied to a local backup before renaming. A temporary symlink from its former
folder to `.board/` keeps already-running Relay processes able to use it until they restart. The
original files were compared by SHA-256 after the move; only generated `POLICY.md` changed where it
already existed. The new board passed `Board.check()` and the generated `AGENTS.md` pointer names
`.board/POLICY.md` in every project.

| Project | Former folder | Original files | Cards | Board errors |
| --- | --- | ---: | ---: | ---: |
| web-sites | `board/` | 5 | 0 | 0 |
| Wily | `.switchboard/` | 3 | 0 | 0 |
| Sweet Street | `.switchboard/` | 4 | 0 | 0 |
| Tracelaw | `.switchboard/` | 7 | 1 | 0 |

The final migration of this repository's `issues/` board is pending coordination with live
sessions that hold and edit its more than 1,000 tracked files. Its location stays unchanged until
those sessions can be stopped or handed over without losing their writes.

## Final step: this repository (2026-09-24)

After a full Relay restart on the new build, the owner moved the board by hand while Relay was
closed: `mv issues .board` plus a temporary `issues -> .board` compatibility link. The link was
then lowered for the rename commit itself, because `land.py` reads `issues/…` through a symlink
and would have resurrected the old tree; it went back up immediately after the commit.

- Pre-move backup: `/home/elliott/data/relay-checkout-archive/2026-09-23-GRT2-pre-restart/`
  (git bundle of all refs, full uncommitted-work patch, every untracked file, a complete
  `issues/` copy, and Relay session state), with `manifest.json`.
- Post-move check: `.board/board.yaml`, `POLICY.md` and `BOARD.md` present; zero files missing
  against the backup (`diff -rq` shows only cards and threads other sessions touched between the
  backup and the move); `board_list` resolves 681 cards with `.board/` paths.
- The rename commit carries 1,158 `issues/…` deletions and 1,208 `.board/…` additions, the
  regenerated `POLICY.md` and pointer blocks (hidden-folder `rg --hidden` guidance included),
  `.gitattributes` moved to `.board/threads/*.md` and `.board/cases.jsonl`, `WARP.md`, and the
  intake-file paths in `scripts/land.py` and the two board briefs. `.board/.private/` and
  `.board/cases.jsonl` stay untracked, as they were under `issues/`.
- Live sessions after the restart resolve `.board/` first (new `BOARD_FOLDERS` order), so no
  writer used `issues/` while the link was down. Sessions holding pre-move `land.py` claims on
  `issues/…` paths will conflict-abort harmlessly and can re-begin against `.board/…`.
