// SPDX-License-Identifier: AGPL-3.0-or-later
// Card #XCXD — two-queue UI and interrupt UX (UI contract half).
//
// Included by tests/consolemode_test.cpp after its `namespace cases` closes and after that file's
// CHECK/CHECK_EQ macros and StubContext; called from the default run and from `--xcxd-only`
// (ctest name queuecontract, wired by the parent). Deterministic local events only — no provider.
//
// Covered:
//   · Esc is not queue navigation: plain/Alt/Shift Esc return None from relay::queuenav::decide
//     while a row is selected (the pane's stop decision owns the key, the edited text survives),
//     Down still leaves the last row and Enter still saves.
//   · A mixed restored queue (explicit agent rows + a shell command row) renders and stays paused.
//   · The stop matrix on a real pane: Esc stops the agent when both run (shell keeps running),
//     Alt+Esc interrupts the shell alone, Esc interrupts the shell when the shell is all that
//     runs — from a non-empty draft, which survives — and Esc does nothing when nothing runs.
//     Autorepeat Esc is swallowed: no stop message at all.
//   · A blank Enter skips an open question (empty answers sent, no queue action behind it) and
//     never skips an approval.
//   · Evidence shots into RELAY_XCXD_EVIDENCE_DIR as queue-wide.png / queue-narrow.png
//     (dual waiting queues + both stop controls) and queue-shell-only.png. They render restored
//     fixtures — UI evidence, not proof of an external agent having run.

#include <QCoreApplication>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>

#include <climits>

namespace cases {

namespace {
// The console tests' context has no shell (switchboard shape). The shell half of the card needs
// the real one, with terminal routing, so the sleep fixture is an honest running program.
class XcxdShellContext final : public StubContext {
public:
    relay::agent::ContextSpec spec() const override
    {
        relay::agent::ContextSpec spec = StubContext::spec();
        spec.shell = true;
        spec.routing = QStringLiteral("auto");
        return spec;
    }
};

bool xcxdHasButton(const Pane &pane, const QString &prefix)
{
    for (QToolButton *button : pane.findChildren<QToolButton *>())
        if (!button->isHidden() && button->text().startsWith(prefix))
            return true;
    return false;
}
bool xcxdHasLaneLabel(const Pane &pane)
{
    for (QLabel *label : pane.findChildren<QLabel *>())
        if (!label->isHidden() && (label->objectName() == QStringLiteral("agentLaneLabel")
                                   || label->objectName() == QStringLiteral("terminalLaneLabel")))
            return true;
    return false;
}
QPlainTextEdit *xcxdEditor(Pane &pane)
{
    return pane.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
}
void xcxdEnter(Pane &pane)
{
    if (auto *editor = xcxdEditor(pane)) {
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &enter);
    }
}
void xcxdEsc(Pane &pane, Qt::KeyboardModifiers mods = Qt::NoModifier, bool autorepeat = false)
{
    if (auto *editor = xcxdEditor(pane)) {
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, mods, QString(), autorepeat, 1);
        QCoreApplication::sendEvent(editor, &esc);
    }
}
void xcxdPump(int msecs)
{
    for (int left = msecs; left > 0; left -= 25) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(25);
    }
}
// Sends `sleep <secs>` to the terminal and waits for the shell's stop control to appear — the
// new visibility rule made the control itself the busy signal. Fails rather than skips.
void xcxdRunSleep(Pane &pane, const char *name)
{
    CHECK(xcxdEditor(pane) != nullptr);
    pane.draftInComposer(QStringLiteral("sleep 20"));
    if (auto *editor = xcxdEditor(pane)) {
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
        QCoreApplication::sendEvent(editor, &enter);
    }
    for (int i = 0; i < 100 && !xcxdHasButton(pane, QStringLiteral("Stop shell (")); ++i)
        xcxdPump(25);
    CHECK(xcxdHasButton(pane, QStringLiteral("Stop shell (")));
    if (!xcxdHasButton(pane, QStringLiteral("Stop shell (")))
        std::fprintf(stderr, "FAIL xcxd %s: the shell never reported busy (sleep %s)\n", name, name);
}
void xcxdShot(Pane &pane, const QString &path)
{
    // Let the strip's rebuild finish first: the old header widgets go by deleteLater, and the
    // strip is placed on a zero-length timer after a resize.
    xcxdPump(200);
    CHECK(pane.grab().save(path));
}
QJsonArray xcxdMixedQueue()
{
    return QJsonArray{
        QJsonObject{{QStringLiteral("text"), QStringLiteral("Fix the failing build")},
                    {QStringLiteral("agent"), true}},
        QJsonObject{{QStringLiteral("text"), QStringLiteral("Then run the tests again")},
                    {QStringLiteral("agent"), true}},
        QJsonObject{{QStringLiteral("text"), QStringLiteral("printf queued-shell")},
                    {QStringLiteral("agent"), false}},
    };
}
QListWidget *xcxdLaneList(const Pane &pane, bool agentLane)
{
    return pane.findChild<QListWidget *>(agentLane ? QStringLiteral("queueList")
                                                   : QStringLiteral("terminalQueueList"));
}
QToolButton *xcxdLaneButton(Pane &pane, bool agentLane, const QString &text)
{
    const QString name = text == QStringLiteral("Clear") ? QStringLiteral("clearAgentQueue")
        : agentLane ? QStringLiteral("resumeAgentQueue") : QStringLiteral("resumeTerminalQueue");
    return pane.findChild<QToolButton *>(name);
}
bool xcxdLanePaused(Pane &pane, bool agentLane)
{
    auto *label = pane.findChild<QLabel *>(agentLane ? QStringLiteral("agentLaneLabel")
                                                  : QStringLiteral("terminalLaneLabel"));
    return label && label->text().contains(QStringLiteral("paused"));
}
// Counts rows carrying a given RowIdRole — the product's own role enum, same one removal uses.
int xcxdRowIdCount(QListWidget *list, const QString &rowId)
{
    int count = 0;
    for (int row = 0; row < list->count(); ++row) {
        const QString id = list->item(row)->data(QueueRowDelegate::RowIdRole).toString();
        if (id == rowId) ++count;
    }
    return count;
}
}   // namespace

void xcxdUiCases()
{
    // ----- queue navigation: Esc belongs to the pane's stop decision now --------------------
    {
        using relay::queuenav::Action;
        using relay::queuenav::State;
        const auto decide = [](int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            State state; state.selected = 1; state.count = 3; state.promptEmpty = false;
            return relay::queuenav::decide(state, key, mods);
        };
        const auto lastRow = [](int key) {
            State state; state.selected = 2; state.count = 3; state.promptEmpty = false;
            return relay::queuenav::decide(state, key, Qt::NoModifier);
        };
        CHECK_EQ(decide(Qt::Key_Return), Action::Save);
        CHECK_EQ(decide(Qt::Key_Down), Action::Down);
        CHECK_EQ(lastRow(Qt::Key_Down), Action::LeaveToPrompt);   // Down is the way out of the queue
        CHECK_EQ(decide(Qt::Key_Escape), Action::None);
        CHECK_EQ(decide(Qt::Key_Escape, Qt::AltModifier), Action::None);   // Alt+Esc reaches the shell interrupt
        CHECK_EQ(decide(Qt::Key_Escape, Qt::ShiftModifier), Action::None);
    }

    // ----- sole agent: a restored mixed queue stays paused; Esc stops the turn, rows survive --
    QTemporaryDir agentHome;
    StubContext agentContext;
    agentContext.workspace = agentHome.path();
    Pane agentPane(agentContext.workspace, agentContext.workspace, false, relay::defaultEngineCore(), &agentContext);
    QList<QJsonObject> agentSent;
    agentPane.onWorkerLine = [&agentSent](const QJsonObject &message) { agentSent << message; };
    agentPane.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
    agentPane.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
    agentPane.initRestore(QJsonObject{{"queue", xcxdMixedQueue()}});
    CHECK_EQ(agentPane.queuedPrompts(), 3);
    CHECK(agentPane.queuePaused());
    agentSent.clear();
    xcxdEsc(agentPane);
    CHECK(std::any_of(agentSent.cbegin(), agentSent.cend(), [](const QJsonObject &message) {
        return message.value(QStringLiteral("type")).toString() == QStringLiteral("cancel");
    }));
    CHECK_EQ(agentPane.queuedPrompts(), 3);

    // ----- a busy agent with nothing queued: the Relaying line says it, the strip does not ---
    {
        QTemporaryDir busyHome;
        StubContext busyContext;
        busyContext.workspace = busyHome.path();
        Pane busy(busyContext.workspace, busyContext.workspace, false, relay::defaultEngineCore(), &busyContext);
        busy.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
        busy.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
        busy.show();
        xcxdPump(100);
        CHECK(!xcxdHasButton(busy, QStringLiteral("Stop agent (")));
        CHECK(!xcxdHasLaneLabel(busy));
        auto *strip = busy.findChild<QWidget *>(QStringLiteral("queueStrip"));
        CHECK(strip == nullptr || strip->isHidden());
    }

    // ----- both / autorepeat / blank Enter on a pane with a real shell ----------------------
    QTemporaryDir home;
    XcxdShellContext context;
    context.workspace = home.path();
    Pane console(context.workspace, context.workspace, true, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
    xcxdRunSleep(console, "both");
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
    console.initRestore(QJsonObject{{"queue", xcxdMixedQueue()}});
    console.show();
    for (int i = 0; i < 20 && !xcxdHasButton(console, QStringLiteral("Stop shell (")); ++i)
        xcxdPump(25);
    // The agent's stop is the Relaying line's "Esc stops"; the strip does not repeat it.
    CHECK(!xcxdHasButton(console, QStringLiteral("Stop agent (")));
    CHECK(xcxdHasButton(console, QStringLiteral("Stop shell (")));
    auto *agentList = console.findChild<QListWidget *>(QStringLiteral("queueList"));
    auto *shellList = console.findChild<QListWidget *>(QStringLiteral("terminalQueueList"));
    CHECK(agentList && shellList);
    if (agentList && shellList) {
        CHECK_EQ(agentList->count(), 2);
        CHECK_EQ(shellList->count(), 1);
        console.resize(1200, 650);
        xcxdPump(100);
        CHECK(shellList->mapTo(&console, QPoint()).x() > agentList->mapTo(&console, QPoint()).x());
        console.resize(480, 650);
        xcxdPump(100);
        CHECK(shellList->mapTo(&console, QPoint()).y() > agentList->mapTo(&console, QPoint()).y());
        CHECK_EQ(agentList->count(), 2);
        CHECK_EQ(shellList->count(), 1); // reflow/rebuild must not duplicate rows
    }
    CHECK(!xcxdHasButton(console, QStringLiteral("Stop agent (")));
    CHECK(xcxdHasButton(console, QStringLiteral("Stop shell (Alt+Esc)")));
    // Autorepeat is swallowed while both run: a held key must not stop anything.
    sent.clear();
    xcxdEsc(console, Qt::NoModifier, true);
    CHECK(sent.isEmpty());
    xcxdEsc(console, Qt::AltModifier, true);
    CHECK(sent.isEmpty());
    CHECK(xcxdHasButton(console, QStringLiteral("Stop shell (")));
    // Esc stops the agent only; the shell keeps running.
    sent.clear();
    xcxdEsc(console);
    CHECK(std::any_of(sent.cbegin(), sent.cend(), [](const QJsonObject &message) {
        return message.value(QStringLiteral("type")).toString() == QStringLiteral("cancel");
    }));
    CHECK(!std::any_of(sent.cbegin(), sent.cend(), [](const QJsonObject &message) {
        return message.value(QStringLiteral("type")).toString() == QStringLiteral("input");
    }));
    CHECK(xcxdHasButton(console, QStringLiteral("Stop shell (")));
    // A blank Enter skips an open question, never an approval, and leaves the queue alone.
    console.deliverWorkerEvent(QJsonObject{{"event", "question"},
                                           {"id", QStringLiteral("q1")},
                                           {"questions", QJsonArray{QJsonObject{
                                               {QStringLiteral("header"), QStringLiteral("Which build?")},
                                               {QStringLiteral("question"), QStringLiteral("Debug or release?")}}}}});
    sent.clear();
    xcxdEnter(console);   // the composer is blank
    CHECK(std::any_of(sent.cbegin(), sent.cend(), [](const QJsonObject &message) {
        return message.value(QStringLiteral("type")).toString() == QStringLiteral("question_answer")
               && message.value(QStringLiteral("id")).toString() == QStringLiteral("q1")
               && message.value(QStringLiteral("answers")).toArray() == QJsonArray{QJsonArray{}};
    }));
    console.deliverWorkerEvent(QJsonObject{{"event", "question"},
                                           {"id", QStringLiteral("q2")},
                                           {"kind", QStringLiteral("approval")},
                                           {"header", QStringLiteral("Run it?")},
                                           {"question", QStringLiteral("Allow the deploy?")},
                                           {"capability", QStringLiteral("deploy")},
                                           {"decisions", QJsonArray{QStringLiteral("allow"), QStringLiteral("deny")}}});
    sent.clear();
    xcxdEnter(console);
    CHECK(!std::any_of(sent.cbegin(), sent.cend(), [](const QJsonObject &message) {
        return message.value(QStringLiteral("type")).toString() == QStringLiteral("question_answer");
    }));
    CHECK(std::none_of(sent.cbegin(), sent.cend(), [](const QJsonObject &message) {
        const QString type = message.value(QStringLiteral("type")).toString();
        return type == QStringLiteral("ask") || type == QStringLiteral("resume_queue")
               || type == QStringLiteral("queue_unsteer");
    }));
    // Alt+Esc interrupts the shell alone, even while the agent runs.
    sent.clear();
    xcxdEsc(console, Qt::AltModifier);
    for (int i = 0; i < 100 && xcxdHasButton(console, QStringLiteral("Stop shell (")); ++i)
        xcxdPump(25);
    CHECK(!xcxdHasButton(console, QStringLiteral("Stop shell (")));
    CHECK(std::none_of(sent.cbegin(), sent.cend(), [](const QJsonObject &message) {
        return message.value(QStringLiteral("type")).toString() == QStringLiteral("cancel");
    }));

    // ----- sole shell, from a non-empty draft, which must survive --------------------------
    QTemporaryDir shellHome;
    XcxdShellContext shellContext;
    shellContext.workspace = shellHome.path();
    Pane shell(shellContext.workspace, shellContext.workspace, true, relay::defaultEngineCore(), &shellContext);
    shell.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
    xcxdRunSleep(shell, "sole");
    // A running shell with nothing waiting shows its stop and no empty "Terminal · 0" lane.
    xcxdPump(50);
    CHECK(!xcxdHasLaneLabel(shell));
    shell.draftInComposer(QStringLiteral("draft that must survive the stop"));
    xcxdEsc(shell);
    for (int i = 0; i < 100 && xcxdHasButton(shell, QStringLiteral("Stop shell (")); ++i)
        xcxdPump(25);
    CHECK(!xcxdHasButton(shell, QStringLiteral("Stop shell (")));
    CHECK(xcxdEditor(shell) && xcxdEditor(shell)->toPlainText()
              == QStringLiteral("draft that must survive the stop"));
    // Nothing running, nothing queued: Esc sends nothing and keeps the draft.
    QList<QJsonObject> shellSent;
    shell.onWorkerLine = [&shellSent](const QJsonObject &message) { shellSent << message; };
    shellSent.clear();
    xcxdEsc(shell);
    CHECK(shellSent.isEmpty());
    CHECK(xcxdEditor(shell) && xcxdEditor(shell)->toPlainText()
              == QStringLiteral("draft that must survive the stop"));

    // ----- a program typed at the prompt: the strip leaves when the program does ------------
    // The queue never ran this one, so nothing marks it active; the strip came up on
    // "running" and must still go down on "ready" (owner's report: empty strip stayed up).
    {
        QTemporaryDir typedHome;
        XcxdShellContext typedContext;
        typedContext.workspace = typedHome.path();
        Pane typed(typedContext.workspace, typedContext.workspace, true, relay::defaultEngineCore(), &typedContext);
        typed.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
        xcxdPump(100);
        typed.sendShellInput(QStringLiteral("sleep 2\n"));
        for (int i = 0; i < 200 && !xcxdHasButton(typed, QStringLiteral("Stop shell (")); ++i)
            xcxdPump(25);
        CHECK(xcxdHasButton(typed, QStringLiteral("Stop shell (")));
        // No Esc, nothing queued: sleep exits on its own and its stop control follows.
        for (int i = 0; i < 400 && xcxdHasButton(typed, QStringLiteral("Stop shell (")); ++i)
            xcxdPump(25);
        CHECK(!xcxdHasButton(typed, QStringLiteral("Stop shell (")));
    }

    // ----- lanes: separate resume/clear, the shell ×, and stable rebuilds -------------------
    {
        QTemporaryDir laneHome;
        StubContext laneContext;
        laneContext.workspace = laneHome.path();
        Pane lane(laneContext.workspace, laneContext.workspace, false, relay::defaultEngineCore(), &laneContext);
        QList<QJsonObject> laneSent;
        lane.onWorkerLine = [&laneSent](const QJsonObject &message) { laneSent << message; };
        lane.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
        lane.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
        lane.initRestore(QJsonObject{{"queue", xcxdMixedQueue()}});
        lane.show();
        lane.resize(1200, 650);
        xcxdPump(200);
        QListWidget *agentList = xcxdLaneList(lane, true);
        QListWidget *terminalList = xcxdLaneList(lane, false);
        CHECK(agentList != nullptr && terminalList != nullptr);
        if (agentList && terminalList) {
            CHECK_EQ(agentList->count(), 2);       // prompts for the agent lane
            CHECK_EQ(terminalList->count(), 1);    // the queued command in its own lane
            // Resume is per lane: the terminal's leaves the agent paused, and back.
            CHECK(xcxdLanePaused(lane, true) && xcxdLanePaused(lane, false));
            if (QToolButton *terminalResume = xcxdLaneButton(lane, false, QStringLiteral("Resume"))) {
                terminalResume->click();
                xcxdPump(100);
                CHECK(!xcxdLanePaused(lane, false));
                CHECK(xcxdLanePaused(lane, true));   // one resource resuming is not the other's
            } else {
                std::fprintf(stderr, "FAIL xcxd lanes: no terminal Resume button\n");
                CHECK(false);
            }
            if (QToolButton *agentResume = xcxdLaneButton(lane, true, QStringLiteral("Resume"))) {
                agentResume->click();
                xcxdPump(100);
                CHECK(!xcxdLanePaused(lane, true));
                CHECK(!xcxdLanePaused(lane, false));
            } else {
                std::fprintf(stderr, "FAIL xcxd lanes: no agent Resume button\n");
                CHECK(false);
            }
            // Clear belongs to the agent lane: its prompts go, the terminal's command stays.
            if (QToolButton *clear = xcxdLaneButton(lane, true, QStringLiteral("Clear"))) {
                clear->click();
                xcxdPump(100);
                agentList = xcxdLaneList(lane, true);
                terminalList = xcxdLaneList(lane, false);
                CHECK(agentList && agentList->count() == 0);
                CHECK(terminalList && terminalList->count() == 1);
            } else {
                std::fprintf(stderr, "FAIL xcxd lanes: no agent Clear button\n");
                CHECK(false);
            }
        }
        // The × on a terminal row removes that row alone (the viewport handler is common to both
        // lanes; a synthetic release at the right edge of the row is the click it answers).
        QTemporaryDir xHome;
        StubContext xContext;
        xContext.workspace = xHome.path();
        Pane xPane(xContext.workspace, xContext.workspace, false, relay::defaultEngineCore(), &xContext);
        xPane.onWorkerLine = [](const QJsonObject &) {};
        xPane.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
        xPane.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
        xPane.initRestore(QJsonObject{{"queue", xcxdMixedQueue()}});
        xPane.show();
        xPane.resize(1200, 650);
        xcxdPump(200);
        if (QListWidget *xTerminal = xcxdLaneList(xPane, false)) {
            CHECK_EQ(xTerminal->count(), 1);
            const QModelIndex row0 = xTerminal->model()->index(0, 0);
            const QRect rect = xTerminal->visualRect(row0);
            const QPoint atEdge(xTerminal->viewport()->width() - 10, rect.center().y());
            QMouseEvent xClick(QEvent::MouseButtonRelease, QPointF(atEdge),
                               QPointF(xTerminal->viewport()->mapToGlobal(atEdge)),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(xTerminal->viewport(), &xClick);
            xcxdPump(100);
            xTerminal = xcxdLaneList(xPane, false);
            CHECK(xTerminal && xTerminal->count() == 0);
            if (QListWidget *xAgent = xcxdLaneList(xPane, true)) CHECK_EQ(xAgent->count(), 2);
        } else {
            std::fprintf(stderr, "FAIL xcxd lanes: no terminal list for the × case\n");
            CHECK(false);
        }
        xPane.onWorkerLine = {};
        lane.onWorkerLine = {};
        // Repeated rebuilds across the wide/narrow boundary keep one row per id in each lane —
        // the reflow is deferred and non-reentrant, so no duplicated or dropped rows.
        for (int round = 0; round < 6; ++round) {
            xPane.resize(1200, 650);
            xcxdPump(60);
            xPane.resize(480, 650);
            xcxdPump(60);
        }
        if (QListWidget *xAgent = xcxdLaneList(xPane, true))
            for (int row = 0; row < xAgent->count(); ++row)
                CHECK_EQ(xcxdRowIdCount(xAgent, xAgent->item(row)->data(QueueRowDelegate::RowIdRole).toString()), 1);
        // Drag reordering per lane is not synthesized here: QListWidget's InternalMove needs a
        // real drag source this harness does not provide, and a mime-drop would test the model,
        // not the drag. Lane isolation is covered by the × and Clear cases above.
        std::fprintf(stderr, "note xcxd lanes: drag reordering per lane not synthesized (needs a real drag source)\n");
    }


    const QByteArray evidence = qgetenv("RELAY_XCXD_EVIDENCE_DIR");
    if (!evidence.isEmpty()) {
        QDir dir(QDir(QString::fromUtf8(evidence)).absolutePath());
        CHECK(dir.mkpath(QStringLiteral(".")));
        if (dir.exists()) {
            QTemporaryDir wideHome;
            XcxdShellContext wideContext;
            wideContext.workspace = wideHome.path();
            Pane wide(wideContext.workspace, wideContext.workspace, true, relay::defaultEngineCore(), &wideContext);
            wide.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
            xcxdRunSleep(wide, "wide");
            wide.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
            wide.initRestore(QJsonObject{{"queue", xcxdMixedQueue()}});
            wide.show();
            for (int i = 0; i < 20 && !xcxdHasButton(wide, QStringLiteral("Stop shell (")); ++i)
                xcxdPump(25);
            wide.resize(1200, 650);
            xcxdShot(wide, dir.filePath(QStringLiteral("queue-wide.png")));
            wide.resize(480, 650);
            xcxdShot(wide, dir.filePath(QStringLiteral("queue-narrow.png")));
            // Shell-only header: a running program, nothing queued in either lane. Its own pane:
            // the `shell` pane above was interrupted, which paused its terminal lane.
            QTemporaryDir soloHome;
            XcxdShellContext soloContext;
            soloContext.workspace = soloHome.path();
            Pane solo(soloContext.workspace, soloContext.workspace, true, relay::defaultEngineCore(), &soloContext);
            solo.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
            solo.show();
            xcxdRunSleep(solo, "shot");
            solo.resize(1200, 650);
            xcxdPump(100);
            xcxdShot(solo, dir.filePath(QStringLiteral("queue-shell-only.png")));
        }
    }
    agentPane.onWorkerLine = {};
    console.onWorkerLine = {};
    shell.onWorkerLine = {};
}

}   // namespace cases
