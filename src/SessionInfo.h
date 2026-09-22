// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Conversation info (ⓘ, /status; card #Y63Z): what a pane's agent session is — model and provider,
// context, tokens and cost, the session file, times, turns, instructions — and its traceable
// history: the turns in order with every subagent thread shown where it was started, as a link.
// A thread link opens that thread's own history in the same view, with a link back up to its
// owner session. The data comes from the worker's `session_info` event (protocol section 25);
// the view only asks for it (onRequest) and renders it, so it is testable without a pane.
//
// At the foot of the pane, the "Ask" row (#FEJQ): this pane has no helper agent of its own — it
// is about the owning pane's agent, and that agent answers for it — so the row drafts a question
// about what is on screen into that pane's prompt box. src/AskRow.h holds the wording.
#include "AskRow.h"
#include "PaneView.h"

#include <QDateTime>
#include <QFrame>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QToolButton>
#include <QWidget>
#include <functional>

class QLabel;
class QShowEvent;
class QTextBrowser;
class QToolButton;
class QTimer;
class QUrl;

namespace relay::sessioninfo {

// The pane header's info button: a circle with an i in it, painted (not a glyph from a font, so it
// looks the same with every font and never turns into a colour emoji).
class InfoButton final : public QToolButton {
public:
    explicit InfoButton(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};

// The low-frequency controls behind the header's circle-i. It is a child of the pane rather than
// a Qt::Popup window: hovering it must not steal focus from the terminal, and it must remain
// reachable while the pointer crosses the small gap below the button.
class PaneInfoPopover final : public QFrame {
public:
    PaneInfoPopover(QWidget *pane, QWidget *anchor, const QString &paneId);

    std::function<void()> onToggleDim;
    QToolButton *dimButton() const { return m_dim; }
    QString paneId() const { return m_paneId; }
    void setDimState(int amount, bool manual);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void showAtAnchor();
    void scheduleClose();
    bool pointerOrFocusInside() const;

    QWidget *m_anchor = nullptr;
    QLabel *m_id = nullptr;
    QToolButton *m_copy = nullptr;
    QToolButton *m_dim = nullptr;
    QTimer *m_closeTimer = nullptr;
    QTimer *m_copyTimer = nullptr;
    QString m_paneId;
};

// The `session_info` event as HTML for the view's text browser. Links use the relay-info: scheme
// (thread?id=&dir=&owner=, session?id=&dir=, live?agent=&thread=, file?path=, copy?text=&what=).
// Pure; unit tested.
QString renderInfo(const QJsonObject &info, const QDateTime &now);
// "41.2k", "1.3M", "812"
QString compactNumber(qint64 value);
// The key/value pairs of a relay-info: link, decoded.
QHash<QString, QString> linkQuery(const QUrl &url);

class InfoView final : public QWidget, public relay::PaneView {
public:
    explicit InfoView(QWidget *parent = nullptr);

    // A `session_info` request body without "type" (the pane adds it and sends it to its worker).
    // Its "id" is the view's; the matching event comes back through setInfo().
    std::function<void(const QJsonObject &request)> onRequest;
    // A thread that is still running in this pane's worker: open its live transcript.
    std::function<void(const QString &agentId, const QString &threadId)> onOpenLive;
    std::function<void(const QString &path)> onOpenFile;
    std::function<void()> onClose;          // Esc, or the pane chrome's ×
    std::function<void()> onTitleChanged;
    // The Ask row's draft: the question goes into the owning pane's composer, at the cursor, and
    // the composer takes focus (Pane::insertInComposer). A draft the person confirms, never a
    // send. Left unset — a saved layout restoring a view with no owner, a test — the row is not
    // shown at all; the view notices on its own, so the window has only to assign this.
    std::function<void(const QString &text)> onAskOwner;

    // Start over on the pane's own session (the ⓘ button, /status).
    void showLiveSession();
    // Open a saved session or one thread (from the session manager, or a link).
    void showSession(const QString &sessionId, const QString &sessionDir);
    void showThread(const QString &threadId, const QString &sessionDir, const QString &ownerSession = {});
    void back();
    void refresh();
    // Re-ask when the view is on the pane's live session (after a turn ends).
    void refreshIfLive();

    void setInfo(const QJsonObject &event);           // the worker's `session_info`
    void setError(const QString &requestId, const QString &text);
    bool owns(const QString &requestId) const { return !requestId.isEmpty() && requestId == m_pendingId; }

    QString paneTitle() const override;
    void focusView() override;
    void setHeaderRightInset(int pixels) override;
    QJsonObject current() const { return m_current; }
    // The Ask row, for the tests and for a QA screenshot. It has no Q_OBJECT, so findChild()
    // cannot pick it out of the pane by type.
    relay::askrow::AskRow *askRow() const { return m_ask; }

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void navigate(const QJsonObject &request, bool push);
    void linkActivated(const QUrl &url);
    void updateHeader();
    // What the Ask row says, and whether it is shown at all: its chips carry the figures this view
    // is showing, and they are only live while the view is on the pane's own session.
    void updateAskRow();
    // Show a transient message on the standing hint line, which restores itself two seconds later.
    void flashHint(const QString &message);

    QLabel *m_title = nullptr;
    QToolButton *m_back = nullptr, *m_refresh = nullptr;
    QWidget *m_inset = nullptr;
    QTextBrowser *m_body = nullptr;
    relay::askrow::AskRow *m_ask = nullptr;   // the Ask row, between the body and the hint line
    QLabel *m_hint = nullptr;         // the standing line under the body; flashHint() borrows it
    QTimer *m_hintTimer = nullptr;    // single shot: brings the standing text back after a flash
    QList<QJsonObject> m_stack;        // requests, oldest first; the last one is on screen
    QJsonObject m_current;             // the last event shown
    QString m_pendingId;
    // The anchor the current left-button press started on, when it started on one. A shaky
    // click on a copy link turns into a drag-selection past the drag threshold, Qt drops the
    // anchor and no anchorClicked arrives; the release rescues the copy (see eventFilter).
    QString m_pressAnchor;
    int m_counter = 0;
};

}  // namespace relay::sessioninfo
