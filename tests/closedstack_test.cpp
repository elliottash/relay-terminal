// SPDX-License-Identifier: GPL-3.0-or-later
// Recently closed panes, tabs and windows: the parts that do not need a window — the record and
// its JSON, state/closed.json, the cap, the scrollback ids the layout's prune must spare, and the
// words a list shows.
#include "ClosedStack.h"

#include "WindowState.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace relay::closed;

namespace {

QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QJsonObject pane(const QString &cwd, const QString &session = QString(), const QString &scrollback = QString()) {
    QJsonObject body{{"cwd", cwd}, {"workspace", cwd}};
    if (!session.isEmpty()) body.insert(QStringLiteral("session_id"), session);
    if (!scrollback.isEmpty()) body.insert(QStringLiteral("scrollback"), scrollback);
    return {{"pane", body}};
}

QJsonObject split(const QJsonArray &children) {
    return {{"split", "h"}, {"children", children}, {"sizes", QJsonArray{500, 500}}};
}

Record paneRecord(const QString &cwd, const QString &session = QString(), const QString &scrollback = QString()) {
    Record record;
    record.kind = Record::Pane;
    record.id = uuid();
    record.closedAt = 1'789'000'000'000;
    record.layout = pane(cwd, session, scrollback);
    return record;
}

}  // namespace

class ClosedStackTest : public QObject {
    Q_OBJECT
private slots:
    void paneRoundTripKeepsItsPlace() {
        Record record = paneRecord(QStringLiteral("/home/u/code"), QStringLiteral("s1"));
        record.orientation = Qt::Vertical;
        record.before = true;
        record.sizes = {300, 700};
        record.slot = 0;
        record.titles = QStringList{QStringLiteral("Fix the parser")};
        Record back;
        QVERIFY(fromJson(toJson(record), &back));
        QCOMPARE(back.kind, Record::Pane);
        QCOMPARE(back.id, record.id);
        QCOMPARE(back.closedAt, record.closedAt);
        QCOMPARE(back.orientation, Qt::Vertical);
        QVERIFY(back.before);
        QCOMPARE(back.sizes, record.sizes);
        QCOMPARE(back.slot, 0);
        QCOMPARE(back.layout, record.layout);
        QCOMPARE(back.titles, record.titles);
    }

    void tabKeepsItsHandSetName() {
        Record record;
        record.kind = Record::Tab;
        record.id = uuid();
        record.index = 2;
        record.layout = split({pane(QStringLiteral("/a")), pane(QStringLiteral("/b"))});
        record.tabNames = QStringList{QStringLiteral("release")};
        Record back;
        QVERIFY(fromJson(toJson(record), &back));
        QCOMPARE(back.index, 2);
        QCOMPARE(back.tabNames, QStringList{QStringLiteral("release")});
        QCOMPARE(label(back), QStringLiteral("release · 2 panes"));
    }

    void windowDropsTabsThatCannotBeRebuilt() {
        Record record;
        record.kind = Record::Window;
        record.id = uuid();
        record.index = 2;
        record.geometry = QRect(10, 20, 800, 600);
        record.tabs = QJsonArray{pane(QStringLiteral("/a")), QJsonObject{}, pane(QStringLiteral("/c"))};
        record.tabNames = QStringList{QStringLiteral("one"), QString(), QStringLiteral("three")};
        record.titles = QStringList{QStringLiteral("x"), QStringLiteral("y")};
        Record back;
        QVERIFY(fromJson(toJson(record), &back));
        QCOMPARE(back.tabs.size(), 2);
        QCOMPARE(back.tabNames, (QStringList{QStringLiteral("one"), QStringLiteral("three")}));
        QCOMPARE(back.index, 1);              // clamped onto the tabs that are left
        QCOMPARE(back.geometry, record.geometry);
        QVERIFY(back.titles.isEmpty());       // they no longer line up with the leaves
        QCOMPARE(label(back), QStringLiteral("2 tabs · 2 panes"));
    }

    void unusableRecordsAreRefused() {
        Record out;
        QVERIFY(!fromJson(QJsonObject{{"kind", "sheet"}, {"id", uuid()}}, &out));
        QVERIFY(!fromJson(QJsonObject{{"kind", "pane"}, {"id", uuid()}, {"layout", QJsonObject{}}}, &out));
        QVERIFY(!fromJson(QJsonObject{{"kind", "window"}, {"id", uuid()}, {"tabs", QJsonArray{}}}, &out));
        // An id is only ever compared, but it comes from a file.
        QJsonObject bad = toJson(paneRecord(QStringLiteral("/a")));
        bad.insert(QStringLiteral("id"), QStringLiteral("../../etc/passwd"));
        QVERIFY(!fromJson(bad, &out));
        // A slot that points outside the sizes is dropped, not trusted.
        QJsonObject odd = toJson(paneRecord(QStringLiteral("/a")));
        odd.insert(QStringLiteral("sizes"), QJsonArray{100, 200});
        odd.insert(QStringLiteral("slot"), 5);
        QVERIFY(fromJson(odd, &out));
        QVERIFY(out.sizes.isEmpty());
        QCOMPARE(out.slot, -1);
    }

    void fileRoundTripIsPrivateAndCapped() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("state/closed.json"));
        QList<Record> records;
        for (int i = 0; i < kMaxItems + 5; ++i) push(&records, paneRecord(QStringLiteral("/p/%1").arg(i)));
        QCOMPARE(records.size(), kMaxItems);
        QCOMPARE(place(records.first(), QString()), QStringLiteral("/p/5"));   // the oldest five went
        QString error;
        QVERIFY2(save(path, records, &error), qPrintable(error));
        QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther),
                 QFile::Permissions());
        const QList<Record> back = load(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(back.size(), kMaxItems);
        QCOMPARE(back.last().id, records.last().id);
        // An empty list removes the file rather than leaving "nothing closed" on disk.
        QVERIFY(save(path, {}, &error));
        QVERIFY(!QFile::exists(path));
        QVERIFY(load(path, &error).isEmpty());
        QVERIFY(error.isEmpty());
    }

    void pushReportsWhatFellOff() {
        QList<Record> records;
        const Record first = paneRecord(QStringLiteral("/first"));
        QVERIFY(push(&records, first, 2).isEmpty());
        QVERIFY(push(&records, paneRecord(QStringLiteral("/second")), 2).isEmpty());
        const QList<Record> dropped = push(&records, paneRecord(QStringLiteral("/third")), 2);
        QCOMPARE(dropped.size(), 1);
        QCOMPARE(dropped.first().id, first.id);
    }

    void damagedFilesAreIgnoredWithAReason() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("closed.json"));
        QString error;
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("{ not json");
        }
        QVERIFY(load(path, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(QJsonDocument(QJsonObject{{"version", kSchemaVersion + 1}, {"closed", QJsonArray{}}}).toJson());
        }
        QVERIFY(load(path, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("version")));
        // One bad record does not cost the good ones.
        {
            QJsonObject doc = document({paneRecord(QStringLiteral("/good"))});
            QJsonArray closed = doc.value(QStringLiteral("closed")).toArray();
            closed.prepend(QJsonObject{{"kind", "pane"}});
            doc.insert(QStringLiteral("closed"), closed);
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(QJsonDocument(doc).toJson());
        }
        const QList<Record> back = load(path, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(back.size(), 1);
        QCOMPARE(place(back.first(), QString()), QStringLiteral("/good"));
    }

    void scrollbackIdsCoverEveryKind() {
        const QString a = uuid(), b = uuid(), c = uuid();
        Record tab;
        tab.kind = Record::Tab; tab.id = uuid();
        tab.layout = split({pane(QStringLiteral("/a"), QString(), a), pane(QStringLiteral("/b"), QString(), b)});
        Record window;
        window.kind = Record::Window; window.id = uuid();
        window.tabs = QJsonArray{pane(QStringLiteral("/c"), QString(), c)};
        const QStringList ids = scrollbackIds({tab, window, paneRecord(QStringLiteral("/d"))});
        QCOMPARE(ids, (QStringList{a, b, c}));
    }

    void anOpenConversationIsNotResumedTwice() {
        Record tab;
        tab.kind = Record::Tab; tab.id = uuid();
        tab.layout = split({pane(QStringLiteral("/a"), QStringLiteral("s1")), pane(QStringLiteral("/b"), QStringLiteral("s2"))});
        QCOMPARE(sessionIds(tab), (QStringList{QStringLiteral("s1"), QStringLiteral("s2")}));
        int removed = 0;
        const Record safe = withoutSessions(tab, {QStringLiteral("s2")}, &removed);
        QCOMPARE(removed, 1);
        QCOMPARE(sessionIds(safe), QStringList{QStringLiteral("s1")});
        // Everything else about the pane is still there.
        QCOMPARE(contents(safe).first().panes.at(1).cwd, QStringLiteral("/b"));
        QCOMPARE(sessionIds(withoutSessions(tab, {}, &removed)).size(), 2);
        QCOMPARE(removed, 0);
    }

    void contentsAndWords() {
        Record record = paneRecord(QStringLiteral("/home/u/code/relay"), QStringLiteral("s1"));
        QCOMPARE(label(record), QStringLiteral("relay"));                    // no title: the directory's name
        record.titles = QStringList{QStringLiteral("Fix the parser")};
        QCOMPARE(label(record), QStringLiteral("Fix the parser"));
        QCOMPARE(place(record, QStringLiteral("/home/u")), QStringLiteral("~/code/relay"));
        QCOMPARE(place(paneRecord(QStringLiteral("/home/u")), QStringLiteral("/home/u")), QStringLiteral("~"));
        QCOMPARE(place(paneRecord(QStringLiteral("/home/user2/x")), QStringLiteral("/home/u")), QStringLiteral("/home/user2/x"));

        Record tab;
        tab.kind = Record::Tab; tab.id = uuid();
        tab.layout = split({pane(QStringLiteral("/a")), QJsonObject{{"explorer", QJsonObject{{"path", "/a/src"}}}}});
        tab.titles = QStringList{QStringLiteral("Build"), QStringLiteral("src")};
        const QList<TabInfo> tabs = contents(tab);
        QCOMPARE(tabs.size(), 1);
        QCOMPARE(tabs.first().panes.size(), 2);
        QCOMPARE(tabs.first().panes.at(1).kind, QStringLiteral("explorer"));
        QCOMPARE(tabs.first().panes.at(1).cwd, QStringLiteral("/a/src"));
        QCOMPARE(label(tab), QStringLiteral("Build; src · 2 panes"));
    }

    void filterMatchesEveryWordAnywhere() {
        Record tab;
        tab.kind = Record::Tab; tab.id = uuid();
        tab.layout = split({pane(QStringLiteral("/home/u/code/relay")), pane(QStringLiteral("/srv/www"))});
        tab.tabNames = QStringList{QStringLiteral("Release")};
        tab.titles = QStringList{QStringLiteral("Fix the parser"), QString()};
        QVERIFY(matches(tab, QString()));
        QVERIFY(matches(tab, QStringLiteral("  ")));
        QVERIFY(matches(tab, QStringLiteral("release")));          // the hand-set name
        QVERIFY(matches(tab, QStringLiteral("PARSER www")));       // a title and a directory, any order
        QVERIFY(matches(tab, QStringLiteral("tab")));              // its kind
        QVERIFY(!matches(tab, QStringLiteral("parser nothing")));  // every word has to be there
    }

    void ageReadsLikeAPerson() {
        const qint64 now = 1'789'000'000'000;
        QCOMPARE(age(now - 5'000, now), QStringLiteral("just now"));
        QCOMPARE(age(now - 5 * 60'000, now), QStringLiteral("5 min ago"));
        QCOMPARE(age(now - 3 * 3'600'000LL, now), QStringLiteral("3 h ago"));
        QCOMPARE(age(now - 30 * 3'600'000LL, now), QStringLiteral("yesterday"));
        QCOMPARE(age(now - 4 * 86'400'000LL, now), QStringLiteral("4 days ago"));
        QVERIFY(age(now - 30 * 86'400'000LL, now).contains(QLatin1Char('-')));
        QVERIFY(age(0, now).isEmpty());
        QCOMPARE(age(now + 60'000, now), QStringLiteral("just now"));   // a clock that went backwards
    }
};

QTEST_APPLESS_MAIN(ClosedStackTest)
#include "closedstack_test.moc"
