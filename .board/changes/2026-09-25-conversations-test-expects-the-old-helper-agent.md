---
id: TJJ3
type: work
status: discussing
labels: [bug, tests, sessions]
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'pane 1, 2026-09-26, while running the #G2C7 suite'
links: {plans: [], commits: [], evidence: [], related: [G2C7], github: null}
---
# conversations test expects the old 'Helper Agent' label the #E8V1 rename dropped

## Issue
Discovered while landing #G2C7: ConversationsTest::theHelperConsoleSitsAtTheBottomCollapsedAndAsksAsTheSessionsPane fails at HEAD — commit 521d3f97 (#E8V1) renamed the helper row's button to "Agent"/"Agent (Ctrl+/)" in SessionManager::updateHelperRow (src/Conversations.cpp:1813-1814) but left the test (tests/conversations_test.cpp:2293) and the comment above the method expecting "Helper Agent (Ctrl+/)". Measured: QT_QPA_PLATFORM=offscreen ./build/relay-conversations-tests theHelperConsole… → Actual "Agent (Ctrl+/)", Expected "Helper Agent (Ctrl+/)".
