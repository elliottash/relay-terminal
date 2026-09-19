# The planner asks the user questions (#MQ9C) — implementer evidence, 2026-09-19

These are **implementer** screenshots, not a QA verdict. Reproduce with

```
docs/qa_evidence/2026-09-19-the-planner-asks-questions/drive.sh [build-dir]
```

which runs the app under Xvfb in a sandbox (its own `HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`
and `TMPDIR`) against `stub-provider.py` on loopback — a planner that answers its first call with
`ask_user` and only writes its plan once the pane has answered. No provider account, no keys.

| Shot | What it shows |
|---|---|
| `implementer-card.png` | Plan mode, the first question. The card is in the amber "needs human" ink; option 1 carries the model's `← recommended`; the descriptions are muted; `0` skips. The prompt box invites the answer ("Scope · 1–3, 0 to skip, or your own words") and the turn clock says **waiting for your answer · 6 s**, not "thinking" — the turn really is blocked inside the tool. |
| `implementer-card-tab.png` | The tab bar with the asking pane in the background: the amber `needs-you` glyph, from `panestatus::resolve` (`questionOpen` outranks `Working`). |
| `implementer-second.png` | `1` answered the first question — echoed as `✦ Scope: This file only` in the agent's violet, the way any line the user sends the agent is — and the second, multi-select question is up ("Numbers (\"1,3\"), or your own words"). |
| `implementer-open.png` | The third question has **no options at all**: no numbered list, no `0`, the footer reads "Type your answer · /skip to pass" and the prompt box says "Wording · type your answer, or /skip". |
| `implementer-answered.png` | All three answered, the last in the user's own sentence. The turn resumed, `write_plan` quoted both answers back into the plan (`## What you told me`), the plan pane opened with Execute / Execute in fresh context / Keep planning, and the folded line reads `asked you about Scope, Checks · 17 s`. |

## What a QA session should check

- [ ] A question card appears in plan mode and the turn does not continue until it is answered.
- [ ] The card is amber in every shipped theme, and legible on the light ones.
- [ ] The pane's status glyph and its tab go to "needs you" while the card is up, and back after.
- [ ] A number answers; several numbers answer a `multiple` question; `0` skips; anything else is
      sent as the user's own words.
- [ ] `0` on every question tells the model to decide for itself, and it does not ask again.
- [ ] **Ctrl+Shift+Enter still runs a shell command while a card is up** — a question from the
      agent must not take the terminal away.
- [ ] Stop (Esc) while a card is up: the card goes away, the turn ends, nothing is left waiting.
- [ ] `/new` and an agent-worker restart both take the card down.
- [ ] Typing an option out in full instead of its number shows the "Next time: just type 2" hint
      once, and an answer in the user's own words does not.
- [ ] Build mode carries the tool too: `Shift+Tab` back to BUILD and a turn can still ask.
- [ ] An open question (no `options`) prints with no numbered list, the prompt box says "type your
      answer, or /skip", and whatever is typed comes back as the answer.
- [ ] `/skip` passes on both kinds.
- [ ] A subagent cannot ask: `ask_user` is not in its tool list (`tests/test_questions.py` covers
      the refusal; a live check would need a subagent definition that names it).
- [ ] On a paired phone: the card is readable in the pane and a line typed there answers it.
