// SPDX-License-Identifier: AGPL-3.0-or-later
// The "Recently closed" list widget: newest first, the filter, what unfolding a row shows, and the
// keys. It is fed records and a fake scrollback reader, so no window and no state directory.
#include "ClosedList.h"

#include <QTreeWidget>
#include <QUuid>
#include <QtTest>

using namespace relay::closed;

namespace {

QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QJsonObject pane(const QString &cwd, const QString &scrollback, const QString &session = QString()) {
    QJsonObject body{{"cwd", cwd}, {"workspace", cwd}, {"scrollback", scrollback}};
    if (!session.isEmpty()) body.insert(QStringLiteral("session_id"), session);
    return {{"pane", body}};
}

Record paneRecord(const QString &cwd, const QString &title, const QString &scrollback, qint64 closedAt) {
    Record record;
    record.kind = Record::Pane; record.id = uuid(); record.closedAt = closedAt;
    record.layout = pane(cwd, scrollback, QStringLiteral("s-") + title);
    record.titles = QStringList{title};
    return record;
}

}  // namespace

class ClosedListTest : public QObject {
    Q_OBJECT
private slots:
    void previewTailKeepsTheEnd() {
        QStringList lines;
        for (int i = 0; i < 20; ++i) lines << QStringLiteral("line %1   ").arg(i);
        lines << QString() << QString() << QStringLiteral("$ make") << QString() << QString();
        const QStringList tail = previewTail(lines, 4, 160);
        QCOMPARE(tail, (QStringList{QStringLiteral("line 18"), QStringLiteral("line 19"), QString(), QStringLiteral("$ make")}));
        QCOMPARE(previewTail({QString(200, QLatin1Char('x'))}, 4, 10).first().size(), 10);
        QVERIFY(previewTail({QString(), QStringLiteral("   ")}).isEmpty());
    }

    // The saved text carries SGR and image-row links (#1MGS); the preview shows what they draw.
    void previewTailIsPlainText() {
        const QString saved = QStringLiteral("\x1b[1;31mred\x1b[0m \x1b]8;;relay-image:0/2/4//tmp/a.png\x1b\\")
                              + QChar(0x2800) + QStringLiteral("\x1b]8;;\x1b\\ done\x07");
        QCOMPARE(previewTail({saved}), QStringList{QStringLiteral("red ") + QChar(0x2800) + QStringLiteral(" done")});
    }

    void newestFirstAndFiltered() {
        ListView view;
        const Record older = paneRecord(QStringLiteral("/home/u/relay"), QStringLiteral("Fix the parser"), uuid(), 1000);
        const Record newer = paneRecord(QStringLiteral("/srv/www"), QStringLiteral("Deploy"), uuid(), 2000);
        view.setRecords({older, newer});
        QCOMPARE(view.visibleCount(), 2);
        QCOMPARE(view.selectedId(), newer.id);                 // the newest is on top and selected
        QVERIFY(view.tree()->topLevelItem(0)->text(0).contains(QStringLiteral("Deploy")));
        view.setFilter(QStringLiteral("parser relay"));
        QCOMPARE(view.visibleCount(), 1);
        QCOMPARE(view.selectedId(), older.id);
        view.setFilter(QStringLiteral("nothing like it"));
        QCOMPARE(view.visibleCount(), 0);
        QVERIFY(view.selectedId().isEmpty());
    }

    void unfoldingReadsTheTextOnce() {
        ListView view;
        const QString text = uuid();
        int reads = 0;
        view.readText = [&](const QString &id) {
            ++reads;
            return id == text ? QStringList{QStringLiteral("$ ctest"), QStringLiteral("100% tests passed")} : QStringList();
        };
        view.setRecords({paneRecord(QStringLiteral("/home/u/relay"), QStringLiteral("Tests"), text, 1000)});
        QCOMPARE(reads, 0);                                    // nothing is read for a folded list
        QTreeWidgetItem *row = view.tree()->topLevelItem(0);
        row->setExpanded(true);
        QCOMPARE(reads, 1);
        QTreeWidgetItem *paneRow = row->child(0);
        QVERIFY(paneRow->text(1).contains(QStringLiteral("conversation")));
        QCOMPARE(paneRow->childCount(), 2);
        QCOMPARE(paneRow->child(1)->text(0), QStringLiteral("100% tests passed"));
        QVERIFY(paneRow->isExpanded());                        // a lone pane shows its text at once
        row->setExpanded(false);
        row->setExpanded(true);
        QCOMPARE(reads, 1);
    }

    void aWindowUnfoldsIntoTabsAndPanes() {
        ListView view;
        view.readText = [](const QString &) { return QStringList(); };
        Record window;
        window.kind = Record::Window; window.id = uuid(); window.closedAt = 1000;
        window.tabs = QJsonArray{pane(QStringLiteral("/a"), uuid()),
                                 QJsonObject{{"split", "h"}, {"sizes", QJsonArray{1, 1}},
                                             {"children", QJsonArray{pane(QStringLiteral("/b"), uuid()), pane(QStringLiteral("/c"), uuid())}}}};
        window.tabNames = QStringList{QString(), QStringLiteral("release")};
        view.setRecords({window});
        QTreeWidgetItem *row = view.tree()->topLevelItem(0);
        QVERIFY(row->text(0).contains(QStringLiteral("2 tabs · 3 panes")));
        QCOMPARE(row->childCount(), 2);
        QCOMPARE(row->child(1)->text(0), QStringLiteral("Tab 2 · release"));
        QCOMPARE(row->child(1)->childCount(), 2);
        row->setExpanded(true);
        // No text was saved: the pane says so rather than showing nothing.
        QCOMPARE(row->child(0)->child(0)->childCount(), 1);
        QVERIFY(row->child(0)->child(0)->child(0)->text(0).contains(QStringLiteral("no terminal text")));
    }

    void keysReopenAndDiscard() {
        ListView view;
        const Record one = paneRecord(QStringLiteral("/a"), QStringLiteral("One"), uuid(), 1000);
        const Record two = paneRecord(QStringLiteral("/b"), QStringLiteral("Two"), uuid(), 2000);
        view.setRecords({one, two});
        QStringList reopened, discarded;
        view.onReopen = [&](const QString &id) { reopened << id; };
        view.onDiscard = [&](const QString &id) { discarded << id; };
        QTest::keyClick(view.tree(), Qt::Key_Down);
        QTest::keyClick(view.tree(), Qt::Key_Return);
        QCOMPARE(reopened, QStringList{one.id});
        QTest::keyClick(view.tree(), Qt::Key_Delete);
        QCOMPARE(discarded, QStringList{one.id});
    }

    void typingOnTheRowsFilters() {
        ListView view;
        view.setRecords({paneRecord(QStringLiteral("/a"), QStringLiteral("One"), uuid(), 1000),
                         paneRecord(QStringLiteral("/b"), QStringLiteral("Two"), uuid(), 2000)});
        QTest::keyClicks(view.tree(), QStringLiteral("tw"));
        QCOMPARE(view.visibleCount(), 1);
        QVERIFY(view.tree()->topLevelItem(0)->text(0).contains(QStringLiteral("Two")));
    }

    void aRefreshKeepsThePlace() {
        ListView view;
        view.readText = [](const QString &) { return QStringList{QStringLiteral("text")}; };
        const Record one = paneRecord(QStringLiteral("/a"), QStringLiteral("One"), uuid(), 1000);
        const Record two = paneRecord(QStringLiteral("/b"), QStringLiteral("Two"), uuid(), 2000);
        view.setRecords({one, two});
        view.tree()->setCurrentItem(view.tree()->topLevelItem(1));
        view.tree()->topLevelItem(1)->setExpanded(true);
        const Record three = paneRecord(QStringLiteral("/c"), QStringLiteral("Three"), uuid(), 3000);
        view.setRecords({one, two, three});                    // something else closed meanwhile
        QCOMPARE(view.selectedId(), one.id);
        QVERIFY(view.tree()->topLevelItem(2)->isExpanded());
        QCOMPARE(view.tree()->topLevelItem(2)->child(0)->childCount(), 1);
    }
};

QTEST_MAIN(ClosedListTest)
#include "closedlist_test.moc"
