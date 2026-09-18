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
#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
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

    // Worker events.
    void setResults(const QJsonObject &event);
    void setPreview(const QJsonObject &event);
    // Row gone: drop it and re-run the query.
    void removed(const QString &sessionId);

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
    void rebuildTree(const QString &keep);
    void selectionChanged();
    void activate(bool newPane);
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
    QComboBox *m_scope = nullptr, *m_model = nullptr, *m_date = nullptr, *m_kind = nullptr, *m_sort = nullptr;
    QCheckBox *m_open = nullptr, *m_threads = nullptr;
    QTreeWidget *m_tree = nullptr;
    QTextBrowser *m_preview = nullptr;
    QLabel *m_status = nullptr, *m_header = nullptr;
    QPushButton *m_resume = nullptr, *m_newPane = nullptr, *m_info = nullptr, *m_rename = nullptr,
                *m_pin = nullptr, *m_delete = nullptr, *m_more = nullptr;
    QJsonArray m_items;
    QString m_pendingSelect;
    int m_nextOffset = -1;
    double m_elapsed = 0;
    QTimer *m_debounce = nullptr;
    bool m_filling = false;
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
