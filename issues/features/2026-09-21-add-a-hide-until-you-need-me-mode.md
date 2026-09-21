---
id: RG0Z
type: work
status: needs-verification
labels: [feature, panes, appearance]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-21'
links: {plans: [], commits: [a4c930d05b9964c05a7b7b932c736c6137785d4c], evidence: [docs/qa_evidence/2026-09-21-pane-dimming/], related: [], github: null}
---
# add a "hide until you need me" mode

## Issue
add a "hide until you need me" mode
Full request:

> add a "hide until you need me" mode. panes will dim to almost black (or white for light mode) when the agent is working and then rebrighten when stopped -- question / blocked / done.
>
> also make the dimmer a button in case i want to focus on something later.
>
> also make a focus mode that dims all other panes except the one you are working in.
>
> can pick the dimmer level in the options menu.

> lets also make alt + mouse wheel, or alt + +/-, work as dimmer knobs.

## Decisions
Owner: "completion does not ovefrride manual dimming". Owner: "otherwise i agree with your recs, put it in the card".

- Global automatic hide-while-working toggle; separate focus mode and per-pane manual dimmer.
- Alt+wheel adjusts the hovered pane; Alt++/− adjusts the active pane. Up/+ brightens, down/− dims in 5% steps, clamped to 0–95% dimming. Options sets the default (90%); per-pane overrides are temporary.
- Entering a pane temporarily reveals it; leaving restores its dim setting. Explicit button/knob adjustments take effect immediately, even while active.
- Questions and blocked states reveal a pane without stealing focus. Completion reveals automatically dimmed panes but never overrides manual dimming. Keep controls and status indicators legible.
- Fade toward black in dark themes and white in light themes. Dimming does not stop execution.

## Plan
Goal: automatic working-agent dimming, manual pane dimmer, focus mode, Options strength, and Alt dimmer knobs.

Findings: src/PaneChrome.h owns shared pane controls; src/RelayWindow.h owns active-pane tracking, status polling and Appearance settings; src/Pane.h exposes built-in status facts and guest busy state; src/Keymap.h owns configurable shortcuts; src/SettingsCache.h provides hot-path reads. Existing tests cover panestatus, panes and settings. Other sessions have edits in this checkout.

Steps:
1. Add a tested dimming policy/overlay in src/PaneDimming.h; retain manual intent independently from focus/attention overrides.
2. Add the per-pane dim button and visible status/control strip in src/PaneChrome.h.
3. Integrate focus/status/theme updates and guest-agent activity in src/RelayWindow.h, plus global automatic/focus toggles and strength under Options > Appearance.
4. Register Alt++/− actions in src/Keymap.h and Alt+wheel on hovered panes; respect rebinding and add shortcut hints on the slow path. Never move focus on wheel or agent attention.
5. Add focused policy/widget tests in tests/panedimming_test.cpp and CMakeLists.txt. Build through scripts/relay-build; run targeted tests and isolated Xvfb visual checks.
6. Record evidence and QA checklist, land only scoped edits through scripts/land.py, move to needs-verification.

Risks: focused-but-busy panes must dim until deliberately re-entered; guest agents use separate lifecycle state; overlays must not intercept input or hide restore controls; shared-file edits require hunk review.

Verify: manual completion exemption, attention overrides and restoration, focus reveal, auto start/stop and guest activity, 5% clamping, hovered versus active targeting, configurable keys, light/dark colors, settings persistence/live changes, pane resize/close/tab switching. Targeted Qt tests plus Xvfb screenshots with isolated XDG_CONFIG_HOME.

Research: iTerm2 adjustable inactive dimming (https://iterm2.com/documentation-preferences-appearance.html); Ghostty overlay fill (https://ghostty.org/docs/config/reference#unfocused-split-opacity); Warp inactive dimming and agent notifications (https://docs.warp.dev/terminal/appearance/pane-dimming); cmux attention rings (https://cmux.com/docs/changelog/0.60.0).

## Tests
`ctest --test-dir build -R '^(panedimming|panestatus|settings|panes)$' --output-on-failure`

`manual: docs/qa_evidence/2026-09-21-pane-dimming/`

All four targeted tests pass. panedimming covers automatic lifecycle, manual completion exemption, attention/reveal restoration, bounds and wheel accumulation, overlay input transparency and light/dark pixels.

## Execution Summary
Implemented pane-header ◐ toggle; automatic hide-while-working and focus modes; Options > Appearance strength (90% default); Alt+wheel over hovered pane and Alt++/− for active pane in 5% steps. Bright status/header controls remain accessible. Dark themes fade to black, light themes to white. Manual dimming persists through completion and temporary focus/attention reveal. Added Actions entries and configurable keyboard bindings, plus the dimmer shortcut hint. Guest busy state is included.

Evidence: docs/qa_evidence/2026-09-21-pane-dimming/README.md and screenshots 01–08. GUI build passed; all four targeted tests passed. No paid live model run: lifecycle precedence is covered by deterministic tests; live agent integrations remain QA.

## QA checklist
- [ ] Start built-in and guest agents: panes dim while working and brighten for questions, blocked input and completion, without focus theft.
- [ ] Manually dim a working pane: completion leaves it dim with a visible status indicator; a question temporarily reveals it.
- [ ] Enter a manually dimmed pane and leave: reveal then restore. Explicit knob/button adjustment while active takes effect immediately.
- [ ] Alt+wheel targets the hovered pane and preserves keyboard focus; Alt++/− targets the active pane; test rebound keys and desktop shortcut interception.
- [ ] Toggle focus mode and automatic mode in Options/Actions; change strength and restart to check global setting persistence.
- [ ] Check dark/light themes, tool panes, narrow splits, resizing, moving between windows, closing panes and switching tabs. Controls and header remain readable.
- [ ] Verify no scroll/input loss and no content left partly undimmed at the header edge.
