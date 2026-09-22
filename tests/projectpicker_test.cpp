// SPDX-License-Identifier: AGPL-3.0-or-later
// The project picker pane (src/ProjectPicker.h, card #916B): the rows it shows, the order a
// filter puts them in, what Enter does before anything is chosen, and the two answers it gives.
// Offscreen: the pane knows nothing about tabs, the registry file or the worker.
#include "ProjectPicker.h"

#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>

using namespace relay::projects;

namespace {

Record record(const QString &path, const QString &reason, qint64 attached)
{
    Record out;
    out.path = path;
    out.key = keyFor(path);
    out.name = nameFor(path);
    out.reason = reason;
    out.knownSince = attached;
    out.lastAttached = attached;
    return out;
}

// Most recently attached first, as Registry::knownProjects() hands them over.
QList<Record> three()
{
    return {record(QStringLiteral("/srv/widgetworks"), QStringLiteral("switchboard"), 3000),
            record(QStringLiteral("/srv/relay-terminal"), QStringLiteral("picker"), 2000),
            record(QStringLiteral("/home/u/notes"), QStringLiteral("card-command"), 1000)};
}

}  // namespace

class ProjectPickerTests : public QObject {
    Q_OBJECT
private slots:
    void anEmptyFilterKeepsTheRegistrysOrder();
    void aFilterRanksByNameThenPathAndDropsTheRest();
    void reasonsAndAgesReadAsWords();
    void theInitRowIsFirstAndIsTheDefaultAnswer();
    void theDefaultProjectIsPreselectedWhenSet();
    void typingSelectsTheBestMatchAndEnterPicksIt();
    void nothingMatchingFallsBackToInitializingHere();
    void escCloses();
    void noDirectoryMeansNoInitRow();
};

void ProjectPickerTests::anEmptyFilterKeepsTheRegistrysOrder()
{
    const QList<Record> ranked = rankProjects(three(), QString());
    QCOMPARE(ranked.size(), 3);
    QCOMPARE(ranked.at(0).path, QStringLiteral("/srv/widgetworks"));
    QCOMPARE(ranked.at(2).path, QStringLiteral("/home/u/notes"));
    QCOMPARE(rankProjects(three(), QStringLiteral("   ")).size(), 3);
}

void ProjectPickerTests::aFilterRanksByNameThenPathAndDropsTheRest()
{
    // "rel" is in relay-terminal's name and nowhere else.
    QList<Record> ranked = rankProjects(three(), QStringLiteral("rel"));
    QCOMPARE(ranked.size(), 1);
    QCOMPARE(ranked.first().path, QStringLiteral("/srv/relay-terminal"));
    // "srv" is only in two paths; a path match keeps the row, a name match would outrank it.
    ranked = rankProjects(three(), QStringLiteral("srv"));
    QCOMPARE(ranked.size(), 2);
    QCOMPARE(ranked.at(0).path, QStringLiteral("/srv/widgetworks"));
    QCOMPARE(ranked.at(1).path, QStringLiteral("/srv/relay-terminal"));
    // A subsequence matches too ("wgw" → widgetworks), case-insensitively.
    ranked = rankProjects(three(), QStringLiteral("WGW"));
    QCOMPARE(ranked.size(), 1);
    QCOMPARE(ranked.first().path, QStringLiteral("/srv/widgetworks"));
    QVERIFY(rankProjects(three(), QStringLiteral("zzz")).isEmpty());
}

void ProjectPickerTests::reasonsAndAgesReadAsWords()
{
    QCOMPARE(reasonText(QStringLiteral("switchboard")), QStringLiteral("opened its Board"));
    QCOMPARE(reasonText(QStringLiteral("picker")), QStringLiteral("chosen in the project picker"));
    QCOMPARE(reasonText(QStringLiteral("init-command")), QStringLiteral("initialized with /init"));
    // Every reason of the closed set has words; nothing falls through to the raw id.
    for (const QString &reason : reasons()) QVERIFY2(reasonText(reason) != reason, qPrintable(reason));
    QCOMPARE(reasonText(QStringLiteral("something-else")), QStringLiteral("something-else"));

    const qint64 now = 1'800'000'000;
    QCOMPARE(agoText(0, now), QString());
    QCOMPARE(agoText(now - 5, now), QStringLiteral("just now"));
    QCOMPARE(agoText(now - 15 * 60, now), QStringLiteral("15 min ago"));
    QCOMPARE(agoText(now - 3600, now), QStringLiteral("an hour ago"));
    QCOMPARE(agoText(now - 5 * 3600, now), QStringLiteral("5 hours ago"));
    QCOMPARE(agoText(now - 86400, now), QStringLiteral("yesterday"));
    QCOMPARE(agoText(now - 12 * 86400, now), QStringLiteral("12 days ago"));
    QVERIFY(agoText(now - 400 * 86400, now).contains(QRegularExpression(QStringLiteral("^\\d+ \\w+ \\d{4}$"))));
}

void ProjectPickerTests::theInitRowIsFirstAndIsTheDefaultAnswer()
{
    ProjectPicker picker;
    picker.setHere(QStringLiteral("/home/u/Downloads"));
    picker.setProjects(three());
    picker.show();
    const QStringList paths = picker.visiblePaths();
    QCOMPARE(paths.size(), 4);
    QCOMPARE(paths.at(0), QString());                             // the init row
    QCOMPARE(paths.at(1), QStringLiteral("/srv/widgetworks"));    // then the registry's order
    QCOMPARE(paths.at(3), QStringLiteral("/home/u/notes"));
    QVERIFY(picker.initRowSelected());

    int inits = 0;
    QString picked;
    picker.onInitHere = [&inits] { ++inits; };
    picker.onPick = [&picked](const QString &path) { picked = path; };
    picker.accept();
    QCOMPARE(inits, 1);
    QVERIFY(picked.isEmpty());
    // The button row says the same thing: Initialize here is the default button, Attach is off.
    QVERIFY(!picker.findChild<QPushButton *>(QStringLiteral("projectPickerAttach"))->isEnabled());
    QVERIFY(picker.findChild<QPushButton *>(QStringLiteral("projectPickerInit"))->isDefault());
}

void ProjectPickerTests::theDefaultProjectIsPreselectedWhenSet()
{
    ProjectPicker picker;
    picker.setHere(QStringLiteral("/home/u/Downloads"));
    picker.setProjects(three());
    picker.setDefaultProject(QStringLiteral("/srv/relay-terminal"));
    picker.show();
    QCOMPARE(picker.selectedPath(), QStringLiteral("/srv/relay-terminal"));
    QVERIFY(!picker.initRowSelected());
    // The row says so.
    auto *list = picker.findChild<QListWidget *>(QStringLiteral("projectPickerList"));
    QVERIFY(list->currentItem()->text().contains(QStringLiteral("default for loose cards")));
    QString picked;
    picker.onPick = [&picked](const QString &path) { picked = path; };
    QTest::keyClick(picker.findChild<QLineEdit *>(QStringLiteral("projectPickerSearch")), Qt::Key_Return);
    QCOMPARE(picked, QStringLiteral("/srv/relay-terminal"));
}

void ProjectPickerTests::typingSelectsTheBestMatchAndEnterPicksIt()
{
    ProjectPicker picker;
    picker.setHere(QStringLiteral("/home/u/Downloads"));
    picker.setProjects(three());
    picker.show();
    auto *search = picker.findChild<QLineEdit *>(QStringLiteral("projectPickerSearch"));
    QVERIFY(search);
    QTest::keyClicks(search, QStringLiteral("not"));
    // The init row stays on top; the selection is on the match.
    QCOMPARE(picker.visiblePaths(), (QStringList{QString(), QStringLiteral("/home/u/notes")}));
    QCOMPARE(picker.selectedPath(), QStringLiteral("/home/u/notes"));
    QString picked;
    picker.onPick = [&picked](const QString &path) { picked = path; };
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(picked, QStringLiteral("/home/u/notes"));

    // ↑ from the first match reaches the init row; ↓ walks back down.
    picked.clear();
    QTest::keyClick(search, Qt::Key_Up);
    QVERIFY(picker.initRowSelected());
    QTest::keyClick(search, Qt::Key_Down);
    QCOMPARE(picker.selectedPath(), QStringLiteral("/home/u/notes"));
    // Clearing the filter goes back to the registry's order with the init row selected again.
    search->clear();
    QCOMPARE(picker.visiblePaths().size(), 4);
    QVERIFY(picker.initRowSelected());
}

void ProjectPickerTests::nothingMatchingFallsBackToInitializingHere()
{
    ProjectPicker picker;
    picker.setHere(QStringLiteral("/home/u/Downloads"));
    picker.setProjects(three());
    picker.show();
    picker.setFilter(QStringLiteral("qqq"));
    QCOMPARE(picker.visiblePaths(), QStringList{QString()});
    QVERIFY(picker.initRowSelected());
    int inits = 0;
    picker.onInitHere = [&inits] { ++inits; };
    picker.accept();
    QCOMPARE(inits, 1);
}

void ProjectPickerTests::escCloses()
{
    ProjectPicker picker;
    picker.setHere(QStringLiteral("/home/u/Downloads"));
    picker.show();
    int closed = 0;
    picker.onClose = [&closed] { ++closed; };
    QTest::keyClick(picker.findChild<QLineEdit *>(QStringLiteral("projectPickerSearch")), Qt::Key_Escape);
    QCOMPARE(closed, 1);
    QTest::keyClick(&picker, Qt::Key_Escape);
    QCOMPARE(closed, 2);
    picker.findChild<QPushButton *>(QStringLiteral("projectPickerClose"))->click();
    QCOMPARE(closed, 3);
}

void ProjectPickerTests::noDirectoryMeansNoInitRow()
{
    ProjectPicker picker;
    picker.setProjects(three());
    picker.show();
    QCOMPARE(picker.visiblePaths().size(), 3);
    QCOMPARE(picker.selectedPath(), QStringLiteral("/srv/widgetworks"));
    QVERIFY(!picker.findChild<QPushButton *>(QStringLiteral("projectPickerInit"))->isVisibleTo(&picker));
    QCOMPARE(picker.paneTitle(), QStringLiteral("Projects"));
}

QTEST_MAIN(ProjectPickerTests)
#include "projectpicker_test.moc"
