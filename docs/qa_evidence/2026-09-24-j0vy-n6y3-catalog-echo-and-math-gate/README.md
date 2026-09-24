# #J0VY + #N6Y3 — app-catalog echo loop, inline-math false positive

Date: 2026-09-24 · Session: pane 0806c7fd · Commits: `fc6c5cce` (#J0VY), `15f65489` (#N6Y3)

## The fault as measured (before the fix)

Live app, pid 3747251, up 14 h, ~37 panes:

```
$ grep -c app_catalog_updated ~/.local/share/relay/logs/relay.log
50185                      # over 14:56Z–18:27Z; one broadcast every ~8 s, echoed per pane
# per-minute histogram peak: 690 lines/min at 18:18Z, 672 at 18:19Z  (= 14:18–14:19 EDT)
# GUI main thread (tid == pid), app otherwise idle: 30.0% CPU over 4 s, ~15% over 3 s
# index.db-wal: 53 MB, written every second
```

The false positive that made the user look at math at all:

```
$ cat ~/.cache/relay/media/db61b044*.json
{"kind": "math", "latex": "1,500–2,300. A minimal version costs about ", "version": 1}
```

## #J0VY fix — content-gate every `app_catalog` send

- `AppCommands::catalogChanged(QByteArray &lastSent, const QJsonObject &app)` — serialize
  compact, compare with the blob that worker last received; unchanged content sends nothing.
- `Pane::sendAppCatalog()` gates on `m_lastAppCatalog`, cleared in the worker `started`
  handler (`PaneRuntime.cpp`) so a fresh worker always gets its first catalog.
- `RelayWindow::sendHelperCatalogs()` gates per helper worker via a `relayLastAppCatalog`
  property, which dies with the worker.
- No backend changes.

Test: `relay-appcommands-tests` slot `catalogChangedGatesIdenticalResends` — first send
passes, identical suppressed, changed passes, cleared (worker restart) passes.

```
$ ./build/relay-appcommands-tests catalogChangedGatesIdenticalResends
Totals: 3 passed, 0 failed   (full suite: 60 passed, 0 failed)
$ ctest --test-dir build -R 'markdown|appcommands'
100% tests passed, 0 tests failed out of 2
```

Note: the running binary predates the fix; the flood continues until Relay restarts.

## #N6Y3 fix — single-`$` runs must look like math (Pandoc's rules)

`MarkdownAnsi::delimitsInlineMath()`: opening `$` not followed by whitespace, closing `$`
not preceded by whitespace and not followed by a digit. Display `$$…$$` exempt. The
rejected `$` is emitted literally and scanning continues.

Test: `relay-markdown-tests` slot `inlineMathMustLookLikeMath` — the wild false positive
stays prose (whole and streamed, both `$` survive as text), `$5 and $10` stays prose, the
quadratic formula still escapes whole and streamed, padded `$$ x = 1 $$` still escapes.

```
$ ./build/relay-markdown-tests inlineMathMustLookLikeMath
Totals: 3 passed, 0 failed
```

## Verify build

`land.py` built the exact landed tree (`/tmp/claude-1000/land/relayfixes/verify`) before
the compare-and-swap: "verify: the exact tree builds". `scripts/relay-build --target relay`
clean. Hunks belonging to the concurrent #SMDX session (3 in `src/AppCommands.cpp`, 2 in
`src/Pane.h`) were left out and remain uncommitted in the working tree.
