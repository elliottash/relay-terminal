// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Switchboard pane: tabs, columns of cards with drag and drop, quick add, filters and a card
// detail view with the body, the `## Tasks` checklist, the links and the thread with a reply box.
// Design: docs/SWITCHBOARD-DESIGN.md section 4. Protocol: docs/AGENT-SESSIONS-PROTOCOL.md 17.
//
// The view holds no process: it hands JSON commands to `onSend` and is fed events through
// `handleEvent`, so it can be driven by a per-window Switchboard worker or, in tests, by hand.
#include <QJsonObject>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "BoardModel.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QSplitter;
class QFileSystemWatcher;
class QTabBar;
class QTextBrowser;
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
    void openSelected();
    void closeDetail();
    void focusFilter();
    void moveSelected();            // the `m` popup
    void copyReference();
    void sendSelectionToTerminal();
    void openSelectedFile();
    void selectCard(const QString &id);
    QString selectedCard() const { return m_selected; }

    // Re-render from the model; public so a test can drive it without the worker.
    void rebuild();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void buildChrome(QVBoxLayout *layout);
    void rebuildTabs();
    void updateTabCounts();
    QWidget *buildColumn(const board::Column &column);
    void moveCard(const QString &id, const QString &columnId, const QString &beforeId,
                  const QString &afterId);
    void send(const QJsonObject &message);
    void watchIssues();
    void showProblems(const QJsonArray &problems);
    void step(int columns, int rows);
    QListWidget *listFor(const QString &columnId) const;
    QStringList columnIds() const;

    QString m_workspace;
    board::Model m_model;
    QString m_tab, m_selected, m_askCard;
    bool m_open = false, m_buildingTabs = false;

    QTabBar *m_tabs = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *m_problems = nullptr;
    QLabel *m_empty = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_columns = nullptr;
    QSplitter *m_splitter = nullptr;
    CardDetail *m_detail = nullptr;
    QLineEdit *m_quickAdd = nullptr;
    QMap<QString, QListWidget *> m_lists;
    // The cards are files: a write from a pane agent, a collaborator's `git pull` or an editor
    // all reach the pane the same way (protocol 17.2, board_refresh).
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_refresh = nullptr;
};

}  // namespace relay
