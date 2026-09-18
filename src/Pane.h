// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One terminal pane -- the single largest thing in the app: a shell behind relay::TerminalBackend,
// its Bash bridge, the composer, the queue, and the pane's own agent worker and conversation.
// Pane never names RelayWindow or WindowManager; it calls up through std::function callbacks the
// window sets, which is what lets it sit below them here. QueueRowDelegate draws the queue rows.

#include "AppPaths.h"
#include "Keymap.h"
#include "Isolation.h"

#include "RichEditor.h"
#include "Theme.h"
#include "BoardPane.h"
#include "AgentUi.h"
#include "Completion.h"
#include "FileIndex.h"
#include "ShellHighlighter.h"
#include "Hints.h"
#include "Notifications.h"
#include "InputPolicy.h"
#include "ScreenPrompt.h"
#include "PaneLayout.h"
#include "QueueNav.h"
#include "PaneTitles.h"
#include "TurnTranscript.h"
#include "ModelSettings.h"
#include "SkillsDialog.h"
#include "SubagentTranscript.h"
#include "SubagentsPanel.h"
#include "JobsPanel.h"
#include "RequestLedger.h"
#include "RequestsPanel.h"
#include "Conversations.h"
#include "Logging.h"
#include "TerminalBackends.h"
#include "TerminalBackend.h"
#include "WindowState.h"
#include "RuntimeDirs.h"
#include "RemoteShare.h"
#include "backend/VTermBackend.h"
#include "Voice.h"
#include "Images.h"
#include "Aliases.h"
#include "MarkdownAnsi.h"
#include "WordWrap.h"
#include "OutputLinks.h"
#include "SlashCommands.h"
#include "view/TerminalView.h"
#include "session/TerminalSession.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
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
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QDesktopServices>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QKeySequence>
#include <QUrl>
#include <QInputDialog>
#include <QRegularExpression>
#include <QIcon>
#include <QTextCharFormat>
#include <QElapsedTimer>
#include <QDateTime>
#include <QListWidget>
#include <QTreeWidget>
#include <QToolButton>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QMimeDatabase>
#include <QScrollBar>

#include <sys/stat.h>
#include <sys/syscall.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <cmath>
#include <csignal>
#include <stdexcept>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

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
        // run_in_terminal (protocol 22). On a terminal entry: report its exit to the agent. On an
        // agent entry: it *is* that report, a prompt Relay wrote, shown and queued like a fix.
        bool handoff = false;
        bool noHandoff = false;   // a prompt from a paired device never gets the tool
        bool written() const { return fix || (agent && handoff); }   // by Relay, not by the user
        QString label() const {
            return fix ? QStringLiteral("fix request") : written() ? QStringLiteral("terminal result") : text;
        }
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
        // Say who is using it, so another Relay's startup sweep can tell this directory from one a
        // crash left behind (src/RuntimeDirs.h).
        relay::runtimedirs::markOwned(m_runtime.path());
        m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        // The saved scrollback is filed under the pane's token unless a restore hands it the id
        // its saved text already has (initRestore).
        m_scrollbackId = m_token;
        buildUi();
        startWorker();
        startTerminal(cleanShell);
        connect(&m_poll, &QTimer::timeout, this, [this] { pollShell(); });
        connect(&m_secretPoll, &QTimer::timeout, this, [this] { checkPasswordPrompt(); checkOomKills(); });
        m_secretPoll.start(1000);
        m_poll.start(kPollFastMs);
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
        if (!m_backend) return false;
        const int foreground = foregroundPid();   // an ioctl: asked once, this runs on every poll
        return foreground > 0 && foreground != shellPid();
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
        // The terminal text this pane had when Relay was last quit (src/WindowState.h). The pane
        // keeps the saved id, so the same file is rewritten instead of one per restart, and the
        // lines are replayed once the restarted shell reports its first prompt.
        const QString scrollback = spec.value(QStringLiteral("scrollback")).toString();
        if (relay::windowstate::isScrollbackId(scrollback)) {
            m_scrollbackId = scrollback;
            m_restoredScrollback = relay::windowstate::readScrollback(scrollback);
        }
        changed();
    }

    // ----- terminal scrollback across a restart (owner report, 2026-09-18) ---------------------
    // Saved when the window closes and when Relay quits; replayed into the restarted pane at its
    // first prompt. Plain text: see the note in src/WindowState.h on why the colours do not come
    // back with it.
    QString scrollbackId() const { return m_scrollbackId; }
    // The two rules the replay prints around the restored block. They are also what the save
    // filters out, so a pane that has been restored twice does not stack them.
    static QString scrollbackOpenMark() { return QStringLiteral("— scrollback from before the restart —"); }
    static QString scrollbackCloseMark() { return QStringLiteral("— end of restored scrollback; this shell is new —"); }
    void saveScrollback() const {
        if (!terminalCan(relay::TerminalBackend::Scrollback)) return;
        QStringList lines = m_backend->scrollbackText(relay::windowstate::kScrollbackMaxLines);
        // The visible screen is the newest part of what the user was reading. A full-screen
        // program (vim, less) owns it instead, and its frame is not output worth keeping.
        if (terminalCan(relay::TerminalBackend::ScreenText) && !m_backend->altScreen())
            lines += m_backend->screenText().split(QLatin1Char('\n'));
        // The rules a previous restore printed are Relay's own chrome, not output: saving them
        // would stack one pair per restart inside the history. Each run prints its own.
        lines.erase(std::remove_if(lines.begin(), lines.end(), [](const QString &line) {
                        return line == scrollbackOpenMark() || line == scrollbackCloseMark();
                    }),
                    lines.end());
        QString error;
        if (!relay::windowstate::writeScrollback(m_scrollbackId, lines, &error) && !error.isEmpty())
            fprintf(stderr, "relay: could not save this pane's scrollback: %s\n", qPrintable(error));
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
        m_entries.clear(); m_entriesPaused = false;
        keepSelectionOn(0);   // nothing left to select: the prompt box gives the item's text back
        rebuildQueueStrip(); changed();
    }
    void resumeAgentQueue() {
        m_entriesPaused = false; m_pauseReason.clear();
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
        // "role:" is a Main/Flash row, "gear:" the model options modal, and "vision:" the model this
        // one turn is running on because it carries an image: none of the three is a preset to
        // switch to. The first two are acted on where the box is built (chooseAgentRole,
        // openRolesDialog); this is the guard for the other callers — /model and the roles modal.
        if (id.startsWith(QStringLiteral("role:")) || id.startsWith(QStringLiteral("gear:"))
            || id.startsWith(QStringLiteral("vision:"))) return;
        // Picking a model from the chip puts the pane back on the main agent (protocol 13).
        if (m_agentRole != QStringLiteral("main")) { setAgentRole(QStringLiteral("main")); if (id == m_currentPreset) return; }
        if (id.isEmpty() || id == m_currentPreset) return;
        const auto preset = presetById(id);
        // A full configure starts a new conversation, which a running turn cannot have.
        if (!m_configured || preset.isEmpty()) {
            if (m_agentBusy) { status(QStringLiteral("Stop the current agent turn before configuring a new provider.")); changed(); return; }
            configurePreset(id, true); return;
        }
        // Switch the provider, keeping the conversation. Allowed while a turn runs (issue 3ES1): the
        // worker lets the request in flight finish on the old model and sends the next one to this
        // model; `model_changed` says which, and `model_applied` marks the moment in the transcript.
        send({{"type", "set_model"}, {"preset", id}, {"use_stored_key", true},
              {"base_url", preset.value(QStringLiteral("base_url")).toString()},
              {"model", preset.value(QStringLiteral("model")).toString()},
              {"extra", preset.value(QStringLiteral("extra")).toObject()},
              {"max_tokens", QSettings().value(QStringLiteral("provider/max_tokens"), 32768).toInt()}});
        rememberPreset(id);
        m_currentPreset = id; changed();
    }
    void openProviderDialog() { configure(); }
    // The pane's provider settings follow a model switch as a whole (card WFJM): the provider dialog
    // reads `provider/base|model|extra` as its defaults, and they used to keep the first preset's
    // endpoint after every chip or /model switch.
    void rememberPreset(const QString &id) {
        const auto preset = presetById(id);
        QSettings settings;
        settings.setValue(QStringLiteral("provider/preset"), id);
        if (preset.isEmpty()) return;
        settings.setValue(QStringLiteral("provider/base"), preset.value(QStringLiteral("base_url")).toString());
        settings.setValue(QStringLiteral("provider/model"), preset.value(QStringLiteral("model")).toString());
        settings.setValue(QStringLiteral("provider/extra"), QString::fromUtf8(
            QJsonDocument(preset.value(QStringLiteral("extra")).toObject()).toJson(QJsonDocument::Compact)));
    }

    // ----- model roles (protocol 13) -------------------------------------------------------
    // Every role defaults to "same as the main agent". Settings live under roles/<id>/{preset,model,effort};
    // a role is unset when it has no preset. The worker resolves keys and per-provider defaults.
    // Mirrors relay_core.roles.ROLES minus "main" (the pane's own model).
    static QStringList roleIds() {
        return {QStringLiteral("terminal_use"), QStringLiteral("subagent"), QStringLiteral("switchboard"),
                QStringLiteral("flash"), QStringLiteral("summaries"), QStringLiteral("suggestions"),
                QStringLiteral("chores"), QStringLiteral("audit"), QStringLiteral("vision"),
                QStringLiteral("route_assist")};
    }
    // The pane-agent role was called "fast" until 2026-09-18 (see backend/relay_core/roles.py:
    // DEPRECATED_ROLES). Saved layouts and settings written before then still say "fast"; every
    // read goes through this, and nothing writes the old name any more.
    static QString canonicalRole(const QString &role) {
        return role == QStringLiteral("fast") ? QStringLiteral("flash") : role;
    }
    static QString roleLabel(const QString &role) {
        static const QHash<QString, QString> labels{
            {QStringLiteral("main"), QStringLiteral("Main agent")},
            {QStringLiteral("terminal_use"), QStringLiteral("Terminal-use agent")},
            {QStringLiteral("subagent"), QStringLiteral("Subagent")},
            {QStringLiteral("switchboard"), QStringLiteral("Switchboard agent")},
            {QStringLiteral("flash"), QStringLiteral("Flash agent")},
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
    // surprise, not a saving; the Flash agent is a choice per pane (the model chip, /flash, Alt+F, or
    // the toggle in Settings › Agent) rather than what every pane after the first does by itself.
    static bool newPanesUseFlashAgent() {
        return QSettings().value(QStringLiteral("agent/panes_flash"), false).toBool();
    }
    QString agentRole() const { return m_agentRole; }
    // The model a role resolves to, as last reported by the worker.
    QString roleModel(const QString &role) const {
        return m_roleSummary.value(role).toObject().value(QStringLiteral("model")).toString();
    }
    void setAgentRole(const QString &role, bool announce = true) {
        if (role == m_agentRole) return;
        // Allowed mid-turn (issue 3ES1): the worker applies it before the turn's next request.
        m_agentRole = role;
        if (m_configured) send({{"type", "set_agent_role"}, {"role", role}});
        if (announce) {
            const QString model = roleModel(role);
            toast(role == QStringLiteral("main") ? QStringLiteral("Main agent for this pane")
                                                 : QStringLiteral("%1 for this pane%2").arg(roleLabel(role), model.isEmpty() ? QString() : QStringLiteral(" · ") + model));
        }
        changed();
    }
    // The model to print beside a role in the model box: the pane's live model when the pane is
    // running that role, otherwise the model the worker resolved the role to. Empty until the
    // worker has reported one (no key yet), and then the row is just the role's name.
    QString roleModelText(const QString &role) const {
        if (role == m_agentRole && !m_model.isEmpty()) return m_model;
        return roleModel(role);
    }
    // Picked from the model box (owner report, 2026-09-18: "the main use case for that is going to
    // be swapping between the main and flash models"). setAgentRole is a no-op when the pane is
    // already on that role and refuses mid-turn, and in both cases the box is left sitting on the
    // row that was clicked — so the chip is always rebuilt from the pane's actual state afterwards.
    void chooseAgentRole(const QString &role) {
        setAgentRole(canonicalRole(role));
        refreshPickers();
    }
    // Before the first configure: the role a new or restored pane starts with.
    void initAgentRole(const QString &role) { if (!m_configured) m_agentRole = role; }
    void toggleFlashAgent() {
        setAgentRole(m_agentRole == QStringLiteral("flash") ? QStringLiteral("main") : QStringLiteral("flash"));
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
                                                : QStringLiteral("Input: Auto"));
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
    // Whether the keyboard is in this pane, or will be once its window is active again. Timers
    // change a pane behind the user's back — a command finishes, a program asks for a password —
    // and a pane that is not being typed in must never take the keyboard for it: the rest of a
    // sentence meant for another pane landed in this one's prompt box, or in its masked password
    // field, one Enter from the program. focusInput() puts the keyboard in the right widget
    // whenever the user does come here.
    bool holdsFocus() const {
        const QWidget *top = window();
        const QWidget *focus = top ? top->focusWidget() : nullptr;
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
        if (key == QStringLiteral("agent/panes_flash")) return;   // only affects panes opened later
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

    // Coming back on screen: catch up at once instead of on the next quiet tick (tunePoll).
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        if (m_poll.isActive()) pollShell();
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
                hint(QStringLiteral("queue.remove.mouse"), QStringLiteral("Next time: ↑ opens a queued item in the prompt box, Shift+Delete removes, Ctrl+↑/↓ reorders"));
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
            for (const auto &pair : {std::pair<const char *, const char *>{"auto", "auto"},
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
            const QString data = m_modelBox->itemData(index).toString();
            // Owner report, 2026-09-18: "selecting model options in the model dropdown didnt do
            // anything. the main use case for that is going to be swapping between the main and
            // flash models." Two entries in this list are not models: the gear (the model options
            // modal, which lost its branch here when the strip was rebuilt) and the Main/Flash rows
            // (the pane's agent role). Both are handled before selectModel, which only knows presets.
            if (data == QStringLiteral("gear:modelOptions")) {
                refreshPickers();   // put the box back on the pane's model: the gear is not a choice
                openRolesDialog();
                hint(QStringLiteral("model.options.mouse"),
                     QStringLiteral("Tip: Actions › Settings › Models opens the same modals"));
                return;
            }
            if (data.startsWith(QStringLiteral("role:"))) {
                chooseAgentRole(data.mid(5)); focusInput();
                hint(QStringLiteral("model.role.mouse"),
                     relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("agent.flashAgent")),
                                                    QStringLiteral("the Main and Flash agents")));
                return;
            }
            selectModel(data); focusInput();
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
        setupJobsUi(layout);        // commands the agent left running, beneath that
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


    void clearNextContext() {
        m_ctxNextUsed = m_ctxNextWindow = m_ctxNextLimit = 0; m_ctxNextPercent = 0;
        m_ctxNextModel.clear(); m_ctxInFlightModel.clear();
    }

    void updateContextLabel() {
        if (!m_ctxLabel) return;
        if (m_ctxWindow <= 0) { m_ctxLabel->hide(); return; }
        // While a switch waits (issue 3ES1) the chip already names the new model, so the bar agrees
        // with it: the conversation against the window that serves the next request. The ↻ and the
        // tooltip say the request in flight is still on the old model.
        const bool next = m_ctxNextWindow > 0;
        const qint64 used = next ? m_ctxNextUsed : m_ctxUsed, window = next ? m_ctxNextWindow : m_ctxWindow;
        const qint64 limit = next ? m_ctxNextLimit : m_ctxLimit;
        const double percent = next ? m_ctxNextPercent : m_ctxPercent;
        if (m_compacting) {
            m_ctxLabel->setText(QStringLiteral("compacting…"));
        } else {
            const double left = std::max(0.0, 100.0 - percent);
            m_ctxLabel->setText(QStringLiteral("%1% left%2").arg(QString::number(left, 'f', left < 10 ? 1 : 0),
                                                                 next ? QStringLiteral(" ↻") : QString()));
        }
        const bool near = limit > 0 && used >= limit * 9 / 10;
        m_ctxLabel->setProperty("warn", near);
        m_ctxLabel->style()->unpolish(m_ctxLabel); m_ctxLabel->style()->polish(m_ctxLabel);
        QString tip = QStringLiteral("%1 of %2 tokens%3\nAuto-compacts at %4 tokens")
            .arg(QLocale().toString(used), QLocale().toString(window), m_ctxEstimated || next ? QStringLiteral(" (estimated)") : QString(),
                 QLocale().toString(limit));
        if (next)
            tip = QStringLiteral("Measured against %1's window, which serves the next request.\n%2\n%3\n"
                                 "The request in flight is still on %4 (%5-token window) until the switch lands.")
                .arg(m_ctxNextModel, tip,
                     used >= limit ? QStringLiteral("Over it: the switch compacts the conversation first.") : QString(),
                     m_ctxInFlightModel, QLocale().toString(m_ctxWindow)).replace(QStringLiteral("\n\n"), QStringLiteral("\n"));
        m_ctxLabel->setToolTip(tip);
        m_ctxLabel->show();
    }

    static QString relayMdPath() {
        const QString base = qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config"));
        return base + QStringLiteral("/relay/relay.md");
    }

    // Turn limits and request audit from Agent options (protocol 12.1).
    static QJsonObject requestOptions() {
        QSettings settings;
        return {{"max_steps", std::clamp(settings.value(QStringLiteral("agent/max_steps"), 256).toInt(), 1, 500)},
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
            // The header is a widget wrapping its row, not a bare layout, so placeThinking() can
            // measure what the panel spends on chrome instead of assuming a number (thinkingChrome).
            m_thinkingHeaderRow = new QWidget;
            auto *header = new QHBoxLayout(m_thinkingHeaderRow); header->setContentsMargins(0, 0, 0, 0);
            m_thinkingHeader = new QLabel; m_thinkingHeader->setObjectName(QStringLiteral("transcriptHeader"));
            header->addWidget(m_thinkingHeader, 1);
            auto *close = new QToolButton; close->setText(QStringLiteral("×")); close->setAutoRaise(true); close->setFocusPolicy(Qt::NoFocus);
            close->setToolTip(QStringLiteral("Hide for this turn (Actions › Agent options › Show thinking turns it off)"));
            connect(close, &QToolButton::clicked, this, [this] { m_thinkingDismissed = true; m_thinkingHeld = false; hideBubble(m_thinking); });
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
            box->addWidget(m_thinkingHeaderRow);
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
            m_thinkingHeld = false;   // this turn owns the panel now, not the key that reopened the last one
            m_thinkingView->clear();
            m_thinkingHeader->setText(QStringLiteral("Thinking… · %1").arg(m_model.isEmpty() ? QStringLiteral("agent") : m_model));
        }
        QTextCursor cursor(m_thinkingView->document());
        cursor.movePosition(QTextCursor::End);
        QTextCharFormat format; format.setForeground(relay::theme::TextMuted); format.setFontItalic(true);
        cursor.insertText(sanitize(text), format);
        m_thinkingView->verticalScrollBar()->setValue(m_thinkingView->verticalScrollBar()->maximum());
        if (!m_thinkingDismissed) placeThinking();   // it decides whether there is room to show it
    }

    // Everything in the panel that is not a line of reasoning: the frame's margins, the spacing, the
    // header row with its ▴ and × buttons, and the text view's own document margin. Measured rather
    // than assumed, because a hard-coded 30 px was less than half of it: the compact panel came out
    // 64 px tall with 15 px of viewport for a 17 px line, so it drew its header over an empty box —
    // the same half-drawn window cefdb02 set out to stop. Whoever presses Alt+R wants the reasoning,
    // not the frame around it.
    int thinkingChrome() const {
        if (!m_thinking || !m_thinkingView) return 0;
        const QMargins pad = m_thinking->layout() ? m_thinking->layout()->contentsMargins() : QMargins();
        const int spacing = m_thinking->layout() ? m_thinking->layout()->spacing() : 0;
        const int header = m_thinkingHeaderRow ? m_thinkingHeaderRow->sizeHint().height() : 0;
        return pad.top() + pad.bottom() + spacing + header + 2 * int(m_thinkingView->document()->documentMargin());
    }

    // The height the reasoning panel asks the pane's column for. It is a row now rather than an
    // overlay, so this is what moves the terminal up instead of covering it (owner report,
    // 2026-09-18: "the terminal needs to move up, rather than being covered up"); the two heights
    // the ▴ button switches between are the ones it always had.
    void placeThinking() {
        if (!m_thinking || !m_terminalHost) return;
        // Second in the queue for room, and hidden rather than clipped (owner, 2026-09-18). The
        // reasoning is commentary on a turn: losing it costs less than losing the queue, and the
        // panel is dismissible by hand anyway.
        const int taken = m_queueStrip && m_queueStrip->isVisible()
                          ? m_queueStrip->height() + (layout() ? layout()->spacing() : 0) : 0;
        // m_thinkingHeld is the keyboard holding it open between turns (toggleThinkingPanel): with no
        // turn running there is nothing to stream, but the last turn's reasoning is still in the
        // view and the key asked for it.
        if ((!m_thinkingShown && !m_thinkingHeld) || m_thinkingDismissed || !roomForBubble(taken)) { hideBubble(m_thinking); return; }
        showBubble(m_thinking);
        // Compact by default: the panel takes the terminal's space now, so it must take as little
        // as it can. The ▴ button expands it when the reasoning is worth reading.
        const int lineHeight = std::max(14, m_thinkingView->fontMetrics().height());
        const int chrome = thinkingChrome();
        const int span = bubbleSpan();
        const int wanted = m_thinkingExpanded ? span / 3 : lineHeight * 2 + chrome;
        int height = std::min(m_thinkingExpanded ? 220 : lineHeight * 2 + chrome, std::max(wanted, lineHeight + chrome));
        // Never more than half of what it shares with the terminal: in a pane squeezed down to a
        // few rows the terminal keeps the other half rather than vanishing under the panel.
        height = std::min(height, std::max(lineHeight + chrome, span / 2));
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

    // The least a bubble may be drawn at: one line of its header plus the padding around it. Under
    // that it is hidden instead, because a half-drawn header reads as a broken window rather than
    // as a small one (owner, 2026-09-18, from a three-high stack where both bubbles came out as
    // 14px slivers with their titles sliced through).
    int bubbleRow() const { return std::max(30, fontMetrics().height() + 14); }

    // What the pane could give to bubbles without squeezing the rows around them: what is left once
    // the header, the prompt box and two lines of terminal have what they need. Measured from the
    // pane and its other rows, never from the terminal host, because none of those change as a
    // bubble comes and goes — so the answer cannot oscillate. Measuring the terminal instead let a
    // strip be shown in a pane too short to draw it, and Qt squeezed it past its own minimum: the
    // header came out sliced through by the prompt box.
    int bubbleRoom() const {
        const QMargins margins = layout() ? layout()->contentsMargins() : QMargins();
        const int spacing = layout() ? layout()->spacing() : 0;
        int room = height() - margins.top() - margins.bottom() - 2 * spacing;
        if (m_headerWidget && m_headerWidget->isVisible()) room -= m_headerWidget->sizeHint().height();
        if (m_composer && m_composer->isVisible()) room -= m_composer->minimumSizeHint().height();
        return room - (2 * std::max(14, fontMetrics().height()) + 8);
    }

    // Room for another bubble beside `taken` pixels of bubble already spoken for?
    bool roomForBubble(int taken) const { return bubbleRoom() - taken >= bubbleRow(); }

    // A bubble asks for its height as a maximum over a one-row minimum, never as a fixed height: a
    // row's minimum is part of this pane's minimum, and a fixed one made a pane in a three-high
    // stack overflow its own column, with the queue strip drawn through the prompt box. As a
    // maximum the bubble is squeezed along with the terminal and the prompt box when the pane is
    // too short for all three, and takes exactly what it asked for whenever there is room.
    void setBubbleHeight(QWidget *bubble, int height) {
        height = std::max(0, height);
        const int floor = std::min(height, bubbleRow());
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
        QByteArray out = takeWrapped();
        if (!m_inlineOpen) { out += "\r\x1b[2K"; m_inlineOpen = true; m_atLineStart = true; holdShellResize(true); }
        if (!m_atLineStart) out += "\r\n";
        out += "\x1b]8;;" + url + "\x1b\\" + inkCode(Ink::Note) + sanitize(label).toUtf8() + "\x1b[0m" + "\x1b]8;;\x1b\\";
        out += inkCode(Ink::Note) + QByteArray("  (Ctrl+click)") + "\x1b[0m\r\n";
        m_atLineStart = true;
        writeTerminal(out);
        hint(QStringLiteral("turn.link"), QStringLiteral("Tip: Ctrl+click “tool calls” lines to inspect each call and its output"));
    }

public:
    bool thinkingPanelVisible() const { return m_thinking && m_thinking->isVisible(); }

    // Alt+R shows and hides this pane's reasoning panel (owner report, 2026-09-18: "need a keyboard
    // shortcut for showing / hiding the reasoning traces, maybe an F# key ... or alt+R").
    //
    // Between turns the key reopens the last turn's reasoning rather than going inert. That text is
    // still in the view — endThinking() only hides the panel — so there is something honest to show,
    // and a key that answers only during the seconds an agent happens to be thinking reads as broken
    // for the rest of the session. Reopened after the turn the header says which turn it is, so a
    // finished trace is never mistaken for a live one.
    //
    // Both refusals speak. A pane too short to draw the panel hides it on purpose (placeThinking),
    // and a pane that has never streamed reasoning has nothing to draw; silence in either case would
    // look like the same dead key.
    void toggleThinkingPanel() {
        const QString key = Keymap::instance().shortcutText(QStringLiteral("agent.thinkingPanel"));
        if (thinkingPanelVisible()) {
            m_thinkingDismissed = true;   // the state the × button sets: hidden for the rest of this turn
            m_thinkingHeld = false;
            placeThinking();
            toast(key.isEmpty() ? QStringLiteral("Reasoning hidden")
                                : QStringLiteral("Reasoning hidden · %1 shows it again").arg(key));
            return;
        }
        if (!m_thinking || m_thinkingView->document()->isEmpty()) {
            toast(showThinking() ? QStringLiteral("No reasoning yet in this pane")
                                 : QStringLiteral("Show thinking is off · Settings › General turns it on"));
            return;
        }
        // Second in line for room, behind the queue strip — the way placeThinking() measures it.
        const int taken = m_queueStrip && m_queueStrip->isVisible()
                          ? m_queueStrip->height() + (layout() ? layout()->spacing() : 0) : 0;
        if (!roomForBubble(taken)) {
            toast(QStringLiteral("This pane is too short for the reasoning panel · make it taller"));
            return;
        }
        m_thinkingDismissed = false;
        if (!m_thinkingShown) {
            m_thinkingHeld = true;   // no turn is running: the key holds it up until the next one starts
            if (m_thinkingHeader)
                m_thinkingHeader->setText(QStringLiteral("Thought · last turn · %1")
                                              .arg(m_model.isEmpty() ? QStringLiteral("agent") : m_model));
        }
        placeThinking();
    }

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
    // An alias runs three ways — the Settings pane (search or the Actions tab), `/name`, and the name typed in terminal mode.
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
            hooks.shellPid = [this] { return m_backend ? m_backend->shellPid() : 0; };
            hooks.foregroundPid = [this] {
                return m_backend ? m_backend->foregroundProcessId() : 0;
            };
            hooks.input = [this](const QByteArray &bytes) {
                if (m_backend) m_backend->sendText(QString::fromUtf8(bytes), false);
            };
            hooks.secret = [this](const QByteArray &bytes) { return submitRemoteSecret(bytes); };
            hooks.compose = [this](const QString &text, bool route, const QString &origin) {
                submitRemote(text, route, origin);
            };
            hooks.stopAgent = [this] { stopAgent(); };
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

    // A password line from a phone (docs/REMOTE-PROTOCOL.md section 6.7). The sidecar has
    // already checked the desktop-minted nonce and the prompt; this is the fresh termios read
    // immediately before the write, which is what keeps a line typed at a prompt that ended in
    // flight from landing at the shell — into history, onto the screen and out to every device.
    bool submitRemoteSecret(const QByteArray &bytes) {
        if (terminalMode() != TerminalMode::Secret || !m_backend) return false;
        relay::input::Secret secret;
        secret.set(QString::fromUtf8(bytes));
        QString line = secret.take();        // the line plus the newline, stored copy wiped
        m_backend->sendText(line, false);
        relay::input::wipe(line);
        m_echoTicks = 0;
        status(QStringLiteral("Password sent from your phone."));
        return true;
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
            if (!preset.isEmpty() && role.isEmpty()) { m_currentPreset = preset; rememberPreset(preset); }
            // Mid-turn (issue 3ES1) the chip moves now; the model still answering keeps its window as
            // m_ctxWindow until `model_applied`, and the bar shows the new one from the `context`
            // event's `next`, which follows this event.
            const QString applies = event.value(QStringLiteral("applies")).toString();
            const bool afterCompaction = applies == QStringLiteral("after_compaction");
            const bool later = applies == QStringLiteral("next_step") || applies == QStringLiteral("turn_end") || afterCompaction;
            const bool willCompact = event.value(QStringLiteral("will_compact")).toBool();
            const qint64 window = event.value(QStringLiteral("context_window")).toVariant().toLongLong();
            if (window > 0 && !later) m_ctxWindow = window;
            const QString effort = event.value(QStringLiteral("effort")).toString();
            if (efforts().contains(effort)) m_effort = effort;
            const QString inFlight = event.value(QStringLiteral("in_flight_model")).toString();
            const QString what = later
                ? (afterCompaction
                       ? QStringLiteral("Model: %1 once the conversation is compacted to fit its window").arg(m_model)
                       : applies == QStringLiteral("turn_end")
                       ? QStringLiteral("Model: %1 from the next turn · this image turn finishes on %2").arg(m_model, inFlight)
                       : QStringLiteral("Model: %1 from the next step · %2 is not interrupted").arg(m_model, inFlight))
                : role.isEmpty() || role == QStringLiteral("main")
                ? QStringLiteral("Model: %1 · conversation kept").arg(m_model)
                : QStringLiteral("%1: %2 · conversation kept").arg(roleLabel(role), m_model);
            status(what); toast(what);
            if (later) {
                // The clock owns the status line while a turn runs, so the "not now, next step" part
                // goes in the transcript too; `model_applied` marks where it landed.
                ensureLineStart();
                if (afterCompaction)
                    printInline(QStringLiteral("↻ %1 takes over once the conversation is compacted to fit its window · "
                                               "%2 summarises it\n").arg(m_model, inFlight), Ink::Note);
                else
                    printInline(QStringLiteral("↻ %1 takes over %2 · %3 is not interrupted%4\n")
                        .arg(m_model, applies == QStringLiteral("turn_end") ? QStringLiteral("after this turn")
                                                                            : QStringLiteral("at the next step"),
                             inFlight, willCompact ? QStringLiteral(" · will compact to fit") : QString()), Ink::Note);
                if (!m_agentBusy && !moreTurnsPending()) closeInline();
            }
            changed();
            return true;
        }
        if (type == QStringLiteral("model_applied")) {
            // The moment a mid-turn switch takes effect (issue 3ES1): at a step boundary, before the
            // next request, or once the turn is over. One line in the transcript, where it happened.
            const QString model = event.value(QStringLiteral("model")).toString();
            const qint64 window = event.value(QStringLiteral("context_window")).toVariant().toLongLong();
            if (window > 0) m_ctxWindow = window;
            QString line = QStringLiteral("→ now on %1").arg(model);
            if (event.value(QStringLiteral("at")).toString() == QStringLiteral("turn_end"))
                line += QStringLiteral(" · from the next turn");
            if (event.value(QStringLiteral("history_converted")).toBool())
                line += QStringLiteral(" · conversation converted from %1").arg(event.value(QStringLiteral("from_model")).toString());
            if (event.value(QStringLiteral("compacted")).toBool())
                line += QStringLiteral(" · compacted to fit its window");
            ensureLineStart();
            printInline(line + '\n', Ink::Note);
            if (!m_agentBusy && !moreTurnsPending()) closeInline();
            clearNextContext();
            updateContextLabel();
            changed();
            return true;
        }
        if (type == QStringLiteral("model_switch_refused")) {
            // A switch the new window cannot hold, even compacted (issue 3ES1): the pane stays on the
            // model in force, so the chip, the role and the provider settings go back to it.
            const QString refused = event.value(QStringLiteral("model")).toString();
            const QString current = event.value(QStringLiteral("current_model")).toString();
            if (!current.isEmpty()) m_model = current;
            const QString role = event.value(QStringLiteral("agent_role")).toString();
            const QString preset = event.value(QStringLiteral("preset")).toString();
            if (!role.isEmpty()) m_agentRole = role;
            else if (!preset.isEmpty()) { m_currentPreset = preset; rememberPreset(preset); }
            const qint64 window = event.value(QStringLiteral("context_window")).toVariant().toLongLong();
            if (window > 0) m_ctxWindow = window;
            clearNextContext();
            const QString reason = event.value(QStringLiteral("reason")).toString();
            ensureLineStart();
            printInline(QStringLiteral("✗ %1 did not take over · %2\n").arg(refused, reason), Ink::Error);
            if (!m_agentBusy && !moreTurnsPending()) closeInline();
            const QString what = QStringLiteral("Still on %1 · %2's window is too small for this conversation").arg(m_model, refused);
            status(what); toast(what);
            updateContextLabel();
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
            // A switch waiting to land (issue 3ES1): the bar measures against its window instead.
            const QJsonObject next = event.value(QStringLiteral("next")).toObject();
            if (next.isEmpty()) clearNextContext();
            else {
                m_ctxNextUsed = next.value(QStringLiteral("used_tokens")).toVariant().toLongLong();
                m_ctxNextWindow = next.value(QStringLiteral("window")).toVariant().toLongLong();
                m_ctxNextLimit = next.value(QStringLiteral("limit_tokens")).toVariant().toLongLong();
                m_ctxNextPercent = next.value(QStringLiteral("percent")).toDouble();
                m_ctxNextModel = next.value(QStringLiteral("model")).toString();
                m_ctxInFlightModel = next.value(QStringLiteral("in_flight_model")).toString();
            }
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
            const QString forModel = event.value(QStringLiteral("for_model")).toString();   // issue 3ES1
            printInline(QStringLiteral("Conversation compacted (%1) · %2 → %3 tokens\n")
                .arg(forModel.isEmpty() ? event.value(QStringLiteral("reason")).toString(QStringLiteral("manual"))
                                        : QStringLiteral("to fit %1's window").arg(forModel),
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
    // /conversations and the Actions list: every saved conversation and Relay's terminal
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

    // forAgent: a command the agent handed over is captured whatever the index settings say, because
    // its output goes to the agent (protocol 22); the index still gets only what the settings allow.
    void beginCommandCapture(const QString &command, bool forAgent = false) {
        m_captureForAgent = forAgent; m_handoffOutput.clear();
        if ((!indexTerminalHistory() && !forAgent) || command.trimmed().isEmpty()) { m_captureCommand.clear(); return; }
        m_captureCommand = command;
        m_captureCwd = m_cwd;
        m_captureAt = QDateTime::currentSecsSinceEpoch();
        m_capture.clear();
        m_capturing = (indexTerminalOutput() || forAgent) && m_backend;
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
        if (m_captureForAgent) m_handoffOutput = output.trimmed();
        if (!output.trimmed().isEmpty() && indexTerminalOutput()) item.insert(QStringLiteral("output"), output.trimmed());
        m_captureCommand.clear();
        m_capture.clear();
        m_captureForAgent = false;
        if (m_workerReady && indexTerminalHistory())
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
            // The card `?` shows in an empty prompt box. `/help` is what people type when they do
            // not know `?` yet, and it is where an unknown command points them (issue #Q4SD).
            {QStringLiteral("help"), QString(), QStringLiteral("The keys and prefixes Relay answers to (same as ?)")},
            {QStringLiteral("model"), QStringLiteral("[name]"), QStringLiteral("Switch model, keeping the conversation")},
            {QStringLiteral("main"), QString(), QStringLiteral("Run this pane on the Main model")},
            {QStringLiteral("flash"), QString(), QStringLiteral("Run this pane on the Flash model (same as Alt+F)")},
            {QStringLiteral("glm"), QString(), QStringLiteral("Switch to the GLM Coding Plan")},
            {QStringLiteral("kimi"), QString(), QStringLiteral("Switch to the Kimi Coding Plan")},
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
        // A queued item is in the box, not a command being typed: its leading "/" must not open the
        // popup, which would take the arrow keys the queue is using.
        if (m_selected >= 0) { hideSlashPopup(); return; }
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

    // Every name a `/command` can mean in this pane: the built-ins, then this window's aliases,
    // which `/name` runs the same way (issue G8DK). The order is the order they are suggested in.
    QStringList slashNames() const {
        QStringList names;
        for (const auto &command : slashCommands()) names << command.name;
        for (const auto &alias : m_aliasList)
            if (!alias.shadowed && !names.contains(alias.name)) names << alias.name;
        return names;
    }

    // A `/command` Relay does not have. It used to fall through to the router, which sent the line
    // to the agent — and the agent, or terminal mode, handed it to Bash, so the answer came back as
    // `bash: /nosuchthing: command not found`: the shell answering for a command the shell never
    // owned (owner report, 2026-09-18). Relay answers it instead, names the closest real commands
    // and points at the list. Only a name-shaped first word takes this path: `/usr/bin/foo` and
    // `/tmp` are paths and still run (relay::slash::attemptedName), and `/shell …` and `/agent …`
    // are the router's own prefixes, so they are found among the names and left alone.
    bool reportUnknownSlashCommand(const QString &text) {
        const QString name = relay::slash::attemptedName(text);
        if (name.isEmpty()) return false;
        const QStringList names = slashNames();
        if (names.contains(name)) return false;
        m_editor->remember(text.trimmed());
        m_editor->clear();
        hideSlashPopup();
        clearAiGhost();
        ensureLineStart();
        printInline(QStringLiteral("✗ ") + relay::slash::unknownLine(name, names) + '\n', Ink::Error);
        closeInline();
        status(QStringLiteral("Unknown command: /%1").arg(name));
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
        } else if (name == QStringLiteral("main") || name == QStringLiteral("flash")) {
            // The pane's own agent, not the tier table: /flash runs this conversation on the Flash
            // model and /main puts it back, both keeping the conversation (the same switch as Alt+F).
            // Saying so even when the pane is already there means the command always reports where
            // it ended up, rather than looking like it did nothing.
            if (m_agentRole == name) {
                const QString model = name == QStringLiteral("main") ? m_model : roleModel(name);
                status(QStringLiteral("Already on the %1%2.").arg(roleLabel(name), model.isEmpty() ? QString() : QStringLiteral(" · ") + model));
                return;
            }
            setAgentRole(name);
        } else if (name == QStringLiteral("glm") || name == QStringLiteral("kimi")) {
            // One word for the two providers the owner actually pays for. The Coding Plan preset is
            // tried first so the subscription is spent before pay-as-you-go credit, and the other
            // preset in the family is the fallback; with neither key stored the command says which
            // provider is missing instead of opening a picker.
            const bool glm = name == QStringLiteral("glm");
            const QStringList order = glm ? QStringList{QStringLiteral("glm-coding"), QStringLiteral("glm")}
                                          : QStringList{QStringLiteral("kimi-code"), QStringLiteral("kimi")};
            // No busy check: selectModel accepts a switch mid-turn (issue 3ES1).
            for (const QString &id : order) {
                const auto stored = std::find_if(m_stored.cbegin(), m_stored.cend(),
                                                 [&](const auto &entry) { return entry.first == id; });
                if (stored == m_stored.cend()) continue;
                if (id == m_currentPreset && m_agentRole == QStringLiteral("main")) {
                    status(QStringLiteral("Already on %1.").arg(stored->second));
                    return;
                }
                selectModel(id);   // also puts the pane back on the Main agent
                status(QStringLiteral("Model: %1.").arg(stored->second));
                return;
            }
            status(QStringLiteral("No stored %1 key. Add one in Settings › Models › API keys….")
                       .arg(glm ? QStringLiteral("GLM") : QStringLiteral("Kimi")));
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
        else if (name == QStringLiteral("resume")) {
            openResume();
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.resume")); !keys.isEmpty())
                hint(QStringLiteral("resume.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("resume a session")));
        } else if (name == QStringLiteral("conversations")) {
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
        } else if (name == QStringLiteral("help")) {
            // The same card `?` shows, never a second surface for the same list. Typing the
            // command while the card is up must leave it up, so this shows rather than toggles.
            if (!(m_helpCard && m_helpCard->isVisible())) toggleHelpCard();
            hint(QStringLiteral("help.slash"), QStringLiteral("Next time: press ? in an empty prompt box"));
        }
    }

    // ----- steering, away recaps, AI suggestions -------------------------------------------------
    struct SteerEntry { QString requestId, itemId, text; QJsonArray attachments; };

    // Enter on an empty prompt right after queuing an agent prompt while the agent works: deliver that
    // prompt at the agent's next tool call instead of after the turn.
    bool upgradeLastQueuedToSteer() {
        if (!m_agentBusy || m_entries.isEmpty() || !m_lastQueuedAt.isValid() || m_lastQueuedAt.elapsed() > 15000) return false;
        const QueueEntry &last = m_entries.last();
        if (!last.agent || last.written() || last.id != m_lastQueuedEntryId) return false;
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
        if (m_jobsPanel) m_jobsPanel->setAllowed(!m_composer || m_composer->isVisible());
        placeQueueStrip();
    }

    void placeSubagentOverlay() {
        if (!m_subagentOverlay) return;
        const int w = std::min(width() - 16, std::max(340, width() * 3 / 5));
        // Below the pane's header, like the requests panel: the header's top right is where the
        // pane chrome's buttons sit, and the overlay's × would land on the pane's own × there.
        const int top = m_terminalHost ? m_terminalHost->mapTo(this, QPoint(0, 0)).y() : 0;
        m_subagentOverlay->setGeometry(width() - w - 8, top + 8, w, std::max(160, height() - top - 16));
    }
    // ----- end subagents UI ---------------------------------------------------------------------

    // ----- jobs: commands the agent left running (src/JobsPanel.h) ----------------------------
    void setupJobsUi(QVBoxLayout *layout) {
        m_jobsPanel = new relay::JobsPanel(&m_jobs, this);
        layout->addWidget(m_jobsPanel);
        m_jobsPanel->onOpen = [this](const QString &id) { send({{"type", "job_output_get"}, {"job_id", id}}); };
        m_jobsPanel->onStop = [this](const QString &id, bool mouse) {
            send({{"type", "job_stop"}, {"job_id", id}});
            toast(QStringLiteral("Stopping ") + id);
            if (mouse)
                hint(QStringLiteral("jobs.stop.mouse"), relay::ShortcutHints::nextTime(QStringLiteral("↓ from the prompt, then x"), QStringLiteral("stop a command")));
        };
        m_jobsPanel->onExit = [this] { focusInput(); };
        // Up past the first job goes back to the running-agents list when it is showing.
        m_jobsPanel->onExitUp = [this] {
            if (m_agentsPanel && m_agentsPanel->isVisible()) m_agentsPanel->setFocus(); else focusInput();
        };
        if (m_agentsPanel) m_agentsPanel->onBelow = [this] { if (m_jobsPanel && m_jobsPanel->isVisible()) m_jobsPanel->enter(); };
        m_jobs.onChanged = [this] { m_jobsPanel->refresh(); placeQueueStrip(); };
        m_jobs.onFinished = [this](const relay::JobRow &row) {
            status(QStringLiteral("%1 finished (%2): %3").arg(row.id, row.state(row.elapsedMs), row.command.left(80)));
        };
    }

    // The job's output as the worker kept it, in a file pane like a stored tool call's output.
    void openJobOutput(const QJsonObject &event) {
        QJsonObject result{{"output", event.value(QStringLiteral("output"))}};
        if (!event.value(QStringLiteral("running")).toBool() && event.value(QStringLiteral("exit_code")).isDouble())
            result.insert(QStringLiteral("exit_code"), event.value(QStringLiteral("exit_code")));
        QString preview = QStringLiteral("$ ") + event.value(QStringLiteral("command")).toString();
        if (event.value(QStringLiteral("running")).toBool()) preview += QStringLiteral("\n(still running: this is its output so far)");
        if (event.value(QStringLiteral("truncated")).toBool())
            preview += QStringLiteral("\n(%1 earlier bytes not shown)").arg(qint64(event.value(QStringLiteral("omitted_bytes")).toDouble()));
        openToolOutput({{"name", "job"}, {"call_id", event.value(QStringLiteral("job_id"))}, {"preview", preview}, {"result", result}});
    }

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
            row(keys.shortcutText(QStringLiteral("board.open")), QStringLiteral("Switchboard: cards and threads"));
            // The explorer is one of the keys people reach for most and it was only in the full
            // list (owner, 2026-09-17). One key opens and closes it, which the wording has to say.
            row(keys.shortcutText(QStringLiteral("files.explorer")), QStringLiteral("file explorer (again to close)"));
            row(keys.shortcutText(QStringLiteral("agent.requests")).isEmpty() ? QStringLiteral("/tasks")
                                                                             : keys.shortcutText(QStringLiteral("agent.requests")),
                QStringLiteral("tasks in this session"));
            row(keys.shortcutText(QStringLiteral("palette.open")), QStringLiteral("every action, in a list you can filter"));
            row(keys.shortcutText(QStringLiteral("app.settings")), QStringLiteral("options"));
            row(keys.shortcutText(QStringLiteral("agent.resume")).isEmpty() ? QStringLiteral("/resume")
                                                                           : keys.shortcutText(QStringLiteral("agent.resume")),
                QStringLiteral("resume a saved session"));
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
    QString tasksProgress() const { return m_ledger.hasTasks() ? m_ledger.chipText() : QString(); }

private:
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
        QByteArray out = takeWrapped();
        if (!m_inlineOpen) { out += "\r\x1b[2K"; m_inlineOpen = true; m_atLineStart = true; holdShellResize(true); }
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
            // A `/command` that is not one of the above never reaches the router: Relay says so
            // itself rather than letting Bash answer with "command not found".
            if (reportUnknownSlashCommand(m_editor->toPlainText())) return;
            clearAiGhost();
        } else if (const SlashCommand *command = slashCommandFor(m_editor->toPlainText())) {
            setRouteText(QStringLiteral("COMMAND · /%1 · %2").arg(command->name, command->description));
            return;
        } else if (const QString name = relay::slash::attemptedName(m_editor->toPlainText());
                   !name.isEmpty() && !slashNames().contains(name)) {
            // The preview says it before Enter does: the label of a command Relay does not have.
            const QStringList close = relay::slash::closest(name, slashNames(), 2);
            setRouteText(close.isEmpty()
                             ? QStringLiteral("COMMAND · /%1 · not a Relay command · / for the list").arg(name)
                             : QStringLiteral("COMMAND · /%1 · not a Relay command · did you mean /%2?")
                                   .arg(name, close.join(QStringLiteral(" or /"))));
            return;
        }
        if (!m_workerReady) {
            if (submit) status(QStringLiteral("Local router is not ready; use the native terminal or restart Relay."));
            return;
        }
        if (submit && (!m_pendingSubmit.isEmpty() || !m_heldDecision.isEmpty() || m_loading)) return;
        const QString id = QString::number(++m_requestId);
        QString mode = overrideMode == QStringLiteral("auto") ? m_modeValue : overrideMode;
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
            m_handoffChain = 0;   // the user typed something: a chain of hand-overs starts over
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
        // A shared pane's agent is watched from elsewhere too. The sidecar's allow-list decides
        // what actually reaches a device; this only offers it.
        relay::RemoteShare::instance().paneEvent(m_token, event);
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
        if (m_jobs.handle(event)) return;   // the jobs list; reset/ready are observed and passed on
        if (type == QStringLiteral("job_output")) { openJobOutput(event); return; }
        if (handleProgramEvent(type, event)) return;    // the agent typing into this pane's program
        if (type == QStringLiteral("terminal_command")) { handleTerminalCommand(event); return; }   // protocol 22
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
            if (takeRemoteRoute(id, event)) return;
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
                // A model server on this machine (card #24XJ) needs no key, so `local` makes a row
                // selectable just as a stored key does. The worker appends those rows after the
                // keyed presets, so they stay last in this list.
                if (preset.value(QStringLiteral("has_stored_key")).toBool()
                    || preset.value(QStringLiteral("local")).toBool())
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
                auto usable = [this](const QString &id) {
                    return std::any_of(m_stored.cbegin(), m_stored.cend(), [&](const auto &entry) { return entry.first == id; });
                };
                // A restored pane keeps the model it had; otherwise the saved choice, then Warp's
                // default agent model, then the first stored key. A local endpoint is selectable
                // but never wins that last step over a key (card #24XJ): it is picked on its own
                // only when it is the restored/saved preset, or when nothing has a key at all.
                QString choice = m_restorePreset;
                m_restorePreset.clear();
                if (!usable(choice)) choice = QSettings().value(QStringLiteral("provider/preset")).toString();
                if (!usable(choice)) choice = event.value(QStringLiteral("warp_default")).toString();
                if (!usable(choice)) {
                    const auto keyed = std::find_if(m_stored.cbegin(), m_stored.cend(), [this](const auto &entry) {
                        return !presetById(entry.first).value(QStringLiteral("local")).toBool();
                    });
                    choice = (keyed == m_stored.cend() ? m_stored.first() : *keyed).first;
                }
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
            if (prompt.handoff) {
                ensureLineStart();
                printInline(QStringLiteral("✦ the command finished · its result went to the agent\n"), Ink::Note);
            } else if (!prompt.fix && !prompt.text.isEmpty()) {
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
            // A command past its wait keeps running as a job the agent reads or stops later
            // (backend/relay_core/jobs.py); a stopped one is not a failure the pane paints red.
            else if (result.value(QStringLiteral("still_running")).toBool())
                printInline(QStringLiteral("▸ still running as %1%2\n").arg(result.value(QStringLiteral("job_id")).toString(), size), Ink::Note);
            else if (result.value(QStringLiteral("stopped")).toBool())
                printInline(QStringLiteral("■ stopped %1%2\n").arg(result.value(QStringLiteral("job_id")).toString(), size), Ink::Note);
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
        // A command the agent put in the prompt box (protocol 22): its exit goes back to the agent,
        // which replaces the fix loop for this one submission.
        const bool handoff = m_handoffPrefill;
        m_handoffPrefill = false;
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
                submitTerminal(text, !handoff, readsLikeRequest, handoff);
                return;
            }
            if (!valid) { submitAgent(text, true, problem); return; }
            submitTerminal(text, false, false, handoff);
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
        tunePoll();   // a background pane waits for the acknowledgement at the fast rate
        m_fixCommand = watch ? text : QString(); m_fixAttempt = attempt; m_fixWatch = watch; m_fixArmed = false;
        m_commandNatural = natural;   // wrong-mode hints: reads like a request (agent_signal)
        // Stage text via a bound Readline function. Enter is sent only after its hash acknowledgement.
        sendShellInput(QString(QChar(24)) + QChar(18));
        const quint64 serial = ++m_loadSerial;
        QTimer::singleShot(2500, this, [this, serial] {
            if (m_loading && m_loadSerial == serial) {
                m_loading = false; clearFix(); setNative(true);
                m_handoffNext = false;
                answerTerminalCommand(false, QStringLiteral("failed"), QStringLiteral("The shell did not acknowledge the command, so Enter was not sent."));
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
        m_shellSequence.clear(); m_stateSeen = false; m_inlineOpen = false; m_atLineStart = true; m_autoHuman = false;
        m_shellResizeHeld = false;   // the new terminal starts unheld
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
        const bool mine = holdsFocus();   // asked before the prompt box hides and Qt moves the focus on
        m_secretMode = true;
        m_secretProgram = program;
        hideAtPopup(); hideCardPopup(); hideSlashPopup(); hideTabPopup(); clearAiGhost();
        m_secretChip->setText(relay::input::passwordChip(m_secretProgram));
        m_secretChip->show();
        setRouteText(QStringLiteral("PASSWORD · the line goes to the program, not to Relay"));
        scrubSecretEditor();
        m_editor->hide();
        m_secretEdit->show();
        if (mine) m_secretEdit->setFocus(Qt::OtherFocusReason);
        refreshProgramHint();
        changed();
        toast(QStringLiteral("Password prompt · type it here · %1 asks the agent instead")
                  .arg(Keymap::instance().shortcutText(QStringLiteral("input.toggle"))));
    }

    void leaveSecretMode() {
        if (!m_secretMode) return;
        const bool mine = holdsFocus();   // before the masked field hides
        m_secretMode = false;
        m_secretProgram.clear();
        scrubSecretEditor();
        m_secretEdit->hide();
        m_secretChip->hide();
        m_editor->show();
        if (mine && !m_native) m_editor->setFocus(Qt::OtherFocusReason);
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
        // Model roles (protocol 13), as their own group under the presets. Owner report,
        // 2026-09-18: "the main use case for that is going to be swapping between the main and
        // flash models" — Alt+F was the only way to do it, so the two roles a pane can run are
        // rows here, the live one ticked. Picking a preset above still puts the pane back on the
        // main agent.
        m_modelBox->insertSeparator(m_modelBox->count());
        QStringList paneRoles{QStringLiteral("main"), QStringLiteral("flash")};
        if (!paneRoles.contains(m_agentRole)) paneRoles << m_agentRole;   // a role a session restored
        for (const QString &role : std::as_const(paneRoles)) {
            // A pane on another role reads as that role, not as its stored preset: the collapsed
            // chip is the role's row, which is where it sat before these rows existed. That row is
            // the box's current item, so it needs no tick; the main row does, because when the pane
            // is on the main agent the box sits on the preset above.
            const bool live = role == m_agentRole;
            const bool selects = live && role != QStringLiteral("main");
            const QString model = roleModelText(role);
            m_modelBox->addItem(QStringLiteral("%1 %2%3").arg(live && !selects ? QString(QChar(0x2713)) : QStringLiteral(" "),
                                                              roleLabel(role),
                                                              model.isEmpty() ? QString() : QStringLiteral(" · ") + model),
                                QStringLiteral("role:") + role);
            if (selects) m_modelBox->setCurrentIndex(m_modelBox->count() - 1);
        }
        // Last entry: the model options modal (default provider, Main/Flash/Lite, per-job overrides).
        // It stays reachable with no stored key, which is exactly when it is needed most.
        m_modelBox->insertSeparator(m_modelBox->count());
        m_modelBox->addItem(QString(QChar(0x2699)) + QStringLiteral("  Model options…"),
                            QStringLiteral("gear:modelOptions"));
        m_modelBox->setEnabled(true);
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
            // A model server on this machine reads as local rather than as one more provider
            // (card #24XJ): no key stands behind it and it answers from this machine.
            const QString mark = preset.value(QStringLiteral("local")).toBool()
                                     ? QStringLiteral(" · local") : QString();
            const QString model = preset.value(QStringLiteral("model")).toString();
            if (!model.isEmpty()) return model.section('/', -1).toLower() + mark;
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

    // ----- run_in_terminal: the agent hands a command to this pane's shell (protocol 22) --------
    // The agent chose "run" or "prefill"; relay::input::handoffAction decides what happens from the
    // state right now. A run goes through runInTerminal like any other command, so Enter is only
    // sent after the shell's hash acknowledgement, and the answer to the worker waits for it.
    void handleTerminalCommand(const QJsonObject &event) {
        const QString id = event.value(QStringLiteral("id")).toString();
        const QString command = event.value(QStringLiteral("command")).toString();
        const QString intent = event.value(QStringLiteral("intent")).toString();
        const bool report = event.value(QStringLiteral("report_back")).toBool(true);
        if (!m_handoffId.isEmpty()) answerTerminalCommand(false, QStringLiteral("failed"), QStringLiteral("Another command was handed over first."));
        m_handoffId = id;
        relay::input::HandoffState state;
        state.wantsRun = event.value(QStringLiteral("mode")).toString() == QStringLiteral("run");
        state.ceiling = relay::input::handoffCeiling(QSettings().value(QStringLiteral("agent/terminal_handoff")).toString());
        state.shellIdle = shellIdleForQueue() && !(m_activeValid && !m_active.agent);
        state.boxFree = !m_native && m_selected < 0 && m_editor->toPlainText().trimmed().isEmpty();
        state.chain = m_handoffChain;
        auto action = relay::input::handoffAction(state);
        ensureLineStart();
        if (action == relay::input::HandoffAction::Run) {
            printInline(QStringLiteral("✦ running in your terminal · %1\n").arg(intent), Ink::Note);
            m_handoffNext = report;
            if (runInTerminal(command, false, 0)) { ++m_handoffChain; return; }   // answered from `loaded`
            m_handoffNext = false;
            state.shellIdle = false;
            action = relay::input::handoffAction(state);
        }
        if (action == relay::input::HandoffAction::Prefill) {
            printInline(QStringLiteral("✦ in your prompt box · %1 · Enter runs it\n").arg(intent), Ink::Note);
            if (m_modeValue != QStringLiteral("shell")) setPrefixMode(QStringLiteral("shell"));   // this submission only
            setComposerText(command);
            m_handoffPrefill = report;
            answerTerminalCommand(true, QStringLiteral("prefilled"));
            return;
        }
        printInline(QStringLiteral("✦ not run · %1\n").arg(
            action == relay::input::HandoffAction::RefuseDraft ? QStringLiteral("the prompt box has your text in it")
            : action == relay::input::HandoffAction::RefuseChain ? QStringLiteral("too many commands in a row without you")
                                                                 : QStringLiteral("the terminal is busy")), Ink::Note);
        answerTerminalCommand(false, relay::input::handoffRefusalCode(action));
    }

    // Answers the `terminal_command` that is waiting, if one is. ok: `what` is the action; else the code.
    void answerTerminalCommand(bool ok, const QString &what, const QString &error = QString()) {
        if (m_handoffId.isEmpty()) return;
        QJsonObject reply{{"type", "terminal_command_result"}, {"id", m_handoffId}, {"ok", ok},
                          {ok ? "action" : "code", what}};
        if (!error.isEmpty()) reply.insert(QStringLiteral("error"), error);
        m_handoffId.clear();
        send(reply);
    }

    // The handed-over command exited: the agent hears how, unless the user stopped it themselves.
    void finishHandoff(int status) {
        m_handoffArmed = false;
        const QString command = m_handoffCommand, output = m_handoffOutput;
        m_handoffCommand.clear(); m_handoffOutput.clear();
        if (status == 130 || !m_configured) return;   // Ctrl+C: the user ended it on purpose
        QueueEntry entry; entry.agent = true; entry.handoff = true;
        entry.text = relay::input::handoffReport(command, status, output);
        // Defer until Readline has drawn the prompt, as the fix loop does.
        QTimer::singleShot(200, this, [this, entry]() mutable {
            if (m_entries.isEmpty() && !m_activeValid && !m_agentBusy) startAgentEntry(entry, false);
            else { entry.id = ++m_entrySerial; m_entries.prepend(entry); rebuildQueueStrip(); pumpQueue(); }
        });
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
            m_wrap.reset();
            holdShellResize(true);
        }
        // Agent prose is Markdown, rendered as it streams (MarkdownAnsi holds back only what it
        // cannot decide yet). Any other ink ends the Markdown run first, so held text lands before it.
        // Everything then goes through the word wrapper, so a line breaks between words at the
        // pane's width rather than wherever the terminal runs out of columns (src/WordWrap.h).
        if (ink == Ink::Agent) {
            out += wrapped(m_markdown.feed(clean));
            m_atLineStart = clean.endsWith('\n');
            writeTerminal(out);
            return;
        }
        out += wrapped(m_markdown.finish());
        const QString code = QString::fromUtf8(inkCode(ink));
        QString body = code;
        for (const QChar ch : clean) { if (ch == '\n') body += QStringLiteral("\x1b[0m\n") + code; else body += ch; }
        body += QStringLiteral("\x1b[0m");
        out += wrapped(body) + terminalLines(m_wrap.flush());
        m_atLineStart = clean.endsWith('\n');
        writeTerminal(out);
    }

    // While Relay's own output owns the cursor row, a resize (the reasoning panel opening or
    // closing, a split) must not reach the shell: Readline answers SIGWINCH with "\r\x1b[K",
    // which blanked the row of the reply being written, a row at a time (TerminalBackend.h).
    // The hold ends with the inline block, or as soon as a program other than the idle shell
    // could be reading the size.
    void holdShellResize(bool hold) {
        if (hold == m_shellResizeHeld || !m_backend) return;
        m_shellResizeHeld = hold;
        m_backend->holdProgramResize(hold);
    }

    QByteArray wrapped(const QString &rendered) {
        m_wrap.setColumns(m_backend ? m_backend->columns() : 0);
        return terminalLines(m_wrap.feed(rendered));
    }

    // Something written around the wrapper (a hyperlink line): the held word goes first, and the
    // write ends in "\r\n", so the wrapper starts over at column 0 after it.
    QByteArray takeWrapped() {
        QByteArray out = terminalLines(m_wrap.flush());
        m_wrap.reset();
        return out;
    }

    static QByteArray terminalLines(const QString &rendered) {
        QByteArray bytes = rendered.toUtf8();
        bytes.replace('\n', "\r\n");
        return bytes;
    }

    void ensureLineStart() { if (m_inlineOpen && !m_atLineStart) printInline(QStringLiteral("\n"), Ink::Note); }

    void closeInline() {
        if (!m_inlineOpen) return;
        if (m_markdown.holding()) writeTerminal(wrapped(m_markdown.finish()));
        writeTerminal(takeWrapped());
        if (!m_atLineStart) writeTerminal("\r\n");
        m_inlineOpen = false; m_atLineStart = true;
        holdShellResize(false);   // the cursor is on a fresh row: the shell may redraw there
        // Ctrl+X Ctrl+P is bound to a no-op shell function; Readline redraws the prompt after it.
        if (m_backend && shellIdleAtPrompt()) m_backend->redrawPrompt();
    }

    // The pane's text from before the last quit (src/WindowState.h), printed once, at the
    // restarted shell's first prompt. The idle prompt line is erased the way inline agent output
    // erases it and a fresh one is asked for below, so the restored lines land above the prompt in
    // the order they were written. They print plain — no saved colours, so they take the live
    // theme — between two muted rules, because the shell under them is new and nothing was re-run.
    void replayRestoredScrollback() {
        if (m_restoredScrollback.isEmpty() || m_scrollbackReplayed || !m_backend) return;
        if (m_inlineOpen || !shellIdleAtPrompt()) return;   // busy: the next prompt tries again
        m_scrollbackReplayed = true;
        const QStringList lines = m_restoredScrollback;
        m_restoredScrollback.clear();
        QByteArray out = "\r\x1b[2K";
        out += inkCode(Ink::Note) + scrollbackOpenMark().toUtf8() + "\x1b[0m\r\n";
        // Saved output is replayed as text: any escape sequence left in the file is stripped, so
        // a hand-edited (or truncated) file cannot drive the terminal.
        for (const QString &line : lines) out += sanitize(line).toUtf8() + "\r\n";
        out += inkCode(Ink::Note) + scrollbackCloseMark().toUtf8() + "\x1b[0m\r\n";
        writeTerminal(out);
        // Not redrawPrompt(): Readline still believes its prompt is where it drew it, and the
        // restored block has just scrolled the screen out from under it, so the repaint is a no-op
        // and the pane is left with no prompt at all (the same trap clearTerminal() documents).
        // An empty line is the shell's own way of printing a fresh prompt where the cursor now is.
        sendShellInput(QStringLiteral("\n"));
        status(QStringLiteral("Restored %1 line(s) of scrollback from before the restart.").arg(lines.size()));
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
        const int tokens = settings.value(QStringLiteral("provider/max_tokens"), 32768).toInt();
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
        entry.noHandoff = m_remoteSubmit;
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
        if (m_entries.isEmpty() && !m_activeValid && !m_agentBusy) {
            startAgentEntry(entry, false);
            return;
        }
        enqueue(entry);
    }

    // A prompt from a paired device. It never touches the composer: the person at the desktop may
    // be typing, and their draft is theirs. `route` asks the worker's router to decide, exactly as
    // the composer does; without it the text can only reach the agent, which is what keeps a
    // view-or-agent device away from the shell.
    void submitRemote(const QString &text, bool route, const QString &origin) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) return;
        status(QStringLiteral("Prompt from %1").arg(origin.isEmpty() ? QStringLiteral("a phone")
                                                                     : origin));
        if (!route || !m_workerReady) {
            m_remoteSubmit = true; submitAgent(trimmed, false, origin); m_remoteSubmit = false;
            return;
        }
        const QString id = QStringLiteral("remote-") + QString::number(++m_requestId);
        m_remotePrompts.insert(id, {trimmed, origin});
        send({{"type", "route"}, {"id", id}, {"text", trimmed}, {"mode", QStringLiteral("auto")},
              {"known_commands", m_knownCommands}, {"path", m_shellPath}, {"cwd", m_cwd}});
    }

    // The router's verdict for a remote prompt. Returns true when the event was one of ours, so
    // the composer's own preview and submit state never see it.
    bool takeRemoteRoute(const QString &id, const QJsonObject &event) {
        const auto pending = m_remotePrompts.find(id);
        if (pending == m_remotePrompts.end()) return false;
        const RemotePrompt prompt = *pending;
        m_remotePrompts.erase(pending);
        if (event.value(QStringLiteral("route")).toString() == QStringLiteral("shell")) {
            submitTerminal(prompt.text, false);
        } else {
            m_remoteSubmit = true; submitAgent(prompt.text, false, prompt.origin); m_remoteSubmit = false;
        }
        return true;
    }

    void submitTerminal(const QString &text, bool watch, bool natural = false, bool handoff = false) {
        if (m_entries.isEmpty() && !m_activeValid && shellIdleForQueue()) {
            m_handoffNext = handoff;
            if (!runInTerminal(text, watch, 0, natural)) m_handoffNext = false;
            return;
        }
        m_editor->remember(text);
        if (m_editor->toPlainText() == m_submittedDraft) m_editor->clear();
        QueueEntry entry; entry.agent = false; entry.text = text; entry.watch = watch; entry.natural = natural;
        entry.handoff = handoff;
        enqueue(entry);
    }

    bool shellIdleForQueue() const { return m_backend && m_shellReady && !m_loading && !m_native && !processBusy(); }

    void enqueue(QueueEntry entry) {
        // A queued item is edited where it stands now (selectQueueEntry / saveQueueEdit), so nothing
        // is ever pulled out and resubmitted at the head: everything queued here joins the back.
        entry.id = ++m_entrySerial;
        m_entries.append(entry);
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
        prompt.handoff = entry.agent && entry.handoff;
        if (!entry.fix) { m_subagents.clearFinished(); m_jobs.clearFinished(); }   // finished rows linger until a new user turn
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
        // run_in_terminal (protocol 22): offered unless the setting says off. Never to a fix turn,
        // which has its own relay-run block, and never to a prompt from a paired device, which
        // must not reach the shell through the agent either.
        const QString ceiling = relay::input::handoffCeiling(
            QSettings().value(QStringLiteral("agent/terminal_handoff")).toString());
        if (!ceiling.isEmpty() && !entry.fix && !entry.noHandoff)
            context.insert(QStringLiteral("terminal_handoff"), ceiling);
        request.insert(QStringLiteral("context"), context);
        const QString requestId = sendPrompt(request, prompt);
        if (fromQueue) { m_active = entry; m_activeValid = true; m_activeRequest = requestId; }
    }

    // Start the head of the queue when its resource is free and nothing from the queue is running.
    void pumpQueue() {
        if (queueBlocked() || m_activeValid || m_entries.isEmpty()) return;
        const QueueEntry head = m_entries.first();
        const quint64 selected = selectedEntryId();
        if (head.agent) {
            if (m_agentBusy || !m_configured) return;
            m_entries.removeFirst();
            startAgentEntry(head, true);
        } else {
            if (!shellIdleForQueue()) return;
            m_entries.removeFirst();
            m_active = head; m_activeValid = true; m_activeLoaded = false;
            m_handoffNext = head.handoff;
            if (!runInTerminal(head.text, head.watch, 0, head.natural)) { m_handoffNext = false; m_entries.prepend(head); m_activeValid = false; return; }
        }
        keepSelectionOn(selected);   // every index below the head just moved up one
        rebuildQueueStrip(); changed();
    }

    // ----- editing a queued item in the prompt box ----------------------------------------------
    // Selecting a queued item puts its text in the prompt box, where it is edited like anything
    // else; the highlighted row keeps the stored text until the edit is saved, so the two only
    // differ while an edit is in flight. Owner, 2026-09-18: "selecting items should put the command
    // in the prompt and make it editable".

    // The top item highlighted holds the queue: the item being edited must not run out from under
    // the edit (owner: "if the top (first-queued) item is highlighted, the queue is paused and wont
    // run"). Derived rather than stored, so leaving the selection resumes the queue by itself and
    // there is no second piece of state to keep in step. It also covers the item drifting to the
    // front while it is being edited: the moment it becomes the head, the hold applies.
    bool queueHeldBySelection() const { return m_selected == 0 && !m_entries.isEmpty(); }
    bool queueBlocked() const { return m_entriesPaused || queueHeldBySelection(); }

    // Show a queued item in the prompt box. The text is the row's, not the user's, so it is not
    // remembered in prompt history and it must not be treated as a draft.
    void selectQueueEntry(int index) {
        if (index < 0 || index >= m_entries.size()) return;
        m_selected = index;
        m_editor->setPlainText(m_entries[index].written() ? QString() : m_entries[index].text);
        m_editor->moveCursor(QTextCursor::End);
        rebuildQueueStrip(); changed();
        pumpQueue();   // the selection may have just left the head, which releases the queue
    }

    // Write what is in the prompt box back into the selected item. An emptied box is "no change"
    // rather than a blank queued item: removing one is Shift+Delete, deliberately.
    bool saveQueueEdit() {
        if (m_selected < 0 || m_selected >= m_entries.size()) return false;
        const QString text = m_editor->toPlainText();
        if (text.trimmed().isEmpty() || m_entries[m_selected].written() || text == m_entries[m_selected].text) return false;
        m_entries[m_selected].text = text;
        return true;
    }

    // Step out of the queue. The prompt box goes back to empty, because what was in it belonged to
    // the queue and not to the user.
    void leaveQueueSelection() {
        m_selected = -1;
        m_editor->clear();
        rebuildQueueStrip(); changed();
        pumpQueue();   // nothing is held any more
    }

    quint64 selectedEntryId() const {
        return (m_selected >= 0 && m_selected < m_entries.size()) ? m_entries[m_selected].id : 0;
    }
    // The queue changed under a selection (an item started running, one was removed, rows were
    // dragged): keep the selection on the same item by id, and if that item has gone, let the
    // selection go with it and hand the prompt box back empty — the text in it was the item's.
    void keepSelectionOn(quint64 id) {
        if (m_selected < 0) return;
        m_selected = -1;
        for (int i = 0; i < m_entries.size(); ++i)
            if (m_entries[i].id == id) { m_selected = i; break; }
        if (m_selected < 0) m_editor->clear();
    }

    void queueEditHint() {
        hint(QStringLiteral("queue.edit"),
             QStringLiteral("Editing a queued item · ↑↓ move between items · Enter saves · Esc cancels"));
    }

    void pauseQueue(const QString &reason) {
        if (m_entries.isEmpty()) return;
        m_entriesPaused = true; m_pauseReason = reason;
        rebuildQueueStrip(); changed();
    }

    void removeEntry(quint64 id) {
        const quint64 selected = selectedEntryId();
        for (int i = 0; i < m_entries.size(); ++i)
            if (m_entries[i].id == id) { m_entries.removeAt(i); break; }
        keepSelectionOn(selected);
        if (m_entries.isEmpty()) { m_entriesPaused = false; m_pauseReason.clear(); }
        rebuildQueueStrip(); changed();
        pumpQueue();   // removing the highlighted head releases the queue
    }

    void syncEntriesFromList() {
        if (!m_queueList) return;
        saveQueueEdit();   // m_entries is still in the old order, so the index is still the right one
        const quint64 selected = selectedEntryId();
        QList<QueueEntry> ordered;
        for (int row = 0; row < m_queueList->count(); ++row) {
            const quint64 id = m_queueList->item(row)->data(Qt::UserRole).toULongLong();
            for (const auto &entry : std::as_const(m_entries)) if (entry.id == id) { ordered.append(entry); break; }
        }
        if (ordered.size() == m_entries.size()) m_entries = ordered;
        keepSelectionOn(selected);   // a dragged row keeps the highlight, at its new place
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
        // Arrowing through the queue, with the selected item editable in the prompt box. The rules
        // (including when Up and Down belong to a multi-line item's text instead) are in
        // relay::queuenav so they can be tested without a widget.
        {
            relay::queuenav::State nav;
            nav.selected = m_selected;
            nav.count = int(m_entries.size());
            nav.promptEmpty = m_editor->toPlainText().isEmpty();
            nav.cursorLine = m_editor->textCursor().blockNumber();
            nav.lineCount = m_editor->document()->blockCount();
            using Action = relay::queuenav::Action;
            switch (relay::queuenav::decide(nav, k, mods)) {
            case Action::Enter:
                // In at the item nearest the prompt box, which is the one queued last.
                selectQueueEntry(int(m_entries.size()) - 1);
                queueEditHint();
                return true;
            case Action::Up:      saveQueueEdit(); selectQueueEntry(m_selected - 1); return true;
            case Action::Down:    saveQueueEdit(); selectQueueEntry(m_selected + 1); return true;
            case Action::LeaveToHistory:
                // Straight on past the front of the queue into earlier prompts: the editor's own
                // history takes the key, so the box must be empty before it does.
                saveQueueEdit();
                leaveQueueSelection();
                return false;
            case Action::LeaveToPrompt:
                saveQueueEdit();
                leaveQueueSelection();
                return true;
            case Action::Save: {
                const bool held = queueHeldBySelection();
                const bool edited = saveQueueEdit();
                leaveQueueSelection();
                if (edited) toast(held ? QStringLiteral("Saved · it runs next")
                                       : QStringLiteral("Saved · it runs in its place in the queue"));
                else toast(QStringLiteral("Unchanged · the queue runs on"));
                return true;
            }
            case Action::Cancel:
                leaveQueueSelection();          // the edit is dropped: nothing was written back
                return true;
            case Action::Remove: {
                const quint64 id = m_entries[m_selected].id;
                const int at = m_selected;
                leaveQueueSelection();
                removeEntry(id);
                // Stay in the queue on the item that moved up into the gap, so several can go in a row.
                if (!m_entries.isEmpty()) selectQueueEntry(std::min(at, int(m_entries.size()) - 1));
                return true;
            }
            case Action::MoveUp:
            case Action::MoveDown: {
                saveQueueEdit();
                const int to = m_selected + (k == Qt::Key_Up ? -1 : 1);
                m_entries.move(m_selected, to);
                selectQueueEntry(to);
                return true;
            }
            case Action::None:
                break;
            }
        }
        // --- subagents UI: Down on the last line (history at the draft) enters the running-agents list.
        // Up stays queue/history; the @ picker and a queue selection above already took their keys.
        if (mods == Qt::NoModifier && k == Qt::Key_Down && m_agentsPanel && m_agentsPanel->isVisible()
            && m_editor->textCursor().blockNumber() == m_editor->document()->blockCount() - 1 && m_editor->atDraft()) {
            m_agentsPanel->enter();
            return true;
        }
        // The jobs list is next: entered straight from the prompt when no agents are listed above it.
        if (mods == Qt::NoModifier && k == Qt::Key_Down && m_jobsPanel && m_jobsPanel->isVisible()
            && m_editor->textCursor().blockNumber() == m_editor->document()->blockCount() - 1 && m_editor->atDraft()) {
            m_jobsPanel->enter();
            return true;
        }
        // --- end subagents UI ---
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
        // A handed-over command the user wiped out is gone: what they type next is their own.
        if (m_handoffPrefill && m_pendingSubmit.isEmpty() && m_editor->toPlainText().trimmed().isEmpty())
            m_handoffPrefill = false;
        if (m_editor->toPlainText().trimmed().isEmpty()) refreshDestinationColor();
        if (!m_editor->toPlainText().isEmpty()) m_idleTip.stop();
        if (!m_editor->toPlainText().isEmpty()) clearAiGhost();
        updateSlashPopup();
        // Typing while an item is selected edits that item (it is saved on Enter or on moving to
        // another item), so the selection deliberately survives here.
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

    // The listing itself lives in relay::FileIndex, which runs git without blocking the window
    // (it used to freeze for seconds on the first `@` in a large repository). Results arrive
    // later, so the popup is rebuilt from here whenever they do.
    void refreshFileIndex() {
        if (!m_fileIndex.updated)
            m_fileIndex.updated = [this] { if (m_atList && m_atList->isVisible()) updateAtPopup(); };
        m_fileIndex.refresh(m_cwd);
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
        for (const QString &path : m_fileIndex.files()) {
            const QString rel = QDir(m_cwd).relativeFilePath(path);
            const QString name = QFileInfo(path).fileName();
            int score = 0;
            if (query.isEmpty()) {
                score = 1;
                if (m_recentFiles.contains(path)) score += 100000 - m_recentFiles.indexOf(path);
                if (m_fileIndex.changedFiles().contains(path)) score += 50000;
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
        // Nothing yet and nothing at all look the same in an empty popup, so while the first index
        // of a directory is still being built the picker says so instead of disappearing.
        const bool indexing = ranked.isEmpty() && m_fileIndex.isLoading();
        if (ranked.isEmpty() && !indexing) { hideAtPopup(); return; }
        if (!m_atList) {
            m_atList = new QListWidget(this);
            m_atList->setObjectName(QStringLiteral("atPicker"));
            m_atList->setFocusPolicy(Qt::NoFocus);
            m_atList->setUniformItemSizes(true);
            connect(m_atList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { m_atList->setCurrentItem(item); acceptAtSelection(); });
        }
        m_atList->clear();
        if (indexing) {
            auto *item = new QListWidgetItem(QStringLiteral("Indexing files…"), m_atList);
            item->setFlags(Qt::NoItemFlags);   // greyed out, and Enter cannot pick it
        }
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
        // The engine owns the pty master, and on Linux both ends share one line discipline, so
        // one ioctl on a descriptor it already holds answers this. Opening /proc/<pid>/fd/0 is
        // the fallback for an engine that cannot say (TerminalBackend::LineDiscipline).
        if (const auto flags = m_backend->termiosFlags(); flags.valid) {
            if (!flags.canonical) return TerminalMode::Raw;
            return flags.echo ? TerminalMode::Echoing : TerminalMode::Secret;
        }
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

struct PendingPrompt { QString text, why, program; bool fix = false, handoff = false; QString shellText; };

    // Another turn will start without user action: something is queued and the queue is not paused.
    // Another turn will start without user action: queued items, or a turn that is interrupting this one.
    bool moreTurnsPending() const { return (!m_entries.isEmpty() && !queueBlocked()) || m_interruptPending; }

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
        m_queueWanted = visible;
        if (visible) showBubble(m_queueStrip); else hideBubble(m_queueStrip);
        if (!visible) return;
        auto *header = new QHBoxLayout;
        const bool held = queueHeldBySelection();
        auto *title = new QLabel(queueBlocked() ? QStringLiteral("QUEUE · PAUSED") : QStringLiteral("QUEUE"));
        title->setObjectName(QStringLiteral("queueTitle"));
        QString why = held ? QStringLiteral("The next item is highlighted and being edited in the prompt box, so the"
                                            " queue is holding. Enter saves it, Esc drops the edit; either way the"
                                            " queue runs on.")
                           : QString();
        if (!m_pauseReason.isEmpty()) why = why.isEmpty() ? m_pauseReason : why + QStringLiteral("\nAlso paused: ") + m_pauseReason;
        title->setToolTip(why);
        header->addWidget(title, 1);
        auto *hint = new QLabel(m_selected >= 0
                                    ? QStringLiteral("↑↓ item · Ctrl+↑↓ move · Enter save · Esc cancel · Shift+Del remove")
                                    : QStringLiteral("↑ edit an item · Ctrl+↑↓ move · Shift+Del remove"));
        hint->setObjectName(QStringLiteral("queueHint"));
        header->addWidget(hint);
        if (m_entriesPaused && !held) {
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
        if (m_activeValid) running = (m_active.agent ? QStringLiteral("✦ ") : QStringLiteral("$ ")) + m_active.label();
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
            auto *item = new QListWidgetItem(entry.label(), m_queueList);
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
        if (!m_queueStrip || !m_terminalHost) return;
        // Too short for a legible strip: nothing, rather than a clipped one (owner, 2026-09-18).
        // The queue outranks the reasoning panel when only one of them fits, because it is the one
        // holding work — what is queued, and the Resume and Clear that act on it. Queueing already
        // says so on screen ("Queued · the command runs when the terminal is free"), so a strip
        // that cannot be drawn is not the only word the person gets.
        if (!m_queueWanted || !roomForBubble(0)) { hideBubble(m_queueStrip); return; }
        showBubble(m_queueStrip);
        setBubbleHeight(m_queueStrip, std::max(bubbleRow(), std::min(m_queueStrip->sizeHint().height(), bubbleSpan() / 2)));
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

    // The shell is sitting in Readline waiting for a line: the tty is raw AND the shell's own
    // process group is the terminal's foreground group (so no `vim` or `less` has it). This runs
    // 12 times a second in every pane, so it asks the engine first: one tcgetattr and one
    // TIOCGPGRP on the pty master it already holds, instead of an open/ioctl/close of
    // /proc/<pid>/fd/0 plus a parse of /proc/<pid>/stat.
    bool readlineReady() const {
        if (!m_backend) return false;
        const int pid = shellPid();
        if (pid <= 0) return false;
        if (const auto flags = m_backend->termiosFlags(); flags.valid)
            return !flags.canonical && foregroundPid() == pid; // forkpty made the shell its own group leader
        const auto name = QStringLiteral("/proc/%1/fd/0").arg(pid).toLocal8Bit();
        const int fd = ::open(name.constData(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return false;
        termios state{};
        const bool raw = ::tcgetattr(fd, &state) == 0 && !(state.c_lflag & ICANON);
        ::close(fd);
        // tcgetpgrp() fails with ENOTTY on the SLAVE opened through /proc unless the terminal is
        // the caller's controlling tty, which it never is for Relay. /proc/<pid>/stat field 8
        // (tpgid) reports the same foreground process group without that restriction. (The
        // engine's TIOCGPGRP above is on the MASTER, which carries no such rule.)
        return raw && foregroundGroup(pid) == pid;
    }

    // Foreground process group of the pane's terminal, read from the shell's /proc entry: the
    // fallback for an engine that cannot answer foregroundPid() or termiosFlags().
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
            // Only when the keyboard is already here: the command that just finished may belong
            // to a pane the user left long ago.
            if (holdsFocus()) m_editor->setFocus();
            m_refocus = false;
        }
    }

    // The shell poll costs about a dozen system calls, and every pane runs one. A pane nobody can
    // see (a background tab) with nothing in flight has no one waiting on its answer, so it asks
    // five times less often; it is back to the fast rate the moment it is shown or given work.
    static constexpr int kPollFastMs = 80, kPollQuietMs = 400;
    void tunePoll() {
        const bool quiet = !isVisible() && !m_loading && !m_activeValid && m_entries.isEmpty();
        const int wanted = quiet ? kPollQuietMs : kPollFastMs;
        if (m_poll.interval() != wanted) m_poll.setInterval(wanted);
    }

    void pollShell() {
        tunePoll();
        if (m_shellResizeHeld && !shellIdleAtPrompt()) holdShellResize(false);
        // PROMPT_COMMAND runs before Readline puts the tty into noncanonical mode.
        // Recheck on every tick, even when the state file has not changed.
        refreshShellReady();
        if (!m_entries.isEmpty() && !m_activeValid) pumpQueue();
        // This runs 12 times a second in every pane, and the file changes a few times per
        // command. shell/event.py replaces it atomically, so a new event is a new inode: one
        // stat() says whether there is anything to read, in place of an open, a read and a JSON
        // parse. A small saving; tunePoll() above is the larger one.
        const QString statePath = m_runtime.filePath(QStringLiteral("state.json"));
        struct stat info;
        if (::stat(QFile::encodeName(statePath).constData(), &info) != 0) return;
        if (m_stateSeen && info.st_ino == m_stateInode && info.st_size == m_stateSize
            && info.st_mtim.tv_sec == m_stateMtime.tv_sec && info.st_mtim.tv_nsec == m_stateMtime.tv_nsec) return;
        QFile file(statePath);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return;
        m_stateSeen = true; m_stateInode = info.st_ino; m_stateSize = info.st_size; m_stateMtime = info.st_mtim;
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
            // A restored pane brings its old text back above this first prompt.
            replayRestoredScrollback();
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
            if (m_handoffArmed) finishHandoff(status);
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
            // run_in_terminal (protocol 22): this is the command the agent handed over.
            m_handoffArmed = m_handoffNext; m_handoffNext = false;
            if (m_handoffArmed) m_handoffCommand = m_pendingCommand;
            answerTerminalCommand(true, QStringLiteral("started"));
            beginCommandCapture(m_pendingCommand, m_handoffArmed);   // conversation index (protocol 14)
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
        auto *tokens = new QSpinBox; tokens->setRange(256, 32768); tokens->setValue(settings.value("provider/max_tokens", 32768).toInt());
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
    // Terminal scrollback across a restart: the file this pane's text is saved in, the lines a
    // restore handed it, and whether they have been replayed (once per pane, at the first prompt).
    QString m_scrollbackId;
    QStringList m_restoredScrollback;
    bool m_scrollbackReplayed = false;
    // state.json as pollShell() last read it, so an unchanged file is not read again.
    bool m_stateSeen = false; ino_t m_stateInode = 0; off_t m_stateSize = 0; timespec m_stateMtime{};
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
    // run_in_terminal (protocol 22). m_handoffId: the terminal_command still to be answered.
    // Prefill: a reporting command sits in the prompt box. Next: the command being staged reports.
    // Armed: the running command reports when it exits. Chain: runs since the user last typed.
    QString m_handoffId, m_handoffCommand, m_handoffOutput;
    bool m_handoffPrefill = false, m_handoffNext = false, m_handoffArmed = false;
    bool m_captureForAgent = false, m_remoteSubmit = false;
    int m_handoffChain = 0;
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
    // Its default palette on purpose: the terminal's own foreground and ANSI indices, which the
    // engine resolves from the active theme at paint time, so a theme switch recolours the whole
    // transcript including the scrollback (src/MarkdownAnsi.h). Filling it with absolute RGB from
    // the live tokens, as this did until 2026-09-18, burnt one theme's colours into the history.
    relay::MarkdownAnsi m_markdown;
    relay::WordWrap m_wrap;   // between m_markdown (and the other inks) and the terminal
    bool m_shellResizeHeld = false;   // holdShellResize()
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
    // m_thinkingHeld: the panel reopened by the keyboard between turns (toggleThinkingPanel).
    bool m_thinkingShown = false, m_thinkingDismissed = false, m_thinkingExpanded = false, m_thinkingHeld = false;
    bool m_queueWanted = false;   // the queue has something to show; room decides whether it does
    int m_toolLines = 0;              // lines of the running tool's collapsed output
    bool m_toolPartialLine = false;   // its last chunk had no trailing newline
    QFrame *m_thinking = nullptr;
    QLabel *m_thinkingHeader = nullptr;
    QPlainTextEdit *m_thinkingView = nullptr;
    QWidget *m_thinkingHeaderRow = nullptr;
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
    bool m_activeValid = false, m_activeLoaded = false, m_entriesPaused = false;
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
    relay::FileIndex m_fileIndex;   // the `@` picker's listing; keeps its own cwd and freshness
    QStringList m_recentFiles, m_shellHistory;
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
    // Prompts from paired devices waiting on the router, by request id.
    struct RemotePrompt { QString text; QString origin; };
    QHash<QString, RemotePrompt> m_remotePrompts;
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
    // A model switch waiting to land (issue 3ES1): the bar measures against its window, not the one
    // the request in flight runs on. Set from the `context` event's `next`; zero when none waits.
    qint64 m_ctxNextUsed = 0, m_ctxNextWindow = 0, m_ctxNextLimit = 0;
    double m_ctxNextPercent = 0;
    QString m_ctxNextModel, m_ctxInFlightModel;
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
    relay::JobsModel m_jobs;
    relay::JobsPanel *m_jobsPanel = nullptr;
    QList<QPointer<relay::SubagentTranscriptView>> m_subagentViews;
    QPointer<relay::SubagentTranscriptView> m_subagentOverlay;
};


