// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The Profile result pane (card #7BM4, phase 5; design item (c)). A transient pane beside the
// Switchboard — never an overlay — holding one profile: the header line, the summary table, and
// the two things you do with a profile that is not a number you already believe.
//
// The table comes before the flame graph because every profiling product arrived at that
// independently (docs/SWITCHBOARD-TOOLING-RESEARCH.md section 4.3: Sentry's Slowest Functions,
// Pyroscope's Top table, speedscope's own Sandwich), and because Relay has no QtWebEngine the
// table is the view that stays in the app: four columns, name / self / total / share, sorted by
// self, and the flame graph is a line that hands the raw file to `scripts/relay-speedscope`.
//
// One profile is four different things here — the build on this machine, the build on the second
// runner, the Python tests and the app — so the button asks first. `profileTargets()` is that
// menu, and it mirrors `backend/relay_core/profile_protocol.py`'s `TARGETS` so the menu and the
// worker cannot describe different things.
//
// Like the Test suites pane it knows nothing about windows, workers or the board: everything in
// is `handleEvent()`, everything out is a std::function, which is what lets
// tests/profilepane_test.cpp drive the whole thing with no worker and no window.
#include "PaneView.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>

class QAbstractItemModel;
class QLabel;
class QMenu;
class QPushButton;
class QTableView;
class QTextBrowser;

namespace relay::profile {

// One row of the summary table. For a build, `self` and `total` are the same compile time and
// `selfPct` is the share of the summed step time; for a sampled profile they are what they say.
struct ProfileRow {
    QString name;
    QString file;
    int line = 0;
    double self = 0.0;
    double total = 0.0;
    double selfPct = 0.0;
    double totalPct = 0.0;
};

// The menu under the Profile button: an id the wire uses, the label, and the one line that says
// what it does and roughly how long.
struct ProfileTarget {
    QString id;
    QString label;
    QString detail;
};
const QVector<ProfileTarget> &profileTargets();
const ProfileTarget *profileTarget(const QString &id);

// The columns, in order.
enum Column { ColName = 0, ColSelf, ColTotal, ColShare, ColCount };

class ProfileTableModel;

class ProfilePane final : public QWidget, public relay::PaneView {
public:
    explicit ProfilePane(QWidget *parent = nullptr);
    ~ProfilePane() override;

    // ----- relay::PaneView ----------------------------------------------------------------------
    QString paneTitle() const override;          // "Profile: Build (this machine)"
    void focusView() override;
    void setHeaderRightInset(int pixels) override;

    // ----- the one way in -----------------------------------------------------------------------
    // Every worker event the host hears. Events that are not `profile` are ignored, and so is a
    // `profile` event for a target this pane is not showing.
    void handleEvent(const QJsonObject &event);
    // What this pane is waiting for, set the moment the run is asked for so the pane is never
    // nameless. Clears whatever the last run left.
    void startWaitingFor(const QString &target);

    // ----- the only ways out --------------------------------------------------------------------
    // `profile_stop`, in the contract's own spelling.
    std::function<void(const QJsonObject &request)> onSend;
    // "Open flame graph": the host runs `scripts/relay-speedscope <file>`.
    std::function<void(const QString &file)> onOpenFlameGraph;
    // "Attach to card…": the host owns the board, so it owns the card picker and the write.
    // `markdown` is the `## Profile` block; `evidence` is the directory for `links.evidence`.
    std::function<void(const QString &markdown, const QString &evidence)> onAttachToCard;
    // One line for the board's notice area while the run goes, the way Clean up's progress is
    // shown. Empty text means "nothing to say any more".
    std::function<void(const QString &text)> onNotice;

    void stopRun();

    // ----- for the tests, and for the QA screenshots ---------------------------------------------
    QString target() const { return m_target; }
    QString state() const { return m_state; }
    bool running() const { return m_state == QLatin1String("started") || m_state == QLatin1String("progress"); }
    int rowCount() const { return int(m_rows.size()); }
    const QVector<ProfileRow> &rows() const { return m_rows; }
    QString headerText() const;
    QString statusText() const;
    QString markdown() const { return m_markdown; }
    QString evidenceDir() const { return m_out; }
    QString flameFile() const { return m_flame; }
    QTableView *table() const { return m_table; }
    QAbstractItemModel *tableModel() const;
    // The last N progress lines, newest last: the log under the table while a run goes.
    QStringList lines() const { return m_lines; }
    void refreshTheme();

private:
    void buildUi();
    void applySummary(const QJsonObject &summary);
    void updateChrome();
    void appendLine(const QString &line);
    int logTailHeight() const;

    ProfileTableModel *m_model = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_summary = nullptr;
    QTableView *m_table = nullptr;
    QTextBrowser *m_log = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_flameButton = nullptr;
    QPushButton *m_attach = nullptr;

    QVector<ProfileRow> m_rows;
    QStringList m_lines;
    QString m_target;         // the target id, e.g. "build"
    QString m_label;          // its menu label, e.g. "Build (this machine)"
    QString m_state;          // "", "started", "progress", "finished", "stopped", "error"
    QString m_message;
    QString m_line;           // the worker's ready-made header line
    QString m_kind;           // "build" or "profile": which way the table reads
    QString m_out;            // the evidence directory
    QString m_flame;          // the raw file the flame graph opens
    QString m_markdown;       // the `## Profile` block a card records
};

// The target menu, anchored under `anchor`. `chosen` is called with a target id; `stop` is
// offered instead of the app entry's usual line while `running` is true, and is called with no
// argument. The menu deletes itself.
void showTargetMenu(QWidget *anchor, std::function<void(const QString &)> chosen,
                    bool running = false, std::function<void()> stop = {});

}  // namespace relay::profile
