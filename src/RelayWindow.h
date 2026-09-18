// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One window -- the tab row that doubles as the title bar, the splitter tree of panes inside each
// tab, the actions palette, and the routing of every shortcut to the pane that should act on it --
// and above it WindowManager, which owns the windows, remembers the closed ones and keeps the
// saved layout. The two share a header because they name each other: RelayWindow holds a
// WindowManager and WindowManager::forget() takes a RelayWindow, both inline, so neither can be
// declared first on its own. The WindowManager members that need more than that are in
// WindowManagerImpl.h.

#include "PaneChrome.h"
#include "WindowChrome.h"

#include "Theme.h"
#include "FilePanes.h"
#include "BoardPane.h"
#include "BoardWorker.h"
#include "Hints.h"
#include "Notifications.h"
#include "ScreenPrompt.h"
#include "PaneLayout.h"
#include "QueueNav.h"
#include "PaneTitles.h"
#include "TurnTranscript.h"
#include "SettingsPane.h"
#include "SubagentTranscript.h"
#include "SubagentsPanel.h"
#include "Logging.h"
#include "TerminalBackends.h"
#include "TerminalBackend.h"
#include "WindowState.h"
#include "RuntimeDirs.h"
#include "Voice.h"
#include "Aliases.h"
#include "OutputLinks.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QFileSystemModel>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QDesktopServices>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QKeySequence>
#include <QUrl>
#include <QInputDialog>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QElapsedTimer>
#include <QToolButton>
#include <QTabBar>
#include <QLockFile>
#include <QTimer>
#include <QWindow>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>
#include <cmath>
#include <stdexcept>

// ----- windows, tabs and panes --------------------------------------------------------------
//
// Layout nodes (used to restore closed tabs and windows) are JSON:
//   {"pane": {"cwd": "...", "workspace": "..."}}
//   {"split": "h" | "v", "sizes": [..], "children": [node, ...]}
// Restoring recreates shells in the same directories; scrollback and running programs
// of a closed pane are not preserved, because closing a pane ends its shell.

class RelayWindow;

struct ClosedItem {
    enum Kind { PaneItem, TabItem, WindowItem } kind = PaneItem;
    QPointer<RelayWindow> window;
    QPointer<QWidget> sibling;           // PaneItem: the pane that took focus
    Qt::Orientation orientation = Qt::Horizontal;
    bool before = false;                 // PaneItem: restored pane goes before the sibling
    int index = 0;                       // TabItem: tab position; WindowItem: current tab
    QJsonObject layout;                  // PaneItem / TabItem: a layout node
    QJsonArray tabs;                     // WindowItem
    QRect geometry;                      // WindowItem
};

class WindowManager {
public:
    WindowManager(QString workspace, bool cleanShell) : m_workspace(std::move(workspace)), m_cleanShell(cleanShell) {
        // `relay open PATH` in Relay shells reaches this process through a private local socket. The directory is created mode 0700.
        if (m_socketDir.isValid()) {
            QFile::setPermissions(m_socketDir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
            relay::runtimedirs::markOwned(m_socketDir.path());   // whose socket directory this is
            const QString address = m_socketDir.filePath(QStringLiteral("open.sock"));
            if (m_server.listen(address)) {
                qputenv("RELAY_OPEN_SOCKET", address.toUtf8());
                publishSocketAddress(address);
                QObject::connect(&m_server, &QLocalServer::newConnection, [this] {
                    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
                        QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                        QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                            if (!socket->canReadLine()) { if (socket->bytesAvailable() > 65536) socket->abort(); return; }
                            const auto request = QJsonDocument::fromJson(socket->readLine(65536)).object();
                            const bool ok = handleOpen(request);
                            socket->write(ok ? "ok\n" : "error\n");
                            socket->flush();
                            socket->disconnectFromServer();
                        });
                    }
                });
            }
        }
    }
    bool handleOpen(const QJsonObject &request);
    // Notifications: go back to the pane that posted one, wherever it ended up.
    void focusPane(const QString &token);
    // relay:// links launched by the desktop do not inherit RELAY_OPEN_SOCKET; relay-open reads
    // the most recent address from $XDG_RUNTIME_DIR/relay/open-socket (mode 0600) instead.
    static void publishSocketAddress(const QString &address) {
        const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (runtime.isEmpty() || !QDir().mkpath(runtime + QStringLiteral("/relay"))) return;
        QFile::setPermissions(runtime + QStringLiteral("/relay"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QSaveFile file(runtime + QStringLiteral("/relay/open-socket"));
        if (!file.open(QIODevice::WriteOnly)) return;
        file.write(address.toUtf8());
        if (file.commit()) QFile::setPermissions(runtime + QStringLiteral("/relay/open-socket"), QFile::ReadOwner | QFile::WriteOwner);
    }
    ~WindowManager();   // defined below RelayWindow: it deletes the windows a quit left open
    QString workspace() const { return m_workspace; }
    bool cleanShell() const { return m_cleanShell; }
    RelayWindow *newWindow(const QJsonArray &tabs, int current = 0, const QRect &geometry = QRect());
    RelayWindow *newWindowAt(const QString &cwd);
    RelayWindow *newEmptyWindow(const QRect &geometry);   // the caller adopts a tab into it
    void cycle(RelayWindow *from, int delta);
    void remember(ClosedItem item) {
        m_closed.append(std::move(item));
        while (m_closed.size() > 25) m_closed.removeFirst();
    }
    void restore(RelayWindow *requester);
    void forget(RelayWindow *window) { m_windows.removeAll(window); }

    // ----- saved window layout: "reopen where I left off" (src/WindowState.h) -------------------
    // Relay keeps one layout file per user and rewrites it, debounced, whenever the windows, tabs,
    // panes, directories or models change, and once more when the last window goes away. A crash
    // therefore loses at most the debounce window.
    //
    // Two Relays at once: the file has one owner at a time, held as a lock file for the life of the
    // process. Only the owner restores on start and only the owner saves, so a second Relay opens a
    // plain window and leaves the layout alone; if the owner quits, the next save by a still-running
    // Relay takes the lock over and that instance's windows become the saved set from then on.
    // Writes are atomic (temp file + rename, 0600), so the file is never seen half written.
    static bool restoreEnabled() { return QSettings().value(QStringLiteral("windows/restore"), true).toBool(); }
    void setUpLayoutSaving();
    bool ownsLayout();
    void scheduleSave();
    void saveLayoutNow();
    // Every open pane's terminal text, saved beside the layout (src/WindowState.h). Only on the
    // way out — a window closing and the quit itself — because it reads up to a few thousand
    // lines per pane, which the 1 s layout debounce must not do.
    void saveScrollbacks();
    void noteWindowClosing();
    int restoreSavedLayout();                 // windows reopened, 0 when there was nothing to reopen
    void forgetSavedLayout(bool suspend);     // palette "Start a fresh window set" / setting turned off
    void announceRestore();                   // the one status line about what did or did not reopen

private:
    QJsonArray captureWindows();
    void writeWindows(const QJsonArray &windows);
    void settleWindowClose();

    QString m_workspace;
    bool m_cleanShell = false;
    QList<QPointer<RelayWindow>> m_windows;
    QList<ClosedItem> m_closed;
    QTemporaryDir m_socketDir{QDir::tempPath() + QStringLiteral("/relay-open-XXXXXX")};
    QLocalServer m_server;
    // saved window layout
    QObject m_context;                        // owns the debounce timer and queued callbacks
    QTimer m_saveTimer{&m_context};
    QString m_statePath, m_restoreNote;
    std::unique_ptr<QLockFile> m_stateLock;
    bool m_owner = false, m_saveSuspended = false, m_cascadeActive = false;
    QJsonArray m_cascadeSnapshot;
};

class RelayWindow final : public QMainWindow {
public:
    explicit RelayWindow(WindowManager *manager) : m_manager(manager) {
        setAttribute(Qt::WA_DeleteOnClose);
        setObjectName(QStringLiteral("relayWindow"));
        setWindowTitle(QStringLiteral("Relay"));
        setMinimumSize(760, 520);
        // No OS title bar: Relay draws its own in the tab row (see buildWindowChrome). The window
        // keeps a few pixels of padding so its edges stay grabbable for resizing.
        m_nativeFrame = nativeFrame();
        if (!m_nativeFrame) {
            setWindowFlag(Qt::FramelessWindowHint);
            setAttribute(Qt::WA_Hover);
            setMouseTracking(true);
            applyFrameMargins();
        }
        const QRect available = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 860);
        resize(std::min(1320, available.width() * 9 / 10), std::min(860, available.height() * 9 / 10));
        // "New pane, then ← ↑ ↓ places it" (issue #78BN): one timer closes the window and takes
        // the hint away, whether or not an arrow arrived.
        m_placementTimer.setSingleShot(true);
        connect(&m_placementTimer, &QTimer::timeout, this, [this] { endPlacement(); });
        // Pane state glyphs and tab icons (#XM0T), and the remote-session header (#SPBN).
        connect(&m_statusTimer, &QTimer::timeout, this, [this] { refreshPaneStatus(); });
        m_statusTimer.start(kStatusPollMs);
        // No toolbar: the tab bar starts at the top. Its actions live in the palette (Ctrl+Shift+A).
        Keymap::instance().listen(this, [this] { syncChromeTooltips(); });
        // The status bar stays out of the layout until something transient needs it, so the
        // window has no permanent strip under the composer and the terminal never resizes for one.
        statusBar()->setSizeGripEnabled(false);
        statusBar()->hide();
        connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString &text) {
            statusBar()->setVisible(!text.isEmpty());
        });
        m_tabs = new QTabWidget;
        m_tabs->setDocumentMode(true);
        m_tabs->setTabsClosable(false);
        m_tabs->setMovable(true);
        m_tabs->tabBar()->setExpanding(false);
        // Fusion traces a base line along the free part of the tab row. In a title bar it
        // reads as a stray rule hanging between the last tab and the window buttons.
        m_tabs->tabBar()->setDrawBase(false);
        buildTabBarControls();
        buildWindowChrome();
        auto *central = new QWidget;
        auto *row = new QHBoxLayout(central); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(0);
        row->addWidget(m_tabs, 1);
        setCentralWidget(central);
        Keymap::instance().listen(this, [this] {
            for (Pane *pane : allPanes()) pane->sendKeybindings();
            const auto conflicts = Keymap::instance().conflicts();
            notice(conflicts.isEmpty() ? QStringLiteral("Keyboard shortcuts reloaded.")
                                                         : QStringLiteral("Keyboard shortcuts: ") + conflicts.join(QStringLiteral("; ")));
            refreshSettingsPanes();
        });
        connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
            QWidget *page = m_tabs->currentWidget();
            if (!page) return;
            QWidget *leaf = m_lastActive.value(page);
            if (!leaf) { const auto leaves = leavesIn(page); leaf = leaves.isEmpty() ? nullptr : leaves.first(); }
            if (leaf) { setActiveLeaf(leaf); focusLeaf(leaf); }
        });
        connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { requestCloseTab(index); });
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
            if (QWidget *leaf = leafOf(now); leaf && leaf->window() == this) {
                const bool byMouse = QApplication::mouseButtons() != Qt::NoButton;
                const bool changed = m_activeLeaf && m_activeLeaf.data() != leaf && pageOf(m_activeLeaf) == pageOf(leaf);
                setActiveLeaf(leaf);
                if (byMouse && changed)
                    hint(QStringLiteral("pane.focus.mouse"), QStringLiteral("Next time: %1 / %2 / %3 / %4 moves between panes").arg(
                        Keymap::instance().shortcutText(QStringLiteral("pane.focusLeft")), Keymap::instance().shortcutText(QStringLiteral("pane.focusRight")),
                        Keymap::instance().shortcutText(QStringLiteral("pane.focusUp")), Keymap::instance().shortcutText(QStringLiteral("pane.focusDown"))));
            }
        });
        qApp->installEventFilter(this);
    }

    ~RelayWindow() override { qApp->removeEventFilter(this); }

    // Build a tab from a layout node. Returns false if no pane could be created.
    bool addTab(const QJsonObject &node, int index = -1) {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0);
        QWidget *root = nullptr;
        try {
            root = buildNode(node);
        } catch (const std::exception &error) {
            delete page;
            QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what()));
            return false;
        }
        layout->addWidget(root);
        index = index < 0 ? m_tabs->count() : std::min(index, m_tabs->count());
        m_tabs->insertTab(index, page, QString());
        m_tabs->setCurrentIndex(index);
        const auto leaves = leavesIn(page);
        if (!leaves.isEmpty()) { QWidget *first = leaves.first(); setActiveLeaf(first); QTimer::singleShot(0, first, [this, first] { focusLeaf(first); }); }
        updateTitles();
        return true;
    }

    QJsonObject paneNode(const QString &cwd) const {
        return {{"pane", QJsonObject{{"cwd", cwd}, {"workspace", m_active ? m_active->workspace() : m_manager->workspace()}}}};
    }

    QJsonArray serializeTabs() const {
        QJsonArray tabs;
        for (int i = 0; i < m_tabs->count(); ++i) tabs.append(serializeTab(i));
        return tabs;
    }

    // Saved window layout: tabs that hold nothing restorable (a lone subagent transcript) are
    // left out, so the saved order matches the titles beside it.
    QJsonArray restorableTabs(QStringList *titles, int *current) const {
        QJsonArray tabs;
        if (current) *current = 0;
        for (int i = 0; i < m_tabs->count(); ++i) {
            const QJsonObject node = serializeTab(i);
            if (node.isEmpty()) continue;
            if (current && i <= m_tabs->currentIndex()) *current = int(tabs.size());
            tabs.append(node);
            if (titles) titles->append(withoutMnemonic(m_tabs->tabText(i)));
        }
        return tabs;
    }
    int tabCount() const { return m_tabs->count(); }

    // Saved window layout: each pane's terminal text, written beside the layout as the window
    // closes so the restored pane comes back with its scrollback (src/WindowState.h).
    void savePaneScrollbacks() const {
        for (Pane *pane : allPanes())
            if (pane) pane->saveScrollback();
    }

    // KDE's KAcceleratorManager adds "&" accelerators to tab texts; the saved titles are only read
    // by people and tools, so they are stored the way the tab is shown.
    static QString withoutMnemonic(QString text) {
        return text.replace(QStringLiteral("&&"), QStringLiteral("\x01")).remove(QLatin1Char('&'))
                   .replace(QStringLiteral("\x01"), QStringLiteral("&"));
    }

    QString activeCwd() const {
        if (m_activeLeaf) return leafCwd(m_activeLeaf);
        return m_active ? m_active->cwd() : m_manager->workspace();
    }

    // Restore a closed pane next to a sibling pane that still exists in this window.
    bool restorePaneNextTo(QWidget *sibling, Qt::Orientation orientation, bool before, const QJsonObject &node) {
        if (!sibling || sibling->window() != this || !isLeaf(sibling)) return false;
        QWidget *leaf = nullptr;
        try { leaf = buildNode(node); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return true; }
        insertBeside(sibling, leaf, orientation, before);
        m_tabs->setCurrentWidget(pageOf(leaf));
        setActiveLeaf(leaf); focusLeaf(leaf);
        return true;
    }

    Pane *findPaneByToken(const QString &token) const {
        for (Pane *pane : allPanes()) if (pane->sessionToken() == token) return pane;
        return nullptr;
    }

    // Bring a pane to the front: its tab, the focus and the window itself. Used when a
    // notification is clicked.
    void revealPane(Pane *pane) {
        if (!pane || pane->window() != this) return;
        if (QWidget *page = pageOf(pane)) m_tabs->setCurrentWidget(page);
        setActiveLeaf(pane);
        if (isMinimized()) showNormal();
        raise();
        activateWindow();
        focusLeaf(pane);
    }

    QWidget *activeLeaf() const { return m_activeLeaf; }

    // Open a folder in an explorer pane or a file in a preview pane, next to `anchor`. An existing
    // explorer or preview in the same tab is reused, the way editors reuse a preview tab — unless
    // `newPane`, which a link followed from inside a preview passes so the file that carried the
    // link keeps its pane (issue S1JP).
    void openPath(const QString &path, int line, QWidget *anchor, bool newPane = false) {
        const QFileInfo info(path);
        if (!info.exists()) { notice(QStringLiteral("No such file or folder: ") + path, 6000); return; }
        if (!anchor || !isLeaf(anchor) || anchor->window() != this) anchor = m_activeLeaf;
        if (!anchor) return;
        QWidget *page = pageOf(anchor);
        m_tabs->setCurrentWidget(page);
        const auto kind = info.isDir() ? ToolPane::Kind::Explorer : ToolPane::Kind::Preview;
        ToolPane *target = nullptr;
        for (QWidget *leaf : leavesIn(page)) {
            auto *tool = dynamic_cast<ToolPane *>(leaf);
            if (!tool || tool->kind() != kind) continue;
            // A followed link wants its own pane, but not a second pane on a file one of them is
            // already showing: clicking back and forth between two documents would otherwise pile
            // up panes. So `newPane` reuses only an exact match, and never the anchor itself.
            if (!newPane) target = tool;
            else if (tool != anchor && tool->path() == info.absoluteFilePath()) target = tool;
        }
        if (target) {
            if (kind == ToolPane::Kind::Explorer) target->explorer()->setRoot(info.absoluteFilePath());
            else target->preview()->open(info.absoluteFilePath());
        } else {
            // A preview opens beside an explorer when there is one, otherwise beside the anchor.
            // A link followed from a preview opens beside that preview instead, so the two files
            // sit side by side and the reader can see where they came from.
            if (kind == ToolPane::Kind::Preview && !newPane)
                for (QWidget *leaf : leavesIn(page))
                    if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Explorer) anchor = tool;
            target = createToolPane(kind, info.absoluteFilePath());
            insertBeside(anchor, target, Qt::Horizontal, false);
        }
        if (kind == ToolPane::Kind::Preview && line > 0) target->preview()->goToLine(line);
        setActiveLeaf(target);
        focusLeaf(target);
        updateTitles();
    }

    // "Open items with a single click" (issue #0C7V) reaches every explorer pane that is already
    // open, in every window, not just the next one.
    static void applySingleClickSetting() {
        const bool on = relay::FileExplorer::singleClickDefault();
        const auto tops = QApplication::topLevelWidgets();
        for (QWidget *top : tops)
            for (relay::FileExplorer *explorer : top->findChildren<relay::FileExplorer *>())
                explorer->setSingleClick(on);
    }

    // The terminal pane an explorer's right-click menu acts on: the one that was last active in
    // the same tab, else the first terminal pane there, else any pane in the window.
    Pane *terminalNear(QWidget *leaf) {
        QWidget *page = pageOf(leaf);
        if (page) {
            if (auto *last = dynamic_cast<Pane *>(m_lastActive.value(page).data())) return last;
            const auto panes = panesIn(page);
            if (!panes.isEmpty()) return panes.first();
        }
        const auto panes = allPanes();
        return panes.isEmpty() ? nullptr : panes.first();
    }

    // "Navigate here" (issue #D60R): move that terminal's shell into the folder.
    void navigateTerminalTo(const QString &directory, QWidget *from) {
        Pane *pane = terminalNear(from);
        if (!pane) { statusBar()->showMessage(QStringLiteral("There is no terminal pane in this tab."), 4000); return; }
        if (!pane->changeDirectory(directory)) {
            statusBar()->showMessage(QStringLiteral("Could not change directory; the shell may be busy."), 4000);
            return;
        }
        statusBar()->showMessage(QStringLiteral("cd ") + directory, 4000);
    }

    // "Set as agent workspace" (issue #D60R). It restarts the conversation, so it asks first.
    void setAgentWorkspace(const QString &directory, QWidget *from) {
        Pane *pane = terminalNear(from);
        if (!pane) { statusBar()->showMessage(QStringLiteral("There is no agent pane in this tab."), 4000); return; }
        const auto answer = QMessageBox::question(this, QStringLiteral("Set as agent workspace"),
            QStringLiteral("Restrict the agent's file tools to\n\n%1\n\nThis starts a new conversation in that pane.").arg(directory),
            QMessageBox::Cancel | QMessageBox::Ok, QMessageBox::Ok);
        if (answer != QMessageBox::Ok) return;
        pane->setAgentWorkspace(directory);
    }

    // One path opens and closes the explorer (issue #D60R): the Ctrl+Shift+E action, the folder
    // line in a terminal pane's header, and the folder line in the explorer's own header all come
    // here. An explorer already showing this folder is closed; one showing another folder moves to
    // it; otherwise a new explorer pane opens.
    void toggleExplorer(const QString &path, QWidget *anchor) {
        const QFileInfo info(path);
        if (!info.isDir()) { openPath(path, 0, anchor); return; }
        const QString folder = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
        if (!anchor || !isLeaf(anchor) || anchor->window() != this) anchor = m_activeLeaf;
        QWidget *page = pageOf(anchor ? anchor : m_activeLeaf.data());
        if (!page) { openPath(folder, 0, anchor); return; }
        for (QWidget *leaf : leavesIn(page)) {
            auto *tool = dynamic_cast<ToolPane *>(leaf);
            if (!tool || tool->kind() != ToolPane::Kind::Explorer) continue;
            if (tool->explorer()->root() == folder) closePane(tool, true);
            else { m_tabs->setCurrentWidget(page); tool->explorer()->setRoot(folder); setActiveLeaf(tool); focusLeaf(tool); }
            updateTitles();
            return;
        }
        openPath(folder, 0, anchor);
    }

    // The preview file picker. It follows the "Open items with a single click" setting (#0C7V),
    // which Qt's own dialog has no option for: with it on, a click walks into a folder or picks a
    // file at once. Returns an empty string when nothing was chosen.
    QString pickFileForPreview() {
        // QFileDialog redeclares accept() and done() as protected, so finishing it from a click
        // handler needs this one line.
        struct PreviewDialog : QFileDialog {
            using QFileDialog::QFileDialog;
            void finish() { done(QDialog::Accepted); }
        };
        PreviewDialog dialog(this, QStringLiteral("Open file in a preview pane"), activeCwd());
        dialog.setFileMode(QFileDialog::ExistingFile);
        if (!relay::FileExplorer::singleClickDefault()) return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
        dialog.setOption(QFileDialog::DontUseNativeDialog);
        QString picked;
        const auto views = dialog.findChildren<QAbstractItemView *>();
        for (QAbstractItemView *view : views) {
            connect(view, &QAbstractItemView::clicked, &dialog, [&dialog, &picked](const QModelIndex &index) {
                if (QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) return;
                const QString path = index.data(QFileSystemModel::FilePathRole).toString();
                if (path.isEmpty()) return;
                if (QFileInfo(path).isDir()) dialog.setDirectory(path);
                else { picked = path; dialog.finish(); }
            });
        }
        if (dialog.exec() != QDialog::Accepted) return QString();
        return picked.isEmpty() ? dialog.selectedFiles().value(0) : picked;
    }

    // Agent plans and documents such as relay.md open in an editable pane beside the agent pane; a pane
    // already showing the same file is reused. Plan panes drive their owner: Execute sends plan_execute.
    void openDocument(const QString &path, Pane *owner, bool planActions) {
        if (!owner || owner->window() != this || !QFileInfo::exists(path)) return;
        QWidget *page = pageOf(owner);
        m_tabs->setCurrentWidget(page);
        ToolPane *target = nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Plan && tool->path() == QFileInfo(path).absoluteFilePath()) target = tool;
        if (target) {
            if (!target->plan()->isDirty()) target->plan()->open(path);
        } else {
            target = createToolPane(ToolPane::Kind::Plan, QFileInfo(path).absoluteFilePath(), planActions);
            insertBeside(owner, target, Qt::Horizontal, false);
        }
        QPointer<Pane> guard(owner);
        QPointer<ToolPane> tool(target);
        target->plan()->onExecute = [guard, tool](bool fresh) {
            if (!guard || !tool) return;
            if (tool->plan()->isDirty() && !tool->plan()->save()) return;
            guard->executePlan(tool->path(), fresh);
        };
        target->plan()->onKeepPlanning = [guard] { if (auto *w = windowOf(guard)) { w->setActive(guard); guard->keepPlanning(); } };
        setActiveLeaf(target);
        focusLeaf(target);
        updateTitles();
    }

    // Fork: a new agent pane on the right continues from the same conversation state. The
    // conversation list uses the same path with `fork` false to open a saved conversation.
    void openFork(Pane *source, const QJsonObject &state, const QString &title, bool fork = true) {
        if (!source || source->window() != this) return;
        Pane *pane = nullptr;
        try { pane = createPane({{"cwd", source->cwd()}, {"workspace", source->workspace()}}); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return; }
        pane->setInitialState(state, title, fork);
        insertBeside(source, pane, Qt::Horizontal, false);
        setActive(pane);
        QTimer::singleShot(0, pane, [pane] { pane->focusInput(); });
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        // Tab labels (issue JRWQ): double click a tab to name it by hand; Esc leaves it alone.
        if (object == m_tabs->tabBar() && event->type() == QEvent::MouseButtonDblClick) {
            const int index = m_tabs->tabBar()->tabAt(static_cast<QMouseEvent *>(event)->pos());
            if (index >= 0) {
                renameTab(QString(), true, m_tabs->widget(index));
                hint(QStringLiteral("tab.rename"), QStringLiteral("Next time: /rename-tab <name> · /rename names the pane"));
                return true;
            }
        }
        if (m_tabEdit && object == m_tabEdit) {
            if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                endTabRename();
                return true;
            }
            if (event->type() == QEvent::FocusOut) endTabRename();
        }
        // "New pane, then ← ↑ ↓ places it" (issue #78BN). The window closes on the first key or
        // click; only a bare arrow acts, and everything else is passed on untouched. Which widget
        // has the keyboard does not matter: with no focus at all the key reaches the window itself,
        // and the arrow still places the pane (see #4PW5).
        if (m_placement.armed(m_placementClock.elapsed())
            && (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress)
            && [&] { auto *w = qobject_cast<QWidget *>(object); return !w || w->window() == this; }()) {
            using Placement = relay::panes::PlacementWindow;
            const auto *key = event->type() == QEvent::KeyPress ? static_cast<QKeyEvent *>(event) : nullptr;
            const Placement::Response response =
                key ? m_placement.keyPress(key->key(), key->modifiers(), m_placementClock.elapsed())
                    : m_placement.mousePress(m_placementClock.elapsed());
            if (response.action == Placement::Action::Place) {
                const auto direction = response.direction;
                // Move once the key event has been dealt with: re-parenting panes underneath a
                // delivery in progress is what made the drag path crash before.
                QTimer::singleShot(0, this, [this, direction] { placeNewPane(direction); });
                return true;
            }
            if (response.action == Placement::Action::Dismiss) endPlacement();
        }
        if (headerDrag(object, event)) return true;
        if (toolHeaderDrag(object, event)) return true;
        if (event->type() == QEvent::Resize && isLeaf(qobject_cast<QWidget *>(object)))
            if (auto *chrome = chromeOf(static_cast<QWidget *>(object))) chrome->place();
        if (object == m_tabs->tabBar() && (event->type() == QEvent::Resize || event->type() == QEvent::MouseMove || event->type() == QEvent::Leave
                                           || event->type() == QEvent::Enter || event->type() == QEvent::LayoutRequest))
            QTimer::singleShot(0, this, [this] { placeTabBarControls(); });
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
            return QMainWindow::eventFilter(object, event);
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget || widget->window() != this) return QMainWindow::eventFilter(object, event);
        auto *key = static_cast<QKeyEvent *>(event);
        // Keyboard walk over the links in the output (issue GWXM). While it runs, Enter opens
        // the highlighted link, Esc leaves and the arrows move; the keys are taken here, so the
        // walk works with the prompt box focused, which is the normal state. Anything else ends
        // the walk and is handled as usual, so typing is never swallowed.
        if (m_active && m_active->outputLinkWalkActive()) {
            Pane *walking = m_active;
            switch (key->key()) {
            case Qt::Key_Return: case Qt::Key_Enter:
            case Qt::Key_Escape:
            case Qt::Key_Up: case Qt::Key_Left:
            case Qt::Key_Down: case Qt::Key_Right: {
                event->accept();
                if (event->type() != QEvent::KeyPress || key->isAutoRepeat()) return true;
                const int pressed = key->key();
                QTimer::singleShot(0, this, [walking, pressed] {
                    if (pressed == Qt::Key_Return || pressed == Qt::Key_Enter) walking->openOutputLink();
                    else if (pressed == Qt::Key_Escape) walking->endOutputLinkWalk();
                    else if (pressed == Qt::Key_Down || pressed == Qt::Key_Right) walking->stepOutputLink(1);
                    else walking->stepOutputLink(-1);
                });
                return true;
            }
            default:
                if (event->type() == QEvent::KeyPress && !key->text().isEmpty()
                    && Keymap::instance().match(key) != QStringLiteral("links.step"))
                    walking->endOutputLinkWalk();
                break;
            }
        }
        const QString id = Keymap::instance().match(key);
        if (id.isEmpty()) return QMainWindow::eventFilter(object, event);
        // A program such as vim owns its keys, unless the program_keys rule lets this shortcut act.
        Pane *pane = paneOf(widget);
        // Ctrl+H only takes control from the prompt box; in the terminal it stays Backspace.
        // Ctrl+F only opens the find bar from the prompt box; in the terminal it stays Readline's
        // forward-char, the way Ctrl+H stays Backspace there.
        if ((id == QStringLiteral("control.human") || id == QStringLiteral("input.toggle") || id == QStringLiteral("agent.interrupt")
             || id == QStringLiteral("agent.planToggle") || id == QStringLiteral("agent.effortUp") || id == QStringLiteral("agent.effortDown")
             || id == QStringLiteral("find.inView"))
            && !(pane && pane->ownsComposerWidget(widget)))
            return QMainWindow::eventFilter(object, event);
        if (pane && pane->ownsTerminalWidget(widget) && pane->processBusy() && !Keymap::instance().actsInsidePrograms(key))
            return QMainWindow::eventFilter(object, event);
        // Accept the override so neither the composer nor the terminal consumes the key,
        // then act on the key press itself. Auto-repeat does not open a burst of tabs.
        event->accept();
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
            // Remember the combination as the desktop writes it, so an action can name the key
            // that actually reached it (the shortcuts overlay does, #T9ZS).
            m_lastShortcut = {id, QKeySequence(int(key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier))
                                               | key->key()).toString(QKeySequence::NativeText)};
            QTimer::singleShot(0, this, [this, id] { runAction(id); });
        }
        return true;
    }

    // Saved window layout: geometry changes are saved like any other layout change (debounced).
    void moveEvent(QMoveEvent *event) override {
        QMainWindow::moveEvent(event);
        m_manager->scheduleSave();
    }

    // ----- frameless window: the padding around the window resizes it -----------------------------
    void mousePressEvent(QMouseEvent *event) override {
        const Qt::Edges edges = event->button() == Qt::LeftButton ? edgesAt(event->pos()) : Qt::Edges();
        if (edges) { startWindowDrag(edges, event->globalPos()); event->accept(); return; }
        QMainWindow::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (!m_manualGeometry.isNull()) { continueManualDrag(event->globalPos()); event->accept(); return; }
        if (!m_nativeFrame) {
            const Qt::CursorShape shape = cursorForEdges(edgesAt(event->pos()));
            if (shape == Qt::ArrowCursor) unsetCursor(); else setCursor(shape);
        }
        QMainWindow::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        endManualDrag();
        QMainWindow::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override {
        if (!m_nativeFrame && m_manualGeometry.isNull()) unsetCursor();
        QMainWindow::leaveEvent(event);
    }

    // Maximizing hides the resize padding; the header button turns into "restore".
    void changeEvent(QEvent *event) override {
        QMainWindow::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange) updateChromeState();
    }

    void resizeEvent(QResizeEvent *event) override {
        QMainWindow::resizeEvent(event);
        m_manager->scheduleSave();
    }

    void closeEvent(QCloseEvent *event) override {
        if (!m_confirmedClose) {
            const auto panes = allPanes();
            const bool busy = std::any_of(panes.cbegin(), panes.cend(), [](Pane *p) { return p->agentBusy() || p->processBusy(); });
            if (busy || panes.size() > 1) {
                if (!confirmClose()) { event->ignore(); return; }
            }
        }
        rememberWindow();
        // Saved window layout: snapshot the whole set before this window leaves it, so quitting
        // (every window closes at once) saves them all while closing one of several drops it.
        m_manager->noteWindowClosing();
        m_manager->forget(this);
        event->accept();
    }

private:
    // Ctrl+? (or F1): every action and its keys, so the window itself needs no shortcut bar.

    void hint(const QString &id, const QString &text, int limit = 3) {
        if (text.isEmpty() || !relay::ShortcutHints::instance().shouldShow(id, limit)) return;
        notice(text, 5000);
    }

    // Transient feedback. The window has no status bar: a permanent line under the composer
    // repeated what the composer's own chips already say. Messages ride on the active pane as a
    // toast instead, and the bar only appears (briefly) when there is no pane to put one on.
public:
    void notice(const QString &text, int milliseconds = 5000) {
        if (text.isEmpty()) return;
        if (m_active) { m_active->toast(text, milliseconds); return; }
        statusBar()->showMessage(text, milliseconds);
        statusBar()->show();
    }
private:
    // The explicit "new pane below / left / above" actions still exist, but the one key plus an
    // arrow is faster; say so the first few times one of them is used (issue #78BN).
    void hintPlacement(const QString &arrow) {
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("pane.splitRight"));
        if (keys.isEmpty()) return;
        hint(QStringLiteral("pane.place.arrow"),
             QStringLiteral("Next time: %1 then %2 puts the new pane there").arg(keys, arrow));
    }

    // Saved window layout: forget it and stop saving for the rest of this session, so the next
    // start opens one new window. The windows on screen are left alone.
    void startFreshWindowSet() {
        m_manager->forgetSavedLayout(true);
        notice(QStringLiteral("Saved window layout cleared. This session is no longer saved; the next start opens one fresh window."), 9000);
        const QString key = Keymap::instance().shortcutText(QStringLiteral("windows.fresh"));
        hint(QStringLiteral("windows.fresh.palette"),
             key.isEmpty() ? QStringLiteral("Next time: start Relay with --fresh to skip the saved layout once")
                           : relay::ShortcutHints::nextTime(key, QStringLiteral("a fresh window set")));
    }

    // ----- actions ----------------------------------------------------------------------------
    void runAction(const QString &id) {
        Pane *pane = m_active;
        if (id == QStringLiteral("window.new")) m_manager->newWindowAt(activeCwd());
        else if (id == QStringLiteral("window.next")) m_manager->cycle(this, 1);
        else if (id == QStringLiteral("window.previous")) m_manager->cycle(this, -1);
        else if (id == QStringLiteral("windows.fresh")) startFreshWindowSet();
        else if (id == QStringLiteral("tab.new")) addTab(paneNode(activeCwd()), m_tabs->currentIndex() + 1);
        else if (id == QStringLiteral("tab.next")) cycleTab(1);
        else if (id == QStringLiteral("tab.previous")) cycleTab(-1);
        // One key, one new pane on the right, then ← ↑ ↓ within two seconds to place it (#78BN).
        else if (id == QStringLiteral("pane.splitRight")) splitToward(relay::panes::Direction::Right, true);
        else if (id == QStringLiteral("pane.splitDown")) { splitToward(relay::panes::Direction::Down); hintPlacement(QStringLiteral("↓")); }
        else if (id == QStringLiteral("pane.splitLeft")) { splitToward(relay::panes::Direction::Left); hintPlacement(QStringLiteral("←")); }
        else if (id == QStringLiteral("pane.splitUp")) { splitToward(relay::panes::Direction::Up); hintPlacement(QStringLiteral("↑")); }
        else if (id == QStringLiteral("pane.focusLeft")) navigate(relay::panes::Direction::Left);
        else if (id == QStringLiteral("pane.focusRight")) navigate(relay::panes::Direction::Right);
        else if (id == QStringLiteral("pane.focusUp")) navigate(relay::panes::Direction::Up);
        else if (id == QStringLiteral("pane.focusDown")) navigate(relay::panes::Direction::Down);
        else if (id == QStringLiteral("pane.close")) closeActive();
        else if (id == QStringLiteral("pane.moveLeft")) moveActive(relay::panes::Direction::Left);
        else if (id == QStringLiteral("pane.moveRight")) moveActive(relay::panes::Direction::Right);
        else if (id == QStringLiteral("pane.moveUp")) moveActive(relay::panes::Direction::Up);
        else if (id == QStringLiteral("pane.moveDown")) moveActive(relay::panes::Direction::Down);
        else if (id == QStringLiteral("pane.moveToNewTab")) { if (m_activeLeaf) moveLeafToNewTab(m_activeLeaf); }
        else if (id == QStringLiteral("tab.moveToNewWindow")) moveTabToNewWindow(m_tabs->currentIndex());
        else if (id == QStringLiteral("closed.restore")) m_manager->restore(this);
        else if (id == QStringLiteral("files.explorer")) toggleExplorer(activeCwd(), m_activeLeaf);
        else if (id == QStringLiteral("files.open")) {
            const QString file = pickFileForPreview();
            if (!file.isEmpty()) openPath(file, 0, m_activeLeaf);
        }
        else if (id == QStringLiteral("board.open")) toggleBoardPane();
        else if (id == QStringLiteral("palette.open")) toggleSettingsPane(true);
        else if (id == QStringLiteral("keybindings.reload")) Keymap::instance().reload();
        else if (id == QStringLiteral("help.shortcuts")) openShortcutsTab();
        else if (id == QStringLiteral("app.settings")) toggleSettingsPane(false);
        else if (id == QStringLiteral("keybindings.edit")) {
            Keymap::instance().ensureFile();
            const QString editor = qEnvironmentVariable("VISUAL", qEnvironmentVariable("EDITOR", QStringLiteral("nano")));
            const QString quoted = QStringLiteral("'") + QString(Keymap::instance().path()).replace('\'', QStringLiteral("'\\''")) + '\'';
            if (!pane || !pane->runCommand(editor + ' ' + quoted))
                notice(QStringLiteral("Shortcuts file: ") + Keymap::instance().path());
        }
        else if (id == QStringLiteral("agent.subagentPane")) toggleSubagentPane();   // card #WD83
        else if (!pane) return;
        else if (id == QStringLiteral("terminal.native")) pane->toggleNative();
        else if (id == QStringLiteral("pane.restartShell")) pane->restartStopped();
        else if (id == QStringLiteral("links.step")) pane->stepOutputLink(-1);
        else if (id == QStringLiteral("control.human")) pane->takeControl();
        else if (id == QStringLiteral("control.prompt")) pane->showPrompt();
        else if (id == QStringLiteral("program.delegate")) pane->delegateProgram();
        else if (id == QStringLiteral("input.toggle")) pane->toggleInputMode();
        else if (id == QStringLiteral("agent.flashAgent")) pane->toggleFlashAgent();   // model roles
        else if (id == QStringLiteral("agent.planToggle")) pane->togglePlanMode();
        else if (id == QStringLiteral("agent.effortUp")) pane->effortStep(1);
        else if (id == QStringLiteral("agent.effortDown")) pane->effortStep(-1);
        else if (id == QStringLiteral("agent.compact")) pane->compactNow();
        else if (id == QStringLiteral("agent.rewind")) {
            pane->openRewind();
            hint(QStringLiteral("rewind.chat.mouse"), QStringLiteral("Next time: Esc Esc in an empty prompt box rewinds the chat"));
        } else if (id == QStringLiteral("agent.rewindCode")) {
            pane->openRewind(QStringLiteral("code"));
            hint(QStringLiteral("rewind.code.mouse"), QStringLiteral("Tip: /rewind-code in the prompt box restores the agent's file changes"));
        }
        else if (id == QStringLiteral("agent.fork")) pane->requestFork();
        else if (id == QStringLiteral("agent.resume")) pane->openResume();
        else if (id == QStringLiteral("conversations.open")) pane->openConversations();
        else if (id == QStringLiteral("find.inView")) pane->openFindInView();
        else if (id == QStringLiteral("agent.recap")) pane->requestRecap();
        else if (id == QStringLiteral("agent.requests")) pane->toggleRequests();
        else if (id == QStringLiteral("agent.thinkingPanel")) pane->toggleThinkingPanel();
        else if (id == QStringLiteral("agent.continue")) pane->continueTurn(Keymap::instance().shortcutText(id).isEmpty());
        else if (id == QStringLiteral("agent.instructions")) pane->openInstructions();
        else if (id == QStringLiteral("agent.export")) pane->exportConversation();
        else if (id == QStringLiteral("agent.screenshotPane")) pane->screenshotPane();   // image context
        else if (id == QStringLiteral("agent.interrupt")) pane->interruptAgentWithPrompt();
        else if (id == QStringLiteral("agent.clearQueue")) pane->clearAgentQueue();
        else if (id == QStringLiteral("agent.resumeQueue")) pane->resumeAgentQueue();
        else if (id == QStringLiteral("terminal.interrupt")) pane->interruptShell();
        else if (id == QStringLiteral("agent.newChat")) pane->newChat();
        else if (id == QStringLiteral("agent.stop")) pane->stopAgent();
        else if (id == QStringLiteral("agent.stopAllSubagents")) pane->stopAllSubagents();   // subagents UI
        else if (id == QStringLiteral("agent.agentsMenu")) openAgentsMenu();
        else if (id == QStringLiteral("voice.toggle")) pane->toggleVoice(true);
        else if (id == QStringLiteral("pane.share")) pane->toggleShare();
        else if (id == QStringLiteral("agent.provider")) pane->openProviderDialog();
        else if (id == QStringLiteral("agent.modelKeys")) {
            pane->openKeysDialog();
            hint(QStringLiteral("model.keys.slow"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("app.settings")),
                                                QStringLiteral("settings, including keys and model roles")));
        }
        else if (id == QStringLiteral("agent.modelRoles")) {
            pane->openRolesDialog();
            hint(QStringLiteral("model.roles.slow"),
                 QStringLiteral("Tip: the gear at the bottom of the model box opens this too"));
        }
        else if (id == QStringLiteral("input.modeAuto")) pane->setMode(QStringLiteral("auto"));
        else if (id == QStringLiteral("input.modeTerminal")) pane->setMode(QStringLiteral("shell"));
        else if (id == QStringLiteral("input.modeAgent")) pane->setMode(QStringLiteral("agent"));
    }

    // ----- palette ----------------------------------------------------------------------------
    using PaletteItem = relay::ActionItem;

    // ----- Actions pane and Options pane (src/SettingsPane.h) -----------------------------------
    // Two panes, one at a time, beside the focused pane (owner, 2026-09-18: a full pane, not a strip
    // over the right edge). Ctrl+Shift+A is Actions: one filterable list of everything you can do
    // now, with its keys — resume, the Switchboard, the model, a new pane, rewind, Options itself.
    // Ctrl+Shift+O, Ctrl+, and the gear are Options: what persists, every setting as a real control,
    // one tab per section. Pressing the other's key swaps the pane in place. Either search reaches
    // both catalogs, so "Ctrl+Shift+A, type, Enter" always lands somewhere. The pane is
    // transient: it is not saved with the layout, and closing it returns focus to the widget that
    // had it (vim in the terminal, or the prompt box).
    static ToolPane *settingsPaneIn(QWidget *page) {
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->settings()) return tool;
        return nullptr;
    }

    // The pane-type property is what the pane chrome colours and labels a header by.
    static void markSettingsPaneType(ToolPane *tool) {
        const bool actions = tool->settings()->mode() == relay::SettingsPane::Mode::Actions;
        tool->setProperty("paneType", actions ? QStringLiteral("actions") : QStringLiteral("options"));
    }

    ToolPane *createSettingsPane(relay::SettingsPane::Mode mode) {
        auto *view = new relay::SettingsPane(mode, [this] { return settingsSections(); }, [this] { return searchableActions(); });
        auto *tool = new ToolPane(view, m_manager->workspace());
        relay::theme::polishWindow(tool);
        tool->setObjectName(QStringLiteral("pane"));
        markSettingsPaneType(tool);
        QPointer<ToolPane> guard(tool);
        // An "Options › …" row in Actions swaps the pane itself; its title and type follow.
        view->onModeChanged = [guard] {
            if (!guard) return;
            markSettingsPaneType(guard);
            guard->update();
            if (auto *w = windowOf(guard)) w->updateTitles();
        };
        view->onClose = [guard] { if (auto *w = windowOf(guard)) w->closeSettingsPane(guard); };
        view->onRun = [guard](const relay::ActionItem &item) { if (auto *w = windowOf(guard)) w->runFromSettings(guard, item); };
        return tool;
    }

    // The action catalog for the pane's search and its Actions tab, with the hidden search words
    // folded into each item so the pane needs no table of its own.
    QList<PaletteItem> searchableActions() {
        QList<PaletteItem> items = rootItems();
        for (PaletteItem &item : items) item.aliases = (item.aliases + ' ' + paletteAliases(item)).trimmed();
        return items;
    }

    // Open the pane in this tab in that mode, or swap the one it has to it; `tab` picks the
    // Options section.
    void openSettingsPane(relay::SettingsPane::Mode mode, const QString &tab = QString(), const QString &search = QString()) {
        QWidget *page = m_tabs->currentWidget();
        ToolPane *tool = page ? settingsPaneIn(page) : nullptr;
        if (!tool) {
            m_returnPane = m_active;
            m_returnFocus = QApplication::focusWidget();
            // Aliases are files: one may have arrived from an agent, a git pull or an editor since
            // the list was last read, so ask again on the way in (issue G8DK).
            if (m_active) m_active->refreshAliases();
            QWidget *anchor = m_activeLeaf ? m_activeLeaf.data() : static_cast<QWidget *>(m_active.data());
            tool = createSettingsPane(mode);
            if (anchor) insertBeside(anchor, tool, Qt::Horizontal, false);
            else if (page && page->layout()) page->layout()->addWidget(tool);
        } else if (tool->settings()->mode() != mode) {
            tool->settings()->setMode(mode);   // re-reads both catalogs
        } else {
            tool->settings()->rebuild();
        }
        if (!tab.isEmpty()) tool->settings()->showTab(tab);
        tool->settings()->setSearch(search);
        setActiveLeaf(tool);
        tool->settings()->focusSearch();
        updateTitles();
    }

    // Two keys, one pane: Ctrl+Shift+A is Actions (things to do now), Ctrl+Shift+O and the gear are
    // Options (what persists). Each opens its pane or swaps an open one to it; pressed while its
    // own pane has the focus, it closes it.
    void toggleSettingsPane(bool actions) {
        const auto mode = actions ? relay::SettingsPane::Mode::Actions : relay::SettingsPane::Mode::Options;
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data()); tool && tool->settings()) {
            if (tool->settings()->mode() == mode) { closeSettingsPane(tool); return; }
        }
        openSettingsPane(mode);
    }

    // Ctrl+?: every action and its keys, which is the Actions pane.
    void openShortcutsTab() {
        openSettingsPane(relay::SettingsPane::Mode::Actions);
        // "Ctrl+?" is Ctrl+Shift+/ on most keyboards, so name the key that worked (#T9ZS).
        if (m_lastShortcut.first == QStringLiteral("help.shortcuts") && !m_lastShortcut.second.isEmpty())
            notice(QStringLiteral("Every action and its keys. You pressed %1.").arg(m_lastShortcut.second), 6000);
        m_lastShortcut = {};
    }

    void closeSettingsPane(ToolPane *tool) {
        if (!tool) return;
        QWidget *page = pageOf(tool);
        // Esc must never close the window: when the pane is the last leaf of the last tab, put a
        // terminal pane beside it first.
        if (page && leavesIn(page).size() <= 1 && m_tabs->count() <= 1) {
            try { insertBeside(tool, createPane(paneNode(m_manager->workspace())), Qt::Horizontal, true); }
            catch (const std::exception &error) { notice(QString::fromUtf8(error.what())); }
        }
        QPointer<QWidget> back = m_returnFocus;
        QPointer<Pane> pane = m_returnPane;
        m_returnFocus = nullptr;
        m_returnPane = nullptr;
        closePane(tool, false);
        // Focus goes back exactly where it was, e.g. to vim in the terminal, not to the composer.
        if (back && back->window() == this && back->isVisible()) {
            if (QWidget *leaf = leafOf(back)) setActiveLeaf(leaf);
            back->setFocus(Qt::OtherFocusReason);
        } else if (Pane *target = pane ? pane.data() : m_active.data()) {
            setActiveLeaf(target);
            target->focusInput();
        }
    }

    // An action chosen in the pane: the bookkeeping the palette did (the recent list, the hint that
    // teaches its key), then the pane closes and the action runs against the pane that had focus
    // before it opened. A toggle (`stayOpen`) runs in place and the pane shows its new state.
    void runFromSettings(ToolPane *tool, const PaletteItem &item) {
        if (!item.run) return;
        const QString hintId = QStringLiteral("palette.") + item.key;
        const QString hintText = item.shortcut.isEmpty()
            ? QString() : relay::ShortcutHints::nextTime(item.shortcut, item.label.toLower());
        QStringList recent = QSettings().value(QStringLiteral("palette/recent")).toStringList();
        recent.removeAll(item.key); recent.prepend(item.key);
        QSettings().setValue(QStringLiteral("palette/recent"), QStringList(recent.mid(0, 12)));
        if (item.key == QStringLiteral("app.settings")) {
            // Options, chosen from Actions: the pane swaps in place rather than closing and reopening.
            hint(hintId, hintText);
            openSettingsPane(relay::SettingsPane::Mode::Options);
            return;
        }
        if (item.stayOpen) {
            hint(hintId, hintText);
            item.run();
            QPointer<ToolPane> guard(tool);
            QTimer::singleShot(150, this, [guard] { if (guard && guard->settings()) guard->settings()->rebuild(); });
            return;
        }
        closeSettingsPane(tool);
        item.run();
        // The hint comes after anything the action says itself ("Pane screenshot attached…"), so
        // the shortcut is what stays on screen.
        if (!hintText.isEmpty())
            QTimer::singleShot(400, this, [this, hintId, hintText] { hint(hintId, hintText); });
    }

    // Something a setting depends on changed elsewhere (a keymap reload, a theme file): redraw.
    void refreshSettingsPanes() {
        for (int i = 0; i < m_tabs->count(); ++i)
            if (ToolPane *tool = settingsPaneIn(m_tabs->widget(i))) tool->settings()->rebuild();
    }

    PaletteItem actionItem(const QString &section, const QString &label, const QString &detail, const QString &action, bool checked = false) {
        PaletteItem item;
        item.key = action; item.section = section; item.label = label; item.detail = detail; item.checked = checked;
        item.shortcut = Keymap::instance().shortcutText(action);
        item.run = [this, action] { runAction(action); };
        return item;
    }

    PaletteItem submenu(const QString &key, const QString &section, const QString &label, const QString &detail,
                        std::function<QList<PaletteItem>()> children) {
        PaletteItem item;
        item.key = key; item.section = section; item.label = label; item.detail = detail; item.children = std::move(children);
        return item;
    }

    // ----- settings ---------------------------------------------------------------------------
    //
    // One catalog, one front end: the Settings pane (src/SettingsPane.h) renders these sections
    // as tabs of real controls and searches them together with the actions, so every setting keeps
    // its keyboard path. Values live in QSettings under exactly the keys they used before, because
    // several of them are read straight from QSettings elsewhere.
    //
    // Sections: General, Appearance, Models, Terminal, Agent, Voice, Privacy, Keyboard.
    //
    // What belongs here (owner, 2026-09-18): what persists — a default, true in every pane after
    // a restart. A verb ("Reload themes", "Reset shortcut hints", "Open the log folder") is an
    // action and lives in rootItems(); a button row is for opening the editor of something that
    // persists (API keys, Model roles, Instructions, Skills, keybindings.json).

    relay::SettingRow toggleRow(const QString &key, const QString &label, const QString &detail,
                                bool fallback, std::function<void(bool)> extra = {}) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Toggle;
        row.id = QStringLiteral("option:") + key;
        row.label = label;
        row.detail = detail;
        row.checked = QSettings().value(key, fallback).toBool();
        row.onToggle = [this, key, extra](bool on) {
            QSettings().setValue(key, on);
            if (extra) extra(on);
            if (m_active) m_active->agentOptionsChanged(key);
        };
        return row;
    }

    relay::SettingRow numberRow(const QString &key, const QString &label, const QString &detail,
                                int fallback, int minimum, int maximum, const QString &suffix = QString()) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Number;
        row.id = QStringLiteral("option:") + key;
        row.label = label;
        row.detail = detail;
        row.number = QSettings().value(key, fallback).toInt();
        row.minimum = minimum;
        row.maximum = maximum;
        row.suffix = suffix;
        row.onNumber = [this, key](int value) {
            QSettings().setValue(key, value);
            if (m_active) m_active->agentOptionsChanged(key);
        };
        return row;
    }

    relay::SettingRow textRow(const QString &key, const QString &label, const QString &detail,
                              const QString &placeholder, std::function<void(const QString &)> write = {}) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Text;
        row.id = QStringLiteral("option:") + key;
        row.label = label;
        row.detail = detail;
        row.placeholder = placeholder;
        row.text = QSettings().value(key).toString();
        row.onText = [this, key, write](const QString &value) {
            if (write) write(value);
            else if (value.isEmpty()) QSettings().remove(key);
            else QSettings().setValue(key, value);
            if (m_active) m_active->agentOptionsChanged(key);
        };
        return row;
    }

    static relay::SettingRow headingRow(const QString &label) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Heading;
        row.id = QStringLiteral("heading:") + label;
        row.label = label;
        return row;
    }

    relay::SettingRow buttonRow(const QString &id, const QString &label, const QString &detail,
                                const QString &buttonText, std::function<void()> run) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Button;
        row.id = id;
        row.label = label;
        row.detail = detail;
        row.buttonText = buttonText;
        row.run = std::move(run);
        return row;
    }

    relay::SettingRow choiceRow(const QString &id, const QString &label, const QString &detail,
                                const QStringList &values, const QStringList &labels,
                                const QString &current, std::function<void(const QString &)> choose) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Choice;
        row.id = id;
        row.label = label;
        row.detail = detail;
        row.options = values;
        row.optionLabels = labels;
        row.current = current;
        row.onChoose = std::move(choose);
        return row;
    }

    QList<relay::SettingsSection> settingsSections() {
        QList<relay::SettingsSection> sections;
        QSettings settings;

        relay::SettingsSection general;
        general.id = QStringLiteral("general");
        general.title = QStringLiteral("General");
        general.blurb = QStringLiteral("What Relay shows while it works.");
        general.rows << toggleRow(QStringLiteral("agent/show_thinking"), QStringLiteral("Show thinking"),
                                  QStringLiteral("Stream reasoning above the prompt; a one-line summary always prints"), true);
        general.rows << toggleRow(QStringLiteral("agent/show_tool_output"), QStringLiteral("Show tool output"),
                                  QStringLiteral("Print what each tool returned, not only the one-line summary"), false);
        {
            relay::SettingRow desktop;
            desktop.kind = relay::SettingRow::Toggle;
            desktop.id = QStringLiteral("option:notifications/desktop");
            desktop.label = QStringLiteral("Desktop notifications");
            desktop.detail = QStringLiteral("When the agent finishes or needs you and this window is not in front");
            desktop.aliases = QStringLiteral("notify alerts popup bell toast");
            desktop.checked = relay::NotificationCenter::desktopEnabled();
            desktop.onToggle = [](bool on) { relay::NotificationCenter::setDesktopEnabled(on); };
            general.rows << desktop;
        }
        {
            relay::SettingRow hints;
            hints.kind = relay::SettingRow::Toggle;
            hints.id = QStringLiteral("option:shortcut_hints");
            hints.label = QStringLiteral("Shortcut hints");
            hints.detail = QStringLiteral("A brief tip when you do something the slow way and a key exists");
            hints.checked = relay::ShortcutHints::instance().enabled();
            hints.onToggle = [](bool on) { relay::ShortcutHints::instance().setEnabled(on); };
            general.rows << hints;
        }
        general.rows << toggleRow(QStringLiteral("recap/away"), QStringLiteral("Recap when you come back"),
                                  QStringLiteral("After 3+ minutes away while the agent worked"), true);
        {
            // Dolphin-style opening in the explorer pane and the preview file picker (issue #0C7V).
            relay::SettingRow single = toggleRow(QStringLiteral("files/single_click"),
                                                 QStringLiteral("Open items with a single click"),
                                                 QStringLiteral("In the file explorer and the file picker; Ctrl+click and Shift+click still select"),
                                                 true, [this](bool) { applySingleClickSetting(); });
            single.aliases = QStringLiteral("dolphin explorer double click files folders");
            general.rows << single;
        }
        {
            // Saved window layout. The toggle only decides whether the layout is kept; restoring the
            // last closed pane works either way.
            relay::SettingRow reopen;
            reopen.kind = relay::SettingRow::Toggle;
            reopen.id = QStringLiteral("option:windows/restore");
            reopen.label = QStringLiteral("Reopen windows on start");
            reopen.detail = QStringLiteral("Windows, tabs, panes, directories and conversations come back");
            reopen.aliases = QStringLiteral("session persist startup warp layout remember where you left off");
            reopen.checked = WindowManager::restoreEnabled();
            reopen.onToggle = [this](bool on) {
                QSettings().setValue(QStringLiteral("windows/restore"), on);
                if (on) m_manager->scheduleSave(); else m_manager->forgetSavedLayout(false);
                notice(on ? QStringLiteral("Relay will reopen this window set on start.")
                                           : QStringLiteral("Relay will open one new window on start."), 6000);
            };
            general.rows << reopen;
        }
        // Diagnostics (issue SQAM): how much is logged persists, so it is an option; opening the log
        // folder is something you do, so it is in Actions (Relay › Open the log folder).
        general.rows << headingRow(QStringLiteral("Diagnostics"));
        {
            QStringList ids, labels;
            QString about;
            const QString current = relay::log::levelName(relay::log::level());
            for (const QStringList &choice : relay::log::levelChoices()) {
                ids << choice.at(0); labels << choice.at(1);
                if (choice.at(0) == current) about = choice.at(2);
            }
            relay::SettingRow level = choiceRow(QStringLiteral("option:log_level"), QStringLiteral("Log detail"),
                                                about + (current == QStringLiteral("verbose")
                                                             ? QStringLiteral(" · prompts are written to the log file")
                                                             : QStringLiteral(" · agent workers pick it up when they restart")),
                                                ids, labels, current, [](const QString &id) { relay::log::setLevel(id); });
            level.aliases = QStringLiteral("log logs diagnostics debug verbose troubleshoot");
            general.rows << level;
        }
        sections << general;

        // Colour themes (issue 0JA7). One theme file carries the UI tokens and the 16-colour
        // terminal palette, so the picker restyles the chrome, both terminal engines and the
        // prompt box's syntax colours at once. The Settings pane's search reaches these rows too.
        relay::SettingsSection appearance;
        appearance.id = QStringLiteral("appearance");
        appearance.title = QStringLiteral("Appearance");
        appearance.blurb = QStringLiteral("One file per theme. Built-in themes ship with Relay; your own go in "
                                          "~/.config/relay/themes as <name>.toml — copy a built-in one and edit it.");
        {
            QStringList ids, labels;
            for (const relay::theme::ThemeChoice &choice : relay::theme::availableThemes()) {
                ids << choice.id;
                labels << (choice.builtin ? choice.name : choice.name + QStringLiteral(" (yours)"));
            }
            appearance.rows << choiceRow(QStringLiteral("option:theme"), QStringLiteral("Theme"),
                                         QStringLiteral("Applies at once: the app, the terminal palette and the "
                                                        "prompt box's colours"),
                                         ids, labels, relay::theme::activeThemeId(), [this](const QString &id) {
                if (!relay::theme::setActiveTheme(id)) {
                    notice(QStringLiteral("That theme could not be read."), 6000);
                    return;
                }
                notice(QStringLiteral("Theme: %1.").arg(relay::theme::active().name), 4000);
            });
        }
        sections << appearance;

        relay::SettingsSection models;
        models.id = QStringLiteral("models");
        models.title = QStringLiteral("Models");
        models.blurb = QStringLiteral("Relay is bring-your-own-key. Keys live in the desktop keyring and are sent "
                                      "only to the provider they belong to.");
        models.rows << buttonRow(QStringLiteral("agent.modelKeys"), QStringLiteral("API keys"),
                                 QStringLiteral("One row per provider: status, add or replace, remove, test"),
                                 QStringLiteral("API keys…"), [this] { runAction(QStringLiteral("agent.modelKeys")); });
        models.rows << buttonRow(QStringLiteral("agent.modelRoles"), QStringLiteral("Model roles"),
                                 QStringLiteral("Default provider, the Main / Flash / Lite models, and what each job uses"),
                                 QStringLiteral("Model roles…"), [this] { runAction(QStringLiteral("agent.modelRoles")); });
        {
            const QString effort = settings.value(QStringLiteral("agent/effort"), QStringLiteral("high")).toString();
            models.rows << choiceRow(QStringLiteral("option:effort_default"), QStringLiteral("Default reasoning effort"),
                                     QStringLiteral("New panes and new chats; Alt+. and Alt+, change it per pane"),
                                     Pane::efforts(), Pane::efforts(), effort, [this](const QString &value) {
                QSettings().setValue(QStringLiteral("agent/effort"), value);
                if (m_active) m_active->agentOptionsChanged(QStringLiteral("agent/effort"));
            });
        }
        models.rows << toggleRow(QStringLiteral("agent/panes_flash"), QStringLiteral("New panes use the Flash agent"),
                                 QStringLiteral("Off: every pane starts on the main agent. On: the first pane of a window keeps it"), false);
        models.rows << numberRow(QStringLiteral("provider/max_tokens"), QStringLiteral("Output token limit"),
                                 QStringLiteral("Per model call; applies to the next conversation"), 32768, 256, 32768);
        models.rows << buttonRow(QStringLiteral("agent.provider"), QStringLiteral("Advanced provider settings"),
                                 QStringLiteral("Base URL, model id, extra request JSON and the agent workspace"),
                                 QStringLiteral("Open…"), [this] { runAction(QStringLiteral("agent.provider")); });
        sections << models;

        relay::SettingsSection terminal;
        terminal.id = QStringLiteral("terminal");
        terminal.title = QStringLiteral("Terminal");
        terminal.blurb = QStringLiteral("The shell side of a pane.");
        terminal.rows << toggleRow(QStringLiteral("composer/history_suggestions"),
                                   QStringLiteral("Command suggestions from history"),
                                   QStringLiteral("→ or Ctrl+F accepts, Alt+→ accepts a word"), true);
        {
            const QString current = Pane::defaultControl();
            terminal.rows << choiceRow(QStringLiteral("option:control_default"),
                                       QStringLiteral("Control when a full-screen program starts"),
                                       QStringLiteral("Who gets the keyboard when vim, less or top opens"),
                                       {QStringLiteral("agent"), QStringLiteral("human")},
                                       {QStringLiteral("The prompt box keeps the keyboard"),
                                        QStringLiteral("Relay takes control for you")},
                                       current, [](const QString &value) {
                QSettings().setValue(QStringLiteral("control/default"), value);
            });
        }
        terminal.rows << toggleRow(QStringLiteral("terminal/copy_on_select"), QStringLiteral("Copy on select"),
                                   QStringLiteral("Selecting terminal text copies it"), false);
        terminal.rows << toggleRow(QStringLiteral("terminal/shell_integration"),
                                   QStringLiteral("Shell integration (OSC 7/133)"),
                                   QStringLiteral("Directory and prompt marks; applies to new panes"), false);
        sections << terminal;

        relay::SettingsSection agent;
        agent.id = QStringLiteral("agent");
        agent.title = QStringLiteral("Agent");
        agent.blurb = QStringLiteral("Instructions, skills and the limits of one turn. Most of these apply to the "
                                     "next conversation; the turn limits apply at once.");
        agent.rows << headingRow(QStringLiteral("Instructions and skills"));
        {
            // Where a new pane's prompt box starts; Ctrl+I cycles auto → terminal → agent in the pane.
            const QString current = Pane::defaultInputMode();
            agent.rows << choiceRow(QStringLiteral("option:input_default"),
                                    QStringLiteral("Default input for new sessions"),
                                    QStringLiteral("Ctrl+I cycles auto → terminal → agent; ! and * override one line"),
                                    {QStringLiteral("auto"), QStringLiteral("shell"), QStringLiteral("agent")},
                                    {QStringLiteral("Auto"), QStringLiteral("Terminal"), QStringLiteral("Agent")},
                                    current, [](const QString &value) {
                QSettings().setValue(QStringLiteral("input/default"), value);
            });
        }
        {
            // run_in_terminal (protocol 22): how far a command the agent hands over may go. The
            // value is read when a turn starts, so it applies to the next prompt.
            const QString current = QSettings().value(QStringLiteral("agent/terminal_handoff"),
                                                      QStringLiteral("agent")).toString();
            agent.rows << choiceRow(QStringLiteral("option:terminal_handoff"),
                                    QStringLiteral("Commands the agent hands to your terminal"),
                                    QStringLiteral("For ssh, sudo and logins, which the agent's own shell cannot run"),
                                    {QStringLiteral("agent"), QStringLiteral("prefill"), QStringLiteral("off")},
                                    {QStringLiteral("The agent runs it or puts it in the prompt box"),
                                     QStringLiteral("Always in the prompt box, for you to run"),
                                     QStringLiteral("Off")},
                                    current, [](const QString &value) {
                QSettings().setValue(QStringLiteral("agent/terminal_handoff"), value);
            });
        }
        agent.rows << buttonRow(QStringLiteral("agent.instructions"), QStringLiteral("Instructions"),
                                QStringLiteral("CLAUDE.md, AGENTS.md, WARP.md and other instruction files"),
                                QStringLiteral("Choose…"), [this] { runAction(QStringLiteral("agent.instructions")); });
        agent.rows << buttonRow(QStringLiteral("option:skills"), QStringLiteral("Skills"),
                                QStringLiteral("List, exclude, refine, import from a repository · /skills"),
                                QStringLiteral("Open…"), [this] { if (m_active) m_active->openSkills(); });
        agent.rows << textRow(QStringLiteral("skills/exclude_text"), QStringLiteral("Excluded skills"),
                              QStringLiteral("Comma-separated names to skip (empty: the default Warp-app list)"),
                              QStringLiteral("name, other-name"), [](const QString &value) {
            QStringList names;
            for (const QString &name : value.split(',', Qt::SkipEmptyParts))
                if (!name.trimmed().isEmpty()) names << name.trimmed();
            QSettings settings;
            settings.setValue(QStringLiteral("skills/exclude_text"), value);
            if (names.isEmpty()) settings.remove(QStringLiteral("skills/exclude"));
            else settings.setValue(QStringLiteral("skills/exclude"), names);
        });
        agent.rows << textRow(QStringLiteral("agent/plans_dir"), QStringLiteral("Plans folder"),
                              QStringLiteral("Absolute folder for plans (empty: <project>/.relay/plans)"),
                              QStringLiteral("<project>/.relay/plans"));
        agent.rows << headingRow(QStringLiteral("Turn limits"));
        agent.rows << textRow(QStringLiteral("agent/compact_threshold"), QStringLiteral("Compaction threshold"),
                              QStringLiteral("Fraction of the model window, 0.50–0.98 (empty: 80% minus output room)"),
                              QStringLiteral("0.80"), [](const QString &value) {
            bool ok = false;
            const double number = value.toDouble(&ok);
            if (value.isEmpty()) QSettings().remove(QStringLiteral("agent/compact_threshold"));
            else if (ok && number >= 0.5 && number <= 0.98)
                QSettings().setValue(QStringLiteral("agent/compact_threshold"), number);
        });
        agent.rows << numberRow(QStringLiteral("agent/max_auto_turns"),
                                QStringLiteral("Automatic turns from background agents"),
                                QStringLiteral("In a row without your input (0 = unlimited)"), 50, 0, 10000);
        agent.rows << numberRow(QStringLiteral("agent/max_steps"), QStringLiteral("Step limit per turn"),
                                QStringLiteral("Model calls, then the turn stops with Continue"), 256, 1, 500);
        agent.rows << numberRow(QStringLiteral("agent/max_tool_calls"), QStringLiteral("Tool-call limit per turn"),
                                QStringLiteral("Tool calls in one turn"), 150, 1, 2000);
        // Idle deadline for a model call (protocol 15). Applies to the running agent at once.
        {
            relay::SettingRow stall = numberRow(QStringLiteral("agent/stall_timeout_s"), QStringLiteral("Stop a silent model after"),
                                                QStringLiteral("No output for this long ends the turn: retried once, then a message. "
                                                               "Reasoning models can be quiet for a while; 60 s is the default"),
                                                60, 1, 1800, QStringLiteral(" s"));
            stall.aliases = QStringLiteral("stall timeout hang stuck thinking silent retry");
            agent.rows << stall;
        }
        agent.rows << toggleRow(QStringLiteral("agent/audit_requests"), QStringLiteral("Audit requests after each turn"),
                                QStringLiteral("A small side call flags asks that may be unaddressed"), false);
        sections << agent;

        // Voice transcription (issue NY7Z). The section names the model and says where the audio
        // goes, because that is the one thing a microphone button must not leave implicit.
        relay::SettingsSection voice;
        voice.id = QStringLiteral("voice");
        voice.title = QStringLiteral("Voice");
        voice.blurb = QStringLiteral("Hold the voice key or click the microphone in the prompt strip, speak, and the "
                                     "transcript is inserted into the prompt box. Recordings are sent to OpenRouter "
                                     "(and from there to the model's provider) and are deleted as soon as they come "
                                     "back as text; voice needs an OpenRouter key whatever model your panes use.");
        voice.rows << headingRow(QStringLiteral("Capture"));
        voice.rows << toggleRow(QStringLiteral("voice/enabled"), QStringLiteral("Voice transcription"),
                                QStringLiteral("The microphone chip and the voice key"), true);
        {
            QStringList ids = relay::voice::holdKeys(), labels;
            for (const QString &id : ids) labels << relay::voice::holdKeyLabel(id);
            voice.rows << choiceRow(QStringLiteral("option:voice_hold_key"), QStringLiteral("Voice key"),
                                    QStringLiteral("Held down while you speak; released, it transcribes"),
                                    ids, labels, Pane::voiceHoldKey(), [this](const QString &value) {
                QSettings().setValue(QStringLiteral("voice/hold_key"), value);
                if (m_active) m_active->agentOptionsChanged(QStringLiteral("voice/hold_key"));
            });
        }
        voice.rows << numberRow(QStringLiteral("voice/max_seconds"), QStringLiteral("Longest recording"),
                                QStringLiteral("Recording stops by itself after this many seconds"),
                                relay::voice::kDefaultSeconds, 5, 600, QStringLiteral(" s"));
        voice.rows << textRow(QStringLiteral("voice/device"), QStringLiteral("Microphone"),
                              QStringLiteral("The capture tool's own source name (empty: the desktop default)"),
                              QStringLiteral("default"));
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("option:voice_recorder");
            const QString tool = relay::voice::chooseTool(QSettings().value(QStringLiteral("voice/tool")).toString(),
                                                          relay::voice::toolOnPath);
            // An Info row renders its label only, so the whole sentence goes there.
            info.label = tool.isEmpty()
                ? relay::voice::missingToolsMessage()
                : QStringLiteral("Recorder: %1 · 16 kHz mono WAV in a temporary file, deleted once it comes back "
                                 "as text.").arg(tool);
            voice.rows << info;
        }
        voice.rows << headingRow(QStringLiteral("Model"));
        {
            // The three that were live-tested (issue NY7Z); the ids match backend/relay_core/voice.py.
            const QStringList ids{QStringLiteral("google/gemini-3.5-flash-lite"), QStringLiteral("google/gemini-3.8-flash"),
                                  QStringLiteral("openai/whisper-1")};
            const QStringList labels{QStringLiteral("Gemini 3.5 Flash-Lite — fastest, ~$0.00006 a clip"),
                                     QStringLiteral("Gemini 3.8 Flash — most accurate, ~$0.0004 a clip"),
                                     QStringLiteral("Whisper — transcription endpoint, ~$0.0003 a clip")};
            voice.rows << choiceRow(QStringLiteral("option:voice_model"), QStringLiteral("Transcription model"),
                                    QStringLiteral("Runs on OpenRouter with your OpenRouter key"),
                                    ids, labels, Pane::voiceModel(), [](const QString &value) {
                QSettings().setValue(QStringLiteral("voice/model"), value);
            });
        }
        voice.rows << buttonRow(QStringLiteral("agent.modelKeys"), QStringLiteral("OpenRouter key"),
                                QStringLiteral("Voice needs one of its own, whatever model your panes run"),
                                QStringLiteral("API keys…"), [this] { runAction(QStringLiteral("agent.modelKeys")); });
        sections << voice;

        relay::SettingsSection privacy;
        privacy.id = QStringLiteral("privacy");
        privacy.title = QStringLiteral("Privacy");
        privacy.blurb = QStringLiteral("Relay has no telemetry. Everything below decides what leaves this machine, "
                                       "and it only ever goes to the provider whose key you configured.");
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:privacy");
            info.label = QStringLiteral(
                "Keys are stored in the desktop keyring (secret-tool, service org.relayterminal.Relay) or read "
                "from RELAY_<PROVIDER>_API_KEY. They are never written to Relay's settings files and never logged. "
                "Terminal history is not sent automatically. Shell commands the agent runs are NOT sandboxed: they "
                "have your user permissions. File tools are restricted to the agent workspace.");
            privacy.rows << info;
        }
        privacy.rows << toggleRow(QStringLiteral("suggestions/next_command"),
                                  QStringLiteral("AI next-command suggestions"),
                                  QStringLiteral("After a command finishes. Sends the command and its recent output "
                                                 "to the Suggestions model, which stays on your own provider."), false);
        privacy.rows << toggleRow(QStringLiteral("suggestions/next_prompt"),
                                  QStringLiteral("Suggested next prompts"),
                                  QStringLiteral("After an agent turn. Sends a summary of the conversation."), false);
        privacy.rows << toggleRow(QStringLiteral("instructions/project_auto"),
                                  QStringLiteral("Load project instruction files automatically"),
                                  QStringLiteral("CLAUDE.md, AGENTS.md and WARP.md found in the workspace"), true);
        sections << privacy;

        relay::SettingsSection shortcuts;
        shortcuts.id = QStringLiteral("keyboard");
        shortcuts.title = QStringLiteral("Keyboard");
        shortcuts.blurb = QStringLiteral("Keys are read from keybindings.json, and your own overrides sit on top of the "
                                         "preset. Every action and the keys it answers to: %1.")
                              .arg(Keymap::instance().shortcutText(QStringLiteral("palette.open")));
        {
            const QString presetId = Keymap::instance().preset();
            QStringList values, labels;
            for (const auto &preset : Keymap::presets()) { values << preset.first; labels << preset.second; }
            shortcuts.rows << choiceRow(QStringLiteral("option:keymap_preset"), QStringLiteral("Shortcut preset"),
                                        Keymap::instance().hasOverrides()
                                            ? QStringLiteral("Your custom overrides stay on top")
                                            : QStringLiteral("Starting point for every shortcut"),
                                        values, labels, presetId,
                                        [](const QString &id) { Keymap::instance().setPreset(id); });
        }
        {
            const QString programKeys = Keymap::instance().programKeys();
            shortcuts.rows << choiceRow(QStringLiteral("option:program_keys"), QStringLiteral("Shortcuts inside programs"),
                                        QStringLiteral("Which Relay keys still act while vim, nano or less runs"),
                                        {QStringLiteral("shift-only"), QStringLiteral("all"), QStringLiteral("none")},
                                        {QStringLiteral("Ctrl+Shift and F-keys only"),
                                         QStringLiteral("All shortcuts act"),
                                         QStringLiteral("Programs get every key")},
                                        programKeys,
                                        [](const QString &value) { Keymap::instance().setProgramKeys(value); });
        }
        // Like API keys and Instructions, this opens the editor of something that persists, so it
        // stays an option (and is an action too). Reloading the file is only an action.
        shortcuts.rows << buttonRow(QStringLiteral("keybindings.edit"), QStringLiteral("Edit keyboard shortcuts"),
                                    QStringLiteral("Opens keybindings.json in your editor"),
                                    QStringLiteral("Edit…"), [this] { runAction(QStringLiteral("keybindings.edit")); });
        {
            // What the mouse does, which no keybinding can say (owner, 2026-09-18). These are not
            // actions with keys, so they are one informational row here rather than rows in a list
            // that offers to run them.
            relay::SettingRow mouse;
            mouse.kind = relay::SettingRow::Info;
            mouse.id = QStringLiteral("info:mouse");
            mouse.aliases = QStringLiteral("mouse drag click gestures pointer header grip rename explorer");
            mouse.label = QStringLiteral(
                "With the mouse: drag a pane's header — or the ⠿ grip on an explorer, preview or Switchboard "
                "pane — onto another pane's edge to move it there, or onto the tab bar to give it a tab of its "
                "own; Esc during the drag puts it back. Double click a pane's title to rename it. Click the "
                "folder line on the right of the header to open that folder in an explorer pane, and again to "
                "close it. Ctrl+click a path, a URL or a “tool calls” line in the terminal to open it. Right "
                "click in the terminal for Relay's menu.");
            shortcuts.rows << mouse;
        }
        sections << shortcuts;
        return sections;
    }

    QList<PaletteItem> rootItems() {
        QList<PaletteItem> items;
        Pane *pane = m_active;
        const QString agent = QStringLiteral("Agent"), terminal = QStringLiteral("Terminal"), panes = QStringLiteral("Panes and tabs"),
                      app = QStringLiteral("Relay"), keys = QStringLiteral("Shortcuts");
        QString currentModel;
        if (pane) for (const auto &model : pane->storedModels()) if (model.first == pane->currentPreset()) currentModel = model.second;
        items << submenu(QStringLiteral("menu:model"), agent, QStringLiteral("Model"), currentModel.isEmpty() ? QStringLiteral("No stored keys") : currentModel, [this] {
            QList<PaletteItem> children;
            if (!m_active) return children;
            for (const auto &model : m_active->storedModels()) {
                PaletteItem item;
                const QString id = model.first;
                item.key = QStringLiteral("model:") + id; item.section = QStringLiteral("Model"); item.label = model.second;
                item.detail = QStringLiteral("This pane; starts a new conversation");
                item.checked = m_active->currentPreset() == id;
                item.run = [this, id] { if (m_active) m_active->selectModel(id); };
                children << item;
            }
            return children;
        });
        if (pane) {
            // Model roles (protocol 13): flip this pane between the Main agent and the Flash agent.
            const bool flash = pane->agentRole() == QStringLiteral("flash");
            const QString flashModel = pane->roleModel(QStringLiteral("flash"));
            items << actionItem(agent, QStringLiteral("Flash agent for this pane"),
                                (flash ? QStringLiteral("On · ") : QStringLiteral("Off · "))
                                    + (flashModel.isEmpty() ? QStringLiteral("the Flash model for this pane; the conversation is kept") : flashModel),
                                QStringLiteral("agent.flashAgent"), flash);
        }
        const QString mode = pane ? pane->mode() : QStringLiteral("auto");
        const QString modeName = mode == QStringLiteral("shell") ? QStringLiteral("Terminal") : mode == QStringLiteral("agent") ? QStringLiteral("Agent") : QStringLiteral("Auto");
        items << submenu(QStringLiteral("menu:mode"), agent, QStringLiteral("Input mode"), modeName, [this, mode] {
            return QList<PaletteItem>{
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Auto"), QStringLiteral("Commands to the terminal, everything else to the agent"), QStringLiteral("input.modeAuto"), mode == QStringLiteral("auto")),
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Terminal"), QStringLiteral("Always the terminal; the agent fixes failures"), QStringLiteral("input.modeTerminal"), mode == QStringLiteral("shell")),
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Agent"), QStringLiteral("Always the agent"), QStringLiteral("input.modeAgent"), mode == QStringLiteral("agent"))};
        });
        items << actionItem(agent, QStringLiteral("Toggle terminal / agent input"), QStringLiteral("From the prompt box"), QStringLiteral("input.toggle"));
        {
            // Voice transcription: the same action the microphone chip runs.
            const bool recording = pane && pane->voiceRecording();
            const QString hold = Pane::voiceHoldKey();
            items << actionItem(agent, recording ? QStringLiteral("Finish the recording") : QStringLiteral("Voice transcription"),
                                recording ? QStringLiteral("Transcribe what you said into the prompt box")
                                          : QStringLiteral("Record and transcribe into the prompt box%1").arg(
                                                hold == QStringLiteral("off") ? QString()
                                                    : QStringLiteral(" · hold %1").arg(relay::voice::holdKeyLabel(hold))),
                                QStringLiteral("voice.toggle"));
        }
        if (pane) {
            const QString effort = pane->effort();
            items << submenu(QStringLiteral("menu:effort"), agent, QStringLiteral("Reasoning effort"), effort, [this, effort] {
                QList<PaletteItem> children;
                for (const QString &level : Pane::efforts()) {
                    PaletteItem item;
                    item.key = QStringLiteral("effort:") + level; item.section = QStringLiteral("Reasoning effort"); item.label = level;
                    item.detail = QStringLiteral("This pane; keeps the conversation");
                    item.checked = effort == level; item.stayOpen = true;
                    item.run = [this, level] { if (m_active) m_active->setEffort(level); };
                    children << item;
                }
                return children;
            });
            if (!pane->lastTurnId().isEmpty()) {
                PaletteItem turn; turn.key = QStringLiteral("agent:last_turn"); turn.section = agent;
                turn.label = QStringLiteral("Open last agent turn"); turn.detail = QStringLiteral("tool calls, arguments and full outputs");
                turn.run = [this, guard = QPointer<Pane>(pane), id = pane->lastTurnId()] { if (guard) openTurnPane(guard, id); };
                items << turn;
            }
            {
                PaletteItem skills; skills.key = QStringLiteral("agent:skills"); skills.section = agent;
                skills.label = QStringLiteral("Skills…"); skills.detail = QStringLiteral("list, exclude, refine, import · /skills");
                skills.run = [guard = QPointer<Pane>(pane)] { if (guard) guard->openSkills(); };
                items << skills;
            }
            // Aliases (issue G8DK): saved commands and prompts. The submenu is the slow way to run
            // one, so running from here teaches `/name` and the name typed in terminal mode.
            {
                const int count = relay::aliases::names(pane->aliases()).size();
                items << submenu(QStringLiteral("menu:aliases"), agent, QStringLiteral("Aliases…"),
                                 count ? QStringLiteral("%1 saved command%2 and prompt%3 · /name to run one")
                                             .arg(count).arg(count == 1 ? QString() : QStringLiteral("s"))
                                             .arg(count == 1 ? QString() : QStringLiteral("s"))
                                       : QStringLiteral("Saved commands and prompts — none yet"),
                                 [this] { return aliasMenuItems(); });
                PaletteItem save; save.key = QStringLiteral("alias:save"); save.section = agent;
                save.label = QStringLiteral("Save the prompt box as an alias…");
                save.detail = QStringLiteral("Keep this command or prompt and run it later by name");
                save.run = [guard = QPointer<Pane>(pane)] { if (guard) guard->saveComposerAsAlias(); };
                items << save;
                PaletteItem import; import.key = QStringLiteral("alias:import"); import.section = agent;
                import.label = QStringLiteral("Import Warp workflows and shell aliases…");
                import.detail = QStringLiteral("Preview what would be saved; nothing runs and nothing is written until you confirm");
                import.run = [guard = QPointer<Pane>(pane)] { if (guard) guard->openAliasImport(); };
                items << import;
            }
            items << actionItem(agent, pane->agentMode() == QStringLiteral("plan") ? QStringLiteral("Leave plan mode") : QStringLiteral("Plan mode"),
                                QStringLiteral("Investigate and write a plan before changing anything"), QStringLiteral("agent.planToggle"),
                                pane->agentMode() == QStringLiteral("plan"));
        }
        items << actionItem(agent, QStringLiteral("Compact conversation"), QStringLiteral("Summarize older turns to free context"), QStringLiteral("agent.compact"));
        items << actionItem(agent, QStringLiteral("Rewind chat…"), QStringLiteral("Conversation back to an earlier turn; files unchanged · Esc Esc"), QStringLiteral("agent.rewind"));
        items << actionItem(agent, QStringLiteral("Rewind code…"), QStringLiteral("Restore files the agent changed since a turn · /rewind-code"), QStringLiteral("agent.rewindCode"));
        items << actionItem(agent, QStringLiteral("Fork conversation"), QStringLiteral("Continue this conversation in a new pane"), QStringLiteral("agent.fork"));
        items << actionItem(agent, QStringLiteral("Resume session…"), QStringLiteral("Open a saved agent session in this pane"), QStringLiteral("agent.resume"));
        items << actionItem(agent, QStringLiteral("Conversations…"),
                            QStringLiteral("Search every conversation and Relay's terminal history · /conversations"),
                            QStringLiteral("conversations.open"));
        items << actionItem(agent, QStringLiteral("Find in this pane…"),
                            QStringLiteral("Search this conversation and the terminal scrollback"),
                            QStringLiteral("find.inView"));
        {
            PaletteItem rebuild;
            rebuild.key = QStringLiteral("conversations.rebuild"); rebuild.section = agent;
            rebuild.label = QStringLiteral("Rebuild the conversation index");
            rebuild.detail = QStringLiteral("The index is a cache of the saved conversations; this recreates it");
            rebuild.run = [this] { if (m_active) m_active->rebuildConversationIndex(); };
            items << rebuild;
        }
        items << actionItem(agent, QStringLiteral("Recap"), QStringLiteral("Summarize what happened in this session"), QStringLiteral("agent.recap"));
        items << actionItem(agent, QStringLiteral("Tasks…"),
                            pane && !pane->tasksProgress().isEmpty() ? pane->tasksProgress() + QStringLiteral(" · the agent's task list · /tasks")
                                                                     : QStringLiteral("Task list: what the agent is working on · /tasks"), QStringLiteral("agent.requests"));
        items << actionItem(agent, QStringLiteral("Reasoning panel"),
                            pane && pane->thinkingPanelVisible()
                                ? QStringLiteral("Hide the agent's reasoning in this pane")
                                : QStringLiteral("Show the agent's reasoning · the last turn's, between turns"),
                            QStringLiteral("agent.thinkingPanel"), pane && pane->thinkingPanelVisible());
        items << actionItem(agent, QStringLiteral("Continue agent turn"),
                            pane && pane->limitReached() ? QStringLiteral("The last turn stopped at its step limit · /continue")
                                                         : QStringLiteral("Send “Continue” to the agent · /continue"), QStringLiteral("agent.continue"));
        items << actionItem(agent, QStringLiteral("Export conversation"), QStringLiteral("Save the conversation as Markdown"), QStringLiteral("agent.export"));
        // Image context: reaching this from the palette is the slow path, so the palette's own hint
        // teaches its shortcut (issue EM1E).
        items << actionItem(agent, QStringLiteral("Screenshot this pane"),
                            QStringLiteral("Attach a picture of this pane to your next prompt"),
                            QStringLiteral("agent.screenshotPane"));
        items << actionItem(agent, QStringLiteral("API keys…"),
                            QStringLiteral("Add, replace, remove or test a provider key"),
                            QStringLiteral("agent.modelKeys"));
        items << actionItem(agent, QStringLiteral("Model roles…"),
                            QStringLiteral("Default provider and the Main / Flash / Lite models"),
                            QStringLiteral("agent.modelRoles"));
        items << actionItem(agent, QStringLiteral("New chat"), QStringLiteral("Start a new conversation in this pane and clear the terminal"), QStringLiteral("agent.newChat"));
        items << actionItem(agent, QStringLiteral("Stop agent"), pane && pane->agentBusy() ? QStringLiteral("Cancel the running turn") : QStringLiteral("Agent is idle"), QStringLiteral("agent.stop"));
        // --- subagents UI ---
        {
            const int live = pane ? pane->subagents().liveCount() : 0;
            items << submenu(QStringLiteral("menu:agents"), agent, QStringLiteral("Agents…"),
                             live ? QStringLiteral("%1 running").arg(live) : QStringLiteral("Definitions and running agents"),
                             [this] { return agentsMenuItems(); });
            items << actionItem(agent, QStringLiteral("Stop all agents"), live ? QStringLiteral("%1 running subagent(s)").arg(live) : QStringLiteral("No running subagents"),
                                QStringLiteral("agent.stopAllSubagents"));
        }
        // --- end subagents UI ---
        if (pane && pane->queuedPrompts() > 0)
            items << actionItem(agent, QStringLiteral("Clear queue"), QStringLiteral("%1 queued item(s)").arg(pane->queuedPrompts()), QStringLiteral("agent.clearQueue"));
        if (pane && pane->queuePaused())
            items << actionItem(agent, QStringLiteral("Resume queue"), QStringLiteral("Paused after a stop, failure or edit"), QStringLiteral("agent.resumeQueue"));
        items << actionItem(terminal, QStringLiteral("Interrupt"), pane && pane->processBusy() ? QStringLiteral("Stop the running program · Esc in the prompt box") : QStringLiteral("Nothing is running"), QStringLiteral("terminal.interrupt"));
        items << actionItem(terminal, QStringLiteral("Take control"),
                            QStringLiteral("Hide the prompt box and type into the terminal · the only way keys reach it"),
                            QStringLiteral("control.human"), pane && pane->isNative());
        items << actionItem(terminal, QStringLiteral("Show the Relay prompt"), QStringLiteral("Back to the prompt box; it becomes the input again"), QStringLiteral("control.prompt"), pane && !pane->isNative());
        {
            // Hand the running program to the agent, or take it back (card C1HH).
            const bool driving = pane && pane->agentDriving();
            const QString detail = !pane                     ? QStringLiteral("No pane")
                : !pane->canShowAgentTheScreen()             ? QStringLiteral("This pane's engine cannot show the agent the screen · open a Relay-engine pane")
                : !pane->processBusy()                       ? QStringLiteral("Nothing is running in this pane")
                : driving                                    ? QStringLiteral("The agent may type into %1 · this hands it back").arg(pane->foregroundProgramName())
                : QStringLiteral("The agent may type into %1 until you take control").arg(pane->foregroundProgramName());
            items << actionItem(terminal, driving ? QStringLiteral("Take the program back from the agent")
                                                  : QStringLiteral("Let the agent drive this program"),
                                detail, QStringLiteral("program.delegate"), driving);
        }
        {
            const QString program = pane ? pane->foregroundProgramName() : QString();
            if (!program.isEmpty()) {
                const QString override = Pane::programControl(program);
                PaletteItem agentItem;
                agentItem.key = QStringLiteral("control.program.agent"); agentItem.section = terminal;
                agentItem.label = QStringLiteral("Keep the prompt box when %1 starts").arg(program);
                agentItem.detail = QStringLiteral("%1 gets a \"Take control\" button instead of the keyboard").arg(program);
                agentItem.checked = override == QStringLiteral("agent"); agentItem.stayOpen = true;
                agentItem.run = [program, override] { Pane::setProgramControl(program, override == QStringLiteral("agent") ? QString() : QStringLiteral("agent")); };
                items << agentItem;
                PaletteItem humanItem;
                humanItem.key = QStringLiteral("control.program.human"); humanItem.section = terminal;
                humanItem.label = QStringLiteral("Always take control of %1").arg(program);
                humanItem.detail = QStringLiteral("The prompt box hides as soon as %1 starts").arg(program);
                humanItem.checked = override == QStringLiteral("human"); humanItem.stayOpen = true;
                humanItem.run = [program, override] { Pane::setProgramControl(program, override == QStringLiteral("human") ? QString() : QStringLiteral("human")); };
                items << humanItem;
            }
        }
        if (m_active) {
            PaletteItem clear;
            clear.key = QStringLiteral("terminal.clear"); clear.section = terminal;
            clear.label = QStringLiteral("Clear terminal");
            clear.detail = QStringLiteral("Screen and scrollback · %1").arg(m_active->engineLabel());
            clear.run = [this] { if (m_active) m_active->clearTerminal(); };
            items << clear;
            if (m_active->terminalCan(relay::TerminalBackend::PromptMarks)) {
                for (const int direction : {-1, 1}) {
                    PaletteItem jump;
                    jump.key = QStringLiteral("terminal.prompt%1").arg(direction < 0 ? QStringLiteral("Previous") : QStringLiteral("Next"));
                    jump.section = terminal;
                    jump.label = direction < 0 ? QStringLiteral("Jump to previous prompt") : QStringLiteral("Jump to next prompt");
                    jump.detail = QStringLiteral("Needs the shell integration (OSC 133)");
                    jump.run = [this, direction] {
                        if (m_active && !m_active->jumpToPrompt(direction))
                            notice(QStringLiteral("No prompt mark in that direction. Enable the shell integration (OSC 7/133)."));
                    };
                    items << jump;
                }
            }
            if (m_active->canWalkOutputLinks()) {
                PaletteItem walk;
                walk.key = QStringLiteral("links.step"); walk.section = terminal;
                walk.label = QStringLiteral("Step through links in the output");
                walk.detail = QStringLiteral("Files, folders and URLs · Enter opens, Esc leaves · %1")
                                  .arg(Keymap::instance().shortcutText(QStringLiteral("links.step")));
                walk.run = [this] { if (m_active) m_active->stepOutputLink(-1); };
                items << walk;
            }
            if (m_active->terminalCan(relay::TerminalBackend::Search)) {
                PaletteItem find;
                find.key = QStringLiteral("terminal.find"); find.section = terminal;
                find.label = QStringLiteral("Search terminal…");
                find.detail = QStringLiteral("Screen and scrollback · %1").arg(m_active->engineLabel());
                find.run = [this] {
                    if (!m_active) return;
                    bool ok = false;
                    const QString text = QInputDialog::getText(this, QStringLiteral("Search terminal"),
                                                               QStringLiteral("Find:"), QLineEdit::Normal, QString(), &ok);
                    if (!ok || text.isEmpty() || !m_active) return;
                    const int matches = m_active->findInTerminal(text, false);
                    notice(matches > 0 ? QStringLiteral("%1 match(es) for \"%2\"").arg(matches).arg(text)
                                                         : QStringLiteral("No match for \"%1\"").arg(text));
                };
                items << find;
            }
        }

        items << actionItem(panes, QStringLiteral("File explorer"), QStringLiteral("Open this pane's directory, or close the explorer again"), QStringLiteral("files.explorer"));
        items << actionItem(panes, QStringLiteral("Open folder in explorer"), QStringLiteral("This pane's directory"), QStringLiteral("files.explorer"));
        {
            // Switchboard: the board of cards, threads, plans and memory (design 4.1).
            PaletteItem board = actionItem(panes, QStringLiteral("Switchboard"),
                                           QStringLiteral("Cards, threads, plans and project memory"),
                                           QStringLiteral("board.open"));
            board.aliases = QStringLiteral("board issues cards todo trello kanban scratchpad tickets tracker");
            items << board;
        }
        items << actionItem(panes, QStringLiteral("Open file…"), QStringLiteral("Preview a file in a pane"), QStringLiteral("files.open"));
        // The one key makes a pane on the right; all four directions keep an action of their own
        // so they can be run from here or bound (issue #78BN).
        items << actionItem(panes, QStringLiteral("New pane to the right"),
                            QStringLiteral("Then ← ↑ ↓ within two seconds places it on that side"), QStringLiteral("pane.splitRight"));
        items << actionItem(panes, QStringLiteral("New pane below"), QString(), QStringLiteral("pane.splitDown"));
        items << actionItem(panes, QStringLiteral("New pane to the left"), QString(), QStringLiteral("pane.splitLeft"));
        items << actionItem(panes, QStringLiteral("New pane above"), QString(), QStringLiteral("pane.splitUp"));
        items << actionItem(panes, QStringLiteral("New tab"), QString(), QStringLiteral("tab.new"));
        items << actionItem(panes, QStringLiteral("New window"), QString(), QStringLiteral("window.new"));
        items << actionItem(panes, QStringLiteral("Close pane"), QStringLiteral("Then the tab, then the window"), QStringLiteral("pane.close"));
        items << actionItem(panes, QStringLiteral("Move pane to new tab"), QStringLiteral("Keeps the shell and agent running"), QStringLiteral("pane.moveToNewTab"));
        items << actionItem(panes, QStringLiteral("Move tab to new window"), QStringLiteral("Keeps its panes running"), QStringLiteral("tab.moveToNewWindow"));
        items << actionItem(panes, QStringLiteral("Move pane left"), QStringLiteral("Or drag the ⠿ grip onto another pane's edge"), QStringLiteral("pane.moveLeft"));
        items << actionItem(panes, QStringLiteral("Move pane right"), QString(), QStringLiteral("pane.moveRight"));
        items << actionItem(panes, QStringLiteral("Move pane up"), QString(), QStringLiteral("pane.moveUp"));
        items << actionItem(panes, QStringLiteral("Move pane down"), QString(), QStringLiteral("pane.moveDown"));
        items << actionItem(panes, QStringLiteral("Restore closed"), QStringLiteral("Last closed pane, tab or window"), QStringLiteral("closed.restore"));
        // "Reopen windows on start" is a row in Options › General.
        items << actionItem(panes, QStringLiteral("Start a fresh window set"),
                            QStringLiteral("Forget the saved layout; the next start opens one new window"),
                            QStringLiteral("windows.fresh"));
        items << actionItem(panes, QStringLiteral("Next tab"), QString(), QStringLiteral("tab.next"));
        items << actionItem(panes, QStringLiteral("Previous tab"), QString(), QStringLiteral("tab.previous"));

        // Relay itself. Options is an action like any other: it is how you get there from here.
        items << actionItem(app, QStringLiteral("Options…"),
                            QStringLiteral("What persists: appearance, models, terminal, agent, voice, privacy, keyboard"),
                            QStringLiteral("app.settings"));
        {
            PaletteItem themes; themes.key = QStringLiteral("theme.reload"); themes.section = app;
            themes.label = QStringLiteral("Reload themes"); themes.detail = QStringLiteral("Pick up a theme file you added or edited");
            themes.run = [this] {
                relay::theme::refreshThemes();
                relay::theme::setActiveTheme(relay::theme::activeThemeId());
                notice(QStringLiteral("Themes reloaded. A theme added while Relay runs reaches new terminal panes; "
                                      "restart to give it to the ones already open."), 8000);
            };
            items << themes;
            PaletteItem folder; folder.key = QStringLiteral("theme.folder"); folder.section = app;
            folder.label = QStringLiteral("Open your themes folder"); folder.detail = QStringLiteral("~/.config/relay/themes");
            folder.run = [this] {
                const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                                        .absoluteFilePath(QStringLiteral("relay/themes"));
                QDir().mkpath(dir);
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
                notice(dir, 8000);
            };
            items << folder;
            PaletteItem logs; logs.key = QStringLiteral("logs.open"); logs.section = app;
            logs.label = QStringLiteral("Open the log folder");
            logs.detail = relay::log::directory().isEmpty() ? QStringLiteral("No writable data directory") : relay::log::directory();
            logs.aliases = QStringLiteral("log logs diagnostics debug troubleshoot relay.log worker.log");
            logs.run = [this] {
                const QString dir = relay::log::directory();
                if (dir.isEmpty()) { notice(QStringLiteral("No writable data directory for logs."), 6000); return; }
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
                notice(dir, 8000);
            };
            items << logs;
            PaletteItem hints; hints.key = QStringLiteral("hints.reset"); hints.section = app;
            hints.label = QStringLiteral("Reset shortcut hints"); hints.detail = QStringLiteral("Show every tip again");
            hints.run = [this] {
                relay::ShortcutHints::instance().resetAll();
                notice(QStringLiteral("Shortcut hints reset."), 4000);
            };
            items << hints;
        }

        // "Shortcut hints", "Shortcut preset" and "Shortcuts inside programs" are rows in Options.
        items << actionItem(keys, QStringLiteral("Edit keyboard shortcuts…"), Keymap::instance().path(), QStringLiteral("keybindings.edit"));
        items << actionItem(keys, QStringLiteral("Reload keyboard shortcuts"), QString(), QStringLiteral("keybindings.reload"));
        if (Keymap::instance().hasOverrides()) {
            PaletteItem clear; clear.key = QStringLiteral("keybindings.clearOverrides"); clear.section = keys;
            clear.label = QStringLiteral("Clear custom overrides"); clear.detail = QStringLiteral("Use the preset's keys only");
            clear.run = [] { Keymap::instance().clearOverrides(); };
            items << clear;
        }
        return items;
    }

    // Hidden search words for palette items, so "llm", "thinking" or "keymap" find the right entry.
    static QString paletteAliases(const PaletteItem &item) {
        static const QList<QPair<QString, QString>> table{
            {QStringLiteral("model"), QStringLiteral("llm provider ai kimi glm deepseek openrouter switch model")},
            {QStringLiteral("effort"), QStringLiteral("thinking reasoning depth effort budget")},
            {QStringLiteral("compact"), QStringLiteral("context tokens summary compaction window limit")},
            {QStringLiteral("context"), QStringLiteral("tokens usage window compaction")},
            {QStringLiteral("plan"), QStringLiteral("planning plan mode folder plans spec design")},
            {QStringLiteral("automatic turns"), QStringLiteral("wakeups subagents background auto turns handoff")},
            {QStringLiteral("instruction"), QStringLiteral("rules claude.md agents.md warp.md gemini memory relay.md onboarding")},
            {QStringLiteral("skill"), QStringLiteral("abilities tools refine import skills library")},
            {QStringLiteral("alias"), QStringLiteral("workflow workflows macro snippet saved command saved prompt template shortcut warp")},
            {QStringLiteral("copy on select"), QStringLiteral("clipboard selection highlight copy")},
            {QStringLiteral("shortcut preset"), QStringLiteral("keymap keybindings hotkeys warp vscode konsole preset")},
            {QStringLiteral("inside programs"), QStringLiteral("vim nano less passthrough program keys")},
            {QStringLiteral("suggest"), QStringLiteral("autocomplete ghost ai suggestions next command prompt")},
            {QStringLiteral("recap"), QStringLiteral("summary away return catch up")},
            {QStringLiteral("tasks"), QStringLiteral("todos todo requests ledger asks open items checklist unaddressed progress")},
            {QStringLiteral("continue agent"), QStringLiteral("continue keep going limit steps more turn")},
            {QStringLiteral("limit"), QStringLiteral("max steps tool calls budget turn length continue")},
            {QStringLiteral("audit"), QStringLiteral("unaddressed missed requests check todos")},
            {QStringLiteral("shortcut hint"), QStringLiteral("tips tutorial learn keys hints help")},
            {QStringLiteral("thinking"), QStringLiteral("reasoning chain of thought visibility show")},
            {QStringLiteral("reasoning panel"), QStringLiteral("thinking chain of thought trace show hide panel bubble")},
            {QStringLiteral("agents"), QStringLiteral("subagents workers background explore tasks")},
            {QStringLiteral("rewind"), QStringLiteral("undo checkpoint restore revert history back chat code files")},
            {QStringLiteral("fork"), QStringLiteral("branch copy duplicate conversation")},
            {QStringLiteral("resume"), QStringLiteral("sessions history reopen continue")},
            {QStringLiteral("conversations"), QStringLiteral("search find chats threads history full text index past old sessions grep")},
            {QStringLiteral("find in this pane"), QStringLiteral("search scrollback conversation ctrl+f highlight matches")},
            {QStringLiteral("rebuild the conversation index"), QStringLiteral("reindex search index sqlite fts repair cache")},
            {QStringLiteral("new chat"), QStringLiteral("clear reset conversation fresh")},
            {QStringLiteral("stop agent"), QStringLiteral("cancel abort halt interrupt")},
            {QStringLiteral("provider"), QStringLiteral("api key byok endpoint base url credentials")},
            {QStringLiteral("warp"), QStringLiteral("import keys migrate")},
            {QStringLiteral("input mode"), QStringLiteral("terminal agent auto route destination")},
            {QStringLiteral("split"), QStringLiteral("pane divide window layout")},
            {QStringLiteral("tab"), QStringLiteral("tabs page")},
            {QStringLiteral("window"), QStringLiteral("windows frame")},
            {QStringLiteral("move"), QStringLiteral("drag rearrange relocate detach")},
            {QStringLiteral("explorer"), QStringLiteral("files folder browse tree dolphin")},
        {QStringLiteral("switchboard"), QStringLiteral("board issues cards todo trello kanban scratchpad tracker")},
            {QStringLiteral("open file"), QStringLiteral("preview view read")},
            {QStringLiteral("native"), QStringLiteral("raw direct typing keyboard terminal control")},
            {QStringLiteral("interrupt"), QStringLiteral("ctrl+c kill stop signal")},
            {QStringLiteral("restore"), QStringLiteral("reopen undo close closed")},
            {QStringLiteral("fresh window set"), QStringLiteral("forget saved layout startup session persist reopen")},
            {QStringLiteral("keyboard shortcuts"), QStringLiteral("keymap keybindings hotkeys edit reload")},
            {QStringLiteral("routing"), QStringLiteral("detect classify guess terminal agent")},
        };
        const QString haystack = (item.label + ' ' + item.key + ' ' + item.section).toLower();
        QString words;
        for (const auto &entry : table)
            if (haystack.contains(entry.first)) words += entry.second + ' ';
        return words;
    }

    // ----- subagents UI: palette submenu and transcript panes ---------------------------------
    // Aliases (issue G8DK): one row per saved command or prompt, plus the two ways to get more.
    QList<PaletteItem> aliasMenuItems() {
        QList<PaletteItem> items;
        Pane *pane = m_active;
        if (!pane) return items;
        const QString section = QStringLiteral("Aliases");
        for (const auto &alias : pane->aliases()) {
            if (alias.shadowed) continue;
            PaletteItem item;
            item.key = QStringLiteral("alias:") + alias.name;
            item.section = section;
            item.label = alias.title.isEmpty() ? alias.name : alias.title;
            item.detail = relay::aliases::paletteDetail(alias);
            item.aliases = alias.name + QLatin1Char(' ') + alias.labels.join(QLatin1Char(' '))
                           + QLatin1Char(' ') + alias.text;
            item.run = [guard = QPointer<Pane>(pane), name = alias.name] {
                if (guard) guard->runAlias(name, QString(), true);
            };
            items << item;
        }
        {
            PaletteItem import; import.key = QStringLiteral("alias:import"); import.section = section;
            import.label = QStringLiteral("Import Warp workflows and shell aliases…");
            import.detail = QStringLiteral("Preview first; nothing runs and nothing is written until you confirm");
            import.run = [guard = QPointer<Pane>(pane)] { if (guard) guard->openAliasImport(); };
            items << import;
        }
        return items;
    }

    QList<PaletteItem> agentsMenuItems() {
        QList<PaletteItem> children;
        Pane *pane = m_active;
        if (!pane) return children;
        const auto &model = pane->subagents();
        for (const auto &row : model.rows()) {
            PaletteItem item;
            const QString id = row.id;
            item.key = QStringLiteral("subagent:") + id; item.section = QStringLiteral("Agents");
            item.label = QStringLiteral("%1 %2 %3 · %4").arg(relay::SubagentModel::statusIcon(row.status), row.type, row.id, row.description);
            item.detail = QStringLiteral("%1 · %2 · %3 tools · open the transcript").arg(row.status, relay::SubagentModel::formatElapsed(model.elapsedNow(row))).arg(row.tools);
            QPointer<Pane> guard(pane);
            item.run = [guard, id] { if (guard) guard->openSubagent(id); };
            children << item;
        }
        for (const auto &value : model.definitions()) {
            const QJsonObject def = value.toObject();
            const QString name = def.value(QStringLiteral("name")).toString();
            QStringList tools;
            for (const auto &tool : def.value(QStringLiteral("tools")).toArray()) tools << tool.toString();
            PaletteItem item;
            item.key = QStringLiteral("agentdef:") + name; item.section = QStringLiteral("Agents");
            item.label = QStringLiteral("%1   %2 · model %3 · %4").arg(name, def.value(QStringLiteral("source")).toString(),
                                                                   def.value(QStringLiteral("model")).toString(),
                                                                   tools.isEmpty() ? QStringLiteral("no tools") : tools.join(QStringLiteral(", ")));
            item.detail = def.value(QStringLiteral("description")).toString() + QStringLiteral("\nEnter: ask the main agent to use it");
            QPointer<Pane> guard(pane);
            item.run = [guard, name] { if (guard) guard->setComposerText(QStringLiteral("Use the %1 agent to ").arg(name)); };
            children << item;
        }
        PaletteItem reload;
        reload.key = QStringLiteral("agents.reload"); reload.section = QStringLiteral("Agents");
        reload.label = model.definitions().isEmpty() ? QStringLiteral("Load agent definitions") : QStringLiteral("Reload agent definitions");
        reload.detail = model.definitionWarnings().join('\n');
        reload.stayOpen = true;
        QPointer<Pane> guard(pane);
        reload.run = [guard] { if (guard) guard->refreshAgentDefinitions(); };
        children << reload;
        return children;
    }

    void openAgentsMenu() {
        openSettingsPane(relay::SettingsPane::Mode::Actions);
        if (ToolPane *tool = settingsPaneIn(m_tabs->currentWidget())) tool->settings()->scrollToGroup(QStringLiteral("menu:agents"));
    }

    // The subagent pane (card #WD83): one per main pane, a tab per subagent, split beside its
    // owner the first time and brought forward (tab page, focus, the subagent's tab) after that.
    ToolPane *subagentPaneOf(Pane *owner) const {
        auto *tabs = owner ? owner->subagentTabs() : nullptr;
        return tabs ? dynamic_cast<ToolPane *>(tabs->parentWidget()) : nullptr;
    }

    ToolPane *createSubagentPane(const QString &cwd, const QJsonObject &saved = {}) {
        auto *tabs = new relay::SubagentTabsView;
        if (!saved.isEmpty()) tabs->restore(saved);
        auto *tool = new ToolPane(tabs, cwd);
        tool->setProperty("paneType", QStringLiteral("subagent"));   // pane-type header colours
        relay::theme::polishWindow(tool);
        QPointer<ToolPane> guard(tool);
        // The last tab closed (by its ×, or its row left the list): the pane goes with it.
        tabs->onEmpty = [guard] {
            if (guard) QTimer::singleShot(0, guard.data(), [guard] { if (auto *w = windowOf(guard)) w->closePane(guard, false); });
        };
        tabs->onTitleChanged = [guard] { if (auto *w = windowOf(guard)) w->updateTitles(); };
        // Until it has an owner (a restored pane whose pane did not come back): the tab's first pane.
        tabs->onBackToMain = [guard] {
            auto *w = windowOf(guard);
            const auto panes = w ? panesIn(w->pageOf(guard)) : QList<Pane *>();
            if (!panes.isEmpty()) w->backToMainAgent(panes.first());
        };
        return tool;
    }

    void linkSubagentPane(ToolPane *tool, Pane *owner) {
        if (!tool || !tool->subagent() || !owner) return;
        owner->adoptSubagentTabs(tool->subagent());
        QPointer<Pane> ownerGuard(owner);
        QPointer<ToolPane> guard(tool);
        tool->subagent()->onBackToMain = [ownerGuard] { if (auto *w = windowOf(ownerGuard)) w->backToMainAgent(ownerGuard); };
        // The subagents belong to the owner's worker: the pane closes with it.
        connect(owner, &QObject::destroyed, tool, [guard] {
            if (guard) QTimer::singleShot(0, guard.data(), [guard] { if (auto *w = windowOf(guard)) w->closePane(guard, false); });
        });
    }

    // "← main agent", Esc in the subagent pane, and the subagent pane key from inside it.
    void backToMainAgent(Pane *owner) {
        if (!owner) return;
        if (QWidget *page = pageOf(owner)) m_tabs->setCurrentWidget(page);
        setActiveLeaf(owner);
        owner->focusInput();
    }

    // agent.subagentPane (Alt+A): in the subagent pane, back to its main agent; anywhere else, this
    // pane's subagent pane.
    void toggleSubagentPane() {
        if (auto *tool = dynamic_cast<ToolPane *>(leafOf(QApplication::focusWidget())); tool && tool->subagent()) {
            if (tool->subagent()->onBackToMain) tool->subagent()->onBackToMain();
            return;
        }
        if (m_active) m_active->openSubagentPane();
    }

    // Links saved subagent panes to the panes they belonged to, once the layout is built.
    void linkRestoredSubagentPane(ToolPane *tool, const QString &ownerKey) {
        if (!tool || ownerKey.isEmpty()) return;
        for (int i = 0; i < m_tabs->count(); ++i)
            for (Pane *pane : panesIn(m_tabs->widget(i)))
                if (pane->scrollbackId() == ownerKey && !pane->subagentTabs()) { linkSubagentPane(tool, pane); return; }
    }
    // ----- end subagents UI --------------------------------------------------------------------

public:
    // Opens `subagentId`'s tab in `ownerPane`'s subagent pane: splits the pane beside the owner if
    // it is not open, else brings it forward (its tab page, focus) and switches to the tab. The
    // strip, the palette's Agents list, /agents, ✦ links and Alt+A all come here (card #WD83).
    void openSubagentTab(Pane *ownerPane, const QString &subagentId) {
        if (!ownerPane || subagentId.isEmpty()) return;
        if (!ownerPane->canShowSubagent(subagentId)) { ownerPane->toast(QStringLiteral("No subagent %1 in this pane.").arg(subagentId)); return; }
        ToolPane *tool = subagentPaneOf(ownerPane);
        if (!tool) {
            tool = createSubagentPane(ownerPane->cwd());
            linkSubagentPane(tool, ownerPane);
            // Beside the owner; below it when the owner is too narrow to share its width.
            insertBeside(ownerPane, tool, ownerPane->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
        }
        ownerPane->showSubagentTab(subagentId);
        RelayWindow *w = windowOf(tool);
        if (!w) return;
        if (QWidget *page = w->pageOf(tool)) w->m_tabs->setCurrentWidget(page);
        if (w != this) { w->raise(); w->activateWindow(); }
        w->setActiveLeaf(tool);
        focusLeaf(tool);
        w->updateTitles();
    }

    // Turn details: tool calls of one agent turn, opened from the inline link or the palette.
    void openTurnPane(Pane *owner, const QString &turnId) {
        QWidget *page = pageOf(owner);
        if (!page || turnId.isEmpty()) return;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->turn() && tool->turn()->turnId() == turnId) {
                setActiveLeaf(tool); focusLeaf(tool); return;
            }
        auto *view = new relay::TurnTranscriptView(turnId);
        auto *tool = new ToolPane(view, owner->cwd());
        relay::theme::polishWindow(tool);
        tool->setObjectName(QStringLiteral("pane"));
        owner->requestTurn(turnId, view);
        insertBeside(owner, tool, Qt::Horizontal, false);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }
    // ----- Switchboard (docs/SWITCHBOARD-DESIGN.md 4, protocol 17) -----------------------------
    // Ctrl+Shift+S: open the Switchboard beside the anchor, focus the one this tab already has,
    // or, pressed on it, go back to the last terminal pane.
    void toggleBoardPane() {
        const QString workspace = boardWorkspace();
        if (workspace.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("This workspace has no Switchboard yet "
                                                    "(issues/board.yaml is missing)."), 9000);
            return;
        }
        QWidget *page = m_tabs->currentWidget();
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data()); tool && tool->board()) {
            if (m_active) { setActiveLeaf(m_active); focusLeaf(m_active); }
            return;
        }
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) {
                setActiveLeaf(tool); focusLeaf(tool); return;
            }
        QWidget *anchor = m_activeLeaf ? m_activeLeaf.data() : static_cast<QWidget *>(m_active.data());
        auto *tool = createBoardPane(workspace);
        if (!tool) return;
        if (anchor) insertBeside(anchor, tool, Qt::Horizontal, false);
        else if (page && page->layout()) page->layout()->addWidget(tool);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }

    // One card in this tab's Switchboard, opening the Switchboard first if the tab has none.
    void openBoardCard(const QString &id) {
        auto boardInTab = [this]() -> ToolPane * {
            for (QWidget *leaf : leavesIn(m_tabs->currentWidget()))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) return tool;
            return nullptr;
        };
        if (!boardInTab()) toggleBoardPane();
        ToolPane *tool = boardInTab();
        if (!tool) return;
        tool->board()->selectCard(id);
        tool->board()->openSelected();
        setActiveLeaf(tool);
        focusLeaf(tool);
        // A Switchboard this call just opened has no rows yet, so openSelected() had nothing to
        // open and only the selection survives (the pane restores it when the rows land). Keep
        // asking while they arrive, so one click on a `#K7Q2` in the output really does end on
        // the card and not merely near it (2026-09-18).
        if (!tool->board()->model().card(id)) waitForBoardCard(tool, id, 0);
    }

    // Retries openSelected() every 250 ms for up to 6 s, which covers the worker's first answer
    // on a large tree. It stops as soon as a card detail is open, so a card the *user* opened in
    // the meantime is never yanked out from under them.
    void waitForBoardCard(ToolPane *tool, const QString &id, int attempt) {
        if (attempt >= 24) return;
        QPointer<ToolPane> guard(tool);
        QTimer::singleShot(250, this, [this, guard, id, attempt] {
            ToolPane *pane = guard.data();
            if (!pane || !pane->board() || pane->board()->detailOpen()) return;
            if (!pane->board()->model().card(id)) { waitForBoardCard(pane, id, attempt + 1); return; }
            pane->board()->selectCard(id);
            pane->board()->openSelected();
        });
    }

    // The nearest ancestor of the anchor pane's directory that has a Switchboard.
    QString boardWorkspace() const {
        QStringList candidates;
        if (m_active) candidates << m_active->workspace() << m_active->cwd();
        candidates << m_manager->workspace() << QDir::currentPath();
        for (const QString &candidate : candidates) {
            if (candidate.isEmpty()) continue;
            for (QDir dir(candidate); ; ) {
                if (QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("issues/board.yaml"))))
                    return dir.absolutePath();
                if (!dir.cdUp()) break;
            }
        }
        return QString();
    }

    relay::BoardWorker *boardWorker() {
        if (!m_boardWorker) {
            m_boardWorker = new relay::BoardWorker(
                QStandardPaths::findExecutable(QStringLiteral("python3")), dataRoot(), this);
            QPointer<RelayWindow> guard(this);
            m_boardWorker->onEvent = [guard](const QJsonObject &event) {
                if (!guard) return;
                for (int i = 0; i < guard->m_tabs->count(); ++i)
                    for (QWidget *leaf : leavesIn(guard->m_tabs->widget(i)))
                        if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board())
                            tool->board()->handleEvent(event);
            };
            m_boardWorker->onStatus = [guard](const QString &text) {
                if (guard) guard->statusBar()->showMessage(text, 9000);
            };
        }
        return m_boardWorker;
    }

    // The Switchboard worker runs an ordinary agent on the `switchboard` role (protocol 13), so
    // card threads never enter a pane's conversation.
    //
    // It names its provider the way a pane does — by the preset — rather than pasting an endpoint
    // together out of separate settings keys. `provider/preset` is rewritten on every model switch
    // (the model chip, /model, the palette), while `provider/base`, `provider/model` and
    // `provider/extra` are only rewritten by a full re-configure, so from the first switch onwards
    // the four disagree. A configure built from all four names one preset and carries another
    // provider's URL, and the worker then posts the named preset's stored key to a foreign
    // endpoint: HTTP 401, with nothing on screen saying which provider refused (owner report,
    // 2026-09-18, "ask the agent didnt work. it said provider HTTP 401"). A named preset therefore
    // travels alone and the worker fills in that preset's own base URL, model and extra
    // (protocol 1). Only a custom endpoint, which has no preset to resolve, still carries them.
    void startBoardWorker(const QString &workspace) {
        QSettings settings;
        const QString preset = settings.value(QStringLiteral("provider/preset")).toString();
        const bool named = !preset.isEmpty() && preset != QStringLiteral("custom");
        QJsonObject configure{{QStringLiteral("type"), QStringLiteral("configure")},
                              {QStringLiteral("workspace"), workspace},
                              {QStringLiteral("agent_role"), QStringLiteral("switchboard")},
                              {QStringLiteral("use_stored_key"), true},
                              {QStringLiteral("api_key"), QString()},
                              {QStringLiteral("max_tokens"), settings.value(QStringLiteral("provider/max_tokens"), 32768).toInt()}};
        if (!preset.isEmpty()) configure.insert(QStringLiteral("preset"), preset);
        if (!named) {
            configure.insert(QStringLiteral("base_url"), settings.value(QStringLiteral("provider/base")).toString());
            configure.insert(QStringLiteral("model"), settings.value(QStringLiteral("provider/model")).toString());
            const QJsonObject extra = QJsonDocument::fromJson(
                settings.value(QStringLiteral("provider/extra")).toString().toUtf8()).object();
            if (!extra.isEmpty()) configure.insert(QStringLiteral("extra"), extra);
        }
        const QJsonObject roles = Pane::rolesObject();
        if (!roles.isEmpty()) configure.insert(QStringLiteral("roles"), roles);
        const QJsonObject tiers = Pane::tiersObject();
        if (!tiers.isEmpty()) configure.insert(QStringLiteral("tiers"), tiers);
        boardWorker()->start(configure);
    }

    ToolPane *createBoardPane(const QString &workspace, const QJsonArray &collapsed = {},
                              const QJsonArray &hidden = {}) {
        auto *view = new relay::BoardView(workspace);
        if (!collapsed.isEmpty()) view->setCollapsedSections(collapsed);
        if (!hidden.isEmpty()) view->setHiddenSections(hidden);
        auto *tool = new ToolPane(view, workspace);
        relay::theme::polishWindow(tool);
        tool->setObjectName(QStringLiteral("pane"));
        QPointer<ToolPane> guard(tool);
        view->onSend = [this](const QJsonObject &message) { boardWorker()->send(message); };
        view->onStatus = [guard](const QString &text) {
            if (auto *w = windowOf(guard); w && !text.isEmpty()) w->statusBar()->showMessage(text, 9000);
        };
        view->onTitleChanged = [guard](const QString &) { if (auto *w = windowOf(guard)) w->updateTitles(); };
        view->onOpenFile = [guard](const QString &path) {
            if (auto *w = windowOf(guard)) w->openPath(path, 0, guard);
        };
        view->onSendToTerminal = [guard](const QString &reference) {
            auto *w = windowOf(guard);
            if (!w || !w->m_active) return;
            w->m_active->insertInComposer(reference);
            w->setActiveLeaf(w->m_active);
            w->m_active->focusInput();
        };
        // Execute (#XS6Q): a new terminal pane beside the board, in the board's workspace, on
        // the main agent (it builds the card), handed the card as its first task.
        view->onExecuteCard = [guard, workspace](const QString &card, const QString &task) {
            auto *w = windowOf(guard);
            if (!w) return;
            Pane *pane = nullptr;
            try { pane = w->createPane({{"cwd", workspace}, {"workspace", workspace}, {"agent_role", "main"}}); }
            catch (const std::exception &error) { w->statusBar()->showMessage(QString::fromUtf8(error.what()), 9000); return; }
            w->insertBeside(guard, pane, Qt::Horizontal, false);
            w->setActive(pane);
            focusLeaf(pane);
            pane->startBoardTask(task, card);
            w->updateTitles();
        };
        view->onHint = [guard](const QString &id, const QString &keys) {
            auto *w = windowOf(guard);
            if (!w || keys.isEmpty()) return;
            w->hint(QStringLiteral("board.") + id, relay::ShortcutHints::nextTime(keys));
        };
        startBoardWorker(workspace);
        boardWorker()->open();
        return tool;
    }

private:

    // ----- panes ------------------------------------------------------------------------------
    static RelayWindow *windowOf(QWidget *widget) { return widget ? dynamic_cast<RelayWindow *>(widget->window()) : nullptr; }

    static Pane *paneOf(QWidget *widget) {
        for (QWidget *w = widget; w; w = w->parentWidget())
            if (auto *pane = dynamic_cast<Pane *>(w)) return pane;
        return nullptr;
    }

    static bool isLeaf(QWidget *widget) { return dynamic_cast<Pane *>(widget) || dynamic_cast<ToolPane *>(widget); }

    static QWidget *leafOf(QWidget *widget) {
        for (QWidget *w = widget; w; w = w->parentWidget())
            if (isLeaf(w)) return w;
        return nullptr;
    }

    // Terminal and tool panes in visual order.
    static QList<QWidget *> leavesIn(QWidget *root) {
        QList<QWidget *> leaves;
        if (!root) return leaves;
        if (isLeaf(root)) { leaves.append(root); return leaves; }
        if (auto *splitter = dynamic_cast<QSplitter *>(root)) {
            for (int i = 0; i < splitter->count(); ++i) leaves += leavesIn(splitter->widget(i));
            return leaves;
        }
        const auto children = root->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget *child : children) leaves += leavesIn(child);
        return leaves;
    }

    static void focusLeaf(QWidget *leaf) {
        if (auto *pane = dynamic_cast<Pane *>(leaf)) pane->focusInput();
        else if (auto *tool = dynamic_cast<ToolPane *>(leaf)) tool->focusInput();
    }

    static QString leafCwd(QWidget *leaf) {
        if (auto *pane = dynamic_cast<Pane *>(leaf)) return pane->cwd();
        if (auto *tool = dynamic_cast<ToolPane *>(leaf)) return tool->cwd();
        return {};
    }

    ToolPane *createToolPane(ToolPane::Kind kind, const QString &path, bool planActions = true) {
        auto *tool = new ToolPane(kind, path, planActions);
        QPointer<ToolPane> guard(tool);
        if (tool->explorer()) {
            relay::FileExplorer *explorer = tool->explorer();
            explorer->onOpenFile = [guard](const QString &file) { if (auto *w = windowOf(guard)) w->openPath(file, 0, guard); };
            explorer->onDirectoryChanged = [guard](const QString &) { if (auto *w = windowOf(guard)) w->updateTitles(); };
            // Right-click menu entries the window owns (issue #D60R).
            explorer->onOpenInPreview = [guard](const QString &file) { if (auto *w = windowOf(guard)) w->openPath(file, 0, guard); };
            explorer->onNavigateHere = [guard](const QString &dir) { if (auto *w = windowOf(guard)) w->navigateTerminalTo(dir, guard); };
            explorer->onSetWorkspace = [guard](const QString &dir) { if (auto *w = windowOf(guard)) w->setAgentWorkspace(dir, guard); };
            explorer->onCloseRequested = [guard] { if (auto *w = windowOf(guard)) w->closePane(guard, true); };
        } else if (tool->preview()) {
            tool->preview()->onTitleChanged = [guard](const QString &) { if (auto *w = windowOf(guard)) w->updateTitles(); };
            // A link inside the preview opens its own pane beside this one (issue S1JP).
            tool->preview()->onOpenLink = [guard](const QString &file) { if (auto *w = windowOf(guard)) w->openPath(file, 0, guard, true); };
        } else {
            tool->plan()->onTitleChanged = [guard](const QString &) { if (auto *w = windowOf(guard)) w->updateTitles(); };
        }
        relay::theme::polishWindow(tool);
        tool->setObjectName(QStringLiteral("pane"));
        return tool;
    }

    QWidget *pageOf(QWidget *widget) const {
        for (QWidget *w = widget; w; w = w->parentWidget())
            if (m_tabs->indexOf(w) >= 0) return w;
        return nullptr;
    }

    static QList<Pane *> panesIn(QWidget *root) {
        QList<Pane *> panes;
        if (!root) return panes;
        if (auto *pane = dynamic_cast<Pane *>(root)) { panes.append(pane); return panes; }
        // Walk the layout tree in visual order (splitter child order).
        if (auto *splitter = dynamic_cast<QSplitter *>(root)) {
            for (int i = 0; i < splitter->count(); ++i) panes += panesIn(splitter->widget(i));
            return panes;
        }
        const auto children = root->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget *child : children) panes += panesIn(child);
        return panes;
    }

    QList<Pane *> allPanes() const {
        QList<Pane *> panes;
        for (int i = 0; i < m_tabs->count(); ++i) panes += panesIn(m_tabs->widget(i));
        return panes;
    }

    Pane *createPane(const QJsonObject &spec) {
        // A directory that no longer exists falls back to the pane's workspace, then to this
        // window's workspace, then to $HOME (src/WindowState.h).
        const QString fallback = relay::windowstate::resolveDirectory(m_manager->workspace(), QString(), QDir::homePath());
        const QString workspace = relay::windowstate::resolveDirectory(spec.value(QStringLiteral("workspace")).toString(), fallback, fallback);
        const QString cwd = relay::windowstate::resolveDirectory(spec.value(QStringLiteral("cwd")).toString(), workspace, workspace);
        // The emulator core: the restored session's, else the process default from
        // --engine-core / RELAY_ENGINE_CORE. A session saved before KonsolePart was retired may
        // still carry an "engine" key; it is ignored, and every pane gets Relay's engine.
        const QString core = spec.value(QStringLiteral("engine_core")).toString(relay::defaultEngineCore());
        auto *pane = new Pane(workspace, cwd, m_manager->cleanShell(), core);
        // Model roles (protocol 13): panes opened after the first one default to the Flash agent.
        // A layout saved before 2026-09-18 calls that role "fast"; it is read as "flash" and saved
        // back under the new name (Pane::canonicalRole).
        const QString savedRole = Pane::canonicalRole(spec.value(QStringLiteral("agent_role")).toString());
        if (!savedRole.isEmpty()) pane->initAgentRole(savedRole);
        else if (Pane::newPanesUseFlashAgent() && !allPanes().isEmpty()) pane->initAgentRole(QStringLiteral("flash"));
        // Saved window layout: model/effort/mode and the conversation to reattach.
        pane->initRestore(spec);
        QPointer<Pane> guard(pane);
        // Callbacks find the pane's current window, so panes and tabs can move between windows.
        pane->onStatus = [guard](const QString &text) { if (guard) guard->toast(text, 5000); };
        pane->onStateChanged = [guard] {
            auto *w = windowOf(guard);
            if (!w) return;
            w->updateTitles();
        };
        pane->onShellExited = [guard] { if (auto *w = windowOf(guard)) w->closePane(guard, false); };
        pane->onOpenPath = [guard](const QString &path, int line) { if (auto *w = windowOf(guard)) w->openPath(path, line, guard); };
        pane->onToggleExplorer = [guard](const QString &path) { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->toggleExplorer(path, guard); } };
        pane->onOpenBoard = [guard] { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->toggleBoardPane(); } };
        pane->onOpenCard = [guard](const QString &id) { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->openBoardCard(id); } };
        // Right-click menu entries the window owns (issue #X2F1).
        pane->onWindowAction = [guard](const QString &action) {
            auto *w = windowOf(guard);
            if (!w) return;
            w->setActiveLeaf(guard);
            if (action == QStringLiteral("splitRight")) w->runAction(QStringLiteral("pane.splitRight"));
            else if (action == QStringLiteral("splitDown")) w->runAction(QStringLiteral("pane.splitDown"));
            else if (action == QStringLiteral("close")) w->closePane(guard, true);
        };
        pane->onPlanWritten = [guard](const QString &path, Pane *) { if (auto *w = windowOf(guard)) w->openDocument(path, guard, true); };
        pane->onOpenDocument = [guard](const QString &path) { if (auto *w = windowOf(guard)) w->openDocument(path, guard, false); };
        pane->onForkState = [guard](const QJsonObject &state, const QString &title) { if (auto *w = windowOf(guard)) w->openFork(guard, state, title); };
        pane->onOpenSessionInNewPane = [guard](const QJsonObject &state, const QString &title) { if (auto *w = windowOf(guard)) w->openFork(guard, state, title, false); };
        pane->onOpenSubagent = [guard](const QString &id) { if (auto *w = windowOf(guard)) w->openSubagentTab(guard, id); };   // subagents UI (#WD83)
        pane->onShowAgents = [guard] { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->openAgentsMenu(); } };   // /agents → subagents panel menu
        pane->onOpenTurn = [guard](const QString &turnId) { if (auto *w = windowOf(guard)) w->openTurnPane(guard, turnId); };
        // Pane titles (issue JRWQ): a fresh title relabels the tab; /rename-tab is the window's job.
        pane->onTitleChanged = [guard] { if (auto *w = windowOf(guard)) w->updateTitles(); };
        pane->onTabLabel = [guard](const QString &id, const QString &label, bool related) {
            if (auto *w = windowOf(guard)) w->applyTabJudgement(id, label, related);
        };
        pane->onRenameTab = [guard](const QString &text, bool edit) {
            auto *w = windowOf(guard);
            if (w) w->renameTab(text, edit, w->pageOf(guard));
        };
        relay::theme::polishWindow(pane);
        return pane;
    }

    QWidget *buildNode(const QJsonObject &node) {
        if (node.contains(QStringLiteral("split"))) {
            auto *splitter = newSplitter(node.value(QStringLiteral("split")).toString() == QStringLiteral("v") ? Qt::Vertical : Qt::Horizontal);
            const auto children = node.value(QStringLiteral("children")).toArray();
            for (const auto &child : children) splitter->addWidget(buildNode(child.toObject()));
            QList<int> sizes;
            for (const auto &size : node.value(QStringLiteral("sizes")).toArray()) sizes.append(size.toInt());
            if (sizes.size() == splitter->count()) splitter->setSizes(sizes);
            return splitter;
        }
        if (node.contains(QStringLiteral("board"))) {
            const QJsonObject board = node.value(QStringLiteral("board")).toObject();
            const QString workspace = board.value(QStringLiteral("workspace")).toString();
            if (QFileInfo::exists(workspace + QStringLiteral("/issues/board.yaml")))
                return createBoardPane(workspace, board.value(QStringLiteral("collapsed")).toArray(),
                                       board.value(QStringLiteral("hidden")).toArray());
            return createPane({{"cwd", m_manager->workspace()}, {"workspace", m_manager->workspace()}});
        }
        if (node.contains(QStringLiteral("subagents"))) {   // card #WD83: the tabs' text, then its owner
            const QJsonObject saved = node.value(QStringLiteral("subagents")).toObject();
            ToolPane *tool = createSubagentPane(saved.value(QStringLiteral("cwd")).toString(), saved);
            QPointer<ToolPane> guard(tool);
            const QString owner = saved.value(QStringLiteral("owner")).toString();
            QTimer::singleShot(0, tool, [guard, owner] { if (auto *w = windowOf(guard)) w->linkRestoredSubagentPane(guard, owner); });
            return tool;
        }
        if (node.contains(QStringLiteral("plan"))) {
            const QString path = node.value(QStringLiteral("plan")).toObject().value(QStringLiteral("path")).toString();
            if (QFileInfo::exists(path)) return createToolPane(ToolPane::Kind::Plan, path);
            return createPane({{"cwd", m_manager->workspace()}, {"workspace", m_manager->workspace()}});
        }
        if (node.contains(QStringLiteral("explorer")) || node.contains(QStringLiteral("preview"))) {
            const bool explorer = node.contains(QStringLiteral("explorer"));
            const QString path = node.value(explorer ? QStringLiteral("explorer") : QStringLiteral("preview")).toObject().value(QStringLiteral("path")).toString();
            if (QFileInfo::exists(path)) return createToolPane(explorer ? ToolPane::Kind::Explorer : ToolPane::Kind::Preview, path);
            return createPane({{"cwd", m_manager->workspace()}, {"workspace", m_manager->workspace()}});
        }
        return createPane(node.value(QStringLiteral("pane")).toObject());
    }

    QSplitter *newSplitter(Qt::Orientation orientation) {
        auto *splitter = new QSplitter(orientation);
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(3);
        // Saved window layout: dragging a divider changes the sizes that come back on restart.
        connect(splitter, &QSplitter::splitterMoved, this, [this](int, int) { m_manager->scheduleSave(); });
        return splitter;
    }

    QJsonObject serializeNode(QWidget *widget) const {
        if (auto *pane = dynamic_cast<Pane *>(widget)) {
            // One node shape for both users: "restore last closed" (Ctrl+Shift+W) and the saved
            // window layout (src/WindowState.h). Everything a pane needs to come back lives here.
            QJsonObject leaf{{"cwd", pane->cwd()}, {"workspace", pane->workspace()},
                             {"agent_role", pane->agentRole()},
                             {"agent_mode", pane->agentMode()},
                             {"input_mode", pane->mode()},
                             {"effort", pane->effort()}};
            // An empty core means "whatever the process default is"; storing it would pin the empty
            // string and defeat --engine-core on the next start.
            if (!pane->engineCore().isEmpty()) leaf.insert(QStringLiteral("engine_core"), pane->engineCore());
            if (!pane->currentPreset().isEmpty()) leaf.insert(QStringLiteral("preset"), pane->currentPreset());
            if (!pane->model().isEmpty()) leaf.insert(QStringLiteral("model"), pane->model());
            if (!pane->sessionId().isEmpty()) leaf.insert(QStringLiteral("session_id"), pane->sessionId());
            // Which file holds this pane's terminal text (src/WindowState.h). The id is in every
            // node, including "restore last closed": a reopened pane finds the text of the pane it
            // came from, and the ids in the saved layout are what keeps the store pruned.
            leaf.insert(QStringLiteral("scrollback"), pane->scrollbackId());
            return {{"pane", leaf}};
        }
        if (auto *tool = dynamic_cast<ToolPane *>(widget)) return tool->node();
        if (auto *splitter = dynamic_cast<QSplitter *>(widget)) {
            QJsonArray children, sizes;
            const QList<int> all = splitter->sizes();
            for (int i = 0; i < splitter->count(); ++i) {
                const QJsonObject child = serializeNode(splitter->widget(i));
                if (child.isEmpty()) continue;   // subagent transcripts are not saved
                children.append(child); sizes.append(all.value(i));
            }
            if (children.isEmpty()) return {};
            if (children.size() == 1) return children.first().toObject();
            return {{"split", splitter->orientation() == Qt::Vertical ? "v" : "h"}, {"children", children}, {"sizes", sizes}};
        }
        return {};
    }

    QJsonObject serializeTab(int index) const {
        QWidget *page = m_tabs->widget(index);
        QWidget *root = page && page->layout() && page->layout()->count() ? page->layout()->itemAt(0)->widget() : nullptr;
        return serializeNode(root);
    }

    void setActive(Pane *pane) { setActiveLeaf(pane); }

    // Any pane can be the focused leaf; agent and terminal actions use the last terminal pane.
    void setActiveLeaf(QWidget *leaf) {
        if (!leaf) return;
        if (m_activeLeaf != leaf) {
            if (m_activeLeaf) m_activeLeaf->setProperty("relayActive", false);
            m_activeLeaf = leaf;
            leaf->setProperty("relayActive", true);
            for (int i = 0; i < m_tabs->count(); ++i)
                for (QWidget *w : leavesIn(m_tabs->widget(i))) { repolishLeaf(w); }
        }
        QWidget *page = pageOf(leaf);
        if (auto *pane = dynamic_cast<Pane *>(leaf)) m_active = pane;
        else if (!m_active || pageOf(m_active) != page) {
            const auto panes = panesIn(page);
            m_active = panes.isEmpty() ? nullptr : panes.first();
        }
        if (page) m_lastActive.insert(page, leaf);
        // Clickable paths: only the active pane opens a link on a plain click, so the click
        // that moves the focus into another pane cannot open a file by accident (issue YZTK).
        for (int i = 0; i < m_tabs->count(); ++i)
            for (Pane *p : panesIn(m_tabs->widget(i))) p->setLinkClicksArmed(p == m_active);
        updateTitles();
    }

    // The pane frame and its composer both follow "relayActive", so both have to be repolished.
    static void repolishLeaf(QWidget *leaf) {
        const bool active = leaf->property("relayActive").toBool();
        QList<QWidget *> widgets{leaf};
        for (auto *editor : leaf->findChildren<QPlainTextEdit *>(QStringLiteral("composerEditor")))
            if (auto *frame = qobject_cast<QFrame *>(editor->parentWidget())) {
                frame->setProperty("relayActive", active);
                widgets.append(frame);
            }
        for (QWidget *w : widgets) { w->style()->unpolish(w); w->style()->polish(w); w->update(); }
    }

    static QString shortPath(const QString &path) {
        const QString home = QDir::homePath();
        if (path == home) return QStringLiteral("~");
        const QString name = QFileInfo(path).fileName();
        return name.isEmpty() ? path : name;
    }

    // ----- tab labels (issue JRWQ) -------------------------------------------------------------
    // A tab is labelled from the titles its panes already carry, so it costs no extra model call:
    // one phrase when the panes are on the same work, the titles joined with "; " when they are
    // not. Which of the two is a cheap chores-role judgement in the worker (`tab_label`); until it
    // answers, and whenever no model is configured, the plain-text comparison decides.
    QStringList paneTitlesIn(QWidget *page) const {
        QStringList titles;
        for (QWidget *leaf : leavesIn(page)) {
            if (auto *pane = dynamic_cast<Pane *>(leaf))
                titles << (pane->paneTitle().isEmpty() ? shortPath(pane->cwd()) : pane->paneTitle());
            else if (auto *tool = dynamic_cast<ToolPane *>(leaf))
                titles << tool->title();
        }
        return relay::titles::distinct(titles);
    }

    QString tabLabelFor(QWidget *page, const QStringList &titles) const {
        const QString manual = m_tabNames.value(page);
        if (!manual.isEmpty()) return manual;
        if (titles.isEmpty()) return QStringLiteral("Relay");
        const bool related = m_tabRelated.contains(page) ? m_tabRelated.value(page)
                                                         : relay::titles::relatedText(titles);
        return relay::titles::join(titles, related, m_tabPhrase.value(page));
    }

    // Ask the tab's own worker whether its panes are on one job, but only when the titles changed.
    void refreshTabJudgement(QWidget *page, const QStringList &titles) {
        const QString key = titles.join(QChar(0x1f));
        if (m_tabKey.value(page) == key) return;
        m_tabKey.insert(page, key);
        m_tabRelated.remove(page);
        m_tabPhrase.remove(page);
        if (titles.size() < 2 || !m_tabNames.value(page).isEmpty()) return;
        QWidget *leaf = m_lastActive.value(page);
        Pane *pane = dynamic_cast<Pane *>(leaf);
        if (!pane)
            for (QWidget *candidate : leavesIn(page))
                if ((pane = dynamic_cast<Pane *>(candidate))) break;
        if (!pane) return;
        const QString id = QStringLiteral("tab-%1").arg(++m_tabLabelSerial);
        m_tabLabelRequests.insert(id, {QPointer<QWidget>(page), key});
        pane->requestTabLabel(id, titles);
    }

    void applyTabJudgement(const QString &id, const QString &phrase, bool related) {
        const auto request = m_tabLabelRequests.take(id);
        QWidget *page = request.first.data();
        // A pane title moved on while the worker was thinking: that answer is stale.
        if (!page || m_tabKey.value(page) != request.second) return;
        m_tabRelated.insert(page, related);
        m_tabPhrase.insert(page, related ? phrase : QString());
        updateTitles();
    }

    void forgetTab(QWidget *page) {
        m_tabNames.remove(page);
        m_tabKey.remove(page);
        m_tabRelated.remove(page);
        m_tabPhrase.remove(page);
    }

    void updateTitles() {
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            const auto leaves = leavesIn(page);
            QWidget *leaf = m_lastActive.value(page);
            if (!leaf && !leaves.isEmpty()) leaf = leaves.first();
            const QStringList titles = paneTitlesIn(page);
            refreshTabJudgement(page, titles);
            QString title = tabLabelFor(page, titles);
            if (leaves.size() > 1) title += QStringLiteral("  ·  %1").arg(leaves.size());
            const QFontMetrics metrics(m_tabs->tabBar()->font());
            m_tabs->setTabText(i, metrics.elidedText(title, Qt::ElideRight, 260));
            m_tabs->setTabToolTip(i, (titles.isEmpty() ? QString() : titles.join(QStringLiteral("\n")) + QStringLiteral("\n\n"))
                                     + (leaf ? leafCwd(leaf) : QString())
                                     + QStringLiteral("\n\nDouble click the tab to rename · /rename-tab"));
        }
        syncChrome();
        QString where = m_activeLeaf ? leafCwd(m_activeLeaf) : QString();
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data())) where = tool->path();
        setWindowTitle(where.isEmpty() ? QStringLiteral("Relay") : QStringLiteral("Relay — ") + where);
        // Saved window layout: this runs after every split, close, tab change, directory change
        // and model change, so it is the one place the debounced save hangs off.
        m_manager->scheduleSave();
    }

    // /rename-tab, or a double click on the tab: an editor over the tab itself. An empty name puts
    // the tab back under the pane titles.
    void renameTab(const QString &text, bool edit, QWidget *page = nullptr) {
        if (!page) page = m_tabs->currentWidget();
        if (!page) return;
        const int index = m_tabs->indexOf(page);
        if (index < 0) return;
        if (!edit) {
            if (text.isEmpty()) m_tabNames.remove(page);
            else m_tabNames.insert(page, relay::titles::clean(text, relay::titles::kMaxUserTitle, 0));
            m_tabKey.remove(page);   // re-ask the worker when the tab goes back to automatic
            updateTitles();
            return;
        }
        if (!m_tabEdit) {
            m_tabEdit = new QLineEdit(m_tabs->tabBar());
            m_tabEdit->setObjectName(QStringLiteral("paneTitleEdit"));
            m_tabEdit->installEventFilter(this);
            connect(m_tabEdit, &QLineEdit::returnPressed, this, [this] {
                const QString typed = m_tabEdit->text().trimmed();
                QWidget *target = m_tabEditPage.data();
                endTabRename();
                if (target) renameTab(typed, false, target);
            });
        }
        m_tabEditPage = page;
        const QRect rect = m_tabs->tabBar()->tabRect(index);
        m_tabEdit->setGeometry(rect.adjusted(4, 3, -4, -3));
        m_tabEdit->setText(m_tabNames.value(page, m_tabs->tabBar()->tabText(index)));
        m_tabEdit->selectAll();
        m_tabEdit->show();
        m_tabEdit->raise();
        m_tabEdit->setFocus(Qt::OtherFocusReason);
    }

    void endTabRename() {
        if (m_tabEdit) m_tabEdit->hide();
        m_tabEditPage = nullptr;
    }

    // Tab labels (issue JRWQ): names set by hand, and the worker's last "same work?" judgement per
    // tab page, keyed by the pane titles it was made for.
    QMap<QWidget *, QString> m_tabNames, m_tabKey, m_tabPhrase;
    QMap<QWidget *, bool> m_tabRelated;
    QMap<QString, QPair<QPointer<QWidget>, QString>> m_tabLabelRequests;
    int m_tabLabelSerial = 0;
    QLineEdit *m_tabEdit = nullptr;
    QPointer<QWidget> m_tabEditPage;

    void cycleTab(int delta) {
        if (m_tabs->count() < 2) return;
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + delta + m_tabs->count()) % m_tabs->count());
    }

    // Put `pane` beside `anchor` in the given orientation, reusing the anchor's splitter when
    // it already runs that way, otherwise wrapping the anchor in a new splitter.
    void insertBeside(QWidget *anchor, QWidget *pane, Qt::Orientation orientation, bool before) {
        QWidget *parent = anchor->parentWidget();
        if (auto *splitter = dynamic_cast<QSplitter *>(parent); splitter && splitter->orientation() == orientation) {
            const int index = splitter->indexOf(anchor);
            splitter->insertWidget(before ? index : index + 1, pane);
            QList<int> equal;
            for (int i = 0; i < splitter->count(); ++i) equal.append(1000);
            splitter->setSizes(equal);
        } else {
            auto *wrapper = newSplitter(orientation);
            if (auto *outer = dynamic_cast<QSplitter *>(parent)) {
                const int index = outer->indexOf(anchor);
                outer->replaceWidget(index, wrapper);
            } else if (parent && parent->layout()) {
                delete parent->layout()->replaceWidget(anchor, wrapper);
            }
            wrapper->addWidget(before ? static_cast<QWidget *>(pane) : static_cast<QWidget *>(anchor));
            wrapper->addWidget(before ? static_cast<QWidget *>(anchor) : static_cast<QWidget *>(pane));
            anchor->show(); wrapper->show();
            // Size after the layout settles; sizes set on a hidden splitter follow size hints instead.
            QPointer<QSplitter> guard(wrapper);
            QTimer::singleShot(0, wrapper, [guard] {
                if (!guard) return;
                const int total = guard->orientation() == Qt::Horizontal ? guard->width() : guard->height();
                guard->setSizes({total / 2, total - total / 2});
            });
        }
        pane->show();
        updateTitles();
    }

    void split(Qt::Orientation orientation) {
        splitToward(orientation == Qt::Horizontal ? relay::panes::Direction::Right : relay::panes::Direction::Down);
    }

    // A new pane on `direction`'s side of the focused one. `offerPlacement` opens the short window
    // in which Left, Up or Down re-dock it (issue #78BN); only the one-key "new pane" uses it.
    void splitToward(relay::panes::Direction direction, bool offerPlacement = false) {
        QWidget *anchor = m_activeLeaf;
        if (!anchor) return;
        Pane *pane = nullptr;
        const QString workspace = m_active ? m_active->workspace() : m_manager->workspace();
        QJsonObject spec{{"cwd", leafCwd(anchor)}, {"workspace", workspace}};
        try { pane = createPane(spec); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return; }
        insertBeside(anchor, pane, relay::panes::orientationFor(direction), relay::panes::towardStart(direction));
        setActive(pane);
        // Take the keyboard now and again once the splitter, the engine view and the pane's own
        // startup have settled. A deferred focus on its own loses to anything that focuses while
        // the pane is being inserted, and the pane then has no keyboard at all: nothing typed
        // reaches it and the window's shortcuts find no pane under the focus (#4PW5).
        focusLeaf(pane);
        QPointer<Pane> guard(pane);
        QTimer::singleShot(0, pane, [guard] { if (guard) guard->focusInput(); });
        QTimer::singleShot(120, pane, [this, guard] { if (guard && m_activeLeaf == guard.data()) guard->focusInput(); });
        if (offerPlacement) armPlacement(pane, anchor);
    }

    // ----- "new pane, then an arrow places it" (issue #78BN) --------------------------------------
    //
    // The new pane already exists and is running; an arrow only moves it, through the same code
    // Ctrl+Alt+arrow uses, so its shell, agent and scrollback are never restarted.
    void armPlacement(QWidget *pane, QWidget *anchor) {
        // Re-installing moves this filter to the front of the application's list, so the arrow is
        // seen here before the new pane's prompt box can treat it as cursor movement.
        qApp->installEventFilter(this);
        m_placementClock.start();
        m_placement.arm(0);
        m_placementPane = pane;
        m_placementAnchor = anchor;
        showPlacementHint(pane);
        // One timer, restarted on every arming, so the hint can never outlive its window.
        m_placementTimer.start(int(relay::panes::PlacementWindow::kTimeoutMs) + 20);
    }

    void endPlacement() {
        m_placementTimer.stop();
        m_placement.cancel();
        m_placementPane = nullptr;
        m_placementAnchor = nullptr;
        delete m_placementHint.data();   // a passive label: nothing is in the middle of an event on it
        m_placementHint = nullptr;
    }

    void showPlacementHint(QWidget *pane) {
        delete m_placementHint.data();
        m_placementHint = new QLabel(QStringLiteral("← ↑ ↓ to place"), this);
        m_placementHint->setObjectName(QStringLiteral("toast"));
        m_placementHint->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_placementHint->ensurePolished();
        m_placementHint->adjustSize();
        placePlacementHint(pane);
        m_placementHint->show();
        m_placementHint->raise();
        // The new pane has no geometry until the layout has run, so place it again once it does.
        QPointer<QWidget> guard(pane);
        QTimer::singleShot(0, this, [this, guard] { if (guard) placePlacementHint(guard); });
    }

    void placePlacementHint(QWidget *pane) {
        if (!m_placementHint || !pane || !isAncestorOf(pane)) return;
        const QRect area(pane->mapTo(this, QPoint(0, 0)), pane->size());
        // Low in the pane, but clear of the prompt box and its strip, and never off the window.
        const QSize size = m_placementHint->size();
        m_placementHint->move(std::clamp(area.center().x() - size.width() / 2, 0, std::max(0, width() - size.width())),
                              std::clamp(area.bottom() - size.height() - 120, area.top() + 8,
                                         std::max(0, height() - size.height())));
    }

    // The arrow: re-dock the new pane on that side of the pane it was split from. takeLeaf +
    // insertBeside is the same pair Ctrl+Alt+arrow uses when the two panes do not already share a
    // splitter, so the moved pane keeps its shell, its agent and its scrollback.
    void placeNewPane(relay::panes::Direction direction) {
        QPointer<QWidget> pane(m_placementPane), anchor(m_placementAnchor);
        endPlacement();
        if (!pane || !anchor || !pageOf(pane) || pageOf(pane) != pageOf(anchor)) return;
        if (direction != relay::panes::Direction::Right) {   // Right is where it already is
            if (!takeLeaf(pane) || !anchor || !pane) return;
            insertBeside(anchor, pane, relay::panes::orientationFor(direction), relay::panes::towardStart(direction));
        }
        setActiveLeaf(pane); focusLeaf(pane);
        updateTitles();
    }

    void navigate(relay::panes::Direction direction) {
        QWidget *current = m_activeLeaf;
        if (!current || !pageOf(current)) return;
        // A candidate must lie on the requested side; prefer the nearest, then the most aligned.
        if (QWidget *best = neighborOf(current, direction)) { setActiveLeaf(best); focusLeaf(best); }
    }

    // ----- pane chrome, tab bar controls and moving panes ------------------------------------------
    void buildTabBarControls() {
        QTabBar *bar = m_tabs->tabBar();
        bar->setMouseTracking(true);
        bar->installEventFilter(this);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        m_newTabButton = new ChromeButton(ChromeButton::Glyph::Plus, bar, 22);
        connect(m_newTabButton, &QToolButton::clicked, this, [this] {
            runAction(QStringLiteral("tab.new"));
            hint(QStringLiteral("tab.new.mouse"), relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("tab.new")), QStringLiteral("new tab")));
        });
        connect(bar, &QTabBar::tabMoved, this, [this](int, int) { placeTabBarControls(); });
        connect(bar, &QWidget::customContextMenuRequested, this, [this, bar](const QPoint &at) {
            const int index = bar->tabAt(at);
            QMenu menu(this);
            if (index >= 0) {
                auto *move = menu.addAction(QStringLiteral("Move to new window"));
                move->setShortcut(QKeySequence(Keymap::instance().keysFor(QStringLiteral("tab.moveToNewWindow")).value(0)));
                move->setEnabled(m_tabs->count() > 1);
                connect(move, &QAction::triggered, this, [this, index] { moveTabToNewWindow(index); });
                auto *close = menu.addAction(QStringLiteral("Close tab"));
                connect(close, &QAction::triggered, this, [this, index] { requestCloseTab(index); });
                menu.addSeparator();
            }
            auto *add = menu.addAction(QStringLiteral("New tab"));
            add->setShortcut(QKeySequence(Keymap::instance().keysFor(QStringLiteral("tab.new")).value(0)));
            connect(add, &QAction::triggered, this, [this] { runAction(QStringLiteral("tab.new")); });
            menu.exec(bar->mapToGlobal(at));
        });
    }

    // ----- window header: Relay icon, bell, actions, minimize/maximize/close -----------------------
    // Both corner widgets sit on the tab row, so the tabs, the header buttons and the window
    // controls share one line the way Warp does.
    static bool nativeFrame() { return QSettings().value(QStringLiteral("window/native_frame"), false).toBool(); }

    void buildWindowChrome() {
        auto *left = new QWidget;
        left->setObjectName(QStringLiteral("windowChromeLeft"));
        auto *leftRow = new QHBoxLayout(left);
        leftRow->setContentsMargins(12, 0, 10, 6);   // bottom inset centres the mark on the tab labels
        leftRow->setSpacing(0);
        auto *icon = new QLabel;
        icon->setObjectName(QStringLiteral("windowIcon"));
        // The app icon itself, the same one the launcher and the task bar show (owner, 2026-09-18:
        // "i want this icon everywhere"). It used to be the bare mark from the theme directory,
        // drawn without its tile on the grounds that the tile vanishes into the chrome at this
        // size; on a light theme the tile is what makes it read as the app's icon rather than a
        // stray chevron, and one icon in every place beats a better one in each.
        const QIcon appIcon = QApplication::windowIcon();
        if (appIcon.isNull()) icon->setText(QStringLiteral("◈"));
        else {
            // QIcon::pixmap() ignores the screen's scale factor, so ask for the device pixels.
            const qreal scale = qApp->devicePixelRatio();
            QPixmap mark = appIcon.pixmap(QSize(22, 22) * scale);
            mark.setDevicePixelRatio(scale);
            icon->setPixmap(mark);
        }
        icon->setToolTip(QStringLiteral("Relay"));
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);   // the whole corner drags the window
        leftRow->addWidget(icon);
        left->installEventFilter(this);
        m_tabs->setCornerWidget(left, Qt::TopLeftCorner);

        auto *right = new QWidget;
        right->setObjectName(QStringLiteral("windowChromeRight"));
        auto *rightRow = new QHBoxLayout(right);
        rightRow->setContentsMargins(6, 0, 6, 0);
        rightRow->setSpacing(2);
        m_bell = new ChromeButton(ChromeButton::Glyph::Bell);
        connect(m_bell, &QToolButton::clicked, this, [this] { toggleNotifications(); });
        rightRow->addWidget(m_bell);
        m_settingsButton = new ChromeButton(ChromeButton::Glyph::Gear);
        connect(m_settingsButton, &QToolButton::clicked, this, [this] {
            toggleSettingsPane(false);
            hint(QStringLiteral("chrome.settings"), relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("app.settings")), QStringLiteral("options")));
        });
        rightRow->addWidget(m_settingsButton);
        if (!m_nativeFrame) {
            rightRow->addSpacing(8);
            m_minimize = new ChromeButton(ChromeButton::Glyph::Minimize);
            m_minimize->setToolTip(QStringLiteral("Minimize"));
            connect(m_minimize, &QToolButton::clicked, this, [this] { showMinimized(); });
            rightRow->addWidget(m_minimize);
            m_maximize = new ChromeButton(ChromeButton::Glyph::Maximize);
            connect(m_maximize, &QToolButton::clicked, this, [this] { toggleMaximize(); });
            rightRow->addWidget(m_maximize);
            m_close = new ChromeButton(ChromeButton::Glyph::Close);
            m_close->setToolTip(QStringLiteral("Close window"));
            // close() asks first when panes are busy (closeEvent), exactly like the tab bar's last close.
            connect(m_close, &QToolButton::clicked, this, [this] { close(); });
            rightRow->addWidget(m_close);
        }
        right->installEventFilter(this);
        m_tabs->setCornerWidget(right, Qt::TopRightCorner);

        connect(&relay::NotificationCenter::instance(), &relay::NotificationCenter::changed, this, [this] { updateBell(); });
        updateBell();
        updateChromeState();
        syncChromeTooltips();
    }

    void syncChromeTooltips() {
        if (!m_settingsButton) return;
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("app.settings"));
        m_settingsButton->setToolTip(keys.isEmpty() ? QStringLiteral("Options")
                                                    : QStringLiteral("Options  (%1)").arg(keys));
    }

    void updateBell() {
        if (!m_bell) return;
        const int unseen = relay::NotificationCenter::instance().unseen();
        const int total = relay::NotificationCenter::instance().count();
        m_bell->setBadge(unseen);
        m_bell->setToolTip(unseen > 0 ? QStringLiteral("Notifications · %1 new").arg(unseen)
                                      : total > 0 ? QStringLiteral("Notifications · %1").arg(total)
                                                  : QStringLiteral("Notifications"));
    }

    void toggleNotifications() {
        if (!m_notifications) {
            m_notifications = new NotificationsPopup(this);
            m_notifications->onOpenSource = [this](const QString &token) { m_manager->focusPane(token); };
        }
        if (m_notifications->isVisible()) { m_notifications->hide(); return; }
        m_notifications->popUpUnder(m_bell);
        updateBell();
    }

    void toggleMaximize() {
        if (isMaximized()) showNormal(); else showMaximized();
        updateChromeState();
    }

    // Frameless windows have no resize border of their own, so the window keeps a few pixels of
    // padding all round; edgesAt() turns a press there into a WM resize.
    void applyFrameMargins() {
        const int margin = (m_nativeFrame || isMaximized() || isFullScreen()) ? 0 : kFrameMargin;
        setContentsMargins(margin, margin, margin, margin);
    }

    void updateChromeState() {
        applyFrameMargins();
        if (!m_maximize) return;
        const bool maximized = isMaximized();
        m_maximize->setGlyph(maximized ? ChromeButton::Glyph::Restore : ChromeButton::Glyph::Maximize);
        m_maximize->setToolTip(maximized ? QStringLiteral("Restore") : QStringLiteral("Maximize"));
    }

    Qt::Edges edgesAt(const QPoint &pos) const {
        if (m_nativeFrame || isMaximized() || isFullScreen()) return {};
        const int grab = kFrameMargin + 2;
        Qt::Edges edges;
        if (pos.x() <= grab) edges |= Qt::LeftEdge;
        if (pos.x() >= width() - grab) edges |= Qt::RightEdge;
        if (pos.y() <= grab) edges |= Qt::TopEdge;
        if (pos.y() >= height() - grab) edges |= Qt::BottomEdge;
        return edges;
    }

    static Qt::CursorShape cursorForEdges(Qt::Edges edges) {
        const bool left = edges & Qt::LeftEdge, right = edges & Qt::RightEdge;
        const bool top = edges & Qt::TopEdge, bottom = edges & Qt::BottomEdge;
        if ((left && top) || (right && bottom)) return Qt::SizeFDiagCursor;
        if ((right && top) || (left && bottom)) return Qt::SizeBDiagCursor;
        if (left || right) return Qt::SizeHorCursor;
        if (top || bottom) return Qt::SizeVerCursor;
        return Qt::ArrowCursor;
    }

    // Hand the drag to the window manager, so its snapping, tiling and edge magnetism still work.
    // Should it refuse (older X11 setups), Relay moves or resizes the window itself.
    bool startWindowDrag(Qt::Edges edges, const QPoint &pressedAt) {
        QWindow *handle = windowHandle();
        if (handle) {
            if (!edges && handle->startSystemMove()) return true;
            if (edges && handle->startSystemResize(edges)) return true;
        }
        // No window manager took the drag: follow the pointer from where the press landed, not
        // from wherever it has moved to by the time this runs.
        m_manualEdges = edges;
        m_manualFrom = pressedAt;
        m_manualGeometry = geometry();
        return false;
    }

    void continueManualDrag(const QPoint &global) {
        if (m_manualGeometry.isNull()) return;
        const QPoint delta = global - m_manualFrom;
        if (!m_manualEdges) { move(m_manualGeometry.topLeft() + delta); return; }
        QRect box = m_manualGeometry;
        if (m_manualEdges & Qt::LeftEdge) box.setLeft(std::min(box.left() + delta.x(), box.right() - minimumWidth()));
        if (m_manualEdges & Qt::RightEdge) box.setRight(std::max(box.right() + delta.x(), box.left() + minimumWidth()));
        if (m_manualEdges & Qt::TopEdge) box.setTop(std::min(box.top() + delta.y(), box.bottom() - minimumHeight()));
        if (m_manualEdges & Qt::BottomEdge) box.setBottom(std::max(box.bottom() + delta.y(), box.top() + minimumHeight()));
        setGeometry(box);
    }

    void endManualDrag() { m_manualGeometry = QRect(); m_manualEdges = {}; }

    // The empty part of the tab row is the title bar: dragging it moves the window, a
    // double-click maximizes it. Presses on a tab, on + or on a header button are left alone.
    bool headerDrag(QObject *object, QEvent *event) {
        if (m_nativeFrame) return false;
        const QEvent::Type type = event->type();
        if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick
            && type != QEvent::MouseMove && type != QEvent::MouseButtonRelease) return false;
        auto *header = qobject_cast<QWidget *>(object);
        if (!header || header->window() != this) return false;
        const bool onTabBar = header == m_tabs->tabBar();
        if (!onTabBar && header->objectName() != QLatin1String("windowChromeLeft")
            && header->objectName() != QLatin1String("windowChromeRight")) return false;
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (type == QEvent::MouseMove) {
            if (m_manualGeometry.isNull()) return false;
            continueManualDrag(mouse->globalPos());
            return true;
        }
        if (type == QEvent::MouseButtonRelease) {
            if (m_manualGeometry.isNull()) return false;
            endManualDrag();
            return true;
        }
        if (mouse->button() != Qt::LeftButton) return false;
        if (onTabBar && m_tabs->tabBar()->tabAt(mouse->pos()) >= 0) return false;
        if (onTabBar && m_newTabButton && m_newTabButton->geometry().contains(mouse->pos())) return false;
        if (!onTabBar && header->childAt(mouse->pos())) return false;
        if (type == QEvent::MouseButtonDblClick) { toggleMaximize(); return true; }
        startWindowDrag({}, mouse->globalPos());
        return true;
    }

    void placeTabBarControls() {
        if (!m_newTabButton) return;
        QTabBar *bar = m_tabs->tabBar();
        const QRect last = bar->count() ? bar->tabRect(bar->count() - 1) : QRect();
        const QSize size = m_newTabButton->size();
        int x = last.isValid() ? last.right() + 6 : 6;
        x = std::min(x, bar->width() - size.width() - 2);
        m_newTabButton->move(x, (bar->height() - size.height()) / 2);
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("tab.new"));
        m_newTabButton->setToolTip(keys.isEmpty() ? QStringLiteral("New tab") : QStringLiteral("New tab  (%1)").arg(keys));
        m_newTabButton->show(); m_newTabButton->raise();
        // A "move to new window" button on each tab, visible on the hovered tab.
        for (int i = 0; i < bar->count(); ++i) {
            auto *detach = qobject_cast<QToolButton *>(bar->tabButton(i, QTabBar::LeftSide));
            if (!detach) {
                detach = new QToolButton(bar);
                detach->setObjectName(QStringLiteral("tabDetachButton"));
                detach->setText(QStringLiteral("⧉"));
                detach->setAutoRaise(true);
                detach->setFocusPolicy(Qt::NoFocus);
                detach->setFixedSize(18, 18);
                connect(detach, &QToolButton::clicked, this, [this, detach] {
                    QTabBar *tabs = m_tabs->tabBar();
                    for (int j = 0; j < tabs->count(); ++j)
                        if (tabs->tabButton(j, QTabBar::LeftSide) == detach) { moveTabToNewWindow(j); break; }
                    const QString keys = Keymap::instance().shortcutText(QStringLiteral("tab.moveToNewWindow"));
                    hint(QStringLiteral("tab.detach.mouse"), keys.isEmpty() ? QStringLiteral("Tip: “Move tab to new window” is in the palette; bind a key in keybindings.json")
                                                                          : relay::ShortcutHints::nextTime(keys, QStringLiteral("move tab to new window")));
                });
                bar->setTabButton(i, QTabBar::LeftSide, detach);
            }
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("tab.moveToNewWindow"));
            detach->setToolTip(keys.isEmpty() ? QStringLiteral("Move tab to new window") : QStringLiteral("Move tab to new window  (%1)").arg(keys));
            const bool hovered = bar->tabAt(bar->mapFromGlobal(QCursor::pos())) == i && bar->underMouse();
            // Keep the space reserved so tabs do not jump; only the glyph appears on hover.
            detach->setEnabled(m_tabs->count() > 1);
            detach->setProperty("hovered", hovered);
            detach->setText(hovered ? QStringLiteral("⧉") : QString());

            // Relay's own close cross. Qt's closable tabs take "window-close" from the icon
            // theme, which lands as a red disc next to the flat header glyphs.
            // dynamic_cast, not qobject_cast: ChromeButton has no Q_OBJECT of its own, and Qt5's
            // qobject_cast refuses that at compile time.
            auto *close = dynamic_cast<ChromeButton *>(bar->tabButton(i, QTabBar::RightSide));
            if (!close) {
                close = new ChromeButton(ChromeButton::Glyph::TabClose, bar, 18);
                connect(close, &QToolButton::clicked, this, [this, close] {
                    QTabBar *tabs = m_tabs->tabBar();
                    for (int j = 0; j < tabs->count(); ++j)
                        if (tabs->tabButton(j, QTabBar::RightSide) == close) { requestCloseTab(j); return; }
                });
                bar->setTabButton(i, QTabBar::RightSide, close);
            }
            close->setToolTip(m_tabs->count() > 1 ? QStringLiteral("Close tab") : QStringLiteral("Close window"));
            // Full strength on the tab you are pointing at or working in, faint elsewhere, so a
            // row of tabs is not a row of crosses.
            close->setDim(!hovered && i != bar->currentIndex());
        }
    }

    // One path for every "close this tab": the cross, the context menu and the keyboard.
    void requestCloseTab(int index) {
        if (m_tabs->count() > 1) closeTab(index, true); else closeWindowWithWarning();
        hint(QStringLiteral("tab.close.mouse"), relay::ShortcutHints::nextTime(
            Keymap::instance().shortcutText(QStringLiteral("pane.close")), QStringLiteral("close pane, then tab")));
    }

    void syncChrome() {
        for (int i = 0; i < m_tabs->count(); ++i)
            for (QWidget *leaf : leavesIn(m_tabs->widget(i))) {
                auto *chrome = chromeOf(leaf);
                if (!chrome) {
                    chrome = new PaneChrome(leaf);
                    QPointer<QWidget> guard(leaf);
                    chrome->onAction = [guard](const QString &action) {
                        auto *w = windowOf(guard);
                        if (!w) return;
                        w->setActiveLeaf(guard);
                        const QString keys = Keymap::instance().shortcutText(action);
                        w->runAction(action);
                        if (!keys.isEmpty())
                            w->hint(QStringLiteral("chrome.") + action, relay::ShortcutHints::nextTime(keys, Keymap::instance().description(action).toLower()));
                    };
                    // A terminal pane's whole title row is the drag handle (owner, 2026-09-17).
                    if (auto *pane = dynamic_cast<Pane *>(leaf)) {
                        pane->onHeaderDragMove = [guard](const QPoint &global) { if (auto *w = windowOf(guard)) w->dragPaneMove(guard, global); };
                        pane->onHeaderDragEnd = [guard](const QPoint &global, bool drop) { if (auto *w = windowOf(guard)) w->dragPaneEnd(guard, global, drop); };
                    }
                    leaf->installEventFilter(this);
                }
                chrome->refreshTooltips();
                chrome->place();
                chrome->show();
            }
        placeTabBarControls();
    }

    // PaneChrome has no Q_OBJECT, so findChild<PaneChrome *> would match any QFrame child.
    static PaneChrome *chromeOf(QWidget *leaf) {
        if (!leaf) return nullptr;
        for (QObject *child : leaf->children())
            if (auto *chrome = dynamic_cast<PaneChrome *>(child)) return chrome;
        return nullptr;
    }

    // ----- pane states and tab icons (#XM0T), remote sessions (#SPBN) -------------------------
    // One poll for every pane in the window. Each terminal's header shows its own state; each tab
    // shows the most urgent one among its panes, and a red mark when one of them is in an ssh,
    // mosh or telnet session. A finished turn stays news (done / failed / needs you) until the
    // pane has been the one you are looking at for kSeenAfterMs, so coming back to the window
    // still shows what happened for a moment. Notifications are the pane's own (Pane::notify).
    static constexpr int kStatusPollMs = 400;
    static constexpr qint64 kSeenAfterMs = 1500;

    void refreshPaneStatus() {
        namespace ps = relay::panestatus;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool focused = isActiveWindow();
        QSet<QWidget *> pages;
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            pages.insert(page);
            QList<ps::State> states;
            bool remote = false, terminal = false;
            ps::TypeStyle firstType;
            for (QWidget *leaf : leavesIn(page)) {
                auto *pane = dynamic_cast<Pane *>(leaf);
                if (!pane) {
                    if (!firstType.band) firstType = relay::chrome::styleOf(leaf);
                    continue;
                }
                terminal = true;
                PaneChrome *chrome = chromeOf(pane);
                if (!chrome) continue;
                const ps::Facts facts = pane->statusFacts();
                const bool watched = focused && i == m_tabs->currentIndex() && leaf == m_activeLeaf;
                if (!watched) chrome->watchedSince = 0;
                else if (!chrome->watchedSince) chrome->watchedSince = now;
                if (watched && now - chrome->watchedSince >= kSeenAfterMs) chrome->seenSerial = facts.finishSerial;
                const ps::State state = ps::resolve(facts, chrome->seenSerial);
                const QString remoteLine = pane->remoteCommandLine();
                chrome->setStatus(state, remoteLine, pane->sharedWithPhone());
                states << state;
                remote = remote || !remoteLine.isEmpty();
            }
            const ps::State top = ps::mostUrgent(states);
            const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6").arg(terminal).arg(int(top)).arg(remote)
                                    .arg(int(firstType.glyph)).arg(firstType.ink.name(), relay::theme::activeThemeId());
            if (m_tabIconKey.value(page) == key) continue;
            m_tabIconKey.insert(page, key);
            m_tabs->setTabIcon(i, relay::chrome::tabIcon(terminal, top, remote, firstType.glyph, firstType.ink, devicePixelRatioF()));
        }
        for (auto it = m_tabIconKey.begin(); it != m_tabIconKey.end();)
            it = pages.contains(it.key()) ? std::next(it) : m_tabIconKey.erase(it);
    }

    enum class Edge { None, Left, Right, Top, Bottom, TabBar };

    // Where a drop at `global` would put a pane: an edge of the leaf under the cursor, or a tab bar.
    static QPair<QWidget *, Edge> dropTarget(QWidget *dragged, const QPoint &global) {
        QWidget *under = QApplication::widgetAt(global);
        for (QWidget *w = under; w; w = w->parentWidget())
            if (auto *bar = dynamic_cast<QTabBar *>(w); bar && windowOf(bar)) return {bar, Edge::TabBar};
        QWidget *leaf = leafOf(under);
        if (!leaf || leaf == dragged || !windowOf(leaf)) return {nullptr, Edge::None};
        switch (relay::panes::dropEdge(leaf->mapFromGlobal(global), leaf->size())) {
        case relay::panes::Direction::Left: return {leaf, Edge::Left};
        case relay::panes::Direction::Right: return {leaf, Edge::Right};
        case relay::panes::Direction::Up: return {leaf, Edge::Top};
        case relay::panes::Direction::Down: break;
        }
        return {leaf, Edge::Bottom};
    }

    // ----- dragging a tool pane by its header -----------------------------------------------
    // Terminal panes carry their own handle (Pane::headerDragEvent, which knows exactly which
    // widgets its header is made of). The explorer, a file preview, a plan and the Switchboard do
    // not, so they get the same gesture from here: a press in the top strip of the pane, on
    // something that is not itself a control, starts the drag. Controls — the buttons in those
    // headers, the pane's own chrome, a filter box, a list, the Switchboard's tabs — are left
    // alone, so nothing a click used to do has changed.
    static constexpr int kToolHeaderStrip = 34;

    bool toolHeaderDrag(QObject *object, QEvent *event) {
        const QEvent::Type type = event->type();
        if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove
            && type != QEvent::MouseButtonRelease && type != QEvent::KeyPress) return false;
        switch (type) {
        case QEvent::MouseButtonPress: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton || m_toolPressed) return false;
            auto *widget = qobject_cast<QWidget *>(object);
            if (!widget || widget->window() != this) return false;
            QWidget *leaf = leafOf(widget);
            if (!leaf || dynamic_cast<Pane *>(leaf)) return false;
            const QPoint local = leaf->mapFromGlobal(mouse->globalPos());
            if (!leaf->rect().contains(local) || local.y() >= kToolHeaderStrip) return false;
            for (QWidget *w = widget; w && w != leaf; w = w->parentWidget())
                if (qobject_cast<QAbstractButton *>(w) || qobject_cast<QLineEdit *>(w)
                    || qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractItemView *>(w)
                    || qobject_cast<QTabBar *>(w) || qobject_cast<QAbstractScrollArea *>(w)
                    || dynamic_cast<PaneChrome *>(w)) return false;
            m_toolPressAt = mouse->globalPos();
            m_toolLeaf = leaf;
            m_toolPressed = true;
            m_toolDragging = false;
            return false;
        }
        case QEvent::MouseMove: {
            if (!m_toolPressed || !m_toolLeaf) return false;
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (!m_toolDragging) {
                if ((mouse->globalPos() - m_toolPressAt).manhattanLength() < QApplication::startDragDistance()) return false;
                m_toolDragging = true;
                QApplication::setOverrideCursor(Qt::ClosedHandCursor);
            }
            dragPaneMove(m_toolLeaf, mouse->globalPos());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            if (!m_toolPressed) return false;
            m_toolPressed = false;
            if (!m_toolDragging) return false;
            endToolHeaderDrag(static_cast<QMouseEvent *>(event)->globalPos(), true);
            return true;
        }
        case QEvent::KeyPress:
            if (m_toolDragging && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                m_toolPressed = false;
                endToolHeaderDrag(QCursor::pos(), false);
                return true;
            }
            return false;
        default: break;
        }
        return false;
    }

    void endToolHeaderDrag(const QPoint &global, bool drop) {
        m_toolDragging = false;
        QApplication::restoreOverrideCursor();
        QWidget *leaf = m_toolLeaf;
        m_toolLeaf = nullptr;
        if (leaf) dragPaneEnd(leaf, global, drop);
    }

    void dragPaneMove(QWidget *dragged, const QPoint &global) {
        const auto target = dropTarget(dragged, global);
        if (!target.first) { if (m_dropZone) m_dropZone->hide(); return; }
        RelayWindow *w = windowOf(target.first);
        if (!m_dropZone || m_dropZone->window() != w) {
            delete m_dropZone;
            m_dropZone = new QFrame(w->centralWidget());
            m_dropZone->setObjectName(QStringLiteral("dropZone"));
            m_dropZone->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_dropZone->setAttribute(Qt::WA_StyledBackground);
        }
        QRect rect(target.first->mapTo(w->centralWidget(), QPoint(0, 0)), target.first->size());
        switch (target.second) {
        case Edge::Left: rect.setWidth(rect.width() / 2); break;
        case Edge::Right: rect.setLeft(rect.left() + rect.width() / 2); break;
        case Edge::Top: rect.setHeight(rect.height() / 2); break;
        case Edge::Bottom: rect.setTop(rect.top() + rect.height() / 2); break;
        default: break;
        }
        m_dropZone->setGeometry(rect);
        m_dropZone->show(); m_dropZone->raise();
    }

    void dragPaneEnd(QWidget *dragged, const QPoint &global, bool drop) {
        if (m_dropZone) { m_dropZone->hide(); m_dropZone->deleteLater(); m_dropZone = nullptr; }
        if (!drop || !dragged) return;
        const auto target = dropTarget(dragged, global);
        if (target.second == Edge::None) return;
        if (target.second == Edge::TabBar) {
            RelayWindow *w = windowOf(target.first);
            if (w == this && leavesIn(pageOf(dragged)).size() <= 1) return;   // already its own tab here
            if (!takeLeaf(dragged)) return;
            w->adoptLeafAsTab(dragged);
        } else {
            QPointer<QWidget> anchor(target.first);
            if (!takeLeaf(dragged) || !anchor) return;
            const Qt::Orientation orientation = target.second == Edge::Left || target.second == Edge::Right ? Qt::Horizontal : Qt::Vertical;
            RelayWindow *w = windowOf(anchor);
            w->insertBeside(anchor, dragged, orientation, target.second == Edge::Left || target.second == Edge::Top);
            w->m_tabs->setCurrentWidget(w->pageOf(dragged));
            w->setActiveLeaf(dragged); focusLeaf(dragged);
            if (w != this) { w->raise(); w->activateWindow(); }
        }
        const QString move = Keymap::instance().shortcutText(QStringLiteral("pane.moveLeft"));
        if (!move.isEmpty())
            if (auto *w = windowOf(dragged))
                w->hint(QStringLiteral("pane.drag"), QStringLiteral("Next time: %1 and the other arrows move the focused pane").arg(move));
    }

    // Remove a leaf from its window's layout without destroying it (the shell and worker keep
    // running). A tab left empty is removed; a window left empty closes. The leaf is parentless
    // afterwards and must be inserted somewhere by the caller.
    bool takeLeaf(QWidget *leaf) {
        QWidget *page = pageOf(leaf);
        if (!page) return false;
        // The buttons travel with the pane to wherever it lands.
        const bool last = leavesIn(page).size() <= 1;
        auto *splitter = dynamic_cast<QSplitter *>(leaf->parentWidget());
        leaf->hide();
        leaf->setParent(nullptr);
        if (m_active.data() == leaf) m_active = nullptr;
        if (m_activeLeaf.data() == leaf) m_activeLeaf = nullptr;
        if (m_lastActive.value(page) == leaf) m_lastActive.remove(page);
        if (last) {
            m_lastActive.remove(page);
            forgetTab(page);
            m_tabs->removeTab(m_tabs->indexOf(page));
            page->deleteLater();
            if (m_tabs->count() == 0) { m_confirmedClose = true; m_skipRemember = true; QTimer::singleShot(0, this, [this] { close(); }); return true; }
        } else if (splitter && splitter->count() == 1) {
            QWidget *only = splitter->widget(0);
            QWidget *outerWidget = splitter->parentWidget();
            if (auto *outer = dynamic_cast<QSplitter *>(outerWidget)) outer->replaceWidget(outer->indexOf(splitter), only);
            else if (outerWidget && outerWidget->layout()) delete outerWidget->layout()->replaceWidget(splitter, only);
            only->show();
            splitter->hide();
            splitter->deleteLater();
        }
        if (!m_activeLeaf) {
            QWidget *current = m_tabs->currentWidget();
            QWidget *next = current ? m_lastActive.value(current) : nullptr;
            if (!next && current) { const auto leaves = leavesIn(current); next = leaves.isEmpty() ? nullptr : leaves.first(); }
            if (next) { setActiveLeaf(next); focusLeaf(next); }
        }
        updateTitles();
        return true;
    }

public:
    void adoptLeafAsTab(QWidget *leaf, int index = -1) {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(leaf);
        leaf->show();
        index = index < 0 ? m_tabs->count() : std::min(index, m_tabs->count());
        m_tabs->insertTab(index, page, QString());
        m_tabs->setCurrentIndex(index);
        setActiveLeaf(leaf);
        QTimer::singleShot(0, leaf, [leaf] { focusLeaf(leaf); });
        updateTitles();
    }

    // A whole tab page (with its live panes) from another window.
    void adoptPage(QWidget *page, QWidget *lastActive) {
        m_tabs->addTab(page, QString());
        m_tabs->setCurrentWidget(page);
        page->show();
        const auto leaves = leavesIn(page);
        QWidget *leaf = lastActive && leaves.contains(lastActive) ? lastActive : (leaves.isEmpty() ? nullptr : leaves.first());
        if (leaf) { setActiveLeaf(leaf); QTimer::singleShot(0, leaf, [leaf] { focusLeaf(leaf); }); }
        updateTitles();
    }
private:

    void moveLeafToNewTab(QWidget *leaf) {
        QWidget *page = pageOf(leaf);
        if (!page) return;
        if (leavesIn(page).size() <= 1) { notice(QStringLiteral("This pane is already the only pane in its tab."), 4000); return; }
        const int index = m_tabs->indexOf(page) + 1;
        if (!takeLeaf(leaf)) return;
        adoptLeafAsTab(leaf, index);
    }

    void moveTabToNewWindow(int index) {
        QWidget *page = m_tabs->widget(index);
        if (!page) return;
        if (m_tabs->count() <= 1) { notice(QStringLiteral("This is the only tab in the window."), 4000); return; }
        QWidget *lastActive = m_lastActive.value(page);
        if (m_active && pageOf(m_active) == page) m_active = nullptr;
        if (m_activeLeaf && pageOf(m_activeLeaf) == page) m_activeLeaf = nullptr;
        m_lastActive.remove(page);
        const QString tabName = m_tabNames.value(page);
        forgetTab(page);
        m_tabs->removeTab(index);
        page->setParent(nullptr);
        RelayWindow *window = m_manager->newEmptyWindow(geometry().translated(40, 40));
        window->adoptPage(page, lastActive);
        if (!tabName.isEmpty()) window->renameTab(tabName, false, page);
        if (QWidget *current = m_tabs->currentWidget()) {
            QWidget *leaf = m_lastActive.value(current);
            if (!leaf) { const auto leaves = leavesIn(current); leaf = leaves.isEmpty() ? nullptr : leaves.first(); }
            if (leaf) setActiveLeaf(leaf);
        }
        updateTitles();
        window->raise(); window->activateWindow();
    }

    // Keyboard move: swap with the neighbor in that direction when they share a splitter,
    // otherwise dock on the neighbor's near side. Repeating keeps moving the pane that way.
    void moveActive(relay::panes::Direction direction) {
        QWidget *current = m_activeLeaf;
        QWidget *page = current ? pageOf(current) : nullptr;
        if (!page) return;
        QWidget *neighbor = neighborOf(current, direction);
        if (!neighbor) { notice(QStringLiteral("No pane in that direction."), 2500); return; }
        const Qt::Orientation orientation = relay::panes::orientationFor(direction);
        const bool towardStart = relay::panes::towardStart(direction);
        auto *splitter = dynamic_cast<QSplitter *>(current->parentWidget());
        const bool siblings = splitter && splitter == neighbor->parentWidget() && splitter->orientation() == orientation
                              && std::abs(splitter->indexOf(current) - splitter->indexOf(neighbor)) == 1;
        if (siblings) {
            relay::panes::swapInSplitter(splitter, current, neighbor);
        } else {
            QPointer<QWidget> anchor(neighbor);
            if (!takeLeaf(current) || !anchor) return;
            insertBeside(anchor, current, orientation, !towardStart);
        }
        setActiveLeaf(current); focusLeaf(current);
        updateTitles();
    }

    QWidget *neighborOf(QWidget *current, relay::panes::Direction direction) const {
        QWidget *page = pageOf(current);
        const QRect from(current->mapTo(page, QPoint(0, 0)), current->size());
        QList<QWidget *> panes;
        QList<QRect> rects;
        for (QWidget *pane : leavesIn(page)) {
            if (pane == current) continue;
            panes.append(pane);
            rects.append(QRect(pane->mapTo(page, QPoint(0, 0)), pane->size()));
        }
        const int best = relay::panes::neighborIndex(from, rects, direction);
        return best < 0 ? nullptr : panes.at(best);
    }

    void closeActive() {
        QWidget *pane = m_activeLeaf;
        if (!pane) return;
        // The Settings pane closes the way Esc closes it: focus goes back where it was, and it is
        // never what closes the window. Its × is the chrome's, so that button comes here too.
        if (auto *tool = dynamic_cast<ToolPane *>(pane); tool && tool->settings()) { closeSettingsPane(tool); return; }
        QWidget *page = pageOf(pane);
        if (leavesIn(page).size() > 1) closePane(pane, true);
        else if (m_tabs->count() > 1) closeTab(m_tabs->indexOf(page), true);
        else closeWindowWithWarning();
    }

public:
    void closePane(QWidget *pane, bool record) {
        QWidget *page = pageOf(pane);
        if (!page) return;
        // Once per run, the first time something is closed: say how to get it back (owner, 2026-09-17).
        if (record) {
            static bool toldAboutRestore = false;
            if (!toldAboutRestore) {
                toldAboutRestore = true;
                const QString keys = Keymap::instance().shortcutText(QStringLiteral("closed.restore"));
                hint(QStringLiteral("closed.restore.first"),
                     keys.isEmpty() ? QStringLiteral("Closed. \"Restore closed\" in the palette brings it back, with its conversation.")
                                    : QStringLiteral("Closed. %1 brings it back, with its conversation.").arg(keys), 1);
            }
        }
        if (leavesIn(page).size() <= 1) {
            // Last pane of its tab: close the tab, or the window when it is the last tab.
            if (m_tabs->count() > 1) closeTab(m_tabs->indexOf(page), record);
            else { if (record) { m_confirmedClose = true; } else { m_confirmedClose = true; m_skipRemember = true; } close(); }
            return;
        }
        auto *splitter = dynamic_cast<QSplitter *>(pane->parentWidget());
        if (!splitter) return;
        const int index = splitter->indexOf(pane);
        QWidget *neighbor = splitter->widget(index > 0 ? index - 1 : index + 1);
        const auto neighborPanes = leavesIn(neighbor);
        QWidget *focusNext = neighborPanes.isEmpty() ? nullptr : (index > 0 ? neighborPanes.last() : neighborPanes.first());
        if (record && focusNext && !serializeNode(pane).isEmpty()) {
            ClosedItem item;
            item.kind = ClosedItem::PaneItem; item.window = this; item.sibling = focusNext;
            item.orientation = splitter->orientation(); item.before = index == 0;
            item.layout = serializeNode(pane);
            m_manager->remember(item);
        }
        if (auto *terminal = dynamic_cast<Pane *>(pane)) {
            terminal->onStatus = nullptr; terminal->onStateChanged = nullptr; terminal->onShellExited = nullptr; terminal->onOpenPath = nullptr;
        }
        pane->hide();
        pane->setParent(nullptr);
        pane->deleteLater();
        if (splitter->count() == 1) {
            // Collapse a splitter that now holds a single child into its parent.
            QWidget *only = splitter->widget(0);
            QWidget *outerWidget = splitter->parentWidget();
            if (auto *outer = dynamic_cast<QSplitter *>(outerWidget)) outer->replaceWidget(outer->indexOf(splitter), only);
            else if (outerWidget && outerWidget->layout()) delete outerWidget->layout()->replaceWidget(splitter, only);
            only->show();
            splitter->hide();
            splitter->deleteLater();
        }
        if (m_active.data() == pane) m_active = nullptr;
        if (m_activeLeaf.data() == pane) m_activeLeaf = nullptr;
        if (m_lastActive.value(page) == pane) m_lastActive.remove(page);
        if (focusNext) { setActiveLeaf(focusNext); focusLeaf(focusNext); }
        updateTitles();
    }

    void closeTab(int index, bool record) {
        QWidget *page = m_tabs->widget(index);
        if (!page) return;
        if (record) {
            ClosedItem item;
            item.kind = ClosedItem::TabItem; item.window = this; item.index = index;
            item.layout = serializeTab(index);
            m_manager->remember(item);
        }
        for (Pane *pane : panesIn(page)) { pane->onStatus = nullptr; pane->onStateChanged = nullptr; pane->onShellExited = nullptr; }
        if (m_active && pageOf(m_active) == page) m_active = nullptr;
        if (m_activeLeaf && pageOf(m_activeLeaf) == page) m_activeLeaf = nullptr;
        m_lastActive.remove(page);
        forgetTab(page);
        m_tabs->removeTab(index);
        page->deleteLater();
        if (m_tabs->count() == 0) { m_confirmedClose = true; close(); return; }
        updateTitles();
    }

private:
    bool confirmClose() {
        const auto panes = allPanes();
        const bool busy = std::any_of(panes.cbegin(), panes.cend(), [](Pane *p) { return p->agentBusy() || p->processBusy(); });
        int leafCount = 0;
        for (int i = 0; i < m_tabs->count(); ++i) leafCount += leavesIn(m_tabs->widget(i)).size();
        QString text = QStringLiteral("Close this window and its %1 tab(s) and %2 pane(s)?").arg(m_tabs->count()).arg(leafCount);
        if (busy) text += QStringLiteral("\n\nA program or agent turn is still running and will be stopped.");
        text += QStringLiteral("\n\nCtrl+Shift+W reopens it in the same directories, with new shells.");
        return QMessageBox::warning(this, QStringLiteral("Close window?"), text,
                                    QMessageBox::Close | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Close;
    }

    void closeWindowWithWarning() {
        if (!confirmClose()) return;
        m_confirmedClose = true;
        close();
    }

    void rememberWindow() {
        if (m_skipRemember || m_tabs->count() == 0) return;
        ClosedItem item;
        item.kind = ClosedItem::WindowItem;
        item.tabs = serializeTabs();
        item.index = m_tabs->currentIndex();
        item.geometry = geometry();
        m_manager->remember(item);
    }

    WindowManager *m_manager;
    // Switchboard: one worker per window, started on the first Ctrl+Shift+S (protocol 17).
    QPointer<relay::BoardWorker> m_boardWorker;
    QTabWidget *m_tabs = nullptr;
    QPointer<Pane> m_returnPane;        // where focus was when the Settings pane opened
    QPointer<QWidget> m_returnFocus;
    QPointer<Pane> m_active;
    QHash<QWidget *, QPointer<QWidget>> m_lastActive;
    QPointer<QWidget> m_activeLeaf;
    // Dragging a tool pane (explorer, preview, plan, Switchboard) by its header: the pane being
    // moved, where the press landed, and whether it has gone far enough to be a drag.
    QPointer<QWidget> m_toolLeaf;
    QPoint m_toolPressAt;
    bool m_toolPressed = false, m_toolDragging = false;
    // "New pane, then ← ↑ ↓ places it" (issue #78BN): the open window, the pane it made, the pane
    // it was split from, and the transient hint over the new pane.
    relay::panes::PlacementWindow m_placement;
    QElapsedTimer m_placementClock;
    QTimer m_placementTimer;
    // Pane state glyphs and tab icons (#XM0T): the poll, and each tab's last icon so it is only
    // repainted when what it shows changes.
    QTimer m_statusTimer;
    QHash<QWidget *, QString> m_tabIconKey;
    QPointer<QWidget> m_placementPane, m_placementAnchor;
    QPointer<QLabel> m_placementHint;
    QPointer<QToolButton> m_newTabButton;
    // The last action run from the keyboard and the key that ran it, so an action can name the
    // combination that reached it. Cleared by whoever reads it; a palette run never sets it.
    QPair<QString, QString> m_lastShortcut;
    // Window header (see buildWindowChrome). m_nativeFrame: this window kept the system title bar.
    static constexpr int kFrameMargin = 5;
    bool m_nativeFrame = false;
    QPointer<ChromeButton> m_bell, m_settingsButton, m_minimize, m_maximize, m_close;
    QPointer<NotificationsPopup> m_notifications;
    Qt::Edges m_manualEdges;
    QPoint m_manualFrom;
    QRect m_manualGeometry;
    QPointer<QFrame> m_dropZone;
    bool m_confirmedClose = false, m_skipRemember = false;
};

