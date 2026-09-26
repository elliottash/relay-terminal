---
id: M13J
type: work
status: done
labels: [bug, switchboard]
implemented_by: glm/glm-5.3
verified_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: 'pane 1, 2026-09-27, while landing #NX72'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Tip does not build relay: setBoardRoot landed without its Conversations half

## Issue
Main does not build at tip: RelayWindow.h at tip calls `SessionManager::setBoardRoot`, whose declaration and definition live only in the uncommitted working-tree edits to src/Conversations.h / src/Conversations.cpp from the #E34S session. Every other session's verify-slot landing of `relay` is refused until the two halves meet.

> Unrelated fault noticed while landing #NX72: the land.py verify slot (clean tree of tip 98823eb9) fails `cmake --build … --target relay`: src/RelayWindow.h:4137: error: 'class relay::conversations::SessionManager' has no member named 'setBoardRoot' (RelayWindow.cpp.o / RelayWindowSharing.cpp.o / RelayWindowModels.cpp.o / main.cpp.o). The declaration `void SessionManager::setBoardRoot(const QString&)` and its implementation exist only as uncommitted working-tree edits in src/Conversations.h:243 / src/Conversations.cpp:2206, which also fail -Werror=unused-function (`instantWords`) in the shared build/. The landing session of #E34S (live: s976-finish / y2ba-cards / g2c7-layout) needs to land its Conversations half or the RelayWindow.h call needs a repair.
> — elliott · [session:5d87c4c8dbad4dbcbead00c1670ecbde](relay://session/5d87c4c8dbad4dbcbead00c1670ecbde) · 2026-09-25
