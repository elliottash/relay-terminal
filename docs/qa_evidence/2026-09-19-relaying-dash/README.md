# #RR0G — a dash after "Relaying" (implementer evidence, 2026-09-19)

Owner's request: `add a " -- " after "Relaying", i think it should be "Relating -- thinking..." for
example, or "Relaying -- running grep..", etc`. What landed is a **spaced en dash** (U+2013), the
typeset form of the owner's ` -- `, in every spelling the busy line above the prompt box has.

## What was run

```
bash docs/qa_evidence/2026-09-19-relaying-dash/drive.sh          # all four scenes
```

`drive.sh` is the #HQ2B harness (via #4E13): Xvfb on a free display, an isolated `HOME`,
`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` under a short
`/tmp/relay-rr0g.*` path (the 108-byte unix-socket limit), `RELAY_KEYRING=off` so the owner's real
identity key is never touched, per-pane isolation off (the sandbox runtime dir has no bus socket),
and no provider account: `local:stub` points at `stub-provider.py` on 127.0.0.1:8815.

One change to the copied stub: it keys a scene on **any** user message in the conversation, not the
first. The driver's readiness probe (`echo relayqaready`) reaches the agent in this sandbox and so
*is* the first user message — keying on it made every scene take the 4 s "Done." branch, which is
what the first run of this directory captured. (Keying on the *last* user message is wrong for the
other reason: the worker appends one of its own at the end of a turn.)

## What to look at

In each `implementer-<scene>-x-comp2x.png` (the composer at 200 %), the first row of the composer
frame — the busy line, left-aligned with the prompt text:

| Scene | The line, as captured | Colour |
|---|---|---|
| `relaying` | `Relaying – thinking… · 6 s · step 1/256 · Esc stops` | agent violet |
| `running` | `Relaying – sleep…` | terminal blue |
| `spawn` | `Relaying – waiting for 1 subagent…` | agent violet |
| `question` | `Relaying – waiting for your answer… · 7 s · step 1/256 · Esc skips it` | amber (#MQ9C) |

That is all three spellings the decision names (`tickTurnClock`, and both branches of
`refreshBusyLine`), plus the blocked-on-a-question spelling, which is `tickTurnClock` again.

`implementer-<scene>-x.png` is the whole window for each, `-comp.png` the same crop at 100 %, and
`logs/` holds each scene's `relay.log` and `worker.log`.

The pane header's own state word is **not** this line; it is card #0STR, whose evidence is
`docs/qa_evidence/2026-09-19-header-state-word-and-path/`.
