// SPDX-License-Identifier: AGPL-3.0-or-later
// Find belongs to the card page too (#9NBZ). A card is a document that can scroll for screens,
// and until this card there was no way to find anything in one: `find.inView` (Ctrl+F) reached
// the window's dispatch, found no terminal pane to search — the Switchboard holds none — and
// fell through the `!pane` guard doing nothing. The board now answers that action itself:
// the card page finds inside its own document, and the list page focuses the filter, which
// `/` already did. This test drives BoardView::openFind() — what the dispatch calls on the
// board under the keyboard — through the whole life of the strip: first match, stepping,
// wrapping, counting, Esc handing the keyboard back, and the list page's answer.
#include "BoardPane.h"

#include <QApplication>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QTextBrowser>
#include <QtTest>
#include <QWidget>

namespace {

QJsonObject config()
{
    const auto json = QByteArrayLiteral(R"({
      "columns": ["inbox", "discussing", "ready", "in-progress", "done"],
      "column_statuses": {"inbox": ["inbox"], "discussing": ["discussing"], "ready": ["ready"],
        "in-progress": ["in-progress"], "done": ["done"]},
      "all_statuses": ["inbox", "discussing", "ready", "in-progress", "done"],
      "tabs": [{"id": "features", "folder": "features"}],
      "autonomy": "auto"
    })");
    return QJsonDocument::fromJson(json).object();
}

QJsonObject row(const QString &id)
{
    return QJsonObject{{"id", id}, {"title", id + QStringLiteral(" card")}, {"type", "work"},
                       {"status", QStringLiteral("inbox")}, {"tab", "features"}, {"rank", "i"},
                       {"path", QStringLiteral("issues/features/") + id + QStringLiteral(".md")}};
}

QJsonObject opened(const QList<QJsonObject> &cards)
{
    QJsonArray items;
    for (const QJsonObject &item : cards)
        items << item;
    return QJsonObject{{"event", "board"}, {"config", config()}, {"cards", items},
                       {"problems", QJsonArray{}}};
}

}  // namespace

class BoardFindTests : public QObject {
    Q_OBJECT

private slots:
    void anOpenCardFindsStepsWrapsCountsAndGivesTheKeyboardBack();
    void theListPageFindsThroughTheFilter();
};

void BoardFindTests::anOpenCardFindsStepsWrapsCountsAndGivesTheKeyboardBack()
{
    QWidget window;
    QHBoxLayout layout(&window);
    relay::BoardView view(QStringLiteral("/tmp/relay-card-find-test"));
    layout.addWidget(&view);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    window.resize(1200, 900);
    window.show();
    // Three needles, one per section, in document order — enough to step, to wrap and to count.
    const QString body = QStringLiteral("# K7Q2 card\n\n## Issue\nneedle first\n\n## Done means\n"
                                        "needle second\n\n## Tests\nneedle third\n");
    view.handleEvent(opened({row(QStringLiteral("K7Q2"))}));
    view.openCard(QStringLiteral("K7Q2"));
    QJsonObject reply{{"event", "board_card"}, {"card_id", QStringLiteral("K7Q2")},
                      {"title", QStringLiteral("K7Q2 card")},
                      {"status", "inbox"}, {"tab", "features"}, {"hash", "h1"},
                      {"path", QStringLiteral("issues/features/K7Q2.md")},
                      {"body", body}, {"issue", "the ask"}, {"issue_heading", "Issue"},
                      {"thread", QJsonArray{}}, {"thread_total", 0}};
    if (!sent.isEmpty())
        reply.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
    view.handleEvent(reply);
    QVERIFY(view.detailOpen());
    auto *document = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    auto *strip = view.findChild<QWidget *>(QStringLiteral("boardCardFindStrip"));
    auto *edit = view.findChild<QLineEdit *>(QStringLiteral("boardCardFind"));
    auto *count = view.findChild<QLabel *>(QStringLiteral("boardCardFindCount"));
    QVERIFY(document != nullptr);
    QVERIFY(strip != nullptr);
    QVERIFY(edit != nullptr);
    QVERIFY(count != nullptr);
    QVERIFY(strip->isHidden());   // an unread card keeps its whole first screen

    // What the window's dispatch does on `find.inView` when the board has the keyboard.
    view.openFind();
    QTRY_VERIFY(!strip->isHidden());
    QTRY_COMPARE(QApplication::focusWidget(), edit);
    edit->setText(QStringLiteral("needle"));
    QTRY_VERIFY(!document->textCursor().selectedText().isEmpty());
    QCOMPARE(document->textCursor().selectedText(), QStringLiteral("needle"));
    QCOMPARE(count->text(), QStringLiteral("3 matches"));
    const int first = document->textCursor().position();

    // Enter steps forward through the card's own matches, in document order.
    QTest::keyClick(edit, Qt::Key_Return);
    QVERIFY(document->textCursor().position() > first);
    const int second = document->textCursor().position();
    QTest::keyClick(edit, Qt::Key_Return);
    QVERIFY(document->textCursor().position() > second);

    // The last match is not the end of the search: one more Enter wraps to the first.
    QTest::keyClick(edit, Qt::Key_Return);
    QCOMPARE(document->textCursor().position(), first);

    // Shift+Enter steps back the same way, wrapping through the top.
    QTest::keyClick(edit, Qt::Key_Return, Qt::ShiftModifier);
    QVERIFY(document->textCursor().position() > first);   // back to the third match

    // A needle that is not in the card says so, and moves nothing.
    edit->setText(QStringLiteral("nothing-of-the-sort"));
    QTRY_COMPARE(count->text(), QStringLiteral("No matches"));
    QVERIFY(document->textCursor().selectedText().isEmpty());

    // Esc closes the strip and hands the keyboard back to the card being read.
    edit->setText(QStringLiteral("needle"));
    QTRY_VERIFY(!document->textCursor().selectedText().isEmpty());
    QTest::keyClick(edit, Qt::Key_Escape);
    QVERIFY(strip->isHidden());
    QTRY_COMPARE(QApplication::focusWidget(), document);
}

void BoardFindTests::theListPageFindsThroughTheFilter()
{
    QWidget window;
    QHBoxLayout layout(&window);
    relay::BoardView view(QStringLiteral("/tmp/relay-card-find-list-test"));
    layout.addWidget(&view);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    window.resize(1200, 900);
    window.show();
    view.handleEvent(opened({row(QStringLiteral("K7Q2"))}));
    QCoreApplication::processEvents();
    QVERIFY(!view.detailOpen());

    // On the list page the find is the filter the `/` key already focuses (#9NBZ): same
    // action, and the card list's own full-text search takes the keyboard.
    view.openFind();
    auto *filter = view.findChild<QLineEdit *>(QStringLiteral("boardFilter"));
    QVERIFY(filter != nullptr);
    QTRY_COMPARE(QApplication::focusWidget(), filter);
}

QTEST_MAIN(BoardFindTests)
#include "boardfind_test.moc"
