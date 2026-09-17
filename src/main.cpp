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
    void setMode(const QString &mode) { m_modeValue = mode; requestRoute(false, QStringLiteral("auto")); }
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

protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        // Split panes get narrow: drop the key hints and the agent workspace path first.
        if (m_help) m_help->setVisible(width() >= 900);
        updatePaths();
    }

    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
            auto *key = static_cast<QKeyEvent *>(event);
            auto *widget = qobject_cast<QWidget *>(object);
            // Only widgets inside this pane; do not steal F12 from dialogs.
            const bool inside = widget && (widget == this || isAncestorOf(widget));
            if (inside && key->key() == Qt::Key_F12 && key->modifiers() == Qt::NoModifier) {
                if (event->type() == QEvent::KeyPress) setNative(!m_native);
                event->accept();
                return true;
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
        auto *composerLayout = new QVBoxLayout(composer);
        auto *routeRow = new QHBoxLayout;
        m_routeLabel = new QLabel(QStringLiteral("AUTO · local detection"));
        m_routeLabel->setTextFormat(Qt::PlainText);
        routeRow->addWidget(m_routeLabel, 1);
        auto *submit = new QPushButton(QStringLiteral("Submit ↵"));
        connect(submit, &QPushButton::clicked, this, [this] { requestRoute(true, QStringLiteral("auto")); });
        routeRow->addWidget(submit); composerLayout->addLayout(routeRow);
        m_editor = new RichEditor; composerLayout->addWidget(m_editor);
        auto *help = new QLabel(QStringLiteral("Shift+Enter  newline     Ctrl+Enter  agent     Ctrl+Shift+Enter  terminal     ↑/↓  history     F12  native input"));
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

    // ----- fix and re-run loop (terminal mode) -----------------------------------------
    static constexpr int kMaxFixAttempts = 3;

    void status(const QString &text) { if (onStatus) onStatus(text); }
    void changed() { if (onStateChanged) onStateChanged(); }

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
              {"api_key", QString()}, {"workspace", m_workspace}});
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
        // tcgetpgrp() fails with ENOTTY on newer kernels unless the terminal is the caller's
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

    void setNative(bool enabled) {
        m_native = enabled;
        changed();
        m_editor->setReadOnly(enabled);
        if (enabled) {
            m_routeLabel->setText(QStringLiteral("NATIVE · keystrokes go directly to Konsole. F12 returns to the composer."));
            focusTerminal();
        } else {
            // Returning from native mode cancels Readline's partial line at a prompt.
            // Never inject a cancellation into a foreground TUI/process here.
            if (m_shellReady && m_iface && (m_iface->foregroundProcessId() <= 0 || m_iface->foregroundProcessId() == m_iface->terminalProcessId())) {
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
                  {"workspace", m_workspace}, {"extra", doc.object()}, {"max_tokens", tokens->value()}});
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
        setCentralWidget(m_tabs);
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
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
            return QMainWindow::eventFilter(object, event);
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget || widget->window() != this) return QMainWindow::eventFilter(object, event);
        auto *key = static_cast<QKeyEvent *>(event);
        const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        const int k = key->key();
        const bool ctrl = mods == Qt::ControlModifier, ctrlShift = mods == (Qt::ControlModifier | Qt::ShiftModifier);
        const bool alt = mods == Qt::AltModifier, altShift = mods == (Qt::AltModifier | Qt::ShiftModifier);
        std::function<void()> action;
        if (ctrl && k == Qt::Key_N) action = [this] { m_manager->newWindowAt(activeCwd()); };
        else if (alt && k == Qt::Key_Tab) action = [this] { m_manager->cycle(this, 1); };
        else if (altShift && (k == Qt::Key_Backtab || k == Qt::Key_Tab)) action = [this] { m_manager->cycle(this, -1); };
        else if (ctrl && k == Qt::Key_T) action = [this] { addTab(paneNode(activeCwd()), m_tabs->currentIndex() + 1); };
        else if (ctrl && k == Qt::Key_Tab) action = [this] { cycleTab(1); };
        else if (ctrlShift && (k == Qt::Key_Backtab || k == Qt::Key_Tab)) action = [this] { cycleTab(-1); };
        else if (ctrl && k == Qt::Key_P) action = [this] { split(Qt::Horizontal); };
        else if (ctrlShift && k == Qt::Key_P) action = [this] { split(Qt::Vertical); };
        else if (alt && (k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_Up || k == Qt::Key_Down)) action = [this, k] { navigate(k); };
        else if (ctrl && k == Qt::Key_W) action = [this] { closeActive(); };
        else if (ctrlShift && k == Qt::Key_W) action = [this] { m_manager->restore(this); };
        if (!action) return QMainWindow::eventFilter(object, event);
        // Accept the override so neither the composer nor Konsole consumes the key,
        // then act on the key press itself. Auto-repeat does not open a burst of tabs.
        event->accept();
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) QTimer::singleShot(0, this, action);
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
        m_mode = new QComboBox;
        m_mode->addItem(QStringLiteral("Auto detect"), QStringLiteral("auto"));
        m_mode->addItem(QStringLiteral("Terminal"), QStringLiteral("shell"));
        m_mode->addItem(QStringLiteral("Agent"), QStringLiteral("agent"));
        m_mode->setAccessibleName(QStringLiteral("Input destination"));
        toolbar->addWidget(m_mode);
        connect(m_mode, qOverload<int>(&QComboBox::activated), this, [this](int) {
            if (m_active) { m_active->setMode(m_mode->currentData().toString()); m_active->focusInput(); }
        });
        m_nativeAction = toolbar->addAction(QStringLiteral("Native terminal · F12"));
        m_nativeAction->setCheckable(true);
        connect(m_nativeAction, &QAction::triggered, this, [this](bool) { if (m_active) m_active->toggleNative(); });
        auto *interrupt = toolbar->addAction(QStringLiteral("Interrupt shell"));
        connect(interrupt, &QAction::triggered, this, [this] { if (m_active) m_active->interruptShell(); });
        toolbar->addSeparator();
        m_modelPicker = new QComboBox;
        m_modelPicker->setAccessibleName(QStringLiteral("Agent model"));
        m_modelPicker->setToolTip(QStringLiteral("Agent model for this pane. Keys come from the desktop keyring; switching starts a new conversation."));
        m_modelPicker->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        toolbar->addWidget(m_modelPicker);
        connect(m_modelPicker, qOverload<int>(&QComboBox::activated), this, [this](int index) {
            if (m_active) { m_active->selectModel(m_modelPicker->itemData(index).toString()); m_active->focusInput(); }
        });
        auto *reset = toolbar->addAction(QStringLiteral("New chat"));
        connect(reset, &QAction::triggered, this, [this] { if (m_active) m_active->newChat(); });
        auto *stop = toolbar->addAction(QStringLiteral("Stop agent"));
        connect(stop, &QAction::triggered, this, [this] { if (m_active) m_active->stopAgent(); });
        auto *spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toolbar->addWidget(spacer);
        auto *provider = toolbar->addAction(QStringLiteral("Provider / BYOK…"));
        connect(provider, &QAction::triggered, this, [this] { if (m_active) m_active->openProviderDialog(); });
        syncToolbar();
    }

    void syncToolbar() {
        const QSignalBlocker modeBlock(m_mode), modelBlock(m_modelPicker);
        const Pane *pane = m_active;
        m_mode->setCurrentIndex(std::max(0, m_mode->findData(pane ? pane->mode() : QStringLiteral("auto"))));
        m_nativeAction->setChecked(pane && pane->isNative());
        m_modelPicker->clear();
        const auto models = pane ? pane->storedModels() : QList<QPair<QString, QString>>();
        for (const auto &model : models) m_modelPicker->addItem(model.second, model.first);
        if (models.isEmpty()) m_modelPicker->addItem(QStringLiteral("No stored keys"));
        m_modelPicker->setEnabled(!models.isEmpty());
        if (pane) {
            const int index = m_modelPicker->findData(pane->currentPreset());
            if (index >= 0) m_modelPicker->setCurrentIndex(index);
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
    QComboBox *m_mode = nullptr, *m_modelPicker = nullptr;
    QAction *m_nativeAction = nullptr;
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

