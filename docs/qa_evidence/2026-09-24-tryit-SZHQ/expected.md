# Expected result — sealed until you have answered

- `relay-scratch check` prints ONE line naming what is over (scratch takes 200 MB against a 102 MB budget) and what to run, and exits 1.
- `relay-scratch gc` (dry run) lists exactly two removals — `task-huge` (200 MB) and `session-gone` (12 KB), both idle 30 h — and does not list `session-live`, which is shown with its reason for being kept.
- `relay-scratch gc --apply` reports freeing 200 MB in 2 entries.
- `ls` afterwards shows only `session-live`, still holding `pane.h`; the background process in it was never disturbed.
- The command the check line tells you to run is the one that actually works, and nothing it prints requires reading the source to understand.
