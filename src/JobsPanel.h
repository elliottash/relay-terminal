// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Commands the agent left running: a run_command that outlived its call is a job the agent reads or
// stops later (backend/relay_core/jobs.py). Before this list the user saw such a job only in the
// turn's tool lines, and a dev server the agent forgot lasted until the conversation ended. It sits
// under the prompt box beside the running-agents list and takes layout space the same way, so the
// terminal gives up a few rows instead of being covered. Protocol: `jobs`, `jobs_list`,
// `job_output_get` → `job_output`, `job_stop` (docs/AGENT-SESSIONS-PROTOCOL.md).
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <functional>

namespace relay {

struct JobRow {
    QString id, command;
    bool running = false, stopped = false;
    int exitCode = 0;
    bool hasExitCode = false;
    qint64 elapsedMs = 0;     // as last reported by the worker
    qint64 reportedAt = 0;    // model clock (ms) when elapsedMs arrived
    // "running · 3:12", "exit 0 · 0:41", "stopped · 2:05" (the running-agents list's clock format)
    QString state(qint64 elapsedNow) const;
};

// Pure state: no widgets, so it can be unit tested. The pane feeds it every worker event.
class JobsModel {
public:
    JobsModel();

    std::function<void()> onChanged;
    // A running job ended by itself (not stopped from here): a short status line for the pane.
    std::function<void(const JobRow &row)> onFinished;
    // Replaceable clock for tests (monotonic milliseconds).
    std::function<qint64()> clock;

    // True when the event is the jobs list's and needs no other handling. `reset` and `ready` are
    // observed (the list empties) and passed on.
    bool handle(const QJsonObject &event);

    const QList<JobRow> &rows() const { return m_rows; }
    const JobRow *row(const QString &id) const;
    int runningCount() const;
    bool isEmpty() const { return m_rows.isEmpty(); }
    qint64 elapsedNow(const JobRow &row) const;
    // Finished rows only; a dismissed job stays hidden even when the worker lists it again.
    void dismiss(const QString &id);
    void clearFinished();              // a new user turn
    void clear();

private:
    QList<JobRow> m_rows;
    QSet<QString> m_dismissed;
};

// The list under the prompt box: one row per job. Up/Down move, Enter (or a double click) opens the
// job's output, x or Delete stops a running job or dismisses a finished one, Esc (or Up past the
// first row) leaves. Hidden while empty.
class JobsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit JobsPanel(JobsModel *model, QWidget *parent = nullptr);

    std::function<void(const QString &id)> onOpen;
    // `mouse`: the × was clicked rather than x pressed (the pane shows a shortcut hint).
    std::function<void(const QString &id, bool mouse)> onStop;
    std::function<void()> onExitUp;    // Up past the first row: back to what is above
    std::function<void()> onExit;      // Esc: back to the composer

    void refresh();
    void setAllowed(bool allowed) { m_allowed = allowed; refresh(); }
    void enter();
    QString selectedId() const;
    static constexpr int kMaxVisible = 4;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    int rowHeight() const;
    int firstVisible() const;
    int visibleCount() const;
    int rowAt(const QPoint &pos) const;   // -1 for the header or empty space
    void act(bool mouse);
    JobsModel *m_model;
    int m_selected = 0;
    bool m_allowed = true;
    QTimer m_tick;
};

}  // namespace relay
