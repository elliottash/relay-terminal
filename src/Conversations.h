// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The session manager pane (/resume, /conversations; cards #CCKY, #R6J0) and the Ctrl+F
// find-in-view bar. Plain Qt, no KDE dependencies; the worker does the searching
// (docs/AGENT-SESSIONS-PROTOCOL.md section 14).
//
// The manager never talks to the worker itself: it asks through onQuery/onPreview/... and is fed
// with setResults()/setPreview(), so it can be built and tested without a pane.
#include "PaneView.h"

#include <QDateTime>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>

class QAction;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTabWidget;
class QTextBrowser;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace relay::conversations {

// ----- pure helpers (unit tested in tests/conversations_test.cpp) -------------------------

// "You", "Agent", "Tool", "Tool output", "Command", "Command output"; the raw id when unknown.
QString kindLabel(const QString &kind);
// True for the kinds that come from the terminal rather than an agent thread.
bool isTerminalKind(const QString &kind);
// A match line as rich text with each range wrapped in a highlight span (Qt's rich text has no
// <mark>). Everything is HTML-escaped; ranges outside the line, overlapping or out of order are
// ignored.
QString highlighted(const QString &line, const QJsonArray &ranges);
// "just now", "14 min ago", "yesterday 09:12", "12 Sep", "12 Sep 2025".
QString whenText(double epochSeconds, const QDateTime &now);
// Seconds since the epoch for a date filter id ("any", "today", "week", "month"); 0 means "any".
double sinceFor(const QString &id, const QDateTime &now);
// Printable text from raw terminal output: CSI/OSC sequences, carriage returns and other control
// bytes removed, trailing blank lines dropped, capped at maxChars.
QString stripAnsi(const QByteArray &bytes, int maxChars = 4000);

// "Today", "Yesterday", "This week", "This month", "Older" — the group a conversation falls in
// when the list is grouped by date. `dateGroupOrder()` is the order those groups are shown in.
QString dateGroup(double epochSeconds, const QDateTime &now);
QStringList dateGroupOrder();

// The chip label for one entry of the reply's `parsed.operators`: "file: parser.cpp", an excluded
// word (`{key: "text", negated: true}`) as "not: pelican", a negated operator as "not model: kimi".
QString chipText(const QJsonObject &op);
// The query with that operator's token taken out, quotes and negation included; the rest of the
// text keeps its spelling. Only the first matching token goes, so "a a" loses one "a".
QString removeOperator(const QString &query, const QJsonObject &op);

// "closed 5 min ago" for a recently-closed stamp (milliseconds since the epoch); empty when the
// stamp is not set. Mirrors relay::closed::age, which this library cannot link against.
QString closedAgo(qint64 closedAtMs, qint64 nowMs);
// The short text tags in a row's title cell: pinned, open, closed …, unfinished, edits · N files,
// the branch when it is not the trunk, and last of all cpu / mem when the pane is busy. `openNow`
// is "a pane already has this conversation"; `usageTag` is that pane's live reading (issue
// #D03W), and it comes last because it is the one tag whose width changes while the row is on
// screen — earlier, it would push the fixed badges off a narrow pane and jog the rest sideways.
QStringList badges(const QJsonObject &item, bool openNow, const QString &closedText,
                   const QString &usageTag = QString());
// A path elided in the middle ("src/…/Conversations.cpp"); other text is elided at the end.
QString elideMiddleText(const QString &text, int maxChars);

// The "Continue" rows for an empty query: the conversations of `project` that are pinned,
// unfinished or recently closed, newest first, at most `max` of them.
QJsonArray continueItems(const QJsonArray &items, const QString &project,
                         const QSet<QString> &closedIds, int max = 5);

// The sentence the "Summarise all…" confirmation shows, from a `conversations_summarize_estimate`
// event. It always ends by saying that nothing runs until Start is pressed.
QString estimateText(const QJsonObject &event);
// "12.3k" / "812" for a token count.
QString compactTokens(double tokens);

// ----- the session manager pane (card #R6J0) ----------------------------------------------

// Every saved session, newest first, grouped by project, searchable; with "Subagent threads"
// ticked (off by default) every subagent thread too, under its owner session or labelled with it.
// It was a modal dialog (/conversations) and a resume picker (/resume) until 2026-09-18; it is now
// one pane, opened by /resume, Ctrl+Shift+Y, /conversations and the palette. Other features can add
// tabs beside the list (addTab), e.g. recently closed windows, tabs and panes.
class SessionManager : public QWidget, public relay::PaneView {
    Q_OBJECT
public:
    explicit SessionManager(QWidget *parent = nullptr);

    // Asked whenever the query or a filter changes. The object is the `conversations` request
    // body without "type"/"id": {query, scope, model, has_open_tasks, since, sources, limit,
    // include_threads, sort, offset}.
    std::function<void(const QJsonObject &request)> onQuery;
    std::function<void(const QString &sessionId, const QString &query)> onPreview;
    // The whole result row (session_id, session_dir, title, workspace…) plus Shift+Enter.
    std::function<void(const QJsonObject &item, bool newPane)> onResume;
    // Enter on a subagent thread row: open its history (the ⓘ view).
    std::function<void(const QJsonObject &item)> onOpenThread;
    // Info (Ctrl+I) on a session row: its ⓘ view without resuming it.
    std::function<void(const QJsonObject &item)> onOpenInfo;
    std::function<void(const QString &sessionId, const QString &title)> onRename;
    std::function<void(const QString &sessionId, bool pinned)> onPin;
    std::function<void(const QString &sessionId)> onDelete;
    std::function<void()> onClose;     // Esc, Close, or after a resume
    // Ctrl+Enter: continue the conversation in a new pane without disturbing this one.
    std::function<void(const QJsonObject &item)> onFork;
    // Alt+Enter on a row whose pane, tab or window was closed: put it back where it was.
    std::function<void(const QString &closedId)> onReopenClosed;
    // The summaries of protocol section 18.4 / 14: one session, the cost of doing every session in
    // a scope ("project"/"all"), the batch itself, and stopping it.
    std::function<void(const QString &sessionId, const QString &sessionDir)> onSummarise;
    std::function<void(const QString &scope)> onSummariseEstimate;
    std::function<void(const QString &scope)> onSummariseAll;
    std::function<void()> onSummariseCancel;

    // Worker events.
    void setResults(const QJsonObject &event);
    void setPreview(const QJsonObject &event);
    // `conversation_summary` (one saved session) and `session_summary` (the session a pane holds).
    void setSummary(const QJsonObject &event);
    void setSessionSummary(const QJsonObject &event);
    void setSummariseEstimate(const QJsonObject &event);
    void setSummariseProgress(const QJsonObject &event);
    // Row gone: drop it and re-run the query.
    void removed(const QString &sessionId);

    // Fed by the window, which knows what is open and what was closed; the manager never looks at
    // a window itself. `closed` maps a session id to {closed-list id, closed-at in milliseconds}.
    void setProject(const QString &project);
    void setOpenSessions(const QStringList &sessionIds);
    // What each open conversation's pane is using, as a labelled tag on its row (issue #D03W).
    // Pushed by the window's status poll; a session with no entry shows nothing.
    void setLiveUsage(const QHash<QString, QString> &usage);
    void setClosedSessions(const QHash<QString, QPair<QString, qint64>> &closed);
    // "closed 5 min ago" is a clock, not a stamp: the pane re-reads it on a timer while anything
    // is in the closed list, so a row that said "closed just now" catches up without the list
    // being rebuilt or the worker asked again. Public so a test can tick it by hand.
    void refreshClosedAges();

    void focusSearch();
    QString query() const;
    void setQuery(const QString &text);
    // Re-run the current query (also done whenever the pane is shown).
    void refresh();
    bool threadsShown() const;
    void setThreadsShown(bool on);

    // Tabs beside the list. The list is the tab "sessions"; addTab puts a widget (owned by the
    // pane from then on) after it, and showTab brings one to the front. Unknown ids are ignored.
    void addTab(const QString &id, const QString &label, QWidget *widget);
    void showTab(const QString &id);
    QString currentTab() const;

    QString paneTitle() const override;
    void focusView() override;
    void setHeaderRightInset(int pixels) override;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void requery();
    void requestMore();
    void scheduleQuery();
    QJsonObject queryRequest() const;
    void rebuildTree(const QString &keep);
    QTreeWidgetItem *addSessionRow(QTreeWidgetItem *parent, const QJsonObject &item);
    void decorate(QTreeWidgetItem *row, const QJsonObject &item);
    void selectionChanged();
    void requestPreview(const QString &sessionId);
    void unfold(QTreeWidgetItem *row);
    void fillUnfolded(QTreeWidgetItem *row, const QJsonObject &overview);
    void activate(bool newPane);
    void fork();
    void reopenClosed();
    void summariseSelected();
    void summarise(const QJsonObject &item);
    void updateItemSummary(const QString &sessionId, const QString &summary);
    void rebuildChips(const QJsonObject &parsed);
    void showOperatorHelp();
    void askEstimate();
    void startBatch();
    void clearFilters();
    bool anyFilter() const;
    void fillFacets(const QJsonObject &facets);
    void updateStatus();
    void updateEmptyState();
    QString scopeId() const;
    QString selectedId() const;
    QJsonObject selectedItem() const;
    void rename();
    void togglePin();
    void remove();
    void updateButtons();
    bool eventFilter(QObject *object, QEvent *event) override;

    QTabWidget *m_tabs = nullptr;
    QWidget *m_inset = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_scope = nullptr, *m_model = nullptr, *m_date = nullptr, *m_kind = nullptr,
              *m_sort = nullptr, *m_branch = nullptr, *m_group = nullptr;
    QCheckBox *m_threads = nullptr;
    QToolButton *m_help = nullptr, *m_filters = nullptr;
    QMenu *m_filterMenu = nullptr;
    QAction *m_hasEdits = nullptr, *m_unfinished = nullptr, *m_pinnedOnly = nullptr,
            *m_hasSummary = nullptr, *m_openTasks = nullptr, *m_summariseAll = nullptr;
    QWidget *m_chipRow = nullptr;
    QLabel *m_ignored = nullptr;
    QTreeWidget *m_tree = nullptr;
    QTextBrowser *m_preview = nullptr;
    QLabel *m_status = nullptr, *m_header = nullptr, *m_empty = nullptr;
    QWidget *m_emptyRow = nullptr;
    QPushButton *m_searchAll = nullptr, *m_clearFilters = nullptr;
    QPushButton *m_resume = nullptr, *m_newPane = nullptr, *m_info = nullptr, *m_rename = nullptr,
                *m_pin = nullptr, *m_delete = nullptr, *m_more = nullptr, *m_summarise = nullptr,
                *m_cancelBatch = nullptr, *m_reopen = nullptr;
    QFrame *m_confirm = nullptr;
    QLabel *m_confirmText = nullptr;
    QJsonArray m_items;
    // A conversation can have two rows (the "Continue" group and its own group), so both are kept.
    QMultiHash<QString, QTreeWidgetItem *> m_rows;
    QHash<QString, QJsonObject> m_overviews;   // what an unfolded row shows, once fetched
    QSet<QString> m_summarising;
    QStringList m_openSessions;
    QHash<QString, QString> m_liveUsage;   // session id → "cpu 12% · mem 3%" (issue #D03W)
    QHash<QString, QPair<QString, qint64>> m_closed;
    QString m_project, m_pendingSelect, m_previewPending, m_batchScope, m_note;
    QString m_previewFor, m_previewHtml;      // the side preview as last filled, and for which row
    int m_nextOffset = -1, m_matches = 0, m_sessions = 0, m_threadCount = 0;
    double m_elapsed = 0;
    QTimer *m_debounce = nullptr, *m_ages = nullptr;
    bool m_filling = false, m_sortChosen = false, m_batchRunning = false;
};

// ----- Ctrl+F find in view ----------------------------------------------------------------

// A one-line find bar for a pane: the terminal scrollback through the backend's search, plus the
// number of matches the worker reports for the pane's saved conversation.
class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(QWidget *parent = nullptr);

    // Returns the number of matches in the terminal; called for every step and every edit.
    std::function<int(const QString &text, bool backwards)> onFind;
    // Ask the worker how often `text` appears in this pane's conversation.
    std::function<void(const QString &text)> onCountConversation;
    // "Show in conversation": open the conversation list scoped to this session.
    std::function<void(const QString &text)> onOpenConversation;
    std::function<void()> onClosed;

    void start(const QString &preset);            // show, focus, select the text
    void setConversationMatches(int matches);     // from the worker's `conversation` event
    void setTerminalSearchable(bool can);
    QString text() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void step(bool backwards);
    void refresh();
    void updateLabel();
    bool eventFilter(QObject *object, QEvent *event) override;

    QLineEdit *m_field = nullptr;
    QLabel *m_label = nullptr;
    QToolButton *m_next = nullptr, *m_previous = nullptr, *m_close = nullptr;
    QPushButton *m_inConversation = nullptr;
    int m_terminalMatches = 0, m_conversationMatches = -1;
    bool m_canSearchTerminal = true;
};

}  // namespace relay::conversations
