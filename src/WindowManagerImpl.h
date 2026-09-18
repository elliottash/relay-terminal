// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The WindowManager members that need the complete RelayWindow -- opening, restoring and counting
// windows, and reading and writing the saved layout. They are here rather than in the class
// because they were written out of line, below RelayWindow, for exactly that reason; include this
// after RelayWindow.h. `inline` because they are now definitions in a header, which is the one
// change the split made to the code it moved.

#include "RelayWindow.h"

#include "Theme.h"
#include "WindowState.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QScreen>
#include <QTabWidget>
#include <QUrl>
#include <QLockFile>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <unistd.h>

// ----- saved window layout: "reopen where I left off" ----------------------------------------
// See the block comment on WindowManager. The file format and the pure rules live in
// src/WindowState.h; everything below walks the live windows.

// A quit (Ctrl+Q, a signal, the session ending) stops the event loop with the windows still open,
// and a window is only ever deleted by closing it. Without this nothing destroys their panes: the
// workers are not told to shut down and each pane's private /tmp/relay-XXXXXX directory stays.
inline WindowManager::~WindowManager() {
    const QList<QPointer<RelayWindow>> windows = m_windows;   // a window may still call forget()
    for (const QPointer<RelayWindow> &window : windows) delete window.data();
}

inline void WindowManager::setUpLayoutSaving() {
    m_statePath = relay::windowstate::defaultPath();
    const QString lockPath = relay::windowstate::defaultLockPath();
    if (!lockPath.isEmpty()) {
        QDir().mkpath(QFileInfo(lockPath).absolutePath());
        m_stateLock = std::make_unique<QLockFile>(lockPath);
        // Never steal a lock because it is old; QLockFile still clears one left by a dead process.
        m_stateLock->setStaleLockTime(0);
    }
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000);   // a crash loses at most this much
    QObject::connect(&m_saveTimer, &QTimer::timeout, &m_context, [this] { saveLayoutNow(); });
}

inline bool WindowManager::ownsLayout() {
    if (m_owner) return true;
    if (!m_stateLock) return false;          // no lock file: fall back to last writer wins
    m_owner = m_stateLock->tryLock(0);
    return m_owner;
}

inline void WindowManager::scheduleSave() {
    if (m_saveSuspended || !restoreEnabled() || m_statePath.isEmpty()) return;
    m_saveTimer.start();
}

inline QJsonArray WindowManager::captureWindows() {
    m_windows.removeAll(nullptr);
    QJsonArray windows;
    for (RelayWindow *window : std::as_const(m_windows)) {
        if (!window || window->tabCount() == 0) continue;
        QStringList titles;
        int current = 0;
        const QJsonArray tabs = window->restorableTabs(&titles, &current);
        if (tabs.isEmpty()) continue;
        const QScreen *screen = window->screen();
        windows.append(relay::windowstate::windowRecord(window->geometry(), screen ? screen->name() : QString(),
                                                        tabs, current, titles));
    }
    return windows;
}

inline void WindowManager::writeWindows(const QJsonArray &windows) {
    m_saveTimer.stop();
    // An empty set is never written: it would mean "open nothing next time", and quitting is not
    // a request to forget the layout. "Start a fresh window set" and --fresh are.
    if (windows.isEmpty() || m_saveSuspended || !restoreEnabled() || m_statePath.isEmpty()) return;
    if (!ownsLayout() && m_stateLock) return;   // another Relay owns the file
    QString error;
    if (!relay::windowstate::write(m_statePath, relay::windowstate::document(windows), &error))
        fprintf(stderr, "relay: could not save the window layout: %s\n", qPrintable(error));
    // The saved layout is the list of panes that can still come back, so it is also the list of
    // scrollback files worth keeping: everything else belonged to a pane that is gone for good.
    relay::windowstate::pruneScrollback(relay::windowstate::scrollbackIds(windows));
}

inline void WindowManager::saveLayoutNow() { writeWindows(captureWindows()); }

// One pass over every live pane. Panes that cannot report their scrollback, and panes whose text
// is empty, leave no file behind.
inline void WindowManager::saveScrollbacks() {
    if (m_saveSuspended || !restoreEnabled() || m_statePath.isEmpty()) return;
    if (!ownsLayout() && m_stateLock) return;   // another Relay owns the state directory
    m_windows.removeAll(nullptr);
    for (RelayWindow *window : std::as_const(m_windows))
        if (window) window->savePaneScrollbacks();
}

inline void WindowManager::noteWindowClosing() {
    if (m_cascadeActive) return;
    // Taken while the closing window is still in the list: if every window goes (a quit), this is
    // the set that comes back; if others survive, the settled snapshot below wins instead.
    m_cascadeActive = true;
    // Every window's panes, while their shells and their text are still there: on a quit the
    // whole set goes at once, and the panes of the windows behind this one are closed (and
    // emptied) before any later callback could read them.
    saveScrollbacks();
    m_cascadeSnapshot = captureWindows();
    QTimer::singleShot(0, &m_context, [this] { settleWindowClose(); });
}

inline void WindowManager::settleWindowClose() {
    if (!m_cascadeActive) return;
    m_cascadeActive = false;
    m_windows.removeAll(nullptr);
    if (m_windows.isEmpty()) writeWindows(m_cascadeSnapshot);   // quit: keep what was open
    else saveLayoutNow();                                       // one window closed: drop it
    m_cascadeSnapshot = QJsonArray();
}

inline int WindowManager::restoreSavedLayout() {
    if (!restoreEnabled() || m_statePath.isEmpty()) return 0;
    if (m_stateLock && !ownsLayout()) {
        m_restoreNote = QStringLiteral("Another Relay is running; this window set is not saved.");
        return 0;
    }
    QString error;
    const QJsonObject state = relay::windowstate::read(m_statePath, &error);
    if (!error.isEmpty()) {
        m_restoreNote = QStringLiteral("Saved window layout ignored: ") + error;
        fprintf(stderr, "relay: %s\n", qPrintable(m_restoreNote));
        return 0;
    }
    const QJsonArray windows = relay::windowstate::usableWindows(state);
    if (windows.isEmpty()) return 0;
    QList<relay::windowstate::Screen> screens;
    for (const QScreen *screen : QGuiApplication::screens()) screens.append({screen->name(), screen->availableGeometry()});
    // Wayland clients cannot place their own windows; the compositor decides, so only the size is
    // worth asking for and even that may be ignored.
    const bool wayland = QGuiApplication::platformName().startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive);
    int opened = 0;
    for (const auto &value : windows) {
        const QJsonObject record = value.toObject();
        QRect geometry = relay::windowstate::clampToScreens(relay::windowstate::geometryOf(record),
                                                            relay::windowstate::screenOf(record), screens);
        RelayWindow *window = newWindow(relay::windowstate::tabsOf(record), relay::windowstate::currentOf(record),
                                        wayland ? QRect() : geometry);
        if (!window) continue;
        ++opened;
        if (wayland && geometry.isValid()) window->resize(geometry.size());
    }
    if (opened > 0)
        m_restoreNote = QStringLiteral("Reopened %1 window(s) where you left off. Actions › Start a fresh window set, or relay --fresh.")
                            .arg(opened);
    return opened;
}

// One status line after a restore, so the reopened set never looks like a stuck old window.
inline void WindowManager::announceRestore() {
    m_windows.removeAll(nullptr);
    if (m_restoreNote.isEmpty() || m_windows.isEmpty() || !m_windows.first()) return;
    QPointer<RelayWindow> window = m_windows.first();
    const QString note = m_restoreNote;
    QTimer::singleShot(1200, &m_context, [window, note] { if (window) window->notice(note, 9000); });
}

inline void WindowManager::forgetSavedLayout(bool suspend) {
    m_saveTimer.stop();
    m_saveSuspended = suspend;
    if (!m_statePath.isEmpty()) QFile::remove(m_statePath);
    // Forgetting the layout forgets the terminal text with it: the saved scrollback is only there
    // to fill the panes the layout brings back, and it is the more private half of the pair.
    relay::windowstate::removeAllScrollback();
}

inline RelayWindow *WindowManager::newWindow(const QJsonArray &tabs, int current, const QRect &geometry) {
    auto *window = new RelayWindow(this);
    relay::theme::polishWindow(window);
    int added = 0;
    for (const auto &tab : tabs) added += window->addTab(tab.toObject()) ? 1 : 0;
    if (!added) { window->deleteLater(); return nullptr; }
    if (geometry.isValid()) window->setGeometry(geometry);
    m_windows.append(window);
    window->show();
    if (auto *tabsWidget = window->findChild<QTabWidget *>()) tabsWidget->setCurrentIndex(std::clamp(current, 0, tabsWidget->count() - 1));
    window->raise(); window->activateWindow();
    return window;
}

inline RelayWindow *WindowManager::newEmptyWindow(const QRect &geometry) {
    auto *window = new RelayWindow(this);
    relay::theme::polishWindow(window);
    if (geometry.isValid()) window->setGeometry(geometry);
    m_windows.append(window);
    window->show();
    return window;
}

inline RelayWindow *WindowManager::newWindowAt(const QString &cwd) {
    return newWindow(QJsonArray{QJsonObject{{"pane", QJsonObject{{"cwd", cwd}, {"workspace", m_workspace}}}}});
}

inline void WindowManager::focusPane(const QString &token) {
    m_windows.removeAll(nullptr);
    if (token.isEmpty()) return;
    for (RelayWindow *window : std::as_const(m_windows)) {
        Pane *pane = window->findPaneByToken(token);
        if (!pane) continue;
        window->revealPane(pane);
        return;
    }
}

inline bool WindowManager::handleOpen(const QJsonObject &request) {
    m_windows.removeAll(nullptr);
    if (request.contains(QStringLiteral("url"))) {
        // relay://turn/<pane-token>/<turn-id>: the inline "✦ N tool calls" link.
        const QUrl url(request.value(QStringLiteral("url")).toString());
        const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        // relay://continue/<pane-token>: the inline "▸ Continue" link after a step limit.
        if (url.scheme() == QStringLiteral("relay") && url.host() == QStringLiteral("continue") && parts.size() == 1) {
            for (RelayWindow *window : std::as_const(m_windows))
                if (Pane *pane = window->findPaneByToken(parts.at(0))) { pane->continueTurn(true); window->raise(); window->activateWindow(); return true; }
            return false;
        }
        // relay://open-call/<pane-token>/<turn-id>/<call-id>: a tool-call line whose click is not a
        // fold (#TK9C, protocol § 23.6). The pane knows what its label asked to open; this only
        // finds the pane. relay://call/… never reaches here — it belongs to the engine's fold layer.
        if (url.scheme() == QStringLiteral("relay") && url.host() == QStringLiteral("open-call") && parts.size() == 3) {
            const QString target = request.value(QStringLiteral("url")).toString();
            for (RelayWindow *window : std::as_const(m_windows))
                if (Pane *pane = window->findPaneByToken(QUrl::fromPercentEncoding(parts.at(0).toUtf8()))) {
                    pane->openCallLink(target);
                    window->revealPane(pane);
                    window->raise(); window->activateWindow();
                    return true;
                }
            return false;
        }
        if (url.scheme() != QStringLiteral("relay") || url.host() != QStringLiteral("turn") || parts.size() != 2) return false;
        for (RelayWindow *window : std::as_const(m_windows))
            if (Pane *pane = window->findPaneByToken(parts.at(0))) {
                window->openTurnPane(pane, QUrl::fromPercentEncoding(parts.at(1).toUtf8()));
                window->raise(); window->activateWindow();
                return true;
            }
        return false;
    }
    const QString path = request.value(QStringLiteral("path")).toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) return false;
    const int line = request.value(QStringLiteral("line")).toInt();
    const QString token = request.value(QStringLiteral("token")).toString();
    // A shell names its pane; a link click from another app lands on the focused window.
    if (!token.isEmpty()) {
        for (RelayWindow *window : std::as_const(m_windows))
            if (Pane *pane = window->findPaneByToken(token)) { window->openPath(path, line, pane); return true; }
    }
    RelayWindow *target = dynamic_cast<RelayWindow *>(QApplication::activeWindow());
    if (!target && !m_windows.isEmpty()) target = m_windows.last();
    if (!target) return false;
    target->openPath(path, line, target->activeLeaf());
    target->raise(); target->activateWindow();
    return true;
}

inline void WindowManager::cycle(RelayWindow *from, int delta) {
    m_windows.removeAll(nullptr);
    if (m_windows.size() < 2) return;
    int index = m_windows.indexOf(from);
    if (index < 0) index = 0;
    RelayWindow *next = m_windows.at((index + delta + m_windows.size()) % m_windows.size());
    next->showNormal(); next->raise(); next->activateWindow();
}

inline void WindowManager::restore(RelayWindow *requester) {
    while (!m_closed.isEmpty()) {
        ClosedItem item = m_closed.takeLast();
        switch (item.kind) {
        case ClosedItem::PaneItem:
            if (item.window && item.sibling && item.window->restorePaneNextTo(item.sibling, item.orientation, item.before, item.layout)) return;
            if (item.window) { item.window->addTab(item.layout); return; }
            if (requester) { requester->addTab(item.layout); return; }
            newWindow(QJsonArray{item.layout});
            return;
        case ClosedItem::TabItem:
            if (item.window) { item.window->addTab(item.layout, item.index); return; }
            newWindow(QJsonArray{item.layout});
            return;
        case ClosedItem::WindowItem:
            newWindow(item.tabs, item.index, item.geometry);
            return;
        }
    }
    if (requester) requester->notice(QStringLiteral("Nothing to restore."));
}

