# #MDSG — the sessions list and the file panes, before and after

Machine: spark (aarch64, 20 cores), Qt 5.15.13, KSyntaxHighlighting 5.115, RelWithDebInfo.
"Before" is a clean export of `main` at `53d7e11b` built in `/tmp/claude-1000/pf4k/fix/panes/before`;
"after" is this checkout. Both were given the **same** bench code, so the only difference measured is
the fix. Nothing here reads the owner's conversation store or his real configuration: the sessions
bench feeds `SessionManager::setResults()` the worker's own JSON shape, made up in the test, and the
file bench writes its own C++ files (plus, where named, this repository's `src/Pane.h`).

The benches live with the tests and are skipped unless asked for, so they cost the suite nothing:

```
ctest --test-dir build -R "^(conversations|filepanes)$"        # the tests below

QT_QPA_PLATFORM=offscreen RELAY_PERF_BENCH=1 ./build/relay-conversations-tests searchKeystrokeCost
QT_QPA_PLATFORM=offscreen RELAY_PERF_BENCH=1 \
    RELAY_PERF_FILE=$PWD/src/Pane.h ./build/relay-filepanes-tests openCost
QT_QPA_PLATFORM=offscreen RELAY_SHOT_DIR=<dir> ./build/relay-conversations-tests listScreenshot
QT_QPA_PLATFORM=offscreen RELAY_SHOT_DIR=<dir> ./build/relay-filepanes-tests bigFileScreenshots
```

Under a real X server they were run the same way with `DISPLAY=:71 QT_QPA_PLATFORM=xcb` and every
XDG directory plus `TMPDIR` under `/tmp/pfp`, `RELAY_KEYRING=off`.

## 1. Sessions: what one keystroke costs

A hundred-row result page arrives and the list is rebuilt and repainted, ten times over. GUI CPU per
key (the figure the profile reports); "unfolded" is the same page with every session row open, which
is the shape the profiler measured on the owner's store.

| harness | rows | before | after | |
|---|---|---|---|---|
| Xvfb, xcb | collapsed | 37.2 ms | **4.4 ms** | 8.5× |
| Xvfb, xcb | unfolded | 392.6 ms | **61.5 ms** | 6.4× |
| offscreen | collapsed | 36.0 ms | **4.1 ms** | 8.8× |
| offscreen | unfolded | 126.8 ms | **12.2 ms** | 10.4× |

Two causes, both in `src/Conversations.cpp`:

- **`setFirstColumnSpanned()` per row.** On a row that is already in the tree it makes the view lay
  itself out there and then, and Qt re-measures the three `ResizeToContents` columns over every row
  while it is at it. A hundred-row page called it about a hundred times. The spans are now collected
  while the list is filled and set in one go at the end. On its own: 36.0 → 4.3 ms collapsed.
- **Restoring the unfolded rows.** Each `setExpanded()` makes Qt walk the rows for those same three
  columns. The columns are now held at their width while the list is filled and measured once, over
  the finished tree. On its own: 73.5 → 12.5 ms unfolded (offscreen).
- **The delegate's `QTextDocument` per rich-text row**, laid out once for `sizeHint` and again to
  paint (the finding's own diagnosis). With the two above fixed it is still half of what is left:
  23.8 → 12.2 ms unfolded, offscreen, measured by short-circuiting the cache in a throwaway build.

The pixels are unchanged. `listScreenshot` renders the same page from both builds; the two PNGs are
byte-identical under both platforms (`PIL.ImageChops.difference(...).getbbox()` is `None`, 505×492
under xcb and 512×500 offscreen). `sessions-list-after.png` is the xcb one.

Not done, and why: **skipping `rebuildTree` when the result set has not changed.** It now costs
4 ms, the search is already debounced at 120 ms (`m_debounce`, `src/Conversations.cpp`), and the
worker's reply differs on nearly every key anyway because the match lines do — a deep comparison of
the reply would cost more than it saves and would be one more thing to keep honest.

## 2. File panes: what opening a big file costs

GUI CPU to the first paint (the freeze the reader feels), and GUI CPU for the whole thing including
the colouring, which now happens afterwards. "cold" is the first open of that path in the process.

| file | before, first paint | after, first paint | before, in all | after, in all |
|---|---|---|---|---|
| 300 lines, 7.7 kB | 18 ms cold / 4 ms warm | 19 ms / 5 ms | 19 / 4 ms | 20 / 6 ms |
| 15,830 lines, 419 kB | 120 ms / 125 ms | **33 ms / 41 ms** | 120 / 125 ms | 250 / 249 ms |
| `src/Pane.h`, 15,959 lines, 1.0 MB | 203 ms / 205 ms | **51 ms / 50 ms** | 203 / 205 ms | 452 / 431 ms |

(Xvfb, xcb. The offscreen figures are within a few ms of these.) The freeze is 4× shorter on a 1 MB
file and a 300-line file is unchanged — its 400-block eager slice covers the whole file before the
pane is painted, so nothing small flickers.

The total GUI time for a 1 MB file roughly doubles, 203 → 440 ms, and that is the deliberate trade:
the work is the same colouring done 4 ms at a time with the event loop in between, and each slice
costs the view one relayout of its blocks. Two things already hold it down — the slice is wrapped in
a single `QTextCursor` edit block (without it the 1 MB file cost 556 ms rather than 440, because the
view was told the document had changed once per block) and the frontier never goes over the same
block twice. A longer slice buys little and is felt: 4 ms → 482 ms, 8 ms → 443, 16 ms → 418,
32 ms → 405, all on the 1 MB file, so 4 ms it is.

KSyntaxHighlighting does **not** set the block user state (probed against 5.115: after
`SyntaxHighlighter::highlightBlock`, `currentBlockState()` is still -1), so Qt's "carry on while the
state changes" never fires and one `rehighlightBlock` colours exactly one block. That is what makes
the frontier safe — a block past it is left with no format and no state, so nothing runs past it —
and it is why the slice loop calls `rehighlightBlock` per block rather than once per slice.

**The diff viewer is not affected.** `src/DiffView.cpp` installs no highlighter of any kind (it
colours its own lines), so nothing in this change reaches it; `ctest -R diffview` passes unchanged.

`file-pane-at-open.png` and `file-pane-coloured.png` are an 8,000-line C++ file the moment it is
open and once the frontier has reached the bottom. They differ only below the fold: the first
screenful is coloured before the pane is painted.

## Tests

`ctest --test-dir build -R "^(editor|filepanes|fileindex|conversations|remotefiles|remotesession|diffview)$"`
— 7/7 pass. New:

- `conversations: richTextCacheKeys` — the same html at the same width in the same font is laid out
  once; a different width or font is a different entry; the cache is bounded and drops the least
  recently used; `clear()` empties it.
- `conversations: groupRowsSpanTheWidth` — the group rows and the quick-look placeholders are still
  spanned after a rebuild although the spans are now set at the end of it, and the three narrow
  columns are back to `ResizeToContents` when it finishes.
- `filepanes: aBigFileIsShownFirstAndColouredAfterwards` — an 8,000-line file is complete in the
  viewer immediately with only part of it coloured, and finishes colouring by itself; jumping to
  line 7,994 first does not stop the last line from ending up coloured.
- `filepanes: openingAnotherFileStopsTheColouringOfTheFirst` — opening a second file leaves nothing
  of the first one's running, before or after the next slice would have fired.
- `filepanes: aRemoteFileTooBigToColourIsShownPlain` — a host's file over `kMaxHighlightBytes` is
  shown with no highlighter at all.
