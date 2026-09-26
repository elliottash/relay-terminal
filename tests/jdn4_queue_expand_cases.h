// SPDX-License-Identifier: AGPL-3.0-or-later
// Card #JDN4 — a queue line opens to its whole text, in place, on the desktop.
//
// Included by tests/consolemode_test.cpp after pane_waits.h, and run by `--jdn4-only` (ctest
// queueexpand). A real console pane with a stub context: a long prompt is running and a long
// prompt is queued behind it. Deterministic local events only — no provider.
//
// Covered:
//   · The running line and the queued row each offer ▾; ▾ shows the whole message wrapped in
//     place (to its real last words, line breaks kept), ▴ folds it back to one line.
//   · Opening a row neither selects it nor removes it, and a rebuild of the strip (what every
//     state change does) keeps it open.
//   · The same whole text reaches `pane_state` as `full`, past the label's 400 characters.
//   · Evidence shots into RELAY_JDN4_EVIDENCE_DIR: collapsed.png, expanded.png.

#include <QCoreApplication>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QToolButton>

namespace cases {

namespace {
QString jdn4LongText(const QString &tag, int words)
{
    QString text = tag + QStringLiteral(":");
    for (int i = 0; i < words; ++i) text += QStringLiteral(" word%1").arg(i);
    return text + QStringLiteral("\nTHE REAL END OF ") + tag;
}
void jdn4Enter(Pane &pane, const QString &text)
{
    pane.draftInComposer(text);
    if (auto *editor = pane.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"))) {
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &enter);
    }
}
void jdn4Click(QListWidget *list, const QPoint &pos)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), QPointF(list->viewport()->mapToGlobal(pos)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(list->viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), QPointF(list->viewport()->mapToGlobal(pos)),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(list->viewport(), &release);
}
void jdn4Shot(Pane &pane, const char *name)
{
    const QString dir = qEnvironmentVariable("RELAY_JDN4_EVIDENCE_DIR");
    if (dir.isEmpty()) return;
    xcxdPump(200);
    CHECK(pane.grab().save(dir + QLatin1Char('/') + QString::fromLatin1(name)));
}
}   // namespace

void jdn4QueueExpandCases()
{
    QTemporaryDir home;
    StubContext context;
    context.workspace = home.path();
    Pane pane(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    pane.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    pane.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
    pane.resize(900, 700);
    pane.show();
    xcxdPump(100);

    // A long prompt goes to the agent and becomes the running item.
    const QString runningText = jdn4LongText(QStringLiteral("RUNNING"), 120);
    jdn4Enter(pane, runningText);
    QString requestId;
    for (const QJsonObject &message : sent)
        if (message.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("ask-")))
            requestId = message.value(QStringLiteral("id")).toString();
    CHECK(!requestId.isEmpty());
    pane.deliverWorkerEvent(QJsonObject{{"event", "queued"}, {"request_id", requestId}, {"id", "it1"}});
    pane.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "it1"}});
    // A 2,000-character prompt queued behind it.
    const QString queuedText = jdn4LongText(QStringLiteral("QUEUED"), 300);
    CHECK(queuedText.size() > 2000);
    jdn4Enter(pane, queuedText);
    CHECK_EQ(pane.queuedPrompts(), 1);
    xcxdPump(150);

    auto *list = pane.findChild<QListWidget *>(QStringLiteral("queueList"));
    CHECK(list != nullptr);
    if (!list || list->count() < 1) { std::fprintf(stderr, "FAIL jdn4: no queued row\n"); CHECK(false); return; }
    const QFontMetrics fm = list->fontMetrics();
    const int oneLine = QueueRowDelegate::lineHeight(fm);

    // Folded: one line each, and each offers ▾.
    auto *runningToggle = pane.findChild<QToolButton *>(QStringLiteral("queueRunningExpand"));
    CHECK(runningToggle != nullptr);
    if (runningToggle) CHECK_EQ(runningToggle->text(), QStringLiteral("▾"));
    auto *runningLabel = pane.findChild<QLabel *>(QStringLiteral("queueRunning"));
    CHECK(runningLabel && !runningLabel->text().contains(QStringLiteral("THE REAL END")));
    QModelIndex index = list->model()->index(0, 0);
    CHECK(QueueRowDelegate::expandable(list->visualRect(index), index, fm));
    CHECK_EQ(list->visualRect(index).height(), oneLine);
    jdn4Shot(pane, "collapsed.png");

    // ▾ on the row: open, in place, not selected, not removed.
    const QRect rect = QueueRowDelegate::expandRect(list->visualRect(index), index.data(QueueRowDelegate::SendNowRole).toBool(), fm);
    jdn4Click(list, rect.center());
    xcxdPump(100);
    CHECK_EQ(pane.queuedPrompts(), 1);
    index = list->model()->index(0, 0);
    CHECK(index.data(QueueRowDelegate::ExpandedRole).toBool());
    CHECK(list->visualRect(index).height() > 4 * oneLine);
    CHECK(list->selectedItems().isEmpty());
    CHECK(QueueRowDelegate::fullText(index).endsWith(QStringLiteral("THE REAL END OF QUEUED")));

    // ▾ on the running line. Every rebuild replaces the strip's widgets, so each is looked up anew.
    runningToggle = pane.findChild<QToolButton *>(QStringLiteral("queueRunningExpand"));
    CHECK(runningToggle != nullptr);
    if (runningToggle) runningToggle->click();
    xcxdPump(100);
    runningLabel = pane.findChild<QLabel *>(QStringLiteral("queueRunning"));
    CHECK(runningLabel && runningLabel->text().endsWith(QStringLiteral("THE REAL END OF RUNNING")));
    CHECK(runningLabel && runningLabel->wordWrap());
    runningToggle = pane.findChild<QToolButton *>(QStringLiteral("queueRunningExpand"));
    CHECK(runningToggle && runningToggle->text() == QStringLiteral("▴"));
    jdn4Shot(pane, "expanded.png");

    // A state change and a resize rebuild the strip; both stay open.
    pane.deliverWorkerEvent(QJsonObject{{"event", "queue_changed"}, {"running", "it1"}, {"paused", false},
                                        {"items", QJsonArray{}}, {"steering", QJsonArray{}}});
    pane.resize(920, 700);
    xcxdPump(200);
    index = list->model()->index(0, 0);
    CHECK(index.data(QueueRowDelegate::ExpandedRole).toBool());
    runningLabel = pane.findChild<QLabel *>(QStringLiteral("queueRunning"));
    CHECK(runningLabel && runningLabel->text().contains(QStringLiteral("THE REAL END")));

    // The phone is sent the same whole text.
    const relay::panestate::Inputs in = pane.remoteState();
    CHECK(in.runningFull.trimmed().endsWith(QStringLiteral("THE REAL END OF RUNNING")));
    CHECK(!in.rows.isEmpty() && in.rows.constLast().full.endsWith(QStringLiteral("THE REAL END OF QUEUED")));

    // ▴ folds both back to one line.
    runningToggle = pane.findChild<QToolButton *>(QStringLiteral("queueRunningExpand"));
    if (runningToggle) runningToggle->click();
    xcxdPump(100);
    jdn4Click(list, QueueRowDelegate::expandRect(list->visualRect(index), index.data(QueueRowDelegate::SendNowRole).toBool(), fm).center());
    xcxdPump(100);
    index = list->model()->index(0, 0);
    CHECK(!index.data(QueueRowDelegate::ExpandedRole).toBool());
    CHECK_EQ(list->visualRect(index).height(), oneLine);
    runningLabel = pane.findChild<QLabel *>(QStringLiteral("queueRunning"));
    CHECK(runningLabel && !runningLabel->text().contains(QStringLiteral("THE REAL END")));
    CHECK_EQ(pane.queuedPrompts(), 1);

    // × still removes the row.
    jdn4Click(list, QPoint(list->viewport()->width() - 12, list->visualRect(index).center().y()));
    xcxdPump(100);
    CHECK_EQ(pane.queuedPrompts(), 0);
}

}   // namespace cases
