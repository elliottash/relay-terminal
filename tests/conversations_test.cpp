// SPDX-License-Identifier: GPL-3.0-or-later
// Pure helpers of the conversation list and the find bar (src/Conversations.h).
#include "Conversations.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>

using namespace relay::conversations;

static const QString kOpen = QStringLiteral("<span style=\"background-color:#f5d76e;color:#101216;\">");
static const QString kClose = QStringLiteral("</span>");

static QJsonArray ranges(std::initializer_list<std::pair<int, int>> items) {
    QJsonArray out;
    for (const auto &item : items) out.append(QJsonArray{item.first, item.second});
    return out;
}

class ConversationsTest : public QObject {
    Q_OBJECT
private slots:
    void kindLabels() {
        QCOMPARE(kindLabel(QStringLiteral("prompt")), QStringLiteral("You"));
        QCOMPARE(kindLabel(QStringLiteral("reply")), QStringLiteral("Agent"));
        QCOMPARE(kindLabel(QStringLiteral("tool_output")), QStringLiteral("Tool output"));
        QCOMPARE(kindLabel(QStringLiteral("command")), QStringLiteral("Command"));
        QCOMPARE(kindLabel(QStringLiteral("command_output")), QStringLiteral("Command output"));
        QCOMPARE(kindLabel(QStringLiteral("future")), QStringLiteral("future"));
        QVERIFY(isTerminalKind(QStringLiteral("command")));
        QVERIFY(isTerminalKind(QStringLiteral("command_output")));
        QVERIFY(!isTerminalKind(QStringLiteral("reply")));
    }

    void highlightsWrapTheRanges() {
        QCOMPARE(highlighted(QStringLiteral("find the needle here"), ranges({{9, 6}})),
                 QStringLiteral("find the ") + kOpen + QStringLiteral("needle") + kClose + QStringLiteral(" here"));
        QCOMPARE(highlighted(QStringLiteral("a needle and a needle"), ranges({{2, 6}, {15, 6}})),
                 QStringLiteral("a ") + kOpen + QStringLiteral("needle") + kClose
                     + QStringLiteral(" and a ") + kOpen + QStringLiteral("needle") + kClose);
        // The highlight is inline rich text Qt can render, not a <mark> element.
        QVERIFY(highlighted(QStringLiteral("x"), ranges({{0, 1}})).contains(QStringLiteral("background-color")));
    }

    void highlightsEscapeMarkupAndIgnoreBadRanges() {
        QCOMPARE(highlighted(QStringLiteral("<b>x</b> & y"), {}), QStringLiteral("&lt;b&gt;x&lt;/b&gt; &amp; y"));
        // Out of range, zero length, overlapping and out-of-order ranges are dropped, never crash.
        QCOMPARE(highlighted(QStringLiteral("abc"), ranges({{5, 2}})), QStringLiteral("abc"));
        QCOMPARE(highlighted(QStringLiteral("abc"), ranges({{1, 0}})), QStringLiteral("abc"));
        QCOMPARE(highlighted(QStringLiteral("abcdef"), ranges({{2, 10}})), QStringLiteral("abcdef"));
        QCOMPARE(highlighted(QStringLiteral("abcdef"), ranges({{3, 2}, {1, 2}})),
                 QStringLiteral("abc") + kOpen + QStringLiteral("de") + kClose + QStringLiteral("f"));
        QJsonArray malformed;
        malformed.append(QJsonArray{1});
        malformed.append(QJsonValue(7));
        QCOMPARE(highlighted(QStringLiteral("abc"), malformed), QStringLiteral("abc"));
        QCOMPARE(highlighted(QStringLiteral("<x>"), ranges({{0, 3}})),
                 kOpen + QStringLiteral("&lt;x&gt;") + kClose);
    }

    void relativeTimes() {
        const QDateTime now = QDateTime::fromString(QStringLiteral("2026-09-17T12:00:00"), Qt::ISODate);
        QCOMPARE(whenText(0, now), QStringLiteral("—"));
        QCOMPARE(whenText(double(now.addSecs(-30).toSecsSinceEpoch()), now), QStringLiteral("just now"));
        QCOMPARE(whenText(double(now.addSecs(-14 * 60).toSecsSinceEpoch()), now), QStringLiteral("14 min ago"));
        QCOMPARE(whenText(double(now.addSecs(-3 * 3600).toSecsSinceEpoch()), now), QStringLiteral("3 h ago"));
        QCOMPARE(whenText(double(now.addDays(-1).toSecsSinceEpoch()), now), QStringLiteral("yesterday 12:00"));
        QVERIFY(whenText(double(now.addDays(-40).toSecsSinceEpoch()), now).contains(QStringLiteral("Aug")));
        QVERIFY(whenText(double(now.addYears(-2).toSecsSinceEpoch()), now).contains(QStringLiteral("2024")));
    }

    void dateFilters() {
        const QDateTime now = QDateTime::fromString(QStringLiteral("2026-09-17T12:00:00"), Qt::ISODate);
        QCOMPARE(sinceFor(QStringLiteral("any"), now), 0.0);
        QCOMPARE(sinceFor(QStringLiteral("nonsense"), now), 0.0);
        QCOMPARE(sinceFor(QStringLiteral("today"), now),
                 double(QDateTime(now.date(), QTime(0, 0)).toSecsSinceEpoch()));
        QCOMPARE(sinceFor(QStringLiteral("week"), now), double(now.addDays(-7).toSecsSinceEpoch()));
        QCOMPARE(sinceFor(QStringLiteral("month"), now), double(now.addDays(-30).toSecsSinceEpoch()));
    }

    void ansiStripping() {
        QCOMPARE(stripAnsi(QByteArray("\x1b[1;32mgreen\x1b[0m text")), QStringLiteral("green text"));
        QCOMPARE(stripAnsi(QByteArray("line1\r\nline2\r\n")), QStringLiteral("line1\nline2"));
        QCOMPARE(stripAnsi(QByteArray("\x1b]0;a title\x07visible")), QStringLiteral("visible"));
        QCOMPARE(stripAnsi(QByteArray("\x1b]8;;http://x\x1b\\link")), QStringLiteral("link"));
        QCOMPARE(stripAnsi(QByteArray("keep\ttabs")), QStringLiteral("keep\ttabs"));
        QCOMPARE(stripAnsi(QByteArray("bell\a and \x01" "ctrl")), QStringLiteral("bell and ctrl"));
        QCOMPARE(stripAnsi(QByteArray("\x1b[")), QString());
        QCOMPARE(stripAnsi(QByteArray("\x1b")), QString());
        QCOMPARE(stripAnsi(QByteArray("abcdef"), 3), QStringLiteral("abc"));
        QCOMPARE(stripAnsi(QByteArray("\xc3\xa9t\xc3\xa9")), QString::fromUtf8("été"));
    }

    void dialogGroupsByProjectAndSearches() {
        Dialog dialog;
        QJsonObject asked;
        int queries = 0;
        dialog.onQuery = [&asked, &queries](const QJsonObject &request) { asked = request; ++queries; };
        // Showing the list asks for it: the default view is this project, everything, any time.
        dialog.show();
        QCOMPARE(queries, 1);
        QCOMPARE(asked.value(QStringLiteral("scope")).toString(), QStringLiteral("project"));
        QCOMPARE(asked.value(QStringLiteral("query")).toString(), QString());
        QVERIFY(!asked.contains(QStringLiteral("since")));
        QVERIFY(!asked.contains(QStringLiteral("sources")));
        QJsonObject item{{QStringLiteral("session_id"), QString(32, QLatin1Char('a'))},
                         {QStringLiteral("source"), QStringLiteral("agent")},
                         {QStringLiteral("title"), QStringLiteral("Relay engine")},
                         {QStringLiteral("project"), QStringLiteral("relay-terminal")},
                         {QStringLiteral("workspace"), QStringLiteral("/home/u/relay-terminal")},
                         {QStringLiteral("model"), QStringLiteral("glm-5")},
                         {QStringLiteral("updated"), 1.0e9},
                         {QStringLiteral("turns"), 4},
                         {QStringLiteral("open_requests"), 1},
                         {QStringLiteral("pinned"), 0},
                         {QStringLiteral("match_count"), 2},
                         {QStringLiteral("matches"), QJsonArray{QJsonObject{
                              {QStringLiteral("turn"), 2},
                              {QStringLiteral("kind"), QStringLiteral("reply")},
                              {QStringLiteral("line"), QStringLiteral("the pelican flies")},
                              {QStringLiteral("ranges"), ranges({{4, 7}})}}}}};
        QJsonObject terminal{{QStringLiteral("session_id"), QStringLiteral("term-0123456789abcdef")},
                             {QStringLiteral("source"), QStringLiteral("terminal")},
                             {QStringLiteral("title"), QStringLiteral("Terminal · other")},
                             {QStringLiteral("project"), QStringLiteral("other")},
                             {QStringLiteral("workspace"), QStringLiteral("/home/u/other")},
                             {QStringLiteral("updated"), 1.0e9},
                             {QStringLiteral("turns"), 9},
                             {QStringLiteral("pinned"), 1},
                             {QStringLiteral("matches"), QJsonArray{}}};
        dialog.setResults({{QStringLiteral("items"), QJsonArray{item, terminal}},
                           {QStringLiteral("elapsed_ms"), 3}});
        auto *tree = dialog.findChild<QTreeWidget *>();
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 2);                 // one group per project
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("relay-terminal"));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QVERIFY(tree->topLevelItem(1)->child(0)->text(0).contains(QStringLiteral("📌")));
        QVERIFY(tree->topLevelItem(0)->child(0)->text(2).contains(QStringLiteral("1 open")));

        // Terminal history cannot be resumed.
        tree->setCurrentItem(tree->topLevelItem(1)->child(0));
        bool resumed = false;
        dialog.onResume = [&resumed](const QJsonObject &row, bool) {
            resumed = row.value(QStringLiteral("session_id")).toString().startsWith(QLatin1Char('a'));
        };
        auto buttons = dialog.findChildren<QPushButton *>();
        QPushButton *resume = nullptr;
        for (QPushButton *button : buttons)
            if (button->text() == QStringLiteral("Resume here")) resume = button;
        QVERIFY(resume);
        QVERIFY(!resume->isEnabled());
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        QVERIFY(resume->isEnabled());
        resume->click();
        QVERIFY(resumed);
    }

    void findBarCountsBothSides() {
        FindBar bar;
        QStringList asked;
        int calls = 0;
        bar.onFind = [&calls](const QString &, bool) { ++calls; return 7; };
        bar.onCountConversation = [&asked](const QString &text) { asked << text; };
        bar.start(QStringLiteral("needle"));
        QCOMPARE(asked, QStringList{QStringLiteral("needle")});
        auto *label = bar.findChild<QLabel *>(QStringLiteral("findCount"));
        QVERIFY(label);
        QVERIFY(label->text().contains(QStringLiteral("7 in terminal")));
        bar.setConversationMatches(3);
        QVERIFY(label->text().contains(QStringLiteral("3 in conversation")));
        QCOMPARE(bar.text(), QStringLiteral("needle"));
        bar.setTerminalSearchable(false);
        QVERIFY(label->text().contains(QStringLiteral("needs the Relay engine")));
        const int before = calls;
        bar.setTerminalSearchable(true);
        QCOMPARE(calls, before);   // the label alone does not re-search
    }
};

QTEST_MAIN(ConversationsTest)
#include "conversations_test.moc"
