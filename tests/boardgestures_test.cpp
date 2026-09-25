// The browser's new-tab gestures on the Board (#HKY4): middle-click and Ctrl+click dock the
// card in a pane of its own instead of opening it here — on a row in the list, on a `#ID` link
// in a card page's document, and on a skill page's Linked chip. Its own executable rather than
// more of boardpane_test.cpp, which several sessions hold at once (the same reason as
// boardfocus_test.cpp and its siblings); the Live surface's chips middle-click too, and that
// part is exercised by whatever test owns that surface this week (#C52H's).
#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QTextBrowser>
#include <QtTest>

#include "BoardPane.h"

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

QJsonObject row(const QString &id, const QString &status)
{
    return QJsonObject{{"id", id}, {"title", id + QStringLiteral(" card")}, {"type", "work"},
                       {"status", status}, {"tab", "features"}, {"rank", "i"},
                       {"path", QStringLiteral("issues/features/") + id + ".md"}};
}

QJsonObject opened(const QList<QJsonObject> &cards)
{
    QJsonArray items;
    for (const QJsonObject &item : cards)
        items << item;
    return QJsonObject{{"event", "board"}, {("config"), config()}, {"cards", items},
                       {"problems", QJsonArray{}}};
}

// A `board_card` answer (protocol 19.2) minus the request id — whose id it carries is exactly
// what is under test, so the test stamps it by hand.
QJsonObject cardArrived(const QString &id)
{
    return QJsonObject{{"event", "board_card"}, {"card_id", id},
                       {"title", id + QStringLiteral(" card")}, {"status", "inbox"},
                       {"tab", "features"}, {"hash", "h1"},
                       {"path", QStringLiteral("issues/features/") + id + ".md"},
                       {"body", QStringLiteral("# %1 card\n\n## Issue\nthe ask\n").arg(id)},
                       {"issue", QStringLiteral("the ask")}, {"issue_heading", "Issue"},
                       {"thread", QJsonArray{}}, {"thread_total", 0}};
}

}  // namespace

class BoardGesturesTests : public QObject {
    Q_OBJECT

private slots:
    void middleAndCtrlClickDockTheCardInItsOwnPane();
};

// Every docking gesture lands in `ownPanes` (BoardView::onOpenInNewPane) and leaves this pane
// where it was: the list keeps its selection and no card page opens here. A plain click still
// opens the card in place.
void BoardGesturesTests::middleAndCtrlClickDockTheCardInItsOwnPane()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-board-gestures-test"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QStringList ownPanes;
    view.onOpenInNewPane = [&ownPanes](const QString &id) { ownPanes << id; };

    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("ready")),
                             row(QStringLiteral("M3XJ"), QStringLiteral("ready"))}));
    const auto lastOfType = [&sent](const char *type) {
        for (auto it = sent.crbegin(); it != sent.crend(); ++it)
            if (it->value(QStringLiteral("type")).toString() == QLatin1String(type))
                return *it;
        return QJsonObject();
    };
    const auto sentTypes = [&sent] {
        QStringList types;
        for (const QJsonObject &message : sent)
            types << message.value(QStringLiteral("type")).toString();
        return types;
    };
    auto *list = view.findChild<QListWidget *>(QStringLiteral("boardList"));
    QVERIFY(list);
    list->doItemsLayout();
    // A fresh pane starts folded: every section is a count. One click on each header opens it
    // (the same click a user makes), and the card rows appear underneath.
    const auto collect = [list] {
        QList<QListWidgetItem *> cards;
        for (int i = 0; i < list->count(); ++i)
            if (!list->item(i)->data(Qt::UserRole).toString().isEmpty())   // the card id's role
                cards << list->item(i);
        return cards;
    };
    while (collect().isEmpty()) {
        // One header per pass: a refill rebuilds the items, so a pointer held across it is dead.
        bool clickedHeader = false;
        for (int i = 0; i < list->count() && !clickedHeader; ++i) {
            QListWidgetItem *item = list->item(i);
            if (item->data(Qt::UserRole).toString().isEmpty()
                && item->toolTip().contains(QStringLiteral("click to show them"))) {
                QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  list->visualItemRect(item).center());
                clickedHeader = true;
            }
        }
        if (!clickedHeader)
            break;
    }
    const QList<QListWidgetItem *> cards = collect();
    QCOMPARE(cards.size(), 2);

    // Middle-click on the first card's row: docked, and nothing opened here.
    QTest::mouseClick(list->viewport(), Qt::MiddleButton, Qt::NoModifier,
                      list->visualItemRect(cards.at(0)).center());
    QCOMPARE(ownPanes, (QStringList{QStringLiteral("K7Q2")}));
    QVERIFY(!sentTypes().contains(QStringLiteral("board_card_get")));

    // Ctrl+click on the other row: docked too, still nothing opened here.
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      list->visualItemRect(cards.at(1)).center());
    QCOMPARE(ownPanes, (QStringList{QStringLiteral("K7Q2"), QStringLiteral("M3XJ")}));
    QVERIFY(!sentTypes().contains(QStringLiteral("board_card_get")));

    // A plain click still opens the card in this pane, and no third pane was made.
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(cards.at(0)).center());
    QVERIFY(sentTypes().contains(QStringLiteral("board_card_get")));
    QCOMPARE(ownPanes.size(), 2);
    QJsonObject answer = cardArrived(QStringLiteral("K7Q2"));
    answer.insert(QStringLiteral("id"), lastOfType("board_card_get").value(QStringLiteral("id")));
    view.handleEvent(answer);
    auto *meta = view.findChild<QLabel *>(QStringLiteral("boardCardMeta"));
    QVERIFY(meta);

    // Middle-click on a `#ID` link in the card's doc: docked, the page stays. The body names
    // M3XJ, which the board knows, so it is a `card:` anchor (#3ZAP).
    QJsonObject mentioning = cardArrived(QStringLiteral("K7Q2"));
    mentioning["body"] = QStringLiteral("# K7Q2 card\n\n## Issue\nthe ask, see #M3XJ\n");
    mentioning.insert(QStringLiteral("id"),
                      lastOfType("board_card_get").value(QStringLiteral("id")));
    view.handleEvent(mentioning);
    auto *doc = view.findChild<QTextBrowser *>();
    QVERIFY(doc);
    doc->document()->setTextWidth(qMax<qreal>(doc->viewport()->width(), 300));
    // The anchor may sit below the fold of an unshown viewport; scan the whole document.
    QPoint anchor(-1, -1);
    for (int y = 2; anchor.x() < 0 && y < doc->document()->size().height() + 20; y += 2)
        for (int x = 2; x < qMax(doc->viewport()->width(), 200); x += 2)
            if (doc->anchorAt(QPoint(x, y)).startsWith(QLatin1String("card:M3XJ"))) {
                anchor = QPoint(x, y);
                break;
            }
    QVERIFY(anchor.x() >= 0);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(anchor), QPointF(anchor),
                      Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(doc->viewport(), &press);
    QCOMPARE(ownPanes, (QStringList{QStringLiteral("K7Q2"), QStringLiteral("M3XJ"),
                                    QStringLiteral("M3XJ")}));

    // Ctrl+click on a skill page's Linked chip: docked, not zoomed.
    view.closeDetail();
    view.findChild<QAbstractButton *>(QStringLiteral("boardPageTabSkills"))->click();
    QJsonObject skill{{"id", "zz-skill"}, {"name", "zz-skill"}, {"source", "project-relay"},
        {"project", true}, {"path", QStringLiteral("/home/t/.relay/skills/zz-skill/SKILL.md")},
        {"version", "sha256:abc123"}, {"version_short", "abc123"}, {"description", "a skill"},
        {"profile", QJsonObject{}}, {"stats", QJsonObject{{"cases", 0}}},
        {"cases", QJsonArray{}}, {"cards", QJsonArray{QStringLiteral("K7Q2")}},
        {"changelog", QJsonArray{}}, {"profile_warnings", QJsonArray{}}, {"excluded", false}};
    view.handleEvent(QJsonObject{{"event", "skills_registry"}, {"items", QJsonArray{skill}}});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto *chip = view.findChild<QPushButton *>(QStringLiteral("boardLinkedChip"));
    QVERIFY(chip);
    QTest::mouseClick(chip, Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(ownPanes.last(), QStringLiteral("K7Q2"));
}

QTEST_MAIN(BoardGesturesTests)
#include "boardgestures_test.moc"
