// SPDX-License-Identifier: AGPL-3.0-or-later
// The section editor behind the gear at the end of the Switchboard's section checkboxes: the four
// things it offers (add, remove, merge, rename), what it refuses, and the one `board_sections`
// message it produces. `SectionPlan` holds no widgets, so none of this needs the pane.
//
// The property every test here is really about: **a section is a view of the statuses**. No edit
// moves a card or changes one, and no edit can lose a card — a status the edit leaves uncollected
// comes back as a section of its own.
#include "BoardSections.h"

#include "BoardPane.h"

#include <QCheckBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using relay::board::Model;
using relay::board::SectionPlan;

namespace {

QJsonObject config()
{
    const auto json = QByteArrayLiteral(R"({
      "columns": ["inbox", "discussing", "ready", "in-progress", "waiting", "needs-qa", "done"],
      "column_statuses": {"inbox": ["inbox"], "discussing": ["discussing"], "ready": ["ready"],
        "in-progress": ["in-progress"],
        "waiting": ["needs-review", "needs-labels", "needs-ab"],
        "needs-qa": ["needs-qa-llm", "needs-qa-human"], "done": ["done", "dropped"]},
      "all_statuses": ["inbox", "discussing", "ready", "in-progress", "needs-review",
        "needs-labels", "needs-ab", "needs-qa-llm", "needs-qa-human", "deferred", "done",
        "dropped"],
      "tabs": [{"id": "features", "folder": "features"}],
      "autonomy": "auto"
    })");
    return QJsonDocument::fromJson(json).object();
}

Model board(const QJsonObject &over = {})
{
    QJsonObject merged = config();
    for (auto it = over.begin(); it != over.end(); ++it)
        merged.insert(it.key(), it.value());
    Model model;
    model.setConfig(merged);
    return model;
}

QStringList ids(const SectionPlan &plan)
{
    QStringList out;
    for (const SectionPlan::Row &row : plan.rows())
        out << row.id;
    return out;
}

QStringList titles(const SectionPlan &plan)
{
    QStringList out;
    for (const SectionPlan::Row &row : plan.rows())
        out << row.title();
    return out;
}

QJsonObject opened()
{
    return QJsonObject{{"event", "board"}, {"config", config()},
                       {"cards", QJsonArray{}}, {"problems", QJsonArray{}}};
}

QStringList arrayOf(const QJsonObject &message, const QString &key)
{
    QStringList out;
    for (const QJsonValue &value : message.value(key).toArray())
        out << value.toString();
    return out;
}

}  // namespace

class BoardSectionsTests : public QObject {
    Q_OBJECT
private slots:
    void thePlanIsTheSectionsAsTheyAreDrawn();
    void renamingIsANameOverAnIdThatDoesNotChange();
    void clearingANameGivesTheSectionBackRelaysWording();
    void removingASectionFreesItsStatusesRatherThanHidingThem();
    void aSectionThatIsOnlyThereBecauseOfACardCannotBeRemoved();
    void theLastTwoSectionsCanBeRenamedButNeverTakenAway();
    void mergingPutsBothSetsOfStatusesInOneSection();
    void aNewSectionNeedsANameAndStatusesNobodyElseCollects();
    void theMessageWritesOnlyWhatDiffersFromTheDefault();
    void anUntouchedPlanHasNothingToSay();
    void theGearSitsAfterTheSectionBoxesAndOpensThePage();
    void savingSendsOneBoardSectionsMessageAndClosesThePage();
    void escLeavesTheSectionsAsTheyAre();
    void aRenameReachesTheCheckboxAsWellAsTheHeader();
};

void BoardSectionsTests::thePlanIsTheSectionsAsTheyAreDrawn()
{
    const Model model = board();
    const SectionPlan plan = SectionPlan::from(model);
    QCOMPARE(ids(plan), (QStringList{"inbox", "discussing", "ready", "in-progress", "waiting",
                                     "needs-qa", "verified", "done"}));
    // Configured sections are the ones `columns:` lists; Verified and Done are always last.
    QVERIFY(plan.row(QStringLiteral("ready"))->configured);
    QVERIFY(plan.row(QStringLiteral("verified"))->fixed);
    QVERIFY(plan.row(QStringLiteral("done"))->fixed);
    QCOMPARE(plan.row(QStringLiteral("waiting"))->statuses,
             (QStringList{"needs-review", "needs-labels", "needs-ab"}));
    QVERIFY(!plan.dirty());
    // `deferred` is in no section here, so it is what a new section could be given.
    QVERIFY(plan.freeStatuses().contains(QStringLiteral("deferred")));
    QVERIFY(!plan.freeStatuses().contains(QStringLiteral("ready")));
}

void BoardSectionsTests::renamingIsANameOverAnIdThatDoesNotChange()
{
    const Model model = board();
    SectionPlan plan = SectionPlan::from(model);
    plan.rename(QStringLiteral("ready"), QStringLiteral("Up next"));
    QVERIFY(plan.dirty());
    // The id is untouched — nothing that reads a status or drops a card sees a rename.
    QCOMPARE(ids(plan).at(2), QStringLiteral("ready"));
    QCOMPARE(plan.row(QStringLiteral("ready"))->title(), QStringLiteral("Up next"));
    const QJsonObject message = plan.message();
    QCOMPARE(message.value(QStringLiteral("column_titles")).toObject()
                 .value(QStringLiteral("ready")).toString(), QStringLiteral("Up next"));
    // A rename alone changes no section's statuses and no section's place.
    QCOMPARE(arrayOf(message, "columns"),
             (QStringList{"inbox", "discussing", "ready", "in-progress", "waiting", "needs-qa",
                          "done"}));
    QVERIFY(message.value(QStringLiteral("column_statuses")).toObject().isEmpty());
}

void BoardSectionsTests::clearingANameGivesTheSectionBackRelaysWording()
{
    const Model model = board({{"column_titles", QJsonObject{{"ready", "Up next"}}}});
    SectionPlan plan = SectionPlan::from(model);
    QCOMPARE(plan.row(QStringLiteral("ready"))->title(), QStringLiteral("Up next"));
    plan.rename(QStringLiteral("ready"), QString());
    QCOMPARE(plan.row(QStringLiteral("ready"))->title(), QStringLiteral("Ready to start"));
    // An empty name is written as no entry at all, which is what puts the default back.
    QVERIFY(plan.message().value(QStringLiteral("column_titles")).toObject().isEmpty());
}

void BoardSectionsTests::removingASectionFreesItsStatusesRatherThanHidingThem()
{
    const Model model = board();
    SectionPlan plan = SectionPlan::from(model);
    QVERIFY(plan.canRemove(QStringLiteral("waiting")));
    plan.remove(QStringLiteral("waiting"));
    QVERIFY(!ids(plan).contains(QStringLiteral("waiting")));
    QCOMPARE(arrayOf(plan.message(), "columns"),
             (QStringList{"inbox", "discussing", "ready", "in-progress", "needs-qa", "done"}));
    // The cards are not hidden: their statuses are free again, and the model gives a status no
    // section collects a section of its own.
    for (const char *status : {"needs-review", "needs-labels", "needs-ab"})
        QVERIFY2(plan.freeStatuses().contains(QString::fromLatin1(status)), status);
    QVERIFY(plan.summary().contains(QStringLiteral("section of its own")));
}

void BoardSectionsTests::aSectionThatIsOnlyThereBecauseOfACardCannotBeRemoved()
{
    // A deferred card with no `deferred` column: the model draws the section anyway, and taking
    // it away would do nothing at all — it would be back on the next redraw.
    Model model = board();
    model.reset(QJsonArray{QJsonObject{{"id", "K7Q2"}, {"title", "parked"}, {"type", "work"},
                                       {"status", "deferred"}, {"tab", "features"}, {"rank", "i"},
                                       {"path", "issues/features/k.md"}}});
    SectionPlan plan = SectionPlan::from(model);
    QVERIFY(ids(plan).contains(QStringLiteral("deferred")));
    QVERIFY(!plan.canRemove(QStringLiteral("deferred")));
    QVERIFY(plan.whyNotRemove(QStringLiteral("deferred")).contains(QStringLiteral("cards have")));
    plan.remove(QStringLiteral("deferred"));
    QVERIFY(!plan.dirty());
    // Merging it away is the thing that does work, and it is offered.
    QVERIFY(plan.canMerge(QStringLiteral("deferred")));
    plan.merge(QStringLiteral("deferred"), QStringLiteral("ready"));
    QVERIFY(plan.row(QStringLiteral("ready"))->statuses.contains(QStringLiteral("deferred")));
}

void BoardSectionsTests::theLastTwoSectionsCanBeRenamedButNeverTakenAway()
{
    const Model model = board();
    SectionPlan plan = SectionPlan::from(model);
    for (const QString &id : {QStringLiteral("verified"), QStringLiteral("done")}) {
        QVERIFY2(!plan.canRemove(id), qPrintable(id));
        QVERIFY2(!plan.canMerge(id), qPrintable(id));
        QVERIFY2(!plan.whyNotRemove(id).isEmpty(), qPrintable(id));
    }
    // Nothing can be merged *into* them either: a card only reaches them by being closed.
    QVERIFY(!plan.mergeTargets(QStringLiteral("ready")).contains(QStringLiteral("done")));
    QVERIFY(!plan.mergeTargets(QStringLiteral("ready")).contains(QStringLiteral("verified")));
    // But a name is only a name.
    plan.rename(QStringLiteral("done"), QStringLiteral("Shipped"));
    QCOMPARE(plan.message().value(QStringLiteral("column_titles")).toObject()
                 .value(QStringLiteral("done")).toString(), QStringLiteral("Shipped"));
}

void BoardSectionsTests::mergingPutsBothSetsOfStatusesInOneSection()
{
    const Model model = board();
    SectionPlan plan = SectionPlan::from(model);
    plan.merge(QStringLiteral("waiting"), QStringLiteral("needs-qa"));
    plan.rename(QStringLiteral("needs-qa"), QStringLiteral("Checks"));
    QVERIFY(!ids(plan).contains(QStringLiteral("waiting")));
    QCOMPARE(plan.row(QStringLiteral("needs-qa"))->statuses,
             (QStringList{"needs-qa-llm", "needs-qa-human", "needs-review", "needs-labels",
                          "needs-ab"}));
    const QJsonObject message = plan.message();
    QCOMPARE(arrayOf(message, "columns"),
             (QStringList{"inbox", "discussing", "ready", "in-progress", "needs-qa", "done"}));
    QCOMPARE(arrayOf(message.value(QStringLiteral("column_statuses")).toObject(), "needs-qa"),
             (QStringList{"needs-qa-llm", "needs-qa-human", "needs-review", "needs-labels",
                          "needs-ab"}));
    QCOMPARE(titles(plan).at(4), QStringLiteral("Checks"));
    QVERIFY(plan.summary().contains(QStringLiteral("merged into")));
}

void BoardSectionsTests::aNewSectionNeedsANameAndStatusesNobodyElseCollects()
{
    const Model model = board();
    SectionPlan plan = SectionPlan::from(model);
    QVERIFY(!plan.addRefusal(QString(), {QStringLiteral("deferred")}).isEmpty());
    QVERIFY(plan.addRefusal(QStringLiteral("Parked"), {}).contains(QStringLiteral("at least one")));
    // A status belongs to one section: two would draw the same card twice.
    QVERIFY(plan.addRefusal(QStringLiteral("Parked"), {QStringLiteral("ready")})
                .contains(QStringLiteral("already collected")));
    QVERIFY(!plan.dirty());

    const QString id = plan.add(QStringLiteral("Parked"), {QStringLiteral("deferred")});
    QCOMPARE(id, QStringLiteral("parked"));
    // It goes in before the two that are always last, and into `columns:` before Done.
    QCOMPARE(ids(plan), (QStringList{"inbox", "discussing", "ready", "in-progress", "waiting",
                                     "needs-qa", "parked", "verified", "done"}));
    const QJsonObject message = plan.message();
    QCOMPARE(arrayOf(message, "columns"),
             (QStringList{"inbox", "discussing", "ready", "in-progress", "waiting", "needs-qa",
                          "parked", "done"}));
    // An invented section is only allowed when it says what it collects, so the message carries
    // its statuses even though it is the only section in it.
    QCOMPARE(arrayOf(message.value(QStringLiteral("column_statuses")).toObject(), "parked"),
             (QStringList{"deferred"}));
    QCOMPARE(message.value(QStringLiteral("column_titles")).toObject()
                 .value(QStringLiteral("parked")).toString(), QStringLiteral("Parked"));
    // And its name cannot be taken twice.
    QVERIFY(!plan.addRefusal(QStringLiteral("Parked"), {QStringLiteral("dropped")}).isEmpty());
}

void BoardSectionsTests::theMessageWritesOnlyWhatDiffersFromTheDefault()
{
    const Model model = board();
    SectionPlan plan = SectionPlan::from(model);
    plan.rename(QStringLiteral("inbox"), QStringLiteral("Caught"));
    // Every section still collects exactly what it collects by default, so board.yaml gets no
    // column_statuses at all: a board stays as short as it is ordinary.
    QVERIFY(plan.message().value(QStringLiteral("column_statuses")).toObject().isEmpty());
    plan.merge(QStringLiteral("waiting"), QStringLiteral("needs-qa"));
    const QJsonObject statuses = plan.message().value(QStringLiteral("column_statuses")).toObject();
    QCOMPARE(statuses.keys(), (QStringList{"needs-qa"}));
}

void BoardSectionsTests::anUntouchedPlanHasNothingToSay()
{
    const Model model = board();
    SectionPlan plan = SectionPlan::from(model);
    QVERIFY(!plan.dirty());
    QVERIFY(plan.summary().isEmpty());
    // A rename to the name it already has is not a change.
    plan.rename(QStringLiteral("ready"), QString());
    QVERIFY(!plan.dirty());
}

void BoardSectionsTests::theGearSitsAfterTheSectionBoxesAndOpensThePage()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened());
    auto *gear = view.findChild<QToolButton *>(QStringLiteral("boardSectionGear"));
    QVERIFY(gear);
    // After the checkboxes, not among them: it is about the list of sections, not about one.
    auto *checks = view.findChild<QWidget *>(QStringLiteral("boardSectionChecks"));
    QVERIFY(checks);
    QCOMPARE(gear->parentWidget(), checks);
    const QList<QCheckBox *> boxes = checks->findChildren<QCheckBox *>();
    QVERIFY(!boxes.isEmpty());
    for (QCheckBox *box : boxes)
        QVERIFY(box->x() < gear->x() || box->y() < gear->y());
    // Unticking a section is still its own thing: the gear is not one of the boxes.
    QCOMPARE(boxes.size(), view.model().sections().size());

    auto *page = view.findChild<QWidget *>(QStringLiteral("boardSectionEditor"));
    QVERIFY(page);
    QVERIFY(page->isHidden());
    QVERIFY(!view.sectionsOpen());
    gear->click();
    QVERIFY(view.sectionsOpen());
    QVERIFY(!page->isHidden());
    // A row per section, each with the name it is drawn under.
    const QList<QLineEdit *> names = page->findChildren<QLineEdit *>(QStringLiteral("boardSectionName"));
    QCOMPARE(names.size(), view.model().sections().size());
    QCOMPARE(names.first()->text(), QStringLiteral("Inbox"));
}

void BoardSectionsTests::savingSendsOneBoardSectionsMessageAndClosesThePage()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened());
    view.findChild<QToolButton *>(QStringLiteral("boardSectionGear"))->click();
    auto *page = view.findChild<QWidget *>(QStringLiteral("boardSectionEditor"));
    auto *save = page->findChild<QPushButton *>(QStringLiteral("boardSectionEditorSave"));
    QVERIFY(save);
    QVERIFY(!save->isEnabled());            // nothing changed yet: nothing to write
    save->click();
    QVERIFY(sent.isEmpty());

    // Rename one section and save: one message, and the page gets out of the way.
    auto *name = page->findChildren<QLineEdit *>(QStringLiteral("boardSectionName")).at(2);
    QCOMPARE(name->text(), QStringLiteral("Ready to start"));
    name->setText(QStringLiteral("Up next"));
    emit name->editingFinished();
    QVERIFY(save->isEnabled());
    save->click();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.first().value(QStringLiteral("type")).toString(), QStringLiteral("board_sections"));
    QCOMPARE(sent.first().value(QStringLiteral("column_titles")).toObject()
                 .value(QStringLiteral("ready")).toString(), QStringLiteral("Up next"));
    QVERIFY(!view.sectionsOpen());
    QVERIFY(page->isHidden());

    // The worker answers with the config it wrote, and the section is renamed on the list without
    // the pane being reopened.
    QJsonObject config = ::config();
    config.insert(QStringLiteral("column_titles"), QJsonObject{{"ready", "Up next"}});
    view.handleEvent(QJsonObject{{"event", "board_changed"}, {"upserts", QJsonArray{}},
                                 {"removed", QJsonArray{}}, {"problems", QJsonArray{}},
                                 {"config", config}});
    QCOMPARE(view.model().sectionTitle(QStringLiteral("ready")), QStringLiteral("Up next"));
    QCOMPARE(view.model().sections().at(2).title, QStringLiteral("Up next"));
    // The id did not move: the cards are exactly where they were.
    QCOMPARE(view.model().sections().at(2).id, QStringLiteral("ready"));
}

void BoardSectionsTests::escLeavesTheSectionsAsTheyAre()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened());
    view.findChild<QToolButton *>(QStringLiteral("boardSectionGear"))->click();
    auto *page = view.findChild<QWidget *>(QStringLiteral("boardSectionEditor"));
    QVERIFY(view.sectionsOpen());
    // A staged edit and then Esc: nothing is written, and the page is gone.
    auto *name = page->findChildren<QLineEdit *>(QStringLiteral("boardSectionName")).at(2);
    name->setText(QStringLiteral("Up next"));
    emit name->editingFinished();
    QTest::keyClick(page, Qt::Key_Escape);
    QVERIFY(!view.sectionsOpen());
    QVERIFY(page->isHidden());
    QVERIFY(sent.isEmpty());
    // And the gear opens on the truth again rather than on the abandoned edit.
    view.findChild<QToolButton *>(QStringLiteral("boardSectionGear"))->click();
    QCOMPARE(page->findChildren<QLineEdit *>(QStringLiteral("boardSectionName")).at(2)->text(),
             QStringLiteral("Ready to start"));
}

void BoardSectionsTests::aRenameReachesTheCheckboxAsWellAsTheHeader()
{
    // The checkbox row is rebuilt from the section list, which a rename leaves the *ids* of
    // exactly as they were: comparing those alone left a box reading "READY TO START" under a
    // header that already said "UP NEXT" (seen in the pane, 2026-09-19).
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened());
    auto *checks = view.findChild<QWidget *>(QStringLiteral("boardSectionChecks"));
    auto boxText = [checks] {
        QStringList out;
        for (QCheckBox *box : checks->findChildren<QCheckBox *>())
            out << box->text();
        return out;
    };
    QVERIFY(boxText().contains(QStringLiteral("READY TO START")));

    QJsonObject config = ::config();
    config.insert(QStringLiteral("column_titles"), QJsonObject{{"ready", "Up next"}});
    view.handleEvent(QJsonObject{{"event", "board_changed"}, {"upserts", QJsonArray{}},
                                 {"removed", QJsonArray{}}, {"problems", QJsonArray{}},
                                 {"config", config}});
    QVERIFY(!boxText().contains(QStringLiteral("READY TO START")));
    QVERIFY(boxText().contains(QStringLiteral("UP NEXT")));
    // Still one box per section, and the gear is still the last thing in the row.
    QCOMPARE(boxText().size(), view.model().sections().size());
    QVERIFY(view.findChild<QToolButton *>(QStringLiteral("boardSectionGear")));
}

QTEST_MAIN(BoardSectionsTests)
#include "boardsections_test.moc"
