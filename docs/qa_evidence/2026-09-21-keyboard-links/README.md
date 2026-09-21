# #GWXM — keyboard link actions (implementer evidence)

Ctrl+Shift+L activates the existing link walk. Arrows select files, folders and URLs.
Enter opens a file in Relay's preview (preserving its line) or a folder in Explorer;
Ctrl+Enter opens a file for editing; Shift+Enter opens a local file/folder externally.
URLs retain browser routing. Escape exits; Tab and Shift+Tab retain their bindings.

The target and applicable keys now appear in a persistent indicator that updates immediately,
instead of queued toasts that could describe an earlier selection. The mouse shortcut hint uses
the configured activation key and teaches arrow navigation.

The scan still covers the newest 2,000 history rows and the screen, capped at 500 links. It now
retains the newest 500, and computes history coordinates before adding screen rows (previously
selections in sufficiently long history were shifted upward by the screen height).

## Checks

- `scripts/relay-build --target relay relay-engine-tests relay-outputlinks-tests` (targets built
  in separate invocations). The clean candidate tree is also built by `scripts/land.py`.
- `RELAY_ENGINE_TEST=ViewTest QT_QPA_PLATFORM=offscreen build/engine/relay-engine-tests keyboardLinkWalk keyboardLinkWalkNewestHistory`:
  4 passed including setup/cleanup, on libvterm. Exact selected text in history; newest target
  and wrap to the oldest retained target after 2,100 URL lines. See `engine-tests.txt`.
- `ctest --test-dir build -R '^outputlinks$' --output-on-failure`: passed. Also recorded through
  `TestsCommands.start_run(['ctest:outputlinks'])`; `check_card('GWXM')` has no findings or blocks
  (`tests-check.json`).
- `python3 docs/qa_evidence/2026-09-21-keyboard-links/drive.py`: isolated Xvfb, separate XDG config,
  data/cache/runtime directories, temporary sample file and folder. Uses an `xdg-open`/browser
  recorder rather than opening real desktop apps. See `external-opens.txt` for URL, file and
  folder dispatch; screenshots show file preview at line 2, editing (Save button), folder
  Explorer, and Shift+Tab reaching Plan after Escape. Driver checks external targets and OCR
  markers for edit/Explorer. No file content was changed by the GUI drive.

Existing card-format warnings concern the card's historical 2026-09-17 section names; these
predate this change. This is implementer evidence, not an independent QA verdict. Existing
Unicode/grid-wrap limitations of link collection and explicit OSC 8 label discovery were not
expanded by this change. Concurrent prose-resize corrections belong to #J4WK.
