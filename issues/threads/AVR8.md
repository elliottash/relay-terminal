<!-- relay:entry 20260923T024155Z-1d author=agent kind=progress model=gpt-6-astra pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/3ffc4a43d1ee40adaa447ad5c96b02d8 -->
Claimed through POLICY file fallback. Traced availability checkboxes and Priorities rendering; previous provider-refresh fixes do not change these semantics. Checking intended membership behavior before implementation.

<!-- relay:entry 20260923T024302Z-yr author=agent kind=question model=gpt-6-astra pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/3ffc4a43d1ee40adaa447ad5c96b02d8 -->
1. Should checking Available add the model to Priorities automatically and unchecking remove it, or should rankings stay saved and only the selectable candidates refresh? Recommendation: show enabled models immediately as unranked candidates, preserve existing order/effort, and make disabling explicit. Current behavior deliberately pins ranked models available and hides unranked models until search; this is a behavior choice, not simply a missing repaint.

<!-- relay:entry 20260923T024316Z-88 author=agent kind=event model=gpt-6-astra pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/3ffc4a43d1ee40adaa447ad5c96b02d8 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; appended to `## Planning notes`

<!-- relay:entry 20260923T024316Z-v6 author=agent kind=event model=gpt-6-astra pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/3ffc4a43d1ee40adaa447ad5c96b02d8 -->
- ✦ agent moved this card · Executing → Discussing · Awaiting intended membership behavior: automatic rank addition/removal versus refreshing available candidates while preserving saved rankings.

<!-- relay:entry 20260923T024328Z-bg author=agent kind=evidence model=gpt-6-astra pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/3ffc4a43d1ee40adaa447ad5c96b02d8 -->
Baseline rebuilt via scripts/relay-build. xvfb-run -a ctest --test-dir build -R '^(modelcatalog|modelpicker|modelspane)$' --output-on-failure: 3/3 passed. Existing tests deliberately enforce ranked-model availability pins. No production code changed. Board check reports pre-existing errors/warnings elsewhere; no AVR8 finding.
