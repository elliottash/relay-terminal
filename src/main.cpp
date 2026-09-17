// SPDX-License-Identifier: GPL-3.0-or-later
#include "RichEditor.h"
#include "Theme.h"
#include "FilePanes.h"
#include "AgentUi.h"
#include "Completion.h"
#include "Hints.h"
#include "InputPolicy.h"           // prompt-box-only input: where a submitted line goes
#include "TurnTranscript.h"
#include "SkillsDialog.h"
#include "SubagentTranscript.h"   // subagents UI
#include "SubagentsPanel.h"
#include "RequestLedger.h"         // request ledger UI
#include "RequestsPanel.h"
#include "Conversations.h"         // conversation list, search and the Ctrl+F find bar
#include "TerminalBackends.h"
#include "TerminalBackend.h"
#include "WindowState.h"   // saved window layout ("reopen where I left off")
#include <iterator>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
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
#include <QSignalBlocker>
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
#include <QUuid>
#include <QVBoxLayout>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QDirIterator>
#include <QMimeDatabase>
#include <QTextBlock>
#include <sys/syscall.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#ifndef RELAY_VERSION
#define RELAY_VERSION "0.0.0-dev"
#endif
#ifndef RELAY_DATA_DIR
#define RELAY_DATA_DIR "/usr/local/share/relay"
#endif
#ifndef RELAY_SOURCE_DIR
#define RELAY_SOURCE_DIR "."
#endif

static QString dataRoot() {
    const QStringList choices{qEnvironmentVariable("RELAY_DATA_DIR"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/relay"),
        QStringLiteral(RELAY_DATA_DIR), QStringLiteral(RELAY_SOURCE_DIR)};
    for (const auto &path : choices) {
        if (!path.isEmpty() && QFileInfo::exists(path + QStringLiteral("/backend/worker.py")))
            return QDir(path).absolutePath();
    }
    throw std::runtime_error("Relay's backend and shell data files were not found.");
}


// ----- keyboard shortcuts ----------------------------------------------------------------------
//
// Every window-level shortcut is a named action. Defaults live here; overrides live in
// ~/.config/RelayTerminal/relay/keybindings.json as {"version":1,"bindings":{"pane.close":["Ctrl+W"]},
// "program_keys":"shift-only"}. The file reloads automatically, and the agent's set_keybinding
// tool writes the same file.
//
// program_keys decides what happens while a program such as vim runs in the focused terminal:
//   "shift-only" (default): only Ctrl+Shift shortcuts and F-keys act; everything else reaches the program
//   "all": every shortcut acts, even inside programs
//   "none": no shortcut acts while a program has focus
struct ActionDef { QString id, description, category; QStringList defaults; };

class Keymap {
public:
    static Keymap &instance() { static Keymap map; return map; }

    const QList<ActionDef> &actions() const { return m_actions; }
    QString path() const { return m_path; }
    QString programKeys() const { return m_programKeys; }
    QString preset() const { return m_preset; }
    // Built-in presets. "relay" is the Chrome-style default; the others follow their programs'
    // Linux defaults where Relay has an equivalent action (see docs/KEYBINDING-PRESETS.md).
    static QList<QPair<QString, QString>> presets() {
        return {{QStringLiteral("relay"), QStringLiteral("Relay (Chrome-style)")}, {QStringLiteral("warp"), QStringLiteral("Warp")},
                {QStringLiteral("vscode"), QStringLiteral("VS Code")}, {QStringLiteral("konsole"), QStringLiteral("Konsole")}};
    }
    void setPreset(const QString &id) { writeSetting(QStringLiteral("preset"), id); }
    bool hasOverrides() const { return !m_overrides.isEmpty(); }
    void clearOverrides() {
        QJsonObject root = readObject();
        root.insert(QStringLiteral("bindings"), QJsonObject());
        writeObject(root);
    }
    QStringList keysFor(const QString &id) const { return m_bindings.value(id); }
    QString description(const QString &id) const {
        for (const auto &action : m_actions) if (action.id == id) return action.description;
        return id;
    }
    QString shortcutText(const QString &id) const {
        const auto keys = keysFor(id);
        return keys.isEmpty() ? QString() : QKeySequence::fromString(keys.first(), QKeySequence::PortableText).toString(QKeySequence::NativeText);
    }
    QStringList conflicts() const { return m_conflicts; }

    // Returns the action bound to a key event, or an empty string.
    QString match(const QKeyEvent *event) const {
        int key = event->key();
        auto mods = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        if (key == Qt::Key_Backtab) { key = Qt::Key_Tab; mods |= Qt::ShiftModifier; }
        if (key == 0 || key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) return {};
        QString id = m_lookup.value(combo(key, mods));
        // Shifted symbols differ by keyboard layout: "(" needs Shift on US keyboards but not on
        // AZERTY. For symbol keys, accept the binding written with or without Shift.
        const bool symbol = (key >= 0x21 && key <= 0x2f) || (key >= 0x3a && key <= 0x40) || (key >= 0x5b && key <= 0x60) || (key >= 0x7b && key <= 0x7e);
        if (id.isEmpty() && symbol) id = m_lookup.value(combo(key, mods ^ Qt::ShiftModifier));
        return id;
    }

    // True when a shortcut should act even though a program has keyboard focus.
    bool actsInsidePrograms(const QKeyEvent *event) const {
        if (m_programKeys == QStringLiteral("all")) return true;
        if (m_programKeys == QStringLiteral("none")) return false;
        const auto mods = event->modifiers();
        const bool fkey = event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F35;
        // Alt+arrows move between panes. A terminal cannot send Ctrl+Shift+letter to a program, and
        // few TUIs use Alt+arrows, so these stay Relay's while a program owns the keyboard —
        // otherwise a full-screen program (Claude Code, vim) traps the keyboard in its pane
        // (owner report 2026-09-17).
        const bool arrow = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right
                           || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down;
        const bool altArrow = arrow && (mods & Qt::AltModifier) && !(mods & Qt::ControlModifier);
        return fkey || altArrow || ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier));
    }

    void setProgramKeys(const QString &mode) { writeSetting(QStringLiteral("program_keys"), mode); }

    // Preset table for one preset; actions missing from a table use the Relay default.
    static QHash<QString, QStringList> presetTable(const QString &preset) {
        QHash<QString, QStringList> table;
        const QJsonObject all = QJsonDocument::fromJson(presetJson()).object();
        const QJsonObject chosen = all.value(preset).toObject();
        for (auto it = chosen.begin(); it != chosen.end(); ++it) {
            QStringList keys;
            for (const auto &value : it.value().toArray()) keys << value.toString();
            table.insert(it.key(), keys);
        }
        return table;
    }

    // Writes a complete, editable file when none exists yet.
    void ensureFile() {
        if (QFileInfo::exists(m_path)) return;
        // Overrides start empty so switching presets keeps working; "reference" lists every
        // action with its current keys and is ignored when loading.
        QJsonObject reference;
        for (const auto &action : m_actions)
            reference.insert(action.id, QStringLiteral("%1 [%2]").arg(action.description, m_bindings.value(action.id).join(QStringLiteral(", "))));
        writeObject({{"version", 1}, {"preset", m_preset}, {"program_keys", m_programKeys}, {"bindings", QJsonObject()}, {"reference", reference}});
    }

    QJsonObject catalog() const {
        QJsonArray actions;
        for (const auto &action : m_actions)
            actions.append(QJsonObject{{"id", action.id}, {"description", action.description},
                                       {"keys", QJsonArray::fromStringList(m_bindings.value(action.id))}});
        return {{"path", m_path}, {"actions", actions}};
    }

    void listen(QObject *context, std::function<void()> callback) { m_listeners.append({context, std::move(callback)}); }

    void reload() {
        m_bindings.clear(); m_overrides.clear();
        m_programKeys = QStringLiteral("shift-only");
        m_preset = QStringLiteral("relay");
        QJsonObject root;
        QString problem;
        QFile file(m_path);
        if (file.exists()) {
            if (file.open(QIODevice::ReadOnly) && file.size() < 1024 * 1024) {
                QJsonParseError error;
                const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
                if (error.error != QJsonParseError::NoError || !doc.isObject()) problem = QStringLiteral("keybindings.json is not valid JSON; using defaults.");
                else {
                    root = doc.object();
                    const QString mode = root.value(QStringLiteral("program_keys")).toString();
                    if (mode == QStringLiteral("all") || mode == QStringLiteral("none") || mode == QStringLiteral("shift-only")) m_programKeys = mode;
                    const QString preset = root.value(QStringLiteral("preset")).toString();
                    for (const auto &known : presets()) if (known.first == preset) m_preset = preset;
                }
            } else problem = QStringLiteral("keybindings.json could not be read; using defaults.");
        }
        const auto table = presetTable(m_preset);
        for (const auto &action : m_actions) m_bindings.insert(action.id, table.contains(action.id) ? table.value(action.id) : action.defaults);
        const auto bindings = root.value(QStringLiteral("bindings")).toObject();
        for (auto it = bindings.begin(); it != bindings.end(); ++it) {
            // Earlier builds had separate agent and terminal palettes.
            const QString id = it.key() == QStringLiteral("palette.agent") ? QStringLiteral("palette.open") : it.key();
            if (!m_bindings.contains(id)) continue;
            QStringList keys;
            for (const auto &value : it.value().toArray())
                if (!value.toString().trimmed().isEmpty()) keys << value.toString().trimmed();
            m_bindings.insert(id, keys);
            m_overrides.insert(id);
        }
        m_lookup.clear(); m_conflicts.clear();
        for (const auto &action : m_actions) {
            for (const auto &text : m_bindings.value(action.id)) {
                const int code = parse(text);
                if (!code) { m_conflicts << QStringLiteral("%1: unrecognized key \"%2\"").arg(action.id, text); continue; }
                if (m_lookup.contains(code)) m_conflicts << QStringLiteral("%1 and %2 both use %3").arg(m_lookup.value(code), action.id, text);
                else m_lookup.insert(code, action.id);
            }
        }
        if (!problem.isEmpty()) m_conflicts.prepend(problem);
        for (auto &listener : m_listeners) if (listener.first) listener.second();
    }

private:
    Keymap() {
        auto add = [this](const char *id, const char *category, const char *description, QStringList keys) {
            m_actions.append({QString::fromLatin1(id), QString::fromUtf8(description), QString::fromLatin1(category), std::move(keys)});
        };
        add("window.new", "window", "New window", {QStringLiteral("Ctrl+N")});
        add("window.next", "window", "Next Relay window", {QStringLiteral("Alt+Tab")});
        add("window.previous", "window", "Previous Relay window", {QStringLiteral("Alt+Shift+Tab")});
        add("tab.new", "tab", "New tab", {QStringLiteral("Ctrl+T")});
        add("tab.next", "tab", "Next tab", {QStringLiteral("Ctrl+Tab")});
        add("tab.previous", "tab", "Previous tab", {QStringLiteral("Ctrl+Shift+Tab")});
        add("pane.splitRight", "pane", "New pane to the right", {QStringLiteral("Ctrl+P")});
        add("pane.splitDown", "pane", "New pane below", {QStringLiteral("Ctrl+Shift+P")});
        add("pane.focusLeft", "pane", "Focus pane to the left", {QStringLiteral("Alt+Left")});
        add("pane.focusRight", "pane", "Focus pane to the right", {QStringLiteral("Alt+Right")});
        add("pane.focusUp", "pane", "Focus pane above", {QStringLiteral("Alt+Up")});
        add("pane.focusDown", "pane", "Focus pane below", {QStringLiteral("Alt+Down")});
        add("pane.close", "pane", "Close pane, then tab, then window", {QStringLiteral("Ctrl+W")});
        add("pane.moveLeft", "pane", "Move pane left (swap with or dock beside the neighbor)", {QStringLiteral("Ctrl+Alt+Left")});
        add("pane.moveRight", "pane", "Move pane right", {QStringLiteral("Ctrl+Alt+Right")});
        add("pane.moveUp", "pane", "Move pane up", {QStringLiteral("Ctrl+Alt+Up")});
        add("pane.moveDown", "pane", "Move pane down", {QStringLiteral("Ctrl+Alt+Down")});
        add("pane.moveToNewTab", "pane", "Move pane to a new tab (keeps the shell and agent)", {});
        add("tab.moveToNewWindow", "tab", "Move tab to a new window (keeps its panes)", {});
        add("closed.restore", "pane", "Restore the last closed pane, tab or window", {QStringLiteral("Ctrl+Shift+W")});
        add("windows.fresh", "window", "Start a fresh window set (forget the saved window layout)", {});
        add("palette.open", "palette", "Open the Relay actions palette", {QStringLiteral("Ctrl+Shift+A")});
        add("files.explorer", "pane", "Open this pane's folder in an explorer pane", {});
        add("files.open", "pane", "Open a file in a preview pane", {});
        add("control.human", "terminal", "Take control of the terminal (the only way keys reach it; works from the prompt box)", {QStringLiteral("Ctrl+H")});
        add("control.prompt", "terminal", "Back to the Relay prompt (the agent is in control)", {QStringLiteral("Ctrl+Shift+H")});
        add("terminal.native", "terminal", "Toggle native terminal input (keys go straight to the terminal)", {QStringLiteral("F12")});
        add("pane.restartShell", "terminal", "Restart this pane's shell or agent after it stopped", {QStringLiteral("Ctrl+Shift+R")});
        add("terminal.interrupt", "terminal", "Interrupt the running command (Esc)", {});
        add("agent.newChat", "agent", "Start a new agent conversation", {});
        add("agent.clearQueue", "agent", "Clear queued agent prompts", {});
        add("agent.resumeQueue", "agent", "Resume the paused agent queue", {});
        add("agent.stop", "agent", "Stop the agent turn", {});
        add("agent.stopAllSubagents", "agent", "Stop all running subagents", {QStringLiteral("Ctrl+Shift+X")});   // subagents UI
        add("agent.agentsMenu", "agent", "Agents: definitions and running subagents", {});
        add("agent.interrupt", "agent", "Send to the agent; while it is busy, interrupt it and send now (prompt box)",
            {QStringLiteral("Ctrl+Return"), QStringLiteral("Ctrl+Enter"), QStringLiteral("Ctrl+Alt+Return"), QStringLiteral("Ctrl+Alt+Enter")});
        add("agent.provider", "agent", "Provider and API keys", {});
        add("agent.fastAgent", "agent", "Switch this pane between the main agent and the fast agent", {QStringLiteral("Alt+F")});   // model roles
        add("input.modeAuto", "agent", "Input mode: auto detect", {});
        add("input.modeTerminal", "agent", "Input mode: terminal", {});
        add("input.modeAgent", "agent", "Input mode: agent", {});
        add("input.toggle", "agent", "Toggle input between terminal command and agent prompt (from the prompt box)", {QStringLiteral("Ctrl+I")});
        add("agent.planToggle", "agent", "Toggle plan mode (from the prompt box)", {QStringLiteral("Shift+Tab")});
        add("agent.effortUp", "agent", "Raise reasoning effort (from the prompt box)", {QStringLiteral("Alt+.")});
        add("agent.effortDown", "agent", "Lower reasoning effort (from the prompt box)", {QStringLiteral("Alt+,")});
        add("agent.compact", "agent", "Compact the agent conversation", {});
        add("agent.rewind", "agent", "Rewind chat to an earlier turn; files are not changed (Esc Esc in an empty prompt box)", {});
        add("agent.rewindCode", "agent", "Rewind code: restore files the agent changed since an earlier turn (/rewind-code)", {});
        add("agent.fork", "agent", "Fork the conversation into a new pane", {});
        add("agent.resume", "agent", "Resume a saved agent session", {});
        // Conversation list with full-text search, and find-in-view for this pane.
        add("conversations.open", "agent", "Conversations: list and search every saved conversation and Relay's terminal history",
            {QStringLiteral("Ctrl+Shift+O")});
        add("find.inView", "agent", "Find in this pane: the conversation and the terminal scrollback (from the prompt box)",
            {QStringLiteral("Ctrl+F")});
        add("agent.recap", "agent", "Recap this agent session", {});
        add("agent.requests", "agent", "Tasks: show or hide the task list (/tasks)", {QStringLiteral("Ctrl+Shift+K")});
        add("agent.continue", "agent", "Continue the agent turn after a step limit (/continue)", {});
        add("agent.instructions", "agent", "Choose agent instruction files", {});
        add("agent.export", "agent", "Export the conversation as Markdown", {});
        add("help.shortcuts", "palette", "Show all keyboard shortcuts", {QStringLiteral("Ctrl+?"), QStringLiteral("F1")});
        add("keybindings.edit", "terminal", "Edit keyboard shortcuts", {});
        add("keybindings.reload", "terminal", "Reload keyboard shortcuts", {});
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        m_path = dir + QStringLiteral("/keybindings.json");
        QDir().mkpath(dir);
        // Watch the directory too: atomic replacement swaps the file out from under a file watch.
        m_watcher.addPath(dir);
        if (QFileInfo::exists(m_path)) m_watcher.addPath(m_path);
        auto onChange = [this] {
            if (QFileInfo::exists(m_path) && !m_watcher.files().contains(m_path)) m_watcher.addPath(m_path);
            QTimer::singleShot(100, [this] { reload(); });
        };
        QObject::connect(&m_watcher, &QFileSystemWatcher::fileChanged, onChange);
        QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged, onChange);
        reload();
    }

    static int combo(int key, Qt::KeyboardModifiers mods) { return int(mods) | key; }

    static int parse(const QString &text) {
        const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (sequence.count() != 1) return 0;
#if QT_VERSION_MAJOR >= 6
        int key = sequence[0].key(); Qt::KeyboardModifiers mods = sequence[0].keyboardModifiers();
#else
        int key = sequence[0] & ~int(Qt::KeyboardModifierMask); Qt::KeyboardModifiers mods(sequence[0] & int(Qt::KeyboardModifierMask));
#endif
        if (key == Qt::Key_Backtab) { key = Qt::Key_Tab; mods |= Qt::ShiftModifier; }
        if (key == 0 || key == Qt::Key_unknown) return 0;
        return combo(key, mods);
    }

    QJsonObject readObject() const {
        QFile file(m_path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        const auto doc = QJsonDocument::fromJson(file.readAll());
        return doc.isObject() ? doc.object() : QJsonObject();
    }

    void writeSetting(const QString &name, const QString &value) {
        QJsonObject root = readObject();
        if (!root.contains(QStringLiteral("version"))) root.insert(QStringLiteral("version"), 1);
        root.insert(name, value);
        writeObject(root);
    }

    void writeObject(const QJsonObject &root) {
        QSaveFile out(m_path);
        if (!out.open(QIODevice::WriteOnly)) return;
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (out.commit()) QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner);
        reload();
    }

    QList<ActionDef> m_actions;
    QHash<QString, QStringList> m_bindings;
    QHash<int, QString> m_lookup;
    QStringList m_conflicts;
    QString m_path, m_programKeys = QStringLiteral("shift-only"), m_preset = QStringLiteral("relay");
    QSet<QString> m_overrides;

    // Filled from docs/KEYBINDING-PRESETS.md research. Missing actions fall back to Relay defaults.
    static QByteArray presetJson() {
        return QByteArrayLiteral(R"PRESETS({"relay":{},"warp":{"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+T"],"tab.next":["Ctrl+PgDown","Ctrl+Tab"],"tab.previous":["Ctrl+PgUp","Ctrl+Shift+Tab"],"pane.splitRight":["Ctrl+Shift+D"],"pane.splitDown":["Ctrl+Shift+E"],"pane.focusLeft":["Ctrl+Alt+Left"],"pane.focusRight":["Ctrl+Alt+Right"],"pane.focusUp":["Ctrl+Alt+Up"],"pane.focusDown":["Ctrl+Alt+Down"],"pane.moveLeft":[],"pane.moveRight":[],"pane.moveUp":[],"pane.moveDown":[],"pane.close":["Ctrl+Shift+W"],"closed.restore":["Ctrl+Alt+T"],"palette.open":["Ctrl+Shift+P"],"terminal.native":["F12"],"terminal.interrupt":[],"agent.newChat":["Ctrl+Shift+Y"],"agent.stop":[],"agent.provider":[],"input.modeAuto":[],"input.modeTerminal":["Ctrl+Shift+I"],"input.modeAgent":[],"input.toggle":["Ctrl+I"],"keybindings.edit":["Ctrl+,"],"keybindings.reload":[],"agent.requests":[]},"vscode":{"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+~"],"tab.next":["Ctrl+PgDown","Ctrl+Tab"],"tab.previous":["Ctrl+PgUp","Ctrl+Shift+Tab"],"pane.splitRight":["Ctrl+Shift+%","Ctrl+\\"],"pane.splitDown":["Ctrl+Shift+|"],"pane.focusLeft":["Alt+Left"],"pane.focusRight":["Alt+Right"],"pane.focusUp":["Alt+Up"],"pane.focusDown":["Alt+Down"],"pane.close":["Ctrl+W"],"closed.restore":["Ctrl+Shift+T"],"palette.open":["Ctrl+Shift+P"],"terminal.native":["Ctrl+`","F12"],"terminal.interrupt":[],"agent.newChat":["Ctrl+N"],"agent.stop":["Ctrl+Esc"],"agent.provider":["Ctrl+Alt+."],"input.modeAuto":[],"input.modeTerminal":[],"input.modeAgent":["Ctrl+Shift+Alt+I"],"keybindings.edit":["Ctrl+,"],"keybindings.reload":[],"agent.requests":[]},"konsole":{"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+T"],"tab.next":["Ctrl+PgDown"],"tab.previous":["Ctrl+PgUp"],"pane.splitRight":["Ctrl+Shift+(","Ctrl+("],"pane.splitDown":["Ctrl+Shift+)","Ctrl+)"],"pane.focusLeft":["Ctrl+Shift+Left"],"pane.focusRight":["Ctrl+Shift+Right"],"pane.focusUp":["Ctrl+Shift+Up"],"pane.focusDown":["Ctrl+Shift+Down"],"pane.close":["Ctrl+Shift+W"],"closed.restore":[],"palette.open":["Ctrl+Alt+I"],"terminal.native":["F12"],"terminal.interrupt":[],"agent.newChat":[],"agent.stop":[],"agent.provider":[],"input.modeAuto":[],"input.modeTerminal":[],"input.modeAgent":[],"keybindings.edit":["Ctrl+Alt+,"],"keybindings.reload":[],"agent.requests":[]}})PRESETS");
    }
    QFileSystemWatcher m_watcher;
    QList<QPair<QPointer<QObject>, std::function<void()>>> m_listeners;
};

// ----- per-pane process isolation ----------------------------------------------------------
//
// Each pane's shell and agent worker run in their own transient systemd user scope with memory
// limits, so a runaway command is stopped inside its pane instead of taking down Relay (the kernel
// OOM killer and systemd-oomd otherwise act on Relay's whole app cgroup). `systemd-run --scope`
// execs the command in place, so the PID Relay tracks is still bash's / python's own.
namespace isolation {

inline bool enabled() { return QSettings().value(QStringLiteral("isolation/enabled"), true).toBool(); }

// systemd-run exists and the user manager accepts transient scopes; probed once per process.
inline bool available() {
    static int state = -1;
    if (state < 0) {
        state = 0;
        const QString tool = QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
        if (!tool.isEmpty()) {
            QProcess probe;
            probe.start(tool, {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"), QStringLiteral("--"), QStringLiteral("true")});
            if (probe.waitForFinished(3000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) state = 1;
            else probe.kill();
        }
    }
    return state == 1;
}

// A systemd size such as "8G", "512M" or "infinity"; anything else falls back to the default.
inline QString memory(const char *key, const char *fallback) {
    const QString value = QSettings().value(QString::fromLatin1(key), QString::fromLatin1(fallback)).toString().trimmed();
    static const QRegularExpression valid(QStringLiteral("^(\\d+[KMGT]?|infinity)$"));
    return valid.match(value).hasMatch() ? value : QString::fromLatin1(fallback);
}

// Arguments that run `command` inside the named scope.
inline QStringList wrap(const QString &unit, const QStringList &properties, const QStringList &command) {
    QStringList args{QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"), QStringLiteral("--unit=") + unit};
    for (const QString &property : properties) args << QStringLiteral("-p") << property;
    args << QStringLiteral("--") << command;
    return args;
}

// systemd's result for a finished scope ("oom-kill" when memory limits or systemd-oomd stopped it).
// Failed scopes stay loaded until reset, which also lets the name be reused.
inline QString takeResult(const QString &unit) {
    if (unit.isEmpty()) return {};
    QProcess show;
    show.start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("show"), unit + QStringLiteral(".scope"), QStringLiteral("-p"), QStringLiteral("Result"), QStringLiteral("--value")});
    QString result;
    if (show.waitForFinished(2000)) result = QString::fromUtf8(show.readAllStandardOutput()).trimmed();
    else show.kill();
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("reset-failed"), unit + QStringLiteral(".scope")});
    return result;
}

// memory.events oom_kill counter of the cgroup `pid` belongs to, or -1.
inline long oomKills(int pid) {
    if (pid <= 0) return -1;
    QFile cgroup(QStringLiteral("/proc/%1/cgroup").arg(pid));
    if (!cgroup.open(QIODevice::ReadOnly)) return -1;
    const QString line = QString::fromUtf8(cgroup.readLine()).trimmed();   // "0::/user.slice/..."
    const int split = line.indexOf(QStringLiteral("::"));
    if (split < 0) return -1;
    QFile events(QStringLiteral("/sys/fs/cgroup") + line.mid(split + 2) + QStringLiteral("/memory.events"));
    if (!events.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray &row : events.readAll().split('\n'))
        if (row.startsWith("oom_kill ")) return row.mid(9).trimmed().toLong();
    return -1;
}

}  // namespace isolation

// Case-insensitive subsequence score; 0 means no match. Contiguous and earlier matches score higher.
static int relayFuzzyScore(const QString &needle, const QString &haystack) {
    if (needle.isEmpty()) return 1;
    const QString n = needle.toLower(), h = haystack.toLower();
    const int direct = h.indexOf(n);
    if (direct >= 0) return 10000 - direct;
    int pos = 0, first = -1, last = -1;
    for (const QChar c : n) {
        pos = h.indexOf(c, pos);
        if (pos < 0) return 0;
        if (first < 0) first = pos;
        last = pos++;
    }
    return std::max(1, 5000 - (last - first) * 10 - first);
}

// One row of the combined queue: a colored kind icon ($ terminal, ✦ agent), the text, and a
// remove × at the right edge.
class QueueRowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save();
        const QRect r = option.rect;
        if (option.state & QStyle::State_Selected) painter->fillRect(r, relay::theme::SurfaceRaised.lighter(135));
        const bool agent = index.data(Qt::UserRole + 1).toBool();
        painter->setFont(option.font);
        painter->setPen(agent ? relay::theme::Accent : QColor(0xe5, 0xc0, 0x7b));
        painter->drawText(QRect(r.left() + 4, r.top(), 18, r.height()), Qt::AlignCenter, agent ? QStringLiteral("✦") : QStringLiteral("$"));
        painter->setPen(relay::theme::Text);
        const QString text = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString().simplified(), Qt::ElideRight, std::max(20, r.width() - 54));
        painter->drawText(QRect(r.left() + 26, r.top(), r.width() - 54, r.height()), Qt::AlignVCenter | Qt::AlignLeft, text);
        painter->setPen(relay::theme::TextMuted);
        painter->drawText(QRect(r.right() - 22, r.top(), 18, r.height()), Qt::AlignCenter, QStringLiteral("×"));
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        return {QStyledItemDelegate::sizeHint(option, index).width(), option.fontMetrics.height() + 8};
    }
};

// One terminal pane: a shell behind relay::TerminalBackend (KonsolePart by default, Relay's
// own engine with --engine=relay), its Bash bridge, a composer, and its own agent worker and
// conversation. Windows arrange panes in tabs and splits; the toolbar acts on the active pane.
class Pane final : public QWidget {
public:
    struct QueueEntry { quint64 id = 0; bool agent = false, fix = false, watch = false; QString text, why; QJsonArray attachments; };
    // Why the prompt box is hidden, so it can come back by itself when the reason ends.
    enum class HideReason { None, AltScreen, Remote, Manual };
    Pane(const QString &workspace, const QString &cwd, bool cleanShell,
         relay::EngineKind engine = relay::defaultEngineKind(), const QString &engineCore = relay::defaultEngineCore())
        : m_workspace(workspace), m_cwd(cwd.isEmpty() ? workspace : cwd), m_cleanShell(cleanShell) {
        m_engine = engine;
        m_engineCore = engineCore;
        m_data = dataRoot();
        m_python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (m_python.isEmpty()) throw std::runtime_error("Python 3 is required.");
        if (!m_runtime.isValid()) throw std::runtime_error("Could not create a private shell runtime directory.");
        QFile::setPermissions(m_runtime.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        buildUi();
        startWorker();
        startTerminal(cleanShell);
        connect(&m_poll, &QTimer::timeout, this, [this] { pollShell(); });
        connect(&m_secretPoll, &QTimer::timeout, this, [this] { checkPasswordPrompt(); checkOomKills(); });
        m_secretPoll.start(1000);
        m_poll.start(80);
        m_debounce.setSingleShot(true);
        m_debounce.setInterval(150);
        m_idleTip.setSingleShot(true);
        m_idleTip.setInterval(4000);
        connect(&m_idleTip, &QTimer::timeout, this, [this] { showIdleTip(); });
        m_assistDebounce.setSingleShot(true); m_assistDebounce.setInterval(300);
        connect(&m_assistDebounce, &QTimer::timeout, this, [this] {
            // Only the current text is checked; stale previews are dropped.
            if (m_assistQueuedText == m_editor->toPlainText() && m_assistInflightText != m_assistQueuedText
                && !(m_assistText == m_assistQueuedText && !m_assistRoute.isEmpty()))
                sendRouteAssist(m_assistQueuedText);
        });
        m_assistHold.setSingleShot(true); m_assistHold.setInterval(400);
        connect(&m_assistHold, &QTimer::timeout, this, [this] { releaseHeldDecision(); });
        connect(&m_debounce, &QTimer::timeout, this, [this] { requestRoute(false, QStringLiteral("auto")); });
        connect(m_editor, &QPlainTextEdit::textChanged, this, [this] { m_debounce.start(); onComposerEdited(); });
        connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this] { updateGhost(); });
        // While a command runs: password prompts, answered passwords, and programs waiting for input.
        m_programPoll.setInterval(250);
        connect(&m_programPoll, &QTimer::timeout, this, [this] { pollProgram(); });
        m_editor->onSubmit = [this](const QString &destination) { requestRoute(true, destination); };
        qApp->installEventFilter(this);
        QTimer::singleShot(5000, this, [this] {
            if (!m_seenShell && m_backend) {
                setNative(true);
                status(QStringLiteral("Shell integration did not initialize. Native terminal remains available; try --clean-shell."));
            }
        });
    }

    ~Pane() override {
        m_closing = true;
        delete m_subagentOverlay.data();   // subagents UI: its destroyed() handler uses members
        qApp->removeEventFilter(this);
        m_poll.stop();
        // Destroy the terminal before its private shell state directory is removed.
        m_backend = nullptr;
        m_backendOwned.reset();
        if (m_worker.state() != QProcess::NotRunning) {
            send({{"type", "cancel"}});
            send({{"type", "shutdown"}});
            m_worker.closeWriteChannel();
            if (!m_worker.waitForFinished(1500)) {
                m_worker.kill();
                m_worker.waitForFinished(1000);
            }
        }
    }

    // ----- interface used by RelayWindow ----------------------------------------------------
    std::function<void(const QString &)> onStatus;
    std::function<void()> onStateChanged;   // cwd, model list, native mode or busy state changed
    std::function<void()> onShellExited;
    std::function<void(const QString &)> onOpenPath;   // open a folder or file in a Relay pane
    std::function<void(const QString &turnId)> onOpenTurn;   // "✦ N tool calls" link or palette

    QString cwd() const { return m_cwd; }
    QString sessionToken() const { return m_token; }
    QString workspace() const { return m_workspace; }
    bool cleanShell() const { return m_cleanShell; }
    relay::EngineKind engine() const { return m_engine; }
    QString engineCore() const { return m_engineCore; }
    QString mode() const { return m_modeValue; }
    void setMode(const QString &mode) { m_modeValue = mode; requestRoute(false, QStringLiteral("auto")); changed(); }
    bool isNative() const { return m_native; }
    void toggleNative() { setNative(!m_native); }
    bool agentBusy() const { return m_agentBusy; }
    bool processBusy() const {
        return m_backend && foregroundPid() > 0 && foregroundPid() != shellPid();
    }
    // The pane's shell and the process group in the terminal's foreground, through whichever
    // engine this pane uses. 0 when there is no terminal.
    int shellPid() const { return m_backend ? int(m_backend->shellPid()) : 0; }
    int foregroundPid() const { return m_backend ? int(m_backend->foregroundProcessId()) : 0; }
    void sendShellInput(const QString &text) { if (m_backend) m_backend->sendText(text, false); }
    QList<QPair<QString, QString>> storedModels() const { return m_stored; }
    QString currentPreset() const { return m_currentPreset; }
    QString model() const { return m_model; }
    QString sessionId() const { return m_sessionId; }

    // ----- saved window layout ("reopen where I left off", src/WindowState.h) ------------------
    // Values a restored pane starts with, taken from the saved layout before its first `configure`.
    // Nothing is re-run: the shell starts in the old directory and the conversation is reattached.
    void initRestore(const QJsonObject &spec) {
        const QString preset = spec.value(QStringLiteral("preset")).toString();
        if (!preset.isEmpty()) m_restorePreset = preset;
        const QString effort = spec.value(QStringLiteral("effort")).toString();
        if (efforts().contains(effort)) m_effort = effort;
        const QString agentMode = spec.value(QStringLiteral("agent_mode")).toString();
        if (agentMode == QStringLiteral("plan") || agentMode == QStringLiteral("build")) m_agentMode = agentMode;
        const QString input = spec.value(QStringLiteral("input_mode")).toString();
        if (input == QStringLiteral("auto") || input == QStringLiteral("shell") || input == QStringLiteral("agent")) m_modeValue = input;
        m_restoreSession = spec.value(QStringLiteral("session_id")).toString();
        changed();
    }
    void focusInput() {
        if (m_native) { focusTerminal(); return; }
        if (m_secretMode) { m_secretEdit->setFocus(Qt::OtherFocusReason); return; }
        m_editor->setFocus(Qt::OtherFocusReason);
    }
    // The prompt box is masked and answers a password prompt (see checkPasswordPrompt()).
    bool secretMode() const { return m_secretMode; }

    void interruptShell() {
        if (m_backend) { m_loading = false; m_promptReported = false; clearFix(); sendShellInput(QString(QChar(3))); focusTerminal(); }
    }
    void newChat() {
        if (m_agentBusy) { status(QStringLiteral("Stop the current agent turn first.")); return; }
        send({{"type", "reset"}}); clearFix();
        printInline(QStringLiteral("New agent conversation\n"), Ink::Note); closeInline();
    }
    int queuedPrompts() const { return m_entries.size(); }
    bool queuePaused() const { return m_entriesPaused; }
    void clearAgentQueue() {
        m_entries.clear(); m_entriesPaused = false; m_resubmitAtFront = false; m_selected = -1;
        rebuildQueueStrip(); changed();
    }
    void resumeAgentQueue() {
        m_entriesPaused = false; m_resubmitAtFront = false; m_pauseReason.clear();
        send({{"type", "resume_queue"}});
        rebuildQueueStrip(); changed();
        pumpQueue();
    }
    // Ctrl+Enter: send to the agent. While the agent is busy, stop the current turn and send now.
    void interruptAgentWithPrompt() {
        const QString text = m_editor->toPlainText().trimmed();
        if (!m_agentBusy) {
            if (text.isEmpty()) { status(QStringLiteral("Type a prompt first.")); return; }
            requestRoute(true, QStringLiteral("agent"));
            return;
        }
        if (text.isEmpty()) { status(QStringLiteral("Type a prompt first; Ctrl+Enter interrupts the agent with it.")); return; }
        submitAgent(text, true, QString(), QStringLiteral("interrupt"));
    }
    void stopAgent() {
        send({{"type", "cancel"}}); clearFix();
        status(QStringLiteral("Stopping. Commands that already ran may have changed files; a network read can take up to its timeout to stop."));
    }
    void selectModel(const QString &id) {
        if (id.startsWith(QStringLiteral("role:"))) return;   // the pane's own role chip entry
        // Picking a model from the chip puts the pane back on the main agent (protocol 13).
        if (m_agentRole != QStringLiteral("main")) { setAgentRole(QStringLiteral("main")); if (id == m_currentPreset) return; }
        if (id.isEmpty() || id == m_currentPreset) return;
        if (m_agentBusy) { status(QStringLiteral("Stop the current agent turn before switching models.")); changed(); return; }
        const auto preset = presetById(id);
        if (!m_configured || preset.isEmpty()) { configurePreset(id, true); return; }
        // Switch the provider between turns; the conversation is kept.
        send({{"type", "set_model"}, {"preset", id}, {"use_stored_key", true},
              {"base_url", preset.value(QStringLiteral("base_url")).toString()},
              {"model", preset.value(QStringLiteral("model")).toString()},
              {"extra", preset.value(QStringLiteral("extra")).toObject()},
              {"max_tokens", QSettings().value(QStringLiteral("provider/max_tokens"), 8192).toInt()}});
        QSettings().setValue(QStringLiteral("provider/preset"), id);
        m_currentPreset = id; changed();
    }
    void openProviderDialog() { configure(); }

    // ----- model roles (protocol 13) -------------------------------------------------------
    // Every role defaults to "same as the main agent". Settings live under roles/<id>/{preset,model,effort};
    // a role is unset when it has no preset. The worker resolves keys and per-provider defaults.
    static QStringList roleIds() {
        return {QStringLiteral("terminal_use"), QStringLiteral("subagent"), QStringLiteral("switchboard"),
                QStringLiteral("fast"), QStringLiteral("chores"), QStringLiteral("vision"),
                QStringLiteral("route_assist")};
    }
    static QString roleLabel(const QString &role) {
        static const QHash<QString, QString> labels{
            {QStringLiteral("main"), QStringLiteral("Main agent")},
            {QStringLiteral("terminal_use"), QStringLiteral("Terminal-use agent")},
            {QStringLiteral("subagent"), QStringLiteral("Subagent")},
            {QStringLiteral("switchboard"), QStringLiteral("Switchboard agent")},
            {QStringLiteral("fast"), QStringLiteral("Fast agent")},
            {QStringLiteral("chores"), QStringLiteral("Chores")},
            {QStringLiteral("vision"), QStringLiteral("Vision")},
            {QStringLiteral("route_assist"), QStringLiteral("Route assist")}};
        return labels.value(role, role);
    }
    static QString roleSetting(const QString &role, const QString &field) {
        return QStringLiteral("roles/") + role + '/' + field;
    }
    // The `roles` object of configure / set_agent_options; empty when every role follows the main agent.
    static QJsonObject rolesObject() {
        QSettings settings;
        QJsonObject roles;
        for (const QString &role : roleIds()) {
            const QString preset = settings.value(roleSetting(role, QStringLiteral("preset"))).toString();
            if (preset.isEmpty()) continue;   // same as the main agent
            QJsonObject entry{{"preset", preset}};
            const QString model = settings.value(roleSetting(role, QStringLiteral("model"))).toString().trimmed();
            if (!model.isEmpty()) entry.insert(QStringLiteral("model"), model);
            const QString effort = settings.value(roleSetting(role, QStringLiteral("effort"))).toString();
            if (efforts().contains(effort)) entry.insert(QStringLiteral("effort"), effort);
            roles.insert(role, entry);
        }
        return roles;
    }
    static bool newPanesUseFastAgent() {
        return QSettings().value(QStringLiteral("agent/panes_fast"), true).toBool();
    }
    QString agentRole() const { return m_agentRole; }
    // The model a role resolves to, as last reported by the worker.
    QString roleModel(const QString &role) const {
        return m_roleSummary.value(role).toObject().value(QStringLiteral("model")).toString();
    }
    void setAgentRole(const QString &role, bool announce = true) {
        if (role == m_agentRole) return;
        if (m_agentBusy) { status(QStringLiteral("Stop the current agent turn before switching the pane's agent.")); return; }
        m_agentRole = role;
        if (m_configured) send({{"type", "set_agent_role"}, {"role", role}});
        if (announce) {
            const QString model = roleModel(role);
            toast(role == QStringLiteral("main") ? QStringLiteral("Main agent for this pane")
                                                 : QStringLiteral("Fast agent for this pane%1").arg(model.isEmpty() ? QString() : QStringLiteral(" · ") + model));
        }
        changed();
    }
    // Before the first configure: the role a new or restored pane starts with.
    void initAgentRole(const QString &role) { if (!m_configured) m_agentRole = role; }
    void toggleFastAgent() {
        setAgentRole(m_agentRole == QStringLiteral("fast") ? QStringLiteral("main") : QStringLiteral("fast"));
    }
    // Live update after the Roles section changed (applies to side calls and new subagents at once).
    void rolesChanged() {
        if (m_configured) send({{"type", "set_agent_options"}, {"roles", rolesObject()}});
    }
    void toggleInputMode() {
        // Ctrl+I at a password prompt leaves masked input and talks to the agent instead
        // (issue decision 4), e.g. "paste the password from my clipboard". The agent's typing
        // into the program follows the normal control rules and is not masked.
        if (m_secretMode) {
            m_secretDeclined = true;
            leaveSecretMode();
            setMode(QStringLiteral("agent"));
            toast(QStringLiteral("Input: Agent · the password prompt is still waiting"));
            return;
        }
        const QString next = m_modeValue == QStringLiteral("agent") ? QStringLiteral("shell") : QStringLiteral("agent");
        setMode(next);
        toast(next == QStringLiteral("agent") ? QStringLiteral("Input: Agent") : QStringLiteral("Input: Terminal"));
    }

    // ----- control policy for programs --------------------------------------------------
    // "agent" (the default since the prompt box became the only input): a full-screen or remote
    // program leaves the keyboard in the prompt box and the pane offers "Take control".
    // "human": the old behaviour, Relay switches to native input by itself.
    static QString defaultControl() {
        return QSettings().value(QStringLiteral("control/default"), QStringLiteral("agent")).toString() == QStringLiteral("human")
            ? QStringLiteral("human") : QStringLiteral("agent");
    }
    static QString programControl(const QString &program) {
        const QString value = QSettings().value(QStringLiteral("control/programs")).toMap().value(program).toString();
        return value == QStringLiteral("agent") || value == QStringLiteral("human") ? value : QString();
    }
    static void setProgramControl(const QString &program, const QString &value) {
        QSettings settings;
        QVariantMap map = settings.value(QStringLiteral("control/programs")).toMap();
        if (value.isEmpty()) map.remove(program); else map.insert(program, value);
        settings.setValue(QStringLiteral("control/programs"), map);
    }
    static QString controlFor(const QString &program) {
        const QString override = program.isEmpty() ? QString() : programControl(program);
        return override.isEmpty() ? defaultControl() : override;
    }

    // Command line of the program in the terminal's foreground, or empty at the shell prompt.
    QString foregroundCommandLine() const {
        if (!m_backend) return {};
        const int shell = shellPid();
        long group = shell > 0 ? foregroundGroup(shell) : -1;
        if (group <= 0 || group == shell) {
            const int fallback = foregroundPid();
            if (fallback <= 0 || fallback == shell) return {};
            group = fallback;
        }
        QFile file(QStringLiteral("/proc/%1/cmdline").arg(group));
        if (!file.open(QIODevice::ReadOnly)) return {};
        QByteArray raw = file.read(4096);
        while (raw.endsWith('\0')) raw.chop(1);
        QString line = QString::fromLocal8Bit(raw.replace('\0', ' ')).simplified();
        if (line.size() > 200) line = line.left(200) + QStringLiteral("…");
        return line;
    }
    QString foregroundProgramName() const {
        const QString line = foregroundCommandLine();
        return line.isEmpty() ? QString() : QFileInfo(line.section(' ', 0, 0)).fileName();
    }

    bool ownsComposerWidget(QWidget *widget) const { return m_composer && widget && (widget == m_composer || m_composer->isAncestorOf(widget)); }

    // Human control (Ctrl+H, F12, the "Take control" button): the only way the terminal widget
    // ever gets the keyboard. Everything else keeps it in the prompt box.
    void takeControl() {
        // During a running program the prompt returns when it exits; at an idle shell you stay in control.
        m_autoHuman = processBusy() || !m_promptReported;
        if (!m_native) setNative(true);
        // A full-screen or remote program hands the prompt box back by itself when it exits.
        m_hideReason = m_altScreen ? HideReason::AltScreen : m_remoteProgram ? HideReason::Remote : HideReason::Manual;
        toast(QStringLiteral("You're in control · %1 for the prompt").arg(Keymap::instance().shortcutText(QStringLiteral("control.prompt"))));
    }

    // Back to the prompt. While a program runs, the prompt talks to the agent, which is in control.
    void showPrompt() {
        m_autoHuman = false;
        if (m_native) setNative(false, !processBusy());
        focusInput();
        if (!m_opaqueProgram.isEmpty()) { refreshProgramHint(); return; }
        if (processBusy())
            toast(QStringLiteral("Agent in control · %1 to take control").arg(Keymap::instance().shortcutText(QStringLiteral("control.human"))));
    }

    bool ownsTerminalWidget(QWidget *widget) const { return m_terminal && widget && (widget == m_terminal || m_terminal->isAncestorOf(widget)); }
    // Widgets inside the terminal that are meant to be typed in (the Relay engine's Find bar)
    // keep the focus; the terminal surface itself never does while the prompt box is shown.
    static bool acceptsTypedInput(const QWidget *widget) {
        return widget
            && (widget->inherits("QLineEdit") || widget->inherits("QAbstractButton") || widget->inherits("QComboBox")
                || widget->inherits("QAbstractItemView") || widget->inherits("QAbstractSpinBox"));
    }
    // What this pane's engine can do (engine/TerminalBackend.h). Actions that need a
    // capability are only offered when the pane's backend reports it.
    bool terminalCan(int capability) const { return m_backend && (m_backend->capabilities() & capability); }
    bool jumpToPrompt(int direction) { return m_backend && m_backend->scrollToPrompt(direction); }
    int findInTerminal(const QString &text, bool backwards) { return m_backend ? m_backend->find(text, backwards) : 0; }
    void clearTerminal() {
        if (!m_backend) return;
        closeInline();
        m_backend->clear();
        if (shellIdleAtPrompt()) m_backend->redrawPrompt();
    }
    QString engineLabel() const {
        return m_engine == relay::EngineKind::Relay
            ? (m_engineCore.isEmpty() ? QStringLiteral("Relay engine") : QStringLiteral("Relay engine (%1)").arg(m_engineCore))
            : QStringLiteral("KonsolePart");
    }
    bool runCommand(const QString &command) { return runInTerminal(command, false, 0); }
    void sendKeybindings() { if (m_configured) send(QJsonObject{{"type", "keybindings"}, {"path", Keymap::instance().path()}, {"actions", Keymap::instance().catalog().value(QStringLiteral("actions"))}}); }
    void importWarpKeys() { send({{"type", "import_warp"}}); }

    // ===== agent sessions UI: model/effort, context, plan mode, rewind, fork, resume, recaps, =====
    // ===== instructions, suggestions and steering (docs/AGENT-SESSIONS-PROTOCOL.md)          =====
    std::function<void(const QString &path, Pane *owner)> onPlanWritten;   // open the plan in an editable pane
    std::function<void(const QString &path)> onOpenDocument;               // open e.g. relay.md in an editable pane
    std::function<void(const QJsonObject &state, const QString &title)> onForkState;
    // Conversation list, Shift+Enter: open an existing saved conversation in a new pane.
    std::function<void(const QJsonObject &state, const QString &title)> onOpenSessionInNewPane;
    std::function<void()> onShowAgents;                                    // subagents panel (GUI E2), if present

    static QStringList efforts() { return {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("max")}; }
    QString effort() const { return m_effort; }
    QString agentMode() const { return m_agentMode; }
    // `fork` false: the state is an existing conversation opened in this pane (the conversation
    // list's Shift+Enter), so the pane reports "Session loaded", not "Forked from".
    void setInitialState(const QJsonObject &state, const QString &title, bool fork = true) {
        m_initialState = state; m_forkTitle = title; m_initialIsFork = fork;
    }

    void setEffort(const QString &value) {
        if (!efforts().contains(value)) return;
        QSettings().setValue(QStringLiteral("agent/effort"), value);
        m_effort = value;
        if (m_configured) send({{"type", "set_effort"}, {"effort", value}});
        changed();
        toast(QStringLiteral("Effort: ") + value);
    }
    void effortStep(int delta) {
        int index = efforts().indexOf(m_effort);
        if (index < 0) index = 2;
        index = std::clamp(index + delta, 0, int(efforts().size()) - 1);
        if (efforts().at(index) != m_effort) setEffort(efforts().at(index));
    }

    void setAgentMode(const QString &mode) {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        send({{"type", "set_mode"}, {"mode", mode}});
    }
    void togglePlanMode() { setAgentMode(m_agentMode == QStringLiteral("plan") ? QStringLiteral("build") : QStringLiteral("plan")); }

    void compactNow(const QString &focus = QString()) {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        if (m_agentBusy) { status(QStringLiteral("Wait for the agent turn to finish before compacting.")); return; }
        QJsonObject request{{"type", "compact"}};
        if (!focus.trimmed().isEmpty()) request.insert(QStringLiteral("focus"), focus.trimmed());
        send(request);
    }
    // Rewind chat (default: /rewind, Esc Esc) restores only the conversation and never touches
    // files. Rewind code (/rewind-code) restores the agent's file changes after a confirmation.
    void openRewind(const QString &kind = QStringLiteral("chat")) {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        if (m_agentBusy) { status(QStringLiteral("Stop the agent turn before rewinding.")); return; }
        m_rewindPending = true;
        m_rewindKind = kind;
        send({{"type", "checkpoints"}});
    }
    void requestFork(int turn = -1) {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        if (m_agentBusy) { status(QStringLiteral("Wait for the agent turn to finish before forking.")); return; }
        QJsonObject request{{"type", "fork"}};
        if (turn >= 0) request.insert(QStringLiteral("turn"), turn);
        m_forkPending = true;
        send(request);
    }
    void openResume() {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        m_resumePending = true;
        send({{"type", "sessions"}});
    }
    void requestRecap() {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        m_recapManual = true;
        send({{"type", "recap_request"}, {"reason", "manual"}});
        status(QStringLiteral("Writing a recap…"));
    }
    void openInstructions() {
        if (!m_workerReady) return;
        m_instructionsDialogPending = true;
        send({{"type", "scan_instructions"}, {"workspace", m_workspace}});
    }
    void executePlan(const QString &path, bool fresh) {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        if (fresh && m_agentBusy) { status(QStringLiteral("Stop the agent turn before executing in a fresh context.")); return; }
        send({{"type", "plan_execute"}, {"path", path}, {"fresh", fresh},
              {"when", m_agentBusy ? QStringLiteral("queue") : QStringLiteral("now")}});
        ensureLineStart();
        printInline(QStringLiteral("› Execute the plan %1%2\n").arg(QFileInfo(path).fileName(), fresh ? QStringLiteral(" (fresh context)") : QString()), Ink::User);
        focusInput();
    }
    void keepPlanning() {
        focusInput();
        toast(QStringLiteral("Still planning · tell the agent what to change"));
    }

    // Settings changed in Actions › Agent options.
    void agentOptionsChanged(const QString &key) {
        if (key == QStringLiteral("agent/max_auto_turns")) {
            if (m_configured) send({{"type", "set_agent_options"}, {"max_auto_turns", QSettings().value(QStringLiteral("agent/max_auto_turns"), 50).toInt()}});
            return;
        }
        // Model roles apply to side calls and new subagents at once (protocol 13).
        if (key.startsWith(QStringLiteral("roles/"))) { rolesChanged(); return; }
        if (key == QStringLiteral("agent/panes_fast")) return;   // only affects panes opened later
        // Turn limits and the request audit apply to the running agent at once (protocol 12.1).
        if (key == QStringLiteral("agent/max_steps") || key == QStringLiteral("agent/max_tool_calls") || key == QStringLiteral("agent/audit_requests")) {
            if (m_configured) {
                QJsonObject request{{"type", "set_agent_options"}};
                const QJsonObject options = requestOptions();
                for (auto it = options.begin(); it != options.end(); ++it) request.insert(it.key(), it.value());
                send(request);
            }
            return;
        }
        applyConfigureChange(QStringLiteral("Agent options saved"));
    }

    // Export the saved conversation of this pane as Markdown and open it in a preview pane.
    void exportConversation() {
        if (m_sessionDir.isEmpty() || m_sessionId.isEmpty()) { status(QStringLiteral("Nothing to export yet.")); return; }
        QFile file(m_sessionDir + '/' + m_sessionId + QStringLiteral(".json"));
        if (!file.open(QIODevice::ReadOnly)) { status(QStringLiteral("Nothing to export yet: the conversation has not been saved.")); return; }
        const QJsonObject data = QJsonDocument::fromJson(file.readAll()).object();
        QString markdown = QStringLiteral("# ") + (data.value(QStringLiteral("title")).toString().isEmpty() ? QStringLiteral("Relay conversation") : data.value(QStringLiteral("title")).toString())
            + QStringLiteral("\n\nModel: %1 · exported %2\n").arg(data.value(QStringLiteral("model")).toString(), QDateTime::currentDateTime().toString(Qt::ISODate));
        for (const auto &value : data.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject message = value.toObject();
            const QString role = message.value(QStringLiteral("role")).toString();
            if (role == QStringLiteral("system")) continue;
            QString content = message.value(QStringLiteral("content")).toString();
            if (role == QStringLiteral("user")) markdown += QStringLiteral("\n## You\n\n") + content + '\n';
            else if (role == QStringLiteral("assistant")) {
                markdown += QStringLiteral("\n## Agent\n\n") + content + '\n';
                for (const auto &call : message.value(QStringLiteral("tool_calls")).toArray()) {
                    const QJsonObject function = call.toObject().value(QStringLiteral("function")).toObject();
                    markdown += QStringLiteral("\n```tool %1\n%2\n```\n").arg(function.value(QStringLiteral("name")).toString(), function.value(QStringLiteral("arguments")).toString().left(4000));
                }
            } else if (role == QStringLiteral("tool")) {
                markdown += QStringLiteral("\n<details><summary>tool result</summary>\n\n```\n%1\n```\n</details>\n").arg(content.left(4000));
            }
        }
        const QString dir = m_workspace + QStringLiteral("/.relay/exports");
        QDir().mkpath(dir);
        QString slug = data.value(QStringLiteral("title")).toString().toLower();
        slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        slug = slug.left(40).remove(QRegularExpression(QStringLiteral("^-+|-+$")));
        const QString path = dir + '/' + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmm")) + '-' + (slug.isEmpty() ? QStringLiteral("conversation") : slug) + QStringLiteral(".md");
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly)) { status(out.errorString()); return; }
        out.write(markdown.toUtf8());
        if (!out.commit()) { status(out.errorString()); return; }
        toast(QStringLiteral("Exported to .relay/exports"));
        if (onOpenPath) onOpenPath(path);
    }

    // ----- subagents UI (running-agents list, transcripts) ------------------------------------
    // Opens a transcript pane split right; when unset or the window is narrow, an overlay is used.
    std::function<void(const QString &id)> onOpenSubagent;
    const relay::SubagentModel &subagents() const { return m_subagents; }
    void stopSubagent(const QString &id) { send({{"type", "agent_stop"}, {"id", id}}); }
    void stopAllSubagents() {
        if (m_subagents.liveCount() == 0) { status(QStringLiteral("No running agents to stop.")); return; }
        send({{"type", "agent_stop"}, {"id", "all"}});
        toast(QStringLiteral("Stopping all agents"));
    }
    void refreshAgentDefinitions() { send({{"type", "agents_list"}}); }
    void setComposerText(const QString &text) {
        if (m_native) setNative(false, false);
        m_editor->setPlainText(text); m_editor->moveCursor(QTextCursor::End); focusInput();
    }
    void openSubagent(const QString &id) {
        if (!m_subagents.row(id)) return;
        if (onOpenSubagent && window() && window()->width() >= 1000) { onOpenSubagent(id); return; }
        openSubagentOverlay(id);
    }
    // Subscribes the view to the subagent's stream; unsubscribes when the last view for it closes.
    void attachSubagentView(relay::SubagentTranscriptView *view) {
        const QString id = view->agentId();
        m_subagentViews.append(view);
        QPointer<Pane> self(this);
        view->onSend = [self, id](const QString &text) {
            if (self) self->send({{"type", "agent_message"}, {"id", id}, {"text", text}});
        };
        if (const auto *row = m_subagents.row(id)) view->setRow(*row, m_subagents.elapsedNow(*row));
        const QObject *gone = view;
        connect(view, &QObject::destroyed, this, [this, id, gone] {
            if (m_closing) return;
            m_subagentViews.removeAll(nullptr);
            const bool others = std::any_of(m_subagentViews.cbegin(), m_subagentViews.cend(),
                                            [&](const auto &v) { return v && v.data() != gone && v->agentId() == id; });
            if (!others) send({{"type", "agent_subscribe"}, {"id", id}, {"on", false}});
        });
        send({{"type", "agent_subscribe"}, {"id", id}, {"on", true}});
    }
    // ----- end subagents UI ---------------------------------------------------------------------

protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        // Split panes get narrow: drop the key hints and the agent workspace path first.
        if (m_help) m_help->setVisible(width() >= 900);
        placeQueueStrip();
        placeTakeControl();
        placeSubagentOverlay();   // subagents UI
        placeRequestsPanel();     // request ledger UI
        placeSubagentsPanel();
        QTimer::singleShot(0, this, [this] { placeSubagentsPanel(); });
        if (m_transcript) m_transcript->setMaximumHeight(std::max(120, height() * 2 / 5));
        updatePaths();
    }

    bool eventFilter(QObject *object, QEvent *event) override {
        if ((event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate) && object == window())
            noteWindowActivation(event->type() == QEvent::WindowActivate);
        if (object == m_cwdLabel && event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && !m_cwdLabel->hasSelectedText()) {
            if (onOpenPath) onOpenPath(m_cwd);
            hint(QStringLiteral("files.at"), QStringLiteral("Tip: type @ and a file name in the prompt box to open files"));
            return false;
        }
        // Copy on select (off by default): after a left-button release that finishes a selection
        // in this pane's terminal, copy it to the clipboard.
        if (event->type() == QEvent::MouseButtonRelease && copyOnSelect()
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && ownsTerminalWidget(qobject_cast<QWidget *>(object))) {
            QTimer::singleShot(0, this, [this] { copySelection(); });
        }
        // A plain click in the terminal (not a selection drag) used to give it the keyboard.
        // It no longer does, so say which key still hands the keyboard over.
        if (event->type() == QEvent::MouseButtonPress && ownsTerminalWidget(qobject_cast<QWidget *>(object)))
            m_clickOrigin = static_cast<QMouseEvent *>(event)->pos();
        if (event->type() == QEvent::MouseButtonRelease && !m_native
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
            && ownsTerminalWidget(qobject_cast<QWidget *>(object))
            && (static_cast<QMouseEvent *>(event)->pos() - m_clickOrigin).manhattanLength() < 6)
            hint(QStringLiteral("terminal.click"), QStringLiteral("The prompt box is the input · %1 types into the terminal")
                     .arg(Keymap::instance().shortcutText(QStringLiteral("control.human"))));
        if (m_queueList && object == m_queueList->viewport() && event->type() == QEvent::MouseButtonRelease) {
            // The × at the right edge of a queue row removes it.
            const QPoint pos = static_cast<QMouseEvent *>(event)->pos();
            const QModelIndex index = m_queueList->indexAt(pos);
            if (index.isValid() && pos.x() >= m_queueList->viewport()->width() - 26) {
                removeEntry(index.data(Qt::UserRole).toULongLong());
                hint(QStringLiteral("queue.remove.mouse"), QStringLiteral("Next time: ↑ selects queued items, Delete removes, Ctrl+↑/↓ reorders"));
                return true;
            }
        }
        // The prompt box is the only keyboard input: clicking the terminal selects text, scrolls
        // and follows links, but anything that focuses the terminal surface hands the keyboard
        // straight back. Real widgets inside the terminal (the engine's Find bar) keep it.
        if (event->type() == QEvent::FocusIn && !m_native && m_composer && m_composer->isVisible()) {
            auto *focused = qobject_cast<QWidget *>(object);
            if (ownsTerminalWidget(focused) && !acceptsTypedInput(focused))
                QTimer::singleShot(0, this, [this] { if (!m_native) focusInput(); });
        }
        if (event->type() == QEvent::FocusIn && !m_opaqueProgram.isEmpty()) QTimer::singleShot(0, this, [this] { refreshProgramHint(); });
        // Esc in the masked prompt box gives up on answering: back to the normal prompt box.
        if (event->type() == QEvent::KeyPress && object == m_secretEdit && m_secretMode
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            m_secretDeclined = true;
            leaveSecretMode();
            toast(QStringLiteral("Masked input off · %1 types the password into the terminal")
                      .arg(Keymap::instance().shortcutText(QStringLiteral("control.human"))));
            return true;
        }
        if (event->type() == QEvent::KeyPress && object == m_editor) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (handleComposerKey(key)) return true;
            const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
            // The composer is a few lines tall; PageUp/PageDown scroll the pane's terminal instead.
            if (mods == Qt::NoModifier && (key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown)) {
                if (scrollTerminalPage(key->key() == Qt::Key_PageUp ? -1 : 1)) return true;
            }
        }
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
            auto *key = static_cast<QKeyEvent *>(event);
            auto *widget = qobject_cast<QWidget *>(object);
            // Terminal clipboard: Ctrl+C copies when text is selected, otherwise it reaches the
            // shell as an interrupt. Ctrl+V pastes at a shell prompt; inside a program such as
            // vim it is passed through (visual block). Ctrl+X always reaches the terminal.
            const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
            if (event->type() == QEvent::KeyPress && ownsTerminalWidget(widget) && mods == Qt::ControlModifier && !key->isAutoRepeat()) {
                if (m_backend && key->key() == Qt::Key_C) {
                    if (copySelection()) return true;
                } else if (m_backend && key->key() == Qt::Key_V && !processBusy()) {
                    m_backend->paste();
                    return true;
                }
            }
            // Keys never reach the terminal outside native mode, so typing there can no longer
            // take Readline's line away from the composer; nothing to do here.
        }
        return QWidget::eventFilter(object, event);
    }

private:
    void buildUi() {
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(8, 6, 8, 8); layout->setSpacing(6);
        m_cwdLabel = new QLabel; m_cwdLabel->setTextFormat(Qt::PlainText);
        // Clicking the directory line opens it in the explorer pane.
        m_cwdLabel->setCursor(Qt::PointingHandCursor);
        m_cwdLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_cwdLabel->installEventFilter(this);
        m_cwdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(m_cwdLabel);
        m_terminalHost = new QWidget;
        auto *terminalLayout = new QVBoxLayout(m_terminalHost); terminalLayout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_terminalHost, 1);
        auto *composer = new QFrame; composer->setFrameShape(QFrame::StyledPanel);
        m_composer = composer;
        auto *composerLayout = new QVBoxLayout(composer);
        auto *routeRow = new QHBoxLayout;
        m_routeLabel = new QLabel(QStringLiteral("AUTO · local detection"));
        m_routeLabel->setTextFormat(Qt::PlainText);
        // Narrow split panes: labels may shrink instead of forcing a wide minimum width.
        m_routeLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        // `!` / `*` typed first in an empty prompt: terminal / agent mode for this submission.
        m_prefixChip = new QLabel;
        m_prefixChip->setObjectName(QStringLiteral("prefixChip"));
        m_prefixChip->hide();
        routeRow->addWidget(m_prefixChip);
        // "password for sudo" while the prompt box is masked.
        m_secretChip = new QLabel;
        m_secretChip->setObjectName(QStringLiteral("secretChip"));
        m_secretChip->setTextFormat(Qt::PlainText);
        m_secretChip->setToolTip(QStringLiteral("The line is written to the program and never stored"));
        m_secretChip->hide();
        routeRow->addWidget(m_secretChip);
        routeRow->addWidget(m_routeLabel, 1);
        m_opaqueHint = new QLabel;
        m_opaqueHint->setObjectName(QStringLiteral("opaqueHint"));
        m_opaqueHint->hide();
        routeRow->addWidget(m_opaqueHint, 1);
        // Per-pane controls live under the terminal, next to the input they affect.
        m_modeBox = new QComboBox;
        m_modeBox->addItem(QStringLiteral("Auto detect"), QStringLiteral("auto"));
        m_modeBox->addItem(QStringLiteral("Terminal"), QStringLiteral("shell"));
        m_modeBox->addItem(QStringLiteral("Agent"), QStringLiteral("agent"));
        m_modeBox->setAccessibleName(QStringLiteral("Input destination"));
        m_modeBox->setFocusPolicy(Qt::TabFocus);
        m_modeBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_modeBox->setMinimumContentsLength(6);
        connect(m_modeBox, qOverload<int>(&QComboBox::activated), this, [this](int) {
            setMode(m_modeBox->currentData().toString()); focusInput();
            hint(QStringLiteral("mode.mouse"), QStringLiteral("Next time: type ! for the terminal or * for the agent · %1 toggles")
                 .arg(Keymap::instance().shortcutText(QStringLiteral("input.toggle"))));
        });
        routeRow->addWidget(m_modeBox);
        m_modelBox = new QComboBox;
        m_modelBox->setAccessibleName(QStringLiteral("Agent model"));
        m_modelBox->setToolTip(QStringLiteral("Agent model for this pane. Switching keeps the conversation."));
        m_modelBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_modelBox->setMinimumContentsLength(8);
        m_modelBox->setFocusPolicy(Qt::TabFocus);
        connect(m_modelBox, qOverload<int>(&QComboBox::activated), this, [this](int index) {
            selectModel(m_modelBox->itemData(index).toString()); focusInput();
            hint(QStringLiteral("model.mouse"), QStringLiteral("Tip: /model switches models from the prompt box"));
        });
        routeRow->addWidget(m_modelBox);
        buildSessionControls(routeRow);
        setupRequestsUi(routeRow);   // request ledger UI: the Tasks chip
        auto *cancel = new QToolButton;
        cancel->setObjectName(QStringLiteral("interruptButton"));
        const QString cancelIcon = relay::theme::themeDataDir() + QStringLiteral("/icons/cancel.svg");
        if (QFileInfo::exists(cancelIcon)) cancel->setIcon(QIcon(cancelIcon)); else cancel->setText(QStringLiteral("⊘"));
        cancel->setToolTip(QStringLiteral("Interrupt the running command (Esc in the prompt box)"));
        cancel->setAccessibleName(QStringLiteral("Interrupt shell"));
        cancel->setFocusPolicy(Qt::NoFocus);
        connect(cancel, &QToolButton::clicked, this, [this] { interruptShell(); });
        routeRow->addWidget(cancel);
        refreshPickers();
        auto *submit = new QPushButton(QStringLiteral("Submit ↵"));
        connect(submit, &QPushButton::clicked, this, [this] { requestRoute(true, QStringLiteral("auto")); });
        routeRow->addWidget(submit); composerLayout->addLayout(routeRow);
        m_editor = new RichEditor; composerLayout->addWidget(m_editor);
        // Password prompts (checkPasswordPrompt): the prompt box becomes a masked field whose
        // line goes to the running program. It is a separate widget so the password can never
        // reach the composer's document, its history, its undo stack or route assist.
        m_secretEdit = new QLineEdit;
        m_secretEdit->setObjectName(QStringLiteral("secretEditor"));
        m_secretEdit->setEchoMode(QLineEdit::Password);
        m_secretEdit->setAccessibleName(QStringLiteral("Password for the running program"));
        m_secretEdit->setPlaceholderText(QStringLiteral("Password · Enter sends it to the program, Esc cancels"));
        m_secretEdit->hide();
        connect(m_secretEdit, &QLineEdit::returnPressed, this, [this] { submitSecret(); });
        composerLayout->addWidget(m_secretEdit);
        auto *help = new QLabel(QStringLiteral("Shift+Enter  newline     Ctrl+Enter  agent (interrupts when busy)     Ctrl+Shift+Enter  terminal     Ctrl+H  type into the terminal     Esc  stop agent     @  files     ↑  queue / history"));
        help->setWordWrap(true); composerLayout->addWidget(help);
        m_help = help;
        buildTranscript();
        layout->addWidget(m_transcript);
        // The queue strip floats over the bottom of the terminal instead of taking layout space:
        // resizing the terminal would make the shell redraw its prompt mid-output.
        m_queueStrip = new QFrame(this);
        m_queueStrip->setObjectName(QStringLiteral("queueStrip"));
        m_queueStrip->setAttribute(Qt::WA_StyledBackground);
        auto *queueLayout = new QVBoxLayout(m_queueStrip); queueLayout->setContentsMargins(10, 6, 6, 6); queueLayout->setSpacing(2);
        m_queueStrip->hide();
        // A full-screen program (vim, htop) or an ssh session owns the screen. Relay no longer
        // switches to native input by itself; this button, or Ctrl+H, hands the keyboard over.
        m_takeControl = new QPushButton(this);
        m_takeControl->setObjectName(QStringLiteral("takeControlChip"));
        m_takeControl->setCursor(Qt::PointingHandCursor);
        m_takeControl->setFocusPolicy(Qt::NoFocus);
        m_takeControl->hide();
        connect(m_takeControl, &QPushButton::clicked, this, [this] {
            takeControl();
            hint(QStringLiteral("control.human.mouse"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("control.human")), QStringLiteral("take control")));
        });
        layout->addWidget(composer);
        setupSubagentsUi(layout);   // subagents UI: running-agents list under the composer
        updatePaths();
    }

    // ----- agent sessions UI: helpers ----------------------------------------------------------
    void buildSessionControls(QHBoxLayout *row) {
        m_planChip = new QLabel(QStringLiteral("PLAN"));
        m_planChip->setObjectName(QStringLiteral("planChip"));
        m_planChip->setToolTip(QStringLiteral("Plan mode: the agent investigates and writes a plan (Shift+Tab to leave)"));
        m_planChip->hide();
        row->insertWidget(1, m_planChip);
        m_ctxLabel = new QLabel;
        m_ctxLabel->setObjectName(QStringLiteral("contextLabel"));
        m_ctxLabel->hide();
        row->insertWidget(2, m_ctxLabel);
        m_effortBox = new QComboBox;
        for (const QString &level : efforts()) m_effortBox->addItem(level, level);
        m_effortBox->setAccessibleName(QStringLiteral("Reasoning effort"));
        m_effortBox->setToolTip(QStringLiteral("Reasoning effort for this pane (Alt+. / Alt+,)"));
        m_effortBox->setFocusPolicy(Qt::TabFocus);
        m_effortBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_effortBox->setMinimumContentsLength(5);
        m_effort = QSettings().value(QStringLiteral("agent/effort"), QStringLiteral("high")).toString();
        if (!efforts().contains(m_effort)) m_effort = QStringLiteral("high");
        connect(m_effortBox, qOverload<int>(&QComboBox::activated), this, [this](int index) {
            setEffort(m_effortBox->itemData(index).toString()); focusInput();
            hint(QStringLiteral("effort.mouse"), relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("agent.effortUp"))
                 + QStringLiteral(" / ") + Keymap::instance().shortcutText(QStringLiteral("agent.effortDown")), QStringLiteral("raise / lower effort")));
        });
        row->addWidget(m_effortBox);
    }

    void refreshSessionControls() {
        if (!m_effortBox) return;
        const QSignalBlocker block(m_effortBox);
        m_effortBox->setCurrentIndex(std::max(0, m_effortBox->findData(m_effort)));
        m_planChip->setVisible(m_agentMode == QStringLiteral("plan"));
        updateContextLabel();
    }

    static QString compactTokens(qint64 tokens) {
        if (tokens >= 1000000) return QString::number(tokens / 1000000.0, 'f', tokens >= 10000000 ? 0 : 1) + QStringLiteral("M");
        if (tokens >= 1000) return QString::number(tokens / 1000.0, 'f', tokens >= 100000 ? 0 : 1) + QStringLiteral("k");
        return QString::number(tokens);
    }

    void updateContextLabel() {
        if (!m_ctxLabel) return;
        if (m_ctxWindow <= 0) { m_ctxLabel->hide(); return; }
        if (m_compacting) {
            m_ctxLabel->setText(QStringLiteral("Compacting…"));
        } else {
            m_ctxLabel->setText(QStringLiteral("ctx %1 · %2%").arg(compactTokens(m_ctxUsed)).arg(QString::number(m_ctxPercent, 'f', m_ctxPercent > 0 && m_ctxPercent < 10 ? 1 : 0)));
        }
        const bool near = m_ctxLimit > 0 && m_ctxUsed >= m_ctxLimit * 9 / 10;
        m_ctxLabel->setProperty("warn", near);
        m_ctxLabel->style()->unpolish(m_ctxLabel); m_ctxLabel->style()->polish(m_ctxLabel);
        m_ctxLabel->setToolTip(QStringLiteral("%1 of %2 tokens%3\nAuto-compacts at %4 tokens")
            .arg(QLocale().toString(m_ctxUsed), QLocale().toString(m_ctxWindow), m_ctxEstimated ? QStringLiteral(" (estimated)") : QString(),
                 QLocale().toString(m_ctxLimit)));
        m_ctxLabel->show();
    }

    static QString relayMdPath() {
        const QString base = qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config"));
        return base + QStringLiteral("/relay/relay.md");
    }

    // Turn limits and request audit from Agent options (protocol 12.1).
    static QJsonObject requestOptions() {
        QSettings settings;
        return {{"max_steps", std::clamp(settings.value(QStringLiteral("agent/max_steps"), 50).toInt(), 1, 500)},
                {"max_tool_calls", std::clamp(settings.value(QStringLiteral("agent/max_tool_calls"), 150).toInt(), 1, 2000)},
                {"audit_requests", settings.value(QStringLiteral("agent/audit_requests"), false).toBool()}};
    }

    // Session-related configure fields from settings (protocol sections 1 and 8).
    QJsonObject withSessionFields(QJsonObject request) const {
        QSettings settings;
        request.insert(QStringLiteral("effort"), m_effort.isEmpty() ? settings.value(QStringLiteral("agent/effort"), QStringLiteral("high")).toString() : m_effort);
        bool ok = false;
        const double threshold = settings.value(QStringLiteral("agent/compact_threshold")).toDouble(&ok);
        if (ok && threshold >= 0.5 && threshold <= 0.98) request.insert(QStringLiteral("compact_threshold"), threshold);
        const QString plans = settings.value(QStringLiteral("agent/plans_dir")).toString().trimmed();
        if (!plans.isEmpty() && QDir::isAbsolutePath(QDir::fromNativeSeparators(plans))) request.insert(QStringLiteral("plans_dir"), plans);
        request.insert(QStringLiteral("instructions"), QJsonObject{
            {"files", QJsonArray::fromStringList(settings.value(QStringLiteral("instructions/files")).toStringList())},
            {"project_auto", settings.value(QStringLiteral("instructions/project_auto"), true).toBool()}});
        QJsonObject agents = request.value(QStringLiteral("agents")).toObject();
        agents.insert(QStringLiteral("max_auto_turns"), settings.value(QStringLiteral("agent/max_auto_turns"), 50).toInt());
        request.insert(QStringLiteral("agents"), agents);
        const QJsonObject limits = requestOptions();
        for (auto it = limits.begin(); it != limits.end(); ++it) request.insert(it.key(), it.value());
        const QStringList exclude = settings.value(QStringLiteral("skills/exclude")).toStringList();
        if (!exclude.isEmpty()) request.insert(QStringLiteral("skills"), QJsonObject{{"exclude", QJsonArray::fromStringList(exclude)}});
        // Model roles (protocol 13): the table, and the role this pane's own agent runs.
        const QJsonObject roles = rolesObject();
        if (!roles.isEmpty()) request.insert(QStringLiteral("roles"), roles);
        if (m_agentRole != QStringLiteral("main")) request.insert(QStringLiteral("agent_role"), m_agentRole);
        return request;
    }

    // Settings that live in `configure` need a new agent. Apply now when the conversation is empty,
    // otherwise at the next New chat.
    void applyConfigureChange(const QString &what) {
        if (!m_configured || m_currentPreset.isEmpty()) return;
        if (!m_agentBusy && m_turnsCompleted == 0) {
            configurePreset(m_currentPreset, false);
            toast(what + QStringLiteral(" · applied"));
        } else {
            m_reconfigureOnNewChat = true;
            toast(what + QStringLiteral(" · applies to the next New chat"));
        }
    }

    void onSessionConfigured(const QJsonObject &event) {
        m_agentMode = event.value(QStringLiteral("mode")).toString(QStringLiteral("build"));
        const QString effort = event.value(QStringLiteral("effort")).toString();
        if (efforts().contains(effort)) m_effort = effort;
        m_ctxWindow = event.value(QStringLiteral("context_window")).toVariant().toLongLong();
        m_ctxLimit = event.value(QStringLiteral("limit_tokens")).toVariant().toLongLong();
        m_sessionId = event.value(QStringLiteral("session_id")).toString();
        m_sessionDir = event.value(QStringLiteral("session_dir")).toString();
        m_turnsCompleted = 0;
        refreshSessionControls();
        if (!m_initialState.isEmpty()) {
            const QJsonObject state = m_initialState;
            m_initialState = QJsonObject();
            m_forkLoadPending = m_initialIsFork;
            send({{"type", "load_state"}, {"state", state}});
        }
        resumeRestoredSession();
        // First launch: offer to choose instruction files (once per installation).
        static bool offered = false;
        if (!offered && !QSettings().value(QStringLiteral("instructions/onboarded"), false).toBool()) {
            offered = true;
            m_onboarding = true;
            QTimer::singleShot(400, this, [this] { openInstructions(); });
        }
    }

    // Saved window layout: reattach the conversation this pane had when Relay was last quit.
    // The worker replies with `state_loaded` (the "Session loaded: … · N turn(s)" line, plus the
    // open-task count) and a recap; a session whose file is gone starts fresh with one note.
    void resumeRestoredSession() {
        if (m_restoreSession.isEmpty()) return;
        const QString id = m_restoreSession;
        m_restoreSession.clear();
        if (m_sessionDir.isEmpty() || !QFileInfo::exists(m_sessionDir + '/' + id + QStringLiteral(".json"))) {
            ensureLineStart();
            printInline(QStringLiteral("Previous conversation is no longer saved · starting a new one\n"), Ink::Note);
            return;
        }
        m_restoreRequest = QStringLiteral("restore-") + QString::number(++m_requestId);
        send({{"type", "resume"}, {"session_id", id}, {"id", m_restoreRequest}});
    }

    // ----- routing assist: the model breaks ties the local rules cannot (protocol 11) ----------
    void sendRouteAssist(const QString &text) {
        if (text.trimmed().isEmpty() || !m_workerReady || !m_configured) return;
        m_assistId = QStringLiteral("assist-") + QString::number(++m_requestId);
        m_assistInflightText = text;
        // Reasoning models answer in 2–12 s; the local guess stands until (unless) the answer arrives.
        send({{"type", "route_assist"}, {"id", m_assistId}, {"text", text}, {"cwd", m_cwd}, {"mode", QStringLiteral("auto")}, {"timeout_ms", 4000}});
    }

    void onRouteAssisted(const QJsonObject &event) {
        if (event.value(QStringLiteral("id")).toString() != m_assistId) return;
        const QString text = m_assistInflightText;
        m_assistInflightText.clear();
        const QString route = event.value(QStringLiteral("route")).toString();
        if (route != QStringLiteral("shell") && route != QStringLiteral("agent")) {
            m_assistText = text; m_assistRoute.clear(); m_assistFailedText = text;
            const QString guess = m_assistLocalGuess == QStringLiteral("shell") ? QStringLiteral("TERMINAL") : QStringLiteral("AGENT");
            if (m_editor->toPlainText() == text) m_routeLabel->setText(QStringLiteral("%1 · local guess (model check unavailable%2) · ! or * to choose")
                .arg(guess, event.value(QStringLiteral("error")).toString().isEmpty() ? QString() : QStringLiteral(": ") + event.value(QStringLiteral("error")).toString().left(60)));
        } else {
            m_assistText = text; m_assistRoute = route;
            m_assistConfidence = event.value(QStringLiteral("confidence")).toDouble();
            m_assistReason = event.value(QStringLiteral("reason")).toString();
            if (m_editor->toPlainText() == text || !m_heldDecision.isEmpty()) showAssistLabel();
        }
        if (!m_heldDecision.isEmpty() && m_heldDecision.value(QStringLiteral("text")).toString(m_submittedDraft) == text) {
            m_assistHold.stop();
            const QJsonObject decision = m_heldDecision; m_heldDecision = {};
            dispatch(m_assistRoute.isEmpty() ? decision : withAssistedRoute(decision), m_heldMode);
        }
    }

    void releaseHeldDecision() {
        if (m_heldDecision.isEmpty()) return;
        const QJsonObject decision = m_heldDecision; m_heldDecision = {};
        dispatch(decision, m_heldMode);
    }

    QJsonObject withAssistedRoute(QJsonObject decision) const {
        decision.insert(QStringLiteral("route"), m_assistRoute);
        decision.insert(QStringLiteral("reason"), QStringLiteral("guessed: ") + m_assistReason);
        return decision;
    }

    void showAssistLabel() {
        m_routeLabel->setText(QStringLiteral("%1 · guessed: %2 (%3%)").arg(m_assistRoute == QStringLiteral("shell") ? QStringLiteral("TERMINAL") : QStringLiteral("AGENT"),
                                  m_assistReason.isEmpty() ? QStringLiteral("model") : m_assistReason)
                                  .arg(qRound(m_assistConfidence * 100)));
        m_routeLabel->setToolTip(QStringLiteral("Local rules could not decide, so the agent model guessed.\nPrefix ! for the terminal or * for the agent to be explicit."));
    }

    // ----- thinking, turn summaries, tool outputs, routing assist, skills (protocol 11) --------
    static bool showThinking() { return QSettings().value(QStringLiteral("agent/show_thinking"), true).toBool(); }

    bool handleObservabilityEvent(const QString &type, const QJsonObject &event) {
        if (type == QStringLiteral("error")) {
            // Errors for protocol-11 requests carry the request id; keep them out of the transcript.
            const QString id = event.value(QStringLiteral("id")).toString();
            if (id.startsWith(QStringLiteral("skills-"))) {
                if (m_skillsDialog) m_skillsDialog->handleEvent(event); else status(event.value(QStringLiteral("text")).toString());
                return true;
            }
            if (id.startsWith(QStringLiteral("assist-"))) {
                onRouteAssisted({{"id", id}, {"route", QJsonValue()}, {"error", event.value(QStringLiteral("text")).toString()}});
                return true;
            }
            if (id.startsWith(QStringLiteral("turn-"))) {
                status(QStringLiteral("Turn details: ") + event.value(QStringLiteral("text")).toString());
                return true;
            }
            // Saved window layout: the conversation this pane was restored with could not be
            // reopened (deleted, or written by another Relay). Start fresh with one line.
            if (!id.isEmpty() && id == m_restoreRequest) {
                m_restoreRequest.clear();
                ensureLineStart();
                printInline(QStringLiteral("Previous conversation could not be reopened (%1) · starting a new one\n")
                                .arg(event.value(QStringLiteral("text")).toString()), Ink::Note);
                return true;
            }
            return false;
        }
        if (type == QStringLiteral("thinking_delta")) {
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            QString &buffer = m_turnThinking[turn];
            if (buffer.size() < 200000) buffer += event.value(QStringLiteral("text")).toString();
            if (showThinking()) appendThinking(event.value(QStringLiteral("text")).toString());
            return true;
        }
        if (type == QStringLiteral("thinking_done")) {
            const qint64 ms = event.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
            endThinking();
            // A stream that stopped mid-reasoning reports {elapsed_ms: 0, chars: 0}: nothing to summarize.
            if (event.value(QStringLiteral("chars")).toInt() <= 0 && ms <= 0) return true;
            ensureLineStart();
            printInline(QStringLiteral("✦ thought for %1 s\n").arg(std::max<qint64>(1, (ms + 500) / 1000)), Ink::Note);
            return true;
        }
        if (type == QStringLiteral("turn_summary")) {
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            m_turnSummaries.insert(turn, event);
            m_turnOrder.removeAll(turn); m_turnOrder.append(turn);
            while (m_turnOrder.size() > 50) { const QString old = m_turnOrder.takeFirst(); m_turnSummaries.remove(old); m_turnThinking.remove(old); }
            const int tools = event.value(QStringLiteral("tools")).toArray().size();
            if (tools > 0) {
                const qint64 ms = event.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
                printTurnLink(QStringLiteral("✦ %1 tool call%2 · %3 s").arg(tools).arg(tools == 1 ? QString() : QStringLiteral("s"))
                                  .arg(std::max<qint64>(1, (ms + 500) / 1000)), turn);
            }
            if (m_turnViews.contains(turn) && m_turnViews.value(turn)) m_turnViews.value(turn)->setSummary(event);
            return true;
        }
        if (type == QStringLiteral("turn_transcript")) {
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            if (auto view = m_turnViews.value(turn)) view->setTranscript(event);
            return true;
        }
        // `tool_output` is also the live command-output stream ({text}); the reply to
        // tool_output_get is marked `stored: true` (protocol 11.1).
        if (type == QStringLiteral("tool_output") && event.value(QStringLiteral("stored")).toBool()) {
            openToolOutput(event);
            return true;
        }
        if (type == QStringLiteral("route_assisted")) {
            onRouteAssisted(event);
            return true;
        }
        if (type == QStringLiteral("skills") || type == QStringLiteral("skills_refined") || type == QStringLiteral("skills_import_preview")
            || type == QStringLiteral("skills_imported") || type == QStringLiteral("skills_updates")) {
            if (m_skillsDialog) m_skillsDialog->handleEvent(event);
            else if (type != QStringLiteral("skills")) status(QStringLiteral("Skills: ") + type);
            if (type == QStringLiteral("skills_refined")) {
                const auto items = event.value(QStringLiteral("items")).toArray();
                if (!items.isEmpty() && onOpenDocument) onOpenDocument(items.first().toObject().value(QStringLiteral("path")).toString());
            }
            return true;
        }
        return false;
    }

    // Reasoning streams into a panel floating over the bottom of the terminal. It must not take
    // layout space: resizing the terminal makes the idle shell redraw its prompt mid-output.
    void appendThinking(const QString &text) {
        if (text.isEmpty()) return;
        if (!m_thinking) {
            m_thinking = new QFrame(this);
            m_thinking->setObjectName(QStringLiteral("thinkingOverlay"));
            m_thinking->setAttribute(Qt::WA_StyledBackground);
            auto *box = new QVBoxLayout(m_thinking); box->setContentsMargins(10, 4, 6, 6); box->setSpacing(2);
            auto *header = new QHBoxLayout;
            m_thinkingHeader = new QLabel; m_thinkingHeader->setObjectName(QStringLiteral("transcriptHeader"));
            header->addWidget(m_thinkingHeader, 1);
            auto *close = new QToolButton; close->setText(QStringLiteral("×")); close->setAutoRaise(true); close->setFocusPolicy(Qt::NoFocus);
            close->setToolTip(QStringLiteral("Hide for this turn (Actions › Agent options › Show thinking turns it off)"));
            connect(close, &QToolButton::clicked, this, [this] { m_thinkingDismissed = true; m_thinking->hide(); });
            auto *expand = new QToolButton; expand->setAutoRaise(true); expand->setFocusPolicy(Qt::NoFocus);
            expand->setText(QStringLiteral("▴"));
            expand->setToolTip(QStringLiteral("Show more of the reasoning"));
            connect(expand, &QToolButton::clicked, this, [this, expand] {
                m_thinkingExpanded = !m_thinkingExpanded;
                expand->setText(m_thinkingExpanded ? QStringLiteral("▾") : QStringLiteral("▴"));
                expand->setToolTip(m_thinkingExpanded ? QStringLiteral("Show less") : QStringLiteral("Show more of the reasoning"));
                placeThinking();
            });
            header->addWidget(expand);
            header->addWidget(close);
            box->addLayout(header);
            m_thinkingView = new QPlainTextEdit;
            m_thinkingView->setObjectName(QStringLiteral("transcriptView"));
            m_thinkingView->setReadOnly(true);
            m_thinkingView->setFocusPolicy(Qt::NoFocus);
            m_thinkingView->setMaximumBlockCount(400);
            box->addWidget(m_thinkingView, 1);
            m_thinking->hide();
        }
        if (!m_thinkingShown) {
            m_thinkingShown = true;
            m_thinkingDismissed = false;
            m_thinkingView->clear();
            m_thinkingHeader->setText(QStringLiteral("Thinking… · %1").arg(m_model.isEmpty() ? QStringLiteral("agent") : m_model));
        }
        QTextCursor cursor(m_thinkingView->document());
        cursor.movePosition(QTextCursor::End);
        QTextCharFormat format; format.setForeground(relay::theme::TextMuted); format.setFontItalic(true);
        cursor.insertText(sanitize(text), format);
        m_thinkingView->verticalScrollBar()->setValue(m_thinkingView->verticalScrollBar()->maximum());
        if (!m_thinkingDismissed && !m_thinking->isVisible()) { m_thinking->show(); placeThinking(); }
    }

    void placeThinking() {
        if (!m_thinking || !m_thinking->isVisible() || !m_terminalHost) return;
        const QRect host(m_terminalHost->mapTo(this, QPoint(0, 0)), m_terminalHost->size());
        // Compact by default: the overlay floats over the terminal, so it must cover as little
        // output as possible. The ▴ button expands it when the reasoning is worth reading.
        const int lineHeight = std::max(14, m_thinkingView->fontMetrics().height());
        const int wanted = m_thinkingExpanded ? host.height() / 3 : lineHeight * 2 + 30;
        const int height = std::min(m_thinkingExpanded ? 220 : 90, std::max(wanted, lineHeight + 30));
        int bottom = host.bottom() - 6;
        if (m_queueStrip && m_queueStrip->isVisible()) bottom = m_queueStrip->geometry().top() - 4;
        m_thinking->setGeometry(host.left() + 8, bottom - height, host.width() - 16, height);
        m_thinking->raise();
    }

    void endThinking() {
        if (!m_thinkingShown) return;
        m_thinkingShown = false;
        if (m_thinking) m_thinking->hide();
    }

    // One inline line that is also a terminal hyperlink (OSC 8) to relay://turn/<pane>/<turn>.
    // Konsole opens it through the desktop's x-scheme-handler/relay entry (see ensureUrlHandler).
    void printTurnLink(const QString &label, const QString &turnId) {
        if (!shellIdleAtPrompt()) { printInline(label + QStringLiteral("  (Actions › Open last agent turn)\n"), Ink::Note); m_lastTurnId = turnId; return; }
        m_lastTurnId = turnId;
        const QByteArray url = QStringLiteral("relay://turn/%1/%2").arg(m_token, QString::fromUtf8(QUrl::toPercentEncoding(turnId))).toUtf8();
        QByteArray out;
        if (!m_inlineOpen) { out += "\r\x1b[2K"; m_inlineOpen = true; m_atLineStart = true; }
        if (!m_atLineStart) out += "\r\n";
        out += "\x1b]8;;" + url + "\x1b\\" + inkCode(Ink::Note) + sanitize(label).toUtf8() + "\x1b[0m" + "\x1b]8;;\x1b\\";
        out += inkCode(Ink::Note) + QByteArray("  (Ctrl+click)") + "\x1b[0m\r\n";
        m_atLineStart = true;
        writeTerminal(out);
        hint(QStringLiteral("turn.link"), QStringLiteral("Tip: Ctrl+click “tool calls” lines to inspect each call and its output"));
    }

public:
    void openSkills() {
        if (!m_skillsDialog) {
            m_skillsDialog = new relay::SkillsDialog(window());
            m_skillsDialog->setAttribute(Qt::WA_DeleteOnClose);
            m_skillsDialog->send = [this](QJsonObject request) {
                request.insert(QStringLiteral("id"), QStringLiteral("skills-") + QString::number(++m_requestId));
                send(request);
            };
            m_skillsDialog->onExcludedChanged = [](const QStringList &names) {
                QSettings settings;
                settings.setValue(QStringLiteral("skills/exclude_text"), names.join(QStringLiteral(", ")));
                if (names.isEmpty()) settings.remove(QStringLiteral("skills/exclude")); else settings.setValue(QStringLiteral("skills/exclude"), names);
            };
        }
        m_skillsDialog->excluded = QSettings().value(QStringLiteral("skills/exclude")).toStringList();
        m_skillsDialog->show(); m_skillsDialog->raise(); m_skillsDialog->activateWindow();
        if (!m_workerReady) { m_skillsDialog->handleEvent({{"event", "skills"}, {"items", QJsonArray()}}); return; }
        m_skillsDialog->refresh();
    }
    QString lastTurnId() const { return m_lastTurnId; }
    void requestTurn(const QString &turnId, relay::TurnTranscriptView *view) {
        m_turnViews.insert(turnId, view);
        if (m_turnSummaries.contains(turnId)) view->setSummary(m_turnSummaries.value(turnId));
        if (m_turnThinking.contains(turnId)) view->setThinking(m_turnThinking.value(turnId));
        view->onOpenOutput = [this, turnId](const QString &callId) {
            send({{"type", "tool_output_get"}, {"id", QStringLiteral("turn-") + QString::number(++m_requestId)}, {"turn_id", turnId}, {"call_id", callId}});
        };
        send({{"type", "turn_transcript_get"}, {"id", QStringLiteral("turn-") + QString::number(++m_requestId)}, {"turn_id", turnId}});
    }
private:
    // Full tool output → a temporary file shown in a preview pane.
    void openToolOutput(const QJsonObject &event) {
        const QString name = event.value(QStringLiteral("name")).toString();
        const QString preview = event.value(QStringLiteral("preview")).toString();
        const QJsonValue result = event.value(QStringLiteral("result"));
        QString body;
        if (result.isObject()) {
            const QJsonObject r = result.toObject();
            for (const char *field : {"output", "stdout", "content", "text", "error", "message"})
                if (r.contains(QLatin1String(field)) && r.value(QLatin1String(field)).isString()) body += r.value(QLatin1String(field)).toString() + QLatin1Char('\n');
            if (body.isEmpty()) body = QString::fromUtf8(QJsonDocument(r).toJson(QJsonDocument::Indented));
            if (r.contains(QStringLiteral("exit_code"))) body += QStringLiteral("\n[exit %1]\n").arg(r.value(QStringLiteral("exit_code")).toInt());
        } else if (result.isString()) body = result.toString();
        else body = QString::fromUtf8(QJsonDocument(QJsonArray{result}).toJson(QJsonDocument::Indented));
        const bool diff = preview.contains(QStringLiteral("\n+++ ")) || preview.contains(QStringLiteral("\n--- "));
        const QString dir = QDir::tempPath() + QStringLiteral("/relay-tool-output-") + m_token.left(8);
        QDir().mkpath(dir);
        QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QString safe = event.value(QStringLiteral("call_id")).toString();
        safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
        const QString path = dir + QLatin1Char('/') + (name.isEmpty() ? QStringLiteral("tool") : name) + QLatin1Char('-') + safe.left(40)
                             + (diff ? QStringLiteral(".diff") : QStringLiteral(".log"));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { status(QStringLiteral("Could not write tool output: ") + file.errorString()); return; }
        file.write((preview.isEmpty() ? QString() : preview + QStringLiteral("\n\n")).toUtf8());
        file.write(body.toUtf8());
        file.commit();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        if (onOpenPath) onOpenPath(path);
    }

    // Worker events added by the sessions protocol. Returns true when the event was handled here.
    bool handleSessionEvent(const QString &type, const QJsonObject &event) {
        if (type == QStringLiteral("queued") && event.value(QStringLiteral("when")).toString() == QStringLiteral("steer")) {
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            for (auto &entry : m_steering) if (entry.requestId == requestId) entry.itemId = event.value(QStringLiteral("id")).toString();
            return true;
        }
        if (type == QStringLiteral("steer_delivered")) {
            const QJsonArray requestIds = event.value(QStringLiteral("request_ids")).toArray();
            for (const auto &value : requestIds) {
                for (int i = 0; i < m_steering.size(); ++i) {
                    if (m_steering[i].requestId != value.toString()) continue;
                    ensureLineStart();
                    printInline(QStringLiteral("› ") + m_steering[i].text + QStringLiteral("  ↪ at the next tool call\n"), Ink::User);
                    m_steering.removeAt(i);
                    break;
                }
            }
            rebuildQueueStrip();
            return true;
        }
        if (type == QStringLiteral("steer_escalated")) {
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            if (event.value(QStringLiteral("escalated")).toBool()) {
                for (int i = 0; i < m_steering.size(); ++i)
                    if (m_steering[i].requestId == requestId) { m_steering.removeAt(i); break; }
                ensureLineStart();
                printInline(QStringLiteral("Interrupting the current turn; completed actions are not rolled back.\n"), Ink::Note);
            } else {
                // Delivered (or already back in the queue) before the third Enter reached the worker.
                m_interruptPending = false;
                m_pendingPrompts.remove(event.value(QStringLiteral("new_request_id")).toString());
                toast(QStringLiteral("The agent already has it · nothing was interrupted"));
            }
            rebuildQueueStrip(); changed();
            return true;
        }
        if (type == QStringLiteral("steer_returned")) {
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            for (int i = 0; i < m_steering.size(); ++i) {
                if (m_steering[i].requestId != requestId) continue;
                if (!event.value(QStringLiteral("requeued")).toBool()) {
                    // The turn ended before another tool call: the prompt becomes the next queue item.
                    QueueEntry entry; entry.agent = true; entry.text = m_steering[i].text; entry.attachments = m_steering[i].attachments;
                    entry.id = ++m_entrySerial;
                    m_entries.prepend(entry);
                    toast(QStringLiteral("The agent finished first · your message is next in the queue"));
                }
                m_steering.removeAt(i);
                break;
            }
            rebuildQueueStrip(); changed();
            QTimer::singleShot(0, this, [this] { pumpQueue(); });
            return true;
        }
        if (type == QStringLiteral("model_roles")) {   // protocol 13
            m_roleSummary = event.value(QStringLiteral("roles")).toObject();
            const QString role = event.value(QStringLiteral("agent_role")).toString();
            if (!role.isEmpty()) m_agentRole = role;
            const QJsonArray warnings = event.value(QStringLiteral("warnings")).toArray();
            for (const auto &warning : warnings) {
                ensureLineStart();
                printInline(warning.toString() + '\n', Ink::Note);
                closeInline();
            }
            changed();
            return true;
        }
        if (type == QStringLiteral("model_changed")) {
            m_model = event.value(QStringLiteral("model")).toString();
            const QString preset = event.value(QStringLiteral("preset")).toString();
            const QString role = event.value(QStringLiteral("agent_role")).toString();   // protocol 13
            if (!role.isEmpty()) m_agentRole = role;
            const QString warning = event.value(QStringLiteral("warning")).toString();
            if (!warning.isEmpty()) { ensureLineStart(); printInline(warning + '\n', Ink::Note); closeInline(); }
            // A role switch keeps the pane's main preset: only a set_model changes it.
            if (!preset.isEmpty() && role.isEmpty()) { m_currentPreset = preset; QSettings().setValue(QStringLiteral("provider/preset"), preset); }
            const qint64 window = event.value(QStringLiteral("context_window")).toVariant().toLongLong();
            if (window > 0) m_ctxWindow = window;
            const QString effort = event.value(QStringLiteral("effort")).toString();
            if (efforts().contains(effort)) m_effort = effort;
            const QString what = role.isEmpty() || role == QStringLiteral("main")
                ? QStringLiteral("Model: %1 · conversation kept").arg(m_model)
                : QStringLiteral("%1: %2 · conversation kept").arg(roleLabel(role), m_model);
            status(what); toast(what);
            changed();
            return true;
        }
        if (type == QStringLiteral("effort_changed")) {
            const QString effort = event.value(QStringLiteral("effort")).toString();
            if (efforts().contains(effort)) m_effort = effort;
            changed();
            return true;
        }
        if (type == QStringLiteral("context")) {
            m_ctxUsed = event.value(QStringLiteral("used_tokens")).toVariant().toLongLong();
            m_ctxWindow = event.value(QStringLiteral("window")).toVariant().toLongLong();
            m_ctxLimit = event.value(QStringLiteral("limit_tokens")).toVariant().toLongLong();
            m_ctxPercent = event.value(QStringLiteral("percent")).toDouble();
            m_ctxEstimated = event.value(QStringLiteral("estimated")).toBool();
            if (m_contextNotePending) {
                m_contextNotePending = false;
                ensureLineStart();
                printInline(QStringLiteral("Context: %1 of %2 tokens (%3%) · compacts at %4%5\n")
                    .arg(compactTokens(m_ctxUsed), compactTokens(m_ctxWindow), QString::number(m_ctxPercent, 'f', 1), compactTokens(m_ctxLimit),
                         m_ctxEstimated ? QStringLiteral(" · estimated") : QString()), Ink::Note);
                if (!m_agentBusy && !moreTurnsPending()) closeInline();
            }
            updateContextLabel();
            return true;
        }
        if (type == QStringLiteral("compaction_started")) {
            m_compacting = true; updateContextLabel();
            status(QStringLiteral("Compacting the conversation…"));
            return true;
        }
        if (type == QStringLiteral("compacted")) {
            m_compacting = false;
            status(QStringLiteral("Conversation compacted"));
            ensureLineStart();
            printInline(QStringLiteral("Conversation compacted (%1) · %2 → %3 tokens\n")
                .arg(event.value(QStringLiteral("reason")).toString(QStringLiteral("manual")),
                     compactTokens(event.value(QStringLiteral("before_tokens")).toVariant().toLongLong()),
                     compactTokens(event.value(QStringLiteral("after_tokens")).toVariant().toLongLong())), Ink::Note);
            if (!m_agentBusy && !moreTurnsPending()) closeInline();
            updateContextLabel();
            return true;
        }
        if (type == QStringLiteral("mode_changed")) {
            const QString mode = event.value(QStringLiteral("mode")).toString();
            const bool changedMode = mode != m_agentMode;
            m_agentMode = mode;
            if (changedMode)
                toast(mode == QStringLiteral("plan") ? QStringLiteral("Plan mode · the agent investigates and writes a plan")
                                                     : QStringLiteral("Build mode"));
            changed();
            return true;
        }
        if (type == QStringLiteral("plan_written")) {
            const QString path = event.value(QStringLiteral("path")).toString();
            ensureLineStart();
            printInline(QStringLiteral("Plan written: %1\n").arg(QDir(m_workspace).relativeFilePath(path)), Ink::Note);
            if (onPlanWritten) QTimer::singleShot(0, this, [this, path] { if (onPlanWritten) onPlanWritten(path, this); });
            return true;
        }
        if (type == QStringLiteral("checkpoints")) {
            if (!m_rewindPending) return true;
            m_rewindPending = false;
            const QJsonArray items = event.value(QStringLiteral("items")).toArray();
            QTimer::singleShot(0, this, [this, items] { showRewindPicker(items); });
            return true;
        }
        if (type == QStringLiteral("rewound")) {
            const QJsonArray restored = event.value(QStringLiteral("restored_files")).toArray();
            const QJsonArray conflicts = event.value(QStringLiteral("conflicts")).toArray();
            ensureLineStart();
            const QString restore = event.value(QStringLiteral("restore")).toString();
            const QString what = restore == QStringLiteral("files") ? QStringLiteral("code") : restore == QStringLiteral("both") ? QStringLiteral("code and chat") : QStringLiteral("chat");
            printInline(QStringLiteral("Rewound %1 to turn %2%3\n").arg(what).arg(event.value(QStringLiteral("turn")).toInt())
                .arg(restore == QStringLiteral("conversation") ? QStringLiteral(" · files unchanged") : QStringLiteral(" · %1 file(s) restored").arg(restored.size())), Ink::Note);
            if (!conflicts.isEmpty()) {
                QStringList names;
                for (const auto &value : conflicts) names << (value.isString() ? value.toString() : value.toObject().value(QStringLiteral("path")).toString());
                printInline(QStringLiteral("Changed since, not restored: %1\n").arg(names.join(QStringLiteral(", "))), Ink::Error);
            }
            const QString note = event.value(QStringLiteral("note")).toString();
            if (!note.isEmpty()) printInline(note + '\n', Ink::Note);
            closeInline();
            const QString prompt = event.value(QStringLiteral("prompt")).toString();
            // Rewind code keeps the chat, so the turn's prompt is not put back.
            if (!prompt.isEmpty() && restore != QStringLiteral("files") && m_editor->toPlainText().isEmpty()) {
                m_editor->setPlainText(prompt);
                m_editor->moveCursor(QTextCursor::End);
                m_editor->setFocus();
            }
            m_turnsCompleted = std::max(0, event.value(QStringLiteral("turn")).toInt() - 1);
            return true;
        }
        if (type == QStringLiteral("fork_state")) {
            if (!m_forkPending) return true;
            m_forkPending = false;
            const QJsonObject state = event.value(QStringLiteral("state")).toObject();
            const QString title = state.value(QStringLiteral("title")).toString();
            if (onForkState) QTimer::singleShot(0, this, [this, state, title] { if (onForkState) onForkState(state, title); });
            return true;
        }
        if (type == QStringLiteral("state_loaded")) {
            m_sessionId = event.value(QStringLiteral("session_id")).toString(m_sessionId);
            m_turnsCompleted = event.value(QStringLiteral("turns")).toInt();
            const QString title = event.value(QStringLiteral("title")).toString();
            ensureLineStart();
            if (m_forkLoadPending)
                printInline(QStringLiteral("Forked from “%1” · %2 turn(s)\n").arg(m_forkTitle.isEmpty() ? title : m_forkTitle).arg(m_turnsCompleted), Ink::Note);
            else
                printInline(QStringLiteral("Session loaded%1 · %2 turn(s)\n").arg(title.isEmpty() ? QString() : QStringLiteral(": “") + title + QStringLiteral("”"))
                            .arg(m_turnsCompleted), Ink::Note);
            if (const int open = event.value(QStringLiteral("open_requests")).toInt(); open > 0)
                printInline(QStringLiteral("○ %1 task%2 still open · /tasks\n").arg(open).arg(open == 1 ? QString() : QStringLiteral("s")), Ink::Note);
            m_forkLoadPending = false;
            closeInline();
            clearAgentQueue();
            return true;
        }
        // ----- conversation list and search (protocol section 14) -----------------------------
        if (type == QStringLiteral("conversations")) {
            if (m_conversations) m_conversations->setResults(event);
            return true;
        }
        if (type == QStringLiteral("conversation")) {
            if (event.value(QStringLiteral("id")).toString() == QStringLiteral("find-count")) {
                if (m_findBar) m_findBar->setConversationMatches(event.value(QStringLiteral("match_count")).toInt());
                return true;
            }
            if (m_conversations) m_conversations->setPreview(event);
            return true;
        }
        if (type == QStringLiteral("conversation_deleted") || type == QStringLiteral("conversation_renamed")
            || type == QStringLiteral("conversation_pinned")) {
            if (m_conversations) m_conversations->removed(event.value(QStringLiteral("session_id")).toString());
            if (type == QStringLiteral("conversation_deleted")) status(QStringLiteral("Conversation deleted."));
            return true;
        }
        if (type == QStringLiteral("terminal_history_indexed") || type == QStringLiteral("index_rebuilt")) {
            if (type == QStringLiteral("index_rebuilt"))
                status(QStringLiteral("Conversation index rebuilt: %1 conversation(s), %2 entries, %3 ms")
                           .arg(event.value(QStringLiteral("sessions")).toInt())
                           .arg(event.value(QStringLiteral("entries")).toInt())
                           .arg(event.value(QStringLiteral("ms")).toInt()));
            return true;
        }
        if (type == QStringLiteral("sessions")) {
            if (!m_resumePending) return true;
            m_resumePending = false;
            const QJsonArray items = event.value(QStringLiteral("items")).toArray();
            QTimer::singleShot(0, this, [this, items] { showResumePicker(items); });
            return true;
        }
        if (type == QStringLiteral("recap")) {
            const QString reason = event.value(QStringLiteral("reason")).toString();
            if (event.contains(QStringLiteral("skipped"))) {
                if (reason == QStringLiteral("manual") || m_recapManual)
                    status(event.value(QStringLiteral("skipped")).toString() == QStringLiteral("failed")
                           ? QStringLiteral("Recap failed: ") + event.value(QStringLiteral("error")).toString()
                           : QStringLiteral("Not enough conversation for a recap yet."));
                m_recapManual = false;
                return true;
            }
            m_recapManual = false;
            m_lastRecapTurns = event.value(QStringLiteral("turns_covered")).toInt();
            ensureLineStart();
            printInline(QStringLiteral("Recap · ") + event.value(QStringLiteral("text")).toString() + '\n', Ink::Recap);
            const QString next = event.value(QStringLiteral("next_action")).toString();
            if (!next.isEmpty()) printInline(QStringLiteral("Next · ") + next + '\n', Ink::Recap);
            const QString openLine = relay::RequestLedgerModel::openItemsLine(relay::RequestLedgerModel::parseOpenItems(event.value(QStringLiteral("open_items")).toArray()));
            if (!openLine.isEmpty()) printInline(QStringLiteral("Open · ") + openLine + QStringLiteral("  · /tasks\n"), Ink::Recap);
            if (!m_agentBusy && !moreTurnsPending()) closeInline();
            if (reason == QStringLiteral("away")) toast(QStringLiteral("Welcome back · recap above"));
            return true;
        }
        if (type == QStringLiteral("instructions_found")) {
            if (!m_instructionsDialogPending) return true;
            m_instructionsDialogPending = false;
            const QJsonArray items = event.value(QStringLiteral("items")).toArray();
            QTimer::singleShot(0, this, [this, items] { showInstructionsDialog(items); });
            return true;
        }
        if (type == QStringLiteral("instructions_synthesized")) {
            const QString path = event.value(QStringLiteral("path")).toString();
            QSettings().setValue(QStringLiteral("instructions/files"), QStringList{path});
            status(QStringLiteral("Created %1").arg(path));
            ensureLineStart();
            printInline(QStringLiteral("Created %1 from your instruction files\n").arg(path), Ink::Note);
            closeInline();
            applyConfigureChange(QStringLiteral("relay.md created"));
            if (onOpenDocument) QTimer::singleShot(0, this, [this, path] { if (onOpenDocument) onOpenDocument(path); });
            return true;
        }
        if (type == QStringLiteral("suggestion")) {
            const QString kind = event.value(QStringLiteral("kind")).toString();
            const QString text = event.value(QStringLiteral("text")).toString().trimmed();
            if (event.value(QStringLiteral("id")).toString() != m_suggestionId || text.isEmpty() || !m_editor->toPlainText().isEmpty()) return true;
            if (kind == QStringLiteral("next_command") && m_modeValue == QStringLiteral("agent")) return true;
            if (kind == QStringLiteral("next_prompt") && m_modeValue == QStringLiteral("shell")) return true;
            m_aiGhost = text;
            m_aiGhostKind = kind;
            updateGhost();
            return true;
        }
        if (type == QStringLiteral("agent_options")) {
            status(QStringLiteral("Automatic turns from background agents: up to %1").arg(event.value(QStringLiteral("max_auto_turns")).toInt()));
            return true;
        }
        if (type == QStringLiteral("reset")) {
            m_turnsCompleted = 0; m_lastRecapTurns = -1;
            if (m_reconfigureOnNewChat) {
                m_reconfigureOnNewChat = false;
                QTimer::singleShot(0, this, [this] { if (!m_agentBusy) configurePreset(m_currentPreset, false); });
            }
            return true;
        }
        return false;
    }

    void showRewindPicker(const QJsonArray &items) {
        QList<relay::agentui::PickerRow> rows;
        QList<int> turns;
        for (int i = items.size() - 1; i >= 0; --i) {
            const QJsonObject item = items.at(i).toObject();
            const QStringList files = item.value(QStringLiteral("files")).toVariant().toStringList();
            relay::agentui::PickerRow row;
            row.columns = QStringList{QString::number(item.value(QStringLiteral("turn")).toInt()), item.value(QStringLiteral("prompt_preview")).toString(),
                           QDateTime::fromSecsSinceEpoch(qint64(item.value(QStringLiteral("time")).toDouble())).toString(QStringLiteral("HH:mm")),
                           files.isEmpty() ? QString(QStringLiteral("—")) : QStringLiteral("%1 file(s)").arg(files.size())};
            row.detail = files.join('\n');
            rows << row;
            turns << item.value(QStringLiteral("turn")).toInt();
        }
        const bool code = m_rewindKind == QStringLiteral("code");
        const auto result = code
            ? relay::agentui::pick(this, QStringLiteral("Rewind code"),
                  QStringLiteral("Restore the files the agent changed to how they were just before a turn. The chat is kept unless you pick “Code and chat”. Shell commands are never undone."),
                  {QStringLiteral("Turn"), QStringLiteral("Prompt"), QStringLiteral("Time"), QStringLiteral("Files")}, rows,
                  {{QStringLiteral("files"), QStringLiteral("Rewind code…"), true},
                   {QStringLiteral("both"), QStringLiteral("Code and chat…"), false}})
            : relay::agentui::pick(this, QStringLiteral("Rewind chat"),
                  QStringLiteral("Return the conversation to just before a turn. Files are not changed (use /rewind-code for that)."),
                  {QStringLiteral("Turn"), QStringLiteral("Prompt"), QStringLiteral("Time"), QStringLiteral("Files")}, rows,
                  {{QStringLiteral("conversation"), QStringLiteral("Rewind chat"), true},
                   {QStringLiteral("fork"), QStringLiteral("Fork from here"), false}});
        if (result.row < 0) { focusInput(); return; }
        const int turn = turns.at(result.row);
        if (result.action == QStringLiteral("fork")) { requestFork(turn - 1); return; }
        if (code) {
            // Files touched by this turn and every later one are what the restore puts back.
            QStringList files;
            for (const auto &value : items) {
                const QJsonObject item = value.toObject();
                if (item.value(QStringLiteral("turn")).toInt() < turn) continue;
                for (const QString &file : item.value(QStringLiteral("files")).toVariant().toStringList())
                    if (!files.contains(file)) files << file;
            }
            if (files.isEmpty() && result.action == QStringLiteral("files")) {
                status(QStringLiteral("The agent changed no files since turn %1; nothing to restore.").arg(turn));
                focusInput(); return;
            }
            QStringList shown;
            for (const QString &file : std::as_const(files)) shown << QDir(m_workspace).relativeFilePath(file);
            QMessageBox confirm(QMessageBox::Warning, QStringLiteral("Rewind code"),
                QStringLiteral("Restore %1 file(s) to how they were before turn %2%3?")
                    .arg(files.size()).arg(turn).arg(result.action == QStringLiteral("both") ? QStringLiteral(" and rewind the chat") : QString()),
                QMessageBox::Cancel, this);
            confirm.setInformativeText(QStringLiteral("Files changed since the agent wrote them (by you or a program) are skipped and reported as conflicts. Shell commands are never undone."));
            confirm.setDetailedText(shown.join('\n'));
            auto *restore = confirm.addButton(QStringLiteral("Restore"), QMessageBox::AcceptRole);
            confirm.setDefaultButton(restore);   // Enter confirms
            // Show the file list without an extra click.
            for (auto *button : confirm.buttons())
                if (confirm.buttonRole(button) == QMessageBox::ActionRole) button->click();
            confirm.exec();
            if (confirm.clickedButton() != restore) { focusInput(); return; }
        }
        send({{"type", "rewind"}, {"turn", turn}, {"restore", result.action}});
    }

    void showResumePicker(const QJsonArray &items) {
        QList<relay::agentui::PickerRow> rows;
        QStringList ids;
        for (const auto &value : items) {
            const QJsonObject item = value.toObject();
            relay::agentui::PickerRow row;
            row.columns = QStringList{item.value(QStringLiteral("title")).toString(),
                           QDateTime::fromSecsSinceEpoch(qint64(item.value(QStringLiteral("updated")).toDouble())).toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                           QString::number(item.value(QStringLiteral("turns")).toInt())
                               + (item.value(QStringLiteral("open_requests")).toInt() > 0 ? QStringLiteral(" · %1 open").arg(item.value(QStringLiteral("open_requests")).toInt()) : QString()),
                           item.value(QStringLiteral("model")).toString()};
            rows << row;
            ids << item.value(QStringLiteral("id")).toString();
        }
        const auto result = relay::agentui::pick(this, QStringLiteral("Resume session"),
            QStringLiteral("Saved agent sessions for this workspace. Resuming replaces this pane's conversation."),
            {QStringLiteral("Title"), QStringLiteral("Updated"), QStringLiteral("Turns"), QStringLiteral("Model")}, rows,
            {{QStringLiteral("resume"), QStringLiteral("Resume"), true}});
        if (result.row < 0) { focusInput(); return; }
        if (m_agentBusy) { status(QStringLiteral("Stop the agent turn before resuming a session.")); return; }
        send({{"type", "resume"}, {"id", ids.at(result.row)}});
    }

    // ===== conversation list and full-text search (protocol section 14) =====================
public:
    // Ctrl+Shift+O, /conversations, palette: every saved conversation and Relay's terminal
    // history, searchable. The worker searches the index; this only shows what comes back.
    void openConversations(const QString &initialQuery = QString()) {
        if (!m_workerReady) { status(QStringLiteral("The agent worker is still starting.")); return; }
        if (!m_conversations) {
            m_conversations = new relay::conversations::Dialog(this);
            m_conversations->setAttribute(Qt::WA_DeleteOnClose, false);
            m_conversations->onQuery = [this](const QJsonObject &request) {
                QJsonObject message = request;
                message.insert(QStringLiteral("type"), QStringLiteral("conversations"));
                message.insert(QStringLiteral("workspace"), m_workspace);
                message.insert(QStringLiteral("id"), QStringLiteral("conv-list"));
                send(message);
            };
            m_conversations->onPreview = [this](const QString &sessionId, const QString &query) {
                send({{"type", "conversation_get"}, {"id", QStringLiteral("conv-preview")},
                      {"session_id", sessionId}, {"query", query}});
            };
            m_conversations->onResume = [this](const QJsonObject &item, bool newPane) { openSavedSession(item, newPane); };
            m_conversations->onRename = [this](const QString &sessionId, const QString &title) {
                send({{"type", "conversation_rename"}, {"session_id", sessionId}, {"title", title}});
            };
            m_conversations->onPin = [this](const QString &sessionId, bool pinned) {
                send({{"type", "conversation_pin"}, {"session_id", sessionId}, {"pinned", pinned}});
            };
            m_conversations->onDelete = [this](const QString &sessionId) {
                send({{"type", "conversation_delete"}, {"session_id", sessionId}});
            };
        }
        if (!initialQuery.isEmpty()) m_conversations->findChildren<QLineEdit *>().value(0)->setText(initialQuery);
        m_conversations->show();
        m_conversations->raise();
        m_conversations->activateWindow();
        m_conversations->focusSearch();
    }

    // Enter resumes in this pane, Shift+Enter opens the conversation in a new one. A conversation
    // saved for another workspace comes back through load_state, which accepts a session reference.
    void openSavedSession(const QJsonObject &item, bool newPane) {
        const QString sessionId = item.value(QStringLiteral("session_id")).toString();
        const QString directory = item.value(QStringLiteral("session_dir")).toString();
        const QString title = item.value(QStringLiteral("title")).toString();
        if (sessionId.isEmpty()) return;
        const QJsonObject reference{{QStringLiteral("version"), 1},
                                    {QStringLiteral("kind"), QStringLiteral("relay_agent_state_ref")},
                                    {QStringLiteral("session_id"), sessionId},
                                    {QStringLiteral("session_dir"), directory.isEmpty() ? m_sessionDir : directory}};
        if (newPane) {
            if (onOpenSessionInNewPane) onOpenSessionInNewPane(reference, title);
            else if (onForkState) onForkState(reference, title);
            return;
        }
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        if (m_agentBusy) { status(QStringLiteral("Stop the agent turn before opening another conversation.")); return; }
        if (directory.isEmpty() || directory == m_sessionDir) send({{"type", "resume"}, {"id", sessionId}});
        else send({{"type", "load_state"}, {"state", reference}});
    }

    // Ctrl+F: find in this pane. The terminal scrollback is searched by the engine; the saved
    // conversation is counted by the worker, which can also open it in the list.
    void openFindInView() {
        if (!m_findBar) {
            m_findBar = new relay::conversations::FindBar(this);
            m_findBar->hide();
            if (auto *box = qobject_cast<QVBoxLayout *>(layout()))
                box->insertWidget(m_composer ? box->indexOf(m_composer) : box->count(), m_findBar);
            m_findBar->onFind = [this](const QString &text, bool backwards) { return findInTerminal(text, backwards); };
            m_findBar->onCountConversation = [this](const QString &text) {
                // Nothing is indexed before the first turn is saved; asking then is an error.
                if (!m_workerReady || m_sessionId.isEmpty() || text.isEmpty() || m_turnsCompleted == 0) {
                    if (m_findBar) m_findBar->setConversationMatches(0);
                    return;
                }
                send({{"type", "conversation_get"}, {"id", QStringLiteral("find-count")},
                      {"session_id", m_sessionId}, {"query", text}});
            };
            m_findBar->onOpenConversation = [this](const QString &text) { openConversations(text); };
            m_findBar->onClosed = [this] { focusInput(); };
        }
        m_findBar->setTerminalSearchable(terminalCan(relay::TerminalBackend::Search));
        QString preset = m_backend ? m_backend->selectedText().trimmed() : QString();
        if (preset.contains('\n') || preset.size() > 80) preset.clear();
        m_findBar->start(preset);
    }

    void closeFindInView() { if (m_findBar) m_findBar->hide(); }

    void rebuildConversationIndex() {
        if (!m_workerReady) { status(QStringLiteral("The agent worker is still starting.")); return; }
        status(QStringLiteral("Rebuilding the conversation index…"));
        send({{"type", "index_rebuild"}, {"id", QStringLiteral("index-rebuild")}});
    }

private:
    // Relay-run terminal commands: the command line, its exit status and, on engines that can
    // stream it, its output. Nothing typed straight into the terminal in native mode is seen here.
    static constexpr int kCommandCaptureCap = 64 * 1024;

    static bool indexTerminalHistory() {
        return QSettings().value(QStringLiteral("index/terminal_history"), true).toBool();
    }
    static bool indexTerminalOutput() {
        return QSettings().value(QStringLiteral("index/terminal_output"), true).toBool();
    }

    void beginCommandCapture(const QString &command) {
        if (!indexTerminalHistory() || command.trimmed().isEmpty()) { m_captureCommand.clear(); return; }
        m_captureCommand = command;
        m_captureCwd = m_cwd;
        m_captureAt = QDateTime::currentSecsSinceEpoch();
        m_capture.clear();
        m_capturing = indexTerminalOutput() && m_backend;
        if (m_capturing) m_backend->setOutputCallbackEnabled(true);
    }

    void finishCommandCapture(int exitStatus) {
        if (m_captureCommand.isEmpty()) return;
        if (m_capturing && m_backend) m_backend->setOutputCallbackEnabled(false);
        m_capturing = false;
        QJsonObject item{{QStringLiteral("command"), m_captureCommand},
                         {QStringLiteral("exit_status"), exitStatus},
                         {QStringLiteral("cwd"), m_captureCwd},
                         {QStringLiteral("time"), double(m_captureAt)}};
        // With the shell integration the next prompt starts with OSC 133;A; everything from there
        // is the prompt being redrawn, not the command's output.
        QByteArray captured = m_capture;
        if (const int prompt = captured.indexOf("\x1b]133;A"); prompt >= 0) captured.truncate(prompt);
        QString output = relay::conversations::stripAnsi(captured);
        // The shell echoes the command it is about to run; that line is already the command row.
        if (output.startsWith(m_captureCommand)) output = output.mid(m_captureCommand.size());
        if (!output.trimmed().isEmpty()) item.insert(QStringLiteral("output"), output.trimmed());
        m_captureCommand.clear();
        m_capture.clear();
        if (m_workerReady)
            send({{"type", "terminal_history"}, {"workspace", m_workspace}, {"items", QJsonArray{item}}});
    }


    void showInstructionsDialog(const QJsonArray &items) {
        QSettings settings;
        const QStringList selected = settings.value(QStringLiteral("instructions/files")).toStringList();
        QList<relay::agentui::InstructionFile> files;
        for (const auto &value : items) {
            const QJsonObject item = value.toObject();
            relay::agentui::InstructionFile file;
            file.path = item.value(QStringLiteral("path")).toString();
            file.tool = item.value(QStringLiteral("tool")).toString();
            file.scope = item.value(QStringLiteral("scope")).toString();
            file.bytes = item.value(QStringLiteral("bytes")).toVariant().toLongLong();
            file.exists = item.value(QStringLiteral("exists")).toBool();
            file.checked = selected.contains(file.path);
            files << file;
        }
        if (QFileInfo::exists(relayMdPath()) && std::none_of(files.cbegin(), files.cend(), [](const auto &f) { return f.path == relayMdPath(); })) {
            relay::agentui::InstructionFile relay;
            relay.path = relayMdPath(); relay.tool = QStringLiteral("Relay"); relay.scope = QStringLiteral("global");
            relay.bytes = QFileInfo(relayMdPath()).size(); relay.exists = true; relay.checked = selected.contains(relay.path);
            files.prepend(relay);
        }
        const auto result = relay::agentui::chooseInstructions(this, files, settings.value(QStringLiteral("instructions/project_auto"), true).toBool(), relayMdPath());
        settings.setValue(QStringLiteral("instructions/onboarded"), true);
        m_onboarding = false;
        if (!result.accepted) { focusInput(); return; }
        settings.setValue(QStringLiteral("instructions/project_auto"), result.projectAuto);
        if (result.synthesize && !result.files.isEmpty()) {
            settings.setValue(QStringLiteral("instructions/files"), result.files);
            send({{"type", "synthesize_instructions"}, {"files", QJsonArray::fromStringList(result.files)}, {"target", relayMdPath()}});
            status(QStringLiteral("Creating relay.md from %1 file(s)…").arg(result.files.size()));
            toast(QStringLiteral("Creating relay.md…"));
        } else {
            settings.setValue(QStringLiteral("instructions/files"), result.files);
            applyConfigureChange(QStringLiteral("Instructions saved"));
        }
        focusInput();
    }

    // ----- slash commands in the composer --------------------------------------------------------
    struct SlashCommand { QString name, args, description; };
    static const QList<SlashCommand> &slashCommands() {
        static const QList<SlashCommand> commands{
            {QStringLiteral("new"), QString(), QStringLiteral("Start a new conversation")},
            {QStringLiteral("clear"), QString(), QStringLiteral("Start a new conversation (same as /new)")},
            {QStringLiteral("model"), QStringLiteral("[name]"), QStringLiteral("Switch model, keeping the conversation")},
            {QStringLiteral("effort"), QStringLiteral("[low|medium|high|max]"), QStringLiteral("Set reasoning effort")},
            {QStringLiteral("compact"), QStringLiteral("[focus]"), QStringLiteral("Summarize older turns to free context")},
            {QStringLiteral("context"), QString(), QStringLiteral("Show context usage")},
            {QStringLiteral("rewind"), QString(), QStringLiteral("Rewind chat to an earlier turn (files are not changed)")},
            {QStringLiteral("rewind-code"), QString(), QStringLiteral("Restore files the agent changed since an earlier turn")},
            {QStringLiteral("fork"), QString(), QStringLiteral("Continue this conversation in a new pane")},
            {QStringLiteral("resume"), QString(), QStringLiteral("Resume a saved session")},
            {QStringLiteral("conversations"), QStringLiteral("[words]"), QStringLiteral("List and search every conversation and Relay's terminal history")},
            {QStringLiteral("find"), QStringLiteral("[words]"), QStringLiteral("Find in this pane: conversation and terminal scrollback")},
            {QStringLiteral("plan"), QString(), QStringLiteral("Toggle plan mode")},
            {QStringLiteral("recap"), QString(), QStringLiteral("Summarize this session")},
            {QStringLiteral("tasks"), QString(), QStringLiteral("Task list: progress, mark done, cancel, re-ask")},
            {QStringLiteral("requests"), QString(), QStringLiteral("Task list grouped by what you asked (same as /tasks)")},
            {QStringLiteral("todos"), QString(), QStringLiteral("Task list (same as /tasks)")},
            {QStringLiteral("continue"), QString(), QStringLiteral("Continue the agent turn (after a step limit)")},
            {QStringLiteral("agents"), QString(), QStringLiteral("Subagents: definitions and running agents")},
            {QStringLiteral("skills"), QString(), QStringLiteral("Skills: list, exclude, refine, import from a repository")},
            {QStringLiteral("instructions"), QString(), QStringLiteral("Choose instruction files (CLAUDE.md, AGENTS.md, WARP.md…)")},
            {QStringLiteral("export"), QString(), QStringLiteral("Save the conversation as Markdown")},
            {QStringLiteral("shell"), QStringLiteral("<command>"), QStringLiteral("Send to the terminal")},
            {QStringLiteral("agent"), QStringLiteral("<prompt>"), QStringLiteral("Send to the agent")}};
        return commands;
    }

    void updateSlashPopup() {
        const QString text = m_editor ? m_editor->toPlainText() : QString();
        if (!m_editor || m_native || !text.startsWith('/') || text.contains(QRegularExpression(QStringLiteral("\\s")))) { hideSlashPopup(); return; }
        const QString query = text.mid(1).toLower();
        struct Ranked { int score; int index; };
        QList<Ranked> ranked;
        const auto &commands = slashCommands();
        for (int i = 0; i < commands.size(); ++i) {
            // Name prefix first, then names containing the query; descriptions do not match.
            const QString &name = commands[i].name;
            const int score = name.startsWith(query) ? 3000 - i : name.contains(query) ? 2000 - i : 0;
            if (score > 0) ranked.append({score, i});
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) { return a.score > b.score; });
        if (ranked.isEmpty()) { hideSlashPopup(); return; }
        if (!m_slashList) {
            m_slashList = new QListWidget(this);
            m_slashList->setObjectName(QStringLiteral("atPicker"));
            m_slashList->setFocusPolicy(Qt::NoFocus);
            m_slashList->setUniformItemSizes(true);
            connect(m_slashList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { m_slashList->setCurrentItem(item); acceptSlashSelection(true); });
        }
        m_slashList->clear();
        for (const auto &entry : std::as_const(ranked)) {
            const auto &command = commands[entry.index];
            auto *item = new QListWidgetItem(QStringLiteral("/%1 %2    %3").arg(command.name, command.args, command.description).simplified(), m_slashList);
            item->setData(Qt::UserRole, command.name);
            item->setData(Qt::UserRole + 1, !command.args.isEmpty() && command.args.startsWith('<'));
        }
        m_slashList->setCurrentRow(0);
        if (m_composer) {
            const QRect composer(m_composer->mapTo(this, QPoint(0, 0)), m_composer->size());
            const int rowHeight = std::max(18, m_slashList->sizeHintForRow(0));
            const int height = std::min(9, m_slashList->count()) * rowHeight + 8;
            m_slashList->setGeometry(composer.left() + 12, std::max(0, composer.top() - height - 4), std::min(640, composer.width() - 24), height);
        }
        m_slashList->show();
        m_slashList->raise();
        if (m_editor->ghost().size()) m_editor->setGhost(QString());
    }

    // The Relay command being typed: "/co" while it is a prefix of a command name, or "/compact args".
    static const SlashCommand *slashCommandFor(const QString &text) {
        static const QRegularExpression pattern(QStringLiteral("^/([a-z-]*)(\\s[\\s\\S]*)?$"));
        const auto match = pattern.match(text);
        if (!match.hasMatch()) return nullptr;
        const QString name = match.captured(1);
        const bool hasArgs = match.capturedLength(2) > 0;
        const SlashCommand *prefix = nullptr;
        for (const auto &command : slashCommands()) {
            if (command.name == QStringLiteral("shell") || command.name == QStringLiteral("agent")) continue;
            if (command.name == name) return &command;
            if (!hasArgs && !prefix && command.name.startsWith(name)) prefix = &command;
        }
        return prefix;
    }

    void hideSlashPopup() { if (m_slashList && m_slashList->isVisible()) m_slashList->hide(); }

    // Enter runs the selected command (commands that need an argument are completed instead); Tab completes.
    void acceptSlashSelection(bool run) {
        if (!m_slashList || !m_slashList->currentItem()) return;
        const QString name = m_slashList->currentItem()->data(Qt::UserRole).toString();
        const bool needsArgument = m_slashList->currentItem()->data(Qt::UserRole + 1).toBool();
        hideSlashPopup();
        if (!run || needsArgument) {
            m_editor->setPlainText(QStringLiteral("/") + name + ' ');
            m_editor->moveCursor(QTextCursor::End);
            return;
        }
        m_editor->clear();
        runSlashCommand(name, QString());
    }

    // True when `text` is a Relay slash command (not the router's /shell and /agent prefixes).
    bool tryRunSlashCommand(const QString &text) {
        static const QRegularExpression pattern(QStringLiteral("^/([a-z]+)(?:\\s+([\\s\\S]*))?$"));
        const auto match = pattern.match(text.trimmed());
        if (!match.hasMatch()) return false;
        const QString name = match.captured(1);
        if (name == QStringLiteral("shell") || name == QStringLiteral("agent")) return false;
        if (std::none_of(slashCommands().cbegin(), slashCommands().cend(), [&](const auto &c) { return c.name == name; })) return false;
        m_editor->remember(text.trimmed());
        m_editor->clear();
        hideSlashPopup();
        runSlashCommand(name, match.captured(2).trimmed());
        return true;
    }

    void runSlashCommand(const QString &name, const QString &args) {
        if (name == QStringLiteral("new") || name == QStringLiteral("clear")) newChat();
        else if (name == QStringLiteral("model")) {
            if (!args.isEmpty()) {
                for (const auto &model : std::as_const(m_stored)) {
                    if (model.first.compare(args, Qt::CaseInsensitive) == 0 || model.second.contains(args, Qt::CaseInsensitive)) { selectModel(model.first); return; }
                }
                status(QStringLiteral("No stored model matches “%1”.").arg(args));
                return;
            }
            QList<relay::agentui::PickerRow> rows;
            for (const auto &model : std::as_const(m_stored)) rows << relay::agentui::PickerRow{{model.second, model.first == m_currentPreset ? QStringLiteral("current") : QString()}, model.first, model.first};
            const auto result = relay::agentui::pick(this, QStringLiteral("Model"), QStringLiteral("Switch this pane's model. The conversation is kept."),
                                                     {QStringLiteral("Model"), QString()}, rows, {{QStringLiteral("use"), QStringLiteral("Use"), true}});
            if (result.row >= 0) selectModel(m_stored.at(result.row).first);
        } else if (name == QStringLiteral("effort")) {
            if (efforts().contains(args.toLower())) setEffort(args.toLower());
            else if (args.isEmpty()) effortStep(1 - (efforts().indexOf(m_effort) == efforts().size() - 1 ? 4 : 0));
            else status(QStringLiteral("Effort must be low, medium, high or max."));
        } else if (name == QStringLiteral("compact")) compactNow(args);
        else if (name == QStringLiteral("context")) {
            if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
            m_contextNotePending = true;
            send({{"type", "context"}});
        } else if (name == QStringLiteral("rewind")) openRewind();
        else if (name == QStringLiteral("rewind-code")) openRewind(QStringLiteral("code"));
        else if (name == QStringLiteral("fork")) requestFork();
        else if (name == QStringLiteral("resume")) openResume();
        else if (name == QStringLiteral("conversations")) {
            openConversations(args);
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("conversations.open")); !keys.isEmpty())
                hint(QStringLiteral("conversations.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("conversations")));
        } else if (name == QStringLiteral("find")) {
            openFindInView();
            if (!args.isEmpty() && m_findBar) m_findBar->start(args);
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("find.inView")); !keys.isEmpty())
                hint(QStringLiteral("find.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("find in this pane")));
        }
        else if (name == QStringLiteral("plan")) togglePlanMode();
        else if (name == QStringLiteral("recap")) requestRecap();
        else if (name == QStringLiteral("tasks") || name == QStringLiteral("requests") || name == QStringLiteral("todos")) {
            openRequests();
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.requests")); !keys.isEmpty() && requestsOpen())
                hint(QStringLiteral("tasks.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("task list")));
        }
        else if (name == QStringLiteral("continue")) continueTurn();
        else if (name == QStringLiteral("instructions")) openInstructions();
        else if (name == QStringLiteral("export")) exportConversation();
        else if (name == QStringLiteral("agents")) {
            if (onShowAgents) onShowAgents();
            else { m_agentsListPending = true; send({{"type", "agents_list"}, {"workspace", m_workspace}}); }
        } else if (name == QStringLiteral("skills")) {
            openSkills();
        }
    }

    // ----- steering, away recaps, AI suggestions -------------------------------------------------
    struct SteerEntry { QString requestId, itemId, text; QJsonArray attachments; };

    // Enter on an empty prompt right after queuing an agent prompt while the agent works: deliver that
    // prompt at the agent's next tool call instead of after the turn.
    bool upgradeLastQueuedToSteer() {
        if (!m_agentBusy || m_entries.isEmpty() || !m_lastQueuedAt.isValid() || m_lastQueuedAt.elapsed() > 15000) return false;
        const QueueEntry &last = m_entries.last();
        if (!last.agent || last.fix || last.id != m_lastQueuedEntryId) return false;
        QueueEntry entry = m_entries.takeLast();
        m_lastQueuedAt.invalidate();
        SteerEntry steer;
        steer.requestId = QStringLiteral("steer-%1").arg(++m_askSerial);
        steer.text = entry.text; steer.attachments = entry.attachments;
        m_steering.append(steer);
        QJsonObject request{{"type", "ask"}, {"id", steer.requestId}, {"text", entry.text}, {"when", "steer"}, {"requeue", false}};
        if (!entry.attachments.isEmpty()) request.insert(QStringLiteral("attachments"), entry.attachments);
        send(request);
        m_lastSteerRequest = steer.requestId; m_lastSteeredAt.start();
        rebuildQueueStrip(); changed();
        toast(QStringLiteral("Steering · delivered at the agent's next tool call · Enter again to interrupt and send now"));
        return true;
    }

    // A third Enter on the empty prompt box, right after the steer: stop the running turn and run
    // that prompt as its own turn instead. The worker decides: once the turn has taken the steer
    // (or given it back) the agent already has the prompt and there is nothing to interrupt for,
    // so it answers steer_escalated {escalated: false}. The reply carries the request id reserved
    // here, which keeps the prompt echo wired up the way startAgentEntry() does.
    bool escalateSteerToInterrupt() {
        if (!m_agentBusy || !m_lastSteeredAt.isValid() || m_lastSteeredAt.elapsed() > 15000) return false;
        const auto pending = std::find_if(m_steering.cbegin(), m_steering.cend(),
            [this](const SteerEntry &steer) { return steer.requestId == m_lastSteerRequest; });
        if (pending == m_steering.cend()) return false;
        m_lastSteeredAt.invalidate();
        PendingPrompt prompt; prompt.text = pending->text;
        const QString requestId = QStringLiteral("ask-%1").arg(++m_askSerial);
        m_pendingPrompts.insert(requestId, prompt);
        m_interruptPending = true;   // set before the stop, so agent_finished does not pause the queue
        send({{"type", "queue_unsteer"}, {"request", pending->requestId}, {"as_request", requestId}});
        status(QStringLiteral("Interrupting the current turn to send it now…"));
        return true;
    }

    void noteWindowActivation(bool active) {
        if (!active) {
            if (!m_awaySince.isValid()) m_awaySince.start();
            return;
        }
        const bool wasAway = m_awaySince.isValid();
        const qint64 awayMs = wasAway ? m_awaySince.elapsed() : 0;
        m_awaySince.invalidate();
        const qint64 threshold = qEnvironmentVariableIsSet("RELAY_RECAP_AWAY_SECONDS")
            ? qEnvironmentVariableIntValue("RELAY_RECAP_AWAY_SECONDS") * 1000LL : 180000LL;
        if (!wasAway || awayMs < threshold || !m_finishedWhileAway) return;
        m_finishedWhileAway = false;
        if (!QSettings().value(QStringLiteral("recap/away"), true).toBool() || !m_configured || m_agentBusy) return;
        if (!m_editor->toPlainText().isEmpty() || m_turnsCompleted == m_lastRecapTurns || m_turnsCompleted < 3) return;
        send({{"type", "recap_request"}, {"reason", "away"}});
    }

    void requestSuggestion(const QString &kind, const QJsonObject &fields = QJsonObject()) {
        if (!m_configured || !m_editor->toPlainText().isEmpty()) return;
        m_suggestionId = QStringLiteral("suggest-%1").arg(++m_askSerial);
        QJsonObject request{{"type", "suggest"}, {"kind", kind}, {"id", m_suggestionId}};
        for (auto it = fields.begin(); it != fields.end(); ++it) request.insert(it.key(), it.value());
        send(request);
    }

    void clearAiGhost() {
        if (m_aiGhost.isEmpty()) return;
        m_aiGhost.clear(); m_aiGhostKind.clear(); m_suggestionId.clear();
        if (m_editor && m_editor->toPlainText().isEmpty()) m_editor->setGhost(QString());
        updateGhost();
    }
    // ----- subagents UI -------------------------------------------------------------------------
    void setupSubagentsUi(QVBoxLayout *layout) {
        // Floats over the bottom of the terminal like the queue strip: taking layout space would
        // resize the terminal, and Readline then redraws its prompt in the middle of agent output.
        Q_UNUSED(layout);
        m_agentsPanel = new relay::SubagentsPanel(&m_subagents, this);
        m_agentsPanel->onOpen = [this](const QString &id) { openSubagent(id); };
        m_agentsPanel->onStop = [this](const QString &id) { stopSubagent(id); toast(QStringLiteral("Stopping ") + id); };
        m_agentsPanel->onExit = [this] { focusInput(); };
        m_subagents.onChanged = [this] {
            m_agentsPanel->refresh();
            placeSubagentsPanel();
            for (const auto &view : std::as_const(m_subagentViews))
                if (view) if (const auto *row = m_subagents.row(view->agentId())) view->setRow(*row, m_subagents.elapsedNow(*row));
            if (m_modelBox) m_modelBox->setToolTip(modelTooltip(QStringLiteral("Tokens: ") + m_subagents.tokenSplit()));
        };
        // Only one start and one finish line per subagent reach the terminal, never its tool activity.
        m_subagents.onInline = [this](const QString &line) {
            ensureLineStart();
            printInline(line + '\n', Ink::Note);
            // A wake-up turn often follows a finish line; let it start before redrawing the prompt.
            QTimer::singleShot(400, this, [this] { if (!m_agentBusy && !moreTurnsPending()) closeInline(); });
        };
        m_subagents.onFinished = [this](const relay::SubagentRow &row) {
            notifyIfAway(QStringLiteral("Agent %1 %2").arg(row.type + ' ' + row.id, row.status), row.summary.left(200));
        };
        m_subagents.onTranscript = [this](const QString &id, const QJsonObject &event) {
            for (const auto &view : std::as_const(m_subagentViews)) if (view && view->agentId() == id) view->handleEvent(event);
        };
        m_subagents.onStatus = [this](const QString &text) { status(text); };
    }

    void openSubagentOverlay(const QString &id) {
        if (m_subagentOverlay) {
            if (m_subagentOverlay->agentId() == id) { m_subagentOverlay->focusInput(); return; }
            delete m_subagentOverlay.data();
        }
        auto *view = new relay::SubagentTranscriptView(id, this);
        m_subagentOverlay = view;
        QPointer<relay::SubagentTranscriptView> guard(view);
        view->onClose = [this, guard] { if (guard) guard->deleteLater(); focusInput(); };
        attachSubagentView(view);
        placeSubagentOverlay();
        view->show(); view->raise();
        view->focusInput();
    }

    void placeSubagentsPanel() {
        if (!m_agentsPanel || !m_terminalHost) return;
        m_agentsPanel->setAllowed(!m_composer || m_composer->isVisible());
        if (!m_agentsPanel->isVisible()) { placeQueueStrip(); return; }
        const QRect host(m_terminalHost->mapTo(this, QPoint(0, 0)), m_terminalHost->size());
        const int height = std::min(m_agentsPanel->sizeHint().height(), host.height() / 2);
        m_agentsPanel->setGeometry(host.left() + 8, host.bottom() - height - 6, host.width() - 16, height);
        m_agentsPanel->raise();
        placeQueueStrip();
    }

    void placeSubagentOverlay() {
        if (!m_subagentOverlay) return;
        const int w = std::min(width() - 16, std::max(340, width() * 3 / 5));
        m_subagentOverlay->setGeometry(width() - w - 8, 8, w, std::max(160, height() - 16));
    }
    // ----- end subagents UI ---------------------------------------------------------------------

    // ----- request ledger UI (protocol section 12) ----------------------------------------------
    void setupRequestsUi(QHBoxLayout *row) {
        m_routeRow = row;
        m_requestsChip = new QToolButton;
        m_requestsChip->setObjectName(QStringLiteral("requestsChip"));
        m_requestsChip->setFocusPolicy(Qt::NoFocus);
        m_requestsChip->setAccessibleName(QStringLiteral("Tasks"));
        m_requestsChip->hide();
        row->insertWidget(3, m_requestsChip);   // after the PLAN chip and the context label
        connect(m_requestsChip, &QToolButton::clicked, this, [this] {
            toggleRequests();
            hint(QStringLiteral("tasks.chip"), requestsShortcutHint());
        });
        m_ledger.onChanged = [this] {
            updateRequestsChip();
            if (m_requestsPanel) m_requestsPanel->refresh();
        };
    }

    QString requestsShortcutHint() const {
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.requests"));
        return keys.isEmpty() ? QStringLiteral("Next time: /tasks in the prompt box opens the task list")
                              : relay::ShortcutHints::nextTime(keys, QStringLiteral("task list"));
    }

    void updateRequestsChip() {
        if (!m_requestsChip) return;
        const bool show = m_ledger.hasTasks();
        m_requestsChip->setText(m_ledger.chipText());
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.requests"));
        m_requestsChip->setToolTip(m_ledger.chipToolTip() + QStringLiteral("\nClick, /tasks%1: task list, mark done, cancel, re-ask")
                                       .arg(keys.isEmpty() ? QString() : QStringLiteral(" or ") + keys));
        m_requestsChip->setProperty("state", m_ledger.chipState());
        m_requestsChip->style()->unpolish(m_requestsChip); m_requestsChip->style()->polish(m_requestsChip);
        const bool wasVisible = m_requestsChip->isVisible();
        m_requestsChip->setVisible(show);
        if (wasVisible != show && m_queueStrip && m_queueStrip->isVisible()) rebuildQueueStrip();
    }

public:
    bool requestsOpen() const { return m_requestsPanel && m_requestsPanel->isVisible(); }
    void toggleRequests() { if (requestsOpen()) closeRequests(); else openRequests(); }

    void openRequests() {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        if (!m_requestsPanel) {
            auto *panel = new relay::RequestsPanel(&m_ledger, this);
            m_requestsPanel = panel;
            panel->onSetStatus = [this](const QString &ledgerId, const QString &state) {
                send({{"type", "request_set"}, {"id", QStringLiteral("req-%1").arg(++m_requestId)}, {"ledger_id", ledgerId}, {"status", state}});
                toast(QStringLiteral("%1 %2").arg(ledgerId, relay::RequestLedgerModel::statusLabel(state == QStringLiteral("open") ? QStringLiteral("reopened") : state)));
            };
            panel->onReask = [this](const QString &ledgerId) { reaskRequest(ledgerId); };
            panel->onFetch = [this](const QString &ledgerId) {
                send({{"type", "request_get"}, {"id", QStringLiteral("req-%1").arg(++m_requestId)}, {"ledger_id", ledgerId}});
            };
            panel->onClose = [this] { closeRequests(); };
        }
        send({{"type", "requests"}, {"id", QStringLiteral("req-%1").arg(++m_requestId)}});
        send({{"type", "todos"}, {"id", QStringLiteral("req-%1").arg(++m_requestId)}});
        m_requestsPanel->refresh();
        placeRequestsPanel();
        m_requestsPanel->show();
        m_requestsPanel->raise();
        m_requestsPanel->enter();
    }

    void closeRequests() {
        if (m_requestsPanel) m_requestsPanel->hide();
        focusInput();
    }

    // "Continue" after a turn stopped at its step or tool-call limit: an ordinary ask.
    void continueTurn(bool slowPath = false) {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        m_limitReached = false;
        submitAgent(QStringLiteral("Continue"), false);
        if (slowPath) {
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.continue"));
            hint(QStringLiteral("continue.slow"), keys.isEmpty() ? QStringLiteral("Next time: /continue in the prompt box")
                                                               : relay::ShortcutHints::nextTime(keys, QStringLiteral("continue")));
        }
    }
    bool limitReached() const { return m_limitReached; }
    int openRequestCount() const { return m_ledger.openCount(); }
    QString tasksProgress() const { return m_ledger.hasTasks() ? m_ledger.chipText() : QString(); }

private:
    void reaskRequest(const QString &ledgerId) {
        const relay::LedgerRequest *request = m_ledger.find(ledgerId);
        if (!request) return;
        const QString requestId = QStringLiteral("ask-%1").arg(++m_askSerial);
        PendingPrompt prompt; prompt.text = request->fullText();
        m_pendingPrompts.insert(requestId, prompt);
        send({{"type", "request_reask"}, {"id", requestId}, {"ledger_id", ledgerId}, {"when", "queue"}});
        toast(QStringLiteral("Re-asked %1 · %2").arg(ledgerId, m_agentBusy ? QStringLiteral("queued") : QStringLiteral("starting")));
    }

    void placeRequestsPanel() {
        if (!m_requestsPanel || !m_terminalHost) return;
        const QRect host(m_terminalHost->mapTo(this, QPoint(0, 0)), m_terminalHost->size());
        const int w = std::min(host.width() - 16, std::max(380, host.width() * 3 / 5));
        m_requestsPanel->setGeometry(host.right() - w - 8, host.top() + 8, w, std::max(180, host.height() - 16));
    }

    // Terminal-output lines for the end of a turn: the limit with a Continue link, open items.
    void printTurnEndRequests(const QString &type, const QJsonObject &event) {
        if (type == QStringLiteral("done")) m_limitReached = event.value(QStringLiteral("stop_reason")).toString() == QStringLiteral("limit");
        if (m_limitReached && type == QStringLiteral("done")) {
            ensureLineStart();
            printInline(relay::RequestLedgerModel::limitLine(event) + '\n', Ink::Tool);
            printContinueLink();
        }
        // The worker's `requests`/`todos` events precede done/cancelled/error, so the model is current.
        const QString line = m_ledger.turnEndLine();
        if (!line.isEmpty()) { ensureLineStart(); printInline(QStringLiteral("✦ ") + line + QStringLiteral("  · /tasks\n"), Ink::Note); }
    }

    // "▸ Continue" as a terminal hyperlink (relay://continue/<pane>), like the tool-calls link.
    void printContinueLink() {
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.continue"));
        const QString fast = keys.isEmpty() ? QStringLiteral("/continue") : keys + QStringLiteral(" or /continue");
        if (!shellIdleAtPrompt()) { printInline(QStringLiteral("▸ Continue: %1 (Actions › Continue agent turn)\n").arg(fast), Ink::Note); return; }
        const QByteArray url = QStringLiteral("relay://continue/%1").arg(m_token).toUtf8();
        QByteArray out;
        if (!m_inlineOpen) { out += "\r\x1b[2K"; m_inlineOpen = true; m_atLineStart = true; }
        if (!m_atLineStart) out += "\r\n";
        out += "\x1b]8;;" + url + "\x1b\\" + inkCode(Ink::User) + QByteArray("▸ Continue") + "\x1b[0m" + "\x1b]8;;\x1b\\";
        out += inkCode(Ink::Note) + QStringLiteral("  (Ctrl+click · %1)").arg(fast).toUtf8() + "\x1b[0m\r\n";
        m_atLineStart = true;
        writeTerminal(out);
    }

    bool handleRequestsEvent(const QString &type, const QJsonObject &event) {
        if (type == QStringLiteral("ready")) { m_ledger.clear(); m_limitReached = false; return false; }
        if (m_ledger.handle(event)) {
            if (type == QStringLiteral("request_audit")) {
                const QString line = relay::RequestLedgerModel::auditLine(event);
                if (!line.isEmpty()) {
                    ensureLineStart();
                    printInline(QStringLiteral("⚠ ") + line + QStringLiteral("  · /tasks\n"), Ink::Note);
                    if (!m_agentBusy && !moreTurnsPending()) closeInline();
                }
            }
            return true;
        }
        if (type == QStringLiteral("completion_check")) {
            ensureLineStart();
            printInline(relay::RequestLedgerModel::completionCheckLine(event) + '\n', Ink::Note);
            return true;
        }
        return false;
    }
    // ----- end request ledger UI ------------------------------------------------------------------

    void startWorker() {
        if (!m_workerConnected) connectWorker();
        m_workerBuffer.clear(); m_workerPending.clear();
        const QStringList command{m_python, QStringLiteral("-S"), QStringLiteral("-u"), m_data + QStringLiteral("/backend/worker.py")};
        m_agentUnit.clear();
        if (isolation::enabled() && isolation::available()) {
            m_agentUnit = QStringLiteral("relay-pane-%1-agent-%2").arg(m_token.left(8)).arg(++m_agentGeneration);
            m_worker.setProgram(QStandardPaths::findExecutable(QStringLiteral("systemd-run")));
            m_worker.setArguments(isolation::wrap(m_agentUnit, {QStringLiteral("MemoryMax=") + isolation::memory("isolation/agent_memory_max", "2G"),
                                                                QStringLiteral("MemorySwapMax=") + isolation::memory("isolation/agent_swap_max", "512M"),
                                                                QStringLiteral("TimeoutStopSec=5"),
                                                                QStringLiteral("OOMPolicy=stop")}, command));
        } else {
            m_worker.setProgram(command.first());
            m_worker.setArguments(command.mid(1));
        }
        m_worker.start();
    }

    void connectWorker() {
        m_workerConnected = true;
        connect(&m_worker, &QProcess::readyReadStandardOutput, this, [this] {
            m_workerBuffer += m_worker.readAllStandardOutput();
            if (m_workerBuffer.size() > 8 * 1024 * 1024) {
                m_worker.kill(); status(QStringLiteral("Worker protocol overflow; stopped.")); return;
            }
            int index;
            while ((index = m_workerBuffer.indexOf('\n')) >= 0) {
                const auto line = m_workerBuffer.left(index); m_workerBuffer.remove(0, index + 1);
                QJsonParseError error;
                const auto doc = QJsonDocument::fromJson(line, &error);
                if (error.error == QJsonParseError::NoError && doc.isObject()) handle(doc.object());
            }
        });
        connect(&m_worker, &QProcess::readyReadStandardError, this, [this] {
            // No provider keys or arbitrary provider error bodies are logged.
            m_worker.readAllStandardError();
        });
        connect(&m_worker, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            status(QStringLiteral("Local worker failed: ") + m_worker.errorString());
        });
        connect(&m_worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
            m_workerReady = false; m_configured = false; m_agentBusy = false;
            if (m_closing) return;
            const bool oom = isolation::takeResult(m_agentUnit) == QStringLiteral("oom-kill");
            const bool killed = exit == QProcess::CrashExit || code == 137 || code == 143;
            showBanner(oom ? QStringLiteral("The agent worker stopped because it ran out of memory (limit %1).")
                                 .arg(isolation::memory("isolation/agent_memory_max", "2G"))
                           : killed ? QStringLiteral("The agent worker was stopped.") : QStringLiteral("The agent worker exited."),
                       QStringLiteral("Restart agent"), [this] { hideBanner(); startWorker(); });
        });
        connect(&m_worker, &QProcess::started, this, [this] {
            for (const auto &line : std::as_const(m_workerPending)) m_worker.write(line);
            m_workerPending.clear();
        });
    }

    void startTerminal(bool cleanShell) {
        // Set before the backend starts its shell so the child inherits these values.
        qputenv("RELAY_RUNTIME_DIR", m_runtime.path().toUtf8());
        qputenv("RELAY_SESSION_TOKEN", m_token.toUtf8());
        qputenv("RELAY_SHELL_EVENT", (m_data + QStringLiteral("/shell/event.py")).toUtf8());
        qputenv("RELAY_PYTHON", m_python.toUtf8());
        qputenv("RELAY_CLEAN_SHELL", cleanShell ? "1" : "0");
        // Opt-in OSC 7 / OSC 133 marks (shell/relay-integration.bash). Relay's own engine
        // tracks the working directory and command boundaries from them; Konsole ignores them.
        qputenv("RELAY_SHELL_INTEGRATION",
                QSettings().value(QStringLiteral("terminal/shell_integration"), false).toBool() ? "1" : "0");
        m_backendOwned.reset(relay::createTerminalBackend(m_engine, m_engineCore, m_terminalHost));
        m_backend = m_backendOwned.get();
        m_terminal = m_backend->widget();
        m_terminalHost->layout()->addWidget(m_terminal);
        // The prompt box is the only keyboard input: the terminal does not take focus on click.
        m_terminalFocusPolicy = Qt::NoFocus;
        applyTerminalFocusPolicy();
        m_backend->onFinished = [this](int) {
            m_backend = nullptr; m_terminal = nullptr; m_shellReady = false;
            // The backend outlives this callback; drop it once the stack has unwound.
            QTimer::singleShot(0, this, [this] { if (!m_backend) m_backendOwned.reset(); });
            if (m_closing || m_restarting) return;
            // A shell stopped for memory (its scope's limit or systemd-oomd) keeps the pane open
            // with a restart banner. Any other exit closes the pane, like other terminals.
            const bool oom = isolation::takeResult(m_shellUnit) == QStringLiteral("oom-kill");
            if (oom) {
                showBanner(QStringLiteral("This pane's shell was stopped because it ran out of memory (limit %1).")
                               .arg(isolation::memory("isolation/shell_memory_max", "8G")),
                           QStringLiteral("Restart shell"), [this] { restartShell(); });
                return;
            }
            if (onShellExited) QTimer::singleShot(0, this, [this] { if (onShellExited) onShellExited(); });
        };
        // Alternate screen (vim, less, htop, tmux): KonsolePart reports it through its Session
        // signal, the Relay engine through the emulator itself.
        m_backend->onAltScreenChanged = [this](bool active) { onPrimaryScreen(!active); };
        // OSC 7 from the shell integration (engine panes; see shell/relay-integration.bash).
        m_backend->onCwdChanged = [this](const QString &path) {
            if (path.isEmpty() || path == m_cwd || !QFileInfo(path).isDir()) return;
            m_cwd = path; updatePaths(); changed();
        };
        // OSC 133 prompt marks: Relay keeps its own command state from the Bash bridge, so the
        // marks are only remembered here (engine panes use them to jump between prompts).
        m_backend->onPromptMark = [this](char kind, int exitCode) {
            m_lastPromptMark = kind;
            if (kind == 'D') m_lastMarkExitCode = exitCode;
        };
        // Output of the commands Relay itself ran, for the conversation index (protocol 14).
        // Only enabled between "command loaded" and "shell ready", so it costs nothing otherwise.
        m_backend->setOutputCallbackEnabled(false);
        m_backend->onOutput = [this](const QByteArray &bytes) {
            if (!m_capturing || m_capture.size() >= kCommandCaptureCap) return;
            m_capture.append(bytes.left(kCommandCaptureCap - m_capture.size()));
        };
        m_backend->onLinkActivated = [this](const QString &target, int line, int column) {
            if (target.startsWith(QStringLiteral("http")) || target.startsWith(QStringLiteral("relay://"))) {
                QDesktopServices::openUrl(QUrl(target));
                return;
            }
            Q_UNUSED(line); Q_UNUSED(column);
            if (onOpenPath && QFileInfo::exists(target)) onOpenPath(target);
        };
        // The part has loaded Relay's profile; the shell should see the user's own XDG paths.
        relay::theme::restoreXdgEnvironment();
        // Konsole starts new sessions in its own default directory, so the Bash integration
        // changes to this pane's directory after loading the user's configuration.
        qputenv("RELAY_START_DIR", m_cwd.toUtf8());
        const QStringList shell{QStringLiteral("/bin/bash"), QStringLiteral("--noprofile"),
            QStringLiteral("--rcfile"), m_data + QStringLiteral("/shell/integration.bash"), QStringLiteral("-i")};
        m_shellUnit.clear();
        bool started = false;
        if (isolation::enabled() && isolation::available()) {
            // OOMPolicy=continue (default): when a command exceeds the limit, the kernel stops that
            // command and the shell keeps running; Relay reports the kill from memory.events.
            // isolation/shell_oom_policy=stop ends the whole pane shell instead (restart banner).
            m_shellUnit = QStringLiteral("relay-pane-%1-shell-%2").arg(m_token.left(8)).arg(++m_shellGeneration);
            const QString tool = QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
            started = m_backend->startProgram(tool, isolation::wrap(m_shellUnit,
                {QStringLiteral("MemoryMax=") + isolation::memory("isolation/shell_memory_max", "8G"),
                 QStringLiteral("MemoryHigh=") + isolation::memory("isolation/shell_memory_high", "6G"),
                 QStringLiteral("MemorySwapMax=") + isolation::memory("isolation/shell_swap_max", "2G"),
                 // Interactive bash ignores SIGTERM; SIGHUP ends it (and its jobs) when the scope stops.
                 QStringLiteral("KillSignal=SIGHUP"), QStringLiteral("TimeoutStopSec=5"),
                 QStringLiteral("OOMPolicy=") + (QSettings().value(QStringLiteral("isolation/shell_oom_policy")).toString() == QStringLiteral("stop")
                                                     ? QStringLiteral("stop") : QStringLiteral("continue"))}, shell), m_cwd);
        } else {
            if (isolation::enabled() && !s_isolationNoticeShown) {
                s_isolationNoticeShown = true;
                QTimer::singleShot(1500, this, [this] { status(QStringLiteral("Per-pane memory isolation is unavailable (no systemd user session); panes run unisolated.")); });
            }
            started = m_backend->startProgram(shell.first(), shell.mid(1), m_cwd);
        }
        if (!started) throw std::runtime_error("The pane's shell could not be started.");
        m_oomKills = -1;
    }

    void send(const QJsonObject &object) {
        const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        if (m_worker.state() == QProcess::Running) m_worker.write(line);
        // The worker's first output can be handled before QProcess reports Running.
        // Writes in that window used to be dropped silently; hold them until started().
        else if (m_worker.state() == QProcess::Starting) m_workerPending.append(line);
    }

    void requestRoute(bool submit, const QString &overrideMode) {
        if (m_native) {
            if (submit) status(QStringLiteral("Native input is active. Press %1 or F12 to return to the prompt box.")
                                   .arg(Keymap::instance().shortcutText(QStringLiteral("control.prompt"))));
            return;
        }
        if (submit) {
            // `@path` on its own opens the file (or folder) in a Relay pane.
            static const QRegularExpression only(QStringLiteral("^@(?:\"([^\"]+)\"|(\\S+))$"));
            const auto match = only.match(m_editor->toPlainText().trimmed());
            if (match.hasMatch()) {
                const QString absolute = resolveComposerPath(match.captured(1).isEmpty() ? match.captured(2) : match.captured(1));
                if (!absolute.isEmpty() && onOpenPath) {
                    m_editor->remember(m_editor->toPlainText().trimmed());
                    m_editor->clear();
                    hideAtPopup();
                    m_recentFiles.removeAll(absolute); m_recentFiles.prepend(absolute);
                    while (m_recentFiles.size() > 20) m_recentFiles.removeLast();
                    onOpenPath(absolute);
                    return;
                }
            }
            if (m_atList && m_atList->isVisible()) hideAtPopup();
            if (tryRunSlashCommand(m_editor->toPlainText())) return;
            clearAiGhost();
        } else if (const SlashCommand *command = slashCommandFor(m_editor->toPlainText())) {
            m_routeLabel->setText(QStringLiteral("COMMAND · /%1 · %2").arg(command->name, command->description));
            return;
        }
        if (!m_workerReady) {
            if (submit) status(QStringLiteral("Local router is not ready; use the native terminal or restart Relay."));
            return;
        }
        if (submit && (!m_pendingSubmit.isEmpty() || !m_heldDecision.isEmpty() || m_loading)) return;
        const QString id = QString::number(++m_requestId);
        QString mode = overrideMode == QStringLiteral("auto") ? m_modeValue : overrideMode;
        if (submit && overrideMode == QStringLiteral("auto") && !m_editKind.isEmpty()) mode = m_editKind;
        if (submit) m_editKind.clear();
        // A program is blocked reading a line from the terminal: the prompt box answers it
        // instead of queueing a command (issue decision 5). Agent submissions still go to the
        // agent, so Ctrl+Enter and `*` keep working while a program waits.
        if (submit && sendLineToProgram(mode)) {
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
            return;
        }
        if (submit) {
            const QString typed = m_editor->toPlainText();
            if (typed.startsWith(QStringLiteral("/shell ")))
                hint(QStringLiteral("prefix.bang"), QStringLiteral("Next time: type ! at the start of the prompt for the terminal"));
            else if (typed.startsWith(QStringLiteral("/agent ")))
                hint(QStringLiteral("prefix.star"), QStringLiteral("Next time: type * at the start of the prompt for the agent"));
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);   // one submission only
            m_submitMode = mode;
            m_pendingSubmit = id; m_submittedDraft = typed;
        } else m_previewId = id;
        send({{"type", "route"}, {"id", id}, {"text", m_editor->toPlainText()}, {"mode", mode},
              {"known_commands", m_knownCommands}, {"path", m_shellPath}, {"cwd", m_cwd}});
    }

    // The rule for a submitted line while a foreground program is running: it is written to
    // that program's stdin when the terminal is in canonical (line) mode and a process of the
    // command is blocked reading it. Anything else keeps the existing behaviour — run it now,
    // or queue it until the terminal is free. Never remembered (relay::input::retainable).
    bool sendLineToProgram(const QString &mode) {
        if (m_native || !m_backend || m_secretMode) return false;
        if (relay::input::targetFor(inputState(), mode) != relay::input::LineTarget::Program) return false;
        const QString text = m_editor->toPlainText();
        if (text.contains('\n')) return false;   // a multi-line draft is not an answer to a prompt
        const QString program = foregroundProgramName();
        m_editor->clear();
        hideAtPopup();
        clearAiGhost();
        sendShellInput(text + '\n');
        m_waitTicks = 0;
        endWaiting(false);
        status(relay::input::sentToProgram(program));
        toast(relay::input::sentToProgram(program));
        return true;
    }

    void handle(const QJsonObject &event) {
        const QString type = event.value(QStringLiteral("event")).toString();
        // --- subagents UI: subagent_* events are consumed; main-agent state is observed first ---
        if (type == QStringLiteral("configured")) QTimer::singleShot(0, this, [this] { refreshAgentDefinitions(); });
        if (m_subagents.handle(event)) return;
        // --- end subagents UI ---
        if (handleRequestsEvent(type, event)) return;   // request ledger UI
        if (handleObservabilityEvent(type, event)) return;
        if (handleSessionEvent(type, event)) return;
        if (type == QStringLiteral("ready")) {
            m_workerReady = true; requestRoute(false, QStringLiteral("auto"));
            send({{"type", "presets"}});
        } else if (type == QStringLiteral("route")) {
            const QString id = event.value(QStringLiteral("id")).toString();
            const QString route = event.value(QStringLiteral("route")).toString();
            const bool needsAssist = event.value(QStringLiteral("needs_assist")).toBool()
                && (id == m_pendingSubmit ? m_submitMode : m_modeValue) == QStringLiteral("auto");
            const QString routedText = event.value(QStringLiteral("text")).toString(id == m_pendingSubmit ? m_submittedDraft : m_editor->toPlainText());
            const bool haveAssist = needsAssist && m_assistText == routedText && !m_assistRoute.isEmpty();
            if (id == m_previewId && needsAssist) {
                if (haveAssist) showAssistLabel();
                else {
                    // Show the local guess now; "checking…" only if the model has not answered within ~150 ms.
                    const QString why = event.value(QStringLiteral("assist_reason")).toString(event.value(QStringLiteral("reason")).toString());
                    const QString guess = route == QStringLiteral("shell") ? QStringLiteral("TERMINAL") : QStringLiteral("AGENT");
                    m_routeLabel->setText(QStringLiteral("%1 · local guess · %2").arg(guess, why));
                    m_routeLabel->setToolTip(why);
                    QTimer::singleShot(150, this, [this, text = routedText, guess, why] {
                        if (m_editor->toPlainText() == text && !(m_assistText == text && !m_assistRoute.isEmpty()) && m_assistFailedText != text)
                            m_routeLabel->setText(QStringLiteral("AUTO · checking… · local guess: %1 · %2").arg(guess.toLower(), why));
                    });
                    m_assistQueuedText = routedText;
                    m_assistLocalGuess = route;
                    m_assistDebounce.start();
                }
            } else if (id == m_pendingSubmit && haveAssist) {
                showAssistLabel();
            } else if (id == m_previewId || id == m_pendingSubmit) {
                if (route == QStringLiteral("shell") && !event.value(QStringLiteral("valid")).toBool(true))
                    m_routeLabel->setText(QStringLiteral("TERMINAL · ") + event.value(QStringLiteral("invalid_reason")).toString()
                                          + QStringLiteral(" · the agent will fix it"));
                else
                    m_routeLabel->setText(route.toUpper() + QStringLiteral(" · ") + event.value(QStringLiteral("reason")).toString());
                m_routeLabel->setToolTip(event.value(QStringLiteral("syntax_error")).toString());
            }
            if (id == m_pendingSubmit) {
                m_pendingSubmit.clear();
                if (m_editor->toPlainText() != m_submittedDraft) {
                    status(QStringLiteral("Input changed during routing; submit again to use the current text.")); return;
                }
                if (haveAssist) { dispatch(withAssistedRoute(event), m_submitMode); return; }
                if (needsAssist) {
                    // Wait briefly for the model's opinion; the local guess wins after 400 ms.
                    m_heldDecision = event; m_heldMode = m_submitMode;
                    if (m_assistInflightText != routedText) sendRouteAssist(routedText);
                    m_assistHold.start();
                    return;
                }
                dispatch(event, m_submitMode);
            }
        } else if (type == QStringLiteral("configured")) {
            m_configured = true; m_configuring = false;
            m_model = event.value(QStringLiteral("model")).toString();
            m_skillCount = event.value(QStringLiteral("skills")).toInt();
            // Model roles (protocol 13): the worker reports the effective model of every role and
            // which role this pane runs (a role that could not be used falls back to "main").
            m_roleSummary = event.value(QStringLiteral("roles")).toObject();
            m_agentRole = event.value(QStringLiteral("agent_role")).toString(QStringLiteral("main"));
            onSessionConfigured(event);
            status(QStringLiteral("Agent ready · ") + (m_agentRole == QStringLiteral("main") ? QString() : roleLabel(m_agentRole) + QStringLiteral(" · "))
                   + event.value(QStringLiteral("model")).toString());
            changed();
        } else if (type == QStringLiteral("presets")) {
            m_presets = event.value(QStringLiteral("presets")).toArray();
            m_stored.clear();
            for (const auto &item : m_presets) {
                const auto preset = item.toObject();
                if (preset.value(QStringLiteral("has_stored_key")).toBool())
                    m_stored.append({preset.value(QStringLiteral("id")).toString(), preset.value(QStringLiteral("label")).toString()});
            }
            changed();
            if (m_stored.isEmpty()) {
                status(QStringLiteral("No stored provider keys. Open Provider / BYOK… to import them from Warp or enter one."));
                return;
            }
            if (!m_configured && !m_configuring) {
                auto hasKey = [this](const QString &id) {
                    return std::any_of(m_stored.cbegin(), m_stored.cend(), [&](const auto &entry) { return entry.first == id; });
                };
                // A restored pane keeps the model it had; otherwise the saved choice, then Warp's
                // default agent model, then the first stored key.
                QString choice = m_restorePreset;
                m_restorePreset.clear();
                if (!hasKey(choice)) choice = QSettings().value(QStringLiteral("provider/preset")).toString();
                if (!hasKey(choice)) choice = event.value(QStringLiteral("warp_default")).toString();
                if (!hasKey(choice)) choice = m_stored.first().first;
                configurePreset(choice, false);
            }
        } else if (type == QStringLiteral("warp_imported")) {
            const auto imported = event.value(QStringLiteral("imported")).toArray();
            const auto skipped = event.value(QStringLiteral("skipped")).toArray();
            QStringList names;
            for (const auto &item : imported) names << item.toObject().value(QStringLiteral("name")).toString();
            QMessageBox::information(this, QStringLiteral("Warp import"),
                QStringLiteral("Imported %1 key(s) into the keyring: %2\nSkipped: %3").arg(imported.size())
                    .arg(names.join(QStringLiteral(", ")), skipped.isEmpty() ? QStringLiteral("none") : QString::number(skipped.size())));
            status(QStringLiteral("Warp import finished. Leave the key field empty to use stored keys."));
            send({{"type", "presets"}});
        } else if (type == QStringLiteral("keybindings_updated")) {
        } else if (type == QStringLiteral("key_stored")) {
            status(QStringLiteral("API key saved to the keyring for ") + event.value(QStringLiteral("preset")).toString());
        } else if (type == QStringLiteral("queued")) {
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            if (m_pendingPrompts.contains(requestId)) m_itemPrompts.insert(event.value(QStringLiteral("id")).toString(), m_pendingPrompts.take(requestId));
        } else if (type == QStringLiteral("queue_changed")) {
            m_runningItem = event.value(QStringLiteral("running")).toString();
            m_queuePaused = event.value(QStringLiteral("paused")).toBool();
            m_queueItems.clear();
            for (const auto &value : event.value(QStringLiteral("items")).toArray()) {
                const auto item = value.toObject();
                m_queueItems.append({item.value(QStringLiteral("id")).toString(),
                                     {item.value(QStringLiteral("preview")).toString(), item.value(QStringLiteral("forced")).toBool()}});
            }
            // Forget prompts that are neither running nor queued any more (removed or cleared).
            for (auto it = m_itemPrompts.begin(); it != m_itemPrompts.end();) {
                const bool queued = std::any_of(m_queueItems.cbegin(), m_queueItems.cend(), [&](const auto &q) { return q.first == it.key(); });
                if (!queued && it.key() != m_runningItem && it.key() != m_currentItem) it = m_itemPrompts.erase(it); else ++it;
            }
            rebuildQueueStrip();
            changed();
        } else if (type == QStringLiteral("agents") && m_agentsListPending) {
            m_agentsListPending = false;
            ensureLineStart();
            const QJsonArray items = event.value(QStringLiteral("items")).toArray();
            printInline(QStringLiteral("%1 agent definition(s):\n").arg(items.size()), Ink::Note);
            for (const auto &value : items) {
                const QJsonObject item = value.toObject();
                printInline(QStringLiteral("  %1 · %2 (%3)\n").arg(item.value(QStringLiteral("name")).toString(),
                            item.value(QStringLiteral("description")).toString().left(90), item.value(QStringLiteral("source")).toString()), Ink::ToolOutput);
            }
            closeInline();
        } else if (type == QStringLiteral("interrupting")) {
            status(QStringLiteral("Interrupting the current agent turn…"));
        } else if (type == QStringLiteral("agent_started")) {
            // Busy follows agent_started/agent_finished: the next queued turn may start right after done.
            m_agentBusy = true; m_turnHeader = false; m_turnText.clear();
            m_currentItem = event.value(QStringLiteral("id")).toString();
            QTimer::singleShot(0, this, [this] { rebuildQueueStrip(); });
            const PendingPrompt prompt = m_itemPrompts.value(m_currentItem);
            m_fixAwaitingAgent = prompt.fix;
            if (!prompt.program.isEmpty()) m_transcriptProgram = prompt.program;
            if (!prompt.fix && !prompt.text.isEmpty()) {
                ensureLineStart();
                printInline(QStringLiteral("› ") + prompt.text + '\n', Ink::User);
                if (!prompt.why.isEmpty()) printInline(prompt.why + '\n', Ink::Note);
            }
        } else if (type == QStringLiteral("agent_finished")) {
            const QString outcome = event.value(QStringLiteral("outcome")).toString();
            const bool stopped = outcome == QStringLiteral("cancelled") || outcome == QStringLiteral("error");
            if (m_interruptPending && outcome == QStringLiteral("cancelled")) {
                m_interruptPending = false;   // replaced by the interrupting prompt; keep the queue going
            } else if (stopped) {
                pauseQueue(outcome == QStringLiteral("error") ? QStringLiteral("the agent turn failed") : QStringLiteral("the agent was stopped"));
            }
            if (m_activeValid && m_active.agent) m_activeValid = false;
            QTimer::singleShot(0, this, [this] { pumpQueue(); rebuildQueueStrip(); });
            m_itemPrompts.remove(event.value(QStringLiteral("id")).toString());
            if (m_currentItem == event.value(QStringLiteral("id")).toString()) m_currentItem.clear();
            m_agentBusy = !m_runningItem.isEmpty() && m_runningItem != event.value(QStringLiteral("id")).toString();
            if (!m_agentBusy) m_idleTip.start();
            if (outcome == QStringLiteral("done")) {
                ++m_turnsCompleted;
                if (window() && !window()->isActiveWindow()) m_finishedWhileAway = true;
            }
            if (!m_agentBusy && !moreTurnsPending()) { ensureLineStart(); closeInline(); }
            if (outcome == QStringLiteral("done") && !m_agentBusy && !moreTurnsPending()
                && QSettings().value(QStringLiteral("suggestions/next_prompt"), false).toBool())
                QTimer::singleShot(300, this, [this] { requestSuggestion(QStringLiteral("next_prompt")); });
            changed();
        } else if (type == QStringLiteral("delta")) {
            const QString text = event.value(QStringLiteral("text")).toString();
            m_turnText += text;
            turnHeader(); printInline(text, Ink::Agent);
        } else if (type == QStringLiteral("tool_output")) {
            turnHeader(); printInline(event.value(QStringLiteral("text")).toString(), Ink::ToolOutput);
        } else if (type == QStringLiteral("tool_started")) {
            turnHeader();
            const QString preview = event.value(QStringLiteral("preview")).toString();
            // Compact the backend preview: "RUN COMMAND\n\nWorking directory: …\nTimeout: …\n\ncmd"
            // becomes "⚙ $ cmd"; file tools become "⚙ read path" / "⚙ write path" plus the diff.
            QStringList lines = preview.left(6000).split('\n');
            const QString title = lines.isEmpty() ? QString() : lines.takeFirst().trimmed();
            QStringList body;
            for (const QString &line : std::as_const(lines)) {
                if (line.startsWith(QStringLiteral("Working directory: ")) || line.startsWith(QStringLiteral("Timeout: "))
                    || line.startsWith(QStringLiteral("Old bytes: "))) continue;
                if (body.isEmpty() && line.trimmed().isEmpty()) continue;
                body << line;
            }
            while (!body.isEmpty() && body.last().trimmed().isEmpty()) body.removeLast();
            QString verb = title == QStringLiteral("RUN COMMAND") ? QStringLiteral("$")
                         : title == QStringLiteral("READ FILE") ? QStringLiteral("read")
                         : title == QStringLiteral("LIST DIRECTORY") ? QStringLiteral("list")
                         : title == QStringLiteral("WRITE FILE") ? QStringLiteral("write")
                         : event.value(QStringLiteral("tool")).toString();
            QString head = body.isEmpty() ? QString() : body.takeFirst();
            if (verb != QStringLiteral("$") && head.startsWith(m_workspace + '/')) head = head.mid(m_workspace.size() + 1);
            ensureLineStart();
            printInline(QStringLiteral("⚙ ") + verb + ' ' + head + '\n', Ink::Tool);
            // Multi-line commands continue; write diffs are colored. Skip the diff's blank separator.
            for (const QString &line : std::as_const(body)) {
                if (line.trimmed().isEmpty()) continue;
                Ink ink = Ink::ToolOutput;
                if (line.startsWith('+') && !line.startsWith(QStringLiteral("+++"))) ink = Ink::DiffAdd;
                else if (line.startsWith('-') && !line.startsWith(QStringLiteral("---"))) ink = Ink::DiffRemove;
                printInline(line + '\n', ink);
            }
        } else if (type == QStringLiteral("tool_result")) {
            const auto result = event.value(QStringLiteral("result")).toObject();
            ensureLineStart();
            if (result.contains(QStringLiteral("error"))) printInline(QStringLiteral("✗ ") + result.value(QStringLiteral("error")).toString() + '\n', Ink::Error);
            else if (result.contains(QStringLiteral("exit_code"))) {
                const int code = result.value(QStringLiteral("exit_code")).toInt();
                printInline(QStringLiteral("exit %1%2%3\n").arg(code)
                    .arg(result.value(QStringLiteral("timed_out")).toBool() ? QStringLiteral(" · timed out") : QString())
                    .arg(result.value(QStringLiteral("truncated")).toBool() ? QStringLiteral(" · output truncated") : QString()),
                    code == 0 ? Ink::Note : Ink::Error);
            } else printInline(QStringLiteral("✓ ") + event.value(QStringLiteral("tool")).toString() + '\n', Ink::Note);
        } else if (type == QStringLiteral("status")) {
            status(event.value(QStringLiteral("text")).toString());
        } else if (type == QStringLiteral("done") || type == QStringLiteral("cancelled")) {
            if (type == QStringLiteral("cancelled")) {
                ensureLineStart(); printInline(QStringLiteral("Stopped. Actions that already ran are not rolled back.\n"), Ink::Error);
            }
            printTurnEndRequests(type, event);   // request ledger UI
            ensureLineStart();
            // Readline redraws its prompt asynchronously; closing between queued turns would drop the
            // redrawn prompt into the middle of the next turn's output. Close once the queue is idle.
            if (!moreTurnsPending()) closeInline();
            status(moreTurnsPending() ? QStringLiteral("Next queued prompt…") : QStringLiteral("Ready"));
            finishFixTurn(type == QStringLiteral("done"));
        } else if (type == QStringLiteral("error")) {
            const auto text = event.value(QStringLiteral("text")).toString();
            if (event.value(QStringLiteral("id")).toString() == m_pendingSubmit) m_pendingSubmit.clear();
            const bool wasBusy = m_agentBusy;
            // A rejected ask (queue full, invalid prompt) never started; forget it.
            m_pendingPrompts.remove(event.value(QStringLiteral("id")).toString());
            if (m_activeValid && m_active.agent && event.value(QStringLiteral("id")).toString() == m_activeRequest) {
                m_activeValid = false;
                m_entries.prepend(m_active);
                pauseQueue(QStringLiteral("the worker refused the prompt: ") + text);
            }
            // Route errors do not cancel a concurrent agent turn.
            m_agentBusy = event.value(QStringLiteral("agent_busy")).toBool(false);
            m_configuring = false;
            status(text);
            if (wasBusy && !m_agentBusy) {
                ensureLineStart(); printInline(QStringLiteral("✗ ") + text + '\n', Ink::Error);
                printTurnEndRequests(type, event);   // request ledger UI
                if (!moreTurnsPending()) closeInline();
                finishFixTurn(false);
            }
        }
    }

    void dispatch(const QJsonObject &decision, const QString &mode) {
        const QString route = decision.value(QStringLiteral("route")).toString();
        const QString text = decision.value(QStringLiteral("text")).toString();
        if (route == QStringLiteral("empty")) return;
        if (route == QStringLiteral("shell")) {
            const bool valid = decision.value(QStringLiteral("valid")).toBool(decision.value(QStringLiteral("syntax_ok")).toBool(true));
            const QString problem = decision.value(QStringLiteral("invalid_reason")).toString(
                decision.value(QStringLiteral("syntax_error")).toString());
            if (mode == QStringLiteral("shell")) {
                // Terminal mode (Ctrl+Shift+Enter): always the terminal. An invalid command
                // goes to the agent to be fixed; a failing run is fixed and re-run.
                m_editor->remember(text); m_editor->clear();
                if (!valid) { startFix(text, problem.isEmpty() ? QStringLiteral("not a valid command") : problem, 1); return; }
                submitTerminal(text, true);
                return;
            }
            if (!valid) { submitAgent(text, true, problem); return; }
            submitTerminal(text, false);
        } else {
            // "agent", or a legacy "ambiguous" decision: the agent is the default for invalid input.
            // Show why non-command input went to the agent, e.g. "command not found: foo".
            const QString why = !decision.value(QStringLiteral("valid")).toBool(true) && mode != QStringLiteral("agent")
                ? decision.value(QStringLiteral("invalid_reason")).toString() : QString();
            submitAgent(text, true, why);
        }
    }

    bool runInTerminal(const QString &text, bool watch, int attempt) {
        if (!m_backend || !m_shellReady || m_loading || m_native) {
            status(QStringLiteral("Shell is not at an integrated prompt. Use native input; Relay will not type into a running program."));
            return false;
        }
        if (foregroundPid() > 0 && foregroundPid() != shellPid()) {
            m_shellReady = false; focusTerminal();
            status(QStringLiteral("A foreground program is running. Composer submission was not sent."));
            return false;
        }
        const auto data = text.toUtf8();
        QSaveFile input(m_runtime.filePath(QStringLiteral("input.txt")));
        if (!input.open(QIODevice::WriteOnly)) { status(input.errorString()); return false; }
        input.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        if (input.write(data) != data.size() || !input.commit()) { status(QStringLiteral("Could not stage command.")); return false; }
        closeInline();
        m_pendingHash = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        m_pendingCommand = text; m_loading = true; m_shellReady = false; m_promptReported = false;
        m_fixCommand = watch ? text : QString(); m_fixAttempt = attempt; m_fixWatch = watch; m_fixArmed = false;
        // Stage text via a bound Readline function. Enter is sent only after its hash acknowledgement.
        sendShellInput(QString(QChar(24)) + QChar(18));
        const quint64 serial = ++m_loadSerial;
        QTimer::singleShot(2500, this, [this, serial] {
            if (m_loading && m_loadSerial == serial) {
                m_loading = false; clearFix(); setNative(true);
                if (m_activeValid && !m_active.agent) { m_activeValid = false; m_activeLoaded = false; m_entriesPaused = !m_entries.isEmpty(); rebuildQueueStrip(); }
                status(QStringLiteral("Shell did not acknowledge the editor text. Enter was NOT sent. Inspect the native input line; try --clean-shell."));
            }
        });
        return true;
    }

    // A command in this pane's scope was killed for memory while the shell kept running.
    void checkOomKills() {
        if (!m_backend || m_shellStopped) return;
        int pid = shellPid();
        if (pid > 0) m_shellPid = pid;
        // A shell killed by a signal leaves Konsole showing "Program crashed" instead of closing,
        // and KonsolePart then reports no PID; use the last PID the shell itself reported.
        if (m_shellPid > 0 && !QFileInfo::exists(QStringLiteral("/proc/%1").arg(m_shellPid))) { shellStopped(); return; }
        pid = m_shellPid;
        if (m_shellUnit.isEmpty()) return;
        const long kills = isolation::oomKills(pid);
        if (kills < 0) return;
        if (m_oomKills >= 0 && kills > m_oomKills) {
            showBanner(QStringLiteral("A command in this pane was stopped because it ran out of memory (limit %1). The shell is still running.")
                           .arg(isolation::memory("isolation/shell_memory_max", "8G")),
                       QString(), {});
            notifyIfAway(QStringLiteral("Out of memory"), QStringLiteral("A command in %1 was stopped (limit %2).").arg(m_cwd, isolation::memory("isolation/shell_memory_max", "8G")));
        }
        m_oomKills = kills;
    }

    void shellStopped() {
        m_shellStopped = true;
        m_shellReady = false; m_promptReported = false; m_loading = false;
        if (m_native) setNative(false, false);
        const bool oom = isolation::takeResult(m_shellUnit) == QStringLiteral("oom-kill");
        showBanner(oom ? QStringLiteral("This pane's shell was stopped because it ran out of memory (limit %1).")
                             .arg(isolation::memory("isolation/shell_memory_max", "8G"))
                       : QStringLiteral("This pane's shell was stopped."),
                   QStringLiteral("Restart shell"), [this] { restartShell(); });
        if (oom) notifyIfAway(QStringLiteral("Out of memory"), QStringLiteral("The shell in %1 was stopped.").arg(m_cwd));
    }

    void showBanner(const QString &text, const QString &actionLabel, std::function<void()> action) {
        if (!m_banner) {
            m_banner = new QFrame;
            m_banner->setObjectName(QStringLiteral("paneBanner"));
            m_banner->setAttribute(Qt::WA_StyledBackground);
            auto *row = new QHBoxLayout(m_banner); row->setContentsMargins(12, 8, 8, 8); row->setSpacing(8);
            m_bannerText = new QLabel; m_bannerText->setWordWrap(true); m_bannerText->setTextFormat(Qt::PlainText);
            m_bannerText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            row->addWidget(m_bannerText, 1);
            m_bannerAction = new QPushButton;
            connect(m_bannerAction, &QPushButton::clicked, this, [this] { if (m_bannerCallback) { auto run = m_bannerCallback; run(); } });
            row->addWidget(m_bannerAction);
            auto *dismiss = new QToolButton; dismiss->setText(QStringLiteral("×")); dismiss->setAutoRaise(true);
            connect(dismiss, &QToolButton::clicked, this, [this] { hideBanner(); });
            row->addWidget(dismiss);
            if (auto *box = qobject_cast<QVBoxLayout *>(layout())) box->insertWidget(1, m_banner);
        }
        m_bannerText->setText(text);
        m_bannerCallback = std::move(action);
        const QString shortcut = Keymap::instance().shortcutText(QStringLiteral("pane.restartShell"));
        m_bannerAction->setText(shortcut.isEmpty() || actionLabel.isEmpty() ? actionLabel : actionLabel + QStringLiteral("  (") + shortcut + ')');
        m_bannerAction->setVisible(!actionLabel.isEmpty());
        m_banner->show();
    }

    void hideBanner() { if (m_banner) m_banner->hide(); m_bannerCallback = nullptr; }

public:
    // Ctrl+Shift+R: restart whatever stopped in this pane.
    void restartStopped() {
        if (m_bannerCallback && m_banner && m_banner->isVisible()) { auto run = m_bannerCallback; run(); return; }
        if (!m_backend || m_shellStopped) { restartShell(); return; }
        if (m_worker.state() == QProcess::NotRunning) { hideBanner(); startWorker(); return; }
        status(QStringLiteral("The shell and agent in this pane are running."));
    }

    void restartShell() {
        hideBanner();
        if (m_backend && !m_shellStopped) return;
        if (m_backendOwned) {
            // Replace the terminal that still shows the stopped program; do not close the pane.
            m_restarting = true;
            m_backend = nullptr;
            m_backendOwned.reset();
            m_restarting = false;
        }
        m_backend = nullptr; m_terminal = nullptr; m_shellStopped = false; m_shellPid = 0;
        m_shellReady = false; m_promptReported = false; m_loading = false; m_seenShell = false;
        m_shellSequence.clear(); m_inlineOpen = false; m_atLineStart = true; m_autoHuman = false;
        if (m_native) setNative(false, false);
        try {
            startTerminal(m_cleanShell);
            relay::theme::polishWindow(this);
            focusInput();
            toast(QStringLiteral("Shell restarted"));
        } catch (const std::exception &error) {
            showBanner(QString::fromUtf8(error.what()), QStringLiteral("Try again"), [this] { restartShell(); });
        }
    }

private:
    // Warp's rule: a password prompt turns echo off but keeps canonical (line) input. Full-screen
    // programs and Readline turn canonical input off, so they do not match. Relay no longer hands
    // the keyboard to the terminal for one: the prompt box becomes a masked field whose line is
    // written to the program (issue decision 3). Polled once a second, and every 250 ms while a
    // command runs.
    void checkPasswordPrompt() {
        if (!m_backend) { leaveSecretMode(); return; }
        if (relay::input::secretPrompt(inputState(false)) && !m_promptReported) {
            m_echoTicks = 0;
            if (m_secretMode || m_native || m_secretDeclined) return;
            endWaiting(false);
            enterSecretMode(foregroundProgramName());
            if (!m_secretNotified) {
                m_secretNotified = true;
                notifyIfAway(QStringLiteral("Password prompt"), QStringLiteral("A command in %1 is waiting for a password.").arg(m_cwd));
            }
            return;
        }
        // Echo is back on: the password was answered, or the program moved on. Two ticks of
        // agreement keep a single poll between write and echo from ending masked input early.
        if ((m_secretMode || m_secretDeclined) && ++m_echoTicks >= 2) {
            leaveSecretMode();
            m_secretDeclined = false;
            m_secretNotified = false;
            m_echoTicks = 0;
        }
    }

    // ----- masked prompt box (password prompts) -----------------------------------------------
    void enterSecretMode(const QString &program) {
        if (m_secretMode || !m_secretEdit) return;
        m_secretMode = true;
        m_secretProgram = program;
        hideAtPopup(); hideSlashPopup(); hideTabPopup(); clearAiGhost();
        m_secretChip->setText(relay::input::passwordChip(m_secretProgram));
        m_secretChip->show();
        m_routeLabel->setText(QStringLiteral("PASSWORD · the line goes to the program, not to Relay"));
        scrubSecretEditor();
        m_editor->hide();
        m_secretEdit->show();
        m_secretEdit->setFocus(Qt::OtherFocusReason);
        refreshProgramHint();
        changed();
        toast(QStringLiteral("Password prompt · type it here · %1 asks the agent instead")
                  .arg(Keymap::instance().shortcutText(QStringLiteral("input.toggle"))));
    }

    void leaveSecretMode() {
        if (!m_secretMode) return;
        m_secretMode = false;
        m_secretProgram.clear();
        scrubSecretEditor();
        m_secretEdit->hide();
        m_secretChip->hide();
        m_editor->show();
        if (!m_native) m_editor->setFocus(Qt::OtherFocusReason);
        refreshProgramHint();
        requestRoute(false, QStringLiteral("auto"));
        changed();
    }

    // Overwrite whatever the masked field holds, then clear it. QLineEdit::setText() also drops
    // its undo/redo history, so the characters are not recoverable from the widget.
    void scrubSecretEditor() {
        if (!m_secretEdit) return;
        const int length = m_secretEdit->text().size();
        if (length > 0) m_secretEdit->setText(QString(length, QLatin1Char('\0')));
        m_secretEdit->clear();
    }

    // Enter in the masked prompt box: the line goes straight to the program's stdin. It is never
    // remembered, queued, logged, routed, shown to a model or printed in the terminal.
    void submitSecret() {
        if (!m_secretMode) return;
        relay::input::Secret secret;
        secret.set(m_secretEdit->text());
        scrubSecretEditor();
        if (!m_backend) { secret.wipe(); return; }
        QString line = secret.take();
        m_backend->sendText(line, false);
        relay::input::wipe(line);
        m_echoTicks = 0;
        status(QStringLiteral("Password sent to %1").arg(m_secretProgram.isEmpty() ? QStringLiteral("the program") : m_secretProgram));
    }

    void notifyIfAway(const QString &title, const QString &body) {
        if (window() && window()->isActiveWindow()) return;
        QApplication::alert(window());
        const QString notifier = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
        if (!notifier.isEmpty()) QProcess::startDetached(notifier, {QStringLiteral("-a"), QStringLiteral("Relay"), title, body});
    }

    static bool copyOnSelect() { return QSettings().value(QStringLiteral("terminal/copy_on_select"), false).toBool(); }

    // KonsolePart exposes no "has selection" query, and its copy does nothing without a
    // selection. Copy, and treat a clipboard change as proof that text was selected; the
    // engine behaves the same way (it only writes the clipboard for a non-empty selection).
    bool copySelection() {
        if (!m_backend) return false;
        bool copied = false;
        const auto connection = connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [&copied] { copied = true; });
        m_backend->copySelection();
        disconnect(connection);
        if (!copied) return false;
        const int count = QApplication::clipboard()->text().toUcs4().size();
        if (count > 0) toast(count == 1 ? QStringLiteral("1 character copied") : QStringLiteral("%L1 characters copied").arg(count));
        return true;
    }

    // A small notice over the bottom-right of the terminal that fades after a moment.
    // Shortcut hints (Superhuman-style); see Hints.h and docs/ARCHITECTURE.md "Shortcut hints".
    void hint(const QString &id, const QString &text, int limit = 3) {
        if (text.isEmpty()) return;
        if (relay::ShortcutHints::instance().shouldShow(id, limit)) toast(text, 5000);
    }

    void showIdleTip() {
        if (m_agentBusy || !m_editor->toPlainText().isEmpty() || !m_editor->hasFocus() || m_native) return;
        const auto key = [](const char *id) { return Keymap::instance().shortcutText(QString::fromLatin1(id)); };
        QList<relay::ShortcutHints::Tip> tips{
            {QStringLiteral("idle.at"), QStringLiteral("Tip: @ and a file name attaches or opens a file")},
            {QStringLiteral("idle.plan"), QStringLiteral("Tip: %1 switches to plan mode").arg(key("agent.planToggle"))},
            {QStringLiteral("idle.rewind"), QStringLiteral("Tip: Esc Esc in an empty prompt box rewinds the chat; /rewind-code restores files")},
            {QStringLiteral("idle.agents"), QStringLiteral("Tip: ↓ from the prompt box selects running subagents")},
            {QStringLiteral("idle.palette"), QStringLiteral("Tip: %1 opens every action").arg(key("palette.open"))},
            {QStringLiteral("idle.prefix"), QStringLiteral("Tip: start with ! for the terminal or * for the agent")},
        };
        const auto tip = relay::ShortcutHints::instance().nextIdleTip(tips);
        if (!tip.text.isEmpty()) toast(tip.text, 6000);
    }

    void setPrefixMode(const QString &mode) {
        m_prefixPrevMode = m_modeValue;
        m_prefixMode = mode;
        m_prefixChip->setText(mode == QStringLiteral("shell") ? QStringLiteral("! terminal") : QStringLiteral("* agent"));
        m_prefixChip->setProperty("kind", mode);
        m_prefixChip->style()->unpolish(m_prefixChip); m_prefixChip->style()->polish(m_prefixChip);
        m_prefixChip->show();
        setMode(mode);
    }

    void clearPrefixMode(bool restore) {
        if (m_prefixMode.isEmpty()) return;
        m_prefixMode.clear();
        m_prefixChip->hide();
        if (restore) setMode(m_prefixPrevMode.isEmpty() ? QStringLiteral("auto") : m_prefixPrevMode);
    }

public:
    void toast(const QString &text, int milliseconds = 1600) {
        if (!m_toast) {
            m_toast = new QLabel(this);
            m_toast->setObjectName(QStringLiteral("toast"));
            m_toast->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_toastTimer.setSingleShot(true);
            connect(&m_toastTimer, &QTimer::timeout, m_toast, &QLabel::hide);
        }
        m_toast->setText(text);
        m_toast->adjustSize();
        const QWidget *anchor = m_terminalHost ? m_terminalHost : this;
        const QPoint corner = anchor->mapTo(this, QPoint(anchor->width(), anchor->height()));
        m_toast->move(corner.x() - m_toast->width() - 16, corner.y() - m_toast->height() - 12);
        m_toast->show();
        m_toast->raise();
        m_toastTimer.start(milliseconds);
    }
private:

    // Scroll the terminal's scrollback by one page (Konsole's possibly hidden scrollbar, or
    // the engine's viewport).
    bool scrollTerminalPage(int direction) {
        if (!m_backend || !(m_backend->capabilities() & relay::TerminalBackend::ScrollControl)) return false;
        m_backend->scrollPages(direction);
        return true;
    }

    // ----- fix and re-run loop (terminal mode) -----------------------------------------
    static constexpr int kMaxFixAttempts = 3;

    void status(const QString &text) { if (onStatus) onStatus(text); }
    void changed() { refreshPickers(); refreshSessionControls(); if (onStateChanged) onStateChanged(); }

    void refreshPickers() {
        if (!m_modeBox || !m_modelBox) return;
        const QSignalBlocker modeBlock(m_modeBox), modelBlock(m_modelBox);
        m_modeBox->setCurrentIndex(std::max(0, m_modeBox->findData(m_modeValue)));
        m_modelBox->clear();
        for (const auto &model : std::as_const(m_stored)) m_modelBox->addItem(model.second, model.first);
        if (m_stored.isEmpty()) m_modelBox->addItem(QStringLiteral("No stored keys"));
        m_modelBox->setEnabled(!m_stored.isEmpty());
        const int index = m_modelBox->findData(m_currentPreset);
        if (index >= 0) m_modelBox->setCurrentIndex(index);
        // Model roles (protocol 13): a pane on another role shows that role and its model, and
        // picking a preset from the chip puts the pane back on the main agent.
        if (m_agentRole != QStringLiteral("main")) {
            const QString model = m_model.isEmpty() ? roleModel(m_agentRole) : m_model;
            m_modelBox->insertItem(0, QStringLiteral("%1 · %2").arg(roleLabel(m_agentRole), model), QStringLiteral("role:") + m_agentRole);
            m_modelBox->setCurrentIndex(0);
            m_modelBox->setEnabled(true);
        }
        m_modelBox->setToolTip(modelTooltip());
    }

    // Chip tooltip: the pane's model plus every role's effective model (protocol 13).
    QString modelTooltip(const QString &extra = QString()) const {
        QStringList lines{QStringLiteral("Agent model for this pane. Switching keeps the conversation.")};
        if (!extra.isEmpty()) lines << extra;
        if (!m_roleSummary.isEmpty()) {
            lines << QString();
            for (const QString &role : QStringList{QStringLiteral("main")} + roleIds()) {
                const QJsonObject entry = m_roleSummary.value(role).toObject();
                if (entry.isEmpty()) continue;
                const bool same = entry.value(QStringLiteral("source")).toString() == QStringLiteral("main");
                lines << QStringLiteral("%1: %2%3").arg(roleLabel(role), entry.value(QStringLiteral("model")).toString(),
                                                        same ? QStringLiteral(" (same as main)") : QString());
            }
        }
        return lines.join('\n');
    }

    void clearFix() { m_fixCommand.clear(); m_fixWatch = false; m_fixArmed = false; m_fixAwaitingAgent = false; m_fixAttempt = 0; }

    void startFix(const QString &command, const QString &problem, int attempt) {
        if (attempt > kMaxFixAttempts) {
            ensureLineStart();
            printInline(QStringLiteral("✗ Still failing after %1 fix attempts. Stopping.\n").arg(kMaxFixAttempts), Ink::Error);
            closeInline(); clearFix(); return;
        }
        if (!m_configured) {
            printInline(QStringLiteral("✗ %1. No agent provider is configured to fix it.\n").arg(problem), Ink::Error);
            closeInline(); clearFix(); return;
        }
        // A busy agent does not refuse the fix: it is queued and runs after the current turn.
        m_fixCommand = command; m_fixAttempt = attempt; m_fixAwaitingAgent = false; m_fixWatch = false; m_fixArmed = false;
        ensureLineStart();
        printInline(QStringLiteral("⟳ %1 · asking the agent to fix it (attempt %2 of %3)\n").arg(problem).arg(attempt).arg(kMaxFixAttempts), Ink::Note);
        const QString prompt = QStringLiteral(
            "Terminal fix request, attempt %1 of %2.\n"
            "The user ran this command in their interactive Bash terminal, working directory %3:\n"
            "```bash\n%4\n```\n"
            "Problem: %5.\n"
            "You cannot see the terminal's output. If you need the error message, reproduce it with run_command "
            "(start the command with `cd %3 && `). Keep what the user meant; make the smallest change that fixes it.\n"
            "End your reply with the corrected command in a fenced block tagged relay-run, for example:\n"
            "```relay-run\nls -la\n```\n"
            "Relay runs that block in the user's terminal. If it cannot be fixed, end with an empty relay-run block and a one-line reason before it.")
            .arg(attempt).arg(kMaxFixAttempts).arg(shellQuote(m_cwd), command, problem);
        // Fix turns continue the command the user just ran, so they go ahead of queued items.
        QueueEntry entry; entry.agent = true; entry.fix = true; entry.text = prompt;
        if (m_entries.isEmpty() && !m_activeValid && !m_agentBusy) startAgentEntry(entry, false);
        else { entry.id = ++m_entrySerial; m_entries.prepend(entry); rebuildQueueStrip(); pumpQueue(); }
    }

    void finishFixTurn(bool completed) {
        if (!m_fixAwaitingAgent) return;
        m_fixAwaitingAgent = false;
        if (!completed) { clearFix(); return; }
        const QString marker = QStringLiteral("```relay-run");
        const int start = m_turnText.lastIndexOf(marker);
        QString fixed;
        if (start >= 0) {
            int bodyStart = m_turnText.indexOf('\n', start);
            int end = bodyStart >= 0 ? m_turnText.indexOf(QStringLiteral("```"), bodyStart) : -1;
            if (bodyStart >= 0 && end > bodyStart) fixed = m_turnText.mid(bodyStart + 1, end - bodyStart - 1).trimmed();
        }
        if (fixed.isEmpty()) {
            printInline(QStringLiteral("✗ The agent did not produce a fixed command.\n"), Ink::Error);
            closeInline(); clearFix(); return;
        }
        const int attempt = m_fixAttempt;
        const QString command = fixed;
        // Let Readline redraw its prompt before the fixed command is staged.
        QTimer::singleShot(150, this, [this, command, attempt] {
            if (!runInTerminal(command, true, attempt)) {
                printInline(QStringLiteral("✗ The shell is busy, so the fixed command was not run:\n%1\n").arg(command), Ink::Error);
                closeInline(); clearFix();
            }
        });
    }

    static QString shellQuote(const QString &value) {
        QString quoted = value;
        quoted.replace('\'', QStringLiteral("'\\''"));
        return '\'' + quoted + '\'';
    }

    // ----- inline output in the terminal -------------------------------------------------
    enum class Ink { Agent, User, Tool, ToolOutput, DiffAdd, DiffRemove, Error, Note, Recap };

    static QByteArray inkCode(Ink ink) {
        switch (ink) {
        case Ink::Agent: return "\x1b[38;2;226;229;235m";
        case Ink::User: return "\x1b[1;38;2;62;197;240m";
        case Ink::Tool: return "\x1b[38;2;229;192;123m";
        case Ink::ToolOutput: return "\x1b[38;2;128;135;150m";
        case Ink::DiffAdd: return "\x1b[38;2;126;200;140m";
        case Ink::DiffRemove: return "\x1b[38;2;232;120;128m";
        case Ink::Error: return "\x1b[38;2;240;113;120m";
        case Ink::Note: return "\x1b[3;38;2;128;135;150m";
        case Ink::Recap: return "\x1b[38;2;190;160;240m";
        }
        return {};
    }

    bool shellIdleAtPrompt() const {
        return m_backend && m_promptReported && !m_loading && !m_native
            && (foregroundPid() <= 0 || foregroundPid() == shellPid());
    }

    // Inline agent output: bytes go to the terminal emulator as if the program had printed
    // them. Nothing is typed into the shell, so agent text never reaches shell history and is
    // never executed. KonsolePart does it through its Session, the engine through its parser.
    void writeTerminal(const QByteArray &bytes) {
        if (m_backend && (m_backend->capabilities() & relay::TerminalBackend::DisplayInjection))
            m_backend->writeToDisplay(bytes);
        else
            fprintf(stderr, "%s", bytes.constData());
    }

    // Model and tool output is untrusted: drop C0/C1 controls so it cannot emit escape
    // sequences (clipboard writes, title changes, cursor games).
    static QString sanitize(const QString &text) {
        QString clean;
        clean.reserve(text.size());
        for (const QChar c : text) {
            const ushort u = c.unicode();
            if (u == '\n' || u == '\t' || (u >= 0x20 && u != 0x7f && !(u >= 0x80 && u < 0xa0))) clean += c;
        }
        return clean;
    }

    static QColor inkColor(Ink ink) {
        switch (ink) {
        case Ink::Agent: return QColor(226, 229, 235);
        case Ink::User: return QColor(62, 197, 240);
        case Ink::Tool: return QColor(229, 192, 123);
        case Ink::ToolOutput: case Ink::Note: return QColor(128, 135, 150);
        case Ink::DiffAdd: return QColor(126, 200, 140);
        case Ink::DiffRemove: case Ink::Error: return QColor(240, 113, 120);
        case Ink::Recap: return QColor(190, 160, 240);
        }
        return QColor(226, 229, 235);
    }

    void buildTranscript() {
        m_transcript = new QFrame;
        m_transcript->setObjectName(QStringLiteral("transcript"));
        m_transcript->setAttribute(Qt::WA_StyledBackground);
        auto *box = new QVBoxLayout(m_transcript); box->setContentsMargins(10, 6, 6, 8); box->setSpacing(4);
        auto *header = new QHBoxLayout;
        m_transcriptHeader = new QLabel; m_transcriptHeader->setObjectName(QStringLiteral("transcriptHeader"));
        m_transcriptHeader->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        header->addWidget(m_transcriptHeader, 1);
        auto *close = new QToolButton; close->setText(QStringLiteral("×")); close->setAutoRaise(true);
        close->setToolTip(QStringLiteral("Hide until this program exits; the output still prints in the terminal then"));
        close->setFocusPolicy(Qt::NoFocus);
        connect(close, &QToolButton::clicked, this, [this] { m_transcriptDismissed = true; m_transcript->hide(); });
        header->addWidget(close);
        box->addLayout(header);
        m_transcriptView = new QPlainTextEdit;
        m_transcriptView->setObjectName(QStringLiteral("transcriptView"));
        m_transcriptView->setReadOnly(true);
        m_transcriptView->setFocusPolicy(Qt::ClickFocus);
        m_transcriptView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_transcriptView->setMaximumBlockCount(4000);
        box->addWidget(m_transcriptView, 1);
        m_transcript->hide();
    }

    // While a program owns the terminal, buffered agent output is shown live here and printed
    // into the terminal when the program exits.
    void appendTranscript(const QString &text, Ink ink) {
        if (!m_transcript) return;
        const QString clean = sanitize(text);
        if (clean.isEmpty()) return;
        const QString program = !m_transcriptProgram.isEmpty() ? m_transcriptProgram
                                : (foregroundProgramName().isEmpty() ? QStringLiteral("the program") : foregroundProgramName());
        m_transcriptHeader->setText(QStringLiteral("Agent · %1  —  output will also print in the terminal when %2 exits")
                                        .arg(m_model.isEmpty() ? QStringLiteral("agent") : m_model, program));
        QTextCursor cursor(m_transcriptView->document());
        cursor.movePosition(QTextCursor::End);
        QTextCharFormat format;
        format.setForeground(inkColor(ink));
        if (ink == Ink::User) format.setFontWeight(QFont::Bold);
        if (ink == Ink::Note) format.setFontItalic(true);
        cursor.insertText(clean, format);
        m_transcriptView->verticalScrollBar()->setValue(m_transcriptView->verticalScrollBar()->maximum());
        if (!m_transcriptDismissed && !m_transcript->isVisible()) {
            m_transcript->setMaximumHeight(std::max(120, height() * 2 / 5));
            m_transcript->show();
        }
    }

    void resetTranscript() {
        if (!m_transcript) return;
        m_transcript->hide();
        m_transcriptView->clear();
        m_transcriptDismissed = false;
        m_transcriptProgram.clear();
    }

    void printInline(const QString &text, Ink ink) {
        if (text.isEmpty()) return;
        if (!shellIdleAtPrompt()) { m_inlinePending.append({text, ink}); appendTranscript(text, ink); return; }
        const QString clean = sanitize(text);
        if (clean.isEmpty()) return;
        QByteArray out;
        if (!m_inlineOpen) {
            // Erase the idle prompt line; closeInline() asks Readline to redraw it afterwards.
            out += "\r\x1b[2K";
            m_inlineOpen = true; m_atLineStart = true;
        }
        const QByteArray body = clean.toUtf8();
        out += inkCode(ink);
        for (const char ch : body) { if (ch == '\n') out += "\x1b[0m\r\n" + inkCode(ink); else out += ch; }
        out += "\x1b[0m";
        m_atLineStart = clean.endsWith('\n');
        writeTerminal(out);
    }

    void ensureLineStart() { if (m_inlineOpen && !m_atLineStart) printInline(QStringLiteral("\n"), Ink::Note); }

    void closeInline() {
        if (!m_inlineOpen) return;
        if (!m_atLineStart) writeTerminal("\r\n");
        m_inlineOpen = false; m_atLineStart = true;
        // Ctrl+X Ctrl+P is bound to a no-op shell function; Readline redraws the prompt after it.
        if (m_backend && shellIdleAtPrompt()) m_backend->redrawPrompt();
    }

    void flushInline() {
        if (m_inlinePending.isEmpty() || !shellIdleAtPrompt()) return;
        const auto pending = m_inlinePending;
        m_inlinePending.clear();
        for (const auto &item : pending) printInline(item.first, item.second);
        if (!m_agentBusy) { ensureLineStart(); closeInline(); }
        resetTranscript();
    }

    void turnHeader() {
        if (m_turnHeader) return;
        m_turnHeader = true;
        ensureLineStart();
        printInline(QStringLiteral("▸ ") + (m_model.isEmpty() ? QStringLiteral("agent") : m_model) + '\n', Ink::User);
    }


    QJsonObject presetById(const QString &id) const {
        for (const auto &item : m_presets)
            if (item.toObject().value(QStringLiteral("id")).toString() == id) return item.toObject();
        return {};
    }

    // Configure a built-in preset using its key from the keyring. The key never enters this process.
    void configurePreset(const QString &id, bool announce) {
        const auto preset = presetById(id);
        if (preset.isEmpty()) return;
        if (m_workspace.isEmpty()) m_workspace = QDir::currentPath();
        QSettings settings;
        const int tokens = settings.value(QStringLiteral("provider/max_tokens"), 8192).toInt();
        settings.setValue("provider/preset", id);
        settings.setValue("provider/base", preset.value(QStringLiteral("base_url")).toString());
        settings.setValue("provider/model", preset.value(QStringLiteral("model")).toString());
        settings.setValue("provider/extra", QString::fromUtf8(QJsonDocument(preset.value(QStringLiteral("extra")).toObject()).toJson(QJsonDocument::Compact)));
        m_apiKey.clear(); m_configured = false; m_configuring = true; m_currentPreset = id; changed();
        if (announce) status(QStringLiteral("Switching model. This starts a new conversation."));
        send(withSessionFields(QJsonObject{{"type", "configure"}, {"preset", id}, {"use_stored_key", true},
              {"base_url", preset.value(QStringLiteral("base_url")).toString()},
              {"model", preset.value(QStringLiteral("model")).toString()},
              {"extra", preset.value(QStringLiteral("extra")).toObject()}, {"max_tokens", tokens},
              {"api_key", QString()}, {"workspace", m_workspace}, {"keybindings", Keymap::instance().catalog()}}));
        updatePaths();
    }

    // Agent prompts and terminal commands share one queue per pane, in the order entered. An item
    // starts at once when nothing is queued ahead of it and its resource (agent or shell) is free;
    // otherwise it waits. The worker only ever receives one agent turn at a time from here.
    void submitAgent(const QString &text, bool fromEditor, const QString &why = QString(), QString when = QString()) {
        if (!m_configured) {
            if (fromEditor) { configure(); return; }
            status(QStringLiteral("No agent provider is configured."));
            return;
        }
        if (fromEditor) { m_editor->remember(text); m_editor->clear(); }
        QueueEntry entry;
        entry.agent = true; entry.text = text; entry.why = why; entry.attachments = attachmentsFor(text);
        if (when == QStringLiteral("interrupt") && m_agentBusy) {
            // Bypasses the queue: stop the running turn and run this now. Queued items keep their order.
            m_interruptPending = true;
            startAgentEntry(entry, false, QStringLiteral("interrupt"));
            ensureLineStart();
            printInline(QStringLiteral("Interrupting the current turn; completed actions are not rolled back.\n"), Ink::Note);
            return;
        }
        if (!m_resubmitAtFront && m_entries.isEmpty() && !m_activeValid && !m_agentBusy) {
            startAgentEntry(entry, false);
            return;
        }
        enqueue(entry);
    }

    void submitTerminal(const QString &text, bool watch) {
        if (!m_resubmitAtFront && m_entries.isEmpty() && !m_activeValid && shellIdleForQueue()) {
            runInTerminal(text, watch, 0);
            return;
        }
        m_editor->remember(text);
        if (m_editor->toPlainText() == m_submittedDraft) m_editor->clear();
        QueueEntry entry; entry.agent = false; entry.text = text; entry.watch = watch;
        enqueue(entry);
    }

    bool shellIdleForQueue() const { return m_backend && m_shellReady && !m_loading && !m_native && !processBusy(); }

    void enqueue(QueueEntry entry) {
        entry.id = ++m_entrySerial;
        if (m_resubmitAtFront) {
            // An item pulled from the head for editing goes back to the head and resumes the queue.
            m_entries.prepend(entry);
            m_resubmitAtFront = false;
            m_entriesPaused = false; m_pauseReason.clear();
        } else {
            m_entries.append(entry);
        }
        m_selected = -1;
        if (entry.agent && m_agentBusy) { m_lastQueuedEntryId = entry.id; m_lastQueuedAt.start(); }
        status(entry.agent ? QStringLiteral("Queued · the agent prompt runs after the items ahead of it · Enter again to send at the next tool call")
                           : QStringLiteral("Queued · the command runs when the terminal is free"));
        rebuildQueueStrip(); changed();
        pumpQueue();
    }

    void startAgentEntry(const QueueEntry &entry, bool fromQueue, const QString &when = QStringLiteral("now")) {
        PendingPrompt prompt;
        prompt.text = entry.text; prompt.why = entry.why; prompt.fix = entry.fix;
        if (!entry.fix) m_subagents.clearFinished();   // subagents UI: finished rows linger until a new user turn
        QJsonObject request{{"type", "ask"}, {"text", entry.text}, {"when", when}};
        if (!entry.attachments.isEmpty()) request.insert(QStringLiteral("attachments"), entry.attachments);
        const QString program = processBusy() ? foregroundCommandLine() : QString();
        // The terminal's directory always goes along: `cd` in the terminal must move the agent too.
        QJsonObject context{{"terminal_cwd", m_cwd}};
        if (!program.isEmpty()) {
            // Tell the agent what owns the terminal and that it cannot see or type into it yet.
            context.insert(QStringLiteral("foreground_program"), program);
            prompt.program = QFileInfo(program.section(' ', 0, 0)).fileName();
        }
        request.insert(QStringLiteral("context"), context);
        const QString requestId = sendPrompt(request, prompt);
        if (fromQueue) { m_active = entry; m_activeValid = true; m_activeRequest = requestId; }
    }

    // Start the head of the queue when its resource is free and nothing from the queue is running.
    void pumpQueue() {
        if (m_entriesPaused || m_activeValid || m_entries.isEmpty()) return;
        const QueueEntry head = m_entries.first();
        if (head.agent) {
            if (m_agentBusy || !m_configured) return;
            m_entries.removeFirst();
            startAgentEntry(head, true);
        } else {
            if (!shellIdleForQueue()) return;
            m_entries.removeFirst();
            m_active = head; m_activeValid = true; m_activeLoaded = false;
            if (!runInTerminal(head.text, head.watch, 0)) { m_entries.prepend(head); m_activeValid = false; return; }
        }
        if (m_selected >= m_entries.size()) m_selected = m_entries.size() - 1;
        rebuildQueueStrip(); changed();
    }

    void pauseQueue(const QString &reason) {
        if (m_entries.isEmpty()) return;
        m_entriesPaused = true; m_pauseReason = reason;
        rebuildQueueStrip(); changed();
    }

    void removeEntry(quint64 id) {
        for (int i = 0; i < m_entries.size(); ++i)
            if (m_entries[i].id == id) { m_entries.removeAt(i); break; }
        if (m_selected >= m_entries.size()) m_selected = m_entries.size() - 1;
        if (m_entries.isEmpty()) { m_entriesPaused = false; m_resubmitAtFront = false; m_pauseReason.clear(); }
        rebuildQueueStrip(); changed();
    }

    void syncEntriesFromList() {
        if (!m_queueList) return;
        QList<QueueEntry> ordered;
        for (int row = 0; row < m_queueList->count(); ++row) {
            const quint64 id = m_queueList->item(row)->data(Qt::UserRole).toULongLong();
            for (const auto &entry : std::as_const(m_entries)) if (entry.id == id) { ordered.append(entry); break; }
        }
        if (ordered.size() == m_entries.size()) m_entries = ordered;
        QTimer::singleShot(0, this, [this] { rebuildQueueStrip(); pumpQueue(); });
    }

    // Keys in the composer that belong to the @ picker, the queue selection, the running agent or
    // program, and history suggestions. Returns true when the key was handled.
    bool handleComposerKey(QKeyEvent *key) {
        const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        const int k = key->key();
        // `!` or `*` typed (not pasted) as the first character switches this submission to the
        // terminal or the agent, like Claude Code's `!`. Backspace in the empty box undoes it.
        if (m_prefixMode.isEmpty() && m_editor->toPlainText().isEmpty() && !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
            && (key->text() == QStringLiteral("!") || key->text() == QStringLiteral("*"))) {
            setPrefixMode(key->text() == QStringLiteral("!") ? QStringLiteral("shell") : QStringLiteral("agent"));
            return true;
        }
        if (!m_prefixMode.isEmpty() && k == Qt::Key_Backspace && mods == Qt::NoModifier && m_editor->toPlainText().isEmpty()) {
            clearPrefixMode(true);
            return true;
        }
        const bool enter = k == Qt::Key_Return || k == Qt::Key_Enter;
        if (m_tabList && m_tabList->isVisible()) {
            const bool next = (mods == Qt::NoModifier && (k == Qt::Key_Tab || k == Qt::Key_Down));
            const bool previous = ((mods == Qt::ShiftModifier && k == Qt::Key_Tab) || (mods == Qt::NoModifier && k == Qt::Key_Up));
            if (next || previous) {
                const int rows = m_tabList->count();
                m_tabList->setCurrentRow((m_tabList->currentRow() + (next ? 1 : rows - 1)) % rows);
                return true;
            }
            if (mods == Qt::NoModifier && enter) { acceptTabSelection(); return true; }
            if (k == Qt::Key_Escape) { hideTabPopup(); return true; }
            hideTabPopup();   // any other key edits the line again
        }
        // Tab completes the path or command being typed. Relay sends whole lines, so Readline
        // never sees the half-typed word.
        if (mods == Qt::NoModifier && k == Qt::Key_Tab && !m_native && !m_editor->toPlainText().isEmpty()
            && !(m_slashList && m_slashList->isVisible()) && !(m_atList && m_atList->isVisible())
            && completeInComposer())
            return true;
        if (m_slashList && m_slashList->isVisible()) {
            if (mods == Qt::NoModifier && (k == Qt::Key_Up || k == Qt::Key_Down)) {
                m_slashList->setCurrentRow(std::clamp(m_slashList->currentRow() + (k == Qt::Key_Down ? 1 : -1), 0, m_slashList->count() - 1));
                return true;
            }
            if (mods == Qt::NoModifier && k == Qt::Key_Tab) { acceptSlashSelection(false); return true; }
            if (mods == Qt::NoModifier && enter) { acceptSlashSelection(true); return true; }
            if (k == Qt::Key_Escape) { hideSlashPopup(); return true; }
        }
        // AI suggestions shown in an empty prompt box: Tab (or → / Ctrl+F) accepts.
        if (!m_aiGhost.isEmpty() && m_editor->toPlainText().isEmpty() && mods == Qt::NoModifier && k == Qt::Key_Tab) {
            const QString text = m_aiGhost;
            clearAiGhost();
            m_editor->setPlainText(text);
            m_editor->moveCursor(QTextCursor::End);
            return true;
        }
        // Enter queues; Enter again steers at the next tool call; Enter a third time interrupts.
        if (mods == Qt::NoModifier && enter && m_editor->toPlainText().trimmed().isEmpty()
            && (upgradeLastQueuedToSteer() || escalateSteerToInterrupt()))
            return true;
        // Esc stops a running program, so Ctrl+C is left to copying.
        if (mods == Qt::NoModifier && k == Qt::Key_Escape && !m_agentBusy && m_editor->toPlainText().isEmpty()
            && processBusy() && m_backend) {
            interruptShell();
            toast(QStringLiteral("Interrupted %1").arg(foregroundProgramName().isEmpty() ? QStringLiteral("the program") : foregroundProgramName()));
            return true;
        }
        // Esc Esc in an empty prompt box while the agent is idle opens Rewind. A single Esc no
        // longer takes control of the terminal (owner, 2026-09-17: Esc only interrupts); the
        // keyboard goes to a program through Ctrl+H or the "Take control" button.
        if (mods == Qt::NoModifier && k == Qt::Key_Escape && !m_agentBusy && m_editor->toPlainText().isEmpty()
            && m_selected < 0 && !(m_atList && m_atList->isVisible()) && m_configured) {
            if (m_escTimer.isActive()) { m_escTimer.stop(); openRewind(); return true; }
            m_escTimer.setSingleShot(true);
            m_escTimer.setInterval(350);
            m_escTimer.disconnect();
            m_escTimer.start();
            return true;
        }
        if (m_atList && m_atList->isVisible()) {
            if (mods == Qt::NoModifier && (k == Qt::Key_Up || k == Qt::Key_Down)) {
                const int row = std::clamp(m_atList->currentRow() + (k == Qt::Key_Down ? 1 : -1), 0, m_atList->count() - 1);
                m_atList->setCurrentRow(row);
                return true;
            }
            if (mods == Qt::NoModifier && (enter || k == Qt::Key_Tab)) { acceptAtSelection(); return true; }
            if (k == Qt::Key_Escape) { m_atDismissedAt = m_editor->textCursor().position(); hideAtPopup(); return true; }
        }
        if (m_selected >= 0 && m_selected < m_entries.size()) {
            if (mods == Qt::NoModifier && k == Qt::Key_Up) {
                if (m_selected > 0) { --m_selected; rebuildQueueStrip(); return true; }
                m_selected = -1; rebuildQueueStrip();
                return false;   // past the top: prompt history
            }
            if (mods == Qt::NoModifier && k == Qt::Key_Down) {
                m_selected = m_selected + 1 < m_entries.size() ? m_selected + 1 : -1;
                rebuildQueueStrip(); return true;
            }
            if (mods == Qt::ControlModifier && (k == Qt::Key_Up || k == Qt::Key_Down)) {
                const int to = m_selected + (k == Qt::Key_Up ? -1 : 1);
                if (to >= 0 && to < m_entries.size()) { m_entries.move(m_selected, to); m_selected = to; rebuildQueueStrip(); }
                return true;
            }
            if (mods == Qt::NoModifier && enter) {
                // Pull the item back into the prompt box; if it was next, hold the queue until it is resubmitted.
                const QueueEntry entry = m_entries.takeAt(m_selected);
                const bool wasNext = m_selected == 0;
                m_selected = -1;
                if (wasNext && !m_entries.isEmpty()) { m_resubmitAtFront = true; pauseQueue(QStringLiteral("editing the next item")); }
                else if (wasNext) m_resubmitAtFront = true;
                // The resubmission keeps the item's kind once, without changing the pane's input mode.
                m_editKind = entry.agent ? QStringLiteral("agent") : QStringLiteral("shell");
                m_editor->setPlainText(entry.text);
                m_editor->moveCursor(QTextCursor::End);
                rebuildQueueStrip(); changed();
                toast(entry.agent ? QStringLiteral("Editing a queued agent prompt") : QStringLiteral("Editing a queued command"));
                return true;
            }
            if (mods == Qt::NoModifier && (k == Qt::Key_Delete || k == Qt::Key_Backspace)) {
                removeEntry(m_entries[m_selected].id);
                return true;
            }
            if (k == Qt::Key_Escape) { m_selected = -1; rebuildQueueStrip(); return true; }
        }
        // --- subagents UI: Down on the last line (history at the draft) enters the running-agents list.
        // Up stays queue/history; the @ picker and a queue selection above already took their keys.
        if (mods == Qt::NoModifier && k == Qt::Key_Down && m_agentsPanel && m_agentsPanel->isVisible()
            && m_editor->textCursor().blockNumber() == m_editor->document()->blockCount() - 1 && m_editor->atDraft()) {
            m_agentsPanel->enter();
            return true;
        }
        // --- end subagents UI ---
        const bool empty = m_editor->toPlainText().isEmpty();
        if (empty && mods == Qt::NoModifier && k == Qt::Key_Up && !m_entries.isEmpty()) {
            m_selected = m_entries.size() - 1;
            rebuildQueueStrip();
            return true;
        }
        if (mods == Qt::NoModifier && k == Qt::Key_Escape && m_agentBusy) {
            stopAgent();
            toast(QStringLiteral("Agent interrupted"));
            return true;
        }
        if (!m_editor->ghost().isEmpty() && m_editor->textCursor().atEnd() && !m_editor->textCursor().hasSelection()) {
            if ((mods == Qt::NoModifier && k == Qt::Key_Right) || (mods == Qt::ControlModifier && k == Qt::Key_F))
                return m_editor->acceptGhost(true);
            if ((mods == Qt::AltModifier || mods == (Qt::ControlModifier | Qt::ShiftModifier)) && k == Qt::Key_Right)
                return m_editor->acceptGhost(false);
        }
        return false;
    }

    void onComposerEdited() {
        if (!m_editor->toPlainText().isEmpty()) m_idleTip.stop();
        if (!m_editor->toPlainText().isEmpty()) clearAiGhost();
        updateSlashPopup();
        if (m_selected >= 0 && !m_editor->toPlainText().isEmpty()) { m_selected = -1; rebuildQueueStrip(); }
        updateAtPopup();
        updateGhost();
    }

    // ----- history suggestions (ghost text) ---------------------------------------------------
    void updateGhost() {
        if (!m_editor) return;
        const QString text = m_editor->toPlainText();
        QString remainder;
        const QTextCursor cursor = m_editor->textCursor();
        if (QSettings().value(QStringLiteral("composer/history_suggestions"), true).toBool()
            && m_modeValue != QStringLiteral("agent") && !text.trimmed().isEmpty() && !text.contains('\n')
            && cursor.atEnd() && !cursor.hasSelection() && !(m_atList && m_atList->isVisible()) && !text.startsWith('@')
            && !(m_slashList && m_slashList->isVisible()) && !slashCommandFor(text)) {
            remainder = historySuggestion(text);
        }
        // An AI suggestion in the empty prompt box takes the placeholder's place.
        const bool aiGhost = text.isEmpty() && !m_aiGhost.isEmpty();
        if (aiGhost && !m_editor->placeholderText().isEmpty()) {
            m_savedPlaceholder = m_editor->placeholderText();
            m_editor->setPlaceholderText(QString());
        } else if (!aiGhost && m_editor->placeholderText().isEmpty() && !m_savedPlaceholder.isEmpty()) {
            m_editor->setPlaceholderText(m_savedPlaceholder);
        }
        if (aiGhost) {
            remainder = m_aiGhost;
            m_editor->setToolTip(m_aiGhostKind == QStringLiteral("next_prompt") ? QStringLiteral("Suggested prompt (AI) · Tab accepts")
                                                                                : QStringLiteral("Suggested command (AI) · → or Tab accepts"));
        } else if (m_editor->toolTip().contains(QStringLiteral("(AI)"))) {
            m_editor->setToolTip(QString());
        }
        m_editor->setGhost(remainder);
    }

    // Newest match first: commands run in this directory, then prompt history, then the shell's history file.
    QString historySuggestion(const QString &prefix) {
        auto fits = [&prefix](const QString &candidate) {
            return candidate.size() > prefix.size() && candidate.startsWith(prefix) && !candidate.contains('\n');
        };
        for (int i = m_commandLog.size() - 1; i >= 0; --i)
            if (m_commandLog[i].second == m_cwd && fits(m_commandLog[i].first)) return m_commandLog[i].first.mid(prefix.size());
        const QStringList &history = m_editor->history();
        for (int i = history.size() - 1; i >= 0; --i) if (fits(history[i])) return history[i].mid(prefix.size());
        const QStringList &shell = shellHistory();
        for (int i = shell.size() - 1; i >= 0; --i) if (fits(shell[i])) return shell[i].mid(prefix.size());
        return {};
    }

    const QStringList &shellHistory() {
        const QString path = qEnvironmentVariable("HISTFILE", QDir::homePath() + QStringLiteral("/.bash_history"));
        const QFileInfo info(path);
        if (!info.exists() || info.lastModified() == m_shellHistoryStamp) return m_shellHistory;
        m_shellHistoryStamp = info.lastModified();
        m_shellHistory.clear();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            if (file.size() > 512 * 1024) file.seek(file.size() - 512 * 1024);
            const QList<QByteArray> lines = file.readAll().split('\n');
            for (const QByteArray &line : lines) {
                const QString text = QString::fromUtf8(line).trimmed();
                if (!text.isEmpty() && !text.startsWith('#')) m_shellHistory.append(text);
            }
            while (m_shellHistory.size() > 5000) m_shellHistory.removeFirst();
        }
        return m_shellHistory;
    }

    // ----- @ file picker --------------------------------------------------------------------------
    static bool previewable(const QString &path) {
        static QMimeDatabase database;
        const QMimeType mime = database.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
        return mime.inherits(QStringLiteral("text/plain")) || mime.name().startsWith(QStringLiteral("image/"))
            || mime.name() == QStringLiteral("application/pdf") || mime.inherits(QStringLiteral("application/json"));
    }

    void refreshFileIndex() {
        if (m_fileIndexCwd == m_cwd && m_fileIndexAge.isValid() && m_fileIndexAge.elapsed() < 15000) return;
        m_fileIndexCwd = m_cwd; m_fileIndexAge.start();
        m_fileIndex.clear(); m_changedFiles.clear();
        auto git = [this](const QStringList &args, int timeout) {
            QProcess process;
            process.setWorkingDirectory(m_cwd);
            process.start(QStringLiteral("git"), args);
            if (!process.waitForFinished(timeout) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
                process.kill(); process.waitForFinished(200);
                return QByteArray();
            }
            return process.readAllStandardOutput();
        };
        const QString root = QString::fromUtf8(git({QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, 1000)).trimmed();
        if (!root.isEmpty()) {
            // Tracked plus untracked-but-not-ignored files, relative to the repository root.
            const QList<QByteArray> files = git({QStringLiteral("-C"), root, QStringLiteral("ls-files"), QStringLiteral("--cached"), QStringLiteral("--others"),
                                                 QStringLiteral("--exclude-standard"), QStringLiteral("-z")}, 2500).split('\0');
            for (const QByteArray &file : files) {
                if (file.isEmpty()) continue;
                m_fileIndex.append(QDir(root).filePath(QString::fromUtf8(file)));
                if (m_fileIndex.size() >= 20000) break;
            }
            const QList<QByteArray> status = git({QStringLiteral("-C"), root, QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("-z")}, 1500).split('\0');
            for (const QByteArray &entry : status)
                if (entry.size() > 3) m_changedFiles.insert(QDir(root).filePath(QString::fromUtf8(entry.mid(3))));
            return;
        }
        // Not a repository: a bounded walk that skips hidden and dependency folders.
        QDirIterator it(m_cwd, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext() && m_fileIndex.size() < 5000) {
            const QString path = it.next();
            const QString rel = QDir(m_cwd).relativeFilePath(path);
            if (rel.startsWith('.') || rel.contains(QStringLiteral("/.")) || rel.contains(QStringLiteral("node_modules/"))) continue;
            m_fileIndex.append(path);
        }
    }

    QString composerPath(const QString &absolute) const {
        const QString rel = QDir(m_cwd).relativeFilePath(absolute);
        QString shown = rel.startsWith(QStringLiteral("../../..")) ? absolute : rel;
        return shown.contains(' ') ? QStringLiteral("\"%1\"").arg(shown) : shown;
    }

    void updateAtPopup() {
        if (!m_editor || m_native) { hideAtPopup(); return; }
        const QTextCursor cursor = m_editor->textCursor();
        const QString before = cursor.block().text().left(cursor.positionInBlock());
        static const QRegularExpression token(QStringLiteral("(?:^|\\s)@([^\\s@\"]*)$"));
        const auto match = token.match(before);
        if (!match.hasMatch() || cursor.position() == m_atDismissedAt) { hideAtPopup(); return; }
        const QString query = match.captured(1);
        refreshFileIndex();
        struct Ranked { int score; QString path; };
        QList<Ranked> ranked;
        for (const QString &path : std::as_const(m_fileIndex)) {
            const QString rel = QDir(m_cwd).relativeFilePath(path);
            const QString name = QFileInfo(path).fileName();
            int score = 0;
            if (query.isEmpty()) {
                score = 1;
                if (m_recentFiles.contains(path)) score += 100000 - m_recentFiles.indexOf(path);
                if (m_changedFiles.contains(path)) score += 50000;
            } else {
                const int nameHit = relayFuzzyScore(query, name), pathHit = relayFuzzyScore(query, rel);
                if (!nameHit && !pathHit) continue;
                score = nameHit * 3 + pathHit - rel.size();
                if (name.compare(query, Qt::CaseInsensitive) == 0) score += 100000;
            }
            if (previewable(path)) score += 2000;
            ranked.append({score, path});
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) { return a.score > b.score; });
        if (ranked.isEmpty()) { hideAtPopup(); return; }
        if (!m_atList) {
            m_atList = new QListWidget(this);
            m_atList->setObjectName(QStringLiteral("atPicker"));
            m_atList->setFocusPolicy(Qt::NoFocus);
            m_atList->setUniformItemSizes(true);
            connect(m_atList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { m_atList->setCurrentItem(item); acceptAtSelection(); });
        }
        m_atList->clear();
        for (int i = 0; i < std::min<int>(50, ranked.size()); ++i) {
            const QString rel = QDir(m_cwd).relativeFilePath(ranked[i].path);
            auto *item = new QListWidgetItem((previewable(ranked[i].path) ? QStringLiteral("◆  ") : QStringLiteral("◇  ")) + rel, m_atList);
            item->setData(Qt::UserRole, ranked[i].path);
            item->setToolTip(ranked[i].path);
        }
        m_atList->setCurrentRow(0);
        placeAtPopup();
        m_atList->show();
        m_atList->raise();
        if (m_editor->ghost().size()) m_editor->setGhost(QString());
    }

    void placeAtPopup() {
        if (!m_atList || !m_composer) return;
        const QRect composer(m_composer->mapTo(this, QPoint(0, 0)), m_composer->size());
        const int rowHeight = std::max(18, m_atList->sizeHintForRow(0));
        const int height = std::min(8, m_atList->count()) * rowHeight + 8;
        const int width = std::min(640, composer.width() - 24);
        m_atList->setGeometry(composer.left() + 12, std::max(0, composer.top() - height - 4), width, height);
    }

    void hideAtPopup() { if (m_atList && m_atList->isVisible()) m_atList->hide(); }

    QStringList knownCommandNames() const {
        QStringList names;
        for (const QJsonValue &value : m_knownCommands) names << value.toString();
        return names;
    }

    // Returns true when Tab did something: completed the word, or opened the candidate list.
    bool completeInComposer() {
        const QTextCursor cursor = m_editor->textCursor();
        if (cursor.hasSelection()) return false;
        const QString line = cursor.block().text();
        const relay::Completion completion =
            relay::completeAt(line, cursor.positionInBlock(), m_cwd, knownCommandNames());
        if (completion.inserts.isEmpty()) return true;   // nothing matches: swallow the Tab
        const QString typed = line.mid(completion.start, completion.length);
        if (completion.inserts.size() == 1) {
            // A directory keeps the cursor after the slash, so the next Tab walks into it.
            replaceComposerToken(completion, completion.inserts.first()
                                 + (completion.inserts.first().endsWith('/') ? QString() : QStringLiteral(" ")));
            hideTabPopup();
            return true;
        }
        if (completion.common.size() > typed.size()) replaceComposerToken(completion, completion.common);
        showTabPopup(completion);
        return true;
    }

    void replaceComposerToken(const relay::Completion &completion, const QString &text) {
        QTextCursor cursor = m_editor->textCursor();
        cursor.setPosition(cursor.block().position() + completion.start);
        cursor.setPosition(cursor.block().position() + completion.start + completion.length, QTextCursor::KeepAnchor);
        cursor.insertText(text);
        m_editor->setTextCursor(cursor);
    }

    void showTabPopup(const relay::Completion &completion) {
        if (!m_tabList) {
            m_tabList = new QListWidget(this);
            m_tabList->setObjectName(QStringLiteral("atPicker"));
            m_tabList->setFocusPolicy(Qt::NoFocus);
            m_tabList->setUniformItemSizes(true);
            connect(m_tabList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
                m_tabList->setCurrentItem(item); acceptTabSelection();
            });
        }
        m_tabList->clear();
        m_tabCompletion = completion;
        for (int i = 0; i < completion.labels.size() && i < 200; ++i) {
            auto *item = new QListWidgetItem(completion.labels.at(i), m_tabList);
            item->setData(Qt::UserRole, completion.inserts.at(i));
        }
        m_tabList->setCurrentRow(0);
        placeTabPopup();
        m_tabList->show();
        m_tabList->raise();
        hint(QStringLiteral("completion"), QStringLiteral("Tab again cycles, Enter accepts, Esc closes"));
    }

    void placeTabPopup() {
        if (!m_tabList || !m_composer) return;
        const QRect composer(m_composer->mapTo(this, QPoint(0, 0)), m_composer->size());
        const int rowHeight = std::max(18, m_tabList->sizeHintForRow(0));
        const int height = std::min(8, m_tabList->count()) * rowHeight + 8;
        const int width = std::min(640, composer.width() - 24);
        m_tabList->setGeometry(composer.left() + 12, std::max(0, composer.top() - height - 4), width, height);
    }

    void hideTabPopup() { if (m_tabList && m_tabList->isVisible()) m_tabList->hide(); }

    void acceptTabSelection() {
        if (!m_tabList || !m_tabList->currentItem()) return;
        const QString insert = m_tabList->currentItem()->data(Qt::UserRole).toString();
        replaceComposerToken(m_tabCompletion, insert + (insert.endsWith('/') ? QString() : QStringLiteral(" ")));
        hideTabPopup();
    }

    void acceptAtSelection() {
        if (!m_atList || !m_atList->currentItem()) return;
        const QString path = m_atList->currentItem()->data(Qt::UserRole).toString();
        QTextCursor cursor = m_editor->textCursor();
        const QString before = cursor.block().text().left(cursor.positionInBlock());
        const int at = before.lastIndexOf('@');
        if (at < 0) { hideAtPopup(); return; }
        cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, before.size() - at);
        cursor.insertText(QStringLiteral("@") + composerPath(path) + ' ');
        m_editor->setTextCursor(cursor);
        hideAtPopup();
    }

    QString resolveComposerPath(QString path) const {
        if (path.startsWith(QStringLiteral("~/"))) path = QDir::homePath() + path.mid(1);
        const QFileInfo info(QDir(m_cwd).filePath(path));
        return info.exists() ? info.absoluteFilePath() : QString();
    }

    // `@path` tokens that name existing files become attachments on agent prompts.
    QJsonArray attachmentsFor(const QString &text) const {
        QJsonArray attachments;
        QSet<QString> seen;
        static const QRegularExpression token(QStringLiteral("(?:^|\\s)@(?:\"([^\"]+)\"|([^\\s\"]+))"));
        auto matches = token.globalMatch(text);
        while (matches.hasNext() && attachments.size() < 10) {
            const auto match = matches.next();
            const QString absolute = resolveComposerPath(match.captured(1).isEmpty() ? match.captured(2) : match.captured(1));
            if (absolute.isEmpty() || !QFileInfo(absolute).isFile() || seen.contains(absolute)) continue;
            seen.insert(absolute);
            attachments.append(QJsonObject{{"path", absolute}});
        }
        return attachments;
    }

    // ----- program state: alternate screen, passwords, waiting for input ----------------------
    // Called from the backend's onAltScreenChanged (Konsole's Session signal or the engine).
    void onPrimaryScreen(bool primary) {
        m_altScreen = !primary;
        if (!primary) {
            if (m_native) return;
            const QString program = foregroundProgramName();
            endWaiting(false);
            leaveSecretMode();
            // Relay no longer takes the keyboard for a full-screen program (issue decision 2):
            // the prompt box keeps it and the pane offers "Take control". People who want the
            // old behaviour turn it back on per program or globally (control/default = human).
            if (controlFor(program) == QStringLiteral("human")) {
                m_autoHuman = true;
                setNative(true);
                m_hideReason = HideReason::AltScreen;
                return;
            }
            updateTakeControl();
            toast(QStringLiteral("%1 is running · %2 to type into it")
                  .arg(program.isEmpty() ? QStringLiteral("A full-screen program") : program,
                       Keymap::instance().shortcutText(QStringLiteral("control.human"))));
        } else if (m_native && m_hideReason == HideReason::AltScreen) {
            m_autoHuman = false;
            setNative(false, false);
            updateTakeControl();
        } else {
            updateTakeControl();
        }
    }

    // Remote sessions never switch screens, so a short list stands in for detection.
    static bool remoteSessionProgram(const QString &name) {
        static const QSet<QString> names{QStringLiteral("ssh"), QStringLiteral("mosh"), QStringLiteral("mosh-client"), QStringLiteral("telnet")};
        return names.contains(name);
    }

    void pollProgram() {
        if (m_promptReported || !m_backend) {
            m_programPoll.stop(); endWaiting(true); updateOpaqueProgram(); checkPasswordPrompt(); updateTakeControl();
            return;
        }
        updateOpaqueProgram();
        checkPasswordPrompt();
        if (!m_native && !m_altScreen && !m_secretMode && m_runningSince.isValid() && m_runningSince.elapsed() > 300) {
            const QString program = foregroundProgramName();
            if (remoteSessionProgram(program) && !m_remoteHandled) {
                // ssh and friends never switch screens, so a short list stands in for detection.
                m_remoteHandled = true;
                m_remoteProgram = true;
                endWaiting(false);
                if (controlFor(program) == QStringLiteral("human")) {
                    m_autoHuman = true; setNative(true); m_hideReason = HideReason::Remote;
                    return;
                }
                updateTakeControl();
                toast(QStringLiteral("%1 is running · %2 to type into it")
                          .arg(program, Keymap::instance().shortcutText(QStringLiteral("control.human"))));
                return;
            }
            if (!m_opaqueProgram.isEmpty()) {
                // sudo & co.: Relay cannot read their syscalls, so it cannot see them waiting.
                // A password prompt is still visible in the line discipline (checkPasswordPrompt);
                // anything else queues, and the hint in the composer row says so.
                hint(QStringLiteral("queue.whileRunning"),
                     QStringLiteral("Tip: type the next command here while %1 runs · it is queued until the terminal is free")
                         .arg(m_opaqueProgram));
            } else if (programWaitingForInput()) {
                if (++m_waitTicks == 2) startWaiting();
            } else {
                if (m_waiting) endWaiting(true);
                m_waitTicks = 0;
            }
        }
        updateTakeControl();
    }

    using TerminalMode = relay::input::TerminalMode;
    TerminalMode terminalMode() const {
        if (!m_backend) return TerminalMode::Unknown;
        const int pid = shellPid();
        if (pid <= 0) return TerminalMode::Unknown;
        const auto name = QStringLiteral("/proc/%1/fd/0").arg(pid).toLocal8Bit();
        const int fd = ::open(name.constData(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return TerminalMode::Unknown;
        termios state{};
        const bool ok = ::tcgetattr(fd, &state) == 0;
        ::close(fd);
        if (!ok) return TerminalMode::Unknown;
        if (!(state.c_lflag & ICANON)) return TerminalMode::Raw;
        return (state.c_lflag & ECHO) ? TerminalMode::Echoing : TerminalMode::Secret;
    }

    // Everything the input rules (src/InputPolicy.h) need about this pane. `live` also asks
    // /proc whether a process of the running command is blocked reading the terminal; the
    // 250 ms poll's answer (m_waiting) is used when that is too expensive.
    relay::input::State inputState(bool live = true) const {
        relay::input::State state;
        state.mode = terminalMode();
        state.programRunning = processBusy();
        state.altScreen = m_altScreen;
        state.native = m_native;
        state.programReading = state.programRunning && (m_waiting || (live && programWaitingForInput()));
        return state;
    }

    // A process of this pane's command is blocked in read() on the terminal. Reading
    // /proc/<pid>/syscall needs ptrace access, so programs running as another user (sudo) are
    // not visible here.
    bool programWaitingForInput() const {
        if (!m_backend) return false;
        const int shell = shellPid();
        if (shell <= 0) return false;
        const QString tty = QFileInfo(QStringLiteral("/proc/%1/fd/0").arg(shell)).symLinkTarget();
        if (tty.isEmpty()) return false;
        QList<int> pids{shell};
        for (int i = 0; i < pids.size() && pids.size() < 64; ++i) {
            QFile children(QStringLiteral("/proc/%1/task/%1/children").arg(pids[i]));
            if (!children.open(QIODevice::ReadOnly)) continue;
            for (const QByteArray &child : children.readAll().simplified().split(' ')) {
                bool ok = false; const int pid = child.toInt(&ok);
                if (ok && pid > 0) pids.append(pid);
            }
        }
        for (int pid : std::as_const(pids)) {
            QFile syscallFile(QStringLiteral("/proc/%1/syscall").arg(pid));
            if (!syscallFile.open(QIODevice::ReadOnly)) continue;
            const QList<QByteArray> parts = syscallFile.readAll().simplified().split(' ');
            if (parts.size() < 2) continue;
            bool ok = false;
            if (parts[0].toLong(&ok) != SYS_read || !ok) continue;
            const long fd = parts[1].toLong(&ok, 0);
            if (!ok || fd < 0) continue;
            if (QFileInfo(QStringLiteral("/proc/%1/fd/%2").arg(pid).arg(fd)).symLinkTarget() == tty) return true;
        }
        return false;
    }

    // Programs Relay cannot inspect: sudo, doas, pkexec, su, run0, anything running as another user,
    // or a process whose /proc/<pid>/syscall is unreadable. The prompt box stays visible, but the
    // keyboard focus stays in the terminal; a hint in the composer row says how to type a prompt.
    QString opaqueForegroundProgram() const {
        if (!processBusy()) return {};
        const int shell = shellPid();
        long group = shell > 0 ? foregroundGroup(shell) : -1;
        if (group <= 0 || group == shell) group = foregroundPid();
        if (group <= 0 || group == shell) return {};
        QString name = foregroundProgramName();
        static const QSet<QString> elevators{QStringLiteral("sudo"), QStringLiteral("doas"), QStringLiteral("pkexec"),
                                             QStringLiteral("su"), QStringLiteral("run0")};
        if (elevators.contains(name)) return name;
        if (name.isEmpty()) name = QStringLiteral("A program");
        QFile statusFile(QStringLiteral("/proc/%1/status").arg(group));
        if (statusFile.open(QIODevice::ReadOnly)) {
            for (const QByteArray &line : statusFile.readAll().split('\n')) {
                if (!line.startsWith("Uid:")) continue;
                const QList<QByteArray> ids = line.mid(4).simplified().split(' ');
                if (ids.size() >= 2 && (ids[0].toUInt() != ::getuid() || ids[1].toUInt() != ::geteuid())) return name;
                break;
            }
        }
        auto readable = [](long pid) {
            QFile file(QStringLiteral("/proc/%1/syscall").arg(pid));
            return file.open(QIODevice::ReadOnly) && !file.readAll().isEmpty();
        };
        if (readable(shell) && !readable(group)) return name;
        return {};
    }

    void updateOpaqueProgram() {
        const QString name = (m_promptReported || m_altScreen || !m_backend) ? QString() : opaqueForegroundProgram();
        if (name != m_opaqueProgram) {
            m_opaqueProgram = name;
            if (!name.isEmpty()) endWaiting(false);
        }
        refreshProgramHint();
    }

    // The line in the composer row that says who owns the terminal: a program Relay cannot
    // inspect (sudo & co.), or one that is waiting for a line from the prompt box.
    void refreshProgramHint() {
        if (!m_opaqueHint) return;
        QString text;
        if (!m_native && !m_secretMode) {
            if (m_waiting)
                text = QStringLiteral("%1 is waiting for input · Enter sends your line to it")
                           .arg(foregroundProgramName().isEmpty() ? QStringLiteral("The program") : foregroundProgramName());
            else if (!m_opaqueProgram.isEmpty())
                text = QStringLiteral("%1 is running · prompts queue until it exits · %2 to type into it")
                           .arg(m_opaqueProgram, Keymap::instance().shortcutText(QStringLiteral("control.human")));
        }
        m_opaqueHint->setText(text);
        m_opaqueHint->setVisible(!text.isEmpty());
        m_routeLabel->setVisible(text.isEmpty());
    }

    // A program is blocked reading a line (`apt`'s `[Y/n]`). The prompt box keeps the keyboard;
    // what is submitted there answers the program instead of being queued (issue decision 5).
    void startWaiting() {
        m_waiting = true;
        refreshProgramHint();
        toast(QStringLiteral("%1 is waiting for input · Enter here sends your answer to it")
                  .arg(foregroundProgramName().isEmpty() ? QStringLiteral("The program") : foregroundProgramName()));
    }

    void endWaiting(bool) {
        const bool was = m_waiting;
        m_waiting = false; m_waitTicks = 0;
        if (was) refreshProgramHint();
    }

    struct PendingPrompt { QString text, why, program; bool fix = false; };

    // Another turn will start without user action: something is queued and the queue is not paused.
    // Another turn will start without user action: queued items, or a turn that is interrupting this one.
    bool moreTurnsPending() const { return (!m_entries.isEmpty() && !m_entriesPaused) || m_interruptPending; }

    QString sendPrompt(QJsonObject request, const PendingPrompt &prompt) {
        const QString requestId = QStringLiteral("ask-%1").arg(++m_askSerial);
        request.insert(QStringLiteral("id"), requestId);
        m_pendingPrompts.insert(requestId, prompt);
        send(request);
        return requestId;
    }

    // The queue strip over the bottom of the terminal: what is running, then queued terminal
    // commands ($, amber) and agent prompts (✦, cyan) in order. Rows drag to reorder; × removes.
    void rebuildQueueStrip() {
        if (!m_queueStrip) return;
        const bool visible = !m_entries.isEmpty() || m_entriesPaused || !m_steering.isEmpty();
        auto *layout = static_cast<QVBoxLayout *>(m_queueStrip->layout());
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (QWidget *w = item->widget()) { if (w != m_queueList) w->deleteLater(); }
            else if (QLayout *l = item->layout()) {
                while (QLayoutItem *inner = l->takeAt(0)) { if (inner->widget() && inner->widget() != m_requestsChip) inner->widget()->deleteLater(); delete inner; }
            }
            delete item;
        }
        m_queueStrip->setVisible(visible);
        // The Tasks chip sits in the queue strip while it shows, else in the composer row.
        if (m_requestsChip && m_routeRow && (!visible || !m_ledger.hasTasks()) && m_routeRow->indexOf(m_requestsChip) < 0) {
            m_routeRow->insertWidget(3, m_requestsChip);
            m_requestsChip->setVisible(m_ledger.hasTasks());
        }
        if (!visible) return;
        auto *header = new QHBoxLayout;
        auto *title = new QLabel(m_entriesPaused ? QStringLiteral("QUEUE · PAUSED") : QStringLiteral("QUEUE"));
        title->setObjectName(QStringLiteral("queueTitle"));
        title->setToolTip(m_pauseReason);
        header->addWidget(title, 1);
        if (m_requestsChip && m_ledger.hasTasks()) { header->addWidget(m_requestsChip); m_requestsChip->show(); }
        auto *hint = new QLabel(QStringLiteral("↑ select · Ctrl+↑↓ move · Enter edit · Del remove"));
        hint->setObjectName(QStringLiteral("queueHint"));
        header->addWidget(hint);
        if (m_entriesPaused) {
            auto *resume = new QToolButton; resume->setText(QStringLiteral("Resume")); resume->setFocusPolicy(Qt::NoFocus);
            connect(resume, &QToolButton::clicked, this, [this] { resumeAgentQueue(); });
            header->addWidget(resume);
        }
        if (m_entries.size() > 1) {
            auto *clear = new QToolButton; clear->setText(QStringLiteral("Clear")); clear->setFocusPolicy(Qt::NoFocus);
            connect(clear, &QToolButton::clicked, this, [this] { clearAgentQueue(); });
            header->addWidget(clear);
        }
        layout->addLayout(header);
        QString running;
        if (m_activeValid) running = (m_active.agent ? QStringLiteral("✦ ") : QStringLiteral("$ ")) + (m_active.fix ? QStringLiteral("fix request") : m_active.text);
        else if (m_agentBusy) running = QStringLiteral("✦ ") + m_itemPrompts.value(m_currentItem).text;
        else if (!m_promptReported && !m_pendingCommand.isEmpty()) running = QStringLiteral("$ ") + m_pendingCommand;
        if (!running.trimmed().isEmpty()) {
            auto *label = new QLabel(QStringLiteral("▸ running  ") + fontMetrics().elidedText(running.simplified(), Qt::ElideRight, std::max(160, width() - 180)));
            label->setObjectName(QStringLiteral("queueRunning"));
            layout->addWidget(label);
        }
        for (const auto &steer : std::as_const(m_steering)) {
            auto *label = new QLabel(QStringLiteral("↪ next tool call  ✦ ") + fontMetrics().elidedText(steer.text.simplified(), Qt::ElideRight, std::max(160, width() - 220)));
            label->setObjectName(QStringLiteral("queueSteer"));
            label->setToolTip(QStringLiteral("Delivered inside the running turn at the agent's next tool call"
                                            " · Enter on the empty prompt box interrupts the turn and sends it now"));
            layout->addWidget(label);
        }
        if (!m_queueList) {
            m_queueList = new QListWidget(m_queueStrip);
            m_queueList->setObjectName(QStringLiteral("queueList"));
            m_queueList->setItemDelegate(new QueueRowDelegate(m_queueList));
            m_queueList->setFocusPolicy(Qt::NoFocus);
            m_queueList->setFrameShape(QFrame::NoFrame);
            m_queueList->setSelectionMode(QAbstractItemView::SingleSelection);
            m_queueList->setDragDropMode(QAbstractItemView::InternalMove);
            m_queueList->setDefaultDropAction(Qt::MoveAction);
            m_queueList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            connect(m_queueList->model(), &QAbstractItemModel::rowsMoved, this, [this] { syncEntriesFromList(); });
            connect(m_queueList->model(), &QAbstractItemModel::rowsInserted, this, [this] { if (!m_fillingQueueList) syncEntriesFromList(); });
        }
        m_fillingQueueList = true;
        m_queueList->clear();
        for (const auto &entry : std::as_const(m_entries)) {
            auto *item = new QListWidgetItem(entry.fix ? QStringLiteral("fix request") : entry.text, m_queueList);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.id));
            item->setData(Qt::UserRole + 1, entry.agent);
            item->setToolTip(entry.text);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        m_fillingQueueList = false;
        if (m_selected >= 0 && m_selected < m_queueList->count()) m_queueList->setCurrentRow(m_selected);
        else { m_queueList->clearSelection(); m_queueList->setCurrentRow(-1); }
        const int rowHeight = std::max(20, fontMetrics().height() + 8);
        m_queueList->setFixedHeight(std::min<int>(6, std::max<int>(1, m_entries.size())) * rowHeight + 4);
        m_queueList->setVisible(!m_entries.isEmpty());
        layout->addWidget(m_queueList);
        placeQueueStrip();
        QTimer::singleShot(0, this, [this] { placeQueueStrip(); });
    }

    void placeQueueStrip() {
        QTimer::singleShot(0, this, [this] { placeThinking(); });
        if (!m_queueStrip || !m_queueStrip->isVisible() || !m_terminalHost) return;
        const QRect host(m_terminalHost->mapTo(this, QPoint(0, 0)), m_terminalHost->size());
        const int height = std::min(m_queueStrip->sizeHint().height(), host.height() / 2);
        const int below = m_agentsPanel && m_agentsPanel->isVisible() ? m_agentsPanel->height() + 4 : 0;   // subagents UI
        m_queueStrip->setGeometry(host.left() + 8, host.bottom() - height - 6 - below, host.width() - 16, height);
        m_queueStrip->raise();
    }

    // The "Take control (Ctrl+H)" button floats over the top-right of the terminal, so it does
    // not take layout space away from the program drawing there.
    void placeTakeControl() {
        if (!m_takeControl || !m_takeControl->isVisible() || !m_terminalHost) return;
        const QRect host(m_terminalHost->mapTo(this, QPoint(0, 0)), m_terminalHost->size());
        const QSize size = m_takeControl->sizeHint();
        m_takeControl->setGeometry(host.right() - size.width() - 10, host.top() + 8, size.width(), size.height());
        m_takeControl->raise();
    }

    // Shown while a full-screen program (alternate screen) or a remote session owns the terminal
    // and the prompt box still has the keyboard.
    void updateTakeControl() {
        if (!m_takeControl) return;
        const bool show = relay::input::offerTakeControl(inputState(false), m_remoteProgram);
        if (show) {
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("control.human"));
            m_takeControl->setText(keys.isEmpty() ? QStringLiteral("Take control") : QStringLiteral("Take control (%1)").arg(keys));
            const QString program = foregroundProgramName();
            m_takeControl->setToolTip(program.isEmpty()
                ? QStringLiteral("Hide the prompt box and type into the program")
                : QStringLiteral("Hide the prompt box and type into %1").arg(program));
            m_takeControl->adjustSize();
        }
        m_takeControl->setVisible(show);
        placeTakeControl();
    }

    bool readlineReady() const {
        if (!m_backend) return false;
        const int pid = shellPid();
        if (pid <= 0) return false;
        const auto name = QStringLiteral("/proc/%1/fd/0").arg(pid).toLocal8Bit();
        const int fd = ::open(name.constData(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return false;
        termios state{};
        const bool raw = ::tcgetattr(fd, &state) == 0 && !(state.c_lflag & ICANON);
        ::close(fd);
        // tcgetpgrp() fails with ENOTTY on any Linux kernel unless the terminal is the caller's
        // controlling tty, which it never is for Relay. /proc/<pid>/stat field 8 (tpgid)
        // reports the same foreground process group without that restriction.
        return raw && foregroundGroup(pid) == pid;
    }

    static long foregroundGroup(int pid) {
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!stat.open(QIODevice::ReadOnly)) return -1;
        const QByteArray data = stat.read(4096);
        const int close = data.lastIndexOf(')');
        if (close < 0) return -1;
        // After "pid (comm) ": state ppid pgrp session tty_nr tpgid ...
        const auto fields = data.mid(close + 2).split(' ');
        bool ok = false;
        const long tpgid = fields.size() > 5 ? fields.at(5).toLong(&ok) : -1;
        return ok ? tpgid : -1;
    }

    void refreshShellReady() {
        m_shellReady = m_promptReported && !m_loading && readlineReady();
        if (m_refocus && m_shellReady && !m_native) {
            m_editor->setFocus(); m_refocus = false;
        }
    }

    void pollShell() {
        // PROMPT_COMMAND runs before Readline puts the tty into noncanonical mode.
        // Recheck on every tick, even when the state file has not changed.
        refreshShellReady();
        if (!m_entries.isEmpty() && !m_activeValid) pumpQueue();
        QFile file(m_runtime.filePath(QStringLiteral("state.json")));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return;
        const auto event = QJsonDocument::fromJson(file.readAll()).object();
        if (event.value(QStringLiteral("token")).toString() != m_token) return;
        const auto sequence = event.value(QStringLiteral("sequence")).toString();
        if (sequence.isEmpty() || sequence == m_shellSequence) return;
        m_shellSequence = sequence; m_seenShell = true;
        const QString stage = event.value(QStringLiteral("event")).toString();
        if (const int reported = event.value(QStringLiteral("shell_pid")).toInt(); reported > 0) m_shellPid = reported;
        const QString newCwd = event.value(QStringLiteral("cwd")).toString(m_cwd);
        if (newCwd != m_cwd) { m_cwd = newCwd; updatePaths(); changed(); }
        if (stage == QStringLiteral("ready")) {
            m_promptReported = true;
            refreshShellReady();
            m_knownCommands = event.value(QStringLiteral("known_commands")).toArray();
            m_shellPath = event.value(QStringLiteral("path")).toString();
            const int status = event.value(QStringLiteral("status")).toInt();
            if (!m_agentBusy) this->status(QStringLiteral("Shell ready · exit %1").arg(status));
            // Fast commands can finish between two polls, so "running" is not a reliable trigger.
            const bool suggestNext = m_commandLoaded;
            m_commandLoaded = false;
            if (suggestNext && !m_pendingCommand.isEmpty() && !m_agentBusy
                && QSettings().value(QStringLiteral("suggestions/next_command"), false).toBool()) {
                const QString command = m_pendingCommand;
                QTimer::singleShot(250, this, [this, command, status] {
                    requestSuggestion(QStringLiteral("next_command"), {{"command", command}, {"exit_status", status}, {"cwd", m_cwd}});
                });
            }
            if (m_runningSince.isValid()) {
                const qint64 ms = m_runningSince.elapsed();
                m_runningSince.invalidate();
                if (ms > 30000) notifyIfAway(QStringLiteral("Command finished"), QStringLiteral("Exit %1 after %2 s in %3").arg(status).arg(ms / 1000).arg(m_cwd));
            }
            m_programPoll.stop();
            endWaiting(true);
            updateOpaqueProgram();
            m_remoteHandled = false; m_remoteProgram = false;
            m_secretDeclined = false; m_secretNotified = false;
            leaveSecretMode();
            if (m_native && m_autoHuman) { m_autoHuman = false; setNative(false, false); }
            updateTakeControl();
            if (m_activeValid && !m_active.agent && m_activeLoaded) {
                // The queued command finished; a failure pauses whatever is queued behind it.
                m_activeValid = false; m_activeLoaded = false;
                if (status != 0) pauseQueue(QStringLiteral("`%1` exited with status %2").arg(m_active.text).arg(status));
                QTimer::singleShot(150, this, [this] { rebuildQueueStrip(); pumpQueue(); });
            }
            if (m_fixArmed) {
                const QString command = m_fixCommand;
                const int attempt = m_fixAttempt;
                m_fixArmed = false; m_fixWatch = false;
                if (status == 0) {
                    if (attempt > 0) { printInline(QStringLiteral("✓ Fixed command succeeded.\n"), Ink::Note); closeInline(); }
                    clearFix();
                } else if (status == 130) {
                    clearFix();  // Interrupted with Ctrl+C: the user stopped it on purpose.
                } else {
                    // Defer until Readline has drawn the prompt and put the tty in raw mode.
                    QTimer::singleShot(200, this, [this, command, attempt, status] {
                        startFix(command, QStringLiteral("exited with status %1").arg(status), attempt + 1);
                    });
                }
            }
            // The command Relay ran has finished: index its line, exit status and captured output.
            finishCommandCapture(status);
            QTimer::singleShot(120, this, [this] { flushInline(); });
        } else if (stage == QStringLiteral("running")) {
            m_shellReady = false; m_promptReported = false;
            m_runningSince.start(); m_secretNotified = false;
            // The prompt box stays visible while ordinary programs run so more commands and prompts
            // can be queued. It hides for the alternate screen (Session signal), password prompts,
            // and remote sessions; a program blocked reading the terminal gets the focus instead.
            m_waitTicks = 0; m_echoTicks = 0; m_remoteHandled = false; m_remoteProgram = false; m_secretDeclined = false;
            m_programPoll.start();
            QTimer::singleShot(0, this, [this] { rebuildQueueStrip(); });
        } else if (stage == QStringLiteral("loaded") && m_loading && m_backend &&
                   event.value(QStringLiteral("input_sha256")).toString() == m_pendingHash) {
            m_loading = false; m_shellReady = false; m_promptReported = false; m_refocus = true;
            m_editor->remember(m_pendingCommand);
            m_commandLoaded = true;
            m_commandLog.append({m_pendingCommand, m_cwd});
            if (m_commandLog.size() > 500) m_commandLog.removeFirst();
            beginCommandCapture(m_pendingCommand);   // conversation index (protocol 14)
            const bool fromQueue = m_activeValid && !m_active.agent;
            if (fromQueue) m_activeLoaded = true;
            // Do not discard edits typed while waiting for the shell acknowledgement.
            else if (m_editor->toPlainText() == m_submittedDraft) m_editor->clear();
            m_fixArmed = m_fixWatch;
            // Focus stays in the prompt box so more commands and prompts can be queued.
            sendShellInput(QStringLiteral("\r"));
        } else if (stage == QStringLiteral("unsupported")) {
            m_shellReady = false; m_promptReported = false; setNative(true);
            status(QStringLiteral("Your shell already has a DEBUG hook. It was left untouched; use native mode or relaunch with --clean-shell."));
        }
    }

    void setNative(bool enabled, bool cancelLine = true) {
        // Taking control leaves masked input: the password is then typed into the program itself.
        if (enabled && m_secretMode) { m_secretDeclined = true; leaveSecretMode(); }
        m_native = enabled;
        changed();
        m_editor->setReadOnly(enabled);
        // Human control hides the prompt box entirely; the terminal gets the space and the keys.
        if (m_composer) m_composer->setVisible(!enabled);
        placeSubagentsPanel();   // subagents UI: hidden with the composer
        if (!enabled) m_hideReason = HideReason::None;
        if (enabled) hideAtPopup();
        applyTerminalFocusPolicy();
        if (enabled) {
            m_routeLabel->setText(QStringLiteral("NATIVE · keystrokes go directly to the terminal."));
            focusTerminal();
        } else {
            // Returning from native mode cancels Readline's partial line at a prompt.
            // Never inject a cancellation into a foreground TUI/process here.
            if (cancelLine && m_shellReady && m_backend && (foregroundPid() <= 0 || foregroundPid() == shellPid())) {
                sendShellInput(QString(QChar(3))); m_shellReady = false; m_promptReported = false; m_refocus = true;
            }
            focusInput();
            requestRoute(false, QStringLiteral("auto"));
        }
        refreshProgramHint();
        updateTakeControl();
    }

    // The terminal widget only accepts the keyboard in native mode; in every other state the
    // prompt box is the input, so a click must not be able to focus it.
    void applyTerminalFocusPolicy() {
        QWidget *target = m_backend ? m_backend->focusWidget() : nullptr;
        if (!target) return;
        if (m_terminalFocusPolicy == Qt::NoFocus) m_terminalFocusPolicy = target->focusPolicy();
        target->setFocusPolicy(m_native ? m_terminalFocusPolicy : Qt::NoFocus);
    }

    void focusTerminal() {
        // Only native input puts the keyboard in the terminal (issue decision 1).
        if (!m_native) { focusInput(); return; }
        if (QWidget *target = m_backend ? m_backend->focusWidget() : nullptr) target->setFocus(Qt::OtherFocusReason);
    }
    void updatePaths() {
        if (!m_cwdLabel) return;
        const QString home = QDir::homePath();
        auto tilde = [&home](const QString &path) { return path.startsWith(home) ? QStringLiteral("~") + path.mid(home.size()) : path; };
        m_cwdLabel->setText(width() >= 1000 || m_cwd == m_workspace
            ? QStringLiteral("TERMINAL  ") + tilde(m_cwd) + (m_cwd == m_workspace ? QString() : QStringLiteral("     │     AGENT WORKSPACE  ") + tilde(m_workspace))
            : tilde(m_cwd));
        m_cwdLabel->setToolTip(QStringLiteral("Terminal: ") + m_cwd + QStringLiteral("\nAgent workspace: ") + m_workspace);
    }
    void configure() {
        if (m_agentBusy) { status(QStringLiteral("Stop the current agent turn before changing provider settings.")); return; }
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Relay · Bring your own key")); dialog.resize(650, 520);
        QSettings settings;
        auto *layout = new QVBoxLayout(&dialog);
        auto *form = new QFormLayout;
        struct PresetRow { const char *id, *label, *base, *model, *extra; };
        static const PresetRow presets[] = {
            // Mirrors backend/relay_core/presets.py.
            {"custom", "Custom / current settings", "", "", ""},
            {"kimi", "Kimi · K3", "https://api.moonshot.ai/v1", "kimi-k3", "{\"reasoning_effort\":\"high\"}"},
            {"kimi-code", "Kimi Code · K3", "https://api.kimi.ai/coding/v1", "k3", "{\"reasoning_effort\":\"high\"}"},
            {"glm", "Z.AI · GLM-5.3 · standard API", "https://api.z.ai/api/paas/v4", "glm-5.3",
             "{\"thinking\":{\"type\":\"enabled\"},\"reasoning_effort\":\"high\"}"},
            {"glm-coding", "Z.AI · GLM-5.3 · Coding Plan", "https://api.z.ai/api/coding/paas/v4", "glm-5.3",
             "{\"thinking\":{\"type\":\"enabled\"},\"reasoning_effort\":\"high\"}"},
            {"openrouter", "OpenRouter · DeepSeek V4.1 Flash", "https://openrouter.ai/api/v1", "deepseek/deepseek-v4.1-flash", "{}"},
        };
        auto *preset = new QComboBox;
        for (const auto &row : presets) preset->addItem(QString::fromUtf8(row.label), QString::fromLatin1(row.id));
        const QString savedPreset = settings.value(QStringLiteral("provider/preset"), QStringLiteral("custom")).toString();
        auto *base = new QLineEdit(settings.value(QStringLiteral("provider/base"), QStringLiteral("https://api.moonshot.ai/v1")).toString());
        auto *model = new QLineEdit(settings.value(QStringLiteral("provider/model"), QStringLiteral("kimi-k3")).toString());
        auto *key = new QLineEdit(m_apiKey); key->setEchoMode(QLineEdit::Password);
        auto *extra = new QPlainTextEdit(settings.value(QStringLiteral("provider/extra"), QStringLiteral("{\"reasoning_effort\":\"high\"}")).toString());
        extra->setMaximumHeight(90);
        auto *tokens = new QSpinBox; tokens->setRange(256, 32768); tokens->setValue(settings.value("provider/max_tokens", 8192).toInt());
        auto *workspace = new QLineEdit(m_workspace);
        auto *workspaceRow = new QWidget; auto *workspaceLayout = new QHBoxLayout(workspaceRow); workspaceLayout->setContentsMargins(0, 0, 0, 0);
        auto *browse = new QPushButton(QStringLiteral("Choose…")); workspaceLayout->addWidget(workspace); workspaceLayout->addWidget(browse);
        connect(browse, &QPushButton::clicked, &dialog, [workspace, &dialog] {
            const auto path = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Choose agent workspace"), workspace->text());
            if (!path.isEmpty()) workspace->setText(path);
        });
        connect(preset, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [base, model, extra, key](int index) {
            if (index <= 0 || index >= int(std::size(presets))) return;
            key->clear();
            base->setText(QString::fromLatin1(presets[index].base));
            model->setText(QString::fromLatin1(presets[index].model));
            extra->setPlainText(QString::fromLatin1(presets[index].extra));
        });
        // Restore the preset label without overwriting edited fields.
        { QSignalBlocker blocker(preset); const int i = preset->findData(savedPreset); if (i >= 0) preset->setCurrentIndex(i); }
        key->setPlaceholderText(QStringLiteral("Leave empty to use the keyring key for this preset"));
        auto *saveKey = new QCheckBox(QStringLiteral("Save entered key to the desktop keyring"));
        auto *importWarp = new QPushButton(QStringLiteral("Import keys from Warp"));
        importWarp->setToolTip(QStringLiteral("Copies Warp's custom-endpoint API keys into Relay's keyring entries. Keys never pass through this window."));
        connect(importWarp, &QPushButton::clicked, &dialog, [this] { send({{"type", "import_warp"}}); });
        form->addRow(QStringLiteral("Preset"), preset); form->addRow(QStringLiteral("Base URL"), base);
        form->addRow(QStringLiteral("Model ID"), model); form->addRow(QStringLiteral("API key"), key);
        form->addRow(QString(), saveKey); form->addRow(QString(), importWarp);
        form->addRow(QStringLiteral("Extra request JSON"), extra); form->addRow(QStringLiteral("Output token limit"), tokens);
        form->addRow(QStringLiteral("Agent workspace"), workspaceRow); layout->addLayout(form);
        auto *notice = new QLabel(QStringLiteral("Entered keys are kept in process memory unless you choose to save them to the desktop keyring. Keys are never written to settings files. Changing settings starts a new conversation. Provider access and billing depend on your account.\n\nThe agent runs tools without asking. Shell commands are NOT sandboxed: they have your user permissions. File tools are restricted to this workspace. Terminal history is not sent automatically."));
        notice->setWordWrap(true); layout->addWidget(notice);
        auto *consent = new QCheckBox(QStringLiteral("Send my submitted agent prompts and tool results to this provider."));
        layout->addWidget(consent);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(extra->toPlainText().toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject() || !QFileInfo(workspace->text()).isDir() || !consent->isChecked()) {
                QMessageBox::warning(&dialog, QStringLiteral("Check settings"), QStringLiteral("Choose an existing workspace, enter valid JSON, and confirm provider data sharing.")); return;
            }
            const QString presetId = preset->currentData().toString();
            m_apiKey = key->text().trimmed(); m_workspace = QFileInfo(workspace->text()).canonicalFilePath();
            m_configured = false;
            settings.setValue("provider/preset", presetId); m_currentPreset = presetId; changed();
            settings.setValue("provider/base", base->text().trimmed()); settings.setValue("provider/model", model->text().trimmed());
            settings.setValue("provider/extra", extra->toPlainText()); settings.setValue("provider/max_tokens", tokens->value());
            if (saveKey->isChecked() && !m_apiKey.isEmpty() && presetId != QStringLiteral("custom"))
                send({{"type", "store_key"}, {"preset", presetId}, {"api_key", m_apiKey}});
            send(withSessionFields(QJsonObject{{"type", "configure"}, {"base_url", base->text().trimmed()}, {"model", model->text().trimmed()},
                  {"api_key", m_apiKey}, {"preset", presetId}, {"use_stored_key", m_apiKey.isEmpty()},
                  {"workspace", m_workspace}, {"extra", doc.object()}, {"max_tokens", tokens->value()},
                  {"keybindings", Keymap::instance().catalog()}}));
            updatePaths(); dialog.accept();
        });
        dialog.exec();
    }

    QString m_data, m_python, m_workspace, m_cwd, m_token, m_apiKey;
    QString m_shellSequence, m_shellPath, m_pendingHash, m_pendingCommand, m_pendingSubmit, m_previewId, m_submittedDraft;
    QJsonArray m_knownCommands;
    QTemporaryDir m_runtime{QDir::tempPath() + QStringLiteral("/relay-XXXXXX")};
    QProcess m_worker;
    QByteArray m_workerBuffer;
    QList<QByteArray> m_workerPending;
    QTimer m_poll, m_debounce;
    // The pane's terminal engine (src/TerminalBackends.h). m_backend is cleared when the
    // shell ends; m_backendOwned keeps the object alive until it can be destroyed safely.
    std::unique_ptr<relay::TerminalBackend> m_backendOwned;
    relay::TerminalBackend *m_backend = nullptr;
    relay::EngineKind m_engine = relay::EngineKind::Konsole;
    QString m_engineCore;
    QWidget *m_terminal = nullptr, *m_terminalHost = nullptr;
    RichEditor *m_editor = nullptr;
    QString m_modeValue = QStringLiteral("auto");
    QLabel *m_routeLabel = nullptr, *m_cwdLabel = nullptr, *m_help = nullptr;
    bool m_native = false, m_workerReady = false, m_shellReady = false, m_loading = false;
    bool m_promptReported = false;
    QString m_submitMode, m_model, m_turnText, m_fixCommand;
    int m_fixAttempt = 0;
    bool m_fixWatch = false, m_fixArmed = false, m_fixAwaitingAgent = false, m_turnHeader = false;
    bool m_inlineOpen = false, m_atLineStart = true;
    QList<QPair<QString, Ink>> m_inlinePending;
    QList<QPair<QString, QString>> m_stored;
    QComboBox *m_modeBox = nullptr, *m_modelBox = nullptr;
    QLabel *m_toast = nullptr;
    QLabel *m_prefixChip = nullptr;
    QPointer<relay::SkillsDialog> m_skillsDialog;
    QTimer m_assistDebounce, m_assistHold;
    QString m_assistLocalGuess, m_assistFailedText;
    QString m_assistId, m_assistText, m_assistInflightText, m_assistQueuedText, m_assistRoute, m_assistReason, m_heldMode;
    double m_assistConfidence = 0;
    QJsonObject m_heldDecision;
    QHash<QString, QString> m_turnThinking;
    QHash<QString, QJsonObject> m_turnSummaries;
    QStringList m_turnOrder;
    QHash<QString, QPointer<relay::TurnTranscriptView>> m_turnViews;
    QString m_lastTurnId;
    bool m_thinkingShown = false, m_thinkingDismissed = false, m_thinkingExpanded = false;
    QFrame *m_thinking = nullptr;
    QLabel *m_thinkingHeader = nullptr;
    QPlainTextEdit *m_thinkingView = nullptr;
    QString m_prefixMode, m_prefixPrevMode;
    QTimer m_idleTip;
    QFrame *m_banner = nullptr;
    QLabel *m_bannerText = nullptr;
    QPushButton *m_bannerAction = nullptr;
    std::function<void()> m_bannerCallback;
    QString m_shellUnit, m_agentUnit;
    int m_shellGeneration = 0, m_agentGeneration = 0;
    long m_oomKills = -1;
    int m_shellPid = 0;
    bool m_workerConnected = false, m_shellStopped = false, m_restarting = false;
    static inline bool s_isolationNoticeShown = false;
    QFrame *m_composer = nullptr, *m_transcript = nullptr;
    QLabel *m_transcriptHeader = nullptr;
    QPlainTextEdit *m_transcriptView = nullptr;
    QString m_transcriptProgram;
    QHash<QString, PendingPrompt> m_pendingPrompts, m_itemPrompts;   // request id / queue item id -> prompt
    QList<QPair<QString, QPair<QString, bool>>> m_queueItems;        // item id -> (preview, forced)
    QString m_runningItem, m_currentItem;
    bool m_queuePaused = false;
    QList<QueueEntry> m_entries;
    QueueEntry m_active;
    bool m_activeValid = false, m_activeLoaded = false, m_entriesPaused = false, m_resubmitAtFront = false;
    bool m_interruptPending = false, m_fillingQueueList = false;
    QString m_activeRequest, m_pauseReason;
    quint64 m_entrySerial = 0;
    int m_selected = -1;
    QListWidget *m_queueList = nullptr, *m_atList = nullptr;
    int m_atDismissedAt = -1;
    QString m_editKind;
    QStringList m_fileIndex, m_recentFiles, m_shellHistory;
    QSet<QString> m_changedFiles;
    QString m_fileIndexCwd;
    QElapsedTimer m_fileIndexAge;
    QDateTime m_shellHistoryStamp;
    QList<QPair<QString, QString>> m_commandLog;   // command, directory
    // Conversation list and search (protocol 14) plus the terminal-history capture.
    relay::conversations::Dialog *m_conversations = nullptr;
    relay::conversations::FindBar *m_findBar = nullptr;
    QByteArray m_capture;
    QString m_captureCommand, m_captureCwd;
    qint64 m_captureAt = 0;
    bool m_capturing = false;
    char m_lastPromptMark = 0;      // OSC 133 A/B/C/D, engine panes with the shell integration
    int m_lastMarkExitCode = -1;
    QTimer m_programPoll;
    HideReason m_hideReason = HideReason::None;
    bool m_altScreen = false, m_waiting = false, m_remoteHandled = false, m_remoteProgram = false;
    // prompt-box-only input: masked prompt box at a password prompt, and the take-control button
    QLineEdit *m_secretEdit = nullptr;
    QLabel *m_secretChip = nullptr;
    QPushButton *m_takeControl = nullptr;
    QString m_secretProgram;
    bool m_secretMode = false, m_secretDeclined = false;
    Qt::FocusPolicy m_terminalFocusPolicy = Qt::NoFocus;   // the terminal's own policy, for native mode
    QPoint m_clickOrigin;
    int m_waitTicks = 0, m_echoTicks = 0;
    quint64 m_askSerial = 0;
    QFrame *m_queueStrip = nullptr;
    bool m_transcriptDismissed = false;
    QTimer m_secretPoll;
    QElapsedTimer m_runningSince;
    bool m_autoHuman = false, m_secretNotified = false;
    QTimer m_toastTimer;
    QString m_currentPreset;
    // model roles (protocol 13): this pane's role and the worker's last role table
    QString m_agentRole = QStringLiteral("main");
    QJsonObject m_roleSummary;
    bool m_cleanShell = false, m_closing = false;
    QJsonArray m_presets;
    bool m_configuring = false;
    bool m_seenShell = false, m_refocus = true, m_configured = false, m_agentBusy = false;
    // agent sessions UI
    QLabel *m_planChip = nullptr, *m_ctxLabel = nullptr;
    QComboBox *m_effortBox = nullptr;
    QListWidget *m_slashList = nullptr;
    QListWidget *m_tabList = nullptr;   // Tab completion candidates
    relay::Completion m_tabCompletion;
    QString m_effort = QStringLiteral("high"), m_agentMode = QStringLiteral("build"), m_sessionId, m_sessionDir, m_forkTitle;
    QString m_aiGhost, m_aiGhostKind, m_suggestionId, m_savedPlaceholder;
    QJsonObject m_initialState;
    // saved window layout: the preset and conversation this pane was restored with
    QString m_restorePreset, m_restoreSession, m_restoreRequest;
    qint64 m_ctxUsed = 0, m_ctxWindow = 0, m_ctxLimit = 0;
    double m_ctxPercent = 0;
    bool m_ctxEstimated = false, m_compacting = false, m_contextNotePending = false;
    QString m_rewindKind = QStringLiteral("chat");
    bool m_rewindPending = false, m_forkPending = false, m_resumePending = false, m_recapManual = false;
    bool m_instructionsDialogPending = false, m_onboarding = false, m_agentsListPending = false, m_reconfigureOnNewChat = false;
    bool m_finishedWhileAway = false, m_forkLoadPending = false, m_commandLoaded = false, m_initialIsFork = true;
    int m_turnsCompleted = 0, m_lastRecapTurns = -1, m_skillCount = 0;
    QList<SteerEntry> m_steering;
    quint64 m_lastQueuedEntryId = 0;
    QString m_lastSteerRequest;
    QElapsedTimer m_lastQueuedAt, m_lastSteeredAt, m_awaySince;
    QTimer m_escTimer;
    // sudo & co. in the foreground
    QLabel *m_opaqueHint = nullptr;
    QString m_opaqueProgram;
    quint64 m_requestId = 0, m_loadSerial = 0;
    // subagents UI
    relay::SubagentModel m_subagents;
    // request ledger UI
    relay::RequestLedgerModel m_ledger;
    QToolButton *m_requestsChip = nullptr;
    QHBoxLayout *m_routeRow = nullptr;
    QPointer<relay::RequestsPanel> m_requestsPanel;
    bool m_limitReached = false;   // the last turn stopped at the step or tool-call limit
    relay::SubagentsPanel *m_agentsPanel = nullptr;
    QList<QPointer<relay::SubagentTranscriptView>> m_subagentViews;
    QPointer<relay::SubagentTranscriptView> m_subagentOverlay;
};


// A non-terminal pane: a folder explorer or a file preview. Lives in the same splitter layout
// as terminal panes and is saved and restored as {"explorer": {"path"}} or {"preview": {"path"}}.
class ToolPane final : public QWidget {
public:
    enum class Kind { Explorer, Preview, Plan, Subagent, Turn };

    ToolPane(Kind kind, const QString &path, bool planActions = true) : m_kind(kind) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        if (kind == Kind::Plan) {
            // Editable Markdown: agent plans (with Execute buttons) and documents such as relay.md.
            m_plan = new relay::PlanEditor;
            m_plan->setPlanActions(planActions);
            layout->addWidget(m_plan);
            m_plan->open(path);
        } else if (kind == Kind::Explorer) {
            m_explorer = new relay::FileExplorer(path);
            layout->addWidget(m_explorer);
        } else {
            m_preview = new relay::FilePreview;
            layout->addWidget(m_preview);
            m_preview->open(path);
        }
    }

    // subagents UI: a live subagent transcript. Not saved or restored (node() is empty).
    ToolPane(relay::SubagentTranscriptView *view, const QString &cwd) : m_kind(Kind::Subagent), m_subagent(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }

    // A finished agent turn: tool calls and transcript, opened from the inline summary line.
    ToolPane(relay::TurnTranscriptView *view, const QString &cwd) : m_kind(Kind::Turn), m_turn(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }
    relay::TurnTranscriptView *turn() const { return m_turn; }

    Kind kind() const { return m_kind; }
    relay::FileExplorer *explorer() const { return m_explorer; }
    relay::FilePreview *preview() const { return m_preview; }
    relay::PlanEditor *plan() const { return m_plan; }
    relay::SubagentTranscriptView *subagent() const { return m_subagent; }
    QString path() const { return (m_subagent || m_turn) ? QString() : m_explorer ? m_explorer->root() : m_plan ? m_plan->path() : m_preview->path(); }
    QString cwd() const { return (m_subagent || m_turn) ? m_subagentCwd : m_explorer ? m_explorer->root() : QFileInfo(path()).absolutePath(); }
    QString title() const {
        if (m_subagent) return m_subagent->title();
        if (m_turn) return m_turn->title();
        if (m_plan) return (m_plan->isDirty() ? QStringLiteral("● ") : QString()) + m_plan->title();
        const QString name = QFileInfo(path()).fileName();
        return name.isEmpty() ? path() : name;
    }
    QJsonObject node() const {
        if (m_subagent || m_turn) return {};
        if (m_plan) return {{"plan", QJsonObject{{"path", path()}}}};
        return {{m_explorer ? "explorer" : "preview", QJsonObject{{"path", path()}}}};
    }
    void focusInput() {
        if (m_subagent) m_subagent->focusInput();
        else if (m_turn) m_turn->focusInput();
        else if (m_plan) m_plan->editor()->setFocus(Qt::OtherFocusReason);
        else if (m_explorer) m_explorer->view()->setFocus(Qt::OtherFocusReason);
        else m_preview->setFocus(Qt::OtherFocusReason);
    }

private:
    Kind m_kind;
    relay::FileExplorer *m_explorer = nullptr;
    relay::FilePreview *m_preview = nullptr;
    relay::PlanEditor *m_plan = nullptr;
    relay::SubagentTranscriptView *m_subagent = nullptr;
    relay::TurnTranscriptView *m_turn = nullptr;
    QString m_subagentCwd;
};

// ----- pane chrome: button row and drag handle -----------------------------------------------
// A small overlay in each pane's top-right corner, shown while the mouse is over the pane:
// drag grip, split right, split down, move to new tab, close. Dragging the grip moves the pane.
class PaneChrome final : public QFrame {
public:
    std::function<void(const QString &action)> onAction;
    std::function<void(const QPoint &global)> onDragMove;
    std::function<void(const QPoint &global, bool drop)> onDragEnd;

    explicit PaneChrome(QWidget *leaf) : QFrame(leaf) {
        setObjectName(QStringLiteral("paneChrome"));
        setAttribute(Qt::WA_StyledBackground);
        auto *row = new QHBoxLayout(this); row->setContentsMargins(3, 2, 3, 2); row->setSpacing(1);
        m_grip = new QLabel(QStringLiteral("⠿"));
        m_grip->setObjectName(QStringLiteral("paneGrip"));
        m_grip->setCursor(Qt::OpenHandCursor);
        m_grip->setToolTip(QStringLiteral("Drag onto another pane's edge to move this pane there, or onto the tab bar to make it a tab"));
        m_grip->installEventFilter(this);
        row->addWidget(m_grip);
        // The + makes it obvious that these open a new pane (a new shell and chat), not a layout toggle.
        button(row, QStringLiteral("◫+"), QStringLiteral("pane.splitRight"), QStringLiteral("New pane to the right"));
        button(row, QStringLiteral("⬓+"), QStringLiteral("pane.splitDown"), QStringLiteral("New pane below"));
        button(row, QStringLiteral("⇱"), QStringLiteral("pane.moveToNewTab"), QStringLiteral("Move to new tab"));
        button(row, QStringLiteral("×"), QStringLiteral("pane.close"), QStringLiteral("Close pane"));
        hide();
    }

    void place() {
        const auto *leaf = parentWidget();
        adjustSize();
        move(leaf->width() - width() - 6, 4);
        raise();
    }

    void refreshTooltips() {
        for (auto *b : findChildren<QToolButton *>()) {
            const QString keys = Keymap::instance().shortcutText(b->property("action").toString());
            b->setToolTip(b->property("label").toString() + (keys.isEmpty() ? QString() : QStringLiteral("  (") + keys + ')'));
        }
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object != m_grip) return QFrame::eventFilter(object, event);
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton) break;
            m_pressAt = mouse->globalPos(); m_pressed = true; m_dragging = false;
            return true;
        }
        case QEvent::MouseMove: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (!m_pressed) break;
            if (!m_dragging && (mouse->globalPos() - m_pressAt).manhattanLength() >= QApplication::startDragDistance()) {
                m_dragging = true;
                QApplication::setOverrideCursor(Qt::ClosedHandCursor);
            }
            if (m_dragging && onDragMove) onDragMove(mouse->globalPos());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (!m_pressed) break;
            m_pressed = false;
            if (m_dragging) {
                m_dragging = false;
                QApplication::restoreOverrideCursor();
                if (onDragEnd) onDragEnd(mouse->globalPos(), true);
            }
            return true;
        }
        case QEvent::KeyPress:
            if (m_dragging && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                m_pressed = m_dragging = false;
                QApplication::restoreOverrideCursor();
                if (onDragEnd) onDragEnd(QCursor::pos(), false);
                return true;
            }
            break;
        default: break;
        }
        return QFrame::eventFilter(object, event);
    }

private:
    void button(QHBoxLayout *row, const QString &glyph, const QString &action, const QString &label) {
        auto *b = new QToolButton;
        b->setObjectName(QStringLiteral("paneChromeButton"));
        b->setText(glyph); b->setAutoRaise(true); b->setFocusPolicy(Qt::NoFocus);
        b->setProperty("action", action); b->setProperty("label", label);
        connect(b, &QToolButton::clicked, this, [this, action] { if (onAction) onAction(action); });
        row->addWidget(b);
    }

    QLabel *m_grip = nullptr;
    QPoint m_pressAt;
    bool m_pressed = false, m_dragging = false;
};

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
        // `relay open PATH` in Relay shells and Konsole's file-link editor command both reach
        // this process through a private local socket. The directory is created mode 0700.
        if (m_socketDir.isValid()) {
            QFile::setPermissions(m_socketDir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
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
        setWindowTitle(QStringLiteral("Relay"));
        setMinimumSize(760, 520);
        const QRect available = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 860);
        resize(std::min(1320, available.width() * 9 / 10), std::min(860, available.height() * 9 / 10));
        // No toolbar: the tab bar starts at the top. Its actions live in the palette (Ctrl+Shift+A).
        Keymap::instance().listen(this, [this] { syncToolbar(); });
        m_tabs = new QTabWidget;
        m_tabs->setDocumentMode(true);
        m_tabs->setTabsClosable(true);
        m_tabs->setMovable(true);
        m_tabs->tabBar()->setExpanding(false);
        buildTabBarControls();
        auto *central = new QWidget;
        auto *row = new QHBoxLayout(central); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(0);
        row->addWidget(m_tabs, 1);
        setCentralWidget(central);
        // The palette floats over the right edge instead of resizing the terminal, so programs
        // such as vim do not redraw every time it opens.
        buildSidebar();
        m_sidebar->setParent(central);
        central->installEventFilter(this);
        Keymap::instance().listen(this, [this] {
            for (Pane *pane : allPanes()) pane->sendKeybindings();
            const auto conflicts = Keymap::instance().conflicts();
            statusBar()->showMessage(conflicts.isEmpty() ? QStringLiteral("Keyboard shortcuts reloaded.")
                                                         : QStringLiteral("Keyboard shortcuts: ") + conflicts.join(QStringLiteral("; ")));
            if (m_sidebar->isVisible()) renderPalette();
        });
        connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
            QWidget *page = m_tabs->currentWidget();
            if (!page) return;
            QWidget *leaf = m_lastActive.value(page);
            if (!leaf) { const auto leaves = leavesIn(page); leaf = leaves.isEmpty() ? nullptr : leaves.first(); }
            if (leaf) { setActiveLeaf(leaf); focusLeaf(leaf); }
        });
        connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
            if (m_tabs->count() > 1) closeTab(index, true); else closeWindowWithWarning();
            hint(QStringLiteral("tab.close.mouse"), relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("pane.close")), QStringLiteral("close pane, then tab")));
        });
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

    QWidget *activeLeaf() const { return m_activeLeaf; }

    // Open a folder in an explorer pane or a file in a preview pane, next to `anchor`. An existing
    // explorer or preview in the same tab is reused, the way editors reuse a preview tab.
    void openPath(const QString &path, int line, QWidget *anchor) {
        const QFileInfo info(path);
        if (!info.exists()) { statusBar()->showMessage(QStringLiteral("No such file or folder: ") + path); return; }
        if (!anchor || !isLeaf(anchor) || anchor->window() != this) anchor = m_activeLeaf;
        if (!anchor) return;
        QWidget *page = pageOf(anchor);
        m_tabs->setCurrentWidget(page);
        const auto kind = info.isDir() ? ToolPane::Kind::Explorer : ToolPane::Kind::Preview;
        ToolPane *target = nullptr;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->kind() == kind) target = tool;
        if (target) {
            if (kind == ToolPane::Kind::Explorer) target->explorer()->setRoot(info.absoluteFilePath());
            else target->preview()->open(info.absoluteFilePath());
        } else {
            // A preview opens beside an explorer when there is one, otherwise beside the anchor.
            if (kind == ToolPane::Kind::Preview)
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
        if (event->type() == QEvent::Resize && object == centralWidget()) placeSidebar();
        if (event->type() == QEvent::Resize && isLeaf(qobject_cast<QWidget *>(object)))
            if (auto *chrome = chromeOf(static_cast<QWidget *>(object))) chrome->place();
        if (object == m_tabs->tabBar() && (event->type() == QEvent::Resize || event->type() == QEvent::MouseMove || event->type() == QEvent::Leave
                                           || event->type() == QEvent::Enter || event->type() == QEvent::LayoutRequest))
            QTimer::singleShot(0, this, [this] { placeTabBarControls(); });
        if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove) {
            if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->window() == this) {
                QWidget *leaf = leafOf(widget);
                if (leaf || event->type() == QEvent::Enter) showChromeFor(leaf);
            }
        } else if (event->type() == QEvent::Leave && object == this) showChromeFor(nullptr);
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
            return QMainWindow::eventFilter(object, event);
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget || widget->window() != this) return QMainWindow::eventFilter(object, event);
        auto *key = static_cast<QKeyEvent *>(event);
        if (widget == m_filter && event->type() == QEvent::KeyPress && paletteKey(key)) return true;
        if (widget == m_filter && event->type() == QEvent::ShortcutOverride) {
            const int k = key->key();
            if (k == Qt::Key_Escape || k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Up || k == Qt::Key_Down
                || k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_PageUp || k == Qt::Key_PageDown
                || ((key->modifiers() & Qt::ControlModifier) && (k == Qt::Key_N || k == Qt::Key_P))) { event->accept(); return true; }
        }
        // Palette keys stay inside the palette.
        if (m_sidebar->isVisible() && (widget == m_filter || widget == m_list)) {
            // Only the palette key itself (to close) acts while the palette has focus.
            if (Keymap::instance().match(key) != QStringLiteral("palette.open")) return QMainWindow::eventFilter(object, event);
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
        // Accept the override so neither the composer nor Konsole consumes the key,
        // then act on the key press itself. Auto-repeat does not open a burst of tabs.
        event->accept();
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) QTimer::singleShot(0, this, [this, id] { runAction(id); });
        return true;
    }

    // Saved window layout: geometry changes are saved like any other layout change (debounced).
    void moveEvent(QMoveEvent *event) override {
        QMainWindow::moveEvent(event);
        m_manager->scheduleSave();
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
    // ----- toolbar ----------------------------------------------------------------------------
    // Kept for the palette-driven action list; nothing is shown in a toolbar any more.
    void buildToolbar() {
        auto *toolbar = addToolBar(QStringLiteral("Relay"));
        toolbar->setMovable(false);
        auto addAction = [this, toolbar](const QString &label, const QString &id) {
            auto *action = toolbar->addAction(label);
            connect(action, &QAction::triggered, this, [this, id, label] {
                runAction(id);
                hint(QStringLiteral("toolbar.") + id, relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(id), label.toLower()));
            });
            m_toolbarActions.append({action, id});
        };
        addAction(QStringLiteral("Actions"), QStringLiteral("palette.open"));
        toolbar->addSeparator();
        addAction(QStringLiteral("New chat"), QStringLiteral("agent.newChat"));
        addAction(QStringLiteral("Stop agent"), QStringLiteral("agent.stop"));
        auto *spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toolbar->addWidget(spacer);
        addAction(QStringLiteral("Provider / BYOK…"), QStringLiteral("agent.provider"));
        syncToolbar();
        Keymap::instance().listen(this, [this] { syncToolbar(); });
    }

    void syncToolbar() {
        for (const auto &entry : std::as_const(m_toolbarActions)) {
            const QString shortcut = Keymap::instance().shortcutText(entry.second);
            entry.first->setToolTip(shortcut.isEmpty() ? entry.first->text() : entry.first->text() + QStringLiteral("  (") + shortcut + ')');
        }
    }

    // Ctrl+? (or F1): every action and its keys, so the window itself needs no shortcut bar.
    void showShortcutsOverlay() {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Keyboard shortcuts"));
        dialog.resize(720, std::min(760, height() - 80));
        auto *layout = new QVBoxLayout(&dialog);
        auto *filter = new QLineEdit; filter->setPlaceholderText(QStringLiteral("Search shortcuts…"));
        layout->addWidget(filter);
        auto *tree = new QTreeWidget;
        tree->setColumnCount(2);
        tree->setHeaderLabels({QStringLiteral("Action"), QStringLiteral("Keys")});
        tree->setRootIsDecorated(false);
        tree->setAlternatingRowColors(true);
        layout->addWidget(tree, 1);
        auto *note = new QLabel(QStringLiteral("Unbound actions run from the palette (%1). Edit shortcuts: Actions › Edit keyboard shortcuts.")
                                    .arg(Keymap::instance().shortcutText(QStringLiteral("palette.open"))));
        note->setWordWrap(true); note->setObjectName(QStringLiteral("transcriptHeader"));
        layout->addWidget(note);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        auto fill = [tree](const QString &query) {
            tree->clear();
            QString section;
            QTreeWidgetItem *group = nullptr;
            for (const ActionDef &action : Keymap::instance().actions()) {
                const QString keys = Keymap::instance().shortcutText(action.id);
                if (!query.isEmpty() && !action.description.contains(query, Qt::CaseInsensitive)
                    && !keys.contains(query, Qt::CaseInsensitive) && !action.id.contains(query, Qt::CaseInsensitive))
                    continue;
                if (action.category != section) {
                    section = action.category;
                    group = new QTreeWidgetItem(tree, {section.left(1).toUpper() + section.mid(1), QString()});
                    QFont bold = group->font(0); bold.setBold(true); group->setFont(0, bold);
                    group->setFirstColumnSpanned(true);
                }
                auto *row = new QTreeWidgetItem(tree, {action.description, keys.isEmpty() ? QStringLiteral("—") : keys});
                row->setToolTip(0, action.id);
            }
            tree->resizeColumnToContents(0);
        };
        fill(QString());
        connect(filter, &QLineEdit::textChanged, tree, [fill](const QString &text) { fill(text); });
        filter->setFocus();
        dialog.exec();
    }

    void hint(const QString &id, const QString &text) {
        if (text.isEmpty() || !relay::ShortcutHints::instance().shouldShow(id)) return;
        if (m_active) m_active->toast(text, 5000); else statusBar()->showMessage(text, 6000);
    }

    // Saved window layout: forget it and stop saving for the rest of this session, so the next
    // start opens one new window. The windows on screen are left alone.
    void startFreshWindowSet() {
        m_manager->forgetSavedLayout(true);
        statusBar()->showMessage(QStringLiteral("Saved window layout cleared. This session is no longer saved; the next start opens one fresh window."), 9000);
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
        else if (id == QStringLiteral("pane.splitRight")) split(Qt::Horizontal);
        else if (id == QStringLiteral("pane.splitDown")) split(Qt::Vertical);
        else if (id == QStringLiteral("pane.focusLeft")) navigate(Qt::Key_Left);
        else if (id == QStringLiteral("pane.focusRight")) navigate(Qt::Key_Right);
        else if (id == QStringLiteral("pane.focusUp")) navigate(Qt::Key_Up);
        else if (id == QStringLiteral("pane.focusDown")) navigate(Qt::Key_Down);
        else if (id == QStringLiteral("pane.close")) closeActive();
        else if (id == QStringLiteral("pane.moveLeft")) moveActive(Qt::Key_Left);
        else if (id == QStringLiteral("pane.moveRight")) moveActive(Qt::Key_Right);
        else if (id == QStringLiteral("pane.moveUp")) moveActive(Qt::Key_Up);
        else if (id == QStringLiteral("pane.moveDown")) moveActive(Qt::Key_Down);
        else if (id == QStringLiteral("pane.moveToNewTab")) { if (m_activeLeaf) moveLeafToNewTab(m_activeLeaf); }
        else if (id == QStringLiteral("tab.moveToNewWindow")) moveTabToNewWindow(m_tabs->currentIndex());
        else if (id == QStringLiteral("closed.restore")) m_manager->restore(this);
        else if (id == QStringLiteral("files.explorer")) openPath(activeCwd(), 0, m_activeLeaf);
        else if (id == QStringLiteral("files.open")) {
            const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Open file in a preview pane"), activeCwd());
            if (!file.isEmpty()) openPath(file, 0, m_activeLeaf);
        }
        else if (id == QStringLiteral("palette.open")) togglePalette();
        else if (id == QStringLiteral("keybindings.reload")) Keymap::instance().reload();
        else if (id == QStringLiteral("help.shortcuts")) showShortcutsOverlay();
        else if (id == QStringLiteral("keybindings.edit")) {
            Keymap::instance().ensureFile();
            const QString editor = qEnvironmentVariable("VISUAL", qEnvironmentVariable("EDITOR", QStringLiteral("nano")));
            const QString quoted = QStringLiteral("'") + QString(Keymap::instance().path()).replace('\'', QStringLiteral("'\\''")) + '\'';
            if (!pane || !pane->runCommand(editor + ' ' + quoted))
                statusBar()->showMessage(QStringLiteral("Shortcuts file: ") + Keymap::instance().path());
        }
        else if (!pane) return;
        else if (id == QStringLiteral("terminal.native")) pane->toggleNative();
        else if (id == QStringLiteral("pane.restartShell")) pane->restartStopped();
        else if (id == QStringLiteral("control.human")) pane->takeControl();
        else if (id == QStringLiteral("control.prompt")) pane->showPrompt();
        else if (id == QStringLiteral("input.toggle")) pane->toggleInputMode();
        else if (id == QStringLiteral("agent.fastAgent")) pane->toggleFastAgent();   // model roles
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
        else if (id == QStringLiteral("agent.continue")) pane->continueTurn(Keymap::instance().shortcutText(id).isEmpty());
        else if (id == QStringLiteral("agent.instructions")) pane->openInstructions();
        else if (id == QStringLiteral("agent.export")) pane->exportConversation();
        else if (id == QStringLiteral("agent.interrupt")) pane->interruptAgentWithPrompt();
        else if (id == QStringLiteral("agent.clearQueue")) pane->clearAgentQueue();
        else if (id == QStringLiteral("agent.resumeQueue")) pane->resumeAgentQueue();
        else if (id == QStringLiteral("terminal.interrupt")) pane->interruptShell();
        else if (id == QStringLiteral("agent.newChat")) pane->newChat();
        else if (id == QStringLiteral("agent.stop")) pane->stopAgent();
        else if (id == QStringLiteral("agent.stopAllSubagents")) pane->stopAllSubagents();   // subagents UI
        else if (id == QStringLiteral("agent.agentsMenu")) openAgentsMenu();
        else if (id == QStringLiteral("agent.provider")) pane->openProviderDialog();
        else if (id == QStringLiteral("input.modeAuto")) pane->setMode(QStringLiteral("auto"));
        else if (id == QStringLiteral("input.modeTerminal")) pane->setMode(QStringLiteral("shell"));
        else if (id == QStringLiteral("input.modeAgent")) pane->setMode(QStringLiteral("agent"));
    }

    // ----- palette ----------------------------------------------------------------------------
    // One Relay actions palette (Ctrl+Shift+A). Sections: Recent, then Agent / Terminal ordered by
    // where focus was, then Panes and tabs, then Shortcuts. Typing searches everything, including
    // submenu entries ("deep" finds Model › DeepSeek). Right or Enter opens a submenu; Left or
    // Backspace on an empty filter goes back; Esc clears the filter, goes back, then closes.
    struct PaletteItem {
        QString key, section, label, detail, shortcut, aliases;
        bool checked = false, stayOpen = false;
        std::function<void()> run;
        std::function<QList<PaletteItem>()> children;
    };

    void buildSidebar() {
        m_sidebar = new QWidget;
        m_sidebar->setObjectName(QStringLiteral("sidebar"));
        m_sidebar->setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(m_sidebar); layout->setContentsMargins(12, 12, 12, 12); layout->setSpacing(8);
        m_paletteTitle = new QLabel; m_paletteTitle->setObjectName(QStringLiteral("paletteTitle"));
        layout->addWidget(m_paletteTitle);
        m_filter = new QLineEdit; m_filter->setPlaceholderText(QStringLiteral("Search actions · ↑↓ select · → open · Enter run · Esc back"));
        layout->addWidget(m_filter);
        m_list = new QTreeWidget; m_list->setObjectName(QStringLiteral("paletteList"));
        m_list->setColumnCount(2); m_list->setHeaderHidden(true); m_list->setRootIsDecorated(false);
        m_list->setIndentation(0); m_list->setUniformRowHeights(true); m_list->setFocusPolicy(Qt::NoFocus);
        m_list->header()->setStretchLastSection(false);
        m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        layout->addWidget(m_list, 1);
        m_sidebar->hide();
        connect(m_filter, &QLineEdit::textChanged, this, [this] { renderPalette(); });
        connect(m_list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *row) { if (row && row->data(0, Qt::UserRole).toInt() >= 0) { m_list->setCurrentItem(row); activateSelected(false); } });
        m_filter->installEventFilter(this);
    }

    void togglePalette() {
        if (m_sidebar->isVisible()) { closePalette(); return; }
        m_returnPane = m_active;
        m_returnFocus = QApplication::focusWidget();
        m_terminalContext = m_active && m_active->ownsTerminalWidget(m_returnFocus);
        m_stack.clear();
        m_filter->clear();
        renderPalette();
        placeSidebar();
        m_sidebar->show();
        m_sidebar->raise();
        m_filter->setFocus(Qt::ShortcutFocusReason);
    }

    void placeSidebar() {
        if (!m_sidebar || !centralWidget()) return;
        const QRect area = centralWidget()->rect();
        const int width = std::min(420, area.width() - 40);
        m_sidebar->setGeometry(area.right() - width - 8, area.top() + 36, width, std::max(200, area.height() - 48));
    }

    void closePalette() {
        m_sidebar->hide();
        m_stack.clear();
        // Return focus exactly where it was, e.g. to vim in the terminal, not to the composer.
        if (m_returnFocus && m_returnFocus->window() == this && m_returnFocus != m_filter) m_returnFocus->setFocus(Qt::OtherFocusReason);
        else if (Pane *pane = m_returnPane ? m_returnPane.data() : m_active.data()) pane->focusInput();
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

    // Agent options (Actions › Agent options). Settings live in QSettings; most apply to the next
    // conversation (New chat), except max automatic turns, which applies at once.
    QList<PaletteItem> agentOptionItems() {
        QList<PaletteItem> children;
        const QString section = QStringLiteral("Agent options");
        auto toggle = [&](const QString &key, const QString &label, const QString &detail, bool fallback) {
            PaletteItem item;
            const bool on = QSettings().value(key, fallback).toBool();
            item.key = QStringLiteral("option:") + key; item.section = section; item.label = label;
            item.detail = (on ? QStringLiteral("On · ") : QStringLiteral("Off · ")) + detail;
            item.checked = on; item.stayOpen = true;
            item.run = [key, on] { QSettings().setValue(key, !on); };
            children << item;
        };
        PaletteItem instructions = actionItem(section, QStringLiteral("Instructions…"), QStringLiteral("CLAUDE.md, AGENTS.md, WARP.md and other instruction files"), QStringLiteral("agent.instructions"));
        children << instructions;
        auto askText = [this](const QString &key, const QString &title, const QString &label, const QString &fallback, std::function<void(const QString &)> apply) {
            bool ok = false;
            const QString value = QInputDialog::getText(this, title, label, QLineEdit::Normal, QSettings().value(key, fallback).toString(), &ok);
            if (ok) { apply(value.trimmed()); if (m_active) m_active->agentOptionsChanged(key); }
        };
        {
            PaletteItem item; item.key = QStringLiteral("option:plans_dir"); item.section = section;
            const QString dir = QSettings().value(QStringLiteral("agent/plans_dir")).toString();
            item.label = QStringLiteral("Plans folder…");
            item.detail = dir.isEmpty() ? QStringLiteral("<project>/.relay/plans") : dir;
            item.run = [askText] { askText(QStringLiteral("agent/plans_dir"), QStringLiteral("Plans folder"),
                QStringLiteral("Absolute folder for plans (empty: <project>/.relay/plans)"), QString(),
                [](const QString &value) { QSettings().setValue(QStringLiteral("agent/plans_dir"), value); }); };
            children << item;
        }
        {
            PaletteItem item; item.key = QStringLiteral("option:compact_threshold"); item.section = section;
            const QString value = QSettings().value(QStringLiteral("agent/compact_threshold")).toString();
            item.label = QStringLiteral("Compaction threshold…");
            item.detail = value.isEmpty() ? QStringLiteral("Default: 80% of the model window, minus output room") : value + QStringLiteral(" of the window");
            item.run = [askText] { askText(QStringLiteral("agent/compact_threshold"), QStringLiteral("Compaction threshold"),
                QStringLiteral("Fraction of the model's context window, 0.50–0.98 (empty: default)"), QString(),
                [](const QString &value) {
                    bool ok = false; const double number = value.toDouble(&ok);
                    if (value.isEmpty()) QSettings().remove(QStringLiteral("agent/compact_threshold"));
                    else if (ok && number >= 0.5 && number <= 0.98) QSettings().setValue(QStringLiteral("agent/compact_threshold"), number);
                }); };
            children << item;
        }
        {
            PaletteItem item; item.key = QStringLiteral("option:max_auto_turns"); item.section = section;
            item.label = QStringLiteral("Automatic turns from background agents…");
            item.detail = QStringLiteral("Up to %1 in a row without your input (0 = unlimited)").arg(QSettings().value(QStringLiteral("agent/max_auto_turns"), 50).toInt());
            item.run = [this] {
                bool ok = false;
                const int value = QInputDialog::getInt(this, QStringLiteral("Automatic turns"), QStringLiteral("Consecutive automatic turns (0 = unlimited)"),
                                                       QSettings().value(QStringLiteral("agent/max_auto_turns"), 50).toInt(), 0, 10000, 1, &ok);
                if (!ok) return;
                QSettings().setValue(QStringLiteral("agent/max_auto_turns"), value);
                if (m_active) m_active->agentOptionsChanged(QStringLiteral("agent/max_auto_turns"));
            };
            children << item;
        }
        {
            // Turn limits (protocol 12.1): apply to the running agent at once.
            auto askInt = [this, section](const QString &key, const QString &label, const QString &detail, const QString &prompt, int fallback, int min, int max) {
                PaletteItem item; item.key = QStringLiteral("option:") + key; item.section = section; item.label = label;
                item.detail = detail.arg(QSettings().value(key, fallback).toInt());
                item.run = [this, key, label, prompt, fallback, min, max] {
                    bool ok = false;
                    const int value = QInputDialog::getInt(this, label.chopped(1), prompt, QSettings().value(key, fallback).toInt(), min, max, 1, &ok);
                    if (!ok) return;
                    QSettings().setValue(key, value);
                    if (m_active) m_active->agentOptionsChanged(key);
                };
                return item;
            };
            children << askInt(QStringLiteral("agent/max_steps"), QStringLiteral("Step limit per turn…"),
                               QStringLiteral("%1 model calls, then the turn stops with Continue (default 50)"), QStringLiteral("Model calls per turn (1–500)"), 50, 1, 500);
            children << askInt(QStringLiteral("agent/max_tool_calls"), QStringLiteral("Tool-call limit per turn…"),
                               QStringLiteral("%1 tool calls (default 150)"), QStringLiteral("Tool calls per turn (1–2000)"), 150, 1, 2000);
            PaletteItem audit; audit.key = QStringLiteral("option:agent/audit_requests"); audit.section = section;
            const bool on = QSettings().value(QStringLiteral("agent/audit_requests"), false).toBool();
            audit.label = QStringLiteral("Audit requests after each turn");
            audit.detail = (on ? QStringLiteral("On · ") : QStringLiteral("Off · ")) + QStringLiteral("a small side call flags asks that may be unaddressed");
            audit.checked = on; audit.stayOpen = true;
            audit.run = [this, on] { QSettings().setValue(QStringLiteral("agent/audit_requests"), !on); if (m_active) m_active->agentOptionsChanged(QStringLiteral("agent/audit_requests")); };
            children << audit;
        }
        {
            PaletteItem manage; manage.key = QStringLiteral("option:skills"); manage.section = section;
            manage.label = QStringLiteral("Skills…"); manage.detail = QStringLiteral("list, exclude, refine, import from a repository · /skills");
            manage.run = [pane = m_active] { if (pane) pane->openSkills(); };
            children << manage;
            PaletteItem item; item.key = QStringLiteral("option:skills_exclude"); item.section = section;
            const QStringList list = QSettings().value(QStringLiteral("skills/exclude")).toStringList();
            item.label = QStringLiteral("Excluded skills…");
            item.detail = list.isEmpty() ? QStringLiteral("Default: Warp-app skills") : list.join(QStringLiteral(", "));
            item.run = [askText] { askText(QStringLiteral("skills/exclude_text"), QStringLiteral("Excluded skills"),
                QStringLiteral("Comma-separated skill names to skip (replaces the default list; empty: default)"), QString(),
                [](const QString &value) {
                    QStringList names;
                    for (const QString &name : value.split(',', Qt::SkipEmptyParts)) if (!name.trimmed().isEmpty()) names << name.trimmed();
                    QSettings().setValue(QStringLiteral("skills/exclude_text"), value);
                    if (names.isEmpty()) QSettings().remove(QStringLiteral("skills/exclude")); else QSettings().setValue(QStringLiteral("skills/exclude"), names);
                }); };
            children << item;
        }
        {
            const QString effort = QSettings().value(QStringLiteral("agent/effort"), QStringLiteral("high")).toString();
            PaletteItem item; item.key = QStringLiteral("option:effort_default"); item.section = section;
            item.label = QStringLiteral("Default reasoning effort"); item.detail = effort + QStringLiteral(" · new panes and new chats");
            item.children = [effort] {
                QList<PaletteItem> levels;
                for (const QString &level : Pane::efforts()) {
                    PaletteItem child; child.key = QStringLiteral("option:effort_default:") + level; child.section = QStringLiteral("Default reasoning effort");
                    child.label = level; child.checked = effort == level; child.stayOpen = true;
                    child.run = [level] { QSettings().setValue(QStringLiteral("agent/effort"), level); };
                    levels << child;
                }
                return levels;
            };
            children << item;
        }
        children << rolesMenu(section);
        {
            // "Panes default to the fast agent" (owner decision 2026-09-17): applies to panes opened
            // from now on; the first pane of a window stays on the main agent.
            PaletteItem item; item.key = QStringLiteral("option:agent/panes_fast"); item.section = section;
            const bool on = Pane::newPanesUseFastAgent();
            item.label = QStringLiteral("New panes use the fast agent");
            item.detail = (on ? QStringLiteral("On · ") : QStringLiteral("Off · ")) + QStringLiteral("the first pane keeps the main agent");
            item.checked = on; item.stayOpen = true;
            item.run = [on] { QSettings().setValue(QStringLiteral("agent/panes_fast"), !on); };
            children << item;
        }
        toggle(QStringLiteral("agent/show_thinking"), QStringLiteral("Show thinking"), QStringLiteral("stream reasoning above the prompt; a one-line summary always prints"), true);
        toggle(QStringLiteral("hints/enabled"), QStringLiteral("Shortcut hints"), QStringLiteral("tips when a faster key exists"), true);
        toggle(QStringLiteral("suggestions/next_command"), QStringLiteral("AI next-command suggestions"), QStringLiteral("after a command finishes; uses your API key"), false);
        toggle(QStringLiteral("suggestions/next_prompt"), QStringLiteral("Suggested next prompts"), QStringLiteral("after an agent turn; uses your API key"), false);
        toggle(QStringLiteral("recap/away"), QStringLiteral("Recap when you come back"), QStringLiteral("after 3+ minutes away while the agent worked"), true);
        return children;
    }

    // Agent options › Model roles (protocol 13). Each role is "same as main agent" until a preset is
    // picked here; the worker resolves keys and falls back to the main agent when one is missing.
    PaletteItem rolesMenu(const QString &section) {
        return submenu(QStringLiteral("menu:roles"), section, QStringLiteral("Model roles"),
                       QStringLiteral("one model per job · unset roles follow the main agent"), [this] {
            QList<PaletteItem> roles;
            for (const QString &role : Pane::roleIds()) {
                const QString preset = QSettings().value(Pane::roleSetting(role, QStringLiteral("preset"))).toString();
                const QString model = QSettings().value(Pane::roleSetting(role, QStringLiteral("model"))).toString();
                const QString effort = QSettings().value(Pane::roleSetting(role, QStringLiteral("effort"))).toString();
                QString detail = preset.isEmpty() ? QStringLiteral("Same as main agent") : presetLabel(preset);
                if (!preset.isEmpty() && !model.isEmpty()) detail += QStringLiteral(" · ") + model;
                if (!preset.isEmpty() && !effort.isEmpty()) detail += QStringLiteral(" · ") + effort;
                const QString effective = m_active ? m_active->roleModel(role) : QString();
                if (preset.isEmpty() && !effective.isEmpty()) detail += QStringLiteral(" (") + effective + ')';
                roles << submenu(QStringLiteral("role:") + role, QStringLiteral("Model roles"),
                                 Pane::roleLabel(role), detail, [this, role] { return roleItems(role); });
            }
            return roles;
        });
    }

    QString presetLabel(const QString &id) const {
        if (m_active) for (const auto &model : m_active->storedModels()) if (model.first == id) return model.second;
        return id;
    }

    QList<PaletteItem> roleItems(const QString &role) {
        QList<PaletteItem> items;
        const QString section = Pane::roleLabel(role);
        QSettings settings;
        const QString preset = settings.value(Pane::roleSetting(role, QStringLiteral("preset"))).toString();
        auto apply = [this, role](const QString &field, const QVariant &value) {
            if (value.toString().isEmpty()) QSettings().remove(Pane::roleSetting(role, field));
            else QSettings().setValue(Pane::roleSetting(role, field), value);
            if (m_active) m_active->agentOptionsChanged(Pane::roleSetting(role, field));
        };
        PaletteItem same; same.key = QStringLiteral("role:") + role + QStringLiteral(":same"); same.section = section;
        same.label = QStringLiteral("Same as main agent"); same.detail = QStringLiteral("the default for every role");
        same.checked = preset.isEmpty(); same.stayOpen = true;
        same.run = [apply] { apply(QStringLiteral("preset"), QString()); apply(QStringLiteral("model"), QString()); };
        items << same;
        if (m_active) for (const auto &model : m_active->storedModels()) {
            const QString id = model.first;
            PaletteItem item; item.key = QStringLiteral("role:") + role + ':' + id; item.section = section;
            item.label = model.second; item.detail = QStringLiteral("use this provider for this role");
            item.checked = preset == id; item.stayOpen = true;
            item.run = [apply, id] { apply(QStringLiteral("preset"), id); };
            items << item;
        }
        {
            PaletteItem item; item.key = QStringLiteral("role:") + role + QStringLiteral(":model"); item.section = section;
            item.label = QStringLiteral("Model id…");
            item.detail = settings.value(Pane::roleSetting(role, QStringLiteral("model"))).toString().isEmpty()
                ? QStringLiteral("the preset's own model (empty)")
                : settings.value(Pane::roleSetting(role, QStringLiteral("model"))).toString();
            item.run = [this, role, apply] {
                bool ok = false;
                const QString value = QInputDialog::getText(this, Pane::roleLabel(role) + QStringLiteral(" · model"),
                    QStringLiteral("Model id on the chosen provider (empty: the preset's model)"), QLineEdit::Normal,
                    QSettings().value(Pane::roleSetting(role, QStringLiteral("model"))).toString(), &ok);
                if (ok) apply(QStringLiteral("model"), value.trimmed());
            };
            items << item;
        }
        {
            const QString effort = settings.value(Pane::roleSetting(role, QStringLiteral("effort"))).toString();
            PaletteItem item; item.key = QStringLiteral("role:") + role + QStringLiteral(":effort"); item.section = section;
            item.label = QStringLiteral("Reasoning effort");
            item.detail = effort.isEmpty() ? QStringLiteral("the provider's default for this model") : effort;
            item.children = [role, effort, apply] {
                QList<PaletteItem> levels;
                const QString section = Pane::roleLabel(role) + QStringLiteral(" · effort");
                PaletteItem none; none.key = QStringLiteral("role:") + role + QStringLiteral(":effort:default");
                none.section = section; none.label = QStringLiteral("Provider default"); none.checked = effort.isEmpty();
                none.stayOpen = true; none.run = [apply] { apply(QStringLiteral("effort"), QString()); };
                levels << none;
                for (const QString &level : Pane::efforts()) {
                    PaletteItem child; child.key = QStringLiteral("role:") + role + QStringLiteral(":effort:") + level;
                    child.section = section; child.label = level; child.checked = effort == level; child.stayOpen = true;
                    child.run = [apply, level] { apply(QStringLiteral("effort"), level); };
                    levels << child;
                }
                return levels;
            };
            items << item;
        }
        return items;
    }

    QList<PaletteItem> rootItems() {
        QList<PaletteItem> items;
        Pane *pane = m_active;
        const QString agent = QStringLiteral("Agent"), terminal = QStringLiteral("Terminal"), panes = QStringLiteral("Panes and tabs"), keys = QStringLiteral("Shortcuts");
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
            // Model roles (protocol 13): flip this pane between the main agent and the fast agent.
            const bool fast = pane->agentRole() == QStringLiteral("fast");
            const QString fastModel = pane->roleModel(QStringLiteral("fast"));
            items << actionItem(agent, QStringLiteral("Fast agent for this pane"),
                                (fast ? QStringLiteral("On · ") : QStringLiteral("Off · "))
                                    + (fastModel.isEmpty() ? QStringLiteral("a quick model for this pane; the conversation is kept") : fastModel),
                                QStringLiteral("agent.fastAgent"), fast);
        }
        const QString mode = pane ? pane->mode() : QStringLiteral("auto");
        const QString modeName = mode == QStringLiteral("shell") ? QStringLiteral("Terminal") : mode == QStringLiteral("agent") ? QStringLiteral("Agent") : QStringLiteral("Auto detect");
        items << submenu(QStringLiteral("menu:mode"), agent, QStringLiteral("Input mode"), modeName, [this, mode] {
            return QList<PaletteItem>{
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Auto detect"), QStringLiteral("Commands to the terminal, everything else to the agent"), QStringLiteral("input.modeAuto"), mode == QStringLiteral("auto")),
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Terminal"), QStringLiteral("Always the terminal; the agent fixes failures"), QStringLiteral("input.modeTerminal"), mode == QStringLiteral("shell")),
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Agent"), QStringLiteral("Always the agent"), QStringLiteral("input.modeAgent"), mode == QStringLiteral("agent"))};
        });
        items << actionItem(agent, QStringLiteral("Toggle terminal / agent input"), QStringLiteral("From the prompt box"), QStringLiteral("input.toggle"));
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
                            pane && !pane->tasksProgress().isEmpty() ? pane->tasksProgress() + QStringLiteral(" · mark done, cancel, re-ask · /tasks")
                                                                     : QStringLiteral("Task list: everything you asked and its progress · /tasks"), QStringLiteral("agent.requests"));
        items << actionItem(agent, QStringLiteral("Continue agent turn"),
                            pane && pane->limitReached() ? QStringLiteral("The last turn stopped at its step limit · /continue")
                                                         : QStringLiteral("Send “Continue” to the agent · /continue"), QStringLiteral("agent.continue"));
        items << actionItem(agent, QStringLiteral("Export conversation"), QStringLiteral("Save the conversation as Markdown"), QStringLiteral("agent.export"));
        items << submenu(QStringLiteral("menu:agentOptions"), agent, QStringLiteral("Agent options"), QStringLiteral("Instructions, plans, compaction, suggestions"), [this] {
            return agentOptionItems();
        });
        items << actionItem(agent, QStringLiteral("New chat"), QStringLiteral("Start a new conversation in this pane"), QStringLiteral("agent.newChat"));
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
        items << actionItem(agent, QStringLiteral("Provider and API keys…"), QStringLiteral("Base URL, model ID, key, request options"), QStringLiteral("agent.provider"));
        PaletteItem importKeys; importKeys.key = QStringLiteral("agent.importWarp"); importKeys.section = agent;
        importKeys.label = QStringLiteral("Import keys from Warp"); importKeys.detail = QStringLiteral("Copy Warp's custom-endpoint keys into the keyring");
        importKeys.run = [this] { if (m_active) m_active->importWarpKeys(); };
        items << importKeys;

        items << actionItem(QStringLiteral("palette"), QStringLiteral("Keyboard shortcuts…"), QStringLiteral("Every action and its keys"), QStringLiteral("help.shortcuts"));
        items << actionItem(terminal, QStringLiteral("Interrupt"), pane && pane->processBusy() ? QStringLiteral("Stop the running program · Esc in the prompt box") : QStringLiteral("Nothing is running"), QStringLiteral("terminal.interrupt"));
        items << actionItem(terminal, QStringLiteral("Take control"),
                            QStringLiteral("Hide the prompt box and type into the terminal · the only way keys reach it"),
                            QStringLiteral("control.human"), pane && pane->isNative());
        {
            PaletteItem suggestions;
            const bool on = QSettings().value(QStringLiteral("composer/history_suggestions"), true).toBool();
            suggestions.key = QStringLiteral("composer.historySuggestions"); suggestions.section = terminal;
            suggestions.label = QStringLiteral("Command suggestions from history");
            suggestions.detail = on ? QStringLiteral("On: → or Ctrl+F accepts, Alt+→ accepts a word") : QStringLiteral("Off");
            suggestions.checked = on; suggestions.stayOpen = true;
            suggestions.run = [on] { QSettings().setValue(QStringLiteral("composer/history_suggestions"), !on); };
            items << suggestions;
        }
        items << actionItem(terminal, QStringLiteral("Show the Relay prompt"), QStringLiteral("Back to the prompt box; it becomes the input again"), QStringLiteral("control.prompt"), pane && !pane->isNative());
        {
            const QString current = Pane::defaultControl();
            items << submenu(QStringLiteral("menu:control"), terminal, QStringLiteral("Control when a full-screen program starts"),
                             current == QStringLiteral("agent") ? QStringLiteral("The prompt box keeps the keyboard") : QStringLiteral("Relay takes control for you"), [current] {
                QList<PaletteItem> children;
                const QList<QStringList> options{{QStringLiteral("agent"), QStringLiteral("The prompt box keeps the keyboard"), QStringLiteral("A \"Take control\" button appears; Ctrl+H does the same")},
                                                 {QStringLiteral("human"), QStringLiteral("Relay takes control for you"), QStringLiteral("The old behaviour: the prompt box hides and keys go to the program")}};
                for (const auto &option : options) {
                    PaletteItem item;
                    const QString value = option[0];
                    item.key = QStringLiteral("control.default:") + value; item.section = QStringLiteral("Control when a full-screen program starts");
                    item.label = option[1]; item.detail = option[2]; item.checked = current == value; item.stayOpen = true;
                    item.run = [value] { QSettings().setValue(QStringLiteral("control/default"), value); };
                    children << item;
                }
                return children;
            });
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
        {
            PaletteItem copy;
            const bool on = QSettings().value(QStringLiteral("terminal/copy_on_select"), false).toBool();
            copy.key = QStringLiteral("terminal.copyOnSelect"); copy.section = terminal;
            copy.label = QStringLiteral("Copy on select"); copy.detail = on ? QStringLiteral("On: selecting terminal text copies it") : QStringLiteral("Off");
            copy.checked = on; copy.stayOpen = true;
            copy.run = [on] { QSettings().setValue(QStringLiteral("terminal/copy_on_select"), !on); };
            items << copy;
        }
        {
            // shell/relay-integration.bash: OSC 7 and OSC 133 marks, used by Relay's engine.
            PaletteItem shellIntegration;
            const bool on = QSettings().value(QStringLiteral("terminal/shell_integration"), false).toBool();
            shellIntegration.key = QStringLiteral("terminal.shellIntegration"); shellIntegration.section = terminal;
            shellIntegration.label = QStringLiteral("Shell integration (OSC 7/133)");
            shellIntegration.detail = on ? QStringLiteral("On for new panes: directory and prompt marks")
                                         : QStringLiteral("Off · new panes only");
            shellIntegration.aliases = QStringLiteral("osc7 osc133 prompt marks");
            shellIntegration.checked = on; shellIntegration.stayOpen = true;
            shellIntegration.run = [on] { QSettings().setValue(QStringLiteral("terminal/shell_integration"), !on); };
            items << shellIntegration;
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
                            statusBar()->showMessage(QStringLiteral("No prompt mark in that direction. Enable the shell integration (OSC 7/133)."));
                    };
                    items << jump;
                }
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
                    statusBar()->showMessage(matches > 0 ? QStringLiteral("%1 match(es) for \"%2\"").arg(matches).arg(text)
                                                         : QStringLiteral("No match for \"%1\"").arg(text));
                };
                items << find;
            }
        }

        items << actionItem(panes, QStringLiteral("Open folder in explorer"), QStringLiteral("This pane's directory"), QStringLiteral("files.explorer"));
        items << actionItem(panes, QStringLiteral("Open file…"), QStringLiteral("Preview a file in a pane"), QStringLiteral("files.open"));
        {
            // Per-pane terminal engine (docs/ENGINE.md). Both can run side by side in one window.
            const bool engineDefault = relay::defaultEngineKind() == relay::EngineKind::Relay;
            PaletteItem relayPane;
            relayPane.key = QStringLiteral("pane.splitRight.relay"); relayPane.section = panes;
            relayPane.label = QStringLiteral("New pane (Relay engine)");
            relayPane.detail = QStringLiteral("Splits right using Relay's own terminal engine%1")
                                   .arg(engineDefault ? QStringLiteral(" (the default here)") : QString());
            relayPane.aliases = QStringLiteral("engine vterm ghostty libvterm");
            relayPane.run = [this] { split(Qt::Horizontal, QStringLiteral("relay")); };
            items << relayPane;
            PaletteItem konsolePane;
            konsolePane.key = QStringLiteral("pane.splitRight.konsole"); konsolePane.section = panes;
            konsolePane.label = QStringLiteral("New pane (Konsole engine)");
            konsolePane.detail = QStringLiteral("Splits right using KonsolePart%1")
                                     .arg(engineDefault ? QString() : QStringLiteral(" (the default here)"));
            konsolePane.aliases = QStringLiteral("engine kpart konsolepart");
            konsolePane.run = [this] { split(Qt::Horizontal, QStringLiteral("konsole")); };
            items << konsolePane;
        }
        items << actionItem(panes, QStringLiteral("Split right"), QString(), QStringLiteral("pane.splitRight"));
        items << actionItem(panes, QStringLiteral("Split down"), QString(), QStringLiteral("pane.splitDown"));
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
        {
            // Saved window layout ("reopen where I left off"). The toggle only decides whether the
            // layout is kept; Ctrl+Shift+W (restore last closed) works either way.
            PaletteItem reopen; reopen.key = QStringLiteral("option:windows/restore"); reopen.section = panes;
            const bool on = WindowManager::restoreEnabled();
            reopen.label = QStringLiteral("Reopen windows on start");
            reopen.detail = (on ? QStringLiteral("On · ") : QStringLiteral("Off · "))
                + QStringLiteral("windows, tabs, panes, directories and conversations come back");
            reopen.aliases = QStringLiteral("session persist startup warp layout remember where you left off");
            reopen.checked = on; reopen.stayOpen = true;
            reopen.run = [this, on] {
                QSettings().setValue(QStringLiteral("windows/restore"), !on);
                if (on) m_manager->forgetSavedLayout(false);   // turned off: drop what was saved
                else m_manager->scheduleSave();
                statusBar()->showMessage(on ? QStringLiteral("Relay will open one new window on start.")
                                            : QStringLiteral("Relay will reopen this window set on start."), 6000);
            };
            items << reopen;
            items << actionItem(panes, QStringLiteral("Start a fresh window set"),
                                QStringLiteral("Forget the saved layout; the next start opens one new window"),
                                QStringLiteral("windows.fresh"));
        }
        items << actionItem(panes, QStringLiteral("Next tab"), QString(), QStringLiteral("tab.next"));
        items << actionItem(panes, QStringLiteral("Previous tab"), QString(), QStringLiteral("tab.previous"));

        {
            PaletteItem hints;
            const bool on = relay::ShortcutHints::instance().enabled();
            hints.key = QStringLiteral("option:shortcut_hints"); hints.section = keys;
            hints.label = QStringLiteral("Shortcut hints"); hints.detail = on ? QStringLiteral("On · tips when a faster key exists") : QStringLiteral("Off");
            hints.checked = on; hints.stayOpen = true;
            hints.run = [on] { relay::ShortcutHints::instance().setEnabled(!on); };
            items << hints;
            PaletteItem reset;
            reset.key = QStringLiteral("option:shortcut_hints_reset"); reset.section = keys;
            reset.label = QStringLiteral("Reset shortcut hints"); reset.detail = QStringLiteral("Show every tip again");
            reset.run = [this] { relay::ShortcutHints::instance().resetAll(); statusBar()->showMessage(QStringLiteral("Shortcut hints reset."), 4000); };
            items << reset;
        }
        const QString presetId = Keymap::instance().preset();
        QString presetName;
        for (const auto &preset : Keymap::presets()) if (preset.first == presetId) presetName = preset.second;
        items << submenu(QStringLiteral("menu:preset"), keys, QStringLiteral("Shortcut preset"), presetName, [this, presetId] {
            QList<PaletteItem> children;
            for (const auto &preset : Keymap::presets()) {
                PaletteItem item;
                const QString id = preset.first;
                item.key = QStringLiteral("preset:") + id; item.section = QStringLiteral("Shortcut preset"); item.label = preset.second;
                item.detail = Keymap::instance().hasOverrides() ? QStringLiteral("Your custom overrides stay on top") : QString();
                item.checked = id == presetId; item.stayOpen = true;
                item.run = [id] { Keymap::instance().setPreset(id); };
                children << item;
            }
            return children;
        });
        const QString programKeys = Keymap::instance().programKeys();
        items << submenu(QStringLiteral("menu:programKeys"), keys, QStringLiteral("Shortcuts inside programs"),
                         programKeys == QStringLiteral("all") ? QStringLiteral("All shortcuts act") : programKeys == QStringLiteral("none") ? QStringLiteral("Programs get every key") : QStringLiteral("Ctrl+Shift and F-keys only"),
                         [programKeys] {
            QList<PaletteItem> children;
            const QList<QStringList> options{{QStringLiteral("shift-only"), QStringLiteral("Ctrl+Shift and F-keys only"), QStringLiteral("Other keys reach vim, nano, less")},
                                             {QStringLiteral("all"), QStringLiteral("All shortcuts act"), QStringLiteral("Relay wins inside programs")},
                                             {QStringLiteral("none"), QStringLiteral("Programs get every key"), QStringLiteral("No shortcuts while a program runs")}};
            for (const auto &option : options) {
                PaletteItem item;
                const QString value = option[0];
                item.key = QStringLiteral("programKeys:") + value; item.section = QStringLiteral("Shortcuts inside programs");
                item.label = option[1]; item.detail = option[2]; item.checked = programKeys == value; item.stayOpen = true;
                item.run = [value] { Keymap::instance().setProgramKeys(value); };
                children << item;
            }
            return children;
        });
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

    static int fuzzyScore(const QString &needle, const QString &haystack) {
        if (needle.isEmpty()) return 1;
        const QString n = needle.toLower(), h = haystack.toLower();
        const int direct = h.indexOf(n);
        if (direct >= 0) return 10000 - direct - (direct > 0 && h.at(direct - 1).isLetterOrNumber() ? 500 : 0);
        int pos = 0, first = -1, last = -1;
        for (const QChar c : n) {
            if (c.isSpace()) continue;
            pos = h.indexOf(c, pos);
            if (pos < 0) return 0;
            if (first < 0) first = pos;
            last = pos++;
        }
        return std::max(1, 5000 - (last - first) * 10 - first);
    }

    void renderPalette() {
        const bool nested = !m_stack.isEmpty();
        const QString needle = m_filter->text().trimmed();
        m_rows.clear();
        QList<PaletteItem> source = nested ? m_stack.last().second : rootItems();
        m_paletteTitle->setText(nested ? QStringLiteral("ACTIONS  ›  ") + m_stack.last().first.toUpper() : QStringLiteral("ACTIONS"));
        if (!nested && !needle.isEmpty()) {
            // Search reaches into submenus: "deep" finds Model › DeepSeek directly.
            QList<PaletteItem> flat;
            for (const auto &item : std::as_const(source)) {
                flat << item;
                if (item.children) for (auto child : item.children()) { child.label = item.label + QStringLiteral(" › ") + child.label; flat << child; }
            }
            source = flat;
        }
        struct Scored { int score; int order; PaletteItem item; };
        QList<Scored> scored;
        for (int i = 0; i < source.size(); ++i) {
            const auto &item = source[i];
            const QString aliases = item.aliases + ' ' + paletteAliases(item);
            const int score = std::max({fuzzyScore(needle, item.label), fuzzyScore(needle, item.detail) / 3, fuzzyScore(needle, item.section) / 4,
                                        needle.size() >= 2 ? fuzzyScore(needle, aliases) / 2 : 0});
            if (score > 0) scored.append({score, i, item});
        }
        m_list->clear();
        auto addHeader = [this](const QString &text) {
            auto *row = new QTreeWidgetItem(m_list, {text.toUpper(), QString()});
            row->setData(0, Qt::UserRole, -1);
            row->setFlags(Qt::ItemIsEnabled);
            QFont font = row->font(0); font.setPointSizeF(font.pointSizeF() * 0.85); font.setBold(true); row->setFont(0, font);
            row->setForeground(0, QColor(relay::theme::TextMuted));
        };
        auto addRow = [this](const PaletteItem &item) {
            QString label = (item.checked ? QStringLiteral("✓ ") : QStringLiteral("   ")) + item.label;
            if (item.children) label += QStringLiteral("   ›");
            auto *row = new QTreeWidgetItem(m_list, {label, item.shortcut});
            row->setToolTip(0, item.detail);
            if (!item.detail.isEmpty() && item.children) row->setText(0, label + QStringLiteral("   ") + item.detail);
            row->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            row->setForeground(1, QColor(relay::theme::TextMuted));
            row->setData(0, Qt::UserRole, m_rows.size());
            m_rows.append(item);
        };
        if (!needle.isEmpty()) {
            std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });
            for (const auto &entry : std::as_const(scored)) addRow(entry.item);
        } else if (nested) {
            for (const auto &entry : std::as_const(scored)) addRow(entry.item);
        } else {
            // Recent first, then the section that matches where focus was.
            const QStringList recent = QSettings().value(QStringLiteral("palette/recent")).toStringList();
            QList<PaletteItem> recentItems;
            for (const QString &key : recent)
                for (const auto &entry : std::as_const(scored))
                    if (entry.item.key == key && recentItems.size() < 4) recentItems << entry.item;
            if (!recentItems.isEmpty()) { addHeader(QStringLiteral("Recent")); for (const auto &item : recentItems) addRow(item); }
            QStringList order{QStringLiteral("Agent"), QStringLiteral("Terminal"), QStringLiteral("Panes and tabs"), QStringLiteral("Shortcuts")};
            if (m_terminalContext) order = QStringList{QStringLiteral("Terminal"), QStringLiteral("Panes and tabs"), QStringLiteral("Agent"), QStringLiteral("Shortcuts")};
            for (const QString &section : order) {
                bool header = false;
                for (const auto &entry : std::as_const(scored)) {
                    if (entry.item.section != section) continue;
                    if (!header) { addHeader(section); header = true; }
                    addRow(entry.item);
                }
            }
        }
        selectRow(0, 1);
    }

    // Move the selection to the nearest selectable row from `index` in `direction`.
    void selectRow(int index, int direction) {
        const int count = m_list->topLevelItemCount();
        if (!count) return;
        index = std::clamp(index, 0, count - 1);
        for (int i = index; i >= 0 && i < count; i += direction) {
            if (m_list->topLevelItem(i)->data(0, Qt::UserRole).toInt() >= 0) { m_list->setCurrentItem(m_list->topLevelItem(i)); return; }
        }
        for (int i = index; i >= 0 && i < count; i -= direction) {
            if (m_list->topLevelItem(i)->data(0, Qt::UserRole).toInt() >= 0) { m_list->setCurrentItem(m_list->topLevelItem(i)); return; }
        }
    }

    void moveSelection(int steps) {
        const int count = m_list->topLevelItemCount();
        if (!count) return;
        int index = m_list->indexOfTopLevelItem(m_list->currentItem());
        const int direction = steps > 0 ? 1 : -1;
        int moved = 0;
        for (int i = index + direction; moved < std::abs(steps); i += direction) {
            if (i < 0) i = count - 1; else if (i >= count) i = 0;   // wrap
            if (i == index) break;
            if (m_list->topLevelItem(i)->data(0, Qt::UserRole).toInt() >= 0) { index = i; ++moved; }
        }
        if (index >= 0) m_list->setCurrentItem(m_list->topLevelItem(index));
    }

    void activateSelected(bool openOnly) {
        QTreeWidgetItem *row = m_list->currentItem();
        if (!row) return;
        const int index = row->data(0, Qt::UserRole).toInt();
        if (index < 0 || index >= m_rows.size()) return;
        const PaletteItem item = m_rows[index];
        if (item.children) {
            m_stack.append({item.label, item.children()});
            m_filter->clear();
            renderPalette();
            return;
        }
        if (openOnly || !item.run) return;
        if (!item.shortcut.isEmpty())
            hint(QStringLiteral("palette.") + item.key, relay::ShortcutHints::nextTime(item.shortcut, item.label.toLower()));
        QStringList recent = QSettings().value(QStringLiteral("palette/recent")).toStringList();
        recent.removeAll(item.key); recent.prepend(item.key);
        QSettings().setValue(QStringLiteral("palette/recent"), QStringList(recent.mid(0, 12)));
        if (item.stayOpen) {
            item.run();
            // Toggles stay open and show their new state.
            QTimer::singleShot(150, this, [this] {
                if (!m_sidebar->isVisible()) return;
                if (m_stack.isEmpty()) { const int row = m_list->indexOfTopLevelItem(m_list->currentItem()); renderPalette(); selectRow(row, 1); return; }
                const QString title = m_stack.last().first;
                m_stack.removeLast();
                for (const auto &parent : rootItems()) if (parent.label == title && parent.children) m_stack.append({title, parent.children()});
                renderPalette();
            });
            return;
        }
        const auto run = item.run;
        closePalette();
        QTimer::singleShot(0, this, run);
    }

    bool paletteKey(QKeyEvent *key) {
        const bool ctrl = key->modifiers() & Qt::ControlModifier;
        switch (key->key()) {
        case Qt::Key_Escape:
            if (!m_filter->text().isEmpty()) m_filter->clear();
            else if (!m_stack.isEmpty()) { m_stack.removeLast(); renderPalette(); }
            else closePalette();
            return true;
        case Qt::Key_Return: case Qt::Key_Enter: activateSelected(false); return true;
        case Qt::Key_Right:
            if (m_filter->cursorPosition() < m_filter->text().size()) return false;
            activateSelected(true); return true;
        case Qt::Key_Left: case Qt::Key_Backspace:
            if (!m_filter->text().isEmpty() || m_stack.isEmpty()) return false;
            m_stack.removeLast(); renderPalette(); return true;
        case Qt::Key_Down: moveSelection(1); return true;
        case Qt::Key_Up: moveSelection(-1); return true;
        case Qt::Key_PageDown: moveSelection(8); return true;
        case Qt::Key_PageUp: moveSelection(-8); return true;
        default:
            if (ctrl && key->key() == Qt::Key_N) { moveSelection(1); return true; }
            if (ctrl && key->key() == Qt::Key_P) { moveSelection(-1); return true; }
            return false;
        }
    }

    // ----- subagents UI: palette submenu and transcript panes ---------------------------------
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
        if (!m_sidebar->isVisible()) togglePalette();
        m_stack.clear();
        m_stack.append({QStringLiteral("Agents"), agentsMenuItems()});
        m_filter->clear();
        renderPalette();
    }

    void openSubagentPane(Pane *owner, const QString &id) {
        QWidget *page = pageOf(owner);
        if (!page) return;
        for (QWidget *leaf : leavesIn(page))
            if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->subagent() && tool->subagent()->agentId() == id) {
                setActiveLeaf(tool); focusLeaf(tool); return;
            }
        auto *view = new relay::SubagentTranscriptView(id);
        auto *tool = new ToolPane(view, owner->cwd());
        relay::theme::polishWindow(tool);
        QPointer<ToolPane> guard(tool);
        QPointer<Pane> ownerGuard(owner);
        view->onClose = [guard, ownerGuard] {
            auto *w = windowOf(guard);
            if (!w) return;
            w->closePane(guard, false);
            if (ownerGuard && ownerGuard->window() == w) { w->setActiveLeaf(ownerGuard); ownerGuard->focusInput(); }
        };
        owner->attachSubagentView(view);
        insertBeside(owner, tool, Qt::Horizontal, false);
        setActiveLeaf(tool);
        focusLeaf(tool);
    }
    // ----- end subagents UI --------------------------------------------------------------------

public:
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
            tool->explorer()->onOpenFile = [guard](const QString &file) { if (auto *w = windowOf(guard)) w->openPath(file, 0, guard); };
            tool->explorer()->onDirectoryChanged = [guard](const QString &) { if (auto *w = windowOf(guard)) w->updateTitles(); };
        } else if (tool->preview()) {
            tool->preview()->onTitleChanged = [guard](const QString &) { if (auto *w = windowOf(guard)) w->updateTitles(); };
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
        // Per-pane terminal engine: the spec (palette action or restored session), else the
        // process default from --engine / RELAY_ENGINE.
        relay::EngineKind engine = relay::defaultEngineKind();
        relay::parseEngineKind(spec.value(QStringLiteral("engine")).toString(), &engine);
        const QString core = spec.value(QStringLiteral("engine_core")).toString(relay::defaultEngineCore());
        auto *pane = new Pane(workspace, cwd, m_manager->cleanShell(), engine, core);
        // Model roles (protocol 13): panes opened after the first one default to the fast agent.
        const QString savedRole = spec.value(QStringLiteral("agent_role")).toString();
        if (!savedRole.isEmpty()) pane->initAgentRole(savedRole);
        else if (Pane::newPanesUseFastAgent() && !allPanes().isEmpty()) pane->initAgentRole(QStringLiteral("fast"));
        // Saved window layout: model/effort/mode and the conversation to reattach.
        pane->initRestore(spec);
        QPointer<Pane> guard(pane);
        // Callbacks find the pane's current window, so panes and tabs can move between windows.
        pane->onStatus = [guard](const QString &text) { if (auto *w = windowOf(guard); w && guard == w->m_active) w->statusBar()->showMessage(text); };
        pane->onStateChanged = [guard] {
            auto *w = windowOf(guard);
            if (!w) return;
            if (guard == w->m_active) w->syncToolbar();
            w->updateTitles();
        };
        pane->onShellExited = [guard] { if (auto *w = windowOf(guard)) w->closePane(guard, false); };
        pane->onOpenPath = [guard](const QString &path) { if (auto *w = windowOf(guard)) w->openPath(path, 0, guard); };
        pane->onPlanWritten = [guard](const QString &path, Pane *) { if (auto *w = windowOf(guard)) w->openDocument(path, guard, true); };
        pane->onOpenDocument = [guard](const QString &path) { if (auto *w = windowOf(guard)) w->openDocument(path, guard, false); };
        pane->onForkState = [guard](const QJsonObject &state, const QString &title) { if (auto *w = windowOf(guard)) w->openFork(guard, state, title); };
        pane->onOpenSessionInNewPane = [guard](const QJsonObject &state, const QString &title) { if (auto *w = windowOf(guard)) w->openFork(guard, state, title, false); };
        pane->onOpenSubagent = [guard](const QString &id) { if (auto *w = windowOf(guard)) w->openSubagentPane(guard, id); };   // subagents UI
        pane->onShowAgents = [guard] { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->openAgentsMenu(); } };   // /agents → subagents panel menu
        pane->onOpenTurn = [guard](const QString &turnId) { if (auto *w = windowOf(guard)) w->openTurnPane(guard, turnId); };
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
                             {"engine", relay::engineKindName(pane->engine())},
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
                for (QWidget *w : leavesIn(m_tabs->widget(i))) { w->style()->unpolish(w); w->style()->polish(w); }
        }
        QWidget *page = pageOf(leaf);
        if (auto *pane = dynamic_cast<Pane *>(leaf)) m_active = pane;
        else if (!m_active || pageOf(m_active) != page) {
            const auto panes = panesIn(page);
            m_active = panes.isEmpty() ? nullptr : panes.first();
        }
        if (page) m_lastActive.insert(page, leaf);
        syncToolbar();
        updateTitles();
    }

    static QString shortPath(const QString &path) {
        const QString home = QDir::homePath();
        if (path == home) return QStringLiteral("~");
        const QString name = QFileInfo(path).fileName();
        return name.isEmpty() ? path : name;
    }

    void updateTitles() {
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            const auto leaves = leavesIn(page);
            QWidget *leaf = m_lastActive.value(page);
            if (!leaf && !leaves.isEmpty()) leaf = leaves.first();
            QString title = QStringLiteral("Relay");
            if (auto *pane = dynamic_cast<Pane *>(leaf)) title = shortPath(pane->cwd());
            else if (auto *tool = dynamic_cast<ToolPane *>(leaf)) title = tool->title();
            if (leaves.size() > 1) title += QStringLiteral("  ·  %1").arg(leaves.size());
            m_tabs->setTabText(i, title);
            m_tabs->setTabToolTip(i, leaf ? leafCwd(leaf) : QString());
        }
        syncChrome();
        QString where = m_activeLeaf ? leafCwd(m_activeLeaf) : QString();
        if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data())) where = tool->path();
        setWindowTitle(where.isEmpty() ? QStringLiteral("Relay") : QStringLiteral("Relay — ") + where);
        // Saved window layout: this runs after every split, close, tab change, directory change
        // and model change, so it is the one place the debounced save hangs off.
        m_manager->scheduleSave();
    }

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

    void split(Qt::Orientation orientation, const QString &engine = QString()) {
        QWidget *anchor = m_activeLeaf;
        if (!anchor) return;
        Pane *pane = nullptr;
        const QString workspace = m_active ? m_active->workspace() : m_manager->workspace();
        QJsonObject spec{{"cwd", leafCwd(anchor)}, {"workspace", workspace}};
        if (!engine.isEmpty()) spec.insert(QStringLiteral("engine"), engine);
        try { pane = createPane(spec); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return; }
        insertBeside(anchor, pane, orientation, false);
        setActive(pane);
        QTimer::singleShot(0, pane, [pane] { pane->focusInput(); });
    }

    void navigate(int key) {
        QWidget *current = m_activeLeaf;
        if (!current || !pageOf(current)) return;
        // A candidate must lie on the requested side; prefer the nearest, then the most aligned.
        if (QWidget *best = neighborOf(current, key)) { setActiveLeaf(best); focusLeaf(best); }
    }

    // ----- pane chrome, tab bar controls and moving panes ------------------------------------------
    void buildTabBarControls() {
        QTabBar *bar = m_tabs->tabBar();
        bar->setMouseTracking(true);
        bar->installEventFilter(this);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        m_newTabButton = new QToolButton(bar);
        m_newTabButton->setObjectName(QStringLiteral("newTabButton"));
        m_newTabButton->setText(QStringLiteral("+"));
        m_newTabButton->setAutoRaise(true);
        m_newTabButton->setFocusPolicy(Qt::NoFocus);
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
                connect(close, &QAction::triggered, this, [this, index] { if (m_tabs->count() > 1) closeTab(index, true); else closeWindowWithWarning(); });
                menu.addSeparator();
            }
            auto *add = menu.addAction(QStringLiteral("New tab"));
            add->setShortcut(QKeySequence(Keymap::instance().keysFor(QStringLiteral("tab.new")).value(0)));
            connect(add, &QAction::triggered, this, [this] { runAction(QStringLiteral("tab.new")); });
            menu.exec(bar->mapToGlobal(at));
        });
    }

    void placeTabBarControls() {
        if (!m_newTabButton) return;
        QTabBar *bar = m_tabs->tabBar();
        const QRect last = bar->count() ? bar->tabRect(bar->count() - 1) : QRect();
        const QSize size(std::max(24, bar->height() - 6), std::max(20, bar->height() - 6));
        int x = last.isValid() ? last.right() + 4 : 4;
        x = std::min(x, bar->width() - size.width() - 2);
        m_newTabButton->setGeometry(x, (bar->height() - size.height()) / 2, size.width(), size.height());
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
        }
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
                    chrome->onDragMove = [guard](const QPoint &global) { if (auto *w = windowOf(guard)) w->dragPaneMove(guard, global); };
                    chrome->onDragEnd = [guard](const QPoint &global, bool drop) { if (auto *w = windowOf(guard)) w->dragPaneEnd(guard, global, drop); };
                    leaf->installEventFilter(this);
                }
                chrome->refreshTooltips();
                chrome->place();
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

    void showChromeFor(QWidget *leaf) {
        if (m_hoverLeaf == leaf) return;
        if (m_hoverLeaf) if (auto *old = chromeOf(m_hoverLeaf)) old->hide();
        m_hoverLeaf = leaf;
        if (leaf) if (auto *chrome = chromeOf(leaf)) { chrome->place(); chrome->show(); }
    }

    enum class Edge { None, Left, Right, Top, Bottom, TabBar };

    // Where a drop at `global` would put a pane: an edge of the leaf under the cursor, or a tab bar.
    static QPair<QWidget *, Edge> dropTarget(QWidget *dragged, const QPoint &global) {
        QWidget *under = QApplication::widgetAt(global);
        for (QWidget *w = under; w; w = w->parentWidget())
            if (auto *bar = dynamic_cast<QTabBar *>(w); bar && windowOf(bar)) return {bar, Edge::TabBar};
        QWidget *leaf = leafOf(under);
        if (!leaf || leaf == dragged || !windowOf(leaf)) return {nullptr, Edge::None};
        const QPoint local = leaf->mapFromGlobal(global);
        const double fx = double(local.x()) / std::max(1, leaf->width()), fy = double(local.y()) / std::max(1, leaf->height());
        const double left = fx, right = 1 - fx, top = fy, bottom = 1 - fy;
        const double nearest = std::min({left, right, top, bottom});
        if (nearest == left) return {leaf, Edge::Left};
        if (nearest == right) return {leaf, Edge::Right};
        if (nearest == top) return {leaf, Edge::Top};
        return {leaf, Edge::Bottom};
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
        showChromeFor(nullptr);
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
        if (m_hoverLeaf == leaf) m_hoverLeaf = nullptr;
        if (auto *chrome = chromeOf(leaf)) chrome->hide();
        const bool last = leavesIn(page).size() <= 1;
        auto *splitter = dynamic_cast<QSplitter *>(leaf->parentWidget());
        leaf->hide();
        leaf->setParent(nullptr);
        if (m_active.data() == leaf) m_active = nullptr;
        if (m_activeLeaf.data() == leaf) m_activeLeaf = nullptr;
        if (m_lastActive.value(page) == leaf) m_lastActive.remove(page);
        if (last) {
            m_lastActive.remove(page);
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
        if (leavesIn(page).size() <= 1) { statusBar()->showMessage(QStringLiteral("This pane is already the only pane in its tab."), 4000); return; }
        const int index = m_tabs->indexOf(page) + 1;
        if (!takeLeaf(leaf)) return;
        adoptLeafAsTab(leaf, index);
    }

    void moveTabToNewWindow(int index) {
        QWidget *page = m_tabs->widget(index);
        if (!page) return;
        if (m_tabs->count() <= 1) { statusBar()->showMessage(QStringLiteral("This is the only tab in the window."), 4000); return; }
        QWidget *lastActive = m_lastActive.value(page);
        if (m_hoverLeaf && pageOf(m_hoverLeaf) == page) m_hoverLeaf = nullptr;
        if (m_active && pageOf(m_active) == page) m_active = nullptr;
        if (m_activeLeaf && pageOf(m_activeLeaf) == page) m_activeLeaf = nullptr;
        m_lastActive.remove(page);
        m_tabs->removeTab(index);
        page->setParent(nullptr);
        RelayWindow *window = m_manager->newEmptyWindow(geometry().translated(40, 40));
        window->adoptPage(page, lastActive);
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
    void moveActive(int key) {
        QWidget *current = m_activeLeaf;
        QWidget *page = current ? pageOf(current) : nullptr;
        if (!page) return;
        QWidget *neighbor = neighborOf(current, key);
        if (!neighbor) { statusBar()->showMessage(QStringLiteral("No pane in that direction."), 2500); return; }
        const Qt::Orientation orientation = key == Qt::Key_Left || key == Qt::Key_Right ? Qt::Horizontal : Qt::Vertical;
        const bool towardStart = key == Qt::Key_Left || key == Qt::Key_Up;
        auto *splitter = dynamic_cast<QSplitter *>(current->parentWidget());
        const bool siblings = splitter && splitter == neighbor->parentWidget() && splitter->orientation() == orientation
                              && std::abs(splitter->indexOf(current) - splitter->indexOf(neighbor)) == 1;
        if (siblings) {
            const QList<int> sizes = splitter->sizes();
            splitter->insertWidget(splitter->indexOf(neighbor), current);   // moves `current` before or after
            if (!towardStart) splitter->insertWidget(splitter->indexOf(current), neighbor);
            splitter->setSizes(sizes);
        } else {
            QPointer<QWidget> anchor(neighbor);
            if (!takeLeaf(current) || !anchor) return;
            insertBeside(anchor, current, orientation, !towardStart);
        }
        showChromeFor(nullptr);
        setActiveLeaf(current); focusLeaf(current);
        updateTitles();
    }

    QWidget *neighborOf(QWidget *current, int key) const {
        QWidget *page = pageOf(current);
        const QRect from(current->mapTo(page, QPoint(0, 0)), current->size());
        QWidget *best = nullptr;
        double bestScore = 1e18;
        for (QWidget *pane : leavesIn(page)) {
            if (pane == current) continue;
            const QRect to(pane->mapTo(page, QPoint(0, 0)), pane->size());
            double gap = 0, offset = 0;
            switch (key) {
            case Qt::Key_Right: if (to.left() < from.right() - 4) continue; gap = to.left() - from.right(); offset = std::abs(to.center().y() - from.center().y()); break;
            case Qt::Key_Left: if (to.right() > from.left() + 4) continue; gap = from.left() - to.right(); offset = std::abs(to.center().y() - from.center().y()); break;
            case Qt::Key_Down: if (to.top() < from.bottom() - 4) continue; gap = to.top() - from.bottom(); offset = std::abs(to.center().x() - from.center().x()); break;
            case Qt::Key_Up: if (to.bottom() > from.top() + 4) continue; gap = from.top() - to.bottom(); offset = std::abs(to.center().x() - from.center().x()); break;
            default: continue;
            }
            const double score = std::max(0.0, gap) * 4 + offset;
            if (score < bestScore) { bestScore = score; best = pane; }
        }
        return best;
    }

    void closeActive() {
        QWidget *pane = m_activeLeaf;
        if (!pane) return;
        QWidget *page = pageOf(pane);
        if (leavesIn(page).size() > 1) closePane(pane, true);
        else if (m_tabs->count() > 1) closeTab(m_tabs->indexOf(page), true);
        else closeWindowWithWarning();
    }

public:
    void closePane(QWidget *pane, bool record) {
        QWidget *page = pageOf(pane);
        if (!page) return;
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
    QTabWidget *m_tabs = nullptr;
    QList<QPair<QAction *, QString>> m_toolbarActions;
    QWidget *m_sidebar = nullptr;
    QLabel *m_paletteTitle = nullptr;
    QLineEdit *m_filter = nullptr;
    QTreeWidget *m_list = nullptr;
    QList<QPair<QString, QList<PaletteItem>>> m_stack;
    QList<PaletteItem> m_rows;
    bool m_terminalContext = false;
    QPointer<Pane> m_returnPane;
    QPointer<QWidget> m_returnFocus;
    QPointer<Pane> m_active;
    QHash<QWidget *, QPointer<QWidget>> m_lastActive;
    QPointer<QWidget> m_activeLeaf;
    QPointer<QWidget> m_hoverLeaf;
    QPointer<QToolButton> m_newTabButton;
    QPointer<QFrame> m_dropZone;
    bool m_confirmedClose = false, m_skipRemember = false;
};

// ----- saved window layout: "reopen where I left off" ----------------------------------------
// See the block comment on WindowManager. The file format and the pure rules live in
// src/WindowState.h; everything below walks the live windows.

void WindowManager::setUpLayoutSaving() {
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

bool WindowManager::ownsLayout() {
    if (m_owner) return true;
    if (!m_stateLock) return false;          // no lock file: fall back to last writer wins
    m_owner = m_stateLock->tryLock(0);
    return m_owner;
}

void WindowManager::scheduleSave() {
    if (m_saveSuspended || !restoreEnabled() || m_statePath.isEmpty()) return;
    m_saveTimer.start();
}

QJsonArray WindowManager::captureWindows() {
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

void WindowManager::writeWindows(const QJsonArray &windows) {
    m_saveTimer.stop();
    // An empty set is never written: it would mean "open nothing next time", and quitting is not
    // a request to forget the layout. "Start a fresh window set" and --fresh are.
    if (windows.isEmpty() || m_saveSuspended || !restoreEnabled() || m_statePath.isEmpty()) return;
    if (!ownsLayout() && m_stateLock) return;   // another Relay owns the file
    QString error;
    if (!relay::windowstate::write(m_statePath, relay::windowstate::document(windows), &error))
        fprintf(stderr, "relay: could not save the window layout: %s\n", qPrintable(error));
}

void WindowManager::saveLayoutNow() { writeWindows(captureWindows()); }

void WindowManager::noteWindowClosing() {
    if (m_cascadeActive) return;
    // Taken while the closing window is still in the list: if every window goes (a quit), this is
    // the set that comes back; if others survive, the settled snapshot below wins instead.
    m_cascadeActive = true;
    m_cascadeSnapshot = captureWindows();
    QTimer::singleShot(0, &m_context, [this] { settleWindowClose(); });
}

void WindowManager::settleWindowClose() {
    if (!m_cascadeActive) return;
    m_cascadeActive = false;
    m_windows.removeAll(nullptr);
    if (m_windows.isEmpty()) writeWindows(m_cascadeSnapshot);   // quit: keep what was open
    else saveLayoutNow();                                       // one window closed: drop it
    m_cascadeSnapshot = QJsonArray();
}

int WindowManager::restoreSavedLayout() {
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
void WindowManager::announceRestore() {
    m_windows.removeAll(nullptr);
    if (m_restoreNote.isEmpty() || m_windows.isEmpty() || !m_windows.first()) return;
    QPointer<RelayWindow> window = m_windows.first();
    const QString note = m_restoreNote;
    QTimer::singleShot(1200, &m_context, [window, note] { if (window) window->statusBar()->showMessage(note, 9000); });
}

void WindowManager::forgetSavedLayout(bool suspend) {
    m_saveTimer.stop();
    m_saveSuspended = suspend;
    if (!m_statePath.isEmpty()) QFile::remove(m_statePath);
}

RelayWindow *WindowManager::newWindow(const QJsonArray &tabs, int current, const QRect &geometry) {
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

RelayWindow *WindowManager::newEmptyWindow(const QRect &geometry) {
    auto *window = new RelayWindow(this);
    relay::theme::polishWindow(window);
    if (geometry.isValid()) window->setGeometry(geometry);
    m_windows.append(window);
    window->show();
    return window;
}

RelayWindow *WindowManager::newWindowAt(const QString &cwd) {
    return newWindow(QJsonArray{QJsonObject{{"pane", QJsonObject{{"cwd", cwd}, {"workspace", m_workspace}}}}});
}

bool WindowManager::handleOpen(const QJsonObject &request) {
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
    // A shell names its pane; a Konsole link click comes from the focused window.
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

void WindowManager::cycle(RelayWindow *from, int delta) {
    m_windows.removeAll(nullptr);
    if (m_windows.size() < 2) return;
    int index = m_windows.indexOf(from);
    if (index < 0) index = 0;
    RelayWindow *next = m_windows.at((index + delta + m_windows.size()) % m_windows.size());
    next->showNormal(); next->raise(); next->activateWindow();
}

void WindowManager::restore(RelayWindow *requester) {
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
    if (requester) requester->statusBar()->showMessage(QStringLiteral("Nothing to restore."));
}

// Konsole opens OSC 8 links through KIO, which launches the desktop's handler for the scheme.
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
            if (auto *window = qobject_cast<QMainWindow *>(widget)) window->statusBar()->showMessage(QStringLiteral("Registered relay:// links so agent turn details open from the terminal."), 8000);
    }
}

int main(int argc, char **argv) {
    // Must precede QApplication: KDE platform plugins may open relayrc during construction.
    relay::theme::exposeKonsoleProfile();
    QApplication app(argc, argv);
    relay::theme::applyDarkTheme(app);
    QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
    QCoreApplication::setApplicationName(QStringLiteral("relay"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RELAY_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.relayterminal.Relay"));
    try {
        // Theme icon when installed; the bundled PNG from the source tree or install otherwise.
        const QString root = dataRoot();
        QString png = root + QStringLiteral("/data/icons/hicolor/256x256/apps/org.relayterminal.Relay.png");
        if (!QFileInfo::exists(png)) png = root + QStringLiteral("/../icons/hicolor/256x256/apps/org.relayterminal.Relay.png");
        QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("org.relayterminal.Relay"), QIcon(png)));
    } catch (const std::exception &) {
    }
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("Terminal with rich input and BYOK agents."));
    parser.addHelpOption(); parser.addVersionOption();
    QCommandLineOption workspace(QStringList{QStringLiteral("w"), QStringLiteral("workspace")}, QStringLiteral("Initial terminal directory and agent workspace."), QStringLiteral("path"), QDir::currentPath());
    QCommandLineOption clean(QStringLiteral("clean-shell"), QStringLiteral("Do not source ~/.bashrc; useful for incompatible DEBUG/preexec prompt hooks."));
    QCommandLineOption engine(QStringLiteral("engine"), QStringLiteral("Terminal engine for new panes: konsole (default) or relay. Also RELAY_ENGINE."), QStringLiteral("name"));
    QCommandLineOption engineCore(QStringLiteral("engine-core"), QStringLiteral("Emulator core of Relay-engine panes: ghostty or libvterm. Also RELAY_ENGINE_CORE."), QStringLiteral("name"));
    // Saved window layout: --fresh ignores it this once (the file itself is kept).
    QCommandLineOption fresh(QStringLiteral("fresh"), QStringLiteral("Start with one new window instead of reopening the saved window layout."));
    parser.addOption(workspace); parser.addOption(clean); parser.addOption(engine); parser.addOption(engineCore);
    parser.addOption(fresh); parser.process(app);
    // Per-pane terminal engine (docs/ENGINE.md); the palette can still pick the other one.
    QString engineWarning;
    relay::setDefaultEngineKind(relay::resolveEngineKind(parser.value(engine), qEnvironmentVariable("RELAY_ENGINE"), &engineWarning));
    relay::setDefaultEngineCore(relay::resolveEngineCore(parser.value(engineCore), qEnvironmentVariable("RELAY_ENGINE_CORE")));
    if (!engineWarning.isEmpty()) fprintf(stderr, "relay: %s\n", qPrintable(engineWarning));
    const auto path = QFileInfo(parser.value(workspace)).canonicalFilePath();
    if (path.isEmpty() || !QFileInfo(path).isDir()) { QMessageBox::critical(nullptr, QStringLiteral("Relay"), QStringLiteral("Workspace must be an existing directory.")); return 1; }
    QDir::setCurrent(path);
    try {
        // Make relay-open available to Konsole's file-link command and to Relay shells.
        const QString scripts = dataRoot() + QStringLiteral("/scripts");
        qputenv("RELAY_OPEN_HELPER", (scripts + QStringLiteral("/relay-open")).toUtf8());
        qputenv("PATH", (scripts + ':' + qEnvironmentVariable("PATH")).toUtf8());
        WindowManager manager(path, parser.isSet(clean));
        manager.setUpLayoutSaving();
        // Quit without closing the windows (Ctrl+Q, session logout, SIGTERM through Qt) still
        // saves; closing them goes through RelayWindow::closeEvent instead.
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&manager] { manager.saveLayoutNow(); });
        QTimer::singleShot(1500, &app, [] { registerUrlHandler(); });
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

