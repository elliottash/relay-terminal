---
id: JWSA
type: work
status: inbox
labels: [bug, panes]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzjwsa
created: '2026-09-25'
source: 'found while working #S976, measured 2026-09-25'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-program-input-mode/], related: [S976], github: null}
---
# The queue row keeps saying "running" for a program that has exited while the shell is at its prompt

## Issue
Found while working #S976 (2026-09-25), measured on an isolated profile under Xvfb with `terminal/persistLocal=false`: after `python3 -q` then `exit()`, and `sqlite3 :memory:` then `.quit`, the shell's next prompt is drawn (with its prompt mark) and the chip drops back from PROGRAM, yet the QUEUE panel still shows `running $ sqlite3 :memory:` while `gti status` is being typed at the prompt: `docs/qa_evidence/2026-09-25-program-input-mode/07-terminal-typo-did-you-mean.png` (and `04-python-exited-back-to-auto.png` for python3). The row changes only when the next command starts.

## Done means
- When an interactive program the queue started exits and the shell prints its next prompt, the queue row leaves `running` within a poll tick.
- Fails if the #S976 drive's 04/07 screenshots still show a `running` row for a program that has exited.
