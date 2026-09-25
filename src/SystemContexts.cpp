// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SystemContexts.h"

namespace relay::agent {

namespace {

// The spec every system-pane context shares: the helper role, a named scope, no shell, everything
// typed is a prompt. The window's `TabConsoleContext` puts the tab's project and conversation key
// over it (src/RelayWindow.h), so a context never has to learn what a tab is.
ContextSpec systemSpec(const QString &name, const QString &title, const QString &screen)
{
    ContextSpec spec;
    spec.name = name;
    spec.surface = name;
    spec.agentRole = QStringLiteral("switchboard");
    // Named, never inferred: a board-less console that fell through the worker's inference would
    // get the *pane* branch and the full executor (§33.3, card #AGNT finding).
    spec.scope = QStringLiteral("console");
    spec.briefKey = name;
    spec.briefTitle = title;
    spec.screen = screen;
    spec.shell = false;
    spec.routing = QStringLiteral("agent");
    return spec;
}

// A test's output is a test's output: long, and not ours to paste whole. The head of it carries
// the assertion; the rest is one `tests_history` or one open file away.
QString cut(const QString &text, int limit)
{
    const QString trimmed = text.trimmed();
    return trimmed.size() <= limit ? trimmed : trimmed.left(limit - 1) + QChar(0x2026);
}

Action action(const QString &key, const QString &letter, const QString &label, const QString &tooltip,
              bool enabled, const std::function<void()> &run)
{
    Action a;
    a.key = key;
    a.letter = letter;
    a.label = label;
    a.tooltip = tooltip;
    a.enabled = enabled && bool(run);
    a.run = run;
    return a;
}

}  // namespace

// ----- Tests ---------------------------------------------------------------------------------------

ContextSpec TestsContext::spec() const { return systemSpec(QStringLiteral("tests"), title(), screen()); }

QString TestsContext::screen() const
{
    if (!state)
        return QStringLiteral("Test suites");
    const TestsState s = state();
    QStringList lines;
    if (!s.selectedId.isEmpty()) {
        QStringList facts;
        facts << (s.lastResult.isEmpty() ? QStringLiteral("never run")
                                         : QStringLiteral("last result %1").arg(s.lastResult));
        if (!s.lastRun.isEmpty())
            facts << s.lastRun;
        if (!s.reliability.isEmpty() && s.reliability != QStringLiteral("—"))
            facts << QStringLiteral("%1 reliable").arg(s.reliability);
        if (!s.badges.isEmpty())
            facts << s.badges.join(QStringLiteral(", "));
        lines << QStringLiteral("Selected: test:%1 (%2) · %3")
                     .arg(s.selectedId, s.selectedName, facts.join(QStringLiteral(" · ")));
        if (!s.selectedInvocation.isEmpty())
            lines << QStringLiteral("Run it with: %1").arg(s.selectedInvocation);
        if (!s.selectedFile.isEmpty())
            lines << QStringLiteral("Source: %1").arg(s.selectedFile);
        if (!s.cards.isEmpty()) {
            QStringList ids;
            for (const QString &card : s.cards)
                ids << QLatin1Char('#') + card;
            lines << QStringLiteral("Cards: %1").arg(ids.join(QLatin1Char(' ')));
        }
        if (!s.failureMessage.isEmpty() || !s.failureExcerpt.isEmpty()) {
            QString when = s.failureAt;
            if (!s.failureCommit.isEmpty())
                when += (when.isEmpty() ? QString() : QStringLiteral(" at ")) + s.failureCommit.left(12);
            lines << (when.isEmpty() ? QStringLiteral("Last failure:")
                                     : QStringLiteral("Last failure (%1):").arg(when));
            if (!s.failureMessage.isEmpty())
                lines << cut(s.failureMessage, 400);
            if (!s.failureExcerpt.isEmpty())
                lines << cut(s.failureExcerpt, 700);
        }
    } else {
        lines << QStringLiteral("No test selected.");
    }
    if (!s.filter.trimmed().isEmpty())
        lines << QStringLiteral("Filter: %1 (%2 of %3 shown)").arg(s.filter.trimmed()).arg(s.visible).arg(s.total);
    else
        lines << QStringLiteral("%1 tests shown").arg(s.visible);
    if (!s.summary.isEmpty())
        lines << QStringLiteral("Summary: %1").arg(s.summary);
    if (!s.runLine.isEmpty())
        lines << QStringLiteral("Running: %1").arg(s.runLine);
    if (!s.failingIds.isEmpty()) {
        // As many as fit: the ids are what `test:` links and `tests_run` take.
        QStringList ids;
        int room = kScreenLimit - lines.join(QLatin1Char('\n')).size() - 32;
        for (const QString &id : s.failingIds) {
            room -= id.size() + 7;
            if (room <= 0) {
                ids << QStringLiteral("…");
                break;
            }
            ids << QStringLiteral("test:") + id;
        }
        lines << QStringLiteral("Failing on screen: %1").arg(ids.join(QStringLiteral(", ")));
    }
    return lines.join(QLatin1Char('\n')).left(kScreenLimit);
}

QList<Action> TestsContext::actions() const
{
    const TestsState s = state ? state() : TestsState();
    const bool selected = !s.selectedId.isEmpty();
    return {
        action(QStringLiteral("testsRunSelected"), QStringLiteral("r"), QStringLiteral("Run selected"),
               QStringLiteral("Run the selected test, as Enter in the table does"),
               selected && !s.running, runSelected),
        action(QStringLiteral("testsRunFailed"), QStringLiteral("f"), QStringLiteral("Run failed"),
               QStringLiteral("Run every failing test the filter leaves on screen"),
               !s.failingIds.isEmpty() && !s.running, runFailed),
        action(QStringLiteral("testsAttach"), QStringLiteral("a"), QStringLiteral("Attach to card"),
               QStringLiteral("Name the selected test in a card's ## Tests section"), selected, attachToCard),
    };
}

bool TestsContext::resolveLink(const relay::links::Target &target)
{
    const QString id = relay::links::testIdOf(target.target);
    if (id.isEmpty())
        return false;
    // A `test:` link is this pane's to show; one naming a test it does not have is still claimed,
    // so it does not travel on to a handler that would read `relay://test/…` as a web address.
    if (selectTest)
        selectTest(id);
    return true;
}

QString TestsContext::placeholder() const
{
    return QStringLiteral("Ask the Tests agent — why did it fail, what is flaky, which card names it");
}

// ----- Sharing -------------------------------------------------------------------------------------

ContextSpec SharingContext::spec() const { return systemSpec(QStringLiteral("sharing"), title(), screen()); }

QString SharingContext::screen() const
{
    if (!state)
        return QStringLiteral("Sharing");
    const SharingState s = state();
    QStringList lines;
    lines << QStringLiteral("Sharing › %1").arg(s.page.isEmpty() ? QStringLiteral("People") : s.page);
    if (!s.remote.isEmpty())
        lines << s.remote;
    if (!s.currentToken.isEmpty())
        lines << QStringLiteral("Opened from: pane:%1 (%2)%3")
                     .arg(s.currentToken, s.currentTitle,
                          s.currentShared ? QStringLiteral(", shared") : QStringLiteral(", not shared"));
    if (s.devices.isEmpty())
        lines << QStringLiteral("Paired devices: none");
    else
        lines << QStringLiteral("Paired devices: %1").arg(s.devices.join(QStringLiteral("; ")));
    if (s.shared.isEmpty()) {
        lines << QStringLiteral("Shared panes: none");
    } else {
        lines << QStringLiteral("Shared panes:");
        for (const SharingState::Shared &pane : s.shared) {
            QStringList facts;
            if (!pane.scope.isEmpty() && pane.scope != QStringLiteral("pane"))
                facts << QStringLiteral("shared with its %1").arg(pane.scope);
            facts << (pane.guests == 1 ? QStringLiteral("1 guest") : QStringLiteral("%1 guests").arg(pane.guests));
            if (!pane.driver.isEmpty())
                facts << QStringLiteral("%1 is typing").arg(pane.driver);
            if (pane.waiting > 0)
                facts << QStringLiteral("%1 waiting").arg(pane.waiting);
            if (pane.paused)
                facts << QStringLiteral("paused");
            lines << QStringLiteral("- pane:%1 (%2) · %3").arg(pane.token, pane.title, facts.join(QStringLiteral(" · ")));
        }
    }
    if (!s.guests.isEmpty())
        lines << QStringLiteral("Guests: %1").arg(s.guests.join(QStringLiteral("; ")));
    if (s.waiting > 0)
        lines << QStringLiteral("Waiting for you: %1").arg(s.waiting);
    return lines.join(QLatin1Char('\n')).left(kScreenLimit);
}

QList<Action> SharingContext::actions() const
{
    const SharingState s = state ? state() : SharingState();
    return {
        action(QStringLiteral("sharingPair"), QStringLiteral("p"), QStringLiteral("Pair device"),
               QStringLiteral("A code to type on your phone, or a QR to scan — the Devices page's "
                              "\"Add a device…\""),
               true, pairDevice),
        // Leaves nothing to undo — it disconnects everyone on the pane and burns its links — so it
        // is offered only while the pane the Sharing pane is on is actually shared.
        action(QStringLiteral("sharingStop"), QStringLiteral("e"), QStringLiteral("End sharing"),
               QStringLiteral("Disconnect everyone on this pane and burn its invite links"),
               s.currentShared, stopSharing),
    };
}

bool SharingContext::resolveLink(const relay::links::Target &target)
{
    const QString token = relay::links::paneTokenOf(target.target);
    if (token.isEmpty())
        return false;
    if (focusPane)
        focusPane(token);
    return true;
}

QString SharingContext::placeholder() const
{
    return QStringLiteral("Ask the Sharing agent — who can see this pane, how do I pair my phone");
}

}  // namespace relay::agent
