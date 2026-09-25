// SPDX-License-Identifier: AGPL-3.0-or-later
// Card #8ABD — shared state-based waits for the console-mode cases.
//
// Included first of the cases headers by tests/consolemode_test.cpp. Every wait pumps the event
// loop in 25 ms steps until a pane *state* holds, with a deadline, and reports a diagnostic naming
// what it waited for on timeout — the case headers must not hand-roll
// `for (int i = 0; i < N; ++i) xcxdPump(25)` loops. Waits that read the pane's foreground fact call
// Pane::pollPaneStatusNow() once per step, so a wait ends when the fact is readable rather than on
// the m_programPoll timer's beat.

#include <QLabel>
#include <QToolButton>
#include <QWidget>

namespace cases {
namespace {

bool xcxdHasButton(const Pane &pane, const QString &prefix)
{
    for (QToolButton *button : pane.findChildren<QToolButton *>())
        if (!button->isHidden() && button->text().startsWith(prefix))
            return true;
    return false;
}

void xcxdPump(int msecs)
{
    for (int left = msecs; left > 0; left -= 25) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(25);
    }
}

// The reads the named waits wait on. Keep them here, not in the case headers, so the wait and its
// predicate cannot drift apart.
QString paneBusyText(const Pane &pane)
{
    if (auto *line = pane.findChild<QWidget *>(QStringLiteral("paneBusyLine")))
        return line->accessibleName();
    return QString();
}
QString paneToastText(const Pane &pane)
{
    if (auto *toast = pane.findChild<QLabel *>(QStringLiteral("toast")))
        return toast->text();
    return QString();
}

// Pumps 25 ms at a time until `predicate` holds. On timeout prints a diagnostic naming `what` and
// the deadline and returns false — callers wrap this in CHECK so the failure counts and the case's
// call site is printed too.
template <typename F>
bool waitUntil(F &&predicate, const QString &what, int deadlineMs = 5000)
{
    if (predicate())
        return true;
    for (int elapsed = 0; elapsed < deadlineMs; elapsed += 25) {
        xcxdPump(25);
        if (predicate())
            return true;
    }
    std::fprintf(stderr, "FAIL waitUntil: %s not true after %d ms\n", qPrintable(what), deadlineMs);
    return false;
}

// Like waitUntil, but polls the pane's foreground fact first every step (card #8ABD, plan step 4).
template <typename F>
bool waitForPaneState(Pane &pane, F &&predicate, const QString &what, int deadlineMs = 5000)
{
    return waitUntil(
        [&] {
            pane.pollPaneStatusNow();
            return predicate();
        },
        what, deadlineMs);
}

bool waitForStopButton(Pane &pane, const QString &prefix, bool gone = false, int deadlineMs = 5000)
{
    return waitForPaneState(
        pane, [&] { return xcxdHasButton(pane, prefix) == !gone; },
        QStringLiteral("stop button \"%1\" %2").arg(prefix, gone ? QStringLiteral("gone")
                                                               : QStringLiteral("shown")),
        deadlineMs);
}

bool waitForStripEmpty(Pane &pane, int deadlineMs = 5000)
{
    return waitForStopButton(pane, QStringLiteral("Stop shell ("), true, deadlineMs);
}

bool waitForBusyText(Pane &pane, const QString &substring, int deadlineMs = 5000)
{
    return waitForPaneState(
        pane, [&] { return paneBusyText(pane).contains(substring); },
        QStringLiteral("busy text containing \"%1\"").arg(substring), deadlineMs);
}

bool waitForToast(Pane &pane, const QString &substring, int deadlineMs = 5000)
{
    return waitUntil([&] { return paneToastText(pane).contains(substring); },
                     QStringLiteral("toast containing \"%1\"").arg(substring), deadlineMs);
}

bool waitForProcessBusy(Pane &pane, bool busy, int deadlineMs = 5000)
{
    return waitForPaneState(
        pane, [&] { return pane.processBusy() == busy; },
        busy ? QStringLiteral("pane reporting a foreground program")
             : QStringLiteral("pane back at the prompt"),
        deadlineMs);
}

} // namespace
} // namespace cases
