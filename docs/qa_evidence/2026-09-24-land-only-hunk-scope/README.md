# QA evidence — land.py --only-hunk review scope (#DT6Z)

Commit: `622e7506`.

## Re-diagnosis

The filing claimed the verify tree ignored `--only-hunk`. Re-reading
`cmd_commit` → `apply_hunks` → `plan_path` → `build_tree` shows the tree is
built from the *selected* hunks and the digest covers the exact bytes of every
path in `plans`. In the incident, `--only-hunk tests/test_conv_index.py:1` said
nothing about `src/Pane.h`, so that path stayed wholly selected, the plans were
identical, and the digest legitimately matched — the gate then correctly
refused a commit that would have landed another session's half-hunk. The
defect was the review's wording, which let `--only-hunk` read as narrowing the
whole commit. The correction is recorded in the card's thread; the original
filing text is preserved there.

## Tests

```
$ RELAY_LAND_SCRIPT=/tmp/land-next.py python3 -m unittest tests.test_land
Ran 64 tests ... OK        # 2 new

$ python3 -m unittest tests.test_land      # after the move into scripts/
Ran 64 tests ... OK
```

New cases:

- `test_a_selection_names_one_paths_hunks_not_the_whole_commit` — with two
  contested paths and `--only-hunk f.txt:1`, the held review prints
  `still wholly in this commit — no selection named them: big.txt`, prints the
  new hint (and not "the same command"), lands nothing until confirmed, and
  confirming the printed digest lands f.txt's hunk **and** big.txt whole —
  the tree the digest pinned.
- `test_no_selection_line_when_no_selection_was_given` — the line appears only
  when a selection is active.

## Probe

The behaviour the card was filed against, reproduced under the fixed script:

```
$ python3 scripts/land.py commit <me> -m x --only-hunk a.txt:1
...
  still wholly in this commit — no selection named them: big.txt

  land all of it:  rerun with --confirm <digest>
                    (the digest pins the tree printed above, not the flags you
                     reached it with)
```
