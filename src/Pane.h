// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One terminal pane -- the single largest thing in the app: a shell behind relay::TerminalBackend,
// its Bash bridge, the composer, the queue, and the pane's own agent worker and conversation.
// Pane never names RelayWindow or WindowManager; it calls up through std::function callbacks the
// window sets, which is what lets it sit below them here. QueueRowDelegate draws the queue rows.

#include "AppPaths.h"
#include "CopyOnSelect.h"
#include "Keymap.h"
#include "Isolation.h"

#include "RichEditor.h"
#include "PromptHistory.h"
#include "Theme.h"
#include "BoardPane.h"
#include "Projects.h"      // which project this pane's tab is attached to, and why (#JN7X)
#include "ProjectInit.h"        // when "Initialize a project … here?" is asked, and what it shows
#include "ProjectInitBlock.h"   // …and the inline block that asks it, under the terminal
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
#include "QueueSubmit.h"   // when a submitted agent prompt starts its turn at once (#N8VK)
#include "PaneTitles.h"
#include "PaneUsage.h"    // the pane's own CPU / memory share, for the header chip and the tab
#include "CallLines.h"      // one line per tool call, and what its fold holds (#TK9C)
#include "DiffView.h"       // the diff pane a big edit opens
#include "TurnTranscript.h"
#include "ModelSettings.h"
#include "SkillsDialog.h"
#include "SubagentTranscript.h"
#include "SubagentsPanel.h"
#include "JobsPanel.h"
#include "RequestLedger.h"
#include "RequestsPanel.h"
#include "Conversations.h"
#include "SessionInfo.h"
#include "Logging.h"
#include "GuestBridge.h"   // the Claude IDE bridge: the env a claude pane gets, and its diff answers
#include "TerminalBackends.h"
#include "TerminalBackend.h"
#include "EngineBackend.h"     // applyTerminalSettings(): Options › Terminal reaches the engine view in place
#include "WindowState.h"
#include "RuntimeDirs.h"
#include "RemoteShare.h"
#include "PaneState.h"
#include "PaneStatus.h"
#include "RemoteSession.h"
#include "RemoteFiles.h"     // the host's files: the link probe's cache and `ssh://host/path` (#S5SH)
#include "backend/VTermBackend.h"
#include "Voice.h"
#include "Images.h"
#include "Aliases.h"
#include "MarkdownAnsi.h"
#include "WordWrap.h"
#include "TranscriptGaps.h" // a blank line between blocks of different kinds (#5AWD)
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
#include <QSysInfo>
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

// One row of the queue list, in delivery order: a steer waiting for the running turn's next tool
// call ("↪ next tool call ✦", in the agent's colour), then queued agent prompts (✦) and terminal
// commands ($). Every row has a remove × at the right edge; a steer that is being withdrawn is
// greyed with "withdrawing…" and has none.
class QueueRowDelegate final : public QStyledItemDelegate {
public:
    // Item data roles the pane fills in rebuildQueueStrip().
    static constexpr int EntryIdRole = Qt::UserRole;        // a queued item's id (0 for a steer)
    static constexpr int AgentRole = Qt::UserRole + 1;      // an agent prompt rather than a command
    static constexpr int KindRole = Qt::UserRole + 2;       // "steer", "agent" or "command"
    static constexpr int RowIdRole = Qt::UserRole + 3;      // "steer:<request id>" or "entry:<id>"
    static constexpr int PendingRole = Qt::UserRole + 4;    // a steer being withdrawn
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save();
        const QRect r = option.rect;
        if (option.state & QStyle::State_Selected) painter->fillRect(r, relay::theme::SurfaceRaised.lighter(135));
        const bool steer = index.data(KindRole).toString() == QStringLiteral("steer");
        const bool pending = index.data(PendingRole).toBool();
        const bool agent = index.data(AgentRole).toBool();
        painter->setFont(option.font);
        int left = r.left() + 4;
        if (steer) {
            const QString lead = QStringLiteral("↪ next tool call");
            painter->setPen(pending ? relay::theme::TextMuted : relay::theme::Agent);
            painter->drawText(QRect(left, r.top(), r.width(), r.height()), Qt::AlignVCenter | Qt::AlignLeft, lead);
            left += option.fontMetrics.horizontalAdvance(lead) + 8;
        }
        // The glyph says where the row is going, so it wears the destination pair — `✦` agent
        // violet, `$` shell cyan. It used to be violet/`Accent` for the agent and *amber* for the
        // shell, which is the same mistake `be81edb` took out of the prefix chips (docs/
        // ARCHITECTURE.md § 14, "The prefix chips were wrong"): amber means "something is waiting
        // on you", and a queued shell command is not waiting on anyone.
        painter->setPen(pending ? relay::theme::TextMuted
                                : (steer || agent) ? relay::theme::Agent : relay::theme::Shell);
        painter->drawText(QRect(left, r.top(), 18, r.height()), Qt::AlignCenter, agent ? QStringLiteral("✦") : QStringLiteral("$"));
        left += 22;
        const QString suffix = pending ? QStringLiteral("  withdrawing…") : QString();
        const int room = std::max(20, r.right() - 28 - left - option.fontMetrics.horizontalAdvance(suffix));
        painter->setPen(pending ? relay::theme::TextMuted : (steer ? relay::theme::Agent : relay::theme::Text));
        const QString text = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString().simplified(), Qt::ElideRight, room) + suffix;
        painter->drawText(QRect(left, r.top(), r.right() - 26 - left, r.height()), Qt::AlignVCenter | Qt::AlignLeft, text);
        if (!pending) {
            painter->setPen(relay::theme::TextMuted);
            painter->drawText(QRect(r.right() - 22, r.top(), 18, r.height()), Qt::AlignCenter, QStringLiteral("×"));
        }
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        return {QStyledItemDelegate::sizeHint(option, index).width(), option.fontMetrics.height() + 8};
    }
};

// What a line typed into the prompt box means for the question card on screen (#MQ9C,
// protocol 27.4). Free and pure, so the whole decision sits in one place with nothing of the pane
// around it: Pane::answerQuestion reads the options off the card and then only acts on this.
// Keys are not lines and are not read here: Esc skips the current question (owner, 2026-09-19 —
// `Pane::skipQuestion`, which is what `0` and `/skip` below mean) and Ctrl+Shift+Enter sends the
// box to the shell instead, which is why a card can never take the terminal away.
namespace relay::ask {

struct Reading {
    enum Kind {
        Words,          // their own answer, in their own words (always so for an open question)
        Chosen,         // options, picked by number
        Skip,           // "0" or /skip: this one is left unanswered
        NoSuchOption,   // a number the card has no option for: nothing is sent, the card stays up
    };
    Kind kind = Words;
    QStringList labels;   // Chosen: the labels picked, in the order typed. Words: the line itself.
    QString missing;      // NoSuchOption: the number they typed, as they typed it.
};

// `labels` is the question's options in order, and is empty for an open question.
inline Reading read(const QString &text, const QStringList &labels, bool multiple) {
    const QString trimmed = text.trimmed();
    if (trimmed.compare(QStringLiteral("/skip"), Qt::CaseInsensitive) == 0) return {Reading::Skip, {}, {}};
    if (labels.isEmpty()) return {Reading::Words, {trimmed}, {}};
    static const QRegularExpression numbers(QStringLiteral("^\\s*\\d+(?:\\s*[,\\s]\\s*\\d+)*\\s*$"));
    if (!numbers.match(trimmed).hasMatch()) return {Reading::Words, {trimmed}, {}};
    Reading reading{Reading::Chosen, {}, {}};
    const auto parts = trimmed.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        bool ok = false;
        const qlonglong index = part.toLongLong(&ok);   // only digits got here; !ok means too long
        if (ok && index == 0) return {Reading::Skip, {}, {}};       // skip wins: "0" is never a label
        // A number with no option behind it is a misread list, not an answer. It used to be passed
        // on as the prose answer "4", which reads to the model as a considered reply.
        if (!ok || index > labels.size()) return {Reading::NoSuchOption, {}, part};
        const QString &label = labels.at(int(index) - 1);
        if (!reading.labels.contains(label)) reading.labels.append(label);
        if (!multiple) break;                                       // one answer asked for, one taken
    }
    return reading;
}

}  // namespace relay::ask

// The "Relaying…" line above the prompt box (cards #4E13, #HQ2B): one verb, the colour saying whose
// work it is — the agent's violet while a turn runs or subagents it started still work, the
// terminal's blue while a program runs, amber when the turn is blocked on your answer (#MQ9C).
// Left-aligned with the prompt text and in the normal weight, the caption Warp and Claude carry
// above their composers (owner, 2026-09-19: "should be at the left and above the prompt box,
// more like how warp . claude does it. and not in bold."): the row belongs to the box under it,
// so it starts where that box's own text starts, and it speaks quietly while the prompt is the
// loud thing. Painted rather than a QLabel for the same reason the header's state word is
// (PaneStateWord, src/PaneChrome.h): the colour follows the state, and the theme can change under
// it. Elided from the middle so a long action ("Relaying reading src/deep/path…") never pushes
// the composer wide, and Ignored like the prompt box itself so a narrow pane clips it instead
// (#G152).
class PaneBusyLine final : public QWidget {
public:
    explicit PaneBusyLine(QWidget *parent) : QWidget(parent) {
        setObjectName(QStringLiteral("paneBusyLine"));
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setMinimumWidth(1);   // a narrow pane clips the line rather than growing for it
        hide();
    }
    // The prompt box this row captions, so its left edge can sit on the prompt *text's* edge
    // rather than the frame's. Read live at paint time rather than once at build: the padding
    // that places the text arrives with the theme's polish (theme::polishWindow), after this
    // widget is constructed, and a theme switch can change it again.
    void setPromptEditor(const QPlainTextEdit *editor) { m_prompt = editor; }
    void setBusy(relay::panestatus::State state, const QString &text, const QString &tip) {
        m_state = state; m_text = text;
        setToolTip(tip);
        setAccessibleName(text);
        setVisible(true);
        update();
    }
    void clearBusy() {
        if (m_text.isEmpty() && isHidden()) return;
        m_text.clear();
        setToolTip(QString());
        setAccessibleName(QString());
        hide();
    }
    QSize sizeHint() const override {
        return {leftInset() + QFontMetrics(font()).horizontalAdvance(m_text) + 2, 18};
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if (m_text.isEmpty()) return;
        // Every field, in Tokens' own order: leaving the last one out default-constructed an
        // invalid QColor, which any rule that reached for `tool` would have painted with.
        const relay::panestatus::Tokens t{relay::theme::Background, relay::theme::Text, relay::theme::TextMuted,
                                          relay::theme::Shell, relay::theme::Agent, relay::theme::Success,
                                          relay::theme::Warning, relay::theme::Error, relay::theme::Action,
                                          relay::theme::Tool};
        QPainter p(this);
        // A word is text: the state's own ink, lifted to at least 4.5:1 on the pane's ground
        // (panestatus::stateText), so the line reads in every shipped theme.
        p.setPen(relay::panestatus::stateText(m_state, relay::theme::Background, t));
        p.drawText(rect().adjusted(leftInset(), 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(font()).elidedText(m_text, Qt::ElideMiddle,
                                                    std::max(1, width() - leftInset() - 2)));
    }
private:
    // Where the prompt text starts inside its editor: the stylesheet's padding moves the viewport
    // in, the document's margin moves the text in from that (measured, not assumed: 4 + 4 px with
    // the shipped theme). Zero with no editor set, so a bare PaneBusyLine still paints.
    int leftInset() const {
        return m_prompt ? m_prompt->viewport()->x() + int(m_prompt->document()->documentMargin()) : 0;
    }
    relay::panestatus::State m_state = relay::panestatus::State::Idle;
    QString m_text;
    const QPlainTextEdit *m_prompt = nullptr;
};

// One terminal pane: a shell behind relay::TerminalBackend (Relay's own engine, engine/), its
// Bash bridge, a composer, and its own agent worker and conversation. Windows arrange panes in tabs and splits; the toolbar acts on the active pane.
class Pane final : public QWidget {
public:
    struct QueueEntry {
        quint64 id = 0; bool agent = false, fix = false, watch = false;
        // A guest-composer entry is neither a Relay-agent prompt nor a shell command. It stays
        // in the pane's one delivery queue until this named guest reports that it is idle.
        QString guest;
        QString text, why; QJsonArray attachments, cards;   // cards: `#K7Q2` referenced in the prompt
        // Wrong-mode hints (2026-09-17): natural marks a terminal submission that reads like an
        // agent request; shellText carries an agent submission that is a runnable shell command.
        bool natural = false; QString shellText;
        // run_in_terminal (protocol 22). On a terminal entry: report its exit to the agent. On an
        // agent entry: it *is* that report, a prompt Relay wrote, shown and queued like a fix.
        bool handoff = false;
        bool noHandoff = false;   // a prompt from a paired device never gets the tool
        // Who asked for it, when that is not the person at this desk: a guest's display name, or
        // empty. Never the guest id — the row is read by people (card #W5N2's owner, 2026-09-18).
        QString author;
        bool written() const { return fix || (agent && handoff); }   // by Relay, not by the user
        QString label() const {
            const QString what = fix ? QStringLiteral("fix request")
                                 : written() ? QStringLiteral("terminal result") : text;
            const QString labelled = guest.isEmpty() ? what : guestDisplayName(guest) + QStringLiteral(" · ") + what;
            return author.isEmpty() ? labelled : author + QStringLiteral(" · ") + labelled;
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
        // The phone's pane follows the desktop's theme (owner, 2026-09-19), so a theme change is
        // one of the things that changes what a paired device must be told. Costs nothing while
        // nobody is listening: changed() gathers nothing until onPaneState is wired up.
        connect(relay::theme::notifier(), &relay::theme::Notifier::themeChanged, this,
                [this] { m_paneState.changed(); });
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
        qApp->removeEventFilter(this);
        // The bridge: this pane is gone, so its registration ends here — and so does any diff it
        // still owes claude an answer to. Rejecting is the only honest answer once nobody can
        // show the change (26.5).
        settleGuestDiff(relay::guestbridge::diffRejected(), QStringLiteral("pane closed"));
        if (m_guest == QStringLiteral("claude") && relay::guestbridge::Bridge::started())
            relay::guestbridge::Bridge::instance().guestLeft(m_token);   // the sidecar may stop (26.9)
        unregisterFromBridge();
        stopGuestTail();   // the codex rollout tail dies with the pane it was reporting to (26.6)
        // Voice: a clip whose transcript never came back would otherwise outlive the pane.
        if (m_voiceCapture) m_voiceCapture->cancel();
        if (!m_voiceClip.isEmpty()) QFile::remove(m_voiceClip);
        for (const QString &clip : std::as_const(m_remoteVoiceClips)) QFile::remove(clip);
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

    // The environment a guest helper Relay itself starts needs to reach *this* pane's spool
    // (26.3). It cannot be inherited: `startTerminal` publishes the channel with `qputenv`, which
    // writes the GUI's own environment, so a second pane's shell overwrites what the first one
    // exported and an inherited `RELAY_GUEST_EVENT`/`RELAY_SESSION_TOKEN` names whichever pane
    // started its shell last. Every event of pane A then landed on pane B's spool carrying pane
    // B's token, which pane B duly accepted — one pane showing another's guest.
    QProcessEnvironment guestHelperEnvironment() const {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("RELAY_RUNTIME_DIR"), m_runtime.path());
        environment.insert(QStringLiteral("RELAY_SESSION_TOKEN"), m_token);
        environment.insert(QStringLiteral("RELAY_GUEST_EVENT"), guestEventsDir());
        environment.insert(QStringLiteral("RELAY_PYTHON"), m_python);
        // `-m relay_core.…` needs the backend importable; appended, never prepended (26.4).
        const QString backend = m_data + QStringLiteral("/backend");
        const QString existing = environment.value(QStringLiteral("PYTHONPATH"));
        if (!existing.split(QLatin1Char(':'), Qt::SkipEmptyParts).contains(backend))
            environment.insert(QStringLiteral("PYTHONPATH"),
                               existing.isEmpty() ? backend : existing + QLatin1Char(':') + backend);
        return environment;
    }

    // Scan asynchronously: an unreadable home/config directory or a vanished guest helper must
    // not block a keystroke in the pane. The helper publishes a normal `slash` event atomically.
    void publishGuestSlashCatalog(const QString &guest) {
        if (guest != QStringLiteral("claude") && guest != QStringLiteral("codex")) return;
        auto *scan = new QProcess(this);
        scan->setWorkingDirectory(m_cwd);
        scan->setProcessEnvironment(guestHelperEnvironment());
        scan->setProgram(m_python);
        scan->setArguments({QStringLiteral("-m"), QStringLiteral("relay_core.guest_slash"),
                            QStringLiteral("--emit"), guest, QStringLiteral("--cwd"), m_cwd});
        connect(scan, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, scan, guest](int code, QProcess::ExitStatus exit) {
                    if (exit != QProcess::NormalExit || code != 0)
                        status(QStringLiteral("Could not read %1 slash commands.").arg(guestDisplayName(guest)));
                    scan->deleteLater();
                });
        connect(scan, &QProcess::errorOccurred, this, [scan](QProcess::ProcessError) { scan->deleteLater(); });
        scan->start();
    }

    // ----- interface used by RelayWindow ----------------------------------------------------
    std::function<void(const QString &)> onStatus;
    std::function<void()> onStateChanged;   // cwd, model list, native mode or busy state changed
    std::function<void()> onShellExited;
    // Open a folder (explorer pane) or a file (preview pane); `line` > 0 scrolls the preview there.
    std::function<void(const QString &path, int line)> onOpenPath;
    std::function<void(const QString &)> onToggleExplorer;   // open the explorer, or close it again
    std::function<void()> onOpenBoard;                 // Switchboard: /switchboard from this pane
    // /light, /dark and /theme: the window decides whether the theme is this tab's or everyone's
    // (themes are per tab by default). Unset = the application-wide switch.
    std::function<bool(const QString &themeId)> onChooseTheme;
    // ----- the project this pane's tab is attached to (card #JN7X, src/Projects.h) ------------
    // The `board` block of every `configure` this pane sends (protocol 19.1). The window answers
    // from the tab: `{"attach": false}` while the tab is attached to nothing, which is the quiet
    // default and means no card tools and no board policy in the prompt.
    std::function<QJsonObject()> onBoardSettings;
    // "Has this pane a board it may talk to?", answered with the project's path or an empty
    // string. A non-empty `reason` (projects::kReason*) makes it an explicit project action and
    // attaches the tab to the pane's candidate project first — but only when that project
    // already has a board; a candidate with none puts one line in `*why` and creates nothing.
    // An empty reason never attaches, so the `#` index, the idle tip and the shortcut hints stay
    // quiet in an unattached pane.
    std::function<QString(const QString &reason, QString *why)> onBoardProject;
    // ----- "Initialize a project and create a Switchboard here?" (protocol 19.12) --------------
    // What the window knows about this pane before the question is raised: its candidate project,
    // whether that project already has a board, and what the registry remembers about it. The
    // rules live in src/ProjectInit.h; the window only answers facts.
    std::function<relay::projectinit::Situation()> onProjectInitSituation;
    // A yes: attach this pane's tab to `project` with `reason` (projects::kReason*), so the
    // conversation carries on with the card tools (set_board, protocol 19.11).
    std::function<void(const QString &project, const QString &reason)> onProjectAttach;
    // What happened to the question, for the window to remember. `what` is one of "asked" (it is
    // on screen now), "yes", "no" — written to the registry for ever — "not-now", which silences
    // trigger (1) for this Relay session only, and "reconsider", which `/init` sends to clear a
    // remembered no.
    std::function<void(const QString &project, const QString &what)> onProjectInitEvent;
    std::function<void(const QString &code)> onJoinShared;   // /join CODE, /connect CODE
    std::function<void()> onUpdateApp;   // /update: install the latest release, restart into it
    std::function<void(const QString &)> onOpenCard;   // Switchboard: one card, from the work chip
    std::function<void(const QString &turnId)> onOpenTurn;   // "✦ N tool calls" link or palette
    // The Sharing pane (#W5N2): who is on this shared pane, who is knocking, what is waiting.
    std::function<void()> onOpenSharing;
    // The window's id for the tab this pane is in, and how many terminals it holds, so the share
    // dialog can offer "Share the whole tab". Unset (or "") offers only this pane.
    std::function<QString(int *panes)> onShareTab;
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
    // ----- pane chrome (src/PaneChrome.h): status glyph (#XM0T) and remote session (#SPBN) -----
    QHBoxLayout *headerLayout() const { return m_headerLayout; }
    QWidget *headerWidget() const { return m_headerWidget; }
    relay::panestatus::Facts statusFacts() const {
        relay::panestatus::Facts facts;
        facts.agentBusy = m_agentBusy;
        facts.liveSubagents = m_subagents.liveCount();
        facts.processBusy = processBusy();
        // A guest's permission question blocks the guest exactly as a program's own prompt
        // blocks the terminal, so it reaches the tab glyph the same way (GT7X, 26.4).
        facts.programAsking = facts.processBusy
                              && (m_waiting || m_secretMode || m_screenPrompt.actionable()
                                  || !m_guestQuestions.isEmpty());
        const bool boxHoldsIt = m_handoffOffered && !m_editor->toPlainText().trimmed().isEmpty();
        facts.handoffWaiting = boxHoldsIt && m_handoffPrefill && !m_agentBusy;
        facts.handoffOffered = boxHoldsIt && !m_handoffPrefill;
        facts.finishSerial = m_finishSerial;
        facts.lastOutcome = m_lastOutcome;
        facts.lastAsked = m_lastAsked;
        facts.questionOpen = m_ask.open();
        return facts;
    }
    // This pane's share of the machine (issue #D03W): CPU and memory summed over the shell's
    // process tree and the pane's agent worker, sampled by the window's status poll so every
    // pane is measured on the same interval. An idle pane is a valid sample of nothing.
    relay::usage::Sample usageSample() const { return m_usageSample; }
    void refreshUsage() {
        // With the meters switched off nothing reads the sample, so nothing walks /proc for it:
        // the setting turns the measuring off as well as the three labels, and the baseline goes
        // with it so switching back on reads a fresh interval rather than averaging the gap.
        if (!relay::usage::metersEnabled()) { clearUsage(); return; }
        // The two roots are named, so the breakdown in the chip's tooltip can say "shell" and
        // "agent worker" where "bash" and "python3" would leave the reader to guess which
        // python3 that is. Everything else under them is named after its comm.
        QList<relay::usage::Root> roots;
        if (const int shell = shellPid(); shell > 0)
            roots << relay::usage::Root{shell, QStringLiteral("shell")};
        if (m_worker.state() == QProcess::Running)
            roots << relay::usage::Root{qint64(m_worker.processId()), QStringLiteral("agent worker")};
        m_usageSample = m_usageMeter.update(roots);
    }
    // Forget the reading and its baseline (the meters were switched off, or the pane's processes
    // changed wholesale).
    void clearUsage() {
        m_usageMeter.reset();
        m_usageSample = {};
    }
    // The foreground program's command line while it is ssh, mosh or telnet; empty otherwise.
    // Read live from the terminal's foreground process group, so it is true exactly while the
    // session runs (and also when it was started with the prompt box hidden), and clears when
    // it exits or is suspended. ssh run inside a local tmux is not visible from here.
    QString remoteCommandLine() const {
        if (!processBusy()) return {};
        const QString line = foregroundCommandLine();
        const QString program = line.isEmpty() ? QString() : QFileInfo(line.section(' ', 0, 0)).fileName();
        return remoteSessionProgram(program) || relay::panestatus::isRemoteProgram(program) ? line : QString();
    }
    // The guest agent (Claude Code / Codex) running in this pane's foreground, or empty.
    // Classified from the command line on every program poll (issue GT7X, protocol 26).
    QString guest() const { return m_guest; }
    // One `bridge` event from this pane's guest channel (26.3, 26.5): what the IDE bridge's
    // sidecar learned from a claude connected to it, handed in by the channel's own reader
    // (handleGuestEvent) — the fold the seam was waiting for.
    // `openDiff` is the one that asks for Relay surfaces of its own (the diff view, and the
    // banner that decides it); `openFile` opens the preview pane. Everything else — a selection,
    // a dirty flag, the diagnostics Relay does not have — was answered by the sidecar, which is
    // the only side that can answer it.
    void guestBridgeEvent(const QJsonObject &data) {
        const QString tool = data.value(QStringLiteral("tool")).toString();
        if (tool == QStringLiteral("openDiff")) { showGuestDiff(data); return; }
        if (tool == QStringLiteral("openFile")) {
            const QString path = data.value(QStringLiteral("filePath")).toString();
            if (!path.isEmpty() && onOpenPath) onOpenPath(path, 0);
            return;
        }
        relay::log::debug(QStringLiteral("guest_bridge_tool pane=%1 tool=%2").arg(paneLogId(), tool));
    }
    bool sharedWithPhone() const { return relay::RemoteShare::instance().isSharing(m_token); }
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
    // The worker's guest rows (29.3), for Options › Claude Code and Codex: whether each guest is
    // installed, the models it offers and the reasoning efforts those take.
    QJsonArray guestPresets() const {
        QJsonArray rows;
        for (const auto &item : m_presets)
            if (item.toObject().value(QStringLiteral("group")).toString() == QStringLiteral("guest")) rows.append(item);
        return rows;
    }
    // Options changed `guests/<guest>/model` or `…/effort`. A pane that is on that guest right now
    // asks its harness to move (`set_model` with the `guest` block, as `/model claude opus` does);
    // every other pane picks the new default up the next time it starts the guest.
    void guestOptionsChanged(const QString &guest) {
        if (guestOfPreset(m_currentPreset) != guest || !m_configured) return;
        const QJsonObject preset = presetById(m_currentPreset);
        m_guestRequest = QJsonObject();
        QJsonObject request{{"type", "set_model"}, {"preset", m_currentPreset}, {"use_stored_key", true},
                            {"base_url", preset.value(QStringLiteral("base_url")).toString()},
                            {"model", QString()}};
        request.insert(QStringLiteral("guest"), takeGuestRequest(m_currentPreset));
        send(request);
        status(QStringLiteral("%1 takes the new setting from its next turn.").arg(guestDisplayName(guest)));
        changed();
    }
    // `guests/<guest>/<key>` (model, effort): the default a guest is started with, "" for the
    // CLI's own. One spelling, shared with the Options page (RelayWindow::guestSettingKey).
    static QString guestSetting(const QString &guest, const QString &key) {
        return QSettings().value(QStringLiteral("guests/%1/%2").arg(guest, key)).toString().trimmed();
    }
    QString currentPreset() const { return m_currentPreset; }
    QString model() const { return m_model; }
    QString sessionId() const { return m_sessionId; }
    QString sessionDir() const { return m_sessionDir; }

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
            // The same id names this pane's prompt history: the composer was pointed at the fresh
            // token in buildUi(), before there was a saved spec to read. Nothing has been typed
            // yet, so re-pointing simply loads the pane's own history instead.
            m_editor->useHistoryFile(promptHistoryPath());
        }
        changed();
    }

    // ----- terminal scrollback across a restart (owner report, 2026-09-18) ---------------------
    // Saved when the window closes and when Relay quits; replayed into the restarted pane at its
    // first prompt. Plain text: see the note in src/WindowState.h on why the colours do not come
    // back with it.
    QString scrollbackId() const { return m_scrollbackId; }
    // The file this pane's prompt-box Up/Down history lives in, under the same id (owner report,
    // 2026-09-19: "i want pane histories for up/down"). Empty only without a data location.
    QString promptHistoryPath() const { return relay::prompthistory::pathFor(m_scrollbackId); }
    // The two rules the replay prints around the restored block. They are also what the save
    // filters out, so a pane that has been restored twice does not stack them.
    // True of both ways a pane comes back: Relay restarted, or the pane was closed and reopened.
    static QString scrollbackOpenMark() { return QStringLiteral("— scrollback from this pane's previous shell —"); }
    // What the rule said until 2026-09-18; still filtered, for text saved under it.
    static QString scrollbackLegacyOpenMark() { return QStringLiteral("— scrollback from before the restart —"); }
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
                        return line == scrollbackOpenMark() || line == scrollbackLegacyOpenMark() || line == scrollbackCloseMark();
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
        closeQuestion(QString());   // the conversation it belonged to is over (#MQ9C)
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
        if (sendSelectedSteerNow()) return;   // a selected steer row: that steer, now
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
        // "role:" is a Main/Flash row, "gear:" the model options modal, and "serving:" the model
        // this one turn is running on (an image, plan mode's own role, or a failover): none of them
        // is a preset to switch to. The first two are acted on where the box is built
        // (chooseAgentRole, openRolesDialog); this is the guard for the other callers — /model and
        // the roles modal.
        if (id.startsWith(QStringLiteral("role:")) || id.startsWith(QStringLiteral("gear:"))
            || id.startsWith(QStringLiteral("serving:"))) return;
        // "guest:" is Claude Code or Codex. Two shapes (protocol 29.4): the worker's harness
        // preset, which is an ordinary preset switch carrying a `guest` object, and — when the
        // worker cannot run that guest headless — the Tier B launch of its TUI in this pane's
        // shell (26.9), which is not a preset at all.
        if (const QString guest = guestOfPreset(id); !guest.isEmpty()) {
            if (!guestHarnessUsable(guest)) { chooseGuest(guest); return; }
            // A TUI guest already here owns the prompt box: the pane moves on from it at once
            // and the pick is made again, now with nothing in front (leaveGuest, 26.9).
            if (guestInFront()) { leaveGuest([this, id] { selectModel(id); }); return; }
            // …and on through the ordinary preset path below.
        }
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
        QJsonObject request{{"type", "set_model"}, {"preset", id}, {"use_stored_key", true},
              {"base_url", preset.value(QStringLiteral("base_url")).toString()},
              {"model", preset.value(QStringLiteral("model")).toString()},
              {"extra", preset.value(QStringLiteral("extra")).toObject()},
              {"max_tokens", QSettings().value(QStringLiteral("provider/max_tokens"), 0).toInt()}};
        // A guest preset (29.3): what the harness is started with — the permission mode, and a
        // model, `resume` or `fork` when the pick carried one.
        if (const QJsonObject guest = takeGuestRequest(id); !guest.isEmpty()) request.insert(QStringLiteral("guest"), guest);
        send(request);
        rememberPreset(id);
        rememberPresetBeforeGuest(id);
        m_currentPreset = id; changed();
    }
    // The Main row of the roles modal: run this pane on another model id from the same provider
    // ("kimi-k3-turbo" rather than "kimi-k3"). The pane's model is not a tier the worker resolves,
    // so this is the model chip's own path — set_model, which keeps the conversation — and the id
    // is remembered as `provider/model` so the next pane and the provider dialog agree with it.
    // Empty: back to the provider's default model.
    void setMainModel(const QString &model) {
        const QJsonObject preset = presetById(m_currentPreset);
        // On a guest preset (29.4) the model belongs to the guest: the harness is asked to switch
        // (`set_model` with `guest.model`, which restarts it) and nothing is written to Relay's
        // own provider settings — "sonnet" is not a default for the next pane's provider.
        if (const QString guest = guestOfPreset(m_currentPreset); !guest.isEmpty()) {
            const QString want = model.trimmed();
            if (want.isEmpty() || want == m_model) return;
            if (!m_configured) { status(QStringLiteral("%1 is not running yet.").arg(guestDisplayName(guest))); return; }
            m_guestRequest = QJsonObject{{"model", want}};
            QJsonObject request{{"type", "set_model"}, {"preset", m_currentPreset}, {"use_stored_key", true},
                                {"base_url", preset.value(QStringLiteral("base_url")).toString()},
                                {"model", QString()}};
            request.insert(QStringLiteral("guest"), takeGuestRequest(m_currentPreset));
            send(request);
            changed();
            return;
        }
        const QString id = model.trimmed().isEmpty() ? preset.value(QStringLiteral("model")).toString()
                                                     : model.trimmed();
        if (id.isEmpty()) return;
        QSettings settings;
        settings.setValue(QStringLiteral("provider/model"), id);
        if (!m_configured || preset.isEmpty()) {
            status(QStringLiteral("Model for new conversations: %1.").arg(id));
            changed();
            return;
        }
        if (id == m_model) return;
        send({{"type", "set_model"}, {"preset", m_currentPreset}, {"use_stored_key", true},
              {"base_url", preset.value(QStringLiteral("base_url")).toString()},
              {"model", id},
              {"extra", preset.value(QStringLiteral("extra")).toObject()},
              {"max_tokens", settings.value(QStringLiteral("provider/max_tokens"), 0).toInt()}});
        changed();
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
                QStringLiteral("flash"), QStringLiteral("local"), QStringLiteral("planning"),
                QStringLiteral("summaries"), QStringLiteral("suggestions"), QStringLiteral("chores"),
                QStringLiteral("audit"), QStringLiteral("vision"), QStringLiteral("route_assist")};
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
            {QStringLiteral("local"), QStringLiteral("Local agent")},
            {QStringLiteral("planning"), QStringLiteral("Plan mode")},
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
    // What /local and the Local row say when this machine serves nothing: both ways to fix it.
    static QString noLocalModelMessage() {
        return QStringLiteral("No local model is set up. Settings › Local models, or "
                              "scripts/relay-local.py add --detect.");
    }
    // Whether this machine serves a model at all: a `presets` row with `local: true` (card #24XJ).
    // The Local agent is offered only when there is one, so /local and the chip's Local row never
    // switch a pane onto a tier that would silently resolve back to Main.
    bool hasLocalEndpoint() const {
        for (const auto &item : m_presets)
            if (item.toObject().value(QStringLiteral("local")).toBool()) return true;
        return false;
    }
    // The Local agent, the same switch as the Flash one (owner, 2026-09-18: "add a /local command
    // that switches to your chosen local LLM"). No default shortcut: /local is the fast path.
    void toggleLocalAgent() {
        if (m_agentRole != QStringLiteral("local") && !hasLocalEndpoint()) { status(noLocalModelMessage()); return; }
        setAgentRole(m_agentRole == QStringLiteral("local") ? QStringLiteral("main") : QStringLiteral("local"));
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
    static constexpr int kGuestModelMax = 64;   // program_state.guest_model's ceiling (26.3)
    // The guest event spool (26.3). A file past kGuestEventMax is deleted unread: a hook payload
    // that large is a bug or an attack, never a question worth showing, and the shim caps what it
    // forwards far below it. kGuestEventsPerTick keeps a burst from freezing the UI; the names
    // sort, so the rest are handled in order on the next tick.
    static constexpr qint64 kGuestEventMax = 256 * 1024;
    static constexpr int kGuestEventsPerTick = 64;
    static constexpr int kGuestQuestionsMax = 8;            // permission questions pending at once
    static constexpr int kGuestAnswerKeepSeconds = 15 * 60; // an answer whose shim died first

    relay::screen::Signals screenSignals() const {
        relay::screen::Signals sig;
        sig.mode = terminalMode();
        sig.programRunning = processBusy();
        sig.programReading = m_waiting;
        // A login's alternate screen is the host's multiplexer, not a full-screen program: the
        // classifier is still asked, and what it finds on the cursor's row decides (#S5SH).
        sig.altScreen = m_altScreen && !m_login.active;
        sig.screenReadable = canShowAgentTheScreen();
        return sig;
    }

    // Re-read the last rows and tell the rest of the pane (and the worker) what they say.
    void updateScreenPrompt() {
        relay::screen::Detection next;
        // Relay's own inline output over a remote prompt is not the program asking anything: an
        // agent line ending in "password:" must not mask the prompt box (card #S5SH).
        if (m_backend && !m_promptReported && !m_native && !(m_inlineOpen && m_login.active))
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
        QJsonObject state = QJsonObject{
            {QStringLiteral("type"), QStringLiteral("program_state")},
            {QStringLiteral("granted"), m_delegated && processBusy() && !m_native && !masked},
            {QStringLiteral("reason"), m_delegated ? QStringLiteral("delegated") : m_delegationEnd},
            {QStringLiteral("program"), foregroundProgramName()},
            {QStringLiteral("guest"), m_guest},
            // The guest's live facts (26.3): what it is running on, and whether a turn of its own
            // is going. Its context share joins below, only when the statusline could say.
            {QStringLiteral("guest_model"), m_guestModel.left(kGuestModelMax)},
            {QStringLiteral("guest_busy"), m_guestBusy},
            {QStringLiteral("kind"), QString::fromLatin1(relay::screen::kindName(m_screenPrompt.kind))},
            {QStringLiteral("question"), m_screenPrompt.question},
            {QStringLiteral("masked"), masked},
            {QStringLiteral("alt_screen"), m_altScreen},
            {QStringLiteral("waiting"), m_screenPrompt.actionable() || m_waiting},
            {QStringLiteral("max_writes"), kMaxProgramWrites},
            {QStringLiteral("screen_source"), canShowAgentTheScreen() ? QStringLiteral("engine") : QStringLiteral("none")}};
        if (m_guestContextPct >= 0) state.insert(QStringLiteral("guest_context_pct"), m_guestContextPct);
        return state;
    }

    void sendProgramState() {
        if (!m_configured) return;
        const QJsonObject message = programStateMessage();
        if (message == m_lastProgramState) return;
        m_lastProgramState = message;
        send(message);
    }

    // The guest agent (Claude Code / Codex) the foreground program was classified as; empty
    // for anything else. The worker hears about a change in the next program_state (26.1).
    void setGuest(const QString &guest) {
        if (guest == m_guest) return;
        const QString before = m_guest;
        m_guest = guest;
        if (!m_guest.isEmpty()) m_guestWanted.clear();   // picked and now detected (26.9)
        m_guestLeaving = false;                          // whoever was being left has gone, or is new
        // A guest that left (or changed) takes its live facts and its open question with it;
        // the next statusline or state event repopulates them (26.3).
        clearGuestState();
        m_guestSlashCommands.clear();
        if (!m_guest.isEmpty()) publishGuestSlashCatalog(m_guest);
        // The sidecar's lifetime follows the claude panes (26.9): it hears about every claude that
        // arrives, whether picked or typed, and stops a minute after the last one has gone.
        if (relay::guestbridge::Bridge::started()) {
            if (m_guest == QStringLiteral("claude")) relay::guestbridge::Bridge::instance().guestArrived(m_token);
            else if (before == QStringLiteral("claude")) relay::guestbridge::Bridge::instance().guestLeft(m_token);
        }
        // Codex reports a turn only in its rollout transcript (26.6): no statusline, and a
        // `notify` that fires when the turn is already over. Its `state`/`statusline` events come
        // from a tail that follows this pane's newest rollout for as long as the codex runs.
        startGuestTail(m_guest);
        sendProgramState();
        changed();
        // The bridge follows the guest (26.5). The registration is rewritten either way — it names
        // which guest is in the foreground now, and that is what tells the sidecar's router this
        // pane is the one a claude's request belongs to. It is *not* removed when a guest exits:
        // the pane keeps carrying the port in its shell's environment, so the next claude started
        // there connects before the program poll has noticed it, and a pane that vanished from the
        // router in between would have had its first request refused as unmatched. Only a closing
        // pane unregisters.
        registerWithBridge();
        // A model picked while the guest was running waited for this moment (leaveGuest, 26.9):
        // the guest was asked to exit, the shell is back, and the switch can be made now.
        if (m_guest.isEmpty() && m_guestLeaveThen) {
            const std::function<void()> then = std::move(m_guestLeaveThen);
            m_guestLeaveThen = nullptr;
            then();
        }
    }

    // ----- the codex rollout tail (GT7X, 26.6) -------------------------------------------------
    //
    // Claude has a statusline shim and hooks that bracket a turn; codex has neither. What it does
    // have is the rollout transcript it appends to while it works, so `relay_core.guest_codex
    // tail` follows the newest rollout whose own cwd is this pane's and emits `state` and
    // `statusline` onto the pane's spool exactly as a shim would — which is what makes the codex
    // chip, and the composer's "queued until it is ready", true for codex and not only for claude.
    // One helper per codex pane, ended with the guest, and nothing at all for any other program.
    void startGuestTail(const QString &guest) {
        stopGuestTail();
        if (guest != QStringLiteral("codex") || m_runtime.path().isEmpty()) return;
        m_guestTail = new QProcess(this);
        m_guestTail->setWorkingDirectory(m_cwd);
        m_guestTail->setProcessEnvironment(guestHelperEnvironment());
        m_guestTail->setProgram(m_python);
        m_guestTail->setArguments({QStringLiteral("-m"), QStringLiteral("relay_core.guest_codex"),
                                   QStringLiteral("tail"), QStringLiteral("--cwd"), m_cwd});
        m_guestTail->setStandardOutputFile(QProcess::nullDevice());
        m_guestTail->setStandardErrorFile(QProcess::nullDevice());
        connect(m_guestTail, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            // A missing python or an unimportable backend: the pane works without the tail, so it
            // is logged and not said out loud. Nothing retries it until the next codex.
            relay::log::error(QStringLiteral("guest_tail_failed pane=%1").arg(paneLogId()));
            stopGuestTail();
        });
        m_guestTail->start();
    }

    void stopGuestTail() {
        if (!m_guestTail) return;
        QProcess *tail = m_guestTail;
        m_guestTail = nullptr;
        tail->disconnect(this);            // errorOccurred must not call back into a gone pane
        connect(tail, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                tail, &QObject::deleteLater);
        if (tail->state() == QProcess::NotRunning) { tail->deleteLater(); return; }
        tail->terminate();
        // Never waited for on the UI thread: the tail sleeps a second between polls, so a
        // waitForFinished here would stall a pane close by that second.
        QTimer::singleShot(2000, tail, [tail] {
            if (tail->state() != QProcess::NotRunning) tail->kill();
        });
    }

    // ----- the Claude IDE bridge: this pane's registration, and its diffs (GT7X, 26.5) --------
    //
    // The sidecar routes a request by the longest workspace/cwd prefix of the paths it names, so
    // every pane tells it where it is — once at shell start (the shell carries the port; a claude
    // started in it connects back), again when the cwd or the workspace moves, and again when a
    // claude turns up in the foreground. Rewrites that would say exactly what the last one said
    // are dropped by the bridge itself, so this is cheap enough to ask for on every change.
    void registerWithBridge() {
        // Asked before the singleton, not after: `Bridge::instance()` builds a state directory and
        // a QProcess, and a run in which no pane has launched a claude yet should have neither.
        // The bridge is always on (26.9); it is *started* by the first claude launch.
        if (!relay::guestbridge::Bridge::started()) return;
        auto &bridge = relay::guestbridge::Bridge::instance();
        bridge.setDataRoot(m_data);
        // The shell pid is how the sidecar tells two claudes in one project apart: it walks the
        // connecting process's ancestry up to a registered pane shell (26.5). `shellPid()` is the
        // process on the other end of the pty, so everything started in this pane descends from
        // it; `m_shellPid` is the last one seen, for the window where the backend is being
        // replaced. 0 is honest — the sidecar falls back to routing by path.
        const int shell = shellPid() > 0 ? shellPid() : m_shellPid;
        bridge.registerPane(m_token, m_runtime.path(), m_workspace, m_cwd, m_guest, shell);
    }

    void unregisterFromBridge() {
        if (!relay::guestbridge::Bridge::started()) return;
        relay::guestbridge::Bridge::instance().unregisterPane(m_token);
    }

    // The pane's answer to a blocking openDiff (26.5): the diff view shows the change, this is
    // what the user decided. FILE_SAVED makes the *sidecar* write the file — the GUI never writes
    // a user's file from a bridge event — and either answer returns the waiting tool call.
    void settleGuestDiff(const QString &outcome, const QString &why) {
        if (!m_guestDiff.pending) return;
        const GuestDiff diff = m_guestDiff;
        m_guestDiff = GuestDiff();
        // The answer is going out by this route, so the diff view's own Accept / Reject has
        // nothing left to ask: drop it *without* answering, or the buttons would still be there
        // offering a decision on a call that has already returned.
        if (diff.view) diff.view->clearDecision();
        // And the banner that pointed at it is now pointing at nothing. Only if it is still this
        // diff's own text: anything else up there belongs to whatever replaced it, and taking
        // another notice down is not this answer's business.
        if (!diff.banner.isEmpty() && m_banner && m_banner->isVisible() && m_bannerText
            && m_bannerText->text() == diff.banner)
            hideBanner();
        relay::guestbridge::Bridge::instance().answerDiff(diff.replyPath, outcome);
        const QString name = QFileInfo(diff.file).fileName();
        const bool saved = outcome == relay::guestbridge::fileSaved();
        relay::log::info(QStringLiteral("guest_bridge_diff pane=%1 saved=%2 reason=%3")
                             .arg(paneLogId()).arg(saved ? 1 : 0).arg(why));
        // A closing pane settles its debt without staging a toast nobody will ever see.
        if (!m_closing)
            toast(saved ? QStringLiteral("Saved claude's changes to %1").arg(name)
                        : QStringLiteral("Kept the file as it was · claude's changes to %1 were not applied").arg(name));
    }

    // `openDiff` from the bridge: Relay's diff view beside this pane, with Accept and Reject in
    // that view's own header. claude's call blocks until one of them, so the decision has to stay
    // reachable for as long as the proposal is on screen — which is why it is *not* in the pane's
    // banner any more. The banner is one shared strip: an out-of-memory notice, a shell error or an
    // ssh offer replaced it, and with it the only way to accept the change; the other way round, a
    // diff banner made Ctrl+Shift+R (which runs the visible banner's action) write a file instead
    // of restarting a stopped shell. The banner is now a pointer at the diff pane and nothing more,
    // free to be replaced without settling anything.
    void showGuestDiff(const QJsonObject &data) {
        const QString reply = data.value(QStringLiteral("reply")).toString();
        const QString file = data.value(QStringLiteral("file")).toString();
        const QString diff = data.value(QStringLiteral("diff")).toString();
        const QString tabName = data.value(QStringLiteral("tab_name")).toString();
        if (reply.isEmpty()) {
            // Not a protocol event a pane can answer, so it is not answered: logged and dropped.
            relay::log::error(QStringLiteral("guest_bridge_diff_no_reply pane=%1").arg(paneLogId()));
            return;
        }
        // A second openDiff while one waits: claude replaced its proposal, and the first call
        // cannot be answered any more. Rejecting it is the only honest answer. Done before the
        // view is asked to show the new diff, so that the view's own "a new diff rejects the old
        // decision" rule finds nothing left to reject.
        settleGuestDiff(relay::guestbridge::diffRejected(), QStringLiteral("replaced"));
        const QString name = QFileInfo(file).fileName();
        relay::DiffView *view = diff.isEmpty() || !onOpenDiff
                                    ? nullptr
                                    : onOpenDiff(tabName.isEmpty() ? name : tabName, diff);
        if (!view) {
            // Nowhere to show the change is nowhere to decide it: refuse rather than leave claude
            // waiting on a question this pane cannot put to anyone. The reply goes out directly,
            // because there is no pending diff to settle.
            relay::log::error(QStringLiteral("guest_bridge_diff_unshowable pane=%1").arg(paneLogId()));
            relay::guestbridge::Bridge::instance().answerDiff(reply, relay::guestbridge::diffRejected());
            relay::log::info(QStringLiteral("guest_bridge_diff pane=%1 saved=0 reason=no diff view")
                                 .arg(paneLogId()));
            return;
        }
        m_guestDiff.replyPath = reply;
        m_guestDiff.file = file;
        m_guestDiff.view = view;
        m_guestDiff.pending = true;
        // The view outlives neither answer: it calls this once, and `settleGuestDiff` takes the
        // callback back out of it on every other path (the pane closing, a timeout, `close_tab`).
        // The guard is for the window closing, where the view may be destroyed after the pane.
        view->setDecision(QStringLiteral("Accept"), QStringLiteral("Reject"),
                          [this, guard = QPointer<Pane>(this)](bool accepted) {
                              if (!guard) return;
                              settleGuestDiff(accepted ? relay::guestbridge::fileSaved()
                                                       : relay::guestbridge::diffRejected(),
                                              accepted ? QStringLiteral("accepted")
                                                       : QStringLiteral("rejected"));
                          });
        const QString where = m_workspace.isEmpty() ? name : QStringLiteral("%1 · %2").arg(name, m_workspace);
        // A pointer, not a decision: no action button, so Ctrl+Shift+R still means "restart what
        // stopped here" and any other banner may take this one's place.
        m_guestDiff.banner = QStringLiteral("claude proposes changes to %1 · Accept or Reject in the diff pane")
                                 .arg(where);
        showBanner(m_guestDiff.banner, QString(), {});
    }

    // ----- the guest event channel (issue GT7X, protocol 26.3) ---------------------------------
    // Everything a guest or its shim learns arrives as one file in `guest-events/` under the
    // pane's runtime dir: relay_core.guest_hook writes it with mkstemp and renames it into place,
    // named `<time_ns>-<pid>-<counter>.json` so the names sort into the order they were written.
    // The pane lists the directory on the same tick as state.json, handles the files in that
    // order, and deletes each one — a tool input, with its file paths and contents, is never left
    // lying in the runtime dir.
    //
    // It was a single `guest.json` slot until the review of 51587e3: a statusline tick landing
    // 100 ms after a permission question replaced the question, and the shim then waited out its
    // whole timeout for an answer to a question nobody was ever shown.
    static QString guestEventsDirName() { return QStringLiteral("guest-events"); }
    static QString guestAnswersDirName() { return QStringLiteral("guest-answers"); }
    QString guestEventsDir() const { return m_runtime.filePath(guestEventsDirName()); }
    QString guestAnswersDir() const { return m_runtime.filePath(guestAnswersDirName()); }

    void pollGuestEvents() {
        QDir spool(guestEventsDir());
        if (!spool.exists()) return;
        const QStringList names = spool.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        int handled = 0;
        for (const QString &name : names) {
            // A burst must not freeze the UI; the names sort, so the rest keep their order and
            // are handled on the next tick.
            if (++handled > kGuestEventsPerTick) break;
            QFile file(spool.filePath(name));
            const qint64 size = file.size();
            QByteArray raw;
            if (size > kGuestEventMax)
                relay::log::info(QStringLiteral("guest_event_oversize pane=%1 bytes=%2").arg(paneLogId()).arg(size));
            else if (file.open(QIODevice::ReadOnly))
                raw = file.read(kGuestEventMax);
            file.close();
            file.remove();   // read or refused, it is gone: see the note on tool inputs above
            if (raw.isEmpty()) continue;
            const auto envelope = QJsonDocument::fromJson(raw).object();
            if (envelope.value(QStringLiteral("token")).toString() != m_token) continue;
            handleGuestEvent(envelope.value(QStringLiteral("sequence")).toString(),
                             envelope.value(QStringLiteral("guest")).toString(),
                             envelope.value(QStringLiteral("event")).toString(),
                             envelope.value(QStringLiteral("data")).toObject());
        }
    }

    // One event off the channel: the hooks phase's own events (26.3), the bridge phase's (26.5),
    // and `slash`, Relay's own scanned fallback catalog (26.8). `guest` is the envelope's own id:
    // a guest inside tmux is not detected, so m_guest can be empty while its hooks arrive, and a
    // question is still labelled from the envelope rather than called Claude Code by default.
    void handleGuestEvent(const QString &sequence, const QString &guest, const QString &name, const QJsonObject &data) {
        if (name == QStringLiteral("statusline")) {
            m_guestModel = data.value(QStringLiteral("model")).toString().simplified().left(kGuestModelMax);
            const QJsonValue share = data.value(QStringLiteral("context_pct"));
            m_guestContextPct = share.isDouble() && 0 <= share.toInt() && share.toInt() <= 100 ? share.toInt() : -1;
            updateGuestChip();
            sendProgramState();
            changed();
        } else if (name == QStringLiteral("state")) {
            setGuestBusy(data.value(QStringLiteral("busy")).toBool());
        } else if (name == QStringLiteral("hook")) {
            handleGuestHook(sequence, guest, data.value(QStringLiteral("name")).toString(),
                            data.value(QStringLiteral("payload")).toObject());
        } else if (name == QStringLiteral("bridge")) {
            guestBridgeEvent(data);
        } else if (name == QStringLiteral("slash")) {
            QStringList commands;
            static const QRegularExpression command(QStringLiteral("^/[A-Za-z0-9][A-Za-z0-9._-]*$"));
            for (const QJsonValue &value : data.value(QStringLiteral("commands")).toArray()) {
                const QString item = value.toString().trimmed();
                if (command.match(item).hasMatch() && !commands.contains(item, Qt::CaseInsensitive))
                    commands << item;
            }
            m_guestSlashCommands = commands;
            updateSlashPopup();
        }
    }

    void setGuestBusy(bool busy) {
        if (busy == m_guestBusy) return;
        m_guestBusy = busy;
        updateGuestChip();
        sendProgramState();
        changed();
        // The pane moved to another model while this guest was working (leaveGuest): its turn
        // was left to finish, as a native model's request in flight is, and now it is asked to go.
        if (!m_guestBusy && m_guestLeaving && !m_guest.isEmpty()) askGuestToExit(m_guest);
        if (!m_guestBusy) pumpQueue();
    }

    // The TUI guest the prompt box is talking to: in the foreground, and not one the pane has
    // already moved on from (26.9, "Leaving a guest").
    bool guestInFront() const { return !m_guest.isEmpty() && !m_guestLeaving; }

    // A hook the guest's shim forwarded (26.4). `PermissionRequest` — the hook claude sends only
    // when it is really about to ask — becomes a Relay question on the pane; a notification
    // reaches the notification centre; the rest only move state. `PreToolUse` is not installed
    // (it fires before every tool call, including the auto-allowed ones), but an install that
    // carries one is still read as the busy signal it is.
    void handleGuestHook(const QString &sequence, const QString &guest, const QString &name, const QJsonObject &payload) {
        if (name == QStringLiteral("PermissionRequest")) {
            queueGuestQuestion(sequence, guest, payload);
        } else if (name == QStringLiteral("Notification")) {
            const QString message = payload.value(QStringLiteral("message")).toString().simplified();
            if (!message.isEmpty()) notify(guestDisplayName(guest), message);
        } else if (name == QStringLiteral("notify")) {
            // Codex's own `notify` program (26.6), which is what the Options row installs: its one
            // payload is `agent-turn-complete`, so this is the finished turn reaching the
            // notification centre — and the end of the turn, whatever the rollout tail last said.
            const QString said = payload.value(QStringLiteral("last-assistant-message")).toString().simplified();
            notify(guestDisplayName(guest),
                   said.isEmpty() ? QStringLiteral("finished its turn.") : said.left(200));
            setGuestBusy(false);
        } else if (name == QStringLiteral("UserPromptSubmit") || name == QStringLiteral("PreToolUse")) {
            setGuestBusy(true);   // a guest turn has begun; Stop ends it
        } else if (name == QStringLiteral("Stop")) {
            setGuestBusy(false);
        }
    }

    // The permission question: the guest asks before a tool run, the shim holds the hook open,
    // and Relay answers by writing `guest-answers/<question>.json`. It is never answered on the
    // guest's behalf — the bar stays until the user says, and a shim that times out falls back to
    // the guest's own asking, so nothing is auto-approved.
    //
    // Questions queue. Two tool calls in flight, or a second question arriving while the first is
    // up, each get their turn rather than the first being stranded (review of 51587e3).
    struct GuestQuestion {
        QString sequence;   // the shim's uuid4; it also names the answer file it waits on
        QString guest;      // the envelope's guest id, which need not be m_guest
        QString label;      // the sentence the bar and the notification show
    };

    // A question id names a file this pane writes, so only a uuid4's own alphabet is taken: a
    // shim that sent anything else could otherwise aim the answer at another path.
    static bool validGuestSequence(const QString &sequence) {
        if (sequence.isEmpty() || sequence.size() > 64) return false;
        for (const QChar c : sequence) {
            const bool plain = (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
                               || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
                               || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                               || c == QLatin1Char('-') || c == QLatin1Char('_');
            if (!plain) return false;
        }
        return true;
    }

    void queueGuestQuestion(const QString &sequence, const QString &guest, const QJsonObject &payload) {
        if (!m_guestBar || !validGuestSequence(sequence)) return;
        for (const GuestQuestion &pending : std::as_const(m_guestQuestions))
            if (pending.sequence == sequence) return;   // one envelope, one question
        if (m_guestQuestions.size() >= kGuestQuestionsMax) {
            // Anything past this would wait behind eight others. The shim's own timeout is the
            // fallback, and it leaves the guest asking in the terminal, which is never wrong.
            relay::log::info(QStringLiteral("guest_question_dropped pane=%1 pending=%2")
                                 .arg(paneLogId()).arg(m_guestQuestions.size()));
            return;
        }
        m_guestQuestions.append(GuestQuestion{sequence, guest, guestQuestionLabel(guest, payload)});
        // Away from the pane too: the bell, and the desktop when Relay is not in front. A blocked
        // guest that only drew a small bar in a background tab said nothing at all.
        notify(guestDisplayName(guest), m_guestQuestions.constLast().label,
               relay::NotificationCenter::kindWarning);
        if (m_guestQuestions.size() == 1) showGuestQuestion();
        else refreshGuestBar();      // the one on screen now says how many are behind it
        changed();   // the tab's "needs you" glyph: statusFacts() counts a pending question
    }

    static QString guestQuestionLabel(const QString &guest, const QJsonObject &payload) {
        const QString tool = payload.value(QStringLiteral("tool_name")).toString();
        const QJsonObject input = payload.value(QStringLiteral("tool_input")).toObject();
        QString detail = input.value(QStringLiteral("command")).toString();
        for (const char *field : {"file_path", "path", "url", "pattern"})
            if (detail.isEmpty()) detail = input.value(QLatin1String(field)).toString();
        QString label = guestDisplayName(guest)
                        + QStringLiteral(" wants to run %1").arg(tool.isEmpty() ? QStringLiteral("a tool") : tool);
        if (!detail.isEmpty()) label += QStringLiteral(" · ") + detail.simplified().left(80);
        return label;
    }

    // The front of the queue, on screen, with the count of the ones behind it. Called again
    // whenever the queue changes, so a second question arriving does not leave the bar claiming
    // to be the only one.
    void refreshGuestBar() {
        if (!m_guestBar) return;
        if (m_guestQuestions.isEmpty()) { m_guestBar->hide(); return; }
        QString label = m_guestQuestions.constFirst().label;
        if (m_guestQuestions.size() > 1)
            label = QStringLiteral("%1 of %2 · %3").arg(1).arg(m_guestQuestions.size()).arg(label);
        m_guestBarLabel->setText(m_guestBarLabel->fontMetrics().elidedText(label, Qt::ElideMiddle,
                                                                           std::max(200, width() - 420)));
        m_guestBarLabel->setToolTip(label + QStringLiteral("\nY or Enter to allow · N or Esc to deny"));
        m_guestBar->adjustSize();
        m_guestBar->show();
        m_guestBar->raise();
        placeGuestBar();
    }

    // ...and the bar takes the keyboard while it is up: the guest is blocked on this answer, and
    // a question nobody can answer without a mouse is one a touch-typist cannot answer at all
    // (review of 51587e3).
    void showGuestQuestion() {
        refreshGuestBar();
        if (m_guestAllow && !m_guestQuestions.isEmpty()) m_guestAllow->setFocus(Qt::OtherFocusReason);
    }

    // Y / Enter allow, N / Esc deny, while the bar has the keyboard. Any other key was meant for
    // the guest: the keyboard goes back and the key is delivered there, so nothing is swallowed.
    bool guestBarKey(QKeyEvent *event) {
        if (m_guestQuestions.isEmpty()) return false;
        switch (event->key()) {
        case Qt::Key_Y: case Qt::Key_Return: case Qt::Key_Enter:
            answerGuestPermission(true);
            return true;
        case Qt::Key_N: case Qt::Key_Escape:
            answerGuestPermission(false);
            return true;
        case Qt::Key_Tab: case Qt::Key_Backtab: case Qt::Key_Left: case Qt::Key_Right:
        case Qt::Key_Space:
            return false;    // ordinary focus and button handling inside the bar
        default:
            break;
        }
        focusInput();
        if (QWidget *target = focusWidget(); target && target != m_guestBar && !m_guestBar->isAncestorOf(target))
            QApplication::sendEvent(target, event);
        return true;
    }

    bool guestBarOwns(QObject *object) const {
        auto *widget = qobject_cast<QWidget *>(object);
        return m_guestBar && widget && (widget == m_guestBar || m_guestBar->isAncestorOf(widget));
    }

    // The answer, written atomically as the file the waiting shim names, then the next question.
    void answerGuestPermission(bool allow) {
        if (m_guestQuestions.isEmpty()) return;
        const GuestQuestion question = m_guestQuestions.takeFirst();
        writeGuestAnswer(question.sequence, allow);
        status(allow ? QStringLiteral("Allowed in Relay.") : QStringLiteral("Denied in Relay."));
        if (m_guestBarMouse) {
            m_guestBarMouse = false;
            hint(QStringLiteral("guest.permission.mouse"),
                 QStringLiteral("Next time: Y or Enter allows, N or Esc denies · the bar holds the "
                                "keyboard while it is up"));
        }
        if (m_guestQuestions.isEmpty()) {
            if (m_guestBar) m_guestBar->hide();
            focusInput();          // the keyboard goes back where it came from
        } else {
            showGuestQuestion();
        }
        changed();
    }

    void writeGuestAnswer(const QString &sequence, bool allow) {
        if (!validGuestSequence(sequence) || m_runtime.path().isEmpty()) return;
        QDir answers(guestAnswersDir());
        if (!answers.exists() && !QDir().mkpath(answers.path())) return;
        QFile::setPermissions(answers.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QSaveFile file(answers.filePath(sequence + QStringLiteral(".json")));
        if (!file.open(QIODevice::WriteOnly)) return;
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("token"), m_token},
                                             {QStringLiteral("sequence"), sequence},
                                             {QStringLiteral("decision"), allow ? QStringLiteral("allow")
                                                                                 : QStringLiteral("deny")}})
                       .toJson(QJsonDocument::Compact));
        file.commit();
        sweepGuestAnswers(answers);
    }

    // The shim deletes its answer as it reads it. One whose shim died first would otherwise stay
    // until the pane closes, so anything older than the longest wait a shim can make is removed.
    void sweepGuestAnswers(const QDir &answers) const {
        const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-kGuestAnswerKeepSeconds);
        const auto stale = answers.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files);
        for (const QFileInfo &info : stale)
            if (info.lastModified() < cutoff) QFile::remove(info.absoluteFilePath());
    }

    void clearGuestQuestions() {
        const bool had = !m_guestQuestions.isEmpty();
        // The guest is gone (or changed) with questions still open: each shim behind one is
        // holding its hook for up to 120 s (26.4) on an answer nobody can give any more. It is
        // told no — never yes: a permission is not granted on the user's behalf — so it returns
        // at once and the guest, if it is still there, asks in its own terminal as it always could.
        for (const GuestQuestion &pending : std::as_const(m_guestQuestions))
            writeGuestAnswer(pending.sequence, false);
        m_guestQuestions.clear();
        if (m_guestBar) m_guestBar->hide();
        if (had) focusInput();
    }

    void clearGuestState() {
        m_guestModel.clear();
        m_guestContextPct = -1;
        m_guestBusy = false;
        clearGuestQuestions();
        updateGuestChip();
    }

    // The guest's chip in the prompt-box strip: its model and, when the statusline could say,
    // how much of its context window is in use. The same idiom as the context chip beside it.
    void updateGuestChip() {
        if (!m_guestChip) return;
        if (m_guest.isEmpty()) { m_guestChip->hide(); return; }
        const QString model = m_guestModel.isEmpty() ? guestDisplayName(m_guest) : m_guestModel;
        m_guestChip->setText(m_guestContextPct >= 0
                                 ? QStringLiteral("%1 · %2%").arg(model, QString::number(m_guestContextPct)) : model);
        m_guestChip->setProperty("warn", m_guestContextPct >= 90);
        m_guestChip->style()->unpolish(m_guestChip); m_guestChip->style()->polish(m_guestChip);
        m_guestChip->setToolTip(QStringLiteral("%1 · %2 of its context window%3")
                                    .arg(guestDisplayName(m_guest),
                                         m_guestContextPct >= 0 ? QStringLiteral("%1%").arg(m_guestContextPct)
                                                                : QStringLiteral("an unknown share"),
                                         m_guestBusy ? QStringLiteral(" · a turn is running") : QString()));
        m_guestChip->show();
    }

    // The question bar floats over the terminal's top-left, opposite the program banner's
    // top-right (placeTakeControl), so both can be up at once without overlapping.
    void placeGuestBar() {
        if (!m_guestBar || !m_guestBar->isVisible() || !m_terminalHost) return;
        const QRect host(m_terminalHost->mapTo(this, QPoint(0, 0)), m_terminalHost->size());
        const QSize size = m_guestBar->sizeHint();
        const int barWidth = std::min(size.width(), std::max(240, host.width() - 20));
        m_guestBar->setGeometry(host.left() + 10, host.top() + 8, barWidth, size.height());
        m_guestBar->raise();
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
            // A ✦ subagent line: its tab in the subagent pane (card #WD83).
            if (url.host() == QStringLiteral("subagent") && parts.size() == 2) {
                openSubagent(QUrl::fromPercentEncoding(parts.at(1).toUtf8()));
                return;
            }
            if (url.host() == QStringLiteral("turn") && parts.size() == 2 && onOpenTurn) {
                onOpenTurn(QUrl::fromPercentEncoding(parts.at(1).toUtf8()));
                return;
            }
            // A tool-call line whose click is not a fold: the file, the diff pane, the subagent or
            // the card its label named (#TK9C, § 23.6). relay://call/ never arrives here — the
            // engine's fold layer takes those before anything else sees them.
            if (url.host() == QStringLiteral("open-call") && parts.size() == 3) {
                openCallTarget(target);
                return;
            }
            // The last row of an "updated tasks" fold (card #BDXG): the task list, which is where
            // those tasks stand now. It is the mouse path to a panel with a key, so it teaches it.
            if (url.host() == QStringLiteral("tasks")) {
                openRequests();
                if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.requests"));
                    !keys.isEmpty() && requestsOpen())
                    hint(QStringLiteral("tasks.fold"),
                         relay::ShortcutHints::nextTime(keys, QStringLiteral("task list")));
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
    // Options › Terminal changed something the engine view reads once (links at rest, copy on
    // select): read it again, in place.
    void applyTerminalSettings() { if (auto *engine = dynamic_cast<relay::EngineBackend *>(m_backend)) engine->applySettings(); }
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
        m_lastBlock = relay::gaps::Block::None;   // a cleared screen does not open with a blank line
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
    // Run `command` once this pane's shell is at its prompt: now if it is, else queued like a
    // command typed while the terminal is busy. "Connect to host…" and "Split on the same host"
    // (#S5SH) start their fresh pane with it.
    void queueCommand(const QString &command) { submitTerminal(command, false); }
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
        registerWithBridge();   // the bridge routes by workspace too (26.5)
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
        state.remoteHost = relay::panestatus::remoteHost(remoteCommandLine());   // "New pane on <host>" (#S5SH)
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
            {QStringLiteral("splitSameHost"), QStringLiteral("ssh.splitSameHost")},
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
        if (onWindowAction) onWindowAction(id);   // splitRight, splitDown, splitSameHost, close
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
    // A guest session (protocol 26.7) resumed in a new pane: the guest, the tool's own arguments
    // (`-r <id>`, `resume <id>`…) and the directory it must run in (empty keeps this pane's). The
    // new pane launches it through its own `launchGuest` (26.9), so it is configured like a picked one.
    std::function<void(const QString &guest, const QStringList &extra, const QString &cwd)> onOpenGuestPane;
    std::function<void()> onShowAgents;                                    // subagents panel (GUI E2), if present

    // Relay's four levels, in order. This is the vocabulary a *stored* value may hold — a pane, a
    // tier or a role keeps the level it was set to even on a provider that cannot tell it from its
    // neighbour — so validation reads this list and every picker reads offeredEfforts() below.
    static QStringList efforts() { return {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("max")}; }

    // What this pane's provider can actually be asked for: presets.py `effort_levels`, sent per
    // preset with the `presets` event. Kimi and GLM send the same request for medium and high, so
    // they offer three levels; Relay Free caps at medium; Anthropic and MiniMax have no effort knob
    // at all and offer none. Empty means "this provider has no reasoning setting"; a provider Relay
    // has never heard of falls back to the four.
    QStringList offeredEfforts() const { return effortsFor(m_currentPreset); }
    QStringList effortsFor(const QString &presetId) const {
        const QJsonObject preset = presetById(presetId);
        if (!preset.contains(QStringLiteral("efforts"))) return efforts();
        QStringList levels;
        for (const auto &value : preset.value(QStringLiteral("efforts")).toArray()) levels << value.toString();
        return levels;
    }
    // One line saying what happens to the levels this provider does not offer ("medium is sent as
    // high."), or empty when it offers all four.
    QString effortNote() const {
        return presetById(m_currentPreset).value(QStringLiteral("effort_note")).toString();
    }
    // The offered level a stored one lands on: itself when it is offered, otherwise its nearest
    // neighbour among Relay's four, ties going up. That is the provider's own mapping every time —
    // GLM sends medium as high, Relay Free sends high and max as medium — so a picker shows the
    // request the pane will actually make without having to carry the mapping table around.
    static QString nearestEffort(const QStringList &offered, const QString &level) {
        if (offered.isEmpty() || offered.contains(level)) return level;
        int want = efforts().indexOf(level);
        if (want < 0) want = efforts().indexOf(QStringLiteral("high"));
        QString best;
        int bestDistance = -1;
        for (const QString &candidate : offered) {
            const int distance = qAbs(efforts().indexOf(candidate) - want);
            if (bestDistance < 0 || distance < bestDistance
                || (distance == bestDistance && efforts().indexOf(candidate) > efforts().indexOf(best))) {
                best = candidate;
                bestDistance = distance;
            }
        }
        return best;
    }
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
    // Alt+. / Alt+, walk the levels this provider offers, not Relay's four: on GLM the step from low
    // is high, because medium there is the same request as high.
    void effortStep(int delta) {
        const QStringList levels = offeredEfforts();
        if (levels.isEmpty()) {
            toast(QStringLiteral("This model has no reasoning setting"));
            return;
        }
        int index = levels.indexOf(nearestEffort(levels, m_effort));
        if (index < 0) index = levels.indexOf(nearestEffort(levels, QStringLiteral("high")));
        index = std::clamp(index + delta, 0, int(levels.size()) - 1);
        if (levels.at(index) != m_effort) setEffort(levels.at(index));
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
    // /resume, Ctrl+Shift+Y and the palette's Resume: the session manager pane (card #R6J0), which
    // replaced the resume picker. "/resume words" opens it searching for them.
    void openResume(const QString &query = QString()) { openConversations(query); }
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

    // ----- the agent asks the user something (#MQ9C, protocol 27) --------------------------
    // The worker cannot draw, so a question is a round trip: `question` comes in, the pane prints
    // the card and waits, the prompt box takes the answer, `question_answer` goes back and the
    // agent's turn — which has been blocked on it all this time — carries on.
    //
    // It is printed into the terminal rather than put in a widget over it, in the amber the rest
    // of Relay's "needs you" marks use (#4E13: "questions or items needing human response could be
    // in bold amber"). The keyboard is enough: with options, "2" picks one and "1,3" picks two;
    // without them the question is open and whatever you type is the answer; `/skip` leaves any
    // question unanswered, and so does "0" when there are numbers to be clear of. Questions in one
    // call are asked one at a time; answering the last one sends them all back together.
    // One move of the running turn off this pane's own model (C5). `why` is what moved it —
    // "plan", "vision" or "failover" — which picks the mark the picker draws and the sentence its
    // tooltip writes; `backTo` is the model the worker said the turn returns to, which for a
    // nested move is the model outside it rather than the pane's own.
    struct Serving {
        QString model;
        QString preset;
        QString why;
        QString backTo;
    };

    struct Ask {
        QString id;
        QJsonArray questions;
        QList<QStringList> answers;
        int current = -1;
        bool open() const { return current >= 0 && current < questions.size(); }
    };

    bool questionOpen() const { return m_ask.open(); }

    void showQuestion(const QJsonObject &event) {
        // A second card can only mean the first one is stale (a worker restart, a turn that was
        // stopped between the emit and the reply): drop it rather than stack them. Dropping it is
        // not enough on its own, though — whoever asked the first question is still blocked on an
        // answer to *that* id, and nobody can type one once the card is gone — so the superseded
        // id is answered with nothing first and its turn carries on. Protocol §27 has one question
        // open at a time, so this should never fire; it costs a line and it cannot deadlock.
        // A repeat of the same id is a redraw, not a supersession, and is not answered away.
        const QString incoming = event.value(QStringLiteral("id")).toString();
        if (m_ask.open()) {
            const QString superseded = m_ask.id;
            closeQuestion(QString());
            if (!superseded.isEmpty() && superseded != incoming)
                send({{"type", "question_answer"}, {"id", superseded}, {"answers", QJsonArray()}});
        }
        m_ask = Ask{};
        m_ask.id = incoming;
        m_ask.questions = event.value(QStringLiteral("questions")).toArray();
        if (m_ask.id.isEmpty() || m_ask.questions.isEmpty()) {
            // A card that cannot be drawn would otherwise leave the worker's turn blocked on an
            // answer nobody can type. Say so where the card would have been, and answer nothing:
            // the tool reports every question unanswered and the turn carries on.
            const QString id = m_ask.id;
            m_ask = Ask{};
            ensureLineStart();
            printInline(QStringLiteral("The agent asked a question this pane could not read; "
                                       "it was left unanswered.\n"), Ink::Error);
            closeInline();
            if (!id.isEmpty()) send({{"type", "question_answer"}, {"id", id}, {"answers", QJsonArray()}});
            refreshBackgroundWait();
            changed();
            return;
        }
        for (int i = 0; i < m_ask.questions.size(); ++i) m_ask.answers.append(QStringList());
        m_ask.current = 0;
        printQuestion();
        if (!watched())
            notify(QStringLiteral("Agent needs you"), questionAt(0).value(QStringLiteral("question")).toString(),
                   relay::NotificationCenter::kindWarning);
        refreshBackgroundWait();
        changed();
        focusInput();
    }

    // The worker took the card away: Stop, or the turn ending under it.
    void closeQuestion(const QString &reason) {
        if (!m_ask.open()) { m_ask = Ask{}; return; }
        m_ask = Ask{};
        ensureLineStart();
        printInline(reason.isEmpty() ? QStringLiteral("Question withdrawn\n")
                                     : QStringLiteral("Question withdrawn · %1\n").arg(reason), Ink::Note);
        closeInline();
        refreshBackgroundWait();
        changed();
    }

private:
    QJsonObject questionAt(int index) const {
        return m_ask.questions.at(index).toObject();
    }

    static QJsonArray questionOptions(const QJsonObject &question) {
        return question.value(QStringLiteral("options")).toArray();
    }

    // The card for the question being asked now.
    void printQuestion() {
        const QJsonObject question = questionAt(m_ask.current);
        const QJsonArray options = questionOptions(question);
        const bool multiple = question.value(QStringLiteral("multiple")).toBool();
        ensureLineStart();
        const QString header = question.value(QStringLiteral("header")).toString();
        const QString counter = m_ask.questions.size() > 1
            ? QStringLiteral(" (%1 of %2)").arg(m_ask.current + 1).arg(m_ask.questions.size()) : QString();
        printInline(QStringLiteral("? %1%2 · %3\n").arg(header, counter,
                                                        question.value(QStringLiteral("question")).toString()),
                    Ink::Ask);
        for (int i = 0; i < options.size(); ++i) {
            const QJsonObject option = options.at(i).toObject();
            const bool recommended = option.value(QStringLiteral("recommended")).toBool();
            printInline(QStringLiteral("  %1  %2%3\n").arg(i + 1).arg(option.value(QStringLiteral("label")).toString(),
                                                                     recommended ? QStringLiteral("   ← recommended")
                                                                                 : QString()),
                        Ink::Ask);
            const QString description = option.value(QStringLiteral("description")).toString();
            if (!description.isEmpty())
                printInline(QStringLiteral("     %1\n").arg(description), Ink::Note);
        }
        if (!options.isEmpty()) printInline(QStringLiteral("  0  Skip this one\n"), Ink::Note);
        printInline(options.isEmpty()
                        ? QStringLiteral("  Type your answer · /skip or Esc passes · Ctrl+Shift+Enter still runs a command.\n")
                        : (multiple
                               ? QStringLiteral("  Numbers (\"1,3\"), or your own words · 0 or Esc skips · Ctrl+Shift+Enter still runs a command.\n")
                               : QStringLiteral("  A number, or your own words · 0 or Esc skips · Ctrl+Shift+Enter still runs a command.\n")),
                    Ink::Note);
        // Esc is the card's skip now, not the turn's Stop (owner, 2026-09-19), so the footer says
        // where Stop went. It has to: Esc was the only key that stopped a turn unless the user
        // bound `agent.stop` themselves, and a card that quietly took the one Stop key away would
        // leave a turn nobody could end.
        printInline(QStringLiteral("  %1.\n").arg(stopTurnHint()), Ink::Note);
        closeInline();
    }

    // The turn's Stop, named the way the user can actually reach it: the shortcut they bound to
    // `agent.stop`, or — with nothing bound, which is the default — the action itself. Read live
    // from the Keymap, as every other key Relay prints is.
    static QString stopTurnHint() {
        const QString stop = Keymap::instance().shortcutText(QStringLiteral("agent.stop"));
        return stop.isEmpty() ? QStringLiteral("Actions \u203A Stop agent stops the turn")
                              : QStringLiteral("%1 stops the turn").arg(stop);
    }

    // What the prompt box says while a card is up. Longest first, for a narrow pane.
    QStringList questionPlaceholders() const {
        if (!m_ask.open()) return {};
        const QJsonObject question = questionAt(m_ask.current);
        const int count = questionOptions(question).size();
        const QString header = question.value(QStringLiteral("header")).toString();
        if (count == 0)
            return {QStringLiteral("%1 · type your answer, or /skip").arg(header),
                    QStringLiteral("type your answer, or /skip"),
                    QStringLiteral("answer the question")};
        return {QStringLiteral("%1 · 1–%2, 0 to skip, or your own words").arg(header).arg(count),
                QStringLiteral("1–%1, 0 to skip, or your own words").arg(count),
                QStringLiteral("1–%1, or your own words").arg(count),
                QStringLiteral("answer the question")};
    }

    // The text in the prompt box, read as an answer to the question on screen. `/skip` always
    // skips. With options, numbers pick them and "0" skips; anything else is the user's own words,
    // which is the whole point of not making this a button row. An open question has no numbers to
    // read: it is all own words. The decision itself is `relay::ask::read`, above the class.
    relay::ask::Reading readAnswer(const QString &text) const {
        const QJsonObject question = questionAt(m_ask.current);
        QStringList labels;
        const QJsonArray options = questionOptions(question);
        for (const QJsonValue &option : options)
            labels.append(option.toObject().value(QStringLiteral("label")).toString());
        return relay::ask::read(text, labels, question.value(QStringLiteral("multiple")).toBool());
    }

    // Enter in the prompt box while a card is up. Returns false when there is no card, so the
    // ordinary routing runs. `author` is the person a line from a paired device came from; empty
    // for the desk, whose answers are unsigned because there is only one of them.
    bool answerQuestion(const QString &text, const QString &author = QString()) {
        if (!m_ask.open()) return false;
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) return true;                  // an empty box answers nothing
        const QJsonObject question = questionAt(m_ask.current);
        const relay::ask::Reading reading = readAnswer(trimmed);
        if (reading.kind == relay::ask::Reading::NoSuchOption) {
            // A number the card has no option for: name it and print the card again, rather than
            // send the model "4" as the answer they gave. Nothing is recorded; the wait goes on.
            ensureLineStart();
            printInline(QStringLiteral("There is no option %1 · answer with 1–%2, 0 to skip, or your own words\n")
                            .arg(reading.missing).arg(questionOptions(question).size()), Ink::Ask);
            closeInline();
            printQuestion();
            focusInput();
            changed();
            return true;
        }
        const QStringList answer = reading.kind == relay::ask::Reading::Skip ? QStringList() : reading.labels;
        // The slow path for this card is typing an option out in full when its number would do
        // (WARP.md's standing rule). Only when the words are exactly an option: a real answer in
        // the user's own words is the tool working as intended, not something to correct.
        for (int i = 0; i < questionOptions(question).size(); ++i)
            if (questionOptions(question).at(i).toObject().value(QStringLiteral("label")).toString()
                    .compare(trimmed, Qt::CaseInsensitive) == 0) {
                hint(QStringLiteral("question.number"),
                     QStringLiteral("Next time: just type %1").arg(i + 1));
                break;
            }
        recordAnswer(question, answer, author);
        return true;
    }

    // Esc while a card is up (owner, 2026-09-19): this one question is skipped and the next goes
    // up, which is exactly what typing `0` or `/skip` does. Returns false when there is no card,
    // so Esc goes on stopping the turn everywhere else.
    bool skipQuestion() {
        if (!m_ask.open()) return false;
        recordAnswer(questionAt(m_ask.current), QStringList());
        return true;
    }

    // One question answered (or skipped): recorded, echoed under the card, and the next one put
    // up — or, when that was the last, every answer sent back together. Every door into the card
    // ends here, so Esc, the prompt box and a line from a paired device leave the same trace.
    void recordAnswer(const QJsonObject &question, const QStringList &answer, const QString &author = QString()) {
        m_ask.answers[m_ask.current] = answer;
        ensureLineStart();
        // Signed when it came from somewhere else: on a shared or paired pane the person at the
        // desk should be able to see that the answer in their transcript is not theirs.
        printInline(QStringLiteral("✦ %1: %2%3\n").arg(question.value(QStringLiteral("header")).toString(),
                                                     answer.isEmpty() ? QStringLiteral("skipped")
                                                                      : answer.join(QStringLiteral(", ")),
                                                     author.trimmed().isEmpty()
                                                         ? QString()
                                                         : QStringLiteral(" · from %1").arg(author.trimmed())),
                    Ink::UserAgent);
        closeInline();
        ++m_ask.current;
        if (m_ask.open()) { printQuestion(); refreshBackgroundWait(); return; }
        sendAnswers();
    }

    void sendAnswers() {
        QJsonArray answers;
        for (const QStringList &answer : std::as_const(m_ask.answers)) {
            QJsonArray one;
            for (const QString &label : answer) one.append(label);
            answers.append(one);
        }
        send({{"type", "question_answer"}, {"id", m_ask.id}, {"answers", answers}});
        m_ask = Ask{};
        refreshBackgroundWait();
        changed();
    }

public:

    // Settings changed in Actions › Agent options.
    // Options › Security: whether a program may write the system clipboard (OSC 52, card #3KB7).
    // Applied when the backend is built and again whenever the setting changes.
    void applyClipboardPolicy() {
        if (!m_backend || !terminalCan(relay::TerminalBackend::ClipboardWrite)) return;
        m_backend->setClipboardWriteAllowed(
            QSettings().value(QStringLiteral("security/clipboard_write"), false).toBool());
    }

    void agentOptionsChanged(const QString &key) {
        // Voice settings are the GUI's own: the recorder, the chip and its tooltip, never the agent.
        if (key.startsWith(QStringLiteral("voice/"))) { updateVoiceChip(); return; }
        // The clipboard switch is the terminal's, not the worker's: it never leaves the GUI.
        if (key == QStringLiteral("security/clipboard_write")) { applyClipboardPolicy(); return; }
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
        // The reasoning fold reads it live: nothing to reconfigure, nothing to send.
        if (key == QStringLiteral("agent/thinking_display")) return;
        // Turn limits and the request audit apply to the running agent at once (protocol 12.1).
        if (key == QStringLiteral("agent/max_steps") || key == QStringLiteral("agent/max_tool_calls")
            || key == QStringLiteral("agent/stall_timeout_s") || key == QStringLiteral("agent/audit_requests")
            || key == QStringLiteral("agent/failover") || key == QStringLiteral("agent/failover_hosted")
            || key.startsWith(QStringLiteral("security/"))) {
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

    // ----- subagents UI (running-agents list, the tabbed subagent pane) ------------------------
    // Opens the subagent's tab in this pane's subagent pane (card #WD83): RelayWindow::openSubagentTab
    // splits one beside this pane the first time, and after that brings it forward on that tab.
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
        if (id.isEmpty() || !onOpenSubagent) return;
        onOpenSubagent(id);
    }
    // The subagent pane key (agent.subagentPane) from this pane: the open pane on its current tab,
    // else the row selected in the list, else the first running subagent, else the first one.
    void openSubagentPane() {
        if (m_subagentTabs && m_subagentTabs->count() > 0) { openSubagent(m_subagentTabs->currentId()); return; }
        QString id = m_agentsPanel ? m_agentsPanel->selectedId() : QString();
        for (const auto &row : m_subagents.rows()) if (id.isEmpty() && row.live()) id = row.id;
        if (id.isEmpty() && !m_subagents.isEmpty()) id = m_subagents.rows().first().id;
        if (id.isEmpty()) { status(QStringLiteral("No subagents in this pane.")); return; }
        openSubagent(id);
    }
    relay::SubagentTabsView *subagentTabs() const { return m_subagentTabs; }
    // Whether showSubagentTab(id) has something to show: a row in the list or a tab already open.
    bool canShowSubagent(const QString &id) const {
        return m_subagents.row(id) || (m_subagentTabs && m_subagentTabs->tab(id));
    }
    // The window made (or restored) this pane's subagent pane: tabs subscribe through this pane,
    // and the list folds to one line while it is open.
    void adoptSubagentTabs(relay::SubagentTabsView *tabs) {
        if (!tabs || m_subagentTabs == tabs) return;
        m_subagentTabs = tabs;
        tabs->setOwnerKey(m_scrollbackId);
        tabs->setCwd(m_cwd);
        tabs->setBackKeys(Keymap::instance().shortcutText(QStringLiteral("agent.subagentPane")));
        QPointer<Pane> self(this);
        tabs->onViewCreated = [self](relay::SubagentTranscriptView *view) { if (self) self->attachSubagentView(view); };
        // Closing a finished agent's tab dismisses its row too (dismissing a row closes its tab);
        // a running agent keeps running and keeps its row.
        tabs->onUserClosed = [self](const QString &id) {
            if (!self) return;
            if (const auto *row = self->m_subagents.row(id); row && !row->live()) self->m_subagents.dismiss(id);
        };
        tabs->onBackClicked = [self] {
            if (self) self->hint(QStringLiteral("subagents.back.mouse"),
                                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("agent.subagentPane")),
                                                                QStringLiteral("back to the main agent (Esc works too)")));
        };
        const QObject *gone = tabs;
        connect(tabs, &QObject::destroyed, this, [this, gone] {
            if (m_closing || (m_subagentTabs && m_subagentTabs.data() != gone)) return;
            m_subagentTabs = nullptr;
            foldAgentsStrip();
        });
        // Brought back with Ctrl+Shift+Z while its agents are still listed: those tabs go live again.
        const QString current = tabs->currentId();
        for (const QString &id : tabs->ids())
            if (auto *view = tabs->tab(id); view && view->ended() && m_subagents.row(id)) tabs->showTab(id, true);
        if (!current.isEmpty()) tabs->showTab(current, m_subagents.row(current) != nullptr);
        tabs->syncRows(m_subagents);
        foldAgentsStrip();
    }
    // Opens (or selects) the tab; a restored tab whose agent is gone is only selected.
    relay::SubagentTranscriptView *showSubagentTab(const QString &id) {
        if (!m_subagentTabs || !canShowSubagent(id)) return nullptr;
        auto *view = m_subagentTabs->showTab(id, m_subagents.row(id) != nullptr);
        m_subagentTabs->syncRows(m_subagents);
        return view;
    }
    // Subscribes the view to the subagent's stream; unsubscribes when the last view for it closes.
    void attachSubagentView(relay::SubagentTranscriptView *view) {
        const QString id = view->agentId();
        m_subagentViews.append(view);
        QPointer<Pane> self(this);
        view->onSend = [self, id](const QString &text) {
            if (self) self->send({{"type", "agent_message"}, {"id", id}, {"text", text}});
        };
        // A subagent's own tool line whose diff is more than 12 changed lines opens the diff pane,
        // exactly as the terminal's and the turn pane's do (#TK9C, § 23.6).
        view->onOpenDiff = [self](const QString &title, const QString &unifiedDiff) {
            if (self && self->onOpenDiff) self->onOpenDiff(title, unifiedDiff);
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
        placeGuestBar();
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
        // Answered with the mouse while Y and N were right there: the hint is owed (WARP.md's
        // standing rule). Recorded on the press rather than on `clicked`, because Space on the
        // focused button is `clicked` too and is not the slow path.
        if (event->type() == QEvent::MouseButtonPress && guestBarOwns(object)) m_guestBarMouse = true;
        // A guest's permission question has the keyboard while it is up (GT7X, 26.4). Only the
        // bar's own widgets are filtered, so the terminal keeps every key when it is not.
        if (event->type() == QEvent::KeyPress && guestBarOwns(object)
            && guestBarKey(static_cast<QKeyEvent *>(event)))
            return true;
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
        // The program transcript is a read-only text view, so its copy on select is the shared
        // filter (src/CopyOnSelect.h) installed where it is built. Ctrl+C in the prompt box still
        // copies the terminal's selection (copySelection).
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
            // The × at the right edge of a queue row removes it; on a steer it withdraws it. A steer
            // already being withdrawn has no × to click.
            const QPoint pos = static_cast<QMouseEvent *>(event)->pos();
            const QModelIndex index = m_queueList->indexAt(pos);
            if (index.isValid() && pos.x() >= m_queueList->viewport()->width() - 26) {
                if (index.data(QueueRowDelegate::PendingRole).toBool()) return true;
                const bool steer = index.data(QueueRowDelegate::KindRole).toString() == QStringLiteral("steer");
                removeRow(index.data(QueueRowDelegate::RowIdRole).toString());
                if (steer) hint(QStringLiteral("queue.steer.remove.mouse"), relay::ShortcutHints::nextTime(QStringLiteral("↑ then Shift+Delete")));
                else hint(QStringLiteral("queue.remove.mouse"), QStringLiteral("Next time: ↑ opens a queued item in the prompt box, Shift+Delete removes, Ctrl+↑/↓ reorders"));
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
        if (event->type() == QEvent::KeyPress && ownsKeyboard() && !static_cast<QKeyEvent *>(event)->isAutoRepeat()) {
            // A real keystroke, not a modifier being held down on its own: the owner is typing
            // into this pane, so nobody else is driving it any more (section 10.3). The event is
            // passed on untouched — taking control back must never cost the key that did it.
            switch (static_cast<QKeyEvent *>(event)->key()) {
            case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Alt: case Qt::Key_Meta:
            case Qt::Key_AltGr: case Qt::Key_CapsLock: case Qt::Key_NumLock: break;
            default: takeBackFromGuest();
            }
        }
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
        // half. The minimum goes, and the text is elided from the left in updateHeader() to
        // exactly the room the header has left, so the label asks for no more than it shows.
        // Letting the layout do the squeezing instead cut the path mid-glyph — a crowded header
        // ended in a stray half of a character where the directory should be. The tooltip has
        // both paths in full whatever is shown.
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
            modelBoxPicked(m_modelBox->itemData(index).toString());
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
        connect(m_share, &QToolButton::clicked, this, [this] { shareChipPressed(); });
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
        // Up and Down walk this pane's own history, kept in a file under the pane's layout id so
        // it survives a restart and a close-and-reopen, and stays this pane's alone (owner report,
        // 2026-09-19: "the up/down history seems to be getting commands from other panes, not just
        // mine"). src/PromptHistory.h has the rules; initRestore() re-points this at the saved id
        // when the pane is being restored.
        m_editor->useHistoryFile(promptHistoryPath());
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
        // The "Relaying…" line (cards #4E13, #HQ2B), the first row of the composer frame so native
        // mode hides it with the prompt box: agent work in the agent's violet, saying what it is
        // doing right now ("Relaying reading src/Pane.h… · 12 s · Esc stops"), a terminal program
        // in the terminal's blue ("Relaying sleep…"), left-aligned with the prompt text and in the
        // normal weight above the prompt. The turn clock lived in the strip under the box until
        // #4E13; it moved up here and was restyled into this line — one place, one verb, the
        // colour saying whose work it is.
        m_busyLine = new PaneBusyLine(composer);
        m_busyLine->setPromptEditor(m_editor);   // the row's left edge is the prompt text's (#HQ2B)
        composerLayout->addWidget(m_busyLine);
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
        // A guest's permission question (GT7X, 26.4): the PermissionRequest hook holds the shim open,
        // and this bar is where the user answers it. The same banner idiom, the opposite
        // corner, so a question and the program banner never sit on top of each other.
        m_guestBar = new QFrame(this);
        m_guestBar->setObjectName(QStringLiteral("programBanner"));
        m_guestBar->setAttribute(Qt::WA_StyledBackground);
        auto *questionRow = new QHBoxLayout(m_guestBar);
        questionRow->setContentsMargins(10, 4, 6, 4);
        questionRow->setSpacing(8);
        m_guestBarLabel = new QLabel(m_guestBar);
        m_guestBarLabel->setObjectName(QStringLiteral("programBannerLabel"));
        m_guestBarLabel->setTextFormat(Qt::PlainText);
        questionRow->addWidget(m_guestBarLabel, 1);
        // Unlike the program banner's chips these take the keyboard: the guest is blocked on the
        // answer, and a question that can only be answered with the mouse is one a touch-typist
        // cannot answer at all. showGuestQuestion() focuses Allow; guestBarKey() adds Y/N/Enter/Esc
        // and hands any other key straight back to the pane's own input.
        m_guestAllow = new QPushButton(m_guestBar);
        m_guestAllow->setObjectName(QStringLiteral("delegateChip"));
        m_guestAllow->setCursor(Qt::PointingHandCursor);
        m_guestAllow->setFocusPolicy(Qt::StrongFocus);
        m_guestAllow->setText(QStringLiteral("Allow"));
        m_guestAllow->setToolTip(QStringLiteral("Allow this tool call · Y or Enter"));
        m_guestAllow->installEventFilter(this);
        connect(m_guestAllow, &QPushButton::clicked, this, [this] { answerGuestPermission(true); });
        questionRow->addWidget(m_guestAllow);
        m_guestDeny = new QPushButton(m_guestBar);
        m_guestDeny->setObjectName(QStringLiteral("takeControlChip"));
        m_guestDeny->setCursor(Qt::PointingHandCursor);
        m_guestDeny->setFocusPolicy(Qt::StrongFocus);
        m_guestDeny->setText(QStringLiteral("Deny"));
        m_guestDeny->setToolTip(QStringLiteral("Deny this tool call · N or Esc"));
        m_guestDeny->installEventFilter(this);
        connect(m_guestDeny, &QPushButton::clicked, this, [this] { answerGuestPermission(false); });
        questionRow->addWidget(m_guestDeny);
        m_guestBar->setFocusPolicy(Qt::StrongFocus);
        m_guestBar->installEventFilter(this);
        m_guestBar->hide();
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
        // The guest agent's context (GT7X, 26.3): its model and context share, fed by the
        // statusline shim through the guest event channel. Hidden without a guest, like every
        // chip here that has nothing to say.
        m_guestChip = new QLabel;
        m_guestChip->setObjectName(QStringLiteral("stripChipLabel"));
        m_guestChip->setAccessibleName(QStringLiteral("Guest agent context"));
        m_guestChip->setTextFormat(Qt::PlainText);
        m_guestChip->setMinimumWidth(1);
        m_guestChip->hide();
        row->addWidget(m_guestChip);
        // Relay Free's allowance (protocol 13.9): "Free · 73% left", the same chip idiom as the
        // context bar beside it, shown only while this pane's main preset is the hosted one.
        m_quotaLabel = new QLabel;
        m_quotaLabel->setObjectName(QStringLiteral("stripChipLabel"));
        m_quotaLabel->setAccessibleName(QStringLiteral("Relay Free allowance"));
        m_quotaLabel->setTextFormat(Qt::PlainText);
        m_quotaLabel->hide();
        row->addWidget(m_quotaLabel);
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
        updateQuotaLabel();
        if (!m_effortBox) return;
        const QSignalBlocker block(m_effortBox);
        // The levels follow the pane's provider, so the box never offers a request this endpoint
        // cannot make; the pane's own level is shown as the level it is sent as.
        const QStringList levels = offeredEfforts();
        QStringList shown;
        for (int i = 0; i < m_effortBox->count(); ++i) shown << m_effortBox->itemData(i).toString();
        if (shown != levels) {
            m_effortBox->clear();
            for (const QString &level : levels) m_effortBox->addItem(level, level);
        }
        m_effortBox->setCurrentIndex(std::max(0, m_effortBox->findData(nearestEffort(levels, m_effort))));
        m_planChip->setVisible(m_agentMode == QStringLiteral("plan"));
        updateContextLabel();
    }

    // ----- Relay Free (protocol 13.8/13.9) ------------------------------------------------------
    // Whether this pane's main preset is the hosted one. Its Flash and Lite tiers are the gateway's
    // other roles, so a pane on the Flash agent is still on Relay Free.
    bool onHostedPreset() const { return presetById(m_currentPreset).value(QStringLiteral("hosted")).toBool(); }

    // Every stored row that is not Relay Free: what the exhausted line offers instead. A local
    // endpoint counts — it answers without a key — but only a real key counts as "a key stored",
    // which is what decides whether the dialog offering to add one appears.
    QStringList ownProviderLabels() const {
        QStringList labels;
        for (const auto &entry : std::as_const(m_stored))
            if (!presetById(entry.first).value(QStringLiteral("hosted")).toBool()) labels << entry.second;
        return labels;
    }
    bool anyOwnKeyStored() const {
        for (const auto &item : m_presets)
            if (item.toObject().value(QStringLiteral("has_stored_key")).toBool()) return true;
        return false;
    }

    static QString localClock(qint64 unixSeconds) {
        return QDateTime::fromSecsSinceEpoch(unixSeconds).toLocalTime().toString(QStringLiteral("HH:mm"));
    }

    // The last allowance seen: from the `presets` row before the first call, then from every
    // `hosted_quota` event. The chip and the keys modal both read it.
    void setHostedQuota(const QJsonObject &quota) {
        m_quotaLimit = quota.value(QStringLiteral("limit")).toVariant().toLongLong();
        m_quotaUsed = quota.value(QStringLiteral("used")).toVariant().toLongLong();
        m_quotaResets = quota.value(QStringLiteral("resets_at")).toVariant().toLongLong();
        // A fresh allowance retires the offer to add a key: the next exhaustion may make it again.
        if (m_quotaLimit > 0 && m_quotaUsed < m_quotaLimit) m_hostedOfferShown = false;
        updateQuotaLabel();
    }

    void updateQuotaLabel() {
        if (!m_quotaLabel) return;
        if (!onHostedPreset()) { m_quotaLabel->hide(); return; }
        if (m_quotaLimit <= 0) {
            // No figure yet: the first request through the gateway brings one.
            m_quotaLabel->setText(QStringLiteral("Free"));
            m_quotaLabel->setProperty("warn", false);
            m_quotaLabel->setToolTip(QStringLiteral("Relay Free: an included daily allowance. Usage shows after the first request."));
        } else {
            // Rounded down: an allowance with one call spent must not read as untouched.
            const double left = std::max(0.0, 100.0 * double(m_quotaLimit - m_quotaUsed) / double(m_quotaLimit));
            m_quotaLabel->setText(QStringLiteral("Free · %1% left").arg(left > 0 && left < 10 ? QString::number(std::floor(left * 10) / 10, 'f', 1)
                                                                                           : QString::number(std::floor(left), 'f', 0)));
            m_quotaLabel->setProperty("warn", left <= 10.0);
            m_quotaLabel->setToolTip(QStringLiteral("%1 of %2 tokens today%3")
                .arg(QLocale().toString(std::min(m_quotaUsed, m_quotaLimit)), QLocale().toString(m_quotaLimit),
                     m_quotaResets > 0 ? QStringLiteral(" · resets at %1").arg(localClock(m_quotaResets)) : QString()));
        }
        m_quotaLabel->style()->unpolish(m_quotaLabel); m_quotaLabel->style()->polish(m_quotaLabel);
        m_quotaLabel->show();
    }

    // The first time any pane runs on Relay Free: one line in the transcript saying where the
    // prompts go, once per installation (QSettings hosted/disclosed). Not a modal — the target flow
    // is "open → ask → it works"; the keys modal and Options › Privacy carry the full text.
    void discloseHosted() {
        if (!onHostedPreset()) return;
        QSettings settings;
        if (settings.value(QStringLiteral("hosted/disclosed"), false).toBool()) return;
        settings.setValue(QStringLiteral("hosted/disclosed"), true);
        ensureLineStart();
        printInline(QStringLiteral("Relay Free: this pane's prompts and tool context go to Relay's hosted service and on to "
                                   "the model provider; Relay keeps request metadata only. Add your own key under "
                                   "Options › Models to keep it between you and your provider.\n"), Ink::Note);
        if (!m_agentBusy && !moreTurnsPending()) closeInline();
    }

    // A turn refused by the gateway for the day (`code` quota_exhausted / free_unavailable, protocol
    // 13.9): the line names what to do instead, and when nothing of the user's own is stored, a
    // dialog in the offerVoiceKey shape offers the two ways to add a key — once per exhaustion.
    void onHostedRefusal(const QString &code, const QJsonObject &event) {
        const qint64 resets = event.value(QStringLiteral("resets_at")).toVariant().toLongLong();
        if (resets > 0) m_quotaResets = resets;
        if (code == QStringLiteral("quota_exhausted") && m_quotaLimit > 0) { m_quotaUsed = m_quotaLimit; updateQuotaLabel(); }
        const QString what = code == QStringLiteral("quota_exhausted")
            ? QStringLiteral("Relay Free allowance used for today%1.")
                  .arg(resets > 0 ? QStringLiteral("; resets at %1").arg(localClock(resets)) : QString())
            : QStringLiteral("Relay Free is temporarily unavailable.");
        const QStringList own = ownProviderLabels();
        const QString instead = own.isEmpty()
            ? QStringLiteral("Add a key under Options › Models › API keys… to keep going.")
            : QStringLiteral("Use one of your providers: %1 (/model), or add a key under Options › Models › API keys…")
                  .arg(own.join(QStringLiteral(", ")));
        ensureLineStart(); printInline(QStringLiteral("✗ %1 %2\n").arg(what, instead), Ink::Error);
        if (anyOwnKeyStored() || m_hostedOfferShown) return;
        m_hostedOfferShown = true;
        auto *box = new QMessageBox(window());
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setIcon(QMessageBox::Information);
        box->setWindowTitle(QStringLiteral("Relay Free"));
        box->setText(what);
        box->setInformativeText(QStringLiteral(
            "Relay Free is an included daily allowance. To keep working now, add a key of your own for any "
            "provider: it goes into the desktop keyring and requests on it never touch Relay's server."));
        QPushButton *keys = box->addButton(QStringLiteral("API keys…"), QMessageBox::AcceptRole);
        QPushButton *warp = box->addButton(QStringLiteral("Import from Warp"), QMessageBox::ActionRole);
        box->addButton(QMessageBox::Cancel);
        QPointer<Pane> self(this);
        connect(box, &QMessageBox::finished, this, [self, box, keys, warp] {
            if (!self) return;
            if (box->clickedButton() == keys) self->openKeysDialog();
            else if (box->clickedButton() == warp) self->send({{"type", "import_warp"}});
        });
        box->open();
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
        m_paneState.changed();   // pane_state (relay-terminal-71)
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
                {"audit_requests", settings.value(QStringLiteral("agent/audit_requests"), false).toBool()},
                // Whether a turn whose provider keeps failing continues on another one (#G9VE),
                // and whether Relay Free may be one of those providers (owner, 2026-09-19: opt-in,
                // because a pane on the user's own key never chose Relay's hosted service).
                {"failover", settings.value(QStringLiteral("agent/failover"), true).toBool()},
                {"failover_hosted", settings.value(QStringLiteral("agent/failover_hosted"), false).toBool()},
                // Options › Security (card #3KB7). Always sent, including empty, so clearing a list
                // in Options reaches the worker as "no rules" rather than as "unchanged".
                {"command_denylist", QJsonArray::fromStringList(settings.value(QStringLiteral("security/command_denylist")).toStringList())},
                {"readable_roots", QJsonArray::fromStringList(settings.value(QStringLiteral("security/readable_roots")).toStringList())},
                {"secret_patterns", QJsonArray::fromStringList(settings.value(QStringLiteral("security/secret_patterns")).toStringList())}};
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
        // Which Switchboard this pane's agent is on (protocol 19.1, card #JN7X). Sent on every
        // configure, including `{"attach": false}` for a tab attached to nothing: a configure
        // with *no* board block makes the worker walk up from the workspace instead, and the
        // workspace is the launch directory, which is how one project's board reached every pane.
        if (onBoardSettings) request.insert(QStringLiteral("board"), onBoardSettings());
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
    // Off by default: a tool call is one line that counts its output, and a click unfolds the whole
    // of it under that line (#TK9C). On, the stream prints under the line as it arrives.
    static bool showToolOutput() { return QSettings().value(QStringLiteral("agent/show_tool_output"), false).toBool(); }

    // The command a RUN COMMAND preview was built from, for the wrong-mode hint alone: its heading
    // and the notes every tool preview carries, dropped. Nothing else reads a preview any more
    // (§ 23.1: everything it was parsed for is a field on the label now).
    static QString runCommandFromPreview(const QString &preview) {
        QStringList lines = preview.left(6000).split(QLatin1Char('\n'));
        if (lines.isEmpty() || lines.takeFirst().trimmed() != QStringLiteral("RUN COMMAND")) return {};
        QStringList body;
        for (const QString &line : std::as_const(lines)) {
            if (line.startsWith(QStringLiteral("Working directory: ")) || line.startsWith(QStringLiteral("Timeout: "))
                || line.startsWith(QStringLiteral("Old bytes: "))) continue;
            if (body.isEmpty() && line.trimmed().isEmpty()) continue;
            body << line;
        }
        while (!body.isEmpty() && body.last().trimmed().isEmpty()) body.removeLast();
        return body.join(QLatin1Char('\n')).trimmed();
    }

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
            // A fold asked for a call the worker no longer has ("Unknown turn_id (only the last 50
            // turns are kept)"): the fold says so, in one row, rather than staying empty (#TK9C).
            if (handleFoldError(id, event.value(QStringLiteral("text")).toString())) return true;
            // A local-model save that found nothing to detect (card #24XJ): the Settings pane's
            // section says so under the row that asked, never in the transcript.
            if (id.startsWith(QStringLiteral("lm-"))) {
                if (onLocalModelEvent) onLocalModelEvent(event);
                return true;
            }
            // The ⓘ view's requests (protocol 25): the view says what went wrong, in place.
            if (id.startsWith(QStringLiteral("info-"))) {
                if (m_infoView) m_infoView->setError(id, event.value(QStringLiteral("text")).toString());
                return true;
            }
            // The × on a steer lost the race: the turn took it (or gave it back) first. The
            // transcript or the queue already shows where it went, so this is only a status line.
            if (id.startsWith(QStringLiteral("withdraw-"))) {
                if (!m_withdrawnOnReturn.remove(id.mid(9)))
                    status(QStringLiteral("Too late to withdraw · the agent already had it"));
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
            m_paneState.changed();   // pane_state (relay-terminal-71): the reasoning tail
            // "never" keeps the buffer (the turn pane still shows it) but draws nothing.
            if (thinkingDisplay() != QLatin1String("never")) thinkingDelta(turn);
            pushThinkingToTurnPane(turn);   // a turn pane open on this turn follows the stream
            return true;
        }
        if (type == QStringLiteral("thinking_done")) {
            const qint64 ms = event.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
            // The block is whole now: an open turn pane gets all of it, whatever was drawn here.
            pushThinkingToTurnPane(event.value(QStringLiteral("turn_id")).toString(), true);
            // A stream that stopped mid-reasoning reports {elapsed_ms: 0, chars: 0}: the fold still
            // gets what arrived, and its row says the stream stopped rather than lying with a time.
            if (!m_thinkingAnchor.isEmpty()) { finishThinkingFold(ms); return true; }
            // No fold is streaming ("never", a backend without folds, a program owning the screen):
            // the single ✦ line the transcript has always had.
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
        while (m_turnOrder.size() > 50) {
            const QString old = m_turnOrder.takeFirst();
            m_turnSummaries.remove(old); m_turnThinking.remove(old); m_thinkingBlocks.remove(old);
        }
            const int tools = event.value(QStringLiteral("tools")).toArray().size();
            if (tools > 0) {
                endCallRun();   // a run of reads that ended the turn gets its newline here (#TK9C)
                const qint64 ms = event.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
                printTurnLink(QStringLiteral("✦ %1 tool call%2 · %3 s").arg(tools).arg(tools == 1 ? QString() : QStringLiteral("s"))
                                  .arg(std::max<qint64>(1, (ms + 500) / 1000)), turn);
                // The lines above this one each hold their own detail. Taught once a turn has
                // printed some, with the key that opens the nearest one without the mouse.
                if (terminalFolds())
                    hint(QStringLiteral("call.fold"),
                         QStringLiteral("Click a ▸ line to unfold it here · Ctrl+Shift+Return unfolds the nearest"));
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
        // tool_output_get is marked `stored: true` (protocol 11.1). Which of the two asked for it
        // is the request id: a `fold-` reply fills a fold in the terminal and opens no pane at all.
        if (type == QStringLiteral("tool_output") && event.value(QStringLiteral("stored")).toBool()) {
            if (handleFoldReply(event)) return true;
            const QString turn = m_turnOutputRequests.take(event.value(QStringLiteral("id")).toString());
            if (!turn.isEmpty()) {
                if (auto view = m_turnViews.value(turn)) { view->setToolOutput(event); return true; }
            }
            openToolOutput(event);
            return true;
        }
        if (type == QStringLiteral("route_assisted")) {
            onRouteAssisted(event);
            return true;
        }
        // Settings › Local models (card #24XJ, protocol 28). A local endpoint has no key and no row
        // in the keys dialog, so its test result goes to the Settings pane's section instead.
        if (type.startsWith(QStringLiteral("local_"))
            || (type == QStringLiteral("key_tested")
                && event.value(QStringLiteral("preset")).toString().startsWith(QStringLiteral("local:")))) {
            if (onLocalModelEvent) onLocalModelEvent(event);
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
            // The dialog's list is the fresher one after a refine or an import: `/name` follows it.
            if (type == QStringLiteral("skills")) setSkillCommands(event.value(QStringLiteral("items")).toArray());
            if (type == QStringLiteral("skills_refined")) {
                const auto items = event.value(QStringLiteral("items")).toArray();
                if (!items.isEmpty() && onOpenDocument) onOpenDocument(items.first().toObject().value(QStringLiteral("path")).toString());
            }
            return true;
        }
        return false;
    }

    // ----- reasoning as a streaming fold (issue T8CN) ---------------------------------------------
    //
    // Reasoning streams into a fold in the terminal's own grid, under an anchor row of its own:
    // "▸ ✦ thinking…" while it arrives, rewritten in place to "▸ ✦ thought for N s" when it ends.
    // The fold opens with the first delta, so a reader who wants to watch the model think watches
    // it live; it collapses when the turn is done, so the default transcript keeps its one line
    // per event. A reader who folds it away mid-stream is not asked again: m_thinkingUserToggled
    // holds their answer for the life of the pane. The text is kept per turn whatever the display
    // mode — m_turnThinking feeds the turn pane too — and agent/thinking_display says how much of
    // this runs at all (collapse / always / never; "never" leaves the single ✦ line of the old
    // design, printed by thinking_done).

    // The first delta of a block: anchor row, fold, and its first content. Later deltas only queue
    // a flush — the fold is updated coalesced (queueThinkingFlush), not per chunk.
    void thinkingDelta(const QString &turn) {
        // A fold needs a backend that draws folds and a screen Relay may write to; without either,
        // thinking_done falls back to the single ✦ line the transcript has always had.
        if (!terminalFolds() || !inlineReady()) return;
        if (m_thinkingAnchor.isEmpty()) {
            const int block = ++m_thinkingBlocks[turn];
            m_thinkingAnchor = relay::calllines::foldUri(m_token, turn,
                block <= 1 ? QStringLiteral("thinking") : QStringLiteral("thinking-%1").arg(block));
            m_thinkingUserToggled = false;   // a new block starts unprejudiced
            m_thinkingFoldOpen = true;
            turnHeader();   // "▸ model" first, then its reasoning: one block, no gap between (#5AWD)
            printThinkingAnchor();
            setFold(m_thinkingAnchor, thinkingFoldLines(m_thinkingAnchor, true));
            const QString key = Keymap::instance().shortcutText(QStringLiteral("agent.thinkingPanel"));
            hint(QStringLiteral("thinking.fold"),
                 key.isEmpty() ? QStringLiteral("Reasoning streams under the ✦ line · a click folds it away")
                               : QStringLiteral("Reasoning streams under the ✦ line · a click or %1 folds it away").arg(key));
        }
        queueThinkingFlush();
    }

    // The anchor row itself, in the terminal at column 0 — the ghostty core only finds anchors
    // that start there — hyperlinked to the fold's URI. "▸ " is the placeholder the view
    // overpaints with ▸ or ▾, the convention every tool row uses (#TK9C).
    void printThinkingAnchor() {
        endCallRun();   // a held tool-call row ends before the anchor starts its own (#TK9C)
        beginBlock(relay::gaps::Block::Agent);   // the fold is the reply's first block (#5AWD)
        QByteArray out = takeWrapped();
        if (!m_inlineOpen) { out += "\r\x1b[2K"; m_inlineOpen = true; m_atLineStart = true; holdShellResize(true); }
        if (!m_atLineStart) out += "\r\n";
        out += "\x1b]8;;" + m_thinkingAnchor.toUtf8() + "\x1b\\";
        out += inkCode(Ink::Note) + QByteArray("▸ ✦ thinking…") + "\x1b[0m";
        out += "\x1b]8;;\x1b\\\r\n";
        m_atLineStart = true;
        writeTerminal(out);
    }

    // Fold updates are coalesced to ~4 Hz: re-rendering the whole buffer as markdown per chunk
    // would burn the CPU on a long stream, and four frames a second is more than anyone reads.
    void queueThinkingFlush() {
        if (m_thinkingFlushPending || m_thinkingAnchor.isEmpty()) return;
        m_thinkingFlushPending = true;
        QTimer::singleShot(250, this, [this] { flushThinkingFold(); });
    }

    void flushThinkingFold() {
        m_thinkingFlushPending = false;
        const QString uri = m_thinkingAnchor;
        if (uri.isEmpty() || !m_backend) return;
        // A turn pane opened on this turn — "open in pane" is what the capped fold points at — is
        // the surface holding the whole block, so it follows the stream at the same four frames a
        // second rather than freezing at whatever had arrived when it was opened.
        pushThinkingToTurnPane(turnOfFold(uri));
        // The reader may have folded it away since the last flush. setFoldContent() opens what it
        // sets, so a folded fold is left alone: pushing content would reopen it over their click.
        if (m_thinkingFoldOpen && !m_backend->foldExpanded(uri)) {
            m_thinkingFoldOpen = false;
            m_thinkingUserToggled = true;
        }
        if (!m_thinkingFoldOpen) return;
        setFold(uri, thinkingFoldLines(uri, true));
    }

    // The whole of this turn's reasoning into an open turn pane, if one is open on it. The fold in
    // the grid is capped; the pane its "open in pane" link goes to is where the rest lives, so it
    // is kept current while the block streams and once more, forced, when it ends (#K48R). Called
    // from the delta too, so a mode with no fold at all (`never`, a backend without folds) keeps an
    // open pane current as well — there the ✦ line's link is the only way to the reasoning. Re-
    // rendering the pane's log costs more than a delta does, so it is held to the fold's own 4 Hz.
    void pushThinkingToTurnPane(const QString &turn, bool force = false) {
        if (turn.isEmpty()) return;
        const auto view = m_turnViews.value(turn);
        if (!view) return;
        if (!force && m_thinkingPushAt.isValid() && m_thinkingPushAt.elapsed() < 250) return;
        m_thinkingPushAt.restart();
        view->setThinking(m_turnThinking.value(turn));
    }
    QString turnOfFold(const QString &uri) const {
        const relay::calllines::Ref ref = relay::calllines::parseUri(uri);
        return ref.valid ? ref.turn : QString();
    }

    // How wide the fold's rows are wrapped: the grid less the block indent, which is what
    // FoldLayer::layout() wraps at. The caps below count the rows the view paints, so they are
    // counted at this width (#K48R).
    int foldWrapCells() const {
        const int columns = m_backend ? m_backend->columns() : 0;
        return columns > relay::kFoldIndent + 4 ? columns - relay::kFoldIndent : 0;
    }

    // The fold's rows: the buffer rendered as markdown (src/CallLines.h, foldForMarkdown), capped
    // at the height the owner asked for (#K48R) — the last six rendered rows while it streams,
    // because the end is the part being written, and the first eighteen of a settled fold opened
    // by hand, because that is where reading starts. What is cut is named on a muted row, and the
    // link under it goes to the turn pane, which holds the whole of every block. Empty at done
    // becomes a row that says so, because a blank fold reads as a dead one.
    QVector<relay::FoldLine> thinkingFoldLines(const QString &uri, bool tail) const {
        relay::calllines::FoldOptions options;
        options.maxLines = tail ? relay::calllines::kThinkingStreamRows : relay::calllines::kThinkingDoneRows;
        const relay::calllines::Ref ref = relay::calllines::parseUri(uri);
        if (ref.valid)
            options.openInPane = QStringLiteral("relay://turn/%1/%2")
                                     .arg(m_token, QString::fromUtf8(QUrl::toPercentEncoding(ref.turn)));
        QString text = m_turnThinking.value(ref.valid ? ref.turn : QString());
        // Six rows of a stream need nothing like the whole buffer re-rendered four times a second;
        // 12,000 characters is far more than six rows at any width.
        if (tail) text = text.right(12000);
        const QVector<relay::FoldLine> lines =
            relay::calllines::foldForMarkdown(text, foldPalette(), options, foldWrapCells(), tail);
        return !lines.isEmpty() ? lines
               : tail ? QVector<relay::FoldLine>()
                      : relay::calllines::foldForNote(QStringLiteral("(No reasoning was kept for this turn.)"),
                                                       foldPalette());
    }

    // The block ended: settle the fold, collapse it unless the reader or the setting asked
    // otherwise, and rewrite the anchor row in place so the transcript's one line per event
    // holds. Runs even for a stream that stopped mid-reasoning ({chars: 0}): what arrived is
    // still worth a fold, and the row says the stream stopped rather than lying with a time.
    void finishThinkingFold(qint64 ms) {
        const QString uri = m_thinkingAnchor;
        m_thinkingAnchor.clear();
        m_lastThinkingAnchor = uri;   // Alt+R reopens this one until the next block replaces it
        m_thinkingFlushPending = false;   // a queued flush would resurrect a cleared anchor
        if (!m_backend || uri.isEmpty()) return;
        const bool openNow = m_backend->foldExpanded(uri);
        if (openNow != m_thinkingFoldOpen) m_thinkingUserToggled = true;   // the reader clicked since
        setFold(uri, thinkingFoldLines(uri, false));
        // Collapsed by default: the fold was for reading along, and the rewritten row carries the
        // summary. Kept open when the reader said so — by click, by Alt+R, or by the setting.
        const bool keepOpen = m_thinkingUserToggled ? openNow : thinkingDisplay() == QLatin1String("always");
        m_backend->setFoldExpanded(uri, keepOpen);
        m_thinkingFoldOpen = keepOpen;
        rewriteThinkingAnchor(uri, ms);
        m_paneState.changed();   // pane_state (relay-terminal-71): the reasoning tail settled
    }

    // "▸ ✦ thought for N s" over the anchor row, without moving anything else: the cursor is
    // still on the empty row under the anchor (nothing but fold content — virtual rows the grid
    // does not count — has printed since), so one row up, erase, redraw, and back. Guards for
    // every way that stops being true: the shell redrew its line over the anchor, or output
    // pushed the anchor into history.
    void rewriteThinkingAnchor(const QString &uri, qint64 ms) {
        if (!m_backend || !m_inlineOpen || !m_atLineStart) return;
        const QPoint cursor = m_backend->cursorPosition();
        if (cursor.x() != 0 || cursor.y() < 1) return;
        const QStringList rows = m_backend->screenText().split('\n');
        if (cursor.y() >= rows.size() || !rows.at(cursor.y()).isEmpty()) return;
        if (!rows.at(cursor.y() - 1).contains(QStringLiteral("✦ thinking"))) return;
        const QString label = ms > 0
            ? QStringLiteral("✦ thought for %1 s").arg(std::max<qint64>(1, (ms + 500) / 1000))
            : QStringLiteral("✦ thinking stopped");
        QByteArray out = takeWrapped() + "\x1b[A\r\x1b[2K";
        out += "\x1b]8;;" + uri.toUtf8() + "\x1b\\";
        out += inkCode(Ink::Note) + QByteArray("▸ ") + sanitize(label).toUtf8() + "\x1b[0m";
        out += "\x1b]8;;\x1b\\\r\n";
        writeTerminal(out);
    }

    // ----- the queue strip that shares the terminal's column -------------------------------------
    //
    // The queue strip is a row between the terminal host and the prompt box, so showing it moves
    // the terminal up instead of covering its last lines (owner report, 2026-09-18). Its height is
    // part of this pane's minimum height, and a splitter that cannot satisfy every minimum
    // redistributes all of its panes as soon as one minimum moves (#G152), so every show, hide and
    // height change runs with the enclosing splitters' sizes held.

    // The height the terminal and the queue strip share. Heights are measured against this rather
    // than against the terminal host alone, so showing the strip does not shrink the number the
    // next call sizes it from and leave the two chasing each other.
    int bubbleSpan() const {
        int span = m_terminalHost ? m_terminalHost->height() : 0;
        const int spacing = layout() ? layout()->spacing() : 0;
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
        // The tool lines below it fold their own detail open now (#TK9C), so the tip is about what
        // this line adds: the turn as a whole, with its reasoning and every call in one place.
        hint(QStringLiteral("turn.link"), QStringLiteral("Tip: click a ▸ line to unfold it here · the ✦ line opens the whole turn"));
    }

    // ----- one line per tool call (#TK9C, docs/AGENT-SESSIONS-PROTOCOL.md § 23) -------------------
    //
    // Every call is one row: "▸ ran pytest · 212 lines · exit 1 · 8 s". The row is an OSC 8 anchor
    // over relay://call/<pane>/<turn>/<call> — the prefix the engine's fold layer owns — so a click
    // unfolds the call's detail underneath it, in the terminal (docs/ENGINE.md, "Folds"). A line
    // whose click is not a fold (a file, a big diff, a subagent, a card) anchors relay://open-call/
    // instead, which the fold layer leaves alone and openOutputTarget routes.
    //
    // The anchor begins at column 0 with a "▸ " placeholder the view overpaints with ▸ or ▾: on the
    // ghostty core an anchor is only found when it starts in the first column.
    //
    // Nothing here decides anything — src/CallLines.h does, and is tested headless. This writes the
    // bytes, and is the only place that knows the pane's inline machinery (m_wrap, m_inlineOpen,
    // m_atLineStart, holdShellResize).

    // What a call's fold needs, kept until the map is full or the pane is closed.
    struct CallRecord {
        QString turnId;
        QStringList callIds;                              // one, or the members of a merged run
        relay::toollabel::Label label;
        QString diff;                                     // the unified diff of a write or an edit
        bool merged = false;
        QVector<relay::calllines::RunMember> members;     // a merged run's lines, for its fold
    };

    bool terminalFolds() const { return terminalCan(relay::TerminalBackend::Folds); }

    // How wide a row may be drawn: the pane's columns, less the "▸ " placeholder and one column
    // the engine never draws into. Zero when the width is not known, which means "do not cut".
    int callLineCells() const {
        const int columns = m_backend ? m_backend->columns() : 0;
        return columns > 8 ? columns - 3 : 0;
    }

    // The anchor for one row: a fold URI when a click folds, and the open URI otherwise. A backend
    // with no fold layer anchors everything to open-call, so a click still reaches the detail.
    QString callAnchor(const relay::calllines::Step &step, const QString &turnId,
                       const relay::toollabel::Label &label) const {
        // The rule itself is relay::calllines::anchorsFold, so both answers — including the one a
        // backend with no fold layer gets, which cannot be reached on a machine whose only engine
        // core has folds — are decided by code tests/calllines_test.cpp runs (#BDXG).
        const bool folds = relay::calllines::anchorsFold(relay::calllines::clickFor(label),
                                                         step.merged, terminalFolds());
        return folds ? relay::calllines::foldUri(m_token, turnId, step.callId, step.extra)
                     : relay::calllines::openUri(m_token, turnId, step.callId);
    }

    // Draws one row where the cursor is, exactly as `step` asks. The trailing newline is held back
    // while a run of reads may still grow, so the next result rewrites the row without a cursor-up.
    // beginBlock(Call) goes *before* m_callCursor.start()/result(), never here: a gap prints
    // through printInline, whose endCallRun() tells the cursor "something else printed", and a
    // row started after that could not be rewritten by its own result (#5AWD).
    void drawCallRow(const relay::calllines::Step &step, const QString &turnId, const QString &anchor) {
        QByteArray out = takeWrapped();
        if (!m_inlineOpen) { out += "\r\x1b[2K"; m_inlineOpen = true; m_atLineStart = true; holdShellResize(true); }
        if (step.endRun && !m_atLineStart) { out += "\r\n"; m_atLineStart = true; }
        if (step.rewrite) out += "\r\x1b[2K";
        else if (!m_atLineStart) out += "\r\n";
        out += "\x1b]8;;" + anchor.toUtf8() + "\x1b\\";
        // Two levels, as everywhere else in the pane: a failure takes the error ink, everything
        // else is the same muted grey, and the stats are the italic grey notes already use.
        out += inkCode(step.row.failed ? Ink::Error : Ink::Tool) + QByteArray("▸ ")
               + sanitize(step.row.title).toUtf8() + "\x1b[0m";
        if (!step.row.rest.isEmpty()) out += inkCode(Ink::Note) + sanitize(step.row.rest).toUtf8() + "\x1b[0m";
        out += "\x1b]8;;\x1b\\";
        if (step.hold) { m_atLineStart = false; }
        else { out += "\r\n"; m_atLineStart = true; }
        writeTerminal(out);
        Q_UNUSED(turnId);
    }

    // Ends a held row (a run of reads that nothing has interrupted yet). Called before anything
    // else prints, when the turn ends, and from closeInline().
    void endCallRun() {
        const relay::calllines::Step step = m_callCursor.other();
        if (!step.endRun || !m_inlineOpen) return;
        writeTerminal(takeWrapped() + QByteArray("\r\n"));
        m_atLineStart = true;
    }

    // One call's record, keyed by the anchor its row carries, so a fold request and a click on a
    // relay://open-call line both find it. Bounded: a long session must not grow a map for ever.
    // The bound was 400, which a day's work reaches, and every line past it lost its rich label and
    // its stored diff (#EC58); a record is a label and a few strings, so 5,000 of them is a few MB
    // at worst and keeps the whole of a long session clickable. Past it the URI still fetches the
    // output from the worker — only the label and the diff pane are lost.
    void rememberCall(const QString &anchor, const CallRecord &record) {
        if (anchor.isEmpty()) return;
        if (!m_calls.contains(anchor)) m_callOrder.append(anchor);
        m_calls.insert(anchor, record);
        while (m_callOrder.size() > 5000) m_calls.remove(m_callOrder.takeFirst());
    }

    // The fold's colours, from the live theme. The add/remove tints are the diff pane's: the
    // token blended into the surface, so a light theme gets a light tint (src/DiffView.cpp).
    relay::calllines::Palette foldPalette() const {
        namespace t = relay::theme;
        relay::calllines::Palette palette;
        palette.text = t::Text;
        palette.muted = t::TextMuted;
        palette.code = t::SyntaxCommand;
        palette.link = t::Link;
        palette.add = t::Success;
        palette.remove = t::Error;
        palette.error = t::SyntaxUnknown;
        palette.accent = t::Accent;   // a task in progress, as RequestsPanel paints it (#BDXG)
        auto tint = [](const QColor &token) {
            const QColor base = relay::theme::Surface;
            const qreal mix = 0.22;
            return QColor(int(base.red() + (token.red() - base.red()) * mix),
                          int(base.green() + (token.green() - base.green()) * mix),
                          int(base.blue() + (token.blue() - base.blue()) * mix));
        };
        palette.addBg = tint(t::Success);
        palette.removeBg = tint(t::Error);
        return palette;
    }

    relay::calllines::FoldOptions foldOptions(const QString &anchor, const CallRecord &record) const {
        relay::calllines::FoldOptions options;
        const relay::calllines::Ref ref = relay::calllines::parseUri(anchor);
        if (ref.valid && !record.merged)
            options.openInPane = relay::calllines::openUri(m_token, ref.turn, ref.call);
        const QString path = record.label.openPath.isEmpty() ? record.label.path : record.label.openPath;
        if (!path.isEmpty()) {
            options.openPath = path.startsWith(QLatin1Char('/')) ? path : QDir(m_workspace).filePath(path);
            options.openName = QFileInfo(options.openPath).fileName();
        }
        // A diff of more than 12 changed lines goes to the diff pane, so the fold names it rather
        // than repeating a wall of green and red inside the terminal.
        options.diffToPane = relay::calllines::clickFor(record.label) == relay::calllines::Click::Diff;
        // An "updated tasks" fold already holds the whole list, so its last row does not offer the
        // detail again: it opens the task list, which is where the *current* state of those tasks
        // lives (card #BDXG). A pane restored from scrollback has no record, but handleFoldReply
        // fills the label from the worker's reply before asking for these options, so this still
        // fires on the #EC58 fetch-by-anchor path.
        if (relay::calllines::clickFor(record.label) == relay::calllines::Click::Todos) {
            options.openInPane = QStringLiteral("relay://tasks/") + m_token;
            options.openInPaneText = QStringLiteral("open the task list");
        }
        return options;
    }

    void setFold(const QString &uri, const QVector<relay::FoldLine> &lines) {
        if (m_backend && terminalFolds()) m_backend->setFoldContent(uri, lines);
    }

    // A fold anchor with no content yet was clicked: fetch the call's detail. A merged run needs
    // no worker — its members' lines are already here.
    void foldRequested(const QString &uri) {
        const relay::calllines::Ref ref = relay::calllines::parseUri(uri);
        if (!ref.valid || !ref.fold) return;
        // The reasoning fold answers from the pane's own buffer, with no worker round trip however
        // old the turn (issue T8CN).
        if (ref.call == QLatin1String("thinking") || ref.call.startsWith(QLatin1String("thinking-"))) {
            setFold(uri, thinkingFoldLines(uri, false));
            return;
        }
        const CallRecord record = m_calls.value(uri);
        if (record.merged) { setFold(uri, relay::calllines::foldForRun(record.members, foldPalette(), foldOptions(uri, record))); return; }
        // The anchor already carries the turn and the first call's id, so a line whose record has
        // gone — a pane restored from saved scrollback has none at all, and `rememberCall()` is
        // bounded — can still be fetched from the worker (issue #EC58, owner 2026-09-19: "when i
        // click on tooltipps it sometimes says: the details of this call is not available"). Only a
        // URI that names no call, or no worker to ask, leaves the old note.
        const QString callId = record.callIds.isEmpty() ? ref.call : record.callIds.first();
        if (!m_workerReady || ref.turn.isEmpty() || callId.isEmpty()) {
            setFold(uri, relay::calllines::foldForNote(
                             QStringLiteral("The detail of this call is not available in this pane any more."),
                             foldPalette()));
            return;
        }
        const QString id = QStringLiteral("fold-") + QString::number(++m_requestId);
        m_foldRequests.insert(id, uri);
        while (m_foldRequests.size() > 64) m_foldRequests.erase(m_foldRequests.begin());
        send({{"type", "tool_output_get"}, {"id", id}, {"turn_id", ref.turn}, {"call_id", callId}});
    }

    // The reply to a fold's own tool_output_get. It never opens a pane: the request id says which
    // of the two asked (`fold-` here, `turn-` for the details pane and the turn view).
    bool handleFoldReply(const QJsonObject &event) {
        const QString id = event.value(QStringLiteral("id")).toString();
        if (!id.startsWith(QStringLiteral("fold-"))) return false;
        const QString uri = m_foldRequests.take(id);
        if (uri.isEmpty()) return true;
        CallRecord record = m_calls.value(uri);
        if (!record.label.valid) record.label = relay::toollabel::fromEvent(event);
        setFold(uri, relay::calllines::foldForReply(event, foldPalette(), foldOptions(uri, record)));
        return true;
    }

    // The worker could not answer (the turn has scrolled out of its log of fifty): the fold says so
    // rather than staying empty, which would read as a dead line.
    bool handleFoldError(const QString &id, const QString &text) {
        if (!id.startsWith(QStringLiteral("fold-"))) return false;
        const QString uri = m_foldRequests.take(id);
        if (uri.isEmpty()) return true;
        // A line this pane has no record for was fetched from the anchor alone (#EC58). When the
        // worker cannot answer it either, the worker's own "only the last 50 turns are kept" is
        // half the story: a pane restored from saved scrollback shows lines from *before* this
        // worker existed, and no number of turns would bring them back. Say both reasons.
        const QString note = m_calls.contains(uri)
            ? text
            : QStringLiteral("This call ran before the pane's current agent: its output is not kept "
                             "across a restart, and the worker keeps only the last 50 turns.");
        setFold(uri, relay::calllines::foldForNote(note, foldPalette()));
        return true;
    }

    // A click on a relay://open-call line: § 23.6's `open.type`, with the details pane as the
    // fallback whenever the surface it asks for is not there.
    void openCallTarget(const QString &uri) {
        using relay::calllines::Click;
        const relay::calllines::Ref ref = relay::calllines::parseUri(uri);
        if (!ref.valid) return;
        const CallRecord record = m_calls.value(uri);
        const relay::toollabel::Label &label = record.label;
        auto absolute = [this](const QString &path) {
            return path.isEmpty() || path.startsWith(QLatin1Char('/')) ? path : QDir(m_workspace).filePath(path);
        };
        switch (relay::calllines::clickFor(label)) {
        case Click::File: {
            const QString path = absolute(label.openPath.isEmpty() ? label.path : label.openPath);
            if (!path.isEmpty() && QFileInfo::exists(path) && onOpenPath) { onOpenPath(path, 0); return; }
            break;
        }
        case Click::Diff:
            if (!record.diff.isEmpty() && onOpenDiff) {
                onOpenDiff(label.path.isEmpty() ? label.title : label.path, record.diff);
                hint(QStringLiteral("diff.hunks"), QStringLiteral("Tip: n and p step through the hunks of a diff pane"));
                return;
            }
            break;
        case Click::Subagent:
            if (!label.openId.isEmpty()) { openSubagent(label.openId); return; }
            break;
        case Click::Card:
            if (!label.openId.isEmpty() && onOpenCard) { onOpenCard(label.openId.toUpper()); return; }
            break;
        case Click::Plan: {
            const QString path = absolute(label.path);
            if (!path.isEmpty() && onOpenDocument) { onOpenDocument(path); return; }
            break;
        }
        case Click::Todos:
            // Only reached on a backend with no fold layer (anchorsFold): the row is an open-call
            // anchor, and the task list that call left behind reaches the user as the call's
            // detail in a preview pane — openToolOutput() writes the same `tasks` section the fold
            // would have drawn, through replyAsText (card #BDXG). Not the live task list: an old
            // row must still say what it said.
        case Click::Fold:
            break;
        }
        // Everything the label asked for that this pane cannot open, and every fold-typed line on a
        // backend with no fold layer: the call's stored output, in a preview pane. The URI's own
        // call id stands in for a record this pane no longer holds (#EC58), exactly as in
        // `foldRequested()`; the worker's own "unknown turn_id" is reported separately.
        const QString callId = record.callIds.isEmpty() ? ref.call : record.callIds.first();
        if (!m_workerReady || ref.turn.isEmpty() || callId.isEmpty()) {
            status(QStringLiteral("That call's detail is not available in this pane any more."));
            return;
        }
        send({{"type", "tool_output_get"}, {"id", QStringLiteral("turn-") + QString::number(++m_requestId)},
              {"turn_id", ref.turn}, {"call_id", callId}});
    }

    // The inline diff of a small write or edit (at most 12 changed lines, § 23.2): printed under
    // the line with no click at all, in the add/remove inks the rest of Relay uses.
    void printInlineDiff(const QString &unifiedDiff) {
        const relay::ParsedDiff diff = relay::parseUnifiedDiff(unifiedDiff);
        if (diff.isEmpty()) return;
        for (const relay::DiffLine &line : diff.lines) {
            if (line.kind == relay::DiffLine::FileHeader) continue;   // the row above already names the file
            const Ink ink = line.kind == relay::DiffLine::Add      ? Ink::DiffAdd
                          : line.kind == relay::DiffLine::Remove   ? Ink::DiffRemove
                          : line.kind == relay::DiffLine::Hunk     ? Ink::Note
                                                                   : Ink::ToolOutput;
            printInline(QStringLiteral("  ") + line.text + QLatin1Char('\n'), ink);
        }
    }

public:
    // A write or an edit whose diff is more than 12 changed lines (§ 23.6): the window opens a diff
    // pane beside this one. Also wired into the turn pane's rows (requestTurn).
    // Show one unified diff in a pane beside this one, and hand back the view it went into, so a
    // caller with a decision on that diff can put it in the view's own header (26.5). nullptr when
    // the host showed nothing.
    std::function<relay::DiffView *(const QString &title, const QString &unifiedDiff)> onOpenDiff;

    // A relay://open-call link arriving from outside the window (the desktop's relay: handler).
    void openCallLink(const QString &uri) { openCallTarget(uri); }

    // How reasoning is shown at all (issue T8CN): collapse (the default) streams the fold open and
    // folds it away when the block ends; always leaves it open; never draws nothing at all and lets
    // thinking_done print the single ✦ line. A settings file that still has the agent/show_thinking
    // bool is migrated in place the first time this is asked, and the old key retired.
    static QString thinkingDisplay() {
        QSettings settings;
        const QString value = settings.value(QStringLiteral("agent/thinking_display")).toString();
        if (value == QLatin1String("always") || value == QLatin1String("never")) return value;
        if (value.isEmpty() && settings.contains(QStringLiteral("agent/show_thinking"))) {
            const bool shown = settings.value(QStringLiteral("agent/show_thinking"), true).toBool();
            settings.setValue(QStringLiteral("agent/thinking_display"),
                              shown ? QStringLiteral("collapse") : QStringLiteral("never"));
            settings.remove(QStringLiteral("agent/show_thinking"));
            return shown ? QStringLiteral("collapse") : QStringLiteral("never");
        }
        return QStringLiteral("collapse");
    }

    bool thinkingFoldVisible() const {
        const QString uri = !m_thinkingAnchor.isEmpty() ? m_thinkingAnchor : m_lastThinkingAnchor;
        return !uri.isEmpty() && m_backend && m_backend->foldExpanded(uri);
    }

    // Alt+R opens and folds this pane's reasoning (owner report, 2026-09-18: "need a keyboard
    // shortcut for showing / hiding the reasoning traces, maybe an F# key ... or alt+R"). The
    // reasoning is a fold now, so the key toggles the latest one — live while it streams, the last
    // turn's afterwards: between blocks there is nothing streaming, but the fold and its anchor
    // row are still in the grid, and a key that answers only during the seconds an agent happens
    // to be thinking reads as broken for the rest of the session. Every refusal speaks, or the
    // key would look dead in exactly the panes where it has nothing to do.
    void toggleThinkingPanel() {
        if (thinkingDisplay() == QLatin1String("never")) {
            toast(QStringLiteral("Thinking display is off · Options › General turns it on"));
            return;
        }
        const QString uri = !m_thinkingAnchor.isEmpty() ? m_thinkingAnchor : m_lastThinkingAnchor;
        if (uri.isEmpty() || !m_backend || !terminalFolds()) {
            toast(QStringLiteral("No reasoning yet in this pane"));
            return;
        }
        m_thinkingUserToggled = true;   // however this ends, a person set it
        if (!m_backend->toggleFold(uri)) {
            toast(QStringLiteral("This pane's reasoning has scrolled out of the terminal"));
            return;
        }
        m_thinkingFoldOpen = m_backend->foldExpanded(uri);
        if (!m_thinkingFoldOpen) {
            const QString key = Keymap::instance().shortcutText(QStringLiteral("agent.thinkingPanel"));
            toast(key.isEmpty() ? QStringLiteral("Reasoning folded away")
                                : QStringLiteral("Reasoning folded away · %1 shows it again").arg(key));
        }
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
        relay::RemoteShare &share = relay::RemoteShare::instance();
        const bool sharing = share.isSharing(m_token);
        const int guests = share.sharingModel().guestsOn(m_token);
        m_share->setProperty("dest", sharing ? QStringLiteral("agent") : QVariant());
        m_share->setToolTip(!sharing
            ? QStringLiteral("Share this pane with your phone, or invite someone to it")
            : guests == 0
                ? QStringLiteral("Shared — click for who is here, invites and what is waiting")
                : QStringLiteral("Shared with %1 · click for who is here and what is waiting")
                      .arg(guests == 1 ? QStringLiteral("one other person")
                                       : QStringLiteral("%1 other people").arg(guests)));
        m_share->style()->unpolish(m_share);
        m_share->style()->polish(m_share);
    }

    // The chip under the prompt box. Nothing shared yet: pair a phone or make an invite, which is
    // the dialog. Already shared: the ongoing question is who is here and what is waiting, which
    // is the pane — and its first button opens the dialog again for one more link.
    void shareChipPressed() {
        if (relay::RemoteShare::instance().isSharing(m_token) && onOpenSharing) {
            onOpenSharing();
            // pane.sharing has no key of its own on purpose; the palette is the fast path, so
            // the hint teaches that rather than inventing one (WARP.md, "Shortcut hints").
            hint(QStringLiteral("pane.sharing"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("palette.open")),
                                                QStringLiteral("then “Sharing”")));
            return;
        }
        toggleShare();
    }

    void toggleShare() {
        relay::RemoteShare &share = relay::RemoteShare::instance();
        int tabPanes = 0;
        const QString tab = onShareTab ? onShareTab(&tabPanes) : QString();
        if (!share.isSharing(m_token)) {
            QString error;
            if (!startSharing(share.isTabShared(tab) ? tab : QString(), &error)) {
                status(error);
                return;
            }
        }
        auto *dialog = new relay::RemoteShareDialog(m_token, window());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setTab(tab, tabPanes);
        connect(&share, &relay::RemoteShare::sharingChanged, dialog, [this] { updateShareChip(); });
        dialog->show();
    }

    // The window shares this pane because its tab is shared whole — a pane just split off, or one
    // moved in. Never silent: the share chip lights from the first frame and a toast says who can
    // now see it, because the tab's guests gain it without the owner doing anything else.
    void shareUnderTab(const QString &tab) {
        relay::RemoteShare &share = relay::RemoteShare::instance();
        if (share.isSharing(m_token)) {
            share.setPaneTab(m_token, tab);
            updateShareChip();
            return;
        }
        QString error;
        if (!startSharing(tab, &error)) {
            status(error);
            return;
        }
        toast(QStringLiteral("Shared — this tab is shared, so everyone on it can see this pane."), 5000);
    }

    // Share this pane (no dialog). `tab` is the tab it is shared under, or "" on its own.
    bool startSharing(const QString &tab, QString *error) {
        relay::RemoteShare &share = relay::RemoteShare::instance();
        {
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
            hooks.compose = [this](const QString &text, bool route, const QString &origin,
                                   const QString &when, const QString &originName) {
                submitRemote(text, route, origin, when, originName);
            };
            // pane_state (section 16): the pane publishes, and the phone's actions come back here.
            hooks.queueRemove = [this](const QString &row) { return remoteQueueRemove(row); };
            hooks.queueMove = [this](const QString &row, const QString &to) { return remoteQueueMove(row, to); };
            hooks.queueEdit = [this](const QString &row, QString *text) { return remoteQueueEdit(row, text); };
            hooks.queueSendNow = [this](const QString &row) { return remoteQueueSendNow(row); };
            hooks.modelPick = [this](const QString &choice, const QString &name) { return remoteModelPick(choice, name); };
            hooks.conversationNew = [this](const QString &name) { return remoteConversationNew(name); };
            hooks.conversationOpen = [this](const QString &session, const QString &name) {
                return remoteConversationOpen(session, name);
            };
            hooks.publishPaneState = [this] { m_paneState.publishNow(); };
            hooks.recap = [this] { send({{"type", "recap_request"}, {"reason", "remote"}}); };
            hooks.stopAgent = [this] { stopAgent(); };
            hooks.transcribe = [this](const QString &requestId, const QByteArray &audio,
                                      const QString &format) {
                transcribeForRemote(requestId, audio, format);
            };
            onPaneState = [this](const QJsonObject &state) {
                relay::RemoteShare::instance().paneState(m_token, state);
            };
            if (!share.sharePane(m_token, hooks, error, tab)) {
                onPaneState = nullptr;   // nothing is listening after all
                return false;
            }
            requestRemoteSessions();   // the session list a phone shows, before it asks
            m_paneState.publishNow();
            updateShareChip();
        }
        return true;
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

    // Somebody else is driving this pane and the owner has just typed in it.
    // docs/REMOTE-PROTOCOL.md section 10.3: the owner's physical keystroke always takes control
    // back, without asking. It is the same rule as taking a running program back from the agent,
    // so it is applied in the same two places — setNative(), where endDelegation() already does
    // it for the agent (card #C1HH), and the key filter, for a keystroke that never goes through
    // setNative(). The key itself is never swallowed: this only sends the line that says who is
    // driving.
    //
    // "Somebody else" is a guest **or** one of the owner's own paired devices: section 10.3 has
    // one holder per pane and the owner's phone is in the same book, so a `control_take` that
    // only fired for guests left the phone believing it still had the keyboard and its next line
    // still landing (#W5N2's live drive, shots 13a and 13b).
    void takeBackFromGuest() {
        relay::RemoteShare &share = relay::RemoteShare::instance();
        if (!share.isSharing(m_token)) return;
        const QString driver = share.sharingModel().driverOn(m_token);
        const QString device = driver.isEmpty() ? share.sharingModel().deviceDriverOn(m_token)
                                                : QString();
        if (driver.isEmpty() && device.isEmpty()) return;
        share.takeControl(m_token);
        toast(driver.isEmpty()
                  ? QStringLiteral("You typed — this pane is yours again, not %1's.").arg(device)
                  : QStringLiteral("You typed — this pane is yours again, not %1's.").arg(driver));
    }

    // The window posting on this pane's behalf: somebody knocking, asking for the keyboard or
    // writing a prompt on a pane it is sharing (#W5N2). The same rules as the pane's own notices,
    // which is the point of going through here — the bell always keeps it, the desktop only hears
    // about it while Relay is not the window you are looking at, and neither takes the keyboard.
    void notifyFromWindow(const QString &title, const QString &body, const QString &kind) {
        notify(title, body, kind);
    }

    // The pane status a phone sees: the same vocabulary as the protocol's pane list
    // (docs/REMOTE-PROTOCOL.md section 6.3 — `idle | running | waiting_input | password |
    // failed`). It is the **only** source of a pane's status on the sidecar line, so two of
    // remote/notify.py's five notification triggers — `waiting_input` and `failed` — could never
    // fire from a GUI pane while this answered with three of the six words (#W5N2's live drive).
    //
    // The facts are the ones the pane's own status glyph is decided from (src/PaneStatus.h,
    // #XM0T), so the phone's chip and the desktop's glyph cannot disagree about what the pane is
    // doing: `programAsking` is a foreground program blocked reading the tty or a question read
    // off the screen (`apt`'s `[Y/n]`), and `handoffWaiting` is a command the agent left in the
    // prompt box and is waiting on. Both are "this pane is waiting for the person".
    QString shareStatus() const {
        if (m_secretMode) return QStringLiteral("password");
        const relay::panestatus::Facts facts = statusFacts();
        if (facts.programAsking || facts.handoffWaiting || facts.questionOpen)
            return QStringLiteral("waiting_input");
        if (facts.processBusy || facts.agentBusy) return QStringLiteral("running");
        // The last turn failed and nothing has happened since. Cleared when the pane is used
        // again (a new turn, a command), so a phone is told about a failure once rather than
        // wearing it for the rest of the session.
        if (m_shareFailed) return QStringLiteral("failed");
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
        if (!voiceEnabled()) { status(QStringLiteral("Voice transcription is off (Options › Voice).")); return; }
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
        // Whose clip this was. The worker echoes the id it was given, so a phone's transcript can
        // never land in the desktop's prompt box (someone may be typing in it) and the desktop's
        // own can never be sent to a phone.
        const QString request = event.value(QStringLiteral("id")).toString();
        if (m_remoteVoice.contains(request)) { onRemoteTranscribed(request, event); return; }
        if (!request.isEmpty() && request != m_voiceRequest) return;
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

    // ----- a clip recorded on a paired phone (issue W5N2, protocol section 6.4) ---------------
    // The phone has no key and never talks to a transcription provider: the audio arrives inside
    // the Noise session, takes the same worker request the microphone beside the prompt box
    // takes, and the text goes straight back to the device that spoke.

    void transcribeForRemote(const QString &requestId, const QByteArray &audio,
                             const QString &format) {
        const auto refuse = [this, requestId](const QString &message) {
            relay::RemoteShare::instance().voiceResult(m_token, requestId, false, QString(), message);
            status(QStringLiteral("Voice from a paired device: ") + message);
        };
        if (!voiceEnabled()) {
            refuse(QStringLiteral("Voice transcription is off on the desktop (Options › Voice).")); return;
        }
        if (!voiceKeyStored()) {
            refuse(QStringLiteral("The desktop has no OpenRouter key for transcription.")); return;
        }
        // The worker does the transcribing. Without one the request would be dropped on the floor
        // and the phone would sit through the whole timeout to learn nothing.
        if (m_worker.state() == QProcess::NotRunning) {
            refuse(QStringLiteral("This pane has no agent running to transcribe with.")); return;
        }
        if (audio.isEmpty()) { refuse(QStringLiteral("That clip was empty.")); return; }
        // The extension is chosen here from a fixed table, never taken from the wire: the
        // worker's reader picks its handling from it, and the name is this machine's to make.
        static const QHash<QString, QString> extensions{
            {QStringLiteral("webm"), QStringLiteral("webm")},
            {QStringLiteral("ogg"), QStringLiteral("ogg")},
            {QStringLiteral("wav"), QStringLiteral("wav")},
            {QStringLiteral("mp3"), QStringLiteral("mp3")},
            {QStringLiteral("m4a"), QStringLiteral("m4a")},
        };
        const QString extension = extensions.value(format.toLower());
        if (extension.isEmpty()) { refuse(QStringLiteral("That recording format is not supported.")); return; }
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("relay-voice-%1.%2")
                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces), extension));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            refuse(QStringLiteral("The desktop could not store the clip.")); return;
        }
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const bool written = file.write(audio) == audio.size();
        file.close();
        if (!written) {
            QFile::remove(path);
            refuse(QStringLiteral("The desktop could not store the clip."));
            return;
        }
        const QString worker = QStringLiteral("rvoice-") + QString::number(++m_requestId);
        m_remoteVoice.insert(worker, requestId);
        m_remoteVoiceClips.insert(worker, path);
        status(QStringLiteral("Transcribing a clip from a paired device…"));
        send({{"type", "transcribe"}, {"id", worker}, {"path", path}, {"model", voiceModel()}});
    }

    void onRemoteTranscribed(const QString &worker, const QJsonObject &event) {
        const QString requestId = m_remoteVoice.take(worker);
        const QString clip = m_remoteVoiceClips.take(worker);
        if (!clip.isEmpty()) QFile::remove(clip);      // no audio is kept, however it arrived
        const bool ok = event.value(QStringLiteral("ok")).toBool();
        const QString text = event.value(QStringLiteral("text")).toString();
        QString message = event.value(QStringLiteral("error")).toString();
        if (!ok && message.isEmpty()) message = QStringLiteral("The clip could not be transcribed.");
        relay::RemoteShare::instance().voiceResult(m_token, requestId, ok, text, message);
        status(ok ? QStringLiteral("Transcribed a clip from a paired device")
                  : QStringLiteral("Voice from a paired device: ") + message);
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
        m_keysDialog->setPresets(providerPresets());
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
            // Both duplicate a faster path, so both teach it (WARP.md's standing rule): the Main
            // row's Model… is /model or the model chip, and its effort is Alt+. / Alt+, — the same
            // hint the effort box in the prompt strip shows, under the same id, so the two of them
            // share one show limit rather than teaching the same key twice.
            m_rolesDialog->onMainModelChosen = [this](const QString &model) {
                setMainModel(model);
                hint(QStringLiteral("model.roles"),
                     relay::ShortcutHints::nextTime(QStringLiteral("/model"), QStringLiteral("switch this pane's model")));
            };
            m_rolesDialog->onMainEffortChosen = [this](const QString &level) {
                setEffort(level);
                hint(QStringLiteral("effort.mouse"),
                     relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("agent.effortUp"))
                         + QStringLiteral(" / ") + Keymap::instance().shortcutText(QStringLiteral("agent.effortDown")),
                         QStringLiteral("raise / lower effort")));
            };
        }
        m_rolesDialog->setPresets(providerPresets(), m_tierCatalog, m_roleActions);
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
        // The reply goes back to the view that asked, not to a preview pane: the request id says
        // which of the three asked for a call's output (`fold-` a terminal fold, this map the turn
        // pane, and a plain `turn-` id the "open in pane" path).
        view->onOpenOutput = [this, turnId](const QString &callId) {
            const QString id = QStringLiteral("turn-") + QString::number(++m_requestId);
            m_turnOutputRequests.insert(id, turnId);
            while (m_turnOutputRequests.size() > 64) m_turnOutputRequests.erase(m_turnOutputRequests.begin());
            send({{"type", "tool_output_get"}, {"id", id}, {"turn_id", turnId}, {"call_id", callId}});
        };
        // A row whose diff is more than 12 changed lines opens the diff pane rather than writing a
        // wall of green and red into the turn pane's log (§ 23.6, #TK9C).
        view->onOpenDiff = [this](const QString &title, const QString &unifiedDiff) {
            if (onOpenDiff) onOpenDiff(title, unifiedDiff);
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
        // A reply with § 23.5 sections reads as the fold would have drawn it — an `update_todos`
        // call's task list with its glyphs, a command with its `$ ` — not as the result's JSON.
        // This is the surface a backend with no fold layer gets for every row (#BDXG).
        if (event.value(QStringLiteral("detail")).isArray() && !event.value(QStringLiteral("detail")).toArray().isEmpty())
            body = relay::calllines::replyAsText(event) + QLatin1Char('\n');
        else if (result.isObject()) {
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
            for (auto &entry : m_steering) {
                if (entry.requestId != requestId) continue;
                entry.itemId = event.value(QStringLiteral("id")).toString();
                // × was clicked before the worker had named the item: withdraw it now.
                if (entry.withdraw) send({{"type", "queue_remove"}, {"item", entry.itemId}, {"id", QStringLiteral("withdraw-") + requestId}});
            }
            return true;
        }
        if (type == QStringLiteral("steer_removed")) {
            // Withdrawn before the turn took it. Where it goes now is what the withdraw was for:
            // nowhere (× or Shift+Delete), the prompt box (edited, already there), or the head of
            // the queue (Ctrl+Down).
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            for (int i = 0; i < m_steering.size(); ++i) {
                if (m_steering[i].requestId != requestId) continue;
                const SteerEntry steer = m_steering[i];
                forgetSteer(i);
                if (steer.then == SteerEntry::ToQueue) {
                    requeueSteer(steer);
                    toast(QStringLiteral("Back in the queue · it runs after this turn"));
                } else if (steer.then == SteerEntry::Edit) {
                    toast(QStringLiteral("Taken back · the agent never saw it · Enter queues it again"));
                } else {
                    toast(QStringLiteral("Withdrawn · the agent never saw it"));
                }
                break;
            }
            rebuildQueueStrip(); changed();
            QTimer::singleShot(0, this, [this] { pumpQueue(); });
            return true;
        }
        if (type == QStringLiteral("steer_delivered")) {
            const QJsonArray requestIds = event.value(QStringLiteral("request_ids")).toArray();
            for (const auto &value : requestIds) {
                for (int i = 0; i < m_steering.size(); ++i) {
                    if (m_steering[i].requestId != value.toString()) continue;
                    const SteerEntry steer = m_steering[i];
                    ensureLineStart();
                    // Delivered here, not queued: no "at the next tool call" suffix, which only
                    // belongs on a row still waiting in the strip.
                    printInline(QStringLiteral("✦ ") + steer.text + QLatin1Char('\n'), Ink::UserAgent);
                    forgetSteer(i);
                    if (steer.withdraw) {
                        // The withdraw lost the race: the turn took it first, so the transcript line
                        // above is where it went. The worker's "not queued" answer is expected now.
                        m_withdrawnOnReturn.insert(steer.requestId);
                        if (steer.then == SteerEntry::Edit) {
                            // Its text was already put back in the prompt box. Untouched, that copy
                            // goes; edited, it stays, because it is the user's words now.
                            if (m_editor->toPlainText() == steer.editText) {
                                m_editor->clear();
                                status(QStringLiteral("Too late to edit · the agent already had it at its tool call"));
                            } else {
                                status(QStringLiteral("Too late · the agent already had the original at its tool call"
                                                      " · your edit is still in the prompt box"));
                            }
                        } else {
                            status(QStringLiteral("Too late to withdraw · the agent already had it at its tool call"));
                        }
                    }
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
                    if (m_steering[i].requestId == requestId) { forgetSteer(i); break; }
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
                const SteerEntry steer = m_steering[i];
                forgetSteer(i);
                // A withdraw in flight as the turn ended: the worker's "not queued" answer to it is
                // expected, not news.
                if (steer.withdraw) m_withdrawnOnReturn.insert(requestId);
                if (steer.withdraw && steer.then == SteerEntry::Drop) {
                    // × was clicked as the turn ended: it stays withdrawn rather than coming back.
                    toast(QStringLiteral("Withdrawn · the agent never saw it"));
                } else if (steer.withdraw && steer.then == SteerEntry::Edit) {
                    toast(QStringLiteral("Taken back · the agent never saw it · Enter queues it again"));   // it is in the prompt box
                } else if (!event.value(QStringLiteral("requeued")).toBool()) {
                    // The turn ended before another tool call (or Ctrl+Down asked for exactly this):
                    // the prompt becomes the next queue item.
                    requeueSteer(steer);
                    toast(steer.withdraw ? QStringLiteral("Back in the queue · it runs next")
                                         : QStringLiteral("The agent finished first · your message is next in the queue"));
                }
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
            if (!preset.isEmpty() && role.isEmpty()) { m_currentPreset = preset; rememberPreset(preset); discloseHosted(); }
            // Onto or off a guest preset (29.4): which tool answers now, or none.
            if (role.isEmpty()) noteGuestPreset(event);
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
                       ? QStringLiteral("Model: %1 from the next turn · this turn finishes on %2").arg(m_model, inFlight)
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
            // The reason is a whole sentence that names both models.
            printInline(QStringLiteral("✗ %1\n").arg(reason.isEmpty() ? refused + QStringLiteral(" did not take over.") : reason),
                        Ink::Error);
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
        if (type == QStringLiteral("hosted_quota")) {
            // Relay Free's allowance (protocol 13.9): after every gateway call, and in reply to a
            // hosted_quota request. The chip follows it; so does the keys modal's row when open.
            setHostedQuota(event);
            if (m_keysDialog) m_keysDialog->handleEvent(event);
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
        if (type == QStringLiteral("question")) {
            showQuestion(event);
            return true;
        }
        if (type == QStringLiteral("question_closed")) {
            closeQuestion(event.value(QStringLiteral("reason")).toString() == QStringLiteral("cancelled")
                              ? QStringLiteral("the turn was stopped") : QString());
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
            noteRemoteSessions(event);   // pane_state (relay-terminal-71)
            // Only the manager's own queries (another client may ask this worker too).
            if (m_conversations && event.value(QStringLiteral("id")).toString() == QStringLiteral("conv-list"))
                m_conversations->setResults(event);
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
        // ----- summaries (protocol section 18.4): the session manager's rows and its batch -------
        if (type == QStringLiteral("session_summary")) {
            if (m_conversations) m_conversations->setSessionSummary(event);
            return true;
        }
        if (type == QStringLiteral("conversation_summary")) {
            if (m_conversations) m_conversations->setSummary(event);
            return true;
        }
        if (type == QStringLiteral("conversations_summarize_estimate")) {
            if (m_conversations) m_conversations->setSummariseEstimate(event);
            return true;
        }
        if (type == QStringLiteral("conversations_summarize_progress")) {
            if (m_conversations) m_conversations->setSummariseProgress(event);
            return true;
        }
        if (type == QStringLiteral("conversations_summarize_cancelled")) return true;
        if (type == QStringLiteral("terminal_history_indexed") || type == QStringLiteral("index_rebuilt")) {
            if (type == QStringLiteral("index_rebuilt"))
                status(QStringLiteral("Conversation index rebuilt: %1 conversation(s), %2 entries, %3 ms")
                           .arg(event.value(QStringLiteral("sessions")).toInt())
                           .arg(event.value(QStringLiteral("entries")).toInt())
                           .arg(event.value(QStringLiteral("ms")).toInt()));
            return true;
        }
        // The resume picker's list (protocol section 5) is not asked for any more: /resume opens
        // the session manager. An answer to another client's request is simply dropped.
        if (type == QStringLiteral("sessions")) return true;
        // ----- conversation info, the ⓘ view (protocol section 25) --------------------------
        if (type == QStringLiteral("session_info")) {
            if (m_infoView) m_infoView->setInfo(event);
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
            // A new conversation has a new id (protocol 25); an older worker does not say it, and
            // then the old one must not be taken for what this pane still holds.
            m_sessionId = event.value(QStringLiteral("session_id")).toString();
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

    // ===== the session manager pane and the ⓘ view (protocol sections 14 and 25) ==============
public:
    // The window opens (or brings forward) its session manager pane and binds it to this pane:
    // queries go to this pane's worker, Enter resumes here (cards #CCKY, #R6J0).
    std::function<void(const QString &query)> onOpenSessions;
    // The window opens the ⓘ pane beside this one (the header button, /status) ...
    std::function<void()> onOpenInfo;
    // ... or on one thread (a thread row in the session manager).
    std::function<void(const QString &threadId, const QString &sessionDir, const QString &owner)> onOpenThreadInfo;
    // Before resuming a session here: if another pane already has it open, the window focuses that
    // pane and returns true (two workers must never autosave one session file).
    std::function<bool(const QString &sessionId, const QString &sessionDir)> onSessionOpenElsewhere;

    // /resume, /conversations, Ctrl+Shift+Y and the palette: the session manager pane.
    void openConversations(const QString &initialQuery = QString()) {
        if (!m_workerReady) { status(QStringLiteral("The agent worker is still starting.")); return; }
        if (onOpenSessions) onOpenSessions(initialQuery);
    }

    void bindSessionManager(relay::conversations::SessionManager *view) {
        m_conversations = view;
        QPointer<Pane> self(this);
        view->onQuery = [self](const QJsonObject &request) {
            if (!self) return;
            QJsonObject message = request;
            message.insert(QStringLiteral("type"), QStringLiteral("conversations"));
            message.insert(QStringLiteral("workspace"), self->m_workspace);
            message.insert(QStringLiteral("id"), QStringLiteral("conv-list"));
            // Options › Privacy (review B1, protocol 26.7): whether Relay indexes the guests' own
            // sessions at all. It travels with every listing, so the worker needs no second
            // message and a change takes effect at the next list.
            message.insert(QStringLiteral("index_guests"),
                           QSettings().value(QStringLiteral("sessions/index_guests"), true).toBool());
            self->send(message);
        };
        view->onPreview = [self](const QString &sessionId, const QString &query) {
            if (self) self->send({{"type", "conversation_get"}, {"id", QStringLiteral("conv-preview")},
                                  {"session_id", sessionId}, {"query", query}});
        };
        view->onResume = [self](const QJsonObject &item, bool newPane) { if (self) self->openSavedSession(item, newPane); };
        // Ctrl+Enter. `fork` copies the conversation the worker is holding (protocol 5), so a saved
        // one nobody has loaded cannot be forked in one step: it opens in a new pane instead, and
        // the pane says which of the two happened rather than pretending.
        view->onFork = [self](const QJsonObject &item) {
            if (!self) return;
            // A guest forks itself: `claude --fork-session` / `codex fork` is the row's
            // `fork_command`, and it always goes to a new pane so the original stays where it is.
            if (relay::conversations::isGuestItem(item)) { self->openGuestSession(item, true, true); return; }
            const QString sessionId = item.value(QStringLiteral("session_id")).toString();
            if (sessionId == self->m_sessionId) { self->requestFork(); return; }
            self->status(QStringLiteral("Only the conversation a pane is holding can be forked; opening this one in a new pane."));
            self->openSavedSession(item, true);
        };
        view->onSummarise = [self](const QString &sessionId, const QString &directory) {
            if (!self) return;
            QJsonObject message{{"type", "conversation_summarize"}, {"id", QStringLiteral("conv-summary")},
                                {"session_id", sessionId}};
            if (!directory.isEmpty()) message.insert(QStringLiteral("session_dir"), directory);
            self->send(message);
        };
        view->onSummariseEstimate = [self](const QString &scope) {
            if (self) self->send({{"type", "conversations_summarize_estimate"}, {"id", QStringLiteral("conv-estimate")},
                                  {"scope", scope}, {"workspace", self->m_workspace}});
        };
        view->onSummariseAll = [self](const QString &scope) {
            if (self) self->send({{"type", "conversations_summarize_all"}, {"id", QStringLiteral("conv-batch")},
                                  {"scope", scope}, {"workspace", self->m_workspace}});
        };
        view->onSummariseCancel = [self] {
            if (self) self->send({{"type", "conversations_summarize_cancel"}});
        };
        view->onOpenThread = [self](const QJsonObject &item) {
            if (self && self->onOpenThreadInfo)
                self->onOpenThreadInfo(item.value(QStringLiteral("session_id")).toString(),
                                       item.value(QStringLiteral("session_dir")).toString(),
                                       item.value(QStringLiteral("owner_session")).toString());
        };
        view->onRename = [self](const QString &sessionId, const QString &title) {
            if (self) self->send({{"type", "conversation_rename"}, {"session_id", sessionId}, {"title", title}});
        };
        view->onPin = [self](const QString &sessionId, bool pinned) {
            if (self) self->send({{"type", "conversation_pin"}, {"session_id", sessionId}, {"pinned", pinned}});
        };
        view->onDelete = [self](const QString &sessionId) {
            if (self) self->send({{"type", "conversation_delete"}, {"session_id", sessionId}});
        };
    }

    // The ⓘ view's requests go to this pane's worker; its answers come back as `session_info`.
    void bindInfoView(relay::sessioninfo::InfoView *view) {
        m_infoView = view;
        QPointer<Pane> self(this);
        view->onRequest = [self](const QJsonObject &request) {
            if (!self) return;
            if (!self->m_workerReady) { self->status(QStringLiteral("The agent worker is still starting.")); return; }
            QJsonObject message = request;
            message.insert(QStringLiteral("type"), QStringLiteral("session_info"));
            self->send(message);
        };
        // A thread still running here opens in this pane's subagent pane (RelayWindow::openSubagentTab).
        view->onOpenLive = [self](const QString &agentId, const QString &) { if (self) self->openSubagent(agentId); };
        view->onOpenFile = [self](const QString &path) { if (self && self->onOpenPath) self->onOpenPath(path, 0); };
    }
    relay::sessioninfo::InfoView *infoView() const { return m_infoView; }

    // The ⓘ button and /status.
    void openInfo() {
        if (!m_workerReady) { status(QStringLiteral("The agent worker is still starting.")); return; }
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        if (onOpenInfo) onOpenInfo();
    }

    // Enter resumes in this pane, Shift+Enter opens the conversation in a new one. A conversation
    // saved for another workspace comes back through load_state, which accepts a session reference.
    void openSavedSession(const QJsonObject &item, bool newPane) {
        // claude and codex rows are not Relay conversations: nothing here can load one, and the
        // only way back into it is the tool's own resume command (protocol 26.7).
        if (relay::conversations::isGuestItem(item)) { openGuestSession(item, newPane, false); return; }
        const QString sessionId = item.value(QStringLiteral("session_id")).toString();
        const QString directory = item.value(QStringLiteral("session_dir")).toString();
        const QString title = item.value(QStringLiteral("title")).toString();
        if (sessionId.isEmpty()) return;
        // Open in another pane already: go there instead of loading it twice.
        if (sessionId != m_sessionId && onSessionOpenElsewhere
            && onSessionOpenElsewhere(sessionId, directory.isEmpty() ? m_sessionDir : directory)) return;
        const QJsonObject reference{{QStringLiteral("version"), 1},
                                    {QStringLiteral("kind"), QStringLiteral("relay_agent_state_ref")},
                                    {QStringLiteral("session_id"), sessionId},
                                    {QStringLiteral("session_dir"), directory.isEmpty() ? m_sessionDir : directory}};
        if (newPane) {
            if (onOpenSessionInNewPane) onOpenSessionInNewPane(reference, title);
            else if (onForkState) onForkState(reference, title);
            return;
        }
        if (sessionId == m_sessionId && (directory.isEmpty() || directory == m_sessionDir)) {
            status(QStringLiteral("That session is already open in this pane."));
            return;
        }
        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        if (m_agentBusy) { status(QStringLiteral("Stop the agent turn before opening another conversation.")); return; }
        if (directory.isEmpty() || directory == m_sessionDir) send({{"type", "resume"}, {"id", sessionId}});
        else send({{"type", "load_state"}, {"state", reference}});
    }

    // A guest session row (protocol 26.7). Relay resumes it the way the user would: it runs the
    // guest's own argv — `claude -r <id>`, `codex resume <id>`, and their fork variants — in a
    // pane whose working directory is the session's own, because both guests resolve a session id
    // against the directory they are started in and report an unknown session from anywhere else.
    void openGuestSession(const QJsonObject &item, bool newPane, bool fork) {
        const QString source = item.value(QStringLiteral("source")).toString();
        const QJsonArray argv = item.value(fork ? QStringLiteral("fork_command") : QStringLiteral("resume_command")).toArray();
        QStringList words;
        for (const auto &value : argv) words << value.toString();
        if (words.size() < 2 || guestIdFor(words.first()) != source) {
            status(QStringLiteral("That session did not come with a resume command."));
            return;
        }
        // The tool's own arguments without its name: `launchGuest` adds the launch-time settings,
        // the bypass flag and the bridge (26.9), so a resumed guest is configured like a picked one.
        const QStringList extra = words.mid(1);
        const QString cwd = relay::conversations::guestCwd(item);
        if (newPane) {
            if (onOpenGuestPane) { onOpenGuestPane(source, extra, cwd); return; }
            status(QStringLiteral("This window cannot open another pane; press Enter to resume here."));
            return;
        }
        // Tier A (29.4): when the worker can run this guest headless, the row is resumed *on the
        // pane's own agent* — `configure` with `guest.resume`, in the session's directory — so the
        // conversation, the call lines and the chips are Relay's from the first turn.
        if (guestHarnessUsable(source)) {
            const GuestResume resume = guestResumeFrom(extra);
            resumeGuestPreset(source, resume.sessionId, resume.fork, cwd, extra);
            return;
        }
        // In this pane, in the session's own directory (both guests resolve an id against the
        // directory they start in, 26.7). A guest already here is asked to leave first.
        leaveGuest([this, source, extra, cwd] {
            launchGuest(source, extra, cwd);
            status(QStringLiteral("Resuming the %1 session in this pane.").arg(guestDisplayName(source)));
        }, true);
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
        if (m_capturing && m_backend && !m_login.active) m_backend->setOutputCallbackEnabled(false);
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


    // First launch on a machine with no instruction files at all: the introductory dialog had
    // nothing to offer and still opened, asking the user to choose from an empty list (issue #ZYRB,
    // owner 2026-09-18: "the introductory agent instructions file was still showing when i didnt
    // have any. it shouldnt show any in that case. just init the default RELAY.MD"). So write the
    // starter relay.md, point the agent at it, and say one line. Only the first-run path is quiet:
    // `/instructions` and Options › Agent › Instructions asked for the dialog, so they still get it
    // even when the list is empty — that is where a file is created on purpose.
    bool initDefaultRelayMd() {
        const QString path = relayMdPath();
        if (QFileInfo::exists(path)) return false;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        // Deliberately short: a starter file the user will edit, not advice Relay pretends to have.
        // The agent reads it verbatim, so every line here is a line it is told to follow.
        file.write(
            "# Relay instructions\n"
            "\n"
            "Notes for the agent in every Relay pane on this machine. Edit freely; Relay never\n"
            "rewrites this file. A project's own CLAUDE.md, AGENTS.md or WARP.md is read as well.\n"
            "\n"
            "## How I like to work\n"
            "\n"
            "- Say what you changed and why, briefly.\n"
            "- Ask before anything destructive.\n");
        file.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        QSettings settings;
        settings.setValue(QStringLiteral("instructions/files"), QStringList{path});
        settings.setValue(QStringLiteral("instructions/onboarded"), true);
        ensureLineStart();
        printInline(QStringLiteral("Created %1 · your agent instructions, /instructions to change\n").arg(path),
                    Ink::Note);
        closeInline();
        status(QStringLiteral("Created %1").arg(path));
        applyConfigureChange(QStringLiteral("relay.md created"));
        return true;
    }

    void showInstructionsDialog(const QJsonArray &items) {
        // `scan_instructions` lists the fixed names that are *missing* too (`exists: false`), so
        // "I don't have any" is "nothing in the list is on disk", not "the list is empty".
        const bool anyOnDisk = std::any_of(items.cbegin(), items.cend(), [](const QJsonValue &value) {
            return value.toObject().value(QStringLiteral("exists")).toBool();
        });
        if (m_onboarding && !anyOnDisk && !QFileInfo::exists(relayMdPath())) {
            m_onboarding = false;
            if (initDefaultRelayMd()) { focusInput(); return; }
            // The file could not be written (a read-only config directory): fall through to the
            // dialog rather than swallowing the one chance to say something about instructions.
        }
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
    // `hidden` is a name Relay still answers to but never teaches: it is left out of the `/` popup
    // and never completed from a prefix, so the palette cannot show the word it replaced. Typing it
    // in full still runs it, and it still counts as a known command (so it is not reported unknown
    // and an alias cannot take its name). Card #SHE3: `/todos` is the first of these.
    struct SlashCommand { QString name, args, description; bool hidden = false; };
    // The hidden names, for relay::slash::offered() and resolve().
    static QStringList hiddenSlashNames() {
        QStringList out;
        for (const auto &command : slashCommands()) if (command.hidden) out << command.name;
        return out;
    }
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
            {QStringLiteral("local"), QString(), QStringLiteral("Run this pane on a model served on this machine")},
            {QStringLiteral("glm"), QString(), QStringLiteral("Switch to the GLM Coding Plan")},
            {QStringLiteral("kimi"), QString(), QStringLiteral("Switch to the Kimi Coding Plan")},
            {QStringLiteral("effort"), QStringLiteral("[low|medium|high|max]"), QStringLiteral("Set reasoning effort")},
            {QStringLiteral("compact"), QStringLiteral("[focus]"), QStringLiteral("Summarize older turns to free context")},
            {QStringLiteral("context"), QString(), QStringLiteral("Show context usage")},
            {QStringLiteral("rewind"), QString(), QStringLiteral("Rewind chat to an earlier turn (files are not changed)")},
            {QStringLiteral("rewind-code"), QString(), QStringLiteral("Restore files the agent changed since an earlier turn")},
            {QStringLiteral("fork"), QString(), QStringLiteral("Continue this conversation in a new pane")},
            {QStringLiteral("resume"), QStringLiteral("[words]"), QStringLiteral("Sessions: resume, search, subagent threads (same as /conversations)")},
            {QStringLiteral("sessions"), QStringLiteral("[words]"), QStringLiteral("Sessions: the same pane as /resume, under the name on its header")},
            {QStringLiteral("conversations"), QStringLiteral("[words]"), QStringLiteral("Sessions: search every session and Relay's terminal history")},
            {QStringLiteral("status"), QString(), QStringLiteral("Conversation info: model, tokens, file and history with subagent threads (the ⓘ button)")},
            {QStringLiteral("info"), QString(), QStringLiteral("Conversation info (same as /status)")},
            {QStringLiteral("find"), QStringLiteral("[words]"), QStringLiteral("Find in this pane: conversation and terminal scrollback")},
            {QStringLiteral("plan"), QString(), QStringLiteral("Toggle plan mode")},
            {QStringLiteral("light"), QString(), QStringLiteral("Light theme: IBM Beige")},
            {QStringLiteral("dark"), QString(), QStringLiteral("Dark theme: Dark Copper")},
            {QStringLiteral("theme"), QStringLiteral("[name]"), QStringLiteral("Theme for this tab and new ones: pick from every theme, or name one")},
        {QStringLiteral("switchboard"), QString(), QStringLiteral("Open the Switchboard: cards, threads and plans")},
        {QStringLiteral("card"), QStringLiteral("<text>"), QStringLiteral("Add a card to the Switchboard inbox, verbatim")},
        {QStringLiteral("init"), QString(), QStringLiteral("Initialize a project here and create its Switchboard")},
            {QStringLiteral("recap"), QString(), QStringLiteral("Summarize this session")},
            {QStringLiteral("tasks"), QString(), QStringLiteral("Task list: what the agent is working on")},
            {QStringLiteral("requests"), QString(), QStringLiteral("Task list (same as /tasks)")},
            // A silent alias: still typed by hand and by an older habit, never taught (#SHE3).
            {QStringLiteral("todos"), QString(), QStringLiteral("Task list (same as /tasks)"), true},
            {QStringLiteral("continue"), QString(), QStringLiteral("Continue the agent turn (after a step limit)")},
            {QStringLiteral("agents"), QString(), QStringLiteral("Subagents: definitions and running agents")},
            {QStringLiteral("skills"), QString(), QStringLiteral("Skills: list, exclude, refine, import from a repository")},
            {QStringLiteral("skill"), QStringLiteral("<name> [input]"), QStringLiteral("Run a skill (same as /name; this form wins over a built-in of the same name)")},
            {QStringLiteral("instructions"), QString(), QStringLiteral("Choose instruction files (CLAUDE.md, AGENTS.md, WARP.md…)")},
            {QStringLiteral("rename"), QStringLiteral("[name]"), QStringLiteral("Name this pane (no name: edit it in the header; empty: back to automatic)")},
            {QStringLiteral("rename-tab"), QStringLiteral("[name]"), QStringLiteral("Name this tab (no name: edit it in the tab)")},
            {QStringLiteral("export"), QString(), QStringLiteral("Save the conversation as Markdown")},
            {QStringLiteral("join"), QStringLiteral("[code]"), QStringLiteral("Join someone's shared session: their meeting code, then the PIN")},
            {QStringLiteral("update"), QString(), QStringLiteral("Download and install the latest Relay release, then restart")},
            {QStringLiteral("connect"), QStringLiteral("[code]"), QStringLiteral("Join someone's shared session (same as /join)")},
            {QStringLiteral("shell"), QStringLiteral("<command>"), QStringLiteral("Send to the terminal")},
            {QStringLiteral("agent"), QStringLiteral("<prompt>"), QStringLiteral("Send to the agent")}};
        return commands;
    }

    void updateSlashPopup() {
        // A queued item is in the box, not a command being typed: its leading "/" must not open the
        // popup, which would take the arrow keys the queue is using.
        if (inQueueSelection()) { hideSlashPopup(); return; }
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
        QList<SlashCommand> commands;
        QStringList builtins;
        for (const auto &command : slashCommands()) builtins << command.name;   // hidden ones shadow an alias too
        const QStringList shown = relay::slash::offered(builtins, hiddenSlashNames());
        for (const auto &command : slashCommands())
            if (shown.contains(command.name)) commands.append(command);
        for (const auto &alias : std::as_const(m_aliasList)) {
            if (alias.shadowed || builtins.contains(alias.name)) continue;
            commands.append({alias.name, alias.params.isEmpty() ? QString() : QStringLiteral("[args]"),
                             relay::aliases::paletteDetail(alias)});
        }
        // Then the skills, `/clean-commit` (feature intake 2026-09-18, "like warp"): last, so a
        // built-in or an alias of the same name keeps it, and `/skill <name>` still reaches the skill.
        QStringList taken;
        for (const auto &command : std::as_const(commands)) taken << command.name;
        for (const auto &skill : std::as_const(m_skillCommands)) {
            if (taken.contains(skill.name)) continue;
            QString description = skill.description;
            if (description.size() > 60) description = description.left(59).trimmed() + QChar(0x2026);
            commands.append({skill.name, skill.args, QStringLiteral("Skill · ") + description});
        }
        // A guest catalog follows every Relay-owned surface. Relay's own names (and its aliases
        // and skills) win a collision, while a guest-only or newly introduced command passes
        // through verbatim rather than Relay diagnosing it as unknown.
        QStringList guestNames;
        int guestStart = -1;
        if (guestInFront()) {
            taken.clear();
            for (const auto &command : std::as_const(commands)) taken << command.name;
            guestStart = commands.size();
            for (const QString &slash : std::as_const(m_guestSlashCommands)) {
                const QString name = slash.mid(1);
                if (taken.contains(name, Qt::CaseInsensitive)) continue;
                commands.append({name, QString(), QStringLiteral("%1 · guest").arg(guestDisplayName(m_guest))});
                guestNames << name;
            }
        }
        for (int i = 0; i < commands.size(); ++i) {
            // Name prefix first, then names containing the query; descriptions do not match.
            const QString name = commands[i].name.toLower();
            int score = name.startsWith(query) ? 3000 - i : name.contains(query) ? 2000 - i : 0;
            // With nothing typed the scores follow declaration order, so the nine visible rows
            // would all be Relay's and the guest's commands — appended last — sat below the fold
            // no QA run or user would ever scroll to. While a guest is active the composer is
            // that guest's input line (§26.8): open the list with its commands. Typing a query
            // restores the usual order, where Relay's names win a tie.
            if (score > 0 && query.isEmpty() && guestStart >= 0 && i >= guestStart) score += 9000;
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
            item->setData(Qt::UserRole + 2, guestNames.contains(command.name, Qt::CaseInsensitive));
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
        // The two hidden-name rules are relay::slash's, so the popup, this hint and
        // tests/slashcommands_test.cpp all run the same ones (#SHE3): a hidden name is never
        // completed from a prefix — that would put the retired word back in front of the user —
        // but typed in full it resolves like any other. `/shell` and `/agent` are the router's own
        // prefixes and are not commands here at all.
        QStringList names, hidden;
        for (const auto &command : slashCommands()) {
            if (command.name == QStringLiteral("shell") || command.name == QStringLiteral("agent")) continue;
            names << command.name;
            if (command.hidden) hidden << command.name;
        }
        bool exact = false;
        const QString found = relay::slash::resolve(name, names, hidden, &exact);
        if (found.isEmpty() || (hasArgs && !exact)) return nullptr;
        for (const auto &command : slashCommands())
            if (command.name == found) return &command;
        return nullptr;
    }

    void hideSlashPopup() { if (m_slashList && m_slashList->isVisible()) m_slashList->hide(); }

    // Enter runs the selected command (commands that need an argument are completed instead); Tab completes.
    void acceptSlashSelection(bool run) {
        if (!m_slashList || !m_slashList->currentItem()) return;
        const QString name = m_slashList->currentItem()->data(Qt::UserRole).toString();
        const bool needsArgument = m_slashList->currentItem()->data(Qt::UserRole + 1).toBool();
        const bool guest = m_slashList->currentItem()->data(Qt::UserRole + 2).toBool();
        hideSlashPopup();
        if (!run || needsArgument) {
            m_editor->setPlainText(QStringLiteral("/") + name + ' ');
            m_editor->moveCursor(QTextCursor::End);
            return;
        }
        m_editor->clear();
        if (guest) {
            submitGuest(QStringLiteral("/") + name, false);
            return;
        }
        // An alias name that reached the popup is not a built-in (issue G8DK); a skill is neither.
        if (std::none_of(slashCommands().cbegin(), slashCommands().cend(),
                         [&](const auto &c) { return c.name == name; })) {
            if (relay::aliases::names(m_aliasList).contains(name) || !skillCommand(name)) runAlias(name, QString(), false);
            else submitAgent(QStringLiteral("/") + name, false);
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
        for (const auto &skill : m_skillCommands)
            if (!names.contains(skill.name)) names << skill.name;
        return names;
    }

    // ----- skills as `/name` (feature intake 2026-09-18: "add skills as / commands like warp, eg
    // /clean-commit") ------------------------------------------------------------------------------
    // The worker lists the skills its agent can load (`configured.skill_commands`). `/clean-commit
    // tidy the readme` goes to the agent as typed, with `skills: ["clean-commit"]` on the `ask`, and
    // the worker sends that SKILL.md along as the turn's instructions. A built-in or an alias of the
    // same name wins `/name`; `/skill <name>` always means the skill.
    void setSkillCommands(const QJsonArray &items) {
        m_skillCommands.clear();
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            // A `skills_list` row (the /skills dialog) that the agent does not load.
            if (item.value(QStringLiteral("excluded")).toBool() || item.contains(QStringLiteral("shadowed_by"))) continue;
            const QString name = item.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) continue;
            m_skillCommands.append({name, QStringLiteral("[input]"),
                                    item.value(QStringLiteral("description")).toString().simplified()});
        }
    }

    const SlashCommand *skillCommand(const QString &name) const {
        for (const auto &skill : m_skillCommands)
            if (skill.name == name) return &skill;
        return nullptr;
    }

    // The skill a prompt runs: `/name …` when no built-in or alias has that name, or `/skill name …`.
    QString skillFor(const QString &text) const {
        static const QRegularExpression pattern(
            QStringLiteral("^/(skill\\s+)?([A-Za-z0-9][A-Za-z0-9._-]*)(?:\\s[\\s\\S]*)?$"));
        const auto match = pattern.match(text.trimmed());
        if (!match.hasMatch()) return {};
        const QString name = match.captured(2);
        if (!skillCommand(name)) return {};
        if (match.capturedLength(1) == 0
            && (std::any_of(slashCommands().cbegin(), slashCommands().cend(), [&](const auto &c) { return c.name == name; })
                || relay::aliases::names(m_aliasList).contains(name)))
            return {};
        return name;
    }

    QJsonArray skillsFor(const QString &text) const {
        const QString name = skillFor(text);
        return name.isEmpty() ? QJsonArray() : QJsonArray{name};
    }

    bool tryRunSkillSlash(const QString &text) {
        if (skillFor(text).isEmpty() || text.trimmed().startsWith(QStringLiteral("/skill "))) return false;
        hideSlashPopup();
        submitAgent(text.trimmed(), true);
        return true;
    }

    // The slow path the hint teaches: asking for a skill by name in prose ("use the clean-commit
    // skill to …") when `/clean-commit …` says the same thing and sends the skill with it. Shown
    // when that turn ends: while it runs, the turn's own status holds the same spot.
    void skillSlashHint(const QString &text) {
        if (text.startsWith('/') || !text.contains(QStringLiteral("skill"), Qt::CaseInsensitive)) return;
        for (const auto &skill : std::as_const(m_skillCommands)) {
            if (skill.name.size() < 3) continue;
            const QRegularExpression word(QStringLiteral("(?<![\\w/-])%1(?![\\w-])").arg(QRegularExpression::escape(skill.name)),
                                          QRegularExpression::CaseInsensitiveOption);
            if (!word.match(text).hasMatch()) continue;
            m_skillHintPending = skill.name;
            return;
        }
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
            // Claude Code and Codex are models here too (26.9): `/model codex`, `/model claude`.
            // The rows offered are the presets — which now include the guests the worker can run
            // as this pane's agent (29.4) — plus the guests only the Tier B launch answers for.
            const QStringList guests = tierBGuests();
            if (!args.isEmpty()) {
                // `/model claude opus`: a model named with the guest, which only its harness can
                // honour. Every installed guest is matched here, Tier A or Tier B, so the name
                // always reaches pickGuest rather than a preset whose label happens to contain it.
                const QString firstWord = args.section(QLatin1Char(' '), 0, 0);
                const QString restOfArgs = args.section(QLatin1Char(' '), 1).trimmed();
                for (const QString &id : installedGuests()) {
                    const bool byName = guestDisplayName(id).compare(args, Qt::CaseInsensitive) == 0;
                    if (byName || firstWord.compare(id, Qt::CaseInsensitive) == 0) {
                        pickGuest(id, byName ? QString() : restOfArgs);
                        return;
                    }
                }
                for (const auto &model : std::as_const(m_stored)) {
                    if (model.first.compare(args, Qt::CaseInsensitive) == 0 || model.second.contains(args, Qt::CaseInsensitive)) {
                        leaveGuest([this, id = model.first] { selectModel(id); });
                        return;
                    }
                }
                status(QStringLiteral("No stored model matches “%1”.").arg(args));
                return;
            }
            QList<relay::agentui::PickerRow> rows;
            for (const auto &model : std::as_const(m_stored)) rows << relay::agentui::PickerRow{{model.second, model.first == m_currentPreset && m_guest.isEmpty() ? QStringLiteral("current") : QString()}, model.first, model.first};
            for (const QString &id : guests) rows << relay::agentui::PickerRow{{guestDisplayName(id), id == m_guest ? QStringLiteral("current") : QString()}, QStringLiteral("guest:") + id, id};
            const auto result = relay::agentui::pick(this, QStringLiteral("Model"), QStringLiteral("Switch this pane's model. The conversation is kept."),
                                                     {QStringLiteral("Model"), QString()}, rows, {{QStringLiteral("use"), QStringLiteral("Use"), true}});
            if (result.row < 0) return;
            if (result.row >= m_stored.size()) modelBoxPicked(QStringLiteral("guest:") + guests.at(result.row - m_stored.size()));
            else leaveGuest([this, id = m_stored.at(result.row).first] { selectModel(id); });
        } else if (name == QStringLiteral("main") || name == QStringLiteral("flash")
                   || name == QStringLiteral("local")) {
            // The pane's own agent, not the tier table: /flash runs this conversation on the Flash
            // model and /main puts it back, both keeping the conversation (the same switch as Alt+F).
            // /local is the same switch onto a model served on this machine (owner, 2026-09-18).
            // Saying so even when the pane is already there means the command always reports where
            // it ended up, rather than looking like it did nothing.
            if (m_agentRole == name) {
                const QString model = name == QStringLiteral("main") ? m_model : roleModel(name);
                status(QStringLiteral("Already on the %1%2.").arg(roleLabel(name), model.isEmpty() ? QString() : QStringLiteral(" · ") + model));
                return;
            }
            // Nothing served here: say so and stay put, rather than switching to a role that would
            // resolve straight back to the main model.
            if (name == QStringLiteral("local") && !hasLocalEndpoint()) { status(noLocalModelMessage()); return; }
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
            status(QStringLiteral("No stored %1 key. Add one in Options › Models › API keys….")
                       .arg(glm ? QStringLiteral("GLM") : QStringLiteral("Kimi")));
        } else if (name == QStringLiteral("effort")) {
            const QStringList levels = offeredEfforts();
            const QString wanted = args.toLower();
            if (levels.isEmpty() && !wanted.isEmpty()) status(QStringLiteral("This model has no reasoning setting."));
            else if (efforts().contains(wanted)) {
                setEffort(wanted);
                // A level this provider does not have is kept as typed and sent as the one it maps
                // to; saying so beats silently moving the pane to another level.
                if (!levels.contains(wanted) && !effortNote().isEmpty())
                    status(QStringLiteral("This model offers %1 · %2").arg(levels.join(QStringLiteral(", ")), effortNote()));
            } else if (wanted.isEmpty()) {
                const QStringList walk = levels.isEmpty() ? efforts() : levels;
                effortStep(1 - (walk.indexOf(nearestEffort(walk, m_effort)) == walk.size() - 1 ? walk.size() : 0));
            } else status(QStringLiteral("Effort must be low, medium, high or max."));
        } else if (name == QStringLiteral("compact")) compactNow(args);
        else if (name == QStringLiteral("context")) {
            if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
            m_contextNotePending = true;
            send({{"type", "context"}});
        } else if (name == QStringLiteral("rewind")) openRewind();
        else if (name == QStringLiteral("rewind-code")) openRewind(QStringLiteral("code"));
        else if (name == QStringLiteral("fork")) requestFork();
        else if (name == QStringLiteral("resume") || name == QStringLiteral("sessions")) {
            openResume(args);
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.resume")); !keys.isEmpty())
                hint(QStringLiteral("resume.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("sessions")));
        } else if (name == QStringLiteral("conversations")) {
            openConversations(args);
            // One pane now: the key that opens it is agent.resume's (conversations.open is unbound).
            QString keys = Keymap::instance().shortcutText(QStringLiteral("conversations.open"));
            if (keys.isEmpty()) keys = Keymap::instance().shortcutText(QStringLiteral("agent.resume"));
            if (!keys.isEmpty())
                hint(QStringLiteral("conversations.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("sessions")));
        } else if (name == QStringLiteral("status") || name == QStringLiteral("info")) {
            openInfo();
            if (const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.info")); !keys.isEmpty())
                hint(QStringLiteral("info.slash"), relay::ShortcutHints::nextTime(keys, QStringLiteral("conversation info")));
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
            chooseTheme(name == QStringLiteral("light") ? QStringLiteral("ibm-beige") : QStringLiteral("dark-copper"));
        }
        else if (name == QStringLiteral("theme")) {
            // Every theme, not just the owner's two (owner, 2026-09-19): `/theme gruvbox`, or the
            // bare command for a list. Matched on the id, then the name, then a part of either.
            const QList<relay::theme::ThemeChoice> themes = relay::theme::availableThemes();
            if (!args.isEmpty()) {
                const auto find = [&](auto test) { for (const auto &t : themes) if (test(t)) return t.id; return QString(); };
                QString id = find([&](const auto &t) { return t.id.compare(args, Qt::CaseInsensitive) == 0 || t.name.compare(args, Qt::CaseInsensitive) == 0; });
                if (id.isEmpty()) id = find([&](const auto &t) { return t.id.contains(args, Qt::CaseInsensitive) || t.name.contains(args, Qt::CaseInsensitive); });
                if (id.isEmpty()) { status(QStringLiteral("No theme matches “%1”.").arg(args)); return; }
                chooseTheme(id);
                return;
            }
            QList<relay::agentui::PickerRow> rows;
            for (const auto &t : themes)
                rows << relay::agentui::PickerRow{{t.name, t.id == relay::theme::activeThemeId() ? QStringLiteral("current") : QString()}, t.description, t.id};
            const auto result = relay::agentui::pick(this, QStringLiteral("Theme"), QStringLiteral("The theme for this tab, and for new tabs unless Options › Appearance says a command changes one tab only."),
                                                     {QStringLiteral("Theme"), QString()}, rows, {{QStringLiteral("use"), QStringLiteral("Use"), true}});
            if (result.row >= 0 && result.row < themes.size()) chooseTheme(themes.at(result.row).id);
        }
        else if (name == QStringLiteral("rename")) {
            // With a name it renames straight away; without one it opens the same editor a double
            // click does, where clearing the field hands the pane back to the model.
            if (args.isEmpty()) beginRename(); else renameTo(args);
        }
        else if (name == QStringLiteral("rename-tab")) {
            if (onRenameTab) onRenameTab(args, args.isEmpty());
        }
        else if (name == QStringLiteral("join") || name == QStringLiteral("connect")) {
            // The code is public and four letters; the PIN is asked for in the dialog, never on
            // the command line, where it would land in the prompt history.
            if (onJoinShared) onJoinShared(args.trimmed().toUpper());
        }
        else if (name == QStringLiteral("update")) {
            // The window runs scripts/relay-update.py and shows each of its lines as the notice;
            // on its UPDATED marker it restarts Relay into the version it just installed.
            if (onUpdateApp) onUpdateApp();
        }
        else if (name == QStringLiteral("switchboard")) {
            if (onOpenBoard) onOpenBoard();
            boardShortcutHint(QStringLiteral("board.slash"));
        }
        else if (name == QStringLiteral("card")) {
            if (args.trimmed().isEmpty()) { status(QStringLiteral("Usage: /card <what to remember>")); return; }
            // An explicit project action: it attaches this tab to the pane's candidate project
            // when that project has a Switchboard. When it has none, this is trigger (4) of the
            // init question — and **the card text is held, not lost**: it lands as the first card
            // the moment the board exists (protocol 19.12). Still nothing is created before a yes.
            if (QString why; !attachForBoard(relay::projects::kReasonCardCommand, &why)) {
                if (askProjectInit(relay::projectinit::Trigger::CardCommand, args.trimmed(), &why)) return;
                status(why.isEmpty() ? QStringLiteral("No Switchboard here.") : why);
                return;
            }
            // Quick add to the Inbox without opening the pane; the text is kept verbatim.
            send({{QStringLiteral("type"), QStringLiteral("board_create")},
                  {QStringLiteral("tab"), QStringLiteral("features")},
                  {QStringLiteral("status"), QStringLiteral("inbox")},
                  {QStringLiteral("text"), args.trimmed()}});
            m_cardIndexAsked = false;
            boardShortcutHint(QStringLiteral("card.slash"));
        }
        else if (name == QStringLiteral("init")) {
            // Trigger (5): the explicit command. It asks even about a project the user declined
            // once — typing it is changing your mind — and in a directory that is no project at
            // all it offers to treat the pane's own directory as one.
            QString why;
            if (!askProjectInit(relay::projectinit::Trigger::InitCommand, QString(), &why))
                status(why.isEmpty() ? QStringLiteral("Nothing to initialize here.") : why);
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
        } else if (name == QStringLiteral("skill")) {
            if (args.isEmpty()) { openSkills(); return; }
            const QString text = QStringLiteral("/skill ") + args;
            if (skillFor(text).isEmpty()) {
                const QString wanted = args.section(QRegularExpression(QStringLiteral("\\s")), 0, 0);
                QStringList known;
                for (const auto &skill : std::as_const(m_skillCommands)) known << skill.name;
                const QStringList close = relay::slash::closest(wanted, known, 2);
                status(close.isEmpty() ? QStringLiteral("No skill named “%1” · /skills lists them").arg(wanted)
                                       : QStringLiteral("No skill named “%1” · did you mean %2?").arg(wanted, close.join(QStringLiteral(" or "))));
                return;
            }
            submitAgent(text, false);
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
    // A prompt the running turn takes at its next tool call. `withdraw` is set while a queue_remove
    // for it is in flight; `then` says where it goes once the worker confirms (steer_removed).
    struct SteerEntry {
        enum Then { Drop, Edit, ToQueue };
        QString requestId, itemId, text; QJsonArray attachments, cards;
        bool withdraw = false; Then then = Drop;
        QString editText;   // Edit: what was put in the prompt box, to tell an untouched copy from an edit
    };

public:
    // One row of the queue list, top to bottom in delivery order (queueRows()). `id` is stable for
    // as long as the row exists and is what removeRow() takes: "running", "steer:<request id>" or
    // "entry:<queue id>". kind: "running", "steer", "agent" or "command". state: "running",
    // "waiting" (a steer before the next tool call), "withdrawing", "queued", "editing" (selected
    // in the prompt box) or "paused".
    struct QueueRow { QString id, kind, preview, state; };

    // The rows the queue strip shows, in delivery order: what is running, then the steers, then
    // the queued items. For publishing the queue elsewhere (a paired phone); nothing reads it yet.
    QList<QueueRow> queueRows() const {
        QList<QueueRow> rows;
        if (const QString running = runningLabel(); !running.trimmed().isEmpty())
            rows.append({QStringLiteral("running"), QStringLiteral("running"), running.simplified(), QStringLiteral("running")});
        for (const auto &steer : m_steering)
            rows.append({QStringLiteral("steer:") + steer.requestId, QStringLiteral("steer"), steer.text.simplified(),
                         steer.withdraw ? QStringLiteral("withdrawing")
                                        : steer.requestId == m_selectedSteer ? QStringLiteral("editing") : QStringLiteral("waiting")});
        for (int i = 0; i < m_entries.size(); ++i) {
            const QueueEntry &entry = m_entries[i];
            rows.append({QStringLiteral("entry:%1").arg(entry.id), entry.agent ? QStringLiteral("agent") : QStringLiteral("command"),
                         entry.label().simplified(),
                         i == m_selected ? QStringLiteral("editing") : m_entriesPaused ? QStringLiteral("paused") : QStringLiteral("queued")});
        }
        return rows;
    }

    // Remove one row of the queue by its queueRows() id: a queued item is dropped, a steer is
    // withdrawn if the turn has not taken it yet (the worker confirms with steer_removed). The ×
    // on a row and Shift+Delete both come here. False when there is no such row, or it cannot be
    // removed (what is running is stopped with Esc, not removed).
    bool removeRow(const QString &rowId) {
        if (rowId.startsWith(QStringLiteral("steer:"))) {
            const QString requestId = rowId.mid(6);
            const bool known = std::any_of(m_steering.cbegin(), m_steering.cend(),
                                           [&](const SteerEntry &steer) { return steer.requestId == requestId && !steer.withdraw; });
            if (known) withdrawSteer(requestId);
            return known;
        }
        if (rowId.startsWith(QStringLiteral("entry:"))) {
            bool ok = false;
            const quint64 id = rowId.mid(6).toULongLong(&ok);
            if (!ok || std::none_of(m_entries.cbegin(), m_entries.cend(), [id](const QueueEntry &e) { return e.id == id; })) return false;
            removeEntry(id);
            return true;
        }
        return false;
    }

private:
    // Withdraw a steer the running turn has not taken yet: the × or Shift+Delete on its row
    // (Drop), editing it (Edit: its text is already back in the prompt box) or Ctrl+Down (ToQueue).
    // The row greys out as "withdrawing…" until the worker answers steer_removed; if the turn took
    // it first, steer_delivered prints it into the transcript and the worker says it is no longer
    // queued. A × before the worker named the item is sent when it does (the "queued" event).
    void withdrawSteer(const QString &requestId, SteerEntry::Then then = SteerEntry::Drop) {
        for (auto &steer : m_steering) {
            if (steer.requestId != requestId || steer.withdraw) continue;
            steer.withdraw = true; steer.then = then;
            if (then == SteerEntry::Edit) steer.editText = m_editor->toPlainText();
            if (m_selectedSteer == requestId) {   // its text in the prompt box was the row's
                m_selectedSteer.clear();
                m_editor->clear();
            }
            if (!steer.itemId.isEmpty()) send({{"type", "queue_remove"}, {"item", steer.itemId}, {"id", QStringLiteral("withdraw-") + requestId}});
            status(then == SteerEntry::Edit    ? QStringLiteral("Taking it back to edit · the agent will not get it at its next tool call…")
                   : then == SteerEntry::ToQueue ? QStringLiteral("Moving it back to the queue, to run after this turn…")
                                                 : QStringLiteral("Withdrawing it before the agent's next tool call…"));
            rebuildQueueStrip(); changed();
            return;
        }
    }

    // A steer has left m_steering's rows (delivered, withdrawn, returned, escalated). If it was the
    // selected row its text in the prompt box was the row's, not the user's, so it goes too; any
    // edit would already have taken it back (takeBackEditedSteer). The user is still in the list,
    // so the selection moves to the row that took its place, as after Shift+Delete: otherwise the
    // Esc meant to leave the list would land on an empty box and stop the agent. With no row left,
    // an Esc in the next two seconds only says so (see the queue keys in handleComposerKey).
    void forgetSteer(int index) {
        if (index < 0 || index >= m_steering.size()) return;
        if (m_steering[index].requestId == m_selectedSteer) {
            const int row = selectedQueueRow();
            m_selectedSteer.clear();
            m_editor->clear();
            QTimer::singleShot(0, this, [this, row] {
                if (inQueueSelection() || !m_editor->toPlainText().isEmpty()) return;
                const int rows = int(liveSteers().size() + m_entries.size());
                if (rows > 0) selectQueueRow(std::min(row, rows - 1));
                else m_selectionDroppedAt.start();
            });
        }
        m_steering.removeAt(index);
    }

    // A steer that came back out of the turn becomes the head of the queue, where it runs next.
    void requeueSteer(const SteerEntry &steer) {
        const quint64 selected = selectedEntryId();
        QueueEntry entry; entry.agent = true; entry.text = steer.text; entry.attachments = steer.attachments; entry.cards = steer.cards;
        entry.id = ++m_entrySerial;
        m_entries.prepend(entry);
        keepSelectionOn(selected);   // every queued index just moved down one
    }

    // Turn a queued agent prompt into a steer: out of the queue, delivered inside the running turn
    // at its next tool call. Enter on the empty box right after queuing, Ctrl+Up on the head of the
    // queue and dropping a row above the steers all come here. Returns the steer's request id.
    QString steerQueuedEntry(quint64 id) {
        if (!m_agentBusy) return {};
        const auto at = std::find_if(m_entries.cbegin(), m_entries.cend(), [id](const QueueEntry &e) { return e.id == id; });
        if (at == m_entries.cend() || !at->agent || at->written()) return {};
        const quint64 selected = selectedEntryId();
        const QueueEntry entry = m_entries.takeAt(int(at - m_entries.cbegin()));
        keepSelectionOn(selected);
        if (entry.id == m_lastQueuedEntryId) m_lastQueuedAt.invalidate();
        SteerEntry steer;
        steer.requestId = QStringLiteral("steer-%1").arg(++m_askSerial);
        steer.text = entry.text; steer.attachments = entry.attachments; steer.cards = entry.cards;
        m_steering.append(steer);
        QJsonObject request{{"type", "ask"}, {"id", steer.requestId}, {"text", entry.text}, {"when", "steer"}, {"requeue", false}};
        if (!entry.attachments.isEmpty()) request.insert(QStringLiteral("attachments"), entry.attachments);
        if (!entry.cards.isEmpty()) request.insert(QStringLiteral("cards"), entry.cards);
        if (const QJsonArray skills = skillsFor(entry.text); !skills.isEmpty()) request.insert(QStringLiteral("skills"), skills);
        send(request);
        m_lastSteerRequest = steer.requestId; m_lastSteeredAt.start();
        if (m_entries.isEmpty()) { m_entriesPaused = false; m_pauseReason.clear(); }
        rebuildQueueStrip(); changed();
        toast(QStringLiteral("Steering · delivered at the agent's next tool call · Enter again to interrupt and send now"));
        return steer.requestId;
    }

    // Enter on an empty prompt right after queuing an agent prompt while the agent works: deliver that
    // prompt at the agent's next tool call instead of after the turn.
    bool upgradeLastQueuedToSteer() {
        if (!m_agentBusy || m_entries.isEmpty() || !m_lastQueuedAt.isValid() || m_lastQueuedAt.elapsed() > 15000) return false;
        const QueueEntry &last = m_entries.last();
        if (!last.agent || last.written() || last.id != m_lastQueuedEntryId) return false;
        return !steerQueuedEntry(last.id).isEmpty();
    }

    // A third Enter on the empty prompt box, right after the steer: stop the running turn and run
    // that prompt as its own turn instead.
    bool escalateSteerToInterrupt() {
        if (!m_agentBusy || !m_lastSteeredAt.isValid() || m_lastSteeredAt.elapsed() > 15000) return false;
        if (!sendSteerNow(m_lastSteerRequest)) return false;
        m_lastSteeredAt.invalidate();
        return true;
    }

    // Stop the running turn and run this steer as its own turn instead: the third Enter, or
    // Ctrl+Enter on a selected steer row. The worker decides: once the turn has taken the steer
    // (or given it back) the agent already has the prompt and there is nothing to interrupt for,
    // so it answers steer_escalated {escalated: false}. The reply carries the request id reserved
    // here, which keeps the prompt echo wired up the way startAgentEntry() does.
    bool sendSteerNow(const QString &steerRequest) {
        if (!m_agentBusy) return false;
        const auto pending = std::find_if(m_steering.cbegin(), m_steering.cend(),
            [&](const SteerEntry &steer) { return steer.requestId == steerRequest && !steer.withdraw; });
        if (pending == m_steering.cend()) return false;
        PendingPrompt prompt; prompt.text = pending->text;
        const QString requestId = QStringLiteral("ask-%1").arg(++m_askSerial);
        m_pendingPrompts.insert(requestId, prompt);
        m_interruptPending = true;   // set before the stop, so agent_finished does not pause the queue
        send({{"type", "queue_unsteer"}, {"request", pending->requestId}, {"as_request", requestId}});
        status(QStringLiteral("Interrupting the current turn to send it now…"));
        return true;
    }

    // Ctrl+Enter while a steer row is selected and its text is untouched: that steer, now.
    bool sendSelectedSteerNow() {
        if (m_selectedSteer.isEmpty()) return false;
        const QString requestId = m_selectedSteer;
        if (!sendSteerNow(requestId)) return false;
        leaveQueueSelection();
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
        // Owner, 2026-09-19: the strip carries the open task list too — subagents on the left, the
        // current batch's tasks on the right, one row per (subagent, task) pair, so it shows up
        // with tasks alone and is hidden only when there is neither. Hence the ledger here: the
        // panel reads both models and refresh() is the single entry point for either changing.
        m_agentsPanel = new relay::SubagentsPanel(&m_subagents, &m_ledger, this);
        layout->addWidget(m_agentsPanel);
        m_agentsPanel->onOpen = [this](const QString &id) { openSubagent(id); };
        m_agentsPanel->onOpenPane = [this] { openSubagentPane(); };
        m_agentsPanel->onMouseOpen = [this] {
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.subagentPane"));
            hint(QStringLiteral("subagents.open.mouse"),
                 relay::ShortcutHints::nextTime(keys.isEmpty() ? QStringLiteral("↓ from the prompt, then Enter")
                                                               : keys + QStringLiteral(", or ↓ from the prompt then Enter"),
                                                QStringLiteral("open a subagent's tab")));
        };
        m_agentsPanel->onStop = [this](const QString &id) { stopSubagent(id); toast(QStringLiteral("Stopping ") + id); };
        m_agentsPanel->onExit = [this] { focusInput(); };
        m_agentsPanel->onPickModel = [this](const QString &id, const QPoint &at) { pickSubagentModel(id, at); };
        // The task half of the strip (owner, 2026-09-19): Enter opens the task list on that task, S
        // hands it to a background subagent — the same two acts the floating list offers.
        m_agentsPanel->onOpenTask = [this](const QString &todoId) { openTask(todoId); };
        m_agentsPanel->onRunTaskAsSubagent = [this](const QString &todoId) {
            send({{"type", "todo_subagent"}, {"id", QStringLiteral("req-%1").arg(++m_requestId)}, {"todo_id", todoId}});
            status(QStringLiteral("Handing %1 to a subagent…").arg(todoId));
        };
        m_agentsPanel->onMouseOpenTask = [this] {
            hint(QStringLiteral("tasks.strip.open.mouse"),
                 relay::ShortcutHints::nextTime(QStringLiteral("↓ from the prompt, then → and Enter"),
                                                QStringLiteral("open a task")));
        };
        m_subagents.onChanged = [this] {
            if (m_subagentTabs) m_subagentTabs->syncRows(m_subagents);   // tabs follow the list's rows
            m_agentsPanel->refresh();
            placeSubagentsPanel();
            for (const auto &view : std::as_const(m_subagentViews))
                if (view) if (const auto *row = m_subagents.row(view->agentId())) view->setRow(*row, m_subagents.elapsedNow(*row));
            if (m_modelBox) m_modelBox->setToolTip(modelTooltip(QStringLiteral("Tokens: ") + m_subagents.tokenSplit()));
            refreshBackgroundWait();   // a subagent started or ended (card #V7QD)
            refreshBusyLine();         // the violet waiting line, when no turn of its own runs (#4E13)
        };
        // Only one start and one finish line per subagent reach the terminal, never its tool activity.
        m_subagents.onFinishedCleared = [this] { if (m_subagentTabs) m_subagentTabs->dropEnded(); };
        m_subagents.onInline = [this](const QString &line, const QString &id) {
            ensureLineStart();
            printSubagentLine(line, id);
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

    // ----- "waiting for 2 subagents, 1 job . . ." in the prompt box (cards #V7QD, #KP4M) ----------
    // Owner, 2026-09-18: "if an orchestrator terminal is waiting on subagents, play a … waiting for
    // subagents . . . blinking text in the prompt", and then the same for background jobs. It is the
    // prompt box's own placeholder, not a new widget: a placeholder is read as part of the prompt,
    // and Qt stops drawing it the moment a character is typed, so a steer is never obstructed. The
    // dots grow on a timer that only runs while the line is actually on screen.
    // relay::panestatus::waitingLines decides *whether* it shows; everything here is about drawing it.
    relay::panestatus::Waiting waitingFacts() const {
        relay::panestatus::Waiting w;
        w.subagents = m_subagents.liveCount();
        w.jobs = m_jobs.runningCount();
        w.onSubagents = !m_waitCall.isEmpty() || m_subagents.hasLiveForeground();
        w.onJobs = !m_jobWaitCall.isEmpty();
        w.mainBusy = m_agentBusy;
        return w;
    }

    void refreshBackgroundWait() {
        if (!m_editor) return;
        // An AI ghost suggestion owns the placeholder while it is up (updateGhost). Leave it be;
        // updateGhost calls this again when it hands the placeholder back.
        if (m_editor->placeholderText().isEmpty() && !m_savedPlaceholder.isEmpty()) {
            if (m_waitDots) m_waitDots->stop();
            return;
        }
        // The desktop's "do not blink" (a cursor flash time of 0) is this app's reduce-motion
        // signal — RichEditor::setCaretColor already takes the caret's blink from it.
        const bool animate = QApplication::cursorFlashTime() > 0;
        // A question card owns the prompt box while it is up: what the box invites you to type is
        // the answer, not "waiting for 2 subagents" (#MQ9C).
        const QStringList lines = m_ask.open()
            ? questionPlaceholders()
            : relay::panestatus::waitingLines(waitingFacts(), animate ? m_waitPhase : -1);
        if (lines.isEmpty()) {
            if (m_waitDots) m_waitDots->stop();
            m_waitPhase = 0;
            if (m_waitShown) {   // only ever put back a placeholder this took away
                m_waitShown = false;
                m_editor->setPlaceholders({});
                m_editor->setAccessibleDescription(QString());
            }
            return;
        }
        m_waitShown = true;
        m_editor->setPlaceholders(lines);
        m_editor->setAccessibleDescription(lines.first().trimmed());
        // Nothing is drawn over typed text, so the timer has nothing to animate: stop it and let
        // the next keystroke (updateGhost) start it again once the box is empty.
        if (!animate || m_ask.open() || !m_editor->toPlainText().isEmpty()) {
            if (m_waitDots) m_waitDots->stop();
            return;
        }
        if (!m_waitDots) {
            m_waitDots = new QTimer(this);
            m_waitDots->setInterval(600);   // gentle: four steps, a little over two seconds a cycle
            connect(m_waitDots, &QTimer::timeout, this, [this] { ++m_waitPhase; refreshBackgroundWait(); });
        }
        if (!m_waitDots->isActive()) m_waitDots->start();
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
            if (!guestOfPreset(model.first).isEmpty()) continue;   // a guest runs no subagent (29.3)
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

    // The strip under the prompt (subagents on the left, the open tasks on the right) shows only
    // while the composer does — with the composer hidden there is no prompt for it to hang under.
    void placeSubagentsPanel() {
        if (!m_agentsPanel) return;
        m_agentsPanel->setAllowed(!m_composer || m_composer->isVisible());
        if (m_jobsPanel) m_jobsPanel->setAllowed(!m_composer || m_composer->isVisible());
        placeQueueStrip();
    }

    // While the subagent pane is open the list is one line that names the key to reach it.
    void foldAgentsStrip() {
        if (!m_agentsPanel) return;
        m_agentsPanel->setFolded(m_subagentTabs != nullptr, Keymap::instance().shortcutText(QStringLiteral("agent.subagentPane")));
        placeQueueStrip();
    }

    // A ✦ start or finish line that is also a terminal hyperlink (OSC 8) to relay://subagent/<pane>/<id>:
    // a click opens the subagent's tab, like the strip row. Printed plain while a program runs.
    void printSubagentLine(const QString &line, const QString &id) {
        beginBlock(relay::gaps::Block::Call);   // subagent lines sit with the tool rows (#5AWD)
        if (id.isEmpty() || !shellIdleAtPrompt()) { printInline(line + '\n', Ink::Note); return; }
        const QByteArray url = QStringLiteral("relay://subagent/%1/%2").arg(m_token, QString::fromUtf8(QUrl::toPercentEncoding(id))).toUtf8();
        QByteArray out = takeWrapped();
        if (!m_inlineOpen) { out += "\r\x1b[2K"; m_inlineOpen = true; m_atLineStart = true; holdShellResize(true); }
        if (!m_atLineStart) out += "\r\n";
        out += "\x1b]8;;" + url + "\x1b\\" + inkCode(Ink::Note) + sanitize(line).toUtf8() + "\x1b[0m" + "\x1b]8;;\x1b\\" + "\r\n";
        m_atLineStart = true;
        writeTerminal(out);
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
        m_jobs.onChanged = [this] {
            m_jobsPanel->refresh(); placeQueueStrip();
            refreshBackgroundWait();   // a job was handed back or ended (card #KP4M)
        };
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
        openToolOutput({{"name", "command"}, {"call_id", event.value(QStringLiteral("job_id"))}, {"preview", preview}, {"result", result}});
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
            // The task half of the strip under the prompt. The guard is required: this lambda is
            // installed while the composer is still being built, long before setupSubagentsUi().
            if (m_agentsPanel) { m_agentsPanel->refresh(); placeSubagentsPanel(); }
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
        // The Switchboard sentence only in a pane whose tab is attached to a project: with no
        // project there is no board for that key to open, and the chip must not imply one (#JN7X).
        const QString keys = hasBoard() ? Keymap::instance().shortcutText(QStringLiteral("board.open")) : QString();
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
                                           this, [this] {
                                               openRequests();
                                               // The slow path to a task now has a fast one: the strip under the prompt.
                                               hint(QStringLiteral("tasks.strip.open.mouse"),
                                                    relay::ShortcutHints::nextTime(QStringLiteral("↓ from the prompt, then →"),
                                                                                   QStringLiteral("the open tasks are under the prompt")));
                                           });
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
            // Tasks and subagents (card #QHR1): open a task's subagent, or hand a task to a new one.
            panel->onOpenSubagent = [this](const QString &id, bool mouse) {
                openSubagent(id);
                if (mouse) hint(QStringLiteral("tasks.subagent.open.mouse"),
                                relay::ShortcutHints::nextTime(QStringLiteral("Enter"), QStringLiteral("on a task opens its subagent")));
            };
            panel->onRunAsSubagent = [this](const QString &todoId, bool mouse) {
                send({{"type", "todo_subagent"}, {"id", QStringLiteral("req-%1").arg(++m_requestId)}, {"todo_id", todoId}});
                status(QStringLiteral("Handing %1 to a subagent…").arg(todoId));
                if (mouse) hint(QStringLiteral("tasks.subagent.run.mouse"),
                                relay::ShortcutHints::nextTime(QStringLiteral("S"), QStringLiteral("on a task runs it as a subagent")));
            };
        }
        send({{"type", "requests"}, {"id", QStringLiteral("req-%1").arg(++m_requestId)}});
        send({{"type", "todos"}, {"id", QStringLiteral("req-%1").arg(++m_requestId)}});
        m_requestsPanel->refresh();
        placeRequestsPanel();
        m_requestsPanel->show();
        m_requestsPanel->raise();
        m_requestsPanel->enter();
    }

    // Enter on a task in the strip: the task list, opened on that task.
    void openTask(const QString &todoId) {
        openRequests();
        if (m_requestsPanel && !todoId.isEmpty()) m_requestsPanel->select(todoId);
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
        if (type == QStringLiteral("todo_subagent")) {   // the worker started a subagent for a task (card #QHR1)
            status(QStringLiteral("%1 is with subagent %2 · Enter on it in the task list opens it")
                       .arg(event.value(QStringLiteral("todo_id")).toString(), event.value(QStringLiteral("agent_id")).toString()));
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
        // Programs the agent starts — tmux, Chrome — move themselves into their own app.slice
        // scopes over the session bus (StartTransientUnit), escaping this pane's memory cap
        // (card #Y4RX). Without a session bus address they stay in the pane scope; the worker
        // itself does not use D-Bus, and agent-run systemd-run --user is not supported anyway.
        if (isolation::enabled() && isolation::available())
            environment.remove(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"));
        m_worker.setProcessEnvironment(environment);
        relay::log::info(QStringLiteral("worker_start pane=%1 workspace_set=%2")
                             .arg(paneLogId()).arg(m_workspace.isEmpty() ? 0 : 1));
        m_agentUnit.clear();
        if (isolation::enabled() && isolation::available()) {
            m_agentUnit = QStringLiteral("relay-pane-%1-agent-%2").arg(m_token.left(8)).arg(++m_agentGeneration);
            m_worker.setProgram(QStandardPaths::findExecutable(QStringLiteral("systemd-run")));
            m_worker.setArguments(isolation::wrap(m_agentUnit, {QStringLiteral("MemoryMax=") + isolation::memory("isolation/agent_memory_max", isolation::agentDefault()),
                                                                QStringLiteral("MemorySwapMax=") + isolation::memory("isolation/agent_swap_max", isolation::agentSwapDefault()),
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
            // A `route` that was in flight will never be answered. Left behind, m_pendingSubmit
            // is a one-submission guard that no reply can ever release, so every later terminal
            // submit in this pane is dropped in silence — the worst version of the bug the local
            // dispatch above exists to prevent.
            m_pendingSubmit.clear(); m_previewId.clear(); m_heldDecision = QJsonObject();
            // Nobody is left to answer to (#MQ9C): take the card down rather than leave the pane
            // asking on behalf of a worker that is gone.
            closeQuestion(QStringLiteral("the agent worker stopped"));
            stopTurnClock();
            relay::log::error(QStringLiteral("worker_exit pane=%1 code=%2 crashed=%3")
                                  .arg(paneLogId()).arg(code).arg(exit == QProcess::CrashExit ? 1 : 0));
            if (m_closing) return;
            const bool oom = isolation::takeResult(m_agentUnit) == QStringLiteral("oom-kill");
            const bool killed = exit == QProcess::CrashExit || code == 137 || code == 143;
            showBanner(oom ? QStringLiteral("The agent worker stopped because it ran out of memory (limit %1).")
                                 .arg(isolation::memory("isolation/agent_memory_max", isolation::agentDefault()))
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
        // The guest event channel (GT7X, protocol 26.3): the spool directory the shims a guest's
        // settings call (relay_core.guest_hook) write their events into, beside the runtime dir
        // above. Its presence in the environment is also what the installed hook commands test
        // before they run anything at all, so a claude started outside a pane costs nothing.
        const QString events = guestEventsDir();
        QDir().mkpath(events);
        QFile::setPermissions(events, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QDir().mkpath(guestAnswersDir());
        QFile::setPermissions(guestAnswersDir(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("RELAY_GUEST_EVENT", events.toUtf8());
        // The hook commands run the shim by absolute path (26.4), so no PYTHONPATH is needed for
        // them: "$RELAY_BACKEND_DIR/relay_core/guest_hook.py".
        const QString backendDir = m_data + QStringLiteral("/backend");
        qputenv("RELAY_BACKEND_DIR", backendDir.toUtf8());
        // PYTHONPATH still carries the backend so a user's own script can `import relay_core`.
        // It is appended, never prepended: the user's own PYTHONPATH keeps its order and cwd
        // still wins over both. Appended **only when it is not already there**: qputenv writes
        // Relay's own environment and startTerminal runs once per pane and once per shell
        // restart, so the plain append grew the variable by one copy every time (review of
        // 51587e3) — a hundred shell restarts, a hundred copies, in every child process.
        const QString pythonPath = qEnvironmentVariable("PYTHONPATH");
        if (!pythonPath.split(QLatin1Char(':'), Qt::SkipEmptyParts).contains(backendDir))
            qputenv("PYTHONPATH", (pythonPath.isEmpty() ? backendDir : pythonPath + QLatin1Char(':') + backendDir).toUtf8());
        qputenv("RELAY_PYTHON", m_python.toUtf8());
        qputenv("RELAY_CLEAN_SHELL", cleanShell ? "1" : "0");
        // Opt-in OSC 7 / OSC 133 marks (shell/relay-integration.bash). Relay's own engine
        // tracks the working directory and command boundaries from them.
        qputenv("RELAY_SHELL_INTEGRATION",
                QSettings().value(QStringLiteral("terminal/shell_integration"), false).toBool() ? "1" : "0");
        // The ssh and mosh wrappers (shell/integration.bash, card #S5SH) share the login's
        // connection through a socket here, so the agent can reuse it. Off in Options › Terminal.
        const bool wanted = QSettings().value(QStringLiteral("ssh/enhance"), QStringLiteral("auto")).toString() != QStringLiteral("off");
        QString why;
        const bool wrapSsh = wanted && sshSocketDirReady(&why);
        // A master killed outright leaves its socket behind; nothing else would ever remove it.
        // Once per run, before the first pane's shell: a socket someone answers on is left alone.
        static bool sweptSockets = false;
        if (wrapSsh && !sweptSockets) {
            sweptSockets = true;
            if (const int gone = relay::remote::pruneSockets(sshSocketDir()); gone > 0)
                relay::log::info(QStringLiteral("ssh_sockets_pruned pane=%1 count=%2").arg(paneLogId()).arg(gone));
        }
        if (wanted && !wrapSsh) {
            // Silence here reads later as "the agent cannot reach the host" with no reason given.
            relay::log::error(QStringLiteral("ssh_share_off pane=%1 reason=%2").arg(paneLogId(), why));
            m_sshShareProblem = why;
        }
        qputenv("RELAY_SSH_WRAP", wrapSsh ? "1" : "0");
        qputenv("RELAY_SSH_DIR", sshSocketDir().toUtf8());
        // The Claude IDE bridge (GT7X, 26.9): the shell carries *no* bridge variables. They go on
        // the one command line the pane types when Claude Code is picked (`launchGuest`), so no
        // other program in this shell, and no shell started later, ever sees a port that may since
        // have gone away — the qputenv injection this replaced had to clear the keys on every
        // start for exactly that reason. The keys are still cleared here, once, because they may
        // be in Relay's own inherited environment (a Relay started from an IDE's terminal).
        for (const QString &key : relay::guestbridge::bridgeEnvKeys()) qunsetenv(key.toUtf8().constData());
        // Registered now, not at the first prompt, when a sidecar is running: a claude typed at
        // that prompt after a launch elsewhere must still find a routable pane (26.5).
        registerWithBridge();
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
                               .arg(isolation::memory("isolation/shell_memory_max", isolation::shellDefault())),
                           QStringLiteral("Restart shell"), [this] { restartShell(); });
                return;
            }
            if (onShellExited) QTimer::singleShot(0, this, [this] { if (onShellExited) onShellExited(); });
        };
        // Alternate screen (vim, less, htop, tmux), reported by the emulator itself.
        m_backend->onAltScreenChanged = [this](bool active) { onPrimaryScreen(!active); };
        // OSC 7 from the shell integration (engine panes; see shell/relay-integration.bash).
        // A folder on another machine (OSC 7 from a shell behind ssh names its host) is the
        // login's, never this pane's local directory, even when the same path exists here.
        m_backend->onCwdHostChanged = [this](const QString &path, const QString &host) {
            if (m_login.active) { if (m_login.cwd != path) { m_login.cwd = path; changed(); } return; }
            if (!relay::remote::isLocalHost(host, QSysInfo::machineHostName())) return;
            if (path.isEmpty() || path == m_cwd || !QFileInfo(path).isDir()) return;
            m_cwd = path; updatePaths(); changed();
        };
        // OSC 133 prompt marks: Relay keeps its own command state from the Bash bridge, so the
        // marks are only remembered here (engine panes use them to jump between prompts).
        m_backend->onPromptMark = [this](char kind, int exitCode) {
            m_lastPromptMark = kind;
            if (kind == 'D') m_lastMarkExitCode = exitCode;
            // Marks while a login owns the terminal come from the remote shell: they say exactly
            // when it is at its prompt, without waiting for the screen poll.
            if (m_login.active) { m_login.integration = true; updateLoginPrompt(); }
        };
        // Output of the commands Relay itself ran, for the conversation index (protocol 14).
        // Only enabled between "command loaded" and "shell ready", so it costs nothing otherwise.
        m_backend->setOutputCallbackEnabled(false);
        m_backend->onOutput = [this](const QByteArray &bytes) {
            // While a login runs, keep the row the host is writing. At its prompt that row is the
            // prompt, and Relay prints it back in its own colours after printing over it (#S5SH).
            if (m_login.active) {
                for (const char c : bytes) {
                    if (c == '\n' || c == '\r') m_login.line.clear();
                    else if (m_login.line.size() < 8192) m_login.line += c;
                }
            }
            if (!m_capturing || m_capture.size() >= kCommandCaptureCap) return;
            m_capture.append(bytes.left(kCommandCaptureCap - m_capture.size()));
        };
        // Clickable paths (issue YZTK): a file opens in a preview pane at its line, a folder in
        // an explorer pane, a URL in the browser. The engine only reports paths that exist.
        m_backend->onLinkActivated = [this](const QString &target, int line, int column) {
            Q_UNUSED(column);
            // The engine links paths that exist here; inside a login the output is the remote
            // machine's, and the same path here is a different file (card #S5SH). URLs still open.
            const QUrl url(target);
            if (m_login.active && (url.scheme().isEmpty() || url.isLocalFile())) {
                openRemoteOutputPath(target, line);
                return;
            }
            openOutputTarget(target, line, true);
        };
        // Which paths in the output are real, and where a relative one is relative to (#S5SH):
        // this machine, until the pane is logged into another one — then the host's own answer,
        // from the batched cache in remoteLinkProbe(). Set once; the login is checked per call.
        m_backend->setLinkProbe([this](const QString &path) { return remoteLinkProbe(path); },
                                [this] { return m_login.active ? m_login.cwd : QString(); });
        // `#K7Q2` in the output is a card link when this pane's Switchboard index knows the id
        // (design section 5); the engine asks, the pane answers from the rows it has seen.
        m_backend->setCardLookup([this](const QString &id, QString *title) { return lookupOutputCard(id, title); });
        // Tool-call lines fold their detail open in place (#TK9C, docs/ENGINE.md "Folds"): every
        // OSC 8 URI under this prefix is an anchor the view owns, and a click on one that has no
        // content yet comes back here for it. A backend without the capability ignores both, and
        // the lines are anchored to relay://open-call instead (callAnchor).
        // OSC 52 (Options › Security, card #3KB7): a program — or a command the agent runs — may
        // put text on the system clipboard. Off unless the user turned it on, because output is
        // untrusted and would otherwise be able to replace what they are about to paste. Reads are
        // never answered and have no setting.
        applyClipboardPolicy();
        if (terminalCan(relay::TerminalBackend::Folds)) {
            m_backend->setFoldPrefix(QString(relay::calllines::kFoldPrefix));
            m_backend->onFoldRequested = [this](const QString &uri) { foldRequested(uri); };
        }
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
                {QStringLiteral("MemoryMax=") + isolation::memory("isolation/shell_memory_max", isolation::shellDefault()),
                 QStringLiteral("MemoryHigh=") + isolation::memory("isolation/shell_memory_high",
                 isolation::fractionOf(isolation::memory("isolation/shell_memory_max", isolation::shellDefault()), 80)),
                 QStringLiteral("MemorySwapMax=") + isolation::memory("isolation/shell_swap_max", isolation::shellSwapDefault()),
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
        // Again, now that there is a shell pid to register: the first registration above happened
        // before the backend existed, and the sidecar routes a claude by the pid ancestry that
        // leads back to this shell (26.5).
        registerWithBridge();
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
            // A question card is up: this text is the answer, not a prompt and not a command
            // (#MQ9C). Nothing below runs — not `@path`, not `/commands`, not the router. An
            // explicit terminal submit (Ctrl+Shift+Enter, or Enter with the chip on TERMINAL) is
            // the exception: a question from the agent must not take the user's terminal away,
            // and the card's own footer says so.
            const bool toShell = (overrideMode == QStringLiteral("auto") ? m_modeValue : overrideMode)
                                 == QStringLiteral("shell");
            if (m_ask.open() && !toShell && !m_editor->toPlainText().trimmed().isEmpty()) {
                const QString typed = m_editor->toPlainText().trimmed();
                m_editor->remember(typed);
                m_editor->clear();
                answerQuestion(typed);
                return;
            }
            // `@path` on its own opens the file (or folder) in a Relay pane.
            static const QRegularExpression only(QStringLiteral("^@(?:\"([^\"]+)\"|(\\S+))$"));
            const auto match = only.match(m_editor->toPlainText().trimmed());
            if (m_guest.isEmpty() && match.hasMatch()) {
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
            if (tryRunSkillSlash(m_editor->toPlainText())) return;
            // A guest pane (26.8): the prompt box, the mode chip and the auto router work exactly
            // as in any other pane; only the delivery differs. A line that is a terminal command —
            // terminal mode, Ctrl+Shift+Enter, a typed `!`, or (below) the router's `shell`
            // verdict — is typed into the guest as `!<command>`, which both TUIs run as a local
            // shell line; a prompt is typed as the prompt (owner, 2026-09-19: "relay terminal
            // commands are piped to the agent as '! …' to maintain a seamless / identical
            // experience"). Relay's built-ins, aliases and skills above always win; any other
            // `/command` is the guest's own, never rejected by Relay. A guest the pane has moved
            // on from (a model was picked while it worked) gets nothing more: the line is the new
            // model's, exactly as after a native switch.
            if (guestInFront()) {
                QString line = m_editor->toPlainText();
                const QString mode = overrideMode == QStringLiteral("auto") ? m_modeValue : overrideMode;
                const bool bang = line.startsWith(QLatin1Char('!'));
                if (bang) line = line.mid(1);
                if (bang || m_prefixMode == QStringLiteral("shell") || mode == QStringLiteral("shell")) {
                    if (line.trimmed().isEmpty()) {
                        status(QStringLiteral("Type a terminal command after !."));
                        return;
                    }
                    submitGuest(QLatin1Char('!') + line.trimmed(), true);
                    if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
                    return;
                }
                if (mode == QStringLiteral("agent") || m_prefixMode == QStringLiteral("agent") || !m_workerReady
                    || line.trimmed().startsWith(QLatin1Char('/'))) {
                    submitGuest(line, true);
                    if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
                    return;
                }
                // Auto: the local router decides below, and dispatch() delivers its verdict.
            }
            // A `/command` that is not one of the above never reaches the router: Relay says so
            // itself rather than letting Bash answer with "command not found".
            if (reportUnknownSlashCommand(m_editor->toPlainText())) return;
            clearAiGhost();
        } else if (const SlashCommand *command = slashCommandFor(m_editor->toPlainText())) {
            setRouteText(QStringLiteral("COMMAND · /%1 · %2").arg(command->name, command->description));
            return;
        } else if (const QString skill = skillFor(m_editor->toPlainText()); !skill.isEmpty()) {
            setRouteText(QStringLiteral("SKILL · /%1 · %2").arg(skill, skillCommand(skill)->description));
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
        QString mode = overrideMode == QStringLiteral("auto") ? m_modeValue : overrideMode;
        // An explicit agent submit never needed the worker's router (#N8VK): the verdict cannot
        // change where a forced-agent prompt goes, and the round trip made it refusable ("Local
        // router is not ready" while the worker was still starting) and losable ("Input changed
        // during routing") for a prompt whose destination was never in question. An explicit
        // *terminal* submit never needed it either, and that half was missed: with the worker gone
        // the whole prompt box went with it, `!echo …` included, although the shell was never the
        // worker's to lend. Both are dispatched locally below; only `auto`, which has a real
        // question for the router, is refused — and it says what is wrong and how to send anyway.
        const bool routerDown = !m_workerReady;
        if (submit && mode != QStringLiteral("agent")
            && (!m_pendingSubmit.isEmpty() || !m_heldDecision.isEmpty() || m_loading)) return;
        const QString id = QString::number(++m_requestId);
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
        // The explicit agent submit itself (#N8VK): straight to submitAgent(), which starts the
        // turn at once whenever the agent is free (relay::queuesubmit) instead of waiting on a
        // route reply. The handoff flags are consumed here as dispatch() would consume them.
        if (submit && mode == QStringLiteral("agent")) {
            const QString typed = m_editor->toPlainText();
            if (typed.trimmed().isEmpty()) return;
            m_handoffChain = 0;   // the user typed something: a chain of hand-overs starts over
            m_handoffPrefill = false; m_handoffPrefix = false; m_handoffOffered = false;
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);   // one submission only
            setRouteText(QStringLiteral("AGENT · explicit"));
            submitAgent(typed, true);
            return;
        }
        // The explicit terminal submit with no worker to ask. The router's only contribution to a
        // forced-shell line is the syntax check that offers a broken command to the agent; with no
        // agent to offer it to, the shell makes that judgement itself, exactly as it does for
        // anything typed natively. dispatch() then takes it down the ordinary terminal path —
        // login, queue, hand-off and all — so nothing else about the line changes.
        if (submit && routerDown
            && relay::input::withoutRouter(mode) == relay::input::WithoutRouter::Shell) {
            const QString typed = m_editor->toPlainText();
            if (typed.trimmed().isEmpty()) return;
            m_handoffChain = 0;   // the user typed something: a chain of hand-overs starts over
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);   // one submission only
            setRouteText(QStringLiteral("TERMINAL · explicit"));
            m_submitMode = mode; m_submittedDraft = typed;
            dispatch(QJsonObject{{"route", QStringLiteral("shell")}, {"text", typed}, {"valid", true}}, mode);
            return;
        }
        // Everything left is `auto` (relay::input::WithoutRouter::Refuse — Shell and Agent were
        // taken above), and only the router can answer it. Say what is wrong, name the banner's
        // own action and the key that sends this very line to the terminal anyway.
        if (routerDown) {
            if (submit)
                status(relay::input::noRouterText(Keymap::instance().shortcutText(QStringLiteral("pane.restartShell")),
                                                  QStringLiteral("Ctrl+Shift+Enter")));
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
        QJsonObject route{{"type", "route"}, {"id", id}, {"text", m_editor->toPlainText()}, {"mode", mode},
                          {"known_commands", m_knownCommands}, {"path", m_shellPath}, {"cwd", m_cwd}};
        // At a login the local PATH and aliases describe the wrong machine: the router only
        // decides between a line for the remote shell and a request for the agent (#S5SH).
        if (loginTakesLines()) route.insert(QStringLiteral("remote"), QJsonObject{{"host", loginHost()}});
        send(route);
    }

    // The rule for a submitted line while a foreground program is running: it is written to
    // that program's stdin when the terminal is in canonical (line) mode and a process of the
    // command is blocked reading it. Anything else keeps the existing behaviour — run it now,
    // or queue it until the terminal is free. Never remembered (relay::input::retainable).
    bool sendLineToProgram(const QString &mode) {
        if (m_native || !m_backend || m_secretMode) return false;
        // A question on the screen of a login ("Do you want to continue? [Y/n]" from a remote
        // apt): the local terminal is raw, so only the screen says a line is wanted (#S5SH).
        if (loginTakesLines() && mode != QStringLiteral("agent") && m_screenPrompt.actionable() && !m_screenPrompt.masked) {
            typeIntoLogin(m_editor->toPlainText());
            return true;
        }
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
            setSkillCommands(event.value(QStringLiteral("skill_commands")).toArray());
            // Model roles (protocol 13): the worker reports the effective model of every role and
            // which role this pane runs (a role that could not be used falls back to "main").
            m_roleSummary = event.value(QStringLiteral("roles")).toObject();
            m_tierSummary = event.value(QStringLiteral("tiers")).toObject();
            if (m_rolesDialog) m_rolesDialog->setResolved(m_tierSummary, m_roleSummary);
            m_agentRole = event.value(QStringLiteral("agent_role")).toString(QStringLiteral("main"));
            onSessionConfigured(event);
            noteGuestPreset(event);   // Tier A (29.4): the guest is this pane's agent from here on
            discloseHosted();   // Relay Free: where the prompts go, said once per installation
            runBoardTask();   // a card handed over by the Switchboard's Execute, if any (#XS6Q)
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
            bool hostedUnavailable = false;
            for (const auto &item : m_presets) {
                const auto preset = item.toObject();
                // A model server on this machine (card #24XJ) needs no key, so `local` makes a row
                // selectable just as a stored key does. The worker appends those rows after the
                // keyed presets, so they stay last in this list. Relay Free (`hosted`, protocol
                // 13.8) needs none either, while the worker reports it `available`.
                const bool hosted = preset.value(QStringLiteral("hosted")).toBool();
                if (hosted && !preset.value(QStringLiteral("available")).toBool()) hostedUnavailable = true;
                // A guest (protocol 29.3) needs no key either: `harness` is the worker saying it
                // can run this guest's headless harness here, which is what makes the row usable.
                // A guest row without it is not a row of this box at all — the picker offers that
                // guest the Tier B way (26.9) instead.
                if (preset.value(QStringLiteral("has_stored_key")).toBool()
                    || preset.value(QStringLiteral("local")).toBool()
                    || preset.value(QStringLiteral("harness")).toBool()
                    || (hosted && preset.value(QStringLiteral("available")).toBool()))
                    m_stored.append({preset.value(QStringLiteral("id")).toString(), preset.value(QStringLiteral("label")).toString()});
                // The allowance the worker last saw, so the chip has a figure before the first call.
                if (hosted && preset.value(QStringLiteral("quota")).isObject()) setHostedQuota(preset.value(QStringLiteral("quota")).toObject());
            }
            if (m_keysDialog) m_keysDialog->setPresets(providerPresets());
            if (m_rolesDialog) m_rolesDialog->setPresets(providerPresets(), m_tierCatalog, m_roleActions);
            changed();
            // A pane opened for a guest sessions row (29.4) could not know until now whether the
            // guest can be its agent. It does now: the preset, or the Tier B launch.
            if (!m_pendingGuestResume.guest.isEmpty()) {
                const PendingGuestResume pending = m_pendingGuestResume;
                m_pendingGuestResume = PendingGuestResume();
                startGuestPreset(pending.guest, pending.extra, pending.cwd);
            }
            // …and so could a pane opened by Verify for a guest verifier (#T71W).
            if (!m_pendingGuestTask.guest.isEmpty()) {
                const PendingGuestTask pending = m_pendingGuestTask;
                m_pendingGuestTask = PendingGuestTask();
                startGuestBoardTask(pending.guest, pending.task, pending.card);
            }
            if (m_stored.isEmpty()) {
                status(hostedUnavailable
                           ? QStringLiteral("No stored provider keys, and Relay Free needs python3-cryptography. "
                                            "Open Options › Models › API keys… to add a key or import from Warp.")
                           : QStringLiteral("No stored provider keys. Open Options › Models › API keys… to add one or import from Warp."));
                return;
            }
            if (!m_configured && !m_configuring) {
                auto usable = [this](const QString &id) {
                    return std::any_of(m_stored.cbegin(), m_stored.cend(), [&](const auto &entry) { return entry.first == id; });
                };
                // A restored pane keeps the model it had; otherwise the saved choice, then Warp's
                // default agent model, then the first stored key, then Relay Free, then a local
                // endpoint. A key of the user's own always wins over the included allowance, so a
                // user with any BYOK key is untouched and a fresh install lands on Relay Free and
                // configures at once. A local endpoint never wins over either (card #24XJ): it is
                // picked on its own only when it is the restored/saved preset, or when nothing else
                // is usable at all.
                QString choice = m_restorePreset;
                m_restorePreset.clear();
                if (!usable(choice)) choice = QSettings().value(QStringLiteral("provider/preset")).toString();
                if (!usable(choice)) choice = event.value(QStringLiteral("warp_default")).toString();
                if (!usable(choice)) {
                    auto firstWhere = [this](auto predicate) {
                        const auto found = std::find_if(m_stored.cbegin(), m_stored.cend(), [&](const auto &entry) {
                            return predicate(presetById(entry.first));
                        });
                        return found == m_stored.cend() ? QString() : found->first;
                    };
                    // A guest row is never picked for a pane that asked for nothing (29.4):
                    // starting Claude Code because it happens to be installed is not a default.
                    // It is reached only as the restored or saved choice above, or by hand.
                    auto guestRow = [](const QJsonObject &preset) { return preset.value(QStringLiteral("harness")).toBool(); };
                    choice = firstWhere([&](const QJsonObject &preset) {
                        return !preset.value(QStringLiteral("local")).toBool() && !preset.value(QStringLiteral("hosted")).toBool()
                               && !guestRow(preset);
                    });
                    if (choice.isEmpty()) choice = firstWhere([](const QJsonObject &preset) { return preset.value(QStringLiteral("hosted")).toBool(); });
                    if (choice.isEmpty()) choice = firstWhere([&](const QJsonObject &preset) { return !guestRow(preset); });
                }
                if (choice.isEmpty()) {   // guests only: the pane has no provider of its own yet
                    status(QStringLiteral("No stored provider keys. Open Options › Models › API keys… to add one or import from Warp."));
                    return;
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
            m_shareFailed = false;      // the pane is in use again; the last failure is history
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
            // Status glyphs (#XM0T): the window reads these to show done / failed / needs you
            // until the user has looked at the pane.
            ++m_finishSerial; m_lastOutcome = outcome;
            // What a phone is told (shareStatus, protocol 6.3) and what rings it
            // (remote/notify.py's `failed` trigger). "cancelled" is the user stopping their own
            // turn, which is not news to them.
            m_shareFailed = outcome == QStringLiteral("error");
            // "Asked" also covers a command the agent left in the prompt box and waits on.
            m_lastAsked = outcome == QStringLiteral("done")
                          && (relay::panestatus::endsWithQuestion(m_turnText) || (m_handoffOffered && m_handoffPrefill));
            // Notified only for news the user was not watching (#XM0T): a turn that ended by
            // asking them something, a finished turn, a failed one.
            if (outcome == QStringLiteral("done")) {
                ++m_turnsCompleted;
                if (window() && !window()->isActiveWindow()) m_finishedWhileAway = true;
                if (!watched() && !moreTurnsPending())
                    notify(m_lastAsked ? QStringLiteral("Agent needs you") : QStringLiteral("Agent finished"), turnSummary(),
                           m_lastAsked ? relay::NotificationCenter::kindWarning : relay::NotificationCenter::kindSuccess);
            } else if (outcome == QStringLiteral("error") && !watched()) {
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
            // poured into the pane. Card #X5D1 read an earlier owner decision as "print all of it";
            // the owner corrected that on 2026-09-17. What the count is for changed with #TK9C: the
            // call's own line carries it live, and its fold holds the text. Agent options › Show
            // tool output brings the stream back — and then the line is final where it stands,
            // because the output below it is where the cursor now is.
            const QString text = event.value(QStringLiteral("text")).toString();
            m_toolLines += text.count('\n');
            m_toolPartialLine = !text.isEmpty() && !text.endsWith('\n');
            if (showToolOutput()) { turnHeader(); printInline(text, Ink::ToolOutput); }
            else if (!m_liveCall.isEmpty() && shellIdleAtPrompt()) {
                // The running row counts what the command has printed. Throttled to about ten
                // rewrites a second: a build that prints a thousand lines must not repaint a row a
                // thousand times.
                if (!m_liveTick.isValid() || m_liveTick.elapsed() >= 100) {
                    m_liveTick.restart();
                    const int lines = m_toolLines + (m_toolPartialLine ? 1 : 0);
                    const relay::calllines::Step step =
                        m_callCursor.live(m_liveCall, m_liveLabel, lines, callLineCells());
                    if (!step.nothing) drawCallRow(step, m_liveTurn, callAnchor(step, m_liveTurn, m_liveLabel));
                }
            }
        } else if (type == QStringLiteral("tool_started")) {
            turnHeader();
            m_toolLines = 0; m_toolPartialLine = false;
            const QString call = event.value(QStringLiteral("call_id")).toString();
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            const relay::toollabel::Label label = relay::toollabel::fromEvent(event);
            // Wrong-mode hints are unchanged: they key off this run_command's full text, kept by
            // call_id for the tool_result handler. § 23.1 still sends `preview`, which is where the
            // text is; nothing else is parsed out of it any more.
            if (event.value(QStringLiteral("tool")).toString() == QStringLiteral("run_command")
                || label.kind == QStringLiteral("run") || label.kind == QStringLiteral("job")) {
                const QString command = runCommandFromPreview(event.value(QStringLiteral("preview")).toString());
                if (!command.isEmpty()) m_runCommands.insert(call, command);
            }
            m_liveCall = call; m_liveTurn = turn; m_liveLabel = label;
            m_liveTick.invalidate();
            if (m_agentBusy) tickTurnClock();   // the busy line says this call's gerund now (#4E13)
            // The two calls that block the main agent on the background work it started, so that
            // the prompt box can say so until the call lands: agent_wait (#V7QD) and command_output,
            // which waits on a job (#KP4M).
            const QString liveTool = event.value(QStringLiteral("tool")).toString();
            if (liveTool == QStringLiteral("agent_wait") || liveTool == QStringLiteral("command_output")) {
                (liveTool == QStringLiteral("agent_wait") ? m_waitCall : m_jobWaitCall) = call;
                refreshBackgroundWait();
                tickTurnClock();
            }
            // Deferred while a program owns the terminal: a pending line can only be replayed in
            // its final form, so nothing is drawn until the result arrives.
            if (shellIdleAtPrompt()) {
                m_callCursor.setCells(callLineCells());
                beginBlock(relay::gaps::Block::Call);   // a blank line after prose, none inside a run (#5AWD)
                const relay::calllines::Step step = m_callCursor.start(call, label);
                if (!step.nothing) drawCallRow(step, turn, callAnchor(step, turn, label));
                // With the stream on, the output goes under the line: the row is finished here.
                if (showToolOutput()) endCallRun();
            }
        } else if (type == QStringLiteral("tool_result")) {
            const auto result = event.value(QStringLiteral("result")).toObject();
            const QString call = event.value(QStringLiteral("call_id")).toString();
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            // Wrong-mode hints: a run_command that failed, while in agent mode, whose text is the
            // prompt the turn started from, means the submission was a shell command in the wrong
            // mode. At most once per turn.
            if (event.value(QStringLiteral("tool")).toString() == QStringLiteral("run_command")) {
                const QString runText = m_runCommands.take(call);
                if (!m_modeHintShown && !runText.isEmpty() && result.value(QStringLiteral("exit_code")).toInt() != 0
                    && m_modeValue == QStringLiteral("agent") && relay::input::commandMatchesPrompt(runText, m_turnShellPrompt)) {
                    m_modeHintShown = true;
                    wrongModeHint(false);
                }
            }
            const relay::toollabel::Label label = relay::toollabel::fromEvent(event);
            const QString diff = event.value(QStringLiteral("diff")).toString();
            m_toolLines = 0; m_toolPartialLine = false;
            m_liveCall.clear();
            if (m_agentBusy) tickTurnClock();   // between calls, the busy line says "thinking" again (#4E13)
            if (!call.isEmpty() && (m_waitCall == call || m_jobWaitCall == call)) {
                // The agent_wait (#V7QD) or the command_output (#KP4M) returned.
                if (m_waitCall == call) m_waitCall.clear(); else m_jobWaitCall.clear();
                refreshBackgroundWait();
                tickTurnClock();
            }
            if (!shellIdleAtPrompt()) {
                // Deferred: the finished line, as plain text. It carries no anchor, because a line
                // replayed by flushInline() cannot be rewritten and nothing would fold under it.
                ensureLineStart();
                printInline(QStringLiteral("▸ ") + label.line() + QLatin1Char('\n'),
                            label.failed() ? Ink::Error : Ink::Tool);
            } else {
                beginBlock(relay::gaps::Block::Call);   // already Call after its start: no gap (#5AWD)
                m_callCursor.setCells(callLineCells());
                const relay::calllines::Step step = m_callCursor.result(call, label, callLineCells());
                const QString anchor = callAnchor(step, turn, label);
                drawCallRow(step, turn, anchor);
                CallRecord record;
                record.turnId = turn;
                record.label = label;
                record.diff = diff;
                record.merged = step.merged;
                record.members = m_callCursor.members();
                record.callIds.clear();
                for (relay::calllines::RunMember &member : record.members) {
                    record.callIds << member.call;
                    if (!member.path.isEmpty() && !member.path.startsWith(QLatin1Char('/')))
                        member.path = QDir(m_workspace).filePath(member.path);   // the fold's links open files
                }
                if (record.callIds.isEmpty()) record.callIds << call;
                rememberCall(anchor, record);
                // At most 12 changed lines (§ 23.2): the diff prints under the line with no click.
                if (label.inlineDiff && !diff.isEmpty() && !step.hold) printInlineDiff(diff);
            }
        } else if (type == QStringLiteral("vision_route")) {
            // Image context (protocol 17): this turn runs on another model because the pane's own
            // cannot read images. Said plainly, because the answer comes from a different model.
            ensureLineStart();
            printInline(QStringLiteral("🖼 ") + event.value(QStringLiteral("text")).toString() + '\n', Ink::Note);
            pushServingModel(QStringLiteral("vision"), event);
        } else if (type == QStringLiteral("vision_route_ended")) {
            popServingModel(QStringLiteral("vision"));
        } else if (type == QStringLiteral("vision_unavailable")) {
            // Refused, not failed: the `error` that follows carries the same text, so only the
            // "what to do about it" line is added here.
            ensureLineStart();
            printInline(QStringLiteral("🖼 No vision model · Options › Models › Vision model\n"), Ink::Error);
        } else if (type == QStringLiteral("plan_route")) {
            // Plan mode's own model role (protocol 13): this turn runs on the planning model — by
            // default the pane's own at max reasoning. Said plainly, like the image routing.
            ensureLineStart();
            printInline(QStringLiteral("◆ ") + event.value(QStringLiteral("text")).toString() + '\n', Ink::Note);
            pushServingModel(QStringLiteral("plan"), event);
        } else if (type == QStringLiteral("plan_route_ended")) {
            popServingModel(QStringLiteral("plan"));
        } else if (type == QStringLiteral("provider_retry")) {
            // The model went silent, the request was refused, or the provider keeps failing and the
            // turn has moved to another one (#G9VE). Say which in the transcript: a failover is not
            // a warning — the turn is running, on a provider that answers — and its closing line
            // only says the pane has the model the user chose back.
            const QString reason = event.value(QStringLiteral("reason")).toString();
            const QString mark = reason == QStringLiteral("failover")        ? QStringLiteral("⇄ ")
                               : reason == QStringLiteral("failover_ended")  ? QStringLiteral("↩ ")
                                                                            : QStringLiteral("⚠ ");
            ensureLineStart();
            printInline(mark + event.value(QStringLiteral("text")).toString() + '\n', Ink::Note);
            // A failover is the third way the turn leaves the pane's model, and the picker shows it
            // exactly as it shows the other two (C5). The retries that are not a move — a stall, a
            // truncated step, an HTTP retry — are the same model trying again, so they change
            // nothing here.
            if (reason == QStringLiteral("failover")) pushServingModel(reason, event);
            else if (reason == QStringLiteral("failover_ended")) popServingModel(QStringLiteral("failover"));
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
            if (m_infoView) m_infoView->refreshIfLive();   // the ⓘ pane follows the turns it lists
            if (type == QStringLiteral("cancelled")) {
                ensureLineStart(); printInline(QStringLiteral("Stopped. Actions that already ran are not rolled back.\n"), Ink::Error);
            }
            printTurnEndRequests(type, event);   // request ledger UI
            ensureLineStart();
            // Readline redraws its prompt asynchronously; closing between queued turns would drop the
            // redrawn prompt into the middle of the next turn's output. Close once the queue is idle.
            if (!moreTurnsPending()) closeInline();
            status(moreTurnsPending() ? QStringLiteral("Next queued prompt…") : QStringLiteral("Ready"));
            if (!m_skillHintPending.isEmpty() && !moreTurnsPending()) {
                hint(QStringLiteral("skills.slash"), QStringLiteral("Next time: /%1 runs that skill").arg(m_skillHintPending));
                m_skillHintPending.clear();
            }
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
            // A guest that could not start (29.3): the pane stays on the model it had, rather than
            // on a row whose harness never came up. `m_presetBeforeGuest` is set on the way in and
            // cleared the moment the guest reports itself configured.
            if (onGuestPreset() && !m_presetBeforeGuest.isEmpty()) {
                m_currentPreset = m_presetBeforeGuest;
                m_presetBeforeGuest.clear();
                m_guestSession.clear();
                m_guestRequest = QJsonObject();
                changed();
            }
            m_configuring = false;
            status(text);
            if (wasBusy && !m_agentBusy) {
                // Relay Free's refusals (protocol 13.9) get their own wording: what to use instead.
                // rate_limited and token_expired keep the worker's sentence, like any provider error.
                const QString code = event.value(QStringLiteral("code")).toString();
                if (code == QStringLiteral("quota_exhausted") || code == QStringLiteral("free_unavailable")) onHostedRefusal(code, event);
                else { ensureLineStart(); printInline(QStringLiteral("✗ ") + text + '\n', Ink::Error); }
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
        m_handoffPrefill = false; m_handoffPrefix = false; m_handoffOffered = false;
        // A guest pane (26.8): the auto router's verdict says how the line is typed into the guest
        // — a command as `!<command>`, a prompt as itself. Nothing reaches the pane's own shell.
        if (guestInFront() && (route == QStringLiteral("shell") || route == QStringLiteral("agent"))) {
            submitGuest(route == QStringLiteral("shell") ? QLatin1Char('!') + text.trimmed() : text, true);
            if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
            return;
        }
        if (route == QStringLiteral("shell") && loginTakesLines()) {
            // A command for the remote shell: typed into the login, never queued for the local one.
            typeIntoLogin(text);
            return;
        }
        if (route == QStringLiteral("shell") && m_login.active) {
            // Logged in, but a full-screen program on the host has the keyboard. Queueing this for
            // the local shell would run it on the wrong machine (#S5SH): the text stays in the box.
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("control.human"));
            status(QStringLiteral("%1 has the terminal · %2 types into it, or wait for its prompt")
                       .arg(foregroundProgramName(), keys));
            toast(QStringLiteral("Not sent · a full-screen program on %1 has the terminal · %2")
                      .arg(loginHost(), keys));
            return;
        }
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
                           .arg(isolation::memory("isolation/shell_memory_max", isolation::shellDefault())),
                       QString(), {});
            notify(QStringLiteral("Out of memory"), QStringLiteral("A command in %1 was stopped (limit %2).").arg(m_cwd, isolation::memory("isolation/shell_memory_max", isolation::shellDefault())),
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
                             .arg(isolation::memory("isolation/shell_memory_max", isolation::shellDefault()))
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

    // Hiding the banner decides nothing. It used to reject a pending guest diff, because the
    // decision *was* the banner; now the decision is in the diff view's header (26.5) and the
    // banner only points at it, so dismissing the pointer — or letting any other notice replace
    // it — leaves claude's proposal exactly where the user can still answer it.
    void hideBanner() {
        if (m_banner) m_banner->hide();
        m_bannerCallback = nullptr;
    }

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
        // Inside ssh the local terminal is raw whatever the remote asks, so a remote password
        // prompt (`sudo`, a key's passphrase) is only visible on the screen (card #S5SH).
        const bool loginMasked = m_login.active && !m_altScreen && m_screenPrompt.masked && m_screenPrompt.actionable();
        if ((relay::input::secretPrompt(inputState(false)) || loginMasked) && !m_promptReported) {
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

    // The one setting, shared with every other pane's read-only text (src/CopyOnSelect.h).
    static bool copyOnSelect() { return relay::copyOnSelectEnabled(); }

    // "N characters copied", for a surface that copied something on its own.
    void toastCopied(const QString &text) {
        const int count = text.toUcs4().size();
        if (count > 0)
            toast(count == 1 ? QStringLiteral("1 character copied") : QStringLiteral("%L1 characters copied").arg(count));
    }

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
    // Returns whether it was queued, so a caller can tie another visual (the mode chip flash) to
    // the same gates: per-hint limit, cooldown and the global "Shortcut hints" setting. The hint
    // waits its turn behind other toasts and counts as shown only when it appears (showNextToast);
    // the gates are asked again then, so two hints queued together still keep the global gap.
    // Public: RelayWindow::hint() puts window-level hints on the active pane this way too.
public:
    bool hint(const QString &id, const QString &text, int limit = 3) {
        if (text.isEmpty()) return false;
        if (!relay::ShortcutHints::instance().mayShow(id, limit)) return false;
        if (toastHintPending(id)) return false;   // already waiting or up: one showing, not a pile
        enqueueToast({text, 5000, id, limit, kHintCooldownSeconds});
        return true;
    }
private:

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
        };
        // Only a pane whose tab is attached to a project has a Switchboard to tip about (#JN7X):
        // an unattached pane says nothing about boards at all.
        if (hasBoard())
            tips << relay::ShortcutHints::Tip{
                QStringLiteral("idle.board"),
                QStringLiteral("Tip: %1 opens the Switchboard; # references a card").arg(key("board.open"))};
        // A tip is the least urgent toast there is: it waits for a quiet pane rather than a queue.
        if (m_toastQueue.size() || (m_toast && m_toast->isVisible())) return;
        const auto tip = relay::ShortcutHints::instance().nextIdleTip(tips);
        if (!tip.text.isEmpty())
            enqueueToast({tip.text, 6000, tip.id, relay::ShortcutHints::kIdleTipLimit,
                          relay::ShortcutHints::kIdleTipCooldownSeconds});
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
    // The Switchboard's Execute (#XS6Q): this pane's agent is handed a card as its task. The card
    // travels as `ask {cards: [id]}` whether or not this pane has its board rows yet, and a pane
    // that was only just created runs it once its agent is configured.
    void startBoardTask(const QString &text, const QString &cardId) {
        m_boardTask = text; m_boardTaskCard = cardId;
        if (m_configured) runBoardTask();
        else status(QStringLiteral("#%1 is handed to this pane; the agent starts on it when it is ready.").arg(cardId));
    }

    // Verify (#T71W) hands a card to Claude Code or Codex. The same door the model picker uses:
    // when the worker can run the guest through its harness (29.4) the pane goes onto the guest's
    // preset and the brief is its first ask, with the card attached like any board task; only when
    // it cannot does the guest's own TUI start in this pane's shell, with the brief as its first
    // prompt. Which of the two is not known until the worker's presets are in, so a pane that was
    // just created waits for them (the same wait a guest sessions row makes).
    void startGuestBoardTask(const QString &guest, const QString &task, const QString &cardId) {
        if (m_presets.isEmpty()) { m_pendingGuestTask = {guest, task, cardId}; return; }
        if (!guestHarnessUsable(guest)) { launchGuest(guest, {task}, QString()); return; }
        pickGuest(guest);
        startBoardTask(task, cardId);
    }

    // One prompt from somewhere that is not the prompt box — Settings › Local models › "Set up a
    // model with the agent…" (card #24XJ). It joins this pane's queue like anything else typed.
    void askAgent(const QString &text, const QString &why = QString()) {
        submitAgent(text, false, why);
    }

    // Settings › Local models talks to this pane's worker, the same connection the keys dialog
    // uses and never a second one: the four `local_*` messages of protocol 28 and `test_key` for a
    // `local:` preset go out here, and every answer comes back through onLocalModelEvent. False
    // means the worker is still starting and nothing was sent.
    std::function<void(const QJsonObject &event)> onLocalModelEvent;
    bool sendLocalModelRequest(const QJsonObject &request) {
        if (!m_workerReady) return false;
        send(request);
        return true;
    }
    // A local endpoint was saved or removed: this pane re-reads `presets`, which is what puts the
    // row into (or out of) its model dropdown.
    void refreshPresets() {
        if (m_workerReady) send({{"type", "presets"}});
    }

    // Toasts are events ("12 characters copied", "Withdrawn · the agent never saw it", a shortcut
    // hint) and queue rather than replace each other: while one is up the next waits, and the one
    // up is cut to at least kToastMinMs (or its own time, if shorter) so the wait stays short. An
    // identical toast right behind the last one collapses into it. Ongoing state (the turn clock)
    // has a home of its own in the prompt-box strip and never comes through here.
    void toast(const QString &text, int milliseconds = 1600) {
        enqueueToast({text, milliseconds, QString(), 0, 0});
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
    struct PendingToast {
        QString text;
        int milliseconds = 1600;
        QString hintId;          // a shortcut hint: gated again and counted when it appears
        int hintLimit = 0, hintCooldown = 0;
    };
    static constexpr int kToastMinMs = 1500;           // each toast's least time up while others wait
    static constexpr int kHintCooldownSeconds = 600;   // ShortcutHints::shouldShow's default

    bool toastUp() const { return m_toast && m_toast->isVisible() && m_toastTimer.isActive(); }

    bool toastHintPending(const QString &id) const {
        if (toastUp() && m_toastHintId == id) return true;
        for (const PendingToast &queued : m_toastQueue) if (queued.hintId == id) return true;
        return false;
    }

    void enqueueToast(const PendingToast &next) {
        if (next.text.isEmpty()) return;
        if (!m_toast) {
            m_toast = new QLabel(this);
            m_toast->setObjectName(QStringLiteral("toast"));
            m_toast->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_toastTimer.setSingleShot(true);
            connect(&m_toastTimer, &QTimer::timeout, this, [this] { showNextToast(); });
        }
        // The same words again, straight after themselves: one toast, up for the longer time.
        if (!m_toastQueue.isEmpty()) {
            PendingToast &last = m_toastQueue.last();
            if (last.text == next.text) { last.milliseconds = std::max(last.milliseconds, next.milliseconds); return; }
        } else if (toastUp() && m_toast->text() == next.text) {
            const int shown = int(m_toastShown.elapsed());
            if (next.milliseconds > m_toastTimer.remainingTime()) {
                m_toastMs = shown + next.milliseconds;
                m_toastTimer.start(next.milliseconds);
            }
            return;
        }
        m_toastQueue.append(next);
        if (toastUp()) shortenToastForQueue();
        else showNextToast();
    }

    // Someone is waiting: the toast up keeps at least kToastMinMs (its own time, if shorter).
    void shortenToastForQueue() {
        const qint64 least = std::min<qint64>(m_toastMs, kToastMinMs);
        const int left = int(std::max<qint64>(0, least - m_toastShown.elapsed()));
        if (left < m_toastTimer.remainingTime()) m_toastTimer.start(left);
    }

    void showNextToast() {
        while (!m_toastQueue.isEmpty()) {
            const PendingToast next = m_toastQueue.takeFirst();
            if (!next.hintId.isEmpty()) {
                // A hint is counted now that it reaches the screen, not when it was asked for. The
                // gates are asked again: another hint may have appeared while this one waited.
                auto &hints = relay::ShortcutHints::instance();
                if (!hints.mayShow(next.hintId, next.hintLimit, next.hintCooldown)) continue;
                hints.recordShown(next.hintId);
            }
            m_toastHintId = next.hintId;
            m_toast->setText(next.text);
            m_toast->adjustSize();
            m_toast->show();
            placeToast();
            m_toastShown.start();
            m_toastMs = next.milliseconds;
            m_toastTimer.start(next.milliseconds);
            if (!m_toastQueue.isEmpty()) shortenToastForQueue();
            return;
        }
        m_toastHintId.clear();
        if (m_toast) m_toast->hide();
    }
public:
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

    // "Relaying reading src/Pane.h… · 48 s · Esc stops", above the prompt box (m_busyLine,
    // card #4E13), so a silent turn is never indistinguishable from a hung one. Not a toast: a
    // clock that re-toasted every second covered every real toast in a turn.
    void startTurnClock() {
        m_turnElapsed.start();
        m_turnStep.clear();
        m_waitCall.clear();      // a new turn is not blocked on an agent_wait (#V7QD)
        m_jobWaitCall.clear();   // nor on a command_output (#KP4M)
        refreshBackgroundWait();
        if (!m_turnClock) {
            m_turnClock = new QTimer(this);
            m_turnClock->setInterval(1000);
            connect(m_turnClock, &QTimer::timeout, this, [this] { tickTurnClock(); });
        }
        m_turnClock->start();
        tickTurnClock();
    }

    void stopTurnClock() {
        m_paneState.changed();   // pane_state (relay-terminal-71)
        clearServingModels();    // no turn, nothing serving it but the pane's own model (C5)
        if (m_turnClock) m_turnClock->stop();
        m_turnStep.clear();
        m_waitCall.clear();      // no turn, no agent_wait (#V7QD)
        m_jobWaitCall.clear();   // and no command_output (#KP4M)
        m_turnClockText.clear();
        refreshBackgroundWait();   // the turn ended; background work left running keeps the line up
        refreshBusyLine();         // and subagents or a program may still be relaying (#4E13)
    }

    void tickTurnClock() {
        m_paneState.changed();   // pane_state (relay-terminal-71)
        if (!m_agentBusy) { stopTurnClock(); return; }
        const qint64 seconds = m_turnElapsed.elapsed() / 1000;
        const QString stop = Keymap::instance().shortcutText(QStringLiteral("agent.stop"));
        const QString stopWord = stop.isEmpty() ? QStringLiteral("Esc") : stop;
        // What the agent is doing right now (card #4E13): the live tool call's own gerund
        // ("reading src/Pane.h"), or "thinking" between calls.
        QString what = QStringLiteral("thinking");
        if (!m_liveCall.isEmpty() && !m_liveLabel.running.isEmpty()) what = m_liveLabel.running;
        // Blocked on the background work it started, the line says what it is blocked on rather
        // than "thinking" (cards #V7QD, #KP4M).
        const QString subject = relay::panestatus::waitingSubject(waitingFacts());
        if (!subject.isEmpty()) what = QStringLiteral("waiting for ") + subject;
        // Blocked on a question card is the plainest case of all (#MQ9C): the turn is not thinking,
        // it is waiting for the person reading it, and saying "thinking" would be a lie in the one
        // place the user is looking while they decide.
        const bool asked = m_ask.open();
        if (asked) what = QStringLiteral("waiting for your answer");
        // While a card is up Esc skips the question instead (#MQ9C, owner 2026-09-19), so the line
        // offers the key that is actually live and the tooltip says where Stop went.
        const QString keyHint = asked ? QStringLiteral("Esc skips it") : QStringLiteral("%1 stops").arg(stopWord);
        const QString label = QStringLiteral("Relaying %1… · %2 s%3 · %4")
                                  .arg(what)
                                  .arg(seconds)
                                  .arg(m_turnStep.isEmpty() ? QString() : QStringLiteral(" · ") + m_turnStep)
                                  .arg(keyHint);
        m_turnClockText = label;   // pane_state's clock, for a paired phone (relay-terminal-71)
        if (m_busyLine)
            m_busyLine->setBusy(asked ? relay::panestatus::State::NeedsYou : relay::panestatus::State::Working,
                                label,
                                asked
                                    ? QStringLiteral("The agent asked you something and its turn is blocked on the answer "
                                                     "(%1 s so far). Esc skips this question. %2.")
                                          .arg(seconds).arg(stopTurnHint())
                                      : QStringLiteral("The agent has been on this turn for %1 s. %2 stops it.")
                                          .arg(seconds).arg(stopWord));
    }

    // The "Relaying…" line when no turn of this pane's own is running (card #4E13): subagents it
    // started still working (violet, no clock — nothing of this pane's is being timed), or a
    // program in the terminal (blue, the program's name). Hidden when the pane is idle, so an
    // idle pane looks exactly as it did before. The turn's own line is the turn clock's.
    void refreshBusyLine() {
        if (!m_busyLine) return;
        if (m_agentBusy) { tickTurnClock(); return; }
        const relay::panestatus::Waiting wait = waitingFacts();
        if (wait.subagents > 0) {
            const QString subject = relay::panestatus::waitingSubject(wait);
            m_busyLine->setBusy(relay::panestatus::State::Subagents,
                                QStringLiteral("Relaying waiting for %1…").arg(subject),
                                QStringLiteral("%1 started by this pane's agent still running. The agents list under "
                                                "the composer shows them; the header's relay mark blinks until they end.")
                                    .arg(subject.isEmpty() ? QStringLiteral("Background work") : subject));
            return;
        }
        if (processBusy()) {
            const QString program = foregroundProgramName();
            const QString who = program.isEmpty() ? QStringLiteral("the program") : program;
            m_busyLine->setBusy(relay::panestatus::State::Running,
                                QStringLiteral("Relaying %1…").arg(who),
                                QStringLiteral("%1 owns this terminal; prompts queue until it exits.")
                                    .arg(program.isEmpty() ? QStringLiteral("This program") : program));
            return;
        }
        m_busyLine->clearBusy();
    }

    // One row of the model box, chosen. Split out of the box's `activated` handler so a choice
    // made while a guest is running can be made again once the guest has left (26.9).
    void modelBoxPicked(const QString &data) {
        // Owner report, 2026-09-18: "selecting model options in the model dropdown didnt do
        // anything. the main use case for that is going to be swapping between the main and
        // flash models." Two entries in this list are not models: the gear (the model options
        // modal, which lost its branch here when the strip was rebuilt) and the Main/Flash rows
        // (the pane's agent role). Both are handled before selectModel, which only knows presets.
        if (data == QStringLiteral("gear:modelOptions")) {
            refreshPickers();   // put the box back on the pane's model: the gear is not a choice
            openRolesDialog();
            hint(QStringLiteral("model.options.mouse"),
                 QStringLiteral("Tip: Options › Models opens the same modals"));
            return;
        }
        if (data.startsWith(QStringLiteral("role:"))) {
            const QString role = data.mid(5);
            chooseAgentRole(role); focusInput();
            // The Local agent has no shortcut on purpose (it takes no key), so its hint names
            // the fast path it does have: /local in the prompt box. WARP.md, "Shortcut hints".
            if (role == QStringLiteral("local"))
                hint(QStringLiteral("model.role.local.mouse"),
                     QStringLiteral("Tip: /local runs this pane on the local model, /main goes back"));
            else
                hint(QStringLiteral("model.role.mouse"),
                     relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("agent.flashAgent")),
                                                    QStringLiteral("the Main and Flash agents")));
            return;
        }
        // Claude Code or Codex: the guest is this pane's agent from here on — through its harness
        // when the worker offers one (29.4), as a TUI in this pane's shell when it does not (26.9).
        // Both rows carry the same `guest:<id>` data, and pickGuest decides which.
        if (data.startsWith(QStringLiteral("guest:"))) {
            const QString id = data.mid(6);
            pickGuest(id);
            focusInput();
            hint(QStringLiteral("model.guest.mouse"),
                 QStringLiteral("Tip: /model %1 does this from the prompt box").arg(id));
            return;
        }
        // A preset picked while a guest runs: the pane shifts over at once, like any model switch
        // (leaveGuest); the guest finishes what it is doing and is then asked to exit.
        if (guestInFront()) {
            leaveGuest([this, data] { selectModel(data); });
            focusInput();
            return;
        }
        selectModel(data); focusInput();
        hint(QStringLiteral("model.mouse"), QStringLiteral("Tip: /model switches models from the prompt box"));
    }

    // ----- the model serving this turn (C5, protocol 13.11 / 15.2.2 / 17.3) --------------------
    // Every move of the running turn off this pane's own model comes through here, carrying the
    // event that announced it: `vision_route` and `plan_route` name the model in `model` and the
    // one it came from in `from_model`, a failover names them in `to_model` and `from_model`.
    void pushServingModel(const QString &why, const QJsonObject &event) {
        Serving serving;
        serving.why = why;
        serving.model = event.value(QStringLiteral("model")).toString();
        serving.preset = event.value(QStringLiteral("preset")).toString();
        if (serving.model.isEmpty()) {          // a failover's fields have the other names
            serving.model = event.value(QStringLiteral("to_model")).toString();
            serving.preset = event.value(QStringLiteral("to_preset")).toString();
        }
        serving.backTo = event.value(QStringLiteral("from_model")).toString();
        if (serving.model.isEmpty()) return;   // an event that named no model has nothing to show
        m_serving.append(serving);
        refreshPickers();
    }
    // The move that ended is the one taken out, not simply the last: the three can end in any
    // order, and popping the wrong one would leave the box naming a model the turn has left.
    void popServingModel(const QString &why) {
        for (int i = m_serving.size() - 1; i >= 0; --i)
            if (m_serving.at(i).why == why) { m_serving.removeAt(i); refreshPickers(); return; }
        if (m_serving.isEmpty()) return;
        m_serving.removeLast();                // an end with no move behind it: leave nothing up
        refreshPickers();
    }
    void clearServingModels() {
        if (m_serving.isEmpty()) return;
        m_serving.clear();
        refreshPickers();
    }
    // The marks the transcript notes already use for the three: 🖼 an image turn, ◆ a plan turn,
    // ⇄ a turn that moved provider. The box wears the same one, so the row and the line above it
    // are visibly the same event.
    static QString servingMark(const QString &why) {
        return why == QStringLiteral("vision") ? QStringLiteral("🖼")
             : why == QStringLiteral("plan")   ? QStringLiteral("◆")
                                               : QStringLiteral("⇄");
    }
    // A preset's label from the worker's list, for the tooltip: two stored keys can serve one model
    // id (15.2.2), so the id alone does not say which key this turn is spending.
    QString presetLabelOf(const QString &presetId) const {
        if (presetId.isEmpty()) return QString();
        for (const auto &item : m_presets) {
            const QJsonObject preset = item.toObject();
            if (preset.value(QStringLiteral("id")).toString() == presetId)
                return preset.value(QStringLiteral("label")).toString();
        }
        return QString();
    }
    QString servingName(const Serving &serving) const {
        const QString label = presetLabelOf(serving.preset);
        return label.isEmpty() || label == serving.model
                   ? serving.model : QStringLiteral("%1 (%2)").arg(serving.model, label);
    }
    // What the box's tooltip says while another model serves the turn: what moved it, and that the
    // pane comes back to its own model afterwards. Picking a model while this is up is the ordinary
    // pick — the worker holds a `set_model` that arrives mid-swap until the turn ends (12.6) — so
    // the tooltip says when it will take effect rather than pretending it cannot be made.
    QString servingTooltip() const {
        if (m_serving.isEmpty()) return QString();
        const Serving &serving = m_serving.last();
        const QString name = servingName(serving);
        const QString back = serving.backTo.isEmpty() ? m_model : serving.backTo;
        const QString because = serving.why == QStringLiteral("vision")
            ? QStringLiteral("This turn carries an image, so it runs on %1").arg(name)
            : serving.why == QStringLiteral("plan")
            ? QStringLiteral("Plan mode: this turn runs on %1 (the planning role)").arg(name)
            : QStringLiteral("This turn's provider kept failing, so it is finishing on %1").arg(name);
        return QStringLiteral("%1 — for this turn; back to %2 after. Picking a model here still "
                              "changes this pane's own model, from the end of this turn.")
                   .arg(because, back.isEmpty() ? QStringLiteral("this pane's own model") : back);
    }

    void refreshPickers() {
        m_paneState.changed();   // pane_state (relay-terminal-71): model and mode; changed() runs this
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
        // The Local agent only when this machine serves something (card #JH22): a row that always
        // resolved back to Main would be a promise the pane cannot keep.
        if (hasLocalEndpoint()) paneRoles << QStringLiteral("local");
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
        // Guest agents (26.9): Claude Code and Codex, when installed, as rows like any model — no
        // tier, no key, no worker, so they are offered in a pane with no provider at all. The row
        // is the box's current item while that guest is in the pane's foreground (picked or typed
        // by hand) or was just picked and not yet detected.
        // Tier A (29.4) takes a guest out of this group: when the worker offers it as a usable
        // harness preset, that preset *is* the row — it sits with the models above, and a second
        // row here would be the same tool offered twice. Only the guest actually running as a TUI
        // in this pane keeps its row either way (tierBGuests).
        QStringList guests = tierBGuests();
        const QString liveGuest = m_guestLeaving ? QString() : m_guest.isEmpty() ? m_guestWanted : m_guest;
        if (!liveGuest.isEmpty() && !guests.contains(liveGuest)) guests << liveGuest;   // run by a path
        if (!guests.isEmpty()) {
            m_modelBox->insertSeparator(m_modelBox->count());
            for (const QString &id : std::as_const(guests)) {
                const QString model = id == m_guest && !m_guestModel.isEmpty() ? QStringLiteral(" · ") + m_guestModel : QString();
                m_modelBox->addItem(guestDisplayName(id) + model, QStringLiteral("guest:") + id);
                if (id == liveGuest) m_modelBox->setCurrentIndex(m_modelBox->count() - 1);
            }
        }
        // Last entry: the model options modal (default provider, Main/Flash/Lite, per-job overrides).
        // It stays reachable with no stored key, which is exactly when it is needed most.
        m_modelBox->insertSeparator(m_modelBox->count());
        m_modelBox->addItem(QString(QChar(0x2699)) + QStringLiteral("  Model options…"),
                            QStringLiteral("gear:modelOptions"));
        m_modelBox->setEnabled(true);
        // The model actually serving the turn, when it is not the pane's own (C5): plan mode's
        // planning model, an image turn's vision model, or the provider a failover moved to. One
        // row for all three, marked with what moved it, so an answer never seems to have come from
        // the pane's own model; the tooltip says the pane gets that model back after the turn.
        if (!m_serving.isEmpty()) {
            const Serving &serving = m_serving.last();
            m_modelBox->insertItem(0, QStringLiteral("%1 %2 · this turn").arg(servingMark(serving.why), serving.model),
                                   QStringLiteral("serving:") + serving.model);
            m_modelBox->setCurrentIndex(0);
        }
        m_modelBox->setToolTip(modelTooltip(!m_serving.isEmpty()
            ? servingTooltip()
            : !liveGuest.isEmpty()
            ? QStringLiteral("%1 is this pane's agent: the prompt box is its input. Pick a model to leave it.").arg(guestDisplayName(liveGuest))
            : onGuestPreset()
            // Tier A (29.4): the guest answers through its harness, so the conversation, the chips
            // and the call lines are Relay's and the terminal below is still the user's own shell.
            ? QStringLiteral("%1 is this pane's agent, through its own harness%2. The terminal stays yours.")
                  .arg(guestDisplayName(guestOfPreset(m_currentPreset)),
                       m_guestSession.isEmpty() ? QString() : QStringLiteral(" (session %1)").arg(m_guestSession.left(8)))
            : QString()));
    }

    // ----- Claude Code and Codex from the model picker (GT7X, 26.9) ---------------------------
    //
    // The owner's direction (2026-09-19): no per-project setup, and the guest selectable like any
    // model. Picking a row runs the guest in this pane's own shell and directory, configured on its
    // command line — a per-launch settings file for claude, `-c` overrides for codex, the bypass
    // flag both, and the IDE bridge's port for claude — by `relay_core.guest_launch`, which also
    // removes the retired installers' marked entries from the files that claude would read (they
    // would run every hook twice beside the launch file). The typed line is visible in the
    // terminal, deliberately: it is exactly what the user could type themselves.

    // Which guests this machine has on PATH, by the same binary names the detector uses. Cached
    // for a few seconds: refreshPickers runs on every state change.
    static QStringList installedGuests() {
        static QStringList found;
        static QElapsedTimer clock;
        if (clock.isValid() && clock.elapsed() < 10000) return found;
        found.clear();
        for (const GuestSpec &spec : guestSpecs())
            for (const QString &binary : spec.binaries)
                if (!QStandardPaths::findExecutable(binary).isEmpty()) { found << QString::fromLatin1(spec.id); break; }
        clock.start();
        return found;
    }

    // ----- Tier A: the guest as this pane's own agent (protocol 29.4) -------------------------
    //
    // The worker reports one preset per guest it knows (29.3), `guest:<id>`, and `harness: true`
    // when it can actually run that guest's headless harness here. Such a row is a model row like
    // any other: picking it is a `configure`/`set_model` carrying a `guest` object, the worker puts
    // the ordinary Agent on a HarnessProvider, and the transcript, the call lines, the context chip
    // and the question cards are Relay's own. Nothing is typed into a TUI and the pane's shell
    // stays the user's. A row the worker cannot run (`harness: false`, or a worker that reports no
    // guest rows at all) keeps the Tier B launch of 26.9, which is also what a `claude` typed by
    // hand gets. `guestHarnessUsable` is the one place that decides between the two.
public:
    // "guest:claude" → "claude"; anything else → empty.
    static QString guestOfPreset(const QString &presetId) {
        return presetId.startsWith(QStringLiteral("guest:")) ? presetId.mid(6) : QString();
    }
    // Can this guest be the pane's agent through its harness, here and now? Only the worker knows:
    // it owns the adapter and looks for the binary. No preset (no worker yet, an older worker, a
    // guest it does not know) is a no, which leaves Tier B in charge exactly as before.
    bool guestHarnessUsable(const QString &guest) const {
        if (guest.isEmpty()) return false;
        const QJsonObject preset = presetById(QStringLiteral("guest:") + guest);
        return !preset.isEmpty() && preset.value(QStringLiteral("harness")).toBool();
    }
private:
    bool onGuestPreset() const { return !guestOfPreset(m_currentPreset).isEmpty(); }

    // The guest rows the *picker* still has to draw itself: a guest on PATH that the worker does
    // not already offer as a usable preset. The one running in the pane keeps its row whatever the
    // worker says — a TUI in the foreground is not a preset, and the box must show where the
    // prompt box is going.
    QStringList tierBGuests() const {
        const QString live = m_guest.isEmpty() ? m_guestWanted : m_guest;
        QStringList rows;
        for (const QString &id : installedGuests())
            if (id == live || !guestHarnessUsable(id)) rows << id;
        return rows;
    }

    // "The user picked this guest": the model box, `/model claude`, the palette. One door, so the
    // Tier A/Tier B decision is made once. `model` is a model named with the pick (`/model claude
    // opus`), which only the harness can honour.
    void pickGuest(const QString &guest, const QString &model = QString()) {
        if (!guestHarnessUsable(guest)) {
            if (!model.isEmpty())
                status(QStringLiteral("%1 picks its own model when it runs in the terminal; ignoring “%2”.")
                           .arg(guestDisplayName(guest), model));
            if (!m_guest.isEmpty() && m_guest != guest) leaveGuest([this, guest] { chooseGuest(guest); }, true);
            else chooseGuest(guest);
            return;
        }
        const QString id = QStringLiteral("guest:") + guest;
        if (id == m_currentPreset && m_configured && m_guest.isEmpty()) {
            if (model.isEmpty()) { status(QStringLiteral("Already on %1.").arg(guestDisplayName(guest))); refreshPickers(); }
            else setMainModel(model);   // same guest, another model: the harness restarts on it
            return;
        }
        m_guestRequest = QJsonObject();
        if (!model.isEmpty()) m_guestRequest.insert(QStringLiteral("model"), model);
        selectModel(id);
    }

    // The `guest` object of the request about to be sent, and the end of the one that was staged
    // for it. Empty for every other preset — and staging is dropped there, so a pick that landed
    // somewhere else cannot leak into the next guest request. The owner's rule (29.1) is the
    // default: no per-action approvals, exactly as for Relay's own agent.
    QJsonObject takeGuestRequest(const QString &presetId) {
        QJsonObject staged = m_guestRequest;
        m_guestRequest = QJsonObject();
        const QString guest = guestOfPreset(presetId);
        if (guest.isEmpty()) return QJsonObject();
        if (!staged.contains(QStringLiteral("permissions")))
            staged.insert(QStringLiteral("permissions"), QStringLiteral("bypass"));
        // Options › Claude Code and Codex: the model and the reasoning effort this guest starts
        // with, unless the pick named its own (`/model claude opus`).
        for (const QString &key : {QStringLiteral("model"), QStringLiteral("effort")})
            if (!staged.contains(key))
                if (const QString value = guestSetting(guest, key); !value.isEmpty()) staged.insert(key, value);
        return staged;
    }

    // A guest that cannot start is an `error` on the configure and the pane keeps the model it had
    // (29.3). That model is remembered here, on the way onto the guest.
    void rememberPresetBeforeGuest(const QString &id) {
        if (guestOfPreset(id).isEmpty()) m_presetBeforeGuest.clear();
        else if (!onGuestPreset()) m_presetBeforeGuest = m_currentPreset;
    }

    // `configured` / `model_changed` for a pane that is now on a guest: one line saying which tool
    // answers and which of its sessions this is. `guest` and `guest_session` are the worker's
    // (29.3); a worker that reports neither still gets the line from the preset the pane picked.
    void noteGuestPreset(const QJsonObject &event) {
        m_presetBeforeGuest.clear();
        const QString guest = event.value(QStringLiteral("guest")).toString(guestOfPreset(m_currentPreset));
        if (guest.isEmpty()) { m_guestSession.clear(); return; }
        m_guestSession = event.value(QStringLiteral("guest_session")).toString();
        status(m_guestSession.isEmpty()
                   ? QStringLiteral("%1 is this pane's agent.").arg(guestDisplayName(guest))
                   : QStringLiteral("%1 is this pane's agent (session %2).")
                         .arg(guestDisplayName(guest), m_guestSession.left(8)));
    }

public:
    // A guest's own resume argv (26.7) read back: the sessions row carries what the *tool* would be
    // launched with, and Tier A needs the two values inside it. claude continues with `-r <id>` and
    // forks with `--fork-session`; codex uses the subcommands `resume <id>` and `fork <id>`.
    struct GuestResume { QString sessionId; bool fork = false; };
    static GuestResume guestResumeFrom(const QStringList &extra) {
        GuestResume out;
        for (int i = 0; i < extra.size(); ++i) {
            const QString word = extra.at(i);
            const QString next = i + 1 < extra.size() ? extra.at(i + 1) : QString();
            if (word == QStringLiteral("--fork-session")) out.fork = true;
            else if (word == QStringLiteral("fork")) { out.fork = true; if (!next.startsWith(QLatin1Char('-'))) out.sessionId = next; }
            else if (word == QStringLiteral("resume") || word == QStringLiteral("-r") || word == QStringLiteral("--resume")) {
                if (!next.isEmpty() && !next.startsWith(QLatin1Char('-'))) out.sessionId = next;
            } else if (word.startsWith(QStringLiteral("--resume="))) {
                out.sessionId = word.mid(QStringLiteral("--resume=").size());
            }
        }
        return out;
    }

    // Resume a guest session on this pane's agent (29.4): a `configure` on the guest preset with
    // `guest.resume`, in the session's own directory — both guests resolve an id against the
    // directory they start in, and the harness starts in the workspace of the configure. Public:
    // RelayWindow::openGuestPane calls it on the pane it has just created for the row, which may
    // not have the worker's presets yet; the choice then waits for them, as a restored pane's does.
    // `extra` is the row's own argv, kept only as the Tier B fallback for that case.
    void resumeGuestPreset(const QString &guest, const QString &sessionId, bool fork,
                           const QString &cwd = QString(), const QStringList &extra = QStringList()) {
        if (!cwd.isEmpty() && QFileInfo(cwd).isDir()) {
            m_workspace = cwd;
            // The pane's shell follows the agent into the session's directory, as the Tier B
            // resume does with its `cd &&` — one directory per pane, whatever is answering.
            if (QDir::cleanPath(cwd) != QDir::cleanPath(m_cwd))
                submitTerminal(QStringLiteral("cd ") + relay::conversations::shellWord(cwd), false);
        }
        m_guestRequest = QJsonObject();
        if (!sessionId.isEmpty()) m_guestRequest.insert(QStringLiteral("resume"), sessionId);
        if (fork) m_guestRequest.insert(QStringLiteral("fork"), true);
        if (m_presets.isEmpty()) { m_pendingGuestResume = {guest, extra, cwd}; return; }
        startGuestPreset(guest, extra, cwd);
    }
private:
    // The staged resume, once the worker's presets are in (or straight away when they already are).
    void startGuestPreset(const QString &guest, const QStringList &extra, const QString &cwd) {
        if (!guestHarnessUsable(guest)) {   // the worker cannot run it after all: Tier B, as before
            m_guestRequest = QJsonObject();
            launchGuest(guest, extra, cwd);
            return;
        }
        leaveGuest([this, guest] {
            if (m_agentBusy) stopAgent();   // a configure ends the conversation; the turn goes first
            configurePreset(QStringLiteral("guest:") + guest, false);
            status(QStringLiteral("Resuming the %1 session on this pane's agent.").arg(guestDisplayName(guest)));
        });
    }

    // The picker's row: this guest, here, now.
    void chooseGuest(const QString &guest) {
        if (guestDisplayName(guest) == QStringLiteral("The guest agent")) return;   // not an id the table knows
        if (m_guest == guest) {
            // Picked again while it was being left and has not gone yet: the pane comes back to it.
            const bool back = m_guestLeaving;
            m_guestLeaving = false;
            m_guestLeaveThen = nullptr;
            status((back ? QStringLiteral("Back on %1.") : QStringLiteral("Already on %1.")).arg(guestDisplayName(guest)));
            refreshPickers();
            return;
        }
        if (!m_guest.isEmpty()) { leaveGuest([this, guest] { chooseGuest(guest); }, true); return; }
        launchGuest(guest, {}, QString());
    }

    // Start `guest` in this pane's shell with `extra` after the launch-time flags (a sessions
    // row's `-r <id>` / `resume <id>`), in `cwd` when that is not this pane's directory. A worker
    // turn still running is stopped: the guest is the pane's agent from here on. Public: a new
    // pane created for a session (RelayWindow::openGuestPane) calls it before its shell is up,
    // and the command waits in the queue for the prompt like any other.
public:
    void launchGuest(const QString &guest, const QStringList &extra, const QString &cwd) {
        if (guestDisplayName(guest) == QStringLiteral("The guest agent")) {
            status(QStringLiteral("Unknown guest agent: %1").arg(guest));
            return;
        }
        if (m_guestLaunch) { status(QStringLiteral("A guest is already starting in this pane.")); return; }
        if (m_agentBusy) stopAgent();
        int port = 0;
        if (guest == QStringLiteral("claude")) {
            // The first claude launch of the run starts the sidecar (26.5); the port then goes on
            // this one command line. A bridge that failed to start costs nothing: no port, no diffs.
            auto &bridge = relay::guestbridge::Bridge::instance();
            bridge.setDataRoot(m_data);
            port = bridge.portFor(m_python);
            registerWithBridge();
        }
        const QString directory = cwd.isEmpty() ? m_cwd : cwd;
        auto *launch = new QProcess(this);
        launch->setWorkingDirectory(QFileInfo(directory).isDir() ? directory : m_cwd);
        launch->setProcessEnvironment(guestHelperEnvironment());
        launch->setProgram(m_python);
        QStringList arguments{QStringLiteral("-m"), QStringLiteral("relay_core.guest_launch"), guest,
                              QStringLiteral("--runtime-dir"), m_runtime.path(), QStringLiteral("--cwd"), directory,
                              QStringLiteral("--port"), QString::number(port), QStringLiteral("--python"), m_python};
        // The same defaults as the harness route (Options › Claude Code and Codex), as the CLI's
        // own flags on the launch line; `guest_launch` leaves them out when `extra` names its own.
        if (const QString model = guestSetting(guest, QStringLiteral("model")); !model.isEmpty())
            arguments << QStringLiteral("--model") << model;
        if (const QString effort = guestSetting(guest, QStringLiteral("effort")); !effort.isEmpty())
            arguments << QStringLiteral("--effort") << effort;
        if (!extra.isEmpty()) arguments << QStringLiteral("--") << extra;
        launch->setArguments(arguments);
        m_guestLaunch = launch;
        m_guestWanted = guest;   // the picker shows the row from now until the detector agrees
        refreshPickers();
        connect(launch, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, launch, guest, cwd](int code, QProcess::ExitStatus exit) {
                    launch->deleteLater();
                    if (m_guestLaunch == launch) m_guestLaunch = nullptr;
                    const QJsonObject result = QJsonDocument::fromJson(launch->readAllStandardOutput()).object();
                    if (exit != QProcess::NormalExit || code != 0 || !result.value(QStringLiteral("ok")).toBool()) {
                        const QString why = result.value(QStringLiteral("error")).toString(
                            QString::fromUtf8(launch->readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1));
                        relay::log::error(QStringLiteral("guest_launch_failed pane=%1 guest=%2 code=%3 error=%4")
                                              .arg(paneLogId(), guest).arg(code).arg(why));
                        status(QStringLiteral("Could not start %1: %2").arg(guestDisplayName(guest), why.isEmpty() ? QStringLiteral("the launch helper failed") : why));
                        m_guestWanted.clear();
                        refreshPickers();
                        return;
                    }
                    QString line = result.value(QStringLiteral("command")).toString();
                    // `cd` and the launch are one command line, so the guest never starts in the
                    // wrong place if the cd fails (26.7).
                    if (!cwd.isEmpty() && QDir::cleanPath(cwd) != QDir::cleanPath(m_cwd))
                        line = QStringLiteral("cd ") + relay::conversations::shellWord(cwd) + QStringLiteral(" && ") + line;
                    const QJsonArray legacy = result.value(QStringLiteral("legacy")).toArray();
                    if (!legacy.isEmpty()) {
                        QStringList files;
                        for (const auto &value : legacy) files << value.toString();
                        notify(guestDisplayName(guest), QStringLiteral("Removed Relay's old guest entries from %1 (Relay no longer needs them installed).").arg(files.join(QStringLiteral(", "))));
                    }
                    // Now if the shell is at its prompt, else queued like anything typed while the
                    // terminal is busy; a new pane's shell is not up yet and the queue waits for it.
                    submitTerminal(line, false);
                    status(QStringLiteral("Starting %1…").arg(guestDisplayName(guest)));
                    // The detector takes over once the guest is in the foreground; if it never
                    // arrives (the command failed in the shell), the picker stops claiming it.
                    QTimer::singleShot(30000, this, [this, guest] {
                        if (m_guestWanted == guest && m_guest != guest) { m_guestWanted.clear(); refreshPickers(); }
                    });
                });
        connect(launch, &QProcess::errorOccurred, this, [this, launch, guest](QProcess::ProcessError) {
            launch->deleteLater();
            if (m_guestLaunch == launch) m_guestLaunch = nullptr;
            relay::log::error(QStringLiteral("guest_launch_error pane=%1 guest=%2 error=%3").arg(paneLogId(), guest, launch->errorString()));
            status(QStringLiteral("Could not start %1: %2").arg(guestDisplayName(guest), launch->errorString()));
            m_guestWanted.clear();
            refreshPickers();
        });
        launch->start();
    }
private:

    // Leave the guest and do `then`. No guest: `then` runs now. Otherwise the pane **shifts over at
    // once**, the way a native model switch does (owner, 2026-09-19: "a busy guest model change
    // should be the same as our relay-native models. just shift over immediately"): the box, the
    // `/` popup and the prompt box stop being the guest's from this moment, and `then` — a model
    // pick — runs now. The guest is not interrupted: idle, it is asked to `/exit` here; working,
    // its turn in flight finishes, as a native model's request in flight does, and it is asked
    // when it goes idle (setGuestBusy). A `then` that needs the pane's shell (another guest, a
    // session resumed here: `needsShell`) cannot run beside a TUI, so that one waits for the
    // program poll to see the shell back (setGuest).
    void leaveGuest(std::function<void()> then, bool needsShell = false) {
        if (m_guest.isEmpty()) { if (then) then(); return; }
        const QString guest = m_guest;
        const bool already = m_guestLeaving;
        m_guestLeaving = true;
        if (needsShell) m_guestLeaveThen = std::move(then);
        if (!already) {
            if (m_guestBusy)
                status(QStringLiteral("%1 finishes its turn and then exits · this pane has moved on.")
                           .arg(guestDisplayName(guest)));
            else
                askGuestToExit(guest);
        }
        refreshPickers();
        if (!needsShell && then) then();
    }

    // The guest's own `/exit`, typed into it, and a word if it stays anyway. The pane has already
    // moved on, so a guest that does not go costs nothing but the terminal it is sitting in; a
    // `then` that was waiting for the shell is dropped, and said so.
    void askGuestToExit(const QString &guest) {
        if (guest != m_guest) return;
        typeIntoGuest(guest, QStringLiteral("/exit"));
        status(QStringLiteral("Leaving %1…").arg(guestDisplayName(guest)));
        QTimer::singleShot(15000, this, [this, guest] {
            if (m_guest != guest || !m_guestLeaving) return;
            const bool waiting = static_cast<bool>(m_guestLeaveThen);
            m_guestLeaveThen = nullptr;
            status(waiting ? QStringLiteral("%1 did not exit · type /exit into it, then pick again.").arg(guestDisplayName(guest))
                           : QStringLiteral("%1 is still in the terminal · type /exit into it to close it.").arg(guestDisplayName(guest)));
            refreshPickers();
        });
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
            // A guest (29.4) reads as the tool — "Claude Code", not the model id it happens to be
            // running: the model is the guest's own and shows on the chip once it reports one.
            if (preset.value(QStringLiteral("group")).toString() == QStringLiteral("guest")) return label;
            // Relay Free's model ids are the gateway's roles (relay-main…), which name nothing a
            // person recognises: the row reads as the service.
            if (preset.value(QStringLiteral("hosted")).toBool()) return label;
            const QString model = preset.value(QStringLiteral("model")).toString();
            if (!model.isEmpty()) return model.section('/', -1).toLower() + mark;
        }
        return label;
    }

    // Chip tooltip: the pane's model plus every role's effective model (protocol 13).
    QString modelTooltip(const QString &extra = QString()) const {
        // "medium (sent as high)" when this provider has no level of that name: the tooltip is where
        // the pane's effort is read, so it is where the provider's mapping belongs.
        const QStringList levels = offeredEfforts();
        const QString sent = nearestEffort(levels, m_effort);
        const QString effortText = levels.isEmpty()
            ? QStringLiteral("%1 (this model has no reasoning setting)").arg(m_effort)
            : (sent == m_effort ? m_effort : QStringLiteral("%1 (sent as %2)").arg(m_effort, sent));
        QStringList lines{QStringLiteral("Agent model for this pane. Switching keeps the conversation."),
                          QStringLiteral("Reasoning effort: %1  (%2 / %3 to change)")
                              .arg(effortText, Keymap::instance().shortcutText(QStringLiteral("agent.effortUp")),
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

    // ----- pane_state (relay-terminal-71): this pane as a paired phone draws it ------------------
    // One pane model, two views (docs/REMOTE-PROTOCOL.md section 16). remoteState() gathers what
    // the pane already shows — the turn clock, the reasoning, the queue rows, the model chip and
    // its menu, the prompt box, the context chip, the session list — and relay::panestate builds
    // the message, coalesced to one per 100 ms by m_paneState. The places that change those call
    // m_paneState.changed(); it costs nothing while nobody listens (onPaneState unset). The
    // remote* functions below are what the phone's actions come to: each takes only ids this pane
    // minted and re-checks, against the pane as it is now, that the row still offers the action.
public:
    std::function<void(const QJsonObject &state)> onPaneState;   // wired to RemoteShare in phase B

    relay::panestate::Inputs remoteState() const {
        relay::panestate::Inputs in;
        in.pane = m_token;
        in.busy = m_agentBusy;
        in.toolRunning = m_agentBusy && !m_liveCall.isEmpty();
        const relay::panestatus::Facts facts = statusFacts();
        in.waiting = facts.programAsking || facts.handoffWaiting || facts.questionOpen;
        in.clock = m_agentBusy ? m_turnClockText : QString();
        // The reasoning fold (issue T8CN): its text is the buffer's tail whatever the display mode
        // — the phone draws its own copy from the same bytes — and it is "visible" when the latest
        // fold is on screen. The header says live or last, so a finished trace is never mistaken
        // for a streaming one.
        const QString thinkingUri = !m_thinkingAnchor.isEmpty() ? m_thinkingAnchor : m_lastThinkingAnchor;
        const relay::calllines::Ref thinkingRef = relay::calllines::parseUri(thinkingUri);
        const QString thinkingTurn = thinkingRef.valid ? thinkingRef.turn : QString();
        in.thinkingText = m_turnThinking.value(thinkingTurn).right(relay::panestate::kTailMax);
        in.thinkingVisible = !in.thinkingText.isEmpty() && thinkingDisplay() != QLatin1String("never")
                             && m_backend && m_backend->foldExpanded(thinkingUri);
        in.thinkingHeader = !m_thinkingAnchor.isEmpty()
            ? QStringLiteral("Thinking… · %1").arg(m_model.isEmpty() ? QStringLiteral("agent") : m_model)
            : QStringLiteral("Thought · last turn · %1").arg(m_model.isEmpty() ? QStringLiteral("agent") : m_model);

        in.queuePaused = queueBlocked();
        in.pauseReason = !m_pauseReason.isEmpty() ? m_pauseReason
                         : queueHeldBySelection() ? QStringLiteral("Held while the next item is edited on the desktop")
                                                  : QString();
        for (const QueueRow &row : queueRows()) {
            if (row.kind == QLatin1String("running")) { in.running = row.preview; continue; }
            relay::panestate::Row out{row.id, row.kind, row.preview, row.state, false};
            for (const QueueEntry &entry : m_entries)
                if (QStringLiteral("entry:%1").arg(entry.id) == row.id) { out.written = entry.written(); break; }
            in.rows << out;
        }
        const bool headSteerable = m_selected == 0 && m_agentBusy && !m_entries.isEmpty() && m_entries.first().agent
                                   && !m_entries.first().written();
        in.queueHint = relay::panestate::queueHint(!m_selectedSteer.isEmpty(), headSteerable, m_selected >= 0);

        // The chip's own text, and the rows of its menu that switch the model: the presets with a
        // stored key (m_stored), then the agent roles. Not the gear (desktop settings) and not
        // the "this turn" image row, which is not a choice.
        QString chip = m_modelBox ? m_modelBox->currentText() : m_model;
        in.modelLabel = chip.remove(QChar(0x2713)).trimmed();
        for (const auto &model : std::as_const(m_stored))
            in.choices << relay::panestate::Choice{QStringLiteral("preset:") + model.first, conciseModel(model.first, model.second),
                                                   model.first == m_currentPreset && m_agentRole == QLatin1String("main")};
        QStringList roles{QStringLiteral("main"), QStringLiteral("flash")};
        if (hasLocalEndpoint()) roles << QStringLiteral("local");
        if (!roles.contains(m_agentRole)) roles << m_agentRole;
        for (const QString &role : std::as_const(roles)) {
            const QString model = roleModelText(role);
            in.choices << relay::panestate::Choice{QStringLiteral("role:") + role,
                                                   roleLabel(role) + (model.isEmpty() ? QString() : QStringLiteral(" · ") + model),
                                                   role == m_agentRole};
        }

        in.mode = m_modeValue;
        if (m_editor) in.placeholder = m_editor->placeholderText().isEmpty() ? m_savedPlaceholder : m_editor->placeholderText();

        if (m_ctxLabel && m_ctxWindow > 0) {
            in.contextLabel = m_ctxLabel->text();
            const double percent = m_ctxNextWindow > 0 ? m_ctxNextPercent : m_ctxPercent;
            in.percentLeft = int(std::lround(std::clamp(100.0 - percent, 0.0, 100.0)));
        }

        // The session manager's rows (/resume, /conversations): agent sessions of this project, as
        // the worker last listed them. Terminal history cannot be resumed and threads belong to
        // their session, so neither is a row here. Nor is a guest session (protocol 26.7):
        // resuming one runs claude or codex in the pane's shell, which is not something a paired
        // device gets to do from a list it is only watching. Observing only: nothing here opens one.
        const QDateTime now = QDateTime::currentDateTime();
        for (const QJsonValue &value : m_remoteSessions) {
            const QJsonObject item = value.toObject();
            const QString id = item.value(QStringLiteral("session_id")).toString();
            const QString source = item.value(QStringLiteral("source")).toString();
            if (id.isEmpty() || source == QLatin1String("terminal") || source == QLatin1String("subagent")
                || relay::conversations::isGuestSource(source)) continue;
            QString title = item.value(QStringLiteral("title")).toString();
            if (title.trimmed().isEmpty()) title = item.value(QStringLiteral("first_prompt")).toString();
            if (title.trimmed().isEmpty()) title = QStringLiteral("Untitled conversation");
            const bool current = id == m_sessionId;
            in.sessions << relay::panestate::Session{id, title,
                                                     relay::conversations::whenText(item.value(QStringLiteral("updated")).toDouble(), now),
                                                     current, current && m_agentBusy};
        }
        // The theme the desktop is drawing itself in, by id: the web view has a generated block per
        // shipped theme (app/pane-theme.css) and follows this one (protocol section 16). The
        // application's active theme is the right answer even now that a tab owns a theme, because
        // the tokens, the palette and the stylesheet are the application's — the tab in front
        // decides, and that is the theme this pane is being painted in at this instant. A tab change
        // sets it again, which is a themeChanged() and so a republish.
        in.theme = relay::theme::activeThemeId();
        in.canNew = m_workerReady && !m_agentBusy;
        in.canOpen = m_workerReady && !m_agentBusy;   // as the session manager's own rows behave
        return in;
    }

    // The whole state now, whatever the interval: the answer to a phone's pane_state_get.
    QJsonObject paneStateNow() { return m_paneState.publishNow(); }

    // Ask the worker for the session list a phone sees. Its answer arrives as `conversations`
    // with this id and lands in noteRemoteSessions().
    void requestRemoteSessions() {
        if (!m_workerReady) return;
        send({{"type", "conversations"}, {"id", "conv-remote"}, {"scope", "project"},
              {"workspace", m_workspace}, {"limit", relay::panestate::kSessionsMax}});
    }

    // Queue actions from a phone (queue_remove, queue_move, queue_edit, queue_send_now). False when
    // the row is gone or no longer offers the action — the phone saw an older state.
    bool remoteQueueRemove(const QString &rowId) {
        return remoteRowOffers(rowId, QStringLiteral("remove")) && removeRow(rowId);
    }
    bool remoteQueueMove(const QString &rowId, const QString &to) {
        if (!remoteRowOffers(rowId, to)) return false;
        if (to == QLatin1String("to_queue")) { withdrawSteer(rowId.mid(6), SteerEntry::ToQueue); return true; }
        const quint64 id = rowId.mid(6).toULongLong();
        if (to == QLatin1String("steer")) return !steerQueuedEntry(id).isEmpty();
        int from = -1;
        for (int i = 0; i < m_entries.size(); ++i) if (m_entries[i].id == id) { from = i; break; }
        const int target = from + (to == QLatin1String("up") ? -1 : 1);
        if (from < 0 || target < 0 || target >= m_entries.size()) return false;
        const quint64 selected = selectedEntryId();
        m_entries.move(from, target);
        keepSelectionOn(selected);   // a row the desktop is editing keeps its highlight, wherever it went
        rebuildQueueStrip(); changed();
        return true;
    }
    // Take a row back for the phone's prompt box: a steer is withdrawn, a queued row removed, and
    // the text goes to the phone. Nothing lands in this pane's own prompt box.
    bool remoteQueueEdit(const QString &rowId, QString *text) {
        if (!remoteRowOffers(rowId, QStringLiteral("edit"))) return false;
        if (rowId.startsWith(QLatin1String("steer:"))) {
            const QString requestId = rowId.mid(6);
            for (const auto &steer : std::as_const(m_steering))
                if (steer.requestId == requestId) { *text = steer.text; break; }
            withdrawSteer(requestId, SteerEntry::Drop);
            return true;
        }
        const quint64 id = rowId.mid(6).toULongLong();
        for (const QueueEntry &entry : std::as_const(m_entries))
            if (entry.id == id) { *text = entry.text; removeEntry(id); return true; }
        return false;
    }
    bool remoteQueueSendNow(const QString &rowId) {
        return remoteRowOffers(rowId, QStringLiteral("send_now")) && sendSteerNow(rowId.mid(6));
    }

    // model_pick: a token from this pane's last pane_state, resolved here. Only what the menu
    // offered can be reached — a stored-key preset or a role — never the provider settings.
    bool remoteModelPick(const QString &choiceId, const QString &deviceName) {
        const QString key = m_paneState.choiceKey(choiceId);
        const QString who = deviceName.trimmed().isEmpty() ? QStringLiteral("a paired device") : deviceName.trimmed();
        if (key.startsWith(QLatin1String("role:"))) {
            const QString role = key.mid(5);
            if (role == QLatin1String("local") && !hasLocalEndpoint()) return false;
            chooseAgentRole(role);
        } else if (key.startsWith(QLatin1String("preset:"))) {
            const QString preset = key.mid(7);
            const bool stored = std::any_of(m_stored.cbegin(), m_stored.cend(), [&](const auto &model) { return model.first == preset; });
            if (!stored || !m_configured) return false;
            selectModel(preset);
        } else {
            return false;
        }
        status(QStringLiteral("Model changed from %1 · %2").arg(who, remoteState().modelLabel));
        return true;
    }

    // conversation_open: a token from this pane's last pane_state, resolved here against the list
    // it published. Never a path or a session file name — those would read another conversation
    // into this pane. The owner's level only; the hub sends the list to nobody else.
    bool remoteConversationOpen(const QString &sessionId, const QString &deviceName) {
        if (!m_workerReady || m_agentBusy) return false;
        const QString key = m_paneState.sessionKey(sessionId);
        if (key.isEmpty()) return false;
        for (const QJsonValue &value : std::as_const(m_remoteSessions)) {
            const QJsonObject item = value.toObject();
            if (item.value(QStringLiteral("session_id")).toString() != key) continue;
            openSavedSession(item, false);   // this pane, not a new one: the phone is watching it
            status(QStringLiteral("Conversation opened from %1 · %2")
                       .arg(deviceName.trimmed().isEmpty() ? QStringLiteral("a paired device") : deviceName.trimmed(),
                            item.value(QStringLiteral("title")).toString()));
            return true;
        }
        return false;
    }

    // conversation_new: the same as /new, refused while a turn runs exactly as /new is.
    bool remoteConversationNew(const QString &deviceName) {
        if (!m_workerReady || m_agentBusy) return false;
        newChat();
        status(QStringLiteral("New conversation from %1")
                   .arg(deviceName.trimmed().isEmpty() ? QStringLiteral("a paired device") : deviceName.trimmed()));
        requestRemoteSessions();
        return true;
    }

private:
    bool remoteRowOffers(const QString &rowId, const QString &action) const {
        return relay::panestate::actionsFor(remoteState(), rowId).contains(action);
    }
    // The worker's answer to requestRemoteSessions(), or to the session manager's own unfiltered
    // listing, which is the same list. A search's results are not the list and are left alone.
    void noteRemoteSessions(const QJsonObject &event) {
        const QString id = event.value(QStringLiteral("id")).toString();
        const bool listing = id == QLatin1String("conv-list") && event.value(QStringLiteral("query")).toString().trimmed().isEmpty()
                             && event.value(QStringLiteral("offset")).toInt() == 0;
        if (id != QLatin1String("conv-remote") && !listing) return;
        m_remoteSessions = event.value(QStringLiteral("items")).toArray();
        m_paneState.changed();
    }

    QJsonArray m_remoteSessions;   // the session list a phone sees (noteRemoteSessions)
    relay::panestate::Publisher m_paneState{[this] { return remoteState(); },
                                            [this](const QJsonObject &state) { if (onPaneState) onPaneState(state); },
                                            [this] { return bool(onPaneState); }};

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
        if (relay::queuesubmit::decide(queueSubmitState()) == relay::queuesubmit::Decision::StartNow) startAgentEntry(entry, false);
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
        state.boxFree = !m_native && !inQueueSelection() && m_editor->toPlainText().trimmed().isEmpty();
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
            const bool prefix = m_modeValue != QStringLiteral("shell");
            if (prefix) setPrefixMode(QStringLiteral("shell"));   // this submission only
            setComposerText(command);
            m_handoffPrefill = report; m_handoffPrefix = prefix;
            m_handoffOffered = true;   // status glyph (#XM0T): "recommends", or "needs you" with report_back
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
            if (relay::queuesubmit::decide(queueSubmitState()) == relay::queuesubmit::Decision::StartNow) startAgentEntry(entry, false);
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

    // /light, /dark, /theme: through the window when it listens (a tab's own theme), else app-wide.
    void chooseTheme(const QString &id) {
        const bool ok = onChooseTheme ? onChooseTheme(id) : relay::theme::setActiveTheme(id);
        status(ok ? QStringLiteral("Theme: %1.").arg(relay::theme::active().name) : QStringLiteral("The %1 theme could not be read.").arg(id));
    }

    static QString shellQuote(const QString &value) {
        QString quoted = value;
        quoted.replace('\'', QStringLiteral("'\\''"));
        return '\'' + quoted + '\'';
    }

    // ----- inline output in the terminal -------------------------------------------------
    enum class Ink { Agent, User, UserAgent, Tool, ToolOutput, DiffAdd, DiffRemove, Error, Note, Recap, Ask };

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
        // The one thing waiting on a person (#MQ9C): amber, the warning token, which is what
        // every other "needs you" mark in Relay is drawn in (the status glyph, the work chip).
        case Ink::Ask: return t::Warning;
        }
        return t::Text;
    }

    static QByteArray inkCode(Ink ink) {
        // The one ink that is written with the *indexed* palette rather than 24-bit RGB (#MQ9C).
        // Everything printed into the terminal is frozen at the colour it was written in — the
        // emulator cannot recolour its scrollback — and a question card is the one piece of inline
        // output that is still actionable after a theme switch. Indexed bold yellow is what each
        // theme's own palette renders (`[terminal] palette[3]`: #ecc476 on Relay Dark, the ochre
        // #7a5400 on IBM Beige), so the card follows the theme instead of keeping a dark theme's
        // amber on a light ground. It is the colour MarkdownAnsi's **Need:** bold already uses
        // for the same reason (src/MarkdownAnsi.h, card #4E13).
        if (ink == Ink::Ask) return QByteArray("\x1b[1;33m");
        const QColor c = inkColor(ink);
        // Bold for the lines the user typed, plain otherwise. Notes are not italic: the muted ink
        // marks them, and italic muted monospace was the hardest text to read (docs/ARCHITECTURE.md,
        // "Legible text").
        // Bold amber for a question, as card #4E13 asked: "questions or items needing human
        // response could be in bold amber".
        const QByteArray style = (ink == Ink::User || ink == Ink::UserAgent || ink == Ink::Ask)
                                     ? QByteArray("1;") : QByteArray();
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
        relay::installCopyOnSelect(m_transcriptView, [this](const QString &text) { toastCopied(text); });
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
        // The same bold the terminal gives these two: a line the user sent, and a question waiting
        // on them (#MQ9C). Here the token itself is used — a widget repaints on a theme switch.
        if (ink == Ink::User || ink == Ink::Ask) format.setFontWeight(QFont::Bold);
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
        if (!inlineReady()) { m_inlinePending.append({text, ink}); appendTranscript(text, ink); return; }
        endCallRun();   // a held tool-call row ends before anything else prints (#TK9C)
        const QString clean = sanitize(text);
        if (clean.isEmpty()) return;
        if (ink == Ink::Agent) beginBlock(relay::gaps::Block::Agent);
        else if (ink == Ink::User || ink == Ink::UserAgent) beginBlock(relay::gaps::Block::User);
        else if (ink == Ink::Tool) beginBlock(relay::gaps::Block::Call);
        QByteArray out;
        if (!m_inlineOpen) {
            // A remote prompt without Relay's integration cannot be asked to redraw itself: keep
            // its text, and print it back when the block closes (card #S5SH).
            m_login.promptRow.clear();
            m_login.promptBytes.clear();
            if (loginAtPrompt() && !m_login.integration) {
                const QPoint cursor = m_backend->cursorPosition();
                // The bytes the host drew the prompt with, so it comes back in its own colours. They
                // are only used when the text in them ends exactly where the cursor is: a prompt drawn
                // with cursor moves (a zsh right-hand prompt) would come back the wrong width, and the
                // screen's own text, padded to the cursor, is the safe answer then.
                int width = 0;
                const QByteArray echo = relay::remote::promptEcho(m_login.line, &width);
                if (cursor.x() > 0 && width == cursor.x()) m_login.promptBytes = echo;
                else if (cursor.y() >= 0)
                    m_login.promptRow = m_backend->screenText().split('\n').value(cursor.y()).left(cursor.x()).leftJustified(cursor.x(), ' ');
            }
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
        // A line the user typed carries a *role*, not a colour: every row of it is marked with the
        // private OSC 7772 ("shell" / "agent"), and the engine view paints that row's band and ink
        // from the theme in force when it paints (EngineBackend::applyThemeColors, ColorScheme.h).
        // A colour written here would be frozen in the scrollback; a mark follows a theme switch
        // (owner, 2026-09-19: "can we change the design that the background highlights shift with
        // theme changes"). The text itself is bold in the default foreground.
        if (ink == Ink::User || ink == Ink::UserAgent) {
            const QByteArray mark = ink == Ink::User ? QByteArray("\x1b]7772;shell\x1b\\") : QByteArray("\x1b]7772;agent\x1b\\");
            QString body = QStringLiteral("\x1b[1m");
            for (const QChar ch : clean) { if (ch == '\n') body += QStringLiteral("\x1b[0m\n\x1b[1m"); else body += ch; }
            body += QStringLiteral("\x1b[0m");
            // The word wrapper breaks a long line into rows of its own, so the mark goes at the
            // head of each row that holds something — never after the last newline, which would
            // hand the role to whatever prints next.
            const QByteArray rows = wrapped(body) + terminalLines(m_wrap.flush());
            int from = 0;
            while (from < rows.size()) {
                int end = rows.indexOf("\r\n", from);
                end = end < 0 ? rows.size() : end + 2;
                const QByteArray row = rows.mid(from, end - from);
                const bool empty = QByteArray(row).replace("\x1b[0m", "").replace("\x1b[1m", "").trimmed().isEmpty();
                out += empty ? row : mark + row;
                from = end;
            }
            m_atLineStart = clean.endsWith('\n');
            writeTerminal(out);
            return;
        }
        const QString code = QString::fromUtf8(inkCode(ink));
        QString body = code;
        for (const QChar ch : clean) { if (ch == '\n') body += QStringLiteral("\x1b[0m\n") + code; else body += ch; }
        body += QStringLiteral("\x1b[0m");
        out += wrapped(body) + terminalLines(m_wrap.flush());
        m_atLineStart = clean.endsWith('\n');
        writeTerminal(out);
    }

    // While Relay's own output owns the cursor row, a resize (a split, the queue strip opening or
    // closing) must not reach the shell: Readline answers SIGWINCH with "\r\x1b[K",
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

    // endCallRun() first: a held tool-call row (a run of reads that may still grow) owes a newline,
    // and once it is written the cursor is already at the start of a line (#TK9C).
    void ensureLineStart() { endCallRun(); if (m_inlineOpen && !m_atLineStart) printInline(QStringLiteral("\n"), Ink::Note); }

    // A block of kind `next` is about to print: a blank line first when it follows a block of
    // another kind (#5AWD, the rule is src/TranscriptGaps.h). Prose and ▸ rows are set apart, a run
    // of rows stays single-spaced, and a ✦ line the user typed is set off from whatever came before
    // it — including the previous turn, across the closed block and the shell prompt between.
    // Notes, errors and tool output carry no kind: they stay attached to the block they follow.
    void beginBlock(relay::gaps::Block next) {
        const bool gap = relay::gaps::gapBefore(m_lastBlock, next);
        m_lastBlock = next;
        if (!gap) return;
        ensureLineStart();
        printInline(QStringLiteral("\n"), Ink::Note);   // Note: carries no kind, so no recursion
    }

    void closeInline() {
        if (!m_inlineOpen) return;
        endCallRun();
        if (m_markdown.holding()) writeTerminal(wrapped(m_markdown.finish()));
        writeTerminal(takeWrapped());
        if (!m_atLineStart) writeTerminal("\r\n");
        m_inlineOpen = false; m_atLineStart = true;
        holdShellResize(false);   // the cursor is on a fresh row: the shell may redraw there
        // Ctrl+X Ctrl+P is bound to a no-op shell function; Readline redraws the prompt after it.
        if (m_backend && shellIdleAtPrompt()) m_backend->redrawPrompt();
        else if (m_backend && loginAtPrompt()) {
            // The remote shell: the integration binds the same keys there; without it the prompt
            // is printed back as it was, and the remote line editor never knew it was gone.
            if (m_login.integration) sendShellInput(QStringLiteral("\x18\x10"));
            else if (!m_login.promptBytes.isEmpty()) writeTerminal(m_login.promptBytes + "\x1b[0m");
            else if (!m_login.promptRow.isEmpty()) writeTerminal(sanitize(m_login.promptRow).toUtf8());
            m_login.promptRow.clear(); m_login.promptBytes.clear();
        }
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
        status(QStringLiteral("Restored %1 line(s) of scrollback from this pane's previous shell.").arg(lines.size()));
    }

    // Where inline output may go now: a local shell idle at its prompt, or a remote one (#S5SH).
    // Not under mosh: mosh-client repaints the whole screen from the server's copy, which has
    // never heard of Relay's lines, so they would be drawn over; its replies stay in the panel.
    bool inlineReady() const {
        // Never onto the alternate screen: mosh and a remote tmux both repaint it from their own
        // copy, which has never heard of Relay's lines, so they would be drawn over (#S5SH).
        return shellIdleAtPrompt()
            || (loginAtPrompt() && !m_altScreen && !m_login.program.startsWith(QStringLiteral("mosh")));
    }

    void flushInline() {
        if (m_inlinePending.isEmpty() || !inlineReady()) return;
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
        beginBlock(relay::gaps::Block::Header);   // set off from the ✦ line; what follows joins it (#5AWD)
        ensureLineStart();
        printInline(QStringLiteral("▸ ") + (m_model.isEmpty() ? QStringLiteral("agent") : m_model) + '\n', Ink::Note);
    }


    QJsonObject presetById(const QString &id) const {
        for (const auto &item : m_presets)
            if (item.toObject().value(QStringLiteral("id")).toString() == id) return item.toObject();
        return {};
    }

    // The presets that are *providers*: everything the worker reported except the guests (29.3).
    // A guest holds no key, answers to no tier and cannot run a subagent, so it has no place in
    // the keys dialog, the roles tables or a subagent's model menu — only in the model box, where
    // it is the pane's own agent.
    QJsonArray providerPresets() const {
        QJsonArray rows;
        for (const auto &item : m_presets)
            if (item.toObject().value(QStringLiteral("group")).toString() != QStringLiteral("guest")) rows.append(item);
        return rows;
    }

    // Configure a built-in preset using its key from the keyring. The key never enters this process.
    void configurePreset(const QString &id, bool announce) {
        const auto preset = presetById(id);
        if (preset.isEmpty()) return;
        if (m_workspace.isEmpty()) m_workspace = QDir::currentPath();
        QSettings settings;
        const int tokens = settings.value(QStringLiteral("provider/max_tokens"), 0).toInt();
        settings.setValue("provider/preset", id);
        settings.setValue("provider/base", preset.value(QStringLiteral("base_url")).toString());
        settings.setValue("provider/model", preset.value(QStringLiteral("model")).toString());
        settings.setValue("provider/extra", QString::fromUtf8(QJsonDocument(preset.value(QStringLiteral("extra")).toObject()).toJson(QJsonDocument::Compact)));
        rememberPresetBeforeGuest(id);
        m_apiKey.clear(); m_configured = false; m_configuring = true; m_currentPreset = id; changed();
        if (announce) status(QStringLiteral("Switching model. This starts a new conversation."));
        // Through the funnel, spelled the way tests/boardworkspace_test.cpp counts it: every
        // `configure` this pane sends is `withSessionFields(QJsonObject{{"type", "configure"} …`,
        // so none can leave without its board block.
        QJsonObject request = withSessionFields(QJsonObject{{"type", "configure"}, {"preset", id}, {"use_stored_key", true},
              {"base_url", preset.value(QStringLiteral("base_url")).toString()},
              {"model", preset.value(QStringLiteral("model")).toString()},
              {"extra", preset.value(QStringLiteral("extra")).toObject()}, {"max_tokens", tokens},
              {"api_key", QString()}, {"workspace", m_workspace}, {"keybindings", Keymap::instance().catalog()}});
        // A guest preset (29.3): the harness is started with this — `permissions`, and `model`,
        // `resume` or `fork` when the pick carried them. The harness runs in `workspace`. Added
        // after the funnel, which neither reads nor writes it.
        if (const QJsonObject guest = takeGuestRequest(id); !guest.isEmpty()) request.insert(QStringLiteral("guest"), guest);
        send(request);
        updatePaths();
    }

    // The queue-submit rule's view of this pane right now (#N8VK): a turn the worker has reported
    // running, and an agent entry that has left the queue without that report yet.
    relay::queuesubmit::State queueSubmitState() const {
        relay::queuesubmit::State state;
        state.agentBusy = m_agentBusy;
        state.agentTurnStarting = m_activeValid && m_active.agent;
        return state;
    }

    // Agent prompts and terminal commands share one queue per pane, as two resources: an agent
    // prompt starts at once whenever the agent itself is free, whatever shell work is running or
    // queued ahead of it (relay::queuesubmit, #N8VK); a shell command waits for the terminal.
    // The worker only ever receives one agent turn at a time from here.
    void submitAgent(const QString &text, bool fromEditor, const QString &why = QString(), QString when = QString(),
                     const QString &shellText = QString()) {
        if (!m_configured) {
            if (fromEditor) { configure(); return; }
            status(QStringLiteral("No agent provider is configured."));
            return;
        }
        if (fromEditor) { m_editor->remember(text); m_editor->clear(); skillSlashHint(text); }
        // Trigger (1) of "Initialize a project and create a Switchboard here?": the first prompt
        // sent to the agent in a pane standing in a git repository that has no board (19.12). The
        // prompt is **not** held for it — it goes on below and the turn starts now; the question is
        // raised on the next turn of the event loop and a yes takes effect for the turn after, as
        // a deferred `set_board`. A line typed on a phone or by a guest never raises it: a remote
        // participant cannot answer a question about a folder on this machine.
        if (!m_remoteSubmit) {
            QPointer<Pane> guard(this);
            QTimer::singleShot(0, this, [guard] {
                if (guard) guard->askProjectInit(relay::projectinit::Trigger::AgentWork);
            });
        }
        QueueEntry entry;
        entry.agent = true; entry.text = text; entry.why = why; entry.attachments = attachmentsFor(text);
        entry.shellText = shellText;
        entry.noHandoff = m_remoteSubmit;
        if (m_remoteSubmit) entry.author = m_remoteAuthor;   // a guest's name on their row
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
        // Ctrl+Enter, the `*` prefix, AGENT-mode Enter and every other door into here are the
        // same submission: when the agent itself is free it starts now, bypassing the queue
        // exactly as the interrupt branch above does, and the queued items keep their order
        // (#N8VK). Only a busy — or just-started — agent turn sends a prompt to the back.
        if (relay::queuesubmit::decide(queueSubmitState()) == relay::queuesubmit::Decision::StartNow) {
            startAgentEntry(entry, false);
            return;
        }
        enqueue(entry);
    }

    // A prompt from a paired device. It never touches the composer: the person at the desktop may
    // be typing, and their draft is theirs. `route` asks the worker's router to decide, exactly as
    // the composer does; without it the text can only reach the agent, which is what keeps a
    // view-or-agent device away from the shell.
    void submitRemote(const QString &text, bool route, const QString &origin,
                      const QString &when = QStringLiteral("now"), const QString &originName = QString()) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) return;
        // A line typed on a paired phone, tablet or guest browser is a person's line and is kept
        // like any other (owner, 2026-09-18: "these should always be saved"). It is written here,
        // before anything is routed or refused, so a prompt that bounces off an unconfigured agent
        // is still there to recall — there is no prompt box out there holding on to it. It goes
        // straight to this pane's file rather than through the composer, whose draft and browse
        // position belong to whoever is sitting at this desk; the composer takes it in on its next
        // Up, and the file stores the same line once however this one is routed.
        relay::prompthistory::append(promptHistoryPath(), trimmed);
        const QString who = originName.trimmed().isEmpty()
                                ? (origin.isEmpty() ? QStringLiteral("a phone") : origin)
                                : originName.trimmed();
        status(QStringLiteral("Prompt from %1").arg(who));
        // "steer" is the phone's third choice: deliver it inside the running turn at the agent's
        // next tool call, which is what Enter on an empty prompt box does here (#C4M8). With no
        // turn to steer, the worker queues it, exactly as the desktop's own steer does.
        m_remoteAuthor = originName.trimmed();
        // A question card is up (#MQ9C, protocol 27.4): the line answers it — but only when it was
        // going to the agent anyway, which is the rule at the desk and now the rule here too
        // (owner, 2026-09-19). This used to answer the card before anything was routed, so a phone
        // could not reach the shell at all until somebody dealt with the question. These two doors
        // are agent-bound by themselves: a steer is aimed at the running turn, and a device that
        // cannot ask the router can only reach the agent. The routed line is decided in
        // takeRemoteRoute, when the verdict is in.
        const bool steering = when == QLatin1String("steer") && m_agentBusy;
        if (relay::input::cardTakesRemoteLine({m_ask.open(), false, false})
            && (steering || !route || !m_workerReady)
            && answerQuestion(trimmed, m_remoteAuthor)) {
            m_remoteAuthor.clear();
            return;
        }
        if (steering) {
            m_remoteSubmit = true; submitAgent(trimmed, false, who); m_remoteSubmit = false;   // the name, not the id
            if (!m_entries.isEmpty() && m_entries.last().agent) {
                m_lastQueuedEntryId = m_entries.last().id;
                m_lastQueuedAt.start();
                upgradeLastQueuedToSteer();
            }
            m_remoteAuthor.clear();
            return;
        }
        if (!route || !m_workerReady) {
            m_remoteSubmit = true; submitAgent(trimmed, false, who); m_remoteSubmit = false;   // the name, not the id
            m_remoteAuthor.clear();
            return;
        }
        const QString id = QStringLiteral("remote-") + QString::number(++m_requestId);
        m_remotePrompts.insert(id, {trimmed, origin, originName.trimmed()});
        // The author travels with the pending prompt, not in m_remoteAuthor: the router's verdict
        // comes back later, and anything submitted at the desk in between would otherwise arrive
        // on the queue wearing the phone's name. takeRemoteRoute sets it again for its own submit.
        m_remoteAuthor.clear();
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
        const bool toShell = event.value(QStringLiteral("route")).toString() == QStringLiteral("shell");
        // The card takes the line only now, and only when the router sent it to the agent (27.4):
        // the question is what the agent is blocked on, and a command is not an answer to it.
        if (relay::input::cardTakesRemoteLine({m_ask.open(), true, toShell})) {
            m_remoteAuthor = prompt.author;
            const bool answered = answerQuestion(prompt.text, prompt.author);
            m_remoteAuthor.clear();
            if (answered) return true;
        }
        if (toShell) {
            submitTerminal(prompt.text, false);
        } else {
            // The name, not the id, and the author on the queue row: a routed prompt is as much
            // somebody's as an unrouted one, and it used to arrive on the row with neither.
            m_remoteAuthor = prompt.author;
            m_remoteSubmit = true;
            submitAgent(prompt.text, false, prompt.author.isEmpty() ? prompt.origin : prompt.author);
            m_remoteSubmit = false;
            m_remoteAuthor.clear();
        }
        return true;
    }

    void submitTerminal(const QString &text, bool watch, bool natural = false, bool handoff = false) {
        m_shareFailed = false;          // the pane is in use again (shareStatus, protocol 6.3)
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
    // Relay's rich composer is the foreground guest's input line. We deliberately keep this out
    // of `program_input`: that tool is for an LLM acting under a per-turn delegation grant, while
    // this is the user's own composer text. Ctrl+U clears the TUI line, bracketed paste keeps a
    // multiline prompt literal, and Escape after a slash selection closes the guest menu before
    // Enter (otherwise Claude/Codex consumes Enter as menu navigation).
    bool typeIntoGuest(const QString &guest, const QString &text) {
        if (text.trimmed().isEmpty()) return false;    // nothing to type; submitGuest sends the Return
        if (!m_backend || guest != m_guest) {
            status(QStringLiteral("Guest input was not sent: %1 is no longer in this pane.")
                       .arg(guestDisplayName(guest)));
            return false;
        }
        m_backend->sendInput(QByteArrayLiteral("\x15"));   // Ctrl+U: clear the guest's current input line
        QString body = text;
        // `!` enters the TUI's shell mode only as a keystroke on an empty line (26.8): pasted, it
        // is one more character of a prompt. So the bang goes first, on its own, and the command
        // after it is pasted as usual.
        if (body.startsWith(QLatin1Char('!'))) {
            m_backend->sendInput(QByteArrayLiteral("!"));
            body = body.mid(1);
        }
        m_backend->sendText(body, true);
        if (text.trimmed().startsWith(QLatin1Char('/')))
            m_backend->sendInput(QByteArrayLiteral("\x1b"));  // dismiss its slash popup before Enter
        m_backend->sendInput(QByteArrayLiteral("\r"));
        status(QStringLiteral("Sent to %1.").arg(guestDisplayName(guest)));
        return true;
    }

    void submitGuest(const QString &text, bool fromEditor) {
        if (m_guest.isEmpty()) return;
        if (fromEditor) {
            m_editor->remember(text);
            m_editor->clear();
            hideAtPopup(); hideSlashPopup(); clearAiGhost();
        }
        // Enter on an empty box. The composer is the guest's only input (the terminal does not
        // take focus), so without this there was no way at all to answer a guest's own TUI
        // question — its "1. Yes" list, its file picker — from the prompt box, and Enter instead
        // reported the guest as "no longer ready". A bare Return belongs to what is on the guest's
        // screen now, so it is never queued behind anything.
        if (text.trimmed().isEmpty()) {
            if (m_backend) m_backend->sendInput(QByteArrayLiteral("\r"));
            return;
        }
        if (m_entries.isEmpty() && !m_activeValid && !m_guestBusy) {
            typeIntoGuest(m_guest, text);
            return;
        }
        QueueEntry entry;
        entry.guest = m_guest;
        entry.text = text;
        enqueue(entry);
    }

    void enqueue(QueueEntry entry) {
        // A queued item is edited where it stands now (selectQueueEntry / saveQueueEdit), so nothing
        // is ever pulled out and resubmitted at the head: everything queued here joins the back.
        entry.id = ++m_entrySerial;
        m_entries.append(entry);
        m_selected = -1;
        if (entry.agent && m_agentBusy) { m_lastQueuedEntryId = entry.id; m_lastQueuedAt.start(); }
        // The steer offer ("Enter again…") promises a running turn to steer into; an entry queued
        // while a turn is merely starting has none yet, so it says only that it waits (#N8VK).
        status(entry.agent
                   ? (m_agentBusy
                          ? QStringLiteral("Queued · the agent prompt runs after the items ahead of it · Enter again to send at the next tool call")
                          : QStringLiteral("Queued · the agent prompt runs when its turn comes"))
                   : !entry.guest.isEmpty()
                       ? QStringLiteral("Queued · sent to %1 when it is ready").arg(guestDisplayName(entry.guest))
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
        if (const QJsonArray skills = skillsFor(entry.text); !skills.isEmpty()) request.insert(QStringLiteral("skills"), skills);
        const QString program = processBusy() ? foregroundCommandLine() : QString();
        // The terminal's directory always goes along: `cd` in the terminal must move the agent too.
        QJsonObject context{{"terminal_cwd", m_cwd}};
        // An ssh or mosh login: which host, and whether the agent can run commands there over the
        // user's own connection (docs/SSH-AND-MOSH.md, section 7).
        if (const QJsonObject login = loginContext(); !login.isEmpty()) context.insert(QStringLiteral("remote_session"), login);
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
        if (!head.guest.isEmpty()) {
            if (m_guestBusy || head.guest != m_guest) return;
            m_entries.removeFirst();
            if (!typeIntoGuest(head.guest, head.text)) m_entries.prepend(head);
        } else if (head.agent) {
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
        m_selectedSteer.clear();   // before the text changes, so it does not read as editing the steer
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
        m_selectedSteer.clear();   // before the box empties, so that does not read as editing the steer
        m_editor->clear();
        rebuildQueueStrip(); changed();
        pumpQueue();   // nothing is held any more
    }

    quint64 selectedEntryId() const {
        return (m_selected >= 0 && m_selected < m_entries.size()) ? m_entries[m_selected].id : 0;
    }

    // ----- one list: steers, then queued items ----------------------------------------------------
    // The keyboard walks one list in delivery order: steers still waiting for the next tool call,
    // then the queue. A steer being withdrawn is shown greyed but is not a row the keys can land on.
    // m_selected indexes m_entries and m_selectedSteer names a steer; at most one is set.
    bool inQueueSelection() const { return m_selected >= 0 || !m_selectedSteer.isEmpty(); }
    QStringList liveSteers() const {
        QStringList ids;
        for (const auto &steer : m_steering) if (!steer.withdraw) ids << steer.requestId;
        return ids;
    }
    int selectedQueueRow() const {
        const QStringList steers = liveSteers();
        if (!m_selectedSteer.isEmpty()) return int(steers.indexOf(m_selectedSteer));
        return (m_selected >= 0 && m_selected < m_entries.size()) ? int(steers.size()) + m_selected : -1;
    }
    void selectQueueRow(int row) {
        const QStringList steers = liveSteers();
        if (row < 0) return;
        if (row < steers.size()) selectSteer(steers[row]);
        else selectQueueEntry(row - int(steers.size()));
    }

    // Show a steer in the prompt box, like a queued item. It is still a steer while the text is
    // untouched: the first edit takes it back out of the turn (takeBackEditedSteer), and so does
    // Enter on it; Esc or arrowing away leaves it waiting.
    void selectSteer(const QString &requestId) {
        for (const auto &steer : m_steering) {
            if (steer.requestId != requestId || steer.withdraw) continue;
            m_selected = -1;
            m_selectedSteer = requestId;
            m_editor->setPlainText(steer.text);
            m_editor->moveCursor(QTextCursor::End);
            rebuildQueueStrip(); changed();
            pumpQueue();   // a selection that left the head of the queue releases it
            return;
        }
    }
    QString selectedSteerText() const {
        for (const auto &steer : m_steering) if (steer.requestId == m_selectedSteer) return steer.text;
        return {};
    }
    // The prompt box changed while a steer was selected: that is an edit, so the steer is taken
    // back for it (owner, 2026-09-18: "editing a steer takes it back"). The box's text becomes the
    // user's draft, with its @file attachments in it as typed; Enter queues it as a new prompt.
    void takeBackEditedSteer() {
        if (m_selectedSteer.isEmpty() || m_editor->toPlainText() == selectedSteerText()) return;
        takeBackSelectedSteer();
    }
    void takeBackSelectedSteer() {
        const QString requestId = m_selectedSteer;
        if (requestId.isEmpty()) return;
        m_selectedSteer.clear();
        withdrawSteer(requestId, SteerEntry::Edit);
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
        // Steers keep their place at the top whatever the drop said. A queued row dropped above or
        // among them goes as far up as a queued row can: the head of the queue, and for an agent
        // prompt while the agent works, one step further, which is being a steer — the same as
        // Ctrl+↑ on the head of the queue (owner, 2026-09-18: a delivery point is not a position).
        int lastSteerRow = -1;
        for (int row = 0; row < m_queueList->count(); ++row)
            if (m_queueList->item(row)->data(QueueRowDelegate::KindRole).toString() == QStringLiteral("steer")) lastSteerRow = row;
        QList<QueueEntry> ordered;
        quint64 promoted = 0;
        for (int row = 0; row < m_queueList->count(); ++row) {
            const quint64 id = m_queueList->item(row)->data(QueueRowDelegate::EntryIdRole).toULongLong();
            if (id == 0) continue;
            for (const auto &entry : std::as_const(m_entries)) if (entry.id == id) { ordered.append(entry); break; }
            if (row < lastSteerRow && !promoted) promoted = id;
        }
        const bool whole = ordered.size() == m_entries.size();   // not mid-drop, with a row in two places
        bool moved = false;
        if (whole) {
            for (int i = 0; i < ordered.size(); ++i) moved = moved || ordered[i].id != m_entries[i].id;
            if (promoted) {
                for (int i = 0; i < ordered.size(); ++i) if (ordered[i].id == promoted) { ordered.move(i, 0); break; }
            }
            m_entries = ordered;
        }
        keepSelectionOn(selected);   // a dragged row keeps the highlight, at its new place
        if (whole && promoted) {
            if (m_agentBusy && m_entries.first().agent && !m_entries.first().written()) {
                // Dragged to the top while the agent works: that is Ctrl+↑ on the head of the queue.
                QTimer::singleShot(0, this, [this, promoted] { steerQueuedEntry(promoted); });
                hint(QStringLiteral("queue.steer.drag"),
                     relay::ShortcutHints::nextTime(QStringLiteral("Ctrl+↑"), QStringLiteral("sends it at the next tool call")));
            } else {
                status(QStringLiteral("Only an agent prompt, while the agent works, goes above the queue · it is first in the queue"));
            }
        } else if (whole && moved) {
            hint(QStringLiteral("queue.reorder.drag"),
                 relay::ShortcutHints::nextTime(QStringLiteral("↑ then Ctrl+↑↓"), QStringLiteral("moves a queued row")));
        }
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
        // Ctrl+C with nothing selected in the prompt box copies what is highlighted in the
        // terminal — the reasoning is a fold there now, so its selection is the terminal's.
        if (mods == Qt::ControlModifier && k == Qt::Key_C && !m_editor->textCursor().hasSelection() && copySelection())
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
            && !inQueueSelection() && !(m_atList && m_atList->isVisible()) && m_configured) {
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
        // relay::queuenav so they can be tested without a widget. The rows are one list in
        // delivery order: steers waiting for the next tool call, then the queued items.
        {
            const QStringList steers = liveSteers();
            relay::queuenav::State nav;
            nav.selected = selectedQueueRow();
            nav.steers = int(steers.size());
            nav.count = nav.steers + int(m_entries.size());
            nav.headSteerable = m_agentBusy && !m_entries.isEmpty() && m_entries.first().agent && !m_entries.first().written();
            nav.promptEmpty = m_editor->toPlainText().isEmpty();
            nav.cursorLine = m_editor->textCursor().blockNumber();
            nav.lineCount = m_editor->document()->blockCount();
            const bool onSteer = !m_selectedSteer.isEmpty();
            using Action = relay::queuenav::Action;
            switch (relay::queuenav::decide(nav, k, mods)) {
            case Action::Enter:
                // In at the top row: what is delivered first, so a waiting steer before the queue.
                selectQueueRow(0);
                queueEditHint();
                return true;
            case Action::Up:      saveQueueEdit(); selectQueueRow(nav.selected - 1); return true;
            case Action::Down:    saveQueueEdit(); selectQueueRow(nav.selected + 1); return true;
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
                if (onSteer) {
                    // Enter on a steer is "edit it": out of the turn and into the prompt box.
                    takeBackSelectedSteer();
                    return true;
                }
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
                const QString rowId = onSteer ? QStringLiteral("steer:") + m_selectedSteer
                                              : QStringLiteral("entry:%1").arg(m_entries[m_selected].id);
                const int at = nav.selected;
                leaveQueueSelection();
                removeRow(rowId);
                // Stay in the list on the row that moved up into the gap, so several can go in a
                // row. A steer being withdrawn is no longer a row, so the gap is there at once.
                const int rows = int(liveSteers().size() + m_entries.size());
                if (rows > 0) selectQueueRow(std::min(at, rows - 1));
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
            case Action::Steer: {
                // Ctrl+Up on the head of the queue: one further up is the running turn itself.
                saveQueueEdit();
                const QString requestId = steerQueuedEntry(m_entries[m_selected].id);
                if (!requestId.isEmpty()) selectSteer(requestId);   // the highlight follows it
                return true;
            }
            case Action::Unsteer: {
                // Ctrl+Down on a steer: back out of the turn, to the head of the queue once the
                // worker confirms it had not taken it yet.
                const QString requestId = m_selectedSteer;
                leaveQueueSelection();
                withdrawSteer(requestId, SteerEntry::ToQueue);
                return true;
            }
            case Action::None:
                break;
            }
            // The selected steer was just delivered and the list emptied under the user: this Esc
            // was meant for the selection, not for the running turn.
            if (nav.selected < 0 && mods == Qt::NoModifier && k == Qt::Key_Escape && m_selectionDroppedAt.isValid()) {
                const bool recent = m_selectionDroppedAt.elapsed() < 2000;
                m_selectionDroppedAt.invalidate();
                if (recent) {
                    status(QStringLiteral("The agent already had it at its tool call · Esc again stops the turn"));
                    return true;
                }
            }
        }
        // --- subagents UI: Down on the last line (history at the draft) enters the strip under the
        // prompt — the running agents and, since 2026-09-19, the open tasks beside them. The
        // isVisible() test already covers the tasks-only case, where the strip shows with no
        // subagents at all; ←/→ then move between the two columns inside the widget.
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
            // A question card is up: Esc skips that question rather than stopping the turn (owner,
            // 2026-09-19). The turn is blocked on the person reading it, so the key under their
            // hand should get them past the question, not end the work they are being asked about.
            // There is no second Esc that stops either — on the last question Esc sends the answers
            // and the turn carries on — and the card's footer says where Stop is instead.
            if (skipQuestion()) return true;
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
        takeBackEditedSteer();   // typing over a selected steer takes it back out of the turn
        // A handed-over command the user wiped out is gone: what they type next is their own, and
        // so is the mode. The one-shot terminal mode came with the command and leaves with it.
        if ((m_handoffPrefill || m_handoffPrefix) && m_pendingSubmit.isEmpty() && m_editor->toPlainText().trimmed().isEmpty()) {
            m_handoffPrefill = false;
            if (m_handoffPrefix) { m_handoffPrefix = false; clearPrefixMode(true); }
        }
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
        // Typing, clearing the box and the ghost coming and going all change whether the
        // "waiting for N subagents . . ." placeholder is on screen (card #V7QD).
        refreshBackgroundWait();
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

    // "Next time: Ctrl+Shift+S" after the slow path (WARP.md's standing rule). A pane whose tab
    // is attached to nothing has no Switchboard to open, so it teaches no shortcut for one.
    void boardShortcutHint(const QString &id) {
        if (!hasBoard()) return;
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("board.open"));
        if (!keys.isEmpty())
            hint(id, relay::ShortcutHints::nextTime(keys, QStringLiteral("the Switchboard")));
    }

public:
    // ----- the project this pane's tab is attached to (card #JN7X) ----------------------------

    // Protocol 19.11: the tab has attached to a project, or let go of one. The worker gains or
    // loses the card tools and the Switchboard block of its system prompt **without a new Agent**,
    // so the conversation on screen carries straight on — which is the whole reason `set_board`
    // exists rather than another `configure`. An empty block detaches (`"board": null`).
    //
    // Before the first `configure` there is nothing to re-point: the block travels on that
    // configure instead (withSessionFields).
    void setBoard(const QJsonObject &board) {
        if (!m_configured) return;
        send({{QStringLiteral("type"), QStringLiteral("set_board")},
              {QStringLiteral("board"), board.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(board)}});
    }

    // ----- "Initialize a project and create a Switchboard here?" (protocol 19.12 and 19.13) ----
    //
    // The one question that ever creates `<project>/switchboard/`. Five triggers raise it and
    // nothing else does (the table is in src/ProjectInit.h): the first prompt sent to the agent in
    // a git repository with no board, the agent's first card, opening the Switchboard, `/card` and
    // `/init`. A `cd`, launching Relay, hovering, `#` typed in the composer and opening a file are
    // silent, for ever — the owner's rule is that nothing appears on disk unasked.
    //
    // **It never blocks what raised it.** The prompt has already gone to the agent when the
    // question appears beside it, and a yes lands as a `set_board`, which the worker defers to the
    // end of a running turn — so the card tools arrive for the *next* turn, in the same
    // conversation (19.11).
    //
    // The order on a yes is fixed by the worker and not by taste: an unattached pane has no board
    // object at all, so a `board_init` sent there answers "this project has no Switchboard". The
    // tab is attached first (`set_board {project, state: "uninitialized"}`) and `board_init` goes
    // out when that lands, which is also exactly what makes a mid-turn yes take effect at turn end.
    //
    // Raise the question for `trigger`; `cardText` is a `/card` being held, which lands as the
    // first card on a yes and is never lost. True when a question is on its way.
    bool askProjectInit(relay::projectinit::Trigger trigger, const QString &cardText = QString(),
                        QString *why = nullptr) {
        if (!onProjectInitSituation) return false;
        relay::projectinit::Situation situation = onProjectInitSituation();
        situation.cwd = m_cwd;
        situation.asking = m_initStage != InitStage::Idle;
        const relay::projectinit::Decision decision = relay::projectinit::decide(trigger, situation);
        if (decision.outcome == relay::projectinit::Outcome::Say) {
            if (why) *why = decision.message; else status(decision.message);
            return false;
        }
        if (decision.outcome == relay::projectinit::Outcome::Attach) {
            // Nothing to create: the project already has a board, so this is an ordinary attach.
            if (onProjectAttach) onProjectAttach(decision.project, decision.reason);
            if (!decision.message.isEmpty()) { if (why) *why = decision.message; else status(decision.message); }
            return false;
        }
        if (decision.outcome != relay::projectinit::Outcome::Ask) return false;
        if (decision.clearsDecline && onProjectInitEvent)
            onProjectInitEvent(decision.project, QStringLiteral("reconsider"));
        beginProjectInit(trigger, decision.project, QString(), cardText);
        return true;
    }

private:
    // Where the one flow has got to. Only one runs at a time in a pane; `Asking` is the window the
    // `situation.asking` guard closes, so a second trigger cannot replace the findings under the
    // user's cursor.
    enum class InitStage { Idle, Probing, Asking, Attaching, Creating, Importing };

    // Ask the worker what is in the project, then draw the answer. `project_probe` opens no socket,
    // runs no subprocess and writes no file — the worker guarantees it (19.13) — so the question
    // can say what it found before anything at all has happened.
    void beginProjectInit(relay::projectinit::Trigger trigger, const QString &project,
                          const QString &requestId, const QString &cardText) {
        if (project.isEmpty()) return;
        m_initStage = InitStage::Probing;
        m_initTrigger = trigger;
        m_initProject = project;
        m_initRequestId = requestId;
        m_initCardText = cardText;
        m_initKinds.clear();
        m_initProbeId = QStringLiteral("pi-%1-%2").arg(m_token.left(6)).arg(++m_initSeq);
        // The window notes it at once, not on the answer: one question per project per session, so
        // a second pane standing in the same repository does not raise a second one behind it.
        if (onProjectInitEvent) onProjectInitEvent(project, QStringLiteral("asked"));
        relay::log::info(QStringLiteral("project init asked pane=%1 trigger=%2 project=%3")
                             .arg(m_token.left(8), relay::projectinit::triggerName(trigger), project));
        send({{QStringLiteral("type"), QStringLiteral("project_probe")},
              {QStringLiteral("id"), m_initProbeId},
              {QStringLiteral("project"), project}});
        // The probe is a handful of file reads, but a project on a slow mount must not swallow the
        // question: with no answer in five seconds it is asked with no findings at all.
        QPointer<Pane> guard(this);
        const QString waitingFor = m_initProbeId;
        QTimer::singleShot(5000, this, [guard, waitingFor] {
            if (!guard || guard->m_initStage != InitStage::Probing || guard->m_initProbeId != waitingFor) return;
            guard->showProjectInit(QJsonObject{{QStringLiteral("project"), guard->m_initProject}});
        });
    }

    // The worker asked: the agent called `board_create_card` in a project with no Switchboard, and
    // its turn thread is parked on the answer (19.12). The pane owns the dialog and always answers,
    // so this never simply ignores the request: a trigger the rules refuse is answered `false`.
    void handleBoardInitRequest(const QJsonObject &event) {
        const QString id = event.value(QStringLiteral("id")).toString();
        const QString project = event.value(QStringLiteral("project")).toString();
        if (id.isEmpty() || project.isEmpty()) return;
        relay::projectinit::Situation situation =
            onProjectInitSituation ? onProjectInitSituation() : relay::projectinit::Situation{};
        situation.project = project;   // the worker names it; the tab is already attached to it
        situation.cwd = m_cwd;
        situation.hasBoard = false;    // the worker only asks when there is none
        situation.asking = m_initStage != InitStage::Idle;
        const relay::projectinit::Decision decision =
            relay::projectinit::decide(relay::projectinit::Trigger::AgentCard, situation);
        if (!decision.asks()) {
            send({{QStringLiteral("type"), QStringLiteral("board_init_answer")},
                  {QStringLiteral("id"), id}, {QStringLiteral("accept"), false}});
            return;
        }
        beginProjectInit(relay::projectinit::Trigger::AgentCard, project, id,
                         event.value(QStringLiteral("title")).toString());
    }

    // The block is a row of the pane's own column, directly under the terminal, beside the thinking
    // panel and the queue strip — never a dialog and never an overlay (owner's rule, 2026-09-18).
    relay::ProjectInitBlock *projectInitBlock() {
        if (!m_initBlock) {
            m_initBlock = new relay::ProjectInitBlock(this);
            m_initBlock->hide();
            QPointer<Pane> guard(this);
            m_initBlock->answered = [guard](relay::ProjectInitBlock::Answer answer, const QStringList &kinds) {
                if (guard) guard->answerProjectInit(answer, kinds);
            };
            if (auto *column = qobject_cast<QVBoxLayout *>(layout()))
                column->insertWidget(column->indexOf(m_terminalHost) + 1, m_initBlock);
        }
        return m_initBlock;
    }

    void showProjectInit(const QJsonObject &probeResult) {
        QJsonObject result = probeResult;
        // The probe answers about the project it was given; a result with no `project` (the
        // timeout's empty stand-in) still has to name the folder the yes would create.
        if (result.value(QStringLiteral("project")).toString().isEmpty())
            result.insert(QStringLiteral("project"), m_initProject);
        m_initStage = InitStage::Asking;
        projectInitBlock()->ask(relay::projectinit::questionFrom(result));
    }

    void answerProjectInit(relay::ProjectInitBlock::Answer answer, const QStringList &kinds) {
        const QString project = m_initProject;
        const relay::projectinit::Trigger trigger = m_initTrigger;
        const QString requestId = m_initRequestId;
        if (answer != relay::ProjectInitBlock::Answer::Yes) {
            // A no and a "not now" both release the agent's parked tool call, because the pane owns
            // the dialog and the turn thread is waiting on it. They differ only in what is
            // remembered: a no for ever (the registry), a not-now for this Relay session.
            if (!requestId.isEmpty())
                send({{QStringLiteral("type"), QStringLiteral("board_init_answer")},
                      {QStringLiteral("id"), requestId}, {QStringLiteral("accept"), false}});
            const bool no = answer == relay::ProjectInitBlock::Answer::No;
            if (onProjectInitEvent)
                onProjectInitEvent(project, no ? QStringLiteral("no") : QStringLiteral("not-now"));
            ensureLineStart();
            printInline((no ? relay::projectinit::declinedLine(project)
                            : relay::projectinit::notNowLine(project)) + QLatin1Char('\n'), Ink::Note);
            closeInline();
            resetProjectInit();
            focusInput();
            return;
        }
        m_initKinds = kinds;
        if (onProjectInitEvent) onProjectInitEvent(project, QStringLiteral("yes"));
        if (!requestId.isEmpty()) {
            // The worker asked, so the worker creates: the card the agent was writing is completed
            // on the way through and is never lost (19.12).
            m_initStage = InitStage::Creating;
            send({{QStringLiteral("type"), QStringLiteral("board_init_answer")},
                  {QStringLiteral("id"), requestId}, {QStringLiteral("accept"), true}});
        } else {
            // Attach first (set_board), then `board_init` once that has landed: see askProjectInit.
            m_initStage = InitStage::Attaching;
            if (onProjectAttach) onProjectAttach(project, relay::projectinit::triggerName(trigger));
        }
        focusInput();
    }

    // Is this `board_state` about the project the question is about? The worker echoes the
    // `project` it was pointed with; an older board block may carry only the folder.
    bool projectInitOwns(const QJsonObject &board) const {
        if (m_initProject.isEmpty()) return false;
        if (board.value(QStringLiteral("project")).toString() == m_initProject) return true;
        const QString root = board.value(QStringLiteral("root")).toString();
        return !root.isEmpty() && root.startsWith(m_initProject + QLatin1Char('/'));
    }

    // Each step of a yes, driven by the `board_state` that says `applies: "now"` — the one in force.
    void projectInitBoardState(const QJsonObject &board) {
        if (m_initStage != InitStage::Attaching && m_initStage != InitStage::Creating) return;
        if (!projectInitOwns(board)) return;
        const QString state = board.value(QStringLiteral("state")).toString();
        if (m_initStage == InitStage::Attaching && state == QStringLiteral("uninitialized")) {
            m_initStage = InitStage::Creating;
            send({{QStringLiteral("type"), QStringLiteral("board_init")},
                  {QStringLiteral("project"), m_initProject}});
            return;
        }
        if (m_initStage != InitStage::Creating || state != QStringLiteral("ready")) return;
        // The board is on disk and the pane's tools are the full set. The card the user typed goes
        // in first, then whatever they ticked is imported.
        if (!m_initCardText.isEmpty()) {
            send({{QStringLiteral("type"), QStringLiteral("board_create")},
                  {QStringLiteral("tab"), QStringLiteral("features")},
                  {QStringLiteral("status"), QStringLiteral("inbox")},
                  {QStringLiteral("text"), m_initCardText}});
            m_cardIndexAsked = false;
        }
        if (m_initKinds.isEmpty()) { finishProjectInit(-1); return; }
        m_initStage = InitStage::Importing;
        // Propose first, apply second: nothing from the proposals goes back on the wire but the
        // keys, and the worker re-derives every card body from the project itself (19.13).
        send({{QStringLiteral("type"), QStringLiteral("board_import_propose")},
              {QStringLiteral("id"), m_initProbeId},
              {QStringLiteral("project"), m_initProject},
              {QStringLiteral("kinds"), QJsonArray::fromStringList(m_initKinds)}});
    }

    void projectInitProposals(const QJsonObject &event) {
        if (m_initStage != InitStage::Importing) return;
        QJsonArray keys;
        for (const QJsonValue &value : event.value(QStringLiteral("proposals")).toArray()) {
            const QString key = value.toObject().value(QStringLiteral("source_key")).toString();
            if (!key.isEmpty()) keys.append(key);
        }
        if (keys.isEmpty()) { finishProjectInit(0); return; }
        send({{QStringLiteral("type"), QStringLiteral("board_import_apply")},
              {QStringLiteral("id"), m_initProbeId},
              {QStringLiteral("project"), m_initProject},
              {QStringLiteral("keys"), keys}});
    }

    // One quiet line, and the Switchboard itself when that is what the user was reaching for.
    void finishProjectInit(int imported) {
        const QString project = m_initProject;
        const bool openBoard = m_initTrigger == relay::projectinit::Trigger::Switchboard;
        ensureLineStart();
        printInline(relay::projectinit::createdLine(project, imported) + QLatin1Char('\n'), Ink::Note);
        closeInline();
        status(relay::projectinit::createdLine(project, imported));
        resetProjectInit();
        if (openBoard && onOpenBoard) onOpenBoard();
    }

    void resetProjectInit() {
        m_initStage = InitStage::Idle;
        m_initProject.clear();
        m_initRequestId.clear();
        m_initCardText.clear();
        m_initProbeId.clear();
        m_initKinds.clear();
        if (m_initBlock) m_initBlock->hide();
    }

    InitStage m_initStage = InitStage::Idle;
    relay::projectinit::Trigger m_initTrigger = relay::projectinit::Trigger::AgentWork;
    QString m_initProject, m_initRequestId, m_initCardText, m_initProbeId;
    QStringList m_initKinds;
    int m_initSeq = 0;
    relay::ProjectInitBlock *m_initBlock = nullptr;


    // Whether this pane's tab is attached to a project, without attaching anything or touching
    // the filesystem. The quiet default is false.
    bool hasBoard() const { return onBoardProject && !onBoardProject(QString(), nullptr).isEmpty(); }

    // The explicit project actions of the owner's model: `/card` and the `#` card picker attach
    // the tab to the pane's candidate project, when that project has a board. `why` is the one
    // line to show when it has not; nothing is created either way.
    bool attachForBoard(const char *reason, QString *why = nullptr) {
        if (!onBoardProject) return false;
        return !onBoardProject(QString::fromLatin1(reason), why).isEmpty();
    }

    // Switchboard events a *terminal* pane cares about: the card index behind the `#` picker,
    // and one inline line per agent write (protocol 17.2 and 17.5).
    bool handleBoardEvent(const QString &type, const QJsonObject &event) {
        // ----- the init question's own round trips (protocol 19.12, 19.13) --------------------
        if (type == QStringLiteral("board_init_request")) { handleBoardInitRequest(event); return true; }
        if (type == QStringLiteral("project_probe_result")) {
            if (m_initStage == InitStage::Probing && event.value(QStringLiteral("id")).toString() == m_initProbeId)
                showProjectInit(event);
            return true;
        }
        if (type == QStringLiteral("board_import_proposals")) { projectInitProposals(event); return true; }
        if (type == QStringLiteral("board_imported")) {
            if (m_initStage == InitStage::Importing)
                finishProjectInit(event.value(QStringLiteral("cards")).toArray().size());
            return true;
        }
        // An error while the flow is in flight must not leave it waiting for an event that will
        // never come. A probe that failed still gets its question — with no findings, because the
        // folder a yes creates is the same either way — and any later step gives up quietly. The
        // event is not consumed: the pane's ordinary error line still says what went wrong.
        if (type == QStringLiteral("error") && m_initStage != InitStage::Idle && !m_initProbeId.isEmpty()
            && event.value(QStringLiteral("id")).toString() == m_initProbeId) {
            if (m_initStage == InitStage::Probing) showProjectInit(QJsonObject{});
            else resetProjectInit();
            return false;
        }
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
        // The worker has been re-pointed, or told to let go (protocol 19.11). The one that says
        // `applies: "now"` is the one in force; a deferred one is only a promise about the end of
        // the running turn, and the same event comes again when it lands. An unsolicited one (no
        // `id`) arrives when a board is created under the pane.
        if (type == QStringLiteral("board_state")) {
            if (event.value(QStringLiteral("applies")).toString() != QStringLiteral("now")) return true;
            const QJsonObject board = event.value(QStringLiteral("board")).toObject();
            const QString root = board.value(QStringLiteral("root")).toString();
            relay::log::info(QStringLiteral("board_state pane=%1 board=%2 state=%3")
                                 .arg(m_token.left(8), root.isEmpty() ? QStringLiteral("none") : root,
                                      board.value(QStringLiteral("state")).toString()));
            // A yes walks forward on this event: the attach has landed (so `board_init` may go), or
            // the board is on disk (so the held card and the ticked imports may go). Mid-turn the
            // worker sends it at turn end, which is exactly when the yes is meant to take effect.
            projectInitBoardState(board);
            // Another project's cards are not this one's: drop the picker index and the work
            // chip's card list, and ask again the next time something needs them.
            m_cardIndex.reset({});
            m_cardIndexAsked = false;
            m_workCards.clear();
            updateWorkChip();
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
        // `#` in the composer is an explicit project action, so it may attach the tab — but only
        // to a candidate project that already has a board. A pane in ~/Downloads gets no picker
        // and nothing is created anywhere (#JN7X).
        requestCardIndex(relay::projects::kReasonCardPicker);
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
    //
    // **An unattached pane asks for nothing.** Its worker has no board (`attach: false`), so a
    // `board_open` would only come back an error, and asking would be Relay looking for a project
    // behind the user's back. `reason` non-null is the one exception: the user typed `#`, which
    // is an explicit project action and attaches the tab to a candidate that has a board.
    void requestCardIndex(const char *reason = nullptr) {
        if (m_cardIndexAsked || !m_configured) return;
        if (reason ? !attachForBoard(reason) : !hasBoard()) return;
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
        // A login lives on the alternate screen whenever the host runs mosh, tmux or screen, so it
        // says nothing there about a full-screen program: what the cursor's row holds decides
        // (updateLoginPrompt), and the login keeps taking lines (card #S5SH).
        if (!primary && m_login.active) { updateTakeControl(); refreshProgramHint(); return; }
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

    // ----- ssh and mosh logins (card #S5SH, docs/SSH-AND-MOSH.md) -------------------------------
    // While ssh or mosh owns the terminal the pane keeps a picture of the other end: which host
    // (`ssh -G` over the program's own arguments), the user's control socket for it, the remote
    // folder (OSC 7) and whether the remote shell is sitting at its prompt. At that prompt the
    // prompt box types into the login, the agent's reply prints into the terminal, and the agent
    // may run commands on the host over the socket.

    // Where Relay's `ssh` and `mosh` wrappers keep their control sockets (shell/integration.bash).
    // Short on purpose: a socket path is limited to 107 bytes and %C adds 40.
    static QString sshSocketDir() {
        const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (!runtime.isEmpty()) return runtime + QStringLiteral("/relay-ssh");
        // No runtime directory: the fallback lives in a shared temporary directory, so its name
        // carries the user id and its owner and mode are checked before anything is put in it.
        return QDir::tempPath() + QStringLiteral("/relay-ssh-%1").arg(::getuid());
    }

    // The directory holds the control sockets of the user's own logins: another account owning it,
    // or a mode that lets anyone in, means no sharing rather than sharing through someone's
    // directory. Returns false without creating anything when it cannot be made safe.
    static bool sshSocketDirReady(QString *why) {
        const QString path = sshSocketDir();
        const QFileInfo before(path);
        if (before.exists() && !before.isDir()) { *why = QStringLiteral("%1 is not a directory").arg(path); return false; }
        if (!QDir().mkpath(path)) { *why = QStringLiteral("%1 could not be created").arg(path); return false; }
        if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
            *why = QStringLiteral("%1 could not be made private (owned by someone else?)").arg(path);
            return false;
        }
        struct stat info {};
        if (::stat(path.toLocal8Bit().constData(), &info) != 0) { *why = QStringLiteral("%1 could not be read").arg(path); return false; }
        if (info.st_uid != ::getuid()) { *why = QStringLiteral("%1 belongs to another user").arg(path); return false; }
        if (info.st_mode & (S_IRWXG | S_IRWXO)) { *why = QStringLiteral("%1 is open to other users").arg(path); return false; }
        return true;
    }

    // The foreground program's arguments, unjoined (foregroundCommandLine() joins them with spaces).
    QStringList foregroundArgv() const {
        const int shell = shellPid();
        long group = shell > 0 ? foregroundGroup(shell) : -1;
        if (group <= 0 || group == shell) group = foregroundPid();
        if (group <= 0 || group == shell) return {};
        QFile file(QStringLiteral("/proc/%1/cmdline").arg(group));
        if (!file.open(QIODevice::ReadOnly)) return {};
        QByteArray raw = file.read(16384);
        if (raw.endsWith('\0')) raw.chop(1);
        QStringList argv;
        for (const QByteArray &part : raw.split('\0')) argv << QString::fromLocal8Bit(part);
        return argv;
    }

    // The prompt box types into the login instead of routing to the local shell.
    // Typed ahead when the host is merely busy; on the alternate screen (a remote tmux, mosh, or a
    // full-screen program) only at a prompt, so a line never lands in vim.
    bool loginTakesLines() const {
        return m_login.active && !m_native && !m_secretMode && (m_login.atPrompt || !m_altScreen);
    }
    // The remote shell is idle at its prompt: agent output may be printed there.
    bool loginAtPrompt() const { return loginTakesLines() && m_login.atPrompt; }
    QString loginHost() const {
        if (!m_login.where.host.isEmpty()) return m_login.where.host;
        return relay::panestatus::remoteHost(foregroundCommandLine());
    }

    void beginLogin(const QString &program) {
        m_login = RemoteLogin();
        m_login.active = true;
        m_login.program = program;
        if (m_backend) m_backend->setOutputCallbackEnabled(true);   // the prompt row, for printing it back
        m_login.group = foregroundPid();
        const QStringList argv = foregroundArgv();
        const QStringList args = relay::remote::dumpArguments(argv, sshSocketDir());
        const QString destination = relay::remote::destination(argv);
        relay::log::info(QStringLiteral("login_begin pane=%1 program=%2 resolvable=%3")
                             .arg(paneLogId(), program).arg(args.isEmpty() ? 0 : 1));
        if (args.isEmpty()) return;
        // `ssh -G` reads the configuration and prints the result; it never touches the network.
        auto *dump = new QProcess(this);
        const qint64 group = m_login.group;
        connect(dump, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, dump, group, destination](int code, QProcess::ExitStatus) {
            dump->deleteLater();
            if (!m_login.active || m_login.group != group || code != 0) return;
            m_login.where = relay::remote::parseDump(dump->readAllStandardOutput(), destination);
            m_login.resolved = m_login.where.ok;
            relay::log::info(QStringLiteral("login_resolved pane=%1 shared=%2")
                                 .arg(paneLogId()).arg(loginReachable() ? 1 : 0));
            // Sharing is what lets the agent work on the host. When Relay knows why it is off,
            // the pane says so once per login instead of leaving the agent to report a dead end.
            if (!loginReachable() && !m_sshShareProblem.isEmpty() && !m_login.warnedShare) {
                m_login.warnedShare = true;
                status(QStringLiteral("The agent cannot reach %1: %2").arg(loginHost(), m_sshShareProblem));
            }
            changed();
        });
        connect(dump, &QProcess::errorOccurred, dump, &QObject::deleteLater);
        dump->start(QStringLiteral("ssh"), args);
        QTimer::singleShot(3000, dump, [dump] { if (dump->state() != QProcess::NotRunning) dump->kill(); });
    }

    void endLogin() {
        if (!m_login.active) return;
        relay::log::info(QStringLiteral("login_end pane=%1").arg(paneLogId()));
        forgetLoginFiles();
        if (m_login.offered) hideBanner();
        if (m_backend && !m_capturing) m_backend->setOutputCallbackEnabled(false);
        m_login = RemoteLogin();
        changed();
    }

    // The user's own authenticated connection to the host answers at its control socket.
    bool loginReachable() const {
        if (!m_login.resolved || m_login.where.controlPath.isEmpty()) return false;
        return QFileInfo(m_login.where.controlPath).exists();
    }

    // What the agent is told about the login (ask context `remote_session`, protocol section 9).
    QJsonObject loginContext() const {
        if (!m_login.active) return {};
        const bool reachable = loginReachable();
        QJsonObject out{{"program", m_login.program}, {"host", loginHost()},
                        {"reachable", reachable}, {"shell_integration", m_login.integration},
                        {"at_prompt", m_login.atPrompt}};
        if (m_login.resolved) {
            out.insert(QStringLiteral("hostname"), m_login.where.hostname);
            out.insert(QStringLiteral("user"), m_login.where.user);
            out.insert(QStringLiteral("port"), m_login.where.port);
        }
        if (reachable) out.insert(QStringLiteral("control_path"), m_login.where.controlPath);
        if (!m_login.cwd.isEmpty()) out.insert(QStringLiteral("cwd"), m_login.cwd);
        return out;
    }

    // Is the remote shell at its prompt? With the remote integration the OSC 133 marks say so;
    // without it (mosh, a declined host, a shell other than bash or zsh) the screen does: a
    // shell prompt on the cursor row, the cursor at its end, twice in a row (~0.5 s).
    void updateLoginPrompt() {
        if (!m_login.active) return;
        const bool before = m_login.atPrompt;
        if (m_native || !m_backend) {
            m_login.atPrompt = false; m_login.promptTicks = 0;
        } else if (m_login.integration && !m_altScreen) {
            // The marks come from the shell Relay enhanced. On the alternate screen something else
            // is drawing (a remote tmux, mosh), its own shell's marks never reach here, and the
            // last mark is the "command started" of whatever opened it: the screen decides instead.
            m_login.atPrompt = m_lastPromptMark == 'B' || m_lastPromptMark == 'A';
        } else if (m_inlineOpen && m_login.atPrompt) {
            // Relay's own output is on the cursor row now, not the prompt: it stays a prompt until
            // the block closes or the user sends the login a line (typeIntoLogin).
        } else {
            // The cursor's own row, not the last row of the screen: inside a remote tmux the last
            // row is its status bar. The cursor must sit at the end of what that row holds, and
            // that text must read like a shell's prompt.
            const QPoint cursor = m_backend->cursorPosition();
            const QString row = cursor.y() >= 0 ? m_backend->screenText().split('\n').value(cursor.y()) : QString();
            const QString typed = row.left(std::max(0, cursor.x()));
            bool prompt = !typed.trimmed().isEmpty() && cursor.x() >= row.trimmed().size()
                          && relay::screen::isShellPrompt(typed) && !m_screenPrompt.actionable();
            m_login.promptTicks = prompt ? m_login.promptTicks + 1 : 0;
            m_login.atPrompt = m_login.promptTicks >= 2;
        }
        if (m_login.atPrompt && !before) {
            if (!m_login.greeted) {
                m_login.greeted = true;
                toast(QStringLiteral("Logged in to %1 · the prompt box types there · %2 for keys")
                          .arg(loginHost(), Keymap::instance().shortcutText(QStringLiteral("control.human"))));
            }
            maybeEnhanceLogin();
            flushInline();
            refreshProgramHint();
        }
        if (before != m_login.atPrompt) updateTakeControl();
    }

    // Load shell/remote-integration.sh into the remote shell once per login, per Options ›
    // Terminal › SSH sessions: automatically, after asking, or never. mosh drops the escape
    // sequences the script sends, so only ssh is enhanced.
    void maybeEnhanceLogin() {
        if (m_login.program != QStringLiteral("ssh") || m_login.bootstrapped || m_login.integration) return;
        // Something on the host is asking a question (zsh's first-run menu on a host with no
        // ~/.zshrc, a pager, a wizard): its prompt is not a shell's, and a line typed into it is
        // an answer, not a command. Wait; the next real prompt enhances the login.
        if (m_screenPrompt.actionable()) return;
        QSettings settings;
        const QString mode = settings.value(QStringLiteral("ssh/enhance"), QStringLiteral("auto")).toString();
        const QString host = loginHost();
        const auto listed = [&](const char *key) {
            return settings.value(QString::fromLatin1(key)).toStringList().contains(host, Qt::CaseInsensitive);
        };
        if (mode == QStringLiteral("off") || host.isEmpty() || listed("ssh/hosts_never")) return;
        if (mode == QStringLiteral("ask") && !listed("ssh/hosts_always")) {
            if (m_login.offered) return;
            m_login.offered = true;
            showBanner(QStringLiteral("Enhance this ssh session on %1? Prompt marks, the remote folder, the agent's replies "
                                      "at the prompt · nothing is installed on the host").arg(host),
                       QStringLiteral("Enhance"), [this] { hideBanner(); typeLoginBootstrap(); });
            return;
        }
        typeLoginBootstrap();
    }

    void typeLoginBootstrap() {
        if (!loginAtPrompt() || m_login.bootstrapped) return;
        QFile file(m_data + QStringLiteral("/shell/remote-integration.sh"));
        if (!file.open(QIODevice::ReadOnly)) return;
        const QPoint cursor = m_backend->cursorPosition();
        const QString line = relay::remote::bootstrapLine(file.readAll(), std::max(0, cursor.x()), m_backend->columns());
        m_login.bootstrapped = true;
        m_login.atPrompt = false; m_login.promptTicks = 0;   // the line runs; the next prompt is the enhanced one
        sendShellInput(line + '\r');
        relay::log::info(QStringLiteral("login_enhance pane=%1 bytes=%2").arg(paneLogId()).arg(line.size()));
    }

    // A line from the prompt box, typed into the login. Several lines go as one bracketed paste.
    void typeIntoLogin(const QString &text) {
        m_editor->remember(text);
        m_editor->clear();
        hideAtPopup(); clearAiGhost();
        if (m_inlineOpen) { ensureLineStart(); closeInline(); }
        const bool idle = m_login.atPrompt;
        if (text.contains('\n')) m_backend->sendText(text, true);
        else sendShellInput(text);
        sendShellInput(QStringLiteral("\r"));
        m_login.atPrompt = false; m_login.promptTicks = 0;
        if (!idle) toast(QStringLiteral("Typed into %1 · it was busy, so the line waits for it").arg(loginHost()));
        QTimer::singleShot(0, this, [this] { rebuildQueueStrip(); });
    }

    // ----- files on the host (#S5SH, docs/SSH-AND-MOSH.md section 9) -------------------------
    //
    // A path printed by the host names one of the host's files. Clicking it opens that file, over
    // the connection the user already has, in the same preview pane a local file opens in — and
    // the pane can edit it and save it back (owner, 2026-09-18: "editing allowed so it's equal to
    // local text editing"). The pane's part is three things: telling the file layer which socket
    // this login uses, answering the engine's link probe from the host's filesystem rather than
    // this one's, and turning a clicked path into `ssh://host/path`.

    // Keep relay::remote's record of this login in step with m_login, so a preview pane can reach
    // the host without holding a pointer into this pane — and so that a save after the login ends
    // fails with "the connection is gone" instead of writing somewhere unexpected.
    void announceLoginFiles() {
        if (!m_login.active || !m_login.resolved) return;
        const QString host = loginHost(), socket = m_login.where.controlPath;
        if (host.isEmpty() || socket.isEmpty()) return;
        if (host == m_remoteFilesHost && socket == m_remoteFilesSocket) return;
        m_remoteFilesHost = host;
        m_remoteFilesSocket = socket;
        relay::remote::announceLogin(host, socket);
        m_remoteProbe.setHost(host, socket);
        m_remoteProbe.onAnswers = [this] { if (m_backend) m_backend->linkProbeAnswered(); };
    }

    // The login is over: the socket is dead, the host's answers are worthless, and a preview pane
    // holding one of its files must not be told the connection is still there.
    void forgetLoginFiles() {
        m_remoteProbe.setHost(QString());
        if (m_remoteFilesHost.isEmpty()) return;
        relay::remote::forgetLogin(m_remoteFilesHost);
        m_remoteFilesHost.clear();
        m_remoteFilesSocket.clear();
    }

    // Does this path exist, and is it a folder? The engine asks for every path-shaped span it
    // paints, from a mouse-move, so nothing here may block: outside a login it is this machine's
    // filesystem, and inside one it is whatever the host has already answered. A path the host
    // has not been asked about yet reads as "nothing there" and is queued; the answer arrives a
    // moment later and the view reads the output again (PathProbe::onAnswers above).
    int remoteLinkProbe(const QString &path) {
        if (!m_login.active || !loginReachable()) {
            static const relay::links::Probe local = relay::links::systemProbe();
            return int(local(path));
        }
        announceLoginFiles();
        const relay::remote::Entry entry = m_remoteProbe.lookup(path);
        // "Not answered yet" has to read as "no link": anything else would underline every
        // path-shaped word in the output until the host disagreed.
        return int(entry == relay::remote::Entry::Unknown ? relay::links::Entry::Missing
                                                          : relay::links::Entry(int(entry)));
    }

    // A clicked path, while a login owns the terminal. The engine only offered it as a link
    // because the host said it exists (remoteLinkProbe), so it is the host's file: it opens in a
    // preview pane as `ssh://host/path`, titled with the host and fetched over this login.
    void openRemoteOutputPath(const QString &target, int line) {
        const QString host = loginHost();
        if (host.isEmpty() || !target.startsWith(QLatin1Char('/'))) {
            toast(QStringLiteral("That path is on the host, not this machine."));
            return;
        }
        if (!loginReachable()) {
            // No shared connection: an unwrapped ssh, or `ssh/enhance` off. Nothing can be read
            // from the host without asking for a second login, which Relay does not do.
            toast(QStringLiteral("%1 is on %2 · Relay cannot open it: this login is not sharing its connection")
                      .arg(target, host));
            return;
        }
        announceLoginFiles();
        // A folder opens the explorer pane on the host's folder, a file the preview pane. Which
        // it is, the host already said when it made the path a link: the URL of a folder ends in
        // `/` and RelayWindow::openPath reads that rather than asking this machine.
        const bool folder = m_remoteProbe.lookup(target) == relay::remote::Entry::Directory;
        const QString url = folder ? relay::remote::folderUrl(host, target) : relay::remote::fileUrl(host, target);
        if (url.isEmpty() || !onOpenPath) return;
        relay::log::info(QStringLiteral("remote_%1_open pane=%2 host=%3")
                             .arg(folder ? QStringLiteral("folder") : QStringLiteral("file"), paneLogId(), host));
        onOpenPath(url, line > 0 ? line : 0);
    }

    relay::remote::PathProbe m_remoteProbe;   // which of the host's paths exist (#S5SH)
    QString m_remoteFilesHost, m_remoteFilesSocket;

    // Remote sessions never switch screens, so a short list stands in for detection.
    static bool remoteSessionProgram(const QString &name) {
        static const QSet<QString> names{QStringLiteral("ssh"), QStringLiteral("mosh"), QStringLiteral("mosh-client"), QStringLiteral("telnet")};
        return names.contains(name);
    }

    // Claude Code or Codex in the foreground (issue GT7X, protocol 26): the id
    // backend/relay_core/guest.py gives the same argv, or empty. One rule in two languages —
    // change `guest.classify_command` and this together; tests/test_guest.py reads this table
    // out of this file and fails when the two drift apart.
    //
    // Everything the rule knows about a guest lives in this one table: the ids, the display
    // names, the binary names and the npm packages. guestIdFor, guestIdForPackage and
    // guestDisplayName all read it, so a third guest is one row and nothing else.
    struct GuestSpec {
        const char *id;
        const char *name;
        QStringList binaries;
        QStringList packages;   // as published on npm, scope and all
    };
    static const QList<GuestSpec> &guestSpecs() {
        static const QList<GuestSpec> specs{
            {"claude", "Claude Code", {QStringLiteral("claude"), QStringLiteral("claude-code")},
             {QStringLiteral("@anthropic-ai/claude-code")}},
            {"codex", "Codex", {QStringLiteral("codex"), QStringLiteral("codex-cli")},
             {QStringLiteral("@openai/codex")}},
        };
        return specs;
    }
    static QString guestIdFor(const QString &name) {
        for (const GuestSpec &candidate : guestSpecs())
            if (candidate.binaries.contains(name)) return QString::fromLatin1(candidate.id);
        return {};
    }
    static QString guestIdForPackage(const QString &package) {
        for (const GuestSpec &candidate : guestSpecs())
            if (candidate.packages.contains(package)) return QString::fromLatin1(candidate.id);
        return {};
    }
    // The name a person reads. An id the table does not know — including the empty one, which is
    // what a guest running inside tmux gives, since the pane cannot see it — is "the guest agent"
    // rather than a guess at Claude Code (review of 51587e3).
    static QString guestDisplayName(const QString &guest) {
        for (const GuestSpec &candidate : guestSpecs())
            if (guest == QLatin1String(candidate.id)) return QString::fromLatin1(candidate.name);
        return QStringLiteral("The guest agent");
    }

    // The guest `argv` runs, or empty. The CLI may be the native binary or a script a launcher
    // runs (`node …/bin/codex` for the shebang install, `npx -y claude`, `node …/claude-code/cli.js`
    // for the npm shim), so when the first token is a known launcher the first non-flag token
    // after it decides — by its own basename, or by the npm package it lies in. *Any* path
    // component used to count, which made `node /home/codex/server.js` a codex session.
    static QString guestProgram(const QStringList &argv) {
        if (argv.isEmpty()) return {};
        const QString first = QFileInfo(argv.constFirst()).fileName();
        if (const QString id = guestIdFor(first); !id.isEmpty()) return id;
        static const QSet<QString> launchers{QStringLiteral("node"), QStringLiteral("nodejs"), QStringLiteral("bun"),
                                             QStringLiteral("bunx"), QStringLiteral("deno"), QStringLiteral("npx")};
        if (!launchers.contains(first)) return {};
        for (int i = 1; i < argv.size(); ++i) {
            const QString &token = argv.at(i);
            if (token.startsWith(QLatin1Char('-'))) continue;   // the launcher's own flags: npx -y claude
            // Only the launcher's target decides; what follows are the guest's own arguments.
            const QString id = guestIdFor(guestScriptLeaf(token));
            return id.isEmpty() ? guestPackaged(token) : id;
        }
        return {};
    }

    // A launcher target's own name: its basename with a script extension stripped.
    static QString guestScriptLeaf(const QString &token) {
        static const QStringList extensions{QStringLiteral(".js"), QStringLiteral(".mjs"),
                                            QStringLiteral(".cjs"), QStringLiteral(".ts")};
        QString leaf = QFileInfo(token).fileName();
        for (const QString &extension : extensions)
            if (leaf.endsWith(extension)) { leaf.chop(extension.size()); break; }
        return leaf;
    }

    // The guest whose npm package this path lies in, or empty. Two shapes count and nothing
    // else: a scoped package directory anywhere in the path (`@anthropic-ai/claude-code`), and
    // the package directory immediately under a `node_modules`. A directory that merely shares a
    // guest's name (`/home/codex`, `/var/www/claude`) is not a package and never matches.
    static QString guestPackaged(const QString &token) {
        const QStringList components = token.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (int i = 0; i + 1 < components.size(); ++i) {
            const QString &component = components.at(i), &following = components.at(i + 1);
            if (component.startsWith(QLatin1Char('@'))) {
                if (const QString id = guestIdForPackage(component + QLatin1Char('/') + following); !id.isEmpty())
                    return id;
            } else if (component == QStringLiteral("node_modules")) {
                QString id = guestIdFor(following);
                if (id.isEmpty()) id = guestIdForPackage(following);
                if (!id.isEmpty()) return id;
            }
        }
        return {};
    }

    void pollProgram() {
        if (m_promptReported || !m_backend) {
            m_programPoll.stop(); endWaiting(true); updateOpaqueProgram(); checkPasswordPrompt();
            // The program is gone: the screen detection and the agent's permission go with it.
            endDelegation(QStringLiteral("program_exited"));
            setGuest({});   // and the guest agent the program may have been (GT7X)
            updateScreenPrompt();
            updateTakeControl();
            return;
        }
        updateOpaqueProgram();
        checkPasswordPrompt();
        updateScreenPrompt();
        updateLoginPrompt();
        if (!m_native && !m_secretMode && m_runningSince.isValid() && m_runningSince.elapsed() > 300
            && (!m_altScreen || relay::remote::isLoginProgram(foregroundProgramName()))) {
            const QString program = foregroundProgramName();
            if (remoteSessionProgram(program) && !m_remoteHandled) {
                // ssh and friends never switch screens, so a short list stands in for detection.
                m_remoteHandled = true;
                m_remoteProgram = true;
                endWaiting(false);
                if (relay::remote::isLoginProgram(program)) beginLogin(program);
                if (controlFor(program) == QStringLiteral("human")) {
                    m_autoHuman = true; setNative(true); m_hideReason = HideReason::Remote;
                    return;
                }
                updateTakeControl();
                // A login says hello once its remote prompt shows (updateLoginPrompt).
                if (!m_login.active)
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
        // foregroundArgv(), not foregroundCommandLine(): the latter joins the NUL-separated
        // cmdline with spaces, so `/opt/my tools/claude` split into two tokens (GT7X).
        setGuest(guestProgram(foregroundArgv()));   // GT7X: claude / codex, or "" again
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
        // The tree comes from relay::usage::walkTrees, the one /proc walk in the program (the
        // usage meter is the other reader). It follows every thread's children, so a process a
        // threaded program forked is in the list here too; Detail::PidsOnly keeps this 250 ms
        // poll as cheap as its own walk was, opening no stat or statm, and 64 is the cap it has
        // always used.
        QList<int> pids;
        for (const relay::usage::ProcessInfo &info :
             relay::usage::walkTrees({shell}, 64, relay::usage::Detail::PidsOnly))
            pids.append(int(info.pid));
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
        // The line said all four of its things in amber. Three of them are not waiting on the
        // person — the agent driving is agent work, a program merely running is terminal work —
        // and amber is reserved for what is (docs/ARCHITECTURE.md, "What the colours mean"). So
        // the hint now wears the colour of the state it is describing, which is the same map the
        // pane's own status glyph uses.
        QString state;
        if (!m_native && !m_secretMode) {
            const QString program = foregroundProgramName();
            const QString who = program.isEmpty() ? QStringLiteral("The program") : program;
            if (m_delegated) {
                const QString driven = !m_delegatedProgram.isEmpty() ? m_delegatedProgram
                                       : program.isEmpty() ? QStringLiteral("the program") : program;
                text = QStringLiteral("The agent is driving %1 · %2 takes it back")
                           .arg(driven, Keymap::instance().shortcutText(QStringLiteral("control.human")));
                state = QStringLiteral("agent");
            } else if (m_screenPrompt.actionable()) {
                // Read off the screen, so it names the question: "apt is asking: … [Y/n]".
                text = relay::screen::waitingLine(program, m_screenPrompt);
                state = QStringLiteral("needs-you");
            } else if (m_waiting) {
                text = QStringLiteral("%1 is waiting for input · Enter sends your line to it").arg(who);
                state = QStringLiteral("needs-you");
            } else if (!m_opaqueProgram.isEmpty()) {
                text = QStringLiteral("%1 is running · prompts queue until it exits · %2 to type into it")
                           .arg(m_opaqueProgram, Keymap::instance().shortcutText(QStringLiteral("control.human")));
                state = QStringLiteral("running");
            }
        }
        m_opaqueHint->setText(text);
        if (m_opaqueHint->property("state").toString() != state) {
            m_opaqueHint->setProperty("state", state);
            m_opaqueHint->style()->unpolish(m_opaqueHint);
            m_opaqueHint->style()->polish(m_opaqueHint);
        }
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

    // What the "▸ running" line of the queue strip says: the queue item or agent turn in progress.
    QString runningLabel() const {
        if (m_activeValid) return (m_active.agent ? QStringLiteral("✦ ") : QStringLiteral("$ ")) + m_active.label();
        if (m_agentBusy) return QStringLiteral("✦ ") + m_itemPrompts.value(m_currentItem).text;
        if (!m_promptReported && !m_pendingCommand.isEmpty()) return QStringLiteral("$ ") + m_pendingCommand;
        return {};
    }

    // The queue strip under the terminal: what is running, then one list in delivery order —
    // steers waiting for the running turn's next tool call (↪, the agent's colour), then queued
    // terminal commands ($, amber) and agent prompts (✦, cyan). Every row is selected, edited and
    // removed the same way (↑, Shift+Delete, ×); queued rows drag to reorder. It is a row of
    // the pane's column rather than an overlay, so showing it moves the terminal up instead of
    // covering its last lines (owner report, 2026-09-18), and the show runs through
    // keepPaneSizes() because the row's height is part of this pane's minimum height (#G152).
    void rebuildQueueStrip() {
        m_paneState.changed();   // pane_state (relay-terminal-71)
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
        const bool headSteerable = m_selected == 0 && m_agentBusy && !m_entries.isEmpty() && m_entries.first().agent
                                   && !m_entries.first().written();
        auto *hint = new QLabel(!m_selectedSteer.isEmpty()
                                    ? QStringLiteral("Enter or type to edit · Ctrl+↓ back to the queue · Ctrl+Enter now · Shift+Del withdraw · Esc")
                                : headSteerable
                                    ? QStringLiteral("Ctrl+↑ next tool call · Ctrl+↓ move · Enter save · Esc cancel · Shift+Del remove")
                                : m_selected >= 0
                                    ? QStringLiteral("↑↓ row · Ctrl+↑↓ move · Enter save · Esc cancel · Shift+Del remove")
                                    : QStringLiteral("↑ select a row · Ctrl+↑↓ move · Shift+Del remove"));
        hint->setObjectName(QStringLiteral("queueHint"));
        hint->setToolTip(QStringLiteral(
            "Rows run top to bottom. ↪ rows reach the agent inside this turn, at its next tool call; the rest run after it.\n"
            "↑ on the empty prompt box selects the top row; ↑↓ move between rows.\n"
            "A queued row is edited in the prompt box: Enter saves, Esc cancels. Ctrl+↑↓ reorders it;\n"
            "Ctrl+↑ on the first queued prompt while the agent works sends it at the next tool call.\n"
            "A ↪ row: Enter or typing takes it back into the prompt box to edit, Ctrl+↓ moves it back to\n"
            "the queue, Ctrl+Enter interrupts the turn and sends it now.\n"
            "Shift+Delete or × removes a queued row and withdraws a ↪ row the agent has not taken yet."));
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
        const QString running = runningLabel();
        if (!running.trimmed().isEmpty()) {
            auto *label = new QLabel(QStringLiteral("▸ running  ") + fontMetrics().elidedText(running.simplified(), Qt::ElideRight, std::max(160, width() - 180)));
            label->setObjectName(QStringLiteral("queueRunning"));
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
        using Row = QueueRowDelegate;
        QListWidgetItem *current = nullptr;
        // Steers first: they reach the agent before anything queued. Not draggable, because "the
        // next tool call" is not a place among the others; Ctrl+↓ is how one goes back.
        for (const auto &steer : std::as_const(m_steering)) {
            auto *item = new QListWidgetItem(steer.text, m_queueList);
            item->setData(Row::EntryIdRole, QVariant::fromValue<qulonglong>(0));
            item->setData(Row::AgentRole, true);
            item->setData(Row::KindRole, QStringLiteral("steer"));
            item->setData(Row::RowIdRole, QStringLiteral("steer:") + steer.requestId);
            item->setData(Row::PendingRole, steer.withdraw);
            item->setToolTip(steer.withdraw
                ? QStringLiteral("Withdrawing · unless the agent reaches its next tool call first")
                : QStringLiteral("Delivered inside the running turn at the agent's next tool call\n%1\n\n"
                                 "Enter or typing takes it back to edit · Ctrl+↓ back to the queue · Ctrl+Enter sends it now"
                                 " · Shift+Delete or × withdraws it").arg(steer.text));
            item->setFlags(steer.withdraw ? Qt::NoItemFlags : Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (steer.requestId == m_selectedSteer) current = item;
        }
        for (int i = 0; i < m_entries.size(); ++i) {
            const QueueEntry &entry = m_entries[i];
            auto *item = new QListWidgetItem(entry.label(), m_queueList);
            item->setData(Row::EntryIdRole, QVariant::fromValue<qulonglong>(entry.id));
            item->setData(Row::AgentRole, entry.agent);
            item->setData(Row::KindRole, entry.agent ? QStringLiteral("agent") : QStringLiteral("command"));
            item->setData(Row::RowIdRole, QStringLiteral("entry:%1").arg(entry.id));
            item->setToolTip(entry.text);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            if (i == m_selected) current = item;
        }
        m_fillingQueueList = false;
        if (current) m_queueList->setCurrentItem(current);
        else { m_queueList->clearSelection(); m_queueList->setCurrentRow(-1); }
        const int rowHeight = std::max(20, fontMetrics().height() + 8);
        const int rows = int(m_steering.size() + m_entries.size());
        m_queueList->setFixedHeight(std::min<int>(6, std::max<int>(1, rows)) * rowHeight + 4);
        m_queueList->setVisible(rows > 0);
        if (current) m_queueList->scrollToItem(current);
        layout->addWidget(m_queueList);
        placeQueueStrip();
        QTimer::singleShot(0, this, [this] { placeQueueStrip(); });
    }

    // The height the queue strip asks the pane's column for: what its rows need, still capped at
    // half of what it shares with the terminal. A row now rather than an overlay, so this is what
    // moves the terminal up instead of covering it (owner report, 2026-09-18: "the terminal needs
    // to move up, rather than being covered up").
    void placeQueueStrip() {
        if (!m_queueStrip || !m_terminalHost) return;
        // Too short for a legible strip: nothing, rather than a clipped one (owner, 2026-09-18).
        // Queueing already says so on screen ("Queued · the command runs when the terminal is
        // free"), so a strip that cannot be drawn is not the only word the person gets.
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
        // A login types from the prompt box; Take control stays for full-screen remote programs.
        const bool offerControl = relay::input::offerTakeControl(state, m_remoteProgram && !m_login.active);
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
        // The terminal's blue "Relaying <program>…" line rides the shell poll, which is what
        // notices a program start and end; a turn's violet line keeps its own one-second clock.
        if (!m_agentBusy) refreshBusyLine();
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
        // The guest channel is polled on the same tick (26.3), before state.json's own checks
        // below can return: a guest event must land even in a tick where the shell did not.
        pollGuestEvents();
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
        if (const int reported = event.value(QStringLiteral("shell_pid")).toInt(); reported > 0) {
            const bool fresh = m_shellPid != reported;
            m_shellPid = reported;
            // The shell naming itself is the last thing the bridge's router needs (26.5); a pane
            // that registered before its shell existed says so now, before the prompt a claude
            // could be started at.
            if (fresh) registerWithBridge();
        }
        const QString newCwd = event.value(QStringLiteral("cwd")).toString(m_cwd);
        if (newCwd != m_cwd) {
            m_cwd = newCwd; updatePaths(); changed();
            // The bridge routes by the longest workspace/cwd prefix (26.5): a pane that moved must
            // say so before a request names a place it is no longer in.
            registerWithBridge();
        }
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
            // type into it ends with it (cards YR21, C1HH). The guest agent goes with it (GT7X).
            endDelegation(QStringLiteral("program_exited"));
            setGuest({});
            updateScreenPrompt();
            m_remoteHandled = false; m_remoteProgram = false; endLogin();
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
            m_waitTicks = 0; m_echoTicks = 0; m_remoteHandled = false; m_remoteProgram = false; endLogin(); m_secretDeclined = false;
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
        if (enabled) takeBackFromGuest();          // and the same for a guest (section 10.3)
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
        // The full line is kept here; updateHeader() decides how much of it fits and elides the
        // rest away from the left, so the end of the path — the part that says where you are —
        // is the part that survives.
        m_cwdText = width() >= 1000 || m_cwd == m_workspace
            ? QStringLiteral("TERMINAL  ") + tilde(m_cwd) + (m_cwd == m_workspace ? QString() : QStringLiteral("     │     AGENT WORKSPACE  ") + tilde(m_workspace))
            : tilde(m_cwd);
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

    // The header's give-way ladder (`relay::panes::headerFit`, owner's decision 2026-09-19). Every
    // element in this row — the state glyph and its word, the title, the directory, the ssh, phone
    // and usage chips, the subagent badge (#XM0T, #SPBN, #D03W, #YMSR) — is measured at its natural
    // width, the ladder says what each of them shows in the room there is, and this applies it. The
    // labels are elided by hand rather than left to the layout, which clips a squeezed QLabel
    // mid-glyph: that is how a crowded header came to end in a stray half of a character.
    //
    // PaneChrome owns everything in the row except the two labels, so it fills in its own share
    // through `onHeaderWants` and is handed the answer through `onHeaderFit`. Applying the answer
    // changes what those widgets ask for, Qt lays the row out again and this runs again — which is
    // safe only because the ladder reads natural widths alone and so answers the same thing twice.
    std::function<void(relay::panes::HeaderWants &wants, QList<QWidget *> &mine)> onHeaderWants;
    std::function<void(const relay::panes::HeaderFit &fit)> onHeaderFit;

    void updateHeader() {
        if (!m_titleLabel) return;
        const QString shown = m_title.isEmpty() ? QFileInfo(m_cwd).fileName() : m_title;
        const QFontMetrics metrics(m_titleLabel->font());
        const QFontMetrics cwdMetrics(m_cwdLabel ? m_cwdLabel->font() : m_titleLabel->font());
        relay::panes::HeaderWants wants;
        wants.title = metrics.horizontalAdvance(shown);
        wants.directory = m_cwdLabel && !m_cwdText.isEmpty() ? cwdMetrics.horizontalAdvance(m_cwdText) : 0;
        // What no element on the ladder can have: the `auto` badge, the margins, and the room the
        // hover button row is kept clear of.
        wants.fixed = (m_titleAuto && m_titleAuto->isVisible()
                           ? m_titleAuto->sizeHint().width() + (m_headerLayout ? m_headerLayout->spacing() : 0)
                           : 0)
                      + (m_headerLayout ? m_headerLayout->contentsMargins().right() : 0) + 32;
        QList<QWidget *> chrome;
        if (onHeaderWants) onHeaderWants(wants, chrome);
        // Anything else that has been put in this row keeps its whole width: it is not on the ladder.
        for (int i = 0; m_headerLayout && i < m_headerLayout->count(); ++i)
            if (QWidget *w = m_headerLayout->itemAt(i)->widget(); w && !w->isHidden() && w != m_titleLabel
                && w != m_titleEdit && w != m_titleAuto && w != m_cwdLabel && !chrome.contains(w))
                wants.chips += w->sizeHint().width() + m_headerLayout->spacing();
        const int header = m_headerWidget ? m_headerWidget->width() : width();
        const relay::panes::HeaderFit fit = relay::panes::headerFit(header, wants);
        if (onHeaderFit) onHeaderFit(fit);
        if (m_cwdLabel) {
            // Hiding it cannot change the answer: the ladder was told the directory's full width
            // and never what became of it, so the two cannot chase each other.
            m_cwdLabel->setVisible(fit.directory > 0);
            m_cwdLabel->setText(fit.directory >= wants.directory
                                    ? m_cwdText
                                    : cwdMetrics.elidedText(m_cwdText, Qt::ElideLeft, fit.directory));
        }
        // elidedText() at exactly the text's own advance can still round to an ellipsis, so a title
        // that was granted everything it asked for is set as it is.
        m_titleLabel->setText(fit.title >= wants.title ? shown
                                                      : metrics.elidedText(shown, Qt::ElideRight, fit.title));
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
            {"relay-free", "Relay Free", "https://api.relay-terminal.ai/v1", "relay-main", "{}"},
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
        // 0 is the automatic setting: the model's own documented output cap, which the worker
        // resolves per endpoint (presets.max_output). The spin box shows a word, not a zero.
        auto *tokens = new QSpinBox; tokens->setRange(0, 131072);
        tokens->setSpecialValueText(QStringLiteral("Automatic (the model's own limit)"));
        tokens->setValue(settings.value("provider/max_tokens", 0).toInt());
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
        // The consent is about data leaving the machine. Plain HTTP to a loopback host is a model
        // server on this machine (the same rule as relay_core.provider.loopback_http): nothing
        // leaves, there is no key, and asking to "share data with this provider" only confuses
        // (owner, 2026-09-18, with http://127.0.0.1:8080/v1 in the box).
        auto *localNote = new QLabel(QStringLiteral("This is a model server on this machine: prompts and tool results stay here, and no API key is needed."));
        localNote->setWordWrap(true); layout->addWidget(localNote);
        const auto isLocalServer = [base] {
            const QUrl url(base->text().trimmed());
            const QString host = url.host().toLower();
            return url.scheme().toLower() == QStringLiteral("http")
                && (host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1"));
        };
        const auto showForServer = [=] {
            const bool local = isLocalServer();
            consent->setVisible(!local); localNote->setVisible(local);
            key->setEnabled(!local); saveKey->setEnabled(!local);
            key->setPlaceholderText(local ? QStringLiteral("Not needed for a model server on this machine")
                                          : QStringLiteral("Leave empty to use the keyring key for this preset"));
        };
        connect(base, &QLineEdit::textChanged, &dialog, showForServer);
        showForServer();
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(extra->toPlainText().toUtf8(), &error);
            // One sentence about the thing that is actually wrong, with the keyboard put on it. The
            // old message listed all three conditions whichever one had failed.
            const auto refuse = [&dialog](QWidget *field, const QString &text) {
                QMessageBox::warning(&dialog, QStringLiteral("Check settings"), text);
                field->setFocus();
            };
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                refuse(extra, QStringLiteral("Extra request JSON must be a JSON object, for example {} or {\"temperature\": 0.7}.")); return;
            }
            if (!QFileInfo(workspace->text()).isDir()) {
                refuse(workspace, QStringLiteral("The agent workspace is not an existing folder. Choose one with “Choose…”.")); return;
            }
            if (!isLocalServer() && !consent->isChecked()) {
                refuse(consent, QStringLiteral("Tick “Send my submitted agent prompts and tool results to this provider.” to continue. "
                                               "The agent cannot work without sending them.")); return;
            }
            if (isLocalServer()) key->clear();      // never send a key to a model server on this machine
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
            updatePaths();
            registerWithBridge();   // the bridge routes by workspace too (26.5)
            dialog.accept();
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
    // The guest's live facts and the queue of permission questions the shims are waiting on
    // (GT7X, 26.3/26.4). The events themselves are files in guest-events/, deleted as they are
    // handled, so nothing about them is cached here.
    QString m_guestModel;
    QList<GuestQuestion> m_guestQuestions;   // pending, oldest first; the front one is on screen
    int m_guestContextPct = -1;   // the guest's context window share in use; -1 when unknown
    bool m_guestBusy = false;
    QStringList m_guestSlashCommands;  // slash event catalog; empty until its static scan returns
    QProcess *m_guestTail = nullptr;    // relay_core.guest_codex tail, while a codex is in front
    bool m_guestBarMouse = false;       // the question on screen was reached with the mouse
    // A bridge diff this pane still owes claude an answer to (GT7X, 26.5): where the sidecar is
    // waiting for the decision, and the file the decision is about.
    // The diff view is held weakly: it is a widget in another pane of this tab and may be closed
    // before the answer is given — which is itself a rejection, delivered by the view's own
    // destructor through the callback it holds.
    struct GuestDiff {
        bool pending = false;
        QString replyPath, file;
        QPointer<relay::DiffView> view;
        QString banner;   // the pointer this diff put up, so settling can take down its own and no other
    };
    GuestDiff m_guestDiff;
    QJsonArray m_knownCommands;
    QTemporaryDir m_runtime{QDir::tempPath() + QStringLiteral("/relay-XXXXXX")};
    QProcess m_worker;
    // The pane's resource meter (usageSample() above); one baseline per pane, so percentages
    // are always over the status poll's own interval.
    relay::usage::Meter m_usageMeter;
    relay::usage::Sample m_usageSample;
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
    QString m_cwdText;   // the directory line in full; the label shows as much of it as fits
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
    relay::gaps::Block m_lastBlock = relay::gaps::Block::None;   // what printed last (#5AWD)
    // run_in_terminal (protocol 22). m_handoffId: the terminal_command still to be answered.
    // Prefill: a reporting command sits in the prompt box. Next: the command being staged reports.
    // Armed: the running command reports when it exits. Chain: runs since the user last typed.
    QString m_handoffId, m_handoffCommand, m_handoffOutput;
    bool m_handoffPrefill = false, m_handoffPrefix = false, m_handoffNext = false, m_handoffArmed = false;
    // Status glyphs (#XM0T): a prefill of any kind is in the box; the last finished turn and how.
    bool m_handoffOffered = false, m_lastAsked = false;
    quint64 m_finishSerial = 0;
    QString m_lastOutcome;
    // The last agent turn ended in an error and nothing has happened in the pane since: the
    // `failed` of the protocol's pane status (shareStatus), and remote/notify.py's trigger.
    bool m_shareFailed = false;
    bool m_captureForAgent = false, m_remoteSubmit = false;
    int m_handoffChain = 0;
    // Wrong-mode hints (2026-09-17): a terminal submission that reads like a request
    // (m_commandNatural), the shell command an agent-mode turn started from
    // (m_turnShellPrompt), its run_command texts by call_id (m_runCommands), whether the
    // hint already fired this turn (m_modeHintShown), and the mode chip's flash state.
    bool m_commandNatural = false, m_modeHintShown = false;
    QString m_turnShellPrompt;
    QHash<QString, QString> m_runCommands;
    // One line per tool call (#TK9C): the cursor's own state machine, the record behind each
    // anchor (bounded), the fold requests still in flight, and the call whose row a live
    // tool_output tick is counting for.
    relay::calllines::LineCursor m_callCursor;
    QHash<QString, CallRecord> m_calls;
    QStringList m_callOrder;
    QHash<QString, QString> m_foldRequests;        // request id -> the anchor waiting for its content
    QHash<QString, QString> m_turnOutputRequests;  // request id -> the turn pane that asked
    QString m_liveCall, m_liveTurn;
    relay::toollabel::Label m_liveLabel;
    QElapsedTimer m_liveTick;
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
    QList<SlashCommand> m_skillCommands;   // `/name` for each skill the agent can load
    QString m_skillHintPending;            // a skill asked for in prose; hinted at the turn's end
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
    // The reasoning fold (issue T8CN): the anchor streaming now (empty between blocks), the last
    // finished one (Alt+R reopens it until the next block), whether it is open, whether a reader —
    // not the stream — last set that, whether a coalesced flush is queued, and the per-turn block
    // counter that tells a second block in one turn from the first (thinking, thinking-2, …).
    QString m_thinkingAnchor, m_lastThinkingAnchor;
    bool m_thinkingFoldOpen = false, m_thinkingUserToggled = false, m_thinkingFlushPending = false;
    QHash<QString, int> m_thinkingBlocks;
    QElapsedTimer m_thinkingPushAt;   // last push of the reasoning into an open turn pane (#K48R)
    bool m_queueWanted = false;   // the queue has something to show; room decides whether it does
    int m_toolLines = 0;              // lines of the running tool's collapsed output
    bool m_toolPartialLine = false;   // its last chunk had no trailing newline
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
    QString m_selectedSteer;   // the steer row selected in the queue list (its request id); see selectSteer
    QElapsedTimer m_selectionDroppedAt;   // a selected steer left the list and nothing took its place (forgetSteer)
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
    QPointer<relay::conversations::SessionManager> m_conversations;   // the window's manager pane, bound here
    QPointer<relay::sessioninfo::InfoView> m_infoView;                // the ⓘ pane, bound here
    relay::conversations::FindBar *m_findBar = nullptr;
    QByteArray m_capture;
    QString m_captureCommand, m_captureCwd;
    qint64 m_captureAt = 0;
    bool m_capturing = false;
    QString m_sshShareProblem;   // why this pane's shells cannot share an ssh connection, if so
    char m_lastPromptMark = 0;      // OSC 133 A/B/C/D, engine panes with the shell integration
    int m_lastMarkExitCode = -1;
    relay::TerminalBackend::Link m_walkLink;   // the link Ctrl+Shift+L is sitting on
    QTimer m_programPoll;
    HideReason m_hideReason = HideReason::None;
    bool m_altScreen = false, m_waiting = false, m_remoteHandled = false, m_remoteProgram = false;
    // An ssh or mosh login in the foreground (card #S5SH): see beginLogin().
    struct RemoteLogin {
        bool active = false;         // ssh/mosh owns the terminal and has for 300 ms
        qint64 group = 0;            // its foreground process group
        QString program;             // ssh, mosh, mosh-client
        relay::remote::Resolved where;
        bool resolved = false;       // `ssh -G` answered
        QString cwd;                 // OSC 7 from the remote shell
        bool integration = false;    // the remote shell sends OSC 133 marks
        bool bootstrapped = false;   // Relay typed shell/remote-integration.sh for this login
        bool offered = false;        // the "Enhance this ssh session" banner is up
        bool greeted = false;        // the "Logged in to …" toast was shown
        bool warnedShare = false;    // the pane has said why the agent cannot reach this host
        int promptTicks = 0;         // polls in a row the screen showed a shell prompt
        bool atPrompt = false;
        QString promptRow;           // the prompt, as inline output found it; printed back after
        QByteArray line;             // what the host has written since its last newline: the prompt
        QByteArray promptBytes;      // that prompt, text and colour only (relay::remote::promptEcho)
    } m_login;
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
    // The models serving this pane's running turn instead of its own, innermost last, or empty
    // when the turn is on the pane's own model (C5, owner 2026-09-19). Relay moves a turn off that
    // model in three places — plan mode's `planning` role (protocol 13.11), an image turn's vision
    // model (17.3), and a failover onto a provider that answers (15.2.2) — and they nest: an image
    // inside a plan turn goes back to the *planning* model, not straight to the pane's. So it is
    // one state fed by all three rather than a member each: every move pushes, every `*_ended`
    // pops, and the end of the turn empties it however the turn ended.
    QList<Serving> m_serving;
    QJsonObject m_roleSummary, m_tierSummary, m_tierCatalog;
    QJsonArray m_roleActions;
    bool m_cleanShell = false, m_closing = false;
    QJsonArray m_presets;
    bool m_configuring = false;
    bool m_seenShell = false, m_refocus = true, m_configured = false, m_agentBusy = false;
    // agent sessions UI
    QLabel *m_planChip = nullptr, *m_ctxLabel = nullptr;
    // Relay Free (protocol 13.8/13.9): the allowance chip and the last quota the worker reported.
    // m_hostedOfferShown: the add-a-key dialog went up for the current exhaustion (once per pane).
    QLabel *m_quotaLabel = nullptr;
    qint64 m_quotaLimit = 0, m_quotaUsed = 0, m_quotaResets = 0;
    bool m_hostedOfferShown = false;
    QToolButton *m_interruptButton = nullptr;
    int m_menuFileLine = 0;   // the line the right-clicked path pointed at, for "Open file"
    // voice transcription (issue NY7Z): the chip, the recorder, and the clip in flight
    QToolButton *m_mic = nullptr;
    QToolButton *m_share = nullptr;
    // Prompts from paired devices waiting on the router, by request id.
    // `author` is a display name ("alice"), never the guest id in `origin`: the row shows who
    // asked, and the id stays out of anything a person reads (card #W5N2's owner, 2026-09-18).
    struct RemotePrompt { QString text; QString origin; QString author; };
    QString m_remoteAuthor;   // the author of the prompt being submitted, for its queue entry
    QHash<QString, RemotePrompt> m_remotePrompts;
    relay::voice::Capture *m_voiceCapture = nullptr;
    QString m_voiceRequest, m_voiceClip;
    // Clips from paired devices, by the worker request id: which remote request asked for it,
    // and the file to unlink once the worker has answered.
    QHash<QString, QString> m_remoteVoice, m_remoteVoiceClips;
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
    bool m_rewindPending = false, m_forkPending = false, m_recapManual = false;
    bool m_instructionsDialogPending = false, m_onboarding = false, m_agentsListPending = false, m_reconfigureOnNewChat = false;
    bool m_finishedWhileAway = false, m_forkLoadPending = false, m_commandLoaded = false, m_initialIsFork = true;
    int m_turnsCompleted = 0, m_lastRecapTurns = -1, m_skillCount = 0;
    QList<SteerEntry> m_steering;
    QSet<QString> m_withdrawnOnReturn;   // steers the turn gave back after their × was clicked
    quint64 m_lastQueuedEntryId = 0;
    QString m_lastSteerRequest;
    QElapsedTimer m_lastQueuedAt, m_lastSteeredAt, m_awaySince;
    // In-flight turn clock (issue SQAM), drawn since card #4E13 as the "Relaying…" line
    // above the prompt box (m_busyLine) rather than a label in the strip under it.
    QTimer *m_turnClock = nullptr;
    QElapsedTimer m_turnElapsed;
    QString m_turnStep;
    QString m_turnClockText;              // the turn's line, for pane_state's clock (relay-terminal-71)
    PaneBusyLine *m_busyLine = nullptr;   // the "Relaying…" line above the prompt (#4E13, #HQ2B)
    // "waiting for 2 subagents, 1 job . . ." in the prompt box (cards #V7QD, #KP4M): the call_ids
    // of the main agent's running agent_wait and command_output (empty when there is none), the dot
    // phase, and the timer that grows them.
    QString m_waitCall, m_jobWaitCall;
    QTimer *m_waitDots = nullptr;
    int m_waitPhase = 0;
    bool m_waitShown = false;             // this pane, not something else, owns the placeholder now
    QList<PendingToast> m_toastQueue;     // toasts waiting behind the one up
    QString m_toastHintId;                // the hint the toast up is, if it is one
    QElapsedTimer m_toastShown;
    int m_toastMs = 0;
    QTimer m_escTimer;
    // sudo & co. in the foreground
    QLabel *m_opaqueHint = nullptr;
    QString m_opaqueProgram;
    // Screen-text input detection (card YR21) and agent-driven programs (card C1HH).
    relay::screen::Detection m_screenPrompt;
    bool m_delegated = false;          // the user handed the foreground program to the agent
    QString m_delegatedProgram;
    QString m_guest;                   // claude / codex in the foreground, "" otherwise (issue GT7X)
    QString m_guestWanted;             // picked in the model box, not yet in the foreground (26.9)
    QProcess *m_guestLaunch = nullptr; // relay_core.guest_launch preparing the command line, while it runs
    std::function<void()> m_guestLeaveThen;   // what needs the shell, once the guest has exited
    bool m_guestLeaving = false;       // the pane has moved on from this guest; it exits when idle
    QFrame *m_guestBar = nullptr;      // the guest's permission question, floating over the terminal
    QLabel *m_guestBarLabel = nullptr;
    QPushButton *m_guestAllow = nullptr;   // focused while a question is up; Y/Enter presses it
    QPushButton *m_guestDeny = nullptr;
    QLabel *m_guestChip = nullptr;     // the guest's model/context chip in the prompt-box strip
    // Tier A (29.4): the guest as this pane's agent, through the worker's harness preset.
    QJsonObject m_guestRequest;        // the `guest` object staged for the next configure/set_model
    QString m_presetBeforeGuest;       // the model to go back to if the guest cannot start (29.3)
    QString m_guestSession;            // the guest's own session id, as `configured` reported it
    struct PendingGuestResume { QString guest; QStringList extra; QString cwd; };
    PendingGuestResume m_pendingGuestResume;   // a sessions row waiting for the worker's presets
    struct PendingGuestTask { QString guest, task, card; };
    PendingGuestTask m_pendingGuestTask;       // a Verify on a guest waiting for the same (#T71W)
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
    QString m_boardTask, m_boardTaskCard;   // Execute's task, until the agent is configured (#XS6Q)
    void runBoardTask() {
        if (m_boardTask.isEmpty()) return;
        QueueEntry entry;
        entry.agent = true; entry.text = m_boardTask;
        entry.why = QStringLiteral("Switchboard · Execute #%1").arg(m_boardTaskCard);
        entry.cards = QJsonArray{QJsonObject{{QStringLiteral("id"), m_boardTaskCard}}};
        noteWorkCard(m_boardTaskCard);
        m_boardTask.clear(); m_boardTaskCard.clear();
        if (relay::queuesubmit::decide(queueSubmitState()) == relay::queuesubmit::Decision::StartNow) startAgentEntry(entry, false);
        else enqueue(entry);
    }
    QString m_lastPlanPath;      // the plan this pane's agent wrote last
    Ask m_ask;                   // the question card up in this pane, if any (#MQ9C)
    QPointer<relay::RequestsPanel> m_requestsPanel;
    bool m_limitReached = false;   // the last turn stopped at the step or tool-call limit
    relay::SubagentsPanel *m_agentsPanel = nullptr;
    relay::JobsModel m_jobs;
    relay::JobsPanel *m_jobsPanel = nullptr;
    QList<QPointer<relay::SubagentTranscriptView>> m_subagentViews;
    QPointer<relay::SubagentTabsView> m_subagentTabs;   // this pane's subagent pane, while open
};


