// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Conversation info (ⓘ, /status; card #Y63Z): what a pane's agent session is — model and provider,
// context, tokens and cost, the session file, times, turns, instructions — and its traceable
// history: the turns in order with every subagent thread shown where it was started, as a link.
// A thread link opens that thread's own history in the same view, with a link back up to its
// owner session. The data comes from the worker's `session_info` event (protocol section 25);
// the view only asks for it (onRequest) and renders it, so it is testable without a pane.
#include "PaneView.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QToolButton>
#include <QWidget>
#include <functional>

class QLabel;
class QTextBrowser;
class QToolButton;
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

// The `session_info` event as HTML for the view's text browser. Links use the relay-info: scheme
// (thread?id=&dir=&owner=, session?id=&dir=, live?agent=&thread=, file?path=). Pure; unit tested.
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

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void navigate(const QJsonObject &request, bool push);
    void linkActivated(const QUrl &url);
    void updateHeader();

    QLabel *m_title = nullptr;
    QToolButton *m_back = nullptr, *m_refresh = nullptr;
    QWidget *m_inset = nullptr;
    QTextBrowser *m_body = nullptr;
    QList<QJsonObject> m_stack;        // requests, oldest first; the last one is on screen
    QJsonObject m_current;             // the last event shown
    QString m_pendingId;
    int m_counter = 0;
};

}  // namespace relay::sessioninfo
