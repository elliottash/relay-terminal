// Card #H2KQ: Esc/Alt+Esc stop semantics, the retired program bubble, Take over beside the
// Relaying line, and the agent always driving. Shell cases use a real pty, like the XCXD UI
// cases whose helpers this reuses. Run alone: relay-consolemode-tests --h2kq-only.

namespace cases {

QWidget *h2kqBusyLine(Pane &pane)
{
    return pane.findChild<QWidget *>(QStringLiteral("paneBusyLine"));
}

QString h2kqBusyText(Pane &pane)
{
    QWidget *line = h2kqBusyLine(pane);
    return line ? line->accessibleName() : QString();
}

QToolButton *h2kqBusyAction(Pane &pane)
{
    return pane.findChild<QToolButton *>(QStringLiteral("busyAction"));
}

bool h2kqStripStopGone(Pane &pane)
{
    return !xcxdHasButton(pane, QStringLiteral("Stop shell ("));
}

void h2kqRun(Pane &pane, const QString &command, const char *name)
{
    CHECK(xcxdEditor(pane) != nullptr);
    pane.draftInComposer(command);
    if (auto *editor = xcxdEditor(pane)) {
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
        QCoreApplication::sendEvent(editor, &enter);
    }
    for (int i = 0; i < 100 && !xcxdHasButton(pane, QStringLiteral("Stop shell (")); ++i)
        xcxdPump(25);
    if (!xcxdHasButton(pane, QStringLiteral("Stop shell (")))
        std::fprintf(stderr, "FAIL h2kq %s: the shell never reported busy\n", name);
}

void h2kqKey(Pane &pane, int key, Qt::KeyboardModifiers mods)
{
    if (auto *editor = xcxdEditor(pane)) {
        QKeyEvent press(QEvent::KeyPress, key, mods);
        QCoreApplication::sendEvent(editor, &press);
    }
    xcxdPump(10);
}

void h2kqCases()
{
    // ----- a shell command: Esc stops it, and both surfaces say Esc ---------------------------
    {
        QTemporaryDir homeDir;
        XcxdShellContext ctx;
        ctx.workspace = homeDir.path();
        Pane pane(ctx.workspace, ctx.workspace, true, relay::defaultEngineCore(), &ctx);
        pane.show();
        pane.resize(900, 650);
        xcxdPump(50);
        h2kqRun(pane, QStringLiteral("sleep 20"), "esc-label");
        // The name resolves on the shell poll, a beat after the line itself appears.
        for (int i = 0; i < 100 && !h2kqBusyText(pane).contains(QStringLiteral("sleep… · Esc stops")); ++i)
            xcxdPump(25);
        // The Relaying line names the program and its key (card #H2KQ): sleep's key is Esc.
        CHECK(h2kqBusyText(pane).contains(QStringLiteral("sleep")));
        CHECK(h2kqBusyText(pane).contains(QStringLiteral("Esc stops")));
        CHECK(!h2kqBusyText(pane).contains(QStringLiteral("Alt+Esc")));
        for (int i = 0; i < 100 && !xcxdHasButton(pane, QStringLiteral("Stop shell (Esc)")); ++i)
            xcxdPump(25);
        CHECK(xcxdHasButton(pane, QStringLiteral("Stop shell (Esc)")));
        // Esc stops the command.
        h2kqKey(pane, Qt::Key_Escape, Qt::NoModifier);
        for (int i = 0; i < 100 && !h2kqStripStopGone(pane); ++i)
            xcxdPump(25);
        CHECK(h2kqStripStopGone(pane));
    }

    // ----- a full-screen program: it keeps Esc, Alt+Esc closes it -----------------------------
    {
        QTemporaryDir homeDir;
        XcxdShellContext ctx;
        ctx.workspace = homeDir.path();
        Pane pane(ctx.workspace, ctx.workspace, true, relay::defaultEngineCore(), &ctx);
        pane.show();
        pane.resize(900, 650);
        xcxdPump(50);
        h2kqRun(pane, QStringLiteral("printf '\\033[?1049h'; sleep 20"), "altscreen");
        // Wait for the alternate screen: the line's key becomes Alt+Esc.
        for (int i = 0; i < 100 && !h2kqBusyText(pane).contains(QStringLiteral("Alt+Esc stops")); ++i)
            xcxdPump(25);
        CHECK(h2kqBusyText(pane).contains(QStringLiteral("Alt+Esc stops")));
        for (int i = 0; i < 100 && !xcxdHasButton(pane, QStringLiteral("Stop shell (Alt+Esc)")); ++i)
            xcxdPump(25);
        CHECK(xcxdHasButton(pane, QStringLiteral("Stop shell (Alt+Esc)")));
        // Esc is the program's own now: it must not interrupt.
        h2kqKey(pane, Qt::Key_Escape, Qt::NoModifier);
        xcxdPump(200);
        CHECK(!h2kqStripStopGone(pane));
        // Alt+Esc closes it (first press: Ctrl+C, which ends sleep).
        h2kqKey(pane, Qt::Key_Escape, Qt::AltModifier);
        for (int i = 0; i < 100 && !h2kqStripStopGone(pane); ++i)
            xcxdPump(25);
        CHECK(h2kqStripStopGone(pane));
    }

    // ----- the top-right bubble is retired ----------------------------------------------------
    {
        QTemporaryDir homeDir;
        XcxdShellContext ctx;
        ctx.workspace = homeDir.path();
        Pane pane(ctx.workspace, ctx.workspace, true, relay::defaultEngineCore(), &ctx);
        pane.show();
        pane.resize(900, 650);
        xcxdPump(50);
        h2kqRun(pane, QStringLiteral("sleep 20"), "bubble");
        xcxdPump(100);
        if (auto *bar = pane.findChild<QWidget *>(QStringLiteral("takeControl")))
            CHECK(bar->isHidden());
        h2kqKey(pane, Qt::Key_Escape, Qt::NoModifier);
    }

    // ----- the agent always drives: Take over appears without a click --------------------------
    {
        QTemporaryDir homeDir;
        XcxdShellContext ctx;
        ctx.workspace = homeDir.path();
        Pane pane(ctx.workspace, ctx.workspace, true, relay::defaultEngineCore(), &ctx);
        pane.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
        pane.show();
        pane.resize(900, 650);
        xcxdPump(50);
        h2kqRun(pane, QStringLiteral("sleep 20"), "auto-delegate");
        // No "Let the agent drive" anywhere: the hand-off already happened.
        CHECK(!xcxdHasButton(pane, QStringLiteral("Let the agent drive")));
        auto *action = h2kqBusyAction(pane);
        for (int i = 0; i < 100 && !(action && action->isVisible()); ++i) {
            xcxdPump(25);
            action = h2kqBusyAction(pane);
        }
        CHECK(action != nullptr);
        CHECK(action && action->isVisible());
        CHECK(action && action->text().startsWith(QStringLiteral("Take over")));
        // Taking over hands the keyboard back and the button goes with it.
        if (action && action->isVisible()) {
            action->click();
            xcxdPump(50);
            CHECK(!action->isVisible());
        }
        // Clean up: leave native mode and stop the sleep.
        h2kqKey(pane, Qt::Key_Escape, Qt::AltModifier);
        xcxdPump(50);
    }

    // ----- a pane with no agent configured offers Take control, not Take over -------------------
    {
        QTemporaryDir homeDir;
        XcxdShellContext ctx;
        ctx.workspace = homeDir.path();
        Pane pane(ctx.workspace, ctx.workspace, true, relay::defaultEngineCore(), &ctx);
        pane.show();
        pane.resize(900, 650);
        xcxdPump(50);
        h2kqRun(pane, QStringLiteral("printf '\\033[?1049h'; sleep 20"), "take-control");
        auto *action = h2kqBusyAction(pane);
        for (int i = 0; i < 100 && !(action && action->isVisible()); ++i) {
            xcxdPump(25);
            action = h2kqBusyAction(pane);
        }
        // No agent to take over from; a full-screen program offers Take control instead.
        CHECK(!xcxdHasButton(pane, QStringLiteral("Take over")));
        CHECK(action && action->isVisible());
        CHECK(action && action->text().startsWith(QStringLiteral("Take control")));
        h2kqKey(pane, Qt::Key_Escape, Qt::AltModifier);
        xcxdPump(50);
    }
}

void h2kqQueueLabelCases()
{
    // ----- no "Agent · N" / "Terminal · N" labels (owner, 2026-09-24: it is obvious from the
    // rows); a paused lane says just "paused" next to its Resume --------------------------------
    {
        QTemporaryDir homeDir;
        StubContext ctx;
        ctx.workspace = homeDir.path();
        Pane pane(ctx.workspace, ctx.workspace, false, relay::defaultEngineCore(), &ctx);
        pane.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
        pane.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
        pane.initRestore(QJsonObject{{"queue", xcxdMixedQueue()}});
        pane.show();
        pane.resize(900, 650);
        xcxdPump(100);
        CHECK_EQ(pane.queuedPrompts(), 3);   // the rows are there, labeled only by themselves
        for (QLabel *label : pane.findChildren<QLabel *>()) {
            CHECK(!label->text().startsWith(QStringLiteral("Agent ·")));
            CHECK(!label->text().startsWith(QStringLiteral("Terminal ·")));
        }
        CHECK(xcxdLaneButton(pane, true, QStringLiteral("Resume")) != nullptr);   // the paused lane keeps its Resume
        auto *paused = pane.findChild<QLabel *>(QStringLiteral("agentLaneLabel"));
        CHECK(paused != nullptr);
        if (paused) CHECK_EQ(paused->text(), QStringLiteral("paused"));
    }
}

} // namespace cases
