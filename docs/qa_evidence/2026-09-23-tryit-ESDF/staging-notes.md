# Staging notes for #ESDF's Try it

- `stage.sh` (run it from this directory with `RELAY_BIN=/tmp/esdf-tryit/build/relay ./stage.sh`)
  restages `~/relay-qa/esdf-notes` from scratch every time — ten cards across every stage, whose
  `updated` times are the card and thread files' mtimes, spread from 4 minutes ago (an iPad export
  bug in Needs QA (human)) to ten days ago. No network, no model; the sandbox is
  `/tmp/claude-1000/tryit/esdf` and the app is disposable (`stage.sh --stop`).
- The window opens with the Board pane showing the **flat Recent list** — the default since
  `dc5b457` — so the owner lands directly in the situation the card asked for.
- The mechanical pass (`ai-pass.sh`, captures in `captures/`) has already checked: flat default
  and its order, the STAGE click to sections and back, the sort cells, and the choice surviving
  a restart. What is left for the person is the judgement.
