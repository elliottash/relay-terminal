# A moved card lands in one commit — implementer evidence (#DJX7, 2026-09-19)

## Status of the code

Already fixed by commit `de3510f` ("land.py: a moved file lands in one commit (the
name gate ignores rename detection)", 2026-09-19 13:47 -0400): all three gate diffs
in `scripts/land.py` (`cmd_commit` ×2, the repair path) pass `--no-renames`, and
`tests/test_land.py` gained a regression test that fails on the previous version.
The card was left in Inbox when the fix landed; this note records the verification
and moves it on. See `verify.log` for the grep of the three call sites and the
commit.

## Verification (2026-09-19)

- The three `--no-renames` call sites are at HEAD (lines 1420, 1660, 1727).
- `tests/test_land.py` ran in this session's full `./scripts/test.sh` pass with no
  failure (the suite's 3 errors are all in `test_failover.py`, unrelated).
- End-to-end: this session landed #5G43's card move — delete of the old path plus
  add of the new one, in a single `land.py commit` — through the fixed gate. That
  commit is the live reproduction of the exact scenario the card measured.
