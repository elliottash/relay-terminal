// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Switchboard pane: tabs, columns of cards with drag and drop, quick add, filters and a card
// detail view with the body, the `## Tasks` checklist, the links and the thread with a reply box.
// Design: docs/SWITCHBOARD-DESIGN.md section 4. Protocol: docs/AGENT-SESSIONS-PROTOCOL.md 17.
//
// The view holds no process: it hands JSON commands to `onSend` and is fed events through
// `handleEvent`, so it can be driven by a per-window Switchboard worker or, in tests, by hand.
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "BoardModel.h"

class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QSplitter;
class QFileSystemWatcher;
class QTabBar;
class QTextBrowser;
class QToolButton;
class QVBoxLayout;
class RichEditor;

namespace relay {

class CardDetail;

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
    QString currentTab() const;
    void setCurrentTab(const QString &tabId);
    const board::Model &model() const { return m_model; }
    board::Model &model() { return m_model; }

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
    // The notice line over the columns: "Moved #K7Q2 to Ready · Undo", or a refused write.
    QString notice() const;

    // Room kept free at the right of the tab row while the pane's hover buttons sit there.
    void setHeaderRightInset(int pixels);

    // Re-render from the model; public so a test can drive it without the worker.
    void rebuild();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildChrome(QVBoxLayout *layout);
    void rebuildTabs();
    void updateTabCounts();
    QWidget *buildColumn(const board::Column &column);
    void fillList(QListWidget *list, const board::Column &column);
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
    void step(int columns, int rows);
    void reorder(QListWidget *list, int delta);
    bool handleBoardKey(QKeyEvent *key);
    void updateDetailLayout();
    void revealColumn(QWidget *column);
    void autoScrollDuringDrag();
    QListWidget *listFor(const QString &columnId) const;
    QListWidget *listOfCard(const QString &id) const;
    QStringList columnIds() const;

    QString m_workspace;
    board::Model m_model;
    QString m_tab, m_selected, m_askCard;
    bool m_open = false, m_buildingTabs = false;

    QTabBar *m_tabs = nullptr;
    QHBoxLayout *m_tabRow = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *m_problems = nullptr;
    QFrame *m_notice = nullptr;
    QLabel *m_noticeText = nullptr;
    QToolButton *m_noticeUndo = nullptr;
    QTimer *m_noticeTimer = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_keys = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_columns = nullptr;
    QSplitter *m_splitter = nullptr;
    CardDetail *m_detail = nullptr;
    QPointer<QLineEdit> m_quickAdd;
    QString m_quickAddColumn;
    QMap<QString, QListWidget *> m_lists;
    QMap<QString, QLabel *> m_counts;   // column id -> the count beside its header
    // What the column widgets were built for: a change inside the same tab and columns refills
    // the lists in place, which keeps scroll positions, focus and an open quick-add field.
    QString m_builtTab;
    QStringList m_builtColumns;
    // Requests this view sent carry an id with this prefix, so the worker's answer to them (a
    // write, an undo, a refusal) is told apart from another pane's or an agent's.
    QString m_requestPrefix;
    int m_requestSeq = 0;
    QString m_lastWrite;            // write_id of this pane's last write, for Undo
    QHash<QString, QString> m_pendingNotes;   // request id -> "Moved #K7Q2 to Ready"
    // A drag runs a nested event loop inside the source list's startDrag(); rebuilding the
    // columns then would delete that list under it, so a rebuild waits for the drag to end.
    bool m_dragActive = false, m_rebuildPending = false;
    bool m_detailSized = false;     // the split was sized for the open card already
    bool m_replyOnOpen = false;     // `c` before the card arrived: focus its reply box then
    QTimer *m_follow = nullptr;     // the open card follows the selection, debounced
    QTimer *m_dragScroll = nullptr; // scrolls the columns while a card is dragged near an edge
    // The cards are files: a write from a pane agent, a collaborator's `git pull` or an editor
    // all reach the pane the same way (protocol 17.2, board_refresh).
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_refresh = nullptr;
};

}  // namespace relay
