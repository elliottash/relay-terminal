<!-- relay:entry 20260920T213128Z-c3 author=claude-code kind=event -->
### Claude Code · 2026-09-20 21:31
created this card from a clean-export build of main on sphinxpad: the Qt6 app build and the board test build are both broken by commits landed today; comments left on #0TJ9 and #8YQ9

<!-- relay:entry 20260920T215419Z-jm author=agent kind=evidence model=kimi-k3 pane=b2bc87a9 turn=cd14e3411c104989b6ae99393507dcfd/87603178ea23404fb0df731d160403b3 -->
Reproduced the boardmodel_test.cpp half today while building for #EB4A: at tip d31a5da (and still at e0ee167e), `scripts/relay-build` fails compiling `tests/boardmodel_test.cpp:3635` — the void lambda `view.onModelPick = [&picked](const QString &data) { picked = data; };` does not convert to the committed `std::function<bool(const QString &data)> onModelPick` (src/BoardPane.h:73). The `relay` binary itself builds fine; only the relay-board-tests target fails. Did not touch it — executing another card.

<!-- relay:entry 20260920T215419Z-rz author=agent kind=event model=kimi-k3 pane=b2bc87a9 turn=cd14e3411c104989b6ae99393507dcfd/87603178ea23404fb0df731d160403b3 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started
