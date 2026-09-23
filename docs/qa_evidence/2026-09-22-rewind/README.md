# Rewind output — implementer evidence

The real application was driven under Xvfb with temporary XDG directories and a fake worker (`drive.py`). Two turns were printed, the second was selected in the Rewind picker, and the resulting notice was clicked with the mouse. No provider calls or user settings were used.

- `rewind.png`: earlier answer remains; discarded turn is replaced by a gray italic “5 lines rewound -- click to view” notice with blank lines around it.
- `viewer.png`: clicking that notice opens the saved branch in the adjacent file pane. The removed answer appears there and no longer appears in the terminal.
- `tests.log`: targeted pane and serializer regression results. Pane checks cover the exact removed text, count, italic SGR, mouse hit target and open callback, repeated rewinds retaining prior links, a 5,101-line saved branch, code-only rewind, missing anchors, and write failure.

Build: `scripts/relay-build --target relay relay-consolemode-tests relay-engine-tests -j2` (targets built across incremental runs).

Reproduce the live check: `python3 docs/qa_evidence/2026-09-22-rewind/drive.py`.

Limits: tested with libvterm on Linux. A missing turn anchor or unavailable sidecar leaves terminal text intact. Across application restart, saved scrollback still follows Relay’s existing SGR-only restore behavior: hyperlink labels are not rehydrated, but the saved branch remains on disk. The repository-wide board check reports pre-existing findings; none name RWND.
