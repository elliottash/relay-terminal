// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ReviewPane.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>

class ReviewPaneTests : public QObject {
    Q_OBJECT
private slots:
    void onlyActionableCardsEnterTheQueue();
    void answerUsesTheRevisionRead();
};

void ReviewPaneTests::onlyActionableCardsEnterTheQueue()
{
    relay::ReviewPane pane;
    QJsonObject sent;
    pane.onSend = [&](const QJsonObject &message) { sent = message; };
    pane.requestList();
    QCOMPARE(sent.value(QStringLiteral("type")).toString(), QStringLiteral("board_open"));
    pane.handleEvent({{"event", "board"},
                      {"cards", QJsonArray{
                          QJsonObject{{"id", "LOW2"}, {"title", "Read a result"}, {"review_priority", 4}},
                          QJsonObject{{"id", "NONE"}, {"title", "Machine checked"}, {"review_priority", 0}},
                          QJsonObject{{"id", "HIGH"}, {"title", "Approve publication"}, {"review_priority", 13}}
                      }}});
    QCOMPARE(pane.queueSize(), 2);
    QCOMPARE(pane.selectedCard(), QStringLiteral("HIGH"));
    QCOMPARE(sent.value(QStringLiteral("type")).toString(), QStringLiteral("board_card_get"));
    auto *queue = pane.findChild<QListWidget *>(QStringLiteral("reviewQueue"));
    QVERIFY(queue);
    QCOMPARE(queue->item(0)->data(Qt::UserRole).toString(), QStringLiteral("HIGH"));
    QCOMPARE(queue->item(1)->data(Qt::UserRole).toString(), QStringLiteral("LOW2"));
}

void ReviewPaneTests::answerUsesTheRevisionRead()
{
    relay::ReviewPane pane;
    QList<QJsonObject> sent;
    pane.onSend = [&](const QJsonObject &message) { sent << message; };
    pane.handleEvent({{"event", "board"},
                      {"cards", QJsonArray{QJsonObject{{"id", "K7Q2"}, {"title", "Research result"},
                                                        {"review_priority", 9}}}}});
    pane.handleEvent({{"event", "board_card"}, {"id", "review-card-K7Q2"}, {"card_id", "K7Q2"},
                      {"title", "Research result"}, {"hash", "abc123456789def"},
                      {"body", "## Issue\nCheck the estimate\n\n"
                               "## Done means\nThe estimate is robust on the held-out sample.\n\n"
                               "## Execution Summary\n- Estimated effect: 0.18\n"
                               "- Held-out interval: [0.11, 0.25]\n\n"
                               "## Human QA\n1. Is the estimate convincing?\n"},
                      {"front", QJsonObject{{"verify", QJsonObject{{"human", "required"},
                                                                    {"criteria", "the estimate is convincing"},
                                                                    {"sign_off", "none"}}},
                                             {"links", QJsonObject{{"evidence", QJsonArray{
                                                 QStringLiteral("reports/held-out-check.md")}}}}}}});
    auto *answer = pane.findChild<QLineEdit *>(QStringLiteral("reviewAnswer"));
    auto *submit = pane.findChild<QPushButton *>(QStringLiteral("reviewSubmit"));
    QVERIFY(answer && submit);
    QVERIFY(answer->isVisibleTo(&pane));
    const QByteArray screenshotDir = qgetenv("RELAY_REVIEW_SCREENSHOTS");
    if (!screenshotDir.isEmpty()) {
        pane.resize(780, 710);
        pane.show();
        QCoreApplication::processEvents();
        QDir().mkpath(QString::fromLocal8Bit(screenshotDir));
        QVERIFY(pane.grab().save(QString::fromLocal8Bit(screenshotDir)
                                     + QStringLiteral("/01-review-queue.png")));
    }
    answer->setText(QStringLiteral("Yes, after inspecting the examples"));
    submit->click();
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_update"));
    QCOMPARE(sent.last().value(QStringLiteral("base_hash")).toString(), QStringLiteral("abc123456789def"));
    const QString human = sent.last().value(QStringLiteral("patch")).toObject()
                              .value(QStringLiteral("replace_section")).toObject()
                              .value(QStringLiteral("text")).toString();
    QVERIFY(human.contains(QStringLiteral("Answer: Yes, after inspecting the examples")));
    QVERIFY(human.contains(QStringLiteral("reviewed revision abc123456789")));
    pane.handleEvent({{"event", "error"}, {"id", "review-write-K7Q2"},
                      {"text", "This card changed; read it again."}});
    QCOMPARE(answer->text(), QStringLiteral("Yes, after inspecting the examples"));
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_card_get"));
}

QTEST_MAIN(ReviewPaneTests)
#include "reviewpane_test.moc"
