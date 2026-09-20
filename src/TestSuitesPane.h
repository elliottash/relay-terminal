// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The Test suites pane (card #7BM4, phase 2; design item (b)). A sibling pane of the Switchboard:
// rows are tests, the wide column is TestGrid's run grid — the last N executions newest-left, one
// small cell each — and beside it Buildkite's columns trimmed to reliability, p50, p95, runs, last
// run and cards. Selecting a row fills the detail below it: the executions with their commit and
// host, the last failure's message and excerpt in mono, the linked cards as clickable `#ID`, and
// the source file.
//
// It knows nothing about windows, workers or the board. Everything in goes through
// handleEvent(); everything out goes through the five std::function seams, which is what lets
// tests/testsuites_test.cpp drive the whole pane with no worker and no window, and what lets the
// later registration step (`ToolPane::Kind`, the opener, listenToHelper/sendToHelper) attach it
// without this file changing. It is a relay::PaneView, so the generic ToolPane constructor hosts
// it exactly as it hosts the Activity pane.
//
// Colours come from relay::theme — the run grid's pass/fail/skip cells are the theme's semantic
// inks, never literals — and the pane re-reads them on themeChanged() (docs/THEMES.md).
#include "PaneView.h"
#include "TestSuitesModel.h"

#include <QJsonObject>
#include <QString>
#include <QSet>
#include <QWidget>
#include <functional>

class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTextBrowser;
class QToolButton;

namespace relay::tests {

class TestTableModel;

// The table's columns, in order. `Grid` is the delegate-painted run grid.
enum Column { ColName = 0, ColGrid, ColReliability, ColP50, ColP95, ColRuns, ColLastRun, ColCards, ColCount };

class TestSuitesPane final : public QWidget, public relay::PaneView {
public:
    explicit TestSuitesPane(QWidget *parent = nullptr);
    ~TestSuitesPane() override;

    // ----- relay::PaneView ----------------------------------------------------------------------
    QString paneTitle() const override;
    void focusView() override;
    void setHeaderRightInset(int pixels) override;

    // ----- the one way in -----------------------------------------------------------------------
    // Every worker event the host hears. Events that are not this pane's are ignored.
    void handleEvent(const QJsonObject &event);

    // ----- the only ways out --------------------------------------------------------------------
    // A request for the worker, exactly as the contract spells it (tests_list, tests_run,
    // tests_history, tests_stop). Left unset, the pane still works — it simply asks for nothing.
    std::function<void(const QJsonObject &request)> onSend;
    // Open this test's source. `line` is 0 when the worker did not say.
    std::function<void(const QString &path, int line)> onOpenFile;
    std::function<void(const QString &cardId)> onOpenCard;
    // "Make a card" / "Attach to card": the host owns the board, so it owns both dialogues.
    std::function<void(const TestRow &row)> onMakeCard;
    std::function<void(const TestRow &row)> onAttachToCard;

    // Ask the worker for the inventory: on open, and whenever the worker reconnects.
    void requestList();

    TestSuitesModel *model() { return &m_model; }
    const TestSuitesModel *model() const { return &m_model; }

    // ----- the row actions, as the buttons and the context menu fire them ------------------------
    void runSelected(int repeatUntilFail = 0);
    void runAll();
    void stopRun();
    void openSelectedSource();
    void makeCardForSelected();
    void attachSelectedToCard();

    // ----- for the tests, and for the QA screenshots ---------------------------------------------
    int visibleRowCount() const;
    QString summaryText() const;
    QString detailText() const;
    QString selectedId() const;
    void selectRow(int index);
    QTableView *table() const { return m_table; }
    QLineEdit *filterBox() const { return m_filter; }
    bool columnVisible(int column) const;
    void setColumnVisible(int column, bool visible);
    // The one-sentence empty state on screen, or an empty string while the table is showing.
    QString emptyStateText() const;
    // The theme changed: re-read every colour this pane paints with.
    void refreshTheme();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void fitGridColumn();
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void buildUi();
    void modelChanged();          // the model moved: table, header, detail and buttons follow
    void updateHeader();
    void updateDetail();
    void updateButtons();
    void updateEmptyState();
    void showMenu(const QPoint &at);
    void send(const QJsonObject &request);
    void sortByColumn(int column);
    const TestRow *selectedRow() const;

    TestSuitesModel m_model;
    TestTableModel *m_rows = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_summary = nullptr;
    QLineEdit *m_filter = nullptr;
    QToolButton *m_display = nullptr;
    QStackedWidget *m_stack = nullptr;
    QSplitter *m_split = nullptr;
    QTableView *m_table = nullptr;
    QSet<int> m_autoHidden;   // columns fitGridColumn() hid for room, not the user
    QSet<int> m_userSet;      // columns the Display menu decided; fitGridColumn() leaves them
    QTextBrowser *m_detail = nullptr;
    QLabel *m_empty = nullptr;
    QPushButton *m_emptyRun = nullptr;
    QPushButton *m_run = nullptr;
    QPushButton *m_repeat = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_source = nullptr;
    QPushButton *m_makeCard = nullptr;
    QPushButton *m_attach = nullptr;
    QString m_selected;           // the test id, so a re-sort keeps the selection
    QString m_askedHistoryFor;
};

}  // namespace relay::tests
