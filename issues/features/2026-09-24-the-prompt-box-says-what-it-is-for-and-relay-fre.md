---
id: CWVF
type: work
status: planned
labels: [feature, gui, composer, onboarding]
component: [gui, worker]
milestone: beta
rank: zzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: visual, primary: script, also: [ai-visual, person], human: optional, criteria: 'a fresh pane''s box reads ''Type a command, or say what you want done''; the chip says terminal or agent before Enter on ''ls'' and on ''list the files''; the first Relay Free reply carries the note line once', sign_off: none, effort: medium}
source: 'Claude Fable session in Relay, 2026-09-24, delivery card 2 of 3 from #9HS0'
links: {plans: [], commits: [], evidence: [], related: [9HS0, HG7K, VZ69, 6VMF], github: null}
---
# The prompt box says what it is for, and Relay Free is named from the first turn

## Issue
all recs approved, go ahead and file the three delivery cards linked to #9HS0.

## Done means
- A fresh pane's placeholder reads **Type a command, or say what you want done      ?  help   /  commands**, with narrower rungs "Command or request…" and "Command…" (the ladder in `src/AgentContext.cpp:46-64` splits on commas). After the first successful agent turn the terse placeholder returns.
- While a line is typed, the mode chip says **terminal** or **agent** in the two destination colours, from the same routing the backend will apply at Enter; once the person has used `!` or `*` twice, or hints are off, the chip goes back to its quiet form.
- `?` on an empty box answers "what can you do here?" accurately for this pane: the tools it has, the Board if the tab has one, the model it is on and whether it is Relay Free or the person's own key. Nothing in that answer names a capability that fails.
- The first agent reply on a fresh install carries one `Ink::Note` line: *Answered on Relay Free, the included model · N turns today · your own key: Options › Models.* Once ever. The quota chip is visible from that turn.
- Failure looks like: the old placeholder on a fresh pane; the chip silent or wrong on `git status` versus `why is this failing?`; the note line repeating; `?` naming a tool the pane does not have.

## Plan
### Goal
The prompt box tells a newcomer what it is for and where a line will go, `?` answers what the agent can do here, and Relay Free is named on the first reply. Design: #9HS0, `reports/Beginner UX and onboarding for Relay.md` §3.4 and §3.6; decisions 13 and 16 on #9HS0 apply.

### Findings
- Placeholder: `src/Pane.h:620-622` ("Shell commands or agent prompts…      ?  for help"); rungs from `placeholderRungs` (`src/AgentContext.cpp:46-64`, splits on `, ` and ` — `); `RichEditor::setPlaceholders` (`src/RichEditor.cpp:148-165`); the ask-owns-the-placeholder rule (#R3YN, `src/Pane.h:9080-9095`).
- Mode chip: `m_modeChip`, `applyChipFlash` (`src/Pane.h:10174-10180`); prefix chips `! terminal` / `* agent` (`setPrefixMode`, `:10213`); destination colours `applyDestinationColor` (`src/Pane.h`, `docs/SWITCHBOARD-AESTHETIC.md` §1). Routing is decided by the backend at submit (`docs/ARCHITECTURE.md` §5); the composer already classifies locally for highlighting (`src/ShellHighlighter.cpp`).
- `?` help: the `help.slash` hint and the help handler in `src/Pane.cpp`; the tool list the pane holds is in the `configured` event; the model and provider in the pane's model box.
- Relay Free: the `relay_free` branch (`src/Pane.h:5431-5560`), the `hosted_quota` event (protocol 13.9), `docs/RELAY-FREE.md`; `Ink::Note` printing as in `initDefaultRelayMd`.

### Steps
1. Placeholder ladder: "Type a command, or say what you want done      ?  help   /  commands" → "Command or request…" → "Command…", written comma-first; shown until `hints/first_agent_turn_done` is set, then the terse form.
2. Routing chip while typing: on each edit, the composer's local classification (the same rule the backend applies: a leading `/`, `!`, `*`, `?`, a known program or shell syntax → terminal, else agent) sets the chip's text and destination colour; it goes quiet after two uses of `!` or `*`, or with hints off. Where the local rule and the backend disagree at Enter, the backend wins and the chip flashes the destination it chose (the existing flash).
3. `?` on an empty box: the help answer is built from the pane's own `configured` state (tools, Board present, model, Relay Free or own key) and lists the slash commands; no capability is named that the pane lacks.
4. First-reply note: after the first `done` on a Relay Free turn per install, print one `Ink::Note` line with the turn count from the last `hosted_quota` event; setting `relayfree/introduced`.
5. Docs: README key table row for `?`; `docs/ARCHITECTURE.md` composer section.

### Risks
- A local routing guess that disagrees with the backend would teach the wrong thing; step 2 keeps one rule in one place (the classifier the backend uses is Python; the composer needs the same table in C++ or a cheap round trip on idle keystrokes). Name the choice on the card when made.
- The placeholder ladder must degrade on narrow panes (#VZ69).

### Verify
- `ctest --test-dir build -R composer` and the placeholder-rung tests in `tests/test_agentcontext.py` (or the existing rung test file).
- `PYTHONPATH=backend python3 -m unittest tests.test_presets -v` for the note's quota source.
- Xvfb screenshots: fresh pane placeholder; chip on `git status` and on `why is this build failing?`; the first Relay Free reply with its note; `docs/qa_evidence/<date>-prompt-box/`.
