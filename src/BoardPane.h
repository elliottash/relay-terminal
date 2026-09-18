// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Switchboard pane: one scrolling list of every open card, in a collapsible section per
// status, with drag and drop, quick add, a filter box and a card detail view with the body, the
// `## Tasks` checklist, the links and the thread with a reply box. No tabs (owner decision,
// 2026-09-18): "bug" and "feature" are labels, done is a status, and the filter box is the way
// to slice the list.
// Design: docs/SWITCHBOARD-DESIGN.md sections 4 and 4.6. Protocol: docs/AGENT-SESSIONS-PROTOCOL.md 19.
//
// The view holds no process: it hands JSON commands to `onSend` and is fed events through
// `handleEvent`, so it can be driven by a per-window Switchboard worker or, in tests, by hand.
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "BoardModel.h"

class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QSplitter;
class QFileSystemWatcher;
class QTextBrowser;
class QToolButton;
class QVBoxLayout;
class RichEditor;

namespace relay {

class CardDetail;
class RowList;

class BoardView : public QWidget {
public:
    explicit BoardView(const QString &workspace, QWidget *parent = nullptr);

    // ---- wiring
    std::function<void(const QJsonObject &)> onSend;         // a board_* protocol message
    std::function<void(const QString &reference)> onSendToTerminal;  // `t`: insert #ID in the composer
    std::function<void(const QString &path)> onOpenFile;     // `o`: open the card file in a pane
    std::function<void(const QString &)> onTitleChanged;
    std::function<void(const QString &)> onStatus;           // one line for the pane's status area
    std::function<void(const QString &id, const QString &text)> onHint;  // shortcut hints

    void handleEvent(const QJsonObject &event);
    void focusInput();
    void reload();                                           // board_open

    QString workspace() const { return m_workspace; }
    QString title() const;
    const board::Model &model() const { return m_model; }
    board::Model &model() { return m_model; }
    // What the list shows right now, headers and cards in order (design 4.6). Public so a test
    // can read the list without walking widgets.
    const QList<board::Row> &rows() const { return m_rows; }

    // Which sections are folded, for the layout node: `["deferred", "done"]`. Saved and
    // restored with the rest of the window's state.
    QJsonArray collapsedSections() const;
    void setCollapsedSections(const QJsonArray &state);
    // By value, not by reference: every caller names a section out of `m_rows`, which the
    // rebuild below replaces.
    void toggleSection(QString columnId);

    // ---- actions, also reachable from the palette and the keymap
    void quickAdd();
    void quickAddIn(const QString &columnId);
    void openSelected();
    void closeDetail();
    bool detailOpen() const;
    void focusFilter();
    void moveSelected();            // the `m` popup
    void undoLast();                // Ctrl+Z: board_undo of this pane's last write
    void copyReference();
    void sendSelectionToTerminal();
    void openSelectedFile();
    void selectCard(const QString &id);
    QString selectedCard() const { return m_selected; }
    // The notice line over the list: "Moved #K7Q2 to Ready · Undo", or a refused write.
    QString notice() const;

    // Room kept free at the right of the filter row while the pane's hover buttons sit there.
    void setHeaderRightInset(int pixels);

    // Re-render from the model; public so a test can drive it without the worker.
    void rebuild();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildChrome(QVBoxLayout *layout);
    void buildQuickAdd(QVBoxLayout *layout);
    void closeQuickAdd();
    void updateCounts();
    void refill();
    void moveCard(const QString &id, const QString &columnId, const QString &beforeId,
                  const QString &afterId);
    void moveToTab(const QString &id, const QString &tabId);
    void send(QJsonObject message);
    QString nextRequestId();
    void showNotice(const QString &text, bool error, const QString &undoWriteId = QString());
    void placeNotice();
    void watchIssues();
    void showProblems(const QJsonArray &problems);
    void step(int delta);                  // Up/Down, across section breaks
    void reorder(int delta);               // Alt+Shift+Up/Down, inside the section
    void shiftSection(int delta);          // Alt+Shift+Left/Right, to the next status
    void foldSelected();                   // Left
    void unfoldNearest();                  // Right
    void selectRow(int index);
    bool handleBoardKey(QKeyEvent *key);
    void updateDetailLayout();
    void autoScrollDuringDrag();
    QStringList columnIds() const;
    QString sectionTitle(const QString &columnId) const;
    // Where quick add files a new card: the first category folder board.yaml names. With no
    // tabs there is no "current" category, and `m` re-files a card that belongs elsewhere.
    QString defaultCategory() const;
    bool sectionTakesNewCards(const QString &columnId) const;
    QString focusedSection() const;
    int rowHeight() const;

    QString m_workspace;
    board::Model m_model;
    QString m_selected, m_askCard;
    bool m_open = false;

    QHBoxLayout *m_tools = nullptr;     // the count, the filter and "+ New card"
    QLabel *m_count = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *m_problems = nullptr;
    QFrame *m_notice = nullptr;
    QLabel *m_noticeText = nullptr;
    QToolButton *m_noticeUndo = nullptr;
    QTimer *m_noticeTimer = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_keys = nullptr;
    QWidget *m_listPane = nullptr;      // the quick-add field over the list; hidden when stacked
    RowList *m_list = nullptr;
    QSplitter *m_splitter = nullptr;
    CardDetail *m_detail = nullptr;
    QWidget *m_quickAddRow = nullptr;
    QPointer<QLineEdit> m_quickAdd;
    QString m_quickAddColumn;
    // What the list shows: one entry per visible row, in order, so the widget's row i is this
    // list's entry i. The delegate and the drop logic read it; nothing else holds card data.
    QList<board::Row> m_rows;
    QSet<QString> m_collapsed;          // the folded sections
    // Done and Deferred fold themselves the first time the list is drawn, unless the window's
    // saved state has already said which sections are folded.
    bool m_collapsedSeeded = false;
    // Requests this view sent carry an id with this prefix, so the worker's answer to them (a
    // write, an undo, a refusal) is told apart from another pane's or an agent's.
    QString m_requestPrefix;
    int m_requestSeq = 0;
    QString m_lastWrite;            // write_id of this pane's last write, for Undo
    QHash<QString, QString> m_pendingNotes;   // request id -> "Moved #K7Q2 to Ready"
    // A drag runs a nested event loop inside the list's startDrag(); refilling the list there
    // would delete the items under it, so a rebuild waits for the drag to end.
    bool m_dragActive = false, m_rebuildPending = false;
    bool m_detailSized = false;     // the split was sized for the open card already
    bool m_replyOnOpen = false;     // `c` before the card arrived: focus its reply box then
    QTimer *m_follow = nullptr;     // the open card follows the selection, debounced
    QTimer *m_dragScroll = nullptr; // scrolls the list while a card is dragged near an edge
    // The cards are files: a write from a pane agent, a collaborator's `git pull` or an editor
    // all reach the pane the same way (protocol 19.2, board_refresh).
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_refresh = nullptr;
};

}  // namespace relay
