// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::agent::TestsContext and relay::agent::SharingContext — the docked agent on the Test suites
// and Sharing panes (card #3B1B, slice 9 of #P2W8, decision U4).
//
// The owner, 2026-09-25: "the third type of pane is system pane or options pane. thats like our
// board or models panes. you have an agent docked there as well, same as with artifacts, but that
// agent helps you manage the options." The Board, Options, Sessions and Models already had one;
// these are the two system panes that did not. Each is a `Context` like `ModelsContext`
// (src/ModelsPane.cpp): what the pane is showing right now as `screen`, a row of actions that need
// no typing, and one link scheme it resolves on its own pane — `test:` selects a row, `pane:`
// focuses a pane. The console itself is the ordinary no-shell `Pane` the window builds through
// `RelayWindow::createAgentConsole`, docked by `relay::ContextDock` (src/ContextDock.h).
//
// QtCore only, like the rest of `relay-agentcontext`: the pane hands its state over as the plain
// structs below, so the specs, the action rows and the link rules are tested without a widget
// (tests/agentcontext_test.cpp).
#pragma once

#include "AgentContext.h"

#include <QString>
#include <QStringList>

#include <functional>

namespace relay::agent {

// ---------------------------------------------------------------------------------------------
// What the Test suites pane is showing, asked fresh for every `configure` and every `ask`.
struct TestsState {
    QString summary;           // the header line: "42 tests · 3 failed · 1 flaky · 2.4 s"
    QString runLine;           // the run in flight, or empty
    QString filter;            // what is typed in the filter box
    int visible = 0;           // rows the filter leaves
    int total = 0;             // rows discovered
    bool running = false;      // a run is in flight
    // The selected row; `selectedId` empty when nothing is selected.
    QString selectedId;        // "ctest:panelayout", the stable id a `test:` link names
    QString selectedName;
    QString selectedInvocation;   // "ctest -R panelayout"
    QString selectedFile;         // "tests/panelayout_test.cpp:42"
    QString lastResult;           // "fail", "pass", … or empty when never run
    QString lastRun;              // "3 m ago"
    QString reliability;          // "92 %" or "—"
    QStringList badges;           // "flaky", "slow", …
    QStringList cards;            // card ids whose ## Tests names it
    // Its last failure, when it has one.
    QString failureAt, failureCommit, failureMessage, failureExcerpt;
    // The failing rows on screen, in table order: "Run failed" runs these.
    QStringList failingIds;
};

class TestsContext final : public Context {
  public:
    TestsContext() = default;

    // The pane's half, all optional: a context with none of them is readable and does nothing.
    std::function<TestsState()> state;
    std::function<void()> runSelected;
    std::function<void()> runFailed;
    std::function<void()> attachToCard;
    // Put the row with this id on screen and select it; false when there is no such test.
    std::function<bool(const QString &id)> selectTest;

    ContextSpec spec() const override;
    QList<Action> actions() const override;
    bool resolveLink(const relay::links::Target &target) override;
    QString placeholder() const override;

    QString title() const { return QStringLiteral("Tests agent"); }
    // The `screen` hint as it goes out: the selected row and its last failure first, because that
    // is what "why did it fail" is about, then the filter, the header and the failing rows.
    QString screen() const;
};

// ---------------------------------------------------------------------------------------------
// What the Sharing pane is showing.
struct SharingState {
    QString page;              // "Devices" or "People"
    QString remote;            // the top line: "Remote control on · relay-terminal.ai · iPhone connected"
    QStringList devices;       // "iPhone (online, full)"
    // One line per shared pane: its token first, so the agent can write `pane:<token>`.
    struct Shared {
        QString token, title;
        QString scope;         // "pane", "tab" or "all tabs"
        int guests = 0;
        int waiting = 0;
        QString driver;        // who is typing in it, when it is not the owner
        bool paused = false;
    };
    QList<Shared> shared;
    QStringList guests;        // "alice (editor) on Build log"
    int waiting = 0;           // requests waiting for the owner, all panes
    QString currentToken;      // the pane the Sharing pane was opened from
    QString currentTitle;
    bool currentShared = false;
};

class SharingContext final : public Context {
  public:
    SharingContext() = default;

    std::function<SharingState()> state;
    // "Add a device…": the Devices page and a pairing code, exactly as the button.
    std::function<void()> pairDevice;
    // "End sharing" for the pane the Sharing pane is on — the same call its own button makes.
    std::function<void()> stopSharing;
    // Focus the pane with this session token; false when the window has no such pane.
    std::function<bool(const QString &token)> focusPane;

    ContextSpec spec() const override;
    QList<Action> actions() const override;
    bool resolveLink(const relay::links::Target &target) override;
    QString placeholder() const override;

    QString title() const { return QStringLiteral("Sharing agent"); }
    QString screen() const;
};

}  // namespace relay::agent
