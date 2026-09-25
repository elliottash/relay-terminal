// SPDX-License-Identifier: AGPL-3.0-or-later
// Pure helpers of the session manager and the find bar (src/Conversations.h), the session manager
// pane itself, and the ⓘ view's rendering (src/SessionInfo.h).
#include "Conversations.h"
#include "AgentContext.h"
#include "OutputLinks.h"

#include <QFontMetrics>
#include <algorithm>
#include "SessionInfo.h"
// Keymap.h holds a raw-string preset table moc cannot parse, and moc parses this file
// for its own test class (see tests/keymap_test.cpp for the same guard).
#ifndef Q_MOC_RUN
#include "Keymap.h"
#endif

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSettings>
#include <QPushButton>
#include <QTabBar>
#include <QStackedWidget>
#include <QTest>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QElapsedTimer>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <ctime>

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

// Select through the actual combo popup, not setCurrentIndex: this catches a control whose menu
// appears to work but never delivers the user's click to SessionManager.
// The menu-side twin for the choices the combos left behind (#1Q5V): trigger the action a
// click would, and let its own wiring run.
static bool chooseMenuAction(QMenu *menu, const QString &data) {
    for (QAction *action : menu->actions()) {
        if (action->isSeparator() || action->data().toString() != data) continue;
        action->trigger();
        return true;
    }
    return false;
}
static bool chooseComboItem(QComboBox *combo, const QString &data) {
    const int at = combo->findData(data);
    if (at < 0) return false;
    QTest::mouseClick(combo, Qt::LeftButton, Qt::NoModifier,
                      QPoint(combo->width() - 8, combo->height() / 2));
    QAbstractItemView *view = combo->view();
    if (!view || !view->isVisible()) return false;
    const QModelIndex index = combo->model()->index(at, 0);
    view->scrollTo(index);
    const QPoint point = view->visualRect(index).center();
    QTest::mouseMove(view->viewport(), point);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    if (combo->currentIndex() == at)
        return true;
    // The synthetic click can land dead when the popup has re-opened repositioned — the
    // container shifts to keep the current item aligned, and the offscreen platform cannot
    // grab the mouse the way a windowing system would, so the release never reaches the
    // container's selection logic. The popup is still open and focused, so finish the choice
    // the way a keyboard user would: arrows to the item, Return. That still drives the combo's
    // real popup — which is what this helper exists to check — and not setCurrentIndex.
    const int steps = (at - combo->currentIndex() + combo->count()) % combo->count();
    for (int i = 0; i < steps; ++i)
        QTest::keyClick(view, Qt::Key_Down);
    QTest::keyClick(view, Qt::Key_Return);
    return combo->currentIndex() == at;
}

// ----- a fake console, for the helper agent (card #AGNT step 7) --------------------------------
//
// The window is the only place a `Pane` can be built, so a pane library's own test stands in for
// it: a plain QWidget and the handful of lambdas `relay::agent::ConsoleHandle` carries, each
// recording what the host asked of it. That is the whole seam — if a host needs anything else
// from a console, this struct is what has to grow.
struct FakeConsole {
    QWidget *widget = nullptr;
    relay::agent::Context *context = nullptr;
    int builds = 0, focused = 0;
    QList<bool> collapsed;
    QString draft;

    relay::agent::ConsoleFactory factory() {
        return [this](relay::agent::Context *context, QWidget *parent) {
            ++builds;
            this->context = context;
            widget = new QWidget(parent);
            widget->setObjectName(QStringLiteral("fakeConsole"));
            relay::agent::ConsoleHandle handle;
            handle.widget = widget;
            handle.focusComposer = [this] { ++focused; };
            handle.draftInComposer = [this](const QString &text) { draft = text; };
            handle.composerText = [this] { return draft; };
            handle.setCollapsed = [this](bool on) { collapsed << on; };
            handle.collapsed = [this] { return collapsed.isEmpty() ? true : collapsed.last(); };
            handle.runActionLetter = [](const QString &) { return false; };
            return handle;
        };
    }
};

static QJsonObject sessionItem(const QString &id, const QString &title, const QString &project = QStringLiteral("relay")) {
    return QJsonObject{{QStringLiteral("session_id"), id}, {QStringLiteral("source"), QStringLiteral("agent")},
                       {QStringLiteral("title"), title}, {QStringLiteral("project"), project},
                       {QStringLiteral("session_dir"), QStringLiteral("/data/sessions")},
                       {QStringLiteral("updated"), 1.0e9}, {QStringLiteral("turns"), 3}};
}

static QJsonObject guestItem(const QString &source, const QString &id, const QString &title,
                             const QString &cwd = QStringLiteral("/home/u/repos/relay-terminal")) {
    const QJsonArray resume = source == QLatin1String("claude")
                                  ? QJsonArray{QStringLiteral("claude"), QStringLiteral("-r"), id}
                                  : QJsonArray{QStringLiteral("codex"), QStringLiteral("resume"), id};
    const QJsonArray fork = source == QLatin1String("claude")
                                ? QJsonArray{QStringLiteral("claude"), QStringLiteral("-r"), id, QStringLiteral("--fork-session")}
                                : QJsonArray{QStringLiteral("codex"), QStringLiteral("fork"), id};
    return QJsonObject{{QStringLiteral("session_id"), id}, {QStringLiteral("id"), id},
                       {QStringLiteral("source"), source}, {QStringLiteral("title"), title},
                       {QStringLiteral("project"), QStringLiteral("relay-terminal")},
                       {QStringLiteral("workspace"), cwd}, {QStringLiteral("resume_cwd"), cwd},
                       {QStringLiteral("session_dir"), QString()},
                       {QStringLiteral("updated"), 1.0e9}, {QStringLiteral("turns"), 12},
                       {QStringLiteral("resume_command"), resume}, {QStringLiteral("fork_command"), fork}};
}

static const QString kOpen = QStringLiteral("<span style=\"background-color:#f5d76e;color:#101216;\">");
static const QString kClose = QStringLiteral("</span>");

static QJsonArray ranges(std::initializer_list<std::pair<int, int>> items) {
    QJsonArray out;
    for (const auto &item : items) out.append(QJsonArray{item.first, item.second});
    return out;
}

// ----- the search-as-you-type bench (#MDSG) ---------------------------------------------------
//
// A synthetic result page shaped like the worker's: a hundred rows, each with the match lines a
// text search returns. Nothing here reads the owner's store — setResults() takes the worker's
// JSON, so a made-up page exercises exactly the code a real one does.
static QJsonArray benchItems(int count, const QString &word) {
    QJsonArray items;
    for (int i = 0; i < count; ++i) {
        QJsonObject item = sessionItem(QStringLiteral("s%1").arg(i),
                                       QStringLiteral("Conversation %1 about the %2 index").arg(i).arg(word),
                                       QStringLiteral("project-%1").arg(i % 7));
        item.insert(QStringLiteral("summary"),
                    QStringLiteral("A long-running conversation in which the %1 index was rebuilt, the "
                                   "FTS table re-created and the pane re-measured several times.").arg(word));
        item.insert(QStringLiteral("updated"), 1.0e9 + i);
        QJsonArray matches;
        for (int m = 0; m < 3; ++m)
            matches.append(QJsonObject{{QStringLiteral("turn"), m + 1},
                                       {QStringLiteral("kind"), QStringLiteral("reply")},
                                       {QStringLiteral("line"), QStringLiteral("the %1 index is rebuilt from the "
                                                                               "session files on every start").arg(word)},
                                       {QStringLiteral("ranges"), ranges({{4, 4 + word.size()}})}});
        item.insert(QStringLiteral("matches"), matches);
        item.insert(QStringLiteral("match_count"), matches.size());
        items.append(item);
    }
    return items;
}

// CPU this process has burnt, in milliseconds: the number the profile reports as "GUI CPU".
static double cpuMs() {
    struct timespec ts {};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

class ConversationsTest : public QObject {
    Q_OBJECT
private slots:
    void projectTabsPreserveSearchAndContext() {
        SessionManager pane;
        auto *projects = new QWidget;
        auto *search = new QLineEdit(projects);
        projects->setFocusProxy(search);
        pane.insertTab(0, QStringLiteral("projects"), QStringLiteral("Projects"), projects);
        pane.addTab(QStringLiteral("globals"), QStringLiteral("Globals"), new QWidget);
        pane.setQuery(QStringLiteral("retained"));
        QString activated;
        int userSelections = 0;
        pane.onTabActivated = [&](const QString &tab) { activated = tab; };
        pane.onTabSelectedByUser = [&](const QString &) { ++userSelections; };
        pane.onTabScreen = [](const QString &tab) { return tab + QStringLiteral(" screen"); };
        pane.showTab(QStringLiteral("projects"));
        QCOMPARE(activated, QStringLiteral("projects"));
        QCOMPARE(userSelections, 0);
        QCOMPARE(pane.query(), QStringLiteral("retained"));
        QCOMPARE(pane.agentContext()->spec().screen, QStringLiteral("projects screen"));
        QCOMPARE(pane.agentContext()->spec().name, QStringLiteral("projects"));
        QCOMPARE(pane.agentContext()->spec().briefKey, QStringLiteral("projects"));
        pane.showTab(QStringLiteral("globals"));
        QCOMPARE(pane.agentContext()->spec().name, QStringLiteral("globals"));
        pane.showTab(QStringLiteral("projects"));
        QCOMPARE(pane.paneTitle(), QStringLiteral("Projects"));
        pane.setKnownProjects({{QStringLiteral("demo"), QStringLiteral("/tmp/demo")}});
        QJsonObject request;
        pane.onQuery = [&](const QJsonObject &r) { request = r; };
        pane.selectProject(QStringLiteral("/tmp/demo"));
        QCOMPARE(pane.currentTab(), QStringLiteral("sessions"));
        QCOMPARE(request.value(QStringLiteral("project")).toString(), QStringLiteral("/tmp/demo"));
        QCOMPARE(pane.query(), QStringLiteral("retained"));
        pane.selectProject(QString());
        QVERIFY(request.value(QStringLiteral("outside_projects")).toArray().contains(QStringLiteral("/tmp/demo")));
    }

    void sessionsActivationRefreshesAndClosedIsNested() {
        SessionManager manager;
        manager.addTab(QStringLiteral("projects"), QStringLiteral("Projects"), new QWidget);
        manager.addTab(QStringLiteral("globals"), QStringLiteral("Globals"), new QWidget);
        auto *closedContent = new QLabel(QStringLiteral("Closed items"));
        manager.addTab(QStringLiteral("closed"), QStringLiteral("Recently closed"), closedContent);
        auto *backgroundContent = new QLabel(QStringLiteral("Background items"));
        manager.addTab(QStringLiteral("background"), QStringLiteral("Background"), backgroundContent);
        manager.setKnownProjects({{QStringLiteral("demo"), QStringLiteral("/tmp/demo")}});
        manager.selectProject(QStringLiteral("/tmp/demo"));
        manager.setQuery(QStringLiteral("retained search"));
        manager.showTab(QStringLiteral("projects"));
        manager.resize(1200, 700);
        manager.show();
        QTest::qWait(200);  // Drain show/search debounce before measuring navigation.
        QJsonObject request;
        manager.onQuery = [&](const QJsonObject &r) { request = r; };
        auto *bar = manager.findChild<QTabBar *>();
        QCOMPARE(bar->count(), 3);
        QCOMPARE(bar->tabText(0), QStringLiteral("Sessions"));
        QCOMPARE(bar->tabText(1), QStringLiteral("Projects"));
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
        QCOMPARE(manager.currentTab(), QStringLiteral("sessions"));
        QCOMPARE(request.value(QStringLiteral("query")).toString(), QStringLiteral("retained search"));
        QCOMPARE(request.value(QStringLiteral("project")).toString(), QStringLiteral("/tmp/demo"));
        auto *closed = manager.findChild<QPushButton *>(QStringLiteral("sessionsRecentlyClosed"));
        QVERIFY(closed->isVisible());
        closed->click();
        QCOMPARE(manager.currentTab(), QStringLiteral("sessions"));
        QCOMPARE(bar->currentIndex(), 0);
        QVERIFY(closedContent->isVisible());
        QVERIFY(manager.agentContext()->spec().screen.contains(QStringLiteral("Recently closed")));
        request = {};
        manager.findChild<QPushButton *>(QStringLiteral("sessionsClosedBack"))->click();
        QVERIFY(!closedContent->isVisible());
        QVERIFY(closed->isVisible());
        QCOMPARE(request.value(QStringLiteral("query")).toString(), QStringLiteral("retained search"));
        QCOMPARE(request.value(QStringLiteral("project")).toString(), QStringLiteral("/tmp/demo"));
        auto *background = manager.findChild<QPushButton *>(QStringLiteral("sessionsBackground"));
        QVERIFY(background->isVisible());
        background->click();
        QCOMPARE(manager.currentTab(), QStringLiteral("sessions"));
        QCOMPARE(bar->currentIndex(), 0);
        QVERIFY(backgroundContent->isVisible());
        QVERIFY(manager.agentContext()->spec().screen.contains(QStringLiteral("Background sessions")));
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/sessions-background.png")));
        manager.findChild<QPushButton *>(QStringLiteral("sessionsBackgroundBack"))->click();
        QVERIFY(!backgroundContent->isVisible());
        QVERIFY(background->isVisible());
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/sessions-first.png")));
    }

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
        QCOMPARE(sinceFor(QStringLiteral("day"), now), double(now.addSecs(-86400).toSecsSinceEpoch()));
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
        dialog.resize(1000, 650);
        dialog.show();
        QCOMPARE(queries, 1);
        QCOMPARE(asked.value(QStringLiteral("scope")).toString(), QStringLiteral("project"));
        QCOMPARE(asked.value(QStringLiteral("query")).toString(), QString());
        QVERIFY(!asked.contains(QStringLiteral("since")));
        // "Everything" names every listable source, guests included (protocol 26.7), rather than
        // leaving the worker's own default — Relay's sessions and its terminal history — to decide.
        QCOMPARE(asked.value(QStringLiteral("sources")).toArray(),
                 (QJsonArray{QStringLiteral("agent"), QStringLiteral("terminal"),
                             QStringLiteral("claude"), QStringLiteral("codex")}));
        // Threads are always asked for (#AQ6X): the unticked box drops the *user's* own from the
        // tree, but a signal thread is Relay's and is listed whatever the box says.
        QVERIFY(asked.value(QStringLiteral("include_threads")).toBool());
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
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(2), QStringLiteral("4"));
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(3), QStringLiteral("1"));
        QVERIFY(tree->topLevelItem(0)->child(0)->toolTip(3).contains(QStringLiteral("needs completion")));
        QCOMPARE(tree->topLevelItem(1)->child(0)->text(3), QStringLiteral("—"));
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (!shotDir.isEmpty())
            QVERIFY(dialog.grab().save(shotDir + QStringLiteral("/turns-cell.png")));

        // Terminal history cannot be resumed.
        tree->setCurrentItem(tree->topLevelItem(1)->child(0));
        bool resumed = false;
        dialog.onResume = [&resumed](const QJsonObject &row, bool, bool) {
            resumed = row.value(QStringLiteral("session_id")).toString().startsWith(QLatin1Char('a'));
        };
        auto buttons = dialog.findChildren<QPushButton *>();
        QPushButton *resume = nullptr;
        for (QPushButton *button : buttons)
            if (button->text() == QStringLiteral("Resume")) resume = button;
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
        manager.onResume = [&resumed](const QJsonObject &, bool, bool) { resumed = true; };
        tree->setCurrentItem(threadRow);
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(opened.value(QStringLiteral("session_id")).toString(), thread);
        QVERIFY(!resumed);
    }

    // A signal thread (#AQ6X decision 9): Relay started it, so it is listed whether or not the
    // "Subagent threads" box is ticked, under its project rather than under the board worker's
    // session, marked, titled with the signal's key, and opened by Enter like any thread.
    void signalThreadsAreListedUnderTheProjectWithoutTheBox() {
        SessionManager manager;
        QJsonObject asked;
        manager.onQuery = [&asked](const QJsonObject &request) { asked = request; };
        manager.show();
        auto *box = manager.findChild<QCheckBox *>(QStringLiteral("sessionsThreads"));
        QVERIFY(box);
        QVERIFY(!box->isChecked());
        // Threads are asked for even so: a pickup nobody could see would not be "visible".
        QVERIFY(asked.value(QStringLiteral("include_threads")).toBool());

        const QString owner(32, QLatin1Char('a')), mine(32, QLatin1Char('b')), theirs(32, QLatin1Char('c'));
        auto thread = [&owner](const QString &id, const QString &type, const QString &title) {
            return QJsonObject{{QStringLiteral("session_id"), id}, {QStringLiteral("source"), QStringLiteral("subagent")},
                               {QStringLiteral("title"), title}, {QStringLiteral("project"), QStringLiteral("relay")},
                               {QStringLiteral("owner_session"), owner},
                               {QStringLiteral("owner_title"), QStringLiteral("Switchboard worker")},
                               {QStringLiteral("agent_id"), QStringLiteral("a1")},
                               {QStringLiteral("agent_type"), type},
                               {QStringLiteral("status"), QStringLiteral("running")},
                               {QStringLiteral("workspace"), QStringLiteral("/home/u/relay")},
                               {QStringLiteral("updated"), 1.0e9}};
        };
        manager.setResults({{QStringLiteral("items"), QJsonArray{
            thread(mine, QStringLiteral("signal"), QStringLiteral("ctest:panelayout")),
            thread(theirs, QStringLiteral("general"), QStringLiteral("Find it"))}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QVERIFY(tree);
        QTreeWidgetItem *group = tree->topLevelItem(0);
        QVERIFY(group);
        QCOMPARE(group->text(0), QStringLiteral("relay"));
        // One row: the user's own thread is dropped by the unticked box, the signal thread is not,
        // and it hangs straight under the project — no muted owner row for a session nobody resumes.
        QCOMPARE(group->childCount(), 1);
        QTreeWidgetItem *row = group->child(0);
        QCOMPARE(row->text(0), QStringLiteral("⚑ signal · ctest:panelayout"));
        QVERIFY(row->toolTip(0).contains(QStringLiteral("Relay started this thread itself")));
        QVERIFY(row->toolTip(0).contains(QStringLiteral("ctest:panelayout")));

        // Enter opens its history, exactly as for one of the user's own threads.
        QJsonObject opened;
        bool resumed = false;
        manager.onOpenThread = [&opened](const QJsonObject &item) { opened = item; };
        manager.onResume = [&resumed](const QJsonObject &, bool, bool) { resumed = true; };
        tree->setCurrentItem(row);
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(opened.value(QStringLiteral("session_id")).toString(), mine);
        QCOMPARE(opened.value(QStringLiteral("agent_type")).toString(), QStringLiteral("signal"));
        QVERIFY(!resumed);

        // Ticking the box adds the user's own beside it, under their owner; the signal thread
        // keeps its place in the project group.
        box->setChecked(true);
        manager.setResults({{QStringLiteral("items"), QJsonArray{
            thread(mine, QStringLiteral("signal"), QStringLiteral("ctest:panelayout")),
            thread(theirs, QStringLiteral("general"), QStringLiteral("Find it"))}}});
        group = tree->topLevelItem(0);
        QCOMPARE(group->childCount(), 2);
        QCOMPARE(group->child(0)->text(0), QStringLiteral("⚑ signal · ctest:panelayout"));
        QCOMPARE(group->child(1)->text(0), QStringLiteral("Switchboard worker"));   // the owner row
        QCOMPARE(rowChildren(group->child(1)).size(), 1);
    }

    void extraTabsAndEscape() {
        SessionManager manager;
        auto *bar = manager.findChild<QTabBar *>();
        QVERIFY(bar);
        QVERIFY(!bar->isVisibleTo(&manager));                 // one tab: no tab bar
        manager.addTab(QStringLiteral("closed"), QStringLiteral("Recently closed"), new QLabel(QStringLiteral("x")));
        QVERIFY(!bar->isVisibleTo(&manager));  // Closed items are inside Sessions.
        QCOMPARE(bar->count(), 1);
        QCOMPARE(manager.currentTab(), QStringLiteral("sessions"));
        manager.showTab(QStringLiteral("closed"));
        QCOMPARE(manager.currentTab(), QStringLiteral("sessions"));
        QCOMPARE(manager.findChild<QStackedWidget *>(QStringLiteral("sessionsPages"))->currentIndex(), 1);
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
                                                               {QStringLiteral("total_tokens"), 128000}, {QStringLiteral("requests"), 34},
                                                               {QStringLiteral("cached_tokens"), 96000}}},
                         {QStringLiteral("turns"), 2}, {QStringLiteral("thread_count"), 2},
                         {QStringLiteral("instructions"), QJsonArray{QStringLiteral("/w/CLAUDE.md")}},
                         {QStringLiteral("history"), QJsonArray{
                              QJsonObject{{QStringLiteral("turn"), 1}, {QStringLiteral("prompt"), QStringLiteral("first <b>")},
                                          {QStringLiteral("threads"), QJsonArray{thread}}},
                              QJsonObject{{QStringLiteral("turn"), 2}, {QStringLiteral("prompt"), QStringLiteral("second")}}}}};
        const QString html = renderInfo(info, now);
        for (const char *needle : {"glm-5", "GLM Coding Plan", "41.2k / 200.0k", "20.6%", "128.0k total", "34 requests",
                                   // What the provider's prefix cache served, beside the input it is
                                   // part of (#GMCF decision 5).
                                   "120.0k in", "(96.0k cached)",
                                   "not reported by this provider", "/data/s.json", "CLAUDE.md", "first &lt;b&gt;",
                                   "a1 general", "Find &amp; fix", "a2"})
            QVERIFY2(html.contains(QString::fromUtf8(needle)), needle);
        // A provider that says nothing about caching claims nothing: no "(0 cached)".
        QJsonObject quiet = info;
        quiet[QStringLiteral("usage")] = QJsonObject{{QStringLiteral("prompt_tokens"), 120000},
                                                     {QStringLiteral("completion_tokens"), 8000},
                                                     {QStringLiteral("total_tokens"), 128000},
                                                     {QStringLiteral("requests"), 34}};
        QVERIFY(!renderInfo(quiet, now).contains(QStringLiteral("cached")));
        QVERIFY(html.indexOf(QStringLiteral("Find &amp; fix")) < html.indexOf(QStringLiteral("second")));   // at its turn
        // A link survives a session directory with '&' in it.
        const int at = html.indexOf(QStringLiteral("relay-info:thread?"));
        QVERIFY(at > 0);
        const QString href = html.mid(at, html.indexOf(QLatin1Char('"'), at) - at).replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        const auto query = linkQuery(QUrl(href));
        QCOMPARE(query.value(QStringLiteral("dir")), QStringLiteral("/data/relay/sessions/d&x"));
        QCOMPARE(query.value(QStringLiteral("id")), QString(32, QLatin1Char('b')));

        // Copy (#YQC3): the id itself and the ⧉ icon share one href; it round-trips the exact id.
        auto firstCopyHref = [](const QString &html) -> QString {
            const int at = html.indexOf(QStringLiteral("relay-info:copy?"));
            return at < 0 ? QString()
                          : html.mid(at, html.indexOf(QLatin1Char('"'), at) - at)
                                    .replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        };
        QCOMPARE(html.count(QStringLiteral("<a href=\"relay-info:copy?text=%1&amp;what=session%20id\">").arg(QString(32, QLatin1Char('a')))), 2);
        QVERIFY(html.contains(QStringLiteral("⧉")));
        const QString sessionCopy = firstCopyHref(html);
        QVERIFY(!sessionCopy.isEmpty());
        QCOMPARE(linkQuery(QUrl(sessionCopy)).value(QStringLiteral("text")), QString(32, QLatin1Char('a')));
        QCOMPARE(linkQuery(QUrl(sessionCopy)).value(QStringLiteral("what")), QStringLiteral("session id"));
        // A session with no id yet has no dead copy link.
        QJsonObject noId(info);
        noId.remove(QStringLiteral("session_id"));
        QVERIFY(!renderInfo(noId, now).contains(QStringLiteral("relay-info:copy")));

        QJsonObject threadInfo{{QStringLiteral("kind"), QStringLiteral("thread")}, {QStringLiteral("thread_id"), QString(32, QLatin1Char('b'))},
                               {QStringLiteral("agent_id"), QStringLiteral("a1")}, {QStringLiteral("title"), QStringLiteral("Find it")},
                               {QStringLiteral("owner_session"), QString(32, QLatin1Char('a'))}, {QStringLiteral("owner_title"), QStringLiteral("Index work")},
                               {QStringLiteral("owner_exists"), true}, {QStringLiteral("status"), QStringLiteral("done")},
                               {QStringLiteral("history"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                                                  {QStringLiteral("text"), QStringLiteral("the task")}}}}};
        const QString threadHtml = renderInfo(threadInfo, now);
        QVERIFY(threadHtml.contains(QStringLiteral("↑ owner session: “Index work”")));
        QVERIFY(threadHtml.indexOf(QStringLiteral("↑ owner session")) < threadHtml.indexOf(QStringLiteral("the task")));
        // The thread page's Thread id copies the same way (#YQC3).
        QCOMPARE(threadHtml.count(QStringLiteral("<a href=\"relay-info:copy?text=%1&amp;what=thread%20id\">").arg(QString(32, QLatin1Char('b')))), 2);
        const QString threadCopy = firstCopyHref(threadHtml);
        QVERIFY(!threadCopy.isEmpty());
        QCOMPARE(linkQuery(QUrl(threadCopy)).value(QStringLiteral("text")), QString(32, QLatin1Char('b')));
        QCOMPARE(linkQuery(QUrl(threadCopy)).value(QStringLiteral("what")), QStringLiteral("thread id"));
        QCOMPARE(compactNumber(812), QStringLiteral("812"));
        QCOMPARE(compactNumber(1300000), QStringLiteral("1.3M"));
    }

    void infoShowsTaskAndTurnUsage() {
        const QJsonObject turn{{QStringLiteral("turn"), 2}, {QStringLiteral("model"), QStringLiteral("glm-5")},
                               {QStringLiteral("source"), QStringLiteral("guest")},
                               {QStringLiteral("prompt_tokens"), 12000}, {QStringLiteral("cached_tokens"), 9000},
                               {QStringLiteral("last_prompt_tokens"), 8000}, {QStringLiteral("prefix_changes"), 1},
                               {QStringLiteral("handover_tokens"), 4000}, {QStringLiteral("cost_estimate"), 0.025}};
        const QJsonObject info{{QStringLiteral("kind"), QStringLiteral("session")},
                        {QStringLiteral("title"), QStringLiteral("Usage from a long task")},
                        {QStringLiteral("live"), true},
                        {QStringLiteral("usage"), QJsonObject{{QStringLiteral("prompt_tokens"), 12000},
                                                                {QStringLiteral("total_tokens"), 12000}}},
                        {QStringLiteral("children_count"), 1},
                        {QStringLiteral("children_usage"), QJsonObject{{QStringLiteral("prompt_tokens"), 5000},
                                                                         {QStringLiteral("total_tokens"), 5000}}},
                        {QStringLiteral("task_usage"), QJsonObject{{QStringLiteral("prompt_tokens"), 17000},
                                                                     {QStringLiteral("total_tokens"), 17000}}},
                        {QStringLiteral("turns_usage"), QJsonArray{turn}}};
        const QString html = relay::sessioninfo::renderInfo(info, QDateTime::currentDateTime());
        QVERIFY(html.contains(QStringLiteral("Task total")));
        QVERIFY(html.contains(QStringLiteral("Usage by turn")));
        QVERIFY(html.contains(QStringLiteral("guest reported")));
        QVERIFY(html.contains(QStringLiteral("handover ~4000 tokens")));
        QVERIFY(html.contains(QStringLiteral("1 prefix change(s)")));
        QVERIFY(html.contains(QStringLiteral("OpenRouter list-price estimate")));
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (!shotDir.isEmpty()) {
            relay::sessioninfo::InfoView view;
            QJsonObject request;
            view.onRequest = [&request](const QJsonObject &sent) { request = sent; };
            view.resize(850, 650);
            view.show();
            view.showLiveSession();
            QJsonObject event = info;
            event.insert(QStringLiteral("id"), request.value(QStringLiteral("id")));
            view.setInfo(event);
            QApplication::processEvents();
            QVERIFY(view.grab().save(shotDir + QStringLiteral("/usage-info.png")));
        }
    }

    void paneInfoPopoverCopiesAndKeepsTheInfoClick() {
        using namespace relay::sessioninfo;
        QWidget pane;
        pane.resize(320, 180);
        InfoButton info(&pane);
        info.setObjectName(QStringLiteral("paneInfoAnchor"));
        info.move(280, 4);
        info.show();
        PaneInfoPopover popover(&pane, &info, QStringLiteral("a1b2c3d4"));
        int infoClicks = 0, dimClicks = 0;
        connect(&info, &QToolButton::clicked, &pane, [&] { ++infoClicks; });
        popover.onToggleDim = [&] { ++dimClicks; };
        pane.show();

        // Hover opens it, and the delayed close is cancelled when the pointer crosses from the
        // circle-i onto the popover. This is what makes its buttons genuinely reachable.
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(&info, &enter);
        QVERIFY(popover.isVisible());
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(&info, &leave);
        QEvent enterPopover(QEvent::Enter);
        QApplication::sendEvent(&popover, &enterPopover);
        QTest::qWait(260);
        QVERIFY(popover.isVisible());

        auto buttons = popover.findChildren<QToolButton *>();
        QToolButton *copy = nullptr, *dim = nullptr;
        for (QToolButton *button : buttons) {
            if (button->accessibleName() == QStringLiteral("Copy pane ID")) copy = button;
            if (button->accessibleName() == QStringLiteral("Dim pane")) dim = button;
        }
        QVERIFY(copy);
        QVERIFY(dim);
        QTest::mouseClick(copy, Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("a1b2c3d4"));
        QCOMPARE(copy->text(), QStringLiteral("Copied"));
        QTest::mouseClick(dim, Qt::LeftButton);
        QCOMPARE(dimClicks, 1);

        // Dimming state changes the low-frequency action in place; it never adds a header button.
        popover.setDimState(90, true);
        QVERIFY(dim->isChecked());
        QVERIFY(dim->text().contains(QStringLiteral("Restore automatic")));
        // Hovering the dim button names the toggle's key, read from the live keymap so a
        // rebinding reads correctly (nothing is appended when the action is unbound).
        const QString dimKeys = Keymap::instance().shortcutText(
            QStringLiteral("pane.dimToggle"));
        QVERIFY(!dimKeys.isEmpty());
        QVERIFY2(dim->toolTip().contains(QStringLiteral("(%1)").arg(dimKeys)),
                 qPrintable(dim->toolTip()));
        QVERIFY(dim->toolTip().contains(QStringLiteral("Alt+wheel")));
        popover.setDimState(0, false);
        QVERIFY(dim->toolTip().contains(QStringLiteral("(%1)").arg(dimKeys)));
        QTest::mouseClick(&info, Qt::LeftButton);
        QCOMPARE(infoClicks, 1);   // the circle-i's original Conversation info action is intact

        info.clearFocus();
        QApplication::processEvents();
        popover.hide();
        info.setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(popover.isVisible());   // keyboard users get the same surface as hover
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
        // Copy (#YQC3): a copy link puts its text on the clipboard and borrows the hint line.
        auto *hintLabel = view.findChild<QLabel *>(QStringLiteral("dialogHint"));
        QVERIFY(hintLabel);
        QMetaObject::invokeMethod(view.findChild<QTextBrowser *>(), "anchorClicked",
                                  Q_ARG(QUrl, QUrl(QStringLiteral("relay-info:copy?text=abc&what=session%20id"))));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("abc"));
        QCOMPARE(hintLabel->text(), QStringLiteral("Copied session id to the clipboard"));
        view.back();
        QVERIFY(!asked.last().contains(QStringLiteral("thread_id")));
        bool closed = false;
        view.onClose = [&closed] { closed = true; };
        QTest::keyClick(view.findChild<QTextBrowser *>(), Qt::Key_Escape);
        QVERIFY(closed);
    }

    // A shaky click on the ⧉ copy button: once the pointer crosses into the next character
    // QTextBrowser drops the anchor and selects the glyph instead, no anchorClicked fires, and
    // copy-on-select then puts "⧉" itself on the clipboard. The view rescues a press-and-release
    // that stay on the same copy link, so the clipboard still gets the id.
    void infoViewShakyClickOnCopyStillCopiesTheId() {
        using namespace relay::sessioninfo;
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        InfoView view;
        QList<QJsonObject> asked;
        view.onRequest = [&asked](const QJsonObject &request) { asked << request; };
        view.showLiveSession();
        QCOMPARE(asked.size(), 1);
        view.setInfo({{QStringLiteral("id"), asked.last().value(QStringLiteral("id"))},
                      {QStringLiteral("kind"), QStringLiteral("session")}, {QStringLiteral("live"), true},
                      {QStringLiteral("session_id"), QString(32, QLatin1Char('a'))}});
        view.resize(600, 400);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *body = view.findChild<QTextBrowser *>();
        QVERIFY(body);
        const QTextCursor found = body->document()->find(QStringLiteral("⧉"));
        QVERIFY(!found.isNull());
        QTextCursor atStart(body->document()), atEnd(body->document());
        atStart.setPosition(found.selectionStart());
        atEnd.setPosition(found.selectionEnd());
        const int left = body->cursorRect(atStart).left();
        const int right = body->cursorRect(atEnd).left();
        const int y = body->cursorRect(atStart).center().y();
        QVERIFY(right - left > 4);
        auto send = [&](QEvent::Type type, const QPoint &pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, pos, button, buttons, Qt::NoModifier);
            QApplication::sendEvent(body->viewport(), &event);
        };
        send(QEvent::MouseButtonPress, QPoint(left + 1, y), Qt::LeftButton, Qt::LeftButton);
        for (int x = left + 2; x < right; ++x)
            send(QEvent::MouseMove, QPoint(x, y), Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, QPoint(right - 1, y), Qt::LeftButton, Qt::NoButton);
        // The gesture really did turn into a selection — the bug's first step.
        QVERIFY(body->textCursor().hasSelection());
        QTRY_COMPARE(QApplication::clipboard()->text(), QString(32, QLatin1Char('a')));
        QVERIFY(!body->textCursor().hasSelection());
    }

    // The Ask row at the foot of the ⓘ pane (#FEJQ). This pane has no helper agent of its own:
    // the row drafts a question about the figures on screen into the owning pane's composer and
    // sends nothing. src/AskRow.h holds the wording, which the Activity pane's row shares.
    void infoViewAskRowDraftsAboutTheSessionOnScreen() {
        using namespace relay::sessioninfo;
        InfoView view;
        relay::askrow::AskRow *row = view.askRow();
        QVERIFY(row != nullptr);
        QVERIFY(row->isHidden());                 // no composer wired: no row at all

        QStringList drafted;
        QList<QJsonObject> asked;
        view.onAskOwner = [&drafted](const QString &text) { drafted << text; };
        view.onRequest = [&asked](const QJsonObject &request) { asked << request; };
        view.show();
        QVERIFY(!row->isHidden());
        QVERIFY(!row->available());               // the figures have not arrived yet

        view.showLiveSession();
        QCOMPARE(asked.size(), 1);
        view.setInfo({{QStringLiteral("id"), asked.last().value(QStringLiteral("id"))},
                      {QStringLiteral("kind"), QStringLiteral("session")}, {QStringLiteral("live"), true},
                      {QStringLiteral("title"), QStringLiteral("Mine")},
                      {QStringLiteral("context"), QJsonObject{{QStringLiteral("used_tokens"), 41200},
                                                              {QStringLiteral("window"), 200000},
                                                              {QStringLiteral("percent"), 20.6}}}});
        QVERIFY(row->available());
        QCOMPARE(row->chipLabels(), QStringList({QStringLiteral("Context · 20.6%"),
                                                 QStringLiteral("What it has done"),
                                                 QStringLiteral("Costliest turn")}));
        // The chip, the page and the question all say the same figures.
        QVERIFY(row->draftAt(0).contains(QStringLiteral("20.6%")));
        QVERIFY(row->draftAt(0).contains(QStringLiteral("41.2k / 200.0k")));
        row->chips().at(0)->click();
        QCOMPARE(drafted, QStringList{row->draftAt(0)});
        row->chips().at(2)->click();
        QCOMPARE(drafted.size(), 2);
        QCOMPARE(drafted.last(), relay::askrow::costliestTurnQuestion());
        QCOMPARE(asked.size(), 1);                // a draft asked the worker nothing

        // A subagent thread is somebody else's session: the pane's agent cannot answer for it.
        view.showThread(QString(32, QLatin1Char('b')), QStringLiteral("/d"), QString(32, QLatin1Char('a')));
        view.setInfo({{QStringLiteral("id"), asked.last().value(QStringLiteral("id"))},
                      {QStringLiteral("kind"), QStringLiteral("thread")}, {QStringLiteral("live"), true},
                      {QStringLiteral("agent_id"), QStringLiteral("a1")}, {QStringLiteral("title"), QStringLiteral("Find it")}});
        QVERIFY(!row->available());
        QVERIFY(row->chips().first()->toolTip().contains(QStringLiteral("subagent thread")));
        row->chips().at(0)->click();
        QCOMPARE(drafted.size(), 2);              // a disabled chip drafts nothing

        // A saved session from the manager is not this pane's either, and says so differently.
        view.showSession(QString(32, QLatin1Char('c')), QStringLiteral("/d"));
        view.setInfo({{QStringLiteral("id"), asked.last().value(QStringLiteral("id"))},
                      {QStringLiteral("kind"), QStringLiteral("session")}, {QStringLiteral("live"), false},
                      {QStringLiteral("title"), QStringLiteral("Theirs")}});
        QVERIFY(!row->available());
        QVERIFY(row->chips().first()->toolTip().contains(QStringLiteral("saved session")));
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
        // Live usage (issue #D03W) comes last, after every badge whose text is fixed: the
        // delegate stops drawing at the row's edge, so a varying tag in the middle would push
        // "unfinished" and "edits · 3 files" off a narrow Sessions pane and move them about as
        // the number changed. An empty tag is absent.
        QCOMPARE(badges(item, true, QString(), QStringLiteral("cpu 12% · mem 3%")),
                 QStringList({QStringLiteral("pinned"), QStringLiteral("open"),
                              QStringLiteral("unfinished"), QStringLiteral("edits · 3 files"),
                              QStringLiteral("feature/x"), QStringLiteral("cpu 12% · mem 3%")}));
        QCOMPARE(badges(item, true, QString(), QStringLiteral("cpu 12% · mem 3%")).last(),
                 QStringLiteral("cpu 12% · mem 3%"));
        // The badges before it are exactly the ones it would have displaced.
        QStringList withoutTag = badges(item, true, QString(), QStringLiteral("cpu 12% · mem 3%"));
        withoutTag.removeLast();
        QCOMPARE(withoutTag, badges(item, true, QString(), QString()));
        QCOMPARE(badges(item, true, QString(), QString()).contains(QStringLiteral("cpu 12% · mem 3%")), false);
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

    // Card #MDL1, rule 1: history recorded the id the API took and that stays on disk, but every
    // cell a person reads prints the model's name.
    void theModelColumnAndTheInfoPanelPrintNames() {
        SessionManager manager;
        manager.setResults(QJsonObject{
            {QStringLiteral("items"), QJsonArray{
                QJsonObject{{QStringLiteral("session_id"), QStringLiteral("aaaaaaaa")},
                            {QStringLiteral("title"), QStringLiteral("On OpenRouter")},
                            {QStringLiteral("project"), QStringLiteral("relay")},
                            {QStringLiteral("model"), QStringLiteral("openai/gpt-6-sol")},
                            {QStringLiteral("updated"), 1000.0}},
                // The worker names the one the derivation cannot reach: the Kimi Coding Plan's
                // "k3" is Kimi K3, and only its catalog row says so.
                QJsonObject{{QStringLiteral("session_id"), QStringLiteral("bbbbbbbb")},
                            {QStringLiteral("title"), QStringLiteral("On the coding plan")},
                            {QStringLiteral("project"), QStringLiteral("relay")},
                            {QStringLiteral("model"), QStringLiteral("k3")},
                            {QStringLiteral("model_name"), QStringLiteral("kimi-k3")},
                            {QStringLiteral("updated"), 900.0}}}}});
        QStringList cells;
        for (QTreeWidgetItem *row : manager.findChildren<QTreeWidget *>().first()->findItems(
                 QString(), Qt::MatchContains | Qt::MatchRecursive))
            if (!row->text(4).isEmpty()) cells << row->text(4);
        QVERIFY(cells.contains(QStringLiteral("gpt-6-sol")));    // no vendor prefix
        QVERIFY(cells.contains(QStringLiteral("kimi-k3")));        // the coding plan's "k3"
        QVERIFY(!cells.contains(QStringLiteral("openai/gpt-6-sol")));
        QVERIFY(!cells.contains(QStringLiteral("k3")));
        // The ⓘ panel names it the same way, and lists one model once however it was spelled.
        const QString html = relay::sessioninfo::renderInfo(
            QJsonObject{{QStringLiteral("kind"), QStringLiteral("session")},
                        {QStringLiteral("title"), QStringLiteral("A conversation")},
                        {QStringLiteral("model"), QStringLiteral("MiniMax-M3")},
                        {QStringLiteral("models"), QJsonArray{QStringLiteral("k3"),
                                                              QStringLiteral("kimi-k3"),
                                                              QStringLiteral("openai/gpt-6-sol")}},
                        {QStringLiteral("models_named"), QJsonArray{QStringLiteral("kimi-k3"),
                                                                    QStringLiteral("gpt-6-sol")}}},
            QDateTime::currentDateTime());
        QVERIFY(html.contains(QStringLiteral("minimax-m3")));
        // The provider beside it says which key is spending, and does not name the model again:
        // three preset labels carry the model id, which read as "glm-5.3 · z.ai · glm-5.3 ·
        // standard api (glm)" before card #MDL1.
        const QString withProvider = relay::sessioninfo::renderInfo(
            QJsonObject{{QStringLiteral("kind"), QStringLiteral("session")},
                        {QStringLiteral("model"), QStringLiteral("glm-5.3")},
                        {QStringLiteral("provider"), QStringLiteral("z.ai · glm-5.3 · standard api (glm)")}},
            QDateTime::currentDateTime());
        QVERIFY(withProvider.contains(QStringLiteral("z.ai · standard api (glm)")));
        QVERIFY(!withProvider.contains(QStringLiteral("z.ai · glm-5.3 · standard api")));
        QVERIFY(!html.contains(QStringLiteral("MiniMax-M3")));
        QVERIFY(html.contains(QStringLiteral("kimi-k3, gpt-6-sol")));
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
        // A recently closed session remains in its project's group.
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("relay"));
        QTreeWidgetItem *indexed = rowTitled(tree, QStringLiteral("Index work"));
        QTreeWidgetItem *opened = rowTitled(tree, QStringLiteral("Open elsewhere"));
        QTreeWidgetItem *justClosed = rowTitled(tree, QStringLiteral("Just closed"));
        QVERIFY(indexed && opened && justClosed);
        QCOMPARE(justClosed->parent(), tree->topLevelItem(0));
        QCOMPARE(indexed->text(6), QStringLiteral("Rebuilt the conversation index."));
        QCOMPARE(indexed->data(0, Qt::UserRole + 4).toString(), QString());
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

    void openBadgeDistinguishesGuestSources() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.setOpenSessions({QStringLiteral("claude:shared-id")});
        manager.show();
        manager.setResults({{QStringLiteral("items"), QJsonArray{
            guestItem(QStringLiteral("claude"), QStringLiteral("shared-id"), QStringLiteral("Claude session")),
            guestItem(QStringLiteral("codex"), QStringLiteral("shared-id"), QStringLiteral("Codex session")),
            sessionItem(QStringLiteral("shared-id"), QStringLiteral("Relay session"))}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        auto isOpen = [tree](const QString &title) {
            QTreeWidgetItem *row = rowTitled(tree, title);
            return row && row->data(0, kBadgeRole).toStringList().contains(QStringLiteral("open"));
        };
        QVERIFY(isOpen(QStringLiteral("Claude session")));
        QVERIFY(!isOpen(QStringLiteral("Codex session")));
        QVERIFY(!isOpen(QStringLiteral("Relay session")));
        manager.setOpenSessions({QStringLiteral("codex:shared-id")});
        QVERIFY(!isOpen(QStringLiteral("Claude session")));
        QVERIFY(isOpen(QStringLiteral("Codex session")));
    }

    void sessionRowsExposeFullIds() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.setProject(QStringLiteral("relay"));
        manager.show();
        const QString id = QStringLiteral("01a0ce9a-41c6-75a2-9ffc-1036534eafd3");
        manager.setResults({{QStringLiteral("items"), QJsonArray{
            sessionItem(id, QStringLiteral("Index work"))}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        auto *row = rowTitled(tree, QStringLiteral("Index work"));
        QVERIFY(row);
        QCOMPARE(row->data(0, Qt::UserRole + 1).toString(), id);
        QVERIFY(row->toolTip(0).contains(QStringLiteral("Session ID: ") + id));
        QApplication::clipboard()->clear();
        bool offeredCopy = false;
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            for (QAction *action : menu->actions()) {
                if (action->text() == QStringLiteral("Copy session ID")) {
                    offeredCopy = true;
                    action->trigger();
                }
            }
            menu->close();
        });
        const QPoint point = tree->visualItemRect(row).center();
        QVERIFY(QMetaObject::invokeMethod(tree, "customContextMenuRequested", Q_ARG(QPoint, point)));
        QVERIFY(offeredCopy);
        QCOMPARE(QApplication::clipboard()->text(), id);
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/sessions-with-ids.png")));
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

    // The live usage tag (issue #D03W) arrives with the window's status poll and leaves the same
    // way, on rows that are already built — no new query, no rebuild that would lose an unfold.
    void liveUsageTagsComeAndGo() {
        SessionManager manager;
        int queries = 0;
        manager.onQuery = [&queries](const QJsonObject &) { ++queries; };
        manager.setOpenSessions({QStringLiteral("a")});
        manager.show();
        manager.setResults({{QStringLiteral("items"),
                             QJsonArray{sessionItem(QStringLiteral("a"), QStringLiteral("Building"))}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QTreeWidgetItem *row = rowTitled(tree, QStringLiteral("Building"));
        QVERIFY(row);
        QVERIFY(row->data(0, kBadgeRole).toStringList().contains(QStringLiteral("open")));
        QVERIFY(!row->data(0, kBadgeRole).toStringList().contains(QStringLiteral("cpu")));
        const int asked = queries;

        // The pane's reading rises: the tag appears beside "open", without asking the worker.
        manager.setLiveUsage({{QStringLiteral("a"), QStringLiteral("cpu 12% · mem 3%")}});
        row = rowTitled(tree, QStringLiteral("Building"));
        QVERIFY(row);
        const QStringList busy = row->data(0, kBadgeRole).toStringList();
        QVERIFY(busy.contains(QStringLiteral("open")));
        QVERIFY(busy.contains(QStringLiteral("cpu 12% · mem 3%")));
        QVERIFY(busy.indexOf(QStringLiteral("open")) < busy.indexOf(QStringLiteral("cpu 12% · mem 3%")));
        QCOMPARE(queries, asked);

        // A new number is a patch, not a rebuild.
        manager.setLiveUsage({{QStringLiteral("a"), QStringLiteral("cpu 14% · mem 3%")}});
        row = rowTitled(tree, QStringLiteral("Building"));
        QVERIFY(row && row->data(0, kBadgeRole).toStringList().contains(QStringLiteral("cpu 14% · mem 3%")));

        // The pane goes quiet: the tag goes, "open" stays.
        manager.setLiveUsage({});
        row = rowTitled(tree, QStringLiteral("Building"));
        QVERIFY(row);
        const QStringList idle = row->data(0, kBadgeRole).toStringList();
        QVERIFY(idle.contains(QStringLiteral("open")));
        QVERIFY(!idle.join(QLatin1Char(' ')).contains(QStringLiteral("cpu")));
        QCOMPARE(queries, asked);
    }

    void unfoldAsksOnceAndFillsFromTheOverview() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        QStringList asked;
        manager.onPreview = [&asked](const QString &id, const QString &) { asked << id; };
        manager.show();
        manager.setResults({{QStringLiteral("items"), QJsonArray{sessionItem(QStringLiteral("a"), QStringLiteral("First")),
                                                                sessionItem(QStringLiteral("b"), QStringLiteral("Second"))}}});
        QVERIFY(asked.isEmpty());      // selecting a row no longer loads its full preview
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

    void searchingKeepsNewestFirstUntilTheUserChoosesAnotherSort() {
        SessionManager manager;
        QList<QJsonObject> asked;
        manager.onQuery = [&asked](const QJsonObject &request) { asked << request; };
        manager.resize(900, 650);
        manager.show();
        QVERIFY(!asked.last().contains(QStringLiteral("sort")));         // listing: newest first
        manager.setQuery(QStringLiteral("pelican"));
        QTest::qWait(200);                                              // the box is debounced
        QVERIFY(!asked.last().contains(QStringLiteral("sort")));
        QJsonObject newer = sessionItem(QStringLiteral("newer"), QStringLiteral("Pelican: latest"));
        QJsonObject older = sessionItem(QStringLiteral("older"), QStringLiteral("Pelican: earlier"));
        const double now = double(QDateTime::currentSecsSinceEpoch());
        newer.insert(QStringLiteral("updated"), now);
        older.insert(QStringLiteral("updated"), now - 86400);
        newer.insert(QStringLiteral("match_count"), 1);
        older.insert(QStringLiteral("match_count"), 1);
        manager.setResults({{QStringLiteral("items"), QJsonArray{newer, older}}});
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/newest-first-search.png")));
        manager.setQuery(QString());
        QTest::qWait(200);
        QVERIFY(!asked.last().contains(QStringLiteral("sort")));
        // Best match is still available explicitly and stands as the query changes.
        auto *sort = manager.findChild<QComboBox *>(QStringLiteral("sessionsSort"));
        QCOMPARE(sort->currentData().toString(), QStringLiteral("recent"));
        sort->setCurrentIndex(sort->findData(QStringLiteral("relevance")));
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("relevance"));
        manager.setQuery(QStringLiteral("otter"));
        QTest::qWait(200);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("relevance"));
        sort->setCurrentIndex(sort->findData(QStringLiteral("longest")));
        emit sort->activated(sort->currentIndex());
        manager.setQuery(QStringLiteral("pelican"));
        QTest::qWait(200);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("longest"));
    }

    void headerClickSortsByThatColumn() {
        // The helpers the click and the arrow run on (src/Conversations.cpp).
        QCOMPARE(nextHeaderSort(1, QStringLiteral("recent")), QStringLiteral("oldest"));
        QCOMPARE(nextHeaderSort(1, QStringLiteral("oldest")), QStringLiteral("recent"));
        QCOMPARE(nextHeaderSort(1, QStringLiteral("title")), QStringLiteral("recent"));
        QCOMPARE(nextHeaderSort(2, QStringLiteral("longest")), QStringLiteral("shortest"));
        QCOMPARE(nextHeaderSort(2, QStringLiteral("anything")), QStringLiteral("longest"));
        QCOMPARE(nextHeaderSort(3, QStringLiteral("requests_desc")), QStringLiteral("requests"));
        QCOMPARE(nextHeaderSort(3, QStringLiteral("recent")), QStringLiteral("requests_desc"));
        QCOMPARE(nextHeaderSort(0, QStringLiteral("title")), QStringLiteral("title_desc"));
        QCOMPARE(nextHeaderSort(0, QStringLiteral("title_desc")), QStringLiteral("title"));
        QCOMPARE(nextHeaderSort(4, QStringLiteral("model")), QStringLiteral("model_desc"));
        QCOMPARE(nextHeaderSort(4, QStringLiteral("relevance")), QStringLiteral("model"));
        QCOMPARE(nextHeaderSort(6, QStringLiteral("summary")), QStringLiteral("summary_desc"));
        QCOMPARE(nextHeaderSort(6, QStringLiteral("relevance")), QStringLiteral("summary"));
        QCOMPARE(nextHeaderSort(5, QStringLiteral("recent")), QStringLiteral("tokens_desc"));
        QCOMPARE(nextHeaderSort(5, QStringLiteral("tokens_desc")), QStringLiteral("tokens"));
        QCOMPARE(headerSortColumn(QStringLiteral("recent")), 1);
        QCOMPARE(headerSortColumn(QStringLiteral("shortest")), 2);
        QCOMPARE(headerSortColumn(QStringLiteral("requests_desc")), 3);
        QCOMPARE(headerSortColumn(QStringLiteral("title_desc")), 0);
        QCOMPARE(headerSortColumn(QStringLiteral("model")), 4);
        QCOMPARE(headerSortColumn(QStringLiteral("summary_desc")), 6);
        QCOMPARE(headerSortColumn(QStringLiteral("tokens_desc")), 5);
        QCOMPARE(headerSortColumn(QStringLiteral("relevance")), -1);
        QCOMPARE(int(headerSortOrder(QStringLiteral("title"))), int(Qt::AscendingOrder));
        QCOMPARE(int(headerSortOrder(QStringLiteral("requests"))), int(Qt::AscendingOrder));
        QCOMPARE(int(headerSortOrder(QStringLiteral("longest"))), int(Qt::DescendingOrder));

        SessionManager manager;
        QList<QJsonObject> asked;
        manager.onQuery = [&asked](const QJsonObject &request) { asked << request; };
        manager.show();
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        auto *sort = manager.findChild<QComboBox *>(QStringLiteral("sessionsSort"));
        QHeaderView *header = tree->header();
        QVERIFY(header->sectionsClickable());
        // Newest-first listing carries the arrow on Updated, including while searching.
        QCOMPARE(header->sortIndicatorSection(), 1);
        QCOMPARE(int(header->sortIndicatorOrder()), int(Qt::DescendingOrder));
        manager.setQuery(QStringLiteral("pelican"));
        QTest::qWait(200);                                              // the box is debounced
        QVERIFY(!asked.last().contains(QStringLiteral("sort")));
        QVERIFY(header->isSortIndicatorShown());
        QCOMPARE(header->sortIndicatorSection(), 1);
        QCOMPARE(int(header->sortIndicatorOrder()), int(Qt::DescendingOrder));
        manager.setQuery(QString());
        QTest::qWait(200);
        QVERIFY(header->isSortIndicatorShown());

        // Updated: oldest first, and a second choice toggles back; the combo follows both ways.
        // The header itself is hidden since #1Q5V — sorting is the Sort ▾ menu's job — so
        // the test drives the signal a header click used to carry; the wiring under it is
        // unchanged.
        emit header->sectionClicked(1);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("oldest"));
        QCOMPARE(sort->currentData().toString(), QStringLiteral("oldest"));
        QCOMPARE(header->sortIndicatorSection(), 1);
        QCOMPARE(int(header->sortIndicatorOrder()), int(Qt::AscendingOrder));
        emit header->sectionClicked(1);
        QVERIFY(!asked.last().contains(QStringLiteral("sort")));        // newest first is the default
        QCOMPARE(sort->currentData().toString(), QStringLiteral("recent"));
        QCOMPARE(int(header->sortIndicatorOrder()), int(Qt::DescendingOrder));

        // Session by title; the click is the user's own sort, so typing does not take it back.
        emit header->sectionClicked(0);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("title"));
        QCOMPARE(sort->currentData().toString(), QStringLiteral("title"));
        QCOMPARE(header->sortIndicatorSection(), 0);
        QCOMPARE(int(header->sortIndicatorOrder()), int(Qt::AscendingOrder));
        manager.setQuery(QStringLiteral("pelican"));
        QTest::qWait(200);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("title"));
        QCOMPARE(header->sortIndicatorSection(), 0);

        // Turns, Requests, Model and Recap reach their own pairs too.
        emit header->sectionClicked(2);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("longest"));
        emit header->sectionClicked(2);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("shortest"));
        QCOMPARE(int(header->sortIndicatorOrder()), int(Qt::AscendingOrder));
        emit header->sectionClicked(3);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("requests_desc"));
        QCOMPARE(header->sortIndicatorSection(), 3);
        emit header->sectionClicked(3);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("requests"));
        QCOMPARE(int(header->sortIndicatorOrder()), int(Qt::AscendingOrder));
        emit header->sectionClicked(4);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("model"));
        QCOMPARE(header->sortIndicatorSection(), 4);
        emit header->sectionClicked(6);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("summary"));
        QCOMPARE(header->sortIndicatorSection(), 6);
        emit header->sectionClicked(6);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("summary_desc"));
        emit header->sectionClicked(5);
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("tokens_desc"));
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
        // Since #1Q5V the branch combo never shows: Branch ▸ in More carries it, and it
        // appears only when the facets name more than one branch.
        auto *branchMenu = manager.findChild<QMenu *>(QStringLiteral("sessionsBranchMenu"));
        QVERIFY(branchMenu);
        QVERIFY(branchMenu->menuAction()->isVisible());
        QVERIFY(branchMenu->actions().size() >= 3);   // Any branch, main, work
        auto *branch = manager.findChild<QComboBox *>(QStringLiteral("sessionsBranch"));
        QVERIFY(!branch->isVisibleTo(&manager));
        branch->setCurrentIndex(branch->findData(QStringLiteral("work")));
        QCOMPARE(asked.last().value(QStringLiteral("branch")).toString(), QStringLiteral("work"));
    }

    void groupingKeepsUnfinishedSessionsInTheirRegularPlace() {
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
        // Both sessions sit under their project, including the unfinished one.
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("relay"));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("Today's work"));
        QCOMPARE(tree->topLevelItem(0)->child(1)->text(0), QStringLiteral("Last month"));
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/sessions-by-project.png")));
        // Grouped by date it is in its date group too: a date group with a hole in it would lie.
        auto *group = manager.findChild<QComboBox *>(QStringLiteral("sessionsGroup"));
        group->setCurrentIndex(group->findData(QStringLiteral("date")));
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Today"));
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("Today's work"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Older"));
        // With no grouping, each session is a top-level row.
        group->setCurrentIndex(group->findData(QStringLiteral("none")));
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Today's work"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Last month"));
        // The supplied search results keep the same grouping.
        manager.setQuery(QStringLiteral("month"));
        manager.setResults({{QStringLiteral("items"), QJsonArray{today, older}}});
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Today's work"));
    }

    void sessionsDropdownsRespondToMouseChoices() {
        SessionManager manager;
        QList<QJsonObject> asked;
        manager.onQuery = [&asked](const QJsonObject &request) { asked << request; };
        manager.setKnownProjects({{QStringLiteral("alpha"), QStringLiteral("/tmp/alpha")},
                                  {QStringLiteral("beta"), QStringLiteral("/tmp/beta")}});
        manager.resize(1000, 700);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));
        QJsonObject today = sessionItem(QStringLiteral("a"), QStringLiteral("Alpha work"), QStringLiteral("alpha"));
        today.insert(QStringLiteral("updated"), double(QDateTime::currentSecsSinceEpoch()));
        QJsonObject older = sessionItem(QStringLiteral("b"), QStringLiteral("Beta work"), QStringLiteral("beta"));
        older.insert(QStringLiteral("updated"), double(QDateTime::currentDateTime().addDays(-40).toSecsSinceEpoch()));
        const QJsonArray items{today, older};
        manager.setResults({{QStringLiteral("items"), items},
                            {QStringLiteral("facets"), QJsonObject{
                                {QStringLiteral("models"), QJsonArray{QStringLiteral("glm-5")}},
                                {QStringLiteral("branches"), QJsonArray{QStringLiteral("main"), QStringLiteral("work")}}}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        auto *group = manager.findChild<QComboBox *>(QStringLiteral("sessionsGroup"));
        auto *sortButton = manager.findChild<QToolButton *>(QStringLiteral("sessionsSort"));
        QVERIFY(tree && group && sortButton && sortButton->menu());
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("alpha"));
        // Grouping and sort ride in Sort ▾ now (#1Q5V); the choices deliver the same
        // fields they always did.
        QVERIFY(chooseMenuAction(sortButton->menu(), QStringLiteral("date")));
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Today"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Older"));
        manager.setResults({{QStringLiteral("items"), items}});
        QCOMPARE(group->currentData().toString(), QStringLiteral("date"));
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Today"));
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/by-date.png")));
        QVERIFY(chooseMenuAction(sortButton->menu(), QStringLiteral("none")));
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Alpha work"));
        QVERIFY(chooseMenuAction(sortButton->menu(), QStringLiteral("project")));
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("alpha"));
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/by-project.png")));

        auto *kind = manager.findChild<QComboBox *>(QStringLiteral("sessionsKind"));
        auto *project = manager.findChild<QComboBox *>(QStringLiteral("sessionsProject"));
        auto *scope = manager.findChild<QComboBox *>(QStringLiteral("sessionsScope"));
        auto *sort = manager.findChild<QComboBox *>(QStringLiteral("sessionsSort"));
        auto *branch = manager.findChild<QComboBox *>(QStringLiteral("sessionsBranch"));
        auto *model = manager.findChild<QComboBox *>(QStringLiteral("sessionsModel"));
        auto *date = manager.findChild<QComboBox *>(QStringLiteral("sessionsDate"));
        QVERIFY(kind && project && scope && sort && branch && model && date);
        // Source, Scope and Time ride in More, Branch in its own submenu; Project and
        // Model are the two combos that stayed on the row, and they still take the mouse.
        QVERIFY(chooseMenuAction(manager.findChild<QMenu *>(QStringLiteral("sessionsSourceMenu")),
                                 QStringLiteral("agent")));
        QCOMPARE(asked.last().value(QStringLiteral("sources")).toArray(), QJsonArray{QStringLiteral("agent")});
        QVERIFY(chooseComboItem(project, QStringLiteral("/tmp/beta")));
        QCOMPARE(asked.last().value(QStringLiteral("project")).toString(), QStringLiteral("/tmp/beta"));
        manager.setResults({{QStringLiteral("items"), items}, {QStringLiteral("scope"), QStringLiteral("all")}});
        QVERIFY(chooseMenuAction(manager.findChild<QMenu *>(QStringLiteral("sessionsScopeMenu")),
                                 QStringLiteral("project")));
        QCOMPARE(project->currentData().toString(), QString());
        QCOMPARE(asked.last().value(QStringLiteral("scope")).toString(), QStringLiteral("project"));
        QVERIFY(chooseMenuAction(manager.findChild<QMenu *>(QStringLiteral("sessionsScopeMenu")),
                                 QStringLiteral("all")));
        QCOMPARE(asked.last().value(QStringLiteral("scope")).toString(), QStringLiteral("all"));
        QVERIFY(chooseMenuAction(sortButton->menu(), QStringLiteral("title")));
        QCOMPARE(asked.last().value(QStringLiteral("sort")).toString(), QStringLiteral("title"));
        QVERIFY(chooseComboItem(model, QStringLiteral("glm-5")));
        QCOMPARE(asked.last().value(QStringLiteral("model")).toString(), QStringLiteral("glm-5"));
        QVERIFY(chooseMenuAction(manager.findChild<QMenu *>(QStringLiteral("sessionsTimeMenu")),
                                 QStringLiteral("week")));
        QVERIFY(asked.last().value(QStringLiteral("since")).toDouble() > 0);
        QVERIFY(chooseMenuAction(manager.findChild<QMenu *>(QStringLiteral("sessionsBranchMenu")),
                                 QStringLiteral("work")));
        QCOMPARE(asked.last().value(QStringLiteral("branch")).toString(), QStringLiteral("work"));
        auto *filters = manager.findChild<QToolButton *>(QStringLiteral("sessionsFilters"));
        QVERIFY(filters && filters->menu());
        QAction *hasEdits = manager.findChild<QAction *>(QStringLiteral("filterHasEdits"));
        QVERIFY(hasEdits);
        QTimer::singleShot(0, filters->menu(), [menu = filters->menu(), hasEdits] {
            const QPoint actionPoint = menu->actionGeometry(hasEdits).center();
            QTest::mouseMove(menu, actionPoint);
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, actionPoint);
        });
        QTest::mouseClick(filters, Qt::LeftButton);
        QVERIFY(hasEdits->isChecked());
        QVERIFY(asked.last().value(QStringLiteral("has_edits")).toBool());
    }

    void collapsedProjectStaysCollapsedAcrossResults() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.resize(900, 650);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));
        const QJsonObject alpha = sessionItem(QStringLiteral("a"), QStringLiteral("Alpha work"),
                                              QStringLiteral("alpha"));
        const QJsonObject beta = sessionItem(QStringLiteral("b"), QStringLiteral("Beta work"),
                                             QStringLiteral("beta"));
        const QJsonArray both{alpha, beta};
        manager.setResults({{QStringLiteral("items"), both}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QTreeWidgetItem *alphaGroup = rowTitled(tree, QStringLiteral("alpha"));
        QTreeWidgetItem *betaGroup = rowTitled(tree, QStringLiteral("beta"));
        QVERIFY(alphaGroup && betaGroup);
        QVERIFY(alphaGroup->isExpanded());
        QVERIFY(betaGroup->isExpanded());

        alphaGroup->setExpanded(false);           // same signal as the project's collapse arrow
        QVERIFY(!alphaGroup->isExpanded());
        manager.setResults({{QStringLiteral("items"), both}});   // delayed worker refresh
        alphaGroup = rowTitled(tree, QStringLiteral("alpha"));
        betaGroup = rowTitled(tree, QStringLiteral("beta"));
        QVERIFY(alphaGroup && betaGroup);
        QVERIFY(!alphaGroup->isExpanded());
        QVERIFY(betaGroup->isExpanded());
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/sessions-project-collapsed.png")));

        manager.setResults({{QStringLiteral("items"), QJsonArray{beta}}});
        manager.setResults({{QStringLiteral("items"), both}});
        alphaGroup = rowTitled(tree, QStringLiteral("alpha"));
        QVERIFY(alphaGroup);
        QVERIFY(!alphaGroup->isExpanded());       // a filtered-out group keeps its choice
        alphaGroup->setExpanded(true);
        manager.setResults({{QStringLiteral("items"), both}});
        alphaGroup = rowTitled(tree, QStringLiteral("alpha"));
        QVERIFY(alphaGroup->isExpanded());
    }

    void sessionsTableShowsRecapAndPreviewsOnDemand() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        QStringList previews;
        manager.onPreview = [&previews](const QString &id, const QString &) { previews << id; };
        int previewHints = 0;
        manager.onPreviewHint = [&previewHints] { ++previewHints; };
        QStringList resumed;
        manager.onResume = [&resumed](const QJsonObject &item, bool, bool) {
            resumed << item.value(QStringLiteral("session_id")).toString();
        };
        manager.resize(900, 650);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));
        QJsonObject first = sessionItem(QStringLiteral("a"), QStringLiteral("First"));
        first.insert(QStringLiteral("summary"), QStringLiteral("A final recap of the first conversation."));
        manager.setResults({{QStringLiteral("items"), QJsonArray{
            first, sessionItem(QStringLiteral("b"), QStringLiteral("Second"))}}});

        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        auto *preview = manager.findChild<QTextBrowser *>(QStringLiteral("conversationPreview"));
        auto *stack = manager.findChild<QStackedWidget *>(QStringLiteral("sessionsViewStack"));
        auto *button = manager.findChild<QPushButton *>(QStringLiteral("sessionsPreviewButton"));
        auto *back = manager.findChild<QPushButton *>(QStringLiteral("sessionsPreviewBack"));
        QVERIFY(tree && preview && stack && button && back);
        QCOMPARE(stack->currentWidget(), tree);
        QCOMPARE(tree->headerItem()->text(3), QStringLiteral("Requests"));
        QCOMPARE(tree->headerItem()->text(6), QStringLiteral("Recap"));
        QCOMPARE(rowTitled(tree, QStringLiteral("First"))->text(6),
                 QStringLiteral("A final recap of the first conversation."));
        QTreeWidgetItem *second = rowTitled(tree, QStringLiteral("Second"));
        QVERIFY(second);
        QCOMPARE(second->text(6), QStringLiteral("No recap saved"));
        tree->scrollToItem(second);
        const QPoint point = tree->visualItemRect(second).center();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(tree->currentItem(), second);
        QVERIFY(previews.isEmpty());
        QCOMPARE(resumed.size(), 0);
        QCOMPARE(stack->currentWidget(), tree);
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/list.png")));

        button->click();
        QCOMPARE(previewHints, 1);
        QCOMPARE(stack->currentWidget(), preview->parentWidget());
        QCOMPARE(previews, QStringList{QStringLiteral("b")});
        const QJsonObject previewTurn{{QStringLiteral("turn"), 1},
                                      {QStringLiteral("kind"), QStringLiteral("prompt")},
                                      {QStringLiteral("text"), QStringLiteral("Read this before resuming")}};
        manager.setPreview({{QStringLiteral("session_id"), QStringLiteral("b")},
                            {QStringLiteral("title"), QStringLiteral("Second")},
                            {QStringLiteral("overview"), QJsonObject{{QStringLiteral("summary"),
                                                                         QStringLiteral("Preview summary")}}},
                            {QStringLiteral("items"), QJsonArray{previewTurn}}});
        QVERIFY(preview->toPlainText().contains(QStringLiteral("Read this before resuming")));
        QCOMPARE(resumed.size(), 0);
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/preview.png")));
        back->click();
        QCOMPARE(stack->currentWidget(), tree);
        QCOMPARE(tree->currentItem(), second);
        QCOMPARE(resumed.size(), 0);

        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(stack->currentWidget(), preview->parentWidget());
        QCOMPARE(previewHints, 2);
        QCOMPARE(resumed.size(), 0);
        QTest::keyClick(preview, Qt::Key_Escape);
        QCOMPARE(stack->currentWidget(), tree);
        QTest::keyClick(tree, Qt::Key_P);
        QCOMPARE(stack->currentWidget(), preview->parentWidget());
        QCOMPARE(previewHints, 2);
        button->click();
        QCOMPARE(stack->currentWidget(), tree);

        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(resumed, QStringList{QStringLiteral("b")});
        for (auto *button : manager.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("Resume")) { button->click(); break; }
        QCOMPARE(resumed, (QStringList{QStringLiteral("b"), QStringLiteral("b")}));
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
        int resumed = 0, newPane = 0, keptOpen = 0;
        QString forked, reopened, pinned;
        manager.onResume = [&resumed, &newPane, &keptOpen](const QJsonObject &, bool other, bool keepOpen) {
            other ? ++newPane : ++resumed;
            if (keepOpen) ++keptOpen;
        };
        manager.onFork = [&forked](const QJsonObject &row) { forked = row.value(QStringLiteral("session_id")).toString(); };
        manager.onReopenClosed = [&reopened](const QString &id) { reopened = id; };
        manager.onPin = [&pinned](const QString &id, bool on) { pinned = id + (on ? QStringLiteral(" on") : QStringLiteral(" off")); };
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(resumed, 0);
        QCOMPARE(newPane, 1);
        QCOMPARE(keptOpen, 0);
        // Shift+Enter (card #R6J0 follow-up): still a new pane, but the caller is told to leave
        // this list open, so several conversations can be reattached in a row.
        QTest::keyClick(tree, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(newPane, 2);
        QCOMPARE(keptOpen, 1);
        QPushButton *resume = nullptr;
        for (auto *button : manager.findChildren<QPushButton *>()) {
            QVERIFY(button->text() != QStringLiteral("Resume here"));
            QVERIFY(button->text() != QStringLiteral("Open in new pane"));
            if (button->text() == QStringLiteral("Resume")) resume = button;
        }
        QVERIFY(resume);
        resume->click();
        QCOMPARE(newPane, 3);
        QCOMPARE(keptOpen, 1);   // the button does not offer the keep-open variant
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(newPane, 4);
        QCOMPARE(resumed, 0);
        QCOMPARE(keptOpen, 1);
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

    void searchSitsOnItsOwnRow() {
        // #1Q5V's owner correction, 2026-09-24: sharing a row with the buttons broke the
        // page. The field — with its "?" helper — owns the top row; Project, Model, Sort ▾,
        // More ▾ and the subagent switch sit on the row below it.
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.show();
        auto *search = manager.findChild<QLineEdit *>(QStringLiteral("sessionsSearch"));
        auto *help = manager.findChild<QToolButton *>(QStringLiteral("sessionsHelp"));
        auto *project = manager.findChild<QComboBox *>(QStringLiteral("sessionsProject"));
        auto *model = manager.findChild<QComboBox *>(QStringLiteral("sessionsModel"));
        auto *sort = manager.findChild<QToolButton *>(QStringLiteral("sessionsSort"));
        auto *filters = manager.findChild<QToolButton *>(QStringLiteral("sessionsFilters"));
        auto *threads = manager.findChild<QCheckBox *>(QStringLiteral("sessionsThreads"));
        QVERIFY(search && help && project && model && sort && filters && threads);
        const int fieldBottom = search->y() + search->height();
        QVERIFY2(help->y() + help->height() <= fieldBottom, "the ? helper stays in the field's row");
        const QWidget *peers[] = {project, model, sort, filters, threads};
        for (const QWidget *peer : peers)
            QVERIFY2(peer->y() >= fieldBottom, "every button sits below the search field's row");
        QVERIFY2(project->y() < model->y() + model->height() && model->y() < project->y() + project->height(),
                 "the buttons still share one row among themselves");
    }

    // ----- guest sessions, protocol 26.7 ------------------------------------------------------

    void guestRowsCarryTheToolsOwnResumeCommand() {
        QVERIFY(isGuestSource(QStringLiteral("claude")));
        QVERIFY(isGuestSource(QStringLiteral("codex")));
        QVERIFY(!isGuestSource(QStringLiteral("agent")));
        QVERIFY(!isGuestSource(QStringLiteral("terminal")));
        // Lower-case since card #MDL1 (rule 1), like every other label Relay writes.
        QCOMPARE(guestLabel(QStringLiteral("claude")), QStringLiteral("claude code"));
        QCOMPARE(guestLabel(QStringLiteral("codex")), QStringLiteral("codex"));
        // Ordinary words are left alone; anything else is single-quoted, and a quote of its own
        // is closed and reopened rather than escaped.
        QCOMPARE(shellWord(QStringLiteral("claude")), QStringLiteral("claude"));
        QCOMPARE(shellWord(QStringLiteral("/home/u/my repo")), QStringLiteral("'/home/u/my repo'"));
        QCOMPARE(shellWord(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
        QCOMPARE(shellWord(QString()), QStringLiteral("''"));

        const QString id = QStringLiteral("3f2504e0-4f89-11d3-9a0c-0305e82c3301");
        const QJsonObject claude = guestItem(QStringLiteral("claude"), id, QStringLiteral("Wire the pane"));
        QCOMPARE(guestCommand(claude), QStringLiteral("claude -r ") + id);
        QCOMPARE(guestCommand(claude, true), QStringLiteral("claude -r ") + id + QStringLiteral(" --fork-session"));
        QCOMPARE(guestCwd(claude), QStringLiteral("/home/u/repos/relay-terminal"));
        const QJsonObject codex = guestItem(QStringLiteral("codex"), id, QStringLiteral("Rollout"), QString());
        QCOMPARE(guestCommand(codex), QStringLiteral("codex resume ") + id);
        QCOMPARE(guestCommand(codex, true), QStringLiteral("codex fork ") + id);
        QCOMPARE(guestCwd(codex), QString());          // no workspace: the pane keeps its own
        // An ordinary session is not a guest and has no command of its own.
        QCOMPARE(guestCommand(sessionItem(QStringLiteral("a"), QStringLiteral("Mine"))), QString());
        QCOMPARE(guestCwd(sessionItem(QStringLiteral("a"), QStringLiteral("Mine"))), QString());
    }

    void theKindFilterListsTheGuestsAndAsksForOne() {
        SessionManager manager;
        QList<QJsonObject> asked;
        manager.onQuery = [&asked](const QJsonObject &request) { asked << request; };
        manager.show();
        auto *kind = manager.findChild<QComboBox *>(QStringLiteral("sessionsKind"));
        QVERIFY(kind);
        QVERIFY(kind->findData(QStringLiteral("claude")) > 0);
        QVERIFY(kind->findData(QStringLiteral("codex")) > 0);
        kind->setCurrentIndex(kind->findData(QStringLiteral("claude")));
        QCOMPARE(asked.last().value(QStringLiteral("sources")).toArray(),
                 (QJsonArray{QStringLiteral("claude")}));
        // A guest kind is a filter like any other, and "Clear filters" takes it back to everything.
        manager.setResults({{QStringLiteral("items"), QJsonArray{}}});
        auto *clear = manager.findChild<QPushButton *>(QStringLiteral("clearFilters"));
        QVERIFY(clear->isVisibleTo(&manager));
        clear->click();
        QCOMPARE(asked.last().value(QStringLiteral("sources")).toArray(),
                 (QJsonArray{QStringLiteral("agent"), QStringLiteral("terminal"),
                             QStringLiteral("claude"), QStringLiteral("codex")}));
    }

    // The "Project" chooser (card #916B): a known project goes on the request as `project`, "No
    // project" as every known folder in `outside_projects`, and "Clear filters" drops either.
    void theProjectFilterNamesAKnownProjectOrNone() {
        SessionManager manager;
        QList<QJsonObject> asked;
        manager.onQuery = [&asked](const QJsonObject &request) { asked << request; };
        manager.show();
        auto *project = manager.findChild<QComboBox *>(QStringLiteral("sessionsProject"));
        QVERIFY(project);
        QCOMPARE(project->count(), 2);                                   // Any project · No project
        manager.setKnownProjects({{QStringLiteral("relay"), QStringLiteral("/srv/relay")},
                                  {QStringLiteral("notes"), QStringLiteral("/home/u/notes")},
                                  {QStringLiteral("notes"), QStringLiteral("/srv/notes")}});
        QCOMPARE(project->count(), 5);
        QCOMPARE(project->itemText(1), QStringLiteral("relay"));
        QCOMPARE(project->itemText(2), QStringLiteral("notes  (/home/u/notes)"));   // two "notes": the folder tells
        QCOMPARE(project->itemData(4).toString(), QStringLiteral("none"));
        project->setCurrentIndex(project->findData(QStringLiteral("/srv/relay")));
        QCOMPARE(asked.last().value(QStringLiteral("project")).toString(), QStringLiteral("/srv/relay"));
        QVERIFY(!asked.last().contains(QStringLiteral("outside_projects")));
        project->setCurrentIndex(project->findData(QStringLiteral("none")));
        QCOMPARE(asked.last().value(QStringLiteral("outside_projects")).toArray(),
                 (QJsonArray{QStringLiteral("/srv/relay"), QStringLiteral("/home/u/notes"), QStringLiteral("/srv/notes")}));
        QVERIFY(!asked.last().contains(QStringLiteral("project")));
        // The worker answers across all projects and says so; the scope menu follows.
        manager.setResults({{QStringLiteral("items"), QJsonArray{}}, {QStringLiteral("scope"), QStringLiteral("all")}});
        auto *clear = manager.findChild<QPushButton *>(QStringLiteral("clearFilters"));
        QVERIFY(clear->isVisibleTo(&manager));
        clear->click();
        QVERIFY(!asked.last().contains(QStringLiteral("project")));
        QVERIFY(!asked.last().contains(QStringLiteral("outside_projects")));
        // A fed list keeps the choice; a folder that is no longer known stays selectable.
        project->setCurrentIndex(project->findData(QStringLiteral("/srv/relay")));
        manager.setKnownProjects({{QStringLiteral("notes"), QStringLiteral("/home/u/notes")}});
        QCOMPARE(project->currentData().toString(), QStringLiteral("/srv/relay"));
        QCOMPARE(asked.last().value(QStringLiteral("project")).toString(), QStringLiteral("/srv/relay"));
    }

    void scopeChoiceClearsConflictingProjectFilter() {
        SessionManager manager;
        QList<QJsonObject> asked;
        manager.onQuery = [&asked](const QJsonObject &request) { asked << request; };
        manager.setKnownProjects({{QStringLiteral("relay"), QStringLiteral("/srv/relay")},
                                  {QStringLiteral("notes"), QStringLiteral("/srv/notes")}});
        manager.resize(900, 650);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));
        auto *scope = manager.findChild<QComboBox *>(QStringLiteral("sessionsScope"));
        auto *project = manager.findChild<QComboBox *>(QStringLiteral("sessionsProject"));
        QVERIFY(scope && project);
        QCOMPARE(scope->currentData().toString(), QStringLiteral("project"));
        project->setCurrentIndex(project->findData(QStringLiteral("/srv/notes")));
        QCOMPARE(asked.last().value(QStringLiteral("project")).toString(), QStringLiteral("/srv/notes"));
        // A named project makes the worker search globally, and its reply reflects that.
        manager.setResults({{QStringLiteral("items"), QJsonArray{}},
                            {QStringLiteral("scope"), QStringLiteral("all")}});
        QCOMPARE(scope->currentData().toString(), QStringLiteral("all"));

        scope->setCurrentIndex(scope->findData(QStringLiteral("project")));
        QCOMPARE(project->currentData().toString(), QString());
        QCOMPARE(asked.last().value(QStringLiteral("scope")).toString(), QStringLiteral("project"));
        QVERIFY(!asked.last().contains(QStringLiteral("project")));
        QVERIFY(!asked.last().contains(QStringLiteral("outside_projects")));
        manager.setResults({{QStringLiteral("items"), QJsonArray{
                                sessionItem(QStringLiteral("a"), QStringLiteral("Relay work"), QStringLiteral("relay"))}},
                            {QStringLiteral("scope"), QStringLiteral("project")}});
        QCOMPARE(scope->currentData().toString(), QStringLiteral("project"));
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (!shotDir.isEmpty())
            QVERIFY(manager.grab().save(shotDir + QStringLiteral("/sessions-this-project.png")));

        project->setCurrentIndex(project->findData(QStringLiteral("/srv/notes")));
        scope->setCurrentIndex(scope->findData(QStringLiteral("all")));
        QCOMPARE(project->currentData().toString(), QString());
        QCOMPARE(asked.last().value(QStringLiteral("scope")).toString(), QStringLiteral("all"));
        QVERIFY(!asked.last().contains(QStringLiteral("project")));
        manager.setResults({{QStringLiteral("items"), QJsonArray{
                                sessionItem(QStringLiteral("a"), QStringLiteral("Relay work"), QStringLiteral("relay")),
                                sessionItem(QStringLiteral("b"), QStringLiteral("Notes work"), QStringLiteral("notes"))}},
                            {QStringLiteral("scope"), QStringLiteral("all")}});
        QCOMPARE(scope->currentData().toString(), QStringLiteral("all"));
        QCOMPARE(manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"))->topLevelItemCount(), 2);
        project->setCurrentIndex(project->findData(QStringLiteral("/srv/notes")));
        QCOMPARE(asked.last().value(QStringLiteral("project")).toString(), QStringLiteral("/srv/notes"));
        emit scope->activated(scope->currentIndex());   // reselect All projects from its menu
        QCOMPARE(project->currentData().toString(), QString());
        QCOMPARE(asked.last().value(QStringLiteral("scope")).toString(), QStringLiteral("all"));
        QVERIFY(!asked.last().contains(QStringLiteral("project")));
    }

    void aGuestRowResumesForksAndSaysWhoseItIs() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.show();
        const QString id = QStringLiteral("3f2504e0-4f89-11d3-9a0c-0305e82c3301");
        manager.setResults({{QStringLiteral("items"),
                             QJsonArray{guestItem(QStringLiteral("claude"), id, QStringLiteral("Wire the pane"))}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QTreeWidgetItem *row = tree->topLevelItem(0)->child(0);
        QCOMPARE(row->text(0), QStringLiteral("Wire the pane"));
        QCOMPARE(row->text(4), QStringLiteral("claude code"));      // where a session shows its model
        // Enter and Shift+Enter request a new pane; Ctrl+Enter forks — all with the row,
        // which is what carries the argv.
        QString resumed, newPaned, forked;
        manager.onResume = [&resumed, &newPaned](const QJsonObject &item, bool other, bool) {
            (other ? newPaned : resumed) = guestCommand(item, false);
        };
        manager.onFork = [&forked](const QJsonObject &item) { forked = guestCommand(item, true); };
        QTest::keyClick(tree, Qt::Key_Return);
        QVERIFY(resumed.isEmpty());
        QCOMPARE(newPaned, QStringLiteral("claude -r ") + id);
        QTest::keyClick(tree, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(newPaned, QStringLiteral("claude -r ") + id);
        QTest::keyClick(tree, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(forked, QStringLiteral("claude -r ") + id + QStringLiteral(" --fork-session"));
        // The ⓘ view and the summary read a Relay session file; a guest has none, so neither is
        // offered. Rename, pin and delete are index-only and stay.
        auto button = [&manager](const QString &text) -> QPushButton * {
            for (QPushButton *candidate : manager.findChildren<QPushButton *>())
                if (candidate->text() == text) return candidate;
            return nullptr;
        };
        QVERIFY(button(QStringLiteral("Resume"))->isEnabled());
        QVERIFY(!button(QStringLiteral("Info"))->isEnabled());
        QVERIFY(button(QStringLiteral("Rename…"))->isEnabled());
        QVERIFY(button(QStringLiteral("Pin"))->isEnabled());
        QVERIFY(button(QStringLiteral("Delete…"))->isEnabled());
        QVERIFY(button(QStringLiteral("Resume"))->toolTip().contains(QStringLiteral("claude -r ") + id));
        QVERIFY(button(QStringLiteral("Resume"))->toolTip().contains(QStringLiteral("/home/u/repos/relay-terminal")));
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
        QCOMPARE(row->text(6), QStringLiteral("It did the thing."));
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
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(6), QStringLiteral("Live from the batch."));
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
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(6), QStringLiteral("From the pane."));
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

    // ----- the helper agent (card #FEJQ; a console since card #AGNT step 7) -------------------
    //
    // "When you are in options, actions, or sessions, you have a helper agent, same as the
    // switchboard agent" (owner). Since card #AGNT step 7 that helper is an embedded **console** —
    // a no-shell `Pane` the window builds — over the context this pane owns.
    void theHelperConsoleSitsAtTheBottomCollapsedAndAsksAsTheSessionsPane() {
        // A pane whose window never wired a factory has no helper at all.
        SessionManager unwired;
        unwired.onQuery = [](const QJsonObject &) {};
        unwired.show();
        QVERIFY(QTest::qWaitForWindowExposed(&unwired));
        QVERIFY(!unwired.findChild<QWidget *>(QStringLiteral("boardChatPanel"))->isVisible());
        QVERIFY(!unwired.agentConsole());

        FakeConsole fake;
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.onCreateConsole = fake.factory();
        manager.setHelperTabId(QStringLiteral("tab-3"));
        manager.setHelperWorkspace(QStringLiteral("/home/e/relay"));
        manager.setHelperShortcut(QStringLiteral("sessions.ask"), QStringLiteral("Ctrl+/"));
        manager.resize(900, 700);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));

        // One row, at the bottom right, with the question-mark icon and the live key in its text.
        auto *panel = manager.findChild<QWidget *>(QStringLiteral("boardChatPanel"));
        auto *askRow = manager.findChild<QWidget *>(QStringLiteral("boardChatAskRow"));
        auto *ask = manager.findChild<QToolButton *>(QStringLiteral("boardChatAsk"));
        auto *body = manager.findChild<QWidget *>(QStringLiteral("boardChatBody"));
        QVERIFY(panel && askRow && ask && body);
        QVERIFY(panel->isVisible());
        QVERIFY(askRow->isVisible());
        QVERIFY(!body->isVisible());
        QCOMPARE(ask->text(), QStringLiteral("Helper Agent (Ctrl+/)"));
        QVERIFY(!ask->icon().isNull());
        QVERIFY(ask->geometry().center().x() > askRow->width() / 2);
        QCOMPARE(manager.findChild<QLabel *>(QStringLiteral("boardChatHead"))->text(),
                 QStringLiteral("Sessions helper"));
        QCOMPARE(fake.builds, 0);

        // Expanding builds it once, unfolds it and hands it the cursor.
        ask->click();
        QCOMPARE(fake.builds, 1);
        QVERIFY(body->isVisible());
        QCOMPARE(fake.collapsed.size(), 1);
        QCOMPARE(fake.collapsed.last(), false);
        QCOMPARE(fake.focused, 1);
        QVERIFY(body->isAncestorOf(fake.widget));
        const int line = QFontMetrics(manager.font()).lineSpacing();
        QVERIFY(body->maximumHeight() >= 3 * line);
        QVERIFY(body->maximumHeight() <= std::max(10 * line, manager.height() * 2 / 5));

        // What the worker is told.
        relay::agent::Context *context = manager.agentContext();
        QVERIFY(context);
        relay::agent::ContextSpec spec = context->spec();
        QCOMPARE(spec.name, QStringLiteral("sessions"));
        QCOMPARE(spec.surface, QStringLiteral("sessions"));
        QCOMPARE(spec.agentRole, QStringLiteral("switchboard"));
        QCOMPARE(spec.workspace, QStringLiteral("/home/e/relay"));
        QCOMPARE(spec.scope, QStringLiteral("console"));
        QCOMPARE(spec.persistScope, QStringLiteral("helper"));
        QCOMPARE(spec.persistKey, QStringLiteral("tab-3"));
        QCOMPARE(spec.briefTitle, QStringLiteral("Sessions helper"));
        QCOMPARE(spec.routing, QStringLiteral("agent"));
        QVERIFY(!spec.shell);
        QCOMPARE(context->placeholder(), QStringLiteral("Ask the Sessions helper…"));
        QVERIFY(context->actions().isEmpty());

        // The `screen` hint names the query and the filters in force.
        manager.setQuery(QStringLiteral("Pane.h"));
        QVERIFY(context->spec().screen.contains(QStringLiteral("Search: Pane.h")));
        QVERIFY(context->spec().screen.contains(QStringLiteral("Filters:")));
        QVERIFY(context->spec().screen.size() <= relay::agent::kScreenLimit);

        // A `session:` link in an answer selects that row rather than asking the window for it.
        manager.setResults({{QStringLiteral("items"),
                             QJsonArray{sessionItem(QStringLiteral("a"), QStringLiteral("Index work")),
                                        sessionItem(QStringLiteral("b"), QStringLiteral("Voice work"))}}});
        relay::links::Target session;
        session.valid = true;
        session.kind = relay::links::Kind::Session;
        session.target = relay::links::sessionTarget(QStringLiteral("b"));
        QVERIFY(context->resolveLink(session));
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QVERIFY(tree && tree->currentItem());
        QCOMPARE(tree->currentItem()->text(0), QStringLiteral("Voice work"));
        QVERIFY(context->spec().screen.contains(QStringLiteral("Selected: Voice work (b)")));
        // An `option:` row belongs to the window, and false is how the context says so.
        relay::links::Target option;
        option.valid = true;
        option.kind = relay::links::Kind::Option;
        option.target = relay::links::optionTarget(QStringLiteral("agent"), QStringLiteral("option:writes"));
        QVERIFY(!context->resolveLink(option));

        // The fold goes back to one row — folded, not destroyed — and opening it again is where
        // you left it.
        auto *fold = manager.findChild<QToolButton *>(QStringLiteral("boardChatFold"));
        QVERIFY(fold);
        fold->click();
        QVERIFY(!body->isVisible());
        QVERIFY(askRow->isVisible());
        QCOMPARE(fake.collapsed.last(), true);
        manager.focusHelper();
        QCOMPARE(fake.builds, 1);
        QCOMPARE(fake.focused, 2);

        // A draft is a draft, never a send.
        manager.helperDraft(QStringLiteral("which sessions touched Pane.h?"));
        QCOMPARE(fake.draft, QStringLiteral("which sessions touched Pane.h?"));
    }

    // The delegate's laid-out documents are kept between rebuilds, keyed by html, width and font
    // (#MDSG): the same row asked for twice is laid out once.
    void richTextCacheKeys() {
        RichTextCache cache(3);
        const QString html = QStringLiteral("<b>turn 3</b> the index is rebuilt on every start");
        const QFont font(QStringLiteral("Sans"), 10);
        QTextDocument *first = cache.document(html, 300, font);
        QVERIFY(first);
        QCOMPARE(cache.document(html, 300, font), first);      // the same document, not a copy
        QCOMPARE(cache.misses(), 1);
        QCOMPARE(cache.hits(), 1);

        // The width it was laid out at is part of what it is: a narrower column wraps differently.
        QTextDocument *narrow = cache.document(html, 180, font);
        QVERIFY(narrow != first);
        QCOMPARE(cache.misses(), 2);

        // So is the font — a theme that changes it must not hand back the old layout.
        QFont bigger = font;
        bigger.setPointSize(14);
        QVERIFY(cache.document(html, 300, bigger) != first);
        QCOMPARE(cache.misses(), 3);
        QCOMPARE(cache.size(), 3);

        // Bounded: a fourth entry pushes the least recently used one out, and no more are kept.
        cache.document(html, 300, font);                        // first is now the most recent
        cache.document(QStringLiteral("<b>turn 4</b> another line"), 300, font);
        QCOMPARE(cache.size(), 3);
        QCOMPARE(cache.document(html, 300, font), first);        // kept: it was asked for last
        QCOMPARE(cache.document(html, 180, font) == narrow, false);   // dropped: the oldest

        cache.clear();
        QCOMPARE(cache.size(), 0);
    }

    // The rows that span the whole width are still spanned after a rebuild, although the spans are
    // now set in one go at the end of it (#MDSG).
    void groupRowsSpanTheWidth() {
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));
        manager.setResults({{QStringLiteral("items"),
                             QJsonArray{sessionItem(QStringLiteral("a"), QStringLiteral("First")),
                                        sessionItem(QStringLiteral("b"), QStringLiteral("Second"),
                                                    QStringLiteral("other"))}}});
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QVERIFY(tree && tree->topLevelItemCount() >= 2);
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *group = tree->topLevelItem(i);
            QCOMPARE(group->data(0, Qt::UserRole + 7).toString(), QStringLiteral("group"));
            QVERIFY(group->isFirstColumnSpanned());
            // Its sessions each carry the quick-look placeholder, spanned too.
            QVERIFY(group->childCount() > 0);
            QTreeWidgetItem *row = group->child(0);
            QCOMPARE(row->childCount(), 1);
            QVERIFY(row->child(0)->isFirstColumnSpanned());
        }
        // The narrow columns are back to sizing themselves once the list is filled.
        for (int column = 1; column < 5; ++column)
            QCOMPARE(tree->header()->sectionResizeMode(column), QHeaderView::ResizeToContents);
        QCOMPARE(tree->header()->sectionResizeMode(5), QHeaderView::ResizeToContents);
        QCOMPARE(tree->header()->sectionResizeMode(6), QHeaderView::Stretch);
    }

    // The list as it is drawn, written out as a PNG so the same page can be compared pixel for
    // pixel against another build (#MDSG: the row cache and the deferred spans must change what a
    // keystroke costs and nothing else). Off unless a directory is named for it.
    void listScreenshot() {
        const QString directory = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (directory.isEmpty()) QSKIP("set RELAY_SHOT_DIR=<dir> to write the list as a PNG");
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.onPreview = [](const QString &, const QString &) {};
        manager.resize(900, 760);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QVERIFY(tree);
        manager.setQuery(QStringLiteral("index"));
        QJsonArray items = benchItems(24, QStringLiteral("index"));
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items.at(i).toObject();
            item.insert(QStringLiteral("tokens"), (24 - i) * 125000);
            items.replace(i, item);
        }
        manager.setResults({{QStringLiteral("items"), items}});
        auto *sort = manager.findChild<QComboBox *>(QStringLiteral("sessionsSort"));
        sort->setCurrentIndex(sort->findData(QStringLiteral("tokens_desc")));
        // Two rows unfolded, so the rich-text rows the delegate lays out are on screen too.
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *group = tree->topLevelItem(i);
            for (int j = 0; j < qMin(2, group->childCount()); ++j) {
                QTreeWidgetItem *row = group->child(j);
                manager.setPreview({{QStringLiteral("session_id"), row->data(0, Qt::UserRole + 1).toString()},
                                    {QStringLiteral("overview"), QJsonObject{{QStringLiteral("summary"),
                                        QStringLiteral("Rebuilt the index and re-measured the pane.")}}}});
                row->setExpanded(true);
            }
        }
        // Rebuilt once more: what a keystroke leaves on screen, not what the first fill did.
        manager.setResults({{QStringLiteral("items"), items}});
        QVERIFY(tree->viewport()->grab().save(directory + QStringLiteral("/sessions-list.png")));
        QVERIFY(manager.grab().save(directory + QStringLiteral("/sessions-tokens.png")));
        qInfo("wrote %s/sessions-list.png", qPrintable(directory));
    }

    // What a keystroke costs: a hundred-row page arrives and the list is rebuilt and repainted,
    // ten times over, as search-as-you-type does it (#MDSG). It prints rather than asserts — the
    // number is the machine's — so it only runs when asked for.
    void searchKeystrokeCost() {
        if (qEnvironmentVariableIsEmpty("RELAY_PERF_BENCH"))
            QSKIP("set RELAY_PERF_BENCH=1 to time a keystroke");
        SessionManager manager;
        manager.onQuery = [](const QJsonObject &) {};
        manager.onPreview = [](const QString &, const QString &) {};
        manager.resize(900, 760);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));
        auto *tree = manager.findChild<QTreeWidget *>(QStringLiteral("sessionsTree"));
        QVERIFY(tree);
        manager.setQuery(QStringLiteral("index"));

        for (const bool unfolded : {false, true}) {
            manager.setResults({{QStringLiteral("items"), benchItems(100, QStringLiteral("index"))}});
            if (unfolded)
                for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                    QTreeWidgetItem *group = tree->topLevelItem(i);
                    group->setExpanded(true);
                    for (int j = 0; j < group->childCount(); ++j) {
                        QTreeWidgetItem *row = group->child(j);
                        manager.setPreview({{QStringLiteral("session_id"), row->data(0, Qt::UserRole + 1).toString()},
                                            {QStringLiteral("overview"), QJsonObject{{QStringLiteral("summary"),
                                                QStringLiteral("Rebuilt the index and re-measured the pane.")}}}});
                        row->setExpanded(true);
                    }
                }
            tree->viewport()->grab();
            const double cpuBefore = cpuMs();
            QElapsedTimer wall;
            wall.start();
            const int keys = 10;
            for (int k = 0; k < keys; ++k) {
                manager.setResults({{QStringLiteral("items"), benchItems(100, QStringLiteral("index"))}});
                tree->viewport()->grab();
            }
            qInfo("%s rows: %.1f ms GUI CPU per key, %.1f ms wall per key",
                  unfolded ? "unfolded" : "collapsed", (cpuMs() - cpuBefore) / keys,
                  double(wall.elapsed()) / keys);
        }
    }
    // Card #7QSK: the Sessions pane's "This project" scope must follow the pane's live terminal
    // directory (OSC 7), not the workspace frozen when the pane was made — a pane cd'd into
    // ~/repos/x in a window launched in ~/repos/y listed y's sessions. The request is built
    // inside Pane::bindSessionManager, which needs a live worker, so the rule is pinned on the
    // source the way the Board's openOutputTarget test pins its routing.
    void theScopeFilterFollowsTheLiveTerminalDirectory() {
        const auto bodyOf = [](const QString &text, const QString &signature) {
            const int start = text.indexOf(signature);
            if (start < 0) return QString();
            const int end = text.indexOf(QStringLiteral("\n    }"), start);
            return end > start ? text.mid(start, end - start) : QString();
        };
        QFile pane(QStringLiteral(RELAY_SOURCE_DIR "/src/Pane.h"));
        QVERIFY2(pane.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(pane.fileName()));
        const QString bind = bodyOf(QString::fromUtf8(pane.readAll()),
            QStringLiteral("void bindSessionManager(relay::conversations::SessionManager *view) {"));
        QVERIFY2(!bind.isEmpty(), "Pane::bindSessionManager() is gone");
        QVERIFY2(bind.contains(QStringLiteral(
                      "message.insert(QStringLiteral(\"workspace\"),\n"
                      "                           self->m_cwd.isEmpty() ? self->m_workspace : self->m_cwd);")),
                 "the conversations request carries the frozen launch workspace again");
        QFile window(QStringLiteral(RELAY_SOURCE_DIR "/src/RelayWindow.h"));
        QVERIFY2(window.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(window.fileName()));
        const QString link = bodyOf(QString::fromUtf8(window.readAll()),
            QStringLiteral("void linkSessionsPane(ToolPane *tool, Pane *owner) {"));
        QVERIFY2(!link.isEmpty(), "RelayWindow::linkSessionsPane() is gone");
        QVERIFY2(link.contains(QStringLiteral(
                      "view->setProject(QFileInfo(owner->cwd().isEmpty() ? owner->workspace() : owner->cwd()).fileName());")),
                 "the scope label names the frozen launch project again");
    }
};

QTEST_MAIN(ConversationsTest)
#include "conversations_test.moc"
