# Info and Activity: the "Ask" rows (#FEJQ, step 8)

Implementer evidence for the row at the foot of the ⓘ pane (`src/SessionInfo.{h,cpp}`) and of the
Activity pane (`src/AgentInternalsView.{h,cpp}`). Neither pane has a helper agent of its own — the
owner, 2026-09-20: "Info and Activity get no helper of their own: they are about the pane's own
agent … an 'Ask' row on those panes prefills its composer" — so a chip drafts a question about
what is on screen into the owning pane's composer and sends nothing.

`shot.cpp` renders both views offscreen with `onAskOwner` wired to a callback that only records,
which is what the window will wire to `Pane::insertInComposer`. Build it against the two static
libraries and run it under `QT_QPA_PLATFORM=offscreen`:

    g++ -fPIC -std=c++17 shot.cpp -o shot -Isrc -Iengine $(pkg-config --cflags Qt5Widgets) \
        -L<build> -lrelay-conversations -lrelay-internals -lrelay-calllines -lrelay-diffview \
        -lrelay-highlight -lrelay-theme -lrelay-toollabel -lrelay-markdown -lrelay-calllines \
        $(pkg-config --libs Qt5Widgets)
    QT_QPA_PLATFORM=offscreen ./shot <output dir>

## `info-ask-row.png`

The ⓘ pane on a live session. The row sits between the page and the standing key line:
"Ask the agent about this session · drafts a question in the terminal's prompt box · nothing is
sent", and the chips **Context · 20.6%**, **What it has done**, **Costliest turn**. The first
chip's figure is the one the Context row of the page shows, and the question it drafts carries the
same one ("Why is my context at 20.6% — what is taking the room? (41.2k / 200.0k tokens used.)").

## `activity-ask-row.png`

The Activity pane after two turns. The chips are **Last turn · under a second** and
**Slowest tool calls**; a third, "Turn 1", appears only when the reader's cursor goes back up the
log into an earlier turn, because on the newest turn it would draft the same question as
"Last turn".

## What is not shown here

The row is not drawn at all until the window assigns `InfoView::onAskOwner` /
`AgentInternalsView::onAskOwner`, which is `src/RelayWindow.h`'s to do (`openInfoPane`,
`linkInternalsPane`) and belongs to the session holding that file. Until then both panes look
exactly as they did. The live Xvfb pass — clicking a chip and seeing the draft appear at the
terminal's composer cursor — belongs with that wiring.

## Tests

    ctest --test-dir build -R '^(agentinternals|conversations)$'

`tests/agentinternals_test.cpp`: the wording helpers (one place for both panes), the row hidden
until there is a composer to draft into, each chip drafting exactly what it says with the live
figure in it, no other callback firing, and the "Turn n" chip following the cursor.
`tests/conversations_test.cpp::infoViewAskRowDraftsAboutTheSessionOnScreen`: the same for the ⓘ
pane, plus the row disabled with the reason in its tooltip on a subagent thread and on a saved
session.
