// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RelayWindow.h"

// ---- the Sharing pane (#W5N2, #SMDX) ------------------------------------------------------------
// One pane with two pages replaced the share window: Devices (remote control, the address, your
// paired phones, "Add a device…") and People (what is waiting, who is on which pane, the invite
// form with its scope picker). RemoteShare::attach() wires everything that needs only the sidecar;
// what needs panes — the scope catalogue, publishing a pane, a tab or everything before a link is
// made, the remote-control switch — is wired here, because only the window knows its tabs.

// `focus` is false when a knock opened this by itself: the pane appears and the bell rings, but
// the keyboard stays exactly where the owner left it (section 10.5, and the reason the device
// card puts Refuse on the default button).
ToolPane *RelayWindow::openSharingPane(Pane *owner, bool focus) {
    if (!owner) return nullptr;
    QWidget *page = pageOf(owner);
    if (!page) return nullptr;
    // Whatever has the keyboard right now, if this pane is arriving on its own. Showing a new
    // pane full of buttons does take the focus, and the next keystroke would then land on
    // Admit; it is put back below, and again after the layout has run, because the splitter
    // moves the focus a second time on the next turn of the event loop.
    QPointer<QWidget> held(focus ? nullptr : QApplication::focusWidget());
    ToolPane *tool = sharingPaneIn(page);
    auto *view = tool ? dynamic_cast<relay::sharing::SharingView *>(tool->hosted()) : nullptr;
    if (!tool) {
        view = new relay::sharing::SharingView;
        view->setModel(&relay::RemoteShare::instance().sharingModel());
        tool = new ToolPane(ToolPane::Kind::Sharing, view, view, owner->cwd());
        tool->setProperty("paneType", QStringLiteral("sharing"));
        relay::theme::polishWindow(tool);
        QPointer<ToolPane> guard(tool);
        view->onTitleChanged = [guard] { if (auto *w = windowOf(guard)) w->updateTitles(); };
        view->onClose = [guard] {
            auto *w = windowOf(guard);
            if (!w) return;
            Pane *owner = w->sharingOwnerOf(guard);
            w->closePane(guard, false);
            if (owner) { w->setActiveLeaf(owner); focusLeaf(owner); }
        };
        // Every hook and signal that needs only the sidecar, and a replay of what is already
        // known (the service, the addresses, the devices, an ask still waiting), so that a pane
        // opened after those lines arrived shows them. Bound to the view's lifetime.
        relay::RemoteShare::instance().attach(view);
        // The picker's contents, fresh each time it opens: the pane this Sharing pane was last
        // opened from is the current one, so "Share this pane…" from another pane in the same
        // tab lands on that pane, not on the first one that opened the Sharing pane.
        view->onScopes = [guard]() -> QList<relay::sharing::Scope> {
            auto *w = windowOf(guard);
            return w ? w->sharingScopes(w->sharingOwnerOf(guard)) : QList<relay::sharing::Scope>();
        };
        // Remote control on or off, from the Devices page: the same two steps Options › Remote
        // takes, queued because this runs from the switch's own signal and the refresh redraws it.
        view->onRemoteSwitch = [guard](bool on) {
            auto *w = windowOf(guard);
            if (!w) return;
            relay::RemoteShare::instance().setAlwaysOn(on);
            QTimer::singleShot(0, w, [w] { w->syncAlwaysOnShares(); w->refreshSettingsPanes(); });
        };
        view->onCreateInvite = [guard](const relay::sharing::Scope &scope, const QString &role,
                                       int expires, int uses) {
            if (auto *w = windowOf(guard)) w->inviteToScope(scope, role, expires, uses);
        };
        view->onCreateCode = [guard](const relay::sharing::Scope &scope, const QString &role) {
            if (auto *w = windowOf(guard)) w->codeForScope(scope, role);
        };
        insertBeside(owner, tool, owner->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
    }
    if (view) {
        // Which pane it was opened from, by token rather than pointer: the pane may close while
        // the Sharing pane stays, and a token that resolves to nothing is simply "no current pane".
        tool->setProperty("relaySharingOwner", owner->sessionToken());
        view->focusPane(owner->sessionToken());
        view->refresh();
    }
    if (focus) {
        if (QWidget *shown = pageOf(tool)) m_tabs->setCurrentWidget(shown);
        setActiveLeaf(tool);
        focusLeaf(tool);
    } else if (held) {
        held->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(0, this, [held] { if (held) held->setFocus(Qt::OtherFocusReason); });
    }
    updateTitles();
    return tool;
}

Pane *RelayWindow::sharingOwnerOf(ToolPane *tool) const {
    return tool ? paneWithToken(tool->property("relaySharingOwner").toString()) : nullptr;
}

relay::sharing::SharingView *RelayWindow::sharingViewFor(Pane *owner, bool focus) {
    ToolPane *tool = openSharingPane(owner, focus);
    return tool ? dynamic_cast<relay::sharing::SharingView *>(tool->hosted()) : nullptr;
}

// "Share this pane…" on a pane's share chip, its context menu and the palette (pane.share): the
// People page with the invite form open on this pane. Nothing is published until a link or a
// code is asked for, so pressing it and thinking better of it shares nothing.
void RelayWindow::shareThisPane(Pane *pane) {
    if (!pane) return;
    relay::sharing::SharingView *view = sharingViewFor(pane, true);
    if (!view) return;
    relay::sharing::Scope scope;
    for (const relay::sharing::Scope &candidate : sharingScopes(pane))
        if (candidate.kind == relay::sharing::Scope::Kind::Pane && candidate.id == pane->sessionToken()) {
            scope = candidate;
            break;
        }
    view->startInvite(scope);
}

// "Share more…": the same form with the picker open, so a whole tab or everything is one click
// away. An empty scope is the view's word for "the picker, on the current pane".
void RelayWindow::shareMore(Pane *pane) {
    if (relay::sharing::SharingView *view = sharingViewFor(pane, true)) view->startInvite(relay::sharing::Scope());
}

// The scope picker's contents: every terminal pane in this window (the current tab's first),
// then every tab that holds one, then Everything. A pane with no session token has no screen to
// share yet and is left out; a tab with none of those is left out with it. Tab ids are the ones
// whole-tab shares use, so a scope picked here is exactly what RemoteShare::shareTab() gets.
QList<relay::sharing::Scope> RelayWindow::sharingScopes(Pane *current) {
    using relay::sharing::Scope;
    QWidget *currentPage = current ? pageOf(current) : nullptr;
    QList<Scope> currentPanes, otherPanes, tabs;
    int total = 0;
    for (int i = 0; i < m_tabs->count(); ++i) {
        QWidget *page = m_tabs->widget(i);
        const QList<Pane *> panes = panesIn(page);
        int count = 0;
        for (Pane *pane : panes) if (!pane->sessionToken().isEmpty()) ++count;
        if (count == 0) continue;
        const QString tabId = shareTabId(page);
        // The tab as the owner sees it: its project's name when it is attached to one, else the
        // label the tab bar shows.
        const QString tabTitle = tabLabelFor(page, paneTitlesIn(page));
        for (Pane *pane : panes) {
            const QString token = pane->sessionToken();
            if (token.isEmpty()) continue;
            Scope scope;
            scope.kind = Scope::Kind::Pane;
            scope.id = token;
            // The same label the tab and the phone show: the title, or the folder until it has one.
            scope.title = pane->paneTitle().isEmpty() ? QFileInfo(pane->cwd()).fileName() : pane->paneTitle();
            scope.tab = tabId;
            scope.tabTitle = tabTitle;
            scope.current = pane == current;
            (page == currentPage ? currentPanes : otherPanes) << scope;
        }
        Scope tab;
        tab.kind = Scope::Kind::Tab;
        tab.id = tabId;
        tab.title = tabTitle;
        tab.tab = tabId;
        tab.tabTitle = tabTitle;
        tab.panes = count;
        tab.current = page == currentPage;
        tabs << tab;
        total += count;
    }
    Scope all;
    all.kind = Scope::Kind::All;
    all.id = relay::RemoteShare::allTabsScope();
    all.title = QStringLiteral("Everything");
    all.tab = relay::RemoteShare::allTabsScope();
    all.panes = total;
    return currentPanes + otherPanes + tabs + QList<Scope>{all};
}

// Publish what a scope needs before a link or a code can name it, and say which pane and which
// tab the sidecar's line should carry. False means it said why on the pane or in the window,
// and nothing should be minted.
bool RelayWindow::publishScope(const relay::sharing::Scope &scope, QString *paneId, QString *tab) {
    relay::RemoteShare &share = relay::RemoteShare::instance();
    switch (scope.kind) {
    case relay::sharing::Scope::Kind::Pane: {
        Pane *pane = paneWithToken(scope.id);
        if (!pane) {
            notice(QStringLiteral("That pane is gone — pick another one to share."), 6000);
            return false;
        }
        if (!share.isSharing(scope.id)) {
            // A pane in a tab shared whole is published under that tab, so the tab's guests keep
            // seeing it; otherwise on its own.
            QString error;
            if (!pane->startSharing(share.isTabShared(scope.tab) ? scope.tab : QString(), &error)) {
                pane->toast(error, 5000);   // the same way a whole-tab share reports it
                return false;
            }
        }
        *paneId = scope.id;
        tab->clear();
        return true;
    }
    case relay::sharing::Scope::Kind::Tab: {
        if (!share.isTabShared(scope.id)) share.shareTab(scope.id);
        // tabSharesChanged only queues the sync, and the invite needs a pane under the tab now.
        syncTabShares();
        if (share.panesInTab(scope.id).isEmpty()) {
            for (int i = 0; i < m_tabs->count(); ++i) {
                QWidget *page = m_tabs->widget(i);
                if (page->property("relayShareTab").toString() != scope.id) continue;
                for (Pane *pane : panesIn(page))
                    if (!pane->sessionToken().isEmpty()) pane->shareUnderTab(scope.id);
            }
        }
        const QStringList panes = share.panesInTab(scope.id);
        if (panes.isEmpty()) {
            notice(QStringLiteral("Nothing in that tab can be shared yet — open a terminal in it first."), 6000);
            return false;
        }
        *paneId = panes.first();
        *tab = scope.id;
        return true;
    }
    case relay::sharing::Scope::Kind::All: {
        if (!share.isAllTabsShared()) share.shareAllTabs();
        syncTabShares();
        for (Pane *pane : allPanes())
            if (share.isSharing(pane->sessionToken())) {
                *paneId = pane->sessionToken();
                *tab = relay::RemoteShare::allTabsScope();
                return true;
            }
        notice(QStringLiteral("Nothing can be shared yet — open a terminal first."), 6000);
        return false;
    }
    }
    return false;
}

void RelayWindow::inviteToScope(const relay::sharing::Scope &scope, const QString &role, int expires, int uses) {
    QString paneId, tab;
    if (!publishScope(scope, &paneId, &tab)) return;
    relay::RemoteShare::instance().createInvite(paneId, role, expires, uses, tab);
}

void RelayWindow::codeForScope(const relay::sharing::Scope &scope, const QString &role) {
    QString paneId, tab;
    if (!publishScope(scope, &paneId, &tab)) return;
    relay::RemoteShare::instance().createCode(paneId, role, tab);
}

// "Pair a phone" (#FR1C): the plug menu, the palette (remote.pair) and Options › Remote all land
// here. A phone paired against a desktop that publishes nothing shows an empty list and no
// notification, so the switch goes on first — writing what the Options switch writes, and
// sending the same `start` with `always` — and then the Sharing pane opens on Devices with a
// pairing offer started (#SMDX).
void RelayWindow::pairPhone() {
    relay::RemoteShare &share = relay::RemoteShare::instance();
    if (!share.alwaysOn()) {
        relay::remotesettings::turnOnForPairing();
        share.setAlwaysOn(true);          // brings the sidecar up and sends `start`
        syncAlwaysOnShares();
        refreshSettingsPanes();
    }
    // The Sharing pane sits beside a terminal pane. The active leaf is the natural owner; a tool
    // pane (Options, the Switchboard) is not one, and "Pair a phone" from there is still meant
    // to work.
    Pane *pane = dynamic_cast<Pane *>(m_activeLeaf.data());
    if (!pane) {
        const QList<Pane *> panes = allPanes();
        pane = panes.isEmpty() ? nullptr : panes.first();
    }
    if (!pane) {
        notice(QStringLiteral("Open a terminal pane first — pairing happens in the Sharing pane, "
                              "which sits beside one."), 6000);
        return;
    }
    if (relay::sharing::SharingView *view = sharingViewFor(pane, true)) view->startPairing();
}

// A device asking to be paired names no pane, so every window hears it. One answers: the active
// one, else the first Relay window on screen, so two windows do not both ring and both open a
// Sharing pane for the same phone.
bool RelayWindow::answersDeviceAsks() const {
    if (auto *active = dynamic_cast<RelayWindow *>(QApplication::activeWindow())) return active == this;
    for (QWidget *top : QApplication::topLevelWidgets())
        if (auto *window = dynamic_cast<RelayWindow *>(top); window && window->isVisible()) return window == this;
    return true;
}
