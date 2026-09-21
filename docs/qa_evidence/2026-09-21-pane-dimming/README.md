# Pane dimming — #RG0Z

Implemented automatic working-agent dimming, explicit manual dimming, focus mode, a 0–95% Options strength (default 90%), and pane-local Alt dimmer controls. Working state includes built-in agents, guests and live subagents. Questions/blocked states/errors reveal without changing focus. Completion preserves manual intent. Entering reveals temporarily; a deliberate adjustment acts immediately even while focused.

Validation:
- `scripts/relay-build --target relay` passed (2026-09-21.15H.01).
- `ctest --test-dir build -R '^(panedimming|panestatus|settings|panes)$' --output-on-failure`: all four passed; see tests.txt. The new Qt test has five cases (seven passes including setup/cleanup).
- Policy cases cover start/question/resume/done, completion preserving manual dimming, focus reveal/leave restoration, explicit active-pane adjustment, clamping and accumulated wheel steps.
- Overlay widget tests check light/dark pixels, untouched header, mouse transparency, resize and hide. overlay-dark.png and overlay-light.png are those widget renders.
- Live app driven under Xvfb with isolated XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_RUNTIME_DIR and a scratch workspace. Screenshots 01–06 cover focus mode, ten Alt+minus adjustments, Alt+equal restoration, Alt+wheel over the other pane, retained keyboard focus (typed marker), entering to reveal and leaving to restore.
- 07-options.png shows the dimming controls in Options. 08-light-mode.png checks a light theme in the real two-pane app.
- Board tests_check: every named test collected, run, neither flaky nor slow; no findings. Board format check has pre-existing errors/warnings elsewhere; none names RG0Z.

Limits: agent transitions are covered by deterministic policy tests, not a paid live model run. Live agent lifecycle integration, multi-window movement and platform-specific Alt-wheel interception by a desktop window manager remain verifier checks. The desktop can consume Alt+wheel before Relay receives it; the keyboard controls and pane button remain available.
