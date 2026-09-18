# Package F — every surface except the terminal pane (card #TK9C)

The subagent transcript (`src/SubagentTranscript.*`) and the turn pane (`src/TurnTranscript.*`),
fed the exact events of `docs/AGENT-SESSIONS-PROTOCOL.md` § 23 and photographed. The harness is
`surfaces-shot.cpp` in this directory; it builds the two widgets and hands them the events by hand,
so there is no worker, no provider key and no network in the picture — the surfaces are what is
under test. `libvterm-*.png` in this directory belong to package E (the engine's fold layer).

    g++ -std=c++17 -fPIC $(pkg-config --cflags Qt5Widgets) surfaces-shot.cpp \
        ../../../src/TurnTranscript.cpp -I../../../src \
        ../../../build/librelay-subagents.a ../../../build/librelay-toollabel.a \
        ../../../build/librelay-highlight.a $(pkg-config --libs Qt5Widgets) -o /tmp/surfaces-shot
    HOME=$iso/home XDG_CONFIG_HOME=$iso/config XDG_RUNTIME_DIR=$iso/run TMPDIR=$iso/tmp \
        xvfb-run -a /tmp/surfaces-shot docs/qa_evidence/2026-09-18-concise-tool-call-lines

**Read the colours, not the background.** The harness shows the widgets on Qt's default palette,
because the dark background is the *application* stylesheet's (`QWidget#subagentTranscript`,
`#turnTools`) and no application is running here. The inks in the widgets themselves — grey tool
lines, red failures, green additions — are the ones the code sets, and those are what these
pictures are evidence of.

| Picture | What it shows |
|---|---|
| `subagent-01-a-call-while-it-runs.png` | `running pytest`: the present tense, on its own row, while the call is still going |
| `subagent-02-merged-reads-a-failure-and-an-inline-diff.png` | the same row rewritten to `✗ ran pytest · 12 lines · exit 1 · 2.1 s`; three consecutive reads folded into `▸ read 3 files · 503 lines`; `▸ edited router.py · +2 −1` with its short diff printed underneath, added green and removed red, with no click |
| `subagent-03-the-detail-folded-open-in-place.png` | the failed pytest row clicked: the command and its output folded open under the line, and folded shut again by clicking it once more |
| `turn-01-rows-are-the-labels-lines.png` | the turn pane: every row is the label's line, the run of reads is one parent row whose three members are still there and still openable, and the exact duration of each call is the second column |
| `turn-02-the-detail-sections-in-the-log.png` | `tool_output_get`'s `detail` rendered section by section — `command` as code, `output` as output, and the `(truncated)` note the section asked for |

What is not pictured here: the phone. `tests/test_remote_browser.py` drives the real web client in
a real headless Chrome and asserts the same three lines (`read 2 files · 412 lines`, `✗ ran pytest
· 12 lines · exit 1 · 2.1 s`, `edited router.py · +2 −1`), the red and green diff rows, and that
every `<details>` starts shut with the script behind it.
