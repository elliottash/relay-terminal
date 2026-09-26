// SPDX-License-Identifier: AGPL-3.0-or-later
// An artifact's agent popped out into a linked shell pane, and docked back (card #2FQ9).
//
// The owner, 2026-09-25: "you can pop out the artifact agent and it goes into a side-by-side
// separate connected shell pane. so that agent is working on the artifact but has access to the
// shell as well." What moves is the console widget itself. It stays in `m_consoles`, attached to
// its tab's worker, wrapped around the same host context — so the conversation, a card's
// `board_ask` routing and its thread entries are the same before, during and after — and it gains
// a pty on the vterm that already draws its transcript (`Pane::setLinkedShell`). The widgets that
// make the link visible are in src/LinkedAgent.h.
#include "RelayWindow.h"
#include "LinkedAgent.h"

namespace {

relay::LinkedAgentView *linkedViewOfConsole(const Pane *console) {
    return console ? dynamic_cast<relay::LinkedAgentView *>(console->parentWidget()) : nullptr;
}

relay::LinkedAgentView *linkedViewOfLeaf(QWidget *leaf) {
    auto *tool = dynamic_cast<ToolPane *>(leaf);
    return tool && tool->kind() == ToolPane::Kind::LinkedAgent ? dynamic_cast<relay::LinkedAgentView *>(tool->hosted())
                                                               : nullptr;
}

}  // namespace

bool RelayWindow::toggleLinkedConsole(Pane *console) {
    if (!console) return false;
    return linkedViewOfConsole(console) ? dockConsole(console) : popOutConsole(console);
}

Pane *RelayWindow::linkedConsoleOfLeaf(QWidget *leaf) const {
    relay::LinkedAgentView *view = linkedViewOfLeaf(leaf);
    return view && view->holdsConsole() ? dynamic_cast<Pane *>(view->console()) : nullptr;
}

bool RelayWindow::hostHasLinkedConsole(QWidget *host) const {
    if (!host) return false;
    for (const ConsoleEntry &entry : m_consoles)
        if (relay::LinkedAgentView *view = linkedViewOfConsole(entry.pane.data()); view && view->host() == host)
            return true;
    return false;
}

void RelayWindow::dockLinkedConsolesOf(QWidget *host) {
    if (!host) return;
    QList<QPointer<Pane>> linked;
    for (const ConsoleEntry &entry : m_consoles)
        if (relay::LinkedAgentView *view = linkedViewOfConsole(entry.pane.data()); view && view->host() == host)
            linked << entry.pane;
    for (const QPointer<Pane> &console : std::as_const(linked))
        if (console) dockConsole(console);
}

Pane *RelayWindow::artifactConsoleOf(QWidget *leaf, bool create) {
    auto *tool = dynamic_cast<ToolPane *>(leaf);
    if (!tool) return nullptr;
    if (tool->kind() == ToolPane::Kind::LinkedAgent) return linkedConsoleOfLeaf(tool);
    const auto live = [this](QWidget *widget) -> Pane * {
        for (const ConsoleEntry &entry : m_consoles)
            if (widget && entry.pane.data() == widget) return entry.pane.data();
        return nullptr;
    };
    // A file editor or a plan: the agent docked at its foot, built on first expand (#PBZ4).
    if (relay::ArtifactDock *dock = tool->preview() ? tool->preview()->artifactDock()
                                    : tool->plan()   ? tool->plan()->artifactDock() : nullptr) {
        if (!dock->agentConsole() && create) dock->focusHelper();
        return live(dock->agentConsole().widget);
    }
    // A card page — a Card pane, or the card open in the Board's list pane: the console on the
    // card's surface, wherever it stands right now. The page builds it when the card opens.
    if (tool->board()) {
        for (const ConsoleEntry &entry : m_consoles) {
            Pane *pane = entry.pane.data();
            if (!pane || !entry.context) continue;
            relay::LinkedAgentView *view = linkedViewOfConsole(pane);
            const bool here = view ? view->host() == tool : hostLeafOf(pane) == tool;
            if (here && entry.context->spec().surface.startsWith(QStringLiteral("card:"))) return pane;
        }
    }
    return nullptr;
}

bool RelayWindow::popOutConsole(Pane *console) {
    if (!console || linkedViewOfConsole(console)) return false;
    QWidget *host = hostLeafOf(console);
    QWidget *slotParent = console->parentWidget();
    if (!host || !slotParent || !pageOf(host)) {
        notice(QStringLiteral("This agent is not docked in an artifact, so there is nothing to pop it out of."), 5000);
        return false;
    }
    // Where the console sits in its host: a splitter's child, or an item of a box layout under
    // its parent (a card page nests it in a layout of its own). Found before anything moves.
    auto *splitter = qobject_cast<QSplitter *>(slotParent);
    int index = -1;
    QBoxLayout *box = splitter ? nullptr : relay::boxLayoutHolding(slotParent->layout(), console, &index);
    if (!splitter && (!box || index < 0)) {
        notice(QStringLiteral("This agent's place in its pane could not be found, so it stays docked."), 5000);
        return false;
    }
    relay::LinkedAgentView::Slot slot;
    slot.minimum = console->minimumSize();
    slot.maximum = console->maximumSize();
    slot.policy = console->sizePolicy();
    if (box) slot.stretch = box->stretch(index);
    // The shell first: a console whose terminal surface is not built yet has nothing to run one
    // on, and nothing has moved when it says so.
    const QString cwd = leafCwd(host);
    if (!console->setLinkedShell(true, cwd)) return false;
    auto *placeholder = new relay::LinkedAgentPlaceholder;
    slot.placeholder = placeholder;
    if (splitter) {
        const QList<int> sizes = splitter->sizes();
        splitter->replaceWidget(splitter->indexOf(console), placeholder);
        splitter->setSizes(sizes);
    } else {
        box->insertWidget(index, placeholder, 0);
        box->removeWidget(console);
    }
    placeholder->show();
    // What the bar calls it: the file's name, or the card's id — a Board pane's own title is the
    // board's, and the agent is the card's.
    auto *hostTool = dynamic_cast<ToolPane *>(host);
    QString title = hostTool ? hostTool->title() : QString();
    for (const ConsoleEntry &entry : m_consoles)
        if (entry.pane.data() == console && entry.context)
            if (const QString surface = entry.context->spec().surface; surface.startsWith(QStringLiteral("card:")))
                title = QLatin1Char('#') + surface.mid(5);
    auto *view = new relay::LinkedAgentView(console, host, title, slot);
    auto *tool = new ToolPane(ToolPane::Kind::LinkedAgent, view, view, cwd);
    tool->setProperty("linkedAgentHost", QVariant::fromValue<QObject *>(host));
    relay::theme::polishWindow(tool);
    QPointer<Pane> guard(console);
    QPointer<ToolPane> toolGuard(tool);
    const auto dockBack = [guard] { if (auto *w = windowOf(guard)) w->dockConsole(guard); };
    view->onDockBack = dockBack;
    placeholder->onDockBack = dockBack;
    view->onFocus = [guard] { if (guard) guard->focusComposer(); };
    placeholder->onShow = [toolGuard, guard] {
        auto *w = windowOf(toolGuard);
        if (!w) return;
        w->setActiveLeaf(toolGuard);
        focusLeaf(toolGuard);
        if (guard) guard->focusComposer();
    };
    // `exit` in the linked shell sends the agent home.
    console->onLinkedShellEnded = dockBack;
    // The host owns the context this console is wrapped around, so the console cannot outlive
    // it: a host destroyed while its agent is out (a tab closing around it) takes the console
    // with it, synchronously, before the host's own destructor frees the context. Dock back
    // disconnects this before the placeholder goes (LinkedAgentView::putBack).
    QObject::connect(placeholder, &QObject::destroyed, console, [guard] { delete guard.data(); });
    // …and a console gone for any reason takes its now empty leaf with it.
    QObject::connect(console, &QObject::destroyed, tool, [toolGuard] {
        QTimer::singleShot(0, toolGuard, [toolGuard] {
            if (auto *w = windowOf(toolGuard); w && toolGuard) w->closePane(toolGuard, false);
        });
    });
    dockBeside(host, tool);
    console->show();
    for (const ConsoleEntry &entry : m_consoles)
        if (entry.pane.data() == console && entry.context && entry.context->host()
            && entry.context->host()->onLinkChanged)
            entry.context->host()->onLinkChanged(true);
    setActiveLeaf(tool);
    focusLeaf(tool);
    QTimer::singleShot(0, console, [guard] { if (guard) guard->focusComposer(); });
    updateTitles();
    m_manager->scheduleSave();
    return true;
}

bool RelayWindow::dockConsole(Pane *console) {
    relay::LinkedAgentView *view = linkedViewOfConsole(console);
    if (!view) return false;
    QWidget *tool = hostLeafOf(console);   // the linked leaf, while the console is in it
    QPointer<QWidget> host(view->host());
    // A program running in the shell would end with it: ask, as closing a busy terminal does.
    if (console->linkedShell() && console->processBusy()) {
        const auto choice = QMessageBox::question(this, QStringLiteral("Dock the agent back"),
            QStringLiteral("A command is still running in the linked shell. Docking the agent back ends it. Dock back anyway?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (choice != QMessageBox::Yes || !linkedViewOfConsole(console)) return false;
    }
    console->onLinkedShellEnded = nullptr;
    console->setLinkedShell(false);
    view->putBack();
    for (const ConsoleEntry &entry : m_consoles)
        if (entry.pane.data() == console && entry.context && entry.context->host()
            && entry.context->host()->onLinkChanged)
            entry.context->host()->onLinkChanged(false);
    // The leaf holds no console now, so closing it is an ordinary close, not another dock.
    if (tool && dynamic_cast<ToolPane *>(tool) && linkedViewOfLeaf(tool)) closePane(tool, false);
    if (host && pageOf(host)) {
        setActiveLeaf(host);
        focusLeaf(host);
        QPointer<Pane> guard(console);
        QTimer::singleShot(0, console, [guard] { if (guard) guard->focusComposer(); });
    }
    updateTitles();
    m_manager->scheduleSave();
    return true;
}

void RelayWindow::toggleLinkedConsoleOfActiveLeaf() {
    // The console with the keyboard first — a card page's reply box, a file agent's composer, or
    // the linked leaf itself — then the active leaf's artifact agent, built if it has none yet.
    if (QWidget *focus = QApplication::focusWidget()) {
        for (const ConsoleEntry &entry : m_consoles) {
            Pane *pane = entry.pane.data();
            if (pane && (pane == focus || pane->isAncestorOf(focus)) && pageOf(pane)) {
                toggleLinkedConsole(pane);
                return;
            }
        }
    }
    if (Pane *console = artifactConsoleOf(m_activeLeaf, true)) {
        toggleLinkedConsole(console);
        return;
    }
    notice(QStringLiteral("No artifact agent here. Open a card or a file, then pop its agent out."), 5000);
}

void RelayWindow::relinkRestoredConsole(QWidget *host, int attempt) {
    if (!host || hostHasLinkedConsole(host)) return;
    // A card's page is built when the tab's worker has sent the rows, and a console's terminal
    // surface a moment after the console: wait for both, a few seconds at most, and leave the
    // agent docked — where it is safe and still has its conversation — if they never come.
    Pane *console = artifactConsoleOf(host, true);
    if (console && console->hasTerminalSurface()) {
        popOutConsole(console);
        return;
    }
    if (attempt >= 40) return;
    QPointer<QWidget> guard(host);
    QTimer::singleShot(250, this, [this, guard, attempt] { if (guard) relinkRestoredConsole(guard, attempt + 1); });
}
