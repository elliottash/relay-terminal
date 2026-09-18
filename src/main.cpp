// SPDX-License-Identifier: GPL-3.0-or-later
#include "RichEditor.h"
#include "Theme.h"
#include "FilePanes.h"
#include "BoardPane.h"          // Switchboard: cards, threads, card detail
#include "BoardWorker.h"        // the per-window Switchboard worker (protocol 17)
#include "AgentUi.h"
#include "Completion.h"
#include "FileIndex.h"             // the `@` picker's file listing, built without blocking
#include "ShellHighlighter.h"
#include "Hints.h"
#include "Notifications.h"        // window header: the bell and its list
#include "InputPolicy.h"           // prompt-box-only input: where a submitted line goes
#include "ScreenPrompt.h"          // "is the program waiting for input?", read off the screen
#include "PaneLayout.h"            // pane focus, pane moves and grip drops
#include "QueueNav.h"              // arrowing through the queue while its items are edited
#include "PaneTitles.h"            // model-written pane titles and the tab labels made from them
#include "TurnTranscript.h"
#include "ModelSettings.h"
#include "SettingsPane.h"
#include "SkillsDialog.h"
#include "SubagentTranscript.h"   // subagents UI
#include "SubagentsPanel.h"
#include "RequestLedger.h"         // request ledger UI
#include "RequestsPanel.h"
#include "Conversations.h"         // conversation list, search and the Ctrl+F find bar
#include "Logging.h"               // rotating diagnostics log (~/.local/share/relay/logs)
#include "TerminalBackends.h"
#include "TerminalBackend.h"
#include "WindowState.h"   // saved window layout ("reopen where I left off")
#include "RuntimeDirs.h"   // /tmp/relay-XXXXXX: the owner mark, and the sweep for what a crash left
#include "RemoteShare.h"    // sharing a pane with a phone (docs/REMOTE-PROTOCOL.md)
#include "backend/VTermBackend.h"  // the engine can hand over a frame to a program
#include "core/VtCore.h"           // const scrollback access, for a phone's history pages
#include "session/TerminalSession.h"
#include "view/TerminalView.h"
#include "Voice.h"          // voice transcription: capture, the hold key, the transcript
#include "Images.h"         // image context: paste, drop, `@path` and "Screenshot this pane"
#include "Aliases.h"        // aliases: saved commands and prompts, their fields and invocations
#include "MarkdownAnsi.h"   // agent replies in the terminal: Markdown rendered as it streams
#include "WordWrap.h"       // ...and broken between words at the pane's width
#include "OutputLinks.h"    // what a link in the output is; `relay://card/<id>` for a `#K7Q2`
#include "SlashCommands.h"  // an unknown `/command` is Relay's to answer, not the shell's
#include <iterator>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QFileSystemModel>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
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
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QSet>
#include <QDesktopServices>
#include <QMetaObject>
#include <QShowEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSocketNotifier>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTabWidget>
#include <QFileSystemWatcher>
#include <QKeySequence>
#include <QUrl>
#include <QInputDialog>
#include <QRegularExpression>
#include <QIcon>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTreeView>
#include <QLocalServer>
#include <QLocalSocket>
#include <QElapsedTimer>
#include <QDateTime>
#include <QListWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QToolButton>
#include <QTabBar>
#include <QLockFile>
#include <QTimer>
#include <QToolBar>
#include <QWindow>
#include <QScrollArea>
#include <QPainterPath>
#include <QUuid>
#include <QVBoxLayout>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QDirIterator>
#include <QMimeDatabase>
#include <QTextBlock>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <cmath>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

// The units main.cpp used to hold inline, one header each (see docs/ARCHITECTURE.md).
// This is still one translation unit: the headers are included here and nowhere else,
// so Pane and RelayWindow keep their bodies in the class and the build stays as it was.
#include "AppPaths.h"
#include "Keymap.h"
#include "Isolation.h"
#include "Pane.h"
#include "PaneChrome.h"
#include "WindowChrome.h"
#include "RelayWindow.h"
#include "WindowManagerImpl.h"

// A relay:// link clicked in another application (a browser, an editor, a file manager) reaches
// the desktop's handler for the scheme. Relay's own panes open these links themselves.
// Install a user-level x-scheme-handler/relay entry that runs relay-open (idempotent, silent;
// one status message the first time). RELAY_NO_URL_HANDLER=1 skips it.
static void registerUrlHandler() {
    if (qEnvironmentVariableIntValue("RELAY_NO_URL_HANDLER")) return;
    const QString helper = qEnvironmentVariable("RELAY_OPEN_HELPER");
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (!QFileInfo::exists(helper) || python.isEmpty()) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
    const QString name = QStringLiteral("org.relayterminal.Relay.url-handler.desktop");
    auto quote = [](QString arg) { arg.replace('\\', QStringLiteral("\\\\")).replace('"', QStringLiteral("\\\"")).replace('$', QStringLiteral("\\$")).replace('`', QStringLiteral("\\`")); return '"' + arg + '"'; };
    const QByteArray content = QStringLiteral("[Desktop Entry]\nType=Application\nName=Relay link handler\n"
                                              "Comment=Opens relay:// links (agent turn details) in the running Relay\n"
                                              "Exec=%1 %2 %u\nNoDisplay=true\nTerminal=false\nMimeType=x-scheme-handler/relay;\n")
                                   .arg(quote(python), quote(helper)).toUtf8();
    QFile existing(dir + '/' + name);
    const bool same = existing.open(QIODevice::ReadOnly) && existing.readAll() == content;
    existing.close();
    QProcess query;
    query.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), QStringLiteral("x-scheme-handler/relay")});
    const bool isDefault = query.waitForFinished(3000) && QString::fromUtf8(query.readAllStandardOutput()).trimmed() == name;
    if (same && isDefault) return;
    if (!same) {
        QDir().mkpath(dir);
        QSaveFile file(dir + '/' + name);
        if (!file.open(QIODevice::WriteOnly)) return;
        file.write(content);
        if (!file.commit()) return;
    }
    // Detached so a slow desktop database refresh never blocks the UI.
    QString script = QStringLiteral("xdg-mime default %1 x-scheme-handler/relay; "
                                    "command -v update-desktop-database >/dev/null && update-desktop-database -q %2; "
                                    "command -v kbuildsycoca5 >/dev/null && kbuildsycoca5 >/dev/null 2>&1; true").arg(name, quote(dir));
    QProcess::startDetached(QStringLiteral("sh"), {QStringLiteral("-c"), script});
    if (!QSettings().value(QStringLiteral("url_handler/announced")).toBool()) {
        QSettings().setValue(QStringLiteral("url_handler/announced"), true);
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *window = dynamic_cast<RelayWindow *>(widget)) window->notice(QStringLiteral("Registered relay:// links so agent turn details open from the terminal."), 8000);
    }
}

// The pane-agent role and its "new panes" toggle were renamed from "fast" to "flash" on 2026-09-18.
// Settings are the one place the old spelling would otherwise survive a restart, so they are moved
// once, in place: `roles/fast/*` becomes `roles/flash/*` and `agent/panes_fast` becomes
// `agent/panes_flash`. A value already stored under the new name wins, because it was written by
// this version; the old key is removed either way, so this is a no-op on every later start.
static void migrateFastRoleSettings() {
    QSettings settings;
    for (const QString &field : {QStringLiteral("tier"), QStringLiteral("preset"),
                                 QStringLiteral("model"), QStringLiteral("effort")}) {
        const QString from = QStringLiteral("roles/fast/") + field;
        if (!settings.contains(from)) continue;
        const QString to = QStringLiteral("roles/flash/") + field;
        if (!settings.contains(to)) settings.setValue(to, settings.value(from));
        settings.remove(from);
    }
    if (settings.contains(QStringLiteral("agent/panes_fast"))) {
        if (!settings.contains(QStringLiteral("agent/panes_flash")))
            settings.setValue(QStringLiteral("agent/panes_flash"), settings.value(QStringLiteral("agent/panes_fast")));
        settings.remove(QStringLiteral("agent/panes_fast"));
    }
}

// ----- quitting on a signal ----------------------------------------------------------------------
//
// Qt does nothing about SIGTERM: left alone, `kill`, `pkill relay`, a systemd stop and a logout
// without a session manager all end Relay where it stands — no layout or scrollback saved, the
// workers not told to shut down, and every pane's private /tmp/relay-XXXXXX directory left behind.
// A handler may only do async-signal-safe work, so it writes one byte to a pipe and the event loop
// turns that into an ordinary quit(): aboutToQuit saves, and the destructors clean up.
static int g_quitPipe[2] = {-1, -1};

static void quitSignalHandler(int) {
    const char byte = 1;
    const ssize_t written = ::write(g_quitPipe[1], &byte, 1);
    (void)written;   // a full pipe means a quit is already on its way
}

static void installQuitSignals(QCoreApplication &app) {
    if (::pipe2(g_quitPipe, O_CLOEXEC | O_NONBLOCK) != 0) return;
    auto *notifier = new QSocketNotifier(g_quitPipe[0], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [] {
        char drained[16];
        while (::read(g_quitPipe[0], drained, sizeof drained) > 0) {}
        relay::log::info(QStringLiteral("gui_quit reason=signal"));
        QCoreApplication::quit();
    });
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = quitSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (const int sig : {SIGTERM, SIGINT, SIGHUP}) {
        // Started with the signal ignored (nohup, a launcher): leave it ignored.
        struct sigaction current;
        if (::sigaction(sig, nullptr, &current) == 0 && current.sa_handler == SIG_IGN) continue;
        ::sigaction(sig, &action, nullptr);
    }
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    relay::theme::applyDarkTheme(app);
    QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
    QCoreApplication::setApplicationName(QStringLiteral("relay"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RELAY_VERSION));
    migrateFastRoleSettings();   // "fast" -> "flash", once, before anything reads these keys
    // From here on, stderr is no longer the only record: a launcher-started Relay keeps one too.
    relay::log::installMessageHandler();
    relay::log::info(QStringLiteral("gui_start version=%1 pid=%2 level=%3")
                         .arg(QStringLiteral(RELAY_VERSION)).arg(QCoreApplication::applicationPid())
                         .arg(relay::log::levelName(relay::log::level())));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.relayterminal.Relay"));
    try {
        // Theme icon when installed; the bundled PNG from the source tree or install otherwise.
        const QString root = dataRoot();
        QString png = root + QStringLiteral("/data/icons/hicolor/256x256/apps/org.relayterminal.Relay.png");
        if (!QFileInfo::exists(png)) png = root + QStringLiteral("/../icons/hicolor/256x256/apps/org.relayterminal.Relay.png");
        QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("org.relayterminal.Relay"), QIcon(png)));
    } catch (const std::exception &error) {
        relay::log::info(QStringLiteral("window_icon_unavailable error=%1").arg(QString::fromUtf8(error.what())));
    }
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("Terminal with rich input and BYOK agents."));
    parser.addHelpOption(); parser.addVersionOption();
    QCommandLineOption workspace(QStringList{QStringLiteral("w"), QStringLiteral("workspace")}, QStringLiteral("Initial terminal directory and agent workspace."), QStringLiteral("path"), QDir::currentPath());
    QCommandLineOption clean(QStringLiteral("clean-shell"), QStringLiteral("Do not source ~/.bashrc; useful for incompatible DEBUG/preexec prompt hooks."));
    QCommandLineOption engineCore(QStringLiteral("engine-core"), QStringLiteral("Emulator core of Relay-engine panes: ghostty or libvterm. Also RELAY_ENGINE_CORE."), QStringLiteral("name"));
    // Saved window layout: --fresh ignores it this once (the file itself is kept).
    QCommandLineOption fresh(QStringLiteral("fresh"), QStringLiteral("Start with one new window instead of reopening the saved window layout."));
    parser.addOption(workspace); parser.addOption(clean); parser.addOption(engineCore);
    parser.addOption(fresh); parser.process(app);
    // The emulator core under Relay's engine (docs/ENGINE.md).
    relay::setDefaultEngineCore(relay::resolveEngineCore(parser.value(engineCore), qEnvironmentVariable("RELAY_ENGINE_CORE")));
    const auto path = QFileInfo(parser.value(workspace)).canonicalFilePath();
    if (path.isEmpty() || !QFileInfo(path).isDir()) { QMessageBox::critical(nullptr, QStringLiteral("Relay"), QStringLiteral("Workspace must be an existing directory.")); return 1; }
    QDir::setCurrent(path);
    try {
        // Make relay-open available to Relay shells and to anything they launch.
        const QString scripts = dataRoot() + QStringLiteral("/scripts");
        qputenv("RELAY_OPEN_HELPER", (scripts + QStringLiteral("/relay-open")).toUtf8());
        qputenv("PATH", (scripts + ':' + qEnvironmentVariable("PATH")).toUtf8());
        WindowManager manager(path, parser.isSet(clean));
        manager.setUpLayoutSaving();
        installQuitSignals(app);   // SIGTERM, SIGINT and SIGHUP become an ordinary quit
        // Quit without closing the windows (Ctrl+Q, session logout, a signal) still saves;
        // closing them goes through RelayWindow::closeEvent instead.
        // A quit that never closed a window (a session ending, `relay` told to stop) still saves
        // both halves: the layout and each pane's terminal text.
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&manager] {
            manager.saveScrollbacks();
            manager.saveLayoutNow();
        });
        QTimer::singleShot(1500, &app, [] { registerUrlHandler(); });
        // A crash or `kill -KILL` leaves this Relay's /tmp/relay-XXXXXX directories behind; the
        // owner's /tmp had 876 of them (#9JYK). Sweep the ones whose Relay is gone, well after the
        // first window is up so nothing waits on /tmp, and only say so when something happened.
        QTimer::singleShot(3000, &app, [] {
            const auto swept = relay::runtimedirs::sweep();
            if (swept.removed > 0 || swept.errors > 0)
                relay::log::info(QStringLiteral("runtime_sweep removed=%1 kept_alive=%2 kept_young=%3 errors=%4")
                                     .arg(swept.removed).arg(swept.keptAlive).arg(swept.keptYoung).arg(swept.errors));
        });
        // "Reopen where I left off": on by default, unless --fresh or an explicit --workspace asks
        // for a new window. A fresh profile, an unreadable file or a second Relay opens one window.
        const bool startFresh = parser.isSet(fresh) || parser.isSet(workspace);
        const int reopened = startFresh ? 0 : manager.restoreSavedLayout();
        if (reopened == 0 && !manager.newWindowAt(path)) return 1;
        manager.announceRestore();   // also explains a layout that was deliberately not reopened
        return app.exec();
    } catch (const std::exception &error) {
        QMessageBox::critical(nullptr, QStringLiteral("Relay could not start"), QString::fromUtf8(error.what()));
        return 1;
    }
}

