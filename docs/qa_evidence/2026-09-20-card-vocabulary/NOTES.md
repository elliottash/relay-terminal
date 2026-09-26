# #VQ8T — "card" reserved for Switchboard cards

The word had grown eight senses. Three were renamed, three keep the word under a rule, and two
were grep artefacts (`discard`, `wildcard`). Owner agreed the survey on 2026-09-20.

## What landed

| Commit | Area |
|---|---|
| `9e1b10ae` | the card itself, with the survey and the decisions |
| `820c667b` | `backend/relay_core/` prose, and the new `RELAY.md` "Words" section |
| `c1880026` | `src/`, `tests/`, `app/app.js`: identifiers, three visible strings, comments |
| `e010a064` | the approvals and questions tests' own prose |
| `2b21edbc` | `docs/` reword: protocol §27, architecture, remote protocol, phone design |
| `e71be553` | the README `/help` line |
| `ee3569bd` | the web client renders plan blocks, not "plan cards" |
| `79c4e751` | two docs still cited `relay::input::cardTakesRemoteLine` |
| `44698c60` | the guest harness, missed in the first pass |

## Renames

| from | to |
|---|---|
| `relay::input::cardTakesRemoteLine` | `askTakesRemoteLine` |
| `RemoteLine::cardOpen` | `askOpen` |
| `Pane::unreadableCard` | `unreadableAsk` |
| `m_helpCard` / `toggleHelpCard` / `QFrame#helpCard` | `m_helpPopup` / `toggleHelpPopup` / `QFrame#helpPopup` |
| `ApprovalCardTests` / `PaneApprovalCardTests` / `PaneCardTests` | `ApprovalAskTests` / `PaneApprovalAskTests` / `PaneAskTests` |
| `guest_harness_provider.approval_card` | `approval_ask` |
| `app/app.js` `const card` (plan block) | `const plan` |

Nothing on the wire moved: `question`, `question_closed`, `question_answer`, `ask_user` and
`approvals_ask` are unchanged, and every `card #ID` citation is byte-identical.

## User-visible strings

- `"Answer 1–4 · this card takes a number or the word itself"` → `"Answer 1–4 · a number or the word itself"`
- the approvals tooltip → `"Never ask; what Relay has always done"`
- `"Ask me puts each one to you as a card: Allow, …"` → `"Ask me puts each one to you: Allow, …"`
- Options › Security's opening paragraph, `"…the actions you tick on a card"` → `"…you tick and asks first"`
- the model-visible system prompt, `"the turn waits at a card"` → `"the turn waits at an ask"`

## Proof

**Clean-export build.** `git archive 44698c60` into a scratch tree, configured and built there, not
in the shared checkout. `clean-export-build.txt` is the tail: exit 0, `[100%] Built target relay`.
This matters because another session landed C++ touching `src/RelayWindow.h` and `src/Theme.cpp`
after the rename commit, so the rename's own build gate did not cover the combination.

**Tests.** 300 pass across the questions, approvals and three guest-harness suites, plus the C++
`input`, `theme`, `themeswitch` and `panestatus` targets, 4/4.

```
RELAY_KEYRING=off PYTHONPATH=backend python3 -m unittest \
  tests.test_guest_harness_claude tests.test_guest_harness_codex \
  tests.test_guest_harness_provider tests.test_questions tests.test_approvals
Ran 300 tests in 0.782s — OK
```

**Sweep.** Every remaining `card` within reach of an approval or question word across `src/`,
`backend/relay_core/`, the three protocol docs, `app/`, `tests/`, `README.md` and `RELAY.md` is
Switchboard-sense: a question comment that goes *on* a card, a card thread, or `/card` held text.

## Left as written, deliberately

- `docs/qa_evidence/2026-09-19-claude-codex-guest-integration/approval-drive.py` still calls
  `approval_card`. It is a frozen record of a past run and nothing executes it.
- `docs/AGENT-FEATURES-RESEARCH.md` and `docs/INTAKE-CLARIFICATION-RESEARCH.md` describe Claude
  Code's and Warp's real card UI. Each mention already carries its source tag.
- The theme's material "card" where it means a raised rounded surface, and the board metaphor at
  `src/Theme.cpp:651`.
- `engine/TerminalBackend.h`'s `Link::card` / `Link::cardTitle`. Board vocabulary in a layer that
  knows nothing about the board is a layering question, not a naming one.
