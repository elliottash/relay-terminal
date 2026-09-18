// SPDX-License-Identifier: GPL-3.0-or-later
// Pure helpers of the session manager and the find bar (src/Conversations.h), the session manager
// pane itself, and the ⓘ view's rendering (src/SessionInfo.h).
#include "Conversations.h"
#include "SessionInfo.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTabBar>
#include <QTest>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>

using namespace relay::conversations;

// The roles the rows carry (src/Conversations.cpp): a row's tags and the rich text of the rows an
// unfolded session holds. They are private to the file, so the test names them once, here.
static constexpr int kBadgeRole = Qt::UserRole + 5;
static constexpr int kHtmlRole = Qt::UserRole + 6;

static QString unfoldedText(QTreeWidgetItem *row) {
    QString all;
    for (int i = 0; i < row->childCount(); ++i) all += row->child(i)->data(0, kHtmlRole).toString() + QLatin1Char('\n');
    return all;
}

// A session row always carries one quick-look child (the arrow needs it); these are the others.
static QList<QTreeWidgetItem *> rowChildren(QTreeWidgetItem *row) {
    QList<QTreeWidgetItem *> out;
    for (int i = 0; i < row->childCount(); ++i)
        if (row->child(i)->data(0, Qt::UserRole + 7).toString() != QStringLiteral("preview")) out << row->child(i);
    return out;
}
static QTreeWidgetItem *rowTitled(QTreeWidget *tree, const QString &title) {
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->text(0) == title) return *it;
    return nullptr;
}

static QJsonObject sessionItem(const QString &id, const QString &title, const QString &project = QStringLiteral("relay")) {
    return QJsonObject{{QStringLiteral("session_id"), id}, {QStringLiteral("source"), QStringLiteral("agent")},
                       {QStringLiteral("title"), title}, {QStringLiteral("project"), project},
                       {QStringLiteral("session_dir"), QStringLiteral("/data/sessions")},
                       {QStringLiteral("updated"), 1.0e9}, {QStringLiteral("turns"), 3}};
}

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

    void managerGroupsByProjectAndSearches() {
        SessionManager dialog;
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
        QVERIFY(!asked.contains(QStringLiteral("include_threads")));   // threads are off by default
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
        QVERIFY(tree->topLevelItem(1)->child(0)->data(0, kBadgeRole).toStringList().contains(QStringLiteral("pinned")));
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

    // Card #R6J0: the "Subagent threads" box is unticked at first; ticked, the query asks for
    // threads, and each thread row sits under its owner session or names it.
    void subagentThreadsHangUnderTheirOwner() {
        SessionManager manager;
        QJsonObject asked;
        manager.onQuery = [&asked](const QJsonObject &request) { asked = request; };
        manager.show();
        auto *box = manager.findChild<QCheckBox *>(QStringLiteral("sessionsThreads"));
        QVERIFY(box);
        QVERIFY(!box->isChecked());
        box->setChecked(true);
        QVERIFY(asked.value(QStringLiteral("include_threads")).toBool());
        const QString owner(32, QLatin1Char('a')), thread(32, QLatin1Char('b')), nested(32, QLatin1Char('c')),
            orphan(32, QLatin1Char('d'));
        auto session = [](const QString &id, const QString &title) {
            return QJsonObject{{QStringLiteral("session_id"), id}, {QStringLiteral("source"), QStringLiteral("agent")},
                               {QStringLiteral("title"), title}, {QStringLiteral("project"), QStringLiteral("relay")},
                               {QStringLiteral("updated"), 1.0e9}, {QStringLiteral("turns"), 3}};
        };
        auto sub = [](const QString &id, const QString &ownerId, const QString &parent, const QString &title) {
            return QJsonObject{{QStringLiteral("session_id"), id}, {QStringLiteral("source"), QStringLiteral("subagent")},
                               {QStringLiteral("title"), title}, {QStringLiteral("project"), QStringLiteral("relay")},
                               {QStringLiteral("owner_session"), ownerId}, {QStringLiteral("owner_title"), QStringLiteral("Owner title")},
                               {QStringLiteral("parent_thread"), parent}, {QStringLiteral("agent_id"), QStringLiteral("a1")},
                               {QStringLiteral("agent_type"), QStringLiteral("general")}, {QStringLiteral("status"), QStringLiteral("done")},
                               {QStringLiteral("updated"), 1.0e9}};
        };
        // Newest first from the worker: the nested thread arrives before its parent.
        manager.setResults({{QStringLiteral("items"), QJsonArray{
            sub(nested, owner, thread, QStringLiteral("Nested dig")), sub(thread, owner, QString(), QStringLiteral("Find it")),
            session(owner, QStringLiteral("Owner title")), sub(orphan, QString(40, QLatin1Char('e')).left(32), QString(), QStringLiteral("Lost owner"))}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QVERIFY(tree);
        QTreeWidgetItem *group = tree->topLevelItem(0);
        QTreeWidgetItem *ownerRow = group->child(0);
        QCOMPARE(ownerRow->text(0), QStringLiteral("Owner title"));
        QCOMPARE(rowChildren(ownerRow).size(), 1);
        QTreeWidgetItem *threadRow = rowChildren(ownerRow).first();
        QVERIFY(threadRow->text(0).contains(QStringLiteral("Find it")));
        QCOMPARE(rowChildren(threadRow).size(), 1);                              // nested under its parent
        QVERIFY(rowChildren(threadRow).first()->text(0).contains(QStringLiteral("Nested dig")));
        // A thread whose owner is not in the list still hangs under it: a muted owner row (muted
        // only — not italic as well, per the legibility rules in docs/ARCHITECTURE.md).
        QTreeWidgetItem *lostOwner = group->child(1);
        QCOMPARE(lostOwner->text(0), QStringLiteral("Owner title"));
        QVERIFY(!lostOwner->font(0).italic());
        QCOMPARE(rowChildren(lostOwner).size(), 1);
        QVERIFY(rowChildren(lostOwner).first()->text(0).contains(QStringLiteral("Lost owner")));
        // Enter on a thread opens its history, never a resume.
        QJsonObject opened;
        bool resumed = false;
        manager.onOpenThread = [&opened](const QJsonObject &row) { opened = row; };
        manager.onResume = [&resumed](const QJsonObject &, bool) { resumed = true; };
        tree->setCurrentItem(threadRow);
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(opened.value(QStringLiteral("session_id")).toString(), thread);
        QVERIFY(!resumed);
    }

    void extraTabsAndEscape() {
        SessionManager manager;
        auto *bar = manager.findChild<QTabBar *>();
        QVERIFY(bar);
        QVERIFY(!bar->isVisibleTo(&manager));                 // one tab: no tab bar
        manager.addTab(QStringLiteral("closed"), QStringLiteral("Recently closed"), new QLabel(QStringLiteral("x")));
        QVERIFY(bar->isVisibleTo(&manager));
        QCOMPARE(manager.currentTab(), QStringLiteral("sessions"));
        manager.showTab(QStringLiteral("closed"));
        QCOMPARE(manager.currentTab(), QStringLiteral("closed"));
        manager.showTab(QString());
        QCOMPARE(manager.currentTab(), QStringLiteral("sessions"));
        bool closed = false;
        manager.onClose = [&closed] { closed = true; };
        manager.show();
        QTest::keyClick(manager.findChild<QLineEdit *>(), Qt::Key_Escape);
        QVERIFY(closed);
    }

    // The ⓘ view: the facts the card asks for, thread links placed at their turn, the way back.
    void infoRendersSessionAndThread() {
        using namespace relay::sessioninfo;
        const QDateTime now = QDateTime::fromSecsSinceEpoch(2000000000);
        QJsonObject thread{{QStringLiteral("id"), QString(32, QLatin1Char('b'))}, {QStringLiteral("agent_id"), QStringLiteral("a1")},
                           {QStringLiteral("type"), QStringLiteral("general")}, {QStringLiteral("title"), QStringLiteral("Find & fix")},
                           {QStringLiteral("status"), QStringLiteral("done")}, {QStringLiteral("owner_session"), QString(32, QLatin1Char('a'))},
                           {QStringLiteral("children"), QJsonArray{QJsonObject{{QStringLiteral("id"), QString(32, QLatin1Char('c'))},
                                                                              {QStringLiteral("agent_id"), QStringLiteral("a2")}}}}};
        QJsonObject info{{QStringLiteral("kind"), QStringLiteral("session")}, {QStringLiteral("live"), true},
                         {QStringLiteral("title"), QStringLiteral("Index work")}, {QStringLiteral("model"), QStringLiteral("glm-5")},
                         {QStringLiteral("provider"), QStringLiteral("GLM Coding Plan (glm)")},
                         {QStringLiteral("session_id"), QString(32, QLatin1Char('a'))},
                         {QStringLiteral("session_dir"), QStringLiteral("/data/relay/sessions/d&x")},
                         {QStringLiteral("file"), QStringLiteral("/data/s.json")}, {QStringLiteral("file_exists"), true},
                         {QStringLiteral("context"), QJsonObject{{QStringLiteral("used_tokens"), 41200}, {QStringLiteral("window"), 200000},
                                                                 {QStringLiteral("percent"), 20.6}}},
                         {QStringLiteral("usage"), QJsonObject{{QStringLiteral("prompt_tokens"), 120000}, {QStringLiteral("completion_tokens"), 8000},
                                                               {QStringLiteral("total_tokens"), 128000}, {QStringLiteral("requests"), 34}}},
                         {QStringLiteral("turns"), 2}, {QStringLiteral("thread_count"), 2},
                         {QStringLiteral("instructions"), QJsonArray{QStringLiteral("/w/CLAUDE.md")}},
                         {QStringLiteral("history"), QJsonArray{
                              QJsonObject{{QStringLiteral("turn"), 1}, {QStringLiteral("prompt"), QStringLiteral("first <b>")},
                                          {QStringLiteral("threads"), QJsonArray{thread}}},
                              QJsonObject{{QStringLiteral("turn"), 2}, {QStringLiteral("prompt"), QStringLiteral("second")}}}}};
        const QString html = renderInfo(info, now);
        for (const char *needle : {"glm-5", "GLM Coding Plan", "41.2k / 200.0k", "20.6%", "128.0k total", "34 requests",
                                   "not reported by this provider", "/data/s.json", "CLAUDE.md", "first &lt;b&gt;",
                                   "a1 general", "Find &amp; fix", "a2"})
            QVERIFY2(html.contains(QString::fromUtf8(needle)), needle);
        QVERIFY(html.indexOf(QStringLiteral("Find &amp; fix")) < html.indexOf(QStringLiteral("second")));   // at its turn
        // A link survives a session directory with '&' in it.
        const int at = html.indexOf(QStringLiteral("relay-info:thread?"));
        QVERIFY(at > 0);
        const QString href = html.mid(at, html.indexOf(QLatin1Char('"'), at) - at).replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        const auto query = linkQuery(QUrl(href));
        QCOMPARE(query.value(QStringLiteral("dir")), QStringLiteral("/data/relay/sessions/d&x"));
        QCOMPARE(query.value(QStringLiteral("id")), QString(32, QLatin1Char('b')));

        QJsonObject threadInfo{{QStringLiteral("kind"), QStringLiteral("thread")}, {QStringLiteral("thread_id"), QString(32, QLatin1Char('b'))},
                               {QStringLiteral("agent_id"), QStringLiteral("a1")}, {QStringLiteral("title"), QStringLiteral("Find it")},
                               {QStringLiteral("owner_session"), QString(32, QLatin1Char('a'))}, {QStringLiteral("owner_title"), QStringLiteral("Index work")},
                               {QStringLiteral("owner_exists"), true}, {QStringLiteral("status"), QStringLiteral("done")},
                               {QStringLiteral("history"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                                                  {QStringLiteral("text"), QStringLiteral("the task")}}}}};
        const QString threadHtml = renderInfo(threadInfo, now);
        QVERIFY(threadHtml.contains(QStringLiteral("↑ owner session: “Index work”")));
        QVERIFY(threadHtml.indexOf(QStringLiteral("↑ owner session")) < threadHtml.indexOf(QStringLiteral("the task")));
        QCOMPARE(compactNumber(812), QStringLiteral("812"));
        QCOMPARE(compactNumber(1300000), QStringLiteral("1.3M"));
    }

    void infoViewNavigatesAndGoesBack() {
        using namespace relay::sessioninfo;
        InfoView view;
        QList<QJsonObject> asked;
        view.onRequest = [&asked](const QJsonObject &request) { asked << request; };
        view.showLiveSession();
        QCOMPARE(asked.size(), 1);
        QVERIFY(!asked.last().contains(QStringLiteral("session_id")));
        view.setInfo({{QStringLiteral("event"), QStringLiteral("session_info")}, {QStringLiteral("id"), asked.last().value(QStringLiteral("id"))},
                      {QStringLiteral("kind"), QStringLiteral("session")}, {QStringLiteral("title"), QStringLiteral("Mine")}, {QStringLiteral("live"), true}});
        QCOMPARE(view.paneTitle(), QStringLiteral("Info · Mine"));
        view.showThread(QString(32, QLatin1Char('b')), QStringLiteral("/d"), QString(32, QLatin1Char('a')));
        QCOMPARE(asked.last().value(QStringLiteral("thread_id")).toString(), QString(32, QLatin1Char('b')));
        // A stale answer (an earlier id) is ignored.
        view.setInfo({{QStringLiteral("id"), asked.first().value(QStringLiteral("id"))}, {QStringLiteral("kind"), QStringLiteral("session")},
                      {QStringLiteral("title"), QStringLiteral("Stale")}});
        QCOMPARE(view.paneTitle(), QStringLiteral("Info · Mine"));
        view.back();
        QVERIFY(!asked.last().contains(QStringLiteral("thread_id")));
        bool closed = false;
        view.onClose = [&closed] { closed = true; };
        QTest::keyClick(view.findChild<QTextBrowser *>(), Qt::Key_Escape);
        QVERIFY(closed);
    }

    // ----- the list's own helpers (cards #R6J0, #CCKY) ------------------------------------------

    void dateGroups() {
        const QDateTime now = QDateTime::fromString(QStringLiteral("2026-09-17T12:00:00"), Qt::ISODate);
        auto at = [&now](int days, int hours = 0) {
            return double(now.addDays(-days).addSecs(-hours * 3600).toSecsSinceEpoch());
        };
        QCOMPARE(dateGroup(at(0), now), QStringLiteral("Today"));
        QCOMPARE(dateGroup(at(0, 11), now), QStringLiteral("Today"));          // 01:00 is still today
        QCOMPARE(dateGroup(at(1), now), QStringLiteral("Yesterday"));
        QCOMPARE(dateGroup(at(2), now), QStringLiteral("This week"));
        QCOMPARE(dateGroup(at(6), now), QStringLiteral("This week"));
        QCOMPARE(dateGroup(at(7), now), QStringLiteral("This month"));
        QCOMPARE(dateGroup(at(29), now), QStringLiteral("This month"));
        QCOMPARE(dateGroup(at(30), now), QStringLiteral("Older"));
        QCOMPARE(dateGroup(0, now), QStringLiteral("Older"));
        // A stamp from the future (a clock that went back) is today, never a group of its own.
        QCOMPARE(dateGroup(double(now.addDays(2).toSecsSinceEpoch()), now), QStringLiteral("Today"));
        QCOMPARE(dateGroupOrder().size(), 5);
        QCOMPARE(dateGroupOrder().first(), QStringLiteral("Today"));
        QCOMPARE(dateGroupOrder().last(), QStringLiteral("Older"));
    }

    void chipTextAndRemoval() {
        auto op = [](const QString &key, const QString &value, bool negated = false) {
            QJsonObject out{{QStringLiteral("key"), key}, {QStringLiteral("value"), value}};
            if (negated) out.insert(QStringLiteral("negated"), true);
            return out;
        };
        QCOMPARE(chipText(op(QStringLiteral("file"), QStringLiteral("parser.cpp"))), QStringLiteral("file: parser.cpp"));
        QCOMPARE(chipText(op(QStringLiteral("text"), QStringLiteral("pelican"), true)), QStringLiteral("not: pelican"));
        QCOMPARE(chipText(op(QStringLiteral("model"), QStringLiteral("kimi"), true)), QStringLiteral("not model: kimi"));
        QCOMPARE(chipText(op(QStringLiteral("is"), QStringLiteral("pinned"))), QStringLiteral("is: pinned"));

        // Removing one operator leaves the rest of the box as it was.
        QCOMPARE(removeOperator(QStringLiteral("file:parser.cpp index"), op(QStringLiteral("file"), QStringLiteral("parser.cpp"))),
                 QStringLiteral("index"));
        QCOMPARE(removeOperator(QStringLiteral("index file:parser.cpp more"), op(QStringLiteral("file"), QStringLiteral("parser.cpp"))),
                 QStringLiteral("index more"));
        // A quoted value, with the quotes the reply does not carry.
        QCOMPARE(removeOperator(QStringLiteral("file:\"my file.py\" index"), op(QStringLiteral("file"), QStringLiteral("my file.py"))),
                 QStringLiteral("index"));
        // Negation: the chip's × takes the `-` with it, and the plain word of the same name stays.
        QCOMPARE(removeOperator(QStringLiteral("pelican -pelican"), op(QStringLiteral("text"), QStringLiteral("pelican"), true)),
                 QStringLiteral("pelican"));
        QCOMPARE(removeOperator(QStringLiteral("-\"a phrase\" rest"), op(QStringLiteral("text"), QStringLiteral("a phrase"), true)),
                 QStringLiteral("rest"));
        QCOMPARE(removeOperator(QStringLiteral("-model:kimi rest"), op(QStringLiteral("model"), QStringLiteral("kimi"), true)),
                 QStringLiteral("rest"));
        // A negated operator's chip must not take the plain one away.
        QCOMPARE(removeOperator(QStringLiteral("model:kimi -model:kimi"), op(QStringLiteral("model"), QStringLiteral("kimi"), true)),
                 QStringLiteral("model:kimi"));
        // An operator that is not in the box any more changes nothing.
        QCOMPARE(removeOperator(QStringLiteral("index"), op(QStringLiteral("file"), QStringLiteral("parser.cpp"))),
                 QStringLiteral("index"));
        QCOMPARE(removeOperator(QString(), op(QStringLiteral("file"), QStringLiteral("x"))), QString());
    }

    void badgeComposition() {
        const QDateTime now = QDateTime::fromString(QStringLiteral("2026-09-17T12:00:00"), Qt::ISODate);
        const qint64 nowMs = now.toMSecsSinceEpoch();
        QCOMPARE(closedAgo(0, nowMs), QString());
        QCOMPARE(closedAgo(nowMs - 30'000, nowMs), QStringLiteral("closed just now"));
        QCOMPARE(closedAgo(nowMs - 5 * 60'000, nowMs), QStringLiteral("closed 5 min ago"));
        QCOMPARE(closedAgo(nowMs - 3 * 3600'000LL, nowMs), QStringLiteral("closed 3 h ago"));
        QCOMPARE(closedAgo(nowMs - 3 * 86400'000LL, nowMs), QStringLiteral("closed 3 days ago"));

        QJsonObject item{{QStringLiteral("pinned"), 1}, {QStringLiteral("unfinished"), true},
                         {QStringLiteral("files_count"), 3}, {QStringLiteral("has_edits"), true},
                         {QStringLiteral("branch"), QStringLiteral("feature/x")}};
        QCOMPARE(badges(item, false, QString()),
                 QStringList({QStringLiteral("pinned"), QStringLiteral("unfinished"),
                              QStringLiteral("edits · 3 files"), QStringLiteral("feature/x")}));
        // Open wins over closed: a conversation cannot be both, and the pane it is in matters more.
        QCOMPARE(badges(item, true, QStringLiteral("closed 5 min ago")).at(1), QStringLiteral("open"));
        QCOMPARE(badges(item, false, QStringLiteral("closed 5 min ago")).at(1), QStringLiteral("closed 5 min ago"));
        QCOMPARE(badges({{QStringLiteral("files_count"), 1}, {QStringLiteral("has_edits"), true}}, false, QString()),
                 QStringList{QStringLiteral("edits · 1 file")});
        // The trunk is not worth a tag, and a plain conversation carries none at all.
        QCOMPARE(badges({{QStringLiteral("branch"), QStringLiteral("main")}}, false, QString()), QStringList());
        QCOMPARE(badges({{QStringLiteral("branch"), QStringLiteral("master")}}, false, QString()), QStringList());
        QCOMPARE(badges({}, false, QString()), QStringList());
    }

    void elidesPathsInTheMiddle() {
        QCOMPARE(elideMiddleText(QStringLiteral("short"), 20), QStringLiteral("short"));
        const QString path = QStringLiteral("/home/u/relay-terminal/src/Conversations.cpp");
        const QString cut = elideMiddleText(path, 24);
        QCOMPARE(cut.size(), 24);
        QVERIFY(cut.contains(QChar(0x2026)));
        QVERIFY(cut.endsWith(QStringLiteral("Conversations.cpp")));     // the name survives
        // Plain text has no path to protect, so it is cut at the end.
        const QString words = elideMiddleText(QStringLiteral("a very long sentence of words"), 10);
        QVERIFY(words.endsWith(QChar(0x2026)));
        QVERIFY(words.startsWith(QStringLiteral("a very")));
    }

    void continueSectionPicksWhatIsUnfinished() {
        QJsonObject pinned = sessionItem(QStringLiteral("p"), QStringLiteral("Pinned"));
        pinned.insert(QStringLiteral("pinned"), 1);
        pinned.insert(QStringLiteral("updated"), 100.0);
        QJsonObject unfinished = sessionItem(QStringLiteral("u"), QStringLiteral("Unfinished"));
        unfinished.insert(QStringLiteral("unfinished"), true);
        unfinished.insert(QStringLiteral("updated"), 300.0);
        QJsonObject closed = sessionItem(QStringLiteral("c"), QStringLiteral("Closed"));
        closed.insert(QStringLiteral("updated"), 200.0);
        QJsonObject plain = sessionItem(QStringLiteral("x"), QStringLiteral("Plain"));
        QJsonObject elsewhere = sessionItem(QStringLiteral("e"), QStringLiteral("Other project"), QStringLiteral("other"));
        elsewhere.insert(QStringLiteral("pinned"), 1);
        QJsonObject terminal = sessionItem(QStringLiteral("t"), QStringLiteral("Terminal"));
        terminal.insert(QStringLiteral("source"), QStringLiteral("terminal"));
        terminal.insert(QStringLiteral("pinned"), 1);
        const QJsonArray items{pinned, unfinished, closed, plain, elsewhere, terminal};
        const QJsonArray picked = continueItems(items, QStringLiteral("relay"), {QStringLiteral("c")});
        QCOMPARE(picked.size(), 3);
        QCOMPARE(picked.at(0).toObject().value(QStringLiteral("session_id")).toString(), QStringLiteral("u"));
        QCOMPARE(picked.at(1).toObject().value(QStringLiteral("session_id")).toString(), QStringLiteral("c"));
        QCOMPARE(picked.at(2).toObject().value(QStringLiteral("session_id")).toString(), QStringLiteral("p"));
        QCOMPARE(continueItems(items, QStringLiteral("relay"), {}, 1).size(), 1);
        QCOMPARE(continueItems(items, QStringLiteral("nothing-here"), {}).size(), 0);
    }

    void estimateSentenceAlwaysAsksFirst() {
        const QJsonObject event{{QStringLiteral("scope"), QStringLiteral("project")}, {QStringLiteral("count"), 12},
                                {QStringLiteral("approx_input_tokens"), 45000},
                                {QStringLiteral("approx_output_tokens"), 3840},
                                {QStringLiteral("model"), QStringLiteral("glm-4.6")}};
        const QString text = estimateText(event);
        for (const char *needle : {"12 conversation", "this project", "45.0k input", "3.8k output", "glm-4.6",
                                   "Nothing is summarised unless you press Start"})
            QVERIFY2(text.contains(QString::fromUtf8(needle)), needle);
        QVERIFY(estimateText({{QStringLiteral("count"), 0}}).contains(QStringLiteral("already has a summary")));
        QVERIFY(estimateText({{QStringLiteral("count"), 1}, {QStringLiteral("scope"), QStringLiteral("all")}})
                    .contains(QStringLiteral("1 conversation in all projects has no summary")));
        QCOMPARE(compactTokens(812), QStringLiteral("812"));
        QCOMPARE(compactTokens(1250000), QStringLiteral("1.3M"));
    }

    // ----- the list itself ----------------------------------------------------------------------

    void rowsCarryTheirSummaryAndTags() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.setOpenSessions({QStringLiteral("b")});
        manager.setClosedSessions({{QStringLiteral("c"), {QStringLiteral("closed-1"),
                                                          QDateTime::currentMSecsSinceEpoch() - 5 * 60'000}}});
        manager.show();
        QJsonObject withSummary = sessionItem(QStringLiteral("a"), QStringLiteral("Index work"));
        withSummary.insert(QStringLiteral("summary"), QStringLiteral("Rebuilt the conversation index."));
        withSummary.insert(QStringLiteral("files_count"), 2);
        QJsonObject open = sessionItem(QStringLiteral("b"), QStringLiteral("Open elsewhere"));
        open.insert(QStringLiteral("first_prompt"), QStringLiteral("make the list nicer"));
        QJsonObject closed = sessionItem(QStringLiteral("c"), QStringLiteral("Just closed"));
        manager.setResults({{QStringLiteral("items"), QJsonArray{withSummary, open, closed}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        // The just-closed one is what "Continue" is for, so it heads the list; the others sit in
        // their project's group.
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Continue"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("relay"));
        QTreeWidgetItem *indexed = rowTitled(tree, QStringLiteral("Index work"));
        QTreeWidgetItem *opened = rowTitled(tree, QStringLiteral("Open elsewhere"));
        QTreeWidgetItem *justClosed = rowTitled(tree, QStringLiteral("Just closed"));
        QVERIFY(indexed && opened && justClosed);
        QCOMPARE(justClosed->parent(), tree->topLevelItem(0));
        QCOMPARE(indexed->data(0, Qt::UserRole + 4).toString(), QStringLiteral("Rebuilt the conversation index."));
        QVERIFY(indexed->data(0, kBadgeRole).toStringList().contains(QStringLiteral("edits · 2 files")));
        // No summary yet: the first prompt says what it was about instead.
        QCOMPARE(opened->data(0, Qt::UserRole + 4).toString(), QStringLiteral("make the list nicer"));
        QVERIFY(opened->data(0, kBadgeRole).toStringList().contains(QStringLiteral("open")));
        QVERIFY(justClosed->data(0, kBadgeRole).toStringList().contains(QStringLiteral("closed 5 min ago")));
        // Selecting the open one says what Enter will do with it.
        tree->setCurrentItem(opened);
        auto *status = manager.findChild<QLabel *>();
        Q_UNUSED(status);
        bool said = false;
        for (QLabel *label : manager.findChildren<QLabel *>())
            said = said || label->text().contains(QStringLiteral("Enter goes to that pane"));
        QVERIFY(said);
    }

    // "closed N min ago" is a clock. It has to keep up while the pane sits open, and go when the
    // item is reopened or falls off the end of the list — without asking the worker again.
    void theClosedTagAgesAndThenGoes() {
        SessionManager manager;
        int queries = 0;
        manager.onQuery = [&queries](const QJsonObject &) { ++queries; };
        // Just under the minute, so the wait below carries it over into "1 min ago".
        const qint64 closedAt = QDateTime::currentMSecsSinceEpoch() - 59'000;
        manager.setClosedSessions({{QStringLiteral("a"), {QStringLiteral("closed-3"), closedAt}}});
        manager.show();
        manager.setResults({{QStringLiteral("items"),
                             QJsonArray{sessionItem(QStringLiteral("a"), QStringLiteral("Was in a pane"))}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QTreeWidgetItem *row = rowTitled(tree, QStringLiteral("Was in a pane"));
        QVERIFY(row);
        QVERIFY(row->data(0, kBadgeRole).toStringList().contains(QStringLiteral("closed just now")));
        const int asked = queries;

        // The timer runs while there is something closed to say it about. Tick it by hand rather
        // than waiting out its interval: what is under test is what the tick does.
        QTest::qWait(1500);
        manager.refreshClosedAges();
        QVERIFY(row->data(0, kBadgeRole).toStringList().contains(QStringLiteral("closed 1 min ago")));
        QCOMPARE(queries, asked);   // redrawn from what is already here

        // Resumed in a pane meanwhile: "open" wins, because that is what Enter will do.
        manager.setOpenSessions({QStringLiteral("a")});
        row = rowTitled(tree, QStringLiteral("Was in a pane"));
        QVERIFY(row && row->data(0, kBadgeRole).toStringList().contains(QStringLiteral("open")));
        QVERIFY(!row->data(0, kBadgeRole).toStringList().contains(QStringLiteral("closed 1 min ago")));
        manager.setOpenSessions({});

        // Reopened or pushed off the end of the 25: the tag goes, and so does "Reopen where it was".
        auto *reopen = manager.findChild<QPushButton *>(QStringLiteral("reopenClosed"));
        QVERIFY(reopen && reopen->isVisible());
        manager.setClosedSessions({});
        row = rowTitled(tree, QStringLiteral("Was in a pane"));
        QVERIFY(row);
        for (const QString &tag : row->data(0, kBadgeRole).toStringList())
            QVERIFY(!tag.startsWith(QStringLiteral("closed ")));
        QVERIFY(!reopen->isVisible());
    }

    void unfoldAsksOnceAndFillsFromTheOverview() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        QStringList asked;
        manager.onPreview = [&asked](const QString &id, const QString &) { asked << id; };
        manager.show();
        manager.setResults({{QStringLiteral("items"), QJsonArray{sessionItem(QStringLiteral("a"), QStringLiteral("First")),
                                                                sessionItem(QStringLiteral("b"), QStringLiteral("Second"))}}});
        QCOMPARE(asked, QStringList{QStringLiteral("a")});      // the selected row, not both
        asked.clear();
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QTreeWidgetItem *second = tree->topLevelItem(0)->child(1);
        second->setExpanded(true);
        QCOMPARE(asked, QStringList{QStringLiteral("b")});
        const QJsonObject overview{{QStringLiteral("summary"), QStringLiteral("What it did.")},
                                   {QStringLiteral("first_prompt"), QStringLiteral("do the thing")},
                                   {QStringLiteral("last_turns"), QJsonArray{QJsonObject{
                                        {QStringLiteral("turn"), 3}, {QStringLiteral("prompt"), QStringLiteral("and now?")},
                                        {QStringLiteral("reply"), QStringLiteral("done")}}}},
                                   {QStringLiteral("files"), QJsonArray{QStringLiteral("src/a.cpp")}},
                                   {QStringLiteral("files_count"), 4},
                                   {QStringLiteral("todos"), QJsonArray{
                                        QJsonObject{{QStringLiteral("text"), QStringLiteral("write the test")}, {QStringLiteral("status"), QStringLiteral("pending")}},
                                        QJsonObject{{QStringLiteral("text"), QStringLiteral("done one")}, {QStringLiteral("status"), QStringLiteral("completed")}}}}};
        manager.setPreview({{QStringLiteral("session_id"), QStringLiteral("b")}, {QStringLiteral("overview"), overview},
                            {QStringLiteral("items"), QJsonArray{}}});
        const QString text = unfoldedText(second);
        for (const char *needle : {"What it did.", "First:", "do the thing", "You:", "and now?", "Agent:",
                                   "src/a.cpp", "Files (4)", "and 3 more", "write the test"})
            QVERIFY2(text.contains(QString::fromUtf8(needle)), needle);
        QVERIFY(!text.contains(QStringLiteral("done one")));      // finished todos are not "still to do"
        // Folding and unfolding again is free: the overview is kept.
        second->setExpanded(false);
        second->setExpanded(true);
        QCOMPARE(asked, QStringList{QStringLiteral("b")});
    }

    void refreshKeepsTheSelectionAndWhatWasUnfolded() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        int previews = 0;
        manager.onPreview = [&previews](const QString &, const QString &) { ++previews; };
        manager.show();
        const QJsonArray items{sessionItem(QStringLiteral("a"), QStringLiteral("First")),
                               sessionItem(QStringLiteral("b"), QStringLiteral("Second"))};
        manager.setResults({{QStringLiteral("items"), items}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        tree->setCurrentItem(tree->topLevelItem(0)->child(1));
        tree->topLevelItem(0)->child(1)->setExpanded(true);
        manager.setPreview({{QStringLiteral("session_id"), QStringLiteral("b")},
                            {QStringLiteral("overview"), QJsonObject{{QStringLiteral("summary"), QStringLiteral("Kept.")}}},
                            {QStringLiteral("items"), QJsonArray{}}});
        const int before = previews;
        manager.setResults({{QStringLiteral("items"), items}});
        QTreeWidgetItem *second = tree->topLevelItem(0)->child(1);
        QCOMPARE(tree->currentItem(), second);
        QVERIFY(second->isExpanded());
        QVERIFY(unfoldedText(second).contains(QStringLiteral("Kept.")));
        QCOMPARE(previews, before);            // nothing was asked for a second time
    }

    void chipsFollowTheParsedQueryAndTakeItBack() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.show();
        manager.setQuery(QStringLiteral("file:parser.cpp -pelican index"));
        const QJsonObject parsed{{QStringLiteral("text"), QStringLiteral("index")},
                                 {QStringLiteral("operators"), QJsonArray{
                                      QJsonObject{{QStringLiteral("key"), QStringLiteral("file")}, {QStringLiteral("value"), QStringLiteral("parser.cpp")}},
                                      QJsonObject{{QStringLiteral("key"), QStringLiteral("text")}, {QStringLiteral("value"), QStringLiteral("pelican")},
                                                  {QStringLiteral("negated"), true}}}},
                                 {QStringLiteral("ignored"), QJsonArray{QStringLiteral("before:someday")}}};
        manager.setResults({{QStringLiteral("items"), QJsonArray{}}, {QStringLiteral("parsed"), parsed}});
        const auto chips = manager.findChildren<QToolButton *>(QStringLiteral("stripChip"));
        QCOMPARE(chips.size(), 2);
        QVERIFY(chips.at(0)->text().startsWith(QStringLiteral("file: parser.cpp")));
        QVERIFY(chips.at(1)->text().startsWith(QStringLiteral("not: pelican")));
        bool noted = false;
        for (QLabel *label : manager.findChildren<QLabel *>())
            noted = noted || (label->isVisibleTo(&manager) && label->text() == QStringLiteral("ignored: before:someday"));
        QVERIFY(noted);
        chips.at(0)->click();
        QCOMPARE(manager.query(), QStringLiteral("-pelican index"));
        // An empty `parsed` takes the chips away again.
        manager.setResults({{QStringLiteral("items"), QJsonArray{}}, {QStringLiteral("parsed"), QJsonObject{}}});
        QCOMPARE(manager.findChildren<QToolButton *>(QStringLiteral("stripChip")).size(), 0);
    }

    void searchingSortsByBestMatchUntilTheUserSaysOtherwise() {
        SessionManager manager;
        QList<QJsonObject> asked;
        manager.onQuery = [&asked](const QJsonObject &request) { asked << request; };
        manager.show();
        QVERIFY(!asked.last().contains(QStringLiteral("sort")));         // listing: newest first
        manager.setQuery(QStringLiteral("pelican"));
        QTest::qWait(200);                                              // the box is debounced
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("relevance"));
        manager.setQuery(QString());
        QTest::qWait(200);
        QVERIFY(!asked.last().contains(QStringLiteral("sort")));
        // A sort the user picks by hand stands, whatever they type next.
        auto *sort = manager.findChild<QComboBox *>(QStringLiteral("sessionsSort"));
        sort->setCurrentIndex(sort->findData(QStringLiteral("longest")));
        emit sort->activated(sort->currentIndex());
        manager.setQuery(QStringLiteral("pelican"));
        QTest::qWait(200);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("longest"));
    }

    void filtersSendTheirOwnFieldsAndClear() {
        SessionManager manager;
        QList<QJsonObject> asked;
        manager.onQuery = [&asked](const QJsonObject &request) { asked << request; };
        manager.show();
        auto action = [&manager](const QString &name) { return manager.findChild<QAction *>(name); };
        action(QStringLiteral("filterHasEdits"))->setChecked(true);
        action(QStringLiteral("filterUnfinished"))->setChecked(true);
        QVERIFY(asked.last().value(QStringLiteral("has_edits")).toBool());
        QVERIFY(asked.last().value(QStringLiteral("unfinished")).toBool());
        action(QStringLiteral("filterPinned"))->setChecked(true);
        action(QStringLiteral("filterHasSummary"))->setChecked(true);
        action(QStringLiteral("filterOpenTasks"))->setChecked(true);
        QVERIFY(asked.last().value(QStringLiteral("pinned")).toBool());
        QVERIFY(asked.last().value(QStringLiteral("has_summary")).toBool());
        QVERIFY(asked.last().value(QStringLiteral("has_open_tasks")).toBool());
        // Nothing matched and a filter is on: the way out is offered, and it clears every one.
        manager.setResults({{QStringLiteral("items"), QJsonArray{}}});
        auto *clear = manager.findChild<QPushButton *>(QStringLiteral("clearFilters"));
        QVERIFY(clear->isVisibleTo(&manager));
        QVERIFY(manager.findChild<QPushButton *>(QStringLiteral("searchAllProjects"))->isVisibleTo(&manager));
        clear->click();
        QVERIFY(!asked.last().contains(QStringLiteral("has_edits")));
        QVERIFY(!asked.last().contains(QStringLiteral("has_open_tasks")));
        // The facets fill the model and branch menus; one branch everywhere is not a filter.
        manager.setResults({{QStringLiteral("items"), QJsonArray{}},
                            {QStringLiteral("facets"), QJsonObject{
                                 {QStringLiteral("models"), QJsonArray{QStringLiteral("glm-5"), QStringLiteral("kimi")}},
                                 {QStringLiteral("branches"), QJsonArray{QStringLiteral("main")}}}}});
        QCOMPARE(manager.findChild<QComboBox *>(QStringLiteral("sessionsBranch"))->isVisibleTo(&manager), false);
        manager.setResults({{QStringLiteral("items"), QJsonArray{}},
                            {QStringLiteral("facets"), QJsonObject{
                                 {QStringLiteral("branches"), QJsonArray{QStringLiteral("main"), QStringLiteral("work")}}}}});
        auto *branch = manager.findChild<QComboBox *>(QStringLiteral("sessionsBranch"));
        QVERIFY(branch->isVisibleTo(&manager));
        branch->setCurrentIndex(branch->findData(QStringLiteral("work")));
        QCOMPARE(asked.last().value(QStringLiteral("branch")).toString(), QStringLiteral("work"));
    }

    void groupingByDateAndTheContinueSection() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.setProject(QStringLiteral("relay"));
        manager.show();
        const QDateTime now = QDateTime::currentDateTime();
        QJsonObject today = sessionItem(QStringLiteral("a"), QStringLiteral("Today's work"));
        today.insert(QStringLiteral("updated"), double(now.toSecsSinceEpoch()));
        today.insert(QStringLiteral("unfinished"), true);
        QJsonObject older = sessionItem(QStringLiteral("b"), QStringLiteral("Last month"));
        older.insert(QStringLiteral("updated"), double(now.addDays(-40).toSecsSinceEpoch()));
        manager.setResults({{QStringLiteral("items"), QJsonArray{today, older}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        // Grouped by project, the unfinished one heads the list and is not repeated below it.
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Continue"));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("relay"));
        QCOMPARE(tree->topLevelItem(1)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(1)->child(0)->text(0), QStringLiteral("Last month"));
        // Grouped by date it is in its date group too: a date group with a hole in it would lie.
        auto *group = manager.findChild<QComboBox *>(QStringLiteral("sessionsGroup"));
        group->setCurrentIndex(group->findData(QStringLiteral("date")));
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Continue"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Today"));
        QCOMPARE(tree->topLevelItem(1)->child(0)->text(0), QStringLiteral("Today's work"));
        QCOMPARE(tree->topLevelItem(2)->text(0), QStringLiteral("Older"));
        // No grouping: the rows stand on their own, the Continue group still first.
        group->setCurrentIndex(group->findData(QStringLiteral("none")));
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Continue"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Today's work"));
        // With something typed there is nothing to continue: the query is what matters.
        manager.setQuery(QStringLiteral("month"));
        manager.setResults({{QStringLiteral("items"), QJsonArray{today, older}}});
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Today's work"));
    }

    void keysReachEveryAction() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.setClosedSessions({{QStringLiteral("a"), {QStringLiteral("closed-7"), QDateTime::currentMSecsSinceEpoch()}}});
        manager.show();
        QJsonObject item = sessionItem(QStringLiteral("a"), QStringLiteral("First"));
        item.insert(QStringLiteral("pinned"), 0);
        manager.setResults({{QStringLiteral("items"), QJsonArray{item}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        auto *search = manager.findChild<QLineEdit *>(QStringLiteral("sessionsSearch"));
        int resumed = 0, newPane = 0;
        QString forked, reopened, pinned;
        manager.onResume = [&resumed, &newPane](const QJsonObject &, bool other) { other ? ++newPane : ++resumed; };
        manager.onFork = [&forked](const QJsonObject &row) { forked = row.value(QStringLiteral("session_id")).toString(); };
        manager.onReopenClosed = [&reopened](const QString &id) { reopened = id; };
        manager.onPin = [&pinned](const QString &id, bool on) { pinned = id + (on ? QStringLiteral(" on") : QStringLiteral(" off")); };
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(resumed, 1);
        QTest::keyClick(tree, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(newPane, 1);
        QTest::keyClick(tree, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(forked, QStringLiteral("a"));
        QTest::keyClick(tree, Qt::Key_Return, Qt::AltModifier);
        QCOMPARE(reopened, QStringLiteral("closed-7"));
        QTest::keyClick(tree, Qt::Key_P, Qt::ControlModifier);
        QCOMPARE(pinned, QStringLiteral("a on"));
        // Space unfolds, → unfolds, ← folds.
        QTreeWidgetItem *row = tree->topLevelItem(0)->child(0);
        QTest::keyClick(tree, Qt::Key_Space);
        QVERIFY(row->isExpanded());
        QTest::keyClick(tree, Qt::Key_Space);
        QVERIFY(!row->isExpanded());
        // Typing on the rows types in the box; "/" and Ctrl+F only move the keyboard there.
        QTest::keyClick(tree, Qt::Key_Z);
        QCOMPARE(search->text(), QStringLiteral("z"));
        QCOMPARE(manager.focusWidget(), search);
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Slash);
        QCOMPARE(search->text(), QStringLiteral("z"));
        QCOMPARE(manager.focusWidget(), search);
        // Esc clears the query first and closes second.
        bool closed = false;
        manager.onClose = [&closed] { closed = true; };
        QTest::keyClick(search, Qt::Key_Escape);
        QVERIFY(search->text().isEmpty());
        QVERIFY(!closed);
        QTest::keyClick(search, Qt::Key_Escape);
        QVERIFY(closed);
    }

    void summariesFromTheButtonAndTheBatch() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        QString summarised, estimated, started;
        manager.onSummarise = [&summarised](const QString &id, const QString &) { summarised = id; };
        manager.onSummariseEstimate = [&estimated](const QString &scope) { estimated = scope; };
        manager.onSummariseAll = [&started](const QString &scope) { started = scope; };
        manager.show();
        manager.setResults({{QStringLiteral("items"), QJsonArray{sessionItem(QStringLiteral("a"), QStringLiteral("No summary"))}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QTreeWidgetItem *row = tree->topLevelItem(0)->child(0);
        auto *button = manager.findChild<QPushButton *>(QStringLiteral("summariseOne"));
        QVERIFY(button->isVisibleTo(&manager));
        // The unfolded row offers it too, where the summary would have been.
        row->setExpanded(true);
        manager.setPreview({{QStringLiteral("session_id"), QStringLiteral("a")}, {QStringLiteral("overview"), QJsonObject{}},
                            {QStringLiteral("items"), QJsonArray{}}});
        QVERIFY(manager.findChild<QPushButton *>(QStringLiteral("summariseRow")));
        button->click();
        QCOMPARE(summarised, QStringLiteral("a"));
        QCOMPARE(button->text(), QStringLiteral("Summarising…"));
        QVERIFY(!button->isEnabled());
        manager.setSummary({{QStringLiteral("session_id"), QStringLiteral("a")},
                            {QStringLiteral("summary"), QStringLiteral("It did the thing.")}});
        QCOMPARE(row->data(0, Qt::UserRole + 4).toString(), QStringLiteral("It did the thing."));
        QVERIFY(unfoldedText(row).contains(QStringLiteral("It did the thing.")));
        QVERIFY(!button->isVisibleTo(&manager));            // it has one now
        // An error says so on the row rather than disappearing.
        manager.setResults({{QStringLiteral("items"), QJsonArray{sessionItem(QStringLiteral("b"), QStringLiteral("Second"))}}});
        tree->topLevelItem(0)->child(0)->setExpanded(true);
        manager.setPreview({{QStringLiteral("session_id"), QStringLiteral("b")}, {QStringLiteral("overview"), QJsonObject{}},
                            {QStringLiteral("items"), QJsonArray{}}});
        manager.findChild<QPushButton *>(QStringLiteral("summariseRow"))->click();
        manager.setSummary({{QStringLiteral("session_id"), QStringLiteral("b")}, {QStringLiteral("error"), QStringLiteral("no chores model")}});
        QVERIFY(unfoldedText(tree->topLevelItem(0)->child(0)).contains(QStringLiteral("no chores model")));

        // The batch: the estimate first, and Start is the only thing that runs it.
        manager.findChild<QAction *>(QStringLiteral("summariseAll"))->trigger();
        QCOMPARE(estimated, QStringLiteral("project"));
        auto *start = manager.findChild<QPushButton *>(QStringLiteral("summariseStart"));
        QVERIFY(!start->isEnabled());
        manager.setSummariseEstimate({{QStringLiteral("scope"), QStringLiteral("project")}, {QStringLiteral("count"), 4},
                                      {QStringLiteral("approx_input_tokens"), 20000},
                                      {QStringLiteral("approx_output_tokens"), 1280},
                                      {QStringLiteral("model"), QStringLiteral("glm-4.6")}});
        auto *estimate = manager.findChild<QLabel *>(QStringLiteral("summariseEstimate"));
        QVERIFY(estimate->text().contains(QStringLiteral("4 conversations in this project")));
        QVERIFY(estimate->text().contains(QStringLiteral("Nothing is summarised unless you press Start")));
        QVERIFY(start->isEnabled());
        QVERIFY(started.isEmpty());
        start->click();
        QCOMPARE(started, QStringLiteral("project"));
        QVERIFY(!manager.findChild<QAction *>(QStringLiteral("summariseAll"))->isEnabled());
        auto *cancel = manager.findChild<QPushButton *>(QStringLiteral("cancelSummaries"));
        QVERIFY(cancel->isVisibleTo(&manager));
        manager.setSummariseProgress({{QStringLiteral("done"), 1}, {QStringLiteral("total"), 4},
                                      {QStringLiteral("session_id"), QStringLiteral("b")},
                                      {QStringLiteral("summary"), QStringLiteral("Live from the batch.")}});
        QCOMPARE(tree->topLevelItem(0)->child(0)->data(0, Qt::UserRole + 4).toString(), QStringLiteral("Live from the batch."));
        bool progress = false;
        for (QLabel *label : manager.findChildren<QLabel *>())
            progress = progress || label->text() == QStringLiteral("Summarising 1 of 4…");
        QVERIFY(progress);
        manager.setSummariseProgress({{QStringLiteral("done"), 4}, {QStringLiteral("total"), 4},
                                      {QStringLiteral("finished"), true}, {QStringLiteral("failed"), 1}});
        QVERIFY(manager.findChild<QAction *>(QStringLiteral("summariseAll"))->isEnabled());
        QVERIFY(!cancel->isVisibleTo(&manager));
        // `session_summary` for the conversation a pane is holding updates its row where it stands.
        manager.setSessionSummary({{QStringLiteral("session_id"), QStringLiteral("b")},
                                   {QStringLiteral("summary"), QStringLiteral("From the pane.")}, {QStringLiteral("turn"), 3}});
        QCOMPARE(tree->topLevelItem(0)->child(0)->data(0, Qt::UserRole + 4).toString(), QStringLiteral("From the pane."));
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
