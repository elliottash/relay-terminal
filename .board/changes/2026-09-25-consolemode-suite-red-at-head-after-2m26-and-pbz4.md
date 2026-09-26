---
id: Y2PQ
type: work
status: inbox
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
links: {plans: [], commits: [bf5aa57c, b70c33bf], evidence: [docs/qa_evidence/2026-09-25-verify-bug-cards], related: [PH9G, PBKR], github: null}
---
# The consolemode test suite is red at HEAD

## Issue
`ctest -R '^consolemode$'` fails at clean HEAD `2db96643` with 5 assertion failures (`tests/consolemode_test.cpp:633,634,646` — editor path/line/context expectations; `:1917,1920` — tool-call rendering text). The suite is fully green at `1b05786b` (2026-09-22, checkout-verified: "consolemode: 20 cases, all passed"), so later work regressed the expectations — prime suspects `bf5aa57c` (#2M26 WARP.md→RELAY.md) and `b70c33bf` (#PBZ4 artifact consoles / file editors), plus the 2026-09-25 prompt/recall wording commits. Found during the models-verification sweep (#PH9G/#PBKR name this suite). Plan-mode cases still pass (`--plan-click-only` exits 0).

## Done means
`ctest -R '^consolemode$'` green at HEAD with expectations matching the shipped editor/tool-call behavior (or the regressing change is fixed).

## Tests
`XDG_CONFIG_HOME=$(mktemp -d) xvfb-run -a ctest --test-dir build -R '^consolemode$'` — all pass.
