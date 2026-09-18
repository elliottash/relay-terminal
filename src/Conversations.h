// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Conversation list with full-text search (/conversations) and the Ctrl+F
// find-in-view bar. Plain Qt, no KDE dependencies; the worker does the searching
// (docs/AGENT-SESSIONS-PROTOCOL.md section 14).
//
// The dialog never talks to the worker itself: it asks through onQuery/onPreview/... and is fed
// with setResults()/setPreview(), so it can be built and tested without a pane.
#include <QDateTime>
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;
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

// ----- the dialog ------------------------------------------------------------------------

class Dialog : public QDialog {
    Q_OBJECT
public:
    explicit Dialog(QWidget *parent = nullptr);

    // Asked whenever the query or a filter changes. The object is the `conversations` request
    // body without "type"/"id": {query, scope, model, has_open_tasks, since, sources, limit}.
    std::function<void(const QJsonObject &request)> onQuery;
    std::function<void(const QString &sessionId, const QString &query)> onPreview;
    // The whole result row (session_id, session_dir, title, workspace…) plus Shift+Enter.
    std::function<void(const QJsonObject &item, bool newPane)> onResume;
    std::function<void(const QString &sessionId, const QString &title)> onRename;
    std::function<void(const QString &sessionId, bool pinned)> onPin;
    std::function<void(const QString &sessionId)> onDelete;

    // Worker events.
    void setResults(const QJsonObject &event);
    void setPreview(const QJsonObject &event);
    // Row gone: drop it and re-run the query.
    void removed(const QString &sessionId);

    void focusSearch();
    QString query() const;
    // Re-run the current query (also done whenever the dialog is shown).
    void refresh();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void requery();
    void scheduleQuery();
    void selectionChanged();
    void activate(bool newPane);
    QString selectedId() const;
    QJsonObject selectedItem() const;
    void rename();
    void togglePin();
    void remove();
    void updateButtons();
    bool eventFilter(QObject *object, QEvent *event) override;

    QLineEdit *m_search = nullptr;
    QComboBox *m_scope = nullptr, *m_model = nullptr, *m_date = nullptr, *m_kind = nullptr;
    QCheckBox *m_open = nullptr;
    QTreeWidget *m_tree = nullptr;
    QTextBrowser *m_preview = nullptr;
    QLabel *m_status = nullptr, *m_header = nullptr;
    QPushButton *m_resume = nullptr, *m_newPane = nullptr, *m_rename = nullptr,
                *m_pin = nullptr, *m_delete = nullptr;
    QJsonArray m_items;
    QString m_pendingSelect;
    class QTimer *m_debounce = nullptr;
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
