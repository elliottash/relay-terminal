// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Subagents UI (Claude Code style): a model fed by the worker's subagent_* events and the
// running-agents list shown under a pane's composer. See docs/AGENT-SESSIONS-PROTOCOL.md section 8.
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <functional>

namespace relay {

struct SubagentRow {
    QString id, type, description, model, effort;
    // The main agent's todo this subagent works on ("T3"), or empty (card #QHR1). The description
    // then starts with "T3 · ", so the strip, the tab and the ✦ lines all say which task it is.
    QString todoId;
    QString status = QStringLiteral("waiting");   // waiting | running | done | failed | stopped
    QString lastActivity, summary, handoff;
    QStringList warnings;
    bool background = false, tokensEstimated = true, resumed = false;
    int tools = 0;
    qint64 tokens = 0;
    qint64 elapsedMs = 0;     // as last reported by the worker
    qint64 reportedAt = 0;    // model clock (ms) when elapsedMs arrived
    bool live() const { return status == QStringLiteral("waiting") || status == QStringLiteral("running"); }
};

// Pure state: no widgets, so it can be unit tested. Pane feeds it every worker event.
class SubagentModel {
public:
    SubagentModel();

    // A dim one-line notice for the terminal (start, finish, handoff) and the subagent it is
    // about, so the line can link to its tab. Never tool activity.
    std::function<void(const QString &line, const QString &id)> onInline;
    // A subagent reached done, failed or stopped.
    std::function<void(const SubagentRow &row)> onFinished;
    // Rows, counts or main-agent state changed.
    std::function<void()> onChanged;
    // Finished rows were cleared by the strip's rules (a new user prompt, New chat). Not called by
    // clear() (a worker restart), which also runs when a restored window's workers start.
    std::function<void()> onFinishedCleared;
    // subagent_transcript and subagent_event (with its payload) for open transcript views.
    std::function<void(const QString &id, const QJsonObject &event)> onTranscript;
    // Short status-bar text (message delivered, options changed).
    std::function<void(const QString &text)> onStatus;
    // Replaceable clock for tests (monotonic milliseconds).
    std::function<qint64()> clock;

    // Returns true when the event belongs to the subagents UI and needs no other handling.
    // Other events (agent_started, context, reset, ready) are observed and passed on (false).
    bool handle(const QJsonObject &event);

    const QList<SubagentRow> &rows() const { return m_rows; }
    const SubagentRow *row(const QString &id) const;
    int liveCount() const;
    bool isEmpty() const { return m_rows.isEmpty(); }
    qint64 elapsedNow(const SubagentRow &row) const;
    qint64 agentTokens() const;
    bool agentTokensEstimated() const;
    void dismiss(const QString &id);    // finished rows only
    void clearFinished();               // a new user turn
    void clear();                       // worker restarted
    bool mainBusy() const { return m_mainBusy; }
    // "main ctx 38k · agents ~1.2k tok"
    QString tokenSplit() const;
    const QJsonArray &definitions() const { return m_definitions; }
    QStringList definitionWarnings() const { return m_definitionWarnings; }

    static QString formatElapsed(qint64 ms);
    static QString formatTokens(qint64 tokens, bool estimated);
    static QString statusIcon(const QString &status);
    static QString handoffText(const QString &handoff, int wakeups, int maxAutoTurns);

private:
    SubagentRow *find(const QString &id);
    void changed() { if (onChanged) onChanged(); }
    QList<SubagentRow> m_rows;
    QJsonArray m_definitions;
    QStringList m_definitionWarnings;
    bool m_mainBusy = false;
    qint64 m_mainTokens = -1;
};

// The running-agents list under the composer: a `main` row plus one row per subagent.
// Hidden when there are no subagents. Up/Down move, Enter (or a click) opens the subagent's tab, x or Delete stops a
// running agent or dismisses a finished row, m picks the row's model, Esc (or Up past the first row)
// returns to the composer.
class SubagentsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit SubagentsPanel(SubagentModel *model, QWidget *parent = nullptr);

    std::function<void(const QString &id)> onOpen;
    std::function<void(const QString &id)> onStop;
    std::function<void()> onExit;    // give focus back to the composer
    std::function<void()> onBelow;   // Down past the last row: the list beneath (commands the agent left running)
    // The row's model chip was clicked (or m pressed): show a model picker at `at` (global).
    std::function<void(const QString &id, const QPoint &at)> onPickModel;
    // A row was opened with the mouse (a click): teach the keyboard path.
    std::function<void()> onMouseOpen;
    // Folded: the subagent pane is open, so the list is one line ("2 subagents running · Alt+A to
    // open"). Enter or a click on it calls onOpenPane.
    std::function<void()> onOpenPane;

    // Called after the model changed. Visible when allowed and there are subagents.
    void refresh();
    // The owner hides the list while its composer is hidden (a full-screen program has the keys).
    void setAllowed(bool allowed) { m_allowed = allowed; refresh(); }
    // While this pane's subagent pane is open the list folds to one line. `keys` is the live
    // Keymap text of the key that opens the pane (empty when unbound).
    void setFolded(bool folded, const QString &keys = QString());
    bool folded() const { return m_folded; }
    QString foldedText() const;
    // Focus the list with the first subagent row selected.
    void enter();
    int selectedRow() const { return m_selected; }   // 0 = main, 1.. = subagents
    QString selectedId() const;
    static constexpr int kMaxVisible = 5;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

private:
    int rowHeight() const;
    int firstVisible() const;
    int visibleCount() const;
    int rowAt(const QPoint &pos) const;
    void act(bool stop);
    void pickModel(int row);
    SubagentModel *m_model;
    QHash<int, QRect> m_modelChips;   // row index (1.. = subagents) -> model chip, from the last paint
    int m_selected = 1;
    bool m_allowed = true, m_folded = false;
    QString m_foldKeys;
    QTimer m_tick;
};

}  // namespace relay
