// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// One window -- the tab row that doubles as the title bar, the splitter tree of panes inside each
// tab, the actions palette, and the routing of every shortcut to the pane that should act on it --
// and above it WindowManager, which owns the windows, remembers the closed ones and keeps the
// saved layout. The two share a header because they name each other: RelayWindow holds a
// WindowManager and WindowManager::forget() takes a RelayWindow, both inline, so neither can be
// declared first on its own. The WindowManager members that need more than that are in
// WindowManagerImpl.h.

#include "PaneChrome.h"
#include "PromptHistory.h"
#include "RichEditor.h"
#include "WindowChrome.h"
#include "ThemeTabBar.h"

#include "Theme.h"
#include "FilePanes.h"
#include "BoardPane.h"
// The helper agent's panel, whose composer answers Alt+M and Ctrl+Alt+M over its own model box
// (#PK5Q): the window needs the class, not just its name.
#include "BoardRemote.h"   // the Switchboard on the owner's devices (#SWPH)
#include "BoardWorker.h"
#include "BoardWorkspace.h"   // which project's Switchboard a pane is looking at
#include "Projects.h"         // which project a tab is attached to, and the registry of known ones
#include "ProjectInit.h"      // when "Initialize a project … here?" is asked, and by which trigger
#include "ProjectPicker.h"    // the project picker pane: which project a tab with none attaches to (#916B)
#include "Hints.h"
#include "Notifications.h"
#include "ScreenPrompt.h"
#include "PaneLayout.h"
#include "QueueNav.h"
#include "PaneTitles.h"
#include "PaneUsage.h"    // the tab-level sum of the panes' CPU / memory
#include "SshConfig.h"
#include "TurnTranscript.h"
#include "AgentInternalsView.h"   // the Activity pane beside a terminal (#QT8C)
#include "SettingsPane.h"
#include "AppCommands.h"   // the agent drives the app: the catalog, the executor, the change log (#FEJQ, §30)
#include "CurrentTextComboBox.h"   // the box itself, as the Switchboard and the terminal panes build it
#include "Isolation.h"        // the per-pane memory limits this page edits
#include "EscapeeCaps.h"     // the opt-in cap on tmux and Chrome, which leave their pane (#Y4RX)
#include "LocalModelsSettings.h"
#include "RemoteSettings.h"   // Options › Remote: the always-on switch and the address (#PH0N)
#include "ModelCatalog.h"      // Options › Models: providers, the checklist, the order (owner, 2026-09-20)
#include "SubagentTranscript.h"
#include "SubagentsPanel.h"
#include "Logging.h"
#include "TerminalBackends.h"
#include "TerminalBackend.h"
#include "WindowState.h"
#include "ClosedStack.h"
#include "ClosedList.h"
#include "RuntimeDirs.h"
#include "AppPaths.h"       // dataRoot(): where the shipped backend lives
#include "Voice.h"
#include "Aliases.h"
#include "OutputLinks.h"
#include "Conversations.h"
#include "SessionInfo.h"
#include "ApprovalsPane.h"   // the first-launch approvals choice: a pane beside the first one configured (card #K2FV)
// The Sharing pane and the sidecar controller behind it (#W5N2): this header opens the pane,
// answers RemoteShare's signals and reads sharing::Model for the pane-header chip.
#include "RemoteShare.h"
#include "SharingPane.h"
#include "RemotePane.h"   // Relay-to-Relay: a pane another desktop shares, opened here
#include "TestSuitesPane.h"   // the Test suites pane, beside the Switchboard (card #7BM4)
#include "ProfilePane.h"      // the Profile result pane, ditto (card #7BM4 phase 5)

#include <QAbstractButton>
#include <QDateTime>
#include <QPair>
#include <QUuid>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QAbstractItemView>
#include <QProcess>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QSysInfo>
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
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
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
// Restoring recreates shells in the same directories, with the terminal text the pane had when
// it closed and its conversation; running programs are not preserved, because closing a pane
// ends its shell.

class RelayWindow;

// A closed pane, tab or window. `record` is everything that can be written down and is what
// state/closed.json holds (src/ClosedStack.h); the two pointers are where it sat in this run, and
// are null for an item that came back from the file — it then reopens in the window that asks.
struct ClosedItem {
    relay::closed::Record record;
    QPointer<RelayWindow> window;
    QPointer<QWidget> anchor;            // Pane: what it sat beside — a pane, or a whole nested split
    QPointer<QWidget> sibling;           // Pane: the pane that took focus, if the anchor is gone
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
    // ----- recently closed (src/ClosedStack.h) --------------------------------------------------
    // The last 25 closed panes, tabs and windows, newest last. `restore` brings the newest back
    // (closed.restore); the "Recently closed" list and the palette reopen any of them by id.
    void remember(ClosedItem item);
    void restore(RelayWindow *requester);
    bool restoreClosed(RelayWindow *requester, const QString &id);
    bool discardClosed(const QString &id);    // drop one item; its saved text goes at the next prune
    QList<relay::closed::Record> closedRecords() const {
        QList<relay::closed::Record> records;
        for (const ClosedItem &item : m_closed) records.append(item.record);
        return records;
    }
    // The newest closed item that holds this conversation; an empty id when there is none.
    QString closedItemForSession(const QString &sessionId) const {
        if (sessionId.isEmpty()) return {};
        for (auto it = m_closed.crbegin(); it != m_closed.crend(); ++it)
            if (relay::closed::sessionIds(it->record).contains(sessionId)) return it->record.id;
        return {};
    }
    // Called after every change to the list, for as long as `context` lives.
    void watchClosed(QObject *context, std::function<void()> changed) {
        if (!context || !changed) return;
        m_closedWatchers.append({QPointer<QObject>(context), std::move(changed)});
    }
    void forgetClosed();                      // drop the list and its file
    void flushClosed();                       // the quit's last word: see settleClosed()
    // Whether this Relay writes the state directory at all (restore is on and the layout is ours).
    bool writesState();
    void forget(RelayWindow *window) { m_windows.removeAll(window); }

    // ----- known projects (src/Projects.h) ------------------------------------------------------
    // One registry for the whole process, read from disk the first time it is asked for. A tab
    // attaching to a project is the only thing that writes it (RelayWindow::attachTab), so the
    // file reads back as "you attached this because you opened its Switchboard" and never as a
    // guess Relay made about a directory somebody happened to `cd` into.
    relay::projects::Registry &projects() {
        if (!m_projectsLoaded) { m_projectsLoaded = true; m_projects.load(); }
        return m_projects;
    }

    // "Initialize a project and create a Switchboard here?", raised or answered "Not now" for
    // these projects (normalized paths) since Relay started (protocol 19.12). A no goes in the
    // registry above and outlives the process; this outlives nothing, which is the difference
    // between "no" and "not now". It is the whole process's, not one window's: the same project
    // open in two windows is asked about once.
    QSet<QString> &initSnoozed() { return m_initSnoozed; }

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
    void loadClosed();
    void saveClosed();
    void settleClosed();
    void closedChanged();
    void reopen(RelayWindow *requester, ClosedItem item);
    QStringList openSessionIds();

    QString m_workspace;
    bool m_cleanShell = false;
    relay::projects::Registry m_projects;   // the known-projects file, loaded on first use
    bool m_projectsLoaded = false;
    QSet<QString> m_initSnoozed;            // asked, or "Not now", since this Relay started
    QList<QPointer<RelayWindow>> m_windows;
    QList<ClosedItem> m_closed;
    QList<QPair<QPointer<QObject>, std::function<void()>>> m_closedWatchers;
    // Windows remembered since the last settle. When the whole set goes (a quit) they are what the
    // saved layout reopens on the next start, so they are not "closed" and leave the list again.
    QStringList m_pendingWindowCloses;
    QString m_closedPath;
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
        // "Move left/right, then ↓ docks it beneath" (#Q7Y9): the twin window, same shape.
        m_beneathTimer.setSingleShot(true);
        connect(&m_beneathTimer, &QTimer::timeout, this, [this] { endBeneathDock(); });
        // Pane state glyphs and tab icons (#XM0T), and the remote-session header (#SPBN).
        connect(&m_statusTimer, &QTimer::timeout, this, [this] { refreshPaneStatus(); });
        m_statusTimer.start(kStatusPollMs);
        // The tab's live dot steps on the shared blink grid, not on the poll (owner, 2026-09-19:
        // one cadence). The poll decides what each tab's icon says; this timer only moves the
        // blink on, at the grid's own boundary.
        m_pulseTimer.setSingleShot(true);
        connect(&m_pulseTimer, &QTimer::timeout, this, [this] { applyTabIcons(); });
        // Multiplayer (#W5N2, docs/REMOTE-PROTOCOL.md section 10). A knock, a request for the
        // keyboard or a guest prompt has to be noticed from another pane or another window, and
        // must never take the keyboard: the next keystroke would land on Admit. So it opens the
        // Sharing pane without focusing it and rings the bell, which is exactly what
        // Pane::notify() already does (the desktop only hears about it when Relay is not active).
        {
            relay::RemoteShare &share = relay::RemoteShare::instance();
            connect(&share, &relay::RemoteShare::needsOwner, this,
                    [this](const QString &paneId, const QString &title, const QString &body) {
                        Pane *owner = paneWithToken(paneId);
                        if (!owner) return;          // another window is sharing that pane
                        owner->notifyFromWindow(title, body, relay::NotificationCenter::kindWarning);
                        openSharingPane(owner, false);
                    });
            connect(&share, &relay::RemoteShare::sharingModelChanged, this,
                    [this] { refreshSharingPanes(false); });
            connect(&share, &relay::RemoteShare::secondPassed, this,
                    [this] { refreshSharingPanes(true); });
            // "Share whole tab": a tab switched on or off, and — as a backstop for any way a pane
            // reaches or leaves a tab that no hook below names — once a second while one is on.
            connect(&share, &relay::RemoteShare::tabSharesChanged, this, [this] { scheduleTabShareSync(); });
            connect(&share, &relay::RemoteShare::secondPassed, this, [this] { syncTabShares(); });
            // Remote control as a service (#PH0N): while the switch is on, every pane with a
            // screen is published as it appears — the same one-second backstop, for the same
            // reason, and once more the moment the switch itself moves so that the panes already
            // open are published too.
            connect(&share, &relay::RemoteShare::secondPassed, this, [this] { syncAlwaysOnShares(); });
            connect(&share, &relay::RemoteShare::alwaysOnChanged, this, [this](bool) {
                m_autoShared.clear();
                syncAlwaysOnShares();
                updateRemotePlug();
                refreshSettingsPanes();
            });
            connect(&share, &relay::RemoteShare::remoteStateChanged, this, [this] {
                updateRemotePlug();
                refreshSettingsPanes();   // Options › Remote is showing the state that has moved
            });
            registerBoardRemote();   // this window can serve its boards to the owner's devices (#SWPH)
        }
        // No toolbar: the tab bar starts at the top. Its actions live in the palette (Ctrl+Shift+A).
        Keymap::instance().listen(this, [this] { syncChromeButtons(); });
        // The status bar stays out of the layout until something transient needs it, so the
        // window has no permanent strip under the composer and the terminal never resizes for one.
        statusBar()->setSizeGripEnabled(false);
        statusBar()->hide();
        connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString &text) {
            statusBar()->setVisible(!text.isEmpty());
        });
        m_tabs = new WindowTabWidget;   // ThemeTabBar: an inactive tab wears its own theme (src/ThemeTabBar.h)
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
        // A setting was written anywhere — this window's Options pane, another window's, a dialog,
        // a page reset, an agent — so every worker is sent the catalog again (#FEJQ, §30.2: the
        // block carries current values, and nothing is cached across a refresh).
        relay::SettingsWatch::instance().listen(this, [this] { sendAppCatalog(); });
        Keymap::instance().listen(this, [this] {
            for (Pane *pane : allPanes()) pane->sendKeybindings();
            sendHelperKeybindings();   // the tab's helper reads and writes the same keys (#GMCF)
            const auto conflicts = Keymap::instance().conflicts();
            notice(conflicts.isEmpty() ? QStringLiteral("Keyboard shortcuts reloaded.")
                                                         : QStringLiteral("Keyboard shortcuts: ") + conflicts.join(QStringLiteral("; ")));
            refreshSettingsPanes();
        });
        connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
            // A tab change of any kind — the keys, a click on the tab bar, the scroll wheel over
            // it — closes the chord's window (#Q7Y9). Otherwise the Move-down that follows would
            // restack two panes in a tab nobody is looking at, and focus one of them.
            endBeneathDock();
            QWidget *page = m_tabs->currentWidget();
            if (!page) return;
            applyTabTheme(page);   // themes are per tab: the one coming forward brings its own
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
                if (byMouse && changed) hintPaneFocusByMouse();
            }
        });
        qApp->installEventFilter(this);
        startQaRects();   // only when RELAY_QA_RECTS names a file; see it for why
    }

    // stopBoardWorkers() again (closeEvent has usually run already, and it is idempotent): a
    // window destroyed without being closed must still not leave Switchboard workers behind, and
    // its board panes are about to be destroyed with it.
    ~RelayWindow() override {
        qApp->removeEventFilter(this);
        stopBoardWorkers();
        // The panes under this window are deleted after this body runs, and a shared pane's view
        // dying makes RemoteShare emit into everything still connected to it — including this
        // window's own slots, which then read a tab bar that is already half destroyed (SIGSEGV in
        // refreshSharingPanes, QStackedLayout::widget). It was reachable before whenever a pane was
        // shared at quit; with remote control on (#PH0N) every pane is shared, so it is every quit.
        disconnect(&relay::RemoteShare::instance(), nullptr, this, nullptr);
    }

    // Build a tab from a saved tab. Returns false if no pane could be created. The tab is either
    // a bare layout node or the {"project", "node"} wrapper an attached tab saves as (#JN7X).
    bool addTab(const QJsonObject &tab, int index = -1) {
        const QJsonObject node = relay::windowstate::tabNode(tab);
        const QString project = relay::windowstate::tabProject(tab);
        auto *page = new QWidget;
        // The id this tab was saved with, so its helper worker finds its own conversation again
        // (#FEJQ, §30.7). A tab saved without one gets a fresh id the first time something asks.
        if (const QString id = relay::windowstate::tabId(tab); !id.isEmpty())
            page->setProperty("relayTabId", id);
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
        // Its theme: the one it was saved with, else the default Options › Appearance holds — what
        // Relay opens on and what a new tab starts with. (A *new* tab may then be moved on to the
        // next theme in the list: startNewTabTheme(), behind its own option.)
        if (perTabThemes()) {
            const QString saved = relay::windowstate::tabTheme(tab);
            const bool known = !saved.isEmpty() && relay::theme::specFor(saved).id == saved;
            page->setProperty("relayTheme", known ? saved : relay::theme::startupThemeId());
            applyTabTheme(page);
        }
        // A saved tab comes back attached to the project it was attached to. A project that has
        // been moved or deleted comes back unattached and quiet rather than pointing the tab's
        // panes at a directory that is not there.
        if (!project.isEmpty() && QFileInfo(project).isDir())
            attachTab(page, project, QString::fromLatin1(relay::projects::kReasonRestored));
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
    // `sizes`/`slot` are the divider positions of the splitter the pane left; they are put back
    // only when the pane lands in a splitter of that shape again, in the slot it had.
    bool restorePaneNextTo(QWidget *sibling, Qt::Orientation orientation, bool before, const QJsonObject &node,
                           const QList<int> &sizes = {}, int slot = -1) {
        // The sibling is a pane, or the nested split the pane sat beside (still in a tab here).
        if (!sibling || sibling->window() != this || !(isLeaf(sibling) || dynamic_cast<QSplitter *>(sibling))
            || leavesIn(sibling).isEmpty() || !pageOf(leavesIn(sibling).first())) return false;
        QWidget *leaf = nullptr;
        try { leaf = buildNode(node); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return true; }
        insertBeside(sibling, leaf, orientation, before);
        // Queued: insertBeside() sizes a new two-pane splitter from a queued callback of its own,
        // and the sizes the pane had must be the last word.
        QPointer<QWidget> placed(leaf);
        QTimer::singleShot(0, this, [placed, sizes, slot] {
            auto *splitter = placed ? dynamic_cast<QSplitter *>(placed->parentWidget()) : nullptr;
            if (splitter && slot >= 0 && splitter->count() == sizes.size() && splitter->indexOf(placed) == slot)
                splitter->setSizes(sizes);
        });
        m_tabs->setCurrentWidget(pageOf(leaf));
        setActiveLeaf(leaf); focusLeaf(leaf);
        return true;
    }

    // Recently closed: give reopened tabs the names the owner had set by hand. `first` is the
    // index of the tab `names` starts at.
    void nameTabs(const QStringList &names, int first = 0) {
        for (int i = 0; i < names.size(); ++i)
            if (!names.at(i).isEmpty() && m_tabs->widget(first + i)) renameTab(names.at(i), false, m_tabs->widget(first + i));
    }
    int currentTabIndex() const { return m_tabs->currentIndex(); }
    QStringList openSessionIds() const {
        QStringList ids;
        for (Pane *pane : allPanes()) if (pane && !pane->sessionId().isEmpty()) ids.append(pane->sessionId());
        return ids;
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
    // A file on the host a terminal pane is logged into (#S5SH) travels as `ssh://host/path`: it
    // has no QFileInfo here, it is never a folder, and the preview pane fetches it over that
    // pane's own ssh connection. Everything else about the pane — which one is reused, where it
    // opens, the line it goes to — is the same, so it goes through the same function.
    // `edit` (card #SEJ2, the explorer's Ctrl+Enter) turns the preview into an editor once it is
    // open; it means nothing for a folder.
    void openPath(const QString &path, int line, QWidget *anchor, bool newPane = false, bool edit = false) {
        const bool remote = relay::remote::isFileUrl(path);
        const QFileInfo info(path);
        if (!remote && !info.exists()) { notice(QStringLiteral("No such file or folder: ") + path, 6000); return; }
        if (!anchor || !isLeaf(anchor) || anchor->window() != this) anchor = m_activeLeaf;
        if (!anchor) return;
        QWidget *page = pageOf(anchor);
        m_tabs->setCurrentWidget(page);
        // `ssh://host/etc/nginx/` is the host's folder and `ssh://host/etc/nginx/nginx.conf` its
        // file: the trailing slash is the whole difference, and it was decided on the host (#S5SH).
        const bool remoteFolder = remote && relay::remote::parseFileUrl(path).directory;
        const auto kind = (remote ? remoteFolder : info.isDir()) ? ToolPane::Kind::Explorer : ToolPane::Kind::Preview;
        const QString what = remote ? path : info.absoluteFilePath();   // the URL, or the local path
        ToolPane *target = nullptr;
        for (QWidget *leaf : leavesIn(page)) {
            auto *tool = dynamic_cast<ToolPane *>(leaf);
            if (!tool || tool->kind() != kind) continue;
            // A followed link wants its own pane, but not a second pane on a file one of them is
            // already showing: clicking back and forth between two documents would otherwise pile
            // up panes. So `newPane` reuses only an exact match, and never the anchor itself.
            if (!newPane) target = tool;
            else if (tool != anchor && tool->path() == what) target = tool;
        }
        if (target) {
            if (kind == ToolPane::Kind::Explorer) target->explorer()->setRoot(what);
            else target->preview()->open(what);
        } else {
            // A preview opens beside an explorer when there is one, otherwise beside the anchor.
            // A link followed from a preview opens beside that preview instead, so the two files
            // sit side by side and the reader can see where they came from.
            if (kind == ToolPane::Kind::Preview && !newPane)
                for (QWidget *leaf : leavesIn(page))
                    if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Explorer) anchor = tool;
            target = createToolPane(kind, what);
            insertBeside(anchor, target, Qt::Horizontal, false);
        }
        if (kind == ToolPane::Kind::Preview && line > 0) target->preview()->goToLine(line);
        if (edit && kind == ToolPane::Kind::Preview) target->preview()->startEditing();
        setActiveLeaf(target);
        focusLeaf(target);
        updateTitles();
    }

    // "Open items with a single click" (issue #0C7V) reaches every explorer pane that is already
    // open, in every window, not just the next one.
    static void applySingleClickSetting() {
        const bool on = relay::FileExplorer::singleClickDefault();
        const auto tops = QApplication::topLevelWidgets();
        // FileExplorer has no Q_OBJECT, so findChildren<relay::FileExplorer *> matches every
        // QWidget child (Qt 5 casts them all; Qt 6 refuses to compile it). dynamic_cast is the
        // honest test, the same way chromeOf() finds a PaneChrome.
        for (QWidget *top : tops)
            for (QWidget *child : top->findChildren<QWidget *>())
                if (auto *explorer = dynamic_cast<relay::FileExplorer *>(child))
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
    // A guest session resumed in a pane of its own (protocol 26.7). The pane is created *in* the
    // session's directory rather than cd'd into it afterwards, so the guest's own resume — which
    // resolves its id against the directory it starts in — sees the right one from the first line.
    void openGuestPane(Pane *source, const QString &guest, const QStringList &extra, const QString &cwd) {
        if (!source || source->window() != this || guest.isEmpty()) return;
        const QString directory = QFileInfo(cwd).isDir() ? cwd : source->cwd();
        Pane *pane = nullptr;
        try { pane = createPane({{"cwd", directory}, {"workspace", directory}}); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return; }
        insertBeside(source, pane, Qt::Horizontal, false);
        setActive(pane);
        // Tier A (protocol 29.4): when the worker can run this guest's harness, the new pane
        // resumes the session on its own agent instead — Relay's conversation, the guest's
        // session, no TUI. The source pane is asked because the new one has no presets yet; its
        // own worker answers the same way, and `resumeGuestPreset` waits for that answer.
        if (source->guestHarnessUsable(guest)) {
            const Pane::GuestResume resume = Pane::guestResumeFrom(extra);
            pane->resumeGuestPreset(guest, resume.sessionId, resume.fork, directory, extra);
            return;
        }
        // The pane names itself from its foreground program once the guest starts (the guest
        // registry does the detecting), so nothing is imposed on it here.
        pane->launchGuest(guest, extra, directory);   // the pane's own launch path (26.9)
    }

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
        // click; a bare arrow acts, and so does one with Ctrl still held from the split key when
        // the keymap leaves that chord free (card #JXWT), and everything else is passed on
        // untouched. Which widget has the keyboard does not matter: with no focus at all the key
        // reaches the window itself, and the arrow still places the pane (see #4PW5).
        if (m_placement.armed(m_placementClock.elapsed())
            && (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress)
            && [&] { auto *w = qobject_cast<QWidget *>(object); return !w || w->window() == this; }()) {
            using Placement = relay::panes::PlacementWindow;
            const auto *key = event->type() == QEvent::KeyPress ? static_cast<QKeyEvent *>(event) : nullptr;
            const Placement::Response response =
                key ? m_placement.keyPress(key->key(), key->modifiers(), m_placementClock.elapsed(),
                                           Keymap::instance().match(key))
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
        // The chord's window (#Q7Y9) stays open only for the keys that are part of the chord:
        // a modifier held on its own, and whatever the keymap binds to Move-left/right/down —
        // the Move-down key passes through here on its way to the shortcut that runs it. Every
        // other key closes it, Ctrl+C, Ctrl+D and Ctrl+L included: those are the shell's keys,
        // and treating any modified key as "half a shortcut" left the window armed while the
        // user typed on, so a Move-down minutes later docked a pane out of the blue.
        // A click or a scroll closes it too (the wheel over the tab bar changes tab).
        if (m_beneath.armed(m_beneathClock.elapsed())
            && (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress
                || event->type() == QEvent::Wheel)
            && [&] { auto *w = qobject_cast<QWidget *>(object); return !w || w->window() == this; }()) {
            const auto *pressed = event->type() == QEvent::KeyPress ? static_cast<QKeyEvent *>(event) : nullptr;
            if (!pressed || !relay::panes::chordKeyKeepsWindow(pressed->key(), Keymap::instance().match(pressed)))
                endBeneathDock();
        }
        activateOnPress(object, event);
        if (headerDrag(object, event)) return true;
        if (toolHeaderDrag(object, event)) return true;
        if (event->type() == QEvent::Resize && isLeaf(qobject_cast<QWidget *>(object)))
            if (auto *chrome = chromeOf(static_cast<QWidget *>(object))) chrome->place();
        if (object == m_tabs->tabBar() && (event->type() == QEvent::Resize || event->type() == QEvent::MouseMove || event->type() == QEvent::Leave
                                           || event->type() == QEvent::Enter || event->type() == QEvent::LayoutRequest)) {
            const bool resized = event->type() == QEvent::Resize;
            QTimer::singleShot(0, this, [this, resized] {
                placeTabBarControls();
                if (resized) relabelTabsForWidth();
            });
        }
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
        // The stylesheet and the tokens are the application's, so the window in front decides:
        // coming forward, it puts its current tab's theme back (themes are per tab).
        if (event->type() == QEvent::ActivationChange && isActiveWindow() && m_tabs) applyTabTheme(m_tabs->currentWidget());
        QMainWindow::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange) updateChromeState();
        // Coming back to a window catches the writers that cannot say they wrote: a dialog that has
        // just closed, relay.conf edited by hand, a second Relay. Only worth the redraw when this
        // window is actually showing an Options pane — otherwise activating any window would
        // rebuild every pane in the process.
        if (event->type() == QEvent::ActivationChange && isActiveWindow() && m_tabs
            && !settingsPanesIn(m_tabs->currentWidget()).isEmpty())
            refreshSettingsPanes();
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
        stopBoardWorkers();   // ask every Switchboard worker to exit; the destructor only kills
        // Saved window layout: snapshot the whole set before this window leaves it, so quitting
        // (every window closes at once) saves them all while closing one of several drops it.
        m_manager->noteWindowClosing();
        m_manager->forget(this);
        event->accept();
    }

private:
    // Ctrl+? (or F1): every action and its keys, so the window itself needs no shortcut bar.

    // On the active pane a hint joins that pane's toast queue and counts as shown only when it
    // appears (Pane::hint). Without a pane it goes straight to the status bar, shown at once.
    void hint(const QString &id, const QString &text, int limit = 3) {
        if (text.isEmpty()) return;
        if (m_active) { m_active->hint(id, text, limit); return; }
        if (!relay::ShortcutHints::instance().shouldShow(id, limit)) return;
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
    //
    // It asks first (finding 11 of card #XZZB): the layout it throws away is every window, tab,
    // pane and directory you had arranged, there is no way back to it, and nothing on screen
    // changes when it happens — so run by mistake from the Actions list, the only sign was a notice,
    // and the loss showed up at the next start.
    void startFreshWindowSet() {
        if (QMessageBox::question(this, QStringLiteral("Start a fresh window set?"),
                                  QStringLiteral("Relay forgets the saved layout — every window, tab, pane and "
                                                 "directory it would reopen — and stops saving this session. "
                                                 "The windows on screen are left as they are, and the next start "
                                                 "opens one new window.\n\nThis cannot be undone."),
                                  QMessageBox::Cancel | QMessageBox::Discard, QMessageBox::Cancel) != QMessageBox::Discard)
            return;
        m_manager->forgetSavedLayout(true);
        notice(QStringLiteral("Saved window layout cleared. This session is no longer saved; the next start opens one fresh window."), 9000);
        const QString key = Keymap::instance().shortcutText(QStringLiteral("windows.fresh"));
        hint(QStringLiteral("windows.fresh.palette"),
             key.isEmpty() ? QStringLiteral("Next time: start Relay with --fresh to skip the saved layout once")
                           : relay::ShortcutHints::nextTime(key, QStringLiteral("a fresh window set")));
    }

    // ----- actions ----------------------------------------------------------------------------
    // `target` is the pane the action acts on, and defaults to the focused one — which is every
    // caller but one: the keyboard, the Actions pane, the chrome buttons and the right-click menu
    // all mean "the pane the person is in". Only an `app_command` passes one (#AG7R group 2,
    // §30.3): until it could, a pane-scoped action asked for by the agent in tab 2 landed on
    // whatever pane happened to have the focus, which is why group 3's model, effort, input-mode
    // and plan-mode keys could not be turned on.
    void runAction(const QString &id, Pane *target = nullptr) {
        Pane *pane = m_active;
        // A split from a pane in an ssh session: the same host reached by hand in the new pane
        // teaches Split on the same host (#S5SH). It reads `m_active` and not `target` on purpose:
        // the split itself anchors on the focused leaf (see runActionNow), so the host to teach is
        // the focused pane's.
        const QString sshHost = id.startsWith(QStringLiteral("pane.split")) && pane
            ? relay::panestatus::remoteHost(pane->remoteCommandLine()) : QString();
        runActionNow(id, target);
        if (!sshHost.isEmpty() && m_active != pane) markForSshHint(m_active, sshHost);
    }

    void runActionNow(const QString &id, Pane *target = nullptr) {
        // The pane every `pane->…` branch below acts on. A `target` is the pane an `app_command`
        // was aimed at; unset means the focused pane, which is what every keyboard, palette and
        // chrome caller means and what this line said before there was a target at all.
        //
        // The window and layout branches deliberately do **not** follow it. The splits, the
        // explorer, Equalize, moving a pane or a tab and the focus keys anchor on `m_activeLeaf`
        // — where the person is looking — and they keep doing so: a new pane appearing beside a
        // pane in a tab nobody is watching, or a focus that jumps out of the tab someone is
        // typing in, is a worse surprise than the one being fixed here, and those are all
        // window-scoped keys in the first place (`appcommands::actionIsPaneScoped()` names the
        // set that is not). Aiming them would mean teaching `splitToward()` and its neighbours to
        // take a leaf, which is a change to the layout code and not this card's.
        Pane *pane = target ? target : m_active.data();
        // "Move left/right, then ↓ docks it beneath" (#Q7Y9): only Move-down may land inside the
        // chord's window; any other action closes it, so a stale chord never grabs a later one.
        if (id != QStringLiteral("pane.moveDown")) endBeneathDock();
        if (id == QStringLiteral("window.new")) m_manager->newWindowAt(activeCwd());
        else if (id == QStringLiteral("window.next")) m_manager->cycle(this, 1);
        else if (id == QStringLiteral("window.previous")) m_manager->cycle(this, -1);
        else if (id == QStringLiteral("windows.fresh")) startFreshWindowSet();
        else if (id == QStringLiteral("tab.new")) {
            if (addTab(paneNode(activeCwd()), m_tabs->currentIndex() + 1)) startNewTabTheme(m_tabs->currentWidget());
            markForSshHint(m_active, QString());   // an ssh typed here soon teaches Connect to host
        }
        else if (id == QStringLiteral("ssh.connect")) openSshMenu();
        else if (id == QStringLiteral("ssh.splitSameHost")) splitSameHost();
        else if (id == QStringLiteral("tab.next")) cycleTab(1);
        else if (id == QStringLiteral("tab.previous")) cycleTab(-1);
        // One key, one new pane on the right, then ← ↑ ↓ within two seconds to place it (#78BN).
        else if (id == QStringLiteral("pane.splitRight")) splitToward(relay::panes::Direction::Right, true);
        else if (id == QStringLiteral("pane.splitDown")) { splitToward(relay::panes::Direction::Down); hintPlacement(QStringLiteral("↓")); }
        else if (id == QStringLiteral("pane.splitLeft")) { splitToward(relay::panes::Direction::Left); hintPlacement(QStringLiteral("←")); }
        else if (id == QStringLiteral("pane.splitUp")) { splitToward(relay::panes::Direction::Up); hintPlacement(QStringLiteral("↑")); }
        // The pane chrome's one ⊞ button (#803C): a pane on the right at once, and no arrow window —
        // the pointer is in hand, so the pane is placed by dragging its header.
        else if (id == QStringLiteral("pane.newByMouse")) {
            splitToward(relay::panes::Direction::Right);
            notice(QStringLiteral("New pane · drag its header to place it"), 4000);
            hint(QStringLiteral("pane.new.mouse"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("pane.splitRight")), QStringLiteral("new pane")));
        }
        else if (id == QStringLiteral("pane.focusLeft")) navigate(relay::panes::Direction::Left);
        else if (id == QStringLiteral("pane.focusRight")) navigate(relay::panes::Direction::Right);
        else if (id == QStringLiteral("pane.focusUp")) navigate(relay::panes::Direction::Up);
        else if (id == QStringLiteral("pane.focusDown")) navigate(relay::panes::Direction::Down);
        else if (id == QStringLiteral("pane.close")) closeActive();
        else if (id.startsWith(QStringLiteral("terminal.zoom"))) {
            if (m_active) m_active->runTerminalMenuAction(id.mid(9), {}, {}, {});
        }
        else if (id == QStringLiteral("pane.moveLeft")) moveActive(relay::panes::Direction::Left);
        else if (id == QStringLiteral("pane.moveRight")) moveActive(relay::panes::Direction::Right);
        else if (id == QStringLiteral("pane.moveUp")) moveActive(relay::panes::Direction::Up);
        // Inside the chord's window, Move-down docks the pane beneath the neighbor it moved
        // toward (#Q7Y9); otherwise it moves the pane down as it always did.
        else if (id == QStringLiteral("pane.moveDown")) { if (!dockBeneathNeighbor()) moveActive(relay::panes::Direction::Down); }
        else if (id == QStringLiteral("pane.moveToNewTab")) { if (m_activeLeaf) moveLeafToNewTab(m_activeLeaf); }
        else if (id == QStringLiteral("pane.equalize")) equalizeActivePage();
        else if (id == QStringLiteral("tab.moveToNewWindow")) moveTabToNewWindow(m_tabs->currentIndex());
        else if (id == QStringLiteral("closed.restore")) m_manager->restore(this);
        else if (id == QStringLiteral("closed.list")) openClosedList();
        else if (id == QStringLiteral("files.explorer")) toggleExplorer(activeCwd(), m_activeLeaf);
        else if (id == QStringLiteral("files.open")) {
            const QString file = pickFileForPreview();
            if (!file.isEmpty()) openPath(file, 0, m_activeLeaf);
        }
        else if (id == QStringLiteral("board.open")) toggleBoardPane();
        else if (id == QStringLiteral("tests.open")) openTestSuitesPane();   // card #7BM4
        else if (id == QStringLiteral("helper.ask")) focusHelperOfActiveLeaf();
        else if (id == QStringLiteral("notifications.jump")) jumpToNotification();   // #NQP9
        else if (id == QStringLiteral("project.pick")) openProjectPicker(m_active, QString());
        else if (id == QStringLiteral("palette.open")) toggleSettingsPane(true);
        else if (id == QStringLiteral("keybindings.reload")) Keymap::instance().reload();
        else if (id == QStringLiteral("help.shortcuts")) openShortcutsTab();
        else if (id == QStringLiteral("app.settings")) toggleSettingsPane(false);
        else if (id == QStringLiteral("app.update")) {
            updateApp();
            hint(QStringLiteral("update.palette"), QStringLiteral("Next time: type /update in any prompt box"));
        }
        else if (id == QStringLiteral("keybindings.edit")) {
            Keymap::instance().ensureFile();
            const QString editor = qEnvironmentVariable("VISUAL", qEnvironmentVariable("EDITOR", QStringLiteral("nano")));
            const QString quoted = QStringLiteral("'") + QString(Keymap::instance().path()).replace('\'', QStringLiteral("'\\''")) + '\'';
            if (!pane || !pane->runCommand(editor + ' ' + quoted))
                notice(QStringLiteral("Shortcuts file: ") + Keymap::instance().path());
        }
        else if (id == QStringLiteral("agent.subagentPane")) toggleSubagentPane();   // card #WD83
        else if (id == QStringLiteral("remote.openShared")) openSharedPaneDialog();   // Relay-to-Relay
        else if (id == QStringLiteral("remote.pair")) pairPhone();   // #FR1C: one entry point
        else if (id == QStringLiteral("remote.join")) joinSharedSession();
        // Alt+M and Ctrl+Alt+M in a *helper* prompt box (#PK5Q). The two keys belong to a prompt
        // box rather than to a terminal pane: the helper's composer carries the same model box
        // now, so the same keys reach it. Before the `!pane` guard, because Options, Actions,
        // Sessions and the Switchboard are not terminal panes and this is where they answer.
        else if ((id == QStringLiteral("agent.modelBox") || id == QStringLiteral("agent.model"))
                 && helperComposerHasFocus()) {
            if (id == QStringLiteral("agent.modelBox")) openConsoleModelBox();
            else openConsoleModelPicker();
        }
        else if (!pane) return;
        else if (id == QStringLiteral("terminal.native")) pane->toggleNative();
        else if (id == QStringLiteral("pane.restartShell")) pane->restartStopped();
        else if (id == QStringLiteral("links.step")) pane->stepOutputLink(-1);
        else if (id == QStringLiteral("control.human")) pane->takeControl();
        else if (id == QStringLiteral("control.prompt")) pane->showPrompt();
        else if (id == QStringLiteral("program.delegate")) pane->delegateProgram();
        else if (id == QStringLiteral("input.toggle")) pane->toggleInputMode();
        else if (id == QStringLiteral("agent.flashAgent")) pane->toggleFlashAgent();   // model roles
        else if (id == QStringLiteral("agent.highAgent")) pane->toggleHighAgent();     // /high, card #MDL1
        else if (id == QStringLiteral("agent.model")) pane->openModelPicker();          // Ctrl+Alt+M, /model
        else if (id == QStringLiteral("agent.modelBox")) pane->openModelBox();          // Alt+M
        else if (id == QStringLiteral("agent.effortBox")) pane->openEffortBox();        // Alt+E
        else if (id == QStringLiteral("agent.modelOptions")) openSettingsPane(relay::SettingsPane::Mode::Options, QStringLiteral("models"));   // Ctrl+Shift+M, /models
        else if (id == QStringLiteral("agent.localAgent")) pane->toggleLocalAgent();   // /local
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
        else if (id == QStringLiteral("agent.resume")) toggleSessionsPane(pane);
        // The key (Alt+I), the ⓘ button, the Actions pane and /status all land here. Nothing is
        // taught from here: this is also the keyboard path, and the two slow paths teach the key
        // themselves (the button below in syncChrome, the Actions pane through runFromSettings).
        else if (id == QStringLiteral("agent.info")) pane->openInfo();
        else if (id == QStringLiteral("conversations.open")) toggleSessionsPane(pane);
        else if (id == QStringLiteral("find.inView")) pane->openFindInView();
        else if (id == QStringLiteral("agent.recap")) pane->requestRecap();
        else if (id == QStringLiteral("agent.requests")) pane->toggleRequests();
        else if (id == QStringLiteral("agent.thinkingPanel")) pane->toggleThinkingPanel();
        else if (id == QStringLiteral("agent.internalsPane")) openInternalsPane(pane);
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
        else if (id == QStringLiteral("pane.sharing")) openSharingPane(pane, true);
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
        // Settings › Local models › "Set up a model with the agent…" (card #24XJ). The agent pane
        // comes to the front and is handed one prompt; the skill it names does the rest.
        else if (id == QStringLiteral("agent.localModelSetup")) {
            setActive(pane);
            focusLeaf(pane);
            pane->askAgent(QStringLiteral("Load the local-model-setup skill and set up a local model on this machine for me."),
                           QStringLiteral("Settings · Local models"));
        }
        else if (id == QStringLiteral("input.modeAuto")) pane->setMode(QStringLiteral("auto"));
        else if (id == QStringLiteral("input.modeTerminal")) pane->setMode(QStringLiteral("shell"));
        else if (id == QStringLiteral("input.modeAgent")) pane->setMode(QStringLiteral("agent"));
        // The Model and Reasoning-effort submenus' children, `model:<id>` and `effort:<level>`.
        // Their palette rows run a closure over `m_active` (rootItems()), which is the focused
        // pane and nothing else; these two branches are how the same two setters are reached with
        // a pane named — "put *this* pane on the local model" (#AG7R groups 2 and 3). Matched by
        // prefix, as `appcommands::isOnePickedModel()` does, because the id is a stored preset and
        // the level is whatever the provider offers: there is no fixed set to compare against.
        // A key nothing stored answers to never gets here — it is not in the catalog, so the
        // executor refuses it `unknown_action` first.
        else if (id.startsWith(QStringLiteral("model:")) && id.size() > 6) pane->selectModel(id.mid(6));
        else if (id.startsWith(QStringLiteral("effort:")) && id.size() > 7) pane->setEffort(id.mid(7));
    }

    // ----- palette ----------------------------------------------------------------------------
    using PaletteItem = relay::ActionItem;

    // ----- Actions pane and Options pane (src/SettingsPane.h) -----------------------------------
    // Two panes, beside the focused pane (owner, 2026-09-18: a full pane, not a strip over the
    // right edge). Ctrl+Shift+A is Actions: one filterable list of everything you can do now, with
    // its keys — resume, the Switchboard, the model, a new pane, rewind, Options itself.
    // Ctrl+Shift+O, Ctrl+, and the gear are Options: what persists, every setting as a real
    // control, one tab per section. Either search reaches both catalogs, so "Ctrl+Shift+A, type,
    // Enter" always lands somewhere. The panes are transient: they are not saved with the layout,
    // and closing one returns focus to the widget that had it (vim in the terminal, or the prompt).
    //
    // **They open side by side.** Until 2026-09-18 this was one pane in two modes and the other
    // key swapped it in place, so a setting could not be read while the action that needed it was
    // on screen (owner: "you cant have the options menu and actions menu both open
    // simultaneously"). Each mode is now its own pane, and a key or a button finds, focuses or
    // closes the pane in *its* mode and leaves the other one alone.
    static QList<ToolPane *> settingsPanesIn(QWidget *page) {
        QList<ToolPane *> out;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->settings()) out << tool;
        return out;
    }

    // The pane in that mode, preferring the focused one if a page somehow holds two of it (a row
    // in Actions that reveals an option turns that pane into an Options pane in place).
    ToolPane *settingsPaneIn(QWidget *page, relay::SettingsPane::Mode mode) const {
        if (!page) return nullptr;
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data());
            tool && tool->settings() && pageOf(tool) == page && tool->settings()->mode() == mode)
            return tool;
        for (ToolPane *tool : settingsPanesIn(page))
            if (tool->settings()->mode() == mode) return tool;
        return nullptr;
    }

    // The pane-type property is what the pane chrome colours and labels a header by.
    static void markSettingsPaneType(ToolPane *tool) {
        const bool actions = tool->settings()->mode() == relay::SettingsPane::Mode::Actions;
        tool->setProperty("paneType", actions ? QStringLiteral("actions") : QStringLiteral("options"));
    }

    // ----- the helper's panels are gone; its worker is not (card #AGNT step 5) ----------------
    //
    // `wireHelperPanel` lived here: one template that gave the Options, Actions and Sessions
    // panes the Switchboard's helper panel, its own `board_chat` FIFO and its own model box.
    // `board_chat` is retired on the wire (step 4) and the panel is replaced by an embedded
    // agent console (`createAgentConsole`, above), so the template had nothing left to wire: a
    // panel's `onHelperSend` now has no message the worker would answer. The three hosts each
    // grow a `relay::agent::ConsoleFactory onCreateConsole` instead and keep `focusHelper()`,
    // which is what `helper.ask` (Alt+Q) calls.
    //
    // What did **not** go with it: `helperWorker`, `startBoardWorker`, `sendToHelper`,
    // `listenToHelper` and `deliverToHelperPanels`. Those are the tab worker itself and its
    // subscription, and they still carry the Test suites pane's `tests_*`, the Profile pane's
    // `profile_*`, the board's own writes and `signals_config` — none of which was ever chat.

    // The four things a console host needs from the window, and nothing else. `SettingsPane` and
    // `relay::conversations::SessionManager` name them identically on purpose (step 7), so this is
    // one template rather than two copies that would drift the first time one grew a fifth.
    //
    // `leaf` is the `ToolPane` the view is hosted in. It is not in a tab while its creator runs —
    // `insertBeside` comes afterwards — so the tab's id and workspace are pushed a turn of the
    // event loop later, the way the Switchboard's own `board_open` waits, and again whenever the
    // tab's project changes (`refreshConsoleHosts`).
    template <typename View>
    void wireConsoleHost(View *view, QWidget *leaf, const QString &hintId) {
        QPointer<QWidget> guard(leaf);
        QPointer<View> viewGuard(view);
        view->onCreateConsole = [guard](relay::agent::Context *context, QWidget *parent) {
            auto *w = windowOf(guard);
            return w ? w->createAgentConsole(context, parent) : relay::agent::ConsoleHandle();
        };
        // The live key, never a written one (WARP.md's standing rule): the collapsed row says it.
        view->setHelperShortcut(hintId, Keymap::instance().shortcutText(QStringLiteral("helper.ask")));
        // And the slow path teaches it once. The row's own text carries the key too, but a button
        // that was clicked is exactly the case the standing rule is about, and the host cannot
        // show a toast — it is a pane library with no window. So it says which hint it earned and
        // the window shows it, through the same gates every other hint goes through.
        view->onHelperHint = [guard, hintId] {
            auto *w = windowOf(guard);
            if (!w) return;
            w->hint(hintId, relay::ShortcutHints::nextTime(
                                Keymap::instance().shortcutText(QStringLiteral("helper.ask")),
                                QStringLiteral("the helper agent")));
        };
        QTimer::singleShot(0, this, [guard, viewGuard] {
            auto *w = windowOf(guard);
            if (!w || !viewGuard) return;
            QWidget *page = w->pageOf(guard);
            if (!page) return;
            viewGuard->setHelperTabId(w->tabIdOf(page));
            viewGuard->setHelperWorkspace(w->boardWorkspaceOfTab(page));
        });
    }

    // A tab that gained (or changed) its project: its console hosts are told, so the next ask
    // goes out with the board the tab is now attached to. The conversation does not move — the
    // key is the tab's and the tab has not changed — which is what protocol 30.7's move test says.
    void refreshConsoleHosts(QWidget *page) {
        if (!page) return;
        const QString tab = tabIdOf(page);
        const QString workspace = boardWorkspaceOfTab(page);
        for (QWidget *leaf : leavesIn(page)) {
            auto *tool = dynamic_cast<ToolPane *>(leaf);
            if (!tool) continue;
            if (relay::SettingsPane *settings = tool->settings()) {
                settings->setHelperTabId(tab);
                settings->setHelperWorkspace(workspace);
            }
            if (auto *sessions = sessionsViewOf(tool)) {
                sessions->setHelperTabId(tab);
                sessions->setHelperWorkspace(workspace);
            }
        }
        // And the **worker** is told, so the tab's consoles stop talking about a project they no
        // longer work in. This is the GUI's half of the bargain the backend's `helper_file`
        // names: the window sends the tab's project as soon as it knows it, and the backend does
        // not move a conversation that was started before it did. Without the reconfigure the
        // console kept the old board until its next `configure` — which, for a tab that gains a
        // project, is the next restart.
        if (m_boardWorkers.contains(tab)) reconfigureBoardWorkers();
    }

    // `pickHelperModel`, `applyHelperEntry` and `helperModelState` were here: the window kept the
    // helper's model-box state per tab, drew every helper panel's box from it, and wrote the
    // persisted `switchboard` role on a pick. Every prompt box in Relay is a `Pane` now
    // (card #AGNT), so each console draws **its own** box from its own worker's `configured` and
    // sends its own `set_model`, which is what #PK5Q asked for. The roles dialog is the other
    // writer of that role and still calls `reconfigureBoardWorkers()`.

    // "more models…" on a console's box, and Ctrl+Alt+M in one: the same dialog a terminal
    // pane opens, because a console *is* a terminal pane with no shell. Nothing is kept here —
    // the console asks its own picker with its own current row.
    void openConsoleModelPicker() {
        if (Pane *console = focusedConsole()) console->openModelPicker();
    }

    // The ask key (Keymap `helper.ask`): open the helper of the pane the keyboard is in and put
    // the cursor in its composer. One key for all of them, because it is one helper — the tab's
    // — wherever it is asked; the Switchboard's list page keeps its bare `a` (#8YQ9), which it can
    // have because that list takes no typing.
    // The model box of the helper prompt box the keyboard is in, or null. A terminal pane's box
    // is the pane's own business (Pane::openModelBox); this is every other prompt box in Relay —
    // the Switchboard's composer and an open card's reply box, and the panels in Options, Actions
    // and Sessions (#PK5Q, §30.7).
    //
    // Which prompt box the cursor is in is asked of the **keyboard**, by walking up from the focus
    // widget, rather than of the window's idea of which leaf is active. A live run found out why:
    // clicking into the Switchboard's composer and pressing Alt+M dropped open the *terminal
    // pane's* box, because the window still called the terminal pane the active leaf. The widget
    // the key actually went to cannot be wrong about this.
    // The console the keyboard is in, or null. Since card #AGNT every prompt box in Relay is a
    // `Pane` — a terminal pane, or a console embedded in the Switchboard, a card, Options,
    // Actions or Sessions — so the walk stops at the first `Pane` above the focus widget and that
    // pane answers with its own model box, its own `/model` and its own picker. It used to ask
    // the helper panel for a box, and then `BoardView` for one; with the panels gone both
    // answered null and **Alt+M inside a console did nothing at all**.
    //
    // Which prompt box the cursor is in is asked of the *keyboard*, by walking up from the focus
    // widget, rather than of the window's idea of which leaf is active. A live run found out why:
    // clicking into the Switchboard's composer and pressing Alt+M dropped open the terminal
    // pane's box, because the window still called the terminal pane the active leaf. The widget
    // the key actually went to cannot be wrong about this.
    //
    // dynamic_cast, not qobject_cast: nothing in this stack declares Q_OBJECT.
    Pane *focusedConsole() const {
        for (QWidget *widget = QApplication::focusWidget(); widget != nullptr; widget = widget->parentWidget())
            if (auto *pane = dynamic_cast<Pane *>(widget)) return pane == m_active ? nullptr : pane;
        return nullptr;
    }
    bool helperComposerHasFocus() const { return focusedConsole() != nullptr; }
    void openConsoleModelBox() {
        if (Pane *console = focusedConsole()) console->openModelBox();
    }

    void focusHelperOfActiveLeaf() {
        auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data());
        if (tool && tool->settings()) { tool->settings()->focusHelper(); return; }
        if (tool && tool->board()) { tool->board()->focusChat(); return; }
        if (auto *sessions = sessionsViewOf(tool)) { sessions->focusHelper(); return; }
        notice(QStringLiteral("The helper agent is in Options, Actions, Sessions and the "
                              "Switchboard — open one of those and ask it there."), 5000);
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
        // Local models probes loopback ports, which wakes a sleeping server, so it happens when the
        // section is put in front and on an explicit Refresh — never on a timer (card #24XJ).
        view->onSectionShown = [guard](const QString &sectionId) {
            auto *w = windowOf(guard);
            if (w && sectionId == relay::LocalModelsSettings::sectionId()) w->localModels().refresh();
        };
        view->onClose = [guard] { if (auto *w = windowOf(guard)) w->closeSettingsPane(guard); };
        view->onRun = [guard](const relay::ActionItem &item) { if (auto *w = windowOf(guard)) w->runFromSettings(guard, item); };
        // The agent console at the foot of the pane (#AGNT step 5, replacing #FEJQ's panel). The
        // window makes it, because a pane library cannot name `Pane`; the pane owns the collapsed
        // "? Helper Agent (Alt+Q)" row and asks for one on **first expand**, so a tab whose helper
        // nobody opens pays for nothing. `option:` links resolve inside the pane itself through
        // its own context; everything else the answer names travels the window's own routes.
        wireConsoleHost(view, tool, QStringLiteral("options.ask"));
        return tool;
    }

    // The action catalog for the pane's search and its Actions tab, with the hidden search words
    // folded into each item so the pane needs no table of its own.
    QList<PaletteItem> searchableActions() {
        QList<PaletteItem> items = rootItems();
        for (PaletteItem &item : items) item.aliases = (item.aliases + ' ' + paletteAliases(item)).trimmed();
        return items;
    }

    // Open this tab's pane for that mode, or focus the one it already has; `tab` picks the
    // Options section. A pane in the *other* mode is left where it is.
    void openSettingsPane(relay::SettingsPane::Mode mode, const QString &tab = QString(), const QString &search = QString()) {
        QWidget *page = m_tabs->currentWidget();
        ToolPane *tool = settingsPaneIn(page, mode);
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
        } else {
            tool->settings()->rebuild();
        }
        if (!tab.isEmpty()) tool->settings()->showTab(tab);
        tool->settings()->setSearch(search);
        setActiveLeaf(tool);
        tool->settings()->focusSearch();
        updateTitles();
    }

    // ----- the agent drives the app (card #FEJQ, protocol §30) ---------------------------------
    //
    // One executor per window, two sources: a pane's own worker and the tab's helper worker both
    // send `app_command` down their own pipe, and both are answered here (§30.3). The rules — the
    // catalog's shape, what is settable, what is agent-safe, the change log and Undo — are in
    // relay::AppCommands, which knows nothing about windows and is tested without one.
    relay::AppCommands &appCommands() {
        if (!m_appCommands.sections) {
            m_appCommands.sections = [this] { return settingsSections(); };
            m_appCommands.actions = [this] { return searchableActions(); };
            m_appCommands.writesEnabled = [] { return agentWritesEnabled(); };
            m_appCommands.openTarget = [this](const QJsonObject &command, QString *error) {
                return openAppTarget(command, error);
            };
            // The keybinding registry, for the safe keys the palette has no row for — moving the
            // focus between panes, Actions, the shortcuts page, the agents menu, the session
            // manager. Twelve of them answered `unknown_action` until 2026-09-20 (#AG7R group 1).
            // An unregistered key gets an empty label, which is what keeps it `unknown_action`.
            m_appCommands.registryLabel = [](const QString &key) {
                for (const ActionDef &action : Keymap::instance().actions())
                    if (action.id == key) return action.description;
                return QString();
            };
            m_appCommands.runRegistryAction = [this](const QString &key) { runAction(key); };
            // Aiming a command at a pane (#AG7R group 2, §30.3). A pane is named by its **session
            // token** — the same string the change log's `who` already carries for a pane agent,
            // so a pane agent's own pane resolves out of `who` with nothing asked of the model,
            // and the id an agent reads in `list_panes` is the id it is named by everywhere else.
            m_appCommands.paneExists = [this](const QString &token) {
                return !token.isEmpty() && findPaneByToken(token) != nullptr;
            };
            m_appCommands.runActionAt = [this](const QString &key, const QString &token) {
                Pane *pane = findPaneByToken(token);
                if (!pane) return false;     // closed while the command was in flight
                runAction(key, pane);
                return true;
            };
            // One pane's agent putting a prompt into another pane, and naming a pane or a tab
            // (#AG7R group 8, §30.3). Both take the command with its `pane` already resolved, so
            // the rules about *which* pane live in one place and these only do the act.
            m_appCommands.deliverPrompt = [this](const QJsonObject &command, bool *queued, QString *error) {
                return deliverAppPrompt(command, queued, error);
            };
            m_appCommands.renameTarget = [this](const QJsonObject &command, QString *previous, QString *error) {
                return renameAppTarget(command, previous, error);
            };
            // The panes of this window, for `list_panes`: an agent cannot aim at a pane but its
            // own until it can read the others' ids. Answered on demand rather than carried in the
            // `app` block, because panes open and close between two catalogs and a stale list
            // would have an agent name a pane that has gone.
            m_appCommands.panes = [this]() -> QJsonArray {
                QJsonArray rows;
                for (Pane *pane : allPanes()) {
                    if (!pane) continue;
                    rows.append(QJsonObject{
                        {QStringLiteral("id"), pane->sessionToken()},
                        // What the person would call it: the pane's own title, or its directory
                        // the way the header shows it when there is none.
                        {QStringLiteral("title"), pane->paneTitle().isEmpty()
                                                      ? shortPath(pane->cwd()) : pane->paneTitle()},
                        {QStringLiteral("cwd"), pane->cwd()},
                        {QStringLiteral("tab"), tabIdOfPane(pane)},
                        {QStringLiteral("model"), pane->currentPreset()},
                        {QStringLiteral("mode"), pane->mode()},
                        {QStringLiteral("busy"), pane->agentBusy()},
                        {QStringLiteral("focused"), pane == m_active}});
                }
                return rows;
            };
        }
        return m_appCommands;
    }

    // Options › Agent, "Agents may change options and run actions" (owner decision 3, 2026-09-20):
    // one toggle gating the helper and the pane agent together. On when Relay ships.
    static bool agentWritesEnabled() {
        return QSettings().value(QStringLiteral("agent/app_writes"), true).toBool();
    }

    // The tab's persistent id (§30.2 `tab`, §30.7). A tab had no identity that survived a restart
    // — the saved layout is a list, and its position is not an identity, because a tab moved or
    // closed renumbers its neighbours. The helper worker is keyed by this and so is its persisted
    // conversation, so it is minted once, lazily, and saved with the tab (serializeTab / addTab).
    //
    // It is not shareTabId(): that one is deliberately not saved, because a share does not outlive
    // the process. This one is written into the layout, so the prefix differs ("t…" against
    // "tab-…") and neither can be passed where the other is meant.
    static QString tabIdOf(QWidget *page) {
        if (!page) return {};
        QString id = page->property("relayTabId").toString();
        if (id.isEmpty()) {
            id = QLatin1Char('t') + QUuid::createUuid().toString(QUuid::Id128).left(12);
            page->setProperty("relayTabId", id);
        }
        return id;
    }
    QString tabIdOfPane(QWidget *leaf) { return tabIdOf(pageOf(leaf)); }

    // The `app` block for a worker in this tab. Rebuilt every time: it carries current values, so
    // a setting the person changed by hand has to reach the agent about to describe it (§30.2).
    QJsonObject appCatalogFor(QWidget *page) { return appCommands().catalog(tabIdOf(page)); }

    // The catalog changed — a setting written anywhere, a key added, the Agent toggle flipped — so
    // every worker of this window is sent the whole block again. Nothing is cached across a
    // refresh on either side (§30.2), which is why this sends the block and not a delta.
    void sendAppCatalog() {
        for (Pane *pane : allPanes()) if (pane) pane->sendAppCatalog();
        sendHelperCatalogs();   // the tab's helper worker runs the same tools (§30.7)
    }

    // The helper workers of this window, each sent its own tab's block: the helper is an agent
    // like a pane's and has the same app tools (§30.7), so it is configured with the same catalog.
    void sendHelperCatalogs() {
        for (auto it = m_boardWorkers.cbegin(); it != m_boardWorkers.cend(); ++it)
            if (relay::BoardWorker *worker = it.value().data())
                worker->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("app_catalog")},
                                         {QStringLiteral("app"), appCommands().catalog(helperTab(worker))}});
    }

    // keybindings.json was reloaded, so every helper worker of this window is sent the catalogue
    // again — `Pane::sendKeybindings()` for the helpers (§30.2, #GMCF). It matters twice over
    // since the helper may rebind keys itself: `app_action_list` shows the keys as they are now,
    // and the helper's own `set_keybinding` writes against the file it has just been told about.
    // Only a configured worker: an unconfigured one has no agent to hand the catalogue to and
    // answers the message with an error, which is why a pane guards on `m_configured` too.
    void sendHelperKeybindings() {
        for (auto it = m_boardWorkers.cbegin(); it != m_boardWorkers.cend(); ++it)
            if (relay::BoardWorker *worker = it.value().data(); worker && worker->configured())
                worker->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("keybindings")},
                                         {QStringLiteral("path"), Keymap::instance().path()},
                                         {QStringLiteral("actions"),
                                          Keymap::instance().catalog().value(QStringLiteral("actions"))}});
    }

    // One `app_command`, answered on the connection it arrived on. `who` is for the change log and
    // the notification only: the policy is `writes_enabled`, `settable` and `agent_safe`, and it is
    // the same policy whichever agent asked (§30.8).
    QJsonObject executeAppCommand(const QJsonObject &command, const QString &who) {
        return appCommands().execute(command, who);
    }

    // `open {target, section?, row?, query?, card?}` (§30.3). It opens in *this* window, the one
    // that owns the worker the command came out of — there is no routing field on the wire.
    bool openAppTarget(const QJsonObject &command, QString *error) {
        const auto fail = [error](const QString &word) { if (error) *error = word; return false; };
        const QString target = command.value(QStringLiteral("target")).toString();
        const QString section = command.value(QStringLiteral("section")).toString();
        const QString row = command.value(QStringLiteral("row")).toString();
        const QString query = command.value(QStringLiteral("query")).toString();
        const QString card = command.value(QStringLiteral("card")).toString();
        if (target == QStringLiteral("options") || target == QStringLiteral("actions")) {
            const bool actions = target == QStringLiteral("actions");
            const auto mode = actions ? relay::SettingsPane::Mode::Actions : relay::SettingsPane::Mode::Options;
            openSettingsPane(mode, actions ? QString() : section, query);
            // Zooming to a row is revealOption()'s job, and it switches to Options mode itself, so
            // "open Actions at this option" lands on the control rather than on a search result.
            if (!row.isEmpty()) {
                ToolPane *tool = settingsPaneIn(m_tabs->currentWidget(), mode);
                if (!tool || !tool->settings()) return fail(QStringLiteral("failed"));
                tool->settings()->revealOption(section, row);
            }
            return true;
        }
        if (target == QStringLiteral("sessions")) { openSessions(QString(), query); return true; }
        // `conversation`: open a past conversation itself, which is what Enter on a Sessions row
        // does (`SessionManager::onResume` → `Pane::openSavedSession`). The helper could search
        // the index and open the *list* at the search, and nothing more, so "open a group of
        // previous sessions in new panes" came back as a list of titles (owner, 2026-09-20).
        // The worker resolved the id and sent the whole row: there is no conversation index in
        // this process, and a guest row resumes by running its own argv (26.7).
        if (target == QStringLiteral("conversation")) {
            QJsonObject item;
            bool newPane = true;
            if (!relay::appcommands::conversationToOpen(command, &item, &newPane, error)) return false;
            // The pane it opens from: the pane the command was aimed at (#AG7R group 2 — for a
            // pane agent that is its own pane, so `new_pane: false` means "here" and not "wherever
            // the focus is"), else the active terminal pane, else this tab's first. With
            // `new_pane` it is only the anchor the new pane goes beside; without it, it is the
            // pane the conversation is loaded into — the same pane Enter on a row would have
            // used, since the manager is bound to it.
            Pane *owner = findPaneByToken(command.value(QStringLiteral("pane")).toString());
            if (!owner) owner = m_active.data();
            if (!owner) { const auto panes = panesIn(m_tabs->currentWidget()); owner = panes.isEmpty() ? nullptr : panes.first(); }
            if (!owner) return fail(QStringLiteral("failed"));
            owner->openSavedSession(item, newPane);
            return true;
        }
        if (target == QStringLiteral("switchboard")) {
            if (!card.isEmpty()) { openBoardCard(card); return true; }
            runAction(QStringLiteral("board.open"));
            return true;
        }
        // The rest of Relay's panes (#AG7R group 8). Each had an *action* and no `app_open`
        // target, so an agent could open Options and the Switchboard by naming them and had to
        // reach for a key — two of them among group 1's unreachable twelve — for the explorer,
        // Test suites, Activity, ⓘ, requests and subagents. They route to the same window code
        // the action runs rather than repeating it, and the pane-scoped ones follow the aim
        // (§30.3) instead of landing on whichever pane has the focus.
        Pane *aim = findPaneByToken(command.value(QStringLiteral("pane")).toString());
        if (target == QStringLiteral("files")) {
            // toggleExplorer() takes the anchor the action's own branch passes as `m_activeLeaf`:
            // aimed, the explorer opens on that pane's directory and beside it.
            if (aim) toggleExplorer(aim->cwd(), aim); else runAction(QStringLiteral("files.explorer"));
            return true;
        }
        if (target == QStringLiteral("subagents")) {
            // `agent.subagentPane` is one of the keys that is not in `actionIsPaneScoped()` — its
            // branch reads the focused pane — so an aimed open calls what that branch calls
            // rather than silently landing somewhere else.
            if (aim) aim->openSubagentPane(); else runAction(QStringLiteral("agent.subagentPane"));
            return true;
        }
        static const QMap<QString, QString> paneTargets = {
            {QStringLiteral("tests"), QStringLiteral("tests.open")},            // #7BM4
            {QStringLiteral("activity"), QStringLiteral("agent.internalsPane")},
            {QStringLiteral("info"), QStringLiteral("agent.info")},
            {QStringLiteral("requests"), QStringLiteral("agent.requests")}};
        if (const QString key = paneTargets.value(target); !key.isEmpty()) {
            runAction(key, aim);   // aimed where the action is pane-scoped; ignored where it is not
            return true;
        }
        return fail(QStringLiteral("unknown_target"));
    }

    // `send_prompt` / `prefill_prompt` (§30.3, #AG7R group 8): the prompt goes to the pane the
    // executor resolved, which has already refused a pane that is not this window's, the asking
    // agent's own pane and a chain of agents prompting each other. All that is left here is the
    // pane itself, which knows whether its composer is free and whether its agent is busy.
    bool deliverAppPrompt(const QJsonObject &command, bool *queued, QString *error) {
        Pane *pane = findPaneByToken(command.value(QStringLiteral("pane")).toString());
        if (!pane) { if (error) *error = QStringLiteral("unknown_pane"); return false; }   // closed in flight
        return pane->takeAgentPrompt(command.value(QStringLiteral("text")).toString(),
                                     command.value(QStringLiteral("from_label")).toString(),
                                     command.value(QStringLiteral("send")).toBool(), queued, error);
    }

    // `rename` (§30.3, #AG7R group 8): `/rename` and `/rename-tab` for an agent, since those are
    // slash commands typed into a composer and no agent can type into one. The pane's own rename
    // goes through the pane (it owns the title and the worker that stores it); a tab's goes
    // through `renameTab()`, the same call the double click and `/rename-tab` make.
    bool renameAppTarget(const QJsonObject &command, QString *previous, QString *error) {
        const auto fail = [error](const QString &word) { if (error) *error = word; return false; };
        Pane *pane = findPaneByToken(command.value(QStringLiteral("pane")).toString());
        if (!pane) return fail(QStringLiteral("unknown_pane"));
        const QString name = command.value(QStringLiteral("name")).toString();
        if (command.value(QStringLiteral("what")).toString() == QStringLiteral("tab")) {
            QWidget *page = pageOf(pane);
            if (!page) return fail(QStringLiteral("failed"));
            if (previous) *previous = m_tabNames.value(page);
            renameTab(name, false, page);
            return true;
        }
        return pane->takeAgentRename(name, previous, error);
    }

    // Two keys, a pane each: Ctrl+Shift+A is Actions (things to do now), Ctrl+Shift+O and the gear
    // are Options (what persists). Each opens its own pane, or focuses it if this tab already has
    // one; pressed while that pane has the focus, it closes it. Neither key touches the other's
    // pane, so both can be on screen at once.
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
            // Options, chosen from Actions: Options opens beside the list it was chosen from, which
            // stays open — the point of two panes is reading a setting and its action together.
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

    // Something a setting depends on changed elsewhere (a keymap reload, a theme file, a page put
    // back to its defaults): redraw. Every Options pane in this process listens on the watch, in
    // this window's other tabs and in the other windows too, so a value is never left on screen one
    // edit out of date — which is what happened until 2026-09-19, when only the pane that made the
    // edit rebuilt (finding 3 of card #XZZB). The pane's own controls notify the watch themselves.
    static void refreshSettingsPanes() { relay::SettingsWatch::instance().notify(); }

    // Settings › Local models (card #24XJ). One per window: the rows are a section of the Options
    // pane, the messages go out through whichever pane is active — the same worker connection the
    // keys dialog uses — and the answers come back through Pane::onLocalModelEvent.
    relay::LocalModelsSettings &localModels() {
        if (!m_localModels.send) {
            m_localModels.send = [this](const QJsonObject &request) {
                return m_active && m_active->sendLocalModelRequest(request);
            };
            m_localModels.onChanged = [this] { refreshSettingsPanes(); };
            // A saved or removed endpoint changes what every pane may switch to, and each pane has
            // its own worker, so each is asked for `presets` again — what the keys dialog does for
            // a key, for every pane rather than one.
            m_localModels.onPresetsChanged = [this] {
                for (Pane *pane : allPanes()) pane->refreshPresets();
                refreshSettingsPanes();
            };
            m_localModels.onSetupWithAgent = [this] { runAction(QStringLiteral("agent.localModelSetup")); };
        }
        return m_localModels;
    }
    relay::LocalModelsSettings m_localModels;
    // The agent's side of Options and Actions (#FEJQ, §30): the catalog, the executor for one
    // `app_command`, and the change log that makes every write visible and undoable. One per
    // window, wired lazily by appCommands(); a pane's worker and the tab's helper both go
    // through it, so Undo works whichever of them made the change.
    relay::AppCommands m_appCommands;
    // The panes of this window that asked for their tab worker's events (listenToHelper):
    // Test suites and Profile. Not the consoles, which `deliverToConsoles` reaches.
    struct HelperListener {
        QPointer<QWidget> page;
        QPointer<QObject> owner;
        std::function<void(const QJsonObject &)> handle;
    };
    QList<HelperListener> m_helperListeners;
    // The board writes the Test suites pane has in flight (#7BM4): request id -> what to do with
    // the answer. Two steps each, because a `## Tests` line needs the card's `base_hash`.
    struct TestsCardWrite {
        QPointer<ToolPane> pane;
        QString section;      // the `## Tests` line to append once a hash is in hand; empty = done
        QString note;         // what to say in the status bar when it lands
        bool reveal = false;  // open the card afterwards (a card this pane just created)
    };
    QHash<QString, TestsCardWrite> m_testsWrites;
    quint64 m_testsWriteSeq = 0;
    // The same two-step write for the Profile pane's "Attach to card…" (#7BM4 phase 5): the
    // `## Profile` block and the evidence directory need the card's `base_hash`, so the
    // `board_card_get` answer carries what to write.
    struct ProfileCardWrite {
        QString markdown;     // the `## Profile` block
        QString evidence;     // the directory for links.evidence
    };
    QHash<QString, ProfileCardWrite> m_profileWrites;
    quint64 m_profileWriteSeq = 0;
    // The helper worker's last `presets` answer per tab, and the defaults that came with it:
    // Options › Models reads these when no pane's agent is up (owner, 2026-09-20: "if there is
    // no agent loaded yet, load the helper agent").
    QHash<QString, QJsonArray> m_helperPresets;
    QHash<QString, QJsonObject> m_helperTierDefaults;

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
    // Sections: General, Appearance, Models, Local models, Claude Code and Codex, Terminal,
    // Agent, Security, Voice, Privacy, Keyboard.
    //
    // What belongs here (owner, 2026-09-18): what persists — a default, true in every pane after
    // a restart. A verb ("Reload themes", "Reset shortcut hints", "Open the log folder") is an
    // action and lives in rootItems(); a button row is for opening the editor of something that
    // persists (API keys, Model roles, Instructions, Skills, keybindings.json).
    //
    // Every row that stands for a value also says what that value is when Relay ships, in
    // relay::SettingRow::reset, and the end of settingsSections() turns those into one "Reset to
    // defaults" row per page. The helpers below set it, so a row built by one is covered the day it
    // is added; a row written out by hand sets its own, and a row that stands for a thing rather
    // than a value (buttonRow, an Info line, a saved local server) leaves it empty on purpose.
    //
    // A reset forgets the key rather than writing the fallback into it: that is what a fresh
    // install looks like, so every reader of the key — several read it directly, with fallbacks of
    // their own — ends up exactly where it starts on a new machine. What the row does besides
    // storing runs too, because the default has to be in effect and not merely stored.

    relay::SettingRow toggleRow(const QString &key, const QString &label, const QString &detail,
                                bool fallback, std::function<void(bool)> extra = {}) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Toggle;
        row.id = QStringLiteral("option:") + key;
        row.label = label;
        row.detail = detail;
        row.checked = QSettings().value(key, fallback).toBool();
        row.changed = row.checked != fallback;
        row.onToggle = [this, key, extra](bool on) {
            QSettings().setValue(key, on);
            if (extra) extra(on);
            if (m_active) m_active->agentOptionsChanged(key);
        };
        row.reset = [this, key, fallback, extra] {
            QSettings().remove(key);
            if (extra) extra(fallback);
            if (m_active) m_active->agentOptionsChanged(key);
        };
        return row;
    }

    // A row whose value the Switchboard workers carry too: they are configured once per board
    // root and never see `set_agent_options`, so the change has to be pushed to them here.
    void alsoBoardWorkers(relay::SettingRow &row) {
        const auto number = row.onNumber;
        const auto reset = row.reset;
        row.onNumber = [this, number](int value) {
            if (number) number(value);
            reconfigureBoardWorkers();
        };
        row.reset = [this, reset] {
            if (reset) reset();
            reconfigureBoardWorkers();
        };
    }

    relay::SettingRow numberRow(const QString &key, const QString &label, const QString &detail,
                                int fallback, int minimum, int maximum, const QString &suffix = QString()) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Number;
        row.id = QStringLiteral("option:") + key;
        row.label = label;
        row.detail = detail;
        row.number = QSettings().value(key, fallback).toInt();
        row.changed = row.number != fallback;
        row.minimum = minimum;
        row.maximum = maximum;
        row.suffix = suffix;
        row.onNumber = [this, key](int value) {
            QSettings().setValue(key, value);
            if (m_active) m_active->agentOptionsChanged(key);
        };
        row.reset = [this, key] {
            QSettings().remove(key);
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
        row.changed = !row.text.isEmpty();     // these ship empty; the placeholder says what empty means
        row.onText = [this, key, write](const QString &value) {
            if (write) write(value);
            else if (value.isEmpty()) QSettings().remove(key);
            else QSettings().setValue(key, value);
            if (m_active) m_active->agentOptionsChanged(key);
        };
        // An empty box is what these rows mean by "unset", so the writer is handed one: it is the
        // writer that knows about the sibling keys (skills/exclude beside skills/exclude_text).
        // The key itself goes afterwards, because a writer may have stored the empty string in it.
        row.reset = [this, key, write] {
            if (write) write(QString());
            QSettings().remove(key);
            if (m_active) m_active->agentOptionsChanged(key);
        };
        return row;
    }

    // A host list (#S5SH) stored as a QStringList and edited as one comma-separated line.
    relay::SettingRow hostListRow(const QString &key, const QString &label, const QString &detail) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Text;
        row.id = QStringLiteral("option:") + key;
        row.label = label;
        row.detail = detail;
        row.aliases = QStringLiteral("ssh hosts");
        row.placeholder = QStringLiteral("filly, backup.example.org");
        row.text = QSettings().value(key).toStringList().join(QStringLiteral(", "));
        row.changed = !row.text.isEmpty();     // ships empty: no host is on either list
        row.onText = [key](const QString &value) {
            QStringList hosts;
            for (const QString &word : value.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts))
                if (!hosts.contains(word)) hosts << word;
            if (hosts.isEmpty()) QSettings().remove(key);
            else QSettings().setValue(key, hosts);
        };
        row.reset = [key] { QSettings().remove(key); };   // ships empty: no host is on either list
        return row;
    }

    // A list stored as a QStringList and edited as one line (card #3KB7, the Security section).
    // `separator` is a regular expression, and the caller's detail line says which it is: a command
    // rule may contain spaces ("git push --force*") so its list splits on commas only, while a
    // secret pattern may contain a comma ("x{1,3}") so its list splits on whitespace. There is no
    // multi-line row kind to sidestep the question with.
    relay::SettingRow listRow(const QString &key, const QString &label, const QString &detail,
                              const QString &placeholder, const QString &separator,
                              const QString &aliases = QString()) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Text;
        row.id = QStringLiteral("option:") + key;
        row.label = label;
        row.detail = detail;
        row.aliases = aliases;
        row.placeholder = placeholder;
        row.text = QSettings().value(key).toStringList().join(QStringLiteral(", "));
        row.onText = [this, key, separator](const QString &value) {
            QStringList items;
            for (const QString &word : value.split(QRegularExpression(separator), Qt::SkipEmptyParts)) {
                const QString item = word.trimmed();
                if (!item.isEmpty() && !items.contains(item)) items << item;
            }
            if (items.isEmpty()) QSettings().remove(key);
            else QSettings().setValue(key, items);
            if (m_active) m_active->agentOptionsChanged(key);
        };
        row.reset = [this, key] {
            QSettings().remove(key);          // every one of these ships empty
            if (m_active) m_active->agentOptionsChanged(key);
        };
        return row;
    }

    // `approvals.CAUTIOUS` (backend/relay_core/approvals.py): what asks before the first-launch
    // choice is answered. One list with the ask's "Always allow" and the first-launch pane's
    // buttons (relay::approvals::cautious, src/ApprovalsPane.h), so the rows, an ask and the pane
    // can never disagree about what an unanswered Relay asks.
    static QStringList approvalsCautious() { return relay::approvals::cautious(); }

    // One row of the approvals checklist (card #K2FV): seven capabilities, one saved list
    // (security/approvals_ask). The row stands for membership of that list, and an unticked Relay
    // — the first-launch choice not yet answered — displays the cautious set rather than an empty
    // list, because that is what is in force. Touching any row is itself the choice: the saved
    // list only means something once it replaces the default, so the first toggle marks
    // security/approvals_chosen and the rows become the saved list they were displaying.
    relay::SettingRow approvalRow(const QString &capability, const QString &label, const QString &detail) {
        const bool cautious = approvalsCautious().contains(capability);
        QSettings settings;
        const bool asksNow = settings.value(QStringLiteral("security/approvals_chosen"), false).toBool()
            ? settings.value(QStringLiteral("security/approvals_ask")).toStringList().contains(capability)
            : cautious;
        relay::SettingRow row;
        row.kind = relay::SettingRow::Toggle;
        row.id = QStringLiteral("option:approvals/") + capability;
        row.label = label;
        row.detail = detail;
        row.checked = asksNow;
        row.changed = asksNow != cautious;
        row.onToggle = [this, capability](bool on) {
            QSettings settings;
            QStringList ask = settings.value(QStringLiteral("security/approvals_ask")).toStringList();
            if (!settings.value(QStringLiteral("security/approvals_chosen"), false).toBool()) {
                ask = approvalsCautious();   // start from the set the rows were displaying
                settings.setValue(QStringLiteral("security/approvals_chosen"), true);
            }
            if (on) { if (!ask.contains(capability)) ask << capability; }
            else ask.removeAll(capability);
            settings.setValue(QStringLiteral("security/approvals_ask"), ask);
            if (m_active) m_active->agentOptionsChanged(QStringLiteral("security/approvals_ask"));
        };
        // Resetting any row forgets both keys — the shipped state is "no choice made, cautious
        // set in force" — which is what the one checklist ships as. Idempotent, so the section's
        // Reset to defaults running all seven leaves exactly that.
        row.reset = [this] {
            QSettings settings;
            settings.remove(QStringLiteral("security/approvals_ask"));
            settings.remove(QStringLiteral("security/approvals_chosen"));
            if (m_active) m_active->agentOptionsChanged(QStringLiteral("security/approvals_ask"));
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

    // `current` is what the value is now, `fallback` what it is when Relay ships — and the row is
    // put back to it by choosing it, because a choice row's writer is the thing that applies it
    // (setActiveTheme, log::setLevel, Keymap::setPreset) and not merely a QSettings line. The
    // parameter has no default on purpose: a choice row added later cannot forget to say. Pass an
    // empty string for a row whose default is derived rather than fixed (the voice key, which
    // follows the keyboard layout) and give it a `reset` of its own.
    relay::SettingRow choiceRow(const QString &id, const QString &label, const QString &detail,
                                const QStringList &values, const QStringList &labels,
                                const QString &current, const QString &fallback,
                                std::function<void(const QString &)> choose) {
        relay::SettingRow row;
        row.kind = relay::SettingRow::Choice;
        row.id = id;
        row.label = label;
        row.detail = detail;
        row.options = values;
        row.optionLabels = labels;
        row.current = current;
        row.onChoose = std::move(choose);
        if (!fallback.isEmpty()) {
            row.reset = [fn = row.onChoose, fallback] { if (fn) fn(fallback); };
            row.changed = current != fallback;
        }
        return row;
    }

    // Options › Models (owner, 2026-09-20): one page for everything about models, top to bottom
    // the way it is set up — the providers and their keys, then which of each provider's models
    // the picker shows, then their order, then the defaults. It replaced the API keys and Model
    // roles doors and the separate "Claude Code and Codex" page: those two are providers here like
    // any other, and a guest's model, reasoning level and permission posture sit under its rows.
    // The rows come from relay::models (the catalog every pane's box and the picker read), so a
    // check here is a row there the moment it lands.
    relay::SettingsSection modelsSection() {
        relay::SettingsSection models;
        models.id = QStringLiteral("models");
        models.title = QStringLiteral("Models");
        models.blurb = QStringLiteral("Relay runs on the models you already pay for: an API key, a coding-plan subscription "
                                      "(GLM, Kimi, MiniMax), or your Claude Code and Codex logins. Keys live in the "
                                      "desktop keyring and requests go straight to the provider, never through Relay's "
                                      "server; Relay Free, the included allowance, is the one exception and goes through "
                                      "Relay's hosted service. A profile names the five lists as a set, so \"AI work\" "
                                      "and \"admin work\" can rank models differently and swap in one switch (/profile). "
                                      "Ctrl+Shift+M or /models opens this page; Alt+M drops the "
                                      "model box open; Ctrl+Alt+M or /model opens the picker.");
        Pane *pane = m_active;
        QSettings settings;
        QWidget *page = m_tabs->currentWidget();
        // The pane's worker answers when a pane's agent is up. Otherwise the tab's helper agent
        // does (owner, 2026-09-20: "if there is no agent loaded yet, load the helper agent"): it
        // is started on the first look at this page, asked for `presets`, and every request the
        // page makes goes down its pipe instead.
        const bool viaHelper = !pane || pane->allPresets().isEmpty();
        QJsonArray presets = viaHelper ? m_helperPresets.value(tabIdOf(page)) : pane->allPresets();
        if (viaHelper && presets.isEmpty())
            if (relay::BoardWorker *worker = helperWorker(page, true)) worker->send({{"type", "presets"}});
        auto request = [this, pane, viaHelper, page](const QJsonObject &message) {
            if (!viaHelper && pane) { pane->sendModelRequest(message); return; }
            if (relay::BoardWorker *worker = helperWorker(page, true)) worker->send(message);
        };
        // The pane's catalog, not a bare catalogFrom: it carries the usage limits that arrived
        // since the last presets answer (the "5h 62% left" status line, an exhausted row).
        const relay::models::Catalog catalog = !viaHelper ? pane->modelCatalog() : relay::models::catalogFrom(presets);
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        auto curated = [this] { modelsCurated(); };
        auto str = [](const QJsonObject &object, const char *field) { return object.value(QLatin1String(field)).toString(); };

        // ----- 0. the way to the dialog -----------------------------------------------------------
        // The five lists are section 4 of this page, under the providers and the checklist — owner,
        // 2026-09-21: "the model priority chooser is crtical, and currently its too hard to find --
        // model options, then scroll down." Ctrl+Alt+M is now that chooser (card #MDL1 t:a7, design
        // 5.2) and this is the door to it from here; the rows below still edit the same storage, so
        // whichever you use the other agrees.
        {
            const QString chord = Keymap::instance().shortcutText(QStringLiteral("agent.model"));
            relay::SettingRow row = buttonRow(QStringLiteral("models.prioritize"),
                QStringLiteral("prioritize models"),
                QStringLiteral("The dialog these five lists live in: a tab per list, enter to use a model in this pane, "
                               "alt+↑↓ or a drag to reorder, delete to take one out, and typing to find any model and "
                               "add it. The same lists this page shows"),
                chord.isEmpty() ? QStringLiteral("prioritize models…") : QStringLiteral("prioritize models… (%1)").arg(chord),
                [this] { runAction(QStringLiteral("agent.model")); });
            row.aliases = QStringLiteral("prioritize priority order rank models picker dialog tier lists ctrl+alt+m");
            models.rows << row;
        }

        // ----- 1. providers ---------------------------------------------------------------------
        // Owner (2026-09-20): only the providers you can use are listed — a key stored, Relay Free,
        // Claude Code and Codex on this machine — plus OpenRouter always, because it is the one key
        // the fallbacks and the Lite tier lean on. The rest wait behind "+ add provider". Listed
        // providers keep the order they were added in, and drag to reorder.
        {
            // Folds by default once a provider is set up (owner, 2026-09-20): from then on the
            // page opens on the models, and the keys are one click away.
            relay::SettingRow head = headingRow(QStringLiteral("providers"));
            head.collapsible = true;
            bool anySetUp = false;
            for (const auto &value : presets) {
                const QJsonObject preset = value.toObject();
                anySetUp = anySetUp || preset.value(QStringLiteral("has_stored_key")).toBool() || preset.value(QStringLiteral("custom")).toBool()
                    || (preset.value(QStringLiteral("harness")).toBool() && preset.value(QStringLiteral("logged_in")).toBool());
            }
            head.collapsedByDefault = anySetUp;
            models.rows << head;
        }
        if (presets.isEmpty()) {
            relay::SettingRow none;
            none.kind = relay::SettingRow::Info;
            none.id = QStringLiteral("info:models/none");
            none.label = QStringLiteral("Starting the helper agent to read your providers…");
            models.rows << none;
        }
        // The add-key flow, shared by the listed rows and "+ add provider".
        auto askForKey = [this, request](const QString &id, const QString &label) {
            bool ok = false;
            // Password echo: the key is never rendered and never leaves this call.
            const QString key = QInputDialog::getText(this, QStringLiteral("API key"),
                QStringLiteral("Key for %1.\nIt is saved to the desktop keyring and sent only to this provider.").arg(label),
                QLineEdit::Password, QString(), &ok).trimmed();
            if (!ok || key.isEmpty()) return;
            if (key.contains(QRegularExpression(QStringLiteral("\\s")))) {
                QMessageBox::warning(this, QStringLiteral("API key"), QStringLiteral("An API key cannot contain spaces."));
                return;
            }
            request({{"type", "store_key"}, {"preset", id}, {"api_key", key}});
        };
        // The custom-endpoint form (owner, 2026-09-20: "like in warp custom providers"): a name,
        // an OpenAI-compatible base URL, a key, the model ids. Saved through the pane's worker.
        auto askForCustom = [this, request](const QJsonObject &existing) {
            QDialog dialog(this);
            dialog.setWindowTitle(existing.isEmpty() ? QStringLiteral("custom provider") : QStringLiteral("edit custom provider"));
            auto *form = new QFormLayout(&dialog);
            auto *name = new QLineEdit(existing.value(QStringLiteral("name")).toString());
            name->setPlaceholderText(QStringLiteral("my proxy"));
            auto *url = new QLineEdit(existing.value(QStringLiteral("base_url")).toString());
            url->setPlaceholderText(QStringLiteral("https://host/v1  (OpenAI-compatible; /chat/completions is appended)"));
            auto *key = new QLineEdit;
            key->setEchoMode(QLineEdit::Password);
            key->setPlaceholderText(existing.isEmpty() ? QStringLiteral("API key (optional for a loopback URL)")
                                                       : QStringLiteral("leave empty to keep the stored key"));
            QStringList ids;
            for (const auto &item : existing.value(QStringLiteral("model_ids")).toArray()) ids << item.toString();
            auto *models = new QLineEdit(ids.join(QStringLiteral(", ")));
            models->setPlaceholderText(QStringLiteral("model ids, comma-separated; the first is the default"));
            auto *effort = new QComboBox;
            effort->addItem(QStringLiteral("none · send no reasoning setting"), QStringLiteral("none"));
            effort->addItem(QStringLiteral("openrouter · reasoning.effort"), QStringLiteral("openrouter"));
            effort->addItem(QStringLiteral("kimi · reasoning_effort"), QStringLiteral("kimi"));
            effort->setCurrentIndex(qMax(0, effort->findData(existing.value(QStringLiteral("effort_style")).toString(QStringLiteral("none")))));
            form->addRow(QStringLiteral("name"), name);
            form->addRow(QStringLiteral("base url"), url);
            form->addRow(QStringLiteral("api key"), key);
            form->addRow(QStringLiteral("models"), models);
            form->addRow(QStringLiteral("reasoning"), effort);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            dialog.resize(560, dialog.sizeHint().height());
            if (dialog.exec() != QDialog::Accepted) return;
            QJsonObject provider{{QStringLiteral("name"), name->text().trimmed()},
                                 {QStringLiteral("base_url"), url->text().trimmed()},
                                 {QStringLiteral("models"), models->text().trimmed()},
                                 {QStringLiteral("effort_style"), effort->currentData().toString()}};
            if (!existing.isEmpty()) provider.insert(QStringLiteral("id"), existing.value(QStringLiteral("id")).toString());
            if (!key->text().trimmed().isEmpty()) provider.insert(QStringLiteral("api_key"), key->text().trimmed());
            request({{"type", "custom_provider_save"}, {"provider", provider}});
        };
        QList<QJsonObject> listed, waiting;
        for (const auto &value : presets) {
            const QJsonObject preset = value.toObject();
            const QString id = str(preset, "id");
            if (id.isEmpty() || preset.value(QStringLiteral("local")).toBool()) continue;   // Options › Local models
            // Relay Free is not a provider you set up (owner, 2026-09-20: "don't show relay free in
            // the providers list"): it has no key, no login and nothing to test into. Its models
            // still sit in the checklist and the priority list like any other.
            if (preset.value(QStringLiteral("hosted")).toBool()) continue;
            const bool guest = id.startsWith(QStringLiteral("guest:"));
            const bool usable = preset.value(QStringLiteral("has_stored_key")).toBool()
                || preset.value(QStringLiteral("custom")).toBool()
                || (preset.value(QStringLiteral("hosted")).toBool() && preset.value(QStringLiteral("available")).toBool())
                || (guest && preset.value(QStringLiteral("installed")).toBool(true));
            (usable || id == QStringLiteral("openrouter") ? listed : waiting) << preset;
        }
        // Order of addition, then whatever you dragged (owner, 2026-09-20): the first time a
        // provider is listed is its place, and a drop moves it.
        {
            QStringList ids;
            for (const QJsonObject &preset : std::as_const(listed)) ids << str(preset, "id");
            relay::models::curation::noteProviders(ids);
            const QStringList order = relay::models::curation::providerOrder();
            std::stable_sort(listed.begin(), listed.end(), [&](const QJsonObject &a, const QJsonObject &b) {
                return order.indexOf(str(a, "id")) < order.indexOf(str(b, "id"));
            });
        }
        for (const QJsonObject &preset : std::as_const(listed)) {
            const QString id = str(preset, "id");
            const QString label = str(preset, "label").toLower();
            const bool hosted = preset.value(QStringLiteral("hosted")).toBool();
            const bool guest = id.startsWith(QStringLiteral("guest:"));
            const QString source = str(preset, "key_source");
            const bool hasKey = preset.value(QStringLiteral("has_stored_key")).toBool();
            const QString limits = relay::models::limitsText(catalog.limits.value(id), now);
            QString status;
            if (hosted) {
                status = preset.value(QStringLiteral("available")).toBool()
                    ? QStringLiteral("included, no key needed") : QStringLiteral("needs python3-cryptography");
            } else if (guest) {
                const QJsonValue loggedIn = preset.value(QStringLiteral("logged_in"));
                status = loggedIn.isBool() ? (loggedIn.toBool() ? QStringLiteral("logged in on this machine")
                                                                : QStringLiteral("not logged in: change login runs the CLI's own sign-in"))
                                           : QStringLiteral("on this machine, runs with your own login");
            } else if (source == QStringLiteral("env")) {
                status = QStringLiteral("key from RELAY_%1_API_KEY").arg(id.toUpper().replace(QLatin1Char('-'), QLatin1Char('_')));
            } else if (hasKey) {
                status = QStringLiteral("key stored in the keyring");
            } else {
                status = QStringLiteral("no key yet · get one at %1").arg(str(preset, "key_url"));
            }
            if (!limits.isEmpty()) status += QStringLiteral(" · ") + limits;
            if (!str(preset, "note").isEmpty()) status += QStringLiteral(" · ") + str(preset, "note").toLower();
            relay::SettingRow row;
            row.kind = relay::SettingRow::Buttons;
            row.id = QStringLiteral("provider:") + id;
            row.label = label;
            row.detail = status;
            row.aliases = QStringLiteral("provider key api keyring login ") + id + QLatin1Char(' ') + str(preset, "provider").toLower();
            row.infoUrl = guest ? (id == QStringLiteral("guest:claude") ? QStringLiteral("https://docs.claude.com/en/docs/claude-code")
                                                                         : QStringLiteral("https://developers.openai.com/codex"))
                        : preset.value(QStringLiteral("custom")).toBool() ? str(preset, "base_url")
                                                                          : str(preset, "key_url");
            row.dragGroup = QStringLiteral("providers");
            row.onDropBefore = [id, curated](const QString &draggedRowId) {
                relay::models::curation::moveProviderBefore(draggedRowId.section(QLatin1Char(':'), 1), id);
                curated();
            };
            if (hosted) {
                if (!preset.value(QStringLiteral("available")).toBool()) { row.kind = relay::SettingRow::Info; row.label = label + QStringLiteral(" · ") + status; }
                else {
                    row.buttonTexts = QStringList{QStringLiteral("test")};   // one real call
                    row.onButton = [request, id](int) { request({{"type", "test_key"}, {"preset", id}}); };
                }
            } else if (guest) {
                // The same two buttons as a keyed provider, in the guest's words: its login is its
                // key. The CLI's own sign-in runs in the pane's terminal; test runs one turn.
                const QString cli = id.mid(6);
                const QString login = cli == QStringLiteral("claude") ? QStringLiteral("claude auth login") : cli + QStringLiteral(" login");
                row.buttonTexts = QStringList{QStringLiteral("change login"), QStringLiteral("test")};
                row.onButton = [this, request, id, login](int index) {
                    if (index == 0) {
                        // The CLI's sign-in needs a terminal to run in: a pane, which the helper is not.
                        if (m_active) m_active->runLoginCommand(login);
                        else QMessageBox::information(this, QStringLiteral("Models"), QStringLiteral("Open a terminal pane first: `%1` runs there.").arg(login));
                    } else request({{"type", "test_key"}, {"preset", id}});
                };
            } else if (preset.value(QStringLiteral("custom")).toBool()) {
                // A custom endpoint (§28.6): edit reopens the form, delete removes it and its key.
                row.buttonTexts = QStringList{QStringLiteral("edit…"), QStringLiteral("test"), QStringLiteral("delete")};
                row.onButton = [this, request, id, label, preset, askForCustom](int index) {
                    if (index == 0) askForCustom(preset);
                    else if (index == 1) request({{"type", "test_key"}, {"preset", id}});
                    else if (QMessageBox::question(this, QStringLiteral("Delete provider"),
                                 QStringLiteral("Delete %1 and its stored key?").arg(label)) == QMessageBox::Yes)
                        request({{"type", "custom_provider_delete"}, {"provider_id", id}});
                };
            } else {
                row.buttonTexts = QStringList{hasKey ? QStringLiteral("replace key…") : QStringLiteral("add key…"), QStringLiteral("test")};
                if (source == QStringLiteral("keyring")) row.buttonTexts << QStringLiteral("remove");
                row.onButton = [this, request, id, label, askForKey](int index) {
                    if (index == 0) askForKey(id, label);
                    else if (index == 1) request({{"type", "test_key"}, {"preset", id}});
                    else if (QMessageBox::question(this, QStringLiteral("Remove key"),
                                 QStringLiteral("Remove the stored key for %1 from the keyring?").arg(label)) == QMessageBox::Yes)
                        request({{"type", "remove_key"}, {"preset", id}});
                };
            }
            models.rows << row;
        }
        {
            // The providers without a key, one pick away — and "custom endpoint…" first, always
            // (owner, 2026-09-20): a name, a base URL, a key and model ids, like Warp's custom
            // providers. A built-in pick goes straight to the key box.
            QStringList names; for (const QJsonObject &preset : std::as_const(waiting)) names << str(preset, "provider").toLower();
            names.removeDuplicates();
            models.rows << buttonRow(QStringLiteral("models.addProvider"), QStringLiteral("+ add provider"),
                names.isEmpty() ? QStringLiteral("a custom endpoint") : QStringLiteral("a custom endpoint, or %1").arg(names.join(QStringLiteral(", "))),
                QStringLiteral("add…"), [this, waiting, askForKey, askForCustom, str] {
                    QList<relay::agentui::PickerRow> rows;
                    rows << relay::agentui::PickerRow{{QStringLiteral("custom endpoint…"), QStringLiteral("any OpenAI-compatible url"), QString()},
                                                      QStringLiteral("A name, a base URL, a key and model ids"), QStringLiteral("custom")};
                    for (const QJsonObject &preset : waiting)
                        rows << relay::agentui::PickerRow{{str(preset, "label").toLower(), str(preset, "plan").toLower(), str(preset, "key_url")},
                                                          str(preset, "note"), str(preset, "id")};
                    const auto result = relay::agentui::pick(this, QStringLiteral("add provider"),
                        QStringLiteral("Pick a provider; the next step asks for its key, or for the endpoint."),
                        {QStringLiteral("provider"), QStringLiteral("plan"), QStringLiteral("key page")}, rows,
                        {{QStringLiteral("add"), QStringLiteral("add…"), true}});
                    if (result.row < 0) return;
                    if (result.row == 0) { askForCustom(QJsonObject()); return; }
                    const QJsonObject preset = waiting.at(result.row - 1);
                    askForKey(str(preset, "id"), str(preset, "label").toLower());
                });
        }

        // ----- 2. which models the picker shows -------------------------------------------------
        // One checkbox per provider (owner, 2026-09-20): off hides every model and folds the group.
        // No per-model "openrouter fallback" here any more (owner, later that day): an OpenRouter
        // model is a model like any other — add it to the picker and rank it above the line.
        // A provider whose catalog is open-ended (more than six models — OpenRouter's live list)
        // shows only the models you checked plus an id box that completes from the whole list;
        // one whose list is complete (Claude Code, Codex, a plan's three tiers) shows them all and
        // needs no box.
        // No paragraph under the heading and no count under a provider (owner, 2026-09-20): the
        // heading says what the checkboxes do, and the count is the provider row's hover.
        { relay::SettingRow head = headingRow(QStringLiteral("models in the picker")); head.collapsible = true; models.rows << head; }
        constexpr int kOpenEnded = 6;   // more catalog models than this: an id box, only the checked shown
        for (const QString &presetId : catalog.presets()) {
            const QList<relay::models::Entry> rows = catalog.ofPreset(presetId);
            if (rows.isEmpty() || !rows.first().usable || rows.first().local) continue;
            const bool collapsed = relay::models::curation::isCollapsed(presetId);
            int shownCount = 0;
            for (const relay::models::Entry &entry : rows) if (relay::models::curation::isShown(entry)) ++shownCount;
            relay::SettingRow head;
            head.kind = relay::SettingRow::Toggle;
            head.id = QStringLiteral("option:models/provider/") + presetId;
            head.label = catalog.presetLabels.value(presetId, presetId);
            head.tooltip = collapsed ? QStringLiteral("Hidden from the picker · check to show its models")
                                     : QStringLiteral("%1 of %2 models in the picker · uncheck to hide them all").arg(shownCount).arg(rows.size());
            head.aliases = QStringLiteral("provider models picker show hide ") + rows.first().provider;
            head.strong = true;
            head.checked = !collapsed && shownCount > 0;
            head.onToggle = [catalog, presetId, rows, curated](bool on) {
                relay::models::curation::setCollapsed(presetId, !on);
                for (const relay::models::Entry &entry : rows) relay::models::curation::setShown(entry.key, on, catalog);
                curated();
            };
            models.rows << head;
            if (collapsed) continue;
            const bool openEnded = rows.size() > kOpenEnded;
            QStringList unlisted;   // the ids the box completes from
            for (const relay::models::Entry &entry : rows) {
                if (openEnded && !relay::models::curation::isShown(entry) && !entry.custom) { unlisted << entry.model; continue; }
                relay::SettingRow row;
                row.kind = relay::SettingRow::Toggle;
                row.id = QStringLiteral("option:models/shown/") + entry.key;
                row.indent = 1;
                row.label = entry.label + (entry.custom ? QStringLiteral(" (added by you)") : QString());
                QStringList notes;
                if (!entry.tier.isEmpty()) notes << QStringLiteral("%1 model").arg(entry.tier);
                notes << (entry.efforts.isEmpty() ? QStringLiteral("no reasoning setting")
                                                  : QStringLiteral("reasoning ") + entry.efforts.join(QStringLiteral(" · ")));
                if (entry.intelligence >= 0) notes << QStringLiteral("intelligence %1").arg(entry.intelligence);
                if (entry.label != entry.model.toLower()) notes << entry.model;
                // One line per model (owner, 2026-09-20): the levels, the score and the id are the
                // hover, and ⓘ opens the model's OpenRouter page — one page shape for every model,
                // with context, pricing and the providers behind it — when OpenRouter serves it.
                row.tooltip = notes.join(QStringLiteral(" · "));
                if (entry.preset == QStringLiteral("openrouter")) row.infoUrl = QStringLiteral("https://openrouter.ai/") + entry.model;
                else if (!entry.openrouter.isEmpty()) row.infoUrl = QStringLiteral("https://openrouter.ai/") + entry.openrouter;
                row.aliases = QStringLiteral("model picker show hide ") + entry.model + QLatin1Char(' ') + entry.provider + QLatin1Char(' ') + row.tooltip;
                row.checked = relay::models::curation::isShown(entry);
                row.onToggle = [catalog, key = entry.key, curated](bool on) {
                    relay::models::curation::setShown(key, on, catalog);
                    curated();
                };
                models.rows << row;
                if (entry.custom) {
                    relay::SettingRow remove;
                    remove.kind = relay::SettingRow::Buttons;
                    remove.id = QStringLiteral("models/custom/") + entry.key;
                    remove.indent = 1;
                    remove.label = QStringLiteral("remove %1").arg(entry.label);
                    remove.detail = QStringLiteral("Forget this id; the provider's own list is unaffected");
                    remove.buttonTexts = QStringList{QStringLiteral("remove")};
                    remove.onButton = [key = entry.key, curated](int) { relay::models::curation::removeCustom(key); curated(); };
                    models.rows << remove;
                }
            }
            if (openEnded) {
                // opencode's box: type part of an id and pick from what the provider serves; an id
                // it does not list is added as typed.
                relay::SettingRow add = textRow(QStringLiteral("models/add/") + presetId, QStringLiteral("add a model"),
                    QStringLiteral("%1 more on %2 · type to search their ids; an unlisted id is added as typed")
                        .arg(unlisted.size()).arg(rows.first().provider),
                    QStringLiteral("model id"), [catalog, presetId, curated](const QString &value) {
                        const QString id = value.trimmed();
                        if (id.isEmpty()) return;
                        const QString key = relay::models::Catalog::keyFor(presetId, id);
                        if (catalog.find(key)) relay::models::curation::setShown(key, true, catalog);
                        else relay::models::curation::addCustom(presetId, id, catalog);
                        QSettings().remove(QStringLiteral("models/add/") + presetId);   // the box empties: the row is above now
                        curated();
                    });
                add.completions = unlisted;
                add.indent = 1;
                add.text.clear(); add.changed = false;
                models.rows << add;
            }
            if (const QString guest = presetId.startsWith(QStringLiteral("guest:")) ? presetId.mid(6) : QString(); !guest.isEmpty()) {
                // What the guest does when it wants to run a command or change a file. Relay's own
                // agent has no per-action approvals and neither does a guest by default (the
                // owner's rule, 29.1) — but a pane watching a guest work in somebody else's checkout
                // is a fair reason to want the question, so it is offered rather than assumed.
                const QString key = guestSettingKey(guest, QStringLiteral("permissions"));
                const QString current = QSettings().value(key).toString().trimmed();
                relay::SettingRow ask = choiceRow(QStringLiteral("option:") + key,
                    QStringLiteral("when it wants to use a tool"),
                    QStringLiteral("%1 runs with no per-action approvals, like Relay's own agent. "
                                   "Ask me puts each one to you: Allow, Allow for session, "
                                   "Deny, or Deny and stop the turn").arg(rows.first().provider),
                    {QStringLiteral("bypass"), QStringLiteral("ask"), QStringLiteral("deny")},
                    {QStringLiteral("just run it"), QStringLiteral("ask me"), QStringLiteral("refuse it")},
                    current.isEmpty() ? QStringLiteral("bypass") : current, QStringLiteral("bypass"),
                    [this, key, guest](const QString &value) {
                        if (value.isEmpty() || value == QStringLiteral("bypass")) QSettings().remove(key);
                        else QSettings().setValue(key, value);
                        for (Pane *each : allPanes()) each->guestOptionsChanged(guest);
                        refreshSettingsPanes();
                    });
                ask.aliases = QStringLiteral("claude codex guest permissions approval ask bypass yolo tools sandbox");
                ask.indent = 1;
                // One line like the models above it (owner, 2026-09-20): the explanation is the hover.
                ask.tooltip = ask.detail;
                ask.detail.clear();
                models.rows << ask;
            }
        }

        // ----- 3. profiles ----------------------------------------------------------------------
        // Owner (2026-09-20 evening): "we need model user profiles like warp for the priority lists
        // … so I can have an 'AI work' profile and an 'admin work' profile that sets different model
        // priorities." Warp's Agent Profile is a whole posture — base model, planning model,
        // autonomy, command allow/deny lists, MCP access — switched from an icon in its input area.
        // Here a profile is the five lists below and nothing else, which is what was asked for, and
        // the lists and the profile are one thing: an edit to a list while a profile is current is
        // an edit *of* it, so there is no "unsaved changes" state to explain or lose.
        {
            relay::SettingRow head = headingRow(QStringLiteral("profiles"));
            head.collapsible = true;
            models.rows << head;
        }
        {
            const QStringList names = relay::models::curation::profiles();
            const QString currentProfile = relay::models::curation::currentProfile();
            // Saving the lists as they are under a name, which is also how the first one is made.
            auto saveAs = [this, curated](const QString &initial, const QString &title) {
                bool ok = false;
                const QString name = QInputDialog::getText(this, title,
                    QStringLiteral("A name for the five lists as they are now — “AI work”, “admin work”.\n"
                                   "Editing a list while this profile is chosen edits the profile: there is nothing to save."),
                    QLineEdit::Normal, initial, &ok).trimmed();
                if (!ok || name == initial) return QString();
                if (!relay::models::curation::validProfileName(name)) {
                    if (!name.isEmpty())
                        QMessageBox::warning(this, title, QStringLiteral("A profile name cannot contain “/” or “\\”."));
                    return QString();
                }
                if (relay::models::curation::profiles().contains(name)) {
                    QMessageBox::warning(this, title, QStringLiteral("There is already a profile called “%1”.").arg(name));
                    return QString();
                }
                return name;
            };
            // ----- a profile on disk (owner, 2026-09-21: "allow exporting and importing profiles")
            // JSON, so a profile can be mailed, committed to a dotfiles repo or carried to another
            // machine. The file is the export of one profile or of all of them; the reader takes
            // either, so "export all" and "export this one" import the same way.
            const QString kProfileFilter = QStringLiteral("Relay model profiles (*.json);;All files (*)");
            auto profileDir = [] {
                const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
                return docs.isEmpty() ? QDir::homePath() : docs;
            };
            auto exportProfiles = [this, kProfileFilter, profileDir](const QStringList &names, const QString &suggestion) {
                const QJsonObject document = relay::models::curation::exportProfiles(names);
                const QString title = QStringLiteral("export model profiles");
                if (document.isEmpty()) {
                    QMessageBox::warning(this, title, QStringLiteral("There is nothing to export yet."));
                    return;
                }
                // A profile name is free text; a file name is not. Anything awkward becomes "-".
                QString stem = suggestion;
                stem.replace(QRegularExpression(QStringLiteral("[^\\w .()-]"), QRegularExpression::UseUnicodePropertiesOption),
                             QStringLiteral("-"));
                QString path = QFileDialog::getSaveFileName(this, title,
                                                            QDir(profileDir()).filePath(stem + QStringLiteral(".json")),
                                                            kProfileFilter);
                if (path.isEmpty()) return;
                if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) path += QStringLiteral(".json");
                QFile file(path);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    QMessageBox::warning(this, title, QStringLiteral("Relay could not write %1:\n%2").arg(path, file.errorString()));
                    return;
                }
                file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
                file.close();
                if (file.error() != QFile::NoError) {
                    QMessageBox::warning(this, title, QStringLiteral("Relay could not write %1:\n%2").arg(path, file.errorString()));
                    return;
                }
                statusBar()->showMessage(names.size() == 1
                                             ? QStringLiteral("Exported “%1” to %2").arg(names.first(), path)
                                             : QStringLiteral("Exported %1 profiles to %2").arg(names.size()).arg(path), 6000);
            };
            auto importProfiles = [this, catalog, curated, kProfileFilter, profileDir] {
                const QString title = QStringLiteral("import model profiles");
                const QString path = QFileDialog::getOpenFileName(this, title, profileDir(), kProfileFilter);
                if (path.isEmpty()) return;
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    QMessageBox::warning(this, title, QStringLiteral("Relay could not read %1:\n%2").arg(path, file.errorString()));
                    return;
                }
                QJsonParseError parse{};
                const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
                QString error = parse.error == QJsonParseError::NoError ? QString() : parse.errorString();
                const QList<relay::models::curation::ProfileDoc> incoming =
                    error.isEmpty() ? relay::models::curation::readProfiles(document.object(), &error)
                                    : QList<relay::models::curation::ProfileDoc>();
                if (incoming.isEmpty()) {
                    QMessageBox::warning(this, title, QStringLiteral("%1\n\n%2").arg(path, error));
                    return;
                }
                // A name this machine already uses is the one thing that needs an answer: replacing
                // somebody's "AI work" silently is exactly the accident an import should not have.
                auto freeName = [](const QString &base) {
                    const QStringList taken = relay::models::curation::profiles();
                    if (!taken.contains(base)) return base;
                    for (int n = 2; n < 1000; ++n) {
                        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
                        if (!taken.contains(candidate)) return candidate;
                    }
                    return base;
                };
                const QString wasCurrent = relay::models::curation::currentProfile();
                QStringList added, skipped;
                for (relay::models::curation::ProfileDoc profile : incoming) {
                    if (relay::models::curation::profiles().contains(profile.name)) {
                        QMessageBox box(QMessageBox::Question, title,
                                        QStringLiteral("This machine already has a profile called “%1”.")
                                            .arg(profile.name),
                                        QMessageBox::NoButton, this);
                        box.setInformativeText(profile.name == wasCurrent
                                                   ? QStringLiteral("Replacing it also moves the five lists onto the imported "
                                                                    "ones — they are the lists this profile names.")
                                                   : QStringLiteral("Replacing it overwrites its five lists."));
                        // All three are ActionRole bar the last, so they keep this order in every
                        // button layout: the destructive one first, the safe one default.
                        box.addButton(QStringLiteral("replace"), QMessageBox::ActionRole);
                        QPushButton *both = box.addButton(QStringLiteral("keep both"), QMessageBox::ActionRole);
                        QPushButton *skip = box.addButton(QStringLiteral("skip"), QMessageBox::RejectRole);
                        box.setDefaultButton(both);
                        box.exec();
                        if (box.clickedButton() == skip) { skipped << profile.name; continue; }
                        if (box.clickedButton() == both) profile.name = freeName(profile.name);
                    }
                    relay::models::curation::writeProfile(profile);
                    added << profile.name;
                }
                if (added.isEmpty()) return;
                // An import that replaced the *current* profile moved the live lists with it, and
                // `curated()` carries that to every pane and its worker either way (card #MDL1:
                // there is no second copy of the default left to write when it does).
                curated();
                QMessageBox::information(this, title,
                                         QStringLiteral("Imported %1.%2\nChoose one in the profile box to switch the five "
                                                        "lists onto it.")
                                             .arg(QStringLiteral("“") + added.join(QStringLiteral("”, “")) + QStringLiteral("”"),
                                                  skipped.isEmpty() ? QString()
                                                                    : QStringLiteral(" Skipped %1.").arg(skipped.size())));
            };
            const QString kNew = QStringLiteral("\x01new");   // never a profile name: validProfileName trims
            relay::SettingRow row;
            row.kind = relay::SettingRow::Choice;
            row.id = QStringLiteral("models/profile");
            row.label = QStringLiteral("profile");
            row.detail = QStringLiteral("A named set of the five lists below. Switching one in swaps every list at once "
                                        "(also /profile)");
            row.aliases = QStringLiteral("profile profiles preset workspace ai work admin work priorities switch");
            // "no profile" is offered only while that is where you are: once a profile is chosen the
            // lists belong to it, and the way out is to delete it, as in Warp.
            if (currentProfile.isEmpty()) { row.options << QString(); row.optionLabels << QStringLiteral("no profile"); }
            for (const QString &name : names) { row.options << name; row.optionLabels << name; }
            row.options << kNew;
            row.optionLabels << QStringLiteral("new profile…");
            row.current = currentProfile;
            row.onChoose = [this, catalog, curated, saveAs, kNew](const QString &value) {
                if (value == kNew) {
                    const QString name = saveAs(QString(), QStringLiteral("new profile"));
                    if (name.isEmpty()) { refreshSettingsPanes(); return; }   // put the box back on what is current
                    relay::models::curation::saveProfile(name);
                    curated();
                    return;
                }
                if (value.isEmpty()) return;
                relay::models::curation::applyProfile(value);
                curated();   // rank 1 of the new main list is what the next new pane starts on
            };
            models.rows << row;
            if (!currentProfile.isEmpty()) {
                relay::SettingRow actions;
                actions.kind = relay::SettingRow::Buttons;
                actions.id = QStringLiteral("models.profile.actions");
                actions.label = currentProfile;
                actions.indent = 1;
                actions.tooltip = QStringLiteral("The profile the five lists below belong to right now");
                actions.aliases = QStringLiteral("rename delete profile ") + currentProfile;
                actions.buttonTexts = QStringList{QStringLiteral("rename…"), QStringLiteral("export…"), QStringLiteral("delete")};
                actions.onButton = [this, currentProfile, curated, saveAs, exportProfiles](int index) {
                    if (index == 0) {
                        const QString name = saveAs(currentProfile, QStringLiteral("rename profile"));
                        if (name.isEmpty()) return;
                        relay::models::curation::renameProfile(currentProfile, name);
                        curated();
                        return;
                    }
                    if (index == 1) { exportProfiles(QStringList{currentProfile}, currentProfile); return; }
                    if (QMessageBox::question(this, QStringLiteral("delete profile"),
                                              QStringLiteral("Delete the profile “%1”?\nThe five lists stay exactly as they are; "
                                                             "they simply stop belonging to a profile.").arg(currentProfile))
                        != QMessageBox::Yes)
                        return;
                    relay::models::curation::deleteProfile(currentProfile);
                    curated();
                };
                models.rows << actions;
            }
            {
                // Always here, profiles or none: importing is how the first profile arrives on a
                // second machine. "export all" only appears once there is more than one to mean.
                relay::SettingRow file;
                file.kind = relay::SettingRow::Buttons;
                file.id = QStringLiteral("models.profile.file");
                file.label = QStringLiteral("profiles file");
                file.tooltip = QStringLiteral("JSON you can mail, commit to your dotfiles, or carry to another machine. "
                                              "A file holds one profile or all of them; either imports.");
                file.aliases = QStringLiteral("import export profile profiles file json backup share carry");
                file.buttonTexts = QStringList{QStringLiteral("import…")};
                if (names.size() > 1) file.buttonTexts << QStringLiteral("export all…");
                file.onButton = [names, exportProfiles, importProfiles](int index) {
                    if (index == 0) { importProfiles(); return; }
                    exportProfiles(names, QStringLiteral("relay model profiles"));
                };
                models.rows << file;
            }
        }

        // ----- 4. the five lists ----------------------------------------------------------------
        // Main, high, flash, lite, local (owner, 2026-09-20), in place of one priority list and its
        // line. Each is ordered: rank 1 is what the tier runs on, the rest are its fallbacks, and a
        // model in no list is only ever used when you pick it by hand. A row is a model and the
        // reasoning level it runs at *there* — the provider's own word for it (xhigh, not max, on a
        // GPT entry) — so "glm at max for plans, codex at xhigh" is two rows of the high list.
        const QHash<QString, QString> tierBlurbs{
            {QStringLiteral("main"), QStringLiteral("New panes start on the first; /swap and a failing turn step down the rest")},
            {QStringLiteral("high"), QStringLiteral("Plan mode and the hardest turns. Empty: the main model at its top level")},
            {QStringLiteral("flash"), QStringLiteral("Driving programs in the terminal, quick side calls, Alt+F and /flash")},
            {QStringLiteral("lite"), QStringLiteral("Titles, labels, duplicate checks and other chores")},
            {QStringLiteral("local"), QStringLiteral("Models served on this machine, for /local")}};
        const bool listsSet = relay::models::curation::tierListsSet();
        for (const QString &tier : relay::models::curation::tierIds()) {
            relay::SettingRow heading = headingRow(relay::models::curation::tierLabel(tier));
            heading.collapsible = true;
            models.rows << heading;
            const QList<relay::models::curation::TierEntry> list = relay::models::curation::tierList(tier);
            int rank = 0;
            for (const relay::models::curation::TierEntry &item : list) {
                const relay::models::Entry *entry = catalog.find(item.key);
                ++rank;
                relay::SettingRow row;
                row.id = QStringLiteral("models/tier/") + tier + QLatin1Char('/') + item.key;
                const qint64 until = entry ? relay::models::exhaustedUntil(catalog, entry->preset, now) : -1;
                row.label = QStringLiteral("%1. %2").arg(rank).arg(entry ? entry->displayName() : item.key);
                row.tooltip = !entry ? QStringLiteral("This model is not available right now: its provider has no key, or it left the catalog")
                            : !entry->usable ? QStringLiteral("Skipped: no key for %1").arg(entry->provider)
                            : until >= 0 ? QStringLiteral("Exhausted%1 · skipped until then")
                                               .arg(until > 0 ? QStringLiteral(" · resets ") + relay::models::resetText(until, now) : QString())
                            : rank == 1 ? tierBlurbs.value(tier)
                                        : QStringLiteral("Fallback %1 of the %2 list").arg(rank - 1).arg(tier);
                if (!entry || !entry->usable || until >= 0) row.label += QStringLiteral("  · skipped");
                row.aliases = tier + QStringLiteral(" models list priority fallback ") + item.key;
                row.dragGroup = QStringLiteral("tier:") + tier;
                const int index = rank - 1;
                row.onDropBefore = [tier, index, curated](const QString &draggedRowId) {
                    const QString prefix = QStringLiteral("models/tier/") + tier + QLatin1Char('/');
                    if (!draggedRowId.startsWith(prefix)) return;
                    relay::models::curation::moveInTier(tier, draggedRowId.mid(prefix.size()), index);
                    curated();
                };
                const QStringList levels = entry ? entry->efforts : QStringList();
                if (levels.isEmpty()) {
                    row.kind = relay::SettingRow::Buttons;
                    row.buttonTexts = QStringList{QStringLiteral("×")};
                    row.onButton = [tier, key = item.key, curated](int) { relay::models::curation::removeFromTier(tier, key); curated(); };
                } else {
                    row.kind = relay::SettingRow::Choice;
                    // An entry with no level of its own runs at the model's default, and says so —
                    // not at the top level, which is what a lite row looked like it was set to.
                    QStringList options{QString()}, labels{QStringLiteral("default")};
                    for (const QString &level : levels) { options << level; labels << entry->effortLabel(level); }
                    row.options = options;
                    row.optionLabels = labels;
                    row.current = levels.contains(item.effort) ? item.effort : QString();
                    row.onChoose = [tier, key = item.key, curated](const QString &level) {
                        relay::models::curation::setTierEffort(tier, key, level);
                        curated();
                    };
                    row.buttonTexts = QStringList{QStringLiteral("×")};
                    row.onButton = [tier, key = item.key, curated](int) { relay::models::curation::removeFromTier(tier, key); curated(); };
                }
                models.rows << row;
            }
            // The button is the row (owner, 2026-09-20: "make '+ add a model…' a button rather than
            // the separate 'add…' button on the right"); what the list is for is its hover.
            relay::SettingRow add;
            add.kind = relay::SettingRow::Buttons;
            add.id = QStringLiteral("models.tier.add.") + tier;
            add.tooltip = tierBlurbs.value(tier);
            add.aliases = QStringLiteral("add a model ") + tier + QStringLiteral(" models list");
            add.buttonTexts = QStringList{QStringLiteral("+ add a model…")};
            add.onButton = [this, tier, catalog, list, curated](int) {
                    QList<relay::agentui::PickerRow> rows;
                    QList<relay::models::Entry> offered;
                    for (const relay::models::Entry &entry : relay::models::shown(catalog)) {
                        if ((tier == QStringLiteral("local")) != entry.local) continue;      // local models in local, the rest elsewhere
                        // A guest is a whole agent of its own: it can be a pane's own agent (main) or run a
                        // plan turn (high, owner 2026-09-20: "codex planning at xhigh"), never a side call.
                        if (entry.guest && tier != QStringLiteral("main") && tier != QStringLiteral("high")) continue;
                        bool already = false;
                        for (const auto &item : list) already = already || item.key == entry.key;
                        if (already) continue;
                        offered << entry;
                        rows << relay::agentui::PickerRow{{entry.label, entry.provider + (entry.plan.isEmpty() ? QString() : QStringLiteral(" · ") + entry.plan)},
                                                          entry.model, entry.key};
                    }
                    const auto result = relay::agentui::pick(this, relay::models::curation::tierLabel(tier),
                        QStringLiteral("Checked models from the list above. A model you have not checked is not offered here."),
                        {QStringLiteral("model"), QStringLiteral("provider")}, rows, {{QStringLiteral("add"), QStringLiteral("add"), true}});
                    if (result.row < 0) return;
                    const relay::models::Entry &entry = offered.at(result.row);
                    // Where the new row starts: the level this model starts at *in this list*, which
                    // the worker computes (tierStartEffort). Not the model's top level, which is what
                    // this button used to take — Main is the provider's own default (codex's
                    // gpt-5.6-sol starts at `low`, not the `ultra` that gave, owner report
                    // 2026-09-21), High the level a plan turn uses, Flash and Lite the lowest
                    // (card #TKN7).
                    relay::models::curation::addToTier(tier, entry.key, relay::models::tierStartEffort(entry, tier));
                    curated();
                };
            models.rows << add;
        }
        {
            // The two defaults (owner, 2026-09-20), computed by the worker from the providers you
            // can use: your own, or your own with the cost-sensitive OpenRouter twins after them and
            // OpenRouter leading the lite list, which chores lean on most.
            const QJsonObject defaults = !viaHelper ? pane->tierListDefaults() : m_helperTierDefaults.value(tabIdOf(page));
            auto apply = [this, catalog, curated](const QJsonObject &lists) {
                relay::models::curation::applyTierDefaults(lists);
                curated();
            };
            relay::SettingRow row;
            row.kind = relay::SettingRow::Buttons;
            row.id = QStringLiteral("models.tier.defaults");
            row.label = QStringLiteral("fill the lists");
            row.tooltip = QStringLiteral("defaults: your own providers' models, best first. with openrouter: the same, then their "
                                         "cheaper OpenRouter twins, and OpenRouter first for lite. Voice transcription uses the "
                                         "OpenRouter key either way");
            row.buttonTexts = QStringList{QStringLiteral("defaults"), QStringLiteral("defaults with openrouter (recommended)")};
            row.onButton = [defaults, apply, this](int index) {
                const QJsonObject lists = defaults.value(index == 0 ? QStringLiteral("plain") : QStringLiteral("openrouter")).toObject();
                if (lists.isEmpty()) { hint(QStringLiteral("models.defaults.none"), QStringLiteral("The worker has not sent defaults yet; open a pane's agent first")); return; }
                apply(lists);
            };
            if (!listsSet || !defaults.isEmpty()) models.rows << row;
        }

        // ----- 4. defaults ----------------------------------------------------------------------
        { relay::SettingRow head = headingRow(QStringLiteral("defaults")); head.collapsible = true; models.rows << head; }
        {
            relay::SettingRow failover = toggleRow(QStringLiteral("agent/failover"), QStringLiteral("Fall over to a fallback model"),
                                                  QStringLiteral("A turn whose model keeps failing, after its retries, continues down "
                                                                 "the list it is on — that turn only. The pane keeps the model you chose"), true);
            failover.aliases = QStringLiteral("failover fallback retry provider down error 429 overloaded relay free");
            models.rows << failover;
        }
        models.rows << numberRow(QStringLiteral("provider/max_tokens"), QStringLiteral("Output token limit"),
                                 QStringLiteral("Per model call, reasoning included. 0 = automatic: each model's own "
                                                "documented limit (GLM 131072, Gemini 65536). Applies to the next conversation"),
                                 0, 0, 131072);
        models.rows << buttonRow(QStringLiteral("agent.provider"), QStringLiteral("Advanced provider settings"),
                                 QStringLiteral("Base URL, model id, extra request JSON and the agent workspace"),
                                 QStringLiteral("Open…"), [this] { runAction(QStringLiteral("agent.provider")); });
        models.rows << buttonRow(QStringLiteral("agent.modelRoles"), QStringLiteral("per-job models (advanced)"),
                                 QStringLiteral("The high / main / flash / lite / local tiers and what each job — plan mode, subagents, "
                                                "summaries, chores — runs on"),
                                 QStringLiteral("Model roles…"), [this] { runAction(QStringLiteral("agent.modelRoles")); });
        return models;
    }
    // The lists changed — an edit on Options › Models, a profile switched there or by `/profile`:
    // the page redraws and every pane re-reads them, its box and picker first, then the worker's
    // failover chain (rank 2 reaches it now). One exit for every way the curation moves.
    void modelsCurated() {
        refreshSettingsPanes();
        for (Pane *each : allPanes()) {
            each->modelsCurationChanged();
            each->agentOptionsChanged(QStringLiteral("models/fallback"));
        }
    }
    // `applyMainDefault` was here (card #MDL1). It copied rank 1 of the list into
    // `provider/preset`, `provider/model` and `agent/effort` so that a new pane, which read those
    // keys, would land on it — a second copy of the default that ran on five of the eleven paths
    // that change the list, read a list of its own, and skipped a guest at rank 1. A pane now
    // asks `relay::models::startEntry` for rank 1 itself, so there is no copy left to keep in
    // sync: every one of these call sites goes through `modelsCurated()` instead, which is what
    // tells the panes and their workers. `Pane::rememberFallback` still runs, from there.
    // One spelling of the key, shared with Pane::guestSetting.
    static QString guestSettingKey(const QString &guest, const QString &key) {
        return QStringLiteral("guests/%1/%2").arg(guest, key);
    }

    QList<relay::SettingsSection> settingsSections() {
        QList<relay::SettingsSection> sections;
        QSettings settings;

        relay::SettingsSection general;
        general.id = QStringLiteral("general");
        general.title = QStringLiteral("General");
        general.blurb = QStringLiteral("What Relay shows while it works.");
        // Reasoning display (issue T8CN): three ways, not a bool — the reasoning streams into a
        // fold under a ✦ line that collapses when the block ends (collapse), stays open (always),
        // or never draws and leaves the single ✦ summary line (never). The row reads through
        // Pane::thinkingDisplay() so a not-yet-migrated agent/show_thinking shows as what it maps
        // to, and choosing writes the new key (the old one is retired by the same read).
        {
            relay::SettingRow thinking = choiceRow(QStringLiteral("option:thinking_display"),
                                                  QStringLiteral("Thinking display"),
                                                  QStringLiteral("How the agent's reasoning is shown: folded into the "
                                                                 "terminal and collapsed when it ends, always open, "
                                                                 "or only the ✦ summary line"),
                                                  {QStringLiteral("collapse"), QStringLiteral("always"), QStringLiteral("never")},
                                                  {QStringLiteral("Collapse when done"), QStringLiteral("Always open"), QStringLiteral("Never show")},
                                                  Pane::thinkingDisplay(), QStringLiteral("collapse"),
                                                  [this](const QString &value) {
                                                      QSettings().setValue(QStringLiteral("agent/thinking_display"), value);
                                                      if (m_active) m_active->agentOptionsChanged(QStringLiteral("agent/thinking_display"));
                                                  });
            thinking.aliases = QStringLiteral("reasoning thinking traces show_thinking");
            general.rows << thinking;
        }
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
            desktop.changed = !desktop.checked;
            desktop.onToggle = [](bool on) { relay::NotificationCenter::setDesktopEnabled(on); };
            desktop.reset = [] { relay::NotificationCenter::setDesktopEnabled(true); };   // on when Relay ships
            general.rows << desktop;
        }
        {
            relay::SettingRow hints;
            hints.kind = relay::SettingRow::Toggle;
            hints.id = QStringLiteral("option:shortcut_hints");
            hints.label = QStringLiteral("Shortcut hints");
            hints.detail = QStringLiteral("A brief tip when you do something the slow way and a key exists");
            hints.checked = relay::ShortcutHints::instance().enabled();
            hints.changed = !hints.checked;
            hints.onToggle = [](bool on) { relay::ShortcutHints::instance().setEnabled(on); };
            // On when Relay ships. How often each hint has been shown is not a setting and is not
            // touched here; Actions › Reset shortcut hints is what forgets those counts.
            hints.reset = [] { relay::ShortcutHints::instance().setEnabled(true); };
            general.rows << hints;
        }
        general.rows << toggleRow(QStringLiteral("recap/away"), QStringLiteral("Recap when you come back"),
                                  QStringLiteral("Written into a pane a few minutes after work ends there unwatched"), true);
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
            reopen.changed = !reopen.checked;    // on when Relay ships
            reopen.onToggle = [this](bool on) {
                QSettings().setValue(QStringLiteral("windows/restore"), on);
                if (on) m_manager->scheduleSave(); else m_manager->forgetSavedLayout(false);
                notice(on ? QStringLiteral("Relay will reopen this window set on start.")
                                           : QStringLiteral("Relay will open one new window on start."), 6000);
            };
            // The same two effects as the toggle, without its notice: the reset has one of its own.
            reopen.reset = [this] {
                QSettings().remove(QStringLiteral("windows/restore"));
                if (WindowManager::restoreEnabled()) m_manager->scheduleSave();
                else m_manager->forgetSavedLayout(false);
            };
            general.rows << reopen;
        }
        // Which releases /update offers (card #HDA9). It is an option and not a flag the user types,
        // because /update is the whole surface: the channel has to be answered before the command
        // runs. The updater's own default is the same "all", so an unset key and the shipped row
        // agree.
        general.rows << headingRow(QStringLiteral("Updates"));
        {
            relay::SettingRow channel = choiceRow(QStringLiteral("option:update/channel"),
                                                  QStringLiteral("Update channel"),
                                                  QStringLiteral("Which releases /update offers: every published "
                                                                 "release, betas included, or only the finished ones"),
                                                  {QStringLiteral("all"), QStringLiteral("stable")},
                                                  {QStringLiteral("All releases, betas included"),
                                                   QStringLiteral("Stable releases only")},
                                                  updateChannel(), QStringLiteral("all"),
                                                  [](const QString &value) {
                                                      if (value == QStringLiteral("all"))
                                                          QSettings().remove(QStringLiteral("update/channel"));
                                                      else
                                                          QSettings().setValue(QStringLiteral("update/channel"), value);
                                                  });
            channel.aliases = QStringLiteral("update upgrade release beta prerelease stable channel version");
            general.rows << channel;
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
                                                ids, labels, current, QStringLiteral("info"),
                                                [](const QString &id) { relay::log::setLevel(id); });
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
                                         QStringLiteral("What Relay opens on and what a new tab starts with; this tab takes "
                                                        "it at once, and so do /light, /dark and /theme unless turned off below"),
                                         ids, labels, relay::theme::startupThemeId(),
                                         relay::theme::defaultThemeId(), [this](const QString &id) {
                if (!chooseTheme(id, m_tabs->currentWidget(), true)) {
                    notice(QStringLiteral("That theme could not be read."), 6000);
                    return;
                }
                notice(QStringLiteral("Theme: %1.").arg(relay::theme::active().name), 4000);
            });
        }
        // "Randomize" (card #R4ND, owner 2026-09-20): a theme you did not pick, and a different one
        // every press. It is a button and not a stored mode — what it lands on *is* the theme, as
        // if it had been chosen from the list above — so there is nothing here to reset.
        {
            relay::SettingRow shuffle = buttonRow(QStringLiteral("option:theme_random"), QStringLiteral("Randomize"),
                                                  QStringLiteral("Take a theme at random: never the one you are on, and a "
                                                                 "different one each press"),
                                                  QStringLiteral("Randomize"), [this] { randomizeTheme(); });
            shuffle.aliases = QStringLiteral("random randomise shuffle surprise dice any theme");
            // Undoable in one click — the list above is right there — so an agent asked to
            // randomize the theme may press it (owner decision 2, card #FEJQ).
            shuffle.agentSafeButtons = {0};
            appearance.rows << shuffle;
        }
        // Owner, 2026-09-19: "add an option, on by default, that themes are tab specific. and add
        // an option, off by default, to start tabs with a new theme."
        appearance.rows << toggleRow(QStringLiteral("theme/per_tab"), QStringLiteral("Each tab keeps its own theme"),
                                     QStringLiteral("/light, /dark and /theme change the tab you are in; switching tabs switches "
                                                    "the theme"),
                                     true, [this](bool on) {
            if (on) applyTabTheme(m_tabs->currentWidget());
            else relay::theme::setActiveTheme(relay::theme::startupThemeId(), false);
            m_tabs->tabBar()->update();
        });
        appearance.rows << toggleRow(QStringLiteral("theme/commands_set_default"),
                                     QStringLiteral("/light, /dark and /theme also set the default"),
                                     QStringLiteral("The theme a command picks becomes what new tabs start with and what Relay opens "
                                                    "on; off, a command changes only the tab you are in"),
                                     true);
        appearance.rows << toggleRow(QStringLiteral("theme/new_tab_new_theme"), QStringLiteral("Start each new tab on the next theme"),
                                     QStringLiteral("A new tab takes the next theme in the list instead of the default, so "
                                                    "tabs are easy to tell apart"),
                                     false, [this](bool on) {
            // One way of telling tabs apart at a time.
            if (on) { QSettings().setValue(QStringLiteral("theme/randomize_new_tab"), false); refreshSettingsPanes(); }
        });
        // Card #R4ND, owner 2026-09-20: "i meant a persistent mode. it randomizes on each new
        // tab." The button above is the one-shot; this is the mode.
        appearance.rows << toggleRow(QStringLiteral("theme/randomize_new_tab"), QStringLiteral("Start each new tab on a random theme"),
                                     QStringLiteral("A new tab takes a theme drawn at random — never the default and never the "
                                                    "previous tab's — so tabs are easy to tell apart"),
                                     false, [this](bool on) {
            if (on) { QSettings().setValue(QStringLiteral("theme/new_tab_new_theme"), false); refreshSettingsPanes(); }
        });
        {
            // Pane header colours (#SPBN): the pane chrome reads the key; refreshAll() repaints.
            relay::SettingRow colours = choiceRow(QStringLiteral("option:pane_colours"), QStringLiteral("Pane colours"),
                                                  QStringLiteral("Tint a pane's header by what it is: each type its own, one per "
                                                                 "group (tools, agents), or none. A remote shell is always marked"),
                                                  relay::panestatus::colourModeIds(), relay::panestatus::colourModeLabels(),
                                                  relay::panestatus::colourModeId(relay::panestatus::colourModeFrom(
                                                      QSettings().value(QStringLiteral("appearance/pane_colours")).toString())),
                                                  relay::panestatus::colourModeId(relay::panestatus::ColourMode::ByType),
                                                  [](const QString &id) {
                QSettings().setValue(QStringLiteral("appearance/pane_colours"), id);
                PaneChrome::refreshAll();
                // The title-bar buttons wear the same tints while their pane is open.
                refreshChromeButtonsEverywhere();
            });
            colours.aliases = QStringLiteral("color colors colour header tint band pane type group");
            appearance.rows << colours;
        }
        {
            // Pane CPU / memory meters (issue #D03W): the chip beside each pane's title and the
            // suffix in the tab label. The poll re-reads the key within its next tick.
            relay::SettingRow usage = toggleRow(QStringLiteral("appearance/pane_usage"),
                                               QStringLiteral("Pane CPU and memory"),
                                               QStringLiteral("A small CPU % and memory % beside each pane's title, "
                                                              "in the tab label and tooltip, and on the conversation's row "
                                                              "in Sessions, while the pane is using the machine"),
                                               true, [this](bool) { refreshPaneStatus(); });
            usage.aliases = QStringLiteral("cpu memory ram usage load percent meter resource");
            appearance.rows << usage;
        }
        sections << appearance;

        sections << modelsSection();

        // Right after Models, because a local endpoint is one more thing the model dropdown can
        // offer — it just has no key, so it is not in the API keys dialog (card #24XJ).
        sections << localModels().section();

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
                                       current, QStringLiteral("agent"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("control/default"), value);
            });
        }
        // The key stays `terminal/copy_on_select` although the behaviour is no longer terminal-only
        // (src/CopyOnSelect.h): renaming it would turn the setting off for everyone who had it on.
        terminal.rows << toggleRow(QStringLiteral("terminal/copy_on_select"), QStringLiteral("Copy on select"),
                                   QStringLiteral("Highlighting text copies it, in the terminal and in read-only panes"), false);
        // Owner, 2026-09-19: "clickable things need to be understood from colors" — and "make the
        // 'add color in program output' an option that is on by default".
        terminal.rows << toggleRow(QStringLiteral("terminal/colour_links"),
                                   QStringLiteral("Colour paths and links in output"),
                                   QStringLiteral("A file, folder, URL or #card Relay can open is green at rest, not only under "
                                                  "the pointer; a colour a program chose stays"),
                                   true, [this](bool) { for (Pane *pane : allPanes()) pane->applyTerminalSettings(); });
        {
            const QString current = QSettings().value(QStringLiteral("terminal/echo_band"), QStringLiteral("channel")).toString();
            terminal.rows << choiceRow(QStringLiteral("option:echo_band"),
                                       QStringLiteral("Band behind what you typed"),
                                       QStringLiteral("The line you sent sits on a band: the channel's colour (cyan shell, violet agent), "
                                                      "the theme's chrome, or none"),
                                       {QStringLiteral("channel"), QStringLiteral("chrome"), QStringLiteral("none")},
                                       {QStringLiteral("Channel colour"), QStringLiteral("Theme chrome"), QStringLiteral("None")},
                                       current, QStringLiteral("channel"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("terminal/echo_band"), value);
            });
        }
        terminal.rows << toggleRow(QStringLiteral("terminal/shell_integration"),
                                   QStringLiteral("Shell integration (OSC 7/133)"),
                                   QStringLiteral("Directory and prompt marks; applies to new panes"), false);
        // SSH sessions (#S5SH, docs/SSH-AND-MOSH.md): the wrapper is set up when a pane's shell
        // starts, so the mode applies to new panes; the host lists are read at each login.
        terminal.rows << headingRow(QStringLiteral("SSH"));
        {
            const QString current = QSettings().value(QStringLiteral("ssh/enhance"), QStringLiteral("auto")).toString();
            relay::SettingRow row = choiceRow(QStringLiteral("option:ssh_enhance"), QStringLiteral("SSH sessions"),
                                              QStringLiteral("What Relay adds to an ssh you type (below); applies to new panes"),
                                              {QStringLiteral("auto"), QStringLiteral("ask"), QStringLiteral("off")},
                                              {QStringLiteral("Enhance automatically"), QStringLiteral("Ask for each host"),
                                               QStringLiteral("Off — plain ssh")},
                                              current, QStringLiteral("auto"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("ssh/enhance"), value);
            });
            row.aliases = QStringLiteral("mosh remote host wrapper controlmaster enhance");
            terminal.rows << row;
        }
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:ssh_enhance");
            info.label = QStringLiteral("Enhancing adds OpenSSH connection sharing to the ssh you type, so the agent can run "
                                        "commands on that host over your login without asking for it again, and loads prompt "
                                        "marks into the remote shell for that login only. Nothing is installed on the host.");
            terminal.rows << info;
        }
        terminal.rows << hostListRow(QStringLiteral("ssh/hosts_never"), QStringLiteral("Never enhance on"),
                                     QStringLiteral("Hosts, comma separated: always plain ssh there"));
        terminal.rows << hostListRow(QStringLiteral("ssh/hosts_always"), QStringLiteral("Always enhance on"),
                                     QStringLiteral("Hosts, comma separated: enhanced without asking in Ask mode"));
        sections << terminal;

        relay::SettingsSection agent;
        agent.id = QStringLiteral("agent");
        agent.title = QStringLiteral("Agent");
        agent.blurb = QStringLiteral("Instructions, skills and the Switchboard. Most of these apply to the "
                                     "next conversation; the timeout rows apply at once.");
        agent.rows << headingRow(QStringLiteral("Instructions and skills"));
        {
            // Where a new pane's prompt box starts; Ctrl+I cycles auto → terminal → agent in the pane.
            const QString current = Pane::defaultInputMode();
            agent.rows << choiceRow(QStringLiteral("option:input_default"),
                                    QStringLiteral("Default input for new sessions"),
                                    QStringLiteral("Ctrl+I cycles auto → terminal → agent; ! and * override one line"),
                                    {QStringLiteral("auto"), QStringLiteral("shell"), QStringLiteral("agent")},
                                    {QStringLiteral("Auto"), QStringLiteral("Terminal"), QStringLiteral("Agent")},
                                    current, QStringLiteral("auto"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("input/default"), value);
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
        {
            // The one row whose value is a folder on this machine, so it is the one row with a
            // Browse… button beside the box (card #XZZB); the box still takes a typed path.
            relay::SettingRow plans = textRow(QStringLiteral("agent/plans_dir"), QStringLiteral("Plans folder"),
                                              QStringLiteral("Absolute folder for plans (empty: <project>/.relay/plans)"),
                                              QStringLiteral("<project>/.relay/plans"));
            plans.browse = true;
            agent.rows << plans;
        }
        {
            // Owner decision 3 (card #FEJQ, 2026-09-20): one toggle gates the helper agent and
            // every pane agent together, and it is on when Relay ships — "the main pane agent gets
            // the write tools on by default". It is not an approval prompt (§30.8): the safety net
            // is that every change announces itself and is undoable in one click.
            relay::SettingRow writes = toggleRow(QStringLiteral("agent/app_writes"),
                                                 QStringLiteral("Agents may change options and run actions"),
                                                 QStringLiteral("An agent can set an option on this page, run a "
                                                                "reversible action and open a pane at a row. "
                                                                "Every change says so and can be undone; API keys "
                                                                "are never settable."),
                                                 true, [this](bool) { sendAppCatalog(); });
            writes.aliases = QStringLiteral("agent control app tools options actions writes permission");
            agent.rows << writes;
        }
        {
            // #GMCF decision 7: what a pane sends before the first user word. The full profile is
            // ~14,500 tokens of prompt and tool schemas, which a hosted provider caches and a model
            // served on this machine prefills at about 800 tokens a second — eighteen seconds of
            // silence on every cold turn. "Auto" sends the short profile (18 rules, 8 tools, plus
            // the five Switchboard tools when a project is attached) to a local endpoint, a model
            // on the Lite list of Options › Models (owner, 2026-09-20) or a model with a window of
            // 32k or less, and the full one to everything else; the other two pin it for panes
            // whose owner disagrees.
            relay::SettingRow profile =
                choiceRow(QStringLiteral("agent/prompt_profile"), QStringLiteral("Prompt profile"),
                          QStringLiteral("Auto: the short prompt on a local, Lite-tier or "
                                         "small-window model, the full one elsewhere"),
                          {QStringLiteral("auto"), QStringLiteral("full"), QStringLiteral("short")},
                          {QStringLiteral("Auto"), QStringLiteral("Full"), QStringLiteral("Short")},
                          QSettings().value(QStringLiteral("agent/prompt_profile"),
                                            QStringLiteral("auto")).toString(),
                          QStringLiteral("auto"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("agent/prompt_profile"), value);
            });
            profile.aliases = QStringLiteral("prompt profile short full local lite tier context window tokens prefill speed");
            agent.rows << profile;
        }
        agent.rows << headingRow(QStringLiteral("Switchboard"));
        {
            // Owner, 2026-09-19: new boards are created hidden from now on, so the cards do not
            // clutter the project's root listing. Only decides what a board created from now on is
            // called (`relay::projects::newBoardFolder()`); a board that already exists moves only
            // through the explicit "Hide this board's folder" / "Show this board's folder" action on
            // the Switchboard itself (protocol 19.17), never through this row.
            relay::SettingRow hiddenFolder =
                toggleRow(QString::fromLatin1(relay::projects::kHiddenFolderSetting),
                         QStringLiteral("Hidden Switchboard folder"),
                         QStringLiteral("New boards are created as .switchboard/ rather than switchboard/"),
                         true);
            hiddenFolder.aliases = QStringLiteral("switchboard board folder dotfile hide show dotswitchboard");
            agent.rows << hiddenFolder;
        }
        {
            // Signals (#AQ6X decision 9, owner: "i think yes by default, but its optional"). The
            // flag itself lives in the *board's* `board.yaml` (`signals: {auto_work: …}`), because
            // it is a property of a project and not of this installation — a checkout whose tests
            // are expected to be red does not want threads started on them. So the row writes
            // through the tab's Switchboard worker (`signals_config`, protocol §32.2) and keeps a
            // copy in the settings, which is what it can draw itself from: Options is built before
            // any worker has answered, and a row that showed nothing until one did would read as
            // off. A tab with no project attached has no board to write to and says so.
            relay::SettingRow work =
                toggleRow(QStringLiteral("board/signals_auto_work"),
                          QStringLiteral("Work signals unasked"),
                          QStringLiteral("A failing test nobody is on starts its own agent thread; "
                                         "you get a notification that opens it, and it is listed "
                                         "in Sessions."),
                          true, [this](bool on) {
                QWidget *page = m_tabs ? m_tabs->currentWidget() : nullptr;
                if (!page) return;
                if (relay::projects::boardDirOf(boardWorkspaceOfTab(page)).isEmpty()) {
                    notice(QStringLiteral("This tab has no project attached, so there is no "
                                          "Switchboard to set that on."), 6000);
                    return;
                }
                sendToHelper(page, {{QStringLiteral("type"), QStringLiteral("signals_config")},
                                               {QStringLiteral("auto_work"), on}});
            });
            work.aliases = QStringLiteral("signals signal thread unasked auto work failing test fix "
                                          "agent switchboard board");
            agent.rows << work;
        }
        {
            // The personal inbox board was dropped 2026-09-19 (#916B): a card filed in a tab with
            // no project attached now goes here if it is set, else through the project picker.
            relay::SettingRow defaultProject =
                textRow(QString::fromLatin1(relay::projects::kDefaultProjectSetting),
                       QStringLiteral("Default project for loose cards"),
                       QStringLiteral("Where a card goes when filed with no project attached (empty: ask each time)"),
                       QStringLiteral("(ask each time)"));
            defaultProject.browse = true;
            defaultProject.aliases = QStringLiteral("inbox loose card project picker default switchboard");
            agent.rows << defaultProject;
        }
        {
            // The known-projects list (#916B; the lesson of Warp's #11899, where the list only
            // grows): every project Relay knows, why it became known and when, each with a Remove
            // that forgets the registry record and nothing else — the project's files, its board
            // included, are not touched. The projects the user said no to ("do not make a
            // Switchboard here") follow, each with an Undo, so a no is as reversible as a yes.
            relay::projects::Registry &registry = m_manager->projects();
            const QList<relay::projects::Record> known = registry.knownProjects();
            const QStringList declined = registry.declined();
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            if (known.isEmpty() && declined.isEmpty()) {
                relay::SettingRow none;
                none.kind = relay::SettingRow::Info;
                none.id = QStringLiteral("info:known_projects");
                none.label = QStringLiteral("No known projects yet");
                none.detail = QStringLiteral("A project becomes known when a tab attaches to it: opening its Switchboard, "
                                             "/card, the # card picker, or the project picker");
                none.aliases = QStringLiteral("known projects registry");
                agent.rows << none;
            }
            for (const relay::projects::Record &record : known) {
                relay::SettingRow row;
                row.kind = relay::SettingRow::Buttons;
                row.id = QStringLiteral("project:") + record.key;
                row.label = record.name.isEmpty() ? relay::projects::nameFor(record.path) : record.name;
                QStringList detail{record.path};
                if (!record.reason.isEmpty())
                    detail << QStringLiteral("known because %1, %2").arg(relay::projects::reasonText(record.reason),
                                                                          relay::projects::agoText(record.knownSince, now));
                if (record.lastAttached > 0 && record.lastAttached != record.knownSince)
                    detail << QStringLiteral("last attached %1").arg(relay::projects::agoText(record.lastAttached, now));
                // The filesystem, not the record: a board made after the attach (the picker's
                // "Initialize new project here") is on disk before the record is refreshed.
                if (relay::projects::boardDirOf(record.path).isEmpty()) detail << QStringLiteral("no Switchboard yet");
                row.detail = detail.join(QStringLiteral(" · "));
                row.aliases = QStringLiteral("known project forget remove registry switchboard ") + record.path;
                row.buttonTexts = QStringList{QStringLiteral("Remove")};
                const QString path = record.path;
                row.onButton = [this, path](int) {
                    m_manager->projects().forget(path);
                    notice(QStringLiteral("Forgot %1 · its files, Switchboard included, are untouched; an attached tab stays attached")
                               .arg(relay::projects::nameFor(path)), 7000);
                };
                agent.rows << row;
            }
            for (const QString &path : declined) {
                relay::SettingRow row;
                row.kind = relay::SettingRow::Buttons;
                row.id = QStringLiteral("declined:") + relay::projects::keyFor(path);
                row.label = relay::projects::nameFor(path);
                row.detail = QStringLiteral("%1 · you said no to a Switchboard here, so Relay does not ask again").arg(path);
                row.aliases = QStringLiteral("known projects declined undo ask again switchboard ") + path;
                row.buttonTexts = QStringList{QStringLiteral("Undo")};
                // Undo is the reversible class by definition, and this row's one button is
                // literally labelled it; it was off only because the row never marked it
                // (owner decision 2, #AG7R group 3). All it does is let Relay ask about this
                // folder again — nothing is created, and saying no when it asks declines it
                // once more.
                row.agentSafeButtons = QList<int>{0};
                row.onButton = [this, path](int) {
                    m_manager->projects().undecline(path);
                    notice(QStringLiteral("%1 may be asked about again.").arg(relay::projects::nameFor(path)), 5000);
                };
                agent.rows << row;
            }
        }
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
        // Idle deadline for a model call (protocol 15). Applies to the running agent at once.
        {
            relay::SettingRow stall = numberRow(QStringLiteral("agent/stall_timeout_s"), QStringLiteral("Stop a silent model after"),
                                                QStringLiteral("No output for this long ends the turn: retried once, then a message. "
                                                               "Reasoning models can be quiet for a while; 60 s is the default"),
                                                60, 1, 1800, QStringLiteral(" s"));
            stall.aliases = QStringLiteral("stall timeout hang stuck thinking silent retry");
            agent.rows << stall;
            // The wait for the *first* chunk is a different thing from the gaps between chunks:
            // prefill, queueing and routing on a prompt that may be hundreds of thousands of
            // tokens. A minute of silence in the middle of an answer is a dead stream; a minute
            // before it starts is an ordinary large prompt. 0 keeps both on the one number, which
            // is what Relay did before this row (a local endpoint has always had its own budget).
            relay::SettingRow firstToken =
                numberRow(QStringLiteral("agent/first_token_timeout_s"),
                          QStringLiteral("Wait longer for the first token"),
                          QStringLiteral("Extra patience before the model's first output, for a big "
                                         "prompt or a busy provider (0: use the limit above)"),
                          0, 0, 1800, QStringLiteral(" s"));
            firstToken.aliases = QStringLiteral("first token prefill timeout slow start deadline patience");
            alsoBoardWorkers(firstToken);
            agent.rows << firstToken;
        }
        sections << agent;

        // ----- Security (card #3KB7) -------------------------------------------------------
        // Owner, 2026-09-19: "add a security options menu with various secruity options like that
        // ... more of the approvals options on warp." Warp's execution profiles set a value per
        // capability — always_allow / always_ask / never — plus command and directory lists. Relay
        // keeps the two ends and not the middle: `docs/ROADMAP.md` settled against per-action
        // approvals, so where Warp asks, Relay denies, confines or bounds. The worker enforces
        // every row here (backend/relay_core/security.py); the rows only carry the lists to it.
        relay::SettingsSection security;
        security.id = QStringLiteral("security");
        security.title = QStringLiteral("Security");
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:security");
            info.label = QStringLiteral(
                "Relay allows by default: the agent's commands and file edits run without per-action "
                "approval — Ask before, below, is the opt-in that stops the actions you tick and asks first. "
                "What bounds them is where they may reach, and that is what this "
                "page sets. File tools are confined to the pane's workspace and refuse .ssh, .gnupg, .git, "
                ".env and .pem/.key files, on this machine and on an ssh host. Commands run with your own user "
                "permissions under a systemd memory limit — a denylist below is a guardrail against an obvious "
                "mistake, not a sandbox: a shell line can always be spelled another way.");
            security.rows << info;
        }
        // Moved here from Agent (card #3KB7): how far the agent's own hands reach is a bound,
        // and it belongs with the others. The chain limit (kMaxHandoffChain, src/InputPolicy.h)
        // is the backstop this row cannot turn off.
        {
            // run_in_terminal (protocol 22): how far a command the agent hands over may go. The
            // value is read when a turn starts, so it applies to the next prompt.
            const QString current = QSettings().value(QStringLiteral("agent/terminal_handoff"),
                                                      QStringLiteral("agent")).toString();
            security.rows << choiceRow(QStringLiteral("option:terminal_handoff"),
                                       QStringLiteral("Commands the agent hands to your terminal"),
                                       QStringLiteral("For ssh, sudo and logins, which the agent's own shell cannot run. "
                                                      "Three hand-offs in a row with nothing typed in between is the cap"),
                                       {QStringLiteral("agent"), QStringLiteral("prefill"), QStringLiteral("off")},
                                       {QStringLiteral("The agent runs it or puts it in the prompt box"),
                                        QStringLiteral("Always in the prompt box, for you to run"),
                                        QStringLiteral("Off")},
                                       current, QStringLiteral("agent"), [](const QString &value) {
                QSettings().setValue(QStringLiteral("agent/terminal_handoff"), value);
            });
        }
        security.rows << listRow(QStringLiteral("security/command_denylist"),
                                 QStringLiteral("Commands the agent never runs"),
                                 QStringLiteral("Comma-separated. A bare name is a program, so \"rm\" also refuses "
                                                "\"sudo rm\" and \"ls && rm x\" but not \"rmdir\"; a * makes it a "
                                                "pattern for the whole line (\"git push --force*\"). The agent is told "
                                                "which rule refused it."),
                                 QStringLiteral("rm, shutdown, git push --force*"),
                                 QStringLiteral(","),
                                 QStringLiteral("denylist deny block command"));
        security.rows << listRow(QStringLiteral("security/readable_roots"),
                                 QStringLiteral("Folders the agent may read outside the workspace"),
                                 QStringLiteral("Comma-separated absolute paths. Reading only — writing stays inside "
                                                "the workspace whatever is listed here, and the symlink and "
                                                "secret-file guards apply to these folders too."),
                                 QStringLiteral("/home/you/notes, /srv/reference"),
                                 QStringLiteral(","),
                                 QStringLiteral("directory allowlist folder read"));
        security.rows << listRow(QStringLiteral("security/secret_patterns"),
                                 QStringLiteral("Files the agent never reads"),
                                 QStringLiteral("Space-separated regular expressions, matched against each part of a "
                                                "path, added to the built-in list — which cannot be removed. Use \\s "
                                                "for a space."),
                                 QStringLiteral("\\.vault$ credentials"),
                                 QStringLiteral("\\s+"),
                                 QStringLiteral("secret redact pattern regex"));
        security.rows << toggleRow(QStringLiteral("security/clipboard_write"),
                                   QStringLiteral("Let the terminal put text on your clipboard"),
                                   QStringLiteral("OSC 52, off by default. Lets a command — including one the agent "
                                                  "runs — copy for you, and lets anything else that reaches the "
                                                  "screen replace what you are about to paste. Reading your "
                                                  "clipboard is never allowed and has no switch."), false);
        // Per-pane isolation (src/Isolation.h), moved here from Terminal (card #3KB7): the caps
        // scale with the machine (agent RAM/16 clamped to 2–8G, shell RAM/2 clamped to 4–16G),
        // and these rows write the same [isolation] keys relay.conf takes, so a manual edit and
        // this page agree.
        security.rows << headingRow(QStringLiteral("Memory limits"));
        security.rows << toggleRow(QStringLiteral("isolation/enabled"),
                                   QStringLiteral("Per-pane memory limits"),
                                   QStringLiteral("Each pane's shell and agent run in their own systemd scope, so a runaway "
                                                   "command stops inside its pane; applies to new panes"), true);
        {
            const QString current = QSettings().value(QStringLiteral("isolation/agent_memory_max"), QStringLiteral("auto")).toString();   // unset is "auto", so a fresh install shows no ↺
            relay::SettingRow row = choiceRow(QStringLiteral("option:agent_memory_max"),
                                              QStringLiteral("Agent memory limit"),
                                              QStringLiteral("Per pane, for the agent worker and the commands it runs; the next agent starts under it"),
                                              {QStringLiteral("auto"), QStringLiteral("2G"), QStringLiteral("4G"), QStringLiteral("8G"),
                                               QStringLiteral("16G"), QStringLiteral("infinity")},
                                              {QStringLiteral("Auto — %1 on this machine").arg(isolation::agentDefault()),
                                               QStringLiteral("2 GiB"), QStringLiteral("4 GiB"), QStringLiteral("8 GiB"),
                                               QStringLiteral("16 GiB"), QStringLiteral("No limit")},
                                              current, QStringLiteral("auto"), [](const QString &value) {
                if (value == QStringLiteral("auto")) QSettings().remove(QStringLiteral("isolation/agent_memory_max"));
                else QSettings().setValue(QStringLiteral("isolation/agent_memory_max"), value);
                // Card #Y4RX: the escapee cap below is this same limit, so rewrite its drop-ins.
                if (QSettings().value(QStringLiteral("isolation/cap_escapees"), false).toBool()) {
                    escapees::install(escapees::userConfigRoot(),
                                      {isolation::memory("isolation/agent_memory_max", isolation::agentDefault()),
                                       isolation::memory("isolation/agent_swap_max", isolation::agentSwapDefault())});
                    escapees::reload();
                }
            });
            row.aliases = QStringLiteral("oom memory isolation worker limit kill");
            security.rows << row;
        }
        {
            const QString current = QSettings().value(QStringLiteral("isolation/shell_memory_max"), QStringLiteral("auto")).toString();   // unset is "auto", so a fresh install shows no ↺
            relay::SettingRow row = choiceRow(QStringLiteral("option:shell_memory_max"),
                                              QStringLiteral("Shell memory limit"),
                                              QStringLiteral("Per pane, for the shell you type in; new panes start under it"),
                                              {QStringLiteral("auto"), QStringLiteral("4G"), QStringLiteral("8G"), QStringLiteral("16G"),
                                               QStringLiteral("32G"), QStringLiteral("infinity")},
                                              {QStringLiteral("Auto — %1 on this machine").arg(isolation::shellDefault()),
                                               QStringLiteral("4 GiB"), QStringLiteral("8 GiB"), QStringLiteral("16 GiB"),
                                               QStringLiteral("32 GiB"), QStringLiteral("No limit")},
                                              current, QStringLiteral("auto"), [](const QString &value) {
                if (value == QStringLiteral("auto")) QSettings().remove(QStringLiteral("isolation/shell_memory_max"));
                else QSettings().setValue(QStringLiteral("isolation/shell_memory_max"), value);
            });
            row.aliases = QStringLiteral("oom memory isolation shell limit kill");
            security.rows << row;
        }
        // The hole per-pane limits cannot close, and the owner's opt-in mitigation (card #Y4RX).
        // A child may ask the user's systemd for a transient scope of its own over D-Bus; the scope
        // it gets is a sibling of the pane's, not a child, so nothing Relay does from inside the
        // pane's scope contains it. tmux and Chrome both do exactly that.
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:escapee_scopes");
            info.label = QStringLiteral("Two programs get out from under these limits. tmux moves its server into "
                                        "tmux-spawn-<uuid>.scope and Chrome puts each app instance in "
                                        "app-com.google.Chrome-<pid>.scope, both directly under app.slice: a program "
                                        "may ask systemd for a scope of its own, and Relay cannot contain that from "
                                        "the pane's. Their memory counts against no pane's limit, and an out-of-memory "
                                        "kill in app.slice can land on any program there, not only the pane that "
                                        "started it.");
            security.rows << info;
        }
        {
            const QString cap = isolation::memory("isolation/agent_memory_max", isolation::agentDefault());
            const QString swap = isolation::memory("isolation/agent_swap_max", isolation::agentSwapDefault());
            relay::SettingRow row = toggleRow(QStringLiteral("isolation/cap_escapees"),
                                              QStringLiteral("Cap programs that leave their pane (tmux, Chrome)"),
                                              QStringLiteral("Off by default. Writes systemd user drop-ins that cap those two at "
                                                             "%1 of memory and %2 of swap — machine-wide for them, not per pane: "
                                                             "every tmux server and Chrome app scope on this machine, whether "
                                                             "Relay started it or not").arg(cap, swap),
                                              false, [this](bool on) {
                // Read now, not when the row was built: the limit above may have changed since.
                const QString cap = isolation::memory("isolation/agent_memory_max", isolation::agentDefault());
                const QString swap = isolation::memory("isolation/agent_swap_max", isolation::agentSwapDefault());
                const QString root = escapees::userConfigRoot();
                QStringList skipped;
                const QStringList touched = on ? escapees::install(root, {cap, swap}, &skipped)
                                               : escapees::removeAll(root, &skipped);
                const bool reloaded = (touched.isEmpty() && skipped.isEmpty()) || escapees::reload();
                QString said = on ? QStringLiteral("Capped tmux and Chrome at %1 (%2 drop-in(s) written)").arg(cap).arg(touched.size())
                                  : QStringLiteral("Removed Relay's tmux and Chrome caps (%1 file(s))").arg(touched.size());
                if (!skipped.isEmpty())
                    said += QStringLiteral("; left alone, not written by Relay: ") + skipped.join(QStringLiteral(", "));
                if (!reloaded) said += QStringLiteral("; `systemctl --user daemon-reload` failed, so it takes effect at your next login");
                statusBar()->showMessage(said + QStringLiteral("."), 8000);
            });
            row.aliases = QStringLiteral("tmux chrome browser scope app.slice oom escape dropin systemd cap");
            security.rows << row;
        }
        // Turn bounds, moved here from Agent (card #3KB7): they are the real cost and runaway
        // control. The compaction threshold and the two timeouts stay on the Agent page, where
        // they describe the model call rather than what it may reach.
        security.rows << headingRow(QStringLiteral("Turn bounds"));
        security.rows << numberRow(QStringLiteral("agent/max_auto_turns"),
                                   QStringLiteral("Automatic turns from background agents"),
                                   QStringLiteral("In a row without your input (0 = unlimited)"), 50, 0, 10000);
        // Card #2CZP: both sit at their maxima by default, so a long overnight run is not stopped by
        // a count. A turn that has stopped making progress is ended by the loop detector instead;
        // these two are the fuse behind it, for a runaway turn nothing else catches.
        security.rows << numberRow(QStringLiteral("agent/max_steps"), QStringLiteral("Step limit per turn"),
                                   QStringLiteral("Backstop for a runaway turn; then it stops with Continue"), 500, 1, 500);
        security.rows << numberRow(QStringLiteral("agent/max_tool_calls"), QStringLiteral("Tool-call limit per turn"),
                                   QStringLiteral("Backstop for a runaway turn, not the normal stop"), 2000, 1, 2000);
        security.rows << toggleRow(QStringLiteral("agent/audit_requests"), QStringLiteral("Audit requests after each turn"),
                                   QStringLiteral("A small side call flags asks that may be unaddressed"), false);
        // Card #K2FV: the opt-in ask. Seven rows, one saved list; an approval ask's "Always
        // allow" unticks the matching row by writing the same list. The labels are the ask's
        // headers (approvals.LABELS), so the row an ask names is the row that unticks.
        security.rows << headingRow(QStringLiteral("Ask before"));
        security.rows << approvalRow(QStringLiteral("edit"), QStringLiteral("Change a file that already exists"),
                                     QStringLiteral("edit_file and write_file, on a file that is there"));
        security.rows << approvalRow(QStringLiteral("create"), QStringLiteral("Create a new file"),
                                     QStringLiteral("write_file, where no file is yet"));
        security.rows << approvalRow(QStringLiteral("delete_or_move"), QStringLiteral("Delete or move files"),
                                     QStringLiteral("rm, mv and the like, read from the command line — a script or a "
                                                    "variable can still spell them another way"));
        security.rows << approvalRow(QStringLiteral("read_outside"), QStringLiteral("Read outside the workspace"),
                                     QStringLiteral("read_file and list_directory outside the pane's workspace; the "
                                                    "readable-folders list above widens what counts as inside"));
        security.rows << approvalRow(QStringLiteral("terminal"), QStringLiteral("Run in your terminal"),
                                     QStringLiteral("run_in_terminal: the command runs in your own shell, not the "
                                                    "agent's"));
        security.rows << approvalRow(QStringLiteral("program"), QStringLiteral("Type into your program"),
                                     QStringLiteral("type_into_program, on a program you have handed over"));
        security.rows << approvalRow(QStringLiteral("network"), QStringLiteral("Reach the network"),
                                     QStringLiteral("curl, wget, git push and the like, read from the command line"));
        security.rows << buttonRow(QStringLiteral("option:approvals/again"), QStringLiteral("The first-launch choice"),
                                   QStringLiteral("Cautious by default, or allow everything — shown on the first "
                                                  "configure until it is answered"),
                                   QStringLiteral("Show it again"), [this] {
            QSettings().remove(QStringLiteral("security/approvals_chosen"));
            if (m_active) {
                m_active->agentOptionsChanged(QStringLiteral("security/approvals_ask"));
                openApprovalsPane(m_active);
            }
        });
        sections << security;


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
            // No fixed default: the key Relay starts on is read off the keyboard layout the first
            // time (Pane::voiceHoldKey), so putting it back means forgetting what was stored and
            // letting it be derived again — on this machine's layout, which may have changed.
            relay::SettingRow hold = choiceRow(QStringLiteral("option:voice_hold_key"), QStringLiteral("Voice key"),
                                    QStringLiteral("Held down while you speak; released, it transcribes"),
                                    ids, labels, Pane::voiceHoldKey(), QString(), [this](const QString &value) {
                QSettings().setValue(QStringLiteral("voice/hold_key"), value);
                if (m_active) m_active->agentOptionsChanged(QStringLiteral("voice/hold_key"));
            });
            hold.reset = [this] {
                QSettings().remove(QStringLiteral("voice/hold_key"));
                if (m_active) m_active->agentOptionsChanged(QStringLiteral("voice/hold_key"));
            };
            // Derived rather than fixed, so "changed" is whether a key was ever chosen by hand.
            hold.changed = QSettings().contains(QStringLiteral("voice/hold_key"));
            voice.rows << hold;
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
            // The first is the one Relay ships on, here and in Pane::voiceModel().
            const QStringList ids{QStringLiteral("google/gemini-3.5-flash-lite"), QStringLiteral("google/gemini-3.8-flash"),
                                  QStringLiteral("openai/whisper-1")};
            const QStringList labels{QStringLiteral("Gemini 3.5 Flash-Lite — fastest, ~$0.00006 a clip"),
                                     QStringLiteral("Gemini 3.8 Flash — most accurate, ~$0.0004 a clip"),
                                     QStringLiteral("Whisper — transcription endpoint, ~$0.0003 a clip")};
            voice.rows << choiceRow(QStringLiteral("option:voice_model"), QStringLiteral("Transcription model"),
                                    QStringLiteral("Runs on OpenRouter with your OpenRouter key"),
                                    ids, labels, Pane::voiceModel(), ids.constFirst(), [](const QString &value) {
                QSettings().setValue(QStringLiteral("voice/model"), value);
            });
        }
        voice.rows << buttonRow(QStringLiteral("agent.modelKeys"), QStringLiteral("OpenRouter key"),
                                QStringLiteral("Voice needs one of its own, whatever model your panes run"),
                                QStringLiteral("API keys…"), [this] { runAction(QStringLiteral("agent.modelKeys")); });
        sections << voice;

        // Remote (#PH0N): one switch and the address it uses. The page is built where the
        // settings live (src/RemoteSettings.cpp), so a test can read its rows without a window.
        sections << remoteSection();

        relay::SettingsSection privacy;
        privacy.id = QStringLiteral("privacy");
        privacy.title = QStringLiteral("Privacy");
        privacy.blurb = QStringLiteral("Relay has no telemetry. Everything below decides what leaves this machine. "
                                       "On your own key it goes to that provider only; on Relay Free it goes through "
                                       "Relay's hosted service to the provider, and Relay keeps request metadata only.");
        {
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:privacy");
            info.label = QStringLiteral(
                "Keys are stored in the desktop keyring (secret-tool, service org.relayterminal.Relay) or read "
                "from RELAY_<PROVIDER>_API_KEY. They are never written to Relay's settings files and never logged. "
                "A request on your own key never touches Relay's server. On Relay Free, the pane's prompts and "
                "tool context go to Relay's hosted service and on to the model provider; Relay logs request "
                "metadata (time, size, outcome) and never the text. Pick another provider to switch it off. "
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
        {
            // Review B1 (protocol 26.7): the Sessions list reads Claude Code's and Codex's own
            // transcripts into Relay's index so they can be listed, searched and resumed. The
            // copy never leaves this machine, but it is a copy of every prompt and reply, and
            // until this row there was no way to say no.
            relay::SettingRow row = toggleRow(QStringLiteral("sessions/index_guests"),
                                  QStringLiteral("List Claude Code and Codex sessions"),
                                  QStringLiteral("Relay copies their prompts and replies into its own index (on this "
                                                 "machine only) so Sessions can search and resume them. Off removes "
                                                 "the copies; their own files are never touched"), true);
            row.aliases = QStringLiteral("claude codex guest sessions index transcripts history privacy search");
            privacy.rows << row;
        }
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
                                        values, labels, presetId, QStringLiteral("relay"),
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
                                        programKeys, QStringLiteral("shift-only"),
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
                "pane — onto another pane's edge to move it there; onto a tab's label to move it into that "
                "tab; or onto the tab bar's empty space to give it a tab of its own. Esc during the drag "
                "puts it back. Double click a pane's title to rename it. Click the "
                "folder line on the right of the header to open that folder in an explorer pane, and again to "
                "close it. Ctrl+click a path, a URL or a “tool calls” line in the terminal to open it. Right "
                "click in the terminal for Relay's menu.");
            shortcuts.rows << mouse;
        }
        sections << shortcuts;

        // ----- About (owner, 2026-09-20: "i think there should be an about section in the
        // options") -------------------------------------------------------------------------
        // What this Relay *is*, which is what you go looking for when you want to say which build
        // you saw something in. The build line lived under General › Diagnostics, where nobody
        // thought to look for it; Diagnostics keeps what you *set* (the log level), and this page
        // holds what you *read*. Nothing here is a setting, so the page gets no "Reset to
        // defaults" row — resetRow() builds that from rows that declare a default, and none of
        // these do.
        relay::SettingsSection about;
        about.id = QStringLiteral("about");
        about.title = QStringLiteral("About");
        about.blurb = QStringLiteral("Which Relay this is. The Copy button below puts all of it on the clipboard in "
                                     "one block, which is what a bug report wants.");
        {
            relay::SettingRow identity;
            identity.kind = relay::SettingRow::Info;
            identity.id = QStringLiteral("info:about.identity");
            identity.aliases = QStringLiteral("about version licence license agpl free software source");
            identity.label = QStringLiteral("Relay %1 — a terminal whose panes have their own agents.\n"
                                            "Free software under the GNU Affero General Public License, version 3 or "
                                            "later (the LICENSE file beside the source says it in full).")
                                 .arg(QStringLiteral(RELAY_VERSION));
            about.rows << identity;
        }
        {
            // Which build this window is running, and whether the one on disk has moved on (owner,
            // 2026-09-19). A rebuild never reaches a running Relay, nor the windows it opens.
            const relay::buildinfo::Running &build = relay::buildinfo::running();
            const QString onDisk = relay::buildinfo::idOnDisk();
            relay::SettingRow info;
            info.kind = relay::SettingRow::Info;
            info.id = QStringLiteral("info:build");
            info.aliases = QStringLiteral("about build number version running since binary path rebuild");
            info.label = QStringLiteral("Build %1 · version %2 · running since %3 · %4")
                             .arg(build.id, QStringLiteral(RELAY_VERSION), build.started.toString(QStringLiteral("HH:mm")),
                                  QCoreApplication::applicationFilePath());
            if (onDisk != build.id)
                info.label += QStringLiteral("\nA newer build is on disk: %1. Quit and reopen Relay to run it; New window "
                                             "stays on this one, a launch from the taskbar starts the new one.").arg(onDisk);
            about.rows << info;
        }
        {
            relay::SettingRow parts;
            parts.kind = relay::SettingRow::Info;
            parts.id = QStringLiteral("info:about.parts");
            parts.aliases = QStringLiteral("about engine core libvterm ghostty qt platform kernel architecture");
            // defaultEngineCore() is empty unless --engine-core or RELAY_ENGINE_CORE named one, so
            // it is the *core* rather than the engine; say it the way the pane header already does.
            const QString core = relay::defaultEngineCore();
            parts.label = QStringLiteral("Terminal engine: %1 · Qt %2 · %3 · %4")
                              .arg(core.isEmpty() ? QStringLiteral("Relay engine")
                                                  : QStringLiteral("Relay engine (%1)").arg(core),
                                   QString::fromLatin1(qVersion()),
                                   QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
            about.rows << parts;
        }
        about.rows << buttonRow(QStringLiteral("about.copy"), QStringLiteral("Copy this page"),
                                QStringLiteral("Version, build, engine and platform, as one block to paste into a bug report"),
                                QStringLiteral("Copy"), [this] {
            const relay::buildinfo::Running &build = relay::buildinfo::running();
            const QString onDisk = relay::buildinfo::idOnDisk();
            QStringList block;
            block << QStringLiteral("Relay %1").arg(QStringLiteral(RELAY_VERSION))
                  << QStringLiteral("build: %1").arg(build.id)
                  << QStringLiteral("binary: %1").arg(QCoreApplication::applicationFilePath())
                  << QStringLiteral("engine: %1").arg(relay::defaultEngineCore().isEmpty()
                                                          ? QStringLiteral("relay")
                                                          : QStringLiteral("relay/%1").arg(relay::defaultEngineCore()))
                  << QStringLiteral("qt: %1").arg(QString::fromLatin1(qVersion()))
                  << QStringLiteral("system: %1 (%2)").arg(QSysInfo::prettyProductName(),
                                                           QSysInfo::currentCpuArchitecture());
            if (onDisk != build.id) block << QStringLiteral("build on disk: %1").arg(onDisk);
            QGuiApplication::clipboard()->setText(block.join(QLatin1Char('\n')));
            notice(QStringLiteral("Copied: Relay %1, build %2.").arg(QStringLiteral(RELAY_VERSION), build.id), 5000);
        });
        sections << about;

        // Last on every page: a way to put that page back to what Relay ships with (owner,
        // 2026-09-18). It is built from each section's own rows, so it reaches exactly the options
        // you are looking at and nothing on another tab, and a page where nothing declares a
        // default — Local models, whose rows are the servers you saved — gets no button at all.
        for (relay::SettingsSection &section : sections) {
            relay::SettingRow reset = relay::resetRow(section, [this, title = section.title](int count) {
                // Queued: this runs from the button's own click, and refreshing the panes rebuilds
                // the row the button sits in.
                QTimer::singleShot(0, this, [this, title, count] {
                    notice(QStringLiteral("%1: %2 %3 back to Relay's defaults.")
                               .arg(title).arg(count)
                               .arg(count == 1 ? QStringLiteral("option is") : QStringLiteral("options are")), 6000);
                    // The theme, the log level and the keymap are already in effect; the other open
                    // Options panes are still drawing the values that have just gone.
                    refreshSettingsPanes();
                });
            });
            if (!reset.id.isEmpty()) section.rows << reset;
        }
        return sections;
    }

    QList<PaletteItem> rootItems() {
        QList<PaletteItem> items;
        Pane *pane = m_active;
        const QString agent = QStringLiteral("Agent"), terminal = QStringLiteral("Terminal"), panes = QStringLiteral("Panes and tabs"),
                      app = QStringLiteral("Relay"), keys = QStringLiteral("Shortcuts");
        // The model, by its name and its provider (card #MDL1, rule 1) — "glm-5.3 · z.ai (glm)",
        // not the preset label "z.ai · glm-5.3 · coding plan", which is a plan and not a model.
        QString currentModel;
        if (pane) {
            currentModel = pane->presetModelDisplay(pane->currentPreset());
            if (currentModel.isEmpty())
                for (const auto &model : pane->storedModels())
                    if (model.first == pane->currentPreset()) currentModel = model.second;
        }
        items << submenu(QStringLiteral("menu:model"), agent, QStringLiteral("Model"), currentModel.isEmpty() ? QStringLiteral("No stored keys") : currentModel, [this] {
            QList<PaletteItem> children;
            if (!m_active) return children;
            for (const auto &model : m_active->storedModels()) {
                PaletteItem item;
                const QString id = model.first;
                item.key = QStringLiteral("model:") + id; item.section = QStringLiteral("Model");
                const QString named = m_active->presetModelDisplay(id);
                item.label = named.isEmpty() ? model.second : named;
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
        if (pane && (pane->hasLocalEndpoint() || pane->agentRole() == QStringLiteral("local"))) {
            // The Local agent (card #JH22), listed only when this machine serves a model. /local
            // does the same thing from the prompt box.
            const bool local = pane->agentRole() == QStringLiteral("local");
            const QString localModel = pane->roleModel(QStringLiteral("local"));
            items << actionItem(agent, QStringLiteral("Local agent for this pane"),
                                (local ? QStringLiteral("On · ") : QStringLiteral("Off · "))
                                    + (localModel.isEmpty() ? QStringLiteral("a model served on this machine; the conversation is kept · /local") : localModel),
                                QStringLiteral("agent.localAgent"), local);
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
            // The levels this pane's provider has, not Relay's four: on GLM medium is the same
            // request as high, and Relay Free stops at medium (presets.py effort_levels).
            const QStringList levels = pane->offeredEfforts();
            const QString effortNote = pane->effortNote();
            // The note goes in the submenu's own detail line. It is a sentence about the provider,
            // not something to run, and a row here is a thing you can choose.
            items << submenu(QStringLiteral("menu:effort"), agent, QStringLiteral("Reasoning effort"),
                             levels.isEmpty()
                                 ? QStringLiteral("this model has no reasoning setting")
                                 : Pane::nearestEffort(levels, effort)
                                       + (effortNote.isEmpty() ? QString()
                                                               : QStringLiteral(" · ") + effortNote),
                             [this, effort, levels] {
                QList<PaletteItem> children;
                for (const QString &level : levels) {
                    PaletteItem item;
                    item.key = QStringLiteral("effort:") + level; item.section = QStringLiteral("Reasoning effort"); item.label = level;
                    item.detail = QStringLiteral("This pane; keeps the conversation");
                    item.checked = Pane::nearestEffort(levels, effort) == level; item.stayOpen = true;
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
        {
            // One row for the session manager pane (#R6J0). /resume and /conversations both open it
            // now, so the old "Conversations…" row would be the same row twice; its words find this one.
            PaletteItem sessions = actionItem(agent, QStringLiteral("Sessions…"),
                                              QStringLiteral("Find and resume a session: every conversation and Relay's terminal "
                                                             "history, searchable, with subagent threads · /resume"),
                                              QStringLiteral("agent.resume"));
            sessions.aliases = QStringLiteral("resume session reopen continue conversations search find chats threads history "
                                              "full text past old grep manager");
            items << sessions;
            PaletteItem info = actionItem(agent, QStringLiteral("Conversation info"),
                                          QStringLiteral("This conversation's model, tokens, file and history, with its subagent "
                                                         "threads · /status"),
                                          QStringLiteral("agent.info"));
            info.aliases = QStringLiteral("status info details usage cost context about session");
            items << info;
        }
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
        items << actionItem(agent, QStringLiteral("Reasoning fold"),
                            pane && pane->thinkingFoldVisible()
                                ? QStringLiteral("Fold the agent's reasoning away in this pane")
                                : QStringLiteral("Unfold the agent's reasoning · the last turn's, between turns"),
                            QStringLiteral("agent.thinkingPanel"), pane && pane->thinkingFoldVisible());
        items << actionItem(agent, QStringLiteral("Activity"),
                            pane && pane->internals()
                                ? QStringLiteral("Bring this pane's Activity pane forward")
                                : QStringLiteral("Watch the reasoning and the tool calls in a pane beside the terminal"),
                            QStringLiteral("agent.internalsPane"), pane && pane->internals());
        // The row is a slow path, so it teaches the fast one (#SXF1): agent.continue's own key
        // when it has one, else agent.interrupt's empty-box send-now that sends Continue.
        QString continueKeys = Keymap::instance().shortcutText(QStringLiteral("agent.continue"));
        if (continueKeys.isEmpty()) continueKeys = Keymap::instance().shortcutText(QStringLiteral("agent.interrupt"));
        const QString continueHow = continueKeys.isEmpty() ? QStringLiteral("/continue")
                                                           : continueKeys + QStringLiteral(" on an empty box or /continue");
        items << actionItem(agent, QStringLiteral("Continue agent turn"),
                            pane && (pane->limitReached() || pane->turnCutOff())
                                ? QStringLiteral("The last turn stopped early · %1").arg(continueHow)
                                : QStringLiteral("Send “Continue” to the agent · %1").arg(continueHow),
                            QStringLiteral("agent.continue"));
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
        {
            // Sharing (#W5N2). Two rows, because they are two different things: hand out a way in,
            // and look after the people who came in.
            relay::RemoteShare &share = relay::RemoteShare::instance();
            const bool shared = pane && share.isSharing(pane->sessionToken());
            const int guests = pane ? share.sharingModel().guestsOn(pane->sessionToken()) : 0;
            const int waiting = share.sharingModel().waiting();
            // Pairing your own phone first: it is the common case, and it is the one that turns
            // remote control on by itself (#FR1C).
            items << actionItem(terminal, QStringLiteral("Pair a phone…"),
                                relay::RemoteShare::instance().alwaysOn()
                                    ? QStringLiteral("The code to type on it, and the QR · remote control is on")
                                    : QStringLiteral("Turns remote control on, then shows the code to type on the phone"),
                                QStringLiteral("remote.pair"));
            items << actionItem(terminal, QStringLiteral("Share this pane…"),
                                shared ? QStringLiteral("Already shared · pair another phone, or invite someone")
                                       : QStringLiteral("Pair your phone, or make a link for somebody else"),
                                QStringLiteral("pane.share"));
            if (share.sharingModel().anyShared())
                items << actionItem(terminal, QStringLiteral("Sharing"),
                                    waiting > 0 ? QStringLiteral("%1 waiting for you").arg(waiting)
                                    : guests > 0 ? QStringLiteral("%1 here · invites, roles and what is waiting").arg(guests)
                                                 : QStringLiteral("Who is here, live invites, and what is waiting for you"),
                                    QStringLiteral("pane.sharing"));
            items << actionItem(terminal, QStringLiteral("Open a shared pane…"),
                                QStringLiteral("A pane your other desktop shares, here as one of your devices"),
                                QStringLiteral("remote.openShared"));
            items << actionItem(terminal, QStringLiteral("Join a shared session…"),
                                QStringLiteral("The meeting code and PIN someone gave you · /join CODE"),
                                QStringLiteral("remote.join"));
        }
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
        {
            // The Switchboard's tooling sibling (card #7BM4): the project's tests, their history
            // and their runs. No key of its own — Ctrl+Shift+T is New tab everywhere — so the
            // palette and the board's Tests button are how it is reached.
            PaletteItem tests = actionItem(panes, QStringLiteral("Test suites"),
                                           QStringLiteral("This project's tests, their history and their runs, beside the Switchboard"),
                                           QStringLiteral("tests.open"));
            tests.aliases = QStringLiteral("tests test suites ctest unittest flaky slow failing suite coverage runs");
            items << tests;
        }
        // Offered only while this tab is attached to a project (#JN7X): with no project there is
        // nothing to detach from, and the quiet state must not advertise itself. No shortcut —
        // detaching is rare, so there is no fast path to teach and no hint entry.
        if (const QString project = tabProject(m_tabs->currentWidget()); !project.isEmpty()) {
            PaletteItem detach;
            detach.key = QStringLiteral("project.detach"); detach.section = panes;
            detach.label = QStringLiteral("Detach this tab from %1").arg(relay::projects::nameFor(project));
            detach.detail = QStringLiteral("%1 · its panes lose the card tools; an open Switchboard stays open").arg(project);
            detach.aliases = QStringLiteral("project switchboard attach unattach board");
            detach.run = [this] { detachTab(m_tabs->currentWidget()); };
            items << detach;
        } else {
            // The project picker (#916B): the projects Relay knows, or initialize one here. It has
            // no key of its own, so choosing it here teaches the key that opens it by itself.
            PaletteItem pick = actionItem(panes, QStringLiteral("Attach this tab to a project…"),
                                          QStringLiteral("The projects Relay knows, most recent first, or initialize one here"),
                                          QStringLiteral("project.pick"));
            pick.aliases = QStringLiteral("project switchboard attach known picker initialize init board");
            pick.run = [this] {
                openProjectPicker(m_active, QString());
                if (candidateProject().isEmpty())
                    hint(QStringLiteral("project.pick.palette"),
                         relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("board.open")),
                                                        QStringLiteral("opens this by itself in a tab with no project")));
            };
            items << pick;
        }
        // The one passive entry point to "Initialize a project and create a Switchboard here?"
        // (protocol 19.12). Offered only while the active pane is standing in a project that has
        // no board: with a board there is nothing to create, and outside a project there is
        // nothing to create it in. Choosing it here is the slow path, so it teaches `/init`.
        if (const QString candidate = candidateProject();
            !candidate.isEmpty() && relay::projects::boardDirOf(candidate).isEmpty()) {
            PaletteItem init;
            init.key = QStringLiteral("project.init"); init.section = panes;
            init.label = QStringLiteral("Initialize a project here…");
            init.detail = QStringLiteral("%1 · asks first, then creates %2/")
                              .arg(candidate, relay::projectinit::boardFolderFor(candidate));
            init.aliases = QStringLiteral("switchboard board project init cards start setup");
            init.run = [this] {
                if (!m_active) return;
                m_active->askProjectInit(relay::projectinit::Trigger::InitCommand);
                hint(QStringLiteral("init.palette"), QStringLiteral("Next time: type /init in any prompt box"));
            };
            items << init;
        }
        items << actionItem(panes, QStringLiteral("Open file…"), QStringLiteral("Preview a file in a pane"), QStringLiteral("files.open"));
        // The one key makes a pane on the right; all four directions keep an action of their own
        // so they can be run from here or bound (issue #78BN).
        items << actionItem(panes, QStringLiteral("New pane to the right"),
                            QStringLiteral("Then ← ↑ ↓ within two seconds places it on that side"), QStringLiteral("pane.splitRight"));
        items << actionItem(panes, QStringLiteral("New pane below"), QString(), QStringLiteral("pane.splitDown"));
        items << actionItem(panes, QStringLiteral("New pane to the left"), QString(), QStringLiteral("pane.splitLeft"));
        items << actionItem(panes, QStringLiteral("New pane above"), QString(), QStringLiteral("pane.splitUp"));
        // SSH (#S5SH): the split is offered only while the pane is in a session it can re-run.
        if (pane && !pane->remoteCommandLine().isEmpty()) {
            QString host;
            const QString again = relay::ssh::rerunCommand(relay::ssh::processArgv(pane->foregroundPid()), &host);
            if (!again.isEmpty())
                items << actionItem(panes, QStringLiteral("Split on the same host"),
                                    QStringLiteral("A pane to the right running %1; a shared connection needs no second login").arg(again),
                                    QStringLiteral("ssh.splitSameHost"));
        }
        {
            PaletteItem hosts = submenu(QStringLiteral("menu:ssh"), panes, QStringLiteral("Connect to host…"),
                                        QStringLiteral("ssh in a new tab · ~/.ssh/config and recent hosts"),
                                        [this] { return sshMenuItems(); });
            hosts.aliases = QStringLiteral("ssh mosh remote server login");
            hosts.typed = [this](const QString &search) { return sshTypedItems(search); };
            items << hosts;
        }
        items << actionItem(panes, QStringLiteral("New tab"), QString(), QStringLiteral("tab.new"));
        items << actionItem(panes, QStringLiteral("New window"), QString(), QStringLiteral("window.new"));
        items << actionItem(panes, QStringLiteral("Close pane"), QStringLiteral("Then the tab, then the window"), QStringLiteral("pane.close"));
        items << actionItem(panes, QStringLiteral("Move pane to new tab"), QStringLiteral("Keeps the shell and agent running"), QStringLiteral("pane.moveToNewTab"));
        items << actionItem(panes, QStringLiteral("Equalize pane sizes"), QStringLiteral("Every splitter in this tab back to equal shares"), QStringLiteral("pane.equalize"));
        items << actionItem(panes, QStringLiteral("Move tab to new window"), QStringLiteral("Keeps its panes running"), QStringLiteral("tab.moveToNewWindow"));
        items << actionItem(panes, QStringLiteral("Move pane left"), QStringLiteral("Then ↓ docks it beneath · or drag the ⠿ grip"), QStringLiteral("pane.moveLeft"));
        items << actionItem(panes, QStringLiteral("Move pane right"), QStringLiteral("Then ↓ docks it beneath"), QStringLiteral("pane.moveRight"));
        items << actionItem(panes, QStringLiteral("Move pane up"), QString(), QStringLiteral("pane.moveUp"));
        items << actionItem(panes, QStringLiteral("Move pane down"), QStringLiteral("Straight after a left/right move, beneath that neighbor"), QStringLiteral("pane.moveDown"));
        items << actionItem(panes, QStringLiteral("Restore closed"), QStringLiteral("Last closed pane, tab or window"), QStringLiteral("closed.restore"));
        items << actionItem(panes, QStringLiteral("Recently closed…"), QStringLiteral("The last 25 closed panes, tabs and windows, in the Sessions pane"), QStringLiteral("closed.list"));
        // Any of the last 25, newest first (src/ClosedStack.h). Searching the actions finds them by
        // name and by directory.
        if (const QList<relay::closed::Record> closed = m_manager->closedRecords(); !closed.isEmpty()) {
            items << submenu(QStringLiteral("menu:closed"), panes, QStringLiteral("Recently closed"),
                             QStringLiteral("%1 to reopen, with their text and conversations").arg(closed.size()), [this] {
                QList<PaletteItem> children;
                const QList<relay::closed::Record> records = m_manager->closedRecords();
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                for (auto it = records.crbegin(); it != records.crend(); ++it) {
                    PaletteItem item;
                    const QString id = it->id;
                    item.key = QStringLiteral("closed:") + id; item.section = QStringLiteral("Panes and tabs");
                    item.label = QStringLiteral("Reopen %1: %2").arg(relay::closed::kindName(it->kind).toLower(), relay::closed::label(*it));
                    item.detail = QStringList{relay::closed::place(*it, QDir::homePath()), relay::closed::age(it->closedAt, now)}
                                      .filter(QRegularExpression(QStringLiteral("."))).join(QStringLiteral(" · "));
                    item.aliases = QStringLiteral("restore closed undo reopen");
                    item.run = [this, id] { m_manager->restoreClosed(this, id); };
                    children << item;
                }
                return children;
            });
        }
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
            // "Which build am I on" is a question you ask in words, not by hunting through tabs, and
            // the page it opens is all Info rows — which the Options search skips on purpose, since
            // a search result there is something you press. So the palette is where it is findable.
            const relay::buildinfo::Running &build = relay::buildinfo::running();
            PaletteItem item; item.key = QStringLiteral("app.about"); item.section = app;
            item.label = QStringLiteral("About Relay");
            item.detail = QStringLiteral("Version %1 · build %2 · licence · what it is built on")
                              .arg(QStringLiteral(RELAY_VERSION), build.id);
            item.aliases = QStringLiteral("about version build number licence license agpl qt engine platform diagnostics");
            item.run = [this] { openSettingsPane(relay::SettingsPane::Mode::Options, QStringLiteral("about")); };
            items << item;
        }
        {
            PaletteItem themes; themes.key = QStringLiteral("theme.reload"); themes.section = app;
            themes.label = QStringLiteral("Reload themes"); themes.detail = QStringLiteral("Pick up a theme file you added or edited");
            themes.run = [this] {
                relay::theme::refreshThemes();
                relay::theme::setActiveTheme(relay::theme::activeThemeId(), false);
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
            // The prompt box's Up/Down history outlives a restart (src/PromptHistory.h), so there
            // has to be a way to forget it. Every pane's file goes, and so does the copy every
            // open prompt box holds — including one part-way through a browse.
            PaletteItem history; history.key = QStringLiteral("history.clear"); history.section = app;
            history.label = QStringLiteral("Clear prompt history");
            history.detail = QStringLiteral("Forget pane recall and saved command suggestions");
            history.aliases = QStringLiteral("prompt history up arrow forget clear erase commands prompts");
            history.run = [this] {
                if (QMessageBox::question(this, QStringLiteral("Clear prompt history"),
                                          QStringLiteral("Forget every line the prompt box recalls with Up and all saved command suggestions?"),
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                    return;
                QString error;
                if (!relay::prompthistory::clearAll(&error)) { notice(error, 8000); return; }
                RichEditor::forgetAllHistory();
                notice(QStringLiteral("Prompt history cleared."), 4000);
            };
            items << history;
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
            {QStringLiteral("copy on select"), QStringLiteral("clipboard selection highlight copy primary mouse terminal pane info panes transcript preview diff board")},
            {QStringLiteral("shortcut preset"), QStringLiteral("keymap keybindings hotkeys warp vscode konsole preset")},
            {QStringLiteral("inside programs"), QStringLiteral("vim nano less passthrough program keys")},
            {QStringLiteral("suggest"), QStringLiteral("autocomplete ghost ai suggestions next command prompt")},
            {QStringLiteral("recap"), QStringLiteral("summary away return catch up")},
            {QStringLiteral("tasks"), QStringLiteral("todos todo requests ledger asks open items checklist unaddressed progress")},
            {QStringLiteral("continue agent"), QStringLiteral("continue keep going limit steps more turn")},
            {QStringLiteral("limit"), QStringLiteral("max steps tool calls budget turn length continue uncapped overnight backstop fuse loop stuck")},
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

    // ----- SSH: Connect to host…, Split on the same host (#S5SH, docs/SSH-AND-MOSH.md) ---------
    // Connect opens a new tab whose shell runs `ssh <host>` once it is at its prompt (the pane's
    // queue, as for a command typed while the terminal is busy). The list is the recently used
    // hosts, then every concrete Host of ~/.ssh/config and its Includes, read afresh each time.

    PaletteItem sshHostItem(const QString &target, const QString &detail) {
        PaletteItem item;
        item.key = QStringLiteral("ssh:") + target;
        item.section = QStringLiteral("SSH");
        item.label = target;
        item.detail = detail;
        item.aliases = QStringLiteral("ssh ") + detail;
        item.run = [this, target] { connectToHost(target); };
        return item;
    }

    QList<PaletteItem> sshMenuItems() {
        QList<PaletteItem> items;
        QHash<QString, QString> details;
        const QList<relay::ssh::Host> hosts = relay::ssh::userHosts();
        for (const relay::ssh::Host &host : hosts) details.insert(host.alias, host.detail());
        QSet<QString> listed;
        for (const QString &target : relay::ssh::recentHosts()) {
            const QString detail = details.value(target);
            items << sshHostItem(target, detail.isEmpty() ? QStringLiteral("recent") : QStringLiteral("recent · ") + detail);
            listed.insert(target);
        }
        for (const relay::ssh::Host &host : hosts)
            if (!listed.contains(host.alias)) items << sshHostItem(host.alias, host.detail());
        if (items.isEmpty()) {
            PaletteItem none;
            none.key = QStringLiteral("ssh:none");
            none.section = QStringLiteral("SSH");
            none.label = QStringLiteral("No hosts yet");
            none.detail = QStringLiteral("Type user@host in the search box, or add Host entries to ~/.ssh/config");
            items << none;
        }
        return items;
    }

    // "user@host" or "ssh host" typed in the search box: a row for exactly that.
    QList<PaletteItem> sshTypedItems(const QString &search) {
        const QString target = relay::ssh::typedTarget(search);
        if (target.isEmpty()) return {};
        PaletteItem item = sshHostItem(target, QStringLiteral("connect in a new tab"));
        item.label = relay::ssh::connectCommand(target);
        return {item};
    }

    void openSshMenu() {
        openSettingsPane(relay::SettingsPane::Mode::Actions);
        if (ToolPane *tool = settingsPaneIn(m_tabs->currentWidget(), relay::SettingsPane::Mode::Actions))
            tool->settings()->scrollToGroup(QStringLiteral("menu:ssh"));
    }

    void connectToHost(const QString &target) {
        if (target.trimmed().isEmpty()) return;
        if (!addTab(paneNode(activeCwd()), m_tabs->currentIndex() + 1) || !m_active) return;
        startNewTabTheme(m_tabs->currentWidget());
        m_active->queueCommand(relay::ssh::connectCommand(target));
        relay::ssh::rememberHost(target);
    }

    // The focused pane's ssh or mosh command line, read from the process's own argv so quoting
    // survives, run again in a new pane to the right.
    void splitSameHost() {
        Pane *source = m_active;
        if (!source || source->remoteCommandLine().isEmpty()) {
            notice(QStringLiteral("This pane is not in an ssh or mosh session."));
            return;
        }
        QString host;
        const QString command = relay::ssh::rerunCommand(relay::ssh::processArgv(source->foregroundPid()), &host);
        if (command.isEmpty()) {
            notice(QStringLiteral("Relay splits onto the same host only for an ssh or mosh login; this pane runs %1.")
                       .arg(source->remoteCommandLine().section(QLatin1Char(' '), 0, 0)));
            return;
        }
        if (m_activeLeaf != source) setActiveLeaf(source);
        splitToward(relay::panes::Direction::Right, true);
        if (m_active && m_active != source) m_active->queueCommand(command);
        if (!host.isEmpty()) relay::ssh::rememberHost(host);
    }

    // Shortcut hints for the slow way to the same place: a new tab (host empty) or a split from a
    // pane on `host`, followed by an ssh typed by hand. refreshPaneStatus() sees the session start.
    static constexpr qint64 kSshNewTabHintMs = 12000, kSshSplitHintMs = 60000;
    static void markForSshHint(Pane *pane, const QString &host) {
        if (!pane) return;
        pane->setProperty("relaySshHintAt", QDateTime::currentMSecsSinceEpoch());
        pane->setProperty("relaySshHintHost", host);
    }

    void sshSessionSeen(Pane *pane, const QString &remoteLine) {
        const qint64 at = pane->property("relaySshHintAt").toLongLong();
        if (!at) return;
        const QString host = pane->property("relaySshHintHost").toString();
        pane->setProperty("relaySshHintAt", QVariant());
        pane->setProperty("relaySshHintHost", QVariant());
        const qint64 age = QDateTime::currentMSecsSinceEpoch() - at;
        if (host.isEmpty()) {
            // Connect has no default key: then the faster path is the Actions list itself, which
            // also offers the hosts of ~/.ssh/config and the ones used lately (card #S5SH).
            if (age > kSshNewTabHintMs) return;
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("ssh.connect"));
            hint(QStringLiteral("ssh.connect.typed"),
                 keys.isEmpty() ? QStringLiteral("Next time: Actions › Connect to host… lists your saved and recent hosts")
                                : relay::ShortcutHints::nextTime(keys, QStringLiteral("connect to a host in a new tab")));
            return;
        }
        if (age > kSshSplitHintMs || relay::panestatus::remoteHost(remoteLine) != host) return;
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("ssh.splitSameHost"));
        hint(QStringLiteral("ssh.split.typed"),
             keys.isEmpty() ? QStringLiteral("Next time: Split on the same host (Actions, or right-click › New pane on %1)").arg(host)
                            : relay::ShortcutHints::nextTime(keys, QStringLiteral("split on the same host")));
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
        if (ToolPane *tool = settingsPaneIn(m_tabs->currentWidget(), relay::SettingsPane::Mode::Actions))
            tool->settings()->scrollToGroup(QStringLiteral("menu:agents"));
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

    // ----- the Activity pane (card #QT8C, named by #4X53) ---------------------------------------
    // One per terminal pane, beside it: the reasoning and the tool calls, live, while the terminal
    // prints neither. Opening it again brings the one that is there forward. Closing it — its ×,
    // Ctrl+W, the tab — hands the rows it took back to the terminal (Pane::detachInternals), and
    // closing the owner takes it along with nothing reprinted.
    ToolPane *internalsPaneOf(Pane *owner) const {
        QWidget *page = pageOf(owner);
        if (!page) return nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Internals
                && tool->property("internalsOwnerPane").value<QObject *>() == owner)
                return tool;
        return nullptr;
    }

    void openInternalsPane(Pane *owner) {
        if (!owner) return;
        ToolPane *tool = internalsPaneOf(owner);
        if (!tool) {
            tool = createInternalsPane(owner->cwd());
            linkInternalsPane(tool, owner);
            // Beside the owner; below it when the owner is too narrow to share its width.
            insertBeside(owner, tool, owner->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
        }
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }

    ToolPane *createInternalsPane(const QString &cwd) {
        auto *view = new relay::AgentInternalsView;
        auto *tool = new ToolPane(ToolPane::Kind::Internals, view, view, cwd);
        tool->setProperty("paneType", QStringLiteral("internals"));   // pane-type header colours
        relay::theme::polishWindow(tool);
        tool->setObjectName(QStringLiteral("pane"));
        return tool;
    }

    void linkInternalsPane(ToolPane *tool, Pane *owner) {
        auto *view = tool ? dynamic_cast<relay::AgentInternalsView *>(tool->hosted()) : nullptr;
        if (!view || !owner) return;
        tool->setProperty("internalsOwnerPane", QVariant::fromValue<QObject *>(owner));
        tool->setProperty("internalsOwner", owner->scrollbackId());   // the saved layout's key
        QPointer<Pane> ownerGuard(owner);
        QPointer<ToolPane> guard(tool);
        view->onOpenOutput = [ownerGuard](const QString &turn, const QString &callId) {
            if (ownerGuard) ownerGuard->requestInternalsOutput(turn, callId);
        };
        view->onOpenDiff = [ownerGuard](const QString &title, const QString &diff) {
            if (auto *w = windowOf(ownerGuard)) w->openDiffPane(ownerGuard, title, diff);
        };
        // The Ask row (#FEJQ, §30.5), as on Info: Activity is about this owner's own agent, so the
        // question goes to that agent's composer rather than to a second agent reading a ledger
        // about the first. The row appears only once this is set.
        view->onAskOwner = [ownerGuard](const QString &text) {
            if (ownerGuard) ownerGuard->insertInComposer(text);
        };
        // The view going — the pane closed any way at all — is what hands the rows back. The
        // owner is the context, so when the owner goes first the connection goes with it and a
        // dying pane is reprinted into by nobody.
        connect(view, &QObject::destroyed, owner, [ownerGuard, view] { if (ownerGuard) ownerGuard->detachInternals(view); });
        connect(owner, &QObject::destroyed, tool, [guard] {
            if (guard) QTimer::singleShot(0, guard.data(), [guard] { if (auto *w = windowOf(guard)) w->closePane(guard, false); });
        });
        owner->attachInternals(view);
    }

    // A saved internals pane comes back beside the pane it belonged to (buildNode), once the
    // layout is built; with no such pane, the first terminal of its tab, and with none, it goes.
    void linkRestoredInternalsPane(ToolPane *tool, const QString &ownerKey) {
        if (!tool) return;
        QWidget *page = pageOf(tool);
        const auto panes = page ? panesIn(page) : QList<Pane *>();
        Pane *owner = nullptr;
        for (Pane *pane : panes)
            if (!ownerKey.isEmpty() && pane->scrollbackId() == ownerKey) owner = pane;
        if (!owner && !panes.isEmpty()) owner = panes.first();
        if (!owner || internalsPaneOf(owner)) { closePane(tool, false); return; }
        linkInternalsPane(tool, owner);
    }

    // ----- the Test suites pane (card #7BM4, design item (b)) -----------------------------------
    // The Switchboard's tooling sibling: every test this project has, the history behind it and
    // the runs, in a splitter pane beside the board and never an overlay. It is attached exactly
    // as the Activity pane is, but to the **tab's board worker** (§31, §30.7) — the five `tests_*`
    // requests are the board helper's, not a terminal pane's — so a tab holds one of these, on the
    // same helper its Switchboard and its Options panel ask.
    static ToolPane *testSuitesPaneIn(QWidget *page) {
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::TestSuites)
                return tool;
        return nullptr;
    }
    static relay::tests::TestSuitesPane *testSuitesViewOf(ToolPane *tool) {
        return tool ? dynamic_cast<relay::tests::TestSuitesPane *>(tool->hosted()) : nullptr;
    }

    // `tests.open`, and the Tests button on the Switchboard's tool row. The tab's own pane comes
    // forward and re-asks for the inventory; there is never a second one.
    void openTestSuitesPane() {
        QWidget *page = m_tabs->currentWidget();
        if (!page) return;
        if (ToolPane *open = testSuitesPaneIn(page)) {
            if (auto *view = testSuitesViewOf(open)) view->requestList();
            setActiveLeaf(open);
            focusLeaf(open);
            updateTitles();
            return;
        }
        // Beside the Switchboard when the tab has one — this pane is the board's sibling, and the
        // two are read together — and otherwise beside whatever is active. Below it instead when
        // the anchor is too narrow to share its width, exactly as the Activity pane places itself.
        QWidget *anchor = nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) { anchor = tool; break; }
        if (!anchor) anchor = m_activeLeaf ? m_activeLeaf.data() : static_cast<QWidget *>(m_active.data());
        ToolPane *tool = createTestSuitesPane(boardWorkspaceOfTab(page));
        if (anchor) insertBeside(anchor, tool, anchor->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
        else if (page->layout()) page->layout()->addWidget(tool);
        linkTestSuitesPane(tool);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }

    ToolPane *createTestSuitesPane(const QString &cwd) {
        auto *view = new relay::tests::TestSuitesPane;
        auto *tool = new ToolPane(ToolPane::Kind::TestSuites, view, view, cwd);
        tool->setProperty("paneType", QStringLiteral("testsuites"));   // pane-type header colours
        relay::theme::polishWindow(tool);
        tool->setObjectName(QStringLiteral("pane"));
        return tool;
    }

    // The five seams of src/TestSuitesPane.h, wired to this window. Called once the pane is in a
    // page, because every one of them asks which tab it is in.
    void linkTestSuitesPane(ToolPane *tool) {
        relay::tests::TestSuitesPane *view = testSuitesViewOf(tool);
        QWidget *page = view ? pageOf(tool) : nullptr;
        if (!page) return;
        QPointer<ToolPane> guard(tool);
        // Out: `tests_list`, `tests_run`, `tests_stop`, `tests_history` on the tab's helper, which
        // is started by the first one of them (§30.7). A tab attached to no project has no board,
        // so nothing is sent and no worker is started for it: the pane keeps its "No worker is
        // attached yet" empty state rather than spawning a helper that could only refuse.
        view->onSend = [guard](const QJsonObject &request) {
            auto *w = windowOf(guard);
            if (!w) return;
            QWidget *page = w->pageOf(guard);
            if (!page || w->boardWorkspaceOfTab(page).isEmpty()) return;
            w->sendToHelper(page, request);
        };
        // In: every event of that helper. The pane keeps the `tests_*` ones and ignores the rest,
        // and `ready` — a worker that has just started or restarted — is what makes it ask again,
        // so a pane that outlived its worker fills itself back in without being touched.
        QPointer<relay::tests::TestSuitesPane> viewGuard(view);
        listenToHelper(page, view, [this, guard, viewGuard](const QJsonObject &event) {
            relay::tests::TestSuitesPane *pane = viewGuard.data();
            if (!pane) return;
            const QString type = event.value(QStringLiteral("event")).toString();
            if (type == QStringLiteral("ready")) { pane->requestList(); return; }
            // The two board writes this pane makes are answered on the same connection; they are
            // this window's, keyed by a request id the Switchboard's own prefix cannot collide with.
            if (handleTestsCardReply(guard, event)) return;
            pane->handleEvent(event);
        });
        // A test's source, at its line: the same opener a `src/Pane.h:120` in the output uses. The
        // worker sends project-relative paths, and the project is the tab's board root.
        view->onOpenFile = [guard](const QString &path, int line) {
            auto *w = windowOf(guard);
            if (!w || path.isEmpty()) return;
            const QString root = w->boardWorkspaceOfTab(w->pageOf(guard));
            w->openPath(root.isEmpty() ? path : QDir(root).absoluteFilePath(path), line, guard);
        };
        // A `#ID` in the row's cards column: reveal it in this tab's Switchboard, opening the
        // board first if the tab put it away.
        view->onOpenCard = [guard](const QString &id) {
            if (auto *w = windowOf(guard)) w->openBoardCard(id);
        };
        view->onMakeCard = [guard](const relay::tests::TestRow &row) {
            if (auto *w = windowOf(guard)) w->makeCardForTest(guard, row);
        };
        view->onAttachToCard = [guard](const relay::tests::TestRow &row) {
            if (auto *w = windowOf(guard)) w->attachTestToCard(guard, row);
        };
        view->requestList();
    }

    // A saved Test suites pane (buildNode) is wired once the page it belongs to exists.
    void linkRestoredTestSuitesPane(ToolPane *tool) {
        if (!tool) return;
        if (!pageOf(tool)) { closePane(tool, false); return; }
        linkTestSuitesPane(tool);
    }

    // ----- the Profile result pane (card #7BM4, design item (c)) --------------------------------
    // Transient, the way the diff pane is (`openDiffPane`): a profile is a moment, not a file the
    // layout should bring back, so `node()` saves nothing and one per tab is replaced by the next.
    // It is attached to the **tab's board worker**, like the Test suites pane, because
    // `profile_run` and `profile_stop` are the board helper's (§31.9).
    static ToolPane *profilePaneIn(QWidget *page) {
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Profile)
                return tool;
        return nullptr;
    }
    static relay::profile::ProfilePane *profileViewOf(ToolPane *tool) {
        return tool ? dynamic_cast<relay::profile::ProfilePane *>(tool->hosted()) : nullptr;
    }

    // The Profile button on the Switchboard's tool row: ask which target, then run it. The menu
    // is `relay::profile::showTargetMenu` so the GUI and the worker describe the four targets
    // from one list; while a profile runs it offers Stop instead of starting a second one, which
    // the worker would refuse anyway.
    void openProfileMenu(QWidget *anchor) {
        QWidget *page = m_tabs->currentWidget();
        relay::profile::ProfilePane *open = profileViewOf(profilePaneIn(page));
        const bool running = open && open->running();
        QPointer<RelayWindow> guard(this);
        relay::profile::showTargetMenu(
            anchor,
            [guard](const QString &target) { if (guard) guard->startProfile(target); },
            running,
            [guard] { if (guard) if (auto *view = profileViewOf(profilePaneIn(guard->m_tabs->currentWidget()))) view->stopRun(); });
    }

    // One `profile_run`, and the pane that will show it — opened before the request goes out, so
    // the run is visible from its first line rather than only once it ends.
    void startProfile(const QString &target) {
        QWidget *page = m_tabs->currentWidget();
        if (!page) return;
        if (boardWorkspaceOfTab(page).isEmpty()) {
            notice(QStringLiteral("This tab is not attached to a project, so there is nothing to profile."), 9000);
            return;
        }
        QJsonObject request{{QStringLiteral("type"), QStringLiteral("profile_run")},
                            {QStringLiteral("target"), target}};
        if (target == QLatin1String("build-remote")) {
            // The machine is the user's to name, never the app's: asked once, kept in the
            // settings, and offered back as the default the next time.
            QSettings settings;
            const QString remembered = settings.value(QStringLiteral("tests/remote_host")).toString();
            bool ok = false;
            const QString host = QInputDialog::getText(
                this, QStringLiteral("Build on another machine"),
                QStringLiteral("Machine to build on, as ssh names it (host, or user@host).\n"
                               "It needs passwordless ssh, cmake and ninja, and the project's build "
                               "dependencies; scripts/relay-tooling-setup checks a machine."),
                QLineEdit::Normal, remembered, &ok).trimmed();
            if (!ok || host.isEmpty()) return;
            settings.setValue(QStringLiteral("tests/remote_host"), host);
            request.insert(QStringLiteral("host"), host);
        }
        relay::profile::ProfilePane *view = openProfilePane();
        if (!view) return;
        view->startWaitingFor(target);
        updateTitles();
        sendToHelper(page, request);
    }

    relay::profile::ProfilePane *openProfilePane() {
        QWidget *page = m_tabs->currentWidget();
        if (!page) return nullptr;
        if (ToolPane *open = profilePaneIn(page)) {
            setActiveLeaf(open);
            focusLeaf(open);
            return profileViewOf(open);
        }
        QWidget *anchor = nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) { anchor = tool; break; }
        if (!anchor) anchor = m_activeLeaf ? m_activeLeaf.data() : static_cast<QWidget *>(m_active.data());
        auto *view = new relay::profile::ProfilePane;
        auto *tool = new ToolPane(ToolPane::Kind::Profile, view, view, boardWorkspaceOfTab(page));
        tool->setProperty("paneType", QStringLiteral("profile"));
        relay::theme::polishWindow(tool);
        tool->setObjectName(QStringLiteral("pane"));
        if (anchor) insertBeside(anchor, tool, anchor->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
        else if (page->layout()) page->layout()->addWidget(tool);
        linkProfilePane(tool);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
        return view;
    }

    // The four seams of src/ProfilePane.h, wired to this window.
    void linkProfilePane(ToolPane *tool) {
        relay::profile::ProfilePane *view = profileViewOf(tool);
        QWidget *page = view ? pageOf(tool) : nullptr;
        if (!page) return;
        QPointer<ToolPane> guard(tool);
        QPointer<relay::profile::ProfilePane> viewGuard(view);
        view->onSend = [guard](const QJsonObject &request) {
            auto *w = windowOf(guard);
            if (!w) return;
            QWidget *page = w->pageOf(guard);
            if (!page || w->boardWorkspaceOfTab(page).isEmpty()) return;
            w->sendToHelper(page, request);
        };
        listenToHelper(page, view, [guard, viewGuard](const QJsonObject &event) {
            auto *w = windowOf(guard);
            // The two board writes "Attach to card…" makes are answered on the same connection,
            // keyed by a request id the Switchboard's own prefix cannot collide with.
            if (w && w->handleProfileCardReply(event)) return;
            if (relay::profile::ProfilePane *pane = viewGuard.data()) {
                pane->handleEvent(event);
                if (w) w->updateTitles();
            }
        });
        // "Open flame graph": speedscope's static bundle in the system browser, from the local
        // copy `scripts/relay-tooling-setup` put in ~/.local/share. Nothing is uploaded, and the
        // app has no QtWebEngine to draw it in (docs/PROFILING.md section 4).
        view->onOpenFlameGraph = [guard](const QString &file) {
            auto *w = windowOf(guard);
            if (!w || file.isEmpty()) return;
            const QString script = dataRoot() + QStringLiteral("/scripts/relay-speedscope");
            if (!QFileInfo::exists(script)) {
                w->notice(QStringLiteral("scripts/relay-speedscope is not in this checkout."), 9000);
                return;
            }
            if (QProcess::startDetached(script, {file}))
                w->notice(QStringLiteral("Opening the flame graph in your browser…"), 5000);
            else
                w->notice(QStringLiteral("Could not start scripts/relay-speedscope."), 9000);
        };
        view->onAttachToCard = [guard](const QString &markdown, const QString &evidence) {
            if (auto *w = windowOf(guard)) w->attachProfileToCard(guard, markdown, evidence);
        };
        // The board's notice line, where a cleanup's progress goes: a profile is minutes, and the
        // person who pressed the button may be reading the board rather than the pane.
        view->onNotice = [guard](const QString &text) {
            auto *w = windowOf(guard);
            QWidget *page = w ? w->pageOf(guard) : nullptr;
            if (!page || text.isEmpty()) return;
            for (QWidget *leaf : leavesIn(page))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) {
                    tool->board()->showToolNotice(text, false);
                    return;
                }
        };
    }

    // "Attach to card…": the same card picker the Test suites pane uses, then two writes — the
    // `## Profile` block appended, and the evidence directory added to `links.evidence`, which is
    // where this repo has always kept the raw file (docs/PROFILING.md section 5).
    QString nextProfileWriteId() { return QStringLiteral("pf%1-").arg(quintptr(this), 0, 36) + QString::number(++m_profileWriteSeq); }

    void attachProfileToCard(ToolPane *tool, const QString &markdown, const QString &evidence) {
        QWidget *page = pageOf(tool);
        if (!page || markdown.isEmpty()) return;
        ToolPane *board = nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *found = dynamic_cast<ToolPane *>(leaf); found && found->board()) { board = found; break; }
        if (!board) {
            notice(QStringLiteral("Open this tab's Switchboard first: the card picker is its list of cards."), 9000);
            return;
        }
        const QString card = pickCard(board->board()->model(),
                                      QStringLiteral("Attach this profile to a card"));
        if (card.isEmpty()) return;
        const QString root = boardWorkspaceOfTab(page);
        QString relative = evidence;
        if (!root.isEmpty() && evidence.startsWith(root))
            relative = QDir(root).relativeFilePath(evidence);
        const QString id = nextProfileWriteId();
        m_profileWrites.insert(id, ProfileCardWrite{markdown, relative});
        sendToHelper(page, {{QStringLiteral("type"), QStringLiteral("board_card_get")},
                      {QStringLiteral("id"), id},
                      {QStringLiteral("card"), card}});
    }

    // The `board_card` answer to that first step: append the block and, if the directory is not
    // already there, extend `links.evidence`. Returns true when the event was one of ours.
    bool handleProfileCardReply(const QJsonObject &event) {
        const QString requestId = event.value(QStringLiteral("id")).toString();
        if (requestId.isEmpty() || !m_profileWrites.contains(requestId)) return false;
        const ProfileCardWrite pending = m_profileWrites.take(requestId);
        if (event.value(QStringLiteral("event")).toString() == QLatin1String("error")) {
            notice(QStringLiteral("The board refused that: ") + event.value(QStringLiteral("text")).toString(), 9000);
            return true;
        }
        const QString card = event.value(QStringLiteral("card_id")).toString();
        const QString hash = event.value(QStringLiteral("hash")).toString();
        QWidget *page = m_tabs->currentWidget();
        if (card.isEmpty() || hash.size() != 64 || !page) {
            notice(QStringLiteral("The card could not be read, so nothing was written."), 9000);
            return true;
        }
        QJsonObject patch{{QStringLiteral("append_section"),
                           QJsonObject{{QStringLiteral("heading"), QStringLiteral("Profile")},
                                       {QStringLiteral("text"), pending.markdown}}}};
        if (!pending.evidence.isEmpty()) {
            QJsonObject links = event.value(QStringLiteral("front")).toObject()
                                    .value(QStringLiteral("links")).toObject();
            QJsonArray paths = links.value(QStringLiteral("evidence")).toArray();
            bool seen = false;
            for (const QJsonValue &value : paths)
                if (value.toString() == pending.evidence) { seen = true; break; }
            if (!seen) {
                paths.append(pending.evidence);
                links.insert(QStringLiteral("evidence"), paths);
                patch.insert(QStringLiteral("fields"), QJsonObject{{QStringLiteral("links"), links}});
            }
        }
        sendToHelper(page, {{QStringLiteral("type"), QStringLiteral("board_update")},
                      {QStringLiteral("id"), nextProfileWriteId()},
                      {QStringLiteral("card"), card},
                      {QStringLiteral("base_hash"), hash},
                      {QStringLiteral("patch"), patch}});
        notice(QStringLiteral("Added the profile to #%1.").arg(card), 9000);
        return true;
    }

    // ----- the two board writes the Test suites pane makes (#7BM4, §31) -------------------------
    // Both are two steps, because `board_create` takes no section and `board_update` takes a
    // `base_hash`: ask for the card (or create it), then append one line to its `## Tests` against
    // the hash that answer carried. What is in flight is kept here, keyed by the request id.
    QString nextTestsWriteId() { return QStringLiteral("ts%1-").arg(quintptr(this), 0, 36) + QString::number(++m_testsWriteSeq); }

    // Where a card about a failing test is filed: the board's **bugs tab**, by its id. A tab id is
    // not the folder it writes into — this repo's `bugs` tab keeps its cards in `changes/` — and
    // the worker refuses a tab the board does not have, so the answer is read off the board this
    // tab is showing. With no bugs tab, the first work folder, which is where quick add files a
    // card too; with no board pane in the tab, `features`, the worker's own default.
    QString bugsTabOf(QWidget *page) const {
        QString first;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) {
                for (const relay::board::Tab &tab : tool->board()->model().tabs()) {
                    if (tab.folder.isEmpty() || tab.type != QStringLiteral("work")
                        || tab.id == QStringLiteral("planning"))
                        continue;
                    if (tab.id == QStringLiteral("bugs")) return tab.id;
                    if (first.isEmpty()) first = tab.id;
                }
                break;
            }
        return first.isEmpty() ? QStringLiteral("features") : first;
    }

    // One `## Tests` line, in the shape section 31.5 writes down: the invocation in backticks,
    // then the source file after an em dash when the worker knows one.
    static QString testsSectionLine(const relay::tests::TestRow &row) {
        const QString what = row.invocation.isEmpty() ? row.id : row.invocation;
        QString line = QStringLiteral("- `%1`").arg(what);
        if (!row.file.isEmpty()) line += QStringLiteral(" — ") + row.file;
        return line;
    }

    // "Make a card" on a row: a bug card whose issue text is why the row is worth a card — the
    // last failure's message, verbatim — and whose `## Tests` section already names the test, so
    // the card arrives with the thing that proves it (policy rule 6) rather than needing it added.
    void makeCardForTest(ToolPane *tool, const relay::tests::TestRow &row) {
        QWidget *page = pageOf(tool);
        if (!page || boardWorkspaceOfTab(page).isEmpty()) {
            notice(QStringLiteral("This tab is not attached to a project, so there is no board to file a card on."), 9000);
            return;
        }
        const QString kind = row.flaky ? QStringLiteral("Flaky test") : QStringLiteral("Failing test");
        QString text = QStringLiteral("%1: %2\n\n`%3`").arg(kind, row.name, row.invocation.isEmpty() ? row.id : row.invocation);
        if (row.hasLastFailure && !row.failureMessage.isEmpty())
            text += QStringLiteral("\n\nThe last failure said:\n\n%1").arg(row.failureMessage.trimmed());
        const QString id = nextTestsWriteId();
        m_testsWrites.insert(id, TestsCardWrite{QPointer<ToolPane>(tool), testsSectionLine(row),
                                                QStringLiteral("Filed a card for %1.").arg(row.name), true});
        sendToHelper(page, {{QStringLiteral("type"), QStringLiteral("board_create")},
                      {QStringLiteral("id"), id},
                      {QStringLiteral("tab"), bugsTabOf(page)},
                      {QStringLiteral("status"), QStringLiteral("inbox")},
                      {QStringLiteral("card_type"), QStringLiteral("work")},
                      {QStringLiteral("labels"), QJsonArray{QStringLiteral("bug"), QStringLiteral("tests")}},
                      {QStringLiteral("title"), QStringLiteral("%1: %2").arg(kind, row.name)},
                      {QStringLiteral("source"), QStringLiteral("the Test suites pane")},
                      {QStringLiteral("text"), text}});
    }

    // "Attach to card": the card picker — the board's own fuzzy ranking, the one the composer's
    // `#` uses (relay::board::Model::search) — and then the invocation appended to that card's
    // `## Tests`.
    void attachTestToCard(ToolPane *tool, const relay::tests::TestRow &row) {
        QWidget *page = pageOf(tool);
        if (!page) return;
        ToolPane *board = nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *found = dynamic_cast<ToolPane *>(leaf); found && found->board()) { board = found; break; }
        if (!board) {
            notice(QStringLiteral("Open this tab's Switchboard first: the card picker is its list of cards."), 9000);
            return;
        }
        const QString card = pickCard(board->board()->model(),
                                      QStringLiteral("Attach %1 to a card").arg(row.name));
        if (card.isEmpty()) return;
        const QString id = nextTestsWriteId();
        m_testsWrites.insert(id, TestsCardWrite{QPointer<ToolPane>(tool), testsSectionLine(row),
                                                QStringLiteral("Added %1 to #%2.").arg(row.name, card), false});
        sendToHelper(page, {{QStringLiteral("type"), QStringLiteral("board_card_get")},
                      {QStringLiteral("id"), id},
                      {QStringLiteral("card"), card}});
    }

    // The `#` picker as a dialog: the same ranking, filtered as you type, Enter picks. Empty when
    // the person cancelled.
    QString pickCard(const relay::board::Model &model, const QString &title) {
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        auto *layout = new QVBoxLayout(&dialog);
        auto *filter = new QLineEdit(&dialog);
        filter->setPlaceholderText(QStringLiteral("Filter by id or title"));
        layout->addWidget(filter);
        auto *list = new QListWidget(&dialog);
        list->setUniformItemSizes(true);
        list->setMinimumSize(520, 320);
        layout->addWidget(list, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        const auto fill = [&model, list](const QString &query) {
            list->clear();
            for (const relay::board::Card &card : model.search(query, 40)) {
                auto *item = new QListWidgetItem(
                    QStringLiteral("#%1  %2  ·  %3").arg(card.id, card.title, relay::board::statusTitle(card.status)), list);
                item->setData(Qt::UserRole, card.id);
            }
            if (list->count() > 0) list->setCurrentRow(0);
        };
        fill(QString());
        connect(filter, &QLineEdit::textChanged, &dialog, [fill](const QString &text) { fill(text); });
        connect(list, &QListWidget::itemActivated, &dialog, [&dialog](QListWidgetItem *) { dialog.accept(); });
        filter->setFocus(Qt::OtherFocusReason);
        relay::theme::polishWindow(&dialog);
        if (dialog.exec() != QDialog::Accepted) return {};
        QListWidgetItem *chosen = list->currentItem();
        return chosen ? chosen->data(Qt::UserRole).toString() : QString();
    }

    // The worker's answer to one of those two writes. Returns true when the event was this
    // window's, so the Test suites pane never sees a board reply it has no use for.
    bool handleTestsCardReply(const QPointer<ToolPane> &tool, const QJsonObject &event) {
        const QString requestId = event.value(QStringLiteral("id")).toString();
        if (requestId.isEmpty() || !m_testsWrites.contains(requestId)) return false;
        const TestsCardWrite pending = m_testsWrites.take(requestId);
        const QString type = event.value(QStringLiteral("event")).toString();
        if (type == QStringLiteral("error")) {
            notice(QStringLiteral("The board refused that: ") + event.value(QStringLiteral("text")).toString(), 9000);
            return true;
        }
        const QString card = event.value(QStringLiteral("card_id")).toString();
        const QString hash = event.value(QStringLiteral("hash")).toString();
        QWidget *page = tool ? pageOf(tool) : nullptr;
        // Step two: the card is there and its hash is in hand, so the `## Tests` line goes on.
        if (!pending.section.isEmpty() && !card.isEmpty() && hash.size() == 64 && page) {
            const QString next = nextTestsWriteId();
            m_testsWrites.insert(next, TestsCardWrite{pending.pane, QString(), pending.note, pending.reveal});
            sendToHelper(page, {{QStringLiteral("type"), QStringLiteral("board_update")},
                          {QStringLiteral("id"), next},
                          {QStringLiteral("card"), card},
                          {QStringLiteral("base_hash"), hash},
                          {QStringLiteral("patch"),
                           QJsonObject{{QStringLiteral("append_section"),
                                        QJsonObject{{QStringLiteral("heading"), QStringLiteral("Tests")},
                                                    {QStringLiteral("text"), pending.section}}}}}});
            return true;
        }
        if (!pending.note.isEmpty()) notice(pending.note, 9000);
        // The card's `## Tests` names one more test, so the pane's `cards` column is a fold out of
        // date: one fresh inventory puts it right.
        if (auto *view = testSuitesViewOf(pending.pane.data())) view->requestList();
        if (pending.reveal && !card.isEmpty()) openBoardCard(card);
        return true;
    }


    // One unified diff, in a pane beside the terminal: what a write or an edit of more than 12
    // changed lines opens (#TK9C, protocol § 23.6). A splitter pane, never an overlay — and one
    // per tab: the next big diff replaces what the last one showed, so a long turn does not leave
    // a row of panes behind.
    // Returns the view it showed the diff in, for a caller that has something to put in its
    // header — a guest's `openDiff` hangs its Accept / Reject there (26.5) — or nullptr when there
    // was nothing to show and no pane was opened.
    relay::DiffView *openDiffPane(Pane *owner, const QString &title, const QString &unifiedDiff) {
        QWidget *page = pageOf(owner);
        if (!page || unifiedDiff.isEmpty()) return nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->diff()) {
                tool->diff()->setDiff(title, unifiedDiff);
                setActiveLeaf(tool); focusLeaf(tool); updateTitles(); return tool->diff();
            }
        auto *view = new relay::DiffView;
        view->setDiff(title, unifiedDiff);
        auto *tool = new ToolPane(view, owner->cwd());
        tool->setProperty("paneType", QStringLiteral("diff"));
        relay::theme::polishWindow(tool);
        tool->setObjectName(QStringLiteral("pane"));
        insertBeside(owner, tool, Qt::Horizontal, false);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
        return view;
    }
    // ----- the session manager pane and the ⓘ pane (cards #R6J0, #Y63Z) ------------------------
    // One session manager per tab, bound to the pane that opened it: its queries go to that pane's
    // worker and Enter resumes there. /resume, /conversations, Ctrl+Shift+Y (agent.resume) and
    // conversations.open all come here through Pane::openConversations.
    using SessionsTabFactory = std::function<QWidget *(RelayWindow *window)>;
    struct SessionsTab { QString id, label; SessionsTabFactory make; };
    static QList<SessionsTab> &sessionsTabs() { static QList<SessionsTab> tabs; return tabs; }
    // Another feature's tab in every session manager opened from now on (e.g. "closed": recently
    // closed windows, tabs and panes). The factory builds the widget for one pane in `window`.
    static void addSessionsTab(const QString &id, const QString &label, SessionsTabFactory make) {
        for (const SessionsTab &tab : sessionsTabs()) if (tab.id == id) return;
        sessionsTabs().append({id, label, std::move(make)});
    }

    static ToolPane *sessionsPaneIn(QWidget *page) {
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Sessions) return tool;
        return nullptr;
    }
    static relay::conversations::SessionManager *sessionsViewOf(ToolPane *tool) {
        return tool ? dynamic_cast<relay::conversations::SessionManager *>(tool->hosted()) : nullptr;
    }

    // Open this tab's session manager on `tab` ("" is the session list), bound to the active pane.
    void openSessions(const QString &tab = QString(), const QString &query = QString()) {
        Pane *owner = m_active;
        if (!owner) { const auto panes = panesIn(m_tabs->currentWidget()); owner = panes.isEmpty() ? nullptr : panes.first(); }
        if (owner) openSessionsFor(owner, query, tab);
    }

    // The key is a toggle: Ctrl+Shift+Y opens the session manager, and pressing it again with the
    // manager focused closes it, the way Esc does (owner, 2026-09-20; Warp's Ctrl+Shift+Y closes
    // its conversations menu too). Pressed while the focus is elsewhere it brings the open manager
    // forward and rebinds it to the pane that asked, rather than closing a pane the user is not
    // looking at. `/resume` and `/conversations` are openers, not toggles: they are typed in the
    // prompt box of a pane, which is never the manager.
    void toggleSessionsPane(Pane *owner) {
        if (!owner) return;
        ToolPane *tool = sessionsPaneIn(pageOf(owner));
        if (tool && (m_activeLeaf == tool || tool->isAncestorOf(QApplication::focusWidget()))) {
            closeSessionsPane(tool, owner);
            return;
        }
        owner->openResume();   // opens it, or brings the open one forward and rebinds it here
    }

    void openSessionsFor(Pane *owner, const QString &query, const QString &tab = QString()) {
        QWidget *page = pageOf(owner);
        if (!page) return;
        ToolPane *tool = sessionsPaneIn(page);
        relay::conversations::SessionManager *view = sessionsViewOf(tool);
        const bool fresh = !tool;
        if (!tool) {
            view = new relay::conversations::SessionManager;
            tool = new ToolPane(ToolPane::Kind::Sessions, view, view, owner->cwd());
            tool->setProperty("paneType", QStringLiteral("sessions"));
            relay::theme::polishWindow(tool);
            for (const SessionsTab &extra : sessionsTabs())
                if (QWidget *widget = extra.make ? extra.make(this) : nullptr) view->addTab(extra.id, extra.label, widget);
            // The agent console at the foot of the pane (#AGNT step 5, replacing #FEJQ's panel):
            // the window makes it on first expand, the row stays this pane's. A `session:` link is
            // the manager's own business; an `option:` one is the console's, which opens Options
            // on the row exactly as a terminal pane's transcript does.
            wireConsoleHost(view, tool, QStringLiteral("sessions.ask"));
            insertBeside(owner, tool, owner->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
        }
        // What is open and what was closed is the window's knowledge, not the list's: it is pushed
        // in here, and again whenever the recently-closed list changes, so the "open" and
        // "closed 5 min ago" tags on the rows stay true (card #R6J0).
        QPointer<relay::conversations::SessionManager> viewGuard(view);
        QPointer<RelayWindow> windowGuard(this);
        auto feed = [viewGuard, windowGuard] {
            if (!viewGuard || !windowGuard) return;
            viewGuard->setOpenSessions(windowGuard->openSessionIds());
            QHash<QString, QPair<QString, qint64>> closed;
            const QList<relay::closed::Record> records = windowGuard->m_manager->closedRecords();
            for (const relay::closed::Record &record : records)
                for (const QString &sessionId : relay::closed::sessionIds(record))
                    if (!closed.contains(sessionId) || closed.value(sessionId).second < record.closedAt)
                        closed.insert(sessionId, {record.id, record.closedAt});
            viewGuard->setClosedSessions(closed);
        };
        if (fresh) m_manager->watchClosed(view, feed);
        feed();
        view->setProject(QFileInfo(owner->workspace().isEmpty() ? owner->cwd() : owner->workspace()).fileName());
        // The "Project" chooser (#916B): the projects Relay knows, most recently attached first.
        {
            QList<QPair<QString, QString>> known;
            for (const relay::projects::Record &record : m_manager->projects().knownProjects())
                known.append({record.name.isEmpty() ? relay::projects::nameFor(record.path) : record.name, record.path});
            view->setKnownProjects(known);
        }
        view->onReopenClosed = [windowGuard](const QString &closedId) {
            if (windowGuard) windowGuard->m_manager->restoreClosed(windowGuard, closedId);
        };
        // Bound to the pane that asked, every time: Enter resumes where /resume was typed.
        owner->bindSessionManager(view);
        QPointer<ToolPane> guard(tool);
        QPointer<Pane> ownerGuard(owner);
        auto close = [guard, ownerGuard] {
            auto *w = windowOf(guard);
            if (!w) return;
            w->closeSessionsPane(guard, ownerGuard);
        };
        view->onClose = close;
        auto resume = view->onResume;
        // Close first: a resume may focus another pane (the one that already has the session),
        // and closing afterwards would take the focus back.
        view->onResume = [resume, close](const QJsonObject &item, bool newPane) { close(); if (resume) resume(item, newPane); };
        view->onOpenInfo = [ownerGuard, guard](const QJsonObject &item) {
            auto *w = windowOf(guard);
            if (!w || !ownerGuard) return;
            w->openInfoPane(ownerGuard, item.value(QStringLiteral("session_id")).toString(),
                            item.value(QStringLiteral("session_dir")).toString());
        };
        view->showTab(tab);
        if (!query.isEmpty()) view->setQuery(query);
        view->refresh();
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }

    void closeSessionsPane(ToolPane *tool, Pane *back) {
        if (!tool) return;
        QWidget *page = pageOf(tool);
        if (page && leavesIn(page).size() <= 1 && m_tabs->count() <= 1) {
            try { insertBeside(tool, createPane(paneNode(m_manager->workspace())), Qt::Horizontal, true); }
            catch (const std::exception &error) { notice(QString::fromUtf8(error.what())); }
        }
        closePane(tool, false);
        if (back && back->window() == this) { setActiveLeaf(back); focusLeaf(back); }
    }

    // ----- the project picker (card #916B, src/ProjectPicker.h) -------------------------------
    // What opens when a Switchboard is reached for in a tab that has no project and the pane's
    // directory is no candidate for one (~/Downloads, an admin folder): the projects Relay knows,
    // with "Initialize new project here" on top. One per tab, beside the pane that asked, closed
    // by Esc or by the answer. A pane, not an overlay (owner, 2026-09-18).
    // Hosted as a `PaneView` like the ⓘ view (ToolPane::Kind::Info), told apart by what it hosts;
    // its `paneType` is "projects", which is what the band and the tab lights go by.
    static ToolPane *projectPickerIn(QWidget *page) {
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Info
                && dynamic_cast<relay::projects::ProjectPicker *>(tool->hosted()))
                return tool;
        return nullptr;
    }

    // `heldCard` is a `/card <text>` typed in a tab with no project: it is filed the moment a
    // project is chosen, and never lost. Empty when the Switchboard itself was reached for.
    void openProjectPicker(Pane *owner, const QString &heldCard) {
        if (!owner) { notice(QStringLiteral("No pane to look from, so there is no project to pick for."), 4000); return; }
        QWidget *page = pageOf(owner);
        if (!page) return;
        ToolPane *tool = projectPickerIn(page);
        auto *view = tool ? dynamic_cast<relay::projects::ProjectPicker *>(tool->hosted()) : nullptr;
        if (!tool) {
            view = new relay::projects::ProjectPicker;
            tool = new ToolPane(ToolPane::Kind::Info, view, view, owner->cwd());
            tool->setProperty("paneType", QStringLiteral("projects"));
            relay::theme::polishWindow(tool);
            insertBeside(owner, tool, owner->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
        }
        // Fed every time it opens: the registry may have grown since, and the pane may have moved.
        view->setProjects(m_manager->projects().knownProjects());
        view->setDefaultProject(relay::projects::defaultProject());
        view->setHere(owner->cwd());
        QPointer<ToolPane> guard(tool);
        QPointer<Pane> ownerGuard(owner);
        auto close = [guard, ownerGuard] {
            if (auto *w = windowOf(guard)) w->closeProjectPicker(guard, ownerGuard);
        };
        view->onClose = close;
        // The captures are copied out before the pane closes: closing is deleteLater, but the
        // lambda's own storage lives in the view all the same.
        view->onPick = [guard, ownerGuard, heldCard, close](const QString &path) {
            auto *w = windowOf(guard);
            QPointer<Pane> pane = ownerGuard;
            const QString card = heldCard;
            if (!w || !pane) return;
            close();
            w->attachTab(w->pageOf(pane), path, QString::fromLatin1(relay::projects::kReasonPicker));
            w->afterProjectPicked(pane, card);
        };
        view->onInitHere = [guard, ownerGuard, heldCard, close] {
            auto *w = windowOf(guard);
            QPointer<Pane> pane = ownerGuard;
            const QString card = heldCard;
            if (!w || !pane) return;
            close();
            w->setActiveLeaf(pane);
            pane->initProjectHere(card);
        };
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }

    void closeProjectPicker(ToolPane *tool, Pane *back) {
        if (!tool) return;
        QWidget *page = pageOf(tool);
        if (page && leavesIn(page).size() <= 1 && m_tabs->count() <= 1) {
            try { insertBeside(tool, createPane(paneNode(m_manager->workspace())), Qt::Horizontal, true); }
            catch (const std::exception &error) { notice(QString::fromUtf8(error.what())); }
        }
        closePane(tool, false);
        if (back && back->window() == this) { setActiveLeaf(back); focusLeaf(back); }
    }

    // The tab is attached now. A held card goes to the board; otherwise the Switchboard the user
    // reached for opens — or, for a known project that has no board yet, the one-time init
    // question is asked about it (its yes opens the Switchboard).
    void afterProjectPicked(Pane *owner, const QString &heldCard) {
        if (!owner) return;
        setActiveLeaf(owner);
        if (!heldCard.isEmpty()) { owner->fileCard(heldCard); return; }
        if (relay::projects::boardDirOf(tabProject(pageOf(owner))).isEmpty()) {
            QString why;
            if (!owner->askProjectInit(relay::projectinit::Trigger::Switchboard, QString(), &why) && !why.isEmpty())
                notice(why);
            return;
        }
        toggleBoardPane();
    }

    // A session already open in some pane of any window: that pane, else null.
    static Pane *paneWithSession(const QString &sessionId, const QString &sessionDir, const Pane *except) {
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *w = dynamic_cast<RelayWindow *>(top))
                for (Pane *pane : w->allPanes())
                    if (pane != except && pane->sessionId() == sessionId
                        && (sessionDir.isEmpty() || pane->sessionDir().isEmpty() || pane->sessionDir() == sessionDir))
                        return pane;
        return nullptr;
    }

    // The ⓘ pane beside `owner`: its own live session, or a saved session / one thread when named.
    // One per owner pane; opening it again brings it forward and starts over.
    ToolPane *infoPaneOf(Pane *owner) {
        QWidget *page = pageOf(owner);
        if (!page) return nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Info
                && tool->property("infoOwner").value<QObject *>() == owner)
                return tool;
        return nullptr;
    }

    void openInfoPane(Pane *owner, const QString &sessionId = QString(), const QString &sessionDir = QString(),
                      const QString &threadId = QString(), const QString &threadOwner = QString()) {
        if (!owner) return;
        ToolPane *tool = infoPaneOf(owner);
        relay::sessioninfo::InfoView *view = tool ? dynamic_cast<relay::sessioninfo::InfoView *>(tool->hosted()) : nullptr;
        if (!tool) {
            view = new relay::sessioninfo::InfoView;
            tool = new ToolPane(ToolPane::Kind::Info, view, view, owner->cwd());
            tool->setProperty("paneType", QStringLiteral("info"));
            tool->setProperty("infoOwner", QVariant::fromValue<QObject *>(owner));
            relay::theme::polishWindow(tool);
            QPointer<ToolPane> guard(tool);
            QPointer<Pane> ownerGuard(owner);
            view->onClose = [guard, ownerGuard] {
                auto *w = windowOf(guard);
                if (!w) return;
                w->closePane(guard, false);
                if (ownerGuard && ownerGuard->window() == w) { w->setActiveLeaf(ownerGuard); focusLeaf(ownerGuard); }
            };
            view->onTitleChanged = [guard] { if (auto *w = windowOf(guard)) w->updateTitles(); };
            // The Ask row (#FEJQ, §30.5): Info has no helper agent of its own, because the pane is
            // about the *owner's* agent and that agent has the read tools over its own session. So
            // the row prefills the owner's composer and focuses it, and the row draws itself only
            // when this callback is set.
            view->onAskOwner = [ownerGuard](const QString &text) {
                if (ownerGuard) ownerGuard->insertInComposer(text);
            };
            // Closing the owner takes its ⓘ pane with it: nothing else can answer its links.
            connect(owner, &QObject::destroyed, tool, [guard] { if (auto *w = windowOf(guard)) w->closePane(guard, false); });
            insertBeside(owner, tool, owner->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
        }
        owner->bindInfoView(view);
        if (!threadId.isEmpty()) view->showThread(threadId, sessionDir, threadOwner);
        else if (!sessionId.isEmpty()) view->showSession(sessionId, sessionDir);
        else view->showLiveSession();
        if (QWidget *page = pageOf(tool)) m_tabs->setCurrentWidget(page);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }

    // A signal thread's own history (#AQ6X phase 3), from its notification's button or from the
    // chip on the signal it claimed. It is a subagent thread of the board worker, so it opens in
    // the ⓘ view exactly as one of the pane agent's own threads does — `openInfoPane` with the
    // thread id and its owner session, which is what `SessionManager::onOpenThread` passes too.
    // The active pane hosts it: a thread is not a pane, so there is no pane of its own to put it
    // beside, and the one the user is looking at is the one they came from.
    void openSignalThread(const QString &threadId, const QString &ownerSession) {
        if (threadId.isEmpty()) return;
        if (!m_active) {
            notice(QStringLiteral("No pane to show the thread in."), 4000);
            return;
        }
        openInfoPane(m_active, QString(), QString(), threadId, ownerSession);
    }

    // ----- the first-launch approvals pane (card #K2FV, src/ApprovalsPane.h) --------------------
    // One screen beside a pane, whenever a configure lands in an installation whose choice is
    // unanswered: allow everything — the recommendation — or the cautious checklist. A pane,
    // not a dialog, and undismissable in itself, because a default nobody picked is not a
    // choice; the chrome's × still closes it, and the next configure brings it back. Options
    // › Security's "Show it again" raises the same pane on demand.
    static ToolPane *approvalsPaneIn(QWidget *page) {
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf);
                tool && tool->property("paneType").toString() == QStringLiteral("approvals"))
                return tool;
        return nullptr;
    }

    void openApprovalsPane(Pane *owner) {
        if (!owner) return;
        QWidget *page = pageOf(owner);
        if (!page) return;
        ToolPane *tool = approvalsPaneIn(page);
        if (!tool) {
            auto *view = new relay::ApprovalsView;
            // Kind::Info, but found by its paneType rather than an owner property (that is
            // infoPaneOf's match), so the ⓘ pane beside the same pane is untouched, and an
            // unknown paneType styles this header as a brass tool pane titled "Approvals".
            tool = new ToolPane(ToolPane::Kind::Info, view, view, owner->cwd());
            tool->setProperty("paneType", QStringLiteral("approvals"));
            relay::theme::polishWindow(tool);
            tool->setObjectName(QStringLiteral("pane"));
            QPointer<ToolPane> guard(tool);
            QPointer<Pane> ownerGuard(owner);
            // What the buttons write is the window's knowledge, not the view's — the ⓘ pane's
            // links are wired the same way. Both write both keys; the shared tail is below.
            view->onAllowEverything = [guard, ownerGuard] {
                auto *w = windowOf(guard);
                if (!w) return;
                QSettings settings;
                settings.setValue(QStringLiteral("security/approvals_ask"), QStringList{});
                settings.setValue(QStringLiteral("security/approvals_chosen"), true);
                w->approvalsAnswered(guard, ownerGuard, false);
            };
            view->onChooseChecklist = [guard, ownerGuard] {
                auto *w = windowOf(guard);
                if (!w) return;
                QSettings settings;
                settings.setValue(QStringLiteral("security/approvals_ask"), approvalsCautious());
                settings.setValue(QStringLiteral("security/approvals_chosen"), true);
                w->approvalsAnswered(guard, ownerGuard, true);
            };
            // The owner going takes the question with it; it reappears beside wherever the
            // conversation is resumed and configured again.
            connect(owner, &QObject::destroyed, tool, [guard] { if (auto *w = windowOf(guard)) w->closePane(guard, false); });
            insertBeside(owner, tool, owner->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
        }
        if (QWidget *own = pageOf(tool)) m_tabs->setCurrentWidget(own);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }

    // Either button: the two keys are already written, so what is left is to make the answer
    // real — every pane's worker hears the new list, the Security page redraws its ticks — and
    // to take the pane that asked away again. The checklist answer lands the owner in Options
    // › Security, where the same rows live on; the other lands the keyboard back with the owner.
    void approvalsAnswered(ToolPane *tool, Pane *owner, bool checklist) {
        for (Pane *pane : allPanes()) pane->agentOptionsChanged(QStringLiteral("security/approvals_ask"));
        refreshSettingsPanes();
        QPointer<ToolPane> guard(tool);
        if (guard)
            if (auto *w = windowOf(guard)) w->closePane(guard, false);
        QPointer<Pane> back(owner);
        auto *w = windowOf(back);
        if (!w || !back) return;
        w->setActiveLeaf(back);
        if (checklist) w->openSettingsPane(relay::SettingsPane::Mode::Options, QStringLiteral("security"));
        else w->focusLeaf(back);
    }

    // ----- the Sharing pane (#W5N2, docs/REMOTE-PROTOCOL.md section 10) ------------------------
    // One per tab, beside the pane it was opened from. It shows every share this desktop has, so
    // a second one would only ever repeat the first.
    static ToolPane *sharingPaneIn(QWidget *page) {
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Sharing)
                return tool;
        return nullptr;
    }

    Pane *paneWithToken(const QString &token) const {
        for (Pane *pane : allPanes()) if (pane->sessionToken() == token) return pane;
        return nullptr;
    }

    // `focus` is false when a knock opened this by itself: the pane appears and the bell rings,
    // but the keyboard stays exactly where the owner left it (section 10.5, and the reason the
    // pairing dialog puts Refuse on the default button).
    ToolPane *openSharingPane(Pane *owner, bool focus) {
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
            QPointer<Pane> ownerGuard(owner);
            view->onTitleChanged = [guard] { if (auto *w = windowOf(guard)) w->updateTitles(); };
            view->onClose = [guard, ownerGuard] {
                auto *w = windowOf(guard);
                if (!w) return;
                w->closePane(guard, false);
                if (ownerGuard && ownerGuard->window() == w) { w->setActiveLeaf(ownerGuard); focusLeaf(ownerGuard); }
            };
            relay::RemoteShare &share = relay::RemoteShare::instance();
            view->onKnockAnswer = [&share](const QString &participant, bool admit, const QString &role) {
                share.answerKnock(participant, admit, role);
            };
            view->onControlAnswer = [&share](const QString &pane, const QString &participant, bool grant) {
                share.answerControl(pane, participant, grant);
            };
            view->onPromptAnswer = [&share](const QString &promptId, bool approve) {
                share.answerPrompt(promptId, approve);
            };
            view->onRoleSet = [&share](const QString &participant, const QString &role) {
                share.setRole(participant, role);
            };
            view->onRemove = [&share](const QString &participant) { share.removeParticipant(participant); };
            view->onRevokeInvite = [&share](const QString &inviteId) { share.revokeInvite(inviteId); };
            view->onPause = [&share](const QString &pane, bool on) { share.pauseShare(pane, on); };
            view->onEndShare = [&share](const QString &pane) { share.endShare(pane); };
            view->onOptions = [&share](const QString &pane, bool immediate, bool present) {
                share.setShareOptions(pane, immediate, present);
            };
            // One more link for this share: the dialog, which is also where the QR is.
            view->onInvite = [guard](const QString &pane) {
                auto *w = windowOf(guard);
                if (!w) return;
                Pane *target = w->paneWithToken(pane);
                if (target) target->toggleShare();
            };
            insertBeside(owner, tool, owner->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
        }
        if (view) {
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

    // Relay-to-Relay (src/RemotePane.h): pair with the other desktop, pick one of its panes, and
    // it opens beside the active pane. Transient like the other hosted views: not saved.
    // "Join a shared session" (owner, 2026-09-18): /join BQRT, /connect BQRT, the palette, or the
    // plug at the top right. The host's panes open here as a guest's, in a tab of their own, and a
    // pane the host adds later — a tab shared whole — opens beside the last one.
    void joinSharedSession(const QString &code = QString()) {
        QPointer<RelayWindow> self(this);
        auto last = std::make_shared<QPointer<ToolPane>>();
        relay::JoinDialog::open(this, code, [self, last](relay::RemotePane *view, bool first) {
            if (!self) { delete view; return; }
            auto *tool = new ToolPane(ToolPane::Kind::Info, view, view, self->activeCwd());
            tool->setProperty("paneType", QStringLiteral("shared"));
            // The band names the pane it shows, not just its kind: a joined tab holds several.
            const auto label = [view] {
                const QString title = view->paneTitle();
                return title.isEmpty() ? QStringLiteral("Shared pane") : title;
            };
            tool->setProperty("paneLabel", label());
            relay::theme::polishWindow(tool);
            QPointer<ToolPane> guard(tool);
            view->onTitleChanged = [guard, label] {
                if (!guard) return;
                guard->setProperty("paneLabel", label());
                if (auto *w = windowOf(guard)) w->updateTitles();
            };
            RelayWindow *w = (!first && *last) ? windowOf(last->data()) : nullptr;
            if (w) {
                w->insertBeside(last->data(), tool, Qt::Horizontal, false);
            } else {
                w = self.data();
                auto *page = new QWidget;
                auto *layout = new QVBoxLayout(page);
                layout->setContentsMargins(0, 0, 0, 0);
                layout->addWidget(tool);
                w->m_tabs->insertTab(w->m_tabs->currentIndex() + 1, page, QString());
                w->m_tabs->setCurrentWidget(page);
            }
            *last = tool;
            w->setActiveLeaf(tool);
            focusLeaf(tool);
            w->updateTitles();
        });
    }

    // Which releases /update offers (owner decision, 2026-09-19): "all" is every published
    // release, betas included — what Relay has always done, and what a preview user is following —
    // and "stable" leaves GitHub's prereleases out. The updater takes it as --channel and picks the
    // *highest* version in that channel, not the one GitHub happens to list first.
    static QString updateChannel() {
        const QString value = QSettings().value(QStringLiteral("update/channel")).toString();
        return value == QStringLiteral("stable") ? value : QStringLiteral("all");
    }

    // /update and the palette's Update action: scripts/relay-update.py fetches the newest GitHub
    // release's .deb for this distribution and architecture, checks it against the release's
    // SHA256SUMS and installs it with pkexec (the password dialog is polkit's, never a prompt the
    // app could read). Every line it prints becomes the window's notice. When it finishes with the
    // UPDATED marker, Relay restarts itself: the new binary is started first, then the windows
    // close through their ordinary path so layout and scrollback are saved for it to reopen.
    void updateApp() {
        if (m_updateProcess) { notice(QStringLiteral("An update is already running.")); return; }
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        const QString script = dataRoot() + QStringLiteral("/scripts/relay-update.py");
        if (python.isEmpty() || !QFileInfo::exists(script)) {
            notice(QStringLiteral("The updater is missing: %1").arg(script));
            return;
        }
        auto *process = new QProcess(this);
        m_updateProcess = process;
        m_updateOutput.clear();
        m_updateInstalled = false;
        connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
            m_updateOutput += process->readAllStandardOutput();
            int cut;
            while ((cut = m_updateOutput.indexOf('\n')) >= 0) {   // whole lines only; the rest waits for its newline
                const QString line = QString::fromUtf8(m_updateOutput.left(cut)).trimmed();
                m_updateOutput.remove(0, cut + 1);
                if (line.startsWith(QStringLiteral("UPDATED ")))
                    m_updateInstalled = true;
                else if (!line.isEmpty())
                    notice(line, 20000);
            }
        });
        connect(process, &QProcess::readyReadStandardError, this, [this, process] {
            // The script prints its errors as notice lines; what lands here is the unexpected
            // (a traceback), better shown than swallowed.
            const QString text = QString::fromUtf8(process->readAllStandardError()).simplified();
            if (!text.isEmpty()) notice(text, 20000);
        });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) notice(QStringLiteral("The updater could not start."));
            m_updateProcess = nullptr;
            process->deleteLater();
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process](int code, QProcess::ExitStatus) {
            m_updateProcess = nullptr;
            process->deleteLater();
            if (code != 0 || !m_updateInstalled) return;
            notice(QStringLiteral("Restarting Relay…"), 4000);
            // The running process keeps its inode, so the path can be started before it closes;
            // the arguments this instance was given (a --workspace, say) are the ones to keep.
            QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                    QCoreApplication::arguments().mid(1));
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *window = dynamic_cast<RelayWindow *>(widget)) {
                    window->m_confirmedClose = true;   // /update was asked for; no close dialog on top of it
                    window->close();
                }
        });
        process->start(python, {script, QStringLiteral("install"),
                                QStringLiteral("--channel"), updateChannel()});
        notice(QStringLiteral("Checking GitHub for the latest Relay…"), 20000);
    }

    void openSharedPaneDialog() {
        QPointer<RelayWindow> self(this);
        relay::RemotePaneDialog::open(this, [self](relay::RemotePane *view) {
            if (!self || !self->m_activeLeaf) { delete view; return; }
            auto *tool = new ToolPane(ToolPane::Kind::Info, view, view, self->activeCwd());
            tool->setProperty("paneType", QStringLiteral("shared"));
            // The band names the pane it shows, not just its kind: a joined tab holds several.
            const auto label = [view] {
                const QString title = view->paneTitle();
                return title.isEmpty() ? QStringLiteral("Shared pane") : title;
            };
            tool->setProperty("paneLabel", label());
            relay::theme::polishWindow(tool);
            QPointer<ToolPane> guard(tool);
            view->onTitleChanged = [guard, label] {
                if (!guard) return;
                guard->setProperty("paneLabel", label());
                if (auto *w = windowOf(guard)) w->updateTitles();
            };
            self->insertBeside(self->m_activeLeaf, tool, self->m_activeLeaf->width() >= 900 ? Qt::Horizontal : Qt::Vertical, false);
            self->setActiveLeaf(tool);
            focusLeaf(tool);
            self->updateTitles();
        });
    }

    // Every Sharing pane in this window. `onlyClocks` is the one-second tick: it moves the
    // countdowns without rebuilding the rows under the owner's fingers.
    void refreshSharingPanes(bool onlyClocks) {
        for (int i = 0; i < m_tabs->count(); ++i)
            for (QWidget *leaf : leavesIn(m_tabs->widget(i)))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Sharing)
                    if (auto *view = dynamic_cast<relay::sharing::SharingView *>(tool->hosted())) {
                        if (onlyClocks) view->tick(); else view->refresh();
                    }
        if (!onlyClocks) for (Pane *pane : allPanes()) pane->updateShareChip();
    }

    // ----- which project a tab is attached to (card #JN7X, src/Projects.h) ---------------------
    //
    // The owner's model. **A tab is attached to at most one project, and starts attached to
    // none.** Unattached is the normal, quiet state — no chip, no offers, and the tab's panes get
    // no board tools and no board policy in their prompt — because most of the time the terminal
    // is standing in ~/Downloads or an admin folder and there is no project to talk about.
    //
    // A pane's *candidate* project is derived fresh from its live terminal directory whenever it
    // is needed (`candidateProject()`) and is never cached on the pane: a candidate is an offer,
    // not an attachment. **Typing in the terminal never attaches.** Only an explicit project
    // action does — opening the Switchboard, `/card`, picking a card with `#`, executing a card —
    // and each of those is one of `projects::kReason*`. Attachment is sticky until it is
    // detached, and a tab never switches project silently: a pane that has `cd`-ed into another
    // checkout says so ("This tab's Switchboard is A; … belongs to B") instead of re-pointing.
    //
    // `attachTab` is the **only** writer of m_tabProject, the only caller of `Registry::remember`
    // and the only place the tab's panes are re-pointed, so there is one answer to "how did this
    // tab get a project" and one place to change what attaching does.
    void attachTab(QWidget *page, const QString &project, const QString &reason) {
        if (!page || project.isEmpty() || m_tabs->indexOf(page) < 0) return;
        const QString normalized = relay::projects::normalize(project);
        if (normalized.isEmpty() || m_tabProject.value(page) == normalized) return;
        m_tabProject.insert(page, normalized);
        // The registry record: why this project became known, and where its board is if it has
        // one. A project with no board yet is remembered all the same — the tab is attached to it
        // — with `board: none` until the init question is answered.
        const QString dir = relay::projects::boardDirOf(normalized);
        m_manager->projects().remember(
            normalized, reason,
            QString::fromLatin1(dir.isEmpty() ? relay::projects::kBoardNone : relay::projects::kBoardRepo), dir);
        repointTabPanes(page);
        m_manager->scheduleSave();
        updateTitles();   // the tab's chip (#916B)
    }

    // "Detach this tab from <project>". Nothing is closed and nothing is written: an open
    // Switchboard pane stays open showing that board, and the tab's panes simply lose the card
    // tools on the next `set_board`.
    void detachTab(QWidget *page) {
        if (!page || !m_tabProject.contains(page)) return;
        const QString was = m_tabProject.take(page);
        repointTabPanes(page);
        m_manager->scheduleSave();
        updateTitles();   // the tab label follows the attachment (it names the project)
        statusBar()->showMessage(QStringLiteral("This tab is no longer attached to %1.")
                                     .arg(relay::projects::nameFor(was)), 9000);
    }

    // The project this tab is attached to, or an empty string. The one reader.
    QString tabProject(QWidget *page) const { return m_tabProject.value(page); }

    // The active pane's candidate project, derived fresh from its live terminal directory.
    // Deliberately not its `workspace()`, which is frozen when the pane is made and inherited
    // from the directory Relay was launched in — the thing that made one project's board appear
    // in every pane of every window (#JN7X).
    QString candidateProject() const {
        return m_active ? relay::projects::candidateFor(m_active->cwd()) : QString();
    }

    // ----- Switchboard (docs/SWITCHBOARD-DESIGN.md 4, protocol 17) -----------------------------
    // Ctrl+Shift+S: open the Switchboard beside the anchor, focus the one this tab already has,
    // or, pressed on it, go back to the last terminal pane.
    void toggleBoardPane() {
        QWidget *page = m_tabs->currentWidget();
        // Pressed while the Switchboard is the pane in focus: close it through the pane's own
        // close path (closeToolPane — the title-bar button and Esc use it too). It used to hand
        // focus back and leave the pane open.
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data()); tool && tool->board()) {
            m_boardClosedByToggle = true;
            closeToolPane(tool);
            m_boardClosedByToggle = false;
            return;
        }
        const QString workspace = boardWorkspace();
        const QString from = boardSearchRoot();
        // A tab holds one project's board (owner's rule: one project per tab), so an existing one
        // is what this key shows — but never silently as if it were the active pane's. When the
        // two disagree, say whose board is on screen: dropping a card on it writes into that
        // project's board folder, not the one the pane is standing in.
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) {
                setActiveLeaf(tool); focusLeaf(tool);
                const QString shown = tool->board()->workspace();
                if (!from.isEmpty() && shown != workspace) {
                    statusBar()->showMessage(
                        workspace.isEmpty()
                            ? QStringLiteral("This tab's Switchboard is %1; %2 has no Switchboard of its own.")
                                  .arg(shown, from)
                            : QStringLiteral("This tab's Switchboard is %1; %2 belongs to %3.")
                                  .arg(shown, from, workspace),
                        9000);
                }
                return;
            }
        if (workspace.isEmpty()) {
            // Trigger (3) of the init question: reaching for the Switchboard in a project that has
            // none is the clearest moment to offer one (protocol 19.12). Still nothing is created
            // before the yes, and a project the user has already declined gets the quiet line below
            // instead — src/ProjectInit.h decides, not this key.
            if (m_active && m_active->askProjectInit(relay::projectinit::Trigger::Switchboard)) return;
            const QString candidate = candidateProject();
            // No candidate at all — ~/Downloads, an admin folder — and the key still means "I want
            // a Switchboard": the project picker (#916B) offers the projects Relay knows, or makes
            // this directory one. Nothing is created before the user picks.
            if (candidate.isEmpty() && !from.isEmpty()) { openProjectPicker(m_active, QString()); return; }
            statusBar()->showMessage(
                from.isEmpty()
                    ? QStringLiteral("No pane to look from, so there is no Switchboard to open.")
                    : candidate.isEmpty()
                          ? QStringLiteral("No Switchboard for %1: no switchboard/board.yaml or "
                                           "issues/board.yaml there or in any directory above it.").arg(from)
                          : QStringLiteral("%1 has no Switchboard yet.")
                                .arg(relay::projects::nameFor(candidate)),
                9000);
            return;
        }
        QWidget *anchor = m_activeLeaf ? m_activeLeaf.data() : static_cast<QWidget *>(m_active.data());
        auto *tool = createBoardPane(workspace);
        if (!tool) return;
        if (anchor) insertBeside(anchor, tool, Qt::Horizontal, false);
        else if (page && page->layout()) page->layout()->addWidget(tool);
        // Opening the Switchboard is the explicit project action: from here the tab is this
        // project's, its panes get the card tools, and it stays so until it is detached.
        attachTab(page, workspace, QString::fromLatin1(relay::projects::kReasonSwitchboard));
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
        // Out of retries: the card is not on this board (a `#ID` from another project's output,
        // or a card that has been removed). Say so rather than leave the click looking ignored.
        if (attempt >= 24) {
            statusBar()->showMessage(QStringLiteral("No card #%1 on this board.").arg(id), 9000);
            return;
        }
        QPointer<ToolPane> guard(tool);
        QTimer::singleShot(250, this, [this, guard, id, attempt] {
            ToolPane *pane = guard.data();
            if (!pane || !pane->board() || pane->board()->detailOpen()) return;
            if (!pane->board()->model().card(id)) { waitForBoardCard(pane, id, attempt + 1); return; }
            pane->board()->selectCard(id);
            pane->board()->openSelected();
        });
    }

    // Where a notification's `source` points: a pane session token, or `board:<workspace>#<id>`
    // for a Switchboard card (#NQP9). Tokens go to the manager as they always did; a board
    // source finds an open Switchboard on that workspace — this window's first, then any other
    // window's, which is raised — or opens one here beside whatever is active, and lands on the
    // card. The focus moves only because the user asked: posting (createBoardPane above) never
    // calls into here.
    void openNotificationSource(const QString &source) {
        if (!source.startsWith(QStringLiteral("board:"))) { m_manager->focusPane(source); return; }
        const QString rest = source.mid(6);
        const int cut = rest.lastIndexOf(QLatin1Char('#'));
        if (cut <= 0) return;
        const QString workspace = rest.left(cut);
        const QString id = rest.mid(cut + 1);
        QList<RelayWindow *> order{this};
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *w = dynamic_cast<RelayWindow *>(top); w && w != this) order.append(w);
        for (RelayWindow *w : order) {
            ToolPane *tool = boardPaneFor(w, workspace);
            if (!tool) continue;
            if (w != this) { w->raise(); w->activateWindow(); }
            w->revealBoardCard(tool, id);
            return;
        }
        // No window is showing that board: one beside the active leaf here, the way
        // toggleBoardPane places a first Switchboard.
        auto *tool = createBoardPane(workspace);
        QWidget *anchor = m_activeLeaf ? m_activeLeaf.data() : static_cast<QWidget *>(m_active.data());
        if (anchor) insertBeside(anchor, tool, Qt::Horizontal, false);
        else if (QWidget *page = m_tabs->currentWidget(); page && page->layout()) page->layout()->addWidget(tool);
        revealBoardCard(tool, id);
    }

    // The Switchboard pane in `w` that shows `workspace`, if it has one (any tab of it).
    static ToolPane *boardPaneFor(RelayWindow *w, const QString &workspace) {
        if (!w) return nullptr;
        for (int i = 0; i < w->m_tabs->count(); ++i)
            for (QWidget *leaf : w->leavesIn(w->m_tabs->widget(i)))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf);
                    tool && tool->board() && tool->board()->workspace() == workspace)
                    return tool;
        return nullptr;
    }

    // Select and open a card in a board pane, and keep asking while its rows are still loading
    // (waitForBoardCard). Used by openNotificationSource and openBoardCard's paths.
    void revealBoardCard(ToolPane *tool, const QString &id) {
        if (!tool || !tool->board()) return;
        tool->board()->selectCard(id);
        tool->board()->openSelected();
        setActiveLeaf(tool);
        focusLeaf(tool);
        if (!tool->board()->model().card(id)) waitForBoardCard(tool, id, 0);
    }

    // The directory the Switchboard is looked for from: the pane that asked, and nothing else.
    // The Switchboard is per project, so the answer may only come from the active pane — the
    // terminal's own directory first, because `workspace()` is frozen when the pane is made and a
    // pane that has `cd`-ed into another checkout is standing in that project now.
    QString boardSearchRoot() const {
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data()); tool && tool->board())
            return tool->board()->workspace();
        if (!m_active) return QString();
        return m_active->cwd().isEmpty() ? m_active->workspace() : m_active->cwd();
    }

    // The nearest ancestor of the active pane's directory that has a Switchboard (BoardWorkspace.h).
    // There is deliberately no window-wide or process-wide fallback: `m_manager->workspace()` and
    // QDir::currentPath() are both the directory Relay was launched in, so with them every pane in
    // every window "found" the launch project's board and wrote cards into the wrong repository
    // (owner report, 2026-09-18). A Switchboard pane answers with the board it is already showing.
    QString boardWorkspace() const {
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data()); tool && tool->board())
            return tool->board()->workspace();
        // An attached tab has one project and keeps it wherever its panes wander (#JN7X).
        if (const QString attached = tabProject(m_tabs->currentWidget()); !attached.isEmpty())
            return attached;
        if (!m_active) return QString();
        // Unattached: the active pane's candidate, but only when that project already has a
        // board. `boardRootFor` of the pane's live directory is exactly that — the walk stops at
        // the nearest ancestor with a board, which is the candidate `projects::candidateFor()`
        // returns whenever one exists. The pane's `workspace()` is deliberately not a candidate:
        // it is frozen at creation and inherited from the launch directory.
        return relay::boardRootFor({m_active->cwd()});
    }

    // ----- the `board` block a pane's worker is configured with (protocol 19.1) ----------------
    //
    // Three shapes, one per state of the tab:
    //   unattached          -> {"attach": false}   — no tools, no policy block, no walk-up. The
    //                          worker used to walk up from the pane's workspace with no block at
    //                          all, which is how every pane "found" the launch project's board.
    //   attached, has board -> {"dir", "project", "state": "ready"}
    //   attached, no board  -> {"project", "state": "uninitialized"} — the worker attaches
    //                          anyway, offers `board_create_card` alone and asks before creating
    //                          anything (19.12). Nothing here creates a folder.
    QJsonObject boardSettingsFor(QWidget *page) const {
        const QString project = tabProject(page);
        if (project.isEmpty()) return {{QStringLiteral("attach"), false}};
        const QString dir = relay::projects::boardDirOf(project);
        if (dir.isEmpty())
            return {{QStringLiteral("project"), project}, {QStringLiteral("state"), QStringLiteral("uninitialized")}};
        return {{QStringLiteral("dir"), dir}, {QStringLiteral("project"), project},
                {QStringLiteral("state"), QStringLiteral("ready")}};
    }

    // Tell every terminal pane in the tab which board it is on now, without ending what it was
    // talking about: `set_board` re-points the worker's tools and prompt block and leaves the
    // Agent, its messages and its session id alone (protocol 19.11).
    void repointTabPanes(QWidget *page) {
        const bool attached = !tabProject(page).isEmpty();
        const QJsonObject board = boardSettingsFor(page);
        for (Pane *pane : panesIn(page)) pane->setBoard(attached ? board : QJsonObject());
        // The consoles of the tab are not in `panesIn` — that is the whole point of the walk
        // stopping at a `ToolPane` — so their hosts are told here instead, by the board the tab
        // is now attached to rather than by a `set_board` meant for a terminal agent (#AGNT
        // step 5).
        refreshConsoleHosts(page);
    }

    // ----- the helper worker: one per tab (card #FEJQ, protocol §30.7) --------------------------
    //
    // It was one worker per board root per window until 2026-09-20. That was already right about
    // the thing it was fixing — a single shared worker was re-pointed at whichever board was
    // opened last and broadcast its cards to every Switchboard in the window — but the owner's
    // rule is stronger: **Switchboards are per tab**, so the same project open in two tabs gets
    // two helpers with two conversations over one set of board files. Keyed by workspace, those
    // two tabs shared one worker and one conversation, and an answer meant for one redrew both.
    //
    // It is also no longer only the board's. The helper in Options, Actions and Sessions is this
    // same worker (§30.7), so it is the *tab's* agent: it lives as long as the tab and is started
    // by the first thing that asks it something, whether that is a Switchboard opening or a
    // question typed into the Options panel.
    relay::BoardWorker *boardWorker(QWidget *page) {
        if (!page) return nullptr;
        const QString tab = tabIdOf(page);
        if (relay::BoardWorker *existing = m_boardWorkers.value(tab).data()) return existing;
        auto *worker = new relay::BoardWorker(
            QStandardPaths::findExecutable(QStringLiteral("python3")), dataRoot(), this);
        m_boardWorkers.insert(tab, worker);
        QPointer<RelayWindow> guard(this);
        // Its events reach the views of its own tab and no others. A second tab on the same
        // project has its own worker and its own conversation, and neither redraws the other.
        worker->onEvent = [guard, tab](const QJsonObject &event) {
            if (!guard) return;
            // A worker names its events in `event`, never in `type` — `type` is what the *GUI*
            // calls the messages it sends down (BoardWorker::handleLine, Pane's own reader). This
            // read said `type` from the day the helper route was written (c78c8004), so it matched
            // nothing: every `app_command` the helper sent sat unanswered until its 20-second
            // deadline, and every `presets`/`key_tested`/`configured` below was dropped. The
            // owner's report is what it looks like from the outside — "sessions helper didn't do
            // anything when I asked to open a group of previous sessions in new panes"
            // (2026-09-20): the helper called app_open, nothing answered, and the turn ended with
            // the call still hanging when the tab it was in closed.
            const QString type = event.value(QStringLiteral("event")).toString();
            // An `app_command` out of the *helper* (§30.3). A pane's worker is answered in
            // src/Pane.h; this is the other pipe, and until it was here every write the helper
            // attempted — app_option_set, app_action_run, app_open, app_undo — waited out the
            // 20-second deadline and came back `no_reply`, with nothing on screen to say why.
            // The answer goes back down this worker's own pipe: there is no routing field on the
            // wire. `who` is "helper", which is what the change log and the notification say.
            if (type == QStringLiteral("app_command")) {
                relay::BoardWorker *worker = guard->m_boardWorkers.value(tab).data();
                if (!worker) return;
                QPointer<RelayWindow> window(guard);
                worker->send(relay::appcommands::answerFor(event, [window](const QJsonObject &command) {
                    return window ? window->executeAppCommand(command, QStringLiteral("helper"))
                                  : QJsonObject{};
                }));
                return;
            }
            // Which panels of this tab are waiting on the helper right now (#H6VQ). A worker that
            // dies mid-turn tells nobody: `done` never comes, so the panel that asked keeps its
            // busy strip and its composer refuses a second prompt for ever. This is the list that
            // gets put back — the pane a turn started in, and any pane with a prompt queued
            // behind it.
            // Since card #AGNT the wire says this in one word. The two chat-era events that fed
            // this list are retired; a console's turn is an ordinary pane turn, and `surface`
            // rides on `queued`, on `agent_started` and on `agent_finished` (protocol 33). A
            // finish with no surface is a worker that answered nothing in particular, and clears
            // the list rather than leaving a name in it for ever.
            if (type == QStringLiteral("queued") || type == QStringLiteral("agent_started")) {
                if (const QString surface = event.value(QStringLiteral("surface")).toString(); !surface.isEmpty())
                    guard->m_helperWaiting[tab].insert(surface);
            } else if (type == QStringLiteral("agent_finished")) {
                const QString surface = event.value(QStringLiteral("surface")).toString();
                if (surface.isEmpty()) guard->m_helperWaiting[tab].clear();
                else guard->m_helperWaiting[tab].remove(surface);
            }
            // The handshake, kept for a console made after this worker was already up: it is said
            // once per configure and would otherwise never reach one (attachConsoleToTab).
            if (type == QStringLiteral("ready") || type == QStringLiteral("configured"))
                guard->m_workerHandshake.insert(handshakeKey(tab, type), event);
            // Options › Models on a tab with no pane agent reads the helper's presets, and its key,
            // test and custom-provider requests come back down this pipe (owner, 2026-09-20).
            if (type == QStringLiteral("presets")) {
                guard->m_helperPresets[tab] = event.value(QStringLiteral("presets")).toArray();
                guard->m_helperTierDefaults[tab] = event.value(QStringLiteral("tier_list_defaults")).toObject();
                relay::SettingsWatch::instance().notify();
            } else if (type == QStringLiteral("key_stored") || type == QStringLiteral("key_removed")
                       || type == QStringLiteral("custom_provider_saved") || type == QStringLiteral("custom_provider_deleted")) {
                if (relay::BoardWorker *worker = guard->m_boardWorkers.value(tab).data()) worker->send({{"type", "presets"}});
                if (const QString error = event.value(QStringLiteral("error")).toString(); !error.isEmpty() && !guard->m_active)
                    QMessageBox::warning(guard, QStringLiteral("Models"), error);
            } else if (type == QStringLiteral("key_tested") && !guard->m_active) {
                // No pane to carry the status line: the answer is said where the button was.
                const QString preset = event.value(QStringLiteral("preset")).toString();
                // The model that answered, by name (card #MDL1, rule 1).
                const QString answered = relay::models::nameOf(event.value(QStringLiteral("model")).toString());
                QMessageBox::information(guard, QStringLiteral("Models"), event.value(QStringLiteral("ok")).toBool()
                    ? QStringLiteral("%1 answered%2.").arg(preset, answered.isEmpty()
                          ? QString() : QStringLiteral(" on ") + answered)
                    : QStringLiteral("%1: %2").arg(preset, event.value(QStringLiteral("error")).toString()));
            }
            QWidget *page = guard->pageOfTabId(tab);
            if (!page) return;
            for (QWidget *leaf : leavesIn(page))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board())
                    tool->board()->handleEvent(event);
            guard->deliverToHelperPanels(page, event);
            guard->deliverToConsoles(tab, event);
        };
        // The process has gone and nobody asked it to: every panel of this tab that was waiting
        // on it is put back to idle (#H6VQ, §30.7). Nothing else says so — `done` was the
        // worker's to send — and the next ask starts a fresh one through the first-ask path, so
        // the composer must be usable again the moment this returns.
        worker->onExit = [guard, tab](bool crashed) {
            if (guard) guard->helperWorkerGone(tab, crashed);
        };
        // What the worker says about *itself* — it would not start, it exited, its pipe
        // overflowed. The status bar is not shown in this layout, so until 2026-09-20 a
        // Switchboard whose worker died sat on "Loading the Switchboard…" for ever with the
        // explanation written somewhere nobody can see (#7M6E). It goes to the tab's board panes
        // as well, which put it where the loading line was, with a Retry.
        worker->onStatus = [guard, tab](const QString &text) {
            if (!guard) return;
            guard->statusBar()->showMessage(text, 9000);
            QWidget *page = guard->pageOfTabId(tab);
            if (!page) return;
            const QJsonObject status{{QStringLiteral("event"), QStringLiteral("board_worker_status")},
                                     {QStringLiteral("text"), text}};
            for (QWidget *leaf : leavesIn(page))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board())
                    tool->board()->handleEvent(status);
        };
        // The owner's devices see this tab's board through the same worker (#SWPH): the bridge
        // taps `onEvent` — the handler above runs first and unchanged — and never replaces it.
        relay::BoardRemote::instance().tap(worker, this, tab);
        return worker;
    }

    // A pane that wants the **tab worker's** events — not the agent's, the worker's — registers
    // here: the Test suites pane's `tests_*` and the Profile pane's `profile_*`, which ride the
    // same process because it is the tab's one worker (§30.7). The helper panels registered here
    // too and filtered by a `pane` tag; they are gone with card #AGNT, and a console is told
    // through `deliverToConsoles` instead, which needs no tag because the conversation is one.
    //
    // `owner` owns the subscription the way SettingsWatch's context does: a pane that closes is
    // dropped rather than called.
    void listenToHelper(QWidget *page, QObject *owner, std::function<void(const QJsonObject &)> handle) {
        if (!page || !owner || !handle) return;
        m_helperListeners.append({QPointer<QWidget>(page), QPointer<QObject>(owner), std::move(handle)});
    }

    // A message on the tab's worker, which is started here — at the **first** one rather than
    // when the tab opened (owner decision 5, §30.7). It used to take a `pane` to tag the message
    // with, which was `board_chat`'s way of saying which brief the turn should get; a console
    // says it in its `context` block and its `surface` instead (protocol 33), so the tag is gone
    // and every caller left is a pane of the window asking the tab's worker for something — the
    // Test suites list, a profile run, a board write, the signals configuration.
    void sendToHelper(QWidget *page, QJsonObject message) {
        if (!page) return;
        if (relay::BoardWorker *worker = helperWorker(page, true)) worker->send(message);
    }

    // The helper for this tab, started if it is not running yet. This is what a panel calls at the
    // **first ask** (owner decision 5: not when the tab opens), so a tab nobody asks anything
    // never pays for a worker; `start` false asks only whether there is one already.
    relay::BoardWorker *helperWorker(QWidget *page, bool start) {
        if (!page) return nullptr;
        if (!start) return m_boardWorkers.value(tabIdOf(page)).data();
        // Starting a helper builds the tab's app catalog, and the catalog's Models section asks
        // for this same helper when no pane agent has presets yet — which at startup is always.
        // Without this the two call each other until the stack is gone (SIGSEGV in malloc,
        // relay-gdb.20260920-170825.log). While a tab's helper is being started, the section gets
        // the worker as it stands (none yet) and asks again when the page is next shown.
        const QString tab = tabIdOf(page);
        if (m_helperStarting.contains(tab)) return m_boardWorkers.value(tab).data();
        m_helperStarting.insert(tab);
        startBoardWorker(page);
        m_helperStarting.remove(tab);
        return boardWorker(page);
    }

    // The tab a page is, by its persistent id. The map is keyed by that, so this is how an event
    // or a catalog finds its way back to the tab it belongs to.
    QWidget *pageOfTabId(const QString &tab) const {
        if (tab.isEmpty()) return nullptr;
        for (int i = 0; i < m_tabs->count(); ++i)
            if (QWidget *page = m_tabs->widget(i); page && page->property("relayTabId").toString() == tab)
                return page;
        return nullptr;
    }

    // One tab-worker event to the panes of that tab that asked for them (`listenToHelper`),
    // dropping the ones that have closed.
    void deliverToHelperPanels(QWidget *page, const QJsonObject &event) {
        for (int i = int(m_helperListeners.size()) - 1; i >= 0; --i)
            if (!m_helperListeners.at(i).owner || !m_helperListeners.at(i).page)
                m_helperListeners.removeAt(i);
        const QList<HelperListener> listeners = m_helperListeners;   // a handler may close a pane
        for (const HelperListener &listener : listeners)
            if (listener.owner && listener.page == page) listener.handle(event);
    }

    // The tab a helper worker belongs to: the map's own key.
    QString helperTab(relay::BoardWorker *worker) const {
        for (auto it = m_boardWorkers.cbegin(); it != m_boardWorkers.cend(); ++it)
            if (it.value().data() == worker) return it.key();
        return {};
    }

    // A helper worker that died mid-turn, told to the panels that were waiting on it (#H6VQ).
    // It goes out as the turn's own `error` event, tagged with the pane that asked, because that
    // is the one message every panel already knows how to end a turn on: the busy strip goes, the
    // composer takes prompts again and the line is written into the log where the answer would
    // have been. A fresh worker starts at the next ask (`helperWorker(page, true)`), which is
    // where a helper comes from anyway.
    void helperWorkerGone(const QString &tab, bool crashed) {
        const QSet<QString> waiting = m_helperWaiting.take(tab);
        // Whatever the dead worker said about itself is not true of the next one.
        m_workerHandshake.remove(handshakeKey(tab, QStringLiteral("ready")));
        m_workerHandshake.remove(handshakeKey(tab, QStringLiteral("configured")));
        QWidget *page = pageOfTabId(tab);
        if (!page || waiting.isEmpty()) return;
        const QString text = crashed
            ? QStringLiteral("The helper stopped before it answered (it crashed). Your message is "
                             "kept — ask again and a fresh helper starts.")
            : QStringLiteral("The helper stopped before it answered. Your message is kept — ask "
                             "again and a fresh helper starts.");
        // A **card** turn that was in flight is the one surface the consoles cannot answer for:
        // the card page follows its own turn by card id (`m_cardTurns`), so with the worker gone
        // its busy strip would stay up for ever and `p`/`x`/`v` would stay disabled. It is given
        // the ordinary end-of-turn error it already knows, addressed to the card. The surface a
        // card turn carries is `card:<ID>` (protocol 33, `board_turns.surface_of`).
        //
        // The other surfaces used to get the same event tagged `chat: true` and `pane: <name>`,
        // for the panels to filter on; the panels are gone (card #AGNT) and the consoles are told
        // once, below.
        for (const QString &surface : waiting) {
            if (!surface.startsWith(QStringLiteral("card:"))) continue;
            const QJsonObject event{{QStringLiteral("event"), QStringLiteral("error")},
                                    {QStringLiteral("card_id"), surface.mid(5)},
                                    // The worker's queue went with the worker, so the page drops
                                    // its rows rather than offering prompts nobody holds.
                                    {QStringLiteral("worker_gone"), true},
                                    {QStringLiteral("text"), text}};
            for (QWidget *leaf : leavesIn(page))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board())
                    tool->board()->handleEvent(event);
        }
        // And the consoles of this tab, once rather than per waiting surface: it is one agent
        // that died and every console of the tab is on it. An `error` with no request id is what
        // a pane already ends a stuck turn on — the busy line goes, the clock stops and the
        // composer takes prompts again — and the next ask starts a fresh worker.
        deliverToConsoles(tab, QJsonObject{{QStringLiteral("event"), QStringLiteral("error")},
                                           {QStringLiteral("worker_gone"), true},
                                           {QStringLiteral("text"), text}});
    }

    // The tab has gone, so its helper has nobody left to talk to (§30.7: "closing the tab stops
    // its worker"). It is no longer the last Switchboard closing that ends it: the helper serves
    // the tab's Options, Actions and Sessions panes too, and a Switchboard put away is not a
    // conversation abandoned.
    void releaseBoardWorker(QWidget *page) {
        if (!page) return;
        const QString tab = page->property("relayTabId").toString();
        if (tab.isEmpty()) return;                       // nothing ever asked: no worker to stop
        m_helperWaiting.remove(tab);
        m_tabConsole.remove(tab);
        m_workerHandshake.remove(handshakeKey(tab, QStringLiteral("ready")));
        m_workerHandshake.remove(handshakeKey(tab, QStringLiteral("configured")));
        if (relay::BoardWorker *worker = m_boardWorkers.take(tab).data()) {
            worker->onEvent = nullptr;
            worker->onStatus = nullptr;
            worker->onExit = nullptr;
            worker->stop();
            worker->deleteLater();
        }
    }

    // Every helper worker of this window, told to shut down the way one is when its tab closes.
    // The single per-window worker was never stopped at all: it was a child of the window, so its
    // QProcess was killed by the destructor instead of being asked to exit (closeEvent).
    void stopBoardWorkers() {
        const QList<QPointer<relay::BoardWorker>> workers = m_boardWorkers.values();
        m_boardWorkers.clear();
        m_helperWaiting.clear();
        for (const QPointer<relay::BoardWorker> &worker : workers)
            if (worker) {
                worker->onEvent = nullptr;
                worker->onStatus = nullptr;
                worker->onExit = nullptr;
                worker->stop();
                worker->deleteLater();
            }
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
    void startBoardWorker(QWidget *page) {
        if (!page) return;
        // The tab's own project, so two tabs on one project run two helpers over one set of board
        // files, and a tab attached to nothing gets a helper with no board at all — the app tools
        // and nothing else (§30.7). `workspace` empty is exactly that: the worker is configured
        // with no board root, and the backend attaches no `board_*` tools to it.
        const QString workspace = boardWorkspaceOfTab(page);
        QSettings settings;
        // Where a new pane starts (card #MDL1, rule 3): rank 1 of the main list, model and all.
        // A helper is an agent console like any other, and it answering on a different provider
        // from the panes beside it — whichever one some pane switched to last — is exactly the
        // inconsistency the card is about. `provider/preset` is the fallback for an install whose
        // list cannot answer yet, as it is in a pane.
        relay::models::Catalog catalog;
        for (Pane *each : allPanes()) {
            catalog = each->modelCatalog();
            if (!catalog.entries.isEmpty()) break;
        }
        const relay::models::StartChoice start = relay::models::startEntry(catalog, QString(), QString());
        const QString preset = start.entry.preset.isEmpty()
                                   ? settings.value(QStringLiteral("provider/preset")).toString()
                                   : start.entry.preset;
        const bool named = !preset.isEmpty() && preset != QStringLiteral("custom");
        QJsonObject configure{{QStringLiteral("type"), QStringLiteral("configure")},
                              {QStringLiteral("workspace"), workspace},
                              {QStringLiteral("agent_role"), QStringLiteral("switchboard")},
                              {QStringLiteral("use_stored_key"), true},
                              {QStringLiteral("api_key"), QString()},
                              {QStringLiteral("max_tokens"), settings.value(QStringLiteral("provider/max_tokens"), 0).toInt()}};
        if (!preset.isEmpty()) configure.insert(QStringLiteral("preset"), preset);
        // The list's rank 1 is a model, not only a provider: named alone, the worker would fill in
        // the preset's own default (card #MDL1).
        if (named && preset == start.entry.preset && !start.entry.model.isEmpty())
            configure.insert(QStringLiteral("model"), start.entry.model);
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
        // The same turn limits a pane's agent runs under (protocol 12.1, 15.1): a card's Plan is
        // a turn like any other, and its agent is built from this one's provider and deadlines.
        const QJsonObject limits = Pane::turnOptions();
        for (auto it = limits.begin(); it != limits.end(); ++it) configure.insert(it.key(), it.value());
        // Which tab's helper this is (§30.2 `tab`, §30.7). It rides twice on purpose: `app.tab` is
        // the canonical field and reaches every agent, pane and helper alike, while the top-level
        // `tab` is what the board side keys its persisted conversation by without reaching into
        // the app block — the conversation is per (project, tab), and the project is `workspace`.
        configure.insert(QStringLiteral("tab"), tabIdOf(page));
        configure.insert(QStringLiteral("app"), appCatalogFor(page));
        // The keybinding catalogue a pane's `configure` carries (§30.2), for the same reason and
        // from the same builder: the helper's Actions pane is the palette "with its keyboard
        // shortcut beside it", so `app_action_list` cannot answer without it — and the owner
        // decided on #GMCF (2026-09-20) that the helper may also rebind one, which is the
        // `set_keybinding` this block hands it. Reloads travel as `sendHelperKeybindings()`.
        configure.insert(QStringLiteral("keybindings"), Keymap::instance().catalog());
        // Protocol 33's `context` block: what this tab's agent is *about*. It is the context of
        // the console that last spoke, because the consoles of a tab share one worker and one
        // conversation (owner decision 1) and what differs between them is the brief and the
        // surface name, never the store — `persist {scope: "helper", key: <tab id>}` is the same
        // for all four, so swapping the block reconfigures the brief and leaves the conversation
        // exactly where it was (30.7's move test). A tab with no console attached sends no block
        // at all, which is how this configure read before card #AGNT.
        if (Pane *console = m_tabConsole.value(tabIdOf(page)).data(); console && console->context())
            configure.insert(QStringLiteral("context"), console->contextBlock());
        // Only this tab's helper: another tab, even on the same project, keeps its own.
        if (relay::BoardWorker *worker = boardWorker(page)) worker->start(configure);
    }

    // The board a tab's helper works on: the tab's attached project. Empty for an unattached tab,
    // and that is what gives it a board-less helper — the app tools and no `board_*` at all
    // (§30.7). A project with no board yet is still its project; the worker is told the state in
    // the `board` block and offers `board_create_card` alone (19.12), and nothing here creates a
    // folder.
    QString boardWorkspaceOfTab(QWidget *page) const {
        if (const QString project = tabProject(page); !project.isEmpty()) return project;
        // A Switchboard opened into a tab that was never attached — a `#card` link followed into a
        // window that had no board — still works on the root its view was built for.
        if (page) for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board())
                return tool->board()->workspace();
        return {};
    }

    // ----- the Switchboard on the owner's devices (#SWPH, src/BoardRemote.h) --------------------
    //
    // The bridge is widget-free; this is the window's half of it. A device's request goes down the
    // tab's own helper worker — `helperWorker(page, true)`, the call a Switchboard pane makes, so
    // the worker is started on demand and configured by the one startBoardWorker() there is — and
    // Execute and Verify open their pane through the board pane's own hooks.
    void registerBoardRemote() {
        relay::BoardRemote &bridge = relay::BoardRemote::instance();
        static bool wired = false;      // once for the application: RemoteShare is one, windows are many
        if (!wired) {
            wired = true;
            bridge.remoteOn = [] {
                const relay::RemoteShare &share = relay::RemoteShare::instance();
                return share.alwaysOn() && share.running();
            };
            bridge.sendEvent = [](const QJsonValue &rid, const QJsonObject &event) {
                relay::RemoteShare::instance().sendBoardEvent(rid, event);
            };
            QObject::connect(&relay::RemoteShare::instance(), &relay::RemoteShare::boardRequest, &bridge,
                             [](const QJsonObject &line) { relay::BoardRemote::instance().handleRequest(line); });
        }
        QPointer<RelayWindow> guard(this);
        relay::BoardRemote::Host host;
        host.active = [guard] { return guard && guard->isActiveWindow(); };
        host.boardTab = [guard] { return guard ? guard->remoteBoardTab(false) : QString(); };
        host.adoptBoard = [guard] { return guard ? guard->remoteBoardTab(true) : QString(); };
        host.hasBoard = [guard](const QString &tab) { return guard && guard->tabHasBoard(guard->pageOfTabId(tab)); };
        host.send = [guard](const QString &tab, const QJsonObject &message) {
            QWidget *page = guard ? guard->pageOfTabId(tab) : nullptr;
            relay::BoardWorker *worker = page ? guard->helperWorker(page, true) : nullptr;
            if (!worker) return false;
            worker->send(message);
            return true;
        };
        host.executeCard = [guard](const QString &tab, const QString &card, const QString &task) {
            return guard ? guard->remoteCardPane(tab, card, QString(), task) : QString();
        };
        host.verifyCard = [guard](const QString &tab, const QString &card, const QString &runner, const QString &task) {
            return guard && !runner.isEmpty() ? guard->remoteCardPane(tab, card, runner, task) : QString();
        };
        host.paneExists = [guard](const QString &token) {
            return guard && !token.isEmpty() && guard->findPaneByToken(token) != nullptr;
        };
        // The bridge watches the board folder only while no Switchboard pane of the tab does.
        host.boardDir = [guard](const QString &tab) {
            QWidget *page = guard ? guard->pageOfTabId(tab) : nullptr;
            return page ? relay::projects::boardDirOf(guard->boardWorkspaceOfTab(page)) : QString();
        };
        host.paneWatches = [guard](const QString &tab) {
            QWidget *page = guard ? guard->pageOfTabId(tab) : nullptr;
            if (page) for (QWidget *leaf : leavesIn(page))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) return true;
            return false;
        };
        host.status = [guard](const QString &text) {
            if (guard && !text.isEmpty()) guard->statusBar()->showMessage(text, 9000);
        };
        bridge.addHost(this, host);
    }

    // A tab a device can work on: attached to a project whose board exists. A project with no
    // board yet is not one — creating a Switchboard is asked on the desktop, never from a phone.
    bool tabHasBoard(QWidget *page) const {
        if (!page) return false;
        const QString workspace = boardWorkspaceOfTab(page);
        return !workspace.isEmpty() && !relay::projects::boardDirOf(workspace).isEmpty();
    }

    // The tab whose board a device sees: the current tab when it has one, else the first that has.
    // `adopt` is the bridge's second question, asked only when no window answered the first: the
    // active pane stands in a project that has a Switchboard nobody has opened, and opening it
    // from a device attaches the tab exactly as Ctrl+Shift+S does (toggleBoardPane) — minus the
    // pane, which a phone has no use for.
    QString remoteBoardTab(bool adopt) {
        QWidget *current = m_tabs->currentWidget();
        if (!adopt) {
            if (tabHasBoard(current)) return tabIdOf(current);
            for (int i = 0; i < m_tabs->count(); ++i)
                if (tabHasBoard(m_tabs->widget(i))) return tabIdOf(m_tabs->widget(i));
            return {};
        }
        if (!current || !tabProject(current).isEmpty()) return {};
        const QString workspace = boardWorkspace();
        if (workspace.isEmpty() || relay::projects::boardDirOf(workspace).isEmpty()) return {};
        attachTab(current, workspace, QString::fromLatin1(relay::projects::kReasonSwitchboard));
        statusBar()->showMessage(QStringLiteral("Switchboard opened from a paired device: this tab is now %1's.")
                                     .arg(relay::projects::nameFor(workspace)), 9000);
        return tabHasBoard(current) ? tabIdOf(current) : QString();
    }

    // Execute (`runner` empty) or Verify from a device. With a Switchboard pane in the tab this
    // *is* the desktop's code path: the pane's own onExecuteCard / onVerifyCard, as createBoardPane()
    // installed them. With none, the same pane is opened beside whatever the tab was last using —
    // there is no board to keep a split for — and handed the same task the same way.
    QString remoteCardPane(const QString &tab, const QString &card, const QString &runner, const QString &task) {
        QWidget *page = pageOfTabId(tab);
        if (!page) return {};
        m_tabs->setCurrentWidget(page);     // the new pane takes the focus, as it does on the desktop
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) {
                relay::BoardView *view = tool->board();
                if (runner.isEmpty()) return view->onExecuteCard ? view->onExecuteCard(card, task) : QString();
                return view->onVerifyCard ? view->onVerifyCard(card, runner, task) : QString();
            }
        const QString workspace = boardWorkspaceOfTab(page);
        const bool guest = runner.startsWith(QStringLiteral("guest:"));
        const QString runnerId = runner.section(QLatin1Char(':'), 1);
        if (!runner.isEmpty() && runnerId.isEmpty()) return {};
        QJsonObject spec{{"cwd", workspace}, {"workspace", workspace}, {"agent_role", "main"}};
        if (!runner.isEmpty() && !guest) spec.insert(QStringLiteral("preset"), runnerId);
        Pane *pane = nullptr;
        try { pane = createPane(spec); }
        catch (const std::exception &error) { statusBar()->showMessage(QString::fromUtf8(error.what()), 9000); return {}; }
        QWidget *anchor = m_lastActive.value(page);
        const QList<QWidget *> leaves = leavesIn(page);
        if (!anchor || !leaves.contains(anchor)) anchor = leaves.isEmpty() ? nullptr : leaves.first();
        if (anchor) insertBeside(anchor, pane, Qt::Horizontal, false);
        else if (page->layout()) page->layout()->addWidget(pane);
        setActive(pane);
        focusLeaf(pane);
        if (guest) pane->startGuestBoardTask(runnerId, task, card);
        else pane->startBoardTask(task, card);
        updateTitles();
        return pane->sessionToken();
    }

    // A setting the helper workers carry changed: re-send `configure` to every live one.
    // `BoardWorker::start` on a running process is exactly that (and a no-op when nothing moved).
    void reconfigureBoardWorkers() {
        for (const QString &tab : m_boardWorkers.keys())
            if (m_boardWorkers.value(tab)) startBoardWorker(pageOfTabId(tab));
    }

    ToolPane *createBoardPane(const QString &workspace, const QJsonArray &collapsed = {},
                              const QJsonArray &hidden = {}, const QString &sort = QString(),
                              const QJsonArray &labels = {},
                              const QJsonArray &selfClosed = {}) {
        auto *view = new relay::BoardView(workspace);
        if (!collapsed.isEmpty()) view->setCollapsedSections(collapsed);
        if (!hidden.isEmpty()) view->setHiddenSections(hidden);
        if (!labels.isEmpty()) view->setLabelFilter(labels);
        // Which "N closed by the agent" rows were open (#93WR); none of them is the default.
        if (!selfClosed.isEmpty()) view->setOpenSelfClosed(selfClosed);
        // The sort the pane was saved with; empty (or unknown) leaves it Manual.
        if (!sort.isEmpty()) view->setSortOrder(sort);
        auto *tool = new ToolPane(view, workspace);
        relay::theme::polishWindow(tool);
        tool->setObjectName(QStringLiteral("pane"));
        QPointer<ToolPane> guard(tool);
        // This view's own board root, so a second project's Switchboard in the same window writes
        // through its own worker and into its own board folder.
        view->onSend = [guard](const QJsonObject &message) {
            auto *w = windowOf(guard);
            if (!w) return;
            if (relay::BoardWorker *worker = w->helperWorker(w->pageOf(guard), true)) worker->send(message);
        };
        // A Switchboard put away no longer stops the worker: it is the tab's helper, and the tab's
        // Options, Actions and Sessions panes go on asking it (§30.7). Closing the tab is what
        // ends it — requestCloseTab() → forgetTab() → releaseBoardWorker(page).
        view->onStatus = [guard](const QString &text) {
            if (auto *w = windowOf(guard); w && !text.isEmpty()) w->statusBar()->showMessage(text, 9000);
        };
        view->onTitleChanged = [guard](const QString &) { if (auto *w = windowOf(guard)) w->updateTitles(); };
        view->onNavigationChanged = [guard] { if (auto *w = windowOf(guard)) w->m_manager->scheduleSave(); };
        view->onOpenFile = [guard](const QString &path) {
            if (auto *w = windowOf(guard)) w->openPath(path, 0, guard);
        };
        // The links in the page agent's answers that leave the board (#FEJQ, §30.4): the helper
        // here is the same agent the Options and Sessions panels ask, so an answer about a setting
        // or a conversation should be one click from it, as `card:` already is.
        view->onOpenOption = [guard](const QString &section, const QString &row) {
            auto *w = windowOf(guard);
            if (!w) return;
            w->openSettingsPane(relay::SettingsPane::Mode::Options, section);
            if (ToolPane *pane = w->settingsPaneIn(w->m_tabs->currentWidget(),
                                                   relay::SettingsPane::Mode::Options);
                pane && pane->settings())
                pane->settings()->revealOption(section, row);
        };
        view->onOpenSession = [guard](const QString &id) {
            if (auto *w = windowOf(guard)) w->openSessions(QString(), id);
        };
        // The agent consoles of this board (#AGNT steps 5 and 6): the list page's and an open
        // card's. The view asks for one lazily — the first time the chat area is shown, and the
        // first time a card opens — so a Switchboard nobody talks to pays for no console at all.
        // The tab id is the conversation's key, pushed a turn of the event loop later because
        // this pane is not in a tab yet (insertBeside comes afterwards).
        view->onCreateConsole = [guard](relay::agent::Context *context, QWidget *parent) {
            auto *w = windowOf(guard);
            return w ? w->createAgentConsole(context, parent) : relay::agent::ConsoleHandle();
        };
        QPointer<relay::BoardView> viewGuard(view);
        QTimer::singleShot(0, this, [guard, viewGuard] {
            auto *w = windowOf(guard);
            if (!w || !viewGuard) return;
            if (QWidget *page = w->pageOf(guard)) viewGuard->setTabId(w->tabIdOf(page));
        });
        // The Tests button on the panel's tool row (#7BM4): the Test suites pane is the window's,
        // a splitter pane beside this one on the same tab's worker.
        view->onOpenTestSuites = [guard] {
            if (auto *w = windowOf(guard)) w->openTestSuitesPane();
        };
        // The Profile button beside it (#7BM4 phase 5): the menu is the window's, anchored under
        // the button, and what it starts opens a result pane of the window's too.
        view->onProfile = [guard](QWidget *anchor) {
            if (auto *w = windowOf(guard)) w->openProfileMenu(anchor);
        };
        // A thread entry's pane link (#HKAP): the token names a pane Execute opened — the manager
        // looks in every window, so a pane dragged into its own window still comes back.
        view->onFocusPane = [guard](const QString &token) {
            if (auto *w = windowOf(guard)) w->m_manager->focusPane(token);
        };
        view->onSendToTerminal = [guard](const QString &reference) {
            auto *w = windowOf(guard);
            if (!w || !w->m_active) return;
            w->m_active->insertInComposer(reference);
            w->setActiveLeaf(w->m_active);
            w->m_active->focusInput();
        };
        // Execute (#XS6Q): a new terminal pane beside the board, in the board's workspace, on
        // the main agent (it builds the card), handed the card as its first task. The pane's
        // session token comes back (#HKAP) so the card's thread can link to it; an empty string
        // says no pane was opened.
        view->onExecuteCard = [guard, workspace](const QString &card, const QString &task) {
            auto *w = windowOf(guard);
            if (!w) return QString();
            Pane *pane = nullptr;
            try { pane = w->createPane({{"cwd", workspace}, {"workspace", workspace}, {"agent_role", "main"}}); }
            catch (const std::exception &error) { w->statusBar()->showMessage(QString::fromUtf8(error.what()), 9000); return QString(); }
            // The board keeps its list/card split (#BXCN): the new terminal's half comes out of
            // the panes beside the board, not out of the board itself.
            w->insertBeside(guard, pane, Qt::Horizontal, false, w->boardSplitFloor(guard));
            w->setActive(pane);
            focusLeaf(pane);
            pane->startBoardTask(task, card);
            w->updateTitles();
            return pane->sessionToken();
        };
        // And the card wears that token (#R9G7): the chip on the row and on the card page says
        // which pane claimed it, and whether that pane is still here. The same lookup
        // `onFocusPane`'s reveal uses, asked as each row is filled and each card page drawn — so a
        // pane closed while the board is up reads as closed at the board's next redraw.
        view->paneExists = [guard](const QString &token) {
            auto *w = windowOf(guard);
            return w && !token.isEmpty() && w->findPaneByToken(token) != nullptr;
        };
        // A signal thread's chip (#AQ6X phase 3): the claim's token is the *thread's* id, and no
        // pane has one, so the chip opens that thread's history — the same ⓘ view the Sessions
        // manager's Enter opens and the pickup's notification goes to. The thread file lives in
        // the ordinary sessions directory, so the pane's own worker can read it and no
        // `session_dir` has to be carried across.
        view->onOpenThread = [guard](const QString &threadId, const QString &owner) {
            auto *w = windowOf(guard);
            if (w) w->openSignalThread(threadId, owner);
        };
        // Verify (#T71W): the same pane beside the board, but on the verifier the worker picked —
        // a different provider family from the one that implemented the card. A `preset:` runner
        // is an ordinary Relay agent started on that preset and handed the brief as a board task
        // (the card travels with it); a `guest:` runner is Claude Code or Codex launched in the
        // pane's own shell with the brief as its first positional prompt, which is what both CLIs
        // take (`relay_core.guest_launch.claude_argv` / `codex_argv` pass `extra` through
        // untouched, after the flags), so the guest starts on the card rather than at an empty
        // prompt. The launch flags themselves are the guest module's and are not touched here.
        view->onVerifyCard = [guard, workspace](const QString &card, const QString &runner, const QString &task) {
            auto *w = windowOf(guard);
            if (!w) return QString();
            const bool guest = runner.startsWith(QStringLiteral("guest:"));
            const QString runnerId = runner.section(QLatin1Char(':'), 1);
            if (runnerId.isEmpty()) return QString();
            QJsonObject spec{{"cwd", workspace}, {"workspace", workspace}, {"agent_role", "main"}};
            if (!guest) spec.insert(QStringLiteral("preset"), runnerId);
            Pane *pane = nullptr;
            try { pane = w->createPane(spec); }
            catch (const std::exception &error) { w->statusBar()->showMessage(QString::fromUtf8(error.what()), 9000); return QString(); }
            w->insertBeside(guard, pane, Qt::Horizontal, false, w->boardSplitFloor(guard));
            w->setActive(pane);
            focusLeaf(pane);
            // A guest verifier goes through the pane's own Tier A / Tier B decision: the
            // worker's harness when it can run the guest, the guest's TUI only when it cannot.
            if (guest) pane->startGuestBoardTask(runnerId, task, card);
            else pane->startBoardTask(task, card);
            w->updateTitles();
            return pane->sessionToken();
        };
        // A card turn ended (#NQP9): a Plan that finished or failed gets one bell entry, so the
        // user can leave the board alone — posting only, never a focus move, which is the card's
        // "don't instantly move the active pane there" half. Discuss turns and cancellations
        // stay quiet: they ended by asking the user, who is already looking. The source names
        // the board root and the card, so the jump key and a popup click can find the card.
        view->onTurnEnded = [guard, workspace](const QString &card, const QString &mode, const QString &outcome) {
            if (mode != QStringLiteral("plan")) return;
            if (outcome != QStringLiteral("done") && outcome != QStringLiteral("error")) return;
            relay::BoardView *view = guard ? guard->board() : nullptr;
            const relay::board::Card *row = view ? view->model().card(card) : nullptr;
            const bool failed = outcome == QStringLiteral("error");
            relay::NotificationCenter::instance().post(
                QStringLiteral("%1: #%2").arg(failed ? QStringLiteral("Plan failed")
                                                     : QStringLiteral("Plan ready"), card),
                row ? row->title : QString(),
                failed ? relay::NotificationCenter::kindError : relay::NotificationCenter::kindSuccess,
                QStringLiteral("board:%1#%2").arg(workspace, card));
        };
        view->onHint = [guard](const QString &id, const QString &keys) {
            auto *w = windowOf(guard);
            if (!w || keys.isEmpty()) return;
            w->hint(QStringLiteral("board.") + id, relay::ShortcutHints::nextTime(keys));
        };
        // The board is asked for as soon as the pane is in a tab: the worker is the tab's, and the
        // pane is not in one yet while this runs.
        QTimer::singleShot(0, this, [this, guard] {
            QWidget *page = guard ? pageOf(guard) : nullptr;
            if (!page) return;
            if (relay::BoardWorker *worker = helperWorker(page, true)) worker->open();
        });
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

    // A leaf of the splitter tree: a terminal pane, or a tool pane with whatever it is chroming.
    //
    // **An embedded agent console is a `Pane` and is not a leaf.** It lives inside a `ToolPane`'s
    // subtree, not in the splitter, and `sharesWorker()` is exactly "the window made me one"
    // (`createAgentConsole` sets `onWorkerLine` and nothing else does). Without the test,
    // `leafOf()` -- which walks *up* from a clicked widget and takes the first leaf it meets --
    // answered the console rather than the tool pane around it, so a click in the Switchboard's
    // or the Sessions helper's prompt box made that console `m_activeLeaf` **and** `m_active`.
    // Everything the window then aimed at "the active pane" landed on a surface with half its
    // hooks deliberately unwired: the integration drive of card #AGNT caught `app_open {target:
    // conversation, ids}` from the Sessions helper answering ok and opening nothing, because
    // `m_active->openSavedSession(...)` reached a console whose `onOpenSessionInNewPane` is not
    // set. This is `panesIn` stopping at a `ToolPane` (step 5), one level up: the walk down was
    // fixed and the walk up was not.
    static bool isLeaf(QWidget *widget) {
        if (auto *pane = dynamic_cast<Pane *>(widget)) return !pane->sharesWorker();
        return dynamic_cast<ToolPane *>(widget) != nullptr;
    }

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
            // Ctrl+Enter: open the file ready to edit (card #SEJ2).
            explorer->onEditFile = [guard](const QString &file) { if (auto *w = windowOf(guard)) w->openPath(file, 0, guard, false, true); };
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
        // A `ToolPane` is a leaf, and this stops there the way `leavesIn` above already does.
        // Since card #AGNT a tool pane may *contain* a `Pane` — the agent console the Switchboard,
        // Options, Actions and Sessions embed — and descending into one would put that console in
        // `allPanes()`. Six things break at once when it does: `syncTabShares` and
        // `syncAlwaysOnShares` publish it to the phone as its own inbox row, `closeEvent` and
        // `confirmClose` disagree so every close prompts, `createPane`'s `!allPanes().isEmpty()`
        // makes the first real terminal default to Flash, `savePaneScrollbacks` writes a
        // scrollback the prune then removes, `paneWithToken` resolves a notification to a non-leaf
        // and hands `setActiveLeaf` one, and `repointTabPanes` sends it a `set_board` meant for
        // terminal agents. The console's own worker is the tab's, and the window reaches it
        // through `m_consoles`, never through this walk.
        if (dynamic_cast<ToolPane *>(root)) return panes;
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
        // `exit` (or the shell dying) closes the pane the way × does, so it can be reopened too.
        pane->onShellExited = [guard] { if (auto *w = windowOf(guard)) w->closePane(guard, true); };
        pane->onOpenPath = [guard](const QString &path, int line) { if (auto *w = windowOf(guard)) w->openPath(path, line, guard); };
        pane->onToggleExplorer = [guard](const QString &path) { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->toggleExplorer(path, guard); } };
        pane->onOpenBoard = [guard] { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->toggleBoardPane(); } };
        pane->onChooseTheme = [guard](const QString &id) {
            // A theme command also becomes the new-tab default, unless Options says otherwise.
            if (auto *w = windowOf(guard)) {
                const bool asDefault = themeCommandsSetDefault();
                const bool ok = w->chooseTheme(id, w->pageOf(guard), asDefault);
                if (ok && asDefault) w->refreshSettingsPanes();   // an open Options pane shows the new default
                return ok;
            }
            return relay::theme::setActiveTheme(id);
        };
        // The `board` block of every `configure` this pane sends (protocol 19.1). It is read at
        // the moment the configure is built, so a pane that is re-created or switches model comes
        // back on the board its tab is attached to — or, while the tab is attached to nothing, on
        // none at all.
        pane->onBoardSettings = [guard]() -> QJsonObject {
            auto *w = windowOf(guard);
            return w ? w->boardSettingsFor(w->pageOf(guard)) : QJsonObject{{QStringLiteral("attach"), false}};
        };
        // "Has this pane a board it may talk to?" A non-empty `reason` (projects::kReason*) makes
        // it an explicit project action: the tab attaches to the pane's candidate project first,
        // but only when that project already has a board — a candidate with none is answered with
        // one line and nothing is created. An empty reason attaches nothing and answers from the
        // tab alone, so the `#` index, the idle tip and the shortcut hints stay quiet in a pane
        // whose tab is attached to nothing.
        pane->onBoardProject = [guard](const QString &reason, QString *why) -> QString {
            auto *w = windowOf(guard);
            if (!w) return {};
            QWidget *page = w->pageOf(guard);
            if (const QString attached = w->tabProject(page); !attached.isEmpty()) return attached;
            if (reason.isEmpty()) return {};
            const QString candidate = relay::projects::candidateFor(guard->cwd());
            if (candidate.isEmpty() || relay::projects::boardDirOf(candidate).isEmpty()) {
                if (why)
                    *why = candidate.isEmpty()
                               ? QStringLiteral("No Switchboard here: no project above %1 has one.").arg(guard->cwd())
                               : QStringLiteral("%1 has no Switchboard yet.").arg(relay::projects::nameFor(candidate));
                return {};
            }
            w->attachTab(page, candidate, reason);
            return w->tabProject(page);
        };
        // ----- the init question (protocol 19.12): facts in, the pane decides ------------------
        // The rules are in src/ProjectInit.h and the pixels are in the pane; the window answers
        // only what it knows. An attached tab reports its own project rather than the pane's
        // candidate, so a pane that has `cd`-ed into another checkout never raises a question
        // whose yes would silently re-point the tab (#JN7X's rule, unchanged).
        pane->onProjectInitSituation = [guard]() -> relay::projectinit::Situation {
            relay::projectinit::Situation situation;
            auto *w = windowOf(guard);
            if (!w) return situation;
            const QString attached = w->tabProject(w->pageOf(guard));
            situation.project = attached.isEmpty() ? relay::projects::candidateFor(guard->cwd()) : attached;
            situation.cwd = guard->cwd();
            if (situation.project.isEmpty()) return situation;
            situation.hasBoard = !relay::projects::boardDirOf(situation.project).isEmpty();
            situation.declined = w->m_manager->projects().isDeclined(situation.project);
            situation.snoozed = w->m_manager->initSnoozed().contains(relay::projects::normalize(situation.project));
            // A Pane is always the desktop's own; a guest sees somebody else's through
            // relay::RemotePane, which has no worker of its own and never draws this question. The
            // flag is answered all the same so the rule is stated where the question is raised.
            situation.remote = false;
            return situation;
        };
        pane->onProjectAttach = [guard](const QString &project, const QString &reason) {
            if (auto *w = windowOf(guard)) w->attachTab(w->pageOf(guard), project, reason);
        };
        pane->onPickProject = [guard](const QString &heldCard) {
            if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->openProjectPicker(guard, heldCard); }
        };
        pane->onProjectInitEvent = [guard](const QString &project, const QString &what) {
            auto *w = windowOf(guard);
            if (!w || project.isEmpty()) return;
            // "Not now" is not a no: it silences trigger (1) — the agent prompt — for the rest of
            // this Relay session, and every explicit act asks again. Raising the question counts
            // the same way, so one project is asked about once per session however many panes
            // stand in it. A no is written to disk instead and nothing asks again, in any tab,
            // after any restart; `/init` is the one thing that clears it.
            if (what != QStringLiteral("reconsider"))
                w->m_manager->initSnoozed().insert(relay::projects::normalize(project));
            if (what == QStringLiteral("no")) w->m_manager->projects().decline(project);
            else if (what == QStringLiteral("reconsider")) w->m_manager->projects().undecline(project);
        };
        pane->onJoinShared = [guard](const QString &code) { if (auto *w = windowOf(guard)) w->joinSharedSession(code); };
        pane->onUpdateApp = [guard]() { if (auto *w = windowOf(guard)) w->updateApp(); };
        pane->onOpenCard = [guard](const QString &id) { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->openBoardCard(id); } };
        // The agent drives the app (#FEJQ, §30): the catalog this pane's worker is configured with,
        // and one `app_command` out of it, executed in the window the pane is in.
        pane->onAppCatalog = [guard]() -> QJsonObject {
            auto *w = windowOf(guard);
            return w ? w->appCatalogFor(w->pageOf(guard)) : QJsonObject{};
        };
        pane->onAppCommand = [guard](const QJsonObject &command) -> QJsonObject {
            auto *w = windowOf(guard);
            // `who` is the pane's session token: the change log says which agent did it, and the
            // notification reads the same either way.
            return w ? w->executeAppCommand(command, guard->sessionToken()) : QJsonObject{};
        };
        // The loop guard's reset (#AG7R group 8, §30.3): a prompt the person typed in this pane
        // ends the chain of agent-to-agent prompts that reached it, and lets its agent send again.
        pane->onPersonPrompt = [guard] {
            if (auto *w = windowOf(guard)) w->appCommands().notePersonPrompt(guard->sessionToken());
        };
        // Right-click menu entries the window owns (issue #X2F1).
        pane->onWindowAction = [guard](const QString &action) {
            auto *w = windowOf(guard);
            if (!w) return;
            w->setActiveLeaf(guard);
            if (action == QStringLiteral("splitRight")) w->runAction(QStringLiteral("pane.splitRight"));
            else if (action == QStringLiteral("splitDown")) w->runAction(QStringLiteral("pane.splitDown"));
            else if (action == QStringLiteral("splitSameHost")) w->runAction(QStringLiteral("ssh.splitSameHost"));
            else if (action == QStringLiteral("equalize")) w->runAction(QStringLiteral("pane.equalize"));
            else if (action == QStringLiteral("close")) w->closePane(guard, true);
        };
        // Grey the menu's "Equalize pane sizes" while this pane is alone in its tab: any other
        // leaf counts (an explorer beside it splits the tab just the same).
        pane->hasPaneSiblings = [guard]() -> bool {
            auto *w = windowOf(guard);
            return w && w->leavesIn(w->pageOf(guard)).size() > 1;
        };
        pane->onPlanWritten = [guard](const QString &path, Pane *) { if (auto *w = windowOf(guard)) w->openDocument(path, guard, true); };
        pane->onOpenDocument = [guard](const QString &path) { if (auto *w = windowOf(guard)) w->openDocument(path, guard, false); };
        pane->onForkState = [guard](const QJsonObject &state, const QString &title) { if (auto *w = windowOf(guard)) w->openFork(guard, state, title); };
        pane->onOpenSessionInNewPane = [guard](const QJsonObject &state, const QString &title) { if (auto *w = windowOf(guard)) w->openFork(guard, state, title, false); };
        // A guest session resumed in a new pane launches through that pane's own launch path (26.9):
        // the guest id and its arguments travel, not a finished command line.
        pane->onOpenGuestPane = [guard](const QString &guest, const QStringList &extra, const QString &cwd) {
            if (auto *w = windowOf(guard)) w->openGuestPane(guard, guest, extra, cwd);
        };
        pane->onOpenSubagent = [guard](const QString &id) { if (auto *w = windowOf(guard)) w->openSubagentTab(guard, id); };   // subagents UI (#WD83)
        pane->onShowAgents = [guard] { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->openAgentsMenu(); } };   // /agents → subagents panel menu
        pane->onOpenTurn = [guard](const QString &turnId) { if (auto *w = windowOf(guard)) w->openTurnPane(guard, turnId); };
        pane->onOpenInternals = [guard] { if (auto *w = windowOf(guard)) w->openInternalsPane(guard); };
        // The share chip, once this pane is shared: who is here and what is waiting (#W5N2).
        pane->onOpenSharing = [guard] { if (auto *w = windowOf(guard)) w->openSharingPane(guard, true); };
        pane->onOpenOptions = [guard](const QString &tab) {
            if (auto *w = windowOf(guard)) w->openSettingsPane(relay::SettingsPane::Mode::Options, tab);
        };
        // `/profile` swapped the five lists: the same two steps a switch on Options › Models takes.
        pane->onProfileApplied = [guard] {
            if (auto *w = windowOf(guard)) w->modelsCurated();
        };
        pane->onShareTab = [guard](int *panes) {
            auto *w = windowOf(guard);
            return w ? w->shareTabId(w->pageOf(guard), panes) : QString();
        };
        // A pane opened in a tab that is shared whole is shared as soon as it is in the tab.
        QTimer::singleShot(0, pane, [guard] { if (auto *w = windowOf(guard)) w->scheduleTabShareSync(); });
        // Protocol 23's four `local_*` events, and a `test_key` for a `local:` preset, belong to
        // Settings › Local models rather than to this pane's transcript (card #24XJ).
        pane->onLocalModelEvent = [guard](const QJsonObject &event) {
            if (auto *w = windowOf(guard)) w->localModels().handleEvent(event);
        };
        // A tool-call line whose diff is too big to read inline (#TK9C).
        pane->onOpenDiff = [guard](const QString &title, const QString &unifiedDiff) -> relay::DiffView * {
            if (auto *w = windowOf(guard)) return w->openDiffPane(guard, title, unifiedDiff);
            return nullptr;
        };
        // Session manager and ⓘ (cards #R6J0, #Y63Z).
        pane->onOpenSessions = [guard](const QString &query) { if (auto *w = windowOf(guard)) w->openSessionsFor(guard, query); };
        // An `option:sec/row` in this pane's output (#AGNT step 8): Options on that section, zoomed
        // to the row. The same two steps `app_open {target: "options", row}` takes, and the same
        // two the helper's own answers took before the link became a kind of the transcript.
        pane->onOpenOption = [guard](const QString &section, const QString &row) {
            auto *w = windowOf(guard);
            if (!w) return;
            w->openSettingsPane(relay::SettingsPane::Mode::Options, section);
            if (ToolPane *tool = w->settingsPaneIn(w->m_tabs->currentWidget(), relay::SettingsPane::Mode::Options);
                tool && tool->settings())
                tool->settings()->revealOption(section, row);
        };
        pane->onOpenInfo = [guard] { if (auto *w = windowOf(guard)) w->openInfoPane(guard); };
        pane->onOpenThreadInfo = [guard](const QString &threadId, const QString &dir, const QString &owner) {
            if (auto *w = windowOf(guard)) w->openInfoPane(guard, QString(), dir, threadId, owner);
        };
        // The first-launch approvals choice (card #K2FV): the approvals pane beside this one,
        // raised by Pane::onSessionConfigured while security/approvals_chosen is unset.
        pane->onApprovalsChoice = [guard] { if (auto *w = windowOf(guard)) w->openApprovalsPane(guard); };
        pane->onSessionOpenElsewhere = [guard](const QString &sessionId, const QString &dir) {
            Pane *other = paneWithSession(sessionId, dir, guard);
            auto *w = other ? dynamic_cast<RelayWindow *>(other->window()) : nullptr;
            if (!w) return false;
            w->revealPane(other);
            other->toast(QStringLiteral("This session was already open here"));
            return true;
        };
        // Pane titles (issue JRWQ): a fresh title refreshes the tab's tooltip (and the label of a
        // tab with no terminal pane); /rename-tab is the window's job.
        pane->onTitleChanged = [guard] { if (auto *w = windowOf(guard)) w->updateTitles(); };
        pane->onRenameTab = [guard](const QString &text, bool edit) {
            auto *w = windowOf(guard);
            if (w) w->renameTab(text, edit, w->pageOf(guard));
        };
        relay::theme::polishWindow(pane);
        return pane;
    }

    // ----- the window makes agent consoles (#AGNT step 5) --------------------------------------
    //
    // A console is a `Pane` with a non-terminal context: no shell, no pty, no poll timers, the
    // vterm kept as the transcript surface, and the queue, the thinking bubbles, the tool rows,
    // the model box and the Activity hook of a terminal pane (`7a35a498`). The pane libraries
    // cannot construct one — `Pane` exists only in this executable's translation unit — so a host
    // asks the window for one through `relay::agent::ConsoleFactory` and is handed the widget to
    // embed plus the handful of calls it makes on it. That is the `relay::PaneView` pattern, and
    // it is why `ConsoleHandle` carries `std::function`s rather than a type.
    //
    // What a console is deliberately **not** in: `panesIn` (which stops at a `ToolPane`, above),
    // and therefore the phone's publish list, the close count, the first-pane Flash default, the
    // scrollback store and `paneWithToken`. It is not serialised either — `serializeNode` returns
    // at the `ToolPane` branch without descending — so the host recreates it on restore.

    // What only the window knows about a console's context: which project the tab works in, and
    // where the tab's one conversation is kept. Everything else — the name, the brief, the action
    // row, the links, the placeholder — is the host's and is asked of it fresh, so a host is never
    // made to invent a tab id it has no way of learning.
    //
    // The consoles of one tab share one worker and one conversation (owner decision 1), which is
    // exactly what `persist {scope: "helper", key: <tab id>}` says: `helper` is a wire enum — which
    // store, not which path — and the key is the tab's own persistent id, so the same tab in the
    // same project comes back to the same file after a restart and two tabs on one project keep
    // two conversations.
    class TabConsoleContext final : public relay::agent::Context {
      public:
        TabConsoleContext(RelayWindow *window, relay::agent::Context *host)
            : m_window(window), m_host(host) {
            // The pane owns *this* context's `onChanged`; this is the other half of the chain, so
            // a card going busy or Options swapping mode still reaches the console's action row.
            if (m_host) m_host->onChanged = [this] { changed(); };
        }
        ~TabConsoleContext() override { if (m_host) m_host->onChanged = nullptr; }

        void setConsole(QWidget *console) { m_console = console; }

        relay::agent::ContextSpec spec() const override {
            relay::agent::ContextSpec spec = m_host ? m_host->spec() : relay::agent::ContextSpec();
            QWidget *page = m_window && m_console ? m_window->pageOf(m_console) : nullptr;
            // The tab's project, or none: a tab attached to nothing gets a board-less console,
            // which is a supported state and not an error (protocol 30.7).
            spec.workspace = page ? m_window->boardWorkspaceOfTab(page) : QString();
            spec.persistScope = QStringLiteral("helper");
            spec.persistKey = page ? page->property("relayTabId").toString() : QString();
            // The role every non-terminal surface answers on (protocol 13.1), spelled the way
            // `startBoardWorker` spells it so one worker cannot be asked for two.
            if (spec.agentRole.isEmpty()) spec.agentRole = QStringLiteral("switchboard");
            spec.shell = false;                        // the terminal context is the only one
            spec.routing = QStringLiteral("agent");    // there is nothing else for a line to go to
            if (spec.surface.isEmpty()) spec.surface = spec.name;
            return spec;
        }
        QList<relay::agent::Action> actions() const override {
            return m_host ? m_host->actions() : QList<relay::agent::Action>();
        }
        // Every virtual of `relay::agent::Context` is forwarded, and `submit` is the one that
        // must be: a card's Enter has to travel as the `board_ask` of 19.10 — the thread written
        // before and after, the stage advanced — and a wrapper that swallowed it sent an ordinary
        // `ask` instead, so the card's conversation ran and `issues/threads/<ID>.md` learned
        // nothing (owner decision 2). Nothing in `consolemode` saw it, because those cases hand a
        // context to a `Pane` directly; every console the *window* makes is wrapped.
        bool submit(const QString &route, const QString &text) override {
            return m_host && m_host->submit(route, text);
        }
        bool resolveLink(const relay::links::Target &target) override {
            return m_host && m_host->resolveLink(target);
        }
        void turnFinished(const relay::agent::TurnRecord &record) override {
            if (m_host) m_host->turnFinished(record);
        }
        QString placeholder() const override { return m_host ? m_host->placeholder() : QString(); }

      private:
        QPointer<RelayWindow> m_window;
        relay::agent::Context *m_host;   // the host's, and it outlives the console (the API says so)
        QPointer<QWidget> m_console;
    };

    // One console, made for a host that asked for one. `parent` is the widget it will be embedded
    // in; the host puts the returned `widget` in its own layout and owns nothing else.
    relay::agent::ConsoleHandle createAgentConsole(relay::agent::Context *context, QWidget *parent) {
        relay::agent::ConsoleHandle handle;
        if (!context) return handle;
        auto wrapper = std::make_shared<TabConsoleContext>(this, context);
        // A console has no shell, so it stands nowhere: the directory is the window's workspace,
        // and the *agent's* workspace is the tab's project, which `TabConsoleContext::spec()`
        // answers fresh on every configure.
        const QString here = m_manager->workspace();
        auto *console = new Pane(here, here, m_manager->cleanShell(), relay::defaultEngineCore(), wrapper.get());
        wrapper->setConsole(console);
        if (parent) console->setParent(parent);
        console->setObjectName(QStringLiteral("agentConsole"));
        m_consoles.append(ConsoleEntry{QPointer<Pane>(console), wrapper});
        // The wrapper goes *with* its console, not at the next worker event. Its destructor writes
        // to the host's context (`onChanged = nullptr`), and the API's promise is only that the
        // host outlives the console — `~BoardView` deletes its consoles first for exactly that —
        // not that it outlives this list. Pruned lazily, a Switchboard pane that was closed left
        // its entry here until the tab's worker next spoke, and the wrapper then wrote into a
        // freed BoardView: a bus error the first time a phone touched the board after the pane
        // was closed (#SWPH's hosted drive), and the same for any other event of that worker.
        // By address as well as by a null QPointer: a widget emits `destroyed` from `~QWidget`,
        // before `~QObject` has cleared the pointers that guard it.
        connect(console, &QObject::destroyed, this, [this](QObject *gone) {
            for (int i = int(m_consoles.size()) - 1; i >= 0; --i) {
                const Pane *pane = m_consoles.at(i).pane.data();
                if (!pane || static_cast<const QObject *>(pane) == gone) m_consoles.removeAt(i);
            }
        });
        wireAgentConsole(console);
        QPointer<Pane> guard(console);
        // The tab's worker, and the handshake it has already had. Deferred one turn of the event
        // loop because the host is still building: the console is not in a tab until its
        // `ToolPane` is inserted, and `pageOf` answers nothing before that — the same wait the
        // Switchboard's own `board_open` takes.
        QTimer::singleShot(0, console, [guard] { if (auto *w = windowOf(guard)) w->attachConsoleToTab(guard); });
        handle.widget = console;
        handle.focusComposer = [guard] { if (guard) guard->focusComposer(); };
        handle.draftInComposer = [guard](const QString &text) { if (guard) guard->draftInComposer(text); };
        handle.composerText = [guard] { return guard ? guard->composerText() : QString(); };
        handle.setCollapsed = [guard](bool collapse) { if (guard) guard->setCollapsed(collapse); };
        handle.collapsed = [guard] { return guard && guard->collapsed(); };
        handle.runActionLetter = [guard](const QString &letter) { return guard && guard->runActionLetter(letter); };
        handle.setTranscriptHiddenUntilUsed = [guard](bool on) { if (guard) guard->setTranscriptHiddenUntilUsed(on); };
        return handle;
    }

    // The callbacks a console shares with a terminal pane, and only those. A console is not in any
    // leaf list, so everything here reaches the window through `windowOf` exactly as `createPane`
    // does; what is left out is left out on purpose and said so, because the next person to add a
    // callback to `createPane` will come looking here.
    void wireAgentConsole(Pane *console) {
        QPointer<Pane> guard(console);
        console->onStatus = [guard](const QString &text) { if (guard) guard->toast(text, 5000); };
        // Its line goes on the tab's worker, which is started by the first thing it says.
        console->onWorkerLine = [guard](const QJsonObject &message) {
            if (auto *w = windowOf(guard)) w->sendFromConsole(guard, message);
        };
        // The links an answer can carry, resolved the way a terminal pane resolves them — the
        // context gets first refusal inside the pane, and what it declines arrives here.
        console->onOpenPath = [guard](const QString &path, int line) {
            if (auto *w = windowOf(guard)) w->openPath(path, line, hostLeafOf(guard));
        };
        console->onOpenCard = [guard](const QString &id) { if (auto *w = windowOf(guard)) w->openBoardCard(id); };
        console->onOpenOption = [guard](const QString &section, const QString &row) {
            auto *w = windowOf(guard);
            if (!w) return;
            w->openSettingsPane(relay::SettingsPane::Mode::Options, section);
            if (ToolPane *pane = w->settingsPaneIn(w->m_tabs->currentWidget(), relay::SettingsPane::Mode::Options);
                pane && pane->settings())
                pane->settings()->revealOption(section, row);
        };
        console->onOpenSessions = [guard](const QString &query) {
            if (auto *w = windowOf(guard)) w->openSessions(QString(), query);
        };
        console->onOpenDocument = [guard](const QString &path) {
            if (auto *w = windowOf(guard)) w->openDocument(path, w->paneForConsoleOpen(guard), false);
        };
        console->onOpenTurn = [guard](const QString &turnId) {
            if (auto *w = windowOf(guard)) w->openTurnPane(w->paneForConsoleOpen(guard), turnId);
        };
        console->onOpenInternals = [guard] {
            if (auto *w = windowOf(guard)) w->openInternalsPane(w->paneForConsoleOpen(guard));
        };
        console->onOpenDiff = [guard](const QString &title, const QString &unifiedDiff) -> relay::DiffView * {
            if (auto *w = windowOf(guard)) return w->openDiffPane(w->paneForConsoleOpen(guard), title, unifiedDiff);
            return nullptr;
        };
        console->onOpenOptions = [guard](const QString &tab) {
            if (auto *w = windowOf(guard)) w->openSettingsPane(relay::SettingsPane::Mode::Options, tab);
        };
        console->onOpenInfo = [guard] { if (auto *w = windowOf(guard)) w->openInfoPane(w->paneForConsoleOpen(guard)); };
        console->onToggleExplorer = [guard](const QString &path) {
            if (auto *w = windowOf(guard)) w->toggleExplorer(path, hostLeafOf(guard));
        };
        console->onShowAgents = [guard] { if (auto *w = windowOf(guard)) w->openAgentsMenu(); };
        console->onOpenSubagent = [guard](const QString &id) {
            if (auto *w = windowOf(guard)) w->openSubagentTab(w->paneForConsoleOpen(guard), id);
        };
        // The app catalog of the tab it is in (protocol 30.2). The *answers* to `app_command` are
        // not this console's: one worker serves every console of the tab, and the window already
        // answers that pipe once, tagged `helper` — four consoles answering the same write would
        // run it four times. That is why `onAppCommand` is deliberately not set here, and why
        // `deliverToConsoles` drops `app_command`.
        console->onAppCatalog = [guard]() -> QJsonObject {
            auto *w = windowOf(guard);
            return w ? w->appCatalogFor(w->pageOf(guard)) : QJsonObject{};
        };
        // Protocol 23's `local_*` events belong to Options › Local models wherever they arrive.
        console->onLocalModelEvent = [guard](const QJsonObject &event) {
            if (auto *w = windowOf(guard)) w->localModels().handleEvent(event);
        };
        console->onChooseTheme = [guard](const QString &id) {
            if (auto *w = windowOf(guard)) {
                const bool asDefault = themeCommandsSetDefault();
                const bool ok = w->chooseTheme(id, w->pageOf(guard), asDefault);
                if (ok && asDefault) w->refreshSettingsPanes();
                return ok;
            }
            return relay::theme::setActiveTheme(id);
        };
        console->onProfileApplied = [guard] {
            if (auto *w = windowOf(guard)) w->modelsCurated();
        };
        console->onUpdateApp = [guard]() { if (auto *w = windowOf(guard)) w->updateApp(); };
        console->onJoinShared = [guard](const QString &code) { if (auto *w = windowOf(guard)) w->joinSharedSession(code); };
        // The `board` block of its `configure` is the tab's, exactly as a pane's is.
        console->onBoardSettings = [guard]() -> QJsonObject {
            auto *w = windowOf(guard);
            return w ? w->boardSettingsFor(w->pageOf(guard)) : QJsonObject{{QStringLiteral("attach"), false}};
        };
        // What is **not** wired, and why: `onShellExited`, `onOpenGuestPane`, `onForkState`,
        // `onOpenSessionInNewPane`, `onSessionOpenElsewhere`, `onPlanWritten` and the project-init
        // question are a terminal pane's (there is no shell, no guest, no pane to fork into and no
        // directory the console stands in); `onTitleChanged` / `onRenameTab` / `hasPaneSiblings` /
        // `onWindowAction` / `onShareTab` / `onOpenSharing` belong to a leaf, and a console is not
        // one; and `onPersonPrompt` is a pane agent's loop guard, keyed by a pane's session token.
        relay::theme::polishWindow(console);
    }

    // The leaf a console is embedded in — the host's `ToolPane`. A console is not itself a leaf,
    // so "open this beside me" has to name the widget the window's splitters do know about, or
    // `insertBeside` would replace the console inside its host's own layout.
    static QWidget *hostLeafOf(QWidget *console) {
        for (QWidget *w = console ? console->parentWidget() : nullptr; w; w = w->parentWidget())
            if (dynamic_cast<ToolPane *>(w)) return w;
        return nullptr;
    }

    // The terminal pane the openers that insist on one use for a console: the tab's active pane
    // if the tab has one, else its first. Activity, ⓘ, a turn view, a diff and a subagent tab are
    // all `openX(Pane *owner, …)` — they read the owner's cwd and dock beside it — and a console
    // has neither. With no terminal pane in the tab there is nothing to open beside, and every one
    // of those openers already returns on a null owner. It is the same fallback `openSessions`
    // has taken since #R6J0.
    Pane *paneForConsoleOpen(QWidget *console) const {
        QWidget *page = pageOf(console);
        if (!page) return nullptr;
        if (auto *active = dynamic_cast<Pane *>(m_activeLeaf.data()); active && pageOf(active) == page)
            return active;
        const QList<Pane *> panes = panesIn(page);
        return panes.isEmpty() ? nullptr : panes.first();
    }

    // `ready` and `configured`, per tab. The separator is built rather than spelled as a hex
    // escape in the string: a hex escape runs on into the hex digits of the word after it, so the
    // one naming `configured` would be a single out-of-range character, not a separator and a
    // word — a warning, and a key that never matches.
    static QString handshakeKey(const QString &tab, const QString &type) {
        return tab + QChar(QChar::fromLatin1(0x1f)) + type;
    }

    // The console is in its tab now: start the tab's worker and hand it the handshake it missed.
    //
    // This is where "the tab's agent starts at the first ask" (owner decision 5, 30.7) becomes
    // "at the first console". It has to: a pane refuses to submit while it is unconfigured, so a
    // console that had never heard `configured` would answer the first question with the provider
    // dialog rather than with an agent. The rule survives one step out instead — Options, Actions
    // and Sessions create their console on first *expand*, so a tab whose helper nobody opens
    // still pays for nothing.
    void attachConsoleToTab(Pane *console) {
        QWidget *page = pageOf(console);
        if (!page) return;
        const QString tab = tabIdOf(page);
        if (tab.isEmpty()) return;
        m_tabConsole.insert(tab, QPointer<Pane>(console));
        console->contextChanged();      // the workspace and the persist key are answerable now
        helperWorker(page, true);
        // A worker that was already up said `ready` and `configured` before this console existed
        // and will not say them again until the next reconfigure, which may never come. Both are
        // replayed, in that order, so the console is configured before it can be asked anything.
        if (const QJsonObject ready = m_workerHandshake.value(handshakeKey(tab, QStringLiteral("ready"))); !ready.isEmpty())
            console->deliverWorkerEvent(ready);
        if (const QJsonObject configured = m_workerHandshake.value(handshakeKey(tab, QStringLiteral("configured"))); !configured.isEmpty())
            console->deliverWorkerEvent(configured);
    }

    // A console's line, on its tab's worker. The worker is started here — `helperWorker(page,
    // true)` is the same call a Switchboard pane makes — and `startBoardWorker` builds the one
    // `configure` there is, carrying this console's `context` block. Asking from a second console
    // of the tab therefore reconfigures the brief and leaves the conversation exactly where it is:
    // `persist` does not move, and protocol 30.7's move test is precisely that.
    void sendFromConsole(Pane *console, const QJsonObject &message) {
        QWidget *page = console ? pageOf(console) : nullptr;
        if (!page) return;
        // The window owns this worker's `configure`. A console building one of its own would
        // overwrite the tab's context block and its conversation key with a pane's; a model
        // picked in its box reaches the worker the way every other helper model box does, by
        // writing the `switchboard` role and calling `reconfigureBoardWorkers()`.
        if (message.value(QStringLiteral("type")).toString() == QStringLiteral("configure")) return;
        m_tabConsole.insert(tabIdOf(page), QPointer<Pane>(console));
        if (relay::BoardWorker *worker = helperWorker(page, true)) worker->send(message);
    }

    // Every event of a tab's worker, to every console embedded in that tab. Nothing is filtered by
    // `surface`: the conversation is one (owner decision 1), so a console draws what it can of it
    // and ignores the rest. `app_command` is the exception — the window answers that pipe once for
    // the tab, and a console answering it as well would run the same write twice.
    void deliverToConsoles(const QString &tab, const QJsonObject &event) {
        if (event.value(QStringLiteral("event")).toString() == QStringLiteral("app_command")) return;
        for (int i = int(m_consoles.size()) - 1; i >= 0; --i)
            if (!m_consoles.at(i).pane) m_consoles.removeAt(i);
        const QList<ConsoleEntry> entries = m_consoles;   // a handler may close a pane
        for (const ConsoleEntry &entry : entries) {
            if (!entry.pane) continue;
            QWidget *page = pageOf(entry.pane);
            if (page && tabIdOf(page) == tab) entry.pane->deliverWorkerEvent(event);
        }
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
            // Either spelling of the board folder (protocol 19.12): a saved Switchboard whose
            // project keeps its cards in `issues/` restores exactly like one that uses
            // `switchboard/`.
            if (!relay::projects::boardDirOf(workspace).isEmpty()) {
                ToolPane *tool = createBoardPane(workspace, board.value(QStringLiteral("collapsed")).toArray(),
                                                 board.value(QStringLiteral("hidden")).toArray(),
                                                 board.value(QStringLiteral("sort")).toString(),
                                                 board.value(QStringLiteral("labels")).toArray(),
                                                 board.value(QStringLiteral("self_closed")).toArray());
                // Which of the signals rows were open (#AQ6X), set on the view rather than through
                // one more argument: both are folded by default, so an older node opens nothing.
                if (relay::BoardView *board_view = tool->board()) {
                    board_view->setOpenSignals(board.value(QStringLiteral("signals")).toArray());
                    board_view->restoreNavigation(board.value(QStringLiteral("navigation")).toObject());
                }
                // A restored Switchboard attaches its tab, unless the tab already has a project —
                // the saved `project` on the tab wins, and a tab holds one. Queued, because
                // buildNode() runs before the page the pane will live in exists.
                QPointer<ToolPane> guard(tool);
                QTimer::singleShot(0, tool, [guard, workspace] {
                    auto *w = windowOf(guard);
                    if (!w) return;
                    QWidget *page = w->pageOf(guard);
                    if (page && w->tabProject(page).isEmpty())
                        w->attachTab(page, workspace, QString::fromLatin1(relay::projects::kReasonRestored));
                });
                return tool;
            }
            // The project lost its board (or moved): a terminal in the directory this pane was
            // saved in, which is the project the person was working in. It used to open in the
            // launch directory, quietly moving the pane to another checkout.
            const QString fallback = relay::windowstate::resolveDirectory(
                workspace, m_manager->workspace(), QDir::homePath());
            return createPane({{"cwd", fallback}, {"workspace", fallback}});
        }
        if (node.contains(QStringLiteral("internals"))) {   // card #QT8C: empty until the next event, beside its owner
            const QJsonObject saved = node.value(QStringLiteral("internals")).toObject();
            ToolPane *tool = createInternalsPane(saved.value(QStringLiteral("cwd")).toString());
            QPointer<ToolPane> guard(tool);
            const QString owner = saved.value(QStringLiteral("owner")).toString();
            QTimer::singleShot(0, tool, [guard, owner] { if (auto *w = windowOf(guard)) w->linkRestoredInternalsPane(guard, owner); });
            return tool;
        }
        if (node.contains(QStringLiteral("testsuites"))) {   // card #7BM4: empty until its worker answers
            const QJsonObject saved = node.value(QStringLiteral("testsuites")).toObject();
            ToolPane *tool = createTestSuitesPane(saved.value(QStringLiteral("cwd")).toString());
            QPointer<ToolPane> guard(tool);
            QTimer::singleShot(0, tool, [guard] { if (auto *w = windowOf(guard)) w->linkRestoredTestSuitesPane(guard); });
            return tool;
        }
        if (node.contains(QStringLiteral("subagents"))) {   // card #WD83: the tabs' text, then its owner
            const QJsonObject saved = node.value(QStringLiteral("subagents")).toObject();
            ToolPane *tool = createSubagentPane(saved.value(QStringLiteral("cwd")).toString(), saved);
            QPointer<ToolPane> guard(tool);
            const QString owner = saved.value(QStringLiteral("owner")).toString();
            QTimer::singleShot(0, tool, [guard, owner] { if (auto *w = windowOf(guard)) w->linkRestoredSubagentPane(guard, owner); });
            return tool;
        }
        if (node.contains(QStringLiteral("settings"))) {   // card #XAME: Options or Actions, back where it was read
            const QJsonObject saved = node.value(QStringLiteral("settings")).toObject();
            const auto mode = saved.value(QStringLiteral("mode")).toString() == QStringLiteral("actions")
                                  ? relay::SettingsPane::Mode::Actions : relay::SettingsPane::Mode::Options;
            ToolPane *tool = createSettingsPane(mode);
            const QString tab = saved.value(QStringLiteral("tab")).toString();
            const QString search = saved.value(QStringLiteral("search")).toString();
            const QString row = saved.value(QStringLiteral("row")).toString();
            if (!tab.isEmpty()) tool->settings()->showTab(tab);
            if (!search.isEmpty()) tool->settings()->setSearch(search);
            // A row is revealed only when no search was on: revealOption() clears the search, and
            // with one the row was a result line, not a section row.
            if (!tab.isEmpty() && !row.isEmpty() && search.isEmpty()) tool->settings()->revealOption(tab, row);
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
            if (!pane->paneModel().isEmpty()) leaf.insert(QStringLiteral("model"), pane->paneModel());
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

    // ----- themes per tab (owner, 2026-09-19) -------------------------------------------------
    // A tab owns a theme (the page's "relayTheme" property). The tokens, the palette and the
    // stylesheet are the application's, so "per tab" means: the tab in front of the window in
    // front decides, and applyTabTheme() makes it so on every tab change and window activation.
    // Two windows therefore never show two themes at once; the one you are in wins.
    static bool perTabThemes() { return QSettings().value(QStringLiteral("theme/per_tab"), true).toBool(); }
    static bool newTabNewTheme() { return QSettings().value(QStringLiteral("theme/new_tab_new_theme"), false).toBool(); }
    // "Start each new tab on a random theme" (card #R4ND, owner 2026-09-20: "i meant a persistent
    // mode. it randomizes on each new tab"). Mutually exclusive with the cycling one above —
    // the two Options rows switch each other off.
    static bool randomNewTabTheme() { return QSettings().value(QStringLiteral("theme/randomize_new_tab"), false).toBool(); }
    // Owner, 2026-09-19: "/light or /dark or /theme … should [change the new-tab default]. but in
    // options you can disable that". On: the command's theme is also what Relay opens on and what
    // the next new tab starts with, exactly as if it had been picked in Options.
    static bool themeCommandsSetDefault() { return QSettings().value(QStringLiteral("theme/commands_set_default"), true).toBool(); }
    static QString tabThemeOf(QWidget *page) { return page ? page->property("relayTheme").toString() : QString(); }

    void applyTabTheme(QWidget *page) {
        if (!page || !perTabThemes()) return;
        QString id = tabThemeOf(page);
        if (id.isEmpty()) id = relay::theme::startupThemeId();
        if (id != relay::theme::activeThemeId()) relay::theme::setActiveTheme(id, false);
    }

    // /light, /dark, /theme (asDefault = false) and the Options picker (true: it also becomes what
    // Relay opens on and what a new tab starts with). With per-tab themes off, every choice is the
    // application's and is stored, as it always was.
    bool chooseTheme(const QString &id, QWidget *page, bool asDefault) {
        if (!perTabThemes() || !page) return relay::theme::setActiveTheme(id, true);
        if (relay::theme::specFor(id).id != id) return false;
        // The other tabs keep what they have been showing: a tab that never chose is pinned to it
        // now, or it would follow this choice the next time it came forward.
        for (int i = 0; i < m_tabs->count(); ++i)
            if (QWidget *other = m_tabs->widget(i); other != page && tabThemeOf(other).isEmpty())
                other->setProperty("relayTheme", relay::theme::activeThemeId());
        page->setProperty("relayTheme", id);
        if (page == m_tabs->currentWidget()) { if (!relay::theme::setActiveTheme(id, asDefault)) return false; }
        else if (asDefault) relay::theme::setActiveTheme(id, true);
        m_tabs->tabBar()->update();
        m_manager->scheduleSave();
        return true;
    }

    // Options › Appearance › Randomize (card #R4ND). A theme other than the one in front of you,
    // applied the way the picker applies one — this tab now, and the default for the next tab —
    // and then named in a notice, because a theme you cannot name is one you cannot ask for again.
    bool randomizeTheme() {
        const QString id = relay::theme::randomThemeId({relay::theme::activeThemeId()});
        if (id.isEmpty()) {
            notice(QStringLiteral("There is only one theme installed, so there is nothing to randomize."), 6000);
            return false;
        }
        if (!chooseTheme(id, m_tabs->currentWidget(), true)) {
            notice(QStringLiteral("That theme could not be read."), 6000);
            return false;
        }
        notice(QStringLiteral("Theme: %1.").arg(relay::theme::active().name), 4000);
        // WARP.md's standing rule: the button duplicates a faster path, so it teaches it.
        hint(QStringLiteral("theme.random"), QStringLiteral("Next time: /theme random in any prompt box"));
        return true;
    }

    // "Start each new tab on the next theme": the list order, continuing from the last tab that
    // was started this way (the rotation is the application's, not a window's).
    void startNewTabTheme(QWidget *page) {
        if (!page || !perTabThemes()) return;
        // The persistent Randomize (card #R4ND): the tab takes a theme drawn at random — never
        // the default and never the previous tab's, so a run of new tabs repeats nothing it can
        // avoid. With only two themes installed the default is allowed back (every other tab
        // being the same theme is worse); with one there is nothing to draw. It goes ahead of
        // the cycling mode for the profile that has both keys on, since its row switches the
        // cycling one off.
        if (randomNewTabTheme()) {
            static QString last;
            QString id = relay::theme::randomThemeId({relay::theme::startupThemeId(), last});
            if (id.isEmpty()) id = relay::theme::randomThemeId({last});
            if (id.isEmpty()) return;
            last = id;
            page->setProperty("relayTheme", id);
            if (page == m_tabs->currentWidget()) applyTabTheme(page);
            m_tabs->tabBar()->update();
            m_manager->scheduleSave();
            return;
        }
        if (!newTabNewTheme()) return;
        const QList<relay::theme::ThemeChoice> themes = relay::theme::availableThemes();
        if (themes.size() < 2) return;
        static QString last;
        if (last.isEmpty()) last = relay::theme::startupThemeId();
        int at = 0;
        for (int i = 0; i < themes.size(); ++i) if (themes.at(i).id == last) at = i;
        last = themes.at((at + 1) % themes.size()).id;
        page->setProperty("relayTheme", last);
        if (page == m_tabs->currentWidget()) applyTabTheme(page);
        m_tabs->tabBar()->update();
        m_manager->scheduleSave();
    }

    QJsonObject serializeTab(int index) const {
        QWidget *page = m_tabs->widget(index);
        QWidget *root = page && page->layout() && page->layout()->count() ? page->layout()->itemAt(0)->widget() : nullptr;
        const QJsonObject node = serializeNode(root);
        // An attached tab saves as {"project", "node"} and an unattached one as the bare node it
        // always was, so nothing changes for the quiet default and the schema does not move
        // (src/WindowState.h). An empty node still reads as empty: restorableTabs() drops it.
        const QString project = tabProject(page);
        // A tab's own theme rides in the same wrapper, and only when it is not the default, so a
        // layout nobody themed is what it always was.
        const QString theme = perTabThemes() ? tabThemeOf(page) : QString();
        const bool ownTheme = !theme.isEmpty() && theme != relay::theme::startupThemeId();
        // The tab's persistent id (#FEJQ, §30.7) rides in the same wrapper, and only when the tab
        // has one: it is minted at the first thing that needs it — a helper worker, or an `app`
        // block going to a pane agent — so a layout of tabs nobody has asked anything is exactly
        // the shape it always was. Without it a restart would hand the tab's helper somebody
        // else's conversation, or start it a new one every time.
        const QString id = page ? page->property("relayTabId").toString() : QString();
        if (node.isEmpty() || (project.isEmpty() && !ownTheme && id.isEmpty())) return node;
        QJsonObject tab{{QStringLiteral("node"), node}};
        if (!project.isEmpty()) tab.insert(QStringLiteral("project"), project);
        if (ownTheme) tab.insert(QStringLiteral("theme"), theme);
        if (!id.isEmpty()) tab.insert(QStringLiteral("tab_id"), id);
        return tab;
    }

    void setActive(Pane *pane) { setActiveLeaf(pane); }

    // The mouse is the slow way between panes; the Alt+arrows are the fast one.
    void hintPaneFocusByMouse() {
        hint(QStringLiteral("pane.focus.mouse"), QStringLiteral("Next time: %1 / %2 / %3 / %4 moves between panes").arg(
            Keymap::instance().shortcutText(QStringLiteral("pane.focusLeft")), Keymap::instance().shortcutText(QStringLiteral("pane.focusRight")),
            Keymap::instance().shortcutText(QStringLiteral("pane.focusUp")), Keymap::instance().shortcutText(QStringLiteral("pane.focusDown"))));
    }

    // A press anywhere in a pane makes that pane the active one (issue #H3TQ).
    //
    // Focus alone used to decide it, so only the surfaces that take the keyboard moved the active
    // frame: the prompt box, a list, a text field. Everything else a pane is mostly made of — the
    // terminal body (`Qt::NoFocus` under the prompt-box-only rules), the header and its title, the
    // chips, the pane's own margin — left the old pane active, and the next keystroke went to a
    // pane the user had clicked away from.
    //
    // Returns whether the active pane changed; the press itself is never consumed, so every click
    // still does what it did.
    bool activateOnPress(QObject *object, QEvent *event) {
        if (event->type() != QEvent::MouseButtonPress) return false;
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget || widget->window() != this) return false;
        QWidget *leaf = leafOf(widget);
        if (!leaf || leaf == m_activeLeaf) return false;
        setActiveLeaf(leaf);
        hintPaneFocusByMouse();
        // The press has not moved the keyboard yet — a widget that takes click focus does that
        // when the event reaches it, after this filter. So ask on the next turn instead of
        // guessing from focus policies: if nothing in the new pane ended up with the keyboard,
        // give it to its prompt box, which is what the frame now says is listening. A press that
        // opened a popup (the model box, a menu) is left alone: focusing behind it would shut it.
        QPointer<QWidget> guard(leaf);
        QTimer::singleShot(0, this, [this, guard] {
            if (!guard || m_activeLeaf != guard.data() || QApplication::activePopupWidget()) return;
            if (leafOf(QApplication::focusWidget()) != guard.data()) focusLeaf(guard);
        });
        return true;
    }

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

    // The pane frame, its composer and its header text all follow "relayActive", so each of them
    // has to be given the property and repolished.
    static void repolishLeaf(QWidget *leaf) {
        const bool active = leaf->property("relayActive").toBool();
        QList<QWidget *> widgets{leaf};
        for (auto *editor : leaf->findChildren<QPlainTextEdit *>(QStringLiteral("composerEditor")))
            if (auto *frame = qobject_cast<QFrame *>(editor->parentWidget())) {
                frame->setProperty("relayActive", active);
                widgets.append(frame);
            }
        // The pane's name is at full strength on the active pane and muted on every other one
        // (issue #H3TQ): the second half of telling them apart, and grey like the frame, because
        // the accent means "shell" in Relay's visual language and a pane must not compete with
        // the composer (data/theme/themes/relay-dark.toml, `border_strong`). The path beside it
        // stays legible in both states — it is the pane's address, not a focus mark.
        for (auto *label : leaf->findChildren<QLabel *>(QStringLiteral("paneTitle"))) {
            label->setProperty("relayActive", active);
            widgets.append(label);
        }
        for (QWidget *w : widgets) { w->style()->unpolish(w); w->style()->polish(w); w->update(); }
    }

    static QString shortPath(const QString &path) {
        const QString home = QDir::homePath();
        if (path == home) return QStringLiteral("~");
        const QString name = QFileInfo(path).fileName();
        return name.isEmpty() ? path : name;
    }

    // ----- tab labels -----------------------------------------------------------------------
    // A tab is labelled after where it is (owner, 2026-09-19: "the repo name of the associated
    // project, otherwise the folder of the active pane"), not after what its panes are doing: a
    // hand-set name first, then the tab's attached project (#JN7X), then the repo of its active
    // pane's directory, then that directory's own folder. The agent-written pane titles stay in
    // the pane headers and in the tab's tooltip; the joined-titles label below survives only for a
    // tab with no terminal pane at all, answered offline — nothing asks a model about a tab.
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
        const QString place = placeTabTitle(page);
        if (!place.isEmpty()) return place;
        if (titles.isEmpty()) return QStringLiteral("Relay");
        return relay::titles::join(titles, relay::titles::relatedText(titles));
    }

    // Where a tab is: the repo name of the project it is attached to (#JN7X), else of the project
    // that contains its active pane's directory, else that directory's own folder. The active pane
    // is the tab's last active leaf when that is a terminal, else its first terminal.
    QString placeTabTitle(QWidget *page) const {
        const QString attached = tabProject(page);
        if (!attached.isEmpty()) return relay::projects::nameFor(attached);
        Pane *pane = dynamic_cast<Pane *>(m_lastActive.value(page).data());
        if (!pane)
            for (QWidget *leaf : leavesIn(page))
                if ((pane = dynamic_cast<Pane *>(leaf))) break;
        if (!pane) return QString();
        return relay::titles::placeTitle(pane->cwd(), relay::projects::candidateFor(pane->cwd()));
    }

    void forgetTab(QWidget *page) {
        m_tabNames.remove(page);
        m_tabProject.remove(page);   // the tab is going: its project goes with it (#JN7X)
        releaseBoardWorker(page);    // and its helper agent with it (#FEJQ, §30.7)
    }

    // ---- "Share whole tab" (owner, 2026-09-18) ----------------------------------------------------
    // A tab page's id for sharing, made the first time somebody asks. It is a property of the page
    // widget, so it follows the tab into another window; it is never saved, because a share does
    // not outlive the process.
    QString shareTabId(QWidget *page, int *panes = nullptr) const {
        if (!page) return QString();
        QString id = page->property("relayShareTab").toString();
        if (id.isEmpty()) {
            id = QStringLiteral("tab-") + QUuid::createUuid().toString(QUuid::Id128).left(10);
            page->setProperty("relayShareTab", id);
        }
        if (panes) *panes = int(panesIn(page).size());
        return id;
    }

    void scheduleTabShareSync() {
        if (m_tabShareSyncQueued) return;
        m_tabShareSyncQueued = true;
        QTimer::singleShot(0, this, [this] { m_tabShareSyncQueued = false; syncTabShares(); });
    }

    // Make sharing match the tabs: every terminal in a tab shared whole is shared under that tab,
    // and one shared under a tab it is no longer in follows it — into the tab it is in now if that
    // one is shared whole, else out of the share altogether, since it was the tab that shared it.
    void syncTabShares() {
        relay::RemoteShare &share = relay::RemoteShare::instance();
        if (!share.hasTabShares()) return;   // unshareTab already ended every pane it shared
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            const QString id = page->property("relayShareTab").toString();
            const bool whole = share.isTabShared(id);
            for (Pane *pane : panesIn(page)) {
                const QString token = pane->sessionToken();
                const QString under = share.tabOf(token);
                if (whole) {
                    if (!share.isSharing(token) || under != id) pane->shareUnderTab(id);
                } else if (!under.isEmpty() && share.isSharing(token)) {
                    share.stopSharing(token);
                    pane->toast(QStringLiteral("No longer shared — this pane left a shared tab."), 5000);
                }
            }
        }
    }

    // ----- remote control: every pane published as it appears (#PH0N, phase 1.2) ---------------
    // While the switch in Options › Remote is on, a pane with a screen is reachable by the phones
    // the owner has paired from the moment it exists, with no share button pressed — that is what
    // "all day, from the bus" means. Guests are untouched: the sidecar shows a guest only the panes
    // their invite names. Withdrawing needs nothing here, because the share ends with the pane's
    // view (RemoteShare::sharePane connects its destruction to stopSharing, which sends `unpane`).
    //
    // Silent on purpose: this is a service rather than something the person just did, and a toast
    // per pane at every start would be the loudest thing in the window. The indicator in the
    // chrome is what says it is on.
    void syncAlwaysOnShares() {
        relay::RemoteShare &share = relay::RemoteShare::instance();
        if (!share.alwaysOn()) return;
        QSet<QString> live;
        for (Pane *pane : allPanes()) {
            const QString token = pane->sessionToken();
            if (token.isEmpty()) continue;
            live.insert(token);
            if (share.isSharing(token) || m_autoShared.contains(token)) continue;
            // Remembered whether it worked or not: a pane with no screen to share must not be
            // asked again every second, and one that fails because the sidecar is down would only
            // fail the same way. The switch moving clears the set and tries the lot again.
            m_autoShared.insert(token);
            QString error;
            pane->startSharing(QString(), &error);
        }
        m_autoShared.intersect(live);   // a closed pane's token is nobody's business
    }

    // What the plug and Options › Remote show. `remote_state` is the sidecar's own word and is
    // taken as soon as it arrives; until it does, the switch is all this desktop knows.
    relay::remotesettings::State shownRemoteState() const {
        relay::RemoteShare &share = relay::RemoteShare::instance();
        relay::remotesettings::State state = share.remoteState();
        if (share.alwaysOn() && !state.on) {
            state.on = true;
            if (state.address.isEmpty()) state.address = relay::remotesettings::address();
        }
        return state;
    }

    // The dot on the join plug: the owner's phones that are connected right now (#PH0N). It is the
    // bell's own badge, so one dot means one, and two means a pill with the number in it.
    void updateRemotePlug() {
        if (!m_connect) return;
        const relay::remotesettings::State state = shownRemoteState();
        m_connect->setBadge(state.online ? state.devices : 0);
        m_connect->setToolTip(state.on ? relay::remotesettings::statusLine(state)
                                       : QStringLiteral("Join a shared session"));
    }

    // One plug-menu row chosen. The ids are relay::remotesettings::plugMenu's.
    void runPlugItem(const QString &id) {
        if (id == QStringLiteral("remote.pair")) { pairPhone(); return; }
        if (id == QStringLiteral("remote.control")) {
            if (relay::RemoteShare::instance().alwaysOn()) disconnectRemoteDevices();
            else {
                relay::RemoteShare::instance().setAlwaysOn(true);
                syncAlwaysOnShares();
                refreshSettingsPanes();
                notice(relay::remotesettings::statusLine(shownRemoteState()), 6000);
            }
            return;
        }
        if (id == QStringLiteral("remote.join")) {
            joinSharedSession();
            hint(QStringLiteral("remote.join.button"),
                 QStringLiteral("Next time: type /join and the code in any prompt box"));
            return;
        }
        if (id == QStringLiteral("remote.openShared")) openSharedPaneDialog();
    }

    // "Pair a phone" (#FR1C): the plug menu, the palette (remote.pair) and Options › Remote all
    // land here. A phone paired against a desktop that publishes nothing shows an empty list and
    // no notification, so the switch goes on first — writing what the Options switch writes, and
    // sending the same `start` with `always` — and then the pairing window opens.
    void pairPhone() {
        relay::RemoteShare &share = relay::RemoteShare::instance();
        if (!share.alwaysOn()) {
            relay::remotesettings::turnOnForPairing();
            share.setAlwaysOn(true);          // brings the sidecar up and sends `start`
            syncAlwaysOnShares();
            refreshSettingsPanes();
        }
        // The pairing window belongs to a pane, because the same window also invites people to
        // one. The active leaf is the natural owner; a tool pane (Options, the Switchboard) is
        // not one, and "Pair a phone" from there is still meant to work.
        Pane *pane = dynamic_cast<Pane *>(m_activeLeaf.data());
        if (!pane) {
            const QList<Pane *> panes = allPanes();
            pane = panes.isEmpty() ? nullptr : panes.first();
        }
        if (!pane) {
            notice(QStringLiteral("Open a terminal pane first — pairing happens in a pane's "
                                  "sharing window."), 6000);
            return;
        }
        pane->toggleShare();
    }

    // "Disconnect all" (#PH0N, "forgot it was on"). The phones go and the service goes with them:
    // the sidecar has no "keep running but drop the devices" line today, so this turns the switch
    // off and says so, rather than pretending the two are different things.
    void disconnectRemoteDevices() {
        relay::RemoteShare::instance().setAlwaysOn(false);
        refreshSettingsPanes();
        notice(QStringLiteral("Remote control off — every phone is disconnected and nothing is "
                              "published. Options › Remote turns it back on."), 8000);
    }

    // Options › Remote (#PH0N): the rows are relay::remotesettings::section(), and what they write
    // goes through RemoteShare, which owns the sidecar and the panes.
    relay::SettingsSection remoteSection() {
        relay::remotesettings::SectionHooks hooks;
        relay::RemoteShare &share = relay::RemoteShare::instance();
        hooks.addresses = share.addresses();
        hooks.state = shownRemoteState();
        hooks.setAlwaysOn = [this](bool on) {
            relay::RemoteShare::instance().setAlwaysOn(on);
            // Queued: this runs from the switch's own signal, and the refresh below deletes it.
            QTimer::singleShot(0, this, [this] { syncAlwaysOnShares(); refreshSettingsPanes(); });
        };
        hooks.setAddress = [this](const QString &value) {
            relay::RemoteShare::instance().setRemoteAddress(value);
            QTimer::singleShot(0, this, [this] { refreshSettingsPanes(); });
        };
        // Pairing is the share dialog's, where the QR and the five digits to compare already live.
        // The same door as the plug menu's "Pair a phone…" (#FR1C): the switch first when it is
        // off, then the window with the code and the QR.
        hooks.pairPhone = [this] { pairPhone(); };
        return relay::remotesettings::section(hooks);
    }

    void updateTitles() {
        // Layout changes all end here (split, close, move, adopt), so this is where a pane that
        // reached or left a tab shared whole is noticed at once rather than on the next second.
        scheduleTabShareSync();
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            const QStringList titles = paneTitlesIn(page);
            m_tabs->setTabText(i, tabLabelText(page, titles));
            m_tabs->setTabToolTip(i, tabTooltipText(page, titles));
        }
        syncChrome();
        // Which tool panes this tab holds decides which title-bar buttons are lit, and this runs
        // after every open, close, split, move, restore, tab change and Settings mode swap.
        syncChromeButtons();
        QString where = m_activeLeaf ? leafCwd(m_activeLeaf) : QString();
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data())) where = tool->path();
        setWindowTitle(where.isEmpty() ? QStringLiteral("Relay") : QStringLiteral("Relay — ") + where);
        // Saved window layout: this runs after every split, close, tab change, directory change
        // and model change, so it is the one place the debounced save hangs off.
        m_manager->scheduleSave();
    }

    // /rename-tab, or a double click on the tab: an editor over the tab itself. An empty name puts
    // the tab back under its place title.
    void renameTab(const QString &text, bool edit, QWidget *page = nullptr) {
        if (!page) page = m_tabs->currentWidget();
        if (!page) return;
        const int index = m_tabs->indexOf(page);
        if (index < 0) return;
        if (!edit) {
            if (text.isEmpty()) m_tabNames.remove(page);
            else m_tabNames.insert(page, relay::titles::clean(text, relay::titles::kMaxUserTitle, 0));
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

    // Tab labels: names set by hand. The automatic label is a place (placeTabTitle), decided in
    // the GUI alone, so there is no per-tab judgement left to keep.
    QMap<QWidget *, QString> m_tabNames;
    // Which project each tab is attached to (#JN7X). Written only by attachTab()/detachTab() and
    // cleared by forgetTab(); a tab that is not in it is attached to nothing, which is the
    // ordinary state.
    QMap<QWidget *, QString> m_tabProject;
    QLineEdit *m_tabEdit = nullptr;
    QPointer<QWidget> m_tabEditPage;

    void cycleTab(int delta) {
        if (m_tabs->count() < 2) return;
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + delta + m_tabs->count()) % m_tabs->count());
    }

    // The width a Switchboard pane is owed when the layout is organized (#BXCN): its list/card
    // split needs relay::board::kCardSplitWidth, so "equalize" and an Execute/Verify dock beside
    // the board keep it at or above that and shrink the panes around it. 0 for every other pane.
    // A hand drag is still free to narrow the board: this is what the arithmetic asks for, not a
    // minimumSizeHint.
    int boardSplitFloor(QWidget *leaf) const {
        auto *tool = dynamic_cast<ToolPane *>(leaf);
        if (!tool || tool->kind() != ToolPane::Kind::Board || !tool->board()) return 0;
        // The threshold is the VIEW's width (BoardView::updateDetailLayout), and the pane's own
        // layout margins take their slice off the leaf before the view sees a pixel: the floor
        // owes the view kCardSplitWidth plus that slice, read off the layout so a chrome change
        // keeps the split above the floor instead of silently landing 2 px under it.
        int chrome = 2;
        if (const QLayout *layout = tool->layout())
            chrome = layout->contentsMargins().left() + layout->contentsMargins().right();
        return relay::board::kCardSplitWidth + chrome;
    }

    // Put `pane` beside `anchor` in the given orientation, reusing the anchor's splitter when
    // it already runs that way, otherwise wrapping the anchor in a new splitter. `anchorFloor`
    // (card #BXCN) is a width the anchor is owed after the dock — only Execute and Verify pass
    // one (the Switchboard's split); the newcomer's half then comes out of the panes beside the
    // anchor rather than out of the anchor itself.
    void insertBeside(QWidget *anchor, QWidget *pane, Qt::Orientation orientation, bool before,
                      int anchorFloor = 0) {
        QWidget *parent = anchor->parentWidget();
        if (auto *splitter = dynamic_cast<QSplitter *>(parent); splitter && splitter->orientation() == orientation) {
            const int index = splitter->indexOf(anchor);
            // Only the anchor's share is split between it and the newcomer; the panes beside them
            // keep the sizes the person gave them (owner, 2026-09-19). Read before the insert,
            // which is when the list still describes the splitter. An empty answer means there is
            // nothing worth keeping — a splitter not yet laid out — and equal shares are then as
            // good as any.
            const QList<int> kept = relay::panes::sizesAfterDock(splitter->sizes(), index, anchorFloor);
            splitter->insertWidget(before ? index : index + 1, pane);
            if (kept.size() == splitter->count()) {
                splitter->setSizes(kept);
            } else {
                QList<int> equal;
                for (int i = 0; i < splitter->count(); ++i) equal.append(1000);
                splitter->setSizes(equal);
            }
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
            QPointer<QWidget> anchorGuard(anchor), paneGuard(pane);
            QTimer::singleShot(0, wrapper, [guard, anchorGuard, paneGuard, before, anchorFloor] {
                if (!guard || !anchorGuard || !paneGuard) return;
                const bool horizontal = guard->orientation() == Qt::Horizontal;
                const int total = horizontal ? guard->width() : guard->height();
                // 50/50, unless the anchor is owed a floor (#BXCN): it keeps the lesser of the
                // floor and what leaves the newcomer its own minimum, and never less than half.
                int anchorShare = total / 2;
                if (anchorFloor > 0) {
                    const int newcomerMin = horizontal ? paneGuard->minimumSizeHint().width()
                                                       : paneGuard->minimumSizeHint().height();
                    anchorShare = std::max(total / 2, std::min(anchorFloor, std::max(0, total - newcomerMin)));
                }
                anchorShare = qBound(0, anchorShare, std::max(0, total));
                if (before) guard->setSizes({total - anchorShare, anchorShare});
                else guard->setSizes({anchorShare, total - anchorShare});
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
        // Only one of the two windows may be open: this one owns the toast below, and the chord's
        // cancel must never delete a label this one is still showing (#Q7Y9).
        endBeneathDock();
        // Re-installing moves this filter to the front of the application's list, so the arrow is
        // seen here before the new pane's prompt box can treat it as cursor movement.
        qApp->installEventFilter(this);
        m_placementClock.start();
        m_placement.arm(0);
        m_placementPane = pane;
        m_placementAnchor = anchor;
        showPaneToast(pane, QStringLiteral("← ↑ ↓ to place"));
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

    // The placement window's one transient label (#78BN), owned by it alone: endPlacement()
    // deletes it. The chord (#Q7Y9) teaches itself through the hint registry instead, so nothing
    // else can delete this one while the arrows are still live.
    void showPaneToast(QWidget *pane, const QString &text) {
        delete m_placementHint.data();
        m_placementHint = new QLabel(text, this);
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

    // ----- "move left/right, then ↓ docks it beneath that neighbor" (#Q7Y9) ----------------------
    //
    // The twin of the placement window above: there the second key is a bare arrow, here it is
    // the Move-down action, so the chord follows whatever keys move panes for this user — by
    // default Ctrl+Alt+Left then Ctrl+Alt+Down docks the pane beneath the neighbor on its left,
    // and Ctrl+Alt+Right then Ctrl+Alt+Down beneath the one on its right. After two seconds it
    // is a plain move again; any other action, key or click closes the window without consuming
    // anything, so nothing typed is ever swallowed.
    void armBeneathDock(QWidget *pane, QWidget *anchor) {
        // The two windows are exclusive, and the placement one owns the transient label.
        endPlacement();
        // Re-installing the filter moves it to the front of the application's list again.
        qApp->installEventFilter(this);
        m_beneathClock.start();
        m_beneath.arm(0);
        m_beneathPane = pane;
        m_beneathAnchor = anchor;
        // The teaching line goes through the hint registry like every other shortcut hint
        // (WARP.md, "Shortcut hints"), under its own id: it stops after the registry's limit and
        // the "Shortcut hints" setting turns it off. The window is armed either way, so a user
        // who has learned the chord keeps it without being told about it every single move.
        // It names the two seconds rather than saying "now", because a hint queues behind any
        // toast already up and may reach the screen after this window has closed.
        const QString down = Keymap::instance().shortcutText(QStringLiteral("pane.moveDown"));
        if (!down.isEmpty())
            hint(QStringLiteral("pane.dockBeneath.chord"),
                 QStringLiteral("%1 within two seconds docks it beneath").arg(down));
        m_beneathTimer.start(int(relay::panes::PlacementWindow::kTimeoutMs) + 20);
    }

    void endBeneathDock() {
        m_beneathTimer.stop();
        m_beneath.cancel();
        m_beneathPane = nullptr;
        m_beneathAnchor = nullptr;
    }

    // The second half of the chord: dock the pane beneath the neighbor it moved toward. The same
    // takeLeaf + insertBeside pair every other keyboard move uses, so the shell, the agent and
    // the scrollback all travel with the pane.
    bool dockBeneathNeighbor() {
        if (!m_beneath.armed(m_beneathClock.elapsed())) { endBeneathDock(); return false; }
        QPointer<QWidget> pane(m_beneathPane), anchor(m_beneathAnchor);
        endBeneathDock();
        QWidget *page = pane ? pageOf(pane) : nullptr;
        // Everything is checked BEFORE the pane is detached: takeLeaf leaves it parentless, so a
        // refusal after it would drop the pane out of the window and the caller would then run a
        // plain Move-down on a widget that is in no layout at all. The page must also be the tab
        // on screen — a tab change between the two keys makes this an ordinary Move-down again.
        if (!pane || !anchor || !page || page != pageOf(anchor) || page != m_tabs->currentWidget()) return false;
        if (!takeLeaf(pane)) return false;
        // Past here the pane is detached, so it is always put back somewhere and the answer is
        // always yes. The anchor is in the same page and cannot be the tab's last leaf, so
        // takeLeaf cannot have taken it away; if it ever does, any remaining leaf is a home, and
        // a tab of its own is the last resort.
        QWidget *home = anchor.data();
        if (!home) {
            const auto leaves = leavesIn(m_tabs->currentWidget());
            home = leaves.isEmpty() ? nullptr : leaves.first();
        }
        if (home) insertBeside(home, pane, Qt::Vertical, false);   // beneath the anchor, not above it
        else adoptLeafAsTab(pane);
        if (pane) { setActiveLeaf(pane); focusLeaf(pane); }
        updateTitles();
        return true;
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

    // ----- window header: Relay icon; bell, tool panes, plug and the window buttons ----------------
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
        // The one chrome button something outside this row has to name. Every other one is a
        // `windowChromeButton` and is reached by its neighbours; the bell is what a GUI drive
        // has to *click* to read the "Agent changed … · Undo" notice, and an icon is the one
        // thing OCR cannot find (RELAY_QA_RECTS, startQaRects).
        m_bell->setObjectName(QStringLiteral("windowBellButton"));
        connect(m_bell, &QToolButton::clicked, this, [this] { toggleNotifications(); });
        rightRow->addWidget(m_bell);
        // The row's three hairlines: after the bell, and either side of the plug below, so the
        // groups read as bell ‖ tool panes ending in the gear ‖ plug ‖ window buttons
        // (owner, 2026-09-20).
        rightRow->addSpacing(4);
        rightRow->addWidget(new ChromeSeparator);
        rightRow->addSpacing(4);
        // The tool panes, one button each, ending in the gear (owner, 2026-09-18). The table is
        // relay::panestatus::toolButtons(), so a button's glyph, its light, its tooltip and what its
        // second click closes all come from the one pane type it owns. Each wears its pane's own
        // header band while that pane is open, and closes it when clicked again.
        for (const relay::panestatus::ToolButton &spec : relay::panestatus::toolButtons()) {
            auto *button = new ChromeButton(relay::panestatus::typeStyle(spec.paneType, relay::panestatus::ColourMode::ByType,
                                                                        relay::chrome::tokens()).glyph);
            button->setPaneType(spec.paneType);
            connect(button, &QToolButton::clicked, this, [this, spec] { runToolButton(spec); });
            rightRow->addWidget(button);
            m_toolButtons.insert(spec.paneType, button);
        }
        // The tool buttons end in the gear; the plug sits right of it, between hairlines.
        rightRow->addSpacing(4);
        rightRow->addWidget(new ChromeSeparator);
        rightRow->addSpacing(4);
        // The plug: into somebody else's session. Joining with a code is the common case, so it
        // is the first entry; your own desktop's panes are the second (owner, 2026-09-18).
        m_connect = new ChromeButton(ChromeButton::Glyph::Connect);
        m_connect->setToolTip(QStringLiteral("Join a shared session"));
        connect(m_connect, &QToolButton::clicked, this, [this] {
            QMenu menu(this);
            // The rows and their order are relay::remotesettings::plugMenu (#FR1C), so a test
            // reads them without a window: the status line the plug has carried since #PH0N,
            // "Pair a phone…" first, and the switch itself under it. Turning the switch off here
            // is what "Disconnect all" was — the phones go and the service goes with them — under
            // the name of the thing it actually moves.
            for (const relay::remotesettings::PlugItem &item :
                 relay::remotesettings::plugMenu(shownRemoteState())) {
                using Item = relay::remotesettings::PlugItem;
                if (item.kind == Item::Separator) { menu.addSeparator(); continue; }
                QAction *action = menu.addAction(item.label);
                if (item.kind == Item::Status) { action->setEnabled(false); continue; }
                if (item.kind == Item::Toggle) {
                    action->setCheckable(true);
                    action->setChecked(item.checked);
                }
                const QString id = item.id;
                connect(action, &QAction::triggered, this, [this, id] { runPlugItem(id); });
            }
            menu.exec(m_connect->mapToGlobal(QPoint(0, m_connect->height())));
        });
        rightRow->addWidget(m_connect);
        updateRemotePlug();
        if (!m_nativeFrame) {
            // The third hairline vanishes with the window buttons it parts the plug from.
            rightRow->addSpacing(4);
            rightRow->addWidget(new ChromeSeparator);
            rightRow->addSpacing(4);
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

        connect(&relay::NotificationCenter::instance(), &relay::NotificationCenter::changed, this, [this] {
            updateBell();
            // A fresh post redefines "most recent", so the next jump starts at the newest — unless
            // a walk is running already (the reset timer is active), which stays stable so
            // markSeen's own changed() cannot fold the walk back onto itself (#NQP9).
            if (!m_notificationJumpReset.isActive()) m_notificationJumpIndex = 0;
        });
        m_notificationJumpReset.setSingleShot(true);
        m_notificationJumpReset.setInterval(4000);
        connect(&m_notificationJumpReset, &QTimer::timeout, this, [this] { m_notificationJumpIndex = 0; });
        updateBell();
        updateChromeState();
        syncChromeButtons();
    }

    // ----- the title-bar tool buttons: lit while their pane is open (owner, 2026-09-18) ----------
    // "the sessions / actions / switchboard / options buttons at the top right should be
    // highlighted when they are open (using the header colors). click again to close those panes."
    //
    // **Open means: the tab in front of this window holds a pane of that type.** Every one of these
    // openers works in the current tab and nowhere else (openSettingsPane, openSessionsFor,
    // toggleBoardPane), so the light and the click can never disagree about what they are talking
    // about; another tab, and another window, light their own buttons from their own panes.
    //
    // **Which pane, when a tab somehow holds two of a type** — a restored Switchboard dropped
    // beside one that was already there: the focused one if that is of the type, otherwise the
    // first in the tab's pane order, which is the pane the opener itself would have reused.
    // Actions and Options are separate panes since 2026-09-18, each with its own `paneType`, so
    // both buttons are lit while both are open and each click closes only its own.
    static QString paneTypeOf(QWidget *leaf) {
        if (!leaf) return {};
        const QString set = leaf->property("paneType").toString();
        if (!set.isEmpty()) return set;
        // A pane that has not been polished yet has not been given its default type.
        auto *tool = dynamic_cast<ToolPane *>(leaf);
        return tool ? tool->defaultPaneType() : QString();
    }

    ToolPane *openToolPane(const QString &paneType) const {
        QWidget *page = m_tabs->currentWidget();
        if (!page || paneType.isEmpty()) return nullptr;
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data());
            tool && pageOf(tool) == page && paneTypeOf(tool) == paneType) return tool;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && paneTypeOf(tool) == paneType) return tool;
        return nullptr;
    }

    // A click: the first opens the pane, the second closes it. The hint that teaches the key only
    // goes with the click that opened it, because that is the click the key would have replaced.
    void runToolButton(const relay::panestatus::ToolButton &spec) {
        if (ToolPane *open = openToolPane(spec.paneType)) { closeToolPane(open); return; }
        runAction(spec.action);
        hint(QStringLiteral("chrome.") + spec.action,
             relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(spec.action), spec.what));
    }

    // Closing goes through the pane's own close path, so whatever that path does still happens: the
    // Settings pane puts the focus back where it came from, the session manager hands it to the
    // pane it was bound to, and anything else takes the route the pane's × takes. A toggle must
    // never close the window, so the last pane of the last tab gets a terminal beside it first —
    // which is what Esc in the Settings pane has always done.
    void closeToolPane(ToolPane *tool) {
        if (!tool) return;
        if (tool->settings()) { closeSettingsPane(tool); return; }
        QWidget *page = pageOf(tool);
        if (page && leavesIn(page).size() <= 1 && m_tabs->count() <= 1) {
            try { insertBeside(tool, createPane(paneNode(m_manager->workspace())), Qt::Horizontal, true); }
            catch (const std::exception &error) { notice(QString::fromUtf8(error.what())); }
        }
        if (tool->kind() == ToolPane::Kind::Sessions) { closeSessionsPane(tool, m_active); return; }
        if (dynamic_cast<relay::projects::ProjectPicker *>(tool->hosted())) { closeProjectPicker(tool, m_active); return; }
        setActiveLeaf(tool);
        runAction(QStringLiteral("pane.close"));
    }

    // The lights and the tooltips, from whatever this window's current tab holds right now. Called
    // from updateTitles(), which every open, close, split, move, tab change, restore and mode swap
    // already ends in, and from the keymap listener when the keys change under the tooltips.
    void syncChromeButtons() {
        for (const relay::panestatus::ToolButton &spec : relay::panestatus::toolButtons()) {
            ChromeButton *button = m_toolButtons.value(spec.paneType);
            if (!button) continue;
            const bool open = openToolPane(spec.paneType) != nullptr;
            button->setOpen(open);
            const QString label = open ? spec.openLabel : spec.label;
            const QString keys = Keymap::instance().shortcutText(spec.action);
            button->setToolTip(keys.isEmpty() ? label : QStringLiteral("%1  (%2)").arg(label, keys));
            button->update();   // the tint follows the theme and "appearance/pane_colours"
        }
    }

    // The buttons wear the pane bands' colours, so "appearance/pane_colours" and a theme change
    // have to reach them in every window, exactly as PaneChrome::refreshAll() reaches the panes.
    static void refreshChromeButtonsEverywhere() {
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *w = dynamic_cast<RelayWindow *>(top)) w->syncChromeButtons();
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

    // ----- RELAY_QA_RECTS: where the named widgets are, for a driver that must click one -------
    //
    // A GUI drive reads the screen with OCR, which finds words and cannot find an icon: the bell
    // that opens the notification list, the ⧉ beside a card id, a switch. The integration drive
    // of card #AGNT had to leave "the notification offers Undo, and Undo puts it back" unverified
    // for exactly that — it could see the notice existed and could not aim at it.
    //
    // So, **only when `RELAY_QA_RECTS` names a file**, the window writes every named widget's
    // screen rectangle there as JSON, debounced, whenever the layout settles. Unset — which is
    // every run that is not a drive — this costs one `qEnvironmentVariableIsSet` at startup and
    // nothing else: no timer is created and no event filter looks at anything.
    //
    // It is deliberately names-and-rectangles and nothing else: a driver that could ask the app
    // what it *thinks* is on screen would stop reading what is actually drawn, which is the whole
    // value of driving it live.
    void startQaRects() {
        const QByteArray path = qgetenv("RELAY_QA_RECTS");
        if (path.isEmpty()) return;
        m_qaRects = new QTimer(this);
        m_qaRects->setSingleShot(true);
        m_qaRects->setInterval(400);
        connect(m_qaRects, &QTimer::timeout, this, [this, path] {
            QJsonObject out;
            for (QWidget *widget : findChildren<QWidget *>()) {
                if (widget->objectName().isEmpty() || !widget->isVisible()) continue;
                const QPoint at = widget->mapToGlobal(QPoint(0, 0));
                // Object names are not unique — every prompt box is a `composerEditor` — so the
                // second and later ones are numbered in the order they are found. A driver that
                // wants "the one in the pane I am looking at" reads the rectangles; a driver that
                // wants "how many of these are on screen" counts the keys, which is how "a
                // console draws no pane header" is checked without reading a pixel.
                QString key = widget->objectName();
                for (int n = 2; out.contains(key); ++n)
                    key = widget->objectName() + QLatin1Char('#') + QString::number(n);
                out.insert(key,
                           QJsonObject{{QStringLiteral("x"), at.x()}, {QStringLiteral("y"), at.y()},
                                       {QStringLiteral("w"), widget->width()},
                                       {QStringLiteral("h"), widget->height()},
                                       {QStringLiteral("text"), widget->property("text").toString()}});
            }
            QSaveFile file(QString::fromLocal8Bit(path));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
            file.write(QJsonDocument(out).toJson(QJsonDocument::Compact));
            file.commit();
        });
        qApp->installEventFilter(new QaRectsWatch(m_qaRects));
        m_qaRects->start();
    }
    // Anything that can move a widget restarts the debounce; the write itself is one pass over
    // the window's children and happens 400 ms after the last of them.
    class QaRectsWatch final : public QObject {
      public:
        explicit QaRectsWatch(QTimer *timer) : QObject(timer), m_timer(timer) {}
        bool eventFilter(QObject *, QEvent *event) override {
            switch (event->type()) {
            case QEvent::Show: case QEvent::Hide: case QEvent::Move: case QEvent::Resize:
            case QEvent::LayoutRequest: case QEvent::Polish:
                if (m_timer) m_timer->start();
                break;
            default: break;
            }
            return false;
        }
      private:
        QPointer<QTimer> m_timer;
    };
    QTimer *m_qaRects = nullptr;

    void toggleNotifications() {
        if (!m_notifications) {
            m_notifications = new NotificationsPopup(this);
            m_notifications->onOpenSource = [this](const QString &source) {
                openNotificationSource(source);
                // The row is the mouse path; the key is the faster one, and the standing hint
                // rule says the slow path teaches it (#NQP9).
                hint(QStringLiteral("notifications.jump.mouse"),
                     relay::ShortcutHints::nextTime(
                         Keymap::instance().shortcutText(QStringLiteral("notifications.jump"))));
            };
            // "Agent changed X: before → after · Undo" (#FEJQ, §30.6). The window owns the change
            // log, so the way back works with no agent in the loop; a change another window made
            // is not this window's to revert, and undoFromNotification() says so by doing nothing.
            m_notifications->onAction = [this](const QString &, const QString &actionId) {
                if (appCommands().undoFromNotification(actionId)) return;
                // "Working on ctest:panelayout · Open thread" (#AQ6X phase 3, decision 9): the
                // agent thread Relay started on a failing check nobody was on.
                const auto thread = relay::board::signalThreadOfAction(actionId);
                if (!thread.first.isEmpty()) {
                    openSignalThread(thread.first, thread.second);
                    return;
                }
                notice(QStringLiteral("That change is no longer one this window can undo."), 6000);
            };
        }
        if (m_notifications->isVisible()) { m_notifications->hide(); return; }
        m_notifications->popUpUnder(m_bell);
        updateBell();
    }

    // notifications.jump (#NQP9): go to the newest notification's pane — a Switchboard card for a
    // `board:` source. Pressing it again within the window walks to the next older entry; a fresh
    // post, or a pause longer than the reset timer, starts over at the newest. Entries without a
    // source cannot be gone to and are stepped over.
    void jumpToNotification() {
        QList<relay::Notification> jumpable;
        for (const relay::Notification &note : relay::NotificationCenter::instance().entries())
            if (!note.source.isEmpty()) jumpable.append(note);
        if (jumpable.isEmpty()) { notice(QStringLiteral("No notifications.")); return; }
        if (m_notificationJumpIndex < 0 || m_notificationJumpIndex >= jumpable.size())
            m_notificationJumpIndex = 0;   // walked past the oldest: wrap to the newest
        const relay::Notification note = jumpable.at(m_notificationJumpIndex);
        openNotificationSource(note.source);
        // markSeen() before the increment: its changed() resets the index first (unless a walk is
        // running, see the connection in buildWindowChrome), and then ++ picks the next one down.
        relay::NotificationCenter::instance().markSeen(note.id);
        ++m_notificationJumpIndex;
        m_notificationJumpReset.start();
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
        for (int i = 0; i < bar->count(); ++i) {
            const bool hovered = bar->tabAt(bar->mapFromGlobal(QCursor::pos())) == i && bar->underMouse();

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
                    // The conversation info button (ⓘ, card #Y63Z), first in an agent pane's row.
                    if (dynamic_cast<Pane *>(leaf)) {
                        // The button row is the chrome's own layout while it is one row, and the
                        // first row inside the column once the share button hangs below it
                        // (2026-09-20). Casting only chrome->layout() found nothing after that move,
                        // and the i-button silently went. Take either shape so the order the two
                        // land in does not matter.
                        QHBoxLayout *row = qobject_cast<QHBoxLayout *>(chrome->layout());
                        if (!row)
                            if (auto *column = qobject_cast<QVBoxLayout *>(chrome->layout()))
                                if (column->count() > 0)
                                    row = qobject_cast<QHBoxLayout *>(column->itemAt(0)->layout());
                        if (row) {
                            auto *info = new relay::sessioninfo::InfoButton;
                            info->setObjectName(QStringLiteral("paneChromeButton"));
                            info->setProperty("action", QStringLiteral("agent.info"));
                            // PaneChrome::refreshTooltips appends the live key, so the tooltip
                            // reads "Conversation info  (Alt+I)" and follows a rebinding.
                            info->setProperty("label", QStringLiteral("Conversation info"));
                            QObject::connect(info, &QToolButton::clicked, chrome, [guard] {
                                auto *w = windowOf(guard);
                                if (!w) return;
                                w->setActiveLeaf(guard);
                                // The button is the slow path: it teaches the key it is bound to,
                                // and falls back to /status only while nothing is bound.
                                const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.info"));
                                w->runAction(QStringLiteral("agent.info"));
                                w->hint(QStringLiteral("info.click"),
                                        keys.isEmpty() ? QStringLiteral("Next time: /status in the prompt box")
                                                       : relay::ShortcutHints::nextTime(keys, QStringLiteral("conversation info")));
                            });
                            row->insertWidget(0, info);
                        }
                    }
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
        // One read of `appearance/pane_usage` for the whole poll: the chip, the tab label, the
        // tab tooltip and the Sessions row all answer to it (issue #D03W).
        const bool meters = relay::usage::metersEnabled();
        QHash<QString, QString> liveUsage;   // session id → tag, for the Sessions pane (#D03W)
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            pages.insert(page);
            QList<ps::State> states;
            QList<relay::usage::Sample> usage;
            bool remote = false, terminal = false;
            ps::TypeStyle firstType;
            // Walking /proc for every pane in every tab at 2.5 Hz was 3.7 % of the idle profile
            // (card #057J). Only the tab in front has chips on screen and only a window that is
            // not minimised shows them; everything else that reads these numbers — the tab's own
            // label and the Sessions pane's tag — prints the 5 s average and takes it once every
            // kTabUpdateMs. So a tab nobody is looking at is measured on that clock instead, which
            // gives the label exactly the figure it would have had. The front tab is unchanged.
            const bool front = i == m_tabs->currentIndex() && !isMinimized();
            const qint64 sampledAt = m_tabUsage.value(page).sampledAtMs;
            const bool sample = front || sampledAt == 0 || now - sampledAt >= relay::usage::kTabUpdateMs;
            for (QWidget *leaf : leavesIn(page)) {
                auto *pane = dynamic_cast<Pane *>(leaf);
                if (!pane) {
                    if (!firstType.band) firstType = relay::chrome::styleOf(leaf);
                    continue;
                }
                terminal = true;
                PaneChrome *chrome = chromeOf(pane);
                if (!chrome) continue;
                // The pane's resource meter (issue #D03W) is sampled here so every pane is
                // measured over the same interval this poll keeps — one interval for the tab,
                // whichever clock the tab is on (see `sample` above).
                if (sample) pane->refreshUsage();
                chrome->setUsage(pane->usageSample());
                const ps::Facts facts = pane->statusFacts();
                const bool watched = focused && i == m_tabs->currentIndex() && leaf == m_activeLeaf;
                if (!watched) chrome->watchedSince = 0;
                else if (!chrome->watchedSince) chrome->watchedSince = now;
                if (watched && now - chrome->watchedSince >= kSeenAfterMs) chrome->seenSerial = facts.finishSerial;
                const ps::State state = ps::resolve(facts, chrome->seenSerial);
                const QString remoteLine = pane->remoteCommandLine();
                const bool shared = pane->sharedWithPhone();
                chrome->setStatus(state, remoteLine, shared);
                // How many subagents this pane's agent has running (card #YMSR): the same count
                // the state above was resolved from, so the badge and the glyph cannot disagree.
                chrome->setSubagents(facts.liveSubagents);
                // Shared with how many people, and who is driving when it is not the owner.
                const relay::sharing::ChipState chip =
                    relay::RemoteShare::instance().sharingModel().chip(pane->sessionToken(), shared);
                if (shared) chrome->setSharing(chip.text, chip.tooltip, chip.guestDriving);
                if (!remoteLine.isEmpty()) sshSessionSeen(pane, remoteLine);
                states << state;
                usage << pane->usageSample();
                const QString sessionId = pane->sessionId();
                if (meters && !sessionId.isEmpty()) {
                    const QString tag = relay::usage::liveTag(pane->usageSample());
                    if (!tag.isEmpty()) liveUsage.insert(sessionId, tag);
                }
                remote = remote || !remoteLine.isEmpty();
            }
            // The tab label carries the tab's combined usage (issue #D03W) on a clock of its
            // own (card #MERX, owner 2026-09-19: "update only once every ~5 secs or so, using
            // the 5 sec average"): every poll pours the summed sample into a 5 s window, and
            // once every relay::usage::kTabUpdateMs the label takes the window's mean, printed
            // with both halves always and two digits each — so the label's width never moves
            // and the tab bar's layout never shuffles, and a number that ticks once every 5 s
            // is not a flicker. Between takes the text stands still. Only the tab whose text
            // moved is relabelled. It used to call updateTitles(), which re-elides every tab,
            // rewrites every tooltip and runs syncChrome and the share sync.
            const relay::usage::Sample summed = relay::usage::combined(usage);
            const QString previousText = m_tabUsage.value(page).text;
            QString usageText = previousText;
            if (meters && terminal) {
                TabUsageState &state = m_tabUsage[page];
                // Only a fresh reading goes into the window: a hidden tab's sample is already the
                // mean over the 5 s it covers, and pouring the same one in twelve times over would
                // make the label an average of a repeat rather than of an interval.
                if (sample) { state.window.add(summed, now); state.sampledAtMs = now; }
                if (state.labelledAtMs == 0
                    || now - state.labelledAtMs >= relay::usage::kTabUpdateMs)
                    usageText = relay::usage::tabSuffix(state.window.average());
            } else if (!previousText.isEmpty()) {
                usageText = QString();   // no terminal pane to measure, or the meters are off
            }
            if (usageText != previousText) {
                TabUsageState &state = m_tabUsage[page];
                if (usageText.isEmpty())
                    state = TabUsageState{};   // fresh window and fresh clock for panes to come
                else {
                    state.text = usageText;
                    state.labelledAtMs = now;
                }
                const QStringList titles = paneTitlesIn(page);
                m_tabs->setTabText(i, tabLabelText(page, titles));
                m_tabs->setTabToolTip(i, tabTooltipText(page, titles));
            }
            const ps::State top = ps::mostUrgent(states);
            // What is live in the tab whatever its icon is showing: a news icon (done, needs you)
            // must not hide that work is happening in a sibling pane (card #V8KT).
            const ps::State live = ps::liveMarker(states);
            m_tabMark.insert(page, TabMark{terminal, remote, top, live, firstType});
        }
        for (auto it = m_tabIconKey.begin(); it != m_tabIconKey.end();)
            it = pages.contains(it.key()) ? std::next(it) : m_tabIconKey.erase(it);
        for (auto it = m_tabMark.begin(); it != m_tabMark.end();)
            it = pages.contains(it.key()) ? std::next(it) : m_tabMark.erase(it);
        applyTabIcons();   // after the pruning, so a closed tab cannot keep the blink armed
        for (auto it = m_tabUsage.begin(); it != m_tabUsage.end();)
            it = pages.contains(it.key()) ? std::next(it) : m_tabUsage.erase(it);
        // The session manager shows the same reading as a tag on each open conversation's row
        // (issue #D03W). It never looks at a window itself; like setOpenSessions, this feeds it.
        if (!liveUsage.isEmpty() || m_fedLiveUsage)
            for (int i = 0; i < m_tabs->count(); ++i)
                for (QWidget *leaf : leavesIn(m_tabs->widget(i)))
                    if (auto *tool = dynamic_cast<ToolPane *>(leaf);
                        tool && tool->kind() == ToolPane::Kind::Sessions)
                        if (auto *manager = dynamic_cast<relay::conversations::SessionManager *>(tool->hosted()))
                            manager->setLiveUsage(liveUsage);
        m_fedLiveUsage = !liveUsage.isEmpty();
    }

    // What a tab's icon is drawn from, as the last poll read it. Kept per page so the blink can be
    // moved on without walking every pane again — and so the icon the pulse redraws says exactly
    // what the poll last decided it says.
    struct TabMark {
        bool terminal = false;
        bool remote = false;
        relay::panestatus::State top = relay::panestatus::State::Idle;
        relay::panestatus::State live = relay::panestatus::State::Idle;
        relay::panestatus::TypeStyle type;
    };

    // The tab label's usage state (issue #D03W, card #MERX): the 5 s window its number is the
    // mean of, the text that mean is currently spelled as, and when the label last took from
    // the window (0: never, so the first reading shows at once). The window fills on every
    // poll; the label only moves once every relay::usage::kTabUpdateMs.
    struct TabUsageState {
        relay::usage::RollingMean window;
        QString text;          // the suffix the tab is labelled with ("" when there is none)
        qint64 labelledAtMs = 0;
        // When this tab's panes were last measured (card #057J). The tab in front is measured on
        // every poll, because its chips are on screen; a tab behind it is measured on the label's
        // own clock, which is the only thing that reads it.
        qint64 sampledAtMs = 0;
    };

    // Draw every tab's icon for the step the wall clock is in now (owner, 2026-09-19: one cadence).
    // The phase is relay::panestatus', the same one PaneStateGlyph paints with, so the dot on the
    // tab and the glyph in the pane below it are lit and dark together — in this window and in
    // every other one. A still desktop (a cursor flash time of 0) draws the mark at rest.
    void applyTabIcons() {
        namespace ps = relay::panestatus;
        const bool animate = QApplication::cursorFlashTime() > 0;
        const int phase = animate ? ps::pulsePhaseNow() : -1;
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            const auto mark = m_tabMark.constFind(page);
            if (mark == m_tabMark.constEnd()) continue;
            const int marked = mark->live != ps::State::Idle ? phase : -1;
            const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8").arg(mark->terminal).arg(int(mark->top)).arg(mark->remote)
                                    .arg(int(mark->live)).arg(marked)
                                    .arg(int(mark->type.glyph)).arg(mark->type.ink.name(), relay::theme::activeThemeId());
            if (m_tabIconKey.value(page) == key) continue;
            m_tabIconKey.insert(page, key);
            m_tabs->setTabIcon(i, relay::chrome::tabIcon(mark->terminal, mark->top, mark->remote, mark->type.glyph,
                                                         mark->type.ink, devicePixelRatioF(), mark->live, marked));
        }
        armTabPulse();
    }

    // The next boundary of the blink grid, while anything in this window is live. Waiting for the
    // 400 ms poll instead would show each step up to a poll late — the drift the one cadence is
    // for — and the poll's interval is the meters' sampling interval, which is not ours to move.
    void armTabPulse() {
        if (m_pulseTimer.isActive()) return;   // already due on a boundary; two timers would not agree
        if (QApplication::cursorFlashTime() <= 0) return;
        for (auto it = m_tabMark.constBegin(); it != m_tabMark.constEnd(); ++it)
            if (it->live != relay::panestatus::State::Idle) {
                m_pulseTimer.start(relay::panestatus::msToNextPulseStep());
                return;
            }
    }

    // The tab's own share of the machine: every terminal pane in it summed (issue #D03W).
    relay::usage::Sample tabUsageSample(QWidget *page) const {
        QList<relay::usage::Sample> samples;
        for (QWidget *leaf : leavesIn(page))
            if (auto *pane = dynamic_cast<const Pane *>(leaf)) samples << pane->usageSample();
        return relay::usage::combined(samples);
    }

    // The suffix the tab label carries, or nothing while the meters are switched off — the same
    // setting the chip obeys, read in the one place the poll reads it.
    //
    // It is the reading the tab is *already* labelled with, not a fresh one. The poll decides
    // when the number may move (once every relay::usage::kTabUpdateMs, card #MERX), and
    // updateTitles() runs for all sorts of other reasons — a rename, a pane opening or closing,
    // a theme change — so measuring again here put a new percent on the tab in between and undid
    // that clock. Before the first poll there is nothing displayed yet, so the live sample seeds
    // the label; a tab with no terminal pane has nothing to measure and takes no suffix at all.
    QString tabUsageSuffix(QWidget *page) const {
        if (!relay::usage::metersEnabled()) return {};
        const auto shown = m_tabUsage.constFind(page);
        if (shown != m_tabUsage.constEnd() && !shown->text.isEmpty()) return shown->text;
        for (QWidget *leaf : leavesIn(page))
            if (dynamic_cast<const Pane *>(leaf))
                return relay::usage::tabSuffix(tabUsageSample(page));
        return {};
    }

    // Reconstruct the natural width of every label with its meter, even while the meter is not
    // painted. Reading the currently shown labels would make the decision oscillate: hiding the
    // suffix frees room, then immediately makes it eligible to return.
    QList<int> tabsFullLabelWidths() const {
        QTabBar *bar = m_tabs->tabBar();
        const QFontMetrics metrics(bar->font());
        QList<int> widths;
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            const QString suffix = tabUsageSuffix(page);
            // tabRect() carries QTabBar's style padding and close-button reservation; with this
            // non-expanding bar it is the label's natural width. tabSizeHint() is protected.
            int width = bar->tabRect(i).width();
            if (!suffix.isEmpty() && !bar->tabText(i).contains(suffix))
                width += metrics.horizontalAdvance(suffix);
            widths << width;
        }
        return widths;
    }

    bool tabMetersHaveRoom() const {
        QTabBar *bar = m_tabs->tabBar();
        const QWidget *left = m_tabs->cornerWidget(Qt::TopLeftCorner);
        const QWidget *right = m_tabs->cornerWidget(Qt::TopRightCorner);
        const int usable = bar->width() - (left ? left->width() : 0) - (right ? right->width() : 0);
        return relay::usage::tabMetersFit(usable, tabsFullLabelWidths());
    }

    void relabelTabsForWidth() {
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            const QString text = tabLabelText(page, paneTitlesIn(page));
            if (text != m_tabs->tabText(i)) m_tabs->setTabText(i, text);
        }
    }

    // A tab's tooltip: the pane titles, where the tab's last active pane is, and the usage line,
    // which says what the memory figure is a sum of and names the processes behind the number.
    // It no longer has to say which of the label's numbers is which: since #6BGA the label spells
    // that out itself, in the same words. Off with the same setting as the suffix.
    QString tabTooltipText(QWidget *page, const QStringList &titles) const {
        const auto leaves = leavesIn(page);
        QWidget *leaf = m_lastActive.value(page);
        if (!leaf && !leaves.isEmpty()) leaf = leaves.first();
        QString usageLine;
        if (relay::usage::metersEnabled()) {
            const relay::usage::Sample summed = tabUsageSample(page);
            if (summed.valid) {
                // Which processes the tab's number is made of, the busiest first — the panes'
                // breakdowns merged, so this names the tab's busiest processes rather than each
                // pane's. The label itself stays the sum alone.
                const QString breakdown = relay::usage::processBreakdown(summed);
                usageLine = QStringLiteral("This tab's panes: ")
                            + relay::usage::describe(summed) + QStringLiteral("\n")
                            + (breakdown.isEmpty() ? QString() : breakdown + QStringLiteral("\n"))
                            + relay::usage::memoryNote();
            }
        }
        return (titles.isEmpty() ? QString() : titles.join(QStringLiteral("\n")) + QStringLiteral("\n\n"))
               + (leaf ? leafCwd(leaf) : QString())
               + (usageLine.isEmpty() ? QString() : QStringLiteral("\n\n") + usageLine)
               + QStringLiteral("\n\nDouble click the tab to rename · /rename-tab");
    }

    // A tab's label, elided as the bar shows it: the pane titles, how many panes, and the usage
    // suffix. updateTitles() builds every tab's from here, and the status poll rebuilds the one
    // tab whose usage moved without touching the others.
    QString tabLabelText(QWidget *page, const QStringList &titles) const {
        QString title = tabLabelFor(page, titles);
        if (const int panes = int(leavesIn(page).size()); panes > 1)
            title += QStringLiteral(" (%1)").arg(panes);   // "(pane count)", owner 2026-09-20
        if (tabMetersHaveRoom()) title += tabUsageSuffix(page);
        const QFontMetrics metrics(m_tabs->tabBar()->font());
        return metrics.elidedText(title, Qt::ElideRight, 260);
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

    // Which tab of a tab-bar drop target the cursor is on: >= 0 is that tab's label — the pane
    // joins that tab — and -1 is the bar's empty space or the new-tab button, which still give it
    // a tab of its own (#A0SF).
    static int tabBarIndexAt(const QPair<QWidget *, Edge> &target, const QPoint &global) {
        if (target.second != Edge::TabBar) return -1;
        auto *bar = static_cast<QTabBar *>(target.first);
        return bar->tabAt(bar->mapFromGlobal(global));
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
        // Only the hovered label, so "into this tab" and "a tab of its own" read as two
        // different targets before the release (#A0SF).
        case Edge::TabBar:
            if (const int tab = tabBarIndexAt(target, global); tab >= 0) {
                const QRect label = static_cast<QTabBar *>(target.first)->tabRect(tab);
                rect = QRect(target.first->mapTo(w->centralWidget(), label.topLeft()), label.size());
            }
            break;
        default: break;
        }
        m_dropZone->setGeometry(rect);
        m_dropZone->show(); m_dropZone->raise();
    }

    void dragPaneEnd(QWidget *dragged, const QPoint &global, bool drop) {
        if (m_dropZone) { m_dropZone->hide(); m_dropZone->deleteLater(); m_dropZone = nullptr; }
        if (!drop || !dragged) return;
        bool taughtBeneath = false;
        const auto target = dropTarget(dragged, global);
        if (target.second == Edge::None) return;
        // Which side of its drop anchor the pane came from (#Q7Y9): known only here, before
        // takeLeaf unhooks it. A drop that docks it beneath is the slow path of the chord.
        const QString sideKey = [&] {
            QWidget *anchor = target.first;
            if (target.second != Edge::Bottom || !anchor || !dragged || pageOf(anchor) != pageOf(dragged)) return QString();
            QWidget *page = pageOf(anchor);
            const QRect a(anchor->mapTo(page, QPoint(0, 0)), anchor->size());
            const QRect d(dragged->mapTo(page, QPoint(0, 0)), dragged->size());
            // The chord starts with the move that takes the pane TOWARD the anchor: dragged from
            // the anchor's right, it is Move-left then Move-down. Naming the side the pane came
            // from instead sent the user the opposite way (#Q7Y9 review).
            const std::optional<relay::panes::Direction> toward = relay::panes::moveToward(d, a);
            if (!toward) return QString();
            return Keymap::instance().shortcutText(*toward == relay::panes::Direction::Left
                                                       ? QStringLiteral("pane.moveLeft")
                                                       : QStringLiteral("pane.moveRight"));
        }();
        if (target.second == Edge::TabBar) {
            RelayWindow *w = windowOf(target.first);
            // A label means "into that tab" (#A0SF); the bar's empty space and the new-tab
            // button keep today's behaviour and give the pane a tab of its own.
            QWidget *page = nullptr;
            if (const int tab = tabBarIndexAt(target, global); tab >= 0) page = w->m_tabs->widget(tab);
            if (page == pageOf(dragged)) return;   // dropped on the tab it already lives in
            if (!page && w == this && leavesIn(pageOf(dragged)).size() <= 1) return;   // already its own tab here
            if (!takeLeaf(dragged)) return;
            if (!page) {
                w->adoptLeafAsTab(dragged);
            } else {
                // Dock beside the tab's first leaf, like a Right-edge drop into that page. Made
                // current first: insertBeside sizes the newcomer from the page it lands in, and a
                // page the QTabWidget keeps hidden has no size to give it yet.
                w->m_tabs->setCurrentWidget(page);
                const auto leaves = w->leavesIn(page);
                if (leaves.isEmpty()) w->adoptLeafAsTab(dragged);
                else w->insertBeside(leaves.first(), dragged, Qt::Horizontal, false);
                w->m_tabs->setCurrentWidget(page);
                w->setActiveLeaf(dragged); focusLeaf(dragged);
                if (w != this) { w->raise(); w->activateWindow(); }
            }
        } else {
            QPointer<QWidget> anchor(target.first);
            if (!takeLeaf(dragged) || !anchor) return;
            const Qt::Orientation orientation = target.second == Edge::Left || target.second == Edge::Right ? Qt::Horizontal : Qt::Vertical;
            RelayWindow *w = windowOf(anchor);
            w->insertBeside(anchor, dragged, orientation, target.second == Edge::Left || target.second == Edge::Top);
            w->m_tabs->setCurrentWidget(w->pageOf(dragged));
            w->setActiveLeaf(dragged); focusLeaf(dragged);
            if (w != this) { w->raise(); w->activateWindow(); }
            // The chord teaches itself the one time it is the faster path: dragged from beside
            // the anchor and dropped on its bottom edge.
            if (target.second == Edge::Bottom && !sideKey.isEmpty() && w == windowOf(dragged)) {
                const QString down = Keymap::instance().shortcutText(QStringLiteral("pane.moveDown"));
                if (!down.isEmpty()) {
                    w->hint(QStringLiteral("pane.dockBeneath"),
                            relay::ShortcutHints::nextTime(QStringLiteral("%1 then %2").arg(sideKey, down),
                                                           QStringLiteral("dock it beneath")));
                    taughtBeneath = true;
                }
            }
        }
        const QString move = Keymap::instance().shortcutText(QStringLiteral("pane.moveLeft"));
        if (!move.isEmpty() && !taughtBeneath)
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
        // The new tab is attached to nothing, so a pane that came out of an attached one loses
        // its board here rather than keeping the card tools of a project its tab no longer has
        // (#JN7X). Opening the Switchboard, or `/card`, attaches this tab on its own terms.
        repointTabPanes(page);
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
        const QString project = m_tabProject.value(page);   // the tab's project travels with it
        forgetTab(page);
        m_tabs->removeTab(index);
        page->setParent(nullptr);
        RelayWindow *window = m_manager->newEmptyWindow(geometry().translated(40, 40));
        window->adoptPage(page, lastActive);
        if (!tabName.isEmpty()) window->renameTab(tabName, false, page);
        if (!project.isEmpty())
            window->attachTab(page, project, QString::fromLatin1(relay::projects::kReasonRestored));
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
    // With no neighbor that way the pane is carried past the page's edge (movePastPageEdge).
    void moveActive(relay::panes::Direction direction) {
        endBeneathDock();   // a fresh move starts the chord over (#Q7Y9)
        QWidget *current = m_activeLeaf;
        QWidget *page = current ? pageOf(current) : nullptr;
        if (!page) return;
        QWidget *neighbor = neighborOf(current, direction);
        if (!neighbor) { movePastPageEdge(current, page, direction); return; }
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
        // A left/right move opens the two-second window in which the Move-down key docks the
        // pane beneath the neighbor it just moved toward (#Q7Y9): Ctrl+Alt+Left, Ctrl+Alt+Down.
        if (orientation == Qt::Horizontal) armBeneathDock(current, neighbor);
    }

    // The half of a keyboard move with no neighbour to swap with or dock beside: the pane is
    // at the page's edge in that direction, and the move carries it PAST that edge into a
    // column (left/right) or a row (up/down) of its own — the bottom pane of a stack in the
    // page's rightmost column becomes the whole of a new rightmost column. takeLeaf and a
    // reinsert, the same pair every other move uses, so the shell, the agent and the
    // scrollback travel with it: the root splitter takes the pane at its end (or start) when
    // it already runs that way, and is wrapped in a new splitter of that orientation when the
    // page runs the other way. The notice stays only where the move would change nothing: the
    // tab's only pane, and a pane that already fills that edge alone.
    void movePastPageEdge(QWidget *current, QWidget *page, relay::panes::Direction direction) {
        const QRect pageArea(QPoint(0, 0), page->size());
        const QRect area(current->mapTo(page, QPoint(0, 0)), current->size());
        if (leavesIn(page).size() <= 1) {
            notice(QStringLiteral("This pane is already the only pane in its tab."), 4000);
            return;
        }
        if (relay::panes::fillsTheEdge(area, pageArea, direction)) {
            notice(QStringLiteral("This pane already has that edge to itself."), 2500);
            return;
        }
        const QList<QPointer<QSplitter>> chain = relay::panes::enclosingSplitters(current);
        QSplitter *root = chain.isEmpty() ? nullptr : chain.last().data();
        if (!root) return;   // a page with more than one pane always has a splitter above them
        const Qt::Orientation orientation = relay::panes::orientationFor(direction);
        const bool towardStart = relay::panes::towardStart(direction);
        // Read before takeLeaf, while the list still describes the page the pane is leaving;
        // takeLeaf's unwrapping keeps the root's child count, so the list still fits after.
        if (root->orientation() == orientation) {
            // The pane always comes out of a stack here — a pane alone at the far end of a
            // matching root fills the edge and was refused above — so takeLeaf's unwrapping
            // happens inside the root and never empties it.
            const QList<int> kept = root->sizes();
            if (!takeLeaf(current)) return;
            root->insertWidget(towardStart ? 0 : root->count(), current);
            current->show();
            const QList<int> sized = relay::panes::sizesAfterEdgeDock(kept, towardStart);
            if (sized.size() == root->count()) root->setSizes(sized);
        } else {
            // The page runs the other way — rows, for a left/right move — so the pane goes past
            // the whole page: the root is wrapped in a splitter of the move's orientation, the
            // same dance insertBeside does around an anchor, with the root in the anchor's
            // place. The root is wrapped BEFORE takeLeaf detaches the pane: on a two-pane page
            // takeLeaf would otherwise unwrap the root away into the page layout and delete it,
            // and the wrapper would be built around a splitter that is already going.
            auto *outer = newSplitter(orientation);
            if (QWidget *parent = root->parentWidget(); parent && parent->layout())
                delete parent->layout()->replaceWidget(root, outer);
            outer->addWidget(root);
            root->show(); outer->show();
            if (!takeLeaf(current)) return;   // may collapse the root to its other pane, inside outer
            outer->insertWidget(towardStart ? 0 : outer->count(), current);
            current->show();
            // Size after the layout settles; sizes set on a hidden splitter follow size hints
            // instead. One equal share of the page's now two top-level regions is its half.
            QPointer<QSplitter> guard(outer);
            QTimer::singleShot(0, outer, [guard] {
                if (!guard) return;
                const int total = guard->orientation() == Qt::Horizontal ? guard->width() : guard->height();
                guard->setSizes({total / 2, total - total / 2});
            });
        }
        setActiveLeaf(current); focusLeaf(current);
        updateTitles();
        // No beneath-dock chord to arm (#Q7Y9): it docks the pane beneath the neighbour it just
        // moved toward, and past the page's edge there is no neighbour on that side.
    }

    // Ctrl+Alt+0 (owner report, 2026-09-19: pane sizes jiggle): every splitter in the active tab
    // back to equal shares, top to bottom, so a page a drag has left lopsided goes back to a tidy
    // grid in one key. The same "identical
    // entries, let Qt turn them into ratios of the real width" trick insertBeside's equal-shares
    // fallback and movePastPageEdge's outer wrapper use, just applied to every splitter in the
    // page rather than one.
    void equalizeActivePage() {
        QWidget *page = m_active ? pageOf(m_active) : (m_tabs ? m_tabs->currentWidget() : nullptr);
        const QList<QPointer<QSplitter>> splitters = relay::panes::splittersIn(page);
        for (const auto &splitter : splitters) {
            if (!splitter) continue;
            const QList<int> sizes = splitter->sizes();
            qint64 total = 0;
            for (int size : sizes) total += size;
            // The Switchboard's split comes first (#BXCN): a board pane in the splitter takes its
            // 900 px floor and the panes beside it divide the rest equally. Vertical splitters
            // pass all-zero floors — the floor is a width, not a height.
            QList<int> floors;
            if (splitter->orientation() == Qt::Horizontal)
                for (int i = 0; i < splitter->count(); ++i) floors.append(boardSplitFloor(splitter->widget(i)));
            else
                for (int i = 0; i < splitter->count(); ++i) floors.append(0);
            const QList<int> sized = relay::panes::sizesAfterEqualize(sizes, floors);
            if (!sized.isEmpty()) {
                splitter->setSizes(sized);
                continue;
            }
            // Not laid out yet (or nothing to divide): identical entries, let Qt turn them into
            // ratios of the real width.
            QList<int> equal;
            for (int i = 0; i < splitter->count(); ++i) equal.append(1000);
            splitter->setSizes(equal);
        }
        if (!splitters.isEmpty()) notice(QStringLiteral("Panes equalized"), 2000);
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

    // closed.list: the session manager pane, on its "Recently closed" tab (card #R6J0 owns the
    // pane; registerClosedTab() below is what puts the tab in it).
    void openClosedList() { openSessions(QStringLiteral("closed")); }

public:
    // The "Recently closed" tab of every session manager (src/ClosedList.h). One widget per pane,
    // all showing the manager's one list and refreshed whenever it changes.
    static void registerClosedTab() {
        addSessionsTab(QStringLiteral("closed"), QStringLiteral("Recently closed"), [](RelayWindow *window) -> QWidget * {
            auto *view = new relay::closed::ListView;
            WindowManager *manager = window->m_manager;
            QPointer<RelayWindow> guard(window);
            view->setRecords(manager->closedRecords());
            manager->watchClosed(view, [view, manager] { view->setRecords(manager->closedRecords()); });
            view->onReopen = [manager, guard](const QString &id) { manager->restoreClosed(guard, id); };
            view->onDiscard = [manager](const QString &id) { manager->discardClosed(id); };
            view->onClear = [manager, guard, view] {
                if (QMessageBox::question(view, QStringLiteral("Clear recently closed?"),
                                          QStringLiteral("Forget all %1 closed items and the terminal text saved for them? "
                                                         "Their conversations stay in Sessions.").arg(manager->closedRecords().size()),
                                          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes)
                    manager->forgetClosed();
            };
            return view;
        });
    }
private:

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
        // Closing the one pane kind that can hold unsaved edits asks first (card #SEJ2):
        // Save writes them, Discard closes, Cancel keeps the pane.
        if (auto *tool = dynamic_cast<ToolPane *>(pane);
            tool && tool->kind() == ToolPane::Kind::Preview && tool->preview()->isDirty()) {
            const auto choice = QMessageBox::warning(this, QStringLiteral("Unsaved changes"),
                QStringLiteral("Save your changes to %1 before closing?").arg(tool->preview()->title()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (choice == QMessageBox::Cancel) return;
            // A save that failed (or could not start) keeps the pane, so the edits stay put.
            if (choice == QMessageBox::Save && !tool->preview()->save()) return;
        }
        // The Switchboard's own key (Ctrl+Shift+S) closes it too; a board pane closed any other
        // way — the pane's ×, Ctrl+W — is the slow path that hint names, once.
        if (!m_boardClosedByToggle)
            if (auto *closedBoard = dynamic_cast<ToolPane *>(pane); closedBoard && closedBoard->board()) {
                const QString boardKeys = Keymap::instance().shortcutText(QStringLiteral("board.open"));
                if (!boardKeys.isEmpty())
                    hint(QStringLiteral("board.close"),
                         QStringLiteral("Next time: %1 closes the Switchboard too").arg(boardKeys), 1);
            }
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
            item.window = this; item.anchor = neighbor; item.sibling = focusNext;
            item.record.kind = relay::closed::Record::Pane;
            item.record.orientation = splitter->orientation(); item.record.before = index == 0;
            item.record.sizes = splitter->sizes(); item.record.slot = index;
            item.record.layout = serializeNode(pane);
            item.record.titles = leafTitles(pane);
            saveScrollbacksIn(pane);
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
        if (record && !serializeTab(index).isEmpty()) {
            ClosedItem item;
            item.window = this;
            item.record.kind = relay::closed::Record::Tab;
            item.record.index = index;
            item.record.layout = serializeTab(index);
            item.record.tabNames = QStringList{m_tabNames.value(page)};
            item.record.titles = leafTitles(page);
            saveScrollbacksIn(page);
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
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("closed.restore"));
        text += QStringLiteral("\n\n%1 reopens it in the same directories, with its text and conversations and new shells.")
                    .arg(keys.isEmpty() ? QStringLiteral("\"Restore closed\" in the palette") : keys);
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
        // Only the tabs that can be rebuilt, so the names and titles beside them stay in step.
        ClosedItem item;
        item.record.kind = relay::closed::Record::Window;
        for (int i = 0; i < m_tabs->count(); ++i) {
            const QJsonObject node = serializeTab(i);
            if (node.isEmpty()) continue;
            if (i <= m_tabs->currentIndex()) item.record.index = int(item.record.tabs.size());
            item.record.tabs.append(node);
            item.record.tabNames.append(m_tabNames.value(m_tabs->widget(i)));
            item.record.titles += leafTitles(m_tabs->widget(i));
        }
        if (item.record.tabs.isEmpty()) return;
        item.record.geometry = geometry();
        // The panes' text is written by noteWindowClosing(), which runs next.
        m_manager->remember(item);
    }

    // Recently closed: what each leaf under `root` is called, in the order serializeNode() walks
    // them and leaving out the ones it leaves out.
    QStringList leafTitles(QWidget *root) const {
        QStringList titles;
        for (QWidget *leaf : leavesIn(root)) {
            if (serializeNode(leaf).isEmpty()) continue;
            if (auto *pane = dynamic_cast<Pane *>(leaf)) titles << pane->paneTitle();
            else if (auto *tool = dynamic_cast<ToolPane *>(leaf)) titles << tool->title();
            else titles << QString();
        }
        return titles;
    }

    // A pane that closes on its own takes its shell with it, so its terminal text is written now
    // (the window-close and quit paths write every pane's; src/WindowState.h).
    void saveScrollbacksIn(QWidget *root) const {
        if (!m_manager->writesState()) return;
        for (QWidget *leaf : leavesIn(root))
            if (auto *pane = dynamic_cast<Pane *>(leaf)) pane->saveScrollback();
    }

    WindowManager *m_manager;
    // The helper agents of this window: one per tab, keyed by the tab's persistent id and started
    // by the first thing that asks it something — a Switchboard opening, or a question typed into
    // Options, Actions or Sessions (card #FEJQ, protocol §30.7, owner 2026-09-20). It was keyed by
    // board root until then, which gave the same project open in two tabs one worker and one
    // conversation; the owner's rule is that Switchboards are per tab, so each tab gets its own
    // agent over the one shared set of board files. It ends when the tab does (forgetTab).
    QMap<QString, QPointer<relay::BoardWorker>> m_boardWorkers;
    // Per tab, the panes whose panels are waiting on the helper: a turn's own pane and any pane
    // with a prompt queued behind it. `helperWorkerGone` is the only reader (#H6VQ).
    QMap<QString, QSet<QString>> m_helperWaiting;
    QSet<QString> m_helperStarting;   // tabs whose helper startBoardWorker() is building right now
    // ----- the agent consoles this window made (#AGNT step 5) ---------------------------------
    //
    // A console is not in any leaf list and is not serialised, so this is the only place the
    // window knows about one. The context travels with it because the window made that one: it
    // wraps the host's with the tab's workspace and conversation key, and it must not outlive the
    // pane that holds it — `~Pane` clears its `onChanged` — so the list is pruned by the pane
    // going null, never by the host.
    struct ConsoleEntry {
        QPointer<Pane> pane;
        std::shared_ptr<TabConsoleContext> context;
    };
    QList<ConsoleEntry> m_consoles;
    // Per tab, the console whose context that tab's one worker is configured with: the last one
    // to speak. What differs between the consoles of a tab is the brief and the surface name,
    // never the store, so swapping it reconfigures without moving the conversation.
    QHash<QString, QPointer<Pane>> m_tabConsole;
    // What a tab's worker has already said about itself, kept so a console created after it was
    // up can be handed the handshake it missed. Keyed by `handshakeKey(tab, "ready"|"configured")`.
    QHash<QString, QJsonObject> m_workerHandshake;
    // /update: the one running updater (scripts/relay-update.py), its unread output and whether
    // its final line was the UPDATED marker the restart waits for. One at a time.
    QProcess *m_updateProcess = nullptr;
    QByteArray m_updateOutput;
    bool m_updateInstalled = false;
    QTabWidget *m_tabs = nullptr;
    QPointer<Pane> m_returnPane;        // where focus was when the Settings pane opened
    QPointer<QWidget> m_returnFocus;
    // True while toggleBoardPane is closing the Switchboard with its own key, so closePane does
    // not hint that key to the person who has just used it.
    bool m_boardClosedByToggle = false;
    QPointer<Pane> m_active;
    bool m_tabShareSyncQueued = false;   // syncTabShares is coalesced to one pass per event loop
    // Panes this window has already offered to the always-on share (#PH0N), so one that cannot be
    // shared is not asked again on every tick. Cleared when the switch moves.
    QSet<QString> m_autoShared;
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
    // "Move left/right, then ↓ docks it beneath" (#Q7Y9): the same shape for the chord's window.
    relay::panes::PlacementWindow m_beneath;
    QElapsedTimer m_beneathClock;
    QTimer m_beneathTimer;
    QPointer<QWidget> m_beneathPane, m_beneathAnchor;
    // Pane state glyphs and tab icons (#XM0T, #V8KT): the poll, the live mark's step on that
    // poll's beat, and each tab's last icon so it is only repainted when what it shows changes.
    QTimer m_statusTimer;
    QTimer m_pulseTimer;                       // the blink grid's next boundary, for the tab dots
    QHash<QWidget *, QString> m_tabIconKey;
    QHash<QWidget *, TabMark> m_tabMark;
    QHash<QWidget *, TabUsageState> m_tabUsage;  // each tab's usage window, label text and clock
    bool m_fedLiveUsage = false;               // whether any session tag was pushed last poll
    QPointer<QWidget> m_placementPane, m_placementAnchor;
    QPointer<QLabel> m_placementHint;
    QPointer<QToolButton> m_newTabButton;
    // The last action run from the keyboard and the key that ran it, so an action can name the
    // combination that reached it. Cleared by whoever reads it; a palette run never sets it.
    QPair<QString, QString> m_lastShortcut;
    // Window header (see buildWindowChrome). m_nativeFrame: this window kept the system title bar.
    static constexpr int kFrameMargin = 5;
    bool m_nativeFrame = false;
    QPointer<ChromeButton> m_bell, m_minimize, m_maximize, m_close;
    QPointer<ChromeButton> m_connect;   // the plug: join a shared session
    // The tool-pane buttons, by the pane type each owns (relay::panestatus::toolButtons()).
    QHash<QString, QPointer<ChromeButton>> m_toolButtons;
    QPointer<NotificationsPopup> m_notifications;
    // The walking state of notifications.jump (#NQP9): which entry of the newest-first list the
    // next press goes to, and the single-shot timer that ends a walk (~4 s).
    int m_notificationJumpIndex = 0;
    QTimer m_notificationJumpReset;
    Qt::Edges m_manualEdges;
    QPoint m_manualFrom;
    QRect m_manualGeometry;
    QPointer<QFrame> m_dropZone;
    bool m_confirmedClose = false, m_skipRemember = false;
};
