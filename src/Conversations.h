// SPDX-License-Identifier: AGPL-3.0-or-later
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

// The helper agent this pane embeds is a `relay::agent::Context` and a console the window
// builds for it (card #AGNT step 7). The header is QtCore-only by design.
#include "AgentContext.h"
#include <QString>
#include <functional>

class QAction;
class QCheckBox;
class QComboBox;
class QFont;
class QFrame;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QResizeEvent;
class QTabWidget;
class QStackedWidget;
class QTextBrowser;
class QTextDocument;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace relay {
}

namespace relay::conversations {

// What this pane's helper agent is about: the query, the filters in force and the row that is
// selected. Defined in Conversations.cpp (card #AGNT step 7).
class SessionsContext;

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

// Clicking a session-list header sorts by that column (the sort itself is the worker's, so the
// group rows and the paging survive). `nextHeaderSort` is the sort a click on `column` asks for
// next given the list's current sort: each column toggles between its two orders (Updated:
// newest↔oldest, Turns: most↔fewest, Session and Model: A→Z↔Z→A), and a click on a column the
// current sort does not belong to takes that column's first order. `headerSortColumn` is the
// column a sort id shows its arrow in, -1 when it has none ("relevance" ranks matches, not a
// column), and `headerSortOrder` the arrow's direction.
QString nextHeaderSort(int column, const QString &current);
int headerSortColumn(const QString &sort);
Qt::SortOrder headerSortOrder(const QString &sort);

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

// ----- the list's rich-text rows (#MDSG) ----------------------------------------------------

// The rows that draw rich text — a match line, a summary paragraph — lay a QTextDocument out
// twice, once to answer sizeHint and once to paint, and the list is thrown away and rebuilt on
// every keystroke. On the owner's store that was 100–190 ms of GUI time per key, all of it in
// Qt's text engine. So the laid-out documents are kept here between rebuilds.
//
// The key is the html, the width it is laid out at and the font: the only three things the
// layout depends on. The ink is not part of it — the delegate passes the row's colour in the
// paint context, so a selected row and a theme's colours reuse the same document. What does
// invalidate an entry is a change of font or of the html itself, and both change the key;
// `clear()` is called anyway when the tree's font, palette or style changes, because a style
// sheet can reach the text through neither.
class RichTextCache {
public:
    explicit RichTextCache(int capacity = 512);
    ~RichTextCache();
    RichTextCache(const RichTextCache &) = delete;
    RichTextCache &operator=(const RichTextCache &) = delete;

    // The document for `html`, laid out at `width` in `font`. Never null; owned by the cache and
    // valid until the cache is cleared or enough other documents have pushed it out.
    QTextDocument *document(const QString &html, int width, const QFont &font);
    void clear();
    int size() const;
    int capacity() const { return m_capacity; }
    // Laid out again, or handed back: what the fix is measured by, and what the test reads.
    int hits() const { return m_hits; }
    int misses() const { return m_misses; }

private:
    struct Entry {
        QTextDocument *document = nullptr;
        quint64 used = 0;          // the stamp the least-recently-used eviction compares
    };
    QHash<QString, Entry> m_entries;
    quint64 m_clock = 0;
    int m_capacity, m_hits = 0, m_misses = 0;
};

// ----- guest sessions (protocol 26.7) ------------------------------------------------------
//
// claude and codex sessions are listed beside Relay's own. Relay never loads one: it runs the
// tool's own resume argv (`resume_command`, or `fork_command` for Ctrl+Enter) in a pane whose
// working directory is the session's own (`resume_cwd`), because both guests resolve a session id
// against the directory they start in.

// True for a row of one of the guest sources ("claude", "codex").
bool isGuestSource(const QString &source);
bool isGuestItem(const QJsonObject &item);
// "Claude Code" / "Codex" for a guest source; the raw id when it is not one.
QString guestLabel(const QString &source);
// One argv word quoted for a POSIX shell ("" for an empty word).
QString shellWord(const QString &word);
// The shell line that resumes (or, with `fork`, forks) a guest row: its argv, each word quoted.
// Empty when the row is not a guest's or carries no argv.
QString guestCommand(const QJsonObject &item, bool fork = false);
// The directory that command must run in; empty when the transcript named none, and the pane then
// keeps its own.
QString guestCwd(const QJsonObject &item);

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
// one pane, opened by /resume, Ctrl+Shift+M, /conversations and the palette. Other features can add
// tabs beside the list (addTab), e.g. recently closed windows, tabs and panes.
//
// At the bottom of it sits the **helper agent** (#FEJQ), collapsed to one "Helper Agent" row at
// the bottom right: since card #AGNT step 7 that is an embedded agent console — a `Pane` with no
// shell — over the `SessionsContext` this pane owns. The manager still never talks to a worker
// itself: it says what the agent is about and the window builds the console, exactly as the
// list's own search goes out through `onQuery` — so the pane is testable without one.
class SessionManager : public QWidget, public relay::PaneView {
    Q_OBJECT
public:
    explicit SessionManager(QWidget *parent = nullptr);
    ~SessionManager() override;

    // Asked whenever the query or a filter changes. The object is the `conversations` request
    // body without "type"/"id": {query, scope, model, has_open_tasks, since, sources, limit,
    // include_threads, sort, offset}.
    std::function<void(const QJsonObject &request)> onQuery;
    std::function<void(const QString &sessionId, const QString &query)> onPreview;
    // The whole result row; Resume requests a new pane unless the window finds it open already.
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
    // The projects Relay knows (card #916B), as {name, folder} pairs, most recently attached first:
    // the "Project" chooser lists them between "Any project" and "No project". A chosen folder goes
    // on the request as `project`; "No project" sends every folder as `outside_projects`
    // (protocol 14.3), so the worker answers with rows under none of them.
    void setKnownProjects(const QList<QPair<QString, QString>> &projects);
    // Empty path selects No project; browsing this filter never attaches a tab.
    void selectProject(const QString &path);
    void setOpenSessions(const QStringList &sessionIds);
    // What each open conversation's pane is using, as a labelled tag on its row (issue #D03W).
    // Pushed by the window's status poll; a session with no entry shows nothing.
    void setLiveUsage(const QHash<QString, QString> &usage);
    void setClosedSessions(const QHash<QString, QPair<QString, qint64>> &closed);
    // "closed 5 min ago" is a clock, not a stamp: the pane re-reads it on a timer while anything
    // is in the closed list, so a row that said "closed just now" catches up without the list
    // being rebuilt or the worker asked again. Public so a test can tick it by hand.
    void refreshClosedAges();

    // ----- the helper agent (#FEJQ; a console since card #AGNT step 7) ------------------------
    //
    // "When you are in options, actions, or sessions, you have a helper agent, same as the
    // switchboard agent" (owner). It is no longer a panel of its own: it is an embedded **agent
    // console** — a `Pane` with no shell, the same prompt box, queue and transcript a terminal
    // pane has — over a `relay::agent::Context` this pane owns. The context is what the agent is
    // *about*: the query, the filters, the selected row, and where a `session:` link in an answer
    // goes (§33, src/AgentContext.h).
    //
    // The pane cannot build the console itself — `Pane` exists only inside the `relay`
    // executable's translation unit — so the **window** sets `onCreateConsole` and gets back a
    // `relay::agent::ConsoleHandle`. With no factory there is no helper row at all and the rest of
    // the pane works unchanged, which is what keeps this library and its tests free of a window.
    //
    // The seam is the Options pane's, name for name (src/SettingsPane.h), so the window wires both
    // with the same lines.
    relay::agent::ConsoleFactory onCreateConsole;

    // Where this console's conversation is kept: the tab's id, sent as `persist {scope: "helper",
    // key}` (§33). Unset means no store.
    void setHelperTabId(const QString &tabId);
    // The project this pane's agent works in. Empty is a supported state, not an error.
    void setHelperWorkspace(const QString &workspace);
    // The key that opens the console, in the window's live Keymap wording, for the collapsed row's
    // text (WARP.md's standing rule: the live Keymap text, never a written-down key). `hintId` is
    // the hint a *click* on that row earns; the pane cannot show a toast, so it calls `onHelperHint`
    // and the window does. The key path teaches nothing — somebody who pressed it knows it.
    void setHelperShortcut(const QString &hintId, const QString &keys);
    std::function<void()> onHelperHint;
    std::function<void()> onResumeHint;
    // Open the helper and put the cursor in it — the pane's ask key, and what a click on the row
    // does. `helperDraft` prefills the composer without sending.
    void focusHelper();
    void helperDraft(const QString &text);
    // What the window needs to reach the console it made, and what a test reads.
    const relay::agent::ConsoleHandle &agentConsole() const { return m_console; }
    relay::agent::Context *agentContext() const;

    void focusSearch();
    QString query() const;
    void setQuery(const QString &text);
    // Re-run the current query (also done whenever the pane is shown).
    void refresh();
    bool threadsShown() const;
    void setThreadsShown(bool on);

    // Tabs beside the list. The list is the tab "sessions"; addTab puts a widget (owned by the
    // pane from then on) after it, and showTab brings one to the front. Unknown ids are ignored.
    // The reserved "closed" and "background" pages are buttons within Sessions, not tabs.
    void addTab(const QString &id, const QString &label, QWidget *widget);
    void insertTab(int index, const QString &id, const QString &label, QWidget *widget);
    std::function<void(const QString &)> onTabActivated;
    std::function<void(const QString &)> onTabSelectedByUser;
    std::function<QString(const QString &)> onTabScreen;
    void showTab(const QString &id);
    QString currentTab() const;

    QString paneTitle() const override;
    void focusView() override;
    void setHeaderRightInset(int pixels) override;

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void requery();
    void requestMore();
    void scheduleQuery();
    QJsonObject queryRequest() const;
    void rebuildTree(const QString &keep);
    QTreeWidgetItem *addSessionRow(QTreeWidgetItem *parent, const QJsonObject &item);
    // A group row or a quick-look placeholder spans the whole width. While the list is being
    // filled the request is held back and every span is set at the end (#MDSG, see the .cpp).
    void spanFirstColumn(QTreeWidgetItem *row);
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
    // The header arrow for the sort the list is in (or none, for "relevance").
    void updateSortIndicator();
    QString scopeId() const;
    QString selectedId() const;
    QJsonObject selectedItem() const;
    // The `session:<id>` link in a helper's answer: select that row, or search for it when the
    // query in force does not draw it.
    void revealSession(const QString &sessionId);
    // What the agent is looking at, for the `screen` hint on each ask (§33): the query, the
    // filters in force and the row that is selected. Written here rather than in the context
    // because it is the pane's own widgets that answer it.
    QString agentScreen() const;
    // The helper's own half: the collapsed row, the console under it, and the fold between.
    void buildHelperRow(QVBoxLayout *into);
    void ensureConsole();               // builds it, once, on the first expand
    void applyHelperCollapsed();
    void updateHelperRow();             // the live key in the button's own text
    void updateConsoleHeight();         // ~40 % of the pane, never less than a few lines
    void rename();
    void togglePin();
    void remove();
    void updateButtons();
    bool eventFilter(QObject *object, QEvent *event) override;

    QTabWidget *m_tabs = nullptr;
    QStackedWidget *m_sessionPages = nullptr;
    QWidget *m_closedPage = nullptr;
    QWidget *m_backgroundPage = nullptr;
    QPushButton *m_recentlyClosed = nullptr;
    QPushButton *m_background = nullptr;
    // ----- the helper agent (#FEJQ, card #AGNT step 7) ----------------------------------
    SessionsContext *m_context = nullptr;   // owned; outlives the console, as §33 requires
    relay::agent::ConsoleHandle m_console;
    QWidget *m_helper = nullptr;            // the foot of the pane: the row and the console
    QWidget *m_askRow = nullptr;            // collapsed: one button, bottom right
    QWidget *m_helperBody = nullptr;        // expanded: the fold row and the console
    QLabel *m_helperHead = nullptr;
    QString m_askKeys;
    QString m_askHintId;   // the hint a click on the row earns (onHelperHint)
    bool m_helperCollapsed = true;
    QWidget *m_inset = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_scope = nullptr, *m_model = nullptr, *m_date = nullptr, *m_kind = nullptr,
              *m_sort = nullptr, *m_branch = nullptr, *m_group = nullptr, *m_projectFilter = nullptr;
    QList<QPair<QString, QString>> m_knownProjects;   // {name, folder}, for the "Project" chooser
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
    QPushButton *m_resume = nullptr, *m_info = nullptr, *m_rename = nullptr,
                *m_pin = nullptr, *m_delete = nullptr, *m_more = nullptr, *m_summarise = nullptr,
                *m_cancelBatch = nullptr, *m_reopen = nullptr;
    QFrame *m_confirm = nullptr;
    QLabel *m_confirmText = nullptr;
    QJsonArray m_items;
    // A conversation can have two rows (the "Continue" group and its own group), so both are kept.
    QMultiHash<QString, QTreeWidgetItem *> m_rows;
    QList<QTreeWidgetItem *> m_spanRows;       // rows waiting for their span (#MDSG)
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

    // The context reads the query, the filters and the selection through agentScreen(), and
    // reveals a `session:` link through revealSession(); both are the pane's own.
    friend class SessionsContext;
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
