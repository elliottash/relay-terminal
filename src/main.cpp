// SPDX-License-Identifier: GPL-3.0-or-later
#include "RichEditor.h"
#include "Theme.h"
#include "FilePanes.h"
#include "BoardPane.h"          // Switchboard: cards, threads, card detail
#include "BoardWorker.h"        // the per-window Switchboard worker (protocol 17)
#include "AgentUi.h"
#include "Completion.h"
#include "ShellHighlighter.h"
#include "Hints.h"
#include "Notifications.h"        // window header: the bell and its list
#include "InputPolicy.h"           // prompt-box-only input: where a submitted line goes
#include "ScreenPrompt.h"          // "is the program waiting for input?", read off the screen
#include "PaneLayout.h"            // pane focus, pane moves and grip drops
#include "PaneTitles.h"            // model-written pane titles and the tab labels made from them
#include "TurnTranscript.h"
#include "ModelSettings.h"
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
#include "RemoteShare.h"    // sharing a pane with a phone (docs/REMOTE-PROTOCOL.md)
#include "backend/VTermBackend.h"  // the engine can hand over a frame to a program
#include "Voice.h"          // voice transcription: capture, the hold key, the transcript
#include "Images.h"         // image context: paste, drop, `@path` and "Screenshot this pane"
#include "Aliases.h"        // aliases: saved commands and prompts, their fields and invocations
#include "MarkdownAnsi.h"   // agent replies in the terminal: Markdown rendered as it streams
#include "OutputLinks.h"    // what a link in the output is; `relay://card/<id>` for a `#K7Q2`
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
    // Every key bound to an action, as the desktop writes them. An action whose one gesture needs
    // several spellings (help.shortcuts: Ctrl+? is Ctrl+Shift+/ on most keyboards) can then tell
    // the user which keys actually work instead of only the first one.
    QStringList shortcutTexts(const QString &id) const {
        QStringList texts;
        for (const auto &key : keysFor(id)) {
            const QString text = QKeySequence::fromString(key, QKeySequence::PortableText).toString(QKeySequence::NativeText);
            if (!text.isEmpty() && !texts.contains(text)) texts << text;
        }
        return texts;
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
        add("window.new", "window", "New window", {QStringLiteral("Ctrl+N"), QStringLiteral("Ctrl+Shift+N")});
        add("window.next", "window", "Next Relay window", {QStringLiteral("Alt+Tab")});
        add("window.previous", "window", "Previous Relay window", {QStringLiteral("Alt+Shift+Tab")});
        add("tab.new", "tab", "New tab", {QStringLiteral("Ctrl+T"), QStringLiteral("Ctrl+Shift+T")});
        add("tab.next", "tab", "Next tab", {QStringLiteral("Ctrl+Tab")});
        add("tab.previous", "tab", "Previous tab", {QStringLiteral("Ctrl+Shift+Tab")});
        // Ctrl+E, not Ctrl+P: one-handed (owner, 2026-09-17). Ctrl+D is left alone because it is
        // end-of-input for a running program. Ctrl+Shift+E is the twin that programs cannot swallow.
        // One key is now the whole of "new pane": it makes one on the right, and Left, Up or Down
        // within two seconds re-docks it there instead (issue #78BN). The other three directions
        // keep actions of their own so they can be bound or run from the palette, but the separate
        // "new pane below" key (Ctrl+Alt+E) is dropped.
        add("pane.splitRight", "pane", "New pane to the right (then ← ↑ ↓ places it)",
            {QStringLiteral("Ctrl+E"), QStringLiteral("Ctrl+Shift+E")});
        add("pane.splitDown", "pane", "New pane below", {});
        add("pane.splitLeft", "pane", "New pane to the left", {});
        add("pane.splitUp", "pane", "New pane above", {});
        add("pane.focusLeft", "pane", "Focus pane to the left", {QStringLiteral("Alt+Left")});
        add("pane.focusRight", "pane", "Focus pane to the right", {QStringLiteral("Alt+Right")});
        add("pane.focusUp", "pane", "Focus pane above", {QStringLiteral("Alt+Up")});
        add("pane.focusDown", "pane", "Focus pane below", {QStringLiteral("Alt+Down")});
        add("pane.close", "pane", "Close pane, then tab, then window", {QStringLiteral("Ctrl+W"), QStringLiteral("Ctrl+Shift+W")});
        add("pane.moveLeft", "pane", "Move pane left (swap with or dock beside the neighbor)", {QStringLiteral("Ctrl+Alt+Left")});
        add("pane.moveRight", "pane", "Move pane right", {QStringLiteral("Ctrl+Alt+Right")});
        add("pane.moveUp", "pane", "Move pane up", {QStringLiteral("Ctrl+Alt+Up")});
        add("pane.moveDown", "pane", "Move pane down", {QStringLiteral("Ctrl+Alt+Down")});
        add("pane.moveToNewTab", "pane", "Move pane to a new tab (keeps the shell and agent)", {});
        add("tab.moveToNewWindow", "tab", "Move tab to a new window (keeps its panes)", {});
        add("closed.restore", "pane", "Restore the last closed pane, tab or window", {QStringLiteral("Ctrl+Shift+Z")});
        add("windows.fresh", "window", "Start a fresh window set (forget the saved window layout)", {});
        add("palette.open", "palette", "Open the Relay actions palette", {QStringLiteral("Ctrl+Shift+A")});
        // One key opens and closes the explorer (issue #D60R). Ctrl+B is VS Code's sidebar key and
        // is free in all four Relay presets; Ctrl+Shift+B is the twin a program cannot swallow.
        add("files.explorer", "pane", "File explorer: open or close this pane's folder in an explorer pane",
            {QStringLiteral("Ctrl+B"), QStringLiteral("Ctrl+Shift+B")});
        add("files.open", "pane", "Open a file in a preview pane", {});
        add("board.open", "pane", "Switchboard: cards, threads and plans", {QStringLiteral("Ctrl+Shift+S")});
        add("control.human", "terminal", "Take control of the terminal (the only way keys reach it; works from the prompt box)", {QStringLiteral("Ctrl+H")});
        add("control.prompt", "terminal", "Back to the Relay prompt (the agent is in control)", {QStringLiteral("Ctrl+Shift+H")});
        add("program.delegate", "terminal", "Let the agent drive the program in this pane (with text in the prompt box, ask it now)",
            {QStringLiteral("Ctrl+Shift+J")});
        add("terminal.native", "terminal", "Toggle native terminal input (keys go straight to the terminal)", {QStringLiteral("F12")});
        add("pane.restartShell", "terminal", "Restart this pane's shell or agent after it stopped", {QStringLiteral("Ctrl+Shift+R")});
        add("terminal.interrupt", "terminal", "Interrupt the running command (Esc)", {});
        add("agent.newChat", "agent", "Start a new agent conversation and clear the terminal", {});
        // Keyboard walk over the files, folders and links in the output (issue GWXM). Free in
        // every preset (docs/KEYBINDING-PRESETS.md), so all four keep the Relay default.
        add("links.step", "terminal", "Step through files, folders and links in the output (Enter opens, Esc leaves)",
            {QStringLiteral("Ctrl+Shift+L")});
        add("agent.clearQueue", "agent", "Clear queued agent prompts", {});
        add("agent.resumeQueue", "agent", "Resume the paused agent queue", {});
        add("agent.stop", "agent", "Stop the agent turn", {});
        add("agent.stopAllSubagents", "agent", "Stop all running subagents", {QStringLiteral("Ctrl+Shift+X")});   // subagents UI
        add("agent.agentsMenu", "agent", "Agents: definitions and running subagents", {});
        // Voice transcription: the hold key is its own setting (Settings › Voice), because a
        // push-to-talk key is held rather than pressed and is not a shortcut the keymap can bind.
        add("voice.toggle", "agent", "Voice transcription: start or finish recording", {});
        // Sharing a pane with a phone (#W5N2). No default shortcut: it is a deliberate act,
        // and the strip button beside the microphone is the usual way in.
        add("pane.share", "terminal", "Share this pane with a phone", {});
        add("agent.interrupt", "agent", "Send to the agent; while it is busy, interrupt it and send now (prompt box)",
            {QStringLiteral("Ctrl+Return"), QStringLiteral("Ctrl+Enter"), QStringLiteral("Ctrl+Alt+Return"), QStringLiteral("Ctrl+Alt+Enter")});
        add("agent.provider", "agent", "Provider and API keys (advanced endpoint settings)", {});
        add("agent.modelKeys", "agent", "API keys for model providers", {});
        add("agent.modelRoles", "agent", "Model roles: default provider and the Main / Flash / Lite models", {});
        add("app.settings", "window", "Settings: models, terminal, agent, privacy, shortcuts", {QStringLiteral("Ctrl+,")});
        add("agent.fastAgent", "agent", "Switch this pane between the main agent and the fast agent", {QStringLiteral("Alt+F")});   // model roles
        add("input.modeAuto", "agent", "Input mode: auto detect", {});
        add("input.modeTerminal", "agent", "Input mode: terminal", {});
        add("input.modeAgent", "agent", "Input mode: agent", {});
        add("input.toggle", "agent", "Cycle input: auto detect → terminal → agent (from the prompt box)",
            {QStringLiteral("Ctrl+I"), QStringLiteral("Ctrl+Shift+I")});
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
            {QStringLiteral("Ctrl+F"), QStringLiteral("Ctrl+Shift+F")});
        add("agent.recap", "agent", "Recap this agent session", {});
        add("agent.requests", "agent", "Tasks: show or hide the agent's task list (/tasks)", {QStringLiteral("Ctrl+Shift+K")});
        add("agent.continue", "agent", "Continue the agent turn after a step limit (/continue)", {});
        add("agent.instructions", "agent", "Choose agent instruction files", {});
        add("agent.export", "agent", "Export the conversation as Markdown", {});
        // Image context: G for grab. Ctrl+Shift so it still works while a program owns the terminal,
        // which is exactly when a picture of the pane is worth sending (issue EM1E).
        add("agent.screenshotPane", "agent", "Screenshot this pane and attach it to the next prompt",
            {QStringLiteral("Ctrl+Shift+G")});
        // "Ctrl+?" is one gesture with several spellings. Qt reports the main-row key as
        // Key_Question on a US layout and as Key_Slash on others and on the keypad, with Shift
        // held either way, so all of them are bound: Ctrl+Shift+? reaches Ctrl+? and Ctrl+Shift+/
        // on a Key_Slash layout reaches Ctrl+/ through match()'s shifted-symbol fallback.
        add("help.shortcuts", "palette", "Show all keyboard shortcuts",
            {QStringLiteral("Ctrl+?"), QStringLiteral("Ctrl+Shift+/"), QStringLiteral("Ctrl+/"), QStringLiteral("F1")});
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
        return QByteArrayLiteral(R"PRESETS({"relay":{},"warp":{"app.settings":[],"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+T"],"tab.next":["Ctrl+PgDown","Ctrl+Tab"],"tab.previous":["Ctrl+PgUp","Ctrl+Shift+Tab"],"pane.splitRight":["Ctrl+Shift+D"],"pane.splitDown":[],"pane.splitLeft":[],"pane.splitUp":[],"pane.focusLeft":["Ctrl+Alt+Left"],"pane.focusRight":["Ctrl+Alt+Right"],"pane.focusUp":["Ctrl+Alt+Up"],"pane.focusDown":["Ctrl+Alt+Down"],"pane.moveLeft":[],"pane.moveRight":[],"pane.moveUp":[],"pane.moveDown":[],"pane.close":["Ctrl+Shift+W"],"closed.restore":["Ctrl+Alt+T"],"palette.open":["Ctrl+Shift+P"],"terminal.native":["F12"],"terminal.interrupt":[],"agent.newChat":["Ctrl+Shift+Y"],"agent.stop":[],"agent.provider":[],"input.modeAuto":[],"input.modeTerminal":["Ctrl+Shift+I"],"input.modeAgent":[],"input.toggle":["Ctrl+I"],"keybindings.edit":["Ctrl+,"],"keybindings.reload":[],"agent.requests":[]},"vscode":{"app.settings":[],"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+~"],"tab.next":["Ctrl+PgDown","Ctrl+Tab"],"tab.previous":["Ctrl+PgUp","Ctrl+Shift+Tab"],"pane.splitRight":["Ctrl+Shift+%","Ctrl+\\"],"pane.splitDown":[],"pane.splitLeft":[],"pane.splitUp":[],"pane.focusLeft":["Alt+Left"],"pane.focusRight":["Alt+Right"],"pane.focusUp":["Alt+Up"],"pane.focusDown":["Alt+Down"],"pane.close":["Ctrl+W"],"closed.restore":["Ctrl+Shift+T"],"palette.open":["Ctrl+Shift+P"],"terminal.native":["Ctrl+`","F12"],"terminal.interrupt":[],"agent.newChat":["Ctrl+N"],"agent.stop":["Ctrl+Esc"],"agent.provider":["Ctrl+Alt+."],"input.modeAuto":[],"input.modeTerminal":[],"input.modeAgent":["Ctrl+Shift+Alt+I"],"keybindings.edit":["Ctrl+,"],"keybindings.reload":[],"agent.requests":[]},"konsole":{"app.settings":[],"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+T"],"tab.next":["Ctrl+PgDown"],"tab.previous":["Ctrl+PgUp"],"pane.splitRight":["Ctrl+Shift+(","Ctrl+("],"pane.splitDown":[],"pane.splitLeft":[],"pane.splitUp":[],"pane.focusLeft":["Ctrl+Shift+Left"],"pane.focusRight":["Ctrl+Shift+Right"],"pane.focusUp":["Ctrl+Shift+Up"],"pane.focusDown":["Ctrl+Shift+Down"],"pane.close":["Ctrl+Shift+W"],"closed.restore":[],"palette.open":["Ctrl+Alt+I"],"terminal.native":["F12"],"terminal.interrupt":[],"agent.newChat":[],"agent.stop":[],"agent.provider":[],"input.modeAuto":[],"input.modeTerminal":[],"input.modeAgent":[],"keybindings.edit":["Ctrl+Alt+,"],"keybindings.reload":[],"agent.requests":[]}})PRESETS");
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
        painter->setPen(agent ? relay::theme::Accent : relay::theme::Warning);
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

// One terminal pane: a shell behind relay::TerminalBackend (Relay's own engine, engine/), its
// Bash bridge, a composer, and its own agent worker and conversation. Windows arrange panes in tabs and splits; the toolbar acts on the active pane.
class Pane final : public QWidget {
public:
    struct QueueEntry {
        quint64 id = 0; bool agent = false, fix = false, watch = false;
        QString text, why; QJsonArray attachments, cards;   // cards: `#K7Q2` referenced in the prompt
        // Wrong-mode hints (2026-09-17): natural marks a terminal submission that reads like an
        // agent request; shellText carries an agent submission that is a runnable shell command.
        bool natural = false; QString shellText;
    };
    // Why the prompt box is hidden, so it can come back by itself when the reason ends.
    enum class HideReason { None, AltScreen, Remote, Manual };
    Pane(const QString &workspace, const QString &cwd, bool cleanShell,
         const QString &engineCore = relay::defaultEngineCore())
        : m_workspace(workspace), m_cwd(cwd.isEmpty() ? workspace : cwd), m_cleanShell(cleanShell) {
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
        // Image context (issue EM1E): a picture pasted or dropped into the prompt box is attached.
        m_editor->onImageMime = [this](const QMimeData *data, bool dropped) {
            return attachImages(data, dropped);
        };
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
        // Voice: a clip whose transcript never came back would otherwise outlive the pane.
        if (m_voiceCapture) m_voiceCapture->cancel();
        if (!m_voiceClip.isEmpty()) QFile::remove(m_voiceClip);
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
    // Open a folder (explorer pane) or a file (preview pane); `line` > 0 scrolls the preview there.
    std::function<void(const QString &path, int line)> onOpenPath;
    std::function<void(const QString &)> onToggleExplorer;   // open the explorer, or close it again
    std::function<void()> onOpenBoard;                 // Switchboard: /switchboard from this pane
    std::function<void(const QString &)> onOpenCard;   // Switchboard: one card, from the work chip
    std::function<void(const QString &turnId)> onOpenTurn;   // "✦ N tool calls" link or palette
    // Right-click menu entries the window owns: new pane, close pane, tasks (issue #X2F1).
    std::function<void(const QString &action)> onWindowAction;
    // Dragging the pane's header moves the whole pane; the window decides where it lands (owner,
    // 2026-09-17). Same pair as PaneChrome's grip, so both handles take one path through the window.
    std::function<void(const QPoint &global)> onHeaderDragMove;
    std::function<void(const QPoint &global, bool drop)> onHeaderDragEnd;

    QString cwd() const { return m_cwd; }
    QString sessionToken() const { return m_token; }
    QString workspace() const { return m_workspace; }
    bool cleanShell() const { return m_cleanShell; }
    QString engineCore() const { return m_engineCore; }
    QString mode() const { return m_modeValue; }
    void setMode(const QString &mode) {
        m_modeValue = mode; requestRoute(false, QStringLiteral("auto")); refreshDestinationColor(); changed();
    }
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
    // Switchboard: put `#K7Q2 ` (or any text) at the composer's cursor and focus it. Used by the
    // pane's `t` key and by "work on #K7Q2" from a card.
    void insertInComposer(const QString &text) {
        if (text.isEmpty()) return;
        if (m_secretMode || m_native) { focusInput(); return; }
        m_editor->insertPlainText(text);
        focusInput();
    }
    // The prompt box is masked and answers a password prompt (see checkPasswordPrompt()).
    bool secretMode() const { return m_secretMode; }

    void interruptShell() {
        if (m_backend) { m_loading = false; m_promptReported = false; clearFix(); sendShellInput(QString(QChar(3))); focusTerminal(); }
    }
    void newChat() {
        if (m_agentBusy) { status(QStringLiteral("Stop the current agent turn first.")); return; }
        send({{"type", "reset"}}); clearFix();
        // Agent turns print into the terminal, so a new conversation that left the old ones on
        // screen looked as though /new had done nothing: clear it, the way "Clear terminal" does.
        // The note then goes to the toast rather than the screen, because printing it would erase
        // the prompt Readline has just repainted and nothing puts that prompt back.
        if (m_backend && shellIdleAtPrompt()) {
            clearTerminal();
            toast(QStringLiteral("New agent conversation"), 3000);
            return;
        }
        // A program owns the screen: leave it alone and let the note queue until the prompt is back.
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
        // "role:" is the pane's own role chip entry, "gear:" the model options modal, and
        // "vision:" the model this one turn is running on because it carries an image: all three
        // are labels, not models to switch to.
        if (id.startsWith(QStringLiteral("role:")) || id.startsWith(QStringLiteral("gear:"))
            || id.startsWith(QStringLiteral("vision:"))) return;
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
    // Mirrors relay_core.roles.ROLES minus "main" (the pane's own model).
    static QStringList roleIds() {
        return {QStringLiteral("terminal_use"), QStringLiteral("subagent"), QStringLiteral("switchboard"),
                QStringLiteral("fast"), QStringLiteral("summaries"), QStringLiteral("suggestions"),
                QStringLiteral("chores"), QStringLiteral("audit"), QStringLiteral("vision"),
                QStringLiteral("route_assist")};
    }
    static QString roleLabel(const QString &role) {
        static const QHash<QString, QString> labels{
            {QStringLiteral("main"), QStringLiteral("Main agent")},
            {QStringLiteral("terminal_use"), QStringLiteral("Terminal-use agent")},
            {QStringLiteral("subagent"), QStringLiteral("Subagent")},
            {QStringLiteral("switchboard"), QStringLiteral("Switchboard agent")},
            {QStringLiteral("fast"), QStringLiteral("Fast agent")},
            {QStringLiteral("summaries"), QStringLiteral("Summaries")},
            {QStringLiteral("suggestions"), QStringLiteral("Suggestions")},
            {QStringLiteral("chores"), QStringLiteral("Chores")},
            {QStringLiteral("audit"), QStringLiteral("Request audit")},
            {QStringLiteral("vision"), QStringLiteral("Vision")},
            {QStringLiteral("route_assist"), QStringLiteral("Route assist")}};
        return labels.value(role, role);
    }
    static QString roleSetting(const QString &role, const QString &field) {
        return relay::RolesDialog::roleSetting(role, field);
    }
    // The `roles` object of configure / set_agent_options; empty when every role follows its default.
    // A role is either tiered (`roles/<id>/tier` = main|flash|lite) or pinned to an endpoint
    // (`roles/<id>/preset` plus an optional model), never both — protocol 13.7 rejects the pair.
    static QJsonObject rolesObject() {
        QSettings settings;
        QJsonObject roles;
        for (const QString &role : roleIds()) {
            const QString effort = settings.value(roleSetting(role, QStringLiteral("effort"))).toString();
            const QString tier = settings.value(roleSetting(role, QStringLiteral("tier"))).toString();
            if (relay::RolesDialog::tierIds().contains(tier)) {
                QJsonObject entry{{"tier", tier}};
                if (efforts().contains(effort)) entry.insert(QStringLiteral("effort"), effort);
                roles.insert(role, entry);
                continue;
            }
            const QString preset = settings.value(roleSetting(role, QStringLiteral("preset"))).toString();
            if (preset.isEmpty()) continue;   // follows its built-in tier
            QJsonObject entry{{"preset", preset}};
            const QString model = settings.value(roleSetting(role, QStringLiteral("model"))).toString().trimmed();
            if (!model.isEmpty()) entry.insert(QStringLiteral("model"), model);
            if (efforts().contains(effort)) entry.insert(QStringLiteral("effort"), effort);
            roles.insert(role, entry);
        }
        return roles;
    }
    // The `tiers` object: Flash/Lite overrides only. Main is the pane's own model.
    static QJsonObject tiersObject() {
        QSettings settings;
        QJsonObject tiers;
        for (const QString &tier : relay::RolesDialog::tierIds()) {
            if (tier == QStringLiteral("main")) continue;
            const QString preset = settings.value(relay::RolesDialog::tierSetting(tier, QStringLiteral("preset"))).toString();
            const QString model = settings.value(relay::RolesDialog::tierSetting(tier, QStringLiteral("model"))).toString().trimmed();
            if (preset.isEmpty() && model.isEmpty()) continue;
            QJsonObject entry;
            if (!preset.isEmpty()) entry.insert(QStringLiteral("preset"), preset);
            if (!model.isEmpty()) entry.insert(QStringLiteral("model"), model);
            const QString effort = settings.value(relay::RolesDialog::tierSetting(tier, QStringLiteral("effort"))).toString();
            if (efforts().contains(effort)) entry.insert(QStringLiteral("effort"), effort);
            if (!entry.isEmpty()) tiers.insert(tier, entry);
        }
        return tiers;
    }
    // Off by default (owner report, 2026-09-18: "it keeps changing from glm 5.3 to glm 5.3 flash").
    // A pane that quietly answers on a smaller model than the one the window says it is using is a
    // surprise, not a saving; the fast agent is a choice per pane (the model chip, or the toggle in
    // Settings › Agent) rather than what every pane after the first does by itself.
    static bool newPanesUseFastAgent() {
        return QSettings().value(QStringLiteral("agent/panes_fast"), false).toBool();
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
    // Live update after the roles modal changed something (applies to side calls and new subagents
    // at once). Both tables go together so the worker never resolves half a change.
    void rolesChanged() {
        if (m_configured)
            send({{"type", "set_agent_options"}, {"roles", rolesObject()}, {"tiers", tiersObject()}});
    }
    // Where a new pane starts: Settings › Agent › "Default input for new sessions" (auto by default).
    static QString defaultInputMode() {
        const QString value = QSettings().value(QStringLiteral("input/default"), QStringLiteral("auto")).toString();
        return (value == QStringLiteral("shell") || value == QStringLiteral("agent")) ? value : QStringLiteral("auto");
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
        // Three-way, in this order (owner, 2026-09-17): auto → terminal → agent → auto.
        const QString next = m_modeValue == QStringLiteral("auto")  ? QStringLiteral("shell")
                           : m_modeValue == QStringLiteral("shell") ? QStringLiteral("agent")
                                                                    : QStringLiteral("auto");
        setMode(next);
        toast(next == QStringLiteral("agent") ? QStringLiteral("Input: Agent · ! runs one line in the terminal")
              : next == QStringLiteral("shell") ? QStringLiteral("Input: Terminal · * sends one line to the agent")
                                                : QStringLiteral("Input: Auto detect"));
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

    // ===== screen-text input detection and agent-driven programs (cards YR21, C1HH) ==========
    // Only a backend that can read the screen can show the agent what a program is asking, so
    // everything below is gated on TerminalBackend::ScreenText. Relay's engine has it; the gate
    // stays because the pane must still say so honestly if a backend ever cannot.
    bool canShowAgentTheScreen() const {
        return m_backend && (m_backend->capabilities() & relay::TerminalBackend::ScreenText);
    }
    bool agentDriving() const { return m_delegated; }

    // Ctrl+Shift+J, the banner button and the palette. With text in the prompt box it hands the
    // program over *and* sends that request, so "answer it with y" is one keystroke away.
    void delegateProgram() {
        if (m_delegated) { takeOverFromAgent(); return; }
        const QString typed = m_editor ? m_editor->toPlainText().trimmed() : QString();
        if (!beginDelegation()) return;
        if (!typed.isEmpty()) submitAgent(typed, true);
    }

    // The user takes the program back: the agent stops typing and the keyboard is theirs.
    void takeOverFromAgent() {
        endDelegation(QStringLiteral("take_over"));
        takeControl();
    }

    // The screen the agent is shown: the bottom of the pane, blank rows trimmed, capped. Empty
    // on an engine that cannot read the screen — the agent is then told it cannot see it.
    QString screenSnapshot(int rows = 40) const {
        if (!canShowAgentTheScreen()) return {};
        QStringList lines = relay::screen::lastRows(m_backend->screenText(), rows);
        while (!lines.isEmpty() && lines.constLast().trimmed().isEmpty()) lines.removeLast();
        QString text = lines.join(QLatin1Char('\n'));
        if (text.size() > kScreenSnapshotChars) text = QStringLiteral("…\n") + text.right(kScreenSnapshotChars);
        return text;
    }

private:
    // Keystrokes the agent may send into one program in one turn. The worker enforces it too.
    static constexpr int kMaxProgramWrites = 20;
    static constexpr int kScreenSnapshotChars = 8000;

    relay::screen::Signals screenSignals() const {
        relay::screen::Signals sig;
        sig.mode = terminalMode();
        sig.programRunning = processBusy();
        sig.programReading = m_waiting;
        sig.altScreen = m_altScreen;
        sig.screenReadable = canShowAgentTheScreen();
        return sig;
    }

    // Re-read the last rows and tell the rest of the pane (and the worker) what they say.
    void updateScreenPrompt() {
        relay::screen::Detection next;
        if (m_backend && !m_promptReported && !m_native)
            next = relay::screen::detect(canShowAgentTheScreen()
                                             ? relay::screen::lastRows(m_backend->screenText())
                                             : QStringList(),
                                         screenSignals());
        const bool moved = next.kind != m_screenPrompt.kind || next.question != m_screenPrompt.question
                           || next.masked != m_screenPrompt.masked
                           || next.actionable() != m_screenPrompt.actionable();
        m_screenPrompt = next;
        if (moved) { updateTakeControl(); refreshProgramHint(); }
        sendProgramState();
    }

    bool beginDelegation() {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return false; }
        if (!processBusy()) { status(QStringLiteral("Nothing is running in this pane to hand over.")); return false; }
        if (!canShowAgentTheScreen()) {
            status(QStringLiteral("This pane cannot let Relay read the screen, so the agent cannot see the "
                                  "program."));
            return false;
        }
        if (m_secretMode || m_screenPrompt.masked) {
            status(QStringLiteral("Relay never lets the agent type into a password prompt."));
            return false;
        }
        m_delegated = true;
        m_delegatedProgram = foregroundProgramName();
        m_delegationEnd.clear();
        m_agentWrites = 0;
        const QString who = m_delegatedProgram.isEmpty() ? QStringLiteral("the program") : m_delegatedProgram;
        const QString back = Keymap::instance().shortcutText(QStringLiteral("control.human"));
        ensureLineStart();
        printInline(QStringLiteral("✦ %1 handed to the agent · %2 takes it back\n").arg(who, back), Ink::Note);
        toast(QStringLiteral("The agent may type into %1 · %2 takes it back").arg(who, back));
        sendProgramState(); updateTakeControl(); refreshProgramHint(); changed();
        return true;
    }

    // Every way a delegation ends: the user took control, a password prompt appeared, or the
    // program exited. Safe to call when nothing is delegated.
    void endDelegation(const QString &reason) {
        if (!m_delegated) return;
        m_delegated = false;
        m_delegationEnd = reason;   // the worker tells the agent why, in its own words
        const QString who = m_delegatedProgram.isEmpty() ? QStringLiteral("the program") : m_delegatedProgram;
        m_delegatedProgram.clear();
        ensureLineStart();
        printInline(reason == QStringLiteral("program_exited")
                        ? QStringLiteral("✦ %1 exited · the agent no longer drives this pane\n").arg(who)
                    : reason == QStringLiteral("password")
                        ? QStringLiteral("✦ %1 is asking for a password · the agent stopped typing\n").arg(who)
                        : QStringLiteral("✦ you took %1 back from the agent\n").arg(who),
                    Ink::Note);
        sendProgramState(); updateTakeControl(); refreshProgramHint(); changed();
    }

    // What the worker is told about this pane. Sent whenever it changes, so a take-over reaches
    // a running turn before its next model call (docs/AGENT-SESSIONS-PROTOCOL.md section 17).
    QJsonObject programStateMessage() const {
        const bool masked = m_secretMode || m_screenPrompt.masked;
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("program_state")},
            {QStringLiteral("granted"), m_delegated && processBusy() && !m_native && !masked},
            {QStringLiteral("reason"), m_delegated ? QStringLiteral("delegated") : m_delegationEnd},
            {QStringLiteral("program"), foregroundProgramName()},
            {QStringLiteral("kind"), QString::fromLatin1(relay::screen::kindName(m_screenPrompt.kind))},
            {QStringLiteral("question"), m_screenPrompt.question},
            {QStringLiteral("masked"), masked},
            {QStringLiteral("alt_screen"), m_altScreen},
            {QStringLiteral("waiting"), m_screenPrompt.actionable() || m_waiting},
            {QStringLiteral("max_writes"), kMaxProgramWrites},
            {QStringLiteral("screen_source"), canShowAgentTheScreen() ? QStringLiteral("engine") : QStringLiteral("none")}};
    }

    void sendProgramState() {
        if (!m_configured) return;
        const QJsonObject message = programStateMessage();
        if (message == m_lastProgramState) return;
        m_lastProgramState = message;
        send(message);
    }

    // The grant that rides on one prompt. Without it the worker does not offer the tool at all,
    // and the screen is not sent: nothing of what is on the user's terminal leaves the machine
    // unless they handed the program over.
    QJsonObject programGrant() const {
        if (!m_delegated || !processBusy() || m_native || m_secretMode || m_screenPrompt.masked) return {};
        QJsonObject grant = programStateMessage();
        grant.remove(QStringLiteral("type"));
        grant.insert(QStringLiteral("granted"), true);
        grant.insert(QStringLiteral("screen"), screenSnapshot());
        return grant;
    }

    // Named keys the agent may press; the model never sends a raw escape or control byte.
    static QByteArray programKeyBytes(const QString &key) {
        static const QHash<QString, QByteArray> keys{
            {QStringLiteral("enter"), QByteArrayLiteral("\r")},
            {QStringLiteral("escape"), QByteArrayLiteral("\x1b")},
            {QStringLiteral("tab"), QByteArrayLiteral("\t")},
            {QStringLiteral("backspace"), QByteArrayLiteral("\x7f")},
            {QStringLiteral("up"), QByteArrayLiteral("\x1b[A")},
            {QStringLiteral("down"), QByteArrayLiteral("\x1b[B")},
            {QStringLiteral("right"), QByteArrayLiteral("\x1b[C")},
            {QStringLiteral("left"), QByteArrayLiteral("\x1b[D")},
            {QStringLiteral("home"), QByteArrayLiteral("\x1b[H")},
            {QStringLiteral("end"), QByteArrayLiteral("\x1b[F")},
            {QStringLiteral("page-up"), QByteArrayLiteral("\x1b[5~")},
            {QStringLiteral("page-down"), QByteArrayLiteral("\x1b[6~")},
            {QStringLiteral("ctrl-c"), QByteArrayLiteral("\x03")},
            {QStringLiteral("ctrl-d"), QByteArrayLiteral("\x04")},
            {QStringLiteral("ctrl-z"), QByteArrayLiteral("\x1a")}};
        return keys.value(key);
    }

    static QString refusalCode(relay::input::TypeRefusal refusal) {
        switch (refusal) {
        case relay::input::TypeRefusal::UserInControl: return QStringLiteral("taken_over");
        case relay::input::TypeRefusal::NotAsked: return QStringLiteral("not_granted");
        case relay::input::TypeRefusal::NoProgram: return QStringLiteral("no_program");
        case relay::input::TypeRefusal::Password: return QStringLiteral("password");
        case relay::input::TypeRefusal::None: break;
        }
        return QStringLiteral("failed");
    }

    bool handleProgramEvent(const QString &type, const QJsonObject &event) {
        if (type == QStringLiteral("program_input")) { performProgramInput(event); return true; }
        if (type == QStringLiteral("program_input_refused")) {
            ensureLineStart();
            printInline(QStringLiteral("✦ not typed: ") + event.value(QStringLiteral("error")).toString() + '\n', Ink::Error);
            return true;
        }
        if (type == QStringLiteral("program_control")) return true;   // an ack; nothing to show
        return false;
    }

    // The agent asked to type something. The rules are checked again here, at the instant of the
    // write, because only the pane knows whether the user has since taken control or a password
    // prompt has appeared. Every write is printed in the pane.
    void performProgramInput(const QJsonObject &event) {
        const QString id = event.value(QStringLiteral("id")).toString();
        const QString intent = event.value(QStringLiteral("intent")).toString();
        const QString program = foregroundProgramName();
        updateScreenPrompt();
        const relay::input::TypeRefusal refusal =
            !m_backend ? relay::input::TypeRefusal::NoProgram
                       : relay::input::agentTypeRefusal(inputState(), m_delegated);
        if (refusal != relay::input::TypeRefusal::None) {
            ensureLineStart();
            printInline(QStringLiteral("✦ not typed (%1)%2\n")
                            .arg(refusalCode(refusal), intent.isEmpty() ? QString() : QStringLiteral(" · ") + intent),
                        Ink::Error);
            send({{QStringLiteral("type"), QStringLiteral("program_input_result")}, {QStringLiteral("id"), id},
                  {QStringLiteral("ok"), false}, {QStringLiteral("code"), refusalCode(refusal)},
                  {QStringLiteral("error"), relay::input::typeRefusalText(refusal, program)}});
            return;
        }
        QString typed;
        const QString key = event.value(QStringLiteral("key")).toString();
        if (!key.isEmpty()) {
            const QByteArray bytes = programKeyBytes(key);
            if (bytes.isEmpty()) {
                send({{QStringLiteral("type"), QStringLiteral("program_input_result")}, {QStringLiteral("id"), id},
                      {QStringLiteral("ok"), false}, {QStringLiteral("code"), QStringLiteral("failed")},
                      {QStringLiteral("error"), QStringLiteral("Relay does not know the key \"%1\".").arg(key)}});
                return;
            }
            m_backend->sendInput(bytes);
            typed = QStringLiteral("<%1>").arg(key);
        } else {
            const QString text = event.value(QStringLiteral("text")).toString();
            typed = text + (event.value(QStringLiteral("submit")).toBool(true) ? QStringLiteral("\n") : QString());
            sendShellInput(typed);
        }
        ++m_agentWrites;
        ensureLineStart();
        printInline(relay::input::typedLine(typed)
                        + (intent.isEmpty() ? QString() : QStringLiteral("   · ") + intent) + '\n', Ink::Agent);
        m_waitTicks = 0;
        endWaiting(false);
        updateTakeControl();
        // Let the program react before answering: the screen it leaves behind is the only honest
        // evidence of what the keystroke did, and it is what the agent reads next.
        QTimer::singleShot(400, this, [this, id, typed] {
            updateScreenPrompt();
            send({{QStringLiteral("type"), QStringLiteral("program_input_result")}, {QStringLiteral("id"), id},
                  {QStringLiteral("ok"), true}, {QStringLiteral("typed"), typed},
                  {QStringLiteral("program"), foregroundProgramName()},
                  {QStringLiteral("masked"), m_secretMode || m_screenPrompt.masked},
                  {QStringLiteral("waiting"), m_screenPrompt.actionable() || m_waiting},
                  {QStringLiteral("question"), m_screenPrompt.question},
                  {QStringLiteral("screen"), screenSnapshot()}});
        });
    }

public:
    // ===== end screen detection and agent-driven programs ====================================

    bool ownsComposerWidget(QWidget *widget) const { return m_composer && widget && (widget == m_composer || m_composer->isAncestorOf(widget)); }

    // Whether the keyboard is in this pane. The pane's event filter sits on qApp, so every pane
    // sees every key event; voice push-to-talk must only act in the one being typed in.
    bool ownsKeyboard() const {
        QWidget *focus = QApplication::focusWidget();
        return focus && (focus == this || isAncestorOf(focus));
    }

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

    // ----- links in the output (issues YZTK and GWXM) ---------------------------------------
    // Where a clicked or keyboard-selected link goes. `fromMouse` teaches the keyboard path.
    void openOutputTarget(const QString &target, int line, bool fromMouse) {
        if (target.isEmpty()) return;
        if (target.startsWith(QStringLiteral("relay://"))) {
            const QUrl url(target);
            const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
            if (url.host() == QStringLiteral("continue")) { continueTurn(true); return; }
            if (url.host() == QStringLiteral("turn") && parts.size() == 2 && onOpenTurn) {
                onOpenTurn(QUrl::fromPercentEncoding(parts.at(1).toUtf8()));
                return;
            }
            // A `#K7Q2` in the output (Switchboard design section 5): the Switchboard opens in
            // this tab if it is not there yet, and the card opens in it.
            if (url.host() == QStringLiteral("card") && parts.size() == 1 && onOpenCard) {
                onOpenCard(parts.at(0).toUpper());
                return;
            }
            return;
        }
        if (target.contains(QStringLiteral("://")) || target.startsWith(QStringLiteral("mailto:"))) {
            QDesktopServices::openUrl(QUrl(target));
            return;
        }
        if (!QFileInfo::exists(target)) { status(QStringLiteral("No such file or folder: ") + target); return; }
        if (fromMouse)
            hint(QStringLiteral("links.step"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("links.step")),
                                                QStringLiteral("step through the links in the output")));
        if (onOpenPath) onOpenPath(target, line > 0 ? line : 0);
    }
    bool canWalkOutputLinks() const { return terminalCan(relay::TerminalBackend::LinkWalk); }
    bool outputLinkWalkActive() const { return m_backend && m_backend->linkWalkActive(); }
    // Ctrl+Shift+L and the arrows: highlight the previous (-1) or next (+1) link.
    void stepOutputLink(int delta) {
        if (!m_backend) return;
        if (!canWalkOutputLinks()) {
            status(QStringLiteral("This pane's engine cannot read the screen; start a pane with the Relay engine to step through links."));
            return;
        }
        relay::TerminalBackend::Link link;
        int index = 0, count = 0;
        if (!m_backend->stepLink(delta, &link, &index, &count)) {
            m_walkLink = {};
            status(QStringLiteral("No files, folders, links or cards in this pane's output."));
            return;
        }
        m_walkLink = link;
        const QString where = !link.card.isEmpty() ? cardReferenceLabel(link.card, link.cardTitle)
            : link.line > 0 ? QStringLiteral("%1:%2").arg(link.target).arg(link.line)
                            : link.target;
        status(QStringLiteral("%1 of %2 · %3 · Enter opens, Esc leaves").arg(index + 1).arg(count).arg(where));
    }
    void openOutputLink() {
        const relay::TerminalBackend::Link link = m_walkLink;
        endOutputLinkWalk();
        openOutputTarget(link.target, link.line, false);
    }
    void endOutputLinkWalk() {
        m_walkLink = {};
        if (m_backend) m_backend->endLinkWalk();
    }
    // Only the active pane opens a link on a plain click; in any other pane the first click
    // moves the focus and Ctrl+click still follows the link.
    void setLinkClicksArmed(bool armed) { if (m_backend) m_backend->setPlainClickOpensLinks(armed); }
    int findInTerminal(const QString &text, bool backwards) { return m_backend ? m_backend->find(text, backwards) : 0; }
    // Clearing behind Readline's back (write \x1b[2J to the display, then ask for a redraw)
    // leaves the shell one line out of step: Readline still believes its prompt is where it drew
    // it, so the repaint is a no-op and the screen ends up blank with no prompt. Ctrl+L is
    // Readline's own clear-screen, so it wipes the screen and repaints the prompt itself, and its
    // idea of the cursor stays true. The scrollback is dropped separately.
    // Falls back to the raw clear when a program owns the screen, where Ctrl+L belongs to it.
    void clearTerminal() {
        if (!m_backend) return;
        closeInline();
        if (shellIdleAtPrompt()) {
            m_backend->clearScrollback();
            m_backend->sendInput(QByteArrayLiteral("\x0c"));
        } else {
            m_backend->clear();
        }
    }
    QString engineLabel() const {
        return m_engineCore.isEmpty() ? QStringLiteral("Relay engine")
                                      : QStringLiteral("Relay engine (%1)").arg(m_engineCore);
    }
    bool runCommand(const QString &command) { return runInTerminal(command, false, 0); }
    void sendKeybindings() { if (m_configured) send(QJsonObject{{"type", "keybindings"}, {"path", Keymap::instance().path()}, {"actions", Keymap::instance().catalog().value(QStringLiteral("actions"))}}); }
    void importWarpKeys() { send({{"type", "import_warp"}}); }

    // "Navigate here" in the explorer's right-click menu: change this pane's shell into `path`.
    // A quoted cd is run like any other Relay command, so the shell (and its prompt) follow.
    bool changeDirectory(const QString &path) {
        if (!QFileInfo(path).isDir()) return false;
        return runCommand(QStringLiteral("cd '") + QString(path).replace('\'', QStringLiteral("'\\''")) + '\'');
    }

    // "Set as agent workspace" in the explorer's right-click menu. The agent's file tools are
    // restricted to this folder; re-configuring starts a new conversation, so the caller asks first.
    void setAgentWorkspace(const QString &path) {
        const QFileInfo info(path);
        if (!info.isDir()) return;
        m_workspace = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
        status(QStringLiteral("Agent workspace: ") + m_workspace);
        if (!m_currentPreset.isEmpty()) configurePreset(m_currentPreset, false);
        updatePaths();
        changed();
    }

    // ----- the terminal pane's right-click menu (issue #X2F1) -------------------------------------
    //
    // Built from relay::terminalContextMenu(): Relay's own entries, the terminal items worth
    // having, and the pane actions. `global` is where the click landed, used
    // to find a link or a path under the pointer.
    void showTerminalMenu(const QPoint &global) {
        relay::TerminalMenuState state;
        state.hasTurn = !m_lastTurnId.isEmpty();
        state.canTakeControl = m_backend != nullptr;
        state.canSearch = m_backend != nullptr;   // Relay's find bar searches the pane either way
        state.canReadOutput = terminalCan(relay::TerminalBackend::ScreenText) || terminalCan(relay::TerminalBackend::Scrollback);
        state.canInject = terminalCan(relay::TerminalBackend::DisplayInjection);
        state.canZoom = terminalCan(relay::TerminalBackend::FontZoom);
        if (m_backend) {
            state.hasSelection = !m_backend->selectedText().isEmpty();
            int line = -1, column = -1;
            // The click arrives in whichever child widget the engine put under the pointer; the
            // backend wants it in widget()'s own coordinates, so it is mapped through the screen.
            const QPoint at = m_terminal ? m_terminal->mapFromGlobal(global) : QPoint();
            const QString target = m_terminal ? m_backend->linkAt(at, &line, &column) : QString();
            // A card reference resolves to relay://card/<id> (src/OutputLinks.*), so it is read
            // back out of the target the way a URL or a path is; it is never also a file.
            state.cardId = relay::links::cardIdOf(target);
            if (!state.cardId.isEmpty()) {
                if (const relay::board::Card *card = m_cardIndex.card(state.cardId)) state.cardTitle = card->title;
            } else if (target.startsWith(QStringLiteral("http://")) || target.startsWith(QStringLiteral("https://"))
                || target.startsWith(QStringLiteral("mailto:")) || target.startsWith(QStringLiteral("file://")))
                state.link = target;
            else if (!target.isEmpty() && QFileInfo::exists(target)) {
                state.filePath = target;
                m_menuFileLine = line;
            }
            Q_UNUSED(column);
        }
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        for (const relay::TerminalMenuItem &item : relay::terminalContextMenu(state)) {
            if (item.isSeparator()) { menu->addSeparator(); continue; }
            QAction *action = menu->addAction(item.label);
            action->setEnabled(item.enabled);
            if (const QString keys = terminalMenuShortcut(item.id); !keys.isEmpty())
                action->setShortcut(QKeySequence(keys));
            const QString id = item.id;
            const QString link = state.link, file = state.filePath, card = state.cardId;
            connect(action, &QAction::triggered, this, [this, id, link, file, card] { runTerminalMenuAction(id, link, file, card); });
        }
        menu->popup(global);
    }

    static QString terminalMenuShortcut(const QString &id) {
        static const QHash<QString, QString> actions{
            {QStringLiteral("turn"), QString()},
            {QStringLiteral("takeControl"), QStringLiteral("control.human")},
            {QStringLiteral("tasks"), QStringLiteral("agent.requests")},
            {QStringLiteral("find"), QStringLiteral("find.inView")},
            {QStringLiteral("splitRight"), QStringLiteral("pane.splitRight")},
            {QStringLiteral("splitDown"), QStringLiteral("pane.splitDown")},
            {QStringLiteral("close"), QStringLiteral("pane.close")},
        };
        const QString action = actions.value(id);
        return action.isEmpty() ? QString() : Keymap::instance().keysFor(action).value(0);
    }

    void runTerminalMenuAction(const QString &id, const QString &link, const QString &file, const QString &card) {
        if (id == QStringLiteral("turn")) { if (onOpenTurn) onOpenTurn(m_lastTurnId); return; }
        if (id == QStringLiteral("takeControl")) { takeControl(); return; }
        if (id == QStringLiteral("tasks")) { toggleRequests(); return; }
        if (id == QStringLiteral("find")) { openFindInView(); return; }
        if (id == QStringLiteral("openLink")) { QDesktopServices::openUrl(QUrl(link)); return; }
        if (id == QStringLiteral("copyLink")) { QApplication::clipboard()->setText(link); return; }
        if (id == QStringLiteral("openFile")) { if (onOpenPath) onOpenPath(file, m_menuFileLine); return; }
        // A `#K7Q2` under the pointer: open the card, copy the reference, or put it in the prompt
        // box — the same three the Switchboard's card detail offers.
        if (id == QStringLiteral("openCard")) { if (onOpenCard) onOpenCard(card); return; }
        if (id == QStringLiteral("copyCard")) {
            QApplication::clipboard()->setText(QStringLiteral("#") + card);
            status(QStringLiteral("Copied #") + card);
            return;
        }
        if (id == QStringLiteral("cardToPrompt")) { insertInComposer(QStringLiteral("#") + card + ' '); return; }
        if (!m_backend) return;
        if (id == QStringLiteral("copy")) { if (!copySelection()) status(QStringLiteral("Nothing is selected.")); return; }
        if (id == QStringLiteral("paste")) { m_backend->paste(); return; }
        if (id == QStringLiteral("selectAll")) { m_backend->selectAll(); return; }
        if (id == QStringLiteral("clearScrollback")) { m_backend->clearScrollback(); return; }
        if (id == QStringLiteral("reset")) {
            // Clear the history, then reset the emulator itself (RIS) and redraw the prompt.
            closeInline();
            m_backend->clearScrollback();
            m_backend->writeToDisplay(QByteArrayLiteral("\x1b""c"));
            if (shellIdleAtPrompt()) m_backend->redrawPrompt();
            return;
        }
        if (id == QStringLiteral("saveOutput")) { saveTerminalOutput(); return; }
        if (id.startsWith(QStringLiteral("zoom"))) {
            const int step = id == QStringLiteral("zoomIn") ? 1 : id == QStringLiteral("zoomOut") ? -1 : 0;
            if (!m_backend->zoom(step)) status(QStringLiteral("This engine cannot change its font size."));
            return;
        }
        if (onWindowAction) onWindowAction(id);   // splitRight, splitDown, close
    }

    // "Save output as…": the scrollback and the visible screen as plain text.
    void saveTerminalOutput() {
        if (!m_backend) return;
        const QString suggestion = QDir(m_cwd).filePath(QStringLiteral("relay-output-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".txt"));
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save output as"), suggestion,
                                                          QStringLiteral("Text files (*.txt);;All files (*)"));
        if (path.isEmpty()) return;
        QStringList lines = terminalCan(relay::TerminalBackend::Scrollback) ? m_backend->scrollbackText(200000) : QStringList();
        if (terminalCan(relay::TerminalBackend::ScreenText)) lines += m_backend->screenText().split('\n');
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(lines.join('\n').toUtf8() + '\n') < 0 || !out.commit()) {
            status(QStringLiteral("Could not write ") + path);
            return;
        }
        status(QStringLiteral("Saved ") + path);
    }

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
        // An agent-bound action the user took, so it gets the agent echo: violet, and the ✦ glyph
        // the site and BoardPane already use for agent lines (› is the shell glyph).
        ensureLineStart();
        printInline(QStringLiteral("✦ Execute the plan %1%2\n").arg(QFileInfo(path).fileName(), fresh ? QStringLiteral(" (fresh context)") : QString()), Ink::UserAgent);
        focusInput();
    }
    void keepPlanning() {
        focusInput();
        toast(QStringLiteral("Still planning · tell the agent what to change"));
    }

    // Settings changed in Actions › Agent options.
    void agentOptionsChanged(const QString &key) {
        // Voice settings are the GUI's own: the recorder, the chip and its tooltip, never the agent.
        if (key.startsWith(QStringLiteral("voice/"))) { updateVoiceChip(); return; }
        if (key == QStringLiteral("agent/max_auto_turns")) {
            if (m_configured) send({{"type", "set_agent_options"}, {"max_auto_turns", QSettings().value(QStringLiteral("agent/max_auto_turns"), 50).toInt()}});
            return;
        }
        // Model roles and tiers apply to side calls and new subagents at once (protocol 13).
        if (key.startsWith(QStringLiteral("roles/")) || key.startsWith(QStringLiteral("tiers/"))) {
            rolesChanged();
            return;
        }
        if (key == QStringLiteral("agent/panes_fast")) return;   // only affects panes opened later
        // Turn limits and the request audit apply to the running agent at once (protocol 12.1).
        if (key == QStringLiteral("agent/max_steps") || key == QStringLiteral("agent/max_tool_calls")
            || key == QStringLiteral("agent/stall_timeout_s") || key == QStringLiteral("agent/audit_requests")) {
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
        if (onOpenPath) onOpenPath(path, 0);
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
        updateTranscriptHeight();
        updatePaths();
    }

    bool eventFilter(QObject *object, QEvent *event) override {
        // Moving the pane by its header comes first: once a drag is under way it owns the mouse,
        // so the folder line below cannot open an explorer when the drag happens to end on it.
        if (headerDragEvent(object, event)) return true;
        // A toast that is up when the layout changes (the fix loop opening the agent transcript
        // resizes the terminal host) would strand over the composer; re-anchor it like the other
        // floating overlays (placeQueueStrip and friends in resizeEvent).
        if (object == m_terminalHost && event->type() == QEvent::Resize && m_toast && m_toast->isVisible())
            placeToast();
        if ((event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate) && object == window())
            noteWindowActivation(event->type() == QEvent::WindowActivate);
        // Pane title (issue JRWQ): double click names this pane by hand; Esc leaves it alone.
        if (object == m_titleLabel && event->type() == QEvent::MouseButtonDblClick) {
            beginRename();
            hint(QStringLiteral("pane.rename"), QStringLiteral("Next time: /rename <name> · /rename-tab names the tab"));
            return true;
        }
        if (object == m_titleEdit && event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            endRename();
            return true;
        }
        if (object == m_titleEdit && event->type() == QEvent::FocusOut) {
            endRename();
            return false;
        }
        // The folder line still opens the explorer on a click; headerDragEvent calls this when the
        // press and the release both land on it without a drag in between (issue #D60R).
        // Right-click anywhere in this pane's terminal: Relay's menu, not the engine's (issue
        // #X2F1). Real widgets inside the terminal, such as the engine's Find bar, keep theirs.
        //
        // The engine sends a proper context-menu event, and withholds it while a program is
        // reading the mouse, so that is the event to take.
        if (event->type() == QEvent::ContextMenu) {
            auto *widget = qobject_cast<QWidget *>(object);
            if (ownsTerminalWidget(widget) && !acceptsTypedInput(widget)) {
                showTerminalMenu(static_cast<QContextMenuEvent *>(event)->globalPos());
                return true;
            }
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
        // Voice push-to-talk. The filter is on qApp, so only the pane holding the keyboard acts,
        // and the event is never consumed: Right Alt is AltGr on most layouts and must keep typing.
        // F9 is the exception — it is not a modifier, so it is swallowed while voice uses it.
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) && ownsKeyboard()) {
            auto *key = static_cast<QKeyEvent *>(event);
            const QString hold = voiceHoldKey();
            const bool isVoiceKey = voiceEnabled()
                && relay::voice::isHoldKey(hold, key->key(), key->nativeVirtualKey(), key->nativeScanCode());
            if (isVoiceKey && !key->isAutoRepeat()) {
                if (event->type() == QEvent::KeyPress) voiceKeyPressed(); else voiceKeyReleased();
                if (hold == QStringLiteral("f9")) return true;
            } else if (!isVoiceKey && event->type() == QEvent::KeyPress && m_voiceHold) {
                voiceInterrupted();
            }
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
    // ----- moving the pane by its header (owner, 2026-09-17) ---------------------------------
    // Press anywhere on the header — the title, the "auto" badge, the folder line, the gap
    // between them — and drag: the pane travels, exactly as it does from the chrome's ⠿ grip.
    // Drop it on another pane's edge to split that pane, or on the tab bar to give it a tab of
    // its own; Esc puts it back.
    //
    // A press is not taken here, only watched: anything shorter than the platform's drag distance
    // is still an ordinary click, so double click still renames and the folder line still opens
    // the explorer. This filter runs on qApp (so the mouse is followed wherever it goes during a
    // drag), hence the check that the press really started on *this* pane's header.
    bool headerDragEvent(QObject *object, QEvent *event) {
        const QEvent::Type type = event->type();
        if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove
            && type != QEvent::MouseButtonRelease && type != QEvent::KeyPress) return false;
        switch (type) {
        case QEvent::MouseButtonPress: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton || !onHeader(object)) return false;
            if (m_titleEdit && m_titleEdit->isVisible()) return false;   // renaming: the mouse is the caret's
            // One press arrives here more than once: the label under the mouse ignores it, and Qt
            // re-sends it to the header behind, through this same application-wide filter. The
            // first arrival is the innermost widget, which is the one the click belongs to.
            if (!m_headerPressed) {
                m_headerPressAt = mouse->globalPos();
                m_headerPressOn = qobject_cast<QWidget *>(object);
                m_headerPressed = true;
                m_headerDragging = false;
            }
            return false;
        }
        case QEvent::MouseMove: {
            if (!m_headerPressed) return false;
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (!m_headerDragging) {
                if ((mouse->globalPos() - m_headerPressAt).manhattanLength() < QApplication::startDragDistance()) return false;
                m_headerDragging = true;
                QApplication::setOverrideCursor(Qt::ClosedHandCursor);
            }
            if (onHeaderDragMove) onHeaderDragMove(mouse->globalPos());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            if (!m_headerPressed) return false;
            m_headerPressed = false;
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (m_headerDragging) { endHeaderDrag(mouse->globalPos(), true); return true; }
            // A click, not a drag. The folder line is the one part of the header that acts on one:
            // it opens the explorer, and closes it again when that is already this folder (#D60R).
            // Handled here because the press is what decides who gets the release, and since the
            // header became a drag handle that is no longer the label itself.
            if (m_headerPressOn == m_cwdLabel && m_cwdLabel
                && m_cwdLabel->rect().contains(m_cwdLabel->mapFromGlobal(mouse->globalPos()))) {
                if (onToggleExplorer) onToggleExplorer(m_cwd);
                else if (onOpenPath) onOpenPath(m_cwd, 0);
                hint(QStringLiteral("files.explorer.mouse"),
                     relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("files.explorer")),
                                                    QStringLiteral("the file explorer")));
                return true;
            }
            return false;
        }
        case QEvent::KeyPress:
            if (m_headerDragging && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                m_headerPressed = false;
                endHeaderDrag(QCursor::pos(), false);
                return true;
            }
            return false;
        default: break;
        }
        return false;
    }

    bool onHeader(QObject *object) const {
        auto *widget = qobject_cast<QWidget *>(object);
        for (; widget; widget = widget->parentWidget()) {
            if (widget == m_headerWidget) return true;
            if (widget == this) return false;
        }
        return false;
    }

    void endHeaderDrag(const QPoint &global, bool drop) {
        if (!m_headerDragging) return;
        m_headerDragging = false;
        QApplication::restoreOverrideCursor();
        if (onHeaderDragEnd) onHeaderDragEnd(global, drop);
    }

    void buildUi() {
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(8, 6, 8, 8); layout->setSpacing(6);
        // Pane header (issue JRWQ): what this pane is doing, written by the model and refreshed as
        // the work moves on; the directory keeps its place on the right, smaller and dim. Double
        // click the title to name the pane by hand (/rename does the same without the mouse).
        auto *header = new QWidget;
        header->setObjectName(QStringLiteral("paneHeader"));
        // The whole row is the pane's drag handle (headerDragEvent), so it says so with the cursor.
        header->setCursor(Qt::OpenHandCursor);
        auto *headerRow = new QHBoxLayout(header);
        m_headerLayout = headerRow;
        headerRow->setContentsMargins(0, 0, 0, 0);
        headerRow->setSpacing(8);
        m_titleLabel = new QLabel; m_titleLabel->setTextFormat(Qt::PlainText);
        m_titleLabel->setObjectName(QStringLiteral("paneTitle"));
        // No caret here: the title is dragged far more often than it is renamed, so it inherits
        // the header's open hand. Double click still renames it, as the tooltip says.
        // The text is elided in updateHeader(), so the label asks for exactly what it shows.
        m_titleLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        m_titleLabel->installEventFilter(this);
        m_titleEdit = new QLineEdit;
        m_titleEdit->setObjectName(QStringLiteral("paneTitleEdit"));
        m_titleEdit->setVisible(false);
        m_titleEdit->setMaxLength(relay::titles::kMaxUserTitle);
        m_titleEdit->setPlaceholderText(QStringLiteral("Name this pane — empty goes back to the model's name"));
        m_titleEdit->setMinimumWidth(220);
        m_titleEdit->installEventFilter(this);
        connect(m_titleEdit, &QLineEdit::returnPressed, this, [this] { commitRename(); });
        m_titleAuto = new QLabel(QStringLiteral("auto"));
        m_titleAuto->setObjectName(QStringLiteral("paneAuto"));
        m_titleAuto->setVisible(false);
        m_cwdLabel = new QLabel; m_cwdLabel->setTextFormat(Qt::PlainText);
        m_cwdLabel->setObjectName(QStringLiteral("paneCwd"));
        // Clicking the directory line opens it in the explorer pane.
        m_cwdLabel->setCursor(Qt::PointingHandCursor);
        m_cwdLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        // A QLabel's minimum is its whole text. With the long "TERMINAL <path> │ AGENT
        // WORKSPACE <path>" form that made the pane refuse to go under ~1000 px, so a pane
        // opened beside it (the Switchboard, 2026-09-17) got a third of the window instead of
        // half. Clipped from the left instead; the tooltip has the full paths.
        m_cwdLabel->setMinimumWidth(1);
        m_cwdLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_cwdLabel->installEventFilter(this);
        // Not selectable any more: dragging across the path is how you move the pane now, and a
        // half-selected path is a poor trade for that. The tooltip still has both paths in full.
        headerRow->addWidget(m_titleLabel, 0);
        headerRow->addWidget(m_titleEdit, 1);
        headerRow->addWidget(m_titleAuto, 0);
        headerRow->addStretch(1);
        headerRow->addWidget(m_cwdLabel, 0);
        m_headerWidget = header;
        layout->addWidget(header);
        m_terminalHost = new QWidget;
        auto *terminalLayout = new QVBoxLayout(m_terminalHost); terminalLayout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_terminalHost, 1);
        m_terminalHost->installEventFilter(this);   // the toast follows the terminal host's corner
        auto *composer = new QFrame; composer->setFrameShape(QFrame::StyledPanel);
        // The prompt box never sets the pane's minimum width. Its chip row is wider than a pane in
        // a three-pane split, and a splitter that cannot satisfy every minimum redistributes as
        // soon as one of them changes — which is what made taking control (Ctrl+H) shrink a pane
        // to almost nothing (#G152). Ignored means the row is squeezed instead, as it already is
        // in a narrow pane; the pane's minimum width stays the terminal's.
        composer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_composer = composer;
        auto *composerLayout = new QVBoxLayout(composer);
        auto *routeRow = new QHBoxLayout;
        // The strip under the prompt box (owner design, 2026-09-17): directory, Switchboard and
        // tasks on the left, context left / model / microphone on the right, each in a Warp-style
        // chip. Where the line goes is not in the strip: the mode chip sits in the prompt box's
        // top-right corner (owner, 2026-09-17), on the text it routes, with the `!` / `*` and
        // password chips that qualify it. The corner is a column of its own, so text wraps before
        // it rather than running underneath.
        auto *corner = new QHBoxLayout;
        corner->setContentsMargins(0, 0, 0, 0);
        corner->setSpacing(6);
        m_cwdChip = new QToolButton;
        m_cwdChip->setObjectName(QStringLiteral("stripChip"));
        m_cwdChip->setFocusPolicy(Qt::NoFocus);
        m_cwdChip->setCursor(Qt::PointingHandCursor);
        connect(m_cwdChip, &QToolButton::clicked, this, [this] { if (onOpenPath) onOpenPath(m_cwd, 0); });
        routeRow->addWidget(m_cwdChip);
        m_modeChip = new QToolButton;
        m_modeChip->setObjectName(QStringLiteral("stripChip"));
        m_modeChip->setFocusPolicy(Qt::NoFocus);
        m_modeChip->setCursor(Qt::PointingHandCursor);
        m_modeChip->setPopupMode(QToolButton::InstantPopup);
        {
            auto *menu = new QMenu(m_modeChip);
            for (const auto &pair : {std::pair<const char *, const char *>{"auto", "auto detect"},
                                     {"shell", "terminal"}, {"agent", "agent"}}) {
                const QString value = QString::fromLatin1(pair.first);
                menu->addAction(QString::fromLatin1(pair.second), this, [this, value] { setMode(value); focusInput(); });
            }
            m_modeChip->setMenu(menu);
        }
        setupWorkChip(routeRow);               // Switchboard: this pane's issues, tasks and plan
        // `!` / `*` typed first in an empty prompt: terminal / agent mode for this submission.
        m_prefixChip = new QLabel;
        m_prefixChip->setObjectName(QStringLiteral("prefixChip"));
        m_prefixChip->hide();
        corner->addWidget(m_prefixChip);
        // "password for sudo" while the prompt box is masked.
        m_secretChip = new QLabel;
        m_secretChip->setObjectName(QStringLiteral("secretChip"));
        m_secretChip->setTextFormat(Qt::PlainText);
        m_secretChip->setToolTip(QStringLiteral("The line is written to the program and never stored"));
        m_secretChip->hide();
        corner->addWidget(m_secretChip);
        corner->addWidget(m_modeChip);
        // The routing verdict has no chip of its own: it is the mode chip's tooltip. The label
        // survives only as the place that text and tooltip live, so it is parented to the composer
        // and never added to a layout or shown. A parentless QWidget that is shown becomes a
        // top-level window of its own: that is the tiny second window of #RDQ7.
        m_routeLabel = new QLabel(composer);
        m_routeLabel->hide();
        m_opaqueHint = new QLabel;
        m_opaqueHint->setObjectName(QStringLiteral("opaqueHint"));
        m_opaqueHint->hide();
        routeRow->addWidget(m_opaqueHint, 1);
        routeRow->addStretch(1);
        buildSessionControls(routeRow);        // plan chip and the context chip, next to the model
        m_modelBox = new QComboBox;
        m_modelBox->setObjectName(QStringLiteral("statusPicker"));
        m_modelBox->setAccessibleName(QStringLiteral("Agent model"));
        m_modelBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        m_modelBox->setFocusPolicy(Qt::TabFocus);
        connect(m_modelBox, qOverload<int>(&QComboBox::activated), this, [this](int index) {
            selectModel(m_modelBox->itemData(index).toString()); focusInput();
            hint(QStringLiteral("model.mouse"), QStringLiteral("Tip: /model switches models from the prompt box"));
        });
        routeRow->addWidget(m_modelBox);
        // Voice transcription: the chip toggles recording, the hold key is push-to-talk.
        m_mic = new QToolButton;
        m_mic->setObjectName(QStringLiteral("stripChip"));
        m_mic->setFocusPolicy(Qt::NoFocus);
        m_mic->setIcon(stripIcon(QStringLiteral("mic")));
        m_mic->setIconSize(QSize(14, 14));
        m_mic->setAccessibleName(QStringLiteral("Voice transcription"));
        connect(m_mic, &QToolButton::clicked, this, [this] { toggleVoice(false); });
        routeRow->addWidget(m_mic);
        updateVoiceChip();
        // Share this pane with a phone: the same strip as voice, because both are ways of
        // reaching the pane from somewhere other than this keyboard.
        m_share = new QToolButton;
        m_share->setObjectName(QStringLiteral("stripChip"));
        m_share->setFocusPolicy(Qt::NoFocus);
        m_share->setIcon(stripIcon(QStringLiteral("share")));
        m_share->setIconSize(QSize(14, 14));
        m_share->setAccessibleName(QStringLiteral("Share this pane with a phone"));
        connect(m_share, &QToolButton::clicked, this, [this] { toggleShare(); });
        routeRow->addWidget(m_share);
        updateShareChip();
        auto *cancel = new QToolButton;
        cancel->setObjectName(QStringLiteral("interruptButton"));
        const QString cancelIcon = relay::theme::themeDataDir() + QStringLiteral("/icons/cancel.svg");
        if (QFileInfo::exists(cancelIcon)) cancel->setIcon(QIcon(cancelIcon)); else cancel->setText(QStringLiteral("⊘"));
        cancel->setToolTip(QStringLiteral("Interrupt the running command (Esc in the prompt box)"));
        cancel->setAccessibleName(QStringLiteral("Interrupt shell"));
        cancel->setFocusPolicy(Qt::NoFocus);
        connect(cancel, &QToolButton::clicked, this, [this] { interruptShell(); });
        cancel->hide();
        m_interruptButton = cancel;
        routeRow->addWidget(cancel);
        refreshPickers();
        routeRow->setContentsMargins(2, 0, 2, 0);
        routeRow->setSpacing(6);
        m_editor = new RichEditor;
        m_highlighter = new relay::InputHighlighter(m_editor->document());
        QTimer::singleShot(0, this, [this] { refreshDestinationColor(); });
        m_editor->setAutoHeight(1, 8);   // one line when idle, growing with the text
        auto *inputRow = new QHBoxLayout;
        inputRow->setContentsMargins(0, 0, 0, 0);
        inputRow->setSpacing(6);
        auto *inputColumn = new QVBoxLayout;
        inputColumn->setContentsMargins(0, 0, 0, 0);
        inputColumn->addWidget(m_editor);
        inputRow->addLayout(inputColumn, 1);
        auto *cornerColumn = new QVBoxLayout;
        cornerColumn->setContentsMargins(0, 0, 0, 0);
        cornerColumn->addLayout(corner);
        cornerColumn->addStretch(1);
        inputRow->addLayout(cornerColumn);
        composerLayout->addLayout(inputRow);
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
        inputColumn->addWidget(m_secretEdit);
        composerLayout->addLayout(routeRow);
        // No key-hints row: Ctrl+? lists every shortcut, and the strip stays quiet.
        m_help = nullptr;
        buildTranscript();
        layout->addWidget(m_transcript);
        // The queue strip is a row of the pane's own column, directly under the terminal, and not
        // an overlay floating over it any more: it takes real layout space so the terminal host
        // shrinks and the shell reflows into what is left (owner report, 2026-09-18: "the terminal
        // needs to move up, rather than being covered up"). The width policy is the prompt box's,
        // and for the same reason: a queued line is wider than a pane in a three-pane split, and a
        // minimum that wide would move this pane's minimum and make the splitter redistribute
        // every pane in the row (#G152). placeQueueStrip() sets the height it asks for.
        m_queueStrip = new QFrame(this);
        m_queueStrip->setObjectName(QStringLiteral("queueStrip"));
        m_queueStrip->setAttribute(Qt::WA_StyledBackground);
        m_queueStrip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *queueLayout = new QVBoxLayout(m_queueStrip); queueLayout->setContentsMargins(10, 6, 6, 6); queueLayout->setSpacing(2);
        m_queueStrip->hide();
        layout->insertWidget(layout->indexOf(m_terminalHost) + 1, m_queueStrip);
        // A full-screen program (vim, htop) or an ssh session owns the screen. Relay no longer
        // switches to native input by itself; this button, or Ctrl+H, hands the keyboard over.
        // Both buttons live in one floating banner over the top of the terminal, next to the
        // line that says what the program is asking ("apt is asking: Do you want to continue?
        // [Y/n]"), so the question and the two ways to answer it are in one place.
        m_programBar = new QFrame(this);
        m_programBar->setObjectName(QStringLiteral("programBanner"));
        m_programBar->setAttribute(Qt::WA_StyledBackground);
        auto *bannerRow = new QHBoxLayout(m_programBar);
        bannerRow->setContentsMargins(10, 4, 6, 4);
        bannerRow->setSpacing(8);
        m_programLabel = new QLabel(m_programBar);
        m_programLabel->setObjectName(QStringLiteral("programBannerLabel"));
        m_programLabel->setTextFormat(Qt::PlainText);
        bannerRow->addWidget(m_programLabel, 1);
        // "Let the agent drive" hands the program over; while it drives, the same button is
        // "Take over" and gives the keyboard back (card C1HH).
        m_delegateButton = new QPushButton(m_programBar);
        m_delegateButton->setObjectName(QStringLiteral("delegateChip"));
        m_delegateButton->setCursor(Qt::PointingHandCursor);
        m_delegateButton->setFocusPolicy(Qt::NoFocus);
        connect(m_delegateButton, &QPushButton::clicked, this, [this] {
            const bool wasDriving = m_delegated;
            delegateProgram();
            const QString action = wasDriving ? QStringLiteral("control.human") : QStringLiteral("program.delegate");
            hint(QStringLiteral("program.delegate.mouse"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(action),
                                                wasDriving ? QStringLiteral("take the program back")
                                                           : QStringLiteral("hand the program to the agent")));
        });
        bannerRow->addWidget(m_delegateButton);
        m_takeControl = new QPushButton(m_programBar);
        m_takeControl->setObjectName(QStringLiteral("takeControlChip"));
        m_takeControl->setCursor(Qt::PointingHandCursor);
        m_takeControl->setFocusPolicy(Qt::NoFocus);
        connect(m_takeControl, &QPushButton::clicked, this, [this] {
            takeControl();
            hint(QStringLiteral("control.human.mouse"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("control.human")), QStringLiteral("take control")));
        });
        bannerRow->addWidget(m_takeControl);
        m_programBar->hide();
        layout->addWidget(composer);
        setupSubagentsUi(layout);   // subagents UI: running-agents list beneath the composer
        updatePaths();
    }

    // ----- agent sessions UI: helpers ----------------------------------------------------------
    static QIcon stripIcon(const QString &name) {
        const QString path = relay::theme::themeDataDir() + QStringLiteral("/icons/") + name + QStringLiteral(".svg");
        return QFileInfo::exists(path) ? QIcon(path) : QIcon();
    }

    void buildSessionControls(QHBoxLayout *row) {
        m_planChip = new QLabel(QStringLiteral("PLAN"));
        m_planChip->setObjectName(QStringLiteral("planChip"));
        m_planChip->setToolTip(QStringLiteral("Plan mode: the agent investigates and writes a plan (Shift+Tab to leave)"));
        m_planChip->hide();
        row->addWidget(m_planChip);
        m_ctxLabel = new QLabel;
        m_ctxLabel->setObjectName(QStringLiteral("stripChipLabel"));
        m_ctxLabel->setTextFormat(Qt::PlainText);
        m_ctxLabel->hide();
        row->addWidget(m_ctxLabel);
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
        m_effortBox->hide();   // owner, 2026-09-17: effort shows on the model chip's tooltip, not as a third picker
        row->addWidget(m_effortBox);
    }

    void refreshSessionControls() {
        if (m_modelBox) m_modelBox->setToolTip(modelTooltip());
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

    // A 12 px icon in front of a short label, so the strip reads as symbols with numbers.
    static QString iconText(const QString &icon, const QString &text) {
        const QString path = relay::theme::themeDataDir() + QStringLiteral("/icons/") + icon + QStringLiteral(".svg");
        if (!QFileInfo::exists(path)) return text;
        return QStringLiteral("<img src=\"%1\" width=\"12\" height=\"12\"> %2").arg(path, text.toHtmlEscaped());
    }

    void updateContextLabel() {
        if (!m_ctxLabel) return;
        if (m_ctxWindow <= 0) { m_ctxLabel->hide(); return; }
        if (m_compacting) {
            m_ctxLabel->setText(QStringLiteral("compacting…"));
        } else {
            const double left = std::max(0.0, 100.0 - m_ctxPercent);
            m_ctxLabel->setText(QStringLiteral("%1% left").arg(QString::number(left, 'f', left < 10 ? 1 : 0)));
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
                // Idle deadline for a streamed model call (protocol 15).
                {"stall_timeout_s", std::clamp(settings.value(QStringLiteral("agent/stall_timeout_s"), 60).toInt(), 1, 1800)},
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
        // Model roles (protocol 13): the tables, and the role this pane's own agent runs.
        const QJsonObject roles = rolesObject();
        if (!roles.isEmpty()) request.insert(QStringLiteral("roles"), roles);
        const QJsonObject tiers = tiersObject();
        if (!tiers.isEmpty()) request.insert(QStringLiteral("tiers"), tiers);
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
            if (m_editor->toPlainText() == text) setRouteText(QStringLiteral("%1 · local guess (model check unavailable%2) · ! or * to choose")
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
        setRouteText(QStringLiteral("%1 · guessed: %2 (%3%)").arg(m_assistRoute == QStringLiteral("shell") ? QStringLiteral("TERMINAL") : QStringLiteral("AGENT"),
                                  m_assistReason.isEmpty() ? QStringLiteral("model") : m_assistReason)
                                  .arg(qRound(m_assistConfidence * 100)));
        m_routeLabel->setToolTip(QStringLiteral("Local rules could not decide, so the agent model guessed.\nPrefix ! for the terminal or * for the agent to be explicit."));
    }

    // ----- thinking, turn summaries, tool outputs, routing assist, skills (protocol 11) --------
    static bool showThinking() { return QSettings().value(QStringLiteral("agent/show_thinking"), true).toBool(); }
    // Off by default: tool output collapses to its size, and the turn pane holds the whole thing.
    static bool showToolOutput() { return QSettings().value(QStringLiteral("agent/show_tool_output"), false).toBool(); }
    static constexpr int kInlineDiffLines = 8;   // of a write tool's diff, before "… N more lines"

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
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            const QString label = QStringLiteral("✦ thought for %1 s").arg(std::max<qint64>(1, (ms + 500) / 1000));
            if (turn.isEmpty()) printInline(label + '\n', Ink::Note);
            else printTurnLink(label, turn);   // the turn pane shows the reasoning in full
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
        if (type == QStringLiteral("key_tested") || type == QStringLiteral("key_removed")
            || type == QStringLiteral("agent_tools_imported")) {
            if (m_keysDialog) m_keysDialog->handleEvent(event);
            else if (type == QStringLiteral("key_tested"))
                status(event.value(QStringLiteral("ok")).toBool()
                           ? QStringLiteral("Key works for ") + event.value(QStringLiteral("preset")).toString()
                           : QStringLiteral("Key test failed: ") + event.value(QStringLiteral("error")).toString());
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

    // Reasoning streams into a panel between the terminal and the queue strip. It is a row of the
    // pane's column, not an overlay: it takes real layout space, so the terminal host above it
    // shrinks and the shell reflows into what is left instead of losing its last lines behind the
    // panel (owner report, 2026-09-18: "the terminal needs to move up, rather than being covered
    // up"). placeThinking() sets the height, and every show or hide runs through keepPaneSizes().
    void appendThinking(const QString &text) {
        if (text.isEmpty()) return;
        if (!m_thinking) {
            m_thinking = new QFrame(this);
            m_thinking->setObjectName(QStringLiteral("thinkingOverlay"));
            m_thinking->setAttribute(Qt::WA_StyledBackground);
            // Ignored width for the same reason as the prompt box's: a line of reasoning is wider
            // than a pane in a three-pane split, and a minimum that wide would move this pane's
            // minimum and make the splitter redistribute the whole row (#G152).
            m_thinking->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            auto *box = new QVBoxLayout(m_thinking); box->setContentsMargins(10, 4, 6, 6); box->setSpacing(2);
            auto *header = new QHBoxLayout;
            m_thinkingHeader = new QLabel; m_thinkingHeader->setObjectName(QStringLiteral("transcriptHeader"));
            header->addWidget(m_thinkingHeader, 1);
            auto *close = new QToolButton; close->setText(QStringLiteral("×")); close->setAutoRaise(true); close->setFocusPolicy(Qt::NoFocus);
            close->setToolTip(QStringLiteral("Hide for this turn (Actions › Agent options › Show thinking turns it off)"));
            connect(close, &QToolButton::clicked, this, [this] { m_thinkingDismissed = true; hideBubble(m_thinking); });
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
            // Its own name, not transcriptView's: the overlay is styled quieter than the transcript.
            m_thinkingView->setObjectName(QStringLiteral("thinkingView"));
            m_thinkingView->setReadOnly(true);
            m_thinkingView->setFocusPolicy(Qt::NoFocus);
            m_thinkingView->setMaximumBlockCount(400);
            box->addWidget(m_thinkingView, 1);
            m_thinking->hide();
            // Above the queue strip and below the terminal: the order the two had as overlays.
            if (auto *column = qobject_cast<QVBoxLayout *>(layout()))
                column->insertWidget(column->indexOf(m_terminalHost) + 1, m_thinking);
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
        if (!m_thinkingDismissed && !m_thinking->isVisible()) { showBubble(m_thinking); placeThinking(); }
    }

    // The height the reasoning panel asks the pane's column for. It is a row now rather than an
    // overlay, so this is what moves the terminal up instead of covering it (owner report,
    // 2026-09-18: "the terminal needs to move up, rather than being covered up"); the two heights
    // the ▴ button switches between are the ones it always had.
    void placeThinking() {
        if (!m_thinking || !m_thinking->isVisible() || !m_terminalHost) return;
        // Compact by default: the panel takes the terminal's space now, so it must take as little
        // as it can. The ▴ button expands it when the reasoning is worth reading.
        const int lineHeight = std::max(14, m_thinkingView->fontMetrics().height());
        const int span = bubbleSpan();
        const int wanted = m_thinkingExpanded ? span / 3 : lineHeight * 2 + 30;
        int height = std::min(m_thinkingExpanded ? 220 : 90, std::max(wanted, lineHeight + 30));
        // Never more than half of what it shares with the terminal: in a pane squeezed down to a
        // few rows the terminal keeps the other half rather than vanishing under the panel.
        height = std::min(height, std::max(lineHeight + 30, span / 2));
        setBubbleHeight(m_thinking, height);
    }

    void endThinking() {
        if (!m_thinkingShown) return;
        m_thinkingShown = false;
        if (m_thinking) hideBubble(m_thinking);
    }

    // ----- the two bubbles that share the terminal's column -------------------------------------
    //
    // The reasoning panel and the queue strip are rows between the terminal host and the prompt
    // box, so showing one moves the terminal up instead of covering its last lines (owner report,
    // 2026-09-18). Their height is part of this pane's minimum height, and a splitter that cannot
    // satisfy every minimum redistributes all of its panes as soon as one minimum moves (#G152),
    // so every show, hide and height change runs with the enclosing splitters' sizes held.

    // The height the terminal and whichever bubbles are up share. Heights are measured against
    // this rather than against the terminal host alone, so showing a bubble does not shrink the
    // number the next call sizes it from and leave the two chasing each other.
    int bubbleSpan() const {
        int span = m_terminalHost ? m_terminalHost->height() : 0;
        const int spacing = layout() ? layout()->spacing() : 0;
        if (m_thinking && m_thinking->isVisible()) span += m_thinking->height() + spacing;
        if (m_queueStrip && m_queueStrip->isVisible()) span += m_queueStrip->height() + spacing;
        return span;
    }

    void showBubble(QWidget *bubble) {
        if (!bubble || bubble->isVisible()) return;
        const bool bottom = terminalAtBottom();
        keepPaneSizes([bubble] { bubble->show(); });
        pinTerminalBottom(bottom);
    }

    void hideBubble(QWidget *bubble) {
        if (!bubble || bubble->isHidden()) return;
        const bool bottom = terminalAtBottom();
        keepPaneSizes([bubble] { bubble->hide(); });
        pinTerminalBottom(bottom);
    }

    // A bubble asks for its height as a maximum over a token minimum, never as a fixed height: a
    // row's minimum is part of this pane's minimum, and a fixed one made a pane in a three-high
    // stack overflow its own column, with the queue strip drawn through the prompt box. As a
    // maximum the bubble is squeezed along with the terminal and the prompt box when the pane is
    // too short for all three, and takes exactly what it asked for whenever there is room.
    static constexpr int kBubbleFloor = 24;   // a sliver stays, so the bubble never vanishes silently
    void setBubbleHeight(QWidget *bubble, int height) {
        height = std::max(0, height);
        const int floor = std::min(height, kBubbleFloor);
        if (!bubble || (bubble->maximumHeight() == height && bubble->minimumHeight() == floor)) return;
        const bool bottom = terminalAtBottom();
        keepPaneSizes([bubble, height, floor] { bubble->setMinimumHeight(floor); bubble->setMaximumHeight(height); });
        pinTerminalBottom(bottom);
    }

    bool terminalAtBottom() const {
        return !m_backend || !(m_backend->capabilities() & relay::TerminalBackend::ScrollControl)
               || m_backend->viewportAtBottom();
    }

    // A terminal that was showing the newest output still shows it after a bubble resized it. The
    // view batches its grid change onto the next turn of the event loop, so the pin is queued
    // behind it; a reader who had scrolled back into the history is left where they were.
    void pinTerminalBottom(bool wasAtBottom) {
        if (!wasAtBottom || !m_backend || !(m_backend->capabilities() & relay::TerminalBackend::ScrollControl)) return;
        QTimer::singleShot(0, this, [this] { if (m_backend) m_backend->scrollToBottom(); });
    }

    // One inline line that is also a terminal hyperlink (OSC 8) to relay://turn/<pane>/<turn>.
    // The pane handles a click itself; another app opens it through the desktop's
    // x-scheme-handler/relay entry (see ensureUrlHandler).
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

    // ----- aliases: saved commands and prompts (issue G8DK, protocol 20) ----------------------
    // An alias runs three ways — the actions palette, `/name`, and the name typed in terminal mode.
    // All three end here: the template's `{{parameters}}` become fields in the prompt box, Tab moves
    // between them, and submitting sends the values to the worker, which does the substitution and
    // hands back the exact line. Relay never runs an alias without it passing through the prompt
    // box first, and the quoting that keeps a value *data* rather than shell syntax lives in one
    // place (relay_core/aliases.py), not here.
public:
    const QList<relay::aliases::Alias> &aliases() const { return m_aliasList; }

    void refreshAliases() {
        if (m_workerReady) send({{"type", "aliases"}, {"workspace", m_workspace}});
    }

    // From the palette (`fromPalette`), from `/name`, or from the name typed in terminal mode.
    void runAlias(const QString &name, const QString &args, bool fromPalette) {
        const relay::aliases::Alias *alias = nullptr;
        for (const auto &candidate : std::as_const(m_aliasList)) {
            if (candidate.name == name && !candidate.shadowed) { alias = &candidate; break; }
        }
        if (!alias) { status(QStringLiteral("No alias named “%1”.").arg(name)); return; }
        clearAliasFields();
        relay::aliases::Rendered rendered = relay::aliases::render(*alias);
        if (!args.isEmpty()) {
            // `squash 3 wip` fills the fields in order, the way Warp's workflows do.
            QStringList parts = QProcess::splitCommand(args);
            if (parts.isEmpty()) parts = args.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            for (int i = 0; i < parts.size() && i < rendered.fields.size(); ++i)
                relay::aliases::setField(rendered, i, parts.at(i));
        }
        const QString kind = alias->kind;
        const QString title = alias->title.isEmpty() ? name : alias->title;
        if (relay::aliases::unfilled(*alias, rendered).isEmpty()) {
            sendAliasRun(name, relay::aliases::values(rendered), kind, fromPalette);
            return;
        }
        // Something still has to be typed: show the line in the prompt box with its fields.
        m_aliasName = name;
        m_aliasKind = kind;
        m_aliasFields = rendered;
        m_aliasFromPalette = fromPalette;
        setMode(kind == QStringLiteral("prompt") ? QStringLiteral("agent") : QStringLiteral("shell"));
        m_editor->setPlainText(rendered.text);
        m_editor->setFocus();
        const int first = relay::aliases::nextField(m_aliasFields.fields, -1);
        if (first >= 0) selectAliasField(first);
        status(QStringLiteral("%1 · Tab for the next field, Enter to run.").arg(title));
    }

    void sendAliasRun(const QString &name, const QList<QPair<QString, QString>> &values,
                      const QString &kind, bool fromPalette) {
        if (!m_workerReady) { status(QStringLiteral("The worker is not ready yet.")); return; }
        QJsonObject collected;
        for (const auto &pair : values) collected.insert(pair.first, pair.second);
        m_aliasRunId = QStringLiteral("alias-") + QString::number(++m_requestId);
        m_aliasRunKind = kind;
        m_aliasRunFromPalette = fromPalette;
        m_aliasRunName = name;
        send({{"type", "alias_run"}, {"id", m_aliasRunId}, {"name", name},
              {"workspace", m_workspace}, {"values", collected}});
    }

    void clearAliasFields() {
        m_aliasName.clear();
        m_aliasKind.clear();
        m_aliasFromPalette = false;
        m_aliasFields = relay::aliases::Rendered();
    }

    bool aliasFieldsActive() const { return !m_aliasName.isEmpty() && !m_aliasFields.fields.isEmpty(); }

    // Tab (and Shift+Tab) move between the fields while a template is in the prompt box; the field
    // is selected, so typing replaces it.
    bool moveAliasField(bool forward) {
        if (!aliasFieldsActive() || !m_editor) return false;
        if (!relay::aliases::reparse(m_aliasFields, m_editor->toPlainText())) { clearAliasFields(); return false; }
        const QTextCursor cursor = m_editor->textCursor();
        const int caret = forward ? cursor.selectionEnd() : cursor.selectionStart();
        const int index = relay::aliases::nextField(m_aliasFields.fields, forward ? caret - 1 : caret, forward);
        if (index < 0) return false;
        selectAliasField(index);
        return true;
    }

    void selectAliasField(int index) {
        if (index < 0 || index >= m_aliasFields.fields.size() || !m_editor) return;
        const auto &field = m_aliasFields.fields.at(index);
        QTextCursor cursor = m_editor->textCursor();
        cursor.setPosition(field.start);
        cursor.setPosition(field.start + field.length, QTextCursor::KeepAnchor);
        m_editor->setTextCursor(cursor);
    }

    // Called from requestRoute before anything is routed: a template in the prompt box submits as
    // an alias, so the worker quotes the values. A line the user has rewritten past recognition
    // stops being an alias and goes to the router as itself.
    bool submitAliasFields() {
        if (!aliasFieldsActive()) return false;
        if (!relay::aliases::reparse(m_aliasFields, m_editor->toPlainText())) { clearAliasFields(); return false; }
        const QString name = m_aliasName, kind = m_aliasKind;
        const bool fromPalette = m_aliasFromPalette;
        const auto collected = relay::aliases::values(m_aliasFields);
        m_editor->remember(m_editor->toPlainText());
        m_editor->clear();
        clearAliasFields();
        sendAliasRun(name, collected, kind, fromPalette);
        return true;
    }

    // `/name …` typed in the composer, when `name` is an alias and not a built-in command.
    bool tryRunAliasSlash(const QString &text) {
        if (m_aliasExpanding) return false;   // an alias expands once; see tryRunAliasTyped
        QStringList reserved;
        for (const auto &command : slashCommands()) reserved << command.name;
        const auto match = relay::aliases::matchSlash(text.trimmed(), relay::aliases::names(m_aliasList), reserved);
        if (!match.matched) return false;
        m_editor->remember(text.trimmed());
        m_editor->clear();
        hideSlashPopup();
        runAlias(match.name, match.args, false);
        return true;
    }

    // The alias name typed on its own in terminal mode. Only in terminal mode: in agent mode the
    // same word is prose, and in auto mode the router decides what a bare word means.
    bool tryRunAliasTyped(const QString &text, const QString &mode) {
        // Never while an expansion is being submitted: `alias ll = "ll -h"` would otherwise match
        // its own output and expand for ever. An alias expands once, like a shell alias.
        if (m_aliasExpanding || mode != QStringLiteral("shell")) return false;
        const auto match = relay::aliases::matchTyped(text, relay::aliases::names(m_aliasList));
        if (!match.matched) return false;
        m_editor->remember(text.trimmed());
        m_editor->clear();
        runAlias(match.name, match.args, false);
        return true;
    }

    bool handleAliasEvent(const QString &type, const QJsonObject &event) {
        if (type == QStringLiteral("aliases")) {
            m_aliasList.clear();
            const QJsonArray items = event.value(QStringLiteral("items")).toArray();
            for (const auto &value : items) {
                const QJsonObject object = value.toObject();
                relay::aliases::Alias alias;
                alias.name = object.value(QStringLiteral("name")).toString();
                alias.kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("command"));
                alias.title = object.value(QStringLiteral("title")).toString();
                alias.description = object.value(QStringLiteral("description")).toString();
                alias.text = object.value(QStringLiteral("text")).toString();
                alias.scope = object.value(QStringLiteral("scope")).toString(QStringLiteral("local"));
                alias.shadowed = object.value(QStringLiteral("shadowed")).toBool();
                for (const auto &label : object.value(QStringLiteral("labels")).toArray())
                    alias.labels << label.toString();
                for (const auto &raw : object.value(QStringLiteral("params")).toArray()) {
                    const QJsonObject parameter = raw.toObject();
                    relay::aliases::Param param;
                    param.name = parameter.value(QStringLiteral("name")).toString();
                    param.hasDefault = !parameter.value(QStringLiteral("default")).isNull();
                    param.value = parameter.value(QStringLiteral("default")).toString();
                    param.description = parameter.value(QStringLiteral("description")).toString();
                    alias.params << param;
                }
                if (!alias.name.isEmpty()) m_aliasList << alias;
            }
            return true;
        }
        if (type == QStringLiteral("alias_expanded")) {
            if (event.value(QStringLiteral("id")).toString() != m_aliasRunId) return true;
            m_aliasRunId.clear();
            const QString text = event.value(QStringLiteral("text")).toString();
            const bool prompt = event.value(QStringLiteral("kind")).toString() == QStringLiteral("prompt");
            m_editor->setPlainText(text);
            m_editor->moveCursor(QTextCursor::End);
            // The palette is the slow way in; teach `/name` and the typed name.
            if (m_aliasRunFromPalette) {
                for (const auto &alias : std::as_const(m_aliasList)) {
                    if (alias.name != m_aliasRunName) continue;
                    hint(QStringLiteral("alias.run.") + alias.name, relay::aliases::fastPathHint(alias));
                    break;
                }
            }
            m_aliasExpanding = true;
            requestRoute(true, prompt ? QStringLiteral("agent") : QStringLiteral("shell"));
            m_aliasExpanding = false;
            return true;
        }
        if (type == QStringLiteral("alias_saved")) {
            status(QStringLiteral("Saved alias “%1” (%2). Run it with /%1.")
                       .arg(event.value(QStringLiteral("name")).toString(),
                            event.value(QStringLiteral("scope")).toString()));
            return true;
        }
        if (type == QStringLiteral("alias_deleted")) {
            status(QStringLiteral("Removed alias “%1”.").arg(event.value(QStringLiteral("name")).toString()));
            return true;
        }
        if (type == QStringLiteral("alias_import_preview")) { showAliasImportPreview(event); return true; }
        if (type == QStringLiteral("alias_imported")) {
            const int written = event.value(QStringLiteral("written")).toArray().size();
            const int failed = event.value(QStringLiteral("failed")).toArray().size();
            status(failed ? QStringLiteral("Imported %1 alias(es); %2 failed.").arg(written).arg(failed)
                          : QStringLiteral("Imported %1 alias(es).").arg(written));
            if (m_aliasImport) m_aliasImport->close();
            return true;
        }
        return false;
    }

    // Save the prompt box as an alias. The only way the GUI creates one, so what is stored is
    // always something the user had in front of them.
    void saveComposerAsAlias() {
        const QString text = m_editor ? m_editor->toPlainText().trimmed() : QString();
        if (text.isEmpty()) { status(QStringLiteral("Type the command or prompt first, then save it as an alias.")); return; }
        const bool prompt = m_modeValue == QStringLiteral("agent");
        bool ok = false;
        const QString suggested = relay::aliases::slug(text.left(60), relay::aliases::names(m_aliasList));
        const QString name = QInputDialog::getText(
            window(), QStringLiteral("Save as alias"),
            QStringLiteral("Name for this %1 — run it later with /name%2.\n\n%3")
                .arg(prompt ? QStringLiteral("prompt") : QStringLiteral("command"),
                     prompt ? QString() : QStringLiteral(", or by typing the name in terminal mode"),
                     text.left(400)),
            QLineEdit::Normal, suggested, &ok).trimmed().toLower();
        if (!ok || name.isEmpty()) return;
        if (!relay::aliases::validName(name)) {
            status(QStringLiteral("An alias name is 1–32 characters of a–z, 0–9, - or _."));
            return;
        }
        relay::aliases::Alias draft;
        draft.name = name;
        draft.kind = prompt ? QStringLiteral("prompt") : QStringLiteral("command");
        draft.text = text;
        QJsonArray params;
        QStringList seen;
        for (const auto &field : relay::aliases::render(draft).fields) {
            if (seen.contains(field.name)) continue;
            seen << field.name;
            params.append(QJsonObject{{"name", field.name}});
        }
        send({{"type", "alias_save"}, {"id", QStringLiteral("alias-save-") + QString::number(++m_requestId)},
              {"workspace", m_workspace}, {"scope", QStringLiteral("local")}, {"name", name},
              {"kind", draft.kind}, {"title", text.left(80)}, {"text", text}, {"params", params}});
    }

    // Import Warp workflows and shell aliases. The worker reads them and sends back what *would*
    // be written; nothing is stored until a row is ticked here and Import is pressed.
    void openAliasImport() {
        if (!m_workerReady) { status(QStringLiteral("The worker is not ready yet.")); return; }
        status(QStringLiteral("Reading Warp workflows and shell aliases…"));
        send({{"type", "alias_import_preview"},
              {"id", QStringLiteral("alias-import-") + QString::number(++m_requestId)},
              {"workspace", m_workspace}});
    }

    void showAliasImportPreview(const QJsonObject &event) {
        const QJsonArray items = event.value(QStringLiteral("items")).toArray();
        const QJsonArray skipped = event.value(QStringLiteral("skipped")).toArray();
        const QString token = event.value(QStringLiteral("preview_id")).toString();
        if (items.isEmpty()) {
            status(QStringLiteral("Nothing to import: no Warp workflows and no shell aliases were found."));
            return;
        }
        auto *dialog = new QDialog(window());
        m_aliasImport = dialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QStringLiteral("Import workflows and shell aliases"));
        dialog->resize(1180, 560);
        auto *layout = new QVBoxLayout(dialog);
        auto *caption = new QLabel(QStringLiteral(
            "Nothing here has been run, and nothing is saved until you press Import. "
            "This is exactly the text that would be stored."), dialog);
        caption->setWordWrap(true);
        layout->addWidget(caption);
        auto *tree = new QTreeWidget(dialog);
        tree->setColumnCount(4);
        tree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Kind"),
                               QStringLiteral("What would run"), QStringLiteral("From / warnings")});
        tree->setRootIsDecorated(false);
        tree->setAlternatingRowColors(true);
        for (const auto &value : items) {
            const QJsonObject item = value.toObject();
            QStringList warnings;
            for (const auto &warning : item.value(QStringLiteral("warnings")).toArray())
                warnings << warning.toString();
            auto *row = new QTreeWidgetItem(tree, {item.value(QStringLiteral("name")).toString(),
                                                   item.value(QStringLiteral("kind")).toString(),
                                                   item.value(QStringLiteral("text")).toString().simplified(),
                                                   warnings.isEmpty() ? item.value(QStringLiteral("origin")).toString()
                                                                      : warnings.join(QStringLiteral(" · "))});
            // A row Relay has something to say about starts unticked, so a warning has to be read.
            row->setCheckState(0, warnings.isEmpty() ? Qt::Checked : Qt::Unchecked);
            row->setData(0, Qt::UserRole, item.value(QStringLiteral("name")).toString());
            row->setToolTip(2, item.value(QStringLiteral("text")).toString());
        }
        for (int i = 0; i < 4; ++i) tree->resizeColumnToContents(i);
        layout->addWidget(tree, 1);
        if (!skipped.isEmpty()) {
            QStringList reasons;
            for (const auto &value : skipped) reasons << value.toObject().value(QStringLiteral("reason")).toString();
            auto *note = new QLabel(QStringLiteral("Skipped %1: %2").arg(skipped.size())
                                        .arg(reasons.mid(0, 4).join(QStringLiteral("; "))), dialog);
            note->setWordWrap(true);
            layout->addWidget(note);
        }
        auto *scopeRow = new QHBoxLayout;
        auto *scope = new QComboBox(dialog);
        scope->addItem(QStringLiteral("Save globally (every project)"), QStringLiteral("global"));
        scope->addItem(QStringLiteral("Save in this project"), QStringLiteral("local"));
        scopeRow->addWidget(scope);
        scopeRow->addStretch(1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
        auto *import = buttons->addButton(QStringLiteral("Import"), QDialogButtonBox::AcceptRole);
        scopeRow->addWidget(buttons);
        layout->addLayout(scopeRow);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        connect(import, &QPushButton::clicked, dialog, [this, tree, scope, token] {
            QJsonArray names;
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                auto *row = tree->topLevelItem(i);
                if (row->checkState(0) == Qt::Checked) names.append(row->data(0, Qt::UserRole).toString());
            }
            if (names.isEmpty()) { status(QStringLiteral("Tick at least one alias to import.")); return; }
            send({{"type", "alias_import_apply"},
                  {"id", QStringLiteral("alias-apply-") + QString::number(++m_requestId)},
                  {"workspace", m_workspace}, {"preview_id", token}, {"names", names},
                  {"scope", scope->currentData().toString()}});
        });
        dialog->show();
    }

private:
    QList<relay::aliases::Alias> m_aliasList;
    relay::aliases::Rendered m_aliasFields;
    QString m_aliasName, m_aliasKind, m_aliasRunId, m_aliasRunKind, m_aliasRunName;
    bool m_aliasFromPalette = false, m_aliasRunFromPalette = false, m_aliasExpanding = false;
    QPointer<QDialog> m_aliasImport;

    // ----- voice transcription (issue NY7Z, protocol 16) -------------------------------------
    // Hold the voice key (Right Alt by default, Warp's binding) or click the microphone chip, speak,
    // and the transcript is inserted at the cursor in the prompt box — never submitted, so a
    // misheard word is fixed before anything runs. Recording is a capture tool the desktop already
    // has (src/Voice.cpp); the worker does the transcribing with an OpenRouter key of its own.
public:
    static bool voiceEnabled() { return QSettings().value(QStringLiteral("voice/enabled"), true).toBool(); }
    static QString voiceModel() {
        const QString value = QSettings().value(QStringLiteral("voice/model")).toString();
        return value.isEmpty() ? QStringLiteral("google/gemini-3.5-flash-lite") : value;
    }
    static int voiceSeconds() {
        return relay::voice::clampSeconds(QSettings().value(QStringLiteral("voice/max_seconds")).toInt());
    }

    // The hold key, defaulted once from the keyboard layout: Right Alt is AltGr wherever the layout
    // types with it, and a key that types é must not also start recording.
    static QString voiceHoldKey() {
        QSettings settings;
        const QString stored = settings.value(QStringLiteral("voice/hold_key")).toString();
        if (relay::voice::holdKeys().contains(stored)) return stored;
        QString layouts;
        QFile file(QStringLiteral("/etc/default/keyboard"));
        if (file.exists() && file.size() < 64 * 1024 && file.open(QIODevice::ReadOnly | QIODevice::Text))
            layouts = QString::fromUtf8(file.readAll());
        const QString chosen = relay::voice::defaultHoldKey(relay::voice::layoutsFromKeyboardConfig(layouts));
        settings.setValue(QStringLiteral("voice/hold_key"), chosen);
        return chosen;
    }

    // The microphone chip and the palette action: start, or finish a recording that is running.
    // ----- sharing this pane with a phone --------------------------------------------------------

    void updateShareChip() {
        if (!m_share) return;
        const bool sharing = relay::RemoteShare::instance().isSharing(m_token);
        m_share->setProperty("dest", sharing ? QStringLiteral("agent") : QVariant());
        m_share->setToolTip(sharing
            ? QStringLiteral("Shared with your phone — click to show the code or stop")
            : QStringLiteral("Share this pane with your phone"));
        m_share->style()->unpolish(m_share);
        m_share->style()->polish(m_share);
    }

    void toggleShare() {
        relay::RemoteShare &share = relay::RemoteShare::instance();
        if (!share.isSharing(m_token)) {
            relay::RemoteShare::PaneHooks hooks;
            // The engine hands the phone a frame of the screen (docs/ENGINE.md).
            if (auto *engine = dynamic_cast<relay::VTermBackend *>(m_backend)) {
                hooks.view = engine->view();
            }
            // The same label the tab shows: the pane's title, or its folder until it has one.
            // Never the internal id, which is what a phone saw before.
            hooks.title = [this] {
                return m_title.isEmpty() ? QFileInfo(m_cwd).fileName() : m_title;
            };
            hooks.cwd = [this] { return m_cwd; };
            hooks.status = [this] { return shareStatus(); };
            hooks.input = [this](const QByteArray &bytes) {
                if (m_backend) m_backend->sendText(QString::fromUtf8(bytes), false);
            };
            QString error;
            if (!share.sharePane(m_token, hooks, &error)) {
                status(error);
                return;
            }
            updateShareChip();
        }
        auto *dialog = new relay::RemoteShareDialog(m_token, window());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(&share, &relay::RemoteShare::sharingChanged, dialog, [this] { updateShareChip(); });
        dialog->show();
    }

    // The pane status a phone sees: the same vocabulary as the protocol's pane list.
    QString shareStatus() const {
        if (m_secretMode) return QStringLiteral("password");
        if (processBusy()) return QStringLiteral("running");
        return QStringLiteral("idle");
    }

    void toggleVoice(bool fromKeyboard) {
        if (m_voiceCapture && m_voiceCapture->recording()) { stopVoice(); return; }
        startVoice(false);
        if (!fromKeyboard && voiceHoldKey() != QStringLiteral("off"))
            hint(QStringLiteral("voice.hold"), QStringLiteral("Next time: hold %1 and speak")
                     .arg(relay::voice::holdKeyLabel(voiceHoldKey())));
    }

    // Push-to-talk, from the pane's event filter. Auto-repeat never reaches these.
    void voiceKeyPressed() {
        if (m_voiceHold || (m_voiceCapture && m_voiceCapture->recording())) return;
        m_voiceHold = true;
        startVoice(true);
        if (!(m_voiceCapture && m_voiceCapture->recording())) m_voiceHold = false;   // it did not start
    }
    void voiceKeyReleased() {
        if (!m_voiceHold) return;
        m_voiceHold = false;
        if (m_voiceCapture && m_voiceCapture->recording()) stopVoice();
    }
    // Any other key while the voice key is held: the user is typing (AltGr types é on most
    // layouts), so the recording is dropped rather than sent.
    void voiceInterrupted() {
        if (!m_voiceHold) return;
        m_voiceHold = false;
        if (m_voiceCapture && m_voiceCapture->recording()) {
            m_voiceCapture->cancel();
            updateVoiceChip();
            status(QStringLiteral("Recording cancelled."));
        }
    }
    bool voiceRecording() const { return m_voiceCapture && m_voiceCapture->recording(); }

private:
    void startVoice(bool hold) {
        if (!voiceEnabled()) { status(QStringLiteral("Voice transcription is off (Settings › Voice).")); return; }
        if (m_native) { status(QStringLiteral("Voice types into the prompt box; leave native input first.")); return; }
        if (m_secretMode) { status(QStringLiteral("Not while a password prompt is open.")); return; }
        if (m_voiceTranscribing) { status(QStringLiteral("Still transcribing the last clip…")); return; }
        // Nothing is recorded without a key: the clip would have nowhere to go.
        if (!voiceKeyStored()) { offerVoiceKey(); return; }
        if (!m_workerReady) { status(QStringLiteral("Relay's worker is not ready yet.")); return; }
        ensureCapture();
        relay::voice::Options options;
        options.tool = QSettings().value(QStringLiteral("voice/tool")).toString();
        options.device = QSettings().value(QStringLiteral("voice/device")).toString();
        options.seconds = voiceSeconds();
        QString error;
        if (!m_voiceCapture->start(options, &error)) {
            status(error);
            toast(error, 6000);
            updateVoiceChip();
            return;
        }
        updateVoiceChip();
        toast(hold ? QStringLiteral("Listening… release %1 to transcribe").arg(relay::voice::holdKeyLabel(voiceHoldKey()))
                   : QStringLiteral("Listening… click the microphone again to transcribe"), 4000);
    }

    void stopVoice() {
        if (!m_voiceCapture || !m_voiceCapture->recording()) return;
        m_voiceCapture->stop();
        status(QStringLiteral("Transcribing…"));
        updateVoiceChip();
    }

    void ensureCapture() {
        if (m_voiceCapture) return;
        m_voiceCapture = new relay::voice::Capture(this);
        connect(m_voiceCapture, &relay::voice::Capture::ready, this, [this](const QString &path, qint64 ms) {
            m_voiceClip = path;
            m_voiceTranscribing = true;
            m_voiceRequest = QStringLiteral("voice-") + QString::number(++m_requestId);
            updateVoiceChip();
            Q_UNUSED(ms);
            send({{"type", "transcribe"}, {"id", m_voiceRequest}, {"path", path}, {"model", voiceModel()}});
        });
        connect(m_voiceCapture, &relay::voice::Capture::failed, this, [this](const QString &message) {
            m_voiceHold = false;
            updateVoiceChip();
            status(message);
            toast(message, 5000);
        });
        connect(m_voiceCapture, &relay::voice::Capture::elapsed, this, [this] { updateVoiceChip(); });
    }

    // The openrouter row of the worker's `presets` event. Before it arrives nothing is known, and
    // the worker answers with the no_key code instead.
    bool voiceKeyStored() const {
        if (m_presets.isEmpty()) return true;
        for (const auto &item : m_presets) {
            const auto preset = item.toObject();
            if (preset.value(QStringLiteral("id")).toString() == QStringLiteral("openrouter"))
                return preset.value(QStringLiteral("has_stored_key")).toBool();
        }
        return false;
    }

    void offerVoiceKey() {
        m_voiceHold = false;
        status(QStringLiteral("Voice needs an OpenRouter key."));
        QMessageBox box(window());
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(QStringLiteral("Voice transcription"));
        box.setText(QStringLiteral("Voice needs an OpenRouter key."));
        box.setInformativeText(QStringLiteral(
            "Relay transcribes with %1 on OpenRouter, whatever model this pane's agent runs, so voice needs an "
            "OpenRouter key of its own. Recordings are sent to OpenRouter and Google; nothing is recorded or "
            "sent until a key is stored.").arg(voiceModel()));
        QPushButton *keys = box.addButton(QStringLiteral("API keys…"), QMessageBox::AcceptRole);
        QPushButton *warp = box.addButton(QStringLiteral("Import from Warp"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == keys) openKeysDialog();
        else if (box.clickedButton() == warp) send({{"type", "import_warp"}});
    }

    void onTranscribed(const QJsonObject &event) {
        // The clip has done its work; Relay keeps no audio.
        if (!m_voiceClip.isEmpty()) { QFile::remove(m_voiceClip); m_voiceClip.clear(); }
        m_voiceTranscribing = false;
        m_voiceRequest.clear();
        updateVoiceChip();
        if (!event.value(QStringLiteral("ok")).toBool()) {
            const QString message = event.value(QStringLiteral("error")).toString();
            if (event.value(QStringLiteral("code")).toString() == QStringLiteral("no_key")) { offerVoiceKey(); return; }
            status(QStringLiteral("Voice: ") + message);
            toast(message, 5000);
            return;
        }
        const QString text = event.value(QStringLiteral("text")).toString();
        if (text.isEmpty()) {
            status(QStringLiteral("Nothing was said."));
            toast(QStringLiteral("Nothing was said."), 2500);
            return;
        }
        // Inserted with the cursor, not by replacing the document, so Ctrl+Z still undoes it.
        QTextCursor cursor = m_editor->textCursor();
        const QString before = m_editor->toPlainText();
        const auto insertion = relay::voice::insertTranscript(before, cursor.position(), text);
        const int at = qBound(0, cursor.position(), before.size());
        cursor.setPosition(at);
        cursor.insertText(insertion.text.mid(at, insertion.text.size() - before.size()));
        cursor.setPosition(qBound(0, insertion.cursor, insertion.text.size()));
        m_editor->setTextCursor(cursor);
        focusInput();
        status(QStringLiteral("Transcribed %1 character%2 · Enter sends it")
                   .arg(text.size()).arg(text.size() == 1 ? QString() : QStringLiteral("s")));
    }

    void updateVoiceChip() {
        if (!m_mic) return;
        const bool recording = m_voiceCapture && m_voiceCapture->recording();
        m_mic->setProperty("recording", recording);
        m_mic->style()->unpolish(m_mic);
        m_mic->style()->polish(m_mic);
        if (recording) {
            const qint64 seconds = m_voiceCapture->elapsedMs() / 1000;
            m_mic->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            m_mic->setText(QStringLiteral(" %1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')));
            m_mic->setToolTip(QStringLiteral("Listening… click to transcribe"));
        } else {
            m_mic->setToolButtonStyle(Qt::ToolButtonIconOnly);
            m_mic->setText(QString());
            const QString hold = voiceHoldKey();
            m_mic->setToolTip(m_voiceTranscribing
                ? QStringLiteral("Transcribing…")
                : QStringLiteral("Voice transcription%1").arg(
                      hold == QStringLiteral("off") ? QString()
                          : QStringLiteral(" · hold %1 and speak").arg(relay::voice::holdKeyLabel(hold))));
        }
    }

public:
    // ----- provider and model modals -------------------------------------------------------
    // Both are non-modal windows fed by the worker's `presets` / `model_roles` events. No key
    // material passes through either of them in the read direction: keys only go out, to the
    // worker's keyring commands.
    void openKeysDialog() {
        if (!m_keysDialog) {
            m_keysDialog = new relay::KeysDialog(window());
            m_keysDialog->setAttribute(Qt::WA_DeleteOnClose);
            m_keysDialog->send = [this](QJsonObject request) { send(request); };
            m_keysDialog->onKeysChanged = [this] { send({{"type", "presets"}}); };
        }
        m_keysDialog->setPresets(m_presets);
        m_keysDialog->show(); m_keysDialog->raise(); m_keysDialog->activateWindow();
        if (m_workerReady) send({{"type", "presets"}});
    }

    void openRolesDialog() {
        if (!m_rolesDialog) {
            m_rolesDialog = new relay::RolesDialog(window());
            m_rolesDialog->setAttribute(Qt::WA_DeleteOnClose);
            m_rolesDialog->send = [this](QJsonObject request) { send(request); };
            m_rolesDialog->openKeys = [this] { openKeysDialog(); };
            m_rolesDialog->onProviderChosen = [this](const QString &id) { selectModel(id); };
            m_rolesDialog->onRolesChanged = [this] { rolesChanged(); };
        }
        m_rolesDialog->setPresets(m_presets, m_tierCatalog, m_roleActions);
        m_rolesDialog->setProvider(m_currentPreset);
        m_rolesDialog->setResolved(m_tierSummary, m_roleSummary);
        m_rolesDialog->show(); m_rolesDialog->raise(); m_rolesDialog->activateWindow();
        if (m_workerReady) send({{"type", "presets"}});
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
        if (onOpenPath) onOpenPath(path, 0);
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
                    printInline(QStringLiteral("✦ ") + m_steering[i].text + QStringLiteral("  ↪ at the next tool call\n"), Ink::UserAgent);
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
            m_tierSummary = event.value(QStringLiteral("tiers")).toObject();
            if (m_rolesDialog) m_rolesDialog->setResolved(m_tierSummary, m_roleSummary);
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
            m_lastPlanPath = path;
            updateWorkChip();
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
        // ----- pane title and tab label (protocol section 18) --------------------------------
        if (type == QStringLiteral("session_title")) {
            setTitleFromWorker(event.value(QStringLiteral("title")).toString(),
                               event.value(QStringLiteral("source")).toString() == QStringLiteral("user"));
            return true;
        }
        if (type == QStringLiteral("tab_label")) {
            if (onTabLabel)
                onTabLabel(event.value(QStringLiteral("id")).toString(),
                           event.value(QStringLiteral("label")).toString(),
                           event.value(QStringLiteral("related")).toBool(true));
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
            // The header states the stretch of work the recap covers ("Recap · 09:12 → 11:47 ·
            // 2h 35m"), from the worker's recorded turn stamps (owner request, 2026-09-17). A
            // session with no stamps sends no `span_text`, and the summary follows "Recap · " on
            // one line as before: no span reads better than a guessed one.
            const QString span = event.value(QStringLiteral("span_text")).toString();
            const QString summary = event.value(QStringLiteral("text")).toString();
            if (span.isEmpty()) {
                printInline(QStringLiteral("Recap · ") + summary + '\n', Ink::Recap);
            } else {
                printInline(QStringLiteral("Recap · ") + span + '\n', Ink::Recap);
                printInline(summary + '\n', Ink::Recap);
            }
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
            // A side call that failed says so, on the status line, naming the call and the model it
            // ran on. Without this the only sign was a bare provider error and no ghost text, which
            // reads as "suggestions do not work" (#308N).
            const QString error = event.value(QStringLiteral("error")).toString();
            if (!error.isEmpty()) {
                if (event.value(QStringLiteral("id")).toString() != m_suggestionId) return true;
                m_suggestionId.clear();
                const QString model = event.value(QStringLiteral("model")).toString();
                status(QStringLiteral("%1 suggestion failed%2: %3")
                           .arg(kind == QStringLiteral("next_prompt") ? QStringLiteral("Next-prompt") : QStringLiteral("Next-command"),
                                model.isEmpty() ? QString() : QStringLiteral(" (") + model + ')', error));
                return true;
            }
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
                           QString::number(item.value(QStringLiteral("turns")).toInt()),
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
            {QStringLiteral("new"), QString(), QStringLiteral("Start a new conversation and clear the terminal")},
            {QStringLiteral("clear"), QString(), QStringLiteral("Start a new conversation and clear the terminal (same as /new)")},
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
            {QStringLiteral("light"), QString(), QStringLiteral("Light theme: IBM Beige")},
            {QStringLiteral("dark"), QString(), QStringLiteral("Dark theme: Dark Copper")},
        {QStringLiteral("switchboard"), QString(), QStringLiteral("Open the Switchboard: cards, threads and plans")},
        {QStringLiteral("card"), QStringLiteral("<text>"), QStringLiteral("Add a card to the Switchboard inbox, verbatim")},
            {QStringLiteral("recap"), QString(), QStringLiteral("Summarize this session")},
            {QStringLiteral("tasks"), QString(), QStringLiteral("Task list: what the agent is working on")},
            {QStringLiteral("requests"), QString(), QStringLiteral("Task list (same as /tasks)")},
            {QStringLiteral("todos"), QString(), QStringLiteral("Task list (same as /tasks)")},
            {QStringLiteral("continue"), QString(), QStringLiteral("Continue the agent turn (after a step limit)")},
            {QStringLiteral("agents"), QString(), QStringLiteral("Subagents: definitions and running agents")},
            {QStringLiteral("skills"), QString(), QStringLiteral("Skills: list, exclude, refine, import from a repository")},
            {QStringLiteral("instructions"), QString(), QStringLiteral("Choose instruction files (CLAUDE.md, AGENTS.md, WARP.md…)")},
            {QStringLiteral("rename"), QStringLiteral("[name]"), QStringLiteral("Name this pane (no name: edit it in the header; empty: back to automatic)")},
            {QStringLiteral("rename-tab"), QStringLiteral("[name]"), QStringLiteral("Name this tab (no name: edit it in the tab)")},
            {QStringLiteral("export"), QString(), QStringLiteral("Save the conversation as Markdown")},
            {QStringLiteral("shell"), QStringLiteral("<command>"), QStringLiteral("Send to the terminal")},
            {QStringLiteral("agent"), QStringLiteral("<prompt>"), QStringLiteral("Send to the agent")}};
        return commands;
    }

    void updateSlashPopup() {
        const QString text = m_editor ? m_editor->toPlainText() : QString();
        if (!m_editor || m_native || !text.startsWith('/') || text.contains(QRegularExpression(QStringLiteral("\\s")))) { hideSlashPopup(); return; }
        // The first `/` of a command is the cue to re-read the aliases, for the same reason the
        // palette does: they are files somebody else may have written (issue G8DK).
        if (text == QStringLiteral("/")) refreshAliases();
        const QString query = text.mid(1).toLower();
        struct Ranked { int score; int index; };
        QList<Ranked> ranked;
        // The built-ins, then the aliases (issue G8DK), so `/name` is discoverable and a built-in
        // is never hidden behind an alias of the same name.
        QList<SlashCommand> commands = slashCommands();
        QStringList builtins;
        for (const auto &command : std::as_const(commands)) builtins << command.name;
        for (const auto &alias : std::as_const(m_aliasList)) {
            if (alias.shadowed || builtins.contains(alias.name)) continue;
            commands.append({alias.name, alias.params.isEmpty() ? QString() : QStringLiteral("[args]"),
                             relay::aliases::paletteDetail(alias)});
        }
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
        // An alias name that reached the popup is not a built-in (issue G8DK).
        if (std::none_of(slashCommands().cbegin(), slashCommands().cend(),
                         [&](const auto &c) { return c.name == name; })) {
            runAlias(name, QString(), false);
            return;
        }
        runSlashCommand(name, QString());
    }

    // True when `text` is a Relay slash command (not the router's /shell and /agent prefixes).
    bool tryRunSlashCommand(const QString &text) {
        static const QRegularExpression pattern(QStringLiteral("^/([a-z-]+)(?:\\s+([\\s\\S]*))?$"));
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
        else if (name == QStringLiteral("light") || name == QStringLiteral("dark")) {
            // The owner named the two (0EXJ, 2026-09-18: "light activates beige; dark activates
            // copper"), so these are not "any light theme" — they are those two theme files. The
            // switch is the one the settings picker makes, which restyles the chrome, both
            // terminal engines and the prompt box's colours and stores `theme/name`.
            const QString id = name == QStringLiteral("light") ? QStringLiteral("ibm-beige") : QStringLiteral("dark-copper");
            if (!relay::theme::setActiveTheme(id)) { status(QStringLiteral("The %1 theme could not be read.").arg(id)); return; }
            status(QStringLiteral("Theme: %1.").arg(relay::theme::active().name));
        }
        else if (name == QStringLiteral("rename")) {
            // With a name it renames straight away; without one it opens the same editor a double
            // click does, where clearing the field hands the pane back to the model.
            if (args.isEmpty()) beginRename(); else renameTo(args);
        }
        else if (name == QStringLiteral("rename-tab")) {
            if (onRenameTab) onRenameTab(args, args.isEmpty());
        }
        else if (name == QStringLiteral("switchboard")) {
            if (onOpenBoard) onOpenBoard();
            boardShortcutHint(QStringLiteral("board.slash"));
        }
        else if (name == QStringLiteral("card")) {
            if (args.trimmed().isEmpty()) { status(QStringLiteral("Usage: /card <what to remember>")); return; }
            // Quick add to the Inbox without opening the pane; the text is kept verbatim.
            send({{QStringLiteral("type"), QStringLiteral("board_create")},
                  {QStringLiteral("tab"), QStringLiteral("features")},
                  {QStringLiteral("status"), QStringLiteral("inbox")},
                  {QStringLiteral("text"), args.trimmed()}});
            m_cardIndexAsked = false;
            boardShortcutHint(QStringLiteral("card.slash"));
        }
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
        if (!entry.cards.isEmpty()) request.insert(QStringLiteral("cards"), entry.cards);
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
        // Beneath the prompt box, as in Claude Code: Down from the prompt moves into it. It takes
        // layout space like the growing editor does, so the terminal gives up a few rows while
        // subagents are listed.
        m_agentsPanel = new relay::SubagentsPanel(&m_subagents, this);
        layout->addWidget(m_agentsPanel);
        m_agentsPanel->onOpen = [this](const QString &id) { openSubagent(id); };
        m_agentsPanel->onStop = [this](const QString &id) { stopSubagent(id); toast(QStringLiteral("Stopping ") + id); };
        m_agentsPanel->onExit = [this] { focusInput(); };
        m_agentsPanel->onPickModel = [this](const QString &id, const QPoint &at) { pickSubagentModel(id, at); };
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
            const bool failed = row.status.contains(QStringLiteral("fail")) || row.status.contains(QStringLiteral("error"));
            notify(QStringLiteral("Agent %1 %2").arg(row.type + ' ' + row.id, row.status), row.summary.left(200),
                   failed ? relay::NotificationCenter::kindError : relay::NotificationCenter::kindSuccess);
        };
        m_subagents.onTranscript = [this](const QString &id, const QJsonObject &event) {
            for (const auto &view : std::as_const(m_subagentViews)) if (view && view->agentId() == id) view->handleEvent(event);
        };
        m_subagents.onStatus = [this](const QString &text) { status(text); };
    }

    // The model chip on a subagent row: pick a model, then (with more than one subagent listed)
    // choose between changing every subagent and only this one.
    void pickSubagentModel(const QString &id, const QPoint &at) {
        const auto *row = m_subagents.row(id);
        if (!row) return;
        const QString current = row->model;
        QMenu menu(this);
        QAction *inherit = menu.addAction(QStringLiteral("Same as the main agent"));
        inherit->setData(QStringLiteral("inherit"));
        menu.addSeparator();
        for (const auto &model : std::as_const(m_stored)) {
            QAction *action = menu.addAction(conciseModel(model.first, model.second));
            action->setData(model.first);
            action->setCheckable(true);
            action->setChecked(presetById(model.first).value(QStringLiteral("model")).toString() == current);
        }
        if (m_stored.isEmpty()) menu.addAction(QStringLiteral("No stored keys"))->setEnabled(false);
        const QAction *chosen = menu.exec(at);
        if (!chosen || chosen->data().toString().isEmpty()) { if (m_agentsPanel) m_agentsPanel->setFocus(); return; }
        const QString model = chosen->data().toString();
        const QString label = chosen->text();
        QString target = id;
        const int count = int(m_subagents.rows().size());
        if (count > 1) {
            QMessageBox box(window());
            box.setIcon(QMessageBox::Question);
            box.setWindowTitle(QStringLiteral("Change subagent model"));
            box.setText(QStringLiteral("Change all subagents to %1?").arg(label));
            box.setInformativeText(QStringLiteral("All %1 subagents in the list can switch to %2, or only %3. "
                                                  "A running agent switches before its next step.").arg(count).arg(label, id));
            QPushButton *all = box.addButton(QStringLiteral("Change all"), QMessageBox::AcceptRole);
            QPushButton *one = box.addButton(QStringLiteral("Only ") + id, QMessageBox::ActionRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(all);
            box.exec();
            if (box.clickedButton() == all) target = QStringLiteral("all");
            else if (box.clickedButton() != one) { if (m_agentsPanel) m_agentsPanel->setFocus(); return; }
        }
        send({{"type", "agent_set_model"}, {"id", target}, {"model", model}});
        if (m_agentsPanel && m_agentsPanel->isVisible()) m_agentsPanel->setFocus();
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
        if (!m_agentsPanel) return;
        m_agentsPanel->setAllowed(!m_composer || m_composer->isVisible());
        placeQueueStrip();
    }

    void placeSubagentOverlay() {
        if (!m_subagentOverlay) return;
        const int w = std::min(width() - 16, std::max(340, width() * 3 / 5));
        m_subagentOverlay->setGeometry(width() - w - 8, 8, w, std::max(160, height() - 16));
    }
    // ----- end subagents UI ---------------------------------------------------------------------

    // ----- request ledger UI (protocol section 12) ----------------------------------------------
    // One Switchboard chip for what this pane is working on (owner, 2026-09-17): the cards it
    // referenced, the agent's task list and its plan, summed up as "#K7Q2 · 2/5 · plan". A click
    // opens a menu with each part and a way into the Switchboard itself.
    void setupWorkChip(QHBoxLayout *row) {
        m_workChip = new QToolButton;
        m_workChip->setObjectName(QStringLiteral("workChip"));
        m_workChip->setFocusPolicy(Qt::NoFocus);
        m_workChip->setAccessibleName(QStringLiteral("Switchboard: issues, tasks and plan"));
        m_workChip->setIcon(stripIcon(QStringLiteral("board")));
        m_workChip->setIconSize(QSize(14, 14));
        m_workChip->setCursor(Qt::PointingHandCursor);
        m_workChip->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(m_workChip);
        connect(menu, &QMenu::aboutToShow, this, [this, menu] { fillWorkMenu(menu); });
        m_workChip->setMenu(menu);
        row->addWidget(m_workChip);
        m_ledger.onChanged = [this] {
            updateWorkChip();
            if (m_requestsPanel) m_requestsPanel->refresh();
        };
        Keymap::instance().listen(this, [this] { updateWorkChip(); });
        updateWorkChip();
    }

    QString requestsShortcutHint() const {
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.requests"));
        return keys.isEmpty() ? QStringLiteral("Next time: /tasks in the prompt box opens the task list")
                              : relay::ShortcutHints::nextTime(keys, QStringLiteral("task list"));
    }

    void noteWorkCard(const QString &id) {
        if (id.isEmpty()) return;
        m_workCards.removeAll(id);
        m_workCards.prepend(id);
        while (m_workCards.size() > 8) m_workCards.removeLast();
        updateWorkChip();
    }

    // The icon alone until there is something to show.
    void updateWorkChip() {
        if (!m_workChip) return;
        QStringList parts;
        if (m_workCards.size() == 1) parts << QStringLiteral("#") + m_workCards.first();
        else if (!m_workCards.isEmpty()) parts << QStringLiteral("%1 cards").arg(m_workCards.size());
        const bool tasks = m_ledger.hasTasks();
        if (tasks) parts << m_ledger.summary().progress();
        if (!m_lastPlanPath.isEmpty()) parts << QStringLiteral("plan");
        m_workChip->setText(parts.join(QStringLiteral(" · ")));
        m_workChip->setToolButtonStyle(parts.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("board.open"));
        m_workChip->setToolTip((tasks ? m_ledger.chipToolTip() + '\n' : QString())
                               + QStringLiteral("Issues, tasks and plan for this pane")
                               + (keys.isEmpty() ? QString() : QStringLiteral(" · %1 opens the Switchboard").arg(keys)));
        m_workChip->setProperty("state", m_ledger.chipState());
        m_workChip->style()->unpolish(m_workChip); m_workChip->style()->polish(m_workChip);
    }

    void fillWorkMenu(QMenu *menu) {
        menu->clear();
        auto &keys = Keymap::instance();
        // addSection draws a bare line in Relay's menu style, so each section is a disabled bold
        // row of its own, with a separator above it.
        auto section = [menu](const QString &title) {
            if (!menu->isEmpty()) menu->addSeparator();
            QAction *head = menu->addAction(title);
            head->setEnabled(false);
            QFont bold = head->font();
            bold.setBold(true);
            head->setFont(bold);
        };
        section(QStringLiteral("Issues"));
        if (m_workCards.isEmpty()) {
            menu->addAction(QStringLiteral("Type # in the prompt to reference a card"))->setEnabled(false);
        }
        for (const QString &id : m_workCards) {
            const relay::board::Card *card = m_cardIndex.card(id);
            const QString label = card ? QStringLiteral("#%1  %2  ·  %3").arg(id, card->title, relay::board::statusTitle(card->status))
                                       : QStringLiteral("#") + id;
            menu->addAction(label, this, [this, id] { if (onOpenCard) onOpenCard(id); });
        }
        section(QStringLiteral("Tasks"));
        QList<relay::TaskItem> tasks = m_ledger.tasks();
        int batch = 0;
        for (const auto &task : tasks) batch = std::max(batch, task.batch);
        int shown = 0;
        for (const auto &task : tasks) {
            if (task.batch != batch) continue;
            if (++shown > 8) break;
            QAction *row = menu->addAction(relay::RequestLedgerModel::todoGlyph(task.status) + QStringLiteral("  ") + task.text,
                                           this, [this] { openRequests(); });
            row->setToolTip(task.note);
        }
        if (!shown) menu->addAction(QStringLiteral("No task list yet"))->setEnabled(false);
        QAction *list = menu->addAction(QStringLiteral("Show task list"), this, [this] { toggleRequests(); });
        if (const QString k = keys.keysFor(QStringLiteral("agent.requests")).value(0); !k.isEmpty()) list->setShortcut(QKeySequence(k));
        section(QStringLiteral("Plan"));
        if (!m_lastPlanPath.isEmpty()) {
            const QString path = m_lastPlanPath;
            menu->addAction(QStringLiteral("Open %1").arg(QDir(m_workspace).relativeFilePath(path)), this,
                            [this, path] { if (onPlanWritten) onPlanWritten(path, this); });
        }
        QAction *plan = menu->addAction(QStringLiteral("Plan mode"), this, [this] { togglePlanMode(); });
        plan->setCheckable(true);
        plan->setChecked(m_agentMode == QStringLiteral("plan"));
        if (const QString k = keys.keysFor(QStringLiteral("agent.planToggle")).value(0); !k.isEmpty()) plan->setShortcut(QKeySequence(k));
        menu->addSeparator();
        QAction *board = menu->addAction(stripIcon(QStringLiteral("board")), QStringLiteral("Open the Switchboard"), this,
                                         [this] { if (onOpenBoard) onOpenBoard(); });
        if (const QString k = keys.keysFor(QStringLiteral("board.open")).value(0); !k.isEmpty()) board->setShortcut(QKeySequence(k));
    }

public:
    bool requestsOpen() const { return m_requestsPanel && m_requestsPanel->isVisible(); }
    // The short list people actually need, in the prompt box. Ctrl+? has the complete one.
    void toggleHelpCard() {
        if (m_helpCard && m_helpCard->isVisible()) { m_helpCard->hide(); return; }
        if (!m_helpCard) {
            m_helpCard = new QFrame(this);
            m_helpCard->setObjectName(QStringLiteral("helpCard"));
            m_helpCard->setAttribute(Qt::WA_StyledBackground);
            auto *box = new QVBoxLayout(m_helpCard);
            box->setContentsMargins(14, 10, 14, 10); box->setSpacing(4);
            auto &keys = Keymap::instance();
            auto row = [box](const QString &key, const QString &what) {
                auto *line = new QHBoxLayout; line->setSpacing(8);
                auto *chip = new QLabel(key); chip->setObjectName(QStringLiteral("keyCap"));
                chip->setTextFormat(Qt::PlainText);
                line->addWidget(chip);
                auto *text = new QLabel(what); text->setObjectName(QStringLiteral("helpText"));
                text->setTextFormat(Qt::PlainText);
                line->addWidget(text, 1);
                box->addLayout(line);
            };
            row(QStringLiteral("!"), QStringLiteral("run this line in the terminal"));
            row(QStringLiteral("*"), QStringLiteral("send this line to the agent"));
            row(QStringLiteral("/"), QStringLiteral("slash commands"));
            row(QStringLiteral("@"), QStringLiteral("attach files and folders"));
            row(QStringLiteral("#"), QStringLiteral("reference a Switchboard card"));
            row(keys.shortcutText(QStringLiteral("input.toggle")), QStringLiteral("switch terminal / agent"));
            row(keys.shortcutText(QStringLiteral("palette.open")), QStringLiteral("actions palette"));
            row(keys.shortcutText(QStringLiteral("board.open")), QStringLiteral("Switchboard: cards and threads"));
            // The explorer is one of the keys people reach for most and it was only in the full
            // list (owner, 2026-09-17). One key opens and closes it, which the wording has to say.
            row(keys.shortcutText(QStringLiteral("files.explorer")), QStringLiteral("file explorer (again to close)"));
            row(keys.shortcutText(QStringLiteral("agent.requests")).isEmpty() ? QStringLiteral("/tasks")
                                                                             : keys.shortcutText(QStringLiteral("agent.requests")),
                QStringLiteral("tasks in this session"));
            row(QStringLiteral("Ctrl+Shift+O"), QStringLiteral("search past conversations"));
            row(keys.shortcutText(QStringLiteral("control.human")), QStringLiteral("type into the terminal"));
            row(QStringLiteral("Esc"), QStringLiteral("stop the agent or the program"));
            // Ctrl+? is Ctrl+Shift+/ on most keyboards, so the card names every key that works
            // rather than only the first spelling (#T9ZS).
            const QStringList helpKeys = keys.shortcutTexts(QStringLiteral("help.shortcuts"));
            row(helpKeys.value(0), helpKeys.size() > 1
                    ? QStringLiteral("show all shortcuts (also %1)").arg(helpKeys.mid(1).join(QStringLiteral(", ")))
                    : QStringLiteral("show all shortcuts"));
            auto *hide = new QLabel(QStringLiteral("?  to hide this"));
            hide->setObjectName(QStringLiteral("helpFooter"));
            box->addWidget(hide);
        }
        m_helpCard->adjustSize();
        const QRect composer(m_composer->mapTo(this, QPoint(0, 0)), m_composer->size());
        const int w = std::min(std::max(360, m_helpCard->sizeHint().width()), std::max(360, composer.width() - 24));
        const int h = m_helpCard->sizeHint().height();
        m_helpCard->setGeometry(composer.left() + 12, std::max(0, composer.top() - h - 6), w, h);
        m_helpCard->show();
        m_helpCard->raise();
    }

    void toggleRequests() { if (requestsOpen()) closeRequests(); else openRequests(); }

    void openRequests() {
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        if (!m_requestsPanel) {
            auto *panel = new relay::RequestsPanel(&m_ledger, this);
            m_requestsPanel = panel;
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
    // The link text is white (owner, 2026-09-18): it continues the agent's turn, so it sits with
    // the agent's prose rather than the grey machinery around it.
    void printContinueLink() {
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.continue"));
        const QString fast = keys.isEmpty() ? QStringLiteral("/continue") : keys + QStringLiteral(" or /continue");
        if (!shellIdleAtPrompt()) { printInline(QStringLiteral("▸ Continue: %1 (Actions › Continue agent turn)\n").arg(fast), Ink::Note); return; }
        const QByteArray url = QStringLiteral("relay://continue/%1").arg(m_token).toUtf8();
        QByteArray out;
        if (!m_inlineOpen) { out += "\r\x1b[2K"; m_inlineOpen = true; m_atLineStart = true; }
        if (!m_atLineStart) out += "\r\n";
        out += "\x1b]8;;" + url + "\x1b\\" + inkCode(Ink::Agent) + QByteArray("▸ Continue") + "\x1b[0m" + "\x1b]8;;\x1b\\";
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
        // The worker writes worker.log itself; it needs this pane's id and the chosen detail level.
        // Passed per process rather than with qputenv, which would leak between panes.
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("RELAY_PANE_ID"), paneLogId());
        environment.insert(QStringLiteral("RELAY_LOG_LEVEL"), relay::log::levelName(relay::log::level()));
        m_worker.setProcessEnvironment(environment);
        relay::log::info(QStringLiteral("worker_start pane=%1 workspace_set=%2")
                             .arg(paneLogId()).arg(m_workspace.isEmpty() ? 0 : 1));
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
            stopTurnClock();
            relay::log::error(QStringLiteral("worker_exit pane=%1 code=%2 crashed=%3")
                                  .arg(paneLogId()).arg(code).arg(exit == QProcess::CrashExit ? 1 : 0));
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
        // tracks the working directory and command boundaries from them.
        qputenv("RELAY_SHELL_INTEGRATION",
                QSettings().value(QStringLiteral("terminal/shell_integration"), false).toBool() ? "1" : "0");
        m_backendOwned.reset(relay::createTerminalBackend(m_engineCore, m_terminalHost));
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
        // Alternate screen (vim, less, htop, tmux), reported by the emulator itself.
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
        // Clickable paths (issue YZTK): a file opens in a preview pane at its line, a folder in
        // an explorer pane, a URL in the browser. The engine only reports paths that exist.
        m_backend->onLinkActivated = [this](const QString &target, int line, int column) {
            Q_UNUSED(column);
            openOutputTarget(target, line, true);
        };
        // `#K7Q2` in the output is a card link when this pane's Switchboard index knows the id
        // (design section 5); the engine asks, the pane answers from the rows it has seen.
        m_backend->setCardLookup([this](const QString &id, QString *title) { return lookupOutputCard(id, title); });
        // The Bash integration changes to this pane's directory after loading the user's
        // configuration, so a shell started elsewhere still lands where the pane says.
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
                    onOpenPath(absolute, 0);
                    return;
                }
            }
            if (m_atList && m_atList->isVisible()) hideAtPopup();
            // Aliases (issue G8DK), in the order they can appear: a template already in the box
            // submits as itself so the worker quotes the values, then `/name`, and finally — once
            // the mode is known, below — the name typed on its own in terminal mode.
            if (submitAliasFields()) return;
            if (tryRunSlashCommand(m_editor->toPlainText())) return;
            if (tryRunAliasSlash(m_editor->toPlainText())) return;
            clearAiGhost();
        } else if (const SlashCommand *command = slashCommandFor(m_editor->toPlainText())) {
            setRouteText(QStringLiteral("COMMAND · /%1 · %2").arg(command->name, command->description));
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
        // The alias name typed on its own. It needs the resolved mode, so it sits after the mode
        // is worked out and after a waiting program has had its line (issue G8DK).
        if (submit && tryRunAliasTyped(m_editor->toPlainText(), mode)) {
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
        logEvent(type, event);
        // --- subagents UI: subagent_* events are consumed; main-agent state is observed first ---
        if (type == QStringLiteral("configured"))
            QTimer::singleShot(0, this, [this] {
                refreshAgentDefinitions();
                m_lastProgramState = QJsonObject();   // a fresh worker knows nothing about the pane
                sendProgramState();
            });
        if (handleBoardEvent(type, event)) return;   // Switchboard (protocol 17)
        if (m_subagents.handle(event)) return;
        // --- end subagents UI ---
        if (handleProgramEvent(type, event)) return;    // the agent typing into this pane's program
        if (handleRequestsEvent(type, event)) return;   // request ledger UI
        if (handleObservabilityEvent(type, event)) return;
        if (handleSessionEvent(type, event)) return;
        if (handleAliasEvent(type, event)) return;   // aliases (issue G8DK)
        if (type == QStringLiteral("ready")) {
            m_workerReady = true; requestRoute(false, QStringLiteral("auto"));
            send({{"type", "presets"}});
            refreshAliases();   // the palette and `/name` need the list before anything is typed
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
                    setRouteText(QStringLiteral("%1 · local guess · %2").arg(guess, why));
                    m_routeLabel->setToolTip(why);
                    QTimer::singleShot(150, this, [this, text = routedText, guess, why] {
                        if (m_editor->toPlainText() == text && !(m_assistText == text && !m_assistRoute.isEmpty()) && m_assistFailedText != text)
                            setRouteText(QStringLiteral("AUTO · checking… · local guess: %1 · %2").arg(guess.toLower(), why));
                    });
                    m_assistQueuedText = routedText;
                    m_assistLocalGuess = route;
                    m_assistDebounce.start();
                }
            } else if (id == m_pendingSubmit && haveAssist) {
                showAssistLabel();
            } else if (id == m_previewId || id == m_pendingSubmit) {
                if (route == QStringLiteral("shell") && !event.value(QStringLiteral("valid")).toBool(true))
                    setRouteText(QStringLiteral("TERMINAL · ") + event.value(QStringLiteral("invalid_reason")).toString()
                                         + (event.value(QStringLiteral("agent_signal")).toBool()
                                                ? QStringLiteral(" · this reads like a request for the agent")
                                                : QStringLiteral(" · the agent will fix it")));
                else
                    setRouteText(route.toUpper() + QStringLiteral(" · ") + event.value(QStringLiteral("reason")).toString());
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
            m_tierSummary = event.value(QStringLiteral("tiers")).toObject();
            if (m_rolesDialog) m_rolesDialog->setResolved(m_tierSummary, m_roleSummary);
            m_agentRole = event.value(QStringLiteral("agent_role")).toString(QStringLiteral("main"));
            onSessionConfigured(event);
            // No "Agent ready · <model>" here: the composer's own chips carry the model and the
            // agent role, so announcing it again only filled the window with a permanent line.
            changed();
        } else if (type == QStringLiteral("presets")) {
            m_presets = event.value(QStringLiteral("presets")).toArray();
            // Tier defaults and the Advanced action list come from the worker so the GUI never has
            // to keep a second copy of backend/relay_core/presets.py in step (protocol 13.7).
            m_tierCatalog = event.value(QStringLiteral("tier_defaults")).toObject();
            m_roleActions = event.value(QStringLiteral("role_actions")).toArray();
            m_stored.clear();
            for (const auto &item : m_presets) {
                const auto preset = item.toObject();
                if (preset.value(QStringLiteral("has_stored_key")).toBool())
                    m_stored.append({preset.value(QStringLiteral("id")).toString(), preset.value(QStringLiteral("label")).toString()});
            }
            if (m_keysDialog) m_keysDialog->setPresets(m_presets);
            if (m_rolesDialog) m_rolesDialog->setPresets(m_presets, m_tierCatalog, m_roleActions);
            changed();
            if (m_stored.isEmpty()) {
                status(QStringLiteral("No stored provider keys. Open Settings › Models › API keys… to add one or import from Warp."));
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
        } else if (type == QStringLiteral("transcribed")) {
            onTranscribed(event);
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
            startTurnClock();
            m_currentItem = event.value(QStringLiteral("id")).toString();
            QTimer::singleShot(0, this, [this] { rebuildQueueStrip(); });
            const PendingPrompt prompt = m_itemPrompts.value(m_currentItem);
            m_fixAwaitingAgent = prompt.fix;
            // Wrong-mode hints: the shell command this turn was submitted with, if any, so a
            // failing run_command of the same text can suggest the terminal (see tool_result).
            m_turnShellPrompt = prompt.shellText;
            m_modeHintShown = false;
            m_runCommands.clear();
            if (!prompt.program.isEmpty()) m_transcriptProgram = prompt.program;
            if (!prompt.fix && !prompt.text.isEmpty()) {
                ensureLineStart();
                printInline(QStringLiteral("✦ ") + prompt.text + '\n', Ink::UserAgent);
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
            m_turnShellPrompt.clear();
            m_runCommands.clear();
            if (m_currentItem == event.value(QStringLiteral("id")).toString()) m_currentItem.clear();
            m_agentBusy = !m_runningItem.isEmpty() && m_runningItem != event.value(QStringLiteral("id")).toString();
            if (!m_agentBusy) { stopTurnClock(); m_idleTip.start(); }
            if (outcome == QStringLiteral("done")) {
                ++m_turnsCompleted;
                if (window() && !window()->isActiveWindow()) m_finishedWhileAway = true;
                // A finished turn is only news when the user was not watching this pane.
                if (!watched() && !moreTurnsPending())
                    notify(QStringLiteral("Agent finished"), turnSummary(), relay::NotificationCenter::kindSuccess);
            } else if (outcome == QStringLiteral("error")) {
                notify(QStringLiteral("Agent turn failed"), turnSummary(), relay::NotificationCenter::kindError);
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
            // Collapsed by default (SWITCHBOARD-DESIGN.md 4.3): a tool's output is counted, not
            // poured into the pane, and the turn's "✦ N tool calls" line opens it in full. Card
            // #X5D1 read an earlier owner decision as "print all of it"; the owner corrected that
            // on 2026-09-17. Agent options › Show tool output brings the stream back.
            const QString text = event.value(QStringLiteral("text")).toString();
            m_toolLines += text.count('\n');
            m_toolPartialLine = !text.isEmpty() && !text.endsWith('\n');
            if (showToolOutput()) { turnHeader(); printInline(text, Ink::ToolOutput); }
        } else if (type == QStringLiteral("tool_started")) {
            turnHeader();
            m_toolLines = 0; m_toolPartialLine = false;
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
            // Wrong-mode hints: remember this run_command's full text (multi-line commands
            // continue in the body), keyed by call_id for the tool_result handler.
            if (title == QStringLiteral("RUN COMMAND"))
                m_runCommands.insert(event.value(QStringLiteral("call_id")).toString(),
                                     (head + (body.isEmpty() ? QString() : QStringLiteral("\n") + body.join(QLatin1Char('\n')))).trimmed());
            if (verb != QStringLiteral("$") && head.startsWith(m_workspace + '/')) head = head.mid(m_workspace.size() + 1);
            ensureLineStart();
            printInline(QStringLiteral("⚙ ") + verb + ' ' + head + '\n', Ink::Tool);
            // Multi-line commands continue; write diffs are colored. Skip the diff's blank separator.
            // Bounded like the output above: a long diff belongs in the turn pane, not the terminal.
            int shown = 0, skipped = 0;
            for (const QString &line : std::as_const(body)) {
                if (line.trimmed().isEmpty()) continue;
                if (!showToolOutput() && shown >= kInlineDiffLines) { ++skipped; continue; }
                Ink ink = Ink::ToolOutput;
                if (line.startsWith('+') && !line.startsWith(QStringLiteral("+++"))) ink = Ink::DiffAdd;
                else if (line.startsWith('-') && !line.startsWith(QStringLiteral("---"))) ink = Ink::DiffRemove;
                printInline(line + '\n', ink);
                ++shown;
            }
            if (skipped > 0) printInline(QStringLiteral("  … %1 more lines\n").arg(skipped), Ink::Note);
        } else if (type == QStringLiteral("tool_result")) {
            const auto result = event.value(QStringLiteral("result")).toObject();
            // Wrong-mode hints: a run_command that failed, while in agent mode, whose text is the
            // prompt the turn started from, means the submission was a shell command in the wrong
            // mode. At most once per turn.
            if (event.value(QStringLiteral("tool")).toString() == QStringLiteral("run_command")) {
                const QString runText = m_runCommands.take(event.value(QStringLiteral("call_id")).toString());
                if (!m_modeHintShown && !runText.isEmpty() && result.value(QStringLiteral("exit_code")).toInt() != 0
                    && m_modeValue == QStringLiteral("agent") && relay::input::commandMatchesPrompt(runText, m_turnShellPrompt)) {
                    m_modeHintShown = true;
                    wrongModeHint(false);
                }
            }
            ensureLineStart();
            // What the collapsed output cost: the result line carries the size the pane did not show.
            const int lines = m_toolLines + (m_toolPartialLine ? 1 : 0);
            const QString size = (lines > 0 && !showToolOutput())
                ? QStringLiteral(" · %1 %2").arg(lines).arg(lines == 1 ? QStringLiteral("line") : QStringLiteral("lines"))
                : QString();
            m_toolLines = 0; m_toolPartialLine = false;
            if (result.contains(QStringLiteral("error"))) printInline(QStringLiteral("✗ ") + result.value(QStringLiteral("error")).toString() + '\n', Ink::Error);
            else if (result.contains(QStringLiteral("exit_code"))) {
                const int code = result.value(QStringLiteral("exit_code")).toInt();
                printInline(QStringLiteral("exit %1%2%3%4\n").arg(code)
                    .arg(result.value(QStringLiteral("timed_out")).toBool() ? QStringLiteral(" · timed out") : QString())
                    .arg(result.value(QStringLiteral("truncated")).toBool() ? QStringLiteral(" · output truncated") : QString())
                    .arg(size),
                    code == 0 ? Ink::Note : Ink::Error);
            } else printInline(QStringLiteral("✓ ") + event.value(QStringLiteral("tool")).toString() + size + '\n', Ink::Note);
        } else if (type == QStringLiteral("vision_route")) {
            // Image context (protocol 17): this turn runs on another model because the pane's own
            // cannot read images. Said plainly, because the answer comes from a different model.
            ensureLineStart();
            printInline(QStringLiteral("🖼 ") + event.value(QStringLiteral("text")).toString() + '\n', Ink::Note);
            m_visionModel = event.value(QStringLiteral("model")).toString();
            refreshPickers();
        } else if (type == QStringLiteral("vision_route_ended")) {
            m_visionModel.clear();
            refreshPickers();
        } else if (type == QStringLiteral("vision_unavailable")) {
            // Refused, not failed: the `error` that follows carries the same text, so only the
            // "what to do about it" line is added here.
            ensureLineStart();
            printInline(QStringLiteral("🖼 No vision model · Settings › Models › Vision model\n"), Ink::Error);
        } else if (type == QStringLiteral("provider_retry")) {
            // The model went silent; the worker is retrying this turn once. Say so in the transcript.
            ensureLineStart();
            printInline(QStringLiteral("⚠ ") + event.value(QStringLiteral("text")).toString() + '\n', Ink::Note);
        } else if (type == QStringLiteral("status")) {
            const QString text = event.value(QStringLiteral("text")).toString();
            // While a turn runs the clock owns the status line; a step note rides along with it
            // instead of replacing the elapsed time.
            if (m_agentBusy && text.startsWith(QStringLiteral("Requesting model · "))) {
                m_turnStep = text.mid(QStringLiteral("Requesting model · ").size());
                tickTurnClock();
            } else {
                status(text);
            }
        } else if (type == QStringLiteral("done") || type == QStringLiteral("cancelled")) {
            stopTurnClock();
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
            if (!m_agentBusy) stopTurnClock();
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
            // Wrong-mode hints: agent_signal says the text reads like a request, not a command.
            const bool readsLikeRequest = decision.value(QStringLiteral("agent_signal")).toBool();
            if (mode == QStringLiteral("shell")) {
                // Terminal mode (Ctrl+Shift+Enter): always the terminal. An invalid command
                // goes to the agent to be fixed; a failing run is fixed and re-run.
                if (!valid && readsLikeRequest) {
                    // Wrong mode, not a broken command: nothing runs and nothing is cleared, so
                    // Ctrl+I followed by Enter resubmits the same text to the agent.
                    wrongModeHint(true);
                    ensureLineStart();
                    printInline(QStringLiteral("✗ %1 · this reads like a request for the agent, not a command\n")
                                    .arg(problem.isEmpty() ? QStringLiteral("not a valid command") : problem),
                                Ink::Error);
                    closeInline();
                    return;
                }
                m_editor->remember(text); m_editor->clear();
                if (!valid) { startFix(text, problem.isEmpty() ? QStringLiteral("not a valid command") : problem, 1); return; }
                submitTerminal(text, true, readsLikeRequest);
                return;
            }
            if (!valid) { submitAgent(text, true, problem); return; }
            submitTerminal(text, false);
        } else {
            // "agent", or a legacy "ambiguous" decision: the agent is the default for invalid input.
            // Show why non-command input went to the agent, e.g. "command not found: foo" — but only
            // when the line reads as an attempt at a command. Under a plain request the note reads as
            // the failure of a command the user never meant to run ("symlink from ~/projects to
            // here", answered fine, with "command not found: symlink" under it — owner report,
            // 2026-09-18). Which lines qualify is router.explain_invalid's call, next to the rest of
            // the language rules; an older worker that does not send it keeps the note.
            QString why = !decision.value(QStringLiteral("valid")).toBool(true) && mode != QStringLiteral("agent")
                              && decision.value(QStringLiteral("explain_invalid")).toBool(true)
                ? decision.value(QStringLiteral("invalid_reason")).toString() : QString();
            // Wrong-mode hints: remember a runnable command submitted in agent mode, so a failing
            // run_command of the same text can suggest the terminal (see the tool_result handler).
            const QString shellText = mode == QStringLiteral("agent")
                                      && decision.value(QStringLiteral("valid")).toBool(true)
                                      && !decision.value(QStringLiteral("agent_signal")).toBool()
                                      ? text : QString();
            submitAgent(text, true, why, QString(), shellText);
        }
    }

    bool runInTerminal(const QString &text, bool watch, int attempt, bool natural = false) {
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
        m_commandNatural = natural;   // wrong-mode hints: reads like a request (agent_signal)
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
        // A shell killed by a signal can leave the pane with no PID to ask about; use the last
        // PID the shell itself reported.
        if (m_shellPid > 0 && !QFileInfo::exists(QStringLiteral("/proc/%1").arg(m_shellPid))) { shellStopped(); return; }
        pid = m_shellPid;
        if (m_shellUnit.isEmpty()) return;
        const long kills = isolation::oomKills(pid);
        if (kills < 0) return;
        if (m_oomKills >= 0 && kills > m_oomKills) {
            showBanner(QStringLiteral("A command in this pane was stopped because it ran out of memory (limit %1). The shell is still running.")
                           .arg(isolation::memory("isolation/shell_memory_max", "8G")),
                       QString(), {});
            notify(QStringLiteral("Out of memory"), QStringLiteral("A command in %1 was stopped (limit %2).").arg(m_cwd, isolation::memory("isolation/shell_memory_max", "8G")),
                   relay::NotificationCenter::kindError);
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
        if (oom) notify(QStringLiteral("Out of memory"), QStringLiteral("The shell in %1 was stopped.").arg(m_cwd),
                        relay::NotificationCenter::kindError);
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
                notify(QStringLiteral("Password prompt"), QStringLiteral("A command in %1 is waiting for a password.").arg(m_cwd),
                       relay::NotificationCenter::kindWarning);
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
        // A password prompt ends any delegation: no model ever types into a masked prompt.
        endDelegation(QStringLiteral("password"));
        m_secretMode = true;
        m_secretProgram = program;
        hideAtPopup(); hideCardPopup(); hideSlashPopup(); hideTabPopup(); clearAiGhost();
        m_secretChip->setText(relay::input::passwordChip(m_secretProgram));
        m_secretChip->show();
        setRouteText(QStringLiteral("PASSWORD · the line goes to the program, not to Relay"));
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

    // Something worth telling the user about after the fact. The bell in the window header always
    // keeps it; the desktop only hears about it when Relay is not the active window (and desktop
    // alerts are on), which is what notify-send was always used for here.
    void notify(const QString &title, const QString &body,
                const QString &kind = relay::NotificationCenter::kindInfo) {
        relay::NotificationCenter::instance().post(title, body, kind, sessionToken());
        if (window() && window()->isActiveWindow()) return;
        QApplication::alert(window());
        if (!relay::NotificationCenter::desktopEnabled()) return;
        const QString notifier = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
        if (!notifier.isEmpty()) QProcess::startDetached(notifier, {QStringLiteral("-a"), QStringLiteral("Relay"), title, body});
    }

    // True while this pane is the one the user is looking at: routine notices (a finished agent
    // turn) are not worth a bell entry then, while failures are posted either way.
    bool watched() const {
        return window() && window()->isActiveWindow() && isVisible() && property("relayActive").toBool();
    }

    // The last line the agent wrote this turn, prefixed with the folder, for notification bodies.
    QString turnSummary() const {
        const QString folder = QDir(m_cwd).dirName();
        QString line;
        const QStringList lines = m_turnText.trimmed().split('\n');
        for (auto it = lines.crbegin(); it != lines.crend() && line.isEmpty(); ++it) line = it->trimmed();
        return line.isEmpty() ? folder : folder + QStringLiteral(" · ") + line.left(160);
    }

    static bool copyOnSelect() { return QSettings().value(QStringLiteral("terminal/copy_on_select"), false).toBool(); }

    // A backend with no "has selection" query copies nothing when nothing is selected.
    // Copy, and treat a clipboard change as proof that text was selected; the
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
    // Returns whether it was shown, so a caller can tie another visual (the mode chip flash) to
    // the same gates: per-hint limit, cooldown and the global "Shortcut hints" setting.
    bool hint(const QString &id, const QString &text, int limit = 3) {
        if (text.isEmpty()) return false;
        if (!relay::ShortcutHints::instance().shouldShow(id, limit)) return false;
        toast(text, 5000);
        return true;
    }

    // Wrong-mode hints (2026-09-17): a submission that errored and clearly belongs in the other
    // input mode. The mode chip flashes in the suggested mode's colour and a hint names
    // input.toggle, built from the live Keymap. Both go through the shortcut-hint gates, so a
    // user who turned hints off sees neither; an unbound action teaches nothing and stays quiet.
    void wrongModeHint(bool towardAgent) {
        const QString key = Keymap::instance().shortcutText(QStringLiteral("input.toggle"));
        if (key.isEmpty()) return;
        const QString text = towardAgent
            ? QStringLiteral("That read like a request, not a command · %1 switches to agent mode").arg(key)
            : QStringLiteral("Shell command in agent mode · %1 cycles input modes · ! runs one line in the terminal").arg(key);
        if (hint(towardAgent ? QStringLiteral("mode.requestInTerminal") : QStringLiteral("mode.commandInAgent"), text))
            flashModeChip(towardAgent ? QStringLiteral("agent") : QStringLiteral("shell"));
    }

    // Blink the input-mode chip for ~1.4 s in the destination mode's colour (Theme.cpp, the
    // stripChip[flash] rules), so the eye lands on the control that fixes the wrong mode.
    void flashModeChip(const QString &dest) {
        if (!m_modeChip) return;
        m_chipFlashDest = dest;
        if (!m_chipFlash) {
            m_chipFlash = new QTimer(this);
            m_chipFlash->setInterval(170);
            connect(m_chipFlash, &QTimer::timeout, this, [this] {
                m_chipFlashOn = --m_chipFlashLeft % 2 == 0 && m_chipFlashLeft > 0;
                if (m_chipFlashLeft <= 0) m_chipFlash->stop();
                applyChipFlash();
            });
        }
        m_chipFlashLeft = 8;   // four blinks
        m_chipFlashOn = true;
        applyChipFlash();
        m_chipFlash->start();
    }

    void applyChipFlash() {
        if (!m_modeChip) return;
        m_modeChip->setProperty("flash", m_chipFlashOn ? m_chipFlashDest : QVariant());
        m_modeChip->style()->unpolish(m_modeChip);
        m_modeChip->style()->polish(m_modeChip);
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
            {QStringLiteral("idle.board"), QStringLiteral("Tip: %1 opens the Switchboard; # references a card").arg(key("board.open"))},
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
        m_toast->show();
        placeToast();
        m_toastTimer.start(milliseconds);
    }

    // The toast sits at the terminal host's bottom-right corner. Re-anchored while it is up so a
    // composer that grows under it (the fix loop's agent transcript) neither strands nor buries it.
    void placeToast() {
        if (!m_toast) return;
        const QWidget *anchor = m_terminalHost ? m_terminalHost : this;
        const QPoint corner = anchor->mapTo(this, QPoint(anchor->width(), anchor->height()));
        m_toast->move(corner.x() - m_toast->width() - 16, corner.y() - m_toast->height() - 12);
        m_toast->raise();
    }
private:

    // Scroll the terminal's scrollback by one page (the engine's viewport).
    bool scrollTerminalPage(int direction) {
        if (!m_backend || !(m_backend->capabilities() & relay::TerminalBackend::ScrollControl)) return false;
        m_backend->scrollPages(direction);
        return true;
    }

    // ----- fix and re-run loop (terminal mode) -----------------------------------------
    static constexpr int kMaxFixAttempts = 3;

    void status(const QString &text) { if (onStatus) onStatus(text); }
    void changed() { refreshPickers(); refreshSessionControls(); if (onStateChanged) onStateChanged(); }

    // The strip stays quiet: one word ("TERMINAL", "AGENT", "COMMAND"), the full sentence on hover.
    // The caret and the syntax colouring follow the destination: white while auto has not decided,
    // cyan for the terminal, violet for the agent (owner design, 2026-09-17).
    void applyDestinationColor(relay::InputHighlighter::Destination destination) {
        if (!m_highlighter || !m_editor) return;
        m_highlighter->setDestination(destination);
        // Red for a command that does not resolve belongs to the chosen Terminal mode only.
        m_highlighter->setFlagUnknownCommands(m_modeValue == QStringLiteral("shell")
                                              || m_prefixMode == QStringLiteral("shell"));
        // Qt draws the caret in the widget's *stylesheet* colour, so the palette alone does nothing
        // here (the app stylesheet sets one). The highlighter gives every character an explicit
        // colour, so this only shows up in the caret and the placeholder.
        const QColor color = relay::InputHighlighter::colorFor(destination);
        m_editor->setStyleSheet(QStringLiteral("QPlainTextEdit { color: %1; }").arg(color.name()));
        m_editor->setCaretColor(color);
        if (m_modeChip) {
            const bool decided = destination != relay::InputHighlighter::Destination::Auto;
            m_modeChip->setProperty("dest", decided ? (destination == relay::InputHighlighter::Destination::Shell
                                                       ? QStringLiteral("shell") : QStringLiteral("agent"))
                                                    : QString());
            m_modeChip->style()->unpolish(m_modeChip); m_modeChip->style()->polish(m_modeChip);
        }
    }

    // The fixed modes decide on their own; auto waits for the router's verdict on this text.
    void refreshDestinationColor(const QString &verdict = QString()) {
        using Destination = relay::InputHighlighter::Destination;
        if (m_modeValue == QStringLiteral("shell")) { applyDestinationColor(Destination::Shell); return; }
        if (m_modeValue == QStringLiteral("agent")) { applyDestinationColor(Destination::Agent); return; }
        if (m_editor && m_editor->toPlainText().trimmed().isEmpty()) { applyDestinationColor(Destination::Auto); return; }
        if (verdict.startsWith(QStringLiteral("TERMINAL")) || verdict.startsWith(QStringLiteral("SHELL")))
            applyDestinationColor(Destination::Shell);
        else if (verdict.startsWith(QStringLiteral("AGENT")) || verdict.startsWith(QStringLiteral("COMMAND")))
            applyDestinationColor(Destination::Agent);
    }

    void setRouteText(const QString &full) {
        if (!m_routeLabel) return;
        if (full.startsWith(QStringLiteral("EMPTY"))) { m_routeLabel->clear(); m_routeLabel->setToolTip(QString()); return; }
        const QString head = full.section(QStringLiteral(" · "), 0, 0).trimmed();
        m_routeLabel->setText(head.isEmpty() ? full : head);
        m_routeLabel->setToolTip(full);
        refreshDestinationColor(head);
    }

    // ----- diagnostics log and the in-flight turn clock (issue SQAM) --------------------------
    // Short, stable id for this pane in relay.log and the worker's worker.log.
    QString paneLogId() const { return m_token.left(8); }

    // One line per protocol event. Types, ids and counts only: `delta`, `tool_output` and
    // `thinking_delta` carry the model's text and the shell's output, so they are never logged.
    void logEvent(const QString &type, const QJsonObject &event) {
        if (type == QStringLiteral("delta") || type == QStringLiteral("thinking_delta")
            || type == QStringLiteral("tool_output") || type == QStringLiteral("queue_changed"))
            return;
        const bool notable = type == QStringLiteral("agent_started") || type == QStringLiteral("agent_finished")
                          || type == QStringLiteral("error") || type == QStringLiteral("provider_retry")
                          || type == QStringLiteral("configured") || type == QStringLiteral("turn_summary")
                          || type == QStringLiteral("ready")
                          // A turn served by another model is worth a line: the answer did not come
                          // from the pane's own model (image context).
                          || type == QStringLiteral("vision_route") || type == QStringLiteral("vision_unavailable");
        QString line = QStringLiteral("event type=%1 pane=%2").arg(type, paneLogId());
        for (const char *field : {"turn_id", "outcome", "stop_reason", "tool", "model", "reason", "attempt"}) {
            const QJsonValue value = event.value(QLatin1String(field));
            if (!value.isUndefined() && !value.isNull())
                line += QStringLiteral(" %1=%2").arg(QString::fromLatin1(field), value.toVariant().toString());
        }
        if (type == QStringLiteral("error")) line += QStringLiteral(" msg=\"%1\"").arg(event.value(QStringLiteral("text")).toString().left(200));
        if (type == QStringLiteral("turn_summary")) line += QStringLiteral(" ms=%1").arg(event.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong());
        relay::log::write(type == QStringLiteral("error") ? relay::log::Level::Error
                          : notable ? relay::log::Level::Info : relay::log::Level::Debug, line);
    }

    // "thinking · 48 s · Esc stops" in the status line, and in the thinking overlay's header when
    // it is open, so a silent turn is never indistinguishable from a hung one.
    void startTurnClock() {
        m_turnElapsed.start();
        m_turnStep.clear();
        if (!m_turnClock) {
            m_turnClock = new QTimer(this);
            m_turnClock->setInterval(1000);
            connect(m_turnClock, &QTimer::timeout, this, [this] { tickTurnClock(); });
        }
        m_turnClock->start();
        tickTurnClock();
    }

    void stopTurnClock() {
        if (m_turnClock) m_turnClock->stop();
        m_turnStep.clear();
    }

    void tickTurnClock() {
        if (!m_agentBusy) { stopTurnClock(); return; }
        const qint64 seconds = m_turnElapsed.elapsed() / 1000;
        const QString stop = Keymap::instance().shortcutText(QStringLiteral("agent.stop"));
        const QString label = QStringLiteral("thinking · %1 s%2 · %3 stops")
                                  .arg(seconds)
                                  .arg(m_turnStep.isEmpty() ? QString() : QStringLiteral(" · ") + m_turnStep)
                                  .arg(stop.isEmpty() ? QStringLiteral("Esc") : stop);
        status(label);
        if (m_thinkingShown && m_thinkingHeader)
            m_thinkingHeader->setText(QStringLiteral("Thinking… · %1 · %2 s")
                                          .arg(m_model.isEmpty() ? QStringLiteral("agent") : m_model).arg(seconds));
    }

    void refreshPickers() {
        if (!m_modelBox) return;
        const QSignalBlocker modelBlock(m_modelBox);
        if (m_modeChip) {
            m_modeChip->setText(m_modeValue == QStringLiteral("shell") ? QStringLiteral("terminal")
                                : m_modeValue == QStringLiteral("agent") ? QStringLiteral("agent")
                                                                         : QStringLiteral("auto"));
            m_modeChip->setToolTip(QStringLiteral("Where this line goes (%1 cycles). %2")
                                       .arg(Keymap::instance().shortcutText(QStringLiteral("input.toggle")),
                                            m_routeLabel ? m_routeLabel->toolTip() : QString()).trimmed());
        }
        m_modelBox->clear();
        for (const auto &model : std::as_const(m_stored)) m_modelBox->addItem(conciseModel(model.first, model.second), model.first);
        if (m_stored.isEmpty()) m_modelBox->addItem(QStringLiteral("No stored keys"));
        const int index = m_modelBox->findData(m_currentPreset);
        if (index >= 0) m_modelBox->setCurrentIndex(index);
        // Last entry: the model options modal (default provider, Main/Flash/Lite, per-job overrides).
        // It stays reachable with no stored key, which is exactly when it is needed most.
        m_modelBox->insertSeparator(m_modelBox->count());
        m_modelBox->addItem(QString(QChar(0x2699)) + QStringLiteral("  Model options…"),
                            QStringLiteral("gear:modelOptions"));
        m_modelBox->setEnabled(true);
        // Model roles (protocol 13): a pane on another role shows that role and its model, and
        // picking a preset from the chip puts the pane back on the main agent.
        if (m_agentRole != QStringLiteral("main")) {
            const QString model = m_model.isEmpty() ? roleModel(m_agentRole) : m_model;
            m_modelBox->insertItem(0, QStringLiteral("%1 · %2").arg(roleLabel(m_agentRole), model), QStringLiteral("role:") + m_agentRole);
            m_modelBox->setCurrentIndex(0);
            m_modelBox->setEnabled(true);
        }
        // Image context (protocol 17): while a turn with an image runs on another model, the chip
        // says which one, so an answer never seems to come from the pane's own model.
        if (!m_visionModel.isEmpty()) {
            m_modelBox->insertItem(0, QStringLiteral("🖼 %1 · this turn").arg(m_visionModel),
                                   QStringLiteral("vision:") + m_visionModel);
            m_modelBox->setCurrentIndex(0);
        }
        m_modelBox->setToolTip(modelTooltip(m_visionModel.isEmpty()
            ? QString()
            : QStringLiteral("This turn carries an image, so it runs on %1 and then goes back.").arg(m_visionModel)));
    }

    // "glm-5.3", not "Z.AI · GLM-5.3 · Coding Plan": the model id from the worker's preset list,
    // which is what a person recognises. Falls back to the preset label.
    QString conciseModel(const QString &presetId, const QString &label) const {
        for (const auto &item : m_presets) {
            const QJsonObject preset = item.toObject();
            if (preset.value(QStringLiteral("id")).toString() != presetId) continue;
            const QString model = preset.value(QStringLiteral("model")).toString();
            if (!model.isEmpty()) return model.section('/', -1).toLower();
        }
        return label;
    }

    // Chip tooltip: the pane's model plus every role's effective model (protocol 13).
    QString modelTooltip(const QString &extra = QString()) const {
        QStringList lines{QStringLiteral("Agent model for this pane. Switching keeps the conversation."),
                          QStringLiteral("Reasoning effort: %1  (%2 / %3 to change)")
                              .arg(m_effort, Keymap::instance().shortcutText(QStringLiteral("agent.effortUp")),
                                   Keymap::instance().shortcutText(QStringLiteral("agent.effortDown")))};
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
    enum class Ink { Agent, User, UserAgent, Tool, ToolOutput, DiffAdd, DiffRemove, Error, Note, Recap };

    // Two levels (owner, 2026-09-18): the conversation carries colour — cyan for what the user
    // sent to the shell, violet for what they sent to the agent, white for the agent's prose —
    // and everything the machine did on its own (tools, tool output, recaps, notes) is the same
    // muted grey, so prose stands out and the amber/violet tokens keep their meanings (warn,
    // agent destination). Diffs keep the add/remove pair and failures keep red: content, not
    // chrome.
    // Agent lines follow the active theme: the colours come from the live tokens (src/Theme.h),
    // so a light theme gets dark text instead of the near-white a dark theme uses. Lines already
    // printed keep the colours they were written in; the terminal cannot recolour its scrollback.
    static QColor inkColor(Ink ink) {
        namespace t = relay::theme;
        switch (ink) {
        case Ink::Agent: return t::Text;
        case Ink::User: return t::Shell;
        case Ink::UserAgent: return t::Agent;
        case Ink::Tool: case Ink::ToolOutput: case Ink::Note: case Ink::Recap: return t::TextMuted;
        case Ink::DiffAdd: return t::Success;
        case Ink::DiffRemove: case Ink::Error: return t::Error;
        }
        return t::Text;
    }

    static QByteArray inkCode(Ink ink) {
        const QColor c = inkColor(ink);
        // Bold for the lines the user typed, italic for notes, plain otherwise.
        const QByteArray style = (ink == Ink::User || ink == Ink::UserAgent) ? QByteArray("1;")
                               : ink == Ink::Note ? QByteArray("3;") : QByteArray();
        return "\x1b[" + style + "38;2;" + QByteArray::number(c.red()) + ';' + QByteArray::number(c.green())
               + ';' + QByteArray::number(c.blue()) + 'm';
    }

    bool shellIdleAtPrompt() const {
        return m_backend && m_promptReported && !m_loading && !m_native
            && (foregroundPid() <= 0 || foregroundPid() == shellPid());
    }

    // Inline agent output: bytes go to the terminal emulator as if the program had printed
    // them. Nothing is typed into the shell, so agent text never reaches shell history and is
    // never executed: the engine feeds them to its parser.
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
        if (!m_transcriptDismissed && !m_transcript->isVisible()) m_transcript->show();
        updateTranscriptHeight();
    }

    // The panel is as tall as its text, never taller than 40% of the pane: an empty box over the
    // terminal is wasted space (owner, 2026-09-17), and agent output here is usually a few lines.
    void updateTranscriptHeight() {
        if (!m_transcript || !m_transcriptView || !m_transcript->isVisible()) return;
        const QFontMetrics metrics(m_transcriptView->font());
        const int line = std::max(14, metrics.lineSpacing());
        const auto *document = m_transcriptView->document();
        const int lines = std::max(1, int(std::ceil(document->size().height())));
        const int chrome = m_transcript->layout()->contentsMargins().top() + m_transcript->layout()->contentsMargins().bottom()
                           + m_transcriptHeader->sizeHint().height() + m_transcriptView->frameWidth() * 2 + 10;
        const int wanted = lines * line + chrome;
        const int cap = std::max(line * 3 + chrome, height() * 2 / 5);
        m_transcript->setMaximumHeight(std::min(wanted, cap));
        m_transcript->setMinimumHeight(0);
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
        // Agent prose is Markdown, rendered as it streams (MarkdownAnsi holds back only what it
        // cannot decide yet). Any other ink ends the Markdown run first, so held text lands before it.
        if (ink == Ink::Agent) {
            out += terminalLines(m_markdown.feed(clean));
            m_atLineStart = clean.endsWith('\n');
            writeTerminal(out);
            return;
        }
        out += terminalLines(m_markdown.finish());
        const QByteArray body = clean.toUtf8();
        out += inkCode(ink);
        for (const char ch : body) { if (ch == '\n') out += "\x1b[0m\r\n" + inkCode(ink); else out += ch; }
        out += "\x1b[0m";
        m_atLineStart = clean.endsWith('\n');
        writeTerminal(out);
    }

    static QByteArray terminalLines(const QString &rendered) {
        QByteArray bytes = rendered.toUtf8();
        bytes.replace('\n', "\r\n");
        return bytes;
    }

    void ensureLineStart() { if (m_inlineOpen && !m_atLineStart) printInline(QStringLiteral("\n"), Ink::Note); }

    void closeInline() {
        if (!m_inlineOpen) return;
        if (m_markdown.holding()) writeTerminal(terminalLines(m_markdown.finish()));
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

    // The turn's first line is machinery — which model is about to speak — not one of the user's
    // lines, so it is grey like the rest of the machine's own output, not cyan (owner, 2026-09-18).
    void turnHeader() {
        if (m_turnHeader) return;
        m_turnHeader = true;
        ensureLineStart();
        printInline(QStringLiteral("▸ ") + (m_model.isEmpty() ? QStringLiteral("agent") : m_model) + '\n', Ink::Note);
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
    void submitAgent(const QString &text, bool fromEditor, const QString &why = QString(), QString when = QString(),
                     const QString &shellText = QString()) {
        if (!m_configured) {
            if (fromEditor) { configure(); return; }
            status(QStringLiteral("No agent provider is configured."));
            return;
        }
        if (fromEditor) { m_editor->remember(text); m_editor->clear(); }
        QueueEntry entry;
        entry.agent = true; entry.text = text; entry.why = why; entry.attachments = attachmentsFor(text);
        entry.shellText = shellText;
        entry.cards = cardsFor(text);   // Switchboard: `#K7Q2` in the prompt (protocol 17.6)
        for (const QJsonValue &card : entry.cards) noteWorkCard(card.toObject().value(QStringLiteral("id")).toString());
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

    void submitTerminal(const QString &text, bool watch, bool natural = false) {
        if (!m_resubmitAtFront && m_entries.isEmpty() && !m_activeValid && shellIdleForQueue()) {
            runInTerminal(text, watch, 0, natural);
            return;
        }
        m_editor->remember(text);
        if (m_editor->toPlainText() == m_submittedDraft) m_editor->clear();
        QueueEntry entry; entry.agent = false; entry.text = text; entry.watch = watch; entry.natural = natural;
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
        prompt.text = entry.text; prompt.why = entry.why; prompt.fix = entry.fix; prompt.shellText = entry.shellText;
        if (!entry.fix) m_subagents.clearFinished();   // subagents UI: finished rows linger until a new user turn
        QJsonObject request{{"type", "ask"}, {"text", entry.text}, {"when", when}};
        if (!entry.attachments.isEmpty()) request.insert(QStringLiteral("attachments"), entry.attachments);
        if (!entry.cards.isEmpty()) request.insert(QStringLiteral("cards"), entry.cards);
        const QString program = processBusy() ? foregroundCommandLine() : QString();
        // The terminal's directory always goes along: `cd` in the terminal must move the agent too.
        QJsonObject context{{"terminal_cwd", m_cwd}};
        if (!program.isEmpty()) {
            // Tell the agent what owns the terminal and that it cannot see or type into it yet.
            context.insert(QStringLiteral("foreground_program"), program);
            prompt.program = QFileInfo(program.section(' ', 0, 0)).fileName();
        }
        // When the user has handed the program over, the same note carries the permission, the
        // question and the screen (card C1HH). Without it none of that is sent.
        const QJsonObject grant = programGrant();
        if (!grant.isEmpty()) context.insert(QStringLiteral("program_control"), grant);
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
        if (!runInTerminal(head.text, head.watch, 0, head.natural)) { m_entries.prepend(head); m_activeValid = false; return; }
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
        // `!` and `*` work from every mode: in Terminal mode `*` sends this one line to the agent,
        // in Agent mode `!` runs this one line in the terminal (owner, 2026-09-17).
        if (m_prefixMode.isEmpty() && m_editor->toPlainText().isEmpty() && !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
            && (key->text() == QStringLiteral("!") || key->text() == QStringLiteral("*"))) {
            setPrefixMode(key->text() == QStringLiteral("!") ? QStringLiteral("shell") : QStringLiteral("agent"));
            return true;
        }
        // Warp-style: "?" in an empty prompt box shows the main keys, "?" or Esc hides them again.
        if ((k == Qt::Key_Question || key->text() == QStringLiteral("?"))
            && m_editor->toPlainText().isEmpty() && !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            toggleHelpCard();
            return true;
        }
        if (m_helpCard && m_helpCard->isVisible() && k == Qt::Key_Escape && mods == Qt::NoModifier) {
            m_helpCard->hide();
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
        // An alias's parameters are filled in the prompt box, and Tab moves between them (issue
        // G8DK). It claims Tab before path completion, and Shift+Tab before `agent.planToggle`,
        // for as long as a template is in the box; the moment the line stops matching the
        // template both go back to what they were.
        if (!m_native && aliasFieldsActive() && (k == Qt::Key_Tab || k == Qt::Key_Backtab)
            && (mods == Qt::NoModifier || mods == Qt::ShiftModifier)
            && moveAliasField(mods == Qt::NoModifier && k == Qt::Key_Tab))
            return true;
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
        if (m_cardList && m_cardList->isVisible()) {
            if (k == Qt::Key_Down || k == Qt::Key_Up) {
                const int row = std::clamp(m_cardList->currentRow() + (k == Qt::Key_Down ? 1 : -1), 0, m_cardList->count() - 1);
                m_cardList->setCurrentRow(row);
                return true;
            }
            if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Tab) { acceptCardSelection(); return true; }
            if (k == Qt::Key_Escape) { m_cardDismissedAt = m_editor->textCursor().position(); hideCardPopup(); return true; }
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
        if (m_editor->toPlainText().trimmed().isEmpty()) refreshDestinationColor();
        if (!m_editor->toPlainText().isEmpty()) m_idleTip.stop();
        if (!m_editor->toPlainText().isEmpty()) clearAiGhost();
        updateSlashPopup();
        if (m_selected >= 0 && !m_editor->toPlainText().isEmpty()) { m_selected = -1; rebuildQueueStrip(); }
        updateAtPopup();
        updateCardPopup();
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

    // "Next time: Ctrl+Shift+S" after the slow path (WARP.md's standing rule).
    void boardShortcutHint(const QString &id) {
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("board.open"));
        if (!keys.isEmpty())
            hint(id, relay::ShortcutHints::nextTime(keys, QStringLiteral("the Switchboard")));
    }

    // Switchboard events a *terminal* pane cares about: the card index behind the `#` picker,
    // and one inline line per agent write (protocol 17.2 and 17.5).
    bool handleBoardEvent(const QString &type, const QJsonObject &event) {
        if (type == QStringLiteral("board")) {
            m_cardIndex.setConfig(event.value(QStringLiteral("config")).toObject());
            m_cardIndex.reset(event.value(QStringLiteral("cards")).toArray());
            return true;
        }
        if (type == QStringLiteral("board_changed")) {
            m_cardIndex.upsert(event.value(QStringLiteral("upserts")).toArray());
            QStringList removed;
            for (const QJsonValue &value : event.value(QStringLiteral("removed")).toArray())
                removed << value.toString();
            m_cardIndex.remove(removed);
            return true;
        }
        if (type == QStringLiteral("board_activity")) {
            noteBoardActivity(event);
            return true;
        }
        return false;
    }

    // ----- Switchboard: `#K7Q2` references (design section 5) ---------------------------------
    // `#` after a space, in agent or auto mode, opens a card picker like the `@` file picker.
    // In terminal mode `#` stays a Bash comment.
    void updateCardPopup() {
        if (!m_editor || m_native || m_modeValue == QStringLiteral("shell")) { hideCardPopup(); return; }
        const QTextCursor cursor = m_editor->textCursor();
        const QString before = cursor.block().text().left(cursor.positionInBlock());
        static const QRegularExpression token(QStringLiteral("(?:^|\\s)#([0-9A-Za-z]*)$"));
        const auto match = token.match(before);
        if (!match.hasMatch() || cursor.position() == m_cardDismissedAt) { hideCardPopup(); return; }
        requestCardIndex();
        const QList<relay::board::Card> ranked = m_cardIndex.search(match.captured(1), 20);
        if (ranked.isEmpty()) { hideCardPopup(); return; }
        if (!m_cardList) {
            m_cardList = new QListWidget(this);
            m_cardList->setObjectName(QStringLiteral("atPicker"));
            m_cardList->setFocusPolicy(Qt::NoFocus);
            m_cardList->setUniformItemSizes(true);
            connect(m_cardList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
                m_cardList->setCurrentItem(item); acceptCardSelection(); });
        }
        m_cardList->clear();
        for (const relay::board::Card &card : ranked) {
            auto *item = new QListWidgetItem(
                QStringLiteral("#%1  %2  ·  %3").arg(card.id, card.title,
                                                     relay::board::statusTitle(card.status)), m_cardList);
            item->setData(Qt::UserRole, card.id);
        }
        m_cardList->setCurrentRow(0);
        placeCardPopup();
        m_cardList->show();
        m_cardList->raise();
        if (m_editor->ghost().size()) m_editor->setGhost(QString());
    }

    void placeCardPopup() {
        if (!m_cardList || !m_composer) return;
        const QRect composer(m_composer->mapTo(this, QPoint(0, 0)), m_composer->size());
        const int rowHeight = std::max(18, m_cardList->sizeHintForRow(0));
        const int height = std::min(8, m_cardList->count()) * rowHeight + 8;
        const int width = std::min(640, composer.width() - 24);
        m_cardList->setGeometry(composer.left() + 12, std::max(0, composer.top() - height - 4), width, height);
    }

    void hideCardPopup() { if (m_cardList && m_cardList->isVisible()) m_cardList->hide(); }

    void acceptCardSelection() {
        if (!m_cardList || !m_cardList->currentItem()) return;
        const QString id = m_cardList->currentItem()->data(Qt::UserRole).toString();
        QTextCursor cursor = m_editor->textCursor();
        const QString before = cursor.block().text().left(cursor.positionInBlock());
        const int at = before.lastIndexOf('#');
        if (at < 0) { hideCardPopup(); return; }
        cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, before.size() - at);
        cursor.insertText(QStringLiteral("#") + id + ' ');
        m_editor->setTextCursor(cursor);
        hideCardPopup();
    }

    // The board rows this pane knows, for the picker and for `ask {cards: […]}`. Asked for once
    // per conversation and kept up to date by board_changed.
    void requestCardIndex() {
        if (m_cardIndexAsked || !m_configured) return;
        m_cardIndexAsked = true;
        send({{QStringLiteral("type"), QStringLiteral("board_open")}});
    }

    // "#K7Q2" or "#K7Q2 · Voice transcription": how a reference reads in a tooltip or a status
    // line, with the title only when the board knows one.
    static QString cardReferenceLabel(const QString &id, const QString &title) {
        return title.isEmpty() ? QStringLiteral("#") + id : QStringLiteral("#%1 · %2").arg(id, title);
    }

    // Which `#K7Q2` in this pane's *output* is a link, and what it is called: the engine's link
    // scanner asks this (`relay::links::CardLookup`, src/OutputLinks.h) and leaves every id the
    // board does not know as plain text, so an `#ABCD` nobody filed stays text.
    //
    // The index arrives only when something asks for it, and a pane whose agent never typed `#`
    // has never asked — so the first reference-shaped span in its output asks now and links from
    // the next hover on, rather than never (2026-09-18).
    bool lookupOutputCard(const QString &id, QString *title) {
        if (m_cardIndex.total() == 0) requestCardIndex();
        const relay::board::Card *card = m_cardIndex.card(id);
        if (!card) return false;
        if (title) *title = card->title;
        return true;
    }

    // `#K7Q2` tokens that name a card travel with an agent prompt (protocol 17.6).
    QJsonArray cardsFor(const QString &text) const {
        static const QRegularExpression token(QStringLiteral("(?:^|\\s)#([0-9A-Za-z]{4})\\b"));
        QJsonArray out;
        QStringList seen;
        auto it = token.globalMatch(text);
        while (it.hasNext()) {
            const QString id = it.next().captured(1).toUpper();
            if (seen.contains(id) || !m_cardIndex.card(id) || out.size() >= 10) continue;
            seen << id;
            out.append(QJsonObject{{QStringLiteral("id"), id}});
        }
        return out;
    }

    // One inline line per agent board write, in the pane that caused it (protocol 17.5).
    void noteBoardActivity(const QJsonObject &event) {
        const QString id = event.value(QStringLiteral("id")).toString();
        const QString summary = event.value(QStringLiteral("summary")).toString();
        if (id.isEmpty()) return;
        m_cardIndexAsked = false;   // the rows changed; refresh the picker on its next use
        // A pane with no rows at all asks for them now, so the `◆ #K7Q2` line it is about to
        // print is a link straight away rather than text until someone opens the picker. Once
        // it has rows, `board_changed` keeps them current and no snapshot is needed.
        if (m_cardIndex.total() == 0) requestCardIndex();
        noteWorkCard(id);
        const QString line = QStringLiteral("◆ #%1 · %2").arg(id, summary);
        status(line);
        toast(line, 4000);
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
        // The word to replace is the one under the cursor *now*, not the one the popup opened on:
        // Tab has since filled in the candidates' common prefix, and the user may have typed more.
        // Replacing the stale range left the difference behind — "cd 2026-09-18-EG/G", and with
        // folders that diverge at a dash, the "cd 2026-09-18-EG/-" of the owner's report.
        const QTextCursor cursor = m_editor->textCursor();
        const relay::Completion live =
            relay::completeAt(cursor.block().text(), cursor.positionInBlock(), m_cwd, knownCommandNames());
        replaceComposerToken(live, insert + (insert.endsWith('/') ? QString() : QStringLiteral(" ")));
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

    // ----- image context (issue EM1E) --------------------------------------------------------
    //
    // Four ways in, one path out: paste, drop, `@path` and "Screenshot this pane" all end as an
    // image file whose path sits in the composer as an `@` token, so the worker's attachment
    // plumbing carries it and picks the model (docs/AGENT-SESSIONS-PROTOCOL.md section 17).

    // Called by the composer for every paste and drop. Returns the `@` tokens to insert, or an
    // empty list when there is no image, which lets the ordinary text paste run.
    QStringList attachImages(const QMimeData *data, bool dropped) {
        if (!relay::images::hasImage(data)) return {};
        const QString dir = relay::images::cacheDir();
        relay::images::pruneCache(dir, relay::images::kKeepDays, QDateTime::currentDateTime());
        const QStringList paths = relay::images::fromMimeData(data, dir);
        if (paths.isEmpty()) {
            status(QStringLiteral("That image could not be attached (it may be larger than %1 MiB).")
                       .arg(relay::images::kMaxImageBytes / (1024 * 1024)));
            return {};
        }
        QStringList tokens, tooBig;
        for (const QString &path : paths) {
            if (QFileInfo(path).size() > relay::images::kMaxImageBytes) { tooBig << QFileInfo(path).fileName(); continue; }
            tokens << relay::images::composerToken(path);
        }
        if (!tooBig.isEmpty())
            status(QStringLiteral("Too large to send (over %1 MiB): %2")
                       .arg(QString::number(relay::images::kMaxImageBytes / (1024 * 1024)),
                            tooBig.join(QStringLiteral(", "))));
        if (tokens.isEmpty()) return {};
        noteImagesAttached(tokens.size(), dropped);
        return tokens;
    }

    // Says what happened, and — per the standing shortcut-hints rule — teaches the faster path:
    // dragging a file is the slow way to do what one paste does.
    void noteImagesAttached(int count, bool dropped) {
        status(count == 1 ? QStringLiteral("Image attached · it goes to the agent with your next prompt")
                          : QStringLiteral("%1 images attached · they go to the agent with your next prompt").arg(count));
        if (dropped) {
            const QString paste = QKeySequence(QKeySequence::Paste).toString(QKeySequence::NativeText);
            hint(QStringLiteral("images.drop"),
                 relay::ShortcutHints::nextTime(paste, QStringLiteral("paste an image straight into the prompt box")));
        }
        focusInput();
    }

public:
    // "Screenshot this pane": grabs this pane as it is drawn, writes a PNG and attaches it. The
    // point is showing the agent what the terminal looks like, so the whole pane is captured.
    // Run from the palette, the Actions list or its shortcut, so it is part of the pane's API.
    void screenshotPane() {
        const QString dir = relay::images::cacheDir();
        relay::images::pruneCache(dir, relay::images::kKeepDays, QDateTime::currentDateTime());
        const QPixmap shot = grab();
        const QString path = relay::images::savePng(shot.toImage(),
            relay::images::newCapturePath(dir, QStringLiteral("pane"), QDateTime::currentDateTime()));
        if (path.isEmpty()) {
            status(QStringLiteral("The pane screenshot could not be saved."));
            return;
        }
        QTextCursor cursor = m_editor->textCursor();
        cursor.movePosition(QTextCursor::End);
        const QString before = m_editor->toPlainText();
        const QString lead = (before.isEmpty() || before.endsWith(QLatin1Char(' '))
                              || before.endsWith(QLatin1Char('\n'))) ? QString() : QStringLiteral(" ");
        cursor.insertText(lead + relay::images::composerToken(path) + QLatin1Char(' '));
        m_editor->setTextCursor(cursor);
        // Reached from the palette, the palette's own shortcut hint follows this line (see
        // RelayWindow::activateSelected), so Ctrl+Shift+G is what stays on screen.
        status(QStringLiteral("Pane screenshot attached · describe what you want done with it"));
        focusInput();
    }

private:
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
    // Called from the backend's onAltScreenChanged.
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
            m_programPoll.stop(); endWaiting(true); updateOpaqueProgram(); checkPasswordPrompt();
            // The program is gone: the screen detection and the agent's permission go with it.
            endDelegation(QStringLiteral("program_exited"));
            updateScreenPrompt();
            updateTakeControl();
            return;
        }
        updateOpaqueProgram();
        checkPasswordPrompt();
        updateScreenPrompt();
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
            if (m_screenPrompt.actionable() && !m_screenPrompt.masked) {
                // The screen says a program is asking for a line. This is the case /proc cannot
                // see: `sudo` runs apt in its own pseudo-terminal, so nothing Relay may inspect
                // is blocked in read(). Two ticks (~0.5 s) of agreement before the hint appears,
                // so a question that scrolls past during a download never raises one (card YR21).
                if (++m_waitTicks >= 2) startWaiting();
            } else if (!m_opaqueProgram.isEmpty()) {
                // sudo & co.: Relay cannot read their syscalls, so it cannot see them waiting.
                // A password prompt is still visible in the line discipline (checkPasswordPrompt);
                // anything else queues, and the hint in the composer row says so.
                hint(QStringLiteral("queue.whileRunning"),
                     QStringLiteral("Tip: type the next command here while %1 runs · it is queued until the terminal is free")
                         .arg(m_opaqueProgram));
            } else if (programWaitingForInput()) {
                if (++m_waitTicks >= 2) startWaiting();
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
        // What the screen classifier made of the last rows (src/ScreenPrompt.h). Both stay false
        // on engines that cannot read the screen, which leaves the /proc-only rules unchanged.
        state.screenAsking = m_screenPrompt.actionable() && !m_screenPrompt.masked;
        state.screenMasked = m_screenPrompt.masked && m_screenPrompt.actionable();
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
            const QString program = foregroundProgramName();
            const QString who = program.isEmpty() ? QStringLiteral("The program") : program;
            if (m_delegated) {
                const QString driven = !m_delegatedProgram.isEmpty() ? m_delegatedProgram
                                       : program.isEmpty() ? QStringLiteral("the program") : program;
                text = QStringLiteral("The agent is driving %1 · %2 takes it back")
                           .arg(driven, Keymap::instance().shortcutText(QStringLiteral("control.human")));
            } else if (m_screenPrompt.actionable())
                // Read off the screen, so it names the question: "apt is asking: … [Y/n]".
                text = relay::screen::waitingLine(program, m_screenPrompt);
            else if (m_waiting)
                text = QStringLiteral("%1 is waiting for input · Enter sends your line to it").arg(who);
            else if (!m_opaqueProgram.isEmpty())
                text = QStringLiteral("%1 is running · prompts queue until it exits · %2 to type into it")
                           .arg(m_opaqueProgram, Keymap::instance().shortcutText(QStringLiteral("control.human")));
        }
        m_opaqueHint->setText(text);
        m_opaqueHint->setVisible(!text.isEmpty());
        // m_routeLabel is not shown here: it is the mode chip's tooltip, not a widget on the row.
    }

    // A program is blocked reading a line (`apt`'s `[Y/n]`). The prompt box keeps the keyboard;
    // what is submitted there answers the program instead of being queued (issue decision 5).
    void startWaiting() {
        if (m_waiting) return;   // the poll re-checks every tick; the toast is shown once
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

struct PendingPrompt { QString text, why, program; bool fix = false; QString shellText; };

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

    // The queue strip under the terminal: what is running, then queued terminal commands ($,
    // amber) and agent prompts (✦, cyan) in order. Rows drag to reorder; × removes. It is a row of
    // the pane's column rather than an overlay, so showing it moves the terminal up instead of
    // covering its last lines (owner report, 2026-09-18), and the show runs through
    // keepPaneSizes() because the row's height is part of this pane's minimum height (#G152).
    void rebuildQueueStrip() {
        if (!m_queueStrip) return;
        const bool visible = !m_entries.isEmpty() || m_entriesPaused || !m_steering.isEmpty();
        auto *layout = static_cast<QVBoxLayout *>(m_queueStrip->layout());
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (QWidget *w = item->widget()) { if (w != m_queueList) w->deleteLater(); }
            else if (QLayout *l = item->layout()) {
                while (QLayoutItem *inner = l->takeAt(0)) { if (inner->widget()) inner->widget()->deleteLater(); delete inner; }
            }
            delete item;
        }
        if (visible) showBubble(m_queueStrip); else hideBubble(m_queueStrip);
        if (!visible) return;
        auto *header = new QHBoxLayout;
        auto *title = new QLabel(m_entriesPaused ? QStringLiteral("QUEUE · PAUSED") : QStringLiteral("QUEUE"));
        title->setObjectName(QStringLiteral("queueTitle"));
        title->setToolTip(m_pauseReason);
        header->addWidget(title, 1);
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

    // The height the queue strip asks the pane's column for: what its rows need, still capped at
    // half of what it shares with the terminal. A row now rather than an overlay, so this is what
    // moves the terminal up instead of covering it (owner report, 2026-09-18: "the terminal needs
    // to move up, rather than being covered up").
    void placeQueueStrip() {
        QTimer::singleShot(0, this, [this] { placeThinking(); });
        if (!m_queueStrip || !m_queueStrip->isVisible() || !m_terminalHost) return;
        setBubbleHeight(m_queueStrip, std::min(m_queueStrip->sizeHint().height(), bubbleSpan() / 2));
    }

    // The "Take control (Ctrl+H)" button floats over the top-right of the terminal, so it does
    // not take layout space away from the program drawing there.
    void placeTakeControl() {
        if (!m_programBar || !m_programBar->isVisible() || !m_terminalHost) return;
        const QRect host(m_terminalHost->mapTo(this, QPoint(0, 0)), m_terminalHost->size());
        const QSize size = m_programBar->sizeHint();
        const int width = std::min(size.width(), std::max(240, host.width() - 20));
        m_programBar->setGeometry(host.right() - width - 10, host.top() + 8, width, size.height());
        m_programBar->raise();
    }

    // Shown while a full-screen program (alternate screen) or a remote session owns the terminal
    // and the prompt box still has the keyboard.
    void updateTakeControl() {
        if (!m_programBar) return;
        const relay::input::State state = inputState(false);
        const QString program = foregroundProgramName();
        const QString who = program.isEmpty() ? QStringLiteral("the program") : program;
        const QString question = relay::screen::bannerText(program, m_screenPrompt);
        const bool offerControl = relay::input::offerTakeControl(state, m_remoteProgram);
        // The banner appears whenever Relay has something to say about the program in this pane:
        // a question it read off the screen, a full-screen or remote program the prompt box is
        // holding the keyboard for, or the agent driving it.
        const bool show = !m_native && !m_secretMode && processBusy() && (offerControl || m_delegated || !question.isEmpty());
        if (show) {
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("control.human"));
            m_takeControl->setText(keys.isEmpty() ? QStringLiteral("Take control") : QStringLiteral("Take control (%1)").arg(keys));
            m_takeControl->setToolTip(QStringLiteral("Hide the prompt box and type into %1").arg(who));
            // While the agent drives, the delegate button already reads "Take over", which does
            // the same thing; two buttons with one shortcut would only be noise.
            m_takeControl->setVisible(offerControl && !m_delegated);
            QString label = question.isEmpty() ? QStringLiteral("%1 is running").arg(who) : question;
            if (m_delegated)
                label = m_agentWrites > 0
                    ? QStringLiteral("Agent driving %1 · %2 keystroke(s) · %3").arg(who).arg(m_agentWrites).arg(label)
                    : QStringLiteral("Agent driving %1 · %2").arg(who, label);
            m_programLabel->setText(m_programLabel->fontMetrics().elidedText(
                label, Qt::ElideRight, std::max(200, width() - 320)));
            m_programLabel->setToolTip(label);
            updateDelegateButton(who);
            m_programBar->adjustSize();
        }
        m_programBar->setVisible(show);
        placeTakeControl();
    }

    // "Let the agent drive" / "Take over", and an honest explanation when this pane's engine
    // cannot show the agent the screen.
    void updateDelegateButton(const QString &who) {
        if (!m_delegateButton) return;
        if (m_delegated) {
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("control.human"));
            m_delegateButton->setText(keys.isEmpty() ? QStringLiteral("Take over") : QStringLiteral("Take over (%1)").arg(keys));
            m_delegateButton->setToolTip(QStringLiteral("Stop the agent typing into %1 and take the keyboard").arg(who));
            m_delegateButton->setEnabled(true);
            m_delegateButton->show();
            return;
        }
        const bool masked = m_secretMode || m_screenPrompt.masked;
        const bool possible = canShowAgentTheScreen() && !masked;
        m_delegateButton->setText(QStringLiteral("Let the agent drive"));
        m_delegateButton->setEnabled(possible);
        m_delegateButton->setToolTip(
            masked ? QStringLiteral("Relay never lets the agent type into a password prompt.")
            : !canShowAgentTheScreen()
                ? QStringLiteral("This pane cannot let Relay read the screen, so the agent cannot see %1.").arg(who)
                : QStringLiteral("Let the agent type into %1 · %2 takes it back")
                      .arg(who, Keymap::instance().shortcutText(QStringLiteral("control.human"))));
        m_delegateButton->setVisible(!masked);
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

    void refreshStatusStrip() {
        if (m_interruptButton) m_interruptButton->setVisible(processBusy());
    }

    void refreshShellReady() {
        refreshStatusStrip();
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
            if (m_highlighter) m_highlighter->setKnownCommands(knownCommandNames());
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
                if (ms > 30000) notify(QStringLiteral("Command finished"), QStringLiteral("Exit %1 after %2 s in %3").arg(status).arg(ms / 1000).arg(m_cwd),
                                       status == 0 ? relay::NotificationCenter::kindSuccess : relay::NotificationCenter::kindWarning);
            }
            m_programPoll.stop();
            endWaiting(true);
            updateOpaqueProgram();
            // The program is gone: the screen has nothing to ask and the agent's permission to
            // type into it ends with it (cards YR21, C1HH).
            endDelegation(QStringLiteral("program_exited"));
            updateScreenPrompt();
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
                    // Wrong-mode hints: the command ran and failed but read like a request, so the
                    // pane is probably in the wrong input mode. The fix attempt continues regardless.
                    if (m_commandNatural && m_modeValue == QStringLiteral("shell")) wrongModeHint(true);
                    // Defer until Readline has drawn the prompt and put the tty in raw mode.
                    QTimer::singleShot(200, this, [this, command, attempt, status] {
                        startFix(command, QStringLiteral("exited with status %1").arg(status), attempt + 1);
                    });
                }
            }
            m_commandNatural = false;   // consumed by this completion either way
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

    // Hiding or showing the prompt box changes this pane's minimum size, and every splitter above
    // it then redistributes the panes: taking control in a three-pane row shrank the last pane to
    // almost nothing (#G152), and the saved window layout stored that collapsed size. Run the
    // change with the enclosing splitters' sizes frozen and put them back, once straight away and
    // once after the layout has run, because the new minimum only reaches the splitter then.
    void keepPaneSizes(const std::function<void()> &change) {
        const QList<QPointer<QSplitter>> splitters = relay::panes::enclosingSplitters(this);
        QList<QList<int>> sizes;
        for (const auto &splitter : splitters) sizes.append(splitter ? splitter->sizes() : QList<int>());
        change();
        auto restore = [splitters, sizes] { relay::panes::restoreSizes(splitters, sizes); };
        restore();
        if (!splitters.isEmpty()) QTimer::singleShot(0, this, restore);
    }

    void setNative(bool enabled, bool cancelLine = true) {
        // Taking control leaves masked input: the password is then typed into the program itself.
        if (enabled && m_secretMode) { m_secretDeclined = true; leaveSecretMode(); }
        // The one rule the agent cannot argue with: the moment the user has the keyboard, the
        // agent stops typing. Ctrl+H, the button and F12 all come through here.
        if (enabled) endDelegation(QStringLiteral("take_over"));
        m_native = enabled;
        changed();
        m_editor->setReadOnly(enabled);
        // Human control hides the prompt box entirely; the terminal gets the space and the keys.
        if (m_composer) keepPaneSizes([this, enabled] { m_composer->setVisible(!enabled); });
        placeSubagentsPanel();   // subagents UI: hidden with the composer
        if (!enabled) m_hideReason = HideReason::None;
        if (enabled) hideAtPopup();
        applyTerminalFocusPolicy();
        if (enabled) {
            setRouteText(QStringLiteral("NATIVE · keystrokes go directly to the terminal."));
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
        if (QWidget *target = m_backend ? m_backend->focusWidget() : nullptr) { target->setFocus(Qt::OtherFocusReason); return; }
        // A pane whose terminal is not up yet still has to hold the keyboard: otherwise nothing in
        // the pane is focused, and the window's shortcuts have no pane to act on (#4PW5).
        m_editor->setFocus(Qt::OtherFocusReason);
    }
    void updatePaths() {
        if (m_cwdChip) {
            const QString home = QDir::homePath();
            const QString shown = m_cwd.startsWith(home) ? QStringLiteral("~") + m_cwd.mid(home.size()) : m_cwd;
            const QFontMetrics metrics(m_cwdChip->font());
            m_cwdChip->setText(metrics.elidedText(shown, Qt::ElideLeft, 260));
            m_cwdChip->setToolTip(QStringLiteral("Terminal: ") + m_cwd + QStringLiteral("\nAgent workspace: ") + m_workspace
                                  + QStringLiteral("\nClick to open it in an explorer pane"));
        }
        if (!m_cwdLabel) return;
        const QString home = QDir::homePath();
        auto tilde = [&home](const QString &path) { return path.startsWith(home) ? QStringLiteral("~") + path.mid(home.size()) : path; };
        m_cwdLabel->setText(width() >= 1000 || m_cwd == m_workspace
            ? QStringLiteral("TERMINAL  ") + tilde(m_cwd) + (m_cwd == m_workspace ? QString() : QStringLiteral("     │     AGENT WORKSPACE  ") + tilde(m_workspace))
            : tilde(m_cwd));
        m_cwdLabel->setToolTip(headerTooltip());
        updateHeader();
    }

public:
    // ----- pane title (issue JRWQ) --------------------------------------------------------
    // The header shows the session title; the tab label is derived from it (RelayWindow).
    QString paneTitle() const { return m_title; }
    bool titleIsUser() const { return m_titleUser; }
    // Told when the title changes, so the window can relabel the tab.
    std::function<void()> onTitleChanged;

    QString headerTooltip() const {
        QString tip = m_title.isEmpty() ? QStringLiteral("This pane has no title yet.")
                                        : m_title + (m_titleUser ? QStringLiteral("  (set by hand)")
                                                                 : QStringLiteral("  (written by the model)"));
        return tip + QStringLiteral("\n\nTerminal: ") + m_cwd + QStringLiteral("\nAgent workspace: ") + m_workspace
               + QStringLiteral("\n\nDouble click to rename · /rename")
               + QStringLiteral("\nDrag this header onto another pane's edge to move the pane there, or onto the tab bar to make it a tab");
    }

    // The pane button row floats over the top right of the leaf, exactly where the directory sits.
    // PaneChrome pushes the header clear of itself while it is shown.
    void setHeaderRightInset(int pixels) {
        if (!m_headerLayout || m_headerLayout->contentsMargins().right() == pixels) return;
        m_headerLayout->setContentsMargins(0, 0, pixels, 0);
        updateHeader();
    }

    void updateHeader() {
        if (!m_titleLabel) return;
        const QString shown = m_title.isEmpty() ? QFileInfo(m_cwd).fileName() : m_title;
        const QFontMetrics metrics(m_titleLabel->font());
        // The title takes what the directory, the badge and the hover button row leave.
        const int taken = (m_cwdLabel ? m_cwdLabel->sizeHint().width() : 0)
                          + (m_titleAuto && m_titleAuto->isVisible() ? m_titleAuto->sizeHint().width() : 0)
                          + (m_headerLayout ? m_headerLayout->contentsMargins().right() : 0) + 32;
        const int room = std::max(80, (m_headerWidget ? m_headerWidget->width() : width()) - taken);
        m_titleLabel->setText(metrics.elidedText(shown, Qt::ElideRight, room));
        m_titleLabel->setToolTip(headerTooltip());
        if (m_cwdLabel) m_cwdLabel->setToolTip(headerTooltip());
        // The badge says the name is still the model's to change; a hand-set one loses it.
        m_titleAuto->setVisible(!m_title.isEmpty() && !m_titleUser);
    }

    // Double click on the header, /rename, or /rename with no argument: edit the title in place.
    void beginRename() {
        if (!m_titleEdit || !m_titleLabel) return;
        m_titleEdit->setText(m_title);
        m_titleEdit->selectAll();
        m_titleLabel->setVisible(false);
        m_titleEdit->setVisible(true);
        m_titleEdit->setFocus(Qt::OtherFocusReason);
    }

    void endRename() {
        if (!m_titleEdit) return;
        m_titleEdit->setVisible(false);
        if (m_titleLabel) m_titleLabel->setVisible(true);
        focusInput();
    }

    void commitRename() {
        if (!m_titleEdit || !m_titleEdit->isVisible()) return;
        const QString text = m_titleEdit->text().trimmed();
        endRename();
        renameTo(text);
    }

    // An empty name hands the pane back to the model ("Use automatic name").
    void renameTo(const QString &text) {
        if (!m_workerReady) { status(QStringLiteral("The agent is not configured yet.")); return; }
        send({{"type", "set_session_title"}, {"title", text}});
        status(text.isEmpty() ? QStringLiteral("Pane name back to automatic.")
                              : QStringLiteral("Pane renamed to “%1”.").arg(text));
    }

    void setTitleFromWorker(const QString &title, bool user) {
        if (m_title == title && m_titleUser == user) return;
        m_title = title;
        m_titleUser = user;
        updateHeader();
        if (onTitleChanged) onTitleChanged();
    }

    // Tab labels (issue JRWQ): the window asks one pane's worker whether its tab's panes are on the
    // same work. No extra title call - the titles are already there.
    void requestTabLabel(const QString &requestId, const QStringList &titles) {
        if (!m_workerReady) return;
        QJsonArray items;
        for (const QString &title : titles) items.append(title);
        send({{"type", "tab_label"}, {"id", requestId}, {"titles", items}});
    }
    std::function<void(const QString &id, const QString &label, bool related)> onTabLabel;
    // /rename-tab: the tab belongs to the window, so the pane hands the request over.
    std::function<void(const QString &text, bool edit)> onRenameTab;

private:
    void configure() {
        if (m_agentBusy) { status(QStringLiteral("Stop the current agent turn before changing provider settings.")); return; }
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Relay · Bring your own key")); dialog.resize(650, 520);
        QSettings settings;
        auto *layout = new QVBoxLayout(&dialog);
        auto *form = new QFormLayout;
        struct PresetRow { const char *id, *label, *base, *model, *extra; };
        static const PresetRow presets[] = {
            // Mirrors backend/relay_core/presets.py; tests/test_presets.py fails if the two drift.
            {"custom", "Custom / current settings", "", "", ""},
            {"kimi", "Kimi · K3", "https://api.moonshot.ai/v1", "kimi-k3", "{\"reasoning_effort\":\"high\"}"},
            {"kimi-code", "Kimi Code · K3", "https://api.kimi.ai/coding/v1", "k3", "{\"reasoning_effort\":\"high\"}"},
            {"glm", "Z.AI · GLM-5.3 · standard API", "https://api.z.ai/api/paas/v4", "glm-5.3",
             "{\"thinking\":{\"type\":\"enabled\"},\"reasoning_effort\":\"high\"}"},
            {"glm-coding", "Z.AI · GLM-5.3 · Coding Plan", "https://api.z.ai/api/coding/paas/v4", "glm-5.3",
             "{\"thinking\":{\"type\":\"enabled\"},\"reasoning_effort\":\"high\"}"},
            {"minimax", "MiniMax · M3 · Coding/Token Plan", "https://api.minimax.io/v1", "MiniMax-M3", "{}"},
            {"openrouter", "OpenRouter · DeepSeek V4.1 Flash", "https://openrouter.ai/api/v1", "deepseek/deepseek-v4.1-flash", "{}"},
            {"openai", "OpenAI · GPT-6 Astra", "https://api.openai.com/v1", "gpt-6-astra", "{\"reasoning_effort\":\"high\"}"},
            {"anthropic", "Anthropic · Claude Opus 5", "https://api.anthropic.com/v1", "claude-opus-5", "{}"},
            {"gemini", "Google · Gemini 3.1 Pro", "https://generativelanguage.googleapis.com/v1beta/openai",
             "gemini-3.1-pro-preview", "{\"reasoning_effort\":\"high\"}"},
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
    QString m_engineCore;
    QWidget *m_terminal = nullptr, *m_terminalHost = nullptr;
    RichEditor *m_editor = nullptr;
    QString m_modeValue = defaultInputMode();
    QLabel *m_routeLabel = nullptr, *m_cwdLabel = nullptr, *m_help = nullptr;
    // Pane title (issue JRWQ): the header line, its in-place editor and the "auto" badge.
    QLabel *m_titleLabel = nullptr, *m_titleAuto = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QHBoxLayout *m_headerLayout = nullptr;
    QWidget *m_headerWidget = nullptr;
    // Dragging the pane by its header: where the press landed, and whether it has gone far enough
    // to be a drag rather than a click.
    QPoint m_headerPressAt;
    QPointer<QWidget> m_headerPressOn;
    bool m_headerPressed = false, m_headerDragging = false;
    QString m_title;
    bool m_titleUser = false;
    bool m_native = false, m_workerReady = false, m_shellReady = false, m_loading = false;
    bool m_promptReported = false;
    QString m_submitMode, m_model, m_turnText, m_fixCommand;
    int m_fixAttempt = 0;
    bool m_fixWatch = false, m_fixArmed = false, m_fixAwaitingAgent = false, m_turnHeader = false;
    // Wrong-mode hints (2026-09-17): a terminal submission that reads like a request
    // (m_commandNatural), the shell command an agent-mode turn started from
    // (m_turnShellPrompt), its run_command texts by call_id (m_runCommands), whether the
    // hint already fired this turn (m_modeHintShown), and the mode chip's flash state.
    bool m_commandNatural = false, m_modeHintShown = false;
    QString m_turnShellPrompt;
    QHash<QString, QString> m_runCommands;
    QTimer *m_chipFlash = nullptr;
    QString m_chipFlashDest;
    int m_chipFlashLeft = 0;
    bool m_chipFlashOn = false;
    bool m_inlineOpen = false, m_atLineStart = true;
    QList<QPair<QString, Ink>> m_inlinePending;
    relay::MarkdownAnsi m_markdown{QStringLiteral("38;2;226;229;235")};   // Ink::Agent's colour
    QList<QPair<QString, QString>> m_stored;
    QComboBox *m_modelBox = nullptr;
    QToolButton *m_cwdChip = nullptr, *m_modeChip = nullptr;
    relay::InputHighlighter *m_highlighter = nullptr;
    QLabel *m_toast = nullptr;
    QLabel *m_prefixChip = nullptr;
    QPointer<relay::SkillsDialog> m_skillsDialog;
    QPointer<relay::KeysDialog> m_keysDialog;
    QPointer<relay::RolesDialog> m_rolesDialog;
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
    int m_toolLines = 0;              // lines of the running tool's collapsed output
    bool m_toolPartialLine = false;   // its last chunk had no trailing newline
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
    // Switchboard: the `#K7Q2` picker and the card rows behind it (protocol 17.2, 17.6).
    QListWidget *m_cardList = nullptr;
    relay::board::Model m_cardIndex;
    bool m_cardIndexAsked = false;
    int m_cardDismissedAt = -1;
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
    relay::TerminalBackend::Link m_walkLink;   // the link Ctrl+Shift+L is sitting on
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
    // The model this pane's current turn runs on because it carries an image, or empty (protocol 17).
    QString m_visionModel;
    QJsonObject m_roleSummary, m_tierSummary, m_tierCatalog;
    QJsonArray m_roleActions;
    bool m_cleanShell = false, m_closing = false;
    QJsonArray m_presets;
    bool m_configuring = false;
    bool m_seenShell = false, m_refocus = true, m_configured = false, m_agentBusy = false;
    // agent sessions UI
    QLabel *m_planChip = nullptr, *m_ctxLabel = nullptr;
    QToolButton *m_interruptButton = nullptr;
    int m_menuFileLine = 0;   // the line the right-clicked path pointed at, for "Open file"
    // voice transcription (issue NY7Z): the chip, the recorder, and the clip in flight
    QToolButton *m_mic = nullptr;
    QToolButton *m_share = nullptr;
    relay::voice::Capture *m_voiceCapture = nullptr;
    QString m_voiceRequest, m_voiceClip;
    bool m_voiceHold = false, m_voiceTranscribing = false;
    QFrame *m_helpCard = nullptr;
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
    // In-flight turn clock: "thinking · 48 s · Esc stops" while the agent is busy (issue SQAM).
    QTimer *m_turnClock = nullptr;
    QElapsedTimer m_turnElapsed;
    QString m_turnStep;
    QTimer m_escTimer;
    // sudo & co. in the foreground
    QLabel *m_opaqueHint = nullptr;
    QString m_opaqueProgram;
    // Screen-text input detection (card YR21) and agent-driven programs (card C1HH).
    relay::screen::Detection m_screenPrompt;
    bool m_delegated = false;          // the user handed the foreground program to the agent
    QString m_delegatedProgram;
    QString m_delegationEnd;           // why the last delegation ended: take_over, password, program_exited
    int m_agentWrites = 0;             // keystrokes the agent has sent into it
    QFrame *m_programBar = nullptr;    // the floating banner over the terminal
    QLabel *m_programLabel = nullptr;
    QPushButton *m_delegateButton = nullptr;
    QJsonObject m_lastProgramState;    // what the worker was last told, to keep the pipe quiet
    quint64 m_requestId = 0, m_loadSerial = 0;
    // subagents UI
    relay::SubagentModel m_subagents;
    // request ledger UI
    relay::RequestLedgerModel m_ledger;
    QToolButton *m_workChip = nullptr;
    QStringList m_workCards;     // cards this pane referenced or the agent changed, newest first
    QString m_lastPlanPath;      // the plan this pane's agent wrote last
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
    enum class Kind { Explorer, Preview, Plan, Subagent, Turn, Board };

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

    // The Switchboard: cards, threads and the card detail view (protocol 17). Saved and restored
    // by workspace and tab, not by path.
    ToolPane(relay::BoardView *view, const QString &cwd) : m_kind(Kind::Board), m_board(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }
    relay::BoardView *board() const { return m_board; }

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
    QString path() const { return (m_subagent || m_turn || m_board) ? QString() : m_explorer ? m_explorer->root() : m_plan ? m_plan->path() : m_preview->path(); }
    QString cwd() const { return (m_subagent || m_turn || m_board) ? m_subagentCwd : m_explorer ? m_explorer->root() : QFileInfo(path()).absolutePath(); }
    QString title() const {
        if (m_board) return m_board->title();
        if (m_subagent) return m_subagent->title();
        if (m_turn) return m_turn->title();
        if (m_plan) return (m_plan->isDirty() ? QStringLiteral("● ") : QString()) + m_plan->title();
        const QString name = QFileInfo(path()).fileName();
        return name.isEmpty() ? path() : name;
    }
    QJsonObject node() const {
        if (m_board) return {{"board", QJsonObject{{"workspace", m_board->workspace()},
                                                   {"collapsed", m_board->collapsedSections()}}}};
        if (m_subagent || m_turn) return {};
        if (m_plan) return {{"plan", QJsonObject{{"path", path()}}}};
        return {{m_explorer ? "explorer" : "preview", QJsonObject{{"path", path()}}}};
    }
    void focusInput() {
        if (m_board) m_board->focusInput();
        else if (m_subagent) m_subagent->focusInput();
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
    relay::BoardView *m_board = nullptr;
    QString m_subagentCwd;
};

// ----- pane chrome: button row and drag handle -----------------------------------------------
// A small overlay in each pane's top-right corner. The three a person reaches for — new pane,
// move to a tab of its own, close — are on screen in every pane at all times (owner, 2026-09-17:
// buttons that appear only under the mouse are buttons you have to go looking for). Pointing at
// the pane lifts the row onto its raised tile and adds the two it was hiding: the drag grip and
// "new pane below". Dragging the grip moves the pane, as does dragging a Pane's header.
class PaneChrome final : public QFrame {
public:
    std::function<void(const QString &action)> onAction;

    explicit PaneChrome(QWidget *leaf) : QFrame(leaf) {
        setObjectName(QStringLiteral("paneChrome"));
        setAttribute(Qt::WA_StyledBackground);
        auto *row = new QHBoxLayout(this); row->setContentsMargins(3, 2, 3, 2); row->setSpacing(1);
        // The + makes it obvious that these open a new pane (a new shell and chat), not a layout
        // toggle. Every button is here at all times: the row no longer grows, lifts onto a tile or
        // rearranges itself under the pointer (owner, 2026-09-18). The drag grip is gone with the
        // hover row — pressing anywhere on the header moves the pane.
        button(row, QStringLiteral("⬓+"), QStringLiteral("pane.splitDown"), QStringLiteral("New pane below"));
        button(row, QStringLiteral("◫+"), QStringLiteral("pane.splitRight"), QStringLiteral("New pane to the right"));
        button(row, QStringLiteral("⇱"), QStringLiteral("pane.moveToNewTab"), QStringLiteral("Move to new tab"));
        button(row, QStringLiteral("×"), QStringLiteral("pane.close"), QStringLiteral("Close pane"));
        // The header gives up exactly this much room for good, so the title and the folder line
        // never re-elide.
        adjustSize();
        m_fullWidth = width();
    }

    void place() {
        const auto *leaf = parentWidget();
        adjustSize();
        move(leaf->width() - width() - 6, 4);
        raise();
        syncHeaderInset();
    }

    // Pane title (issue JRWQ): the header's right-hand directory must not end up under these
    // buttons, so the header gives up exactly the room the full row takes.
    void syncHeaderInset() {
        if (auto *pane = dynamic_cast<Pane *>(parentWidget()))
            pane->setHeaderRightInset(isVisible() ? m_fullWidth + 10 : 0);
        // The Switchboard's first row is its tab bar, which these buttons would otherwise cover.
        // So is a preview's header, whose view button names the format and so is wide enough to
        // reach them ("Source (MD)", issue #VXTF), and an explorer's folder line.
        else if (auto *tool = dynamic_cast<ToolPane *>(parentWidget()); tool) {
            const int inset = isVisible() ? m_fullWidth + 4 : 0;
            if (tool->board()) tool->board()->setHeaderRightInset(inset);
            else if (tool->preview()) tool->preview()->setHeaderRightInset(inset);
            else if (tool->explorer()) tool->explorer()->setHeaderRightInset(inset);
        }
    }

protected:
    void showEvent(QShowEvent *event) override { QFrame::showEvent(event); syncHeaderInset(); }
    void hideEvent(QHideEvent *event) override { QFrame::hideEvent(event); syncHeaderInset(); }

public:
    void refreshTooltips() {
        for (auto *b : findChildren<QToolButton *>()) {
            const QString keys = Keymap::instance().shortcutText(b->property("action").toString());
            b->setToolTip(b->property("label").toString() + (keys.isEmpty() ? QString() : QStringLiteral("  (") + keys + ')'));
        }
    }

private:
    QToolButton *button(QHBoxLayout *row, const QString &glyph, const QString &action, const QString &label) {
        auto *b = new QToolButton;
        b->setObjectName(QStringLiteral("paneChromeButton"));
        b->setText(glyph); b->setAutoRaise(true); b->setFocusPolicy(Qt::NoFocus);
        b->setProperty("action", action); b->setProperty("label", label);
        connect(b, &QToolButton::clicked, this, [this, action] { if (onAction) onAction(action); });
        row->addWidget(b);
        return b;
    }

    int m_fullWidth = 0;
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
        // `relay open PATH` in Relay shells reaches this process through a private local socket. The directory is created mode 0700.
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

// ----- window header (Relay draws its own title bar) ------------------------------------------
// The window has no OS title bar: the tab row is the title bar, with the Relay icon on the left
// and the bell, the actions button and minimize/maximize/close on the right. Dragging the empty
// part of the row moves the window, a double-click maximizes it, and a few pixels of padding
// around the window stay grabbable for resizing (see RelayWindow::edgesAt).
//
// "window/native_frame" (Actions › System title bar) gives the system decorations back for
// desktops where they work better; it applies to windows opened after the change.

// A header button. The glyphs are painted rather than typed: a text bell or gear lands in
// whatever font the desktop happens to have (often a colour emoji), and these have to sit at
// the same weight as the tab labels beside them.
class ChromeButton final : public QToolButton {
public:
    // Plus and TabClose are the tab row's own buttons; they are drawn here so the whole header
    // shares one stroke weight instead of mixing painted glyphs with the icon theme's bitmaps.
    enum class Glyph { Bell, Gear, Minimize, Maximize, Restore, Close, Plus, TabClose };

    explicit ChromeButton(Glyph glyph, QWidget *parent = nullptr, int size = kSize)
        : QToolButton(parent), m_glyph(glyph) {
        setObjectName(glyph == Glyph::Close ? QStringLiteral("windowCloseButton")
                    : glyph == Glyph::Plus ? QStringLiteral("newTabButton")
                    : glyph == Glyph::TabClose ? QStringLiteral("tabCloseButton")
                                               : QStringLiteral("windowChromeButton"));
        setAutoRaise(true);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::ArrowCursor);
        setFixedSize(size, size);
    }

    void setGlyph(Glyph glyph) { if (m_glyph == glyph) return; m_glyph = glyph; update(); }
    // Unseen notifications, drawn as a dot on the bell. 0 hides it.
    void setBadge(int count) { if (m_badge == count) return; m_badge = count; update(); }

    static constexpr int kSize = 26;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hovered = underMouse() && isEnabled();
        const bool closing = m_glyph == Glyph::Close;
        if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(closing ? relay::theme::Error.darker(130) : relay::theme::SurfaceRaised);
            const qreal radius = std::min(width(), height()) * 5.0 / kSize;
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), radius, radius);
        }
        // A dimmed button keeps its space in the layout but shows nothing, so a row of tabs is
        // not a row of crosses; pointing at the tab (or selecting it) brings the cross back.
        if (m_dim && !hovered) return;
        QColor ink = hovered ? (closing ? QColor(Qt::white) : relay::theme::Text) : relay::theme::TextMuted;
        if (!isEnabled()) ink = relay::theme::TextMuted.darker(150);
        // Every glyph is drawn for a 26 px button and scaled from there, so a smaller button
        // (the tab's close cross) keeps the same proportions and the same apparent weight.
        const qreal unit = std::min(width(), height()) / qreal(kSize);
        painter.setPen(QPen(ink, 1.3 * std::max(0.85, unit), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QPointF centre(width() / 2.0, height() / 2.0);
        switch (m_glyph) {
        case Glyph::Bell: paintBell(painter, centre, unit); break;
        case Glyph::Gear: paintGear(painter, centre, unit); break;
        case Glyph::Minimize: painter.drawLine(centre + QPointF(-5, 3) * unit, centre + QPointF(5, 3) * unit); break;
        case Glyph::Maximize: painter.drawRect(QRectF(centre + QPointF(-4.5, -4.5) * unit, QSizeF(9 * unit, 9 * unit))); break;
        case Glyph::Restore:
            painter.drawRect(QRectF(centre + QPointF(-5.5, -2.5) * unit, QSizeF(8 * unit, 8 * unit)));
            painter.drawPolyline(QPolygonF({centre + QPointF(-2.5, -5.5) * unit, centre + QPointF(5.5, -5.5) * unit,
                                            centre + QPointF(5.5, 2.5) * unit}));
            break;
        case Glyph::Plus:
            painter.drawLine(centre + QPointF(-4.5, 0) * unit, centre + QPointF(4.5, 0) * unit);
            painter.drawLine(centre + QPointF(0, -4.5) * unit, centre + QPointF(0, 4.5) * unit);
            break;
        case Glyph::Close:
        case Glyph::TabClose: {
            const qreal arm = (m_glyph == Glyph::Close ? 4.5 : 3.6) * unit;
            painter.drawLine(centre + QPointF(-arm, -arm), centre + QPointF(arm, arm));
            painter.drawLine(centre + QPointF(arm, -arm), centre + QPointF(-arm, arm));
            break;
        }
        }
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event) override
#else
    void enterEvent(QEvent *event) override
#endif
    { QToolButton::enterEvent(event); update(); }
    void leaveEvent(QEvent *event) override { QToolButton::leaveEvent(event); update(); }

private:
    void paintBell(QPainter &painter, const QPointF &centre, qreal unit) const {
        painter.save();
        painter.translate(centre);
        painter.scale(unit, unit);
        painter.translate(-centre);
        const qreal x = centre.x(), y = centre.y();
        QPainterPath bell;
        bell.moveTo(x - 5.5, y + 2.5);
        bell.cubicTo(x - 4.2, y + 1.5, x - 4.0, y - 0.5, x - 4.0, y - 1.5);
        bell.cubicTo(x - 4.0, y - 4.6, x - 2.2, y - 6.0, x, y - 6.0);
        bell.cubicTo(x + 2.2, y - 6.0, x + 4.0, y - 4.6, x + 4.0, y - 1.5);
        bell.cubicTo(x + 4.0, y - 0.5, x + 4.2, y + 1.5, x + 5.5, y + 2.5);
        bell.closeSubpath();
        painter.drawPath(bell);
        painter.drawArc(QRectF(x - 2, y + 2.6, 4, 3.4), 200 * 16, 140 * 16);   // clapper
        painter.restore();
        if (m_badge <= 0) return;
        // Unread dot, top-right, over the bell's shoulder.
        painter.setPen(Qt::NoPen);
        painter.setBrush(relay::theme::Accent);
        const QRectF dot(width() - 11.0, 3.0, 8.0, 8.0);
        painter.drawEllipse(dot);
        if (m_badge > 1) {
            QFont small = font();
            small.setPixelSize(7);
            small.setBold(true);
            painter.setFont(small);
            painter.setPen(relay::theme::AccentText);
            painter.drawText(dot, Qt::AlignCenter, m_badge > 9 ? QStringLiteral("9+") : QString::number(m_badge));
        }
    }

    // A cog drawn as one outline: six teeth around a ring, rather than a circle with spokes
    // poking through it. At 26 px the spokes read as noise; the solid outline does not.
    void paintGear(QPainter &painter, const QPointF &centre, qreal unit) const {
        constexpr int teeth = 6;
        const qreal inner = 4.2 * unit, outer = 6.0 * unit;
        const qreal half = M_PI / teeth;          // half of one tooth-and-gap period
        QPolygonF cog;
        for (int i = 0; i < teeth; ++i) {
            const qreal base = i * 2 * half;
            const qreal angle[4] = {base - half * 0.60, base - half * 0.34, base + half * 0.34, base + half * 0.60};
            const qreal radius[4] = {inner, outer, outer, inner};
            for (int k = 0; k < 4; ++k) cog << centre + QPointF(std::cos(angle[k]), std::sin(angle[k])) * radius[k];
        }
        painter.drawPolygon(cog);
        painter.drawEllipse(centre, 2.1 * unit, 2.1 * unit);
    }

public:
    // The tab's cross stays hidden until the tab is hovered or current.
    void setDim(bool dim) { if (m_dim == dim) return; m_dim = dim; update(); }

private:
    Glyph m_glyph;
    int m_badge = 0;
    bool m_dim = false;
};

// The list behind the bell: newest first, one row per notification, click to go back to the pane
// that posted it. Opening it marks everything as seen (that is what the badge counts).
class NotificationsPopup final : public QFrame {
public:
    explicit NotificationsPopup(QWidget *parent) : QFrame(parent, Qt::Popup) {
        setObjectName(QStringLiteral("notificationsPopup"));
        setAttribute(Qt::WA_StyledBackground);
        setFixedWidth(380);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(6);
        auto *head = new QHBoxLayout;
        head->setContentsMargins(2, 0, 2, 0);
        auto *title = new QLabel(QStringLiteral("NOTIFICATIONS"));
        title->setObjectName(QStringLiteral("paletteTitle"));
        head->addWidget(title);
        head->addStretch(1);
        m_clear = new QToolButton;
        m_clear->setObjectName(QStringLiteral("popupTextButton"));
        m_clear->setText(QStringLiteral("Clear all"));
        m_clear->setFocusPolicy(Qt::NoFocus);
        connect(m_clear, &QToolButton::clicked, this, [] { relay::NotificationCenter::instance().clear(); });
        head->addWidget(m_clear);
        layout->addLayout(head);

        m_scroll = new QScrollArea;
        m_scroll->setObjectName(QStringLiteral("notificationsScroll"));
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_rows = new QWidget;
        m_rowsLayout = new QVBoxLayout(m_rows);
        m_rowsLayout->setContentsMargins(0, 0, 0, 0);
        m_rowsLayout->setSpacing(4);
        m_rowsLayout->addStretch(1);
        m_scroll->setWidget(m_rows);
        layout->addWidget(m_scroll, 1);

        m_empty = new QLabel(QStringLiteral("Nothing yet.\nFinished agent turns, long commands, failures and\nprompts waiting for you show up here."));
        m_empty->setObjectName(QStringLiteral("muted"));
        m_empty->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_empty);

        connect(&relay::NotificationCenter::instance(), &relay::NotificationCenter::changed, this, [this] { if (isVisible()) rebuild(); });
    }

    // Called with the pane token of the clicked notification, when it has one.
    std::function<void(const QString &)> onOpenSource;

    void popUpUnder(QWidget *anchor) {
        rebuild();
        const QPoint below = anchor->mapToGlobal(QPoint(anchor->width(), anchor->height() + 6));
        QRect screen = anchor->screen() ? anchor->screen()->availableGeometry() : QRect(0, 0, 1280, 800);
        int x = std::max(screen.left() + 8, below.x() - width());
        x = std::min(x, screen.right() - width() - 8);
        move(x, std::min(below.y(), screen.bottom() - height() - 8));
        show();
        relay::NotificationCenter::instance().markAllSeen();
    }

private:
    void rebuild() {
        for (QWidget *row : std::as_const(m_widgets)) row->deleteLater();
        m_widgets.clear();
        const auto entries = relay::NotificationCenter::instance().entries();
        m_empty->setVisible(entries.isEmpty());
        m_scroll->setVisible(!entries.isEmpty());
        m_clear->setEnabled(!entries.isEmpty());
        for (const relay::Notification &note : entries) m_widgets.append(addRow(note));
        const int rows = std::min(6, int(entries.size()));
        m_scroll->setFixedHeight(entries.isEmpty() ? 0 : std::max(64, rows * 56));
        adjustSize();
    }

    QWidget *addRow(const relay::Notification &note) {
        auto *row = new QFrame;
        row->setObjectName(QStringLiteral("notificationRow"));
        row->setAttribute(Qt::WA_StyledBackground);
        row->setProperty("kind", note.kind);
        row->setCursor(note.source.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(8, 6, 6, 6);
        layout->setSpacing(8);
        auto *dot = new QLabel(QStringLiteral("●"));
        dot->setObjectName(QStringLiteral("notificationDot"));
        dot->setProperty("kind", note.kind);
        dot->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        layout->addWidget(dot);
        auto *text = new QVBoxLayout;
        text->setContentsMargins(0, 0, 0, 0);
        text->setSpacing(1);
        auto *titleRow = new QHBoxLayout;
        titleRow->setContentsMargins(0, 0, 0, 0);
        auto *title = new QLabel(note.title);
        title->setObjectName(QStringLiteral("notificationTitle"));
        titleRow->addWidget(title, 1);
        auto *when = new QLabel(relay::NotificationCenter::relativeTime(note.at));
        when->setObjectName(QStringLiteral("notificationTime"));
        titleRow->addWidget(when);
        text->addLayout(titleRow);
        if (!note.body.isEmpty()) {
            auto *body = new QLabel(note.body.left(240));
            body->setObjectName(QStringLiteral("notificationBody"));
            body->setWordWrap(true);
            text->addWidget(body);
        }
        layout->addLayout(text, 1);
        auto *dismiss = new QToolButton;
        dismiss->setObjectName(QStringLiteral("notificationDismiss"));
        dismiss->setText(QStringLiteral("✕"));
        dismiss->setFocusPolicy(Qt::NoFocus);
        dismiss->setToolTip(QStringLiteral("Dismiss"));
        const QString id = note.id;
        connect(dismiss, &QToolButton::clicked, this, [id] { relay::NotificationCenter::instance().remove(id); });
        layout->addWidget(dismiss, 0, Qt::AlignTop);
        row->installEventFilter(this);
        row->setProperty("relaySource", note.source);
        m_rowsLayout->insertWidget(m_rowsLayout->count() - 1, row);
        return row;
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            const QString source = object->property("relaySource").toString();
            if (!source.isEmpty() && onOpenSource) { hide(); onOpenSource(source); return true; }
        }
        return QFrame::eventFilter(object, event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) { hide(); return; }
        QFrame::keyPressEvent(event);
    }

private:
    QScrollArea *m_scroll = nullptr;
    QWidget *m_rows = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QLabel *m_empty = nullptr;
    QToolButton *m_clear = nullptr;
    QList<QWidget *> m_widgets;
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
        // No toolbar: the tab bar starts at the top. Its actions live in the palette (Ctrl+Shift+A).
        Keymap::instance().listen(this, [this] { syncToolbar(); syncChromeTooltips(); });
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
        // The palette floats over the right edge instead of resizing the terminal, so programs
        // such as vim do not redraw every time it opens.
        buildSidebar();
        m_sidebar->setParent(central);
        central->installEventFilter(this);
        Keymap::instance().listen(this, [this] {
            for (Pane *pane : allPanes()) pane->sendKeybindings();
            const auto conflicts = Keymap::instance().conflicts();
            notice(conflicts.isEmpty() ? QStringLiteral("Keyboard shortcuts reloaded.")
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
        if (event->type() == QEvent::Resize && object == centralWidget()) placeSidebar();
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
        // Keyboard walk over the links in the output (issue GWXM). While it runs, Enter opens
        // the highlighted link, Esc leaves and the arrows move; the keys are taken here, so the
        // walk works with the prompt box focused, which is the normal state. Anything else ends
        // the walk and is handled as usual, so typing is never swallowed.
        if (m_active && m_active->outputLinkWalkActive() && !m_sidebar->isVisible()) {
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
        // Which keys reach this overlay, and which one just did. "Ctrl+?" is Ctrl+Shift+/ on most
        // keyboards, so naming the key that worked is the answer to "Ctrl+? does nothing" (#T9ZS).
        const QStringList openKeys = Keymap::instance().shortcutTexts(QStringLiteral("help.shortcuts"));
        QString opened = QStringLiteral("Opens with %1, or from the palette (%2).")
                             .arg(openKeys.isEmpty() ? QStringLiteral("no key") : openKeys.join(QStringLiteral(", ")),
                                  Keymap::instance().shortcutText(QStringLiteral("palette.open")));
        if (m_lastShortcut.first == QStringLiteral("help.shortcuts") && !m_lastShortcut.second.isEmpty())
            opened += QStringLiteral("  You pressed %1.").arg(m_lastShortcut.second);
        m_lastShortcut = {};
        auto *opensWith = new QLabel(opened);
        opensWith->setWordWrap(true); opensWith->setObjectName(QStringLiteral("transcriptHeader"));
        layout->addWidget(opensWith);
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
        else if (id == QStringLiteral("palette.open")) togglePalette();
        else if (id == QStringLiteral("keybindings.reload")) Keymap::instance().reload();
        else if (id == QStringLiteral("help.shortcuts")) showShortcutsOverlay();
        else if (id == QStringLiteral("app.settings")) openSettings();
        else if (id == QStringLiteral("keybindings.edit")) {
            Keymap::instance().ensureFile();
            const QString editor = qEnvironmentVariable("VISUAL", qEnvironmentVariable("EDITOR", QStringLiteral("nano")));
            const QString quoted = QStringLiteral("'") + QString(Keymap::instance().path()).replace('\'', QStringLiteral("'\\''")) + '\'';
            if (!pane || !pane->runCommand(editor + ' ' + quoted))
                notice(QStringLiteral("Shortcuts file: ") + Keymap::instance().path());
        }
        else if (!pane) return;
        else if (id == QStringLiteral("terminal.native")) pane->toggleNative();
        else if (id == QStringLiteral("pane.restartShell")) pane->restartStopped();
        else if (id == QStringLiteral("links.step")) pane->stepOutputLink(-1);
        else if (id == QStringLiteral("control.human")) pane->takeControl();
        else if (id == QStringLiteral("control.prompt")) pane->showPrompt();
        else if (id == QStringLiteral("program.delegate")) pane->delegateProgram();
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
        // Aliases are files: one may have arrived from an agent, a git pull or an editor since the
        // list was last read, so ask again on the way in (issue G8DK).
        if (m_active) m_active->refreshAliases();
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

    // ----- settings ---------------------------------------------------------------------------
    //
    // One catalog, two front ends. The compact Settings window (src/ModelSettings.h) renders these
    // sections with real controls; the actions palette renders the same rows as menu entries, so
    // every setting keeps its keyboard path. Values live in QSettings under exactly the keys they
    // used before, because several of them are read straight from QSettings elsewhere.
    //
    // Sections: General, Models, Terminal, Agent, Privacy, Shortcuts.
    static QStringList settingsSectionIds() {
        return {QStringLiteral("general"), QStringLiteral("models"), QStringLiteral("terminal"),
                QStringLiteral("agent"), QStringLiteral("privacy"), QStringLiteral("shortcuts")};
    }

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
        general.rows << buttonRow(QStringLiteral("option:shortcut_hints_reset"), QStringLiteral("Reset shortcut hints"),
                                  QStringLiteral("Show every tip again"), QStringLiteral("Reset"), [this] {
            relay::ShortcutHints::instance().resetAll();
            notice(QStringLiteral("Shortcut hints reset."), 4000);
        });
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
        general.rows << buttonRow(QStringLiteral("windows.fresh"), QStringLiteral("Start a fresh window set"),
                                  QStringLiteral("Forget the saved layout; the next start opens one new window"),
                                  QStringLiteral("Forget"), [this] { startFreshWindowSet(); });
        sections << general;

        // Colour themes (issue 0JA7). One theme file carries the UI tokens and the 16-colour
        // terminal palette, so the picker restyles the chrome, both terminal engines and the
        // prompt box's syntax colours at once. The actions palette renders these rows too.
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
        appearance.rows << buttonRow(QStringLiteral("theme.reload"), QStringLiteral("Reload themes"),
                                     QStringLiteral("Pick up a theme file you added or edited"),
                                     QStringLiteral("Reload"), [this] {
            relay::theme::refreshThemes();
            relay::theme::setActiveTheme(relay::theme::activeThemeId());
            if (m_settings) m_settings->rebuild();
            notice(QStringLiteral("Themes reloaded. A theme added while Relay runs reaches new terminal panes; "
                                  "restart to give it to the ones already open."), 8000);
        });
        appearance.rows << buttonRow(QStringLiteral("theme.folder"), QStringLiteral("Your themes folder"),
                                     QStringLiteral("~/.config/relay/themes"), QStringLiteral("Open…"), [this] {
            const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                                    .absoluteFilePath(QStringLiteral("relay/themes"));
            QDir().mkpath(dir);
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
            notice(dir, 8000);
        });
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
        models.rows << toggleRow(QStringLiteral("agent/panes_fast"), QStringLiteral("New panes use the fast agent"),
                                 QStringLiteral("Off: every pane starts on the main agent. On: the first pane of a window keeps it"), false);
        models.rows << numberRow(QStringLiteral("provider/max_tokens"), QStringLiteral("Output token limit"),
                                 QStringLiteral("Per model call; applies to the next conversation"), 8192, 256, 32768);
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
        {
            // Where a new pane's prompt box starts; Ctrl+I cycles auto → terminal → agent in the pane.
            const QString current = Pane::defaultInputMode();
            agent.rows << choiceRow(QStringLiteral("option:input_default"),
                                    QStringLiteral("Default input for new sessions"),
                                    QStringLiteral("Ctrl+I cycles auto → terminal → agent; ! and * override one line"),
                                    {QStringLiteral("auto"), QStringLiteral("shell"), QStringLiteral("agent")},
                                    {QStringLiteral("Auto detect"), QStringLiteral("Terminal"), QStringLiteral("Agent")},
                                    current, [](const QString &value) {
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
        agent.rows << textRow(QStringLiteral("agent/plans_dir"), QStringLiteral("Plans folder"),
                              QStringLiteral("Absolute folder for plans (empty: <project>/.relay/plans)"),
                              QStringLiteral("<project>/.relay/plans"));
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
                                QStringLiteral("Model calls, then the turn stops with Continue"), 50, 1, 500);
        agent.rows << numberRow(QStringLiteral("agent/max_tool_calls"), QStringLiteral("Tool-call limit per turn"),
                                QStringLiteral("Tool calls in one turn"), 150, 1, 2000);
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
        voice.rows << numberRow(QStringLiteral("voice/max_seconds"), QStringLiteral("Longest recording"),
                                QStringLiteral("Recording stops by itself after this many seconds"),
                                relay::voice::kDefaultSeconds, 5, 600, QStringLiteral(" s"));
        voice.rows << textRow(QStringLiteral("voice/device"), QStringLiteral("Microphone"),
                              QStringLiteral("The capture tool's own source name (empty: the desktop default)"),
                              QStringLiteral("default"));
        voice.rows << buttonRow(QStringLiteral("agent.modelKeys"), QStringLiteral("OpenRouter key"),
                                QStringLiteral("Voice needs one of its own, whatever model your panes run"),
                                QStringLiteral("API keys…"), [this] { runAction(QStringLiteral("agent.modelKeys")); });
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
        shortcuts.id = QStringLiteral("shortcuts");
        shortcuts.title = QStringLiteral("Shortcuts");
        shortcuts.blurb = QStringLiteral("Keys are read from keybindings.json; your own overrides sit on top of the preset.");
        shortcuts.rows << buttonRow(QStringLiteral("help.shortcuts"), QStringLiteral("Keyboard shortcuts"),
                                    QStringLiteral("Every action and the keys it answers to"),
                                    QStringLiteral("Show…"), [this] { runAction(QStringLiteral("help.shortcuts")); });
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
        shortcuts.rows << buttonRow(QStringLiteral("keybindings.edit"), QStringLiteral("Edit keyboard shortcuts"),
                                    QStringLiteral("Opens keybindings.json in your editor"),
                                    QStringLiteral("Edit…"), [this] { runAction(QStringLiteral("keybindings.edit")); });
        shortcuts.rows << buttonRow(QStringLiteral("keybindings.reload"), QStringLiteral("Reload keyboard shortcuts"),
                                    QStringLiteral("Re-read keybindings.json now"),
                                    QStringLiteral("Reload"), [this] { runAction(QStringLiteral("keybindings.reload")); });
        sections << shortcuts;
        return sections;
    }

    void openSettings(const QString &section = QString()) {
        if (!m_settings) {
            m_settings = new relay::SettingsWindow([this] { return settingsSections(); }, this);
            m_settings->setAttribute(Qt::WA_DeleteOnClose);
        } else {
            m_settings->rebuild();
        }
        if (!section.isEmpty()) m_settings->showSection(section);
        m_settings->show();
        m_settings->raise();
        m_settings->activateWindow();
    }

    // The palette renders the same catalog: one submenu per section, one entry per row, so every
    // setting is still reachable and searchable from the keyboard.
    QList<PaletteItem> settingsRowItems(const relay::SettingsSection &section) {
        QList<PaletteItem> items;
        for (const relay::SettingRow &row : section.rows) {
            if (row.kind == relay::SettingRow::Info) continue;
            PaletteItem item;
            item.key = QStringLiteral("set:") + section.id + ':' + row.id;
            item.section = section.title;
            item.label = row.label;
            item.aliases = row.aliases;
            switch (row.kind) {
            case relay::SettingRow::Toggle:
                item.detail = (row.checked ? QStringLiteral("On · ") : QStringLiteral("Off · ")) + row.detail;
                item.checked = row.checked;
                item.stayOpen = true;
                item.run = [fn = row.onToggle, on = row.checked] { if (fn) fn(!on); };
                break;
            case relay::SettingRow::Choice: {
                QString current = row.current;
                for (int i = 0; i < row.options.size(); ++i)
                    if (row.options.at(i) == row.current && i < row.optionLabels.size()) current = row.optionLabels.at(i);
                item.detail = current + QStringLiteral(" · ") + row.detail;
                const QString title = row.label;
                item.children = [row, title] {
                    QList<PaletteItem> children;
                    for (int i = 0; i < row.options.size(); ++i) {
                        PaletteItem child;
                        const QString value = row.options.at(i);
                        child.key = QStringLiteral("set:") + row.id + ':' + value;
                        child.section = title;
                        child.label = i < row.optionLabels.size() ? row.optionLabels.at(i) : value;
                        child.checked = value == row.current;
                        child.stayOpen = true;
                        child.run = [fn = row.onChoose, value] { if (fn) fn(value); };
                        children << child;
                    }
                    return children;
                };
                break;
            }
            case relay::SettingRow::Text:
                item.label = row.label + QStringLiteral("…");
                item.detail = row.text.isEmpty() ? row.detail : row.text;
                item.run = [this, row] {
                    bool ok = false;
                    const QString value = QInputDialog::getText(this, row.label, row.detail, QLineEdit::Normal,
                                                                row.text, &ok);
                    if (ok && row.onText) row.onText(value.trimmed());
                };
                break;
            case relay::SettingRow::Number:
                item.label = row.label + QStringLiteral("…");
                item.detail = QStringLiteral("%1 · %2").arg(row.number).arg(row.detail);
                item.run = [this, row] {
                    bool ok = false;
                    const int value = QInputDialog::getInt(this, row.label, row.detail, row.number,
                                                           row.minimum, row.maximum, 1, &ok);
                    if (ok && row.onNumber) row.onNumber(value);
                };
                break;
            case relay::SettingRow::Button:
                item.label = row.label + QStringLiteral("…");
                item.detail = row.detail;
                item.run = row.run;
                break;
            case relay::SettingRow::Info:
                break;
            }
            items << item;
        }
        return items;
    }

    QList<PaletteItem> settingsMenuItems() {
        QList<PaletteItem> items;
        const QString section = QStringLiteral("Settings");
        items << actionItem(section, QStringLiteral("Open the Settings window"),
                            QStringLiteral("General, Models, Terminal, Agent, Privacy, Shortcuts"),
                            QStringLiteral("app.settings"));
        for (const relay::SettingsSection &group : settingsSections()) {
            const QString id = group.id;
            items << submenu(QStringLiteral("menu:settings:") + id, section, group.title, group.blurb,
                             [this, id] {
                for (const relay::SettingsSection &group : settingsSections())
                    if (group.id == id) return settingsRowItems(group);
                return QList<PaletteItem>();
            });
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
        items << actionItem(agent, QStringLiteral("Continue agent turn"),
                            pane && pane->limitReached() ? QStringLiteral("The last turn stopped at its step limit · /continue")
                                                         : QStringLiteral("Send “Continue” to the agent · /continue"), QStringLiteral("agent.continue"));
        items << actionItem(agent, QStringLiteral("Export conversation"), QStringLiteral("Save the conversation as Markdown"), QStringLiteral("agent.export"));
        // Image context: reaching this from the palette is the slow path, so the palette's own hint
        // teaches its shortcut (issue EM1E).
        items << actionItem(agent, QStringLiteral("Screenshot this pane"),
                            QStringLiteral("Attach a picture of this pane to your next prompt"),
                            QStringLiteral("agent.screenshotPane"));
        items << submenu(QStringLiteral("menu:settings"), agent, QStringLiteral("Settings"),
                         QStringLiteral("Models, keys, terminal, agent, privacy, shortcuts"),
                         [this] { return settingsMenuItems(); });
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
        items << actionItem(QStringLiteral("palette"), QStringLiteral("Keyboard shortcuts…"), QStringLiteral("Every action and its keys"), QStringLiteral("help.shortcuts"));
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
        // "Reopen windows on start" is a row in Settings > General.
        items << actionItem(panes, QStringLiteral("Start a fresh window set"),
                            QStringLiteral("Forget the saved layout; the next start opens one new window"),
                            QStringLiteral("windows.fresh"));
        items << actionItem(panes, QStringLiteral("Next tab"), QString(), QStringLiteral("tab.next"));
        items << actionItem(panes, QStringLiteral("Previous tab"), QString(), QStringLiteral("tab.previous"));

        // "Shortcut hints", "Shortcut preset" and "Shortcuts inside programs" are rows in Settings.
        items << actionItem(keys, QStringLiteral("Edit keyboard shortcuts…"), Keymap::instance().path(), QStringLiteral("keybindings.edit"));
        items << actionItem(keys, QStringLiteral("Reload keyboard shortcuts"), QString(), QStringLiteral("keybindings.reload"));
        if (Keymap::instance().hasOverrides()) {
            PaletteItem clear; clear.key = QStringLiteral("keybindings.clearOverrides"); clear.section = keys;
            clear.label = QStringLiteral("Clear custom overrides"); clear.detail = QStringLiteral("Use the preset's keys only");
            clear.run = [] { Keymap::instance().clearOverrides(); };
            items << clear;
        }
        items << logItems();
        return items;
    }

    // ----- diagnostics log (issue SQAM) --------------------------------------------------------
    QList<PaletteItem> logItems() {
        const QString section = QStringLiteral("Diagnostics");
        QList<PaletteItem> items;
        {
            PaletteItem open;
            open.key = QStringLiteral("logs.open"); open.section = section;
            open.label = QStringLiteral("Open log folder");
            open.detail = relay::log::directory().isEmpty() ? QStringLiteral("No writable data directory")
                                                            : relay::log::directory();
            open.aliases = QStringLiteral("log logs diagnostics debug troubleshoot relay.log worker.log");
            open.run = [this] {
                const QString dir = relay::log::directory();
                if (dir.isEmpty()) { notice(QStringLiteral("No writable data directory for logs."), 6000); return; }
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
                notice(dir, 8000);
            };
            items << open;
        }
        {
            // Idle deadline for a model call (protocol 15). Applies to the running agent at once.
            const int seconds = std::clamp(QSettings().value(QStringLiteral("agent/stall_timeout_s"), 60).toInt(), 1, 1800);
            PaletteItem item;
            item.key = QStringLiteral("option:stall_timeout"); item.section = section;
            item.label = QStringLiteral("Stop a silent model after…");
            item.detail = QStringLiteral("%1 s · the turn is retried once, then it ends with a message").arg(seconds);
            item.aliases = QStringLiteral("stall timeout hang stuck thinking silent retry");
            item.run = [this, seconds] {
                bool ok = false;
                const int value = QInputDialog::getInt(window(), QStringLiteral("Provider stall timeout"),
                    QStringLiteral("End a turn when the model sends nothing for this many seconds\n"
                                   "(reasoning models can be quiet for a while; 60 s is the default):"),
                    seconds, 1, 1800, 5, &ok);
                if (!ok) return;
                QSettings().setValue(QStringLiteral("agent/stall_timeout_s"), value);
                if (m_active) m_active->agentOptionsChanged(QStringLiteral("agent/stall_timeout_s"));
            };
            items << item;
        }
        const QString current = relay::log::levelName(relay::log::level());
        QString detail = current + QStringLiteral(" · agent workers pick it up when they restart");
        if (current == QStringLiteral("verbose")) detail = current + QStringLiteral(" · prompts are written to the log file");
        items << submenu(QStringLiteral("menu:logLevel"), section, QStringLiteral("Log detail"), detail, [current] {
            QList<PaletteItem> children;
            for (const QStringList &choice : relay::log::levelChoices()) {
                PaletteItem item;
                const QString id = choice.at(0);
                item.key = QStringLiteral("logs.level:") + id; item.section = QStringLiteral("Log detail");
                item.label = choice.at(1); item.detail = choice.at(2);
                item.checked = id == current; item.stayOpen = true;
                item.run = [id] { relay::log::setLevel(id); };
                children << item;
            }
            return children;
        });
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
        const QString hintId = QStringLiteral("palette.") + item.key;
        const QString hintText = item.shortcut.isEmpty()
            ? QString() : relay::ShortcutHints::nextTime(item.shortcut, item.label.toLower());
        QStringList recent = QSettings().value(QStringLiteral("palette/recent")).toStringList();
        recent.removeAll(item.key); recent.prepend(item.key);
        QSettings().setValue(QStringLiteral("palette/recent"), QStringList(recent.mid(0, 12)));
        if (item.stayOpen) {
            hint(hintId, hintText);
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
        // The hint comes after the action, not before it: an action that says something itself
        // ("Pane screenshot attached…") used to replace its own hint on the same toast, so the
        // shortcut was never the thing left on screen.
        if (!hintText.isEmpty())
            QTimer::singleShot(400, this, [this, hintId, hintText] { hint(hintId, hintText); });
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
    void startBoardWorker(const QString &workspace) {
        QSettings settings;
        const QString preset = settings.value(QStringLiteral("provider/preset")).toString();
        QJsonObject configure{{QStringLiteral("type"), QStringLiteral("configure")},
                              {QStringLiteral("workspace"), workspace},
                              {QStringLiteral("agent_role"), QStringLiteral("switchboard")},
                              {QStringLiteral("use_stored_key"), true},
                              {QStringLiteral("api_key"), QString()},
                              {QStringLiteral("base_url"), settings.value(QStringLiteral("provider/base")).toString()},
                              {QStringLiteral("model"), settings.value(QStringLiteral("provider/model")).toString()},
                              {QStringLiteral("max_tokens"), settings.value(QStringLiteral("provider/max_tokens"), 8192).toInt()}};
        if (!preset.isEmpty()) configure.insert(QStringLiteral("preset"), preset);
        const QJsonObject extra = QJsonDocument::fromJson(
            settings.value(QStringLiteral("provider/extra")).toString().toUtf8()).object();
        if (!extra.isEmpty()) configure.insert(QStringLiteral("extra"), extra);
        const QJsonObject roles = Pane::rolesObject();
        if (!roles.isEmpty()) configure.insert(QStringLiteral("roles"), roles);
        const QJsonObject tiers = Pane::tiersObject();
        if (!tiers.isEmpty()) configure.insert(QStringLiteral("tiers"), tiers);
        boardWorker()->start(configure);
    }

    ToolPane *createBoardPane(const QString &workspace, const QJsonArray &collapsed = {}) {
        auto *view = new relay::BoardView(workspace);
        if (!collapsed.isEmpty()) view->setCollapsedSections(collapsed);
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
        // Model roles (protocol 13): panes opened after the first one default to the fast agent.
        const QString savedRole = spec.value(QStringLiteral("agent_role")).toString();
        if (!savedRole.isEmpty()) pane->initAgentRole(savedRole);
        else if (Pane::newPanesUseFastAgent() && !allPanes().isEmpty()) pane->initAgentRole(QStringLiteral("fast"));
        // Saved window layout: model/effort/mode and the conversation to reattach.
        pane->initRestore(spec);
        QPointer<Pane> guard(pane);
        // Callbacks find the pane's current window, so panes and tabs can move between windows.
        pane->onStatus = [guard](const QString &text) { if (guard) guard->toast(text, 5000); };
        pane->onStateChanged = [guard] {
            auto *w = windowOf(guard);
            if (!w) return;
            if (guard == w->m_active) w->syncToolbar();
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
        pane->onOpenSubagent = [guard](const QString &id) { if (auto *w = windowOf(guard)) w->openSubagentPane(guard, id); };   // subagents UI
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
                return createBoardPane(workspace, board.value(QStringLiteral("collapsed")).toArray());
            return createPane({{"cwd", m_manager->workspace()}, {"workspace", m_manager->workspace()}});
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
        syncToolbar();
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
        // The bare mark, not the app icon: the icon's dark tile vanishes into the chrome and
        // leaves only a speck of chevron at this size.
        const QString markPath = relay::theme::themeDataDir() + QStringLiteral("/icons/relay-mark.svg");
        const QIcon appIcon = QFileInfo::exists(markPath) ? QIcon(markPath) : QApplication::windowIcon();
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
            togglePalette();
            hint(QStringLiteral("chrome.settings"), relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("palette.open")), QStringLiteral("settings and every action")));
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
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("palette.open"));
        m_settingsButton->setToolTip(keys.isEmpty() ? QStringLiteral("Settings and actions")
                                                    : QStringLiteral("Settings and actions  (%1)").arg(keys));
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

    void stopBoardWorker() { if (m_boardWorker) m_boardWorker->stop(); }

    WindowManager *m_manager;
    // Switchboard: one worker per window, started on the first Ctrl+Shift+S (protocol 17).
    QPointer<relay::BoardWorker> m_boardWorker;
    QTabWidget *m_tabs = nullptr;
    QList<QPair<QAction *, QString>> m_toolbarActions;
    QPointer<relay::SettingsWindow> m_settings;
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
    QTimer::singleShot(1200, &m_context, [window, note] { if (window) window->notice(note, 9000); });
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

void WindowManager::focusPane(const QString &token) {
    m_windows.removeAll(nullptr);
    if (token.isEmpty()) return;
    for (RelayWindow *window : std::as_const(m_windows)) {
        Pane *pane = window->findPaneByToken(token);
        if (!pane) continue;
        window->revealPane(pane);
        return;
    }
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
    if (requester) requester->notice(QStringLiteral("Nothing to restore."));
}

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

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    relay::theme::applyDarkTheme(app);
    QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
    QCoreApplication::setApplicationName(QStringLiteral("relay"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RELAY_VERSION));
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

