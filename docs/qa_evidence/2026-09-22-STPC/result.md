# Relaying notifier without step count

- `scripts/relay-build --target relay relay-consolemode-tests -j 2`: passed.
- `ctest --test-dir build -R '^consolemode$' --output-on-failure`: passed (1/1).
- `python3 docs/qa_evidence/2026-09-22-STPC/live-drive.py`: ran the full Relay window under Xvfb with isolated XDG directories and a synthetic stdio worker. The worker emits `agent_started` then `status: Requesting model · step 7/500`.
- Visually inspected `live-notifier.png`: the notifier reads `Relaying · thinking… · 5 s · Esc stops`, without the step count. Action, elapsed time and shortcut remain visible.
- `git diff --check -- src/Pane.h`: passed.
- Board format check: no findings for STPC; unrelated existing board findings were left unchanged.

This is implementer evidence; independent verification remains on the board.
