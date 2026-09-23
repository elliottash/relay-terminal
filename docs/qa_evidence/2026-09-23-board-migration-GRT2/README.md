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
