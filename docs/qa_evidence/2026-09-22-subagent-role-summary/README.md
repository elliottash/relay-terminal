# Subagent tracker role placement

The tracker now shows the ID alone and prefixes the description with its role.
Rendering only: stored descriptions, role metadata and subagent behavior are unchanged.

Validation on 2026-09-22:
- `scripts/relay-build --target relay relay-subagents-tests relay-striplayout-tests` passed.
- `ctest --test-dir build -R '^(subagents|striplayout)$' --output-on-failure`: both suites passed.
- Xvfb with isolated `XDG_CONFIG_HOME`: existing `stripPairsSubagentsWithTasks` passed.
- `roles.png`: live tracker widget, general and explore summaries, inspected visually.
- `tracker.png`: existing narrow paired-task fixture; normal summary elision remains.

The repository-wide board format check reported 12 errors and 754 warnings in other cards; no findings named this card.
