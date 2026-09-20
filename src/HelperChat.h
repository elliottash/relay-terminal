// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The helper agent's panel: one chat surface every pane can embed (#FEJQ, protocol 19.18).
//
// It began as the Switchboard page agent's panel (#8YQ9) — a conversation about the whole board,
// pinned to the bottom of the list page — and 2026-09-20 made it the surface of one helper system
// instead: "the Switchboard agent, the helper agents in Options, Actions and Sessions … are one
// joint thing — one helper worker per tab, one chat surface, one tool set" (owner). So the panel
// takes the **name of the pane it is in** and nothing else changes shape: `pane` rides on every
// `board_chat` it sends, which is what picks the brief on the worker's side, and it is what the
// `chat: true` turn events coming back are filtered by, so one pane's answer can never land in
// another pane's log while the tab's single worker serves all of them.
//
// The worker half landed in 59b7b190 — one persistent conversation per board per worker, a
// worker-side FIFO queue, the `switchboard` role, and the survey as a fresh board's opening turn.
// This is what the owner talks to it through: a log of the conversation, the queue in delivery
// order, a composer whose Enter sends (and whose second prompt *queues* rather than being
// refused), and the board-wide buttons that belong next to an agent rather than in the filter row
// — Clean up, moved down here, and Check beside it.
//
// **The Switchboard's extras are a flag, not a subclass body.** The survey offer, the queue box
// and the panel's own Check button exist only for `pane == "switchboard"`; `relay::BoardChatPanel`
// (src/BoardChat.h) is that pane's name for this class and carries no code of its own. A subclass
// that held them would have to reach into `setChatState`, `setRunning` and `settleTurn` — the
// survey word is part of the head's clock line and the forge look is disabled while a turn runs —
// so the seam would be three virtuals and a handful of protected members to save two `if`s.
//
// **Outside the Switchboard it opens collapsed** (owner, 2026-09-20): a single row at the pane's
// bottom right — a question mark and "Helper Agent (Alt+Q)", the live key in the button's own
// text — because "the Switchboard's 320 px of log plus composer is most of a small pane".
// Clicking the row — or anything giving the composer focus — expands it,
// and a small control on the head row folds it back. The expanded log is then sized to the pane
// it is in (at most ~40 % of the pane's height, never less than three lines) rather than to the
// list page's fixed 320.
//
// It holds no process and no settings. Everything it knows arrives through `handleEvent` — the
// `chat` block on a `board` event, the `chat: true` turn events, `board_survey` — and everything
// it does leaves through `onSend` as a worker message. That is the same contract `BoardView`
// itself keeps, so the panel is drivable from a test by hand (tests/boardmodel_test.cpp).
//
// Design: docs/SWITCHBOARD-DESIGN.md section 4; protocol docs/AGENT-SESSIONS-PROTOCOL.md 19.18.
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

class QFrame;
class QHBoxLayout;
class QLabel;
class QResizeEvent;
class QTextBrowser;
class QTimer;
class QToolButton;
class QVBoxLayout;
class RichEditor;

namespace relay {
namespace voice {
class Capture;
}

namespace board {

// The fix request a clicked problem drafts for the page agent's composer (#8YQ9). One wording for
// the banner over the list and for the Check/triage findings under the conversation, so the two
// paths cannot drift: it names the card when the path has an id in it, quotes the checker, and
// asks — the owner reads it and presses Enter, or does not.
inline QString fixRequest(const QString &path, const QString &message, int total = 1)
{
    const QString file = path.trimmed();
    // A card file is `<something>/<id>-<slug>.md` or carries `#ID` in the message; the checker
    // names the file, which is what the agent needs to open it either way.
    QString request = file.isEmpty()
        ? QStringLiteral("The board's check reports: %1").arg(message)
        : QStringLiteral("Fix %1: %2").arg(file, message);
    request += QStringLiteral(" \u2014 please fix this card.");
    if (total > 1)
        request += QStringLiteral(" (%1 problems are reported in all; the Check button lists "
                                  "them.)").arg(total);
    return request;
}

}  // namespace board


// The panes that have a helper, as the protocol spells them. `switchboard` is the board's own
// page agent; the other three are the panels #FEJQ embeds. A pane name is a plain string on the
// wire (the worker picks a brief by it), so this is the spelling and not a type.
namespace helperpane {
inline QString switchboard() { return QStringLiteral("switchboard"); }
inline QString options() { return QStringLiteral("options"); }
inline QString actions() { return QStringLiteral("actions"); }
inline QString sessions() { return QStringLiteral("sessions"); }
// What the panel's head says it is: "Switchboard agent" on the board, "<Pane> helper" elsewhere
// (owner, 2026-09-20, decision 4: the role's label becomes "Helper agent" and "each panel's
// header says where it is").
QString title(const QString &pane);
}  // namespace helperpane

class HelperChatPanel : public QWidget {
public:
    // `pane` is the helper's name on the wire and the panel's identity: it decides the head, the
    // Switchboard-only rows, whether the panel starts collapsed, and which turn events are ours.
    explicit HelperChatPanel(const QString &pane = QStringLiteral("switchboard"),
                             QWidget *parent = nullptr);

    QString pane() const { return m_pane; }
    // Options and Actions are one widget in two modes (src/SettingsPane.h), and the panel at its
    // foot follows the mode rather than being rebuilt: the conversation is one (§30.7), so a
    // swap must not throw away a log somebody has just read, or strand a turn that is running.
    // Only the embedded panes move this way — the Switchboard's extras are built from its name,
    // so a panel that is, or would become, the board's is left alone.
    void setPane(const QString &pane);

    // ---- wiring ---------------------------------------------------------------------------
    // A worker protocol message: `board_chat`, `board_chat_cancel`, `board_chat_queue_*`,
    // `board_check`, `board_import_apply`, `transcribe`. The panel never picks its own request
    // ids out of thin air — `nextRequestId` is supplied so its messages carry the view's prefix
    // and the worker's answers are told from another pane's.
    std::function<void(const QJsonObject &)> onSend;
    std::function<QString()> nextRequestId;
    std::function<void(const QString &id, const QString &keys)> onHint;   // shortcut hints
    std::function<void(const QString &text)> onStatus;                    // the pane's status line
    std::function<void(const QString &path)> onOpenFile;                  // a link in the log
    std::function<void(const QString &cardId)> onOpenCard;                // `card:K7Q2` in the log
    // The other two link schemes an answer can carry (#FEJQ): `option:<section>/<row>` reveals
    // that row in Options, `session:<id>` opens that conversation. The helper answers with links
    // into the app because the app is what it is talking about; a pane that cannot resolve one
    // simply leaves the callback unset and the link does nothing.
    std::function<void(const QString &sectionId, const QString &rowId)> onOpenOption;
    std::function<void(const QString &sessionId)> onOpenSession;
    // Clean up is the board's, not the panel's: `BoardView` owns the button, its Stop/Clean up
    // text and its run (19.9). The panel only gives it a home — see `addToolWidget`.

    // ---- what the panel is given ------------------------------------------------------------
    // The `chat` block of a `board` event or of any `board_chat_*` answer: `{running, turn_id,
    // model, survey, seconds, queue[], history[]}`. Redraws the whole panel, so a pane opened
    // mid-conversation catches up from one event.
    void setChatState(const QJsonObject &chat);
    // A `chat: true` turn event, or one of `board_chat_started`, `board_chat_queued`,
    // `board_chat_cancelled`, `board_chat_state`, `board_survey`, `transcribed`. True when the
    // panel took it, so `BoardView` never lets one reach a card thread (the `cleanup: true`
    // precedent, BoardView::handleCleanupEvent). An event tagged with another pane's name is
    // never ours, whatever its type (#FEJQ: one worker, four panels).
    bool handleEvent(const QString &type, const QJsonObject &event);

    // ---- what the pane asks of it ------------------------------------------------------------
    // A problem or a triage finding was clicked: put a fix request in the composer and focus it.
    // A **draft**, never sent (owner, 2026-09-19: "draft you confirm").
    void prefill(const QString &text);
    void focusComposer();
    bool composerHasFocus() const;
    bool running() const { return m_running; }
    int queueLength() const { return m_queue.size(); }
    // What the log holds right now, for a test and for the pane's status line.
    QString transcript() const;
    // The draft in the composer, for a test.
    QString draft() const;

    // ---- collapsed / expanded (#FEJQ; the Switchboard is always expanded) ---------------------
    bool collapsed() const { return m_collapsed; }
    // Expanding focuses the composer — the row said "Ask", so the cursor belongs in the box.
    void expand();
    void collapse();
    // The key that focuses this composer, for the collapsed row's key line and for the hint the
    // mouse path teaches (WARP.md's standing rule). The board's own is `board.chat` / Ctrl+/.
    void setAskShortcut(const QString &hintId, const QString &keys);

    // The head row's action buttons (Clean up, Check) and the prompt box's chip strip (the model
    // box). Widgets are reparented in; the panel never owns or styles them.
    //
    // The head row is where an action that needs no typing belongs (owner, 2026-09-20, of the
    // card page's Plan/Execute: "move those buttons out of there … we put the 'clean up' button
    // there for the main switchboard agent, for example"). The box below holds the text and the
    // chips that qualify it, and nothing else.
    void addToolWidget(QWidget *widget);
    void addComposerWidget(QWidget *widget);

    // Triage and Check findings, from `board_problems {items, section}`. `section` empty means the
    // unscoped Check button. Rendered as a clickable list under the conversation; a click calls
    // `prefill`. Returns the number of problems shown.
    int showFindings(const QJsonArray &items, const QString &section);

    // The board's root, so a `board_survey`'s file links resolve. Set from the `board` event.
    void setWorkspace(const QString &workspace) { m_workspace = workspace; }
    // The provider rows, for the microphone's "voice needs an OpenRouter key" offer: the `presets`
    // event's array, the same one the model box reads.
    void setPresets(const QJsonArray &presets) { m_presets = presets; }

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // ---- the conversation ---------------------------------------------------------------
    void rebuildLog();                       // the whole history, then whatever is streaming
    void appendDelta(const QString &text);
    void appendThinking(const QString &text);
    void finishThinking(qint64 ms);
    void setProgress(const QString &line);   // "reading Pane.h", the tool line of the turn
    void setRunning(bool running);
    void settleTurn(const QString &how);     // done / error / cancelled: fold into the history
    void send(QJsonObject message);
    void sendPrompt();                       // Enter in the composer
    void stopTurn();                         // the Stop button

    // ---- collapsed / expanded ---------------------------------------------------------------
    void applyCollapsed();                   // show the ask row or the panel, never both
    void updateAskRow();                     // "Helper Agent (Alt+Q)", in the live key's wording
    void updateLogHeight();                  // ~40 % of the pane, at least three lines

    // ---- the queue (19.18: worker-side and authoritative; this only draws it) -------------
    void rebuildQueue();

    // ---- the survey (19.18; the Switchboard's, and only ever built there) ------------------
    void showSurvey(const QJsonObject &event);
    void hideSurvey();
    void applyImport();                      // `board_import_apply {keys}` for the ticked rows
    // The survey's GitHub offer (#8YQ9 task `t:qt`, owner 2026-09-19: "if its .git, it should
    // offer to look on github.com for an issues corpus to sync"). Looking is `forge_sync_plan`
    // (19.14), which #GDQN landed and which **writes to neither side** — it reports what a sync
    // would do and stops. Bringing the issues in is the sync itself, and that surface is #ZKR0's
    // card, not this one: the card says "offer only, never auto-sync", so nothing here sends
    // `forge_sync_run`.
    void lookForIssues();
    void showForgePlan(const QJsonObject &event);
    void showForgeError(const QJsonObject &event);

    // ---- the microphone (protocol 16, the composer's chip in a pane) -----------------------
    void toggleVoice();
    void startVoice();
    void stopVoice();
    void ensureCapture();
    void onTranscribed(const QJsonObject &event);
    void updateVoiceChip();
    bool voiceKeyStored() const;
    void offerVoiceKey();

    // ---- the context-left chip (`context` events, tagged `chat: true`) ----------------------
    void updateContextChip(const QJsonObject &event);

    QString m_pane;                          // "switchboard" | "options" | "actions" | "sessions"
    bool isBoard() const { return m_pane == helperpane::switchboard(); }
    QString m_workspace;
    QJsonArray m_presets;

    // The conversation as the worker knows it: `{role, text, turn?}` entries, plus what is
    // streaming into the turn that is running now.
    QJsonArray m_history;
    QString m_streamed;
    QString m_thinking;
    bool m_thinkingDone = false;
    qint64 m_thinkingMs = 0;
    bool m_running = false;
    QString m_turnId;
    QString m_progress;
    bool m_surveyTurn = false;
    QElapsedTimer m_clock;
    QTimer *m_render = nullptr;              // a streamed answer re-renders at most 25 times a second

    struct QueueItem {
        QString id;
        QString text;
    };
    QList<QueueItem> m_queue;

    // The widgets. Object names are the qss hooks and what a test finds the panel by, and they
    // are the **same names in every pane** (`boardChatComposer`, `boardChatLog`, …): the board's
    // tests and the QA drivers find the panel by them, and a helper's panel is the same panel.
    QLabel *m_head = nullptr;                // "Switchboard agent" / "Sessions helper" + the clock
    QHBoxLayout *m_toolRow = nullptr;        // Clean up, Check: the actions that need no typing
    QToolButton *m_check = nullptr;
    QTextBrowser *m_log = nullptr;
    QWidget *m_busy = nullptr;               // the running strip: what it is doing, and Stop
    QLabel *m_busyWhat = nullptr;
    QToolButton *m_stop = nullptr;
    QWidget *m_queueBox = nullptr;           // one row per queued prompt, in delivery order
    QVBoxLayout *m_queueLayout = nullptr;
    QWidget *m_findings = nullptr;           // Check / triage findings, clickable
    QVBoxLayout *m_findingsLayout = nullptr;
    QWidget *m_survey = nullptr;             // the survey's import offer
    QVBoxLayout *m_surveyLayout = nullptr;
    QToolButton *m_import = nullptr;
    QStringList m_importKeys;                // the ticked proposals' source keys
    // The GitHub look-up: `owner/name` from the survey's `git` block (so the board needs no
    // `github:` in board.yaml to be asked), the button, the line its answer is written on, and
    // the request id that tells our answer from another pane's.
    QString m_forgeRepo, m_forgeRequest;
    QToolButton *m_forgeLook = nullptr;
    QLabel *m_forgeResult = nullptr;
    // The prompt box: one rounded frame holding the busy line, the editor and the chip strip,
    // styled as `QFrame#composer` is in a terminal pane (owner, 2026-09-20). No Send — Enter
    // sends, and while a turn runs the busy strip's ✕ Stop and Esc in the box end it.
    QFrame *m_box = nullptr;
    QHBoxLayout *m_composerRow = nullptr;    // the chip strip: context, model, microphone
    RichEditor *m_composer = nullptr;
    QToolButton *m_mic = nullptr;
    QLabel *m_context = nullptr;             // "72% left", the pane's chip idiom

    // The collapsed state (#FEJQ): one row that is the whole panel until it is asked for, the
    // control that folds it back, and everything else in one widget so showing or hiding the
    // panel is one call rather than a list of widgets that will fall out of date.
    QWidget *m_body = nullptr;               // every expanded row, in one holder
    QWidget *m_askRow = nullptr;
    QToolButton *m_ask = nullptr;
    QToolButton *m_fold = nullptr;
    bool m_collapsed = false;
    QString m_askHint = QStringLiteral("board.chat"), m_askKeys = QStringLiteral("Ctrl+/");

    // Voice (protocol 16), the pane's flow: record, `transcribe`, insert `transcribed` at the
    // cursor. One clip at a time; Relay keeps no audio.
    voice::Capture *m_capture = nullptr;
    QString m_voiceClip, m_voiceRequest;
    bool m_transcribing = false;
};

}  // namespace relay
