# Subagent tracker role placement

The tracker shows the ID alone. General summaries have no prefix; other roles use bracketed prefixes such as [explore] and [signal].
Rendering only: stored descriptions, role metadata and subagent behavior are unchanged.

Validation on 2026-09-22:
- `scripts/relay-build --target relay relay-subagents-tests relay-striplayout-tests` passed.
- `ctest --test-dir build -R '^(subagents|striplayout)$' --output-on-failure`: both suites passed.
- Xvfb with isolated `XDG_CONFIG_HOME`: existing `stripPairsSubagentsWithTasks` passed.
- `roles.png`: refreshed live tracker widget, unprefixed general and bracketed explore/signal summaries, inspected visually. Build and both suites rerun successfully after the refinement.
- `tracker.png`: existing narrow paired-task fixture; normal summary elision remains.

The repository-wide board format check reported 12 errors and 754 warnings in other cards; no findings named this card.
