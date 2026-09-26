#include "PaneTabNavigation.h"
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
#include "AuxiliaryZoom.h"
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
#include "ProjectsPane.h"
#include "GlobalsPane.h"
#include "Hints.h"
#include "Notifications.h"
#include "ScreenPrompt.h"
#include "PaneLayout.h"
#include "TabTearOff.h"    // when a tab-label drag leaves its window and becomes a tab move (#W6ES)
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
#include "ModelsPane.h"        // the models pane (Ctrl+Shift+M): providers, available, priorities (#MDL1 t:a11)
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
#include "Speech.h"
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
#include "ContextDock.h"      // the docked agent row Test suites and Sharing host (#3B1B); the
                              // panes' headers only forward-declare it, and the window calls it
#include "ProfilePane.h"      // the Profile result pane, ditto (card #7BM4 phase 5)
#include "ActionPalette.h"    // the Actions palette, Ctrl+? (card #MAGP)
#include "PaletteCardSearch.h" // exact #card lookup for Actions search (#WM4K)

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
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QPushButton>
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
#include "ActivePaneClose.h"
#include <functional>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <utility>

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
#ifdef Q_OS_WIN
            const QString address = QStringLiteral("relay-open-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_server.setSocketOptions(QLocalServer::UserAccessOption);
#else
            const QString address = m_socketDir.filePath(QStringLiteral("open.sock"));
#endif
            if (m_server.listen(address)) {
                qputenv("RELAY_OPEN_SOCKET", address.toUtf8());
                publishSocketAddress(address);
                QObject::connect(&m_server, &QLocalServer::newConnection, [this] {
                    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
                        QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                        QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                            if (!socket->canReadLine()) { if (socket->bytesAvailable() > 65536) socket->abort(); return; }
                            const auto request = QJsonDocument::fromJson(socket->readLine(65536)).object();
                            if (request.value(QStringLiteral("type")) == QLatin1String("drive"))
                                socket->write(QJsonDocument(handleDrive(request)).toJson(QJsonDocument::Compact) + '\n');
                            else {
                                const bool ok = handleOpen(request);
                                socket->write(ok ? "ok\n" : "error\n");
                            }
                            socket->flush();
                            socket->disconnectFromServer();
                        });
                    }
                });
            }
        }
    }
    bool handleOpen(const QJsonObject &request);
    QJsonObject handleDrive(const QJsonObject &request);
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
    RelayWindow *newEmptyWindow(const QRect &geometry, bool background = false);   // the caller adopts a tab into it
    QList<Pane *> backgroundPanes() const;
    void refreshBackgroundTasks();
    int backgroundCount(const QString &state) const;
    bool lastVisibleWindow(const RelayWindow *window) const;
    void cycle(RelayWindow *from, int delta);
    // ----- recently closed (src/ClosedStack.h) --------------------------------------------------
    // Every closed pane, tab and window, newest last. `restore` brings the newest back
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
    // file reads back as "you attached this because you opened its Board" and never as a
    // guess Relay made about a directory somebody happened to `cd` into.
    relay::projects::Registry &projects() {
        if (!m_projectsLoaded) { m_projectsLoaded = true; m_projects.load(); }
        return m_projects;
    }

    // "Initialize a project and create a Board here?", raised or answered "Not now" for
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
    // process. Ownership is decided once at startup. Only the owner restores and saves; a second
    // Relay opens a plain window and leaves the layout alone for its entire lifetime. Retrying the
    // lock after the owner quits would replace the old layout with that plain window on a fast
    // restart, before the next launch can restore it.
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
    QHash<QString, QString> m_backgroundStates;
    qint64 m_backgroundRefreshedAt = 0;
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
    QTimer m_scrollbackTimer{&m_context};
    QString m_statePath, m_restoreNote;
    std::unique_ptr<QLockFile> m_stateLock;
    bool m_owner = false, m_layoutLockAttempted = false, m_saveSuspended = false, m_cascadeActive = false;
    QJsonArray m_cascadeSnapshot;
};

class RelayWindow final : public QMainWindow {
    friend class WindowManager;
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
                        // An empty pane id is a device asking to be paired (#SMDX): it belongs to
                        // no pane, so the active terminal pane (or the first) hosts the Sharing
                        // pane, opened on Devices where the approval card is.
                        const bool deviceAsk = paneId.isEmpty();
                        Pane *owner = deviceAsk ? nullptr : paneWithToken(paneId);
                        if (deviceAsk) {
                            if (!answersDeviceAsks()) return;   // another window is the one to ring
                            owner = dynamic_cast<Pane *>(m_activeLeaf.data());
                            if (!owner) {
                                const QList<Pane *> panes = allPanes();
                                owner = panes.isEmpty() ? nullptr : panes.first();
                            }
                        }
                        if (!owner) return;          // another window is sharing that pane
                        owner->notifyFromWindow(title, body, relay::NotificationCenter::kindWarning);
                        relay::sharing::SharingView *view = sharingViewFor(owner, false);
                        if (view && deviceAsk) view->showPage(relay::sharing::SharingView::Page::Devices);
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
        // No toolbar: the tab bar starts at the top. Its actions live in the palette (Ctrl+?).
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
        // Coalesced (#BT7C): a burst of `presets` events from many workers is one fan-out.
        relay::SettingsWatch::instance().listen(this, [this] { scheduleAppCatalog(); });
        // Cross-pane messaging (#R5TC, protocol 37): every pane registers in the process-wide
        // directory; when the roster settles (250 ms of quiet) each pane pushes it to its worker,
        // so every agent can pane_list the others. The whole-process sweep is paneWithSession's
        // shape: several windows share one directory.
        relay::panedir::Directory::instance().setEnabled(
            QSettings().value(QStringLiteral("agent/cross_pane"), true).toBool());
        relay::panedir::Directory::instance().setRosterSink([] {
            for (QWidget *top : QApplication::topLevelWidgets())
                if (auto *w = dynamic_cast<RelayWindow *>(top))
                    for (Pane *pane : w->allPanes()) pane->pushPaneRoster();
        });
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
        static_cast<ThemeTabBar *>(m_tabs->tabBar())->onMiddleClick = [this](int index) {
            requestCloseTab(index);
        };
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
        // The consoles let go of this window before anything else does (card #CTRN). `m_consoles`
        // is an ordinary member, and members are destroyed *after* this body and **before**
        // `~QWidget` deletes the child widgets — so on a quit that deletes the window outright
        // (a SIGTERM never runs `closeEvent`; see src/main.cpp) every `TabConsoleContext` was
        // freed while the console panes holding a pointer to it were still alive. Two
        // use-after-frees followed: `~Pane` writing `m_context->onChanged = nullptr` into the
        // freed wrapper, and each pane's `destroyed` handler reading a `QList` whose destructor
        // had already run. Both are undone here, while everything is still alive.
        const QList<ConsoleEntry> consoles = m_consoles;
        m_consoles.clear();
        for (const ConsoleEntry &entry : consoles) {
            if (Pane *pane = entry.pane.data()) {
                disconnect(pane, &QObject::destroyed, this, nullptr);
                pane->forgetContext();
            }
        }
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
        restoreWorkspaceGroup(page, tab.value(QStringLiteral("artifact_workspace")).toObject());
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
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *window = dynamic_cast<RelayWindow *>(top))
                for (Pane *pane : window->allPanes()) {
                    if (!pane->sessionId().isEmpty()) ids.append(pane->sessionId());
                    const QString source = pane->sessionTextSource();
                    if (relay::conversations::isGuestSource(source) && !pane->guestSessionId().isEmpty())
                        ids.append(source + QLatin1Char(':') + pane->guestSessionId());
                }
        std::sort(ids.begin(), ids.end());
        ids.removeDuplicates();
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
        if (property("backgroundSession").toBool()) {
            m_manager->focusPane(pane->sessionToken());
            return;
        }
        if (QWidget *page = pageOf(pane)) m_tabs->setCurrentWidget(page);
        setActiveLeaf(pane);
        setProperty("backgroundSession", false);
        if (isMinimized() || !isVisible()) showNormal();
        raise();
        activateWindow();
        focusLeaf(pane);
    }

    // Opening a background session moves its live pane into this window. The hidden holder
    // closes after its last leaf is detached; the pane's worker and conversation travel intact.
    bool openBackgroundPane(Pane *pane) {
        auto *source = pane ? dynamic_cast<RelayWindow *>(pane->window()) : nullptr;
        if (!source || source == this || !source->property("backgroundSession").toBool()) return false;
        QWidget *anchor = m_activeLeaf;
        if (!anchor || !pageOf(anchor)) {
            QWidget *page = m_tabs->currentWidget();
            const auto leaves = page ? leavesIn(page) : QList<QWidget *>{};
            anchor = leaves.isEmpty() ? nullptr : leaves.first();
        }
        if (!source->takeLeaf(pane)) return false;
        if (anchor) {
            if (QWidget *page = pageOf(anchor)) m_tabs->setCurrentWidget(page);
            dockBeside(anchor, pane);
            setActiveLeaf(pane);
        } else adoptLeafAsTab(pane);
        m_manager->scheduleSave();
        revealPane(pane);
        return true;
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
        // A source of the tab's artifact workspace opens in its linked editor, at the line (#E85D).
        // A .tex opened from a shell offers its chain first (#R660): open beside, linked.
        if (!remote && info.isFile() && openWorkspaceChainSource(info.absoluteFilePath(), line, anchor)) return;
        if (!remote && info.isFile() && openInWorkspaceEditor(info.absoluteFilePath(), line, anchor)) return;
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
            if (isWorkspaceViewer(tool)) continue;   // a workspace's editor or preview is not reused (#E85D)
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
            dockBeside(anchor, target);
        }
        if (kind == ToolPane::Kind::Preview && line > 0) target->preview()->goToLine(line);
        if (edit && kind == ToolPane::Kind::Preview) target->preview()->startEditing();
        setActiveLeaf(target);
        focusLeaf(target);
        updateTitles();
    }

    // ----- artifact workspaces (card #E85D) — the bodies are in src/RelayWindowWorkspace.cpp ------
    // A tab's editor, console and preview as one group: the two layout presets, the member id a
    // leaf saves in its node, the group the tab saves beside its node, source file:line going to
    // the linked editor, and the status strip over the preview (docs/ARCHITECTURE.md, 10b).
    void applyWorkspacePreset(const QString &layout, const QString &sourceHint = QString());
    bool openInWorkspaceEditor(const QString &path, int line, QWidget *anchor);
    bool isWorkspaceViewer(QWidget *leaf) const;
    QJsonObject workspaceGroupJson(QWidget *page) const;
    void restoreWorkspaceGroup(QWidget *page, const QJsonObject &json);
    void refreshWorkspace(QWidget *page);
    QObject *ensureWorkspace(QWidget *page);
    QJsonObject driveWorkspace(const QJsonObject &request);
    // ----- the chain (#R660): shell -> TeX editor -> PDF preview ---------------------------------
    // `relay open main.tex` from a shell (or a click on the file in its output) offers to open
    // the editor beside it, linked: the shell becomes the chain's head, the editor its downstream.
    // Returns true when the open is answered either way by the chain (formed, or declined and
    // handled by the caller's plain open); false when the path is not the chain's to take.
    bool openWorkspaceChainSource(const QString &path, int line, QWidget *anchor);
    // The next member of the chain downstream of its tail: the editor beside the head, then the
    // preview beside the editor. Applied by the Build that opens or refreshes a preview
    // (docs/TASK-PLUGINS.md relay.tex) and available as `relay-drive workspace next`.
    void openWorkspaceNext(QWidget *page);
    // The preset's shape for the members the chain has (a chain of two takes the preset's
    // prefix), for every member of the chain; used when the chain forms, grows, or moves.
    void dockWorkspaceChain(QWidget *page);
    // The chain chip in every member's chrome: the whole chain, this member bold (#R660 t:qr).
    void refreshChainChips(QWidget *page);
    // Closing a chain's head asks "Close the linked panes too?" — all of them, or just it.
    bool workspaceChainClose(QWidget *pane);
    // A member that lands in another tab of this window brings the chain with it, in order.
    void moveWorkspaceChain(QWidget *member);
    // A member whose file is gone at restore shows a placeholder with "Reopen", keeping its
    // place in the chain until the file is back (or another is picked for it).
    void reconcileMissingMembers(QWidget *page);
    static QJsonObject withWorkspaceMember(QWidget *leaf, QJsonObject node);
    static void tagWorkspaceMember(QWidget *leaf, const QJsonObject &node);

    // Alt+Z (files.toggleWrap): word wrap in the preview the user is reading. The one focus sits
    // in wins; otherwise the active leaf if it is a preview pane, else any preview pane in the
    // same tab. When nothing can wrap (no preview on screen, a rendered Markdown), nothing acts.
    void toggleWrapNear(QWidget *leaf) {
        if (QWidget *focused = QApplication::focusWidget())
            for (QWidget *w = focused; w; w = w->parentWidget())
                if (auto *preview = dynamic_cast<relay::FilePreview *>(w); preview && preview->toggleWrap()) return;
        QWidget *page = pageOf(leaf);
        if (!page) return;
        const auto leaves = leavesIn(page);
        for (QWidget *l : leaves)
            if (auto *tool = dynamic_cast<ToolPane *>(l);
                tool && tool->kind() == ToolPane::Kind::Preview && tool == leaf && tool->preview()->toggleWrap()) return;
        for (QWidget *l : leaves)
            if (auto *tool = dynamic_cast<ToolPane *>(l);
                tool && tool->kind() == ToolPane::Kind::Preview && tool->preview()->toggleWrap()) return;
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
            dockBeside(owner, target);
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
    // `preset` is an account's harness preset (#M8S2): its session resumes there and nowhere else.
    void openGuestPane(Pane *source, const QString &guest, const QStringList &extra, const QString &cwd,
                       const QString &preset = QString()) {
        if (!source || source->window() != this || guest.isEmpty()) return;
        const QString directory = QFileInfo(cwd).isDir() ? cwd : source->cwd();
        Pane *pane = nullptr;
        try { pane = createPane({{"cwd", directory}, {"workspace", directory}}); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return; }
        dockBeside(source, pane);
        setActive(pane);
        // Tier A (protocol 29.4): when the worker can run this guest's harness, the new pane
        // resumes the session on its own agent instead — Relay's conversation, the guest's
        // session, no TUI. The source pane is asked because the new one has no presets yet; its
        // own worker answers the same way, and `resumeGuestPreset` waits for that answer.
        if (source->guestHarnessUsable(guest)) {
            const Pane::GuestResume resume = Pane::guestResumeFrom(extra);
            pane->resumeGuestPreset(guest, resume.sessionId, resume.fork, directory, extra, preset);
            return;
        }
        if (!preset.isEmpty()) return;   // an account's session never falls back to the default login
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
        dockBeside(source, pane);
        setActive(pane);
        QTimer::singleShot(0, pane, [pane] { pane->focusInput(); });
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

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
            const bool busy = std::any_of(panes.cbegin(), panes.cend(), [](Pane *p) { return p->hasCloseWork(); })
                || (m_manager->lastVisibleWindow(this) && !m_manager->backgroundPanes().isEmpty());
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
    void runAction(const QString &id, Pane *target = nullptr) { runActionNow(id, target); }

    void runActionNow(const QString &id, Pane *target = nullptr);

    // ----- palette ----------------------------------------------------------------------------
    using PaletteItem = relay::ActionItem;

    // ----- Actions pane and Options pane (src/SettingsPane.h) -----------------------------------
    // Two panes, beside the focused pane (owner, 2026-09-18: a full pane, not a strip over the
    // right edge). Actions is the shortcut list since #MAGP (the palette's "Shortcut list" row;
    // Ctrl+? opens the palette instead): one filterable list of everything you can do now, with
    // its keys — resume, the Switchboard, the model, a new pane, rewind, Options itself.
    // Ctrl+Shift+O, Ctrl+, and the gear are Options: what persists, every setting as a real
    // control, one tab per section. Either search reaches both catalogs, so "open it, type,
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
        // The live key, never a written one (RELAY.md's standing rule): the collapsed row says it.
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
                                QStringLiteral("the agent")));
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
            if (relay::ArtifactDock *dock = tool->preview() ? tool->preview()->artifactDock()
                                            : tool->plan()   ? tool->plan()->artifactDock() : nullptr)
                dock->setHelperWorkspace(workspace);   // "Review before apply" is per project (#PBZ4)
            if (auto *models = modelsViewOf(tool)) {
                models->setHelperTabId(tab);
                models->setHelperWorkspace(workspace);
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

    // "more models…" on a console's box, and Ctrl+Shift+M in one: the same models pane a terminal
    // pane opens, because a console *is* a terminal pane with no shell. Nothing is kept here —
    // the console asks with its own catalog and its own current row.
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
    // The Switchboard the keyboard is in, or null — asked of the focus widget for the same
    // reason as focusedConsole() just above: the active leaf is the last terminal pane, which
    // is not where the key went when a card is open. A terminal pane is never inside a board,
    // so Ctrl+F in one of those still finds in that terminal and never reaches this walk.
    relay::BoardView *focusedBoardView() const {
        for (QWidget *widget = QApplication::focusWidget(); widget != nullptr; widget = widget->parentWidget())
            if (auto *board = dynamic_cast<relay::BoardView *>(widget)) return board;
        return nullptr;
    }
    bool helperComposerHasFocus() const { return focusedConsole() != nullptr; }
    void openConsoleModelBox() {
        if (Pane *console = focusedConsole()) console->openModelBox();
    }
    // The pane that owns the subagent pane the keyboard is in, or null — asked of the focus
    // widget for the same reason as focusedConsole() above: the active leaf is the last terminal
    // pane, which is not where the key went. The subagent pane is a leaf of its own and not a
    // Pane, so Alt+M there would otherwise open the owner terminal's box (owner, 2026-09-25:
    // "alt m doesnt work to change models from the subagent pane. it should"); it must open the
    // current tab's box, and the owner pane is the one that fills it.
    Pane *subagentModelBoxOwner() const {
        relay::SubagentTabsView *view = nullptr;
        for (QWidget *widget = QApplication::focusWidget(); widget != nullptr && !view;
             widget = widget->parentWidget())
            view = dynamic_cast<relay::SubagentTabsView *>(widget);
        if (!view) return nullptr;
        for (Pane *pane : allPanes())
            if (pane->subagentTabs() == view) return pane;
        return nullptr;
    }

    void focusHelperOfActiveLeaf() {
        auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data());
        if (tool && tool->settings()) { tool->settings()->focusHelper(); return; }
        if (tool && tool->board()) { tool->board()->focusChat(); return; }
        if (auto *sessions = sessionsViewOf(tool)) { sessions->focusHelper(); return; }
        if (auto *models = modelsViewOf(tool)) { models->focusHelper(); return; }
        // The Test suites and Sharing panes' docked agent (card #3B1B).
        if (auto *tests = testSuitesViewOf(tool)) { tests->agentDock()->focusHelper(); return; }
        if (auto *sharing = tool && tool->kind() == ToolPane::Kind::Sharing
                                ? dynamic_cast<relay::sharing::SharingView *>(tool->hosted()) : nullptr) {
            sharing->agentDock()->focusHelper();
            return;
        }
        // A text or Markdown file, and a plan: the agent docked under the editor (#PBZ4).
        relay::ArtifactDock *dock = !tool           ? nullptr
                                  : tool->preview() ? tool->preview()->artifactDock()
                                  : tool->plan()    ? tool->plan()->artifactDock() : nullptr;
        if (dock && !dock->isHidden()) { dock->focusHelper(); return; }
        notice(QStringLiteral("The agent is in Options, Actions, Sessions, Models, Tests, Sharing, "
                              "the Board and file editors — open one of those and ask it there."), 5000);
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

    // Card #05J2: Options / palette "Export settings…" and "Import settings…"
    // over the versioned settings bundle (src/SettingsExport.h).
    void exportSettingsDialog();
    void importSettingsDialog();

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
            if (anchor) dockBeside(anchor, tool);
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

    // ----- the models pane (Ctrl+Shift+M; card #MDL1 t:a11, design 5.8) -------------------------
    //
    // "lets build the models pane … and just remove ctrl alt m" (owner, 2026-09-21). One pane per
    // tab, beside the pane it serves, hosted exactly the way Options is. The key is a toggle in
    // three states:
    //
    //   not open                    → open it beside the active pane, serving that pane, on the
    //                                 priorities tab at that pane's own class, filter focused;
    //   open and focused            → close it ("typing it again closes the pane");
    //   open, the focus elsewhere   → re-target it at the pane that asked, and focus it.
    //
    // Escape inside it hands the focus back and leaves it open, which is `Target::focusBack`.
    static ToolPane *modelsPaneIn(QWidget *page) {
        if (!page) return nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Models) return tool;
        return nullptr;
    }
    static relay::ModelsPane *modelsViewOf(ToolPane *tool) {
        return tool ? dynamic_cast<relay::ModelsPane *>(tool->hosted()) : nullptr;
    }

    ToolPane *createModelsPane(const QString &cwd) {
        // Sources combines the hosted-provider rows and a short local-model summary in one
        // embedded Options page. The two full Options sections remain the source of their state.
        auto viewRef = std::make_shared<QPointer<relay::ModelsPane>>();
        auto *view = new relay::ModelsPane([this, viewRef] {
            relay::SettingsSection sources = modelsSection(true);
            // Keep account setup first, local servers next, and profiles last. Each row retains
            // the callback and ID from its Options section, so these are the same controls.
            const int profilesAt = [&sources] {
                for (int i = 0; i < sources.rows.size(); ++i)
                    if (sources.rows.at(i).id == QStringLiteral("heading:profiles")) return i;
                return sources.rows.size();
            }();
            const QList<relay::SettingRow> profiles = sources.rows.mid(profilesAt);
            sources.rows = sources.rows.mid(0, profilesAt);
            sources.rows += localModels().compactSection().rows;
            relay::SettingRow setup;
            setup.kind = relay::SettingRow::Button;
            setup.id = QStringLiteral("models.local.setup");
            setup.label = QStringLiteral("Set up a local model");
            setup.detail = QStringLiteral("The agent checks this machine and guides setup.");
            setup.buttonText = QStringLiteral("Ask agent…");
            setup.run = [viewRef] {
                if (!*viewRef) return;
                QTimer::singleShot(0, viewRef->data(), [viewRef] {
                    if (*viewRef) (*viewRef)->helperDraft(QStringLiteral(
                        "/skill local-model-setup Help me set up a local model for Relay on this machine. "
                        "Start with the read-only survey and ask before installing or downloading anything."));
                });
            };
            sources.rows << setup;
            relay::SettingRow more;
            more.kind = relay::SettingRow::Button;
            more.id = QStringLiteral("models.local.more");
            more.label = QStringLiteral("Find or manage local servers");
            more.detail = QStringLiteral("Find, add, remove or configure a local server in Options.");
            more.buttonText = QStringLiteral("Open Options…");
            more.run = [this] {
                openSettingsPane(relay::SettingsPane::Mode::Options,
                                 relay::LocalModelsSettings::sectionId());
            };
            sources.rows << more;
            sources.rows += profiles;
            return QList<relay::SettingsSection>{sources};
        });
        *viewRef = view;
        view->onSourcesShown = [this] { localModels().refresh(); };
        auto *tool = new ToolPane(ToolPane::Kind::Models, view, view, cwd);
        tool->setProperty("paneType", QStringLiteral("models"));
        relay::theme::polishWindow(tool);
        // The catalog is the served pane's, and it arrives late: a first-run window has no
        // providers until its worker answers `presets`, and a key added on the providers tab
        // changes what the other two may draw. Both already end in SettingsWatch::notify(), which
        // is what every Options pane redraws on, so this pane re-reads its target there too.
        QPointer<ToolPane> guard(tool);
        relay::SettingsWatch::instance().listen(tool, [guard] {
            if (!guard) return;
            if (guard->property("skipNextModelsCurationRefresh").toBool()) {
                guard->setProperty("skipNextModelsCurationRefresh", false);
                return; // this pane already applied its own edit; rebuilding it can undo a click in flight
            }
            if (auto *w = windowOf(guard)) w->refreshModelsPane(guard);
        });
        // The helper agent at the foot of all four tabs (owner, 2026-09-22: "there needs to be a
        // helper agent on the model page"), wired exactly as Options' and Sessions' are: the pane
        // owns the collapsed row and asks for a console on first expand; Alt+Q reaches it through
        // `focusHelperOfActiveLeaf`.
        wireConsoleHost(view, tool, QStringLiteral("models.ask"));
        return tool;
    }

    // The served pane's catalog, model and level again, keeping the tab, the filter, the class
    // list and the undo stack: `setTarget` with the token it already holds is a re-read, not a
    // re-target (src/ModelsPane.cpp).
    void refreshModelsPane(ToolPane *tool) {
        relay::ModelsPane *view = modelsViewOf(tool);
        if (!view) return;
        // The pane it serves, or — for one that came back with a layout and has not been pointed
        // at anything yet, and for one whose pane has since closed — the first terminal pane of
        // the tab it is in, which is what `linkRestoredModelsPane` would have given it.
        Pane *served = findPaneByToken(view->servedToken());
        if (!served) {
            const QList<Pane *> panes = panesIn(pageOf(tool));
            served = panes.isEmpty() ? nullptr : panes.first();
        }
        if (!served) return;
        applyModelsTarget(tool, served, served->modelsTarget(), QString(), QString());
    }

    // The models pane of the page this pane is on, re-read. What `Pane::onRolesResolved` calls:
    // the jobs tab's "runs on" column is the worker's last `model_roles`, so a report is the one
    // thing that has to reach an open models pane without anybody pressing a key.
    void refreshModelsPaneFor(Pane *served) {
        if (!served) return;
        if (ToolPane *tool = modelsPaneIn(pageOf(served))) {
            auto *view = modelsViewOf(tool);
            if (!view) return;
            const auto target = served->modelsTarget();
            if (view->servedToken() == target.token)
                view->setRoleSummaries(target.roleSummary, target.tierSummary);
        }
    }

    // Point an open models pane at a pane, with the tab and filter the caller asked for. Shared by
    // the key, by Options’ rows and by a models pane that came back with a saved layout.
    void applyModelsTarget(ToolPane *tool, Pane *served, relay::ModelsPane::Target target,
                           const QString &tab, const QString &filter) {
        relay::ModelsPane *view = modelsViewOf(tool);
        if (!view) return;
        QPointer<ToolPane> guard(tool);
        QPointer<Pane> back(served);
        // Before this pane's worker has answered, Providers uses the tab helper's presets.
        // Share that snapshot on both initial opening and refresh, including late guest scans.
        if (served && served->allPresets().isEmpty())
            target.catalog = relay::models::catalogFrom(m_helperPresets.value(tabIdOf(pageOf(tool))));
        target.now = QDateTime::currentSecsSinceEpoch();
        const auto listsChanged = target.listsChanged;
        target.listsChanged = [this, guard, listsChanged] {
            if (listsChanged) listsChanged();
            // The source widget has already updated itself. Keep its controls stable when the
            // batched SettingsWatch fan-out reaches the other panes a moment later.
            if (m_curatedTimer && m_curatedTimer->isActive()) m_curatedSource = guard;
        };
        target.focusBack = [guard, back] {
            auto *w = windowOf(guard);
            if (!w || !back) return;
            w->setActiveLeaf(back);
            back->focusInput();
        };
        view->setTarget(target);
        if (!tab.isEmpty()) view->showTab(tab);
        // An empty filter is "say nothing about the filter", not "rub out what is typed": a
        // re-target from the same pane must not throw away a search half made.
        if (!filter.isEmpty()) view->setFilter(filter);
        updateTitles();
    }

    // What `Pane::onOpenModelsPane` calls: every door into model picking (the key, /model,
    // /models, the box’s "more models…", and Options’ own rows through the pane they were
    // pressed on) arrives here with the served pane’s target already built.
    void openModelsPaneFor(Pane *served, const relay::ModelsPane::Target &target,
                           const QString &tab, const QString &filter) {
        QWidget *page = served ? pageOf(served) : m_tabs->currentWidget();
        if (!page) return;
        ToolPane *tool = modelsPaneIn(page);
        if (!tool) {
            m_returnPane = m_active;
            m_returnFocus = QApplication::focusWidget();
            tool = createModelsPane(served ? served->cwd() : m_manager->workspace());
            QWidget *anchor = served ? static_cast<QWidget *>(served)
                                     : (m_activeLeaf ? m_activeLeaf.data() : nullptr);
            if (anchor) dockBeside(anchor, tool);
            else if (page->layout()) page->layout()->addWidget(tool);
        }
        applyModelsTarget(tool, served, target, tab, filter);
        setActiveLeaf(tool);
        if (relay::ModelsPane *view = modelsViewOf(tool)) view->focusFilter();
        updateTitles();
    }

    void toggleModelsPane(Pane *served) {
        if (ToolPane *tool = modelsPaneIn(m_tabs->currentWidget());
            tool && (m_activeLeaf == tool || tool->isAncestorOf(QApplication::focusWidget()))) {
            closeModelsPane(tool);
            return;
        }
        Pane *on = served ? served : (m_active ? m_active.data() : focusedConsole());
        if (!on) { notice(QStringLiteral("Focus a pane first: the models pane always serves one.")); return; }
        on->openModelPicker();
    }

    void closeModelsPane(ToolPane *tool) {
        if (!tool) return;
        QWidget *page = pageOf(tool);
        // Esc and the toggle must never close the window: the last leaf of the last tab gets a
        // terminal beside it first, exactly as the Settings pane has always done.
        if (page && leavesIn(page).size() <= 1 && m_tabs->count() <= 1) {
            try { insertBeside(tool, createPane(paneNode(m_manager->workspace())), Qt::Horizontal, true); }
            catch (const std::exception &error) { notice(QString::fromUtf8(error.what())); }
        }
        Pane *back = nullptr;
        if (relay::ModelsPane *view = modelsViewOf(tool)) back = findPaneByToken(view->servedToken());
        if (!back) back = m_active;
        QPointer<Pane> guard(back);
        closePane(tool, false);
        if (guard) { setActiveLeaf(guard); guard->focusInput(); }
    }

    // A models pane that came back with the layout (buildNode): it serves the first terminal pane
    // of the tab it landed in, which is the pane it was beside when the window closed. Queued,
    // because buildNode() runs before the page the pane will live in exists.
    void linkRestoredModelsPane(ToolPane *tool, const QString &tab) {
        const QList<Pane *> panes = panesIn(pageOf(tool));
        Pane *served = panes.isEmpty() ? nullptr : panes.first();
        if (!served) return;
        applyModelsTarget(tool, served, served->modelsTarget(), tab, QString());
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
    QJsonObject appCatalogFor(QWidget *page) { return appCatalogForTab(tabIdOf(page)); }

    // During a fan-out each tab's catalog is built once, not once per pane (#BT7C): a window with
    // 26 panes rebuilt every Options section 26 times per notify, even when the gate then sent
    // nothing. Outside a fan-out it is built fresh, as §30.2 asks.
    QJsonObject appCatalogForTab(const QString &tab) {
        if (!m_fanOutCatalogs) return appCommands().catalog(tab);
        auto it = m_fanOutCatalogs->constFind(tab);
        if (it == m_fanOutCatalogs->constEnd()) it = m_fanOutCatalogs->insert(tab, appCommands().catalog(tab));
        return it.value();
    }
    QHash<QString, QJsonObject> *m_fanOutCatalogs = nullptr;

    // A settings change sends the catalog within half a second. The timer is not restarted by
    // later calls, so a steady stream of changes still goes out twice a second, never starved.
    void scheduleAppCatalog() {
        if (!m_appCatalogTimer) {
            m_appCatalogTimer = new QTimer(this);
            m_appCatalogTimer->setSingleShot(true);
            m_appCatalogTimer->setInterval(500);
            QObject::connect(m_appCatalogTimer, &QTimer::timeout, this, [this] { sendAppCatalog(); });
        }
        if (!m_appCatalogTimer->isActive()) m_appCatalogTimer->start();
    }
    QTimer *m_appCatalogTimer = nullptr;

    // The catalog changed — a setting written anywhere, a key added, the Agent toggle flipped — so
    // every worker of this window is sent the whole block again. It is still the whole block and
    // not a delta (§30.2), but each worker now only receives it when its bytes differ from what it
    // last got (#J0VY): the old resend-everything behaviour fed a presets → notify → resend →
    // echo loop that put ~50k `app_catalog_updated` events on the GUI thread in three hours.
    void sendAppCatalog() {
        QHash<QString, QJsonObject> built;
        QHash<QString, QJsonObject> *outer = std::exchange(m_fanOutCatalogs, &built);
        for (Pane *pane : allPanes()) if (pane) pane->sendAppCatalog();
        sendHelperCatalogs();   // the tab's helper worker runs the same tools (§30.7)
        m_fanOutCatalogs = outer;
    }

    // The helper workers of this window, each sent its own tab's block: the helper is an agent
    // like a pane's and has the same app tools (§30.7), so it is configured with the same catalog.
    void sendHelperCatalogs() {
        for (auto it = m_boardWorkers.cbegin(); it != m_boardWorkers.cend(); ++it)
            if (relay::BoardWorker *worker = it.value().data()) {
                const QJsonObject app = appCatalogForTab(helperTab(worker));
                // #J0VY: the same brake as `Pane::sendAppCatalog()`, held as a property so it dies
                // with the worker and a fresh helper always gets its first block.
                QByteArray last = worker->property("relayLastAppCatalog").toByteArray();
                if (!relay::AppCommands::catalogChanged(last, app)) continue;
                worker->setProperty("relayLastAppCatalog", last);
                worker->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("app_catalog")},
                                         {QStringLiteral("app"), app}});
            }
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

    // Two panes: Actions (things to do now; the palette's "Shortcut list" row), Ctrl+Shift+O and the gear
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

    // ----- the Actions palette (#MAGP) ---------------------------------------------------------
    // Ctrl+? opens it over the window; the same chord, Esc or a click outside
    // closes it. It reads the catalog afresh on every open, and actions run against the pane that
    // was focused before it opened (the palette is not a leaf, so opening it moves no pane). The
    // Actions pane is still there as the shortcut list: the palette's "Shortcut list" row.
    static Pane *paneForPaletteConversation(const QString &id) {
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *window = dynamic_cast<RelayWindow *>(top))
                for (Pane *pane : window->allPanes()) {
                    if (pane->sessionId() == id) return pane;
                    const QString source = pane->sessionTextSource();
                    if (relay::conversations::isGuestSource(source)
                        && source + QLatin1Char(':') + pane->guestSessionId() == id) return pane;
                }
        return nullptr;
    }

    static Pane *paneForPaletteToken(const QString &token) {
        if (token.isEmpty()) return nullptr;
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *window = dynamic_cast<RelayWindow *>(top))
                if (Pane *pane = window->findPaneByToken(token)) return pane;
        return nullptr;
    }

    void searchPaletteCards(const QString &query) {
        const QString id = relay::palettecards::code(query);
        QList<relay::ActionItem> items;
        if (!id.isEmpty()) {
            QSet<QString> seenRoots;
            auto addProject = [this, &seenRoots, &items, &id](const QString &project) {
                const QString root = relay::projects::boardDirOf(project);
                if (root.isEmpty() || seenRoots.contains(root) || items.size() >= 12) return;
                seenRoots.insert(root);
                const auto card = relay::palettecards::find(root, id);
                if (!card) return;
                const QString token = card->session;
                if (Pane *claimant = paneForPaletteToken(token)) {
                    relay::ActionItem paneItem;
                    paneItem.key = QStringLiteral("pane:") + token;
                    paneItem.section = QStringLiteral("Cards");
                    paneItem.label = QStringLiteral("Claiming pane · %1").arg(
                        claimant->paneTitle().isEmpty() ? shortPath(claimant->cwd()) : claimant->paneTitle());
                    paneItem.detail = QStringLiteral("#%1 · pane %2").arg(id, token.left(8));
                    paneItem.run = [this, token] { m_manager->focusPane(token); };
                    items.append(paneItem);
                }
                relay::ActionItem cardItem;
                cardItem.key = QStringLiteral("card:") + project + QLatin1Char('#') + id;
                cardItem.section = QStringLiteral("Cards");
                cardItem.label = QStringLiteral("#%1 · %2").arg(id, card->title);
                cardItem.detail = token.isEmpty() ? QStringLiteral("Open Board card")
                                : paneForPaletteToken(token) ? QStringLiteral("Open Board card · pane %1").arg(token.left(8))
                                                             : QStringLiteral("Open Board card · claimed by closed pane %1").arg(token.left(8));
                cardItem.run = [this, project, id] {
                    openNotificationSource(QStringLiteral("board:") + project + QLatin1Char('#') + id);
                };
                items.append(cardItem);
            };
            for (QWidget *top : QApplication::topLevelWidgets()) {
                auto *window = dynamic_cast<RelayWindow *>(top);
                if (!window) continue;
                for (int tab = 0; tab < window->m_tabs->count(); ++tab) {
                    const QString project = window->boardWorkspaceOfTab(window->m_tabs->widget(tab));
                    if (!project.isEmpty()) addProject(project);
                }
                for (Pane *pane : window->allPanes()) {
                    const QString project = relay::boardRootFor({pane->workspace(), pane->cwd()});
                    if (!project.isEmpty()) addProject(project);
                }
            }
        }
        if (m_palette) m_palette->setCardResults(query, items);
    }

    void searchPaletteConversations(const QString &query) {
        searchPaletteCards(query);
        Pane *owner = m_active;
        if (!owner) {
            const QList<Pane *> panes = allPanes();
            owner = panes.isEmpty() ? nullptr : panes.first();
        }
        if (!owner) return;
        QPointer<relay::ActionPalette> palette = m_palette;
        owner->onPaletteConversations = [palette](const QJsonObject &event) {
            if (!palette) return;
            struct Match { int rank; relay::ActionItem item; };
            QList<Match> matches;
            for (const QJsonValue &value : event.value(QStringLiteral("items")).toArray()) {
                const QJsonObject row = value.toObject();
                const QString id = row.value(QStringLiteral("session_id")).toString();
                if (id.isEmpty() || !paneForPaletteConversation(id)) continue;
                const QString best = row.value(QStringLiteral("best_match_kind")).toString();
                int rank = best == QStringLiteral("title") ? 0 : best == QStringLiteral("summary") ? 1 : 2;
                QString detail = row.value(QStringLiteral("summary")).toString();
                bool bodyDetailSet = false;
                for (const QJsonValue &matchValue : row.value(QStringLiteral("matches")).toArray()) {
                    const QJsonObject match = matchValue.toObject();
                    const QString kind = match.value(QStringLiteral("kind")).toString();
                    if (rank == 2 && !bodyDetailSet
                        && kind != QStringLiteral("title") && kind != QStringLiteral("summary")) {
                        detail = match.value(QStringLiteral("line")).toString();
                        bodyDetailSet = true;
                    } else if (detail.isEmpty()) detail = match.value(QStringLiteral("line")).toString();
                }
                relay::ActionItem item;
                item.key = QStringLiteral("conversation:") + id;
                item.section = QStringLiteral("Conversations");
                item.label = row.value(QStringLiteral("title")).toString();
                item.detail = detail.simplified().left(180);
                item.run = [id] {
                    if (Pane *pane = paneForPaletteConversation(id))
                        if (auto *window = dynamic_cast<RelayWindow *>(pane->window())) window->revealPane(pane);
                };
                matches.append({rank, item});
            }
            std::stable_sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
                return a.rank < b.rank;
            });
            QList<relay::ActionItem> items;
            for (const Match &match : std::as_const(matches).mid(0, 12)) items.append(match.item);
            palette->setConversationResults(event.value(QStringLiteral("query")).toString(), items);
        };
        owner->searchPaletteConversations(query, openSessionIds());
    }

    void togglePalette() {
        if (!m_palette)
            m_palette = new relay::ActionPalette(this, [this] { return searchableActions(); },
                                                 [this] { return paletteForThisPane(); },
                                                 [] { return QSettings().value(QStringLiteral("palette/recent")).toStringList(); },
                                                 [this](const QString &key) { rememberPaletteChoice(key); });
        m_palette->setConversationSearch([this](const QString &query) { searchPaletteConversations(query); });
        m_palette->setEditShortcut([this](const QString &key) { editShortcutOf(key); });
        QList<QKeySequence> chords;
        for (const QString &id : {QStringLiteral("palette.open"), QStringLiteral("help.shortcuts")})
            for (const QString &keys : Keymap::instance().keysFor(id))
                chords << QKeySequence::fromString(keys, QKeySequence::PortableText);
        m_palette->setToggleKeys(chords);
        m_palette->toggle();
    }

    // "Change shortcut…" on a palette row (#MAGP): a small dialog that takes the new keys. Press a
    // combination and Save writes it to keybindings.json for this action (Keymap::setBinding),
    // taking it off whatever action held it before, which the dialog names first. Remove unbinds
    // the action; Cancel or Esc leaves everything as it was. A row that is not a Keymap action
    // (an Options section, a slash command) has no shortcut of its own to change.
    void editShortcutOf(const QString &key) {
        Keymap &keymap = Keymap::instance();
        const bool registered = std::any_of(keymap.actions().cbegin(), keymap.actions().cend(),
                                            [&](const ActionDef &action) { return action.id == key; });
        if (!registered) {
            notice(QStringLiteral("That row has no shortcut of its own to change."), 6000);
            return;
        }
        const auto name = [&keymap](const QString &id) {
            return keymap.description(id).section(QLatin1Char('('), 0, 0).section(QLatin1Char(':'), 0, 0).trimmed();
        };
        const QStringList current = keymap.shortcutTexts(key);
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Change shortcut"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *intro = new QLabel(QStringLiteral("%1: %2. Press the new keys.")
                                     .arg(name(key), current.isEmpty() ? QStringLiteral("no shortcut now")
                                                                        : QStringLiteral("now %1").arg(current.join(QStringLiteral(", ")))), &dialog);
        intro->setWordWrap(true);
        auto *edit = new QKeySequenceEdit(&dialog);
        auto *clash = new QLabel(&dialog);
        clash->setWordWrap(true);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        auto *remove = buttons->addButton(QStringLiteral("Remove shortcut"), QDialogButtonBox::DestructiveRole);
        remove->setEnabled(!current.isEmpty());
        buttons->button(QDialogButtonBox::Save)->setEnabled(false);
        layout->addWidget(intro);
        layout->addWidget(edit);
        layout->addWidget(clash);
        layout->addWidget(buttons);
        QString chosen;
        // One combination, not a sequence: keep the first chord and show who holds it now.
        QObject::connect(edit, &QKeySequenceEdit::keySequenceChanged, &dialog, [&](const QKeySequence &sequence) {
            if (sequence.count() > 1) { edit->setKeySequence(QKeySequence(sequence[0])); return; }
            chosen = sequence.toString(QKeySequence::PortableText);
            const QString holder = chosen.isEmpty() ? QString() : keymap.actionForKey(chosen);
            clash->setText(holder.isEmpty() || holder == key ? QString()
                           : QStringLiteral("%1 is %2's now; saving moves it here.").arg(chosen, name(holder)));
            buttons->button(QDialogButtonBox::Save)->setEnabled(!chosen.isEmpty() && holder != key);
        });
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(remove, &QPushButton::clicked, &dialog, [&] { chosen.clear(); dialog.done(2); });
        edit->setFocus(Qt::OtherFocusReason);
        const int result = dialog.exec();
        if (result == 2) {
            keymap.setBinding(key, {});
            notice(QStringLiteral("%1 has no shortcut now.").arg(name(key)), 6000);
        } else if (result == QDialog::Accepted && !chosen.isEmpty()) {
            const QString holder = keymap.actionForKey(chosen);
            if (!holder.isEmpty() && holder != key) {
                QStringList left = keymap.keysFor(holder);
                left.erase(std::remove_if(left.begin(), left.end(), [&](const QString &text) { return Keymap::sameKey(text, chosen); }), left.end());
                keymap.setBinding(holder, left);
            }
            keymap.setBinding(key, {chosen});
            notice(QStringLiteral("%1 is %2 now.").arg(name(key), keymap.shortcutText(key)), 6000);
        }
    }

    // A palette choice: the same recent list the Actions pane keeps (the last 12 distinct keys),
    // and the hint that teaches the fast way next time.
    void rememberPaletteChoice(const QString &key) {
        if (key.isEmpty()) return;
        QStringList recent = QSettings().value(QStringLiteral("palette/recent")).toStringList();
        recent.removeAll(key); recent.prepend(key);
        QSettings().setValue(QStringLiteral("palette/recent"), QStringList(recent.mid(0, 12)));
        const QString keys = Keymap::instance().shortcutText(key);
        const QString fastPath = keys.isEmpty() ? relay::actionSlashCommands(key) : keys;
        if (fastPath.isEmpty()) return;
        const QString text = relay::ShortcutHints::nextTime(fastPath, Keymap::instance().description(key).section(QLatin1Char('('), 0, 0).trimmed().toLower());
        QTimer::singleShot(400, this, [this, key, text] { hint(QStringLiteral("palette.") + key, text); });
    }

    // What applies to the focused pane's state right now, first in the empty palette: restart
    // what stopped, stop a running turn, take the keyboard or give it back, clear a full prompt box.
    QList<PaletteItem> paletteForThisPane() {
        QList<PaletteItem> items;
        Pane *pane = m_active;
        if (!pane) return items;
        const QString here = QStringLiteral("For this pane");
        if (pane->hasStopped())
            items << actionItem(here, QStringLiteral("Restart this pane's shell or agent"),
                                QStringLiteral("It stopped · the banner's button does the same"), QStringLiteral("pane.restartShell"));
        if (pane->agentBusy())
            items << actionItem(here, QStringLiteral("Stop agent"), QStringLiteral("Cancel the running turn"), QStringLiteral("agent.stop"));
        if (const int live = pane->subagents().liveCount(); live > 0)
            items << actionItem(here, QStringLiteral("Stop all agents"), QStringLiteral("%1 running subagent(s)").arg(live),
                                QStringLiteral("agent.stopAllSubagents"));
        if (pane->isNative())
            items << actionItem(here, QStringLiteral("Back to the prompt box"), QStringLiteral("The prompt box becomes the input again"),
                                QStringLiteral("control.human"));
        else if (pane->processBusy())
            items << actionItem(here, QStringLiteral("Take control"),
                                QStringLiteral("Type into %1 directly").arg(pane->foregroundProgramName().isEmpty()
                                                                                ? QStringLiteral("the running program")
                                                                                : pane->foregroundProgramName()),
                                QStringLiteral("control.human"));
        else
            items << actionItem(here, QStringLiteral("Take control of the terminal"),
                                QStringLiteral("Keys go straight to the shell until you give it back"), QStringLiteral("control.human"));
        if (!pane->composerText().trimmed().isEmpty())
            items << actionItem(here, QStringLiteral("Clear the prompt box"), QStringLiteral("One undo step: Ctrl+Z brings it back"),
                                QStringLiteral("prompt.clear"));
        return items;
    }

    // Ctrl+?: every action and its keys, which is the Actions palette (#MAGP).
    void openShortcutsTab() {
        const bool opening = !m_palette || !m_palette->isOpen();
        togglePalette();
        // "Ctrl+?" is Ctrl+Shift+/ on most keyboards, so name the key that worked (#T9ZS).
        if (opening && m_lastShortcut.first == QStringLiteral("help.shortcuts") && !m_lastShortcut.second.isEmpty())
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
        const QString fastPath = item.shortcut.isEmpty() ? relay::actionSlashCommands(item.key) : item.shortcut;
        const QString hintText = relay::ShortcutHints::nextTime(fastPath, item.label.toLower());
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
    // The helper worker's last `presets` answer per tab: Options › Models reads it when no pane's
    // agent is up (owner, 2026-09-20: "if there is no agent loaded yet, load the helper agent").
    // The `tier_list_defaults` that arrive with it are not kept here any more — "fill from
    // defaults" is a button of the Ctrl+Alt+M dialog now, and only a pane holds what its own
    // worker computed (card #MDL1 t:a10, design 5.5).
    QHash<QString, QJsonArray> m_helperPresets;

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
    // `inModelsPane` is the same section drawn as the models pane's **providers** tab (card #MDL1
    // t:a11, design 5.8: "one renderer, two hosts"). The only difference is the row at the top
    // that opens that pane, which inside it would be a door to where you already are.
    relay::SettingsSection modelsSection(bool inModelsPane = false);
    // The lists changed — an edit on Options › Models, a profile switched there or by `/profile`:
    // the page redraws and every pane re-reads them, its box and picker first, then the worker's
    // failover chain (rank 2 reaches it now). One exit for every way the curation moves.
    //
    // Coalesced (card #HJ1T): the edit itself is already in QSettings when this is called, and the
    // picker has already repainted the row, so only the fan-out waits. Run inline it redrew every
    // settings page and rebuilt every pane's catalog and worker tiers *inside the click*, once per
    // tick — owner, 2026-09-23: "the checkboxes are not responsive and they are laggy". Five ticks
    // in a row now cost one fan-out, a moment after the last.
    void modelsCurated() {
        m_curatedSource.clear();
        if (!m_curatedTimer) {
            m_curatedTimer = new QTimer(this);
            m_curatedTimer->setSingleShot(true);
            m_curatedTimer->setInterval(200);
            connect(m_curatedTimer, &QTimer::timeout, this, [this] { modelsCuratedNow(); });
        }
        m_curatedTimer->start();
    }
    void modelsCuratedNow() {
        if (m_curatedTimer) m_curatedTimer->stop();
        if (m_curatedSource)
            m_curatedSource->setProperty("skipNextModelsCurationRefresh", true);
        m_curatedSource.clear();
        refreshSettingsPanes();
        for (Pane *each : allPanes()) {
            each->modelsCurationChanged();
            each->agentOptionsChanged(QStringLiteral("models/fallback"));
        }
    }
    QTimer *m_curatedTimer = nullptr;
    QPointer<ToolPane> m_curatedSource;
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

    QList<relay::SettingsSection> settingsSections();

    QList<PaletteItem> rootItems();

    // The registered actions the catalog deliberately has no row for, because a row already does
    // the same thing (#ACDG): the per-tab doors into Sessions & Projects are that row's children
    // (conversations.open stands for agent.resume), control.prompt is control.human's other
    // direction since that key became a toggle (#QWAS), and palette.open and help.shortcuts open
    // the palette the list is shown in. tests/test_action_catalog.py keeps this list honest.
    static const QStringList &catalogEquivalents() {
        static const QStringList ids{QStringLiteral("agent.resume"), QStringLiteral("projects.open"),
                                     QStringLiteral("globals.open"), QStringLiteral("control.prompt"),
                                     QStringLiteral("palette.open"), QStringLiteral("help.shortcuts")};
        return ids;
    }

    // A plain row for every registered action no row in `items` has the key of, walking submenus
    // too, so an action added to the Keymap tomorrow is in the palette the day it is added. Its
    // label is the registry's description, a closing "(…)" becoming the detail line.
    void appendRegisteredActions(QList<PaletteItem> &items) {
        QSet<QString> present;
        std::function<void(const QList<PaletteItem> &)> walk = [&](const QList<PaletteItem> &list) {
            for (const PaletteItem &item : list) {
                present.insert(item.key);
                if (item.children) walk(item.children());
            }
        };
        walk(items);
        for (const ActionDef &action : Keymap::instance().actions()) {
            if (present.contains(action.id) || catalogEquivalents().contains(action.id)) continue;
            QString label = action.description, detail;
            if (const qsizetype open = label.indexOf(QStringLiteral(" (")); open > 0 && label.endsWith(QLatin1Char(')'))) {
                detail = label.mid(open + 2, label.size() - open - 3);
                label.truncate(open);
            }
            const QString section = action.category == QStringLiteral("agent") ? QStringLiteral("Agent")
                : action.category == QStringLiteral("terminal") ? QStringLiteral("Terminal")
                : action.category == QStringLiteral("palette") ? QStringLiteral("Shortcuts")
                : action.category == QStringLiteral("window") ? QStringLiteral("Relay")
                : QStringLiteral("Panes and tabs");
            items << actionItem(section, label, detail, action.id);
            present.insert(action.id);
        }
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
        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("sshHostPicker"));
        dialog.setWindowTitle(QStringLiteral("Connect to SSH"));
        dialog.resize(600, 420);
        auto *layout = new QVBoxLayout(&dialog);
        auto *filter = new QLineEdit(&dialog);
        filter->setPlaceholderText(QStringLiteral("Search saved hosts or enter user@host"));
        layout->addWidget(filter);
        auto *list = new QListWidget(&dialog);
        layout->addWidget(list, 1);
        auto *empty = new QLabel(QStringLiteral("No saved hosts. Enter a host above to connect."), &dialog);
        empty->setWordWrap(true);
        layout->addWidget(empty);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        auto *connectButton = buttons->button(QDialogButtonBox::Ok);
        connectButton->setText(QStringLiteral("Connect"));
        layout->addWidget(buttons);
        const auto hosts = sshMenuItems();
        auto refresh = [=](const QString &query) {
            list->clear();
            bool exact = false;
            for (const auto &host : hosts) {
                if (!host.run) continue;  // the empty-list placeholder is not a host
                const QString target = host.key.mid(4);
                if (target.compare(query.trimmed(), Qt::CaseInsensitive) == 0) exact = true;
                if (!query.isEmpty() && !host.label.contains(query, Qt::CaseInsensitive)
                    && !host.detail.contains(query, Qt::CaseInsensitive)) continue;
                auto *row = new QListWidgetItem(host.label + (host.detail.isEmpty() ? QString() : QStringLiteral("  ·  ") + host.detail), list);
                row->setData(Qt::UserRole, target);
                row->setToolTip(host.detail);
            }
            const QString target = relay::ssh::typedTarget(QStringLiteral("ssh ") + query.trimmed());
            if (!target.isEmpty() && !exact) {
                auto *row = new QListWidgetItem(QStringLiteral("Connect to %1").arg(target), list);
                row->setData(Qt::UserRole, target);
            }
            empty->setVisible(list->count() == 0);
            empty->setText(query.isEmpty() ? QStringLiteral("No saved hosts. Enter a host above to connect.")
                                         : QStringLiteral("No matching hosts. Enter a hostname or user@host."));
            connectButton->setEnabled(list->count() > 0);
            if (list->count()) list->setCurrentRow(0);
        };
        connect(filter, &QLineEdit::textChanged, &dialog, refresh);
        connect(filter, &QLineEdit::returnPressed, connectButton, &QPushButton::click);
        connect(list, &QListWidget::itemActivated, &dialog, [&](QListWidgetItem *) { dialog.accept(); });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        refresh(QString());
        filter->setFocus();
        if (dialog.exec() == QDialog::Accepted && list->currentItem())
            connectToHost(list->currentItem()->data(Qt::UserRole).toString());
    }

    void connectToHost(const QString &target) {
        if (target.trimmed().isEmpty()) return;
        if (!addTab(paneNode(activeCwd()), m_tabs->currentIndex() + 1) || !m_active) return;
        startNewTabTheme(m_tabs->currentWidget());
        // A plain `ssh <target>` is persistent now: the pane shell's wrapper holds it in a session
        // on the host (#XQ8F), so there is nothing special for the tab to run.
        m_active->queueCommand(relay::ssh::connectCommand(target));
        relay::ssh::rememberHost(target);
    }

    // The line a split queues into the new pane so it opens where the source pane is on the host:
    // The wrapper reads RELAY_SSH_CWD as an env prefix on the command the pane runs.
    static QString hostSplitCommand(const QString &cwd, const QString &command) {
        if (!cwd.isEmpty())
            return QStringLiteral("RELAY_SSH_CWD=") + relay::ssh::shellQuote(cwd) + QLatin1Char(' ') + command;
        return command;
    }

    // Every split lands on the host when the focused pane is on one (#XQ8F): the new pane runs the
    // same ssh or mosh command line, read from the process's own argv so quoting survives, and the
    // shared connection opens it without a second login. A local pane gets the ordinary local
    // split, silently (the defaults must not scold), while `explain` — the explicit
    // ssh.splitSameHost — says why nothing happened instead of splitting here.
    void splitOnHost(relay::panes::Direction direction, bool offerPlacement = false, bool explain = false) {
        Pane *source = m_active;
        QString host;
        const QStringList argv = source ? relay::ssh::processArgv(source->foregroundPid()) : QStringList();
        const QString command = source ? relay::ssh::rerunCommand(argv, &host) : QString();
        if (command.isEmpty()) {
            if (explain) {
                if (source && !source->remoteCommandLine().isEmpty())
                    notice(QStringLiteral("Relay splits onto the same host only for an ssh or mosh login; this pane runs %1.")
                               .arg(source->remoteCommandLine().section(QLatin1Char(' '), 0, 0)));
                else
                    notice(QStringLiteral("This pane is not in an ssh or mosh session."));
                return;
            }
            splitToward(direction, offerPlacement);
            return;
        }
        if (m_activeLeaf != source) setActiveLeaf(source);
        splitToward(direction, offerPlacement);
        // OSC 7 tracks the live remote cwd, including a `cd` after this session began.
        const QString cwd = source->remoteCwd().isEmpty() ? relay::ssh::holderCwd(argv) : source->remoteCwd();
        if (m_active && m_active != source) m_active->queueCommand(hostSplitCommand(cwd, command));
        if (!host.isEmpty()) relay::ssh::rememberHost(host);
    }

    // The live login behind a pane: the host its own command line names, and the shared
    // connection to it. Pane keeps its resolved login context private (loginContext()), but it
    // announces every live login to relay::remote (announceLoginFiles, for the remote file panes),
    // and the registry checks the control socket still answers — which is what "reachable" means
    // here. The hostname the pane resolved is not published, so ssh resolves the alias once more;
    // with the socket in hand that is no network round trip.
    struct HostLogin {
        QString host, controlPath;
        bool reachable = false;
    };
    static HostLogin hostLoginOf(const Pane *pane) {
        HostLogin login;
        if (!pane) return login;
        QString host;
        relay::ssh::rerunCommand(relay::ssh::processArgv(pane->foregroundPid()), &host);
        if (host.isEmpty()) host = relay::panestatus::remoteHost(pane->remoteCommandLine());
        if (host.isEmpty()) return login;
        login.host = host;
        login.controlPath = relay::remote::loginControlPath(host);
        login.reachable = !login.controlPath.isEmpty();
        return login;
    }

    // The argv that runs one command on `login`'s host over the shared connection, the same shape
    // the keepalive and the remote file panes use (docs/SSH-AND-MOSH.md §7): no master of its own,
    // no ProxyCommand from ~/.ssh/config, no password prompt to hang on.
    static QStringList hostControlArgs(const HostLogin &login, const QString &command) {
        return {QStringLiteral("-S"), login.controlPath,
                QStringLiteral("-o"), QStringLiteral("ControlMaster=no"),
                QStringLiteral("-o"), QStringLiteral("ProxyCommand=none"),
                QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                login.host, command};
    }

    // pane.closeEndRemote. Closing a pane leaves its holder session running on the host by design
    // (#XQ8F); close only after the host confirms the kill, so a failed command cannot silently
    // leave a session running while telling the person it ended.
    void closeEndRemote() {
        Pane *pane = m_active;
        if (!pane) return;
        const QString session = relay::ssh::holderSession(relay::ssh::processArgv(pane->foregroundPid()));
        const HostLogin login = hostLoginOf(pane);
        if (session.isEmpty()) {
            notice(QStringLiteral("This pane's session is not a persistent one; closing it as usual."));
            closePane(pane, true);
            return;
        }
        if (login.host.isEmpty()) {
            // A local holder pane (#87HB): its session lives on Relay's own tmux socket on this
            // machine, so the kill runs here - the same command, no ssh around it - and the pane
            // still closes only once the kill is confirmed.
            auto *local = new QProcess(this);
            local->setProgram(QStringLiteral("/bin/sh"));
            local->setArguments({QStringLiteral("-c"), relay::ssh::killSessionCommand(session)});
            QTimer::singleShot(10000, local, [local] { local->kill(); });
            QPointer<Pane> localGuard(pane);
            connect(local, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, local, localGuard, session](int code, QProcess::ExitStatus status) {
                        if (code == 0 && status == QProcess::NormalExit) {
                            if (localGuard) closePane(localGuard, true);
                        } else {
                            notice(QStringLiteral("Could not end %1 on this machine; its pane remains open.")
                                       .arg(session));
                        }
                        local->deleteLater();
                    });
            relay::log::info(QStringLiteral("closeEndLocal session=%1").arg(session));
            local->start();
            return;
        }
        if (!login.reachable) {
            notice(QStringLiteral("Cannot reach %1 over the shared connection; %2 is still running there.")
                       .arg(login.host, session));
            return;
        }
        auto *process = new QProcess(this);
        process->setProgram(QStringLiteral("ssh"));
        process->setArguments(hostControlArgs(login, relay::ssh::killSessionCommand(session)));
        QTimer::singleShot(10000, process, [process] { process->kill(); });
        QPointer<Pane> guard(pane);
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process, guard, session](int code, QProcess::ExitStatus status) {
                    if (code == 0 && status == QProcess::NormalExit) {
                        if (guard) closePane(guard, true);
                    } else {
                        notice(QStringLiteral("Could not end %1 on the host; its pane remains open.").arg(session));
                    }
                    process->deleteLater();
                });
        relay::log::info(QStringLiteral("closeEndRemote host=%1 session=%2").arg(login.host, session));
        process->start();
    }

    // ssh.remoteSessions. The holder sessions this pane can reach, listed and ended without a
    // terminal: on the host over the shared connection (#XQ8F), or - for a pane on this machine -
    // on Relay's own socket, the local holder of #87HB, so leftover relay-* sessions are visible
    // and killable here too. Enter (or a double-click) attaches to one in a new tab; End
    // terminates it. The same dialog serves both; only the commands differ.
    void openRemoteSessions() {
        Pane *pane = m_active;
        if (!pane) return;
        const HostLogin login = hostLoginOf(pane);
        const QString host = login.host;
        const bool local = host.isEmpty();   // no shared connection: this pane's sessions are Relay's own
        if (!local && !login.reachable) {
            notice(QStringLiteral("Cannot reach %1 over the shared connection, so its sessions "
                                  "cannot be listed.").arg(host));
            return;
        }
        const QString where = local ? QStringLiteral("this machine") : host;

        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("remoteSessionPicker"));
        dialog.setWindowTitle(local ? QStringLiteral("Local persistent sessions")
                                    : QStringLiteral("Remote sessions on %1").arg(host));
        dialog.resize(600, 380);
        auto *layout = new QVBoxLayout(&dialog);
        auto *list = new QListWidget(&dialog);
        layout->addWidget(list, 1);
        auto *empty = new QLabel(QStringLiteral("Reading the sessions on %1…").arg(where), &dialog);
        empty->setWordWrap(true);
        layout->addWidget(empty);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        auto *endButton = buttons->addButton(QStringLiteral("End session"), QDialogButtonBox::ActionRole);
        endButton->setEnabled(false);
        layout->addWidget(buttons);

        // Every list, kill and attach runs the same holder commands; locally they go to this
        // machine's own socket through sh rather than over the shared connection.
        const auto runHolderCommand = [this, local, &login](const QString &command) {
            auto *process = new QProcess(this);
            if (local) {
                process->setProgram(QStringLiteral("/bin/sh"));
                process->setArguments({QStringLiteral("-c"), command});
            } else {
                process->setProgram(QStringLiteral("ssh"));
                process->setArguments(hostControlArgs(login, command));
            }
            return process;
        };

        const auto fill = [&](const QList<relay::ssh::RemoteSession> &sessions) {
            list->clear();
            for (const relay::ssh::RemoteSession &session : sessions) {
                const QString started = session.created
                    ? QDateTime::fromSecsSinceEpoch(session.created).toString(QStringLiteral("HH:mm, d MMM")) : QString();
                const QString state = session.attached ? QStringLiteral("attached") : QStringLiteral("detached");
                auto *row = new QListWidgetItem(
                    started.isEmpty() ? QStringLiteral("%1 · %2").arg(session.name, state)
                                      : QStringLiteral("%1 · started %2 · %3").arg(session.name, started, state),
                    list);
                row->setData(Qt::UserRole, session.name);
            }
            empty->setText(sessions.isEmpty() ? QStringLiteral("No persistent sessions on %1.").arg(where) : QString());
            empty->setVisible(sessions.isEmpty());
            endButton->setEnabled(list->currentItem() != nullptr);
        };
        const auto readSessions = [&](const auto &then) {
            auto *process = runHolderCommand(relay::ssh::listSessionsCommand());
            QTimer::singleShot(10000, process, [process] { process->kill(); });
            connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
            connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &dialog,
                    [process, then](int code, QProcess::ExitStatus) {
                        then(code == 0 ? relay::ssh::parseSessionList(process->readAllStandardOutput())
                                       : QList<relay::ssh::RemoteSession>());
                    });
            process->start();
        };
        const auto endSelected = [&] {
            QListWidgetItem *row = list->currentItem();
            if (!row) return;
            const QString session = row->data(Qt::UserRole).toString();
            auto *process = runHolderCommand(relay::ssh::killSessionCommand(session));
            QTimer::singleShot(10000, process, [process] { process->kill(); });
            connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
            connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &dialog,
                    [&readSessions, &fill] { readSessions(fill); });  // the next decision is from a fresh list
            relay::log::info(QStringLiteral("remoteSessions end %1 session=%2").arg(where, session));
            process->start();
        };
        const auto reattach = [&] {
            QListWidgetItem *row = list->currentItem();
            if (!row) return;
            const QString session = row->data(Qt::UserRole).toString();
            // A local session re-attaches through the holder script itself, as a restored pane's
            // local_login line does (#87HB); a host session re-attaches through ssh (#XQ8F).
            const QString line = local
                ? QStringLiteral("/bin/sh ")
                    + relay::ssh::shellQuote(dataRoot() + QStringLiteral("/shell/remote-holder.sh"))
                    + QLatin1Char(' ') + session
                : relay::ssh::reattachCommand(host, session);
            if (addTab(paneNode(activeCwd()), m_tabs->currentIndex() + 1) && m_active) {
                startNewTabTheme(m_tabs->currentWidget());
                m_active->queueCommand(line);
            }
            dialog.accept();
        };
        connect(list, &QListWidget::itemActivated, &dialog, [&](QListWidgetItem *) { reattach(); });
        connect(list, &QListWidget::itemSelectionChanged, &dialog,
                [&] { endButton->setEnabled(list->currentItem() != nullptr); });
        connect(endButton, &QPushButton::clicked, &dialog, endSelected);
        auto *del = new QAction(&dialog);
        del->setShortcut(QKeySequence(Qt::Key_Delete));
        connect(del, &QAction::triggered, &dialog, endSelected);
        dialog.addAction(del);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        readSessions(fill);
        list->setFocus();
        dialog.exec();
    }

    // Shortcut hint for the slow way to a new tab on a host: a new tab, then an ssh typed by
    // hand. refreshPaneStatus() sees the session start. The split twin is retired (#XQ8F): a split
    // lands on the host by itself now, so there is no slow way left to teach.
    static constexpr qint64 kSshNewTabHintMs = 12000;
    static void markForSshHint(Pane *pane) {
        if (!pane) return;
        pane->setProperty("relaySshHintAt", QDateTime::currentMSecsSinceEpoch());
        pane->setProperty("relaySshHintHost", QVariant());
    }

    void sshSessionSeen(Pane *pane) {
        const qint64 at = pane->property("relaySshHintAt").toLongLong();
        if (!at) return;
        pane->setProperty("relaySshHintAt", QVariant());
        pane->setProperty("relaySshHintHost", QVariant());
        const qint64 age = QDateTime::currentMSecsSinceEpoch() - at;
        if (age > kSshNewTabHintMs) return;
        // Connect has no default key: then the faster path is the Actions list itself, which also
        // offers the hosts of ~/.ssh/config and the ones used lately (card #S5SH).
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("ssh.connect"));
        hint(QStringLiteral("ssh.connect.typed"),
             keys.isEmpty() ? QStringLiteral("Next time: Actions › Connect to SSH… lists your saved and recent hosts")
                            : relay::ShortcutHints::nextTime(keys, QStringLiteral("connect to a host in a new tab")));
    }

    // Sessions & Projects' children (#SPSG): each opens the one pane on that tab.
    QList<PaletteItem> sessionsMenuItems() {
        const QString section = QStringLiteral("Sessions & Projects");
        QList<PaletteItem> items;
        items << actionItem(section, QStringLiteral("Sessions"),
                            QStringLiteral("Find and resume a session: every conversation and Relay's terminal history, "
                                           "searchable, with subagent threads · /resume"),
                            QStringLiteral("conversations.open"));
        items << actionItem(section, QStringLiteral("Projects"),
                            QStringLiteral("Known projects, active sessions and project actions"), QStringLiteral("projects.open"));
        items << actionItem(section, QStringLiteral("Globals"),
                            QStringLiteral("Board HQ: global memories, aliases and instructions"), QStringLiteral("globals.open"));
        return items;
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

    // "← main agent" and Esc in the subagent pane.
    void backToMainAgent(Pane *owner) {
        if (!owner) return;
        if (QWidget *page = pageOf(owner)) m_tabs->setCurrentWidget(page);
        setActiveLeaf(owner);
        owner->focusInput();
    }

    // agent.subagentPane (Alt+A): close the active subagent pane and return to its owner;
    // anywhere else, open this pane's subagent pane.
    void toggleSubagentPane() {
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data()); tool && tool->subagent()) {
            const auto backToMain = tool->subagent()->onBackToMain;
            tool->setProperty("closedBySubagentShortcut", true);
            closeToolPane(tool);
            if (backToMain) backToMain();
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
    QJsonObject drive(const QJsonObject &request) {
        const QString op = request.value(QStringLiteral("op")).toString();
        if (op == QLatin1String("action") || op == QLatin1String("panes")) {
            QJsonObject command = request;
            command.insert(QStringLiteral("command"), op == QLatin1String("panes")
                           ? QStringLiteral("list_panes") : QStringLiteral("run_action"));
            command.insert(QStringLiteral("action"), request.value(QStringLiteral("name")));
            return executeAppCommand(command, QStringLiteral("named driver"));
        }
        if (op == QLatin1String("workspace")) return driveWorkspace(request);   // #E85D
        if (op == QLatin1String("open")) {
            QJsonObject command{{"command", "open"}, {"target", "switchboard"}, {"card", request.value("card")}};
            return executeAppCommand(command, QStringLiteral("named driver"));
        }
        // Menu actions are explicitly named as well, and only run while their menu is open.
        if (op == QLatin1String("press") && request.value("name").toString().startsWith("profileTarget:")) {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (menu) for (QAction *action : menu->actions())
                if (action->objectName() == request.value("name").toString() && action->isEnabled()) {
                    action->trigger(); menu->close(); return {{"ok", true}};
                }
            return {{"ok", false}, {"error", "control_not_visible"}};
        }
        ToolPane *tool = nullptr;
        for (QWidget *leaf : leavesIn(m_tabs->currentWidget()))
            if (auto *candidate = dynamic_cast<ToolPane *>(leaf); candidate && candidate->board()) {
                if (tool) return {{"ok", false}, {"error", "ambiguous_board"}};
                tool = candidate;
            }
        if (!tool || !tool->board()) return {{"ok", false}, {"error", "board_not_open"}};
        return tool->board()->drive(request);
    }
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
            dockBeside(ownerPane, tool);
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
        dockBeside(owner, tool);
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
            dockBeside(owner, tool);
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

    static ToolPane *reviewPaneIn(QWidget *page) {
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == ToolPane::Kind::Review)
                return tool;
        return nullptr;
    }
    static relay::ReviewPane *reviewViewOf(ToolPane *tool) {
        return tool ? dynamic_cast<relay::ReviewPane *>(tool->hosted()) : nullptr;
    }
    void openReviewPane() {
        QWidget *page = m_tabs->currentWidget();
        if (!page) return;
        if (ToolPane *open = reviewPaneIn(page)) {
            if (auto *view = reviewViewOf(open)) view->requestList();
            setActiveLeaf(open);
            focusLeaf(open);
            updateTitles();
            return;
        }
        QWidget *anchor = nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) { anchor = tool; break; }
        if (!anchor) anchor = m_activeLeaf ? m_activeLeaf.data() : static_cast<QWidget *>(m_active.data());
        ToolPane *tool = createReviewPane(boardWorkspaceOfTab(page));
        if (anchor) dockBeside(anchor, tool);
        else if (page->layout()) page->layout()->addWidget(tool);
        linkReviewPane(tool);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }
    ToolPane *createReviewPane(const QString &cwd) {
        auto *view = new relay::ReviewPane;
        auto *tool = new ToolPane(ToolPane::Kind::Review, view, view, cwd);
        tool->setProperty("paneType", QStringLiteral("review"));
        relay::theme::polishWindow(tool);
        tool->setObjectName(QStringLiteral("pane"));
        return tool;
    }
    void linkReviewPane(ToolPane *tool) {
        relay::ReviewPane *view = reviewViewOf(tool);
        QWidget *page = view ? pageOf(tool) : nullptr;
        if (!page) return;
        QPointer<ToolPane> guard(tool);
        view->onSend = [guard](const QJsonObject &request) {
            if (auto *w = windowOf(guard))
                if (QWidget *page = w->pageOf(guard)) w->sendToHelper(page, request);
        };
        QPointer<relay::ReviewPane> viewGuard(view);
        listenToHelper(page, view, [viewGuard](const QJsonObject &event) {
            if (!viewGuard) return;
            if (event.value(QStringLiteral("event")).toString() == QStringLiteral("ready"))
                viewGuard->requestList();
            else viewGuard->handleEvent(event);
        });
        view->onOpenCard = [guard](const QString &id) {
            if (auto *w = windowOf(guard)) w->openBoardCard(id);
        };
        view->requestList();
    }
    void linkRestoredReviewPane(ToolPane *tool) {
        if (!tool) return;
        if (!pageOf(tool)) { closePane(tool, false); return; }
        linkReviewPane(tool);
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
        if (anchor) dockBeside(anchor, tool);
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
        // The docked agent (card #3B1B), wired as Options' and Models' are: the pane owns the
        // collapsed "Agent (Alt+Q)" row and asks for a console on the first expand. Here rather
        // than in linkTestSuitesPane so a restored pane has it too.
        wireConsoleHost(view->agentDock(), tool, QStringLiteral("tests.ask"));
        QPointer<ToolPane> guard(tool);
        view->onShortcutHint = [guard](const QString &hintId, const QString &keys, const QString &what) {
            if (auto *w = windowOf(guard)) w->hint(hintId, relay::ShortcutHints::nextTime(keys, what));
        };
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
        if (anchor) dockBeside(anchor, tool);
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
            notice(QStringLiteral("Open this tab's Board first: the card picker is its list of cards."), 9000);
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
            notice(QStringLiteral("Open this tab's Board first: the card picker is its list of cards."), 9000);
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
        dockBeside(owner, tool);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
        return view;
    }
    // ----- the session manager pane and the ⓘ pane (cards #R6J0, #Y63Z) ------------------------
    // One session manager per tab, bound to the pane that opened it: its queries go to that pane's
    // worker and Enter resumes there. /resume, /conversations, Ctrl+Shift+S (sessions.open), agent.resume and
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

    Pane *workspaceOwner(ToolPane *tool) const {
        if (!tool) return nullptr;
        const auto panes = panesIn(pageOf(tool));
        auto *owner = dynamic_cast<Pane *>(tool->property("workspaceOwner").value<QObject *>());
        return panes.contains(owner) ? owner : (panes.isEmpty() ? nullptr : panes.first());
    }

    // Open this tab's session manager on `tab` ("" is the session list), bound to the active pane.
    void openSessions(const QString &tab = QString(), const QString &query = QString()) {
        Pane *owner = m_active;
        if (!owner || !owner->hasShell() || !panesIn(m_tabs->currentWidget()).contains(owner)) {
            ToolPane *existing = sessionsPaneIn(m_tabs->currentWidget());
            owner = existing ? dynamic_cast<Pane *>(existing->property("workspaceOwner").value<QObject *>()) : nullptr;
            if (!owner || !panesIn(m_tabs->currentWidget()).contains(owner)) {
                const auto panes = panesIn(m_tabs->currentWidget());
                owner = panes.isEmpty() ? nullptr : panes.first();
            }
        }
        if (!owner) {
            ToolPane *existing = sessionsPaneIn(m_tabs->currentWidget());
            if (!existing) return;
            auto *view = sessionsViewOf(existing);
            if (tab == QStringLiteral("projects") || tab == QStringLiteral("globals")
                || tab == QStringLiteral("closed")) {
                view->showTab(tab);
                if (view->onTabActivated) view->onTabActivated(view->currentTab());
                setActiveLeaf(existing); focusLeaf(existing); updateTitles();
                return;
            }
            // Sessions needs a destination for Resume. Restore one only when Sessions is asked
            // for; browsing Projects/Globals alone never starts an otherwise unwanted terminal.
            owner = createPane(paneNode(existing->cwd()));
            insertBeside(existing, owner, Qt::Horizontal, true);
            spreadAfterAdding(owner);
        }
        openSessionsFor(owner, query, tab);
    }

    // Sessions & Projects is one pane with direct keys for its Sessions, Projects and Globals tabs.
    // Each key is a toggle: it opens its tab, and pressing it again with that tab focused
    // focused closes it, the way Esc does (owner, 2026-09-20). Pressed while the focus is elsewhere
    // it brings the open pane forward and rebinds it to the pane that asked, rather than closing a
    // pane the user is not looking at. The per-tab ids (`projects.open`, `globals.open`,
    // `agent.resume`) toggle the same way on their own tab, and switch tabs when the pane is focused
    // on another. `/resume` and `/conversations` are openers, not toggles: they are typed in the
    // prompt box of a pane, which is never the manager.
    void toggleSessionsPane(Pane *owner, const QString &tab = QString()) {
        ToolPane *tool = sessionsPaneIn(m_tabs->currentWidget());
        const QString current = tool ? sessionsViewOf(tool)->currentTab() : QString();
        if (tool && (tab.isEmpty() || current == tab)
            && (m_activeLeaf == tool || tool->isAncestorOf(QApplication::focusWidget()))) {
            closeSessionsPane(tool, owner);
            return;
        }
        openSessions(!tab.isEmpty() ? tab : tool ? current : m_sessionsTab);
    }
    QString m_sessionsTab = QStringLiteral("sessions");   // the tab Sessions & Projects was last on

    // Refresh the project browser from the one registry and live panes, without changing scope.
    void feedProjects(relay::projects::ProjectsPane *projects,
                      relay::conversations::SessionManager *sessions) {
        if (!projects || !sessions) return;
        const auto known = m_manager->projects().knownProjects();
        projects->setProjects(known);
        projects->setDeclined(m_manager->projects().declined());
        QList<QPair<QString, QString>> filter;
        for (const auto &record : known)
            filter.append({record.name.isEmpty() ? relay::projects::nameFor(record.path) : record.name, record.path});
        sessions->setKnownProjects(filter);
        QJsonArray active;
        for (QWidget *top : QApplication::topLevelWidgets()) {
            auto *window = dynamic_cast<RelayWindow *>(top);
            if (!window) continue;
            for (Pane *pane : window->allPanes()) {
                const QString project = window->tabProject(window->pageOf(pane));
                active.append(QJsonObject{{"session_id", pane->sessionId()}, {"session_dir", pane->sessionDir()},
                    {"pane_token", pane->sessionToken()}, {"workspace", pane->workspace()},
                    {"project_path", project},
                    {"title", pane->paneTitle().isEmpty() ? shortPath(pane->cwd()) : pane->paneTitle()},
                    {"status", pane->dimmingAgentBusy() ? QStringLiteral("Working") : QStringLiteral("Open")}});
            }
        }
        projects->setActiveSessions(active);
    }

    void openProjectTab(const QString &path, bool board = false) {
        if (!QFileInfo(path).isDir()) { notice(QStringLiteral("That project folder is no longer available.")); return; }
        if (!addTab(paneNode(path), m_tabs->currentIndex() + 1)) return;
        QWidget *page = m_tabs->currentWidget();
        attachTab(page, path, QString::fromLatin1(relay::projects::kReasonPicker));
        if (board && m_active) afterProjectPicked(m_active, QString());
    }

    // The Sessions & Projects pane itself, with no owner pane yet: the SessionManager, its
    // Projects and Globals tabs and every callback that does not need an owner — they all resolve
    // it dynamically through workspaceOwner(guard) at the moment they run, the way Options/Actions
    // and Models already do. `linkSessionsPane` does the rest once an owner is known: a fresh pane
    // bound on the spot by `openSessionsFor`, or a saved layout finding a neighbour to serve
    // (card #8EXS, `linkRestoredSessionsPane`).
    ToolPane *createSessionsPane(const QString &cwd);

    // Bind `tool` to `owner`: its requests go to that pane's worker, Resume resolves its
    // destination through it, and the ⓘ info button opens beside it. Called once for a fresh pane
    // (`openSessionsFor`) and once for a saved one finding a neighbour to serve (card #8EXS,
    // `linkRestoredSessionsPane`) — the parts that need an owner, split out of what used to be one
    // inline block in `openSessionsFor`.
    void linkSessionsPane(ToolPane *tool, Pane *owner) {
        relay::conversations::SessionManager *view = sessionsViewOf(tool);
        if (!view || !owner) return;
        tool->setProperty("workspaceOwner", QVariant::fromValue<QObject *>(owner));
        QPointer<ToolPane> ownerTool(tool);
        connect(owner, &QObject::destroyed, tool, [ownerTool, owner] {
            if (ownerTool && ownerTool->property("workspaceOwner").value<QObject *>() == owner)
                ownerTool->setProperty("workspaceOwner", QVariant());
        });
        // The label names the project the scope actually narrows to, so it follows the live
        // terminal directory first (card #7QSK) — `workspace()` is the frozen launch dir.
        view->setProject(QFileInfo(owner->cwd().isEmpty() ? owner->workspace() : owner->cwd()).fileName());
        // (#G2C7) The #card codes' claimed-or-was-claimed state is read from the board this
        // project runs on, so the pane knows whose chips to bold.
        view->setBoardRoot(relay::boardRootFor({owner->workspace(), owner->cwd()}));
        // Bind requests to the initiating pane; Resume resolves its destination below.
        owner->bindSessionManager(view);
        QPointer<ToolPane> guard(tool);
        QPointer<Pane> ownerGuard(owner);
        auto resume = view->onResume;
        auto close = view->onClose;
        // Close first: a resume may focus another pane (the one that already has the session),
        // and closing afterwards would take the focus back. Shift+Enter keeps this list open and
        // returns focus to the control the user was using, so they can open another row (#E7FP).
        view->onResume = [resume, close, ownerGuard, guard](const QJsonObject &item, bool, bool keepOpen) {
            QPointer<QWidget> focused = keepOpen ? QApplication::focusWidget() : nullptr;
            auto restoreListFocus = [guard, focused, keepOpen] {
                if (!keepOpen || !guard) return;
                // openFork queues focusInput() for the new pane. Queue this after resume so that
                // its focus change runs first; the same path also handles an existing pane.
                QTimer::singleShot(0, guard, [guard, focused] {
                    auto *w = windowOf(guard);
                    if (!w) return;
                    if (QWidget *page = w->pageOf(guard)) w->m_tabs->setCurrentWidget(page);
                    w->setActiveLeaf(guard);
                    w->raise();
                    w->activateWindow();
                    if (focused && (focused == guard || guard->isAncestorOf(focused)))
                        focused->setFocus(Qt::OtherFocusReason);
                    else focusLeaf(guard);
                });
            };
            if (!keepOpen && close) close();
            const QString id = item.value(QStringLiteral("session_id")).toString();
            if (id.isEmpty()) return;
            Pane *existing = nullptr;
            if (relay::conversations::isGuestItem(item)) {
                const QString source = item.value(QStringLiteral("source")).toString();
                for (QWidget *top : QApplication::topLevelWidgets())
                    if (auto *w = dynamic_cast<RelayWindow *>(top))
                        for (Pane *pane : w->allPanes())
                            if (pane->sessionTextSource() == source && pane->guestSessionId() == id)
                                existing = pane;
            } else {
                QString dir = item.value(QStringLiteral("session_dir")).toString();
                if (dir.isEmpty() && ownerGuard) dir = ownerGuard->sessionDir();
                existing = paneWithSession(id, dir, nullptr);
            }
            if (existing) {
                if (auto *w = windowOf(existing)) w->revealPane(existing);
                restoreListFocus();
                return;
            }
            if (resume) resume(item, true, keepOpen);
            restoreListFocus();
        };
        view->onOpenInfo = [ownerGuard, guard](const QJsonObject &item) {
            auto *w = windowOf(guard);
            if (!w || !ownerGuard) return;
            w->openInfoPane(ownerGuard, item.value(QStringLiteral("session_id")).toString(),
                            item.value(QStringLiteral("session_dir")).toString());
        };
    }

    // The tail of opening Sessions & Projects, shared by a fresh open (`openSessionsFor`) and a
    // restored one (`linkRestoredSessionsPane`): refresh Projects, show the tab that was asked
    // for, restore a search and re-run it.
    void finishSessionsTab(ToolPane *tool, const QString &tab, const QString &query) {
        relay::conversations::SessionManager *view = sessionsViewOf(tool);
        if (!view) return;
        if (auto *projects = dynamic_cast<relay::projects::ProjectsPane *>(view->findChild<QWidget *>(QStringLiteral("workspaceProjects"))))
            feedProjects(projects, view);
        view->showTab(tab);
        if (view->currentTab() == QStringLiteral("globals") && view->onTabActivated)
            view->onTabActivated(QStringLiteral("globals"));
        if (!query.isEmpty()) view->setQuery(query);
        if (view->currentTab() == QStringLiteral("sessions")) view->refresh();
    }

    void openSessionsFor(Pane *owner, const QString &query, const QString &tab = QString()) {
        QWidget *page = pageOf(owner);
        if (!page) return;
        ToolPane *tool = sessionsPaneIn(page);
        if (!tool) {
            tool = createSessionsPane(owner->cwd());
            dockBeside(owner, tool);
        }
        linkSessionsPane(tool, owner);
        finishSessionsTab(tool, tab, query);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }

    // A saved Sessions & Projects pane (buildNode, card #8EXS) comes back beside the first
    // terminal pane of the tab it landed in — the same rule the models pane uses (`linkRestoredModelsPane`)
    // — and is dropped if there is none, the way a saved Activity pane is (`linkRestoredInternalsPane`).
    // Queued, because `buildNode()` runs before the page the pane will live in exists.
    void linkRestoredSessionsPane(ToolPane *tool, const QString &tab, const QString &query) {
        if (!tool) return;
        const QList<Pane *> panes = panesIn(pageOf(tool));
        Pane *owner = panes.isEmpty() ? nullptr : panes.first();
        if (!owner) { closePane(tool, false); return; }
        linkSessionsPane(tool, owner);
        finishSessionsTab(tool, tab, query);
    }

    // Globals › Suggestions, on one suggestion when `id` is set (#MEMS): a transcript's Edit, and
    // the Review button on the "memories from Claude Code to review" notification.
    void openMemorySuggestion(const QString &id) {
        openSessions(QStringLiteral("globals"));
        ToolPane *tool = sessionsPaneIn(m_tabs->currentWidget());
        auto *view = tool ? sessionsViewOf(tool) : nullptr;
        if (auto *globals = view ? dynamic_cast<relay::globals::GlobalsPane *>(view->findChild<QWidget *>(QStringLiteral("workspaceGlobals"))) : nullptr)
            globals->showSuggestion(id);
    }
    // `/skills [name]`, the palette's Skills… and Options' Skills row (#JVEJ, which retired the
    // Skills dialog): Globals › Skills, with a global skill's page open when one is named; a
    // project skill (`project`) opens on this tab's Board, Skills tab, instead — decision 2 on
    // #9FX8 puts a workspace's own skills there and nowhere else.
    void openSkills(const QString &name, bool project) {
        if (project) {
            auto boardInTab = [this]() -> ToolPane * {
                for (QWidget *leaf : leavesIn(m_tabs->currentWidget()))
                    if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board() && !tool->board()->pinned())
                        return tool;
                return nullptr;
            };
            if (!boardInTab()) toggleBoardPane();
            if (ToolPane *tool = boardInTab()) {
                tool->board()->showSkill(name);
                setActiveLeaf(tool);
                focusLeaf(tool);
                return;
            }
        }
        openSessions(QStringLiteral("globals"));
        ToolPane *tool = sessionsPaneIn(m_tabs->currentWidget());
        auto *view = tool ? sessionsViewOf(tool) : nullptr;
        if (auto *globals = view ? dynamic_cast<relay::globals::GlobalsPane *>(view->findChild<QWidget *>(QStringLiteral("workspaceGlobals"))) : nullptr)
            globals->showSkill(name);
    }
    // A transcript's Keep or No: every Globals list on screen stops offering that suggestion.
    static void refreshVisibleGlobals() { relay::globals::GlobalsPane::refreshVisible(); }

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

    // Project selection lives only in the shared Projects tab. Preserve a loose /card request
    // until an explicit Attach or Initialize action supplies its destination.
    void openProjectsFor(Pane *owner, const QString &heldCard) {
        if (owner) openSessionsFor(owner, QString(), QStringLiteral("projects"));
        else openSessions(QStringLiteral("projects"));
        if (auto *tool = sessionsPaneIn(m_tabs->currentWidget()); tool && !heldCard.isEmpty()) {
            tool->setProperty("pendingProjectCard", heldCard);
            notice(QStringLiteral("Choose a project and Attach this tab to file your card, or Initialize here."));
        }
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
            dockBeside(owner, tool);
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
            dockBeside(owner, tool);
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

    // ---- the Sharing pane (#W5N2, #SMDX): bodies in src/RelayWindowSharing.cpp -----------------
    // Open (or find) the Sharing pane beside `owner`. `focus` is false when a knock opened it by
    // itself: the pane appears and the bell rings, but the keyboard stays where the owner left it.
    ToolPane *openSharingPane(Pane *owner, bool focus);
    // The same, as the view, for the callers that then ask it for a page or a form.
    relay::sharing::SharingView *sharingViewFor(Pane *owner, bool focus);
    // The pane a Sharing pane was last opened from, or null once that pane has closed.
    Pane *sharingOwnerOf(ToolPane *tool) const;
    // The share chip's two rows: the People page with the invite form on this pane, or with the
    // scope picker open.
    void shareThisPane(Pane *pane);
    void shareMore(Pane *pane);
    // What the invite form's scope picker offers: this window's panes, its tabs, everything.
    QList<relay::sharing::Scope> sharingScopes(Pane *current);
    // Publish what the scope needs, then ask the sidecar for a link or a meeting code.
    void inviteToScope(const relay::sharing::Scope &scope, const QString &role, int expires, int uses);
    void codeForScope(const relay::sharing::Scope &scope, const QString &role);
    bool publishScope(const relay::sharing::Scope &scope, QString *paneId, QString *tab);
    // Whether this window is the one that rings and opens Devices for a device's pairing ask.
    bool answersDeviceAsks() const;

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
                w->dockBeside(last->data(), tool);
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

    // /restart and a completed /update share one handoff. The detached replacement waits for this
    // exact process (pid and start time) to exit before it creates QApplication or takes the layout
    // lock. aboutToQuit writes scrollback and layout while every pane is still alive.
    bool restartApp() {
        static bool queued = false;
        if (queued) { notice(QStringLiteral("Relay is already restarting.")); return true; }
        if (m_updateProcess) { notice(QStringLiteral("Finish the update before restarting Relay.")); return false; }
        if (!WindowManager::restoreEnabled() || !m_manager->ownsLayout()) {
            notice(QStringLiteral("This window set is not being saved. Enable Reopen windows on start, then restart the owning Relay."), 8000);
            return false;
        }
        int busy = 0;
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *window = dynamic_cast<RelayWindow *>(top))
                for (Pane *pane : window->allPanes())
                    if (pane->hasCloseWork()) ++busy;
        for (Pane *pane : m_manager->backgroundPanes())
            if (pane && pane->hasCloseWork()) ++busy;
        if (busy && QMessageBox::question(this, QStringLiteral("Restart Relay?"),
                QStringLiteral("%1 running program or agent turn(s) will stop. Save the workspace and restart now?").arg(busy),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return false;
        const relay::runtimedirs::Owner owner = relay::runtimedirs::self();
        if (!owner.isValid() || !owner.startTime) {
            notice(QStringLiteral("Relay could not identify this process; restart manually after quitting."), 8000);
            return false;
        }
        const QString arg = QStringLiteral("--relay-restart-after=%1:%2").arg(owner.pid).arg(owner.startTime);
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {arg}, QDir::currentPath())) {
            notice(QStringLiteral("Relay could not launch its restart helper. This window is still open."), 8000);
            return false;
        }
        queued = true;
        notice(QStringLiteral("Saving the workspace; Relay will reopen after this instance exits."), 4000);
        QTimer::singleShot(0, qApp, [] { QCoreApplication::quit(); });
        return true;
    }

    // /update and the palette's Update action: scripts/relay-update.py fetches the newest GitHub
    // release's .deb for this distribution and architecture, checks it against the release's
    // SHA256SUMS and installs it with pkexec (the password dialog is polkit's, never a prompt the
    // app could read). Every line it prints becomes the window's notice. When it finishes with the
    // UPDATED marker, Relay uses the same exit-then-launch handoff as /restart.
    void updateApp() {
#ifdef Q_OS_MACOS
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://relay-terminal.ai/#install")));
        notice(QStringLiteral("Download the macOS disk image to update Relay."));
        return;
#endif
#ifdef Q_OS_WIN
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://relay-terminal.ai/#install")));
        notice(QStringLiteral("Download the Windows installer to update Relay."));
        return;
#endif
        if (m_updateProcess) { notice(QStringLiteral("An update is already running.")); return; }
        const QString python = relayPython();
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
            if (!restartApp()) notice(QStringLiteral("Update installed. Restart Relay manually when ready."), 8000);
        });
        process->start(python, {QStringLiteral("-X"), QStringLiteral("utf8"), script, QStringLiteral("install"),
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
            self->dockBeside(self->m_activeLeaf, tool);
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
    // checkout says so ("This tab's Board is A; … belongs to B") instead of re-pointing.
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

    // ----- Switchboard (docs/BOARD-DESIGN.md 4, protocol 17) -----------------------------
    // Ctrl+Shift+A: open the Switchboard beside the anchor, focus the one this tab already has,
    // or, pressed on it, go back to the last terminal pane.
    void toggleBoardPane(bool background = false) {
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
        QString workspace = boardWorkspace();
        const QString from = boardSearchRoot();
        // A tab holds one project's board (owner's rule: one project per tab), so an existing one
        // is what this key shows — but never silently as if it were the active pane's. When the
        // two disagree, say whose board is on screen: dropping a card on it writes into that
        // project's board folder, not the one the pane is standing in.
        // A solo card pane (#Y2BA) is one card, not the tab's Board, so it is not what this finds.
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board() && !tool->board()->pinned()) {
                if (background) tool->board()->showBackgroundPage();
                setActiveLeaf(tool); focusLeaf(tool);
                const QString shown = tool->board()->workspace();
                if (!from.isEmpty() && shown != workspace) {
                    statusBar()->showMessage(
                        workspace.isEmpty()
                            ? QStringLiteral("This tab's Board is %1; %2 has no Board of its own.")
                                  .arg(shown, from)
                            : QStringLiteral("This tab's Board is %1; %2 belongs to %3.")
                                  .arg(shown, from, workspace),
                        9000);
                }
                return;
            }
        if (workspace.isEmpty()) {
            // Opening Switchboard is an explicit project action. An empty directory gets its
            // empty board view; opening it does not initialize a repository or write board files.
            workspace = candidateProject();
            if (workspace.isEmpty()) workspace = from;
            if (workspace.isEmpty()) {
                notice(QStringLiteral("Open a terminal in the directory whose Board you want."));
                return;
            }
        }
        QWidget *anchor = m_activeLeaf ? m_activeLeaf.data() : static_cast<QWidget *>(m_active.data());
        auto *tool = createBoardPane(workspace);
        if (!tool) return;
        if (background) tool->board()->showBackgroundPage();
        if (anchor) dockBeside(anchor, tool);
        else if (page && page->layout()) page->layout()->addWidget(tool);
        // Opening the Switchboard is the explicit project action: from here the tab is this
        // project's, its panes get the card tools, and it stays so until it is detached.
        attachTab(page, workspace, QString::fromLatin1(relay::projects::kReasonSwitchboard));
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }

    // One card in this tab's Switchboard, opening the Switchboard first if the tab has none. A
    // solo card pane already on that card (#Y2BA) is where a `#ID` link goes first; otherwise the
    // tab's list Board, never a solo pane showing some other card.
    void openBoardCard(const QString &id) {
        if (ToolPane *solo = soloCardPaneIn(m_tabs->currentWidget(), id)) {
            setActiveLeaf(solo);
            focusLeaf(solo);
            return;
        }
        auto boardInTab = [this]() -> ToolPane * {
            for (QWidget *leaf : leavesIn(m_tabs->currentWidget()))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board() && !tool->board()->pinned())
                    return tool;
            return nullptr;
        };
        if (!boardInTab()) {
            QString workspace = boardWorkspace();
            if (workspace.isEmpty()) {
                workspace = candidateProject();
                if (workspace.isEmpty()) workspace = boardSearchRoot();
            }
            if (workspace.isEmpty()) {
                notice(QStringLiteral("Open a terminal in the directory whose Board you want."));
                return;
            }
            QWidget *page = m_tabs->currentWidget();
            QWidget *anchor = m_activeLeaf ? m_activeLeaf.data() : static_cast<QWidget *>(m_active.data());
            ToolPane *created = createBoardPane(workspace, {}, {}, QString(), {}, {},
                                                QString(), ToolPane::Kind::Board, true);
            if (anchor) dockBeside(anchor, created);
            else if (page && page->layout()) page->layout()->addWidget(created);
            attachTab(page, workspace, QString::fromLatin1(relay::projects::kReasonSwitchboard));
            if (page) page->setProperty("relayDeferBoardAgent", true);
        }
        ToolPane *tool = boardInTab();
        if (!tool) return;
        tool->board()->openCardSolo(id);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
    }

    // Retries the reveal every 250 ms for up to 6 s, which covers the worker's first answer
    // on a large tree. It stops as soon as a card detail is open, so a card the *user* opened in
    // the meantime is never yanked out from under them.
    void waitForBoardCard(ToolPane *tool, const QString &id, int attempt) {
        // Out of retries: the card is not on this board (a `#ID` from another project's output,
        // or a card that has been removed). Say so rather than leave the click looking ignored.
        if (attempt >= 24) {
            statusBar()->showMessage(QStringLiteral("No card #%1 on this board.").arg(id), 9000);
            // A card pane (#Y2BA) whose card is not among rows that did arrive has nothing to
            // show: a card deleted while the window was closed. Rows that never came leave it be.
            if (tool && tool->board() && tool->board()->pinned() && tool->board()->model().total() > 0
                && !tool->board()->model().card(id))
                closeToolPane(tool);
            return;
        }
        QPointer<ToolPane> guard(tool);
        QTimer::singleShot(250, this, [this, guard, id, attempt] {
            ToolPane *pane = guard.data();
            if (!pane || !pane->board() || pane->board()->detailOpen()) return;
            if (!pane->board()->model().card(id)) { waitForBoardCard(pane, id, attempt + 1); return; }
            if (pane->board()->pinned()) pane->board()->pinSolo(id);
            else pane->board()->openCardSolo(id);
        });
    }

    // The solo card pane (#Y2BA) in `page` that is showing `id`, if there is one.
    ToolPane *soloCardPaneIn(QWidget *page, const QString &id) const {
        if (!page || id.isEmpty()) return nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf);
                tool && tool->board() && tool->board()->pinned() && tool->board()->pinnedCard() == id)
                return tool;
        return nullptr;
    }

    // A card in a Board pane of its own (#Y2BA): Shift+Enter on a row, the page's ⤴ button, and
    // a new card created while another card's page is open all come here. The pane docks beside
    // `anchor` (the pane that asked; the active leaf when none is given), shows that one card and
    // never its list, and closes on Esc. Several of them make several cards open at once in one
    // tab, each with its own docked conversation (`<tab>/card:<ID>`, keyed by the card). A pane
    // already on that card is focused rather than duplicated.
    void openBoardCardInNewPane(const QString &id, QWidget *anchor = nullptr) {
        QWidget *page = anchor ? pageOf(anchor) : m_tabs->currentWidget();
        if (!page || id.isEmpty()) return;
        if (ToolPane *solo = soloCardPaneIn(page, id)) { setActiveLeaf(solo); focusLeaf(solo); return; }
        if (!anchor) anchor = m_activeLeaf ? m_activeLeaf.data() : static_cast<QWidget *>(m_active.data());
        QString workspace;
        if (auto *tool = dynamic_cast<ToolPane *>(anchor); tool && tool->board()) workspace = tool->board()->workspace();
        if (workspace.isEmpty()) workspace = tabProject(page);
        if (workspace.isEmpty()) workspace = boardWorkspace();
        if (workspace.isEmpty()) {
            notice(QStringLiteral("Open a terminal in the directory whose Board you want."));
            return;
        }
        ToolPane *tool = createCardPane(workspace);
        if (!tool) return;
        if (anchor) dockBeside(anchor, tool);
        else if (page->layout()) page->layout()->addWidget(tool);
        if (tabProject(page).isEmpty())
            attachTab(page, workspace, QString::fromLatin1(relay::projects::kReasonSwitchboard));
        tool->board()->pinSolo(id);
        setActiveLeaf(tool);
        focusLeaf(tool);
        updateTitles();
        m_manager->scheduleSave();
        // A new pane's rows are still on their way, exactly as for a first Switchboard.
        if (!tool->board()->model().card(id)) waitForBoardCard(tool, id, 0);
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
        if (anchor) dockBeside(anchor, tool);
        else if (QWidget *page = m_tabs->currentWidget(); page && page->layout()) page->layout()->addWidget(tool);
        revealBoardCard(tool, id);
    }

    // The Switchboard pane in `w` that shows `workspace`, if it has one (any tab of it).
    static ToolPane *boardPaneFor(RelayWindow *w, const QString &workspace) {
        if (!w) return nullptr;
        for (int i = 0; i < w->m_tabs->count(); ++i)
            for (QWidget *leaf : w->leavesIn(w->m_tabs->widget(i)))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf);
                    tool && tool->board() && !tool->board()->pinned() && tool->board()->workspace() == workspace)
                    return tool;
        return nullptr;
    }

    // Open a card in a board pane on the card alone (#K4SQ), and keep asking while its rows are
    // still loading (waitForBoardCard). Used by openNotificationSource and openBoardCard's paths.
    void revealBoardCard(ToolPane *tool, const QString &id) {
        if (!tool || !tool->board()) return;
        tool->board()->openCardSolo(id);
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
    relay::BoardWorker *boardWorker(QWidget *page);

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
        // "3 memories from Claude Code to review · Review" (#MEMS): the same bell entry a pane's
        // worker posts, since only the one worker whose import found something says so.
        if (event.value(QStringLiteral("event")).toString() == QStringLiteral("memory_import")) {
            QString title, body;
            if (relay::globals::memoryImportNotice(event, &title, &body))
                relay::NotificationCenter::instance().postWithAction(title, body, relay::NotificationCenter::kindInfo,
                    QString(), QStringLiteral("Review"), QStringLiteral("memory.review"));
        }
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
        relay::log::routingDraw(start.draw, QStringLiteral("switchboard"), QStringLiteral("switchboard"));
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
        // An attached project can have no board yet; send that state explicitly so board_open
        // returns its empty view instead of attempting to discover a nonexistent board.
        configure.insert(QStringLiteral("board"), boardSettingsFor(page));
        if (page->property("relayDeferBoardAgent").toBool())
            configure.insert(QStringLiteral("defer_agent"), true);
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
        // Protocol 34 (#MEMS): the tab's worker may be the first process to start, so it imports
        // Claude Code and Codex memories too, and says how many (deliverToHelperPanels).
        configure.insert(QStringLiteral("memory_import"), settings.value(QStringLiteral("memory/import_guests"), true).toBool());
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
            return guard ? guard->remoteCardPane(tab, card, QString(), task, true) : QString();
        };
        host.verifyCard = [guard](const QString &tab, const QString &card, const QString &runner, const QString &task) {
            return guard && !runner.isEmpty() ? guard->remoteCardPane(tab, card, runner, task, false) : QString();
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
    // from a device attaches the tab exactly as Ctrl+Shift+A does (toggleBoardPane) — minus the
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
        statusBar()->showMessage(QStringLiteral("Board opened from a paired device: this tab is now %1's.")
                                     .arg(relay::projects::nameFor(workspace)), 9000);
        return tabHasBoard(current) ? tabIdOf(current) : QString();
    }

    // Run (`runner` empty) or Verify from a device. With a Switchboard pane in the tab this
    // *is* the desktop's code path: the pane's own onExecuteCard / onVerifyCard, as createBoardPane()
    // installed them. With none, the same pane is opened beside whatever the tab was last using —
    // there is no board to keep a split for — and handed the same task the same way. A Run works
    // in the background (#E728): the desktop's tab and focus stay put, and the pane moves to the
    // background window once its agent accepts the task. A Verify still takes the focus.
    QString remoteCardPane(const QString &tab, const QString &card, const QString &runner, const QString &task,
                           bool background) {
        QWidget *page = pageOfTabId(tab);
        if (!page) return {};
        if (!background) m_tabs->setCurrentWidget(page);    // a Verify takes the focus, as on the desktop
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board()) {
                relay::BoardView *view = tool->board();
                if (runner.isEmpty()) return view->onExecuteCard ? view->onExecuteCard(card, task, background) : QString();
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
        if (anchor) dockBeside(anchor, pane);
        else if (page->layout()) page->layout()->addWidget(pane);
        if (background) pane->markBackgroundTask(true);
        else { setActive(pane); focusLeaf(pane); }
        if (guest) pane->startGuestBoardTask(runnerId, task, card);
        else pane->startBoardTask(task, card);
        updateTitles();
        if (background) backgroundPaneWhenWorking(pane);
        return pane->sessionToken();
    }

    // A setting the helper workers carry changed: re-send `configure` to every live one.
    // `BoardWorker::start` on a running process is exactly that (and a no-op when nothing moved).
    void reconfigureBoardWorkers() {
        for (const QString &tab : m_boardWorkers.keys())
            if (m_boardWorkers.value(tab)) startBoardWorker(pageOfTabId(tab));
    }

    // A card pane (#Y2BA): a Board view that pinSolo will pin to one card, in a Kind::Card pane.
    ToolPane *createCardPane(const QString &workspace) {
        ToolPane *tool = createBoardPane(workspace, {}, {}, QString(), {}, {}, QString(), ToolPane::Kind::Card);
        // A drag of the card/console divider is part of the layout (#ZPHJ).
        if (relay::AgentSplit *split = tool->board()->cardSplit()) {
            QPointer<ToolPane> guard(tool);
            split->onUserMoved = [guard] { if (auto *w = windowOf(guard)) w->m_manager->scheduleSave(); };
        }
        return tool;
    }

    ToolPane *createBoardPane(const QString &workspace, const QJsonArray &collapsed = {},
                              const QJsonArray &hidden = {}, const QString &sort = QString(),
                              const QJsonArray &labels = {},
                              const QJsonArray &selfClosed = {},
                              const QString &grouping = QString(),
                              ToolPane::Kind kind = ToolPane::Kind::Board,
                              bool directCard = false) {
        auto *view = new relay::BoardView(workspace);
        if (!collapsed.isEmpty()) view->setCollapsedSections(collapsed);
        if (!hidden.isEmpty()) view->setHiddenSections(hidden);
        if (!labels.isEmpty()) view->setLabelFilter(labels);
        // Which "N closed by the agent" rows were open (#93WR); none of them is the default.
        if (!selfClosed.isEmpty()) view->setOpenSelfClosed(selfClosed);
        // The sort the pane was saved with; empty (or unknown) leaves it Manual.
        if (!sort.isEmpty()) view->setSortOrder(sort);
        // Sections or one flat list (#ESDF); empty — a new pane, or a node saved before the
        // choice existed — is the flat list the owner asked for, newest-updated first.
        view->restoreGrouping(grouping);
        auto *tool = new ToolPane(view, workspace, kind);
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
        // Solo card panes (#Y2BA). The close is queued: it can come from inside the view's own
        // event handling (Esc, or the card removed under it), and the view is deleted with the pane.
        view->onOpenInNewPane = [guard](const QString &id) {
            if (auto *w = windowOf(guard)) w->openBoardCardInNewPane(id, guard);
        };
        view->onClosePane = [guard] {
            QTimer::singleShot(0, guard, [guard] {
                if (auto *w = windowOf(guard)) w->closeToolPane(guard);
            });
        };
        view->onQuickAddElsewhere = [guard] {
            auto *w = windowOf(guard);
            if (!w) return;
            QWidget *page = w->pageOf(guard);
            ToolPane *list = nullptr;
            for (QWidget *leaf : w->leavesIn(page))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board() && !tool->board()->pinned()) {
                    list = tool;
                    break;
                }
            if (!list) {
                w->setActiveLeaf(guard);
                w->toggleBoardPane();
                list = dynamic_cast<ToolPane *>(w->m_activeLeaf.data());
            }
            if (!list || !list->board()) return;
            w->setActiveLeaf(list);
            w->focusLeaf(list);
            list->board()->quickAdd();
        };
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
            if (w)
                if (QWidget *page = w->pageOf(guard)) page->setProperty("relayDeferBoardAgent", false);
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
        view->onOpenReview = [guard] {
            if (auto *w = windowOf(guard)) {
                w->openReviewPane();
                w->hint(QStringLiteral("review.open.board"), relay::ShortcutHints::nextTime(
                    Keymap::instance().shortcutText(QStringLiteral("review.open"))));
            }
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
        view->onFocusBackground = view->onFocusPane;
        // Try it (#JNYN, §31.10): the card's `## Try it` names one line that opens what the turn
        // staged, and when that line is a command it runs in a terminal pane beside the board —
        // the same pane Execute opens, minus the agent task. `queueCommand` waits for that
        // pane's own shell prompt, so a pane created a moment ago still runs it.
        view->onRunCommand = [guard, workspace](const QString &command) {
            auto *w = windowOf(guard);
            if (!w || command.isEmpty()) return;
            Pane *pane = nullptr;
            try { pane = w->createPane({{"cwd", workspace}, {"workspace", workspace}}); }
            catch (const std::exception &error) { w->statusBar()->showMessage(QString::fromUtf8(error.what()), 9000); return; }
            w->insertBeside(guard, pane, Qt::Horizontal, false, w->boardSplitFloor(guard));
            w->spreadAfterAdding(pane);
            w->setActive(pane);
            focusLeaf(pane);
            pane->queueCommand(command);
            w->updateTitles();
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
        view->onExecuteCard = [guard, workspace](const QString &card, const QString &task, bool runInBackground) {
            auto *w = windowOf(guard);
            if (!w) return QString();
            Pane *pane = nullptr;
            try { pane = w->createPane({{"cwd", workspace}, {"workspace", workspace}, {"agent_role", "main"}}); }
            catch (const std::exception &error) { w->statusBar()->showMessage(QString::fromUtf8(error.what()), 9000); return QString(); }
            if (runInBackground) {
                // A background run never touches the foreground layout (#NX72): the pane goes
                // straight into a hidden background window — the destination the working-poll
                // below used to move it to a second later. The old path inserted the pane beside
                // the board, activated and focused it, then took it back out: the board visibly
                // split and closed again. The task is parked until the pane's agent is
                // configured, and a marked pane with no owned requests already reports "working"
                // (BackgroundTasks.h), so the arrival poll posts no notice; the card's chip still
                // reveals the pane (focusPane pulls background panes into view) and
                // refreshBackgroundTasks still posts the settle notices.
                pane->markBackgroundTask(true);
                RelayWindow *background = w->m_manager->newEmptyWindow(w->geometry(), true);
                background->adoptLeafAsTab(pane);
                if (!workspace.isEmpty()) background->attachTab(background->pageOf(pane), workspace,
                    QString::fromLatin1(relay::projects::kReasonRestored));
                pane->startBoardTask(task, card);
                w->m_manager->scheduleSave();
                w->notice(QStringLiteral("Job continues in the background. Reopen it in Board → Background."), 7000);
                w->hint(QStringLiteral("background.open"),
                        relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("background.open"))));
                return pane->sessionToken();
            }
            // The board keeps its list/card split (#BXCN): the new terminal's half comes out of
            // the panes beside the board, not out of the board itself.
            w->insertBeside(guard, pane, Qt::Horizontal, false, w->boardSplitFloor(guard));
            w->spreadAfterAdding(pane);
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
        // The Live strip on the Cards tab (#TBRH): every terminal pane, in any window, whose tab
        // is attached to this project or whose workspace is it — the walk `feedProjects` does for
        // the Projects page, filtered to one project. Asked on each refresh, stored nowhere.
        view->livePanes = [workspace]() {
            QJsonArray live;
            const QString project = QDir::cleanPath(workspace);
            for (QWidget *top : QApplication::topLevelWidgets()) {
                auto *window = dynamic_cast<RelayWindow *>(top);
                if (!window) continue;
                for (Pane *pane : window->allPanes()) {
                    const QString tabProject = window->tabProject(window->pageOf(pane));
                    const bool here = (!tabProject.isEmpty() && QDir::cleanPath(tabProject) == project)
                        || (!pane->workspace().isEmpty() && QDir::cleanPath(pane->workspace()) == project);
                    if (!here || pane->sessionToken().isEmpty()) continue;
                    live.append(QJsonObject{{"token", pane->sessionToken()},
                        {"title", pane->paneTitle().isEmpty() ? shortPath(pane->cwd()) : pane->paneTitle()},
                        {"model", pane->paneModel()}, {"busy", pane->dimmingAgentBusy()}});
                }
            }
            return live;
        };
        view->backgroundPanes = [guard, workspace]() {
            QJsonArray rows;
            auto *window = windowOf(guard);
            if (!window) return rows;
            const QString project = QDir::cleanPath(workspace);
            for (Pane *pane : window->m_manager->backgroundPanes()) {
                const QString tabProject = windowOf(pane)
                    ? windowOf(pane)->tabProject(windowOf(pane)->pageOf(pane)) : QString();
                const bool here = (!tabProject.isEmpty() && QDir::cleanPath(tabProject) == project)
                    || (!pane->workspace().isEmpty() && QDir::cleanPath(pane->workspace()) == project);
                if (!here || pane->sessionToken().isEmpty()) continue;
                const QString state = pane->property("backgroundInterrupted").toBool()
                    ? QStringLiteral("interrupted") : pane->backgroundTaskState();
                rows.append(QJsonObject{{"token", pane->sessionToken()},
                    {"title", pane->paneTitle().isEmpty() ? shortPath(pane->cwd()) : pane->paneTitle()},
                    {"model", pane->paneModel()}, {"state", state}});
            }
            return rows;
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
            w->spreadAfterAdding(pane);
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
        QTimer::singleShot(0, this, [this, guard, directCard] {
            QWidget *page = guard ? pageOf(guard) : nullptr;
            if (!page) return;
            if (relay::BoardWorker *worker = helperWorker(page, true); worker && !directCard)
                worker->open();
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
        // The agent docked at the foot of a file editor (card #PBZ4): the same console seam as
        // Options and Models, built on first expand. The plugins it lists are the installed ones.
        if (relay::ArtifactDock *dock = tool->preview() ? tool->preview()->artifactDock()
                                        : tool->plan()   ? tool->plan()->artifactDock() : nullptr) {
            static const bool searched = [] {
                try {
                    relay::FilePreview::setPluginSearch(relay::agent::PluginSearch::defaults(
                        dataRoot() + QStringLiteral("/backend/relay_core/plugins_bundled")));
                } catch (const std::exception &) {}
                return true;
            }();
            Q_UNUSED(searched);
            wireConsoleHost(dock, tool, QStringLiteral("artifact.ask"));
        }
        // A drag of the file/agent divider is part of the layout (#ZPHJ).
        if (relay::AgentSplit *split = tool->agentSplit())
            split->onUserMoved = [guard] { if (auto *w = windowOf(guard)) w->m_manager->scheduleSave(); };
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

    Pane *createPane(const QJsonObject &spec);

    // ----- where Alt+click material goes (card #7BYT) --------------------------------------------
    //
    // The owner's rule, 2026-09-25: "it works in the current pane or linked pane where there is a
    // prompt box. after that, the most recently opened pane." Pane may not name RelayWindow, so
    // the pane asks through its onPromptTargetPane callback and this answers with that order.
    Pane *promptTargetPane(Pane *from);
    // A pane was created (or restored through createPane): it is the most recently opened one
    // until the next is. QPointer, so a closed pane drops out without a teardown hook.
    void notePaneOpened(Pane *pane) {
        if (!pane) return;
        m_paneOpenOrder.removeAll(QPointer<Pane>(pane));
        m_paneOpenOrder.prepend(QPointer<Pane>(pane));
    }
    QList<QPointer<Pane>> m_paneOpenOrder;

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
            const QString tab = page ? page->property("relayTabId").toString() : QString();
            spec.persistScope = QStringLiteral("helper");
            // The conversation is the tab's — except a card's, which is its own, keyed per (tab,
            // card): the owner's decision 1 on card #CTRN, because the worker runs one turn at a
            // time per supervisor and folding cards into the tab's conversation would make two
            // cards serial again (#DR4K, #0Z13). The tab id is still the window's to supply, since
            // a view has no idea what a tab is; a host that already knows it and put it in front
            // (`BoardView::setTabId`) is not prefixed twice. Options, Sessions and the board list
            // share the tab's key exactly as they did.
            // A file artifact's is its own the same way, keyed per (tab, file) (card #PBZ4).
            int card = spec.persistKey.indexOf(QStringLiteral("card:"));
            if (card < 0 && spec.persistKey.startsWith(QStringLiteral("file:"))) card = 0;
            spec.persistKey = card < 0                   ? tab
                            : tab.isEmpty()              ? spec.persistKey.mid(card)
                                                         : tab + QLatin1Char('/') + spec.persistKey.mid(card);
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
        QList<relay::agent::ContextCommand> slashCommands() const override {
            return m_host ? m_host->slashCommands() : QList<relay::agent::ContextCommand>();
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
        // Alt+click in a console targets through the window like any pane (#7BYT) — its own
        // composer first, then the linked and most recently opened panes. It is not a leaf, so
        // it never enters m_paneOpenOrder itself.
        QPointer<Pane> consoleGuard(console);
        console->onPromptTargetPane = [consoleGuard](Pane *from) -> Pane * {
            auto *w = windowOf(from ? from : consoleGuard.data());
            return w ? w->promptTargetPane(from ? from : consoleGuard.data()) : nullptr;
        };
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
        handle.submitPrompt = [guard](const QString &text) { if (guard) guard->submitPrompt(text); };
        handle.composerText = [guard] { return guard ? guard->composerText() : QString(); };
        handle.setCollapsed = [guard](bool collapse) { if (guard) guard->setCollapsed(collapse); };
        handle.collapsed = [guard] { return guard && guard->collapsed(); };
        handle.runActionLetter = [guard](const QString &letter) { return guard && guard->runActionLetter(letter); };
        handle.setTranscriptHiddenUntilUsed = [guard](bool on) { if (guard) guard->setTranscriptHiddenUntilUsed(on); };
        handle.clearTranscript = [guard](const QString &surface) { if (guard) guard->clearTranscript(surface); };
        handle.turnRunning = [guard] { if (guard) guard->consoleTurnRunning(); };
        return handle;
    }

    // The callbacks a console shares with a terminal pane, and only those. A console is not in any
    // leaf list, so everything here reaches the window through `windowOf` exactly as `createPane`
    // does; what is left out is left out on purpose and said so, because the next person to add a
    // callback to `createPane` will come looking here.
    void wireAgentConsole(Pane *console);

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
        const QString type = message.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("configure")) return;
        // Only an *ask* makes this console the one the worker is briefed for. Every console of the
        // tab gets every event of the worker, and a console answers `configured` with lines of its
        // own (route_assist, its role); when each of those re-pointed the context block, two
        // consoles in one tab took turns reconfiguring the worker twice a second, for hours
        // (2026-09-21, 15,865 `configured` events in one log, card #CFG1). A line that is not an
        // ask goes down the pipe the worker is on.
        const QString tab = tabIdOf(page);
        const bool ask = type == QStringLiteral("ask");
        if (ask) m_tabConsole.insert(tab, QPointer<Pane>(console));
        relay::BoardWorker *worker = ask || !boardWorker(page) ? helperWorker(page, true) : boardWorker(page);
        if (worker) worker->send(message);
    }

    // Which card's turn an event belongs to, or empty when it belongs to the tab's own
    // conversation. A card turn's events are tagged `surface: "card:<ID>"` by the supervisor that
    // runs them (protocol 33, `queue.TurnSupervisor.agent_emit`), which is what makes `surface`
    // provenance rather than a filter.
    //
    // The row fallback below is for a worker that tags only the rows. Step 2 of this card put
    // `queue_changed` in `board_turns.QUEUE_TAGGED`, so a card supervisor's envelope names the
    // card as well — including an empty one, which is the shape the rows cannot speak for. The
    // fallback is kept because `queue.py` itself tags an envelope only while a turn is running
    // (`_emit` adds the running item's surface) and because rows carry the surface whatever the
    // worker's age: rows that all name one card make the envelope that card's. An envelope with
    // no surface and no rows says nothing about whose it is and broadcasts, as it always did.
    static QString cardSurfaceOf(const QJsonObject &event) {
        const auto ofCard = [](const QString &surface) { return surface.startsWith(QStringLiteral("card:")); };
        const QString tagged = event.value(QStringLiteral("surface")).toString();
        if (ofCard(tagged)) return tagged;
        if (!tagged.isEmpty()
            || event.value(QStringLiteral("event")).toString() != QStringLiteral("queue_changed"))
            return QString();
        QString rows;
        for (const QString &key : {QStringLiteral("items"), QStringLiteral("steering")})
            for (const QJsonValue &value : event.value(key).toArray()) {
                const QString surface = value.toObject().value(QStringLiteral("surface")).toString();
                if (!ofCard(surface) || (!rows.isEmpty() && rows != surface)) return QString();
                rows = surface;
            }
        return rows;
    }

    // Every event of a tab's worker, to every console embedded in that tab. One exception, and it
    // is the predicate #AGNT said could be added back in one line on the day it was needed (card
    // #CTRN, Planning notes question 1): an event of a **card** turn goes to that card's console
    // and to no other. A card is genuinely a different conversation — one per (tab, card), which
    // is what lets two cards be planned at once — so a card's bubbles and tool rows in the board's
    // console, and in Options', were one turn drawn in three places. Everything else broadcasts as
    // before: the tab's other consoles share one conversation (owner decision 1 on #AGNT) and each
    // draws what it can of it and ignores the rest. `app_command` is the other exception — the
    // window answers that pipe once for the tab, and a console answering it as well would run the
    // same write twice.
    void deliverToConsoles(const QString &tab, const QJsonObject &event) {
        if (event.value(QStringLiteral("event")).toString() == QStringLiteral("app_command")) return;
        const QString card = cardSurfaceOf(event);
        // The card an unsurfaced event is about, when it names one (card #KSKH): a card console
        // stops it there, so one card's write line, toast and chip do not print in the middle of
        // another card's conversation — the owner's "two agents seemingly going at once" while
        // planning several cards. The Switchboard, Options and Sessions consoles share the tab's
        // conversation (owner decision 1 on #AGNT) and keep every card's line exactly as before.
        const QString about = relay::board::namedCardOf(event);
        for (int i = int(m_consoles.size()) - 1; i >= 0; --i)
            if (!m_consoles.at(i).pane) m_consoles.removeAt(i);
        const QList<ConsoleEntry> entries = m_consoles;   // a handler may close a pane
        for (const ConsoleEntry &entry : entries) {
            if (!entry.pane) continue;
            QWidget *page = pageOf(entry.pane);
            if (!page || tabIdOf(page) != tab) continue;
            // The console's own `surface`, read through the wrapper so it is the same string the
            // `configure` block carried: `card:<ID>` on a card page, `switchboard`, `options` or
            // `sessions` everywhere else, and none of those can equal a card's.
            const QString mine = entry.context ? entry.context->spec().surface : QString();
            if (!card.isEmpty() && (!entry.context || mine != card)) continue;
            if (!about.isEmpty() && mine.startsWith(QStringLiteral("card:"))
                && about != mine.section(QLatin1Char(':'), 1))
                continue;
            entry.pane->deliverWorkerEvent(event);
        }
    }

    QWidget *buildNode(const QJsonObject &node);
    QWidget *buildNodeWidget(const QJsonObject &node);   // buildNode() less the workspace member (#E85D)

    QSplitter *newSplitter(Qt::Orientation orientation) {
        auto *splitter = new QSplitter(orientation);
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(3);
        // Saved window layout: dragging a divider changes the sizes that come back on restart.
        // A drag is also the slow path to a tidy layout, so it teaches the key that does it in one
        // press: pane.equalize puts every splitter in the tab back to equal shares (#GSJ7). Only a
        // real drag teaches it -- QSplitter::splitterMoved also fires for Relay's own setSizes()
        // (a split, a dock, equalize itself), and those run with no mouse button down.
        connect(splitter, &QSplitter::splitterMoved, this, [this](int, int) {
            m_manager->scheduleSave();
            if (!(QApplication::mouseButtons() & Qt::LeftButton)) return;
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("pane.equalize"));
            if (keys.isEmpty()) return;
            hint(QStringLiteral("pane.resize.equalize"),
                 relay::ShortcutHints::nextTime(keys, QStringLiteral("equal panes")));
        });
        return splitter;
    }

    QJsonObject serializeNode(QWidget *widget) const {
        if (auto *pane = dynamic_cast<Pane *>(widget)) {
            // One node shape for both users: "restore last closed" (Ctrl+Shift+W) and the saved
            // window layout (src/WindowState.h). Everything a pane needs to come back lives here.
            QJsonObject leaf{{"cwd", pane->cwd()}, {"workspace", pane->workspace()},
                             {"agent_role", pane->agentRole()},
                             {"agent_mode", pane->agentMode()},
                             {"input_mode", pane->mode() == QStringLiteral("program") ? QStringLiteral("auto") : pane->mode()},
                             {"effort", pane->effort()}};
            // An empty core means "whatever the process default is"; storing it would pin the empty
            // string and defeat --engine-core on the next start.
            if (!pane->engineCore().isEmpty()) leaf.insert(QStringLiteral("engine_core"), pane->engineCore());
            if (!pane->currentPreset().isEmpty()) leaf.insert(QStringLiteral("preset"), pane->currentPreset());
            if (!pane->paneModel().isEmpty()) leaf.insert(QStringLiteral("model"), pane->paneModel());
            // What the pane picked for the model box's other modes (card #MDL1, section 5.1):
            // `{"flash": {"preset", "model", "effort"}}`. `model` above is main's, which is the
            // pane's own model; these are the modes it is not in but would come back to.
            if (const QJsonObject picks = pane->modePicks(); !picks.isEmpty())
                leaf.insert(QStringLiteral("mode_picks"), picks);
            if (!pane->sessionIdForLayout().isEmpty()) leaf.insert(QStringLiteral("session_id"), pane->sessionIdForLayout());
            // A console pane names its plugin (#83YV): restoring it asks the pane's worker for
            // the console program again (the kernel's connection file is replayed by the worker),
            // so the pane comes back as a console, not as the shell it would otherwise start.
            if (!pane->consolePlugin().isEmpty()) leaf.insert(QStringLiteral("console_plugin"), pane->consolePlugin());
            // The guest session a guest preset is running right now (#PCJY): a restored pane whose
            // conversation could not be resumed still names the session to resume its guest on.
            if (const QString guestSession = pane->guestSessionForLayout(); !guestSession.isEmpty())
                leaf.insert(QStringLiteral("guest_session"), guestSession);
            // Which file holds this pane's terminal text (src/WindowState.h). The id is in every
            // node, including "restore last closed": a reopened pane finds the text of the pane it
            // came from, and the ids in the saved layout are what keeps the store pruned.
            leaf.insert(QStringLiteral("scrollback"), pane->scrollbackId());
            // A pane on a host comes back on that host (#XQ8F). The login is rebuilt from the
            // process's own argv, and when the wrapper holds it in a session the line is the
            // reattach form — the restored pane attaches to the session it had, with its programs
            // still running, instead of logging in fresh. `Pane` runs it at the first prompt
            // (src/PaneRuntime.cpp); the `queue` below stays the person's own typing, not this.
            {
                QString loginHost;
                const QStringList argv = relay::ssh::processArgv(pane->foregroundPid());
                const QString login = relay::ssh::rerunCommand(argv, &loginHost);
                const QString holder = relay::ssh::holderSession(argv);
                if (!login.isEmpty())
                    leaf.insert(QStringLiteral("remote_login"),
                                holder.isEmpty() ? login
                                    : QStringLiteral("RELAY_SSH_SESSION=") + relay::ssh::shellQuote(holder)
                                          + QLatin1Char(' ') + login);
                // The local twin (#87HB): a pane whose shell runs under the local holder saves
                // its re-attach line, and a restarted pane runs it at the first prompt
                // (Pane::initRestore, runRestoredRemoteLogin). rerunCommand reads this pane's
                // tmux client as no ssh login, so the two leaves never both appear.
                if (const QString localLine = pane->localHolderLine(); !localLine.isEmpty())
                    leaf.insert(QStringLiteral("local_login"), localLine);
            }
            if (const QJsonArray queue = pane->queueForRestore(); !queue.isEmpty())
                leaf.insert(QStringLiteral("queue"), queue);
            return withWorkspaceMember(widget, {{"pane", leaf}});
        }
        // A leaf in an artifact workspace saves its member id beside its node (#E85D).
        if (auto *tool = dynamic_cast<ToolPane *>(widget)) return withWorkspaceMember(widget, tool->node());
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
        // RELAY.md's standing rule: the button duplicates a faster path, so it teaches it.
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
        // The tab's artifact workspace group (#E85D), in the same wrapper and only when it has one.
        const QJsonObject group = workspaceGroupJson(page);
        if (node.isEmpty() || (project.isEmpty() && !ownTheme && id.isEmpty() && group.isEmpty())) return node;
        QJsonObject tab{{QStringLiteral("node"), node}};
        if (!group.isEmpty()) tab.insert(QStringLiteral("artifact_workspace"), group);
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
        refreshPaneDimming();
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

    // Make sharing match the selected scope: a whole-tab share follows that tab, while All tabs
    // publishes every pane in every window and keeps following panes and tabs created later.
    void syncTabShares() {
        relay::RemoteShare &share = relay::RemoteShare::instance();
        if (!share.hasTabShares()) return;   // unshareTab already ended every pane it shared
        const bool all = share.isAllTabsShared();
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            const QString id = page->property("relayShareTab").toString();
            const bool whole = share.isTabShared(id);
            for (Pane *pane : panesIn(page)) {
                const QString token = pane->sessionToken();
                const QString under = share.tabOf(token);
                if (all || whole) {
                    const QString wanted = whole ? id : QString();
                    if (!share.isSharing(token)) {
                        QString error;
                        if (pane->startSharing(wanted, &error)) {
                            if (all) share.markAllTabsPane(token);
                        } else if (!error.isEmpty()) {
                            pane->toast(error, 5000);
                        }
                    } else if (under != wanted) {
                        share.setPaneTab(token, wanted);
                    }
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
                                       : QStringLiteral("Sharing and remote connections"));
    }

    // One plug-menu row chosen. The ids are relay::remotesettings::plugMenu's.
    void runPlugItem(const QString &id) {
        if (id == QStringLiteral("remote.sharing")) {
            Pane *owner = dynamic_cast<Pane *>(m_activeLeaf.data());
            if (!owner) owner = m_active;
            if (!owner) {
                const QList<Pane *> panes = allPanes();
                owner = panes.isEmpty() ? nullptr : panes.first();
            }
            if (owner) openSharingPane(owner, true);
            else notice(QStringLiteral("Open a terminal pane first to show Sharing."), 6000);
            return;
        }
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
    // land here. The switch goes on first when it is off, then the Sharing pane opens on Devices
    // with a pairing offer started. Body in src/RelayWindowSharing.cpp.
    void pairPhone();

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
        // Pairing is the Sharing pane's Devices page, where the QR, the typed code and the five
        // digits to compare live (#SMDX). The same door as the plug menu's "Pair a phone…"
        // (#FR1C): the switch first when it is off, then that page with an offer started.
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

    // A pane opened for `anchor` — Sessions, Settings, a file, Review, Info, a fork — docks where
    // every other one does (card #QVGQ): relay::panes::dockOrientation says right, or beneath a
    // pane too narrow to share. Splits, moves and drags name their side and call insertBeside.
    void dockBeside(QWidget *anchor, QWidget *pane) {
        insertBeside(anchor, pane, relay::panes::dockOrientation(anchor->width()), false);
        spreadAfterAdding(pane);
    }

    // Adding a pane spreads the tab; rearranging and closing keep the person's sizes (card
    // #QVGQ, the model in PaneLayout.h). Every path that ADDS a pane ends here once it is in its
    // splitter: equalizePage, with the Switchboard's floor, once Qt has laid out a newly wrapped
    // splitter. Keep it out of insertBeside(), which also powers moves and drags.
    void spreadAfterAdding(QWidget *pane) {
        QPointer<QWidget> pageGuard(pageOf(pane));
        QTimer::singleShot(0, pane, [this, pageGuard] { if (pageGuard) equalizePage(pageGuard); });
    }

    void split(Qt::Orientation orientation) {
        splitToward(orientation == Qt::Horizontal ? relay::panes::Direction::Right : relay::panes::Direction::Down);
    }

    // A new pane on `direction`'s side of the focused one. `offerPlacement` opens the short window
    // in which Left, Up or Down re-dock it (issue #78BN); only the one-key "new pane" uses it.
    // `consolePlugin` (#83YV) makes the pane a console pane: instead of a shell it asks its own
    // worker for that plugin's console program (`workspace_activate {console: true}`, protocol 36)
    // and runs the argv of the `workspace_console` answer in its pty. It rides in the spec — the
    // same field a saved layout replays — so nothing here needs to know what a console is.
    void splitToward(relay::panes::Direction direction, bool offerPlacement = false,
                     const QString &consolePlugin = QString()) {
        QWidget *anchor = m_activeLeaf;
        if (!anchor) return;
        Pane *pane = nullptr;
        const QString workspace = m_active ? m_active->workspace() : m_manager->workspace();
        QJsonObject spec{{"cwd", leafCwd(anchor)}, {"workspace", workspace}};
        if (!consolePlugin.isEmpty()) spec.insert(QStringLiteral("console_plugin"), consolePlugin);
        try { pane = createPane(spec); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return; }
        insertBeside(anchor, pane, relay::panes::orientationFor(direction), relay::panes::towardStart(direction));
        setActive(pane);
        spreadAfterAdding(pane);   // #EQM2
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
            spreadAfterAdding(pane);   // the arrow finishes an add, so it spreads like one
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
        // (RELAY.md, "Shortcut hints"), under its own id: it stops after the registry's limit and
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

    void buildWindowChrome();

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
        if (tool->kind() == ToolPane::Kind::Models) { closeModelsPane(tool); return; }
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
                // "3 memories from Claude Code to review · Review" (#MEMS): the one review list.
                if (actionId == QStringLiteral("memory.review")) { m_notifications->hide(); openMemorySuggestion(QString()); return; }
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

    void toggleFullscreen() {
        // Keep WindowMaximized so exiting fullscreen restores the previous window mode.
        setWindowState(windowState() ^ Qt::WindowFullScreen);
        updateChromeState();
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
                    // The conversation info button (ⓘ, card #Y63Z) is the chrome's own; here it
                    // gets its overlay (card #7EWF), which the pane binds to its worker.
                    if (auto *pane = dynamic_cast<Pane *>(leaf); pane && chrome->infoButton()) {
                        auto *overlay = new relay::sessioninfo::InfoOverlay(leaf, chrome->infoButton(),
                                                                           pane->sessionToken().left(8));
                        chrome->infoOverlay = overlay;
                        pane->bindInfoOverlay(overlay);
                        overlay->onClosed = [guard] {
                            auto *w = windowOf(guard);
                            if (w && w->m_activeLeaf.data() == guard.data()) focusLeaf(guard);
                        };
                        chrome->onInfo = [guard] {
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
                        };
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
                        // Middle click on the header (owner, 2026-09-24): the chrome × button's
                        // exchange — activate this pane, then pane.close — including the key hint.
                        pane->onHeaderClose = [guard] {
                            auto *w = windowOf(guard);
                            if (!w) return;
                            w->setActiveLeaf(guard);
                            w->runAction(QStringLiteral("pane.close"));
                            const QString keys = Keymap::instance().shortcutText(QStringLiteral("pane.close"));
                            if (!keys.isEmpty())
                                w->hint(QStringLiteral("chrome.pane.close"),
                                        relay::ShortcutHints::nextTime(keys, Keymap::instance().description(QStringLiteral("pane.close")).toLower()));
                        };
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

    void adjustPaneDimming(QWidget *leaf, int delta) {
        if (auto *chrome = chromeOf(leaf)) {
            chrome->dimming.adjust(delta, chrome->dimAmount);
            refreshPaneDimming();
        }
    }

    void refreshPaneDimming() {
        const bool automatic = relay::settings::boolValue(QStringLiteral("appearance/auto_dim"), false);
        const bool includeActive = relay::settings::boolValue(QStringLiteral("appearance/auto_dim_active"), false);
        const bool focus = relay::settings::boolValue(QStringLiteral("appearance/focus_mode"), false);
        const int strength = relay::settings::intValue(QStringLiteral("appearance/dim_strength"), 90);
        for (int i = 0; i < m_tabs->count(); ++i) {
            for (QWidget *leaf : leavesIn(m_tabs->widget(i))) {
                auto *chrome = chromeOf(leaf);
                if (!chrome) continue;
                auto *pane = dynamic_cast<Pane *>(leaf);
                const bool busy = pane && pane->dimmingAgentBusy();
                bool attention = false, done = false;
                if (pane) {
                    const auto state = relay::panestatus::resolve(pane->statusFacts(), chrome->seenSerial);
                    attention = state == relay::panestatus::State::NeedsYou || state == relay::panestatus::State::Failed;
                    done = state == relay::panestatus::State::Done;
                }
                // Guest lifecycle reports busy separately from the built-in turn finish serial.
                if (chrome->dimming.working && !busy) chrome->dimming.completed = true;
                if (busy) chrome->dimming.completed = false;
                if (leaf == m_activeLeaf && chrome->dimming.revealed) chrome->dimming.completed = false;
                done = done || chrome->dimming.completed;
                chrome->dimming.observe(leaf == m_activeLeaf && i == m_tabs->currentIndex(), busy, attention, done);
                chrome->paintDimming(chrome->dimming.amount(automatic, focus, strength, includeActive));
            }
        }
    }

    // ----- pane states and tab icons (#XM0T), remote sessions (#SPBN) -------------------------
    // One poll for every pane in the window. Each terminal's header shows its own state; each tab
    // shows the most urgent one among its panes, and a red mark when one of them is in an ssh,
    // mosh or telnet session. A finished turn stays news (done / failed / needs you) until the
    // pane has been the one you are looking at for kSeenAfterMs, so coming back to the window
    // still shows what happened for a moment. Notifications are the pane's own (Pane::notify).
    static constexpr int kStatusPollMs = 400;
    static constexpr qint64 kSeenAfterMs = 1500;

    void refreshPaneStatus();

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

    // ----- dragging a tab out of its window (#W6ES) ---------------------------------------
    // QTabBar already drags a tab label inside the bar (reorder); pulling it further, out of
    // the tab row, used to be nothing at all. tabDrag watches the bar's mouse events from the
    // window's event filter and, once the cursor leaves the tab row (relay::tabs::leavesTabRow —
    // not the window: the target usually overlaps the source, and a maximized window can never
    // be left at all, which made the gesture a silent no-op in the common layouts), turns the
    // gesture into a tab move: the whole page — splits included — goes to the Relay window
    // under the cursor, or to a window of its own on empty space. Every event is passed
    // on untouched: Qt's internal drag owns the mouse grab for as long as the button is down
    // and its state has to run to its own release, so this only observes.

    // The Relay window at a global point, if any: a torn-off tab targets whole windows, not
    // panes or tab labels. The window the cursor is really over answers first — with
    // overlapping windows, topLevelWidgets() is in creation order and would name whichever
    // window happened to be opened first, front or back — with the geometry scan kept as the
    // fallback for a release on a window's frame, where no widget is found.
    static RelayWindow *relayWindowAt(const QPoint &global) {
        for (QWidget *w = QApplication::widgetAt(global); w; w = w->parentWidget())
            if (auto *window = dynamic_cast<RelayWindow *>(w); window && !window->isMinimized())
                return window;
        for (QWidget *w : QApplication::topLevelWidgets())
            if (auto *window = dynamic_cast<RelayWindow *>(w);
                window && !window->isMinimized() && window->frameGeometry().contains(global))
                return window;
        return nullptr;
    }

    // The drop highlight, reparented to whichever window the drop would land in.
    QFrame *dropZoneOverlay(RelayWindow *w) {
        if (!m_dropZone || m_dropZone->window() != w) {
            delete m_dropZone;
            m_dropZone = new QFrame(w->centralWidget());
            m_dropZone->setObjectName(QStringLiteral("dropZone"));
            m_dropZone->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_dropZone->setAttribute(Qt::WA_StyledBackground);
        }
        return m_dropZone;
    }

    bool tabDrag(QObject *object, QEvent *event) {
        if (object != m_tabs->tabBar()) return false;
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *mouse = dynamic_cast<QMouseEvent *>(event);
            const int tab = mouse && mouse->button() == Qt::LeftButton
                                ? m_tabs->tabBar()->tabAt(mouse->pos()) : -1;
            m_tabDragPressed = tab >= 0;
            m_tabDragTorn = false;
            m_tabDragPage = tab >= 0 ? m_tabs->widget(tab) : nullptr;
            if (m_tabDragPressed) m_tabDragPressAt = mouse->globalPos();
            return false;
        }
        case QEvent::MouseMove: {
            if (!m_tabDragPressed) return false;
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (!(mouse->buttons() & Qt::LeftButton)) {   // the grab broke without a release
                m_tabDragPressed = false;
                m_tabDragTorn = false;
                if (m_dropZone) m_dropZone->hide();
                return false;
            }
            const QTabBar *bar = m_tabs->tabBar();
            const QRect barGlobal(bar->mapToGlobal(QPoint(0, 0)), bar->size());
            if (!relay::tabs::leavesTabRow(m_tabDragPressAt, mouse->globalPos(), barGlobal,
                                           QApplication::startDragDistance())) {
                // Back inside the tab row: Qt's reorder owns the gesture again, and the window
                // under the cursor stops offering a drop.
                if (m_tabDragTorn) tabDragMove(nullptr, mouse->globalPos());
                m_tabDragTorn = false;
                return false;
            }
            m_tabDragTorn = true;
            tabDragMove(m_tabDragPage, mouse->globalPos());
            return false;
        }
        case QEvent::MouseButtonRelease: {
            if (!m_tabDragPressed) return false;
            if (static_cast<QMouseEvent *>(event)->button() != Qt::LeftButton) return false;
            m_tabDragPressed = false;
            const bool torn = m_tabDragTorn;
            m_tabDragTorn = false;
            if (torn) {
                const QPoint global = static_cast<QMouseEvent *>(event)->globalPos();
                tabDragMove(nullptr, global);   // the highlight has done its job
                tabDragEnd(m_tabDragPage, global);
            }
            return false;
        }
        default: return false;
        }
    }

    // Live feedback for a torn-off tab: the window under the cursor lights up where the tab
    // would land — its tab row, since wherever it is dropped it arrives as a tab. A null page
    // means "show nothing": the cursor came back inside this window, or the drag ended.
    void tabDragMove(QWidget *page, const QPoint &global) {
        RelayWindow *w = page ? relayWindowAt(global) : nullptr;
        if (!w || w == this) {
            if (m_dropZone) m_dropZone->hide();
            return;
        }
        QFrame *zone = dropZoneOverlay(w);
        QWidget *bar = w->m_tabs->tabBar();
        zone->setGeometry(QRect(bar->mapTo(w->centralWidget(), QPoint(0, 0)), bar->size()));
        zone->show();
        zone->raise();
    }

    // The release of a torn-off tab: the page moves to the window under the cursor, or to a
    // window of its own centred on the drop point when the cursor is on empty space or over
    // another application. moveTabToWindow owns the whole move-out sequence.
    void tabDragEnd(QWidget *page, const QPoint &global) {
        RelayWindow *target = relayWindowAt(global);
        if (target == this) target = nullptr;   // the drag left this window; anything else is stale
        moveTabToWindow(m_tabs->indexOf(page), target, global);
    }

    void dragPaneMove(QWidget *dragged, const QPoint &global) {
        const auto target = dropTarget(dragged, global);
        if (!target.first) { if (m_dropZone) m_dropZone->hide(); return; }
        RelayWindow *w = windowOf(target.first);
        dropZoneOverlay(w);
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
                // A chain member that lands in another tab brings the chain with it (#R660).
                if (w == this) moveWorkspaceChain(dragged);
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
            // Same (#R660): a drop into another tab's pane is a cross-tab move for a member.
            if (w == this && w->pageOf(dragged) != nullptr
                && !dragged->property("relayWorkspaceMember").toString().isEmpty())
                moveWorkspaceChain(dragged);
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
        // A Board pane names its own project. Moving it into a fresh tab must attach that tab
        // before its helper is started; otherwise the view shows cards while the helper has no
        // board tools. Ordinary panes still create unattached tabs (#JN7X).
        auto *board = dynamic_cast<ToolPane *>(leaf);
        const QString boardProject = board && board->board() ? board->board()->workspace() : QString();
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(leaf);
        leaf->show();
        index = index < 0 ? m_tabs->count() : std::min(index, m_tabs->count());
        m_tabs->insertTab(index, page, QString());
        m_tabs->setCurrentIndex(index);
        if (!boardProject.isEmpty()) {
            attachTab(page, boardProject, QString::fromLatin1(relay::projects::kReasonSwitchboard));
            board->board()->setTabId(tabIdOf(page));
        } else {
            repointTabPanes(page);
        }
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
        // A chain member that takes a tab of its own brings the chain with it (#R660).
        moveWorkspaceChain(leaf);
    }

    // The move-out sequence of one tab, shared by "Move tab to new window" and a tear-off
    // release (#W6ES): detach the page — its name and project travel with it — hand it to
    // `target`, creating the window when there is none yet, and fix up both windows. A window
    // created here is centred on `dropAt` (a tear-off release) or offset from this one (the
    // action's offset-from-source behaviour, kept).
    void moveTabToWindow(int index, RelayWindow *target, const QPoint &dropAt) {
        QWidget *page = m_tabs->widget(index);
        if (!page) return;
        if (!target && m_tabs->count() <= 1) {   // a window keeps at least its last tab
            notice(QStringLiteral("This is the only tab in the window."), 4000); return;
        }
        QWidget *lastActive = m_lastActive.value(page);
        if (m_active && pageOf(m_active) == page) m_active = nullptr;
        if (m_activeLeaf && pageOf(m_activeLeaf) == page) m_activeLeaf = nullptr;
        m_lastActive.remove(page);
        const QString tabName = m_tabNames.value(page);
        const QString project = m_tabProject.value(page);   // the tab's project travels with it
        forgetTab(page);
        m_tabs->removeTab(index);
        page->setParent(nullptr);
        if (!target) {
            const QSize size = page->size().isEmpty() ? this->size() : page->size();
            target = m_manager->newEmptyWindow(
                QRect(dropAt - QPoint(size.width() / 2, size.height() / 3), size));
        }
        target->adoptPage(page, lastActive);
        if (!tabName.isEmpty()) target->renameTab(tabName, false, page);
        if (!project.isEmpty())
            target->attachTab(page, project, QString::fromLatin1(relay::projects::kReasonRestored));
        if (QWidget *current = m_tabs->currentWidget()) {
            QWidget *leaf = m_lastActive.value(current);
            if (!leaf) { const auto leaves = leavesIn(current); leaf = leaves.isEmpty() ? nullptr : leaves.first(); }
            if (leaf) setActiveLeaf(leaf);
        }
        updateTitles();
        target->raise(); target->activateWindow();
        if (m_tabs->count() == 0) {   // its last tab left for another window: the window goes with it,
            m_confirmedClose = true;   // the way the last tab's cross closes it (and is not remembered)
            m_skipRemember = true;
            QTimer::singleShot(0, this, [this] { close(); });
        }
    }

    void moveTabToNewWindow(int index) {
        moveTabToWindow(index, nullptr, geometry().translated(40, 40).center());
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

    // Alt+0, "auto-resize" (owner report, 2026-09-19: pane sizes jiggle; the key was Ctrl+Alt+0
    // until #GSJ7): every splitter in the active tab back to equal shares, top to bottom, so a page
    // a drag has left lopsided goes back to a tidy grid in one key. The drag that leaves it
    // lopsided is also where newSplitter teaches this key. The same "identical
    // entries, let Qt turn them into ratios of the real width" trick insertBeside's equal-shares
    // fallback and movePastPageEdge's outer wrapper use, just applied to every splitter in the
    // page rather than one.
    void equalizePage(QWidget *page) {
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
    }

    void equalizeActivePage() {
        QWidget *page = m_active ? pageOf(m_active) : (m_tabs ? m_tabs->currentWidget() : nullptr);
        const QList<QPointer<QSplitter>> splitters = relay::panes::splittersIn(page);
        equalizePage(page);
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

    // Actions opens the list only when requested; Sessions keeps its own tab of the same view.
    void openClosedList() {
        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("recentlyClosedPicker"));
        dialog.setWindowTitle(QStringLiteral("Recently closed"));
        dialog.resize(860, 520);
        auto *layout = new QVBoxLayout(&dialog);
        auto *view = createClosedList(this);
        layout->addWidget(view);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        QString chosen;
        view->onReopen = [&](const QString &id) { chosen = id; dialog.accept(); };
        if (dialog.exec() == QDialog::Accepted && !chosen.isEmpty())
            m_manager->restoreClosed(this, chosen);
    }

    static relay::closed::ListView *createClosedList(RelayWindow *window) {
        auto *view = new relay::closed::ListView;
        WindowManager *manager = window->m_manager;
        QPointer<RelayWindow> guard(window);
        view->setRecords(manager->closedRecords());
        manager->watchClosed(view, [view, manager] { view->setRecords(manager->closedRecords()); });
        view->onReopen = [manager, guard](const QString &id) { manager->restoreClosed(guard, id); };
        view->onDiscard = [manager](const QString &id) { manager->discardClosed(id); };
        view->onClear = [manager, view] {
            if (QMessageBox::question(view, QStringLiteral("Clear recently closed?"),
                                      QStringLiteral("Forget all %1 closed items and the terminal text saved for them? "
                                                     "Their conversations stay in Sessions.").arg(manager->closedRecords().size()),
                                      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes)
                manager->forgetClosed();
        };
        return view;
    }

public:
    static void registerClosedTab() {
        addSessionsTab(QStringLiteral("closed"), QStringLiteral("Recently closed"), [](RelayWindow *window) -> QWidget * {
            return createClosedList(window);
        });
    }

private:

    void runBackgroundPane(Pane *pane) {
        // An agent already working here (native turn, guest CLI turn, live subagents) moves
        // to the background as-is: the button and Ctrl+Alt+Enter hand the live pane over
        // instead of submitting a second task (#BGRN). A guest pane needs no configured
        // native agent for that, so this check comes before the readiness one.
        if (pane && pane->agentActive()) { moveBackgroundPane(pane); return; }
        if (!pane || !pane->agentReady()) { notice(QStringLiteral("Choose an agent before running in background.")); return; }
        const QString task = pane->composerText().trimmed();
        if (task.isEmpty()) { notice(QStringLiteral("Type a task before running in background.")); return; }
        pane->markBackgroundTask(true);
        pane->askAgent(task);
        pane->draftInComposer(QString());
        QPointer<Pane> pending(pane);
        QPointer<RelayWindow> owner(this);
        auto attempts = std::make_shared<int>(0);
        auto *timer = new QTimer(pane);
        timer->setInterval(250);
        connect(timer, &QTimer::timeout, pane, [timer, pending, owner, attempts] {
            if (!pending || !owner || ++*attempts > 80) {
                timer->stop(); timer->deleteLater(); return;
            }
            // A prompt that immediately returns for clarification stays visible. Once the turn
            // has continued working, the ordinary live-pane move retains the exact session.
            if (*attempts >= 4 && pending->agentBusy()
                && pending->backgroundTaskState() == QStringLiteral("working")) {
                timer->stop(); timer->deleteLater(); owner->backgroundPane(pending);
            }
        });
        timer->start();
    }

    void moveBackgroundPane(Pane *pane) {
        if (!pane || !pane->agentActive()) { notice(QStringLiteral("No running agent to move to background.")); return; }
        if (pane->backgroundTaskState() == QStringLiteral("needs-you")) {
            notice(QStringLiteral("Answer the agent's question before moving it to background.")); return;
        }
        pane->markBackgroundTask(false);
        backgroundPane(pane);
    }

    void closeActive() {
        QWidget *pane = m_activeLeaf;
        if (!pane) return;
        // The Settings pane closes the way Esc closes it: focus goes back where it was, and it is
        // never what closes the window. Its × is the chrome's, so that button comes here too.
        if (auto *tool = dynamic_cast<ToolPane *>(pane); tool && tool->settings()) { closeSettingsPane(tool); return; }
        closePane(pane, true);
    }

    // A new pane configures asynchronously. A phone-triggered background run (#E728) keeps the
    // pane visible until its agent accepts the task, then moves it to the background window; a
    // failed start leaves the reason and the pane on screen. The board's own Run button does not
    // come through here (#NX72): it adopts its pane into a hidden background window at once, so
    // the foreground layout never moves.
    void backgroundPaneWhenWorking(Pane *pane) {
        if (!pane) return;
        auto attempts = std::make_shared<int>(0);
        QPointer<Pane> pending(pane);
        QPointer<RelayWindow> owner(this);
        auto *timer = new QTimer(pane);
        timer->setInterval(250);
        connect(timer, &QTimer::timeout, pane, [timer, attempts, pending, owner] {
            if (!pending || !owner || ++*attempts > 80) {
                timer->stop(); timer->deleteLater(); return;
            }
            if (*attempts >= 4 && pending->agentBusy()
                && pending->backgroundTaskState() == QStringLiteral("working")) {
                timer->stop(); timer->deleteLater();
                owner->backgroundPane(pending);
            }
        });
        timer->start();
    }

    bool backgroundPane(Pane *pane) {
        QWidget *page = pageOf(pane);
        if (!page) return false;
        if (!pane->property("backgroundMarked").toBool()) pane->markBackgroundTask(false);
        const QString project = tabProject(page);
        // Leave a reachable window when the last pane is backgrounded. No desktop tray is
        // required, and Qt's last-window exit cannot silently kill the session just retained.
        if (m_tabs->count() == 1 && leavesIn(page).size() == 1)
            if (!addTab(paneNode(pane->cwd()))) return false;
        RelayWindow *background = m_manager->newEmptyWindow(geometry(), true);
        if (!takeLeaf(pane)) { background->deleteLater(); return false; }
        background->adoptLeafAsTab(pane);
        if (!project.isEmpty()) background->attachTab(background->pageOf(pane), project,
            QString::fromLatin1(relay::projects::kReasonRestored));
        m_manager->scheduleSave();
        notice(QStringLiteral("Job continues in the background. Reopen it in Board → Background."), 7000);
        hint(QStringLiteral("background.open"),
             relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("background.open"))));
        return true;
    }

    relay::paneclose::Choice askActiveClose(int count = 1) {
        if (m_closePrompt) return relay::paneclose::Choice::Cancel;
        m_closePrompt = true;
        QPointer<RelayWindow> guard(this);
        const auto choice = relay::paneclose::ask(this, count);
        if (guard) m_closePrompt = false;
        return guard ? choice : relay::paneclose::Choice::Cancel;
    }

public:
    void closePane(QWidget *pane, bool record);

    void closeTab(int index, bool record, bool stopApproved = false) {
        if (m_closePrompt) return;
        QWidget *page = m_tabs->widget(index);
        if (!page) return;
        if (!stopApproved) {
            int busy = 0;
            for (Pane *pane : panesIn(page)) if (pane->hasCloseWork()) ++busy;
            if (busy) {
                QPointer<QWidget> guard(page);
                const auto choice = askActiveClose(busy);
                if (choice == relay::paneclose::Choice::Cancel || !guard) return;
                if (choice == relay::paneclose::Choice::Background) {
                    const auto terminals = panesIn(page);
                    for (Pane *pane : terminals) if (!backgroundPane(pane)) return;
                    if (guard && m_tabs->indexOf(guard) >= 0) closeTab(m_tabs->indexOf(guard), record, true);
                    return;
                }
                index = m_tabs->indexOf(page);
                if (index < 0) return;
            }
        }
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
        const bool busy = std::any_of(panes.cbegin(), panes.cend(), [](Pane *p) { return p->hasCloseWork(); });
        int leafCount = 0;
        for (int i = 0; i < m_tabs->count(); ++i) leafCount += leavesIn(m_tabs->widget(i)).size();
        QString text = QStringLiteral("Close this window and its %1 tab(s) and %2 pane(s)?").arg(m_tabs->count()).arg(leafCount);
        if (busy) text += QStringLiteral("\n\nA program or agent turn is still running and will be stopped.");
        if (m_manager->lastVisibleWindow(this) && !m_manager->backgroundPanes().isEmpty())
            text += QStringLiteral("\n\nClosing the last window exits Relay and stops its %1 background session(s).")
                .arg(m_manager->backgroundPanes().size());
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
    bool m_closePrompt = false;
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
    QPointer<relay::ActionPalette> m_palette;   // Ctrl+?, made on first use (#MAGP)
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
    // Dragging a tab label out of its window (#W6ES): the pressed tab's page, where the press
    // landed, and whether the cursor has since left this window with the button still down.
    // Inside the window the gesture is QTabBar's own (reorder); past the window edge it is ours.
    QPointer<QWidget> m_tabDragPage;
    QPoint m_tabDragPressAt;
    bool m_tabDragPressed = false, m_tabDragTorn = false;
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
    QHash<QString, QPointer<QToolButton>> m_backgroundCountButtons;
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
