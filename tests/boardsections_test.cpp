// SPDX-License-Identifier: AGPL-3.0-or-later
// The section editor behind the gear at the end of the Board's section checkboxes: the five
// things it offers (add, remove, merge, rename, move), what it refuses, and the one
// `board_sections` message it produces. `SectionPlan` holds no widgets, so none of this needs the
// pane.
//
// The property every test here is really about: **a section is a view of the statuses**. No edit
// moves a card or changes one, and no edit can lose a card — a status the edit leaves uncollected
// comes back as a section of its own.
#include "BoardSections.h"

#include "BoardPane.h"

#include <QCheckBox>
#include <QJsonArray>
#include <QLabel>
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
    void movingASectionRewritesColumnsAndNeverACard();
    void theRowsMoveWithTheButtonsAndTheDropLandsWhereItWasDropped();
    void theFolderRowOffersToMoveTheBoardToBoard();
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
    // A card carrying a status no column collects (a `needs-review` one here): the model draws
    // the section anyway, and taking it away would do nothing at all — it would be back on the
    // next redraw. (`deferred` used to be the example until it stopped drawing a section at all,
    // owner, 2026-09-25: "remove active and deferred".)
    Model model = board();
    model.reset(QJsonArray{QJsonObject{{"id", "K7Q2"}, {"title", "parked"}, {"type", "work"},
                                       {"status", "icebox"}, {"tab", "features"}, {"rank", "i"},
                                       {"path", "issues/features/k.md"}}});
    SectionPlan plan = SectionPlan::from(model);
    QVERIFY(ids(plan).contains(QStringLiteral("icebox")));
    QVERIFY(!plan.canRemove(QStringLiteral("icebox")));
    QVERIFY(plan.whyNotRemove(QStringLiteral("icebox")).contains(QStringLiteral("cards have")));
    plan.remove(QStringLiteral("icebox"));
    QVERIFY(!plan.dirty());
    // Merging it away is the thing that does work, and it is offered.
    QVERIFY(plan.canMerge(QStringLiteral("icebox")));
    plan.merge(QStringLiteral("icebox"), QStringLiteral("ready"));
    QVERIFY(plan.row(QStringLiteral("ready"))->statuses.contains(QStringLiteral("icebox")));
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
    // No statuses is a manual section (#3XZV): one you fill by hand with a drop, so it is allowed.
    QVERIFY(plan.addRefusal(QStringLiteral("Parked"), {}).isEmpty());
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

// Moving a section (owner, 2026-09-19: "we do need sorting of sections though. enable those to be
// dragged and dropped, with up and down buttons for moving them, in the section settings modal"):
// the order is `columns:`, so a move is one rewrite of the section list and no card moves at all.
void BoardSectionsTests::movingASectionRewritesColumnsAndNeverACard()
{
    const Model model = board();
    SectionPlan plan = SectionPlan::from(model);
    QCOMPARE(ids(plan), (QStringList{"inbox", "discussing", "ready", "in-progress", "waiting",
                                     "needs-qa", "verified", "done"}));

    // One place up, and `columns:` follows the list the sections are drawn in.
    QVERIFY(plan.canMove(QStringLiteral("ready"), -1));
    plan.move(QStringLiteral("ready"), -1);
    QCOMPARE(ids(plan), (QStringList{"inbox", "ready", "discussing", "in-progress", "waiting",
                                     "needs-qa", "verified", "done"}));
    QCOMPARE(arrayOf(plan.message(), QStringLiteral("columns")),
             (QStringList{"inbox", "ready", "discussing", "in-progress", "waiting", "needs-qa",
                          "done"}));
    QVERIFY(plan.summary().contains(QStringLiteral("Ready to start moved above Discussing")));

    // Verified and Done are always the last two: nothing moves past them, and they do not move.
    QVERIFY(!plan.canMove(QStringLiteral("inbox"), -1));
    QVERIFY(!plan.canMove(QStringLiteral("needs-qa"), 1));
    QVERIFY(!plan.canMove(QStringLiteral("verified"), -1));
    QVERIFY(!plan.canMove(QStringLiteral("done"), -1));
    QVERIFY(plan.whyNotMove(QStringLiteral("done"), -1).contains(QStringLiteral("always")));
    QVERIFY(plan.whyNotMove(QStringLiteral("inbox"), -1).contains(QStringLiteral("first")));

    // A drag: `waiting` dropped in front of the first section goes to the top, and a drop that
    // would write the same file writes nothing at all.
    QVERIFY(plan.moveBefore(QStringLiteral("waiting"), QStringLiteral("inbox")));
    QCOMPARE(ids(plan).first(), QStringLiteral("waiting"));
    QVERIFY(!plan.moveBefore(QStringLiteral("waiting"), QStringLiteral("inbox")));   // already there
    // A drop at or under the two that are last lands just above them.
    QVERIFY(plan.moveBefore(QStringLiteral("waiting"), QStringLiteral("verified")));
    QCOMPARE(ids(plan).at(5), QStringLiteral("waiting"));
    QCOMPARE(ids(plan).at(6), QStringLiteral("verified"));
    QCOMPARE(ids(plan).last(), QStringLiteral("done"));

    // A section that is only there because a card carries that status moves like any other, and is
    // simply not written into `columns:` — it has no line in the file to move. (`deferred` used
    // to be the example here until it stopped drawing a section at all, owner, 2026-09-25:
    // "remove active and deferred".)
    Model withCard = board();
    withCard.reset(QJsonArray{QJsonObject{{"id", "DEF1"}, {"title", "Needs review"},
                                          {"type", "work"}, {"status", "icebox"},
                                          {"tab", "features"}, {"rank", "i"},
                                          {"path", "issues/features/DEF1.md"}}});
    SectionPlan extra = SectionPlan::from(withCard);
    QVERIFY(ids(extra).contains(QStringLiteral("icebox")));
    QVERIFY(!extra.row(QStringLiteral("icebox"))->configured);
    QVERIFY(extra.moveBefore(QStringLiteral("icebox"), QStringLiteral("inbox")));
    QCOMPARE(ids(extra).first(), QStringLiteral("icebox"));
    QVERIFY(!arrayOf(extra.message(), QStringLiteral("columns"))
                 .contains(QStringLiteral("icebox")));
}

// The same verb from the page: the ▲ ▼ buttons, the drag handle, and a section dropped on a row.
void BoardSectionsTests::theRowsMoveWithTheButtonsAndTheDropLandsWhereItWasDropped()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened());
    view.findChild<QToolButton *>(QStringLiteral("boardSectionGear"))->click();
    auto *page = view.findChild<QWidget *>(QStringLiteral("boardSectionEditor"));
    auto *save = page->findChild<QPushButton *>(QStringLiteral("boardSectionEditorSave"));
    const auto names = [page] {
        QStringList out;
        for (QLineEdit *name : page->findChildren<QLineEdit *>(QStringLiteral("boardSectionName")))
            out << name->text();
        return out;
    };

    // One handle and one ▲ ▼ per section, in the order the list draws them; the two that are always
    // last cannot be dragged and their buttons are off.
    const QList<QLabel *> handles =
        page->findChildren<QLabel *>(QStringLiteral("boardSectionHandle"));
    QCOMPARE(handles.size(), view.model().sections().size());
    QVERIFY(!handles.first()->isHidden());
    QVERIFY(handles.at(handles.size() - 2)->isHidden());    // Verified
    QVERIFY(handles.last()->isHidden());                    // Done
    const QList<QToolButton *> ups =
        page->findChildren<QToolButton *>(QStringLiteral("boardSectionUp"));
    const QList<QToolButton *> downs =
        page->findChildren<QToolButton *>(QStringLiteral("boardSectionDown"));
    QCOMPARE(ups.size(), view.model().sections().size());
    QCOMPARE(downs.size(), view.model().sections().size());
    QVERIFY(!ups.first()->isEnabled());                     // Inbox is already first
    QVERIFY(ups.at(2)->isEnabled());                        // Ready can move up
    QVERIFY(!ups.at(ups.size() - 2)->isEnabled());          // Verified is the last movable one
    QVERIFY(!downs.last()->isEnabled());                    // Done is the last section

    // ▲ on Ready: the rows are redrawn in the new order, and nothing is written until Save.
    ups.at(2)->click();
    QVERIFY(sent.isEmpty());
    QVERIFY(save->isEnabled());
    QCOMPARE(names().at(0), QStringLiteral("Inbox"));
    QCOMPARE(names().at(1), QStringLiteral("Ready to start"));
    QVERIFY(page->findChild<QLabel *>(QStringLiteral("boardSectionEditorSummary"))->text()
                .contains(QStringLiteral("moved above")));

    // A drop: a section dropped on the top half of a row goes in front of that row's section. Qt
    // delivers a real drop only through its own drag machinery, so the row's own entry point is
    // what is driven here — the half of the row the pointer is in is the row's decision.
    const auto rowAt = [page](int index) {
        return dynamic_cast<relay::board::SectionRow *>(
            page->findChildren<QWidget *>(QStringLiteral("boardSectionRow")).at(index));
    };
    auto *first = rowAt(0);
    QVERIFY(first);
    first->resize(first->width(), 40);          // the page is not shown: give the row a height
    first->dropHere(QStringLiteral("in-progress"), 2);
    QTest::qWait(1);                           // the rebuild is deferred out of the drop itself
    QCOMPARE(names().first(), QStringLiteral("In progress"));
    // Dropping a section on its own row changes nothing, and the bottom half of a row means under
    // it: `In progress` lands after Inbox.
    first = rowAt(0);
    QVERIFY(first);
    first->resize(first->width(), 40);
    first->dropHere(QStringLiteral("in-progress"), 2);      // its own row: nothing to do
    QTest::qWait(1);
    QCOMPARE(names().first(), QStringLiteral("In progress"));
    auto *second = rowAt(1);
    QVERIFY(second);
    second->resize(second->width(), 40);
    second->dropHere(QStringLiteral("in-progress"), 38);
    QTest::qWait(1);
    QCOMPARE(names().first(), QStringLiteral("Inbox"));
    QCOMPARE(names().at(1), QStringLiteral("In progress"));

    // Save writes the section list in that order, and nothing about a card.
    save->click();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.first().value(QStringLiteral("type")).toString(), QStringLiteral("board_sections"));
    QCOMPARE(arrayOf(sent.first(), QStringLiteral("columns")),
             (QStringList{"inbox", "in-progress", "ready", "discussing", "waiting", "needs-qa",
                          "done"}));
    QVERIFY(!view.sectionsOpen());
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

// The board's folder (protocol 19.17, cards #916B and #1CXD): the gear's page says where the board
// is kept and offers the one move — to `board/` — on the two older spellings, and nothing at all on
// a `board/` board, which is already there, or an `issues/` one, which is never moved.
void BoardSectionsTests::theFolderRowOffersToMoveTheBoardToBoard()
{
    relay::board::SectionEditor editor;
    editor.setModel(Model());
    auto *row = editor.findChild<QWidget *>(QStringLiteral("boardFolderRow"));
    auto *button = editor.findChild<QPushButton *>(QStringLiteral("boardFolderButton"));
    QVERIFY(row && button);
    QVERIFY(!row->isVisibleTo(&editor));                       // nothing named yet: nothing offered

    int asked = 0;
    editor.onFolder = [&asked] { ++asked; };

    editor.setFolder(QStringLiteral("switchboard"));
    QVERIFY(row->isVisibleTo(&editor));
    QCOMPARE(button->text(), QStringLiteral("Move this board to .board/"));
    QVERIFY(button->toolTip().contains(QStringLiteral("Rename switchboard/ to .board/")));
    button->click();
    QCOMPARE(asked, 1);

    editor.setFolder(QStringLiteral(".switchboard"));
    QVERIFY(row->isVisibleTo(&editor));
    QCOMPARE(button->text(), QStringLiteral("Move this board to .board/"));   // the same move
    QVERIFY(button->toolTip().contains(QStringLiteral("Rename .switchboard/ to .board/")));
    button->click();
    QCOMPARE(asked, 2);

    editor.setFolder(QStringLiteral("board"));
    QVERIFY(row->isVisibleTo(&editor));
    QVERIFY(button->toolTip().contains(QStringLiteral("Rename board/ to .board/")));

    editor.setFolder(QStringLiteral(".board"));                 // already there: nothing to offer
    QVERIFY(!row->isVisibleTo(&editor));
    QCOMPARE(editor.folder(), QStringLiteral(".board"));

    editor.setFolder(QStringLiteral("issues"));                // the original spelling: never moved
    QVERIFY(!row->isVisibleTo(&editor));
    QCOMPARE(editor.folder(), QStringLiteral("issues"));
}

QTEST_MAIN(BoardSectionsTests)
#include "boardsections_test.moc"
