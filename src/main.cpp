// SPDX-License-Identifier: GPL-3.0-or-later
#include "RichEditor.h"
#include "Theme.h"
#include <iterator>
#include <KParts/ReadOnlyPart>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <kde_terminal_interface.h>
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
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QSet>
#include <QDBusConnection>
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
#include <QElapsedTimer>
#include <QListWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QToolButton>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <cmath>
#include <stdexcept>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

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
        return fkey || ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier));
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
        add("closed.restore", "pane", "Restore the last closed pane, tab or window", {QStringLiteral("Ctrl+Shift+W")});
        add("palette.open", "palette", "Open the Relay actions palette", {QStringLiteral("Ctrl+Shift+A")});
        add("control.human", "terminal", "Take control of the terminal (hides the prompt; works from the prompt box)", {QStringLiteral("Ctrl+H")});
        add("control.prompt", "terminal", "Back to the Relay prompt (the agent is in control)", {QStringLiteral("Ctrl+Shift+H")});
        add("terminal.native", "terminal", "Toggle native terminal input", {QStringLiteral("F12")});
        add("terminal.interrupt", "terminal", "Interrupt the running command (Ctrl+C)", {});
        add("agent.newChat", "agent", "Start a new agent conversation", {});
        add("agent.stop", "agent", "Stop the agent turn", {});
        add("agent.provider", "agent", "Provider and API keys", {});
        add("input.modeAuto", "agent", "Input mode: auto detect", {});
        add("input.modeTerminal", "agent", "Input mode: terminal", {});
        add("input.modeAgent", "agent", "Input mode: agent", {});
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
        return QByteArrayLiteral(R"PRESETS({"relay":{},"warp":{"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+T"],"tab.next":["Ctrl+PgDown","Ctrl+Tab"],"tab.previous":["Ctrl+PgUp","Ctrl+Shift+Tab"],"pane.splitRight":["Ctrl+Shift+D"],"pane.splitDown":["Ctrl+Shift+E"],"pane.focusLeft":["Ctrl+Alt+Left"],"pane.focusRight":["Ctrl+Alt+Right"],"pane.focusUp":["Ctrl+Alt+Up"],"pane.focusDown":["Ctrl+Alt+Down"],"pane.close":["Ctrl+Shift+W"],"closed.restore":["Ctrl+Alt+T"],"palette.open":["Ctrl+Shift+P"],"terminal.native":["F12"],"terminal.interrupt":[],"agent.newChat":["Ctrl+Shift+Y"],"agent.stop":[],"agent.provider":[],"input.modeAuto":[],"input.modeTerminal":["Ctrl+Shift+I"],"input.modeAgent":["Ctrl+I"],"keybindings.edit":["Ctrl+,"],"keybindings.reload":[]},"vscode":{"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+~"],"tab.next":["Ctrl+PgDown","Ctrl+Tab"],"tab.previous":["Ctrl+PgUp","Ctrl+Shift+Tab"],"pane.splitRight":["Ctrl+Shift+%","Ctrl+\\"],"pane.splitDown":["Ctrl+Shift+|"],"pane.focusLeft":["Alt+Left"],"pane.focusRight":["Alt+Right"],"pane.focusUp":["Alt+Up"],"pane.focusDown":["Alt+Down"],"pane.close":["Ctrl+W"],"closed.restore":["Ctrl+Shift+T"],"palette.open":["Ctrl+Shift+P"],"terminal.native":["Ctrl+`","F12"],"terminal.interrupt":[],"agent.newChat":["Ctrl+N"],"agent.stop":["Ctrl+Esc"],"agent.provider":["Ctrl+Alt+."],"input.modeAuto":[],"input.modeTerminal":[],"input.modeAgent":["Ctrl+Shift+Alt+I"],"keybindings.edit":["Ctrl+,"],"keybindings.reload":[]},"konsole":{"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+T"],"tab.next":["Ctrl+PgDown"],"tab.previous":["Ctrl+PgUp"],"pane.splitRight":["Ctrl+Shift+(","Ctrl+("],"pane.splitDown":["Ctrl+Shift+)","Ctrl+)"],"pane.focusLeft":["Ctrl+Shift+Left"],"pane.focusRight":["Ctrl+Shift+Right"],"pane.focusUp":["Ctrl+Shift+Up"],"pane.focusDown":["Ctrl+Shift+Down"],"pane.close":["Ctrl+Shift+W"],"closed.restore":[],"palette.open":["Ctrl+Alt+I"],"terminal.native":["F12"],"terminal.interrupt":[],"agent.newChat":[],"agent.stop":[],"agent.provider":[],"input.modeAuto":[],"input.modeTerminal":[],"input.modeAgent":[],"keybindings.edit":["Ctrl+Alt+,"],"keybindings.reload":[]}})PRESETS");
    }
    QFileSystemWatcher m_watcher;
    QList<QPair<QPointer<QObject>, std::function<void()>>> m_listeners;
};

// One terminal pane: a KonsolePart shell, its Bash bridge, a composer, and its own agent worker
// and conversation. Windows arrange panes in tabs and splits; the toolbar acts on the active pane.
class Pane final : public QWidget {
public:
    Pane(const QString &workspace, const QString &cwd, bool cleanShell)
        : m_workspace(workspace), m_cwd(cwd.isEmpty() ? workspace : cwd), m_cleanShell(cleanShell) {
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
        connect(&m_secretPoll, &QTimer::timeout, this, [this] { checkPasswordPrompt(); });
        m_secretPoll.start(1000);
        m_poll.start(80);
        m_debounce.setSingleShot(true);
        m_debounce.setInterval(150);
        connect(&m_debounce, &QTimer::timeout, this, [this] { requestRoute(false, QStringLiteral("auto")); });
        connect(m_editor, &QPlainTextEdit::textChanged, this, [this] { m_debounce.start(); });
        m_editor->onSubmit = [this](const QString &destination) { requestRoute(true, destination); };
        m_editor->onNative = [this] { setNative(true); };
        qApp->installEventFilter(this);
        QTimer::singleShot(5000, this, [this] {
            if (!m_seenShell && m_iface) {
                setNative(true);
                status(QStringLiteral("Shell integration did not initialize. Native terminal remains available; try --clean-shell."));
            }
        });
    }

    ~Pane() override {
        m_closing = true;
        qApp->removeEventFilter(this);
        m_poll.stop();
        // Destroy the part before its private shell state directory is removed.
        if (m_part) delete m_part.data();
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

    QString cwd() const { return m_cwd; }
    QString workspace() const { return m_workspace; }
    bool cleanShell() const { return m_cleanShell; }
    QString mode() const { return m_modeValue; }
    void setMode(const QString &mode) { m_modeValue = mode; requestRoute(false, QStringLiteral("auto")); changed(); }
    bool isNative() const { return m_native; }
    void toggleNative() { setNative(!m_native); }
    bool agentBusy() const { return m_agentBusy; }
    bool processBusy() const {
        return m_iface && m_iface->foregroundProcessId() > 0 && m_iface->foregroundProcessId() != m_iface->terminalProcessId();
    }
    QList<QPair<QString, QString>> storedModels() const { return m_stored; }
    QString currentPreset() const { return m_currentPreset; }
    void focusInput() { if (m_native) focusTerminal(); else m_editor->setFocus(Qt::OtherFocusReason); }

    void interruptShell() {
        if (m_iface) { m_loading = false; m_promptReported = false; clearFix(); m_iface->sendInput(QString(QChar(3))); focusTerminal(); }
    }
    void newChat() {
        if (m_agentBusy) { status(QStringLiteral("Stop the current agent turn first.")); return; }
        send({{"type", "reset"}}); clearFix();
        printInline(QStringLiteral("New agent conversation\n"), Ink::Note); closeInline();
    }
    void stopAgent() {
        send({{"type", "cancel"}}); clearFix();
        status(QStringLiteral("Stopping. Commands that already ran may have changed files; a network read can take up to its timeout to stop."));
    }
    void selectModel(const QString &id) {
        if (id.isEmpty() || id == m_currentPreset) return;
        if (m_agentBusy) { status(QStringLiteral("Stop the current agent turn before switching models.")); changed(); return; }
        configurePreset(id, true);
    }
    void openProviderDialog() { configure(); }
    bool ownsComposerWidget(QWidget *widget) const { return m_composer && widget && (widget == m_composer || m_composer->isAncestorOf(widget)); }

    // Human control: the prompt box hides and keys go to the terminal.
    void takeControl() {
        // During a running program the prompt returns when it exits; at an idle shell you stay in control.
        m_autoHuman = processBusy() || !m_promptReported;
        if (!m_native) setNative(true);
        toast(QStringLiteral("You're in control · %1 for the prompt").arg(Keymap::instance().shortcutText(QStringLiteral("control.prompt"))));
    }

    // Back to the prompt. While a program runs, the prompt talks to the agent, which is in control.
    void showPrompt() {
        m_autoHuman = false;
        if (m_native) setNative(false, !processBusy());
        else m_editor->setFocus(Qt::OtherFocusReason);
        if (processBusy())
            toast(QStringLiteral("Agent in control · %1 to take control").arg(Keymap::instance().shortcutText(QStringLiteral("control.human"))));
    }

    bool ownsTerminalWidget(QWidget *widget) const { return m_terminal && widget && (widget == m_terminal || m_terminal->isAncestorOf(widget)); }
    bool runCommand(const QString &command) { return runInTerminal(command, false, 0); }
    void sendKeybindings() { if (m_configured) send(QJsonObject{{"type", "keybindings"}, {"path", Keymap::instance().path()}, {"actions", Keymap::instance().catalog().value(QStringLiteral("actions"))}}); }
    void importWarpKeys() { send({{"type", "import_warp"}}); }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        // Split panes get narrow: drop the key hints and the agent workspace path first.
        if (m_help) m_help->setVisible(width() >= 900);
        updatePaths();
    }

    bool eventFilter(QObject *object, QEvent *event) override {
        // Copy on select (off by default): after a left-button release that finishes a selection
        // in this pane's terminal, copy it to the clipboard.
        if (event->type() == QEvent::MouseButtonRelease && copyOnSelect()
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && ownsTerminalWidget(qobject_cast<QWidget *>(object))) {
            QTimer::singleShot(0, this, [this] { copySelection(); });
        }
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
            auto *key = static_cast<QKeyEvent *>(event);
            auto *widget = qobject_cast<QWidget *>(object);
            // Terminal clipboard: Ctrl+C copies when text is selected, otherwise it reaches the
            // shell as an interrupt. Ctrl+V pastes at a shell prompt; inside a program such as
            // vim it is passed through (visual block). Ctrl+X always reaches the terminal.
            const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
            if (event->type() == QEvent::KeyPress && ownsTerminalWidget(widget) && mods == Qt::ControlModifier && !key->isAutoRepeat()) {
                QObject *display = terminalDisplay();
                if (display && key->key() == Qt::Key_C) {
                    if (copySelection()) return true;
                } else if (display && key->key() == Qt::Key_V && !processBusy()) {
                    QMetaObject::invokeMethod(display, "pasteFromClipboard", Qt::DirectConnection);
                    return true;
                }
            }
            if (event->type() == QEvent::KeyPress && widget && m_terminal &&
                (widget == m_terminal || m_terminal->isAncestorOf(widget)) && !m_loading && m_shellReady) {
                // Directly typing in the terminal gives Readline ownership of its line.
                // It must not later be overwritten by an unrelated composer submission.
                setNative(true);
            }
        }
        return QWidget::eventFilter(object, event);
    }

private:
    void buildUi() {
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(8, 6, 8, 8); layout->setSpacing(6);
        m_cwdLabel = new QLabel; m_cwdLabel->setTextFormat(Qt::PlainText);
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
        routeRow->addWidget(m_routeLabel, 1);
        // Per-pane controls live under the terminal, next to the input they affect.
        m_modeBox = new QComboBox;
        m_modeBox->addItem(QStringLiteral("Auto detect"), QStringLiteral("auto"));
        m_modeBox->addItem(QStringLiteral("Terminal"), QStringLiteral("shell"));
        m_modeBox->addItem(QStringLiteral("Agent"), QStringLiteral("agent"));
        m_modeBox->setAccessibleName(QStringLiteral("Input destination"));
        m_modeBox->setFocusPolicy(Qt::TabFocus);
        connect(m_modeBox, qOverload<int>(&QComboBox::activated), this, [this](int) {
            setMode(m_modeBox->currentData().toString()); focusInput();
        });
        routeRow->addWidget(m_modeBox);
        m_modelBox = new QComboBox;
        m_modelBox->setAccessibleName(QStringLiteral("Agent model"));
        m_modelBox->setToolTip(QStringLiteral("Agent model for this pane. Switching starts a new conversation."));
        m_modelBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        m_modelBox->setFocusPolicy(Qt::TabFocus);
        connect(m_modelBox, qOverload<int>(&QComboBox::activated), this, [this](int index) {
            selectModel(m_modelBox->itemData(index).toString()); focusInput();
        });
        routeRow->addWidget(m_modelBox);
        auto *cancel = new QToolButton;
        cancel->setObjectName(QStringLiteral("interruptButton"));
        const QString cancelIcon = relay::theme::themeDataDir() + QStringLiteral("/icons/cancel.svg");
        if (QFileInfo::exists(cancelIcon)) cancel->setIcon(QIcon(cancelIcon)); else cancel->setText(QStringLiteral("⊘"));
        cancel->setToolTip(QStringLiteral("Interrupt the running command (Ctrl+C)"));
        cancel->setAccessibleName(QStringLiteral("Interrupt shell"));
        cancel->setFocusPolicy(Qt::NoFocus);
        connect(cancel, &QToolButton::clicked, this, [this] { interruptShell(); });
        routeRow->addWidget(cancel);
        refreshPickers();
        auto *submit = new QPushButton(QStringLiteral("Submit ↵"));
        connect(submit, &QPushButton::clicked, this, [this] { requestRoute(true, QStringLiteral("auto")); });
        routeRow->addWidget(submit); composerLayout->addLayout(routeRow);
        m_editor = new RichEditor; composerLayout->addWidget(m_editor);
        auto *help = new QLabel(QStringLiteral("Shift+Enter  newline     Ctrl+Enter  agent     Ctrl+Shift+Enter  terminal     ↑/↓  history"));
        help->setWordWrap(true); composerLayout->addWidget(help);
        m_help = help;
        layout->addWidget(composer);
        updatePaths();
    }

    void startWorker() {
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
        connect(&m_worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int, QProcess::ExitStatus) {
            m_workerReady = false; m_configured = false; m_agentBusy = false;
            status(QStringLiteral("Local worker stopped. Native terminal remains available; restart Relay to restore routing and agents."));
        });
        connect(&m_worker, &QProcess::started, this, [this] {
            for (const auto &line : std::as_const(m_workerPending)) m_worker.write(line);
            m_workerPending.clear();
        });
        m_worker.setProgram(m_python);
        m_worker.setArguments({QStringLiteral("-S"), QStringLiteral("-u"), m_data + QStringLiteral("/backend/worker.py")});
        m_worker.start();
    }

    void startTerminal(bool cleanShell) {
        // Set before the KPart constructs its KPtyProcess so it inherits these values.
        qputenv("RELAY_RUNTIME_DIR", m_runtime.path().toUtf8());
        qputenv("RELAY_SESSION_TOKEN", m_token.toUtf8());
        qputenv("RELAY_SHELL_EVENT", (m_data + QStringLiteral("/shell/event.py")).toUtf8());
        qputenv("RELAY_PYTHON", m_python.toUtf8());
        qputenv("RELAY_CLEAN_SHELL", cleanShell ? "1" : "0");
#if QT_VERSION_MAJOR >= 6
        const auto factory = KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("kf6/parts/konsolepart")));
        if (!factory.plugin) throw std::runtime_error("Qt6 KonsolePart could not be loaded. Install the KDE Frameworks 6 version of Konsole.");
#else
        // KF5 Konsole (e.g. Ubuntu 24.04 konsole-kpart) installs the part at the plugin root.
        auto factory = KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("konsolepart")));
        if (!factory.plugin) factory = KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("kf5/parts/konsolepart")));
        if (!factory.plugin) throw std::runtime_error("Qt5 KonsolePart could not be loaded. Install the KDE Frameworks 5 Konsole part (konsole-kpart).");
#endif
        m_part = factory.plugin->create<KParts::ReadOnlyPart>(this);
        if (!m_part) throw std::runtime_error("KonsolePart could not be created.");
        m_iface = qobject_cast<TerminalInterface *>(m_part.data());
        if (!m_iface) throw std::runtime_error("KonsolePart does not provide TerminalInterface.");
        m_terminal = m_part->widget();
        m_terminalHost->layout()->addWidget(m_terminal);
        connect(m_part.data(), &QObject::destroyed, this, [this] {
            m_iface = nullptr; m_terminal = nullptr; m_shellReady = false;
            // Like other terminals, a pane closes when its shell exits.
            if (!m_closing && onShellExited) QTimer::singleShot(0, this, [this] { if (onShellExited) onShellExited(); });
        });
        // The part has loaded Relay's profile; the shell should see the user's own XDG paths.
        relay::theme::restoreXdgEnvironment();
        // Konsole starts new sessions in its own default directory, so the Bash integration
        // changes to this pane's directory after loading the user's configuration.
        qputenv("RELAY_START_DIR", m_cwd.toUtf8());
        m_iface->startProgram(QStringLiteral("/bin/bash"), {QStringLiteral("/bin/bash"), QStringLiteral("--noprofile"),
            QStringLiteral("--rcfile"), m_data + QStringLiteral("/shell/integration.bash"), QStringLiteral("-i")});
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
            if (submit) status(QStringLiteral("Native input is active. Press F12 to return to the composer."));
            return;
        }
        if (!m_workerReady) {
            if (submit) status(QStringLiteral("Local router is not ready; use the native terminal or restart Relay."));
            return;
        }
        if (submit && (!m_pendingSubmit.isEmpty() || m_loading)) return;
        const QString id = QString::number(++m_requestId);
        const QString mode = overrideMode == QStringLiteral("auto") ? m_modeValue : overrideMode;
        if (submit) {
            m_submitMode = mode;
            m_pendingSubmit = id; m_submittedDraft = m_editor->toPlainText();
        } else m_previewId = id;
        send({{"type", "route"}, {"id", id}, {"text", m_editor->toPlainText()}, {"mode", mode},
              {"known_commands", m_knownCommands}, {"path", m_shellPath}, {"cwd", m_cwd}});
    }

    void handle(const QJsonObject &event) {
        const QString type = event.value(QStringLiteral("event")).toString();
        if (type == QStringLiteral("ready")) {
            m_workerReady = true; requestRoute(false, QStringLiteral("auto"));
            send({{"type", "presets"}});
        } else if (type == QStringLiteral("route")) {
            const QString id = event.value(QStringLiteral("id")).toString();
            const QString route = event.value(QStringLiteral("route")).toString();
            if (id == m_previewId || id == m_pendingSubmit) {
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
                dispatch(event, m_submitMode);
            }
        } else if (type == QStringLiteral("configured")) {
            m_configured = true; m_configuring = false;
            m_model = event.value(QStringLiteral("model")).toString();
            status(QStringLiteral("Agent ready · ") + event.value(QStringLiteral("model")).toString());
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
                // Saved choice first, then Warp's default agent model, then the first stored key.
                auto hasKey = [this](const QString &id) {
                    return std::any_of(m_stored.cbegin(), m_stored.cend(), [&](const auto &entry) { return entry.first == id; });
                };
                QString choice = QSettings().value(QStringLiteral("provider/preset")).toString();
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
        } else if (type == QStringLiteral("agent_started")) {
            m_agentBusy = true; m_turnHeader = false;
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
            m_agentBusy = false;
            if (type == QStringLiteral("cancelled")) {
                ensureLineStart(); printInline(QStringLiteral("Stopped. Actions that already ran are not rolled back.\n"), Ink::Error);
            }
            ensureLineStart(); closeInline();
            status(QStringLiteral("Ready"));
            finishFixTurn(type == QStringLiteral("done"));
        } else if (type == QStringLiteral("error")) {
            const auto text = event.value(QStringLiteral("text")).toString();
            if (event.value(QStringLiteral("id")).toString() == m_pendingSubmit) m_pendingSubmit.clear();
            const bool wasBusy = m_agentBusy;
            // Route errors do not cancel a concurrent agent turn.
            m_agentBusy = event.value(QStringLiteral("agent_busy")).toBool(false);
            m_configuring = false;
            status(text);
            if (wasBusy && !m_agentBusy) {
                ensureLineStart(); printInline(QStringLiteral("✗ ") + text + '\n', Ink::Error); closeInline();
                finishFixTurn(false);
            }
        }
    }

    void dispatch(const QJsonObject &decision, const QString &mode) {
        const QString route = decision.value(QStringLiteral("route")).toString();
        const QString text = decision.value(QStringLiteral("text")).toString();
        if (route == QStringLiteral("empty")) return;
        if (route == QStringLiteral("shell") && processBusy() && mode != QStringLiteral("shell")) {
            // A program owns the terminal; the prompt talks to the agent.
            submitAgent(text, true, QStringLiteral("A program is running in this pane, so this went to the agent."));
            return;
        }
        if (route == QStringLiteral("shell")) {
            const bool valid = decision.value(QStringLiteral("valid")).toBool(decision.value(QStringLiteral("syntax_ok")).toBool(true));
            const QString problem = decision.value(QStringLiteral("invalid_reason")).toString(
                decision.value(QStringLiteral("syntax_error")).toString());
            if (mode == QStringLiteral("shell")) {
                // Terminal mode (Ctrl+Shift+Enter): always the terminal. An invalid command
                // goes to the agent to be fixed; a failing run is fixed and re-run.
                m_editor->remember(text); m_editor->clear();
                if (!valid) { startFix(text, problem.isEmpty() ? QStringLiteral("not a valid command") : problem, 1); return; }
                runInTerminal(text, true, 0);
                return;
            }
            if (!valid) { submitAgent(text, true, problem); return; }
            runInTerminal(text, false, 0);
        } else {
            // "agent", or a legacy "ambiguous" decision: the agent is the default for invalid input.
            // Show why non-command input went to the agent, e.g. "command not found: foo".
            const QString why = !decision.value(QStringLiteral("valid")).toBool(true) && mode != QStringLiteral("agent")
                ? decision.value(QStringLiteral("invalid_reason")).toString() : QString();
            submitAgent(text, true, why);
        }
    }

    bool runInTerminal(const QString &text, bool watch, int attempt) {
        if (!m_iface || !m_shellReady || m_loading || m_native) {
            status(QStringLiteral("Shell is not at an integrated prompt. Use native input; Relay will not type into a running program."));
            return false;
        }
        if (m_iface->foregroundProcessId() > 0 && m_iface->foregroundProcessId() != m_iface->terminalProcessId()) {
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
        m_iface->sendInput(QString(QChar(24)) + QChar(18));
        const quint64 serial = ++m_loadSerial;
        QTimer::singleShot(2500, this, [this, serial] {
            if (m_loading && m_loadSerial == serial) {
                m_loading = false; clearFix(); setNative(true);
                status(QStringLiteral("Shell did not acknowledge the editor text. Enter was NOT sent. Inspect the native input line; try --clean-shell."));
            }
        });
        return true;
    }

    // Warp's rule: a password prompt turns echo off but keeps canonical (line) input. Full-screen
    // programs and Readline turn canonical input off, so they do not match.
    void checkPasswordPrompt() {
        if (m_promptReported || m_secretNotified || !m_iface) return;
        const int pid = m_iface->terminalProcessId();
        if (pid <= 0) return;
        const auto name = QStringLiteral("/proc/%1/fd/0").arg(pid).toLocal8Bit();
        const int fd = ::open(name.constData(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return;
        termios state{};
        const bool secret = ::tcgetattr(fd, &state) == 0 && (state.c_lflag & ICANON) && !(state.c_lflag & ECHO);
        ::close(fd);
        if (!secret) return;
        m_secretNotified = true;
        if (!m_native) { m_autoHuman = true; setNative(true); }
        focusTerminal();
        toast(QStringLiteral("Password prompt · you're in control"));
        notifyIfAway(QStringLiteral("Password prompt"), QStringLiteral("A command in %1 is waiting for a password.").arg(m_cwd));
    }

    void notifyIfAway(const QString &title, const QString &body) {
        if (window() && window()->isActiveWindow()) return;
        QApplication::alert(window());
        const QString notifier = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
        if (!notifier.isEmpty()) QProcess::startDetached(notifier, {QStringLiteral("-a"), QStringLiteral("Relay"), title, body});
    }

    static bool copyOnSelect() { return QSettings().value(QStringLiteral("terminal/copy_on_select"), false).toBool(); }

    // Konsole exposes no "has selection" query, and its copy does nothing without a selection.
    // Copy, and treat a clipboard change as proof that text was selected.
    bool copySelection() {
        QObject *display = terminalDisplay();
        if (!display) return false;
        bool copied = false;
        const auto connection = connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [&copied] { copied = true; });
        QMetaObject::invokeMethod(display, "copyToClipboard", Qt::DirectConnection);
        disconnect(connection);
        if (!copied) return false;
        const int count = QApplication::clipboard()->text().toUcs4().size();
        if (count > 0) toast(count == 1 ? QStringLiteral("1 character copied") : QStringLiteral("%L1 characters copied").arg(count));
        return true;
    }

    // A small notice over the bottom-right of the terminal that fades after a moment.
    void toast(const QString &text) {
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
        m_toastTimer.start(1600);
    }

    QObject *terminalDisplay() const {
        if (!m_terminal) return nullptr;
        if (m_terminal->metaObject()->indexOfMethod("copyToClipboard()") >= 0) return m_terminal;
        for (QObject *child : m_terminal->findChildren<QObject *>())
            if (child->metaObject()->indexOfMethod("copyToClipboard()") >= 0) return child;
        return nullptr;
    }

    // ----- fix and re-run loop (terminal mode) -----------------------------------------
    static constexpr int kMaxFixAttempts = 3;

    void status(const QString &text) { if (onStatus) onStatus(text); }
    void changed() { refreshPickers(); if (onStateChanged) onStateChanged(); }

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
        if (m_agentBusy) {
            printInline(QStringLiteral("✗ %1. The agent is busy, so no fix was started.\n").arg(problem), Ink::Error);
            closeInline(); clearFix(); return;
        }
        m_fixCommand = command; m_fixAttempt = attempt; m_fixAwaitingAgent = true; m_fixWatch = false; m_fixArmed = false;
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
        m_turnText.clear(); m_turnHeader = false; m_agentBusy = true;
        send({{"type", "ask"}, {"text", prompt}});
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
    enum class Ink { Agent, User, Tool, ToolOutput, DiffAdd, DiffRemove, Error, Note };

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
        }
        return {};
    }

    // Konsole's Session is not reachable through KParts, but every session registers itself on
    // D-Bus. In-process, objectRegisteredAt() returns the Session QObject, whose onReceiveBlock()
    // slot feeds bytes to the terminal emulator exactly like program output. Nothing is typed
    // into the shell, so agent text never reaches shell history and is never executed.
    QObject *konsoleSession() {
        if (m_session) return m_session;
        if (!m_iface) return nullptr;
        const int pid = m_iface->terminalProcessId();
        for (int n = 1; n <= 256 && pid > 0; ++n) {
            QObject *session = QDBusConnection::sessionBus().objectRegisteredAt(QStringLiteral("/Sessions/%1").arg(n));
            if (!session) continue;
            for (QObject *child : session->children()) {
                int childPid = 0;
                if (child->metaObject()->indexOfMethod("processId()") >= 0
                    && QMetaObject::invokeMethod(child, "processId", Qt::DirectConnection, Q_RETURN_ARG(int, childPid))
                    && childPid == pid) {
                    m_session = session;
                    return session;
                }
            }
        }
        return nullptr;
    }

    bool shellIdleAtPrompt() const {
        return m_iface && m_promptReported && !m_loading && !m_native
            && (m_iface->foregroundProcessId() <= 0 || m_iface->foregroundProcessId() == m_iface->terminalProcessId());
    }

    void writeTerminal(const QByteArray &bytes) {
        QObject *session = konsoleSession();
        if (!session) { fprintf(stderr, "%s", bytes.constData()); return; }
        QMetaObject::invokeMethod(session, "onReceiveBlock", Qt::DirectConnection,
                                  Q_ARG(const char *, bytes.constData()), Q_ARG(int, bytes.size()));
    }

    void printInline(const QString &text, Ink ink) {
        if (text.isEmpty()) return;
        if (!shellIdleAtPrompt()) { m_inlinePending.append({text, ink}); return; }
        QString clean;
        clean.reserve(text.size());
        for (const QChar c : text) {
            const ushort u = c.unicode();
            // Model and tool output is untrusted: drop C0/C1 controls so it cannot emit
            // escape sequences (clipboard writes, title changes, cursor games).
            if (u == '\n' || u == '\t' || (u >= 0x20 && u != 0x7f && !(u >= 0x80 && u < 0xa0))) clean += c;
        }
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
        if (m_iface && shellIdleAtPrompt()) m_iface->sendInput(QString(QChar(24)) + QChar(16));
    }

    void flushInline() {
        if (m_inlinePending.isEmpty() || !shellIdleAtPrompt()) return;
        const auto pending = m_inlinePending;
        m_inlinePending.clear();
        for (const auto &item : pending) printInline(item.first, item.second);
        if (!m_agentBusy) { ensureLineStart(); closeInline(); }
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
        send({{"type", "configure"}, {"preset", id}, {"use_stored_key", true},
              {"base_url", preset.value(QStringLiteral("base_url")).toString()},
              {"model", preset.value(QStringLiteral("model")).toString()},
              {"extra", preset.value(QStringLiteral("extra")).toObject()}, {"max_tokens", tokens},
              {"api_key", QString()}, {"workspace", m_workspace}, {"keybindings", Keymap::instance().catalog()}});
        updatePaths();
    }

    void submitAgent(const QString &text, bool fromEditor, const QString &why = QString()) {
        if (!m_configured) {
            if (fromEditor) { configure(); return; }
            status(QStringLiteral("No agent provider is configured."));
            return;
        }
        if (m_agentBusy) {
            status(QStringLiteral("An agent turn is active. Stop it or wait for completion before submitting another request."));
            return;
        }
        if (fromEditor) { m_editor->remember(text); m_editor->clear(); }
        ensureLineStart();
        printInline(QStringLiteral("› ") + text + '\n', Ink::User);
        if (!why.isEmpty()) printInline(why + '\n', Ink::Note);
        m_turnText.clear(); m_turnHeader = false; m_agentBusy = true;
        send({{"type", "ask"}, {"text", text}});
    }

    bool readlineReady() const {
        if (!m_iface) return false;
        const int pid = m_iface->terminalProcessId();
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
        QFile file(m_runtime.filePath(QStringLiteral("state.json")));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return;
        const auto event = QJsonDocument::fromJson(file.readAll()).object();
        if (event.value(QStringLiteral("token")).toString() != m_token) return;
        const auto sequence = event.value(QStringLiteral("sequence")).toString();
        if (sequence.isEmpty() || sequence == m_shellSequence) return;
        m_shellSequence = sequence; m_seenShell = true;
        const QString stage = event.value(QStringLiteral("event")).toString();
        const QString newCwd = event.value(QStringLiteral("cwd")).toString(m_cwd);
        if (newCwd != m_cwd) { m_cwd = newCwd; updatePaths(); changed(); }
        if (stage == QStringLiteral("ready")) {
            m_promptReported = true;
            refreshShellReady();
            m_knownCommands = event.value(QStringLiteral("known_commands")).toArray();
            m_shellPath = event.value(QStringLiteral("path")).toString();
            const int status = event.value(QStringLiteral("status")).toInt();
            if (!m_agentBusy) this->status(QStringLiteral("Shell ready · exit %1").arg(status));
            if (m_runningSince.isValid()) {
                const qint64 ms = m_runningSince.elapsed();
                m_runningSince.invalidate();
                if (ms > 30000) notifyIfAway(QStringLiteral("Command finished"), QStringLiteral("Exit %1 after %2 s in %3").arg(status).arg(ms / 1000).arg(m_cwd));
            }
            if (m_native && m_autoHuman) { m_autoHuman = false; setNative(false, false); }
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
            QTimer::singleShot(120, this, [this] { flushInline(); });
        } else if (stage == QStringLiteral("running")) {
            m_shellReady = false; m_promptReported = false;
            m_runningSince.start(); m_secretNotified = false;
            // Any program that keeps running hands control to the human, like Warp: the prompt
            // hides and keys go to the program (passwords, REPLs, vim, long builds).
            const QString sequence = m_shellSequence;
            QTimer::singleShot(150, this, [this, sequence] {
                if (m_shellSequence != sequence || m_promptReported || m_native) return;
                m_autoHuman = true;
                setNative(true);
            });
        } else if (stage == QStringLiteral("loaded") && m_loading && m_iface &&
                   event.value(QStringLiteral("input_sha256")).toString() == m_pendingHash) {
            m_loading = false; m_shellReady = false; m_promptReported = false; m_refocus = true;
            m_editor->remember(m_pendingCommand);
            // Do not discard edits typed while waiting for the shell acknowledgement.
            if (m_editor->toPlainText() == m_submittedDraft) m_editor->clear();
            m_fixArmed = m_fixWatch;
            m_iface->sendInput(QStringLiteral("\r")); focusTerminal();
        } else if (stage == QStringLiteral("unsupported")) {
            m_shellReady = false; m_promptReported = false; setNative(true);
            status(QStringLiteral("Your shell already has a DEBUG hook. It was left untouched; use native mode or relaunch with --clean-shell."));
        }
    }

    void setNative(bool enabled, bool cancelLine = true) {
        m_native = enabled;
        changed();
        m_editor->setReadOnly(enabled);
        // Human control hides the prompt box entirely; the terminal gets the space and the keys.
        if (m_composer) m_composer->setVisible(!enabled);
        if (enabled) {
            m_routeLabel->setText(QStringLiteral("NATIVE · keystrokes go directly to Konsole."));
            focusTerminal();
        } else {
            // Returning from native mode cancels Readline's partial line at a prompt.
            // Never inject a cancellation into a foreground TUI/process here.
            if (cancelLine && m_shellReady && m_iface && (m_iface->foregroundProcessId() <= 0 || m_iface->foregroundProcessId() == m_iface->terminalProcessId())) {
                m_iface->sendInput(QString(QChar(3))); m_shellReady = false; m_promptReported = false; m_refocus = true;
            }
            m_editor->setFocus(); requestRoute(false, QStringLiteral("auto"));
        }
    }

    void focusTerminal() { if (m_terminal) m_terminal->setFocus(Qt::OtherFocusReason); }
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
            send({{"type", "configure"}, {"base_url", base->text().trimmed()}, {"model", model->text().trimmed()},
                  {"api_key", m_apiKey}, {"preset", presetId}, {"use_stored_key", m_apiKey.isEmpty()},
                  {"workspace", m_workspace}, {"extra", doc.object()}, {"max_tokens", tokens->value()},
                  {"keybindings", Keymap::instance().catalog()}});
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
    QPointer<KParts::ReadOnlyPart> m_part;
    TerminalInterface *m_iface = nullptr;
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
    QPointer<QObject> m_session;
    QList<QPair<QString, QString>> m_stored;
    QComboBox *m_modeBox = nullptr, *m_modelBox = nullptr;
    QLabel *m_toast = nullptr;
    QFrame *m_composer = nullptr;
    QTimer m_secretPoll;
    QElapsedTimer m_runningSince;
    bool m_autoHuman = false, m_secretNotified = false;
    QTimer m_toastTimer;
    QString m_currentPreset;
    bool m_cleanShell = false, m_closing = false;
    QJsonArray m_presets;
    bool m_configuring = false;
    bool m_seenShell = false, m_refocus = true, m_configured = false, m_agentBusy = false;
    quint64 m_requestId = 0, m_loadSerial = 0;
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
    WindowManager(QString workspace, bool cleanShell) : m_workspace(std::move(workspace)), m_cleanShell(cleanShell) {}
    QString workspace() const { return m_workspace; }
    bool cleanShell() const { return m_cleanShell; }
    RelayWindow *newWindow(const QJsonArray &tabs, int current = 0, const QRect &geometry = QRect());
    RelayWindow *newWindowAt(const QString &cwd);
    void cycle(RelayWindow *from, int delta);
    void remember(ClosedItem item) {
        m_closed.append(std::move(item));
        while (m_closed.size() > 25) m_closed.removeFirst();
    }
    void restore(RelayWindow *requester);
    void forget(RelayWindow *window) { m_windows.removeAll(window); }
private:
    QString m_workspace;
    bool m_cleanShell = false;
    QList<QPointer<RelayWindow>> m_windows;
    QList<ClosedItem> m_closed;
};

class RelayWindow final : public QMainWindow {
public:
    explicit RelayWindow(WindowManager *manager) : m_manager(manager) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QStringLiteral("Relay"));
        setMinimumSize(760, 520);
        const QRect available = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 860);
        resize(std::min(1320, available.width() * 9 / 10), std::min(860, available.height() * 9 / 10));
        buildToolbar();
        m_tabs = new QTabWidget;
        m_tabs->setDocumentMode(true);
        m_tabs->setTabsClosable(true);
        m_tabs->setMovable(true);
        m_tabs->tabBar()->setExpanding(false);
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
            Pane *pane = m_lastActive.value(page);
            if (!pane) { const auto panes = panesIn(page); pane = panes.isEmpty() ? nullptr : panes.first(); }
            if (pane) { setActive(pane); pane->focusInput(); }
        });
        connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
            if (m_tabs->count() > 1) closeTab(index, true); else closeWindowWithWarning();
        });
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
            if (Pane *pane = paneOf(now); pane && pane->window() == this) setActive(pane);
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
        const auto panes = panesIn(page);
        if (!panes.isEmpty()) { setActive(panes.first()); QTimer::singleShot(0, panes.first(), [p = panes.first()] { p->focusInput(); }); }
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

    QString activeCwd() const { return m_active ? m_active->cwd() : m_manager->workspace(); }

    // Restore a closed pane next to a sibling pane that still exists in this window.
    bool restorePaneNextTo(QWidget *siblingWidget, Qt::Orientation orientation, bool before, const QJsonObject &node) {
        Pane *sibling = dynamic_cast<Pane *>(siblingWidget);
        if (!sibling || sibling->window() != this) return false;
        Pane *pane = nullptr;
        try { pane = createPane(node.value(QStringLiteral("pane")).toObject()); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return true; }
        insertBeside(sibling, pane, orientation, before);
        m_tabs->setCurrentWidget(pageOf(pane));
        setActive(pane); pane->focusInput();
        return true;
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Resize && object == centralWidget()) placeSidebar();
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
        if (id == QStringLiteral("control.human") && !(pane && pane->ownsComposerWidget(widget)))
            return QMainWindow::eventFilter(object, event);
        if (pane && pane->ownsTerminalWidget(widget) && pane->processBusy() && !Keymap::instance().actsInsidePrograms(key))
            return QMainWindow::eventFilter(object, event);
        // Accept the override so neither the composer nor Konsole consumes the key,
        // then act on the key press itself. Auto-repeat does not open a burst of tabs.
        event->accept();
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) QTimer::singleShot(0, this, [this, id] { runAction(id); });
        return true;
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
        m_manager->forget(this);
        event->accept();
    }

private:
    // ----- toolbar ----------------------------------------------------------------------------
    void buildToolbar() {
        auto *toolbar = addToolBar(QStringLiteral("Relay"));
        toolbar->setMovable(false);
        auto *brand = new QLabel(QStringLiteral("  RELAY  "));
        auto font = brand->font(); font.setBold(true); font.setPointSize(13); brand->setFont(font);
        toolbar->addWidget(brand);
        toolbar->addSeparator();
        auto addAction = [this, toolbar](const QString &label, const QString &id) {
            auto *action = toolbar->addAction(label);
            connect(action, &QAction::triggered, this, [this, id] { runAction(id); });
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

    // ----- actions ----------------------------------------------------------------------------
    void runAction(const QString &id) {
        Pane *pane = m_active;
        if (id == QStringLiteral("window.new")) m_manager->newWindowAt(activeCwd());
        else if (id == QStringLiteral("window.next")) m_manager->cycle(this, 1);
        else if (id == QStringLiteral("window.previous")) m_manager->cycle(this, -1);
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
        else if (id == QStringLiteral("closed.restore")) m_manager->restore(this);
        else if (id == QStringLiteral("palette.open")) togglePalette();
        else if (id == QStringLiteral("keybindings.reload")) Keymap::instance().reload();
        else if (id == QStringLiteral("keybindings.edit")) {
            Keymap::instance().ensureFile();
            const QString editor = qEnvironmentVariable("VISUAL", qEnvironmentVariable("EDITOR", QStringLiteral("nano")));
            const QString quoted = QStringLiteral("'") + QString(Keymap::instance().path()).replace('\'', QStringLiteral("'\\''")) + '\'';
            if (!pane || !pane->runCommand(editor + ' ' + quoted))
                statusBar()->showMessage(QStringLiteral("Shortcuts file: ") + Keymap::instance().path());
        }
        else if (!pane) return;
        else if (id == QStringLiteral("terminal.native")) pane->toggleNative();
        else if (id == QStringLiteral("control.human")) pane->takeControl();
        else if (id == QStringLiteral("control.prompt")) pane->showPrompt();
        else if (id == QStringLiteral("terminal.interrupt")) pane->interruptShell();
        else if (id == QStringLiteral("agent.newChat")) pane->newChat();
        else if (id == QStringLiteral("agent.stop")) pane->stopAgent();
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
        QString key, section, label, detail, shortcut;
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
        const QString mode = pane ? pane->mode() : QStringLiteral("auto");
        const QString modeName = mode == QStringLiteral("shell") ? QStringLiteral("Terminal") : mode == QStringLiteral("agent") ? QStringLiteral("Agent") : QStringLiteral("Auto detect");
        items << submenu(QStringLiteral("menu:mode"), agent, QStringLiteral("Input mode"), modeName, [this, mode] {
            return QList<PaletteItem>{
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Auto detect"), QStringLiteral("Commands to the terminal, everything else to the agent"), QStringLiteral("input.modeAuto"), mode == QStringLiteral("auto")),
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Terminal"), QStringLiteral("Always the terminal; the agent fixes failures"), QStringLiteral("input.modeTerminal"), mode == QStringLiteral("shell")),
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Agent"), QStringLiteral("Always the agent"), QStringLiteral("input.modeAgent"), mode == QStringLiteral("agent"))};
        });
        items << actionItem(agent, QStringLiteral("New chat"), QStringLiteral("Start a new conversation in this pane"), QStringLiteral("agent.newChat"));
        items << actionItem(agent, QStringLiteral("Stop agent"), pane && pane->agentBusy() ? QStringLiteral("Cancel the running turn") : QStringLiteral("Agent is idle"), QStringLiteral("agent.stop"));
        items << actionItem(agent, QStringLiteral("Provider and API keys…"), QStringLiteral("Base URL, model ID, key, request options"), QStringLiteral("agent.provider"));
        PaletteItem importKeys; importKeys.key = QStringLiteral("agent.importWarp"); importKeys.section = agent;
        importKeys.label = QStringLiteral("Import keys from Warp"); importKeys.detail = QStringLiteral("Copy Warp's custom-endpoint keys into the keyring");
        importKeys.run = [this] { if (m_active) m_active->importWarpKeys(); };
        items << importKeys;

        items << actionItem(terminal, QStringLiteral("Interrupt"), pane && pane->processBusy() ? QStringLiteral("Send Ctrl+C to the running program") : QStringLiteral("Nothing is running"), QStringLiteral("terminal.interrupt"));
        items << actionItem(terminal, QStringLiteral("Take control"), QStringLiteral("Hide the prompt and type into the terminal"), QStringLiteral("control.human"), pane && pane->isNative());
        items << actionItem(terminal, QStringLiteral("Show the Relay prompt"), QStringLiteral("The agent is in control of a running program"), QStringLiteral("control.prompt"), pane && !pane->isNative());
        {
            PaletteItem copy;
            const bool on = QSettings().value(QStringLiteral("terminal/copy_on_select"), false).toBool();
            copy.key = QStringLiteral("terminal.copyOnSelect"); copy.section = terminal;
            copy.label = QStringLiteral("Copy on select"); copy.detail = on ? QStringLiteral("On: selecting terminal text copies it") : QStringLiteral("Off");
            copy.checked = on; copy.stayOpen = true;
            copy.run = [on] { QSettings().setValue(QStringLiteral("terminal/copy_on_select"), !on); };
            items << copy;
        }

        items << actionItem(panes, QStringLiteral("Split right"), QString(), QStringLiteral("pane.splitRight"));
        items << actionItem(panes, QStringLiteral("Split down"), QString(), QStringLiteral("pane.splitDown"));
        items << actionItem(panes, QStringLiteral("New tab"), QString(), QStringLiteral("tab.new"));
        items << actionItem(panes, QStringLiteral("New window"), QString(), QStringLiteral("window.new"));
        items << actionItem(panes, QStringLiteral("Close pane"), QStringLiteral("Then the tab, then the window"), QStringLiteral("pane.close"));
        items << actionItem(panes, QStringLiteral("Restore closed"), QStringLiteral("Last closed pane, tab or window"), QStringLiteral("closed.restore"));
        items << actionItem(panes, QStringLiteral("Next tab"), QString(), QStringLiteral("tab.next"));
        items << actionItem(panes, QStringLiteral("Previous tab"), QString(), QStringLiteral("tab.previous"));

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
            const int score = std::max({fuzzyScore(needle, item.label), fuzzyScore(needle, item.detail) / 3, fuzzyScore(needle, item.section) / 4});
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

    // ----- panes ------------------------------------------------------------------------------
    static Pane *paneOf(QWidget *widget) {
        for (QWidget *w = widget; w; w = w->parentWidget())
            if (auto *pane = dynamic_cast<Pane *>(w)) return pane;
        return nullptr;
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
        QString cwd = spec.value(QStringLiteral("cwd")).toString();
        if (!QFileInfo(cwd).isDir()) cwd = m_manager->workspace();
        QString workspace = spec.value(QStringLiteral("workspace")).toString();
        if (!QFileInfo(workspace).isDir()) workspace = m_manager->workspace();
        auto *pane = new Pane(workspace, cwd, m_manager->cleanShell());
        QPointer<Pane> guard(pane);
        pane->onStatus = [this, guard](const QString &text) { if (guard && guard == m_active) statusBar()->showMessage(text); };
        pane->onStateChanged = [this, guard] {
            if (!guard) return;
            if (guard == m_active) syncToolbar();
            updateTitles();
        };
        pane->onShellExited = [this, guard] { if (guard) closePane(guard, false); };
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
        return createPane(node.value(QStringLiteral("pane")).toObject());
    }

    static QSplitter *newSplitter(Qt::Orientation orientation) {
        auto *splitter = new QSplitter(orientation);
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(3);
        return splitter;
    }

    QJsonObject serializeNode(QWidget *widget) const {
        if (auto *pane = dynamic_cast<Pane *>(widget))
            return {{"pane", QJsonObject{{"cwd", pane->cwd()}, {"workspace", pane->workspace()}}}};
        if (auto *splitter = dynamic_cast<QSplitter *>(widget)) {
            QJsonArray children, sizes;
            for (int i = 0; i < splitter->count(); ++i) children.append(serializeNode(splitter->widget(i)));
            for (int size : splitter->sizes()) sizes.append(size);
            return {{"split", splitter->orientation() == Qt::Vertical ? "v" : "h"}, {"children", children}, {"sizes", sizes}};
        }
        return {};
    }

    QJsonObject serializeTab(int index) const {
        QWidget *page = m_tabs->widget(index);
        QWidget *root = page && page->layout() && page->layout()->count() ? page->layout()->itemAt(0)->widget() : nullptr;
        return serializeNode(root);
    }

    void setActive(Pane *pane) {
        if (!pane) return;
        if (m_active != pane) {
            if (m_active) m_active->setProperty("relayActive", false);
            m_active = pane;
            pane->setProperty("relayActive", true);
            for (Pane *p : allPanes()) { p->style()->unpolish(p); p->style()->polish(p); }
        }
        if (QWidget *page = pageOf(pane)) m_lastActive.insert(page, pane);
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
            Pane *pane = m_lastActive.value(page);
            const auto panes = panesIn(page);
            if (!pane && !panes.isEmpty()) pane = panes.first();
            QString title = pane ? shortPath(pane->cwd()) : QStringLiteral("Relay");
            if (panes.size() > 1) title += QStringLiteral("  ·  %1").arg(panes.size());
            m_tabs->setTabText(i, title);
            m_tabs->setTabToolTip(i, pane ? pane->cwd() : QString());
        }
        setWindowTitle(m_active ? QStringLiteral("Relay — ") + m_active->cwd() : QStringLiteral("Relay"));
    }

    void cycleTab(int delta) {
        if (m_tabs->count() < 2) return;
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + delta + m_tabs->count()) % m_tabs->count());
    }

    // Put `pane` beside `anchor` in the given orientation, reusing the anchor's splitter when
    // it already runs that way, otherwise wrapping the anchor in a new splitter.
    void insertBeside(Pane *anchor, Pane *pane, Qt::Orientation orientation, bool before) {
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
            wrapper->setSizes({1000, 1000});
            anchor->show(); wrapper->show();
        }
        pane->show();
        updateTitles();
    }

    void split(Qt::Orientation orientation) {
        Pane *anchor = m_active;
        if (!anchor) return;
        Pane *pane = nullptr;
        try { pane = createPane({{"cwd", anchor->cwd()}, {"workspace", anchor->workspace()}}); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return; }
        insertBeside(anchor, pane, orientation, false);
        setActive(pane);
        QTimer::singleShot(0, pane, [pane] { pane->focusInput(); });
    }

    void navigate(int key) {
        Pane *current = m_active;
        QWidget *page = current ? pageOf(current) : nullptr;
        if (!page) return;
        const QRect from(current->mapTo(page, QPoint(0, 0)), current->size());
        Pane *best = nullptr;
        double bestScore = 1e18;
        for (Pane *pane : panesIn(page)) {
            if (pane == current) continue;
            const QRect to(pane->mapTo(page, QPoint(0, 0)), pane->size());
            double gap = 0, offset = 0;
            // A candidate must lie on the requested side; prefer the nearest, then the most aligned.
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
        if (best) { setActive(best); best->focusInput(); }
    }

    void closeActive() {
        Pane *pane = m_active;
        if (!pane) return;
        QWidget *page = pageOf(pane);
        if (panesIn(page).size() > 1) closePane(pane, true);
        else if (m_tabs->count() > 1) closeTab(m_tabs->indexOf(page), true);
        else closeWindowWithWarning();
    }

public:
    void closePane(Pane *pane, bool record) {
        QWidget *page = pageOf(pane);
        if (!page) return;
        if (panesIn(page).size() <= 1) {
            // Last pane of its tab: close the tab, or the window when it is the last tab.
            if (m_tabs->count() > 1) closeTab(m_tabs->indexOf(page), record);
            else { if (record) { m_confirmedClose = true; } else { m_confirmedClose = true; m_skipRemember = true; } close(); }
            return;
        }
        auto *splitter = dynamic_cast<QSplitter *>(pane->parentWidget());
        if (!splitter) return;
        const int index = splitter->indexOf(pane);
        QWidget *neighbor = splitter->widget(index > 0 ? index - 1 : index + 1);
        const auto neighborPanes = panesIn(neighbor);
        Pane *focusNext = neighborPanes.isEmpty() ? nullptr : (index > 0 ? neighborPanes.last() : neighborPanes.first());
        if (record && focusNext) {
            ClosedItem item;
            item.kind = ClosedItem::PaneItem; item.window = this; item.sibling = focusNext;
            item.orientation = splitter->orientation(); item.before = index == 0;
            item.layout = serializeNode(pane);
            m_manager->remember(item);
        }
        pane->onStatus = nullptr; pane->onStateChanged = nullptr; pane->onShellExited = nullptr;
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
        if (m_active == pane || !m_active) m_active = nullptr;
        if (m_lastActive.value(page) == pane) m_lastActive.remove(page);
        if (focusNext) { setActive(focusNext); focusNext->focusInput(); }
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
        QString text = QStringLiteral("Close this window and its %1 tab(s) and %2 pane(s)?").arg(m_tabs->count()).arg(panes.size());
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
    QHash<QWidget *, QPointer<Pane>> m_lastActive;
    bool m_confirmedClose = false, m_skipRemember = false;
};

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

RelayWindow *WindowManager::newWindowAt(const QString &cwd) {
    return newWindow(QJsonArray{QJsonObject{{"pane", QJsonObject{{"cwd", cwd}, {"workspace", m_workspace}}}}});
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

int main(int argc, char **argv) {
    // Must precede QApplication: KDE platform plugins may open relayrc during construction.
    relay::theme::exposeKonsoleProfile();
    QApplication app(argc, argv);
    relay::theme::applyDarkTheme(app);
    QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
    QCoreApplication::setApplicationName(QStringLiteral("relay"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.relayterminal.Relay"));
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("Konsole-based terminal with rich input and BYOK agents."));
    parser.addHelpOption(); parser.addVersionOption();
    QCommandLineOption workspace(QStringList{QStringLiteral("w"), QStringLiteral("workspace")}, QStringLiteral("Initial terminal directory and agent workspace."), QStringLiteral("path"), QDir::currentPath());
    QCommandLineOption clean(QStringLiteral("clean-shell"), QStringLiteral("Do not source ~/.bashrc; useful for incompatible DEBUG/preexec prompt hooks."));
    parser.addOption(workspace); parser.addOption(clean); parser.process(app);
    const auto path = QFileInfo(parser.value(workspace)).canonicalFilePath();
    if (path.isEmpty() || !QFileInfo(path).isDir()) { QMessageBox::critical(nullptr, QStringLiteral("Relay"), QStringLiteral("Workspace must be an existing directory.")); return 1; }
    QDir::setCurrent(path);
    try {
        WindowManager manager(path, parser.isSet(clean));
        if (!manager.newWindowAt(path)) return 1;
        return app.exec();
    } catch (const std::exception &error) {
        QMessageBox::critical(nullptr, QStringLiteral("Relay could not start"), QString::fromUtf8(error.what()));
        return 1;
    }
}

