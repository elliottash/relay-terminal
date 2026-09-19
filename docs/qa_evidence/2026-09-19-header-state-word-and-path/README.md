# #0STR — the pane header loses the state word, and the path loses its words (2026-09-19)

Owner's request: `clean up the headers of tabs and panes. they are busy`. What landed, for panes
only (the tab bar was deliberately left alone):

1. the live state's word beside the glyph — "Relaying…" / "Command running" / "Subagents working" —
   is gone, widget and give-way rung and all. The glyph stays, still blinks, and its tooltip still
   spells the state out; the sentence lives on the busy line above the prompt box.
2. the right-hand label is the terminal's path and nothing else. `TERMINAL  ~/project     │
   AGENT WORKSPACE  ~/x` became `~/project`, and the words and the workspace path moved into the
   header tooltip, in a sentence.
3. a pane whose title is already the folder's own name does not show the path at all.

## What was run

```
RELAY_QA_PREFIX=before      drive.sh <build of ffbbd3d>   # the BEFORE set
RELAY_QA_PREFIX=implementer drive.sh <build of this>      # the AFTER set
```

`drive.sh` is the #RR0G / #HQ2B harness: Xvfb on a free display, an isolated `HOME`,
`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` under a short
`/tmp/relay-0str.*` path (the 108-byte unix-socket limit), `RELAY_KEYRING=off`, per-pane isolation
off, and `local:stub` pointing at `stub-provider.py` on 127.0.0.1:8825. Each scene renames the pane
with `/rename` so the title is a known string rather than whatever a model would write.

## What to look at

`<prefix>-<scene>-hdr2x.png` is the pane's title row at 200 %; `<prefix>-<scene>.png` is the whole
window. Every "before" file has an "implementer" twin shot from the same script.

| Scene | Before | After |
|---|---|---|
| `same` | `project` … `TERMINAL  ~/project` | `project`, and **no path at all** — the title already says the folder |
| `diff` | `Header cleanup` … `TERMINAL  ~/project` | `Header cleanup` … `~/project` |
| `busy` | violet **`Relaying…`** between the glyph and the title | the glyph alone; the title starts where the word used to |
| `workspace` | `TERMINAL  ~/project/sub     │     AGENT WORKSPACE  ~/project` | `~/project/sub` |
| `tooltip` | `Terminal: …` / `Agent workspace: …`, two lines of labels | one sentence: "The terminal is in …, and the agent's workspace is …" |
| `narrow` | a 560 px pane, `TERMINAL  ~/project` | a 560 px pane, `~/project` |

The `tooltip` frames are captures of the whole screen, not of Relay's window: a tooltip is its own
X window and never lands in an `import -window $win` frame.

The busy line above the prompt box is where the state's sentence lives, and it is card #RR0G's
evidence: `docs/qa_evidence/2026-09-19-relaying-dash/`.
