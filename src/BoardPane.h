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
#include <QElapsedTimer>
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
    // Execute (`x`, #XS6Q): open a terminal pane beside the board whose agent is handed `task`
    // with card `id` attached. The board has already moved the card to In progress.
    std::function<void(const QString &id, const QString &task)> onExecuteCard;
    // Verify (`v`, #T71W): open a terminal pane beside the board on `runner` — "guest:codex",
    // "guest:claude" or "preset:<id>", the verifier the worker recommends for this card — and hand
    // it `task`, the QA brief. The card keeps its QA status: only the verifier's verdict moves it.
    std::function<void(const QString &id, const QString &runner, const QString &task)> onVerifyCard;
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

    // Which sections are folded, for the layout node. A new pane starts with every section
    // folded; saved panes restore their exact set with the rest of the window's state.
    QJsonArray collapsedSections() const;
    void setCollapsedSections(const QJsonArray &state);
    // By value, not by reference: every caller names a section out of `m_rows`, which the
    // rebuild below replaces.
    void toggleSection(QString columnId);

    // Which sections the checkboxes at the top of the list page are keeping off it:
    // `["deferred", "done"]`, same shape and same home in the layout node as the folded set
    // above. Unchecked, not folded: the section is not on the page at all and its cards are out
    // of the counts. Saved and restored with the rest of the window's state.
    QJsonArray hiddenSections() const;
    void setHiddenSections(const QJsonArray &state);

    // ---- actions, also reachable from the palette and the keymap
    void quickAdd();
    void quickAddIn(const QString &columnId);
    void openSelected();
    void editSelected();             // `e`: edit the open card's title and issue text
    // `p` / `x` / `v` on the open or the selected card: Plan it, Execute it (#XS6Q) or hand it to
    // its recommended verifier (#T71W). "plan", "execute" or "verify"; Discuss is the reply box's
    // Enter.
    void cardAction(const QString &action);
    void closeDetail();
    bool detailOpen() const;
    void focusFilter();
    // "Clean up" (protocol 19.9): hands the whole board to the agent to tidy — merge or split
    // sections and cards, review statuses. The first click is always a **preview** (`dry_run`),
    // because a cleanup rewrites many of the owner's files; the result panel then offers Apply.
    // While a run is going the same button is Stop and sends `cancel`.
    void requestCleanup();
    bool cleanupRunning() const { return !m_cleanupRun.isEmpty(); }
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
    // The top of the list page: the count, the filter, "+ New card", "Clean up", and under them
    // one checkbox per section. They belong to the list, not to the pane's header, so an open
    // card is not looking at the list's tools (owner, 2026-09-18).
    void buildListTools(QVBoxLayout *layout);
    // The cleanup's result, in the list page itself rather than over it: outcome, counts, every
    // change with its card as a link, the refusals, the agent's report and the changelog.
    void buildCleanupPanel(QVBoxLayout *layout);
    void startCleanup(bool dryRun);
    void endCleanup();                        // the run is over, whatever ended it
    void updateCleanupButton();
    void showCleanupSummary(const QJsonObject &summary);
    void hideCleanupPanel();
    // One line about the run in the notice area: the step it is on and what it has written or
    // proposed so far. It stays up until the run ends — a cleanup takes minutes.
    void showCleanupProgress(const QString &step);
    // A turn event, a board_activity or a summary belonging to a cleanup (19.9: `cleanup: true`,
    // a `run_id` and never a `card_id`). Returns true when it was one, so an open card's thread
    // never sees it.
    bool handleCleanupEvent(const QString &type, const QJsonObject &event);
    void buildQuickAdd(QVBoxLayout *layout);
    void closeQuickAdd();
    // One checkbox per section the model has right now, rebuilt only when that set changes.
    void syncSectionChecks();
    void applyRightInset();         // keeps the pane's hover buttons off whichever row is on top
    // The pane's hover buttons take their room out of the top row for good, so in a narrow pane
    // the two buttons drop to a line of their own rather than eliding to "…".
    void layoutListTools();
    void updateCounts();
    void refill();
    void moveCard(const QString &id, const QString &columnId, const QString &beforeId,
                  const QString &afterId);
    void moveToTab(const QString &id, const QString &tabId);
    // One `board_update` for what the card detail's editor changed, against the hash the card
    // was read at. The worker writes the file; a stale hash comes back as `board_conflict`.
    void saveCardEdit(const QJsonObject &patch, const QString &baseHash);
    void executeCard(const QString &note);
    void verifyCard(const QString &note);
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
    // The `issues` directory the worker that owns this view is reading, learned from the `board`
    // event's `root` and then used to refuse events from another project's worker (see
    // handleEvent). Empty while no event has carried one — older workers send none.
    QString m_root;
    board::Model m_model;
    QString m_selected, m_askCard;
    QString m_askText;                  // the question in flight, to put back if it is refused
    QString m_busyCard;                 // the card told "a cleanup is running", to un-tell it
    bool m_open = false;

    // The pane's header row. It holds nothing at all while the list is on screen — the list's
    // own tools sit at the top of the list page — and one "← Back to board" control while a
    // card is open (owner, 2026-09-18).
    QWidget *m_head = nullptr;
    QHBoxLayout *m_tools = nullptr;     // the header row's layout, for the hover-button inset
    QToolButton *m_back = nullptr;
    QHBoxLayout *m_listTools = nullptr; // the count, the filter, "+ New card" and "Clean up"
    QWidget *m_toolsWrapRow = nullptr;  // where those two buttons go when the row is too narrow
    QHBoxLayout *m_toolsWrap = nullptr;
    QToolButton *m_add = nullptr, *m_cleanup = nullptr;
    bool m_toolsWrapped = false;
    QWidget *m_checks = nullptr;        // the section checkboxes, wrapping in a narrow pane
    QLayout *m_checksLayout = nullptr;
    QStringList m_checkIds;             // the sections the boxes stand for, in order
    int m_rightInset = 0;
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

    // ---- the cleanup run (protocol 19.9). One at a time, on the whole board.
    QWidget *m_cleanupPanel = nullptr;      // the result, in the list page
    QLabel *m_cleanupHead = nullptr;
    QTextBrowser *m_cleanupBody = nullptr;
    QToolButton *m_cleanupApply = nullptr, *m_cleanupLog = nullptr, *m_cleanupDismiss = nullptr;
    // Empty when nothing is running. It is set to a placeholder on the click, before
    // `board_cleanup_started` names the real run, so the button says Stop from the click and a
    // card's ask is refused in this pane rather than by the worker.
    QString m_cleanupRun;
    QString m_cleanupRequest;               // the id this pane sent the message under
    QString m_cleanupChangelog;             // where the run says it will write its changelog
    bool m_cleanupDry = false;
    int m_cleanupWrites = 0, m_cleanupCards = 0;
    QElapsedTimer m_cleanupClock;
    // A worker that dies mid-run would otherwise leave the button on Stop for good: the last turn
    // event starts this, and it ends the run if no summary follows.
    QTimer *m_cleanupGuard = nullptr;
    // What the list shows: one entry per visible row, in order, so the widget's row i is this
    // list's entry i. The delegate and the drop logic read it; nothing else holds card data.
    QList<board::Row> m_rows;
    QSet<QString> m_collapsed;          // the folded sections
    QSet<QString> m_hidden;             // the sections whose checkbox is unticked
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
    bool m_editOnOpen = false;      // `e` before the card arrived: start editing it then
    // That edit is a card the quick-add field has just made, whose `## Issue` is the one line
    // typed there. The editor offers that line selected rather than as settled text (#VZ69).
    bool m_editOnOpenFresh = false;
    QString m_actionOnOpen;         // `p` / `x` / `v` before the card arrived: "plan", "execute", "verify"
    QTimer *m_follow = nullptr;     // the open card follows the selection, debounced
    QTimer *m_dragScroll = nullptr; // scrolls the list while a card is dragged near an edge
    // The cards are files: a write from a pane agent, a collaborator's `git pull` or an editor
    // all reach the pane the same way (protocol 19.2, board_refresh).
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_refresh = nullptr;
};

}  // namespace relay
