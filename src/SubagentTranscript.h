// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Live transcript of one subagent: the subagent_transcript snapshot, then streamed deltas, tool
// calls and outputs (subagent_event payloads), with a small input that sends agent_message.
//
// A tool call is **one line** (docs/AGENT-SESSIONS-PROTOCOL.md § 23, card #TK9C): "reading x.py"
// while it runs, rewritten in the same row to "read x.py · 412 lines" when it lands. Clicking the
// line folds its detail open underneath — the command and its output, the arguments, the diff —
// and clicking it again folds it shut. A diff folds behind that same click however short it is
// (#WXT6: nothing auto-expands), and a run of consecutive reads or
// listings collapses into "read 6 files · 4,100 lines".
#include "SubagentsPanel.h"
#include "ToolLabel.h"
#include "CallLines.h"
#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTextCursor>
#include <QVector>
#include <QWidget>
#include <functional>
#include <memory>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QTabBar;
class QTimer;
class QToolButton;

namespace relay {

class SubagentTranscriptView final : public QWidget {
    Q_OBJECT
public:
    explicit SubagentTranscriptView(const QString &id, QWidget *parent = nullptr);

    QString agentId() const { return m_id; }
    QString title() const;

    std::function<void(const QString &text)> onSend;
    std::function<void()> onClose;

    // Row state from the model: title, status line.
    void setRow(const SubagentRow &row, qint64 elapsedMs);
    // subagent_transcript {status, messages}, subagent_event {payload}, subagent_progress, subagent_finished.
    void handleEvent(const QJsonObject &event);
    void appendNote(const QString &text);
    void focusInput();
    QString plainText() const;
    // In a pane of its own the pane chrome's × closes it, so its own × (which the floating overlay
    // needs) is hidden; two crosses there landed on top of each other (owner report, 2026-09-18).
    void setHostedInPane(bool hosted);
    // Room kept free at the right of the title row for the pane chrome's buttons (PaneChrome).
    void setHeaderRightInset(int pixels);
    // A tab restored from the saved layout: the text it showed when Relay quit. The agent itself
    // ended with the previous worker, so the message box is off.
    void restore(const QString &type, const QString &description, const QString &status, const QString &text);
    // A restored tab whose agent is gone: the message box is off and the status says so.
    void setEnded(bool ended);
    static QString restoredMark() { return QStringLiteral("── from before the restart; this agent ended with the previous session ──"); }
    bool ended() const { return m_ended; }
    QString type() const { return m_type; }
    QString description() const { return m_description; }
    QString statusText() const { return m_lastStatus; }
    // The line above the message box while the agent works (card #XDZP): the main pane's
    // "Relaying · …" line, said for whom — "Relaying for main agent · reading x… · 12 s". Empty,
    // and the line hidden, when the agent is not running. For tests.
    QString busyText() const;

    // A tool line whose diff is too big to print inline (`open: {"type": "diff"}`, more than 12
    // changed lines) was clicked. The host opens it in a diff pane (src/DiffView.h); this view has
    // no pane of its own, so it only reports. Unset: the detail folds open in place instead.
    std::function<void(const QString &title, const QString &unifiedDiff)> onOpenDiff;

    // The tool lines drawn so far, newest last, exactly as they read (marker included). A merged
    // run of reads is one entry. For tests and for the pane's own bookkeeping.
    QStringList toolLines() const;
    int toolCallCount() const;
    // Folds one tool line's detail open, or shut again. Out-of-range does nothing.
    void toggleToolCall(int index);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    enum class Ink { Agent, User, Tool, ToolOutput, DiffAdd, DiffRemove, Error, Note };
    static QColor inkColor(Ink ink);   // from the live theme tokens
    // One tool call's row: the line itself, what a click folds open under it, and where both sit.
    struct ToolCall {
        QString callId;
        toollabel::Label label;
        QString detail;          // command, arguments, output, error — what the fold shows
        QString diff;            // the unified diff of a write or an edit, when the event had one
        QTextCursor line;        // start of the row's own first block (the line, then any inline diff)
        QTextCursor after;       // start of the block under the row; the fold is inserted here
        int foldChars = 0;       // characters the open fold occupies, 0 when it is shut
        bool expanded = false;
        bool done = false;
        bool thinking = false;
        bool userToggled = false;
    };
    void append(const QString &text, Ink ink);
    void appendProse(const QString &text);
    void finishProse();
    void thinkingEvent(const QJsonObject &payload);
    void toggleRow(int index);
    void refreshThinking();
    void ensureLineStart() { if (!m_atLineStart) append(QStringLiteral("\n"), Ink::Note); }
    void toolStarted(const QJsonObject &payload);
    void toolResult(const QJsonObject &payload);
    void toolOutput(const QString &text);
    // Replaces the whole block `call.line` sits in, keeping everything around it.
    void rewriteLine(ToolCall &call, const QString &text, Ink ink);
    // The row as it should read now: the fold arrow, the ✗ of a failure, and the label's line.
    QString rowText(const ToolCall &call) const;
    void drawRow(ToolCall &call);
    int callAt(int blockNumber) const;
    int indexOfCall(const QString &callId) const;
    void forgetCalls();
    void refreshBusy();
    QString m_id, m_type, m_description, m_lastStatus;
    QHBoxLayout *m_header = nullptr;
    QToolButton *m_close = nullptr;
    QLabel *m_title = nullptr, *m_status = nullptr, *m_busy = nullptr;
    QTimer *m_busyClock = nullptr;   // ticks the seconds while the agent runs
    qint64 m_elapsedMs = 0;          // as of the last setRow
    QElapsedTimer m_elapsedSince;    // since the last setRow
    QPlainTextEdit *m_log = nullptr;
    QLineEdit *m_input = nullptr;
    QVector<ToolCall> m_calls;
    toollabel::MergeRun m_merge;
    int m_mergeHead = -1;        // the row showing the merged run, or -1 when none is open
    std::unique_ptr<calllines::MarkdownStream> m_prose;
    QTextCursor m_proseTail;
    int m_thinking = -1;
    bool m_thinkingRefreshPending = false;
    bool m_atLineStart = true, m_snapshot = false, m_ended = false;
};

// One pane per main pane, one tab per subagent (owner, 2026-09-18, card #WD83). The tab shows the
// status glyph and `type id`; the header's "← main agent" (and Esc in a message box) go back to
// the owning pane's prompt box. Tabs follow the running-agents list: a tab whose row is dismissed
// or cleared by the list's rules closes, and the pane closes with its last tab.
class SubagentTabsView final : public QWidget {
    Q_OBJECT
public:
    explicit SubagentTabsView(QWidget *parent = nullptr);

    // A new tab's view, for the owner to subscribe (Pane::attachSubagentView).
    std::function<void(SubagentTranscriptView *view)> onViewCreated;
    // "← main agent", Esc in a message box.
    std::function<void()> onBackToMain;
    // The ← control was clicked (teach the key).
    std::function<void()> onBackClicked;
    // The last tab closed.
    std::function<void()> onEmpty;
    // The current tab changed (the pane's title follows it).
    std::function<void()> onTitleChanged;
    // A tab was closed with its × (before it goes): the owner dismisses a finished agent's row.
    std::function<void(const QString &id)> onUserClosed;

    // Opens (or selects) the tab for `id`; returns its view. `live`: the owner's list has this row,
    // so a restored tab with the same id (an earlier agent) is replaced by a live one.
    SubagentTranscriptView *showTab(const QString &id, bool live = true);
    SubagentTranscriptView *tab(const QString &id) const;
    SubagentTranscriptView *current() const;
    QString currentId() const;
    QStringList ids() const;
    int count() const;
    void closeTab(const QString &id);
    // The tab's ×: onUserClosed, then closeTab.
    void closeTabByUser(const QString &id);
    // Rows changed: relabel tabs and close the tabs of rows the list no longer has. A tab whose id
    // the model has never had (restored from before a restart) is left alone.
    void syncRows(const SubagentModel &model);
    // The list's rules cleared finished rows (a new prompt, New chat): tabs restored from before a
    // restart go with them.
    void dropEnded();
    // Tooltip text for the ← control, with the live key (set by the owner).
    void setBackKeys(const QString &keys);

    QString title() const;
    QString agentId() const { return currentId(); }
    void focusInput();
    void setHeaderRightInset(int pixels);

    // Saved layout: {"subagents": {"owner", "cwd", "current", "tabs": [{id, type, description, status, text}]}}.
    void setOwnerKey(const QString &key) { m_ownerKey = key; }
    QString ownerKey() const { return m_ownerKey; }
    void setCwd(const QString &cwd) { m_cwd = cwd; }
    QJsonObject node() const;
    void restore(const QJsonObject &subagents);
    static constexpr int kSavedChars = 16000;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    SubagentTranscriptView *ensureTab(const QString &id, bool live, bool select);
    int indexOf(const QString &id) const;
    int addTabFor(const QString &id, int at = -1);
    void relabel(int index);
    QHBoxLayout *m_header = nullptr;
    QToolButton *m_back = nullptr;
    QTabBar *m_bar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QHash<QString, QPointer<SubagentTranscriptView>> m_views;
    QHash<QString, bool> m_seen;   // the owner's model has had this row
    QSet<QString> m_closed;       // explicitly closed running tabs stay closed until reopened
    QString m_ownerKey, m_cwd;
};

}  // namespace relay
