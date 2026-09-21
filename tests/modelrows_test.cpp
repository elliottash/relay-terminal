// SPDX-License-Identifier: AGPL-3.0-or-later
// The one list every model box draws (#PK5Q, owner 2026-09-20: "can you have the picker be the
// same as in the main terminal"; #MDL1, owner 2026-09-21, design 5.3: a header per class and its
// top-ranked models under it). The point of the module is that a terminal pane's box and a
// console's box built from the same catalog are the *same rows*, so that is what is asserted here
// — plus the four rulings that shaped the second design: headers are not selectable, two per
// class by default, exhausted models do not show up, and the chip is the model alone.
#include "FilterPopup.h"   // kTrailingItemRole: the via column, as a combo row carries it
#include "ModelRows.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace relay;
using namespace relay::models;

namespace {

QJsonObject model(const QString &id, const QString &label, const QString &tier)
{
    return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
                       {QStringLiteral("tier"), tier}};
}

QJsonArray presets()
{
    QJsonArray out;
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("glm-coding")},
                       {QStringLiteral("label"), QStringLiteral("z.ai · glm-5.3 · coding plan")},
                       {QStringLiteral("provider"), QStringLiteral("z.ai (glm)")},
                       {QStringLiteral("model"), QStringLiteral("glm-5.3")},
                       {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("glm-5.3"), QStringLiteral("glm-5.3"), QStringLiteral("main")),
                            model(QStringLiteral("glm-5.3-flash"), QStringLiteral("glm-5.3 flash"), QStringLiteral("flash"))}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("kimi-code")},
                       {QStringLiteral("label"), QStringLiteral("kimi · k3 · coding plan")},
                       {QStringLiteral("provider"), QStringLiteral("kimi")},
                       {QStringLiteral("model"), QStringLiteral("kimi-k3")},
                       {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("kimi-k3"), QStringLiteral("kimi k3"), QStringLiteral("main"))}}};
    // The same model from a second provider: one row, and the row says which one it would spend
    // (card #MDL1, rule 2).
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("openrouter")},
                       {QStringLiteral("label"), QStringLiteral("openrouter")},
                       {QStringLiteral("provider"), QStringLiteral("openrouter")},
                       {QStringLiteral("model"), QStringLiteral("z-ai/glm-5.3")},
                       {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("z-ai/glm-5.3"), QStringLiteral("glm-5.3"), QStringLiteral("main"))}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("openai")},
                       {QStringLiteral("label"), QStringLiteral("openai · gpt-6-astra")},
                       {QStringLiteral("provider"), QStringLiteral("openai")},
                       {QStringLiteral("model"), QStringLiteral("gpt-6-astra")},
                       {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("gpt-6-astra"), QStringLiteral("gpt-6-astra"), QStringLiteral("main"))}}};
    // A provider with no key: never a row the box can run, but it is not hidden either — it is
    // greyed in place wherever a list ranks it.
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("anthropic")},
                       {QStringLiteral("label"), QStringLiteral("anthropic · claude-opus-5")},
                       {QStringLiteral("provider"), QStringLiteral("anthropic")},
                       {QStringLiteral("model"), QStringLiteral("claude-opus-5")},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("claude-opus-5"), QStringLiteral("claude-opus-5"), QStringLiteral("main"))}}};
    return out;
}

// The owner's lists: main is kimi then glm then the openrouter twin, high is the openai model,
// flash is glm's flash model. Deliberately *not* alphabetical — the order is the information.
void seedTierLists()
{
    const auto entry = [](const QString &key) { return curation::TierEntry{key, QString()}; };
    // Four entries, folding to three rows: kimi-k3, glm-5.3 (z.ai and its OpenRouter twin are one
    // row, rule 2) and gpt-6-astra. Three rows against a cutoff of two is what makes the cutoff
    // visible at all.
    curation::setTierList(QStringLiteral("main"), {entry(QStringLiteral("kimi-code|kimi-k3")),
                                                   entry(QStringLiteral("glm-coding|glm-5.3")),
                                                   entry(QStringLiteral("openrouter|z-ai/glm-5.3")),
                                                   entry(QStringLiteral("openai|gpt-6-astra"))});
    curation::setTierList(QStringLiteral("high"), {entry(QStringLiteral("openai|gpt-6-astra")),
                                                   entry(QStringLiteral("anthropic|claude-opus-5"))});
    curation::setTierList(QStringLiteral("flash"), {entry(QStringLiteral("glm-coding|glm-5.3-flash"))});
}

// What a terminal pane hands the builder: the modes it offers, the one it is in, and its own model.
modelrows::Context paneContext()
{
    modelrows::Context context;
    context.catalog = catalogFrom(presets());
    context.modePick.insert(QStringLiteral("main"), QStringLiteral("kimi-code|kimi-k3"));
    context.roleModel.insert(QStringLiteral("high"), QStringLiteral("kimi-k3"));
    context.now = 1;
    return context;
}

// What a console hands it: the same catalog and the same modes, and its main-tier worker role
// ("switchboard") standing in for main (owner, 2026-09-21: "those should be the same systems").
modelrows::Context consoleContext()
{
    modelrows::Context context = paneContext();
    context.modeRole.insert(QStringLiteral("main"), QStringLiteral("switchboard"));
    return context;
}

QStringList textsOf(const QList<modelrows::Row> &rows)
{
    QStringList out;
    for (const modelrows::Row &row : rows) out << row.text + QLatin1Char('\t') + row.trailing;
    return out;
}

QStringList dataOf(const QList<modelrows::Row> &rows)
{
    QStringList out;
    for (const modelrows::Row &row : rows) out << row.data;
    return out;
}

// The box as a person reads it down the screen: a header is its own name, a model row is indented
// under it. One string per row, so a whole shape can be compared in one line.
QStringList shapeOf(const QList<modelrows::Row> &rows)
{
    QStringList out;
    for (const modelrows::Row &row : rows)
        out << (row.header ? row.text : QStringLiteral("  ") + row.text);
    return out;
}

void setBox(const QString &klass, int cutoff, bool shown)
{
    curation::setBoxCutoff(klass, cutoff);
    curation::setBoxShown(klass, shown);
}

void resetBox()
{
    for (const QString &klass : curation::boxClasses()) setBox(klass, curation::kBoxCutoffDefault, true);
}

}  // namespace

class ModelRowsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTest"));
        QCoreApplication::setApplicationName(QStringLiteral("modelrows"));
        QSettings().clear();
        seedTierLists();
    }

    void cleanup() { resetBox(); }

    // The shape the owner confirmed (design 5.3): a header per class, that class's list under it
    // down to the cutoff, then a separator and "more models…". "customize…" is gone — the dialog
    // has the Options button.
    void classesWithTheirTopModelsUnderThem()
    {
        const modelrows::Box shown = modelrows::box(paneContext());
        QCOMPARE(shapeOf(shown.rows),
                 QStringList({QStringLiteral("high"), QStringLiteral("  gpt-6-astra"),
                              QStringLiteral("main"), QStringLiteral("  kimi-k3"), QStringLiteral("  glm-5.3"),
                              QStringLiteral("flash"), QStringLiteral("  glm-5.3-flash"),
                              QStringLiteral("  more models…")}));
        QCOMPARE(dataOf(shown.rows).last(), QStringLiteral("gear:picker"));
        QVERIFY(shown.rows.last().separatorBefore);
        QVERIFY2(modelrows::indexOf(shown.rows, QStringLiteral("gear:modelOptions")) < 0,
                 "customize… is no longer a row of the box");
        // A model row carries the class it belongs to and, in list order, what it would spend.
        const int glm = modelrows::indexOf(shown.rows, QStringLiteral("pick:main|glm-coding|glm-5.3"));
        QVERIFY(glm > 0);
        QCOMPARE(shown.rows.at(glm).group, QStringLiteral("main"));
        QCOMPARE(shown.rows.at(glm).trailing, QStringLiteral("z.ai (glm) +1"));
        // Enter on a row switches the pane to that class *and* that model.
        QCOMPARE(dataOf(shown.rows).at(1), QStringLiteral("pick:high|openai|gpt-6-astra"));
        // The pane's own model opens highlighted, and it is the one in the pane's own class.
        QCOMPARE(shown.rows.at(shown.current).data, QStringLiteral("pick:main|kimi-code|kimi-k3"));
    }

    // Owner, 2026-09-21: "the class header rows are not selectable in the picker. thats
    // redundant." So a header is flagged, and it is flagged disabled — which is what every list in
    // Relay reads as "Up and Down step over this".
    void headersAreLabelsAndNeverSelectable()
    {
        int headers = 0;
        for (const modelrows::Row &row : modelrows::build(paneContext())) {
            if (!row.header) { QVERIFY2(row.enabled, qPrintable(row.data)); continue; }
            ++headers;
            QVERIFY2(!row.enabled, qPrintable(row.data));
            QCOMPARE(row.data, QStringLiteral("class:") + row.text);
            QCOMPARE(row.group, row.text);
        }
        QCOMPARE(headers, 3);   // high, main, flash — local only where this machine serves one
    }

    // "Two per class by default", and the cutoff the dialog's "show in box" column writes moves it.
    void theCutoffIsTwoByDefaultAndStored()
    {
        QCOMPARE(curation::boxCutoff(QStringLiteral("main")), 2);
        const modelrows::Context context = paneContext();
        QCOMPARE(shapeOf(modelrows::build(context)).mid(2, 3),
                 QStringList({QStringLiteral("main"), QStringLiteral("  kimi-k3"), QStringLiteral("  glm-5.3")}));
        // The third row of main is behind the cutoff, so the header says Right opens it.
        const QList<modelrows::Row> rows = modelrows::build(context);
        QCOMPARE(rows.at(modelrows::indexOf(rows, QStringLiteral("class:main"))).trailing, QStringLiteral("›"));
        QVERIFY(modelrows::expandable(context, QStringLiteral("main")));

        curation::setBoxCutoff(QStringLiteral("main"), 3);
        QCOMPARE(shapeOf(modelrows::build(context)).mid(2, 4),
                 QStringList({QStringLiteral("main"), QStringLiteral("  kimi-k3"), QStringLiteral("  glm-5.3"),
                              QStringLiteral("  gpt-6-astra")}));
        // Nothing left behind it: the header stops offering to open.
        QVERIFY(!modelrows::expandable(context, QStringLiteral("main")));
        QCOMPARE(modelrows::build(context).at(2).trailing, QString());
        // A cutoff longer than the list is not an error, and draws no empty rows.
        curation::setBoxCutoff(QStringLiteral("main"), 9);
        QCOMPARE(shapeOf(modelrows::build(context)).mid(2, 4),
                 QStringList({QStringLiteral("main"), QStringLiteral("  kimi-k3"), QStringLiteral("  glm-5.3"),
                              QStringLiteral("  gpt-6-astra")}));
    }

    // Right expands a class to its whole list, Left collapses it. The state is the caller's, so
    // this module only has to answer what the expanded box looks like.
    void expandingAClassShowsItsWholeList()
    {
        modelrows::Context context = paneContext();
        context.expanded.insert(QStringLiteral("main"));
        const QList<modelrows::Row> rows = modelrows::build(context);
        QCOMPARE(shapeOf(rows).mid(2, 4),
                 QStringList({QStringLiteral("main"), QStringLiteral("  kimi-k3"), QStringLiteral("  glm-5.3"),
                              QStringLiteral("  gpt-6-astra")}));
        // Expanded, the header says so the other way round.
        QCOMPARE(rows.at(modelrows::indexOf(rows, QStringLiteral("class:main"))).trailing, QStringLiteral("⌄"));
        // And only that class: high and flash are untouched.
        QCOMPARE(shapeOf(rows).mid(0, 2), QStringList({QStringLiteral("high"), QStringLiteral("  gpt-6-astra")}));
    }

    // A class switched off is not drawn at all — no header, no rows (design 5.3, the tab's
    // "show this class in the box" switch).
    void aClassSwitchedOffLeavesTheBox()
    {
        curation::setBoxShown(QStringLiteral("high"), false);
        const QStringList shape = shapeOf(modelrows::build(paneContext()));
        QVERIFY2(!shape.contains(QStringLiteral("high")), qPrintable(shape.join(QLatin1Char('/'))));
        QVERIFY(shape.startsWith(QStringLiteral("main")));
        QVERIFY(shape.contains(QStringLiteral("flash")));
        // gpt-6-astra is rank 4 of main and behind its cutoff, so switching high off really does
        // take it out of the box rather than moving it.
        QVERIFY(!shape.contains(QStringLiteral("  gpt-6-astra")));
    }

    // … unless the pane is *running* that class, when the class comes back with the pane's own
    // row and nothing else — the same rule that spares a pane's model below the cutoff. A box that
    // opens with nothing highlighted is a box that says the pane is running nothing.
    void aSwitchedOffClassKeepsThePanesOwnRow()
    {
        modelrows::Context context = paneContext();
        context.mode = QStringLiteral("high");
        context.modePick.insert(QStringLiteral("high"), QStringLiteral("openai|gpt-6-astra"));
        curation::setBoxShown(QStringLiteral("high"), false);
        const modelrows::Box shown = modelrows::box(context);
        const QStringList shape = shapeOf(shown.rows);
        // The class is drawn, with one row: the model this pane is on.
        QCOMPARE(shape.mid(0, 2), QStringList({QStringLiteral("high"), QStringLiteral("  gpt-6-astra")}));
        QCOMPARE(shown.rows.at(shown.current).data, QStringLiteral("pick:high|openai|gpt-6-astra"));
        // Rank 2 of high stays out, and the header offers no expansion: the class is still off.
        QVERIFY2(!shape.contains(QStringLiteral("  claude-opus-5")), qPrintable(shape.join(QLatin1Char('/'))));
        const modelrows::Row &head = shown.rows.at(modelrows::indexOf(shown.rows, QStringLiteral("class:high")));
        QCOMPARE(head.trailing, QString());
        QVERIFY(head.tooltip.contains(QStringLiteral("hidden from this box")));
        QVERIFY(!modelrows::expandable(context, QStringLiteral("high")));
        // Expanding it anyway (Right on the header) changes nothing while it is off.
        context.expanded.insert(QStringLiteral("high"));
        QCOMPARE(shapeOf(modelrows::build(context)).mid(0, 2),
                 QStringList({QStringLiteral("high"), QStringLiteral("  gpt-6-astra")}));
        context.expanded.clear();
        // A pane in another class does not bring it back: the switch means what it says.
        context.mode = QStringLiteral("main");
        QVERIFY(!shapeOf(modelrows::build(context)).contains(QStringLiteral("high")));
        // And a class whose only live row is not the pane's own is still not drawn: the pane is on
        // main, so nothing in high can be its home.
        curation::setBoxShown(QStringLiteral("high"), true);
    }

    // Owner, 2026-09-21: "exhausted models dont show up." Neither do the unusable ones — this is
    // the one place that differs from the dialog and the tier lists, which grey them in rank.
    void spentAndKeylessModelsAreNotInTheBox()
    {
        modelrows::Context context = paneContext();
        context.catalog.status.insert(QStringLiteral("kimi-code"), QStringLiteral("rejected"));
        const QList<modelrows::Row> rows = modelrows::build(context);
        QCOMPARE(modelrows::indexOf(rows, QStringLiteral("pick:main|kimi-code|kimi-k3")), -1);
        // glm-5.3 has two providers and only needs one of them, so its row stays.
        QVERIFY(modelrows::indexOf(rows, QStringLiteral("pick:main|glm-coding|glm-5.3")) > 0);
        // The third rank moves up into the room the spent one left: two per class, always two.
        QCOMPARE(shapeOf(rows).mid(2, 3),
                 QStringList({QStringLiteral("main"), QStringLiteral("  glm-5.3"), QStringLiteral("  gpt-6-astra")}));
        // claude-opus-5 is rank 2 of high and has no stored key: absent, not greyed.
        QCOMPARE(modelrows::indexOf(rows, QStringLiteral("pick:high|anthropic|claude-opus-5")), -1);
        QVERIFY(modelrows::indexOf(rows, QStringLiteral("pick:high|openai|gpt-6-astra")) > 0);
    }

    // "If the pane's own model is below the cutoff its row is shown anyway, so the highlight has
    // a home."
    void thePanesOwnModelSurvivesTheCutoff()
    {
        modelrows::Context context = paneContext();
        context.modePick.insert(QStringLiteral("main"), QStringLiteral("openai|gpt-6-astra"));   // rank 3 of main
        const modelrows::Box shown = modelrows::box(context);
        QCOMPARE(shapeOf(shown.rows).mid(2, 4),
                 QStringList({QStringLiteral("main"), QStringLiteral("  kimi-k3"), QStringLiteral("  glm-5.3"),
                              QStringLiteral("  gpt-6-astra")}));
        QCOMPARE(shown.rows.at(shown.current).data, QStringLiteral("pick:main|openai|gpt-6-astra"));
        // It is the *pane's* row, so the same model ranked below the cutoff of a class the pane is
        // not in does not get the same favour.
        modelrows::Context other = paneContext();
        other.mode = QStringLiteral("flash");
        QCOMPARE(shapeOf(modelrows::build(other)).mid(2, 3),
                 QStringList({QStringLiteral("main"), QStringLiteral("  kimi-k3"), QStringLiteral("  glm-5.3")}));
    }

    // A console's box is a terminal pane's box, row for row — only the worker role behind the main
    // class differs, and that is not something the box says (owner, 2026-09-21: the Switchboard
    // agent's box had read "kimi-k3 (switchboard)"; "(main)" was not wanted either).
    void aConsoleBuildsTheSameRowsAsAPane()
    {
        const QList<modelrows::Row> pane = modelrows::build(paneContext());
        const QList<modelrows::Row> console = modelrows::build(consoleContext());
        QCOMPARE(textsOf(console), textsOf(pane));
        QCOMPARE(dataOf(console), dataOf(pane));
        for (const modelrows::Row &row : console) QVERIFY(!row.text.contains(QStringLiteral("switchboard")));
    }

    // The collapsed chip is **the model alone**, on every mode (owner, 2026-09-21: "no need to
    // show the model class in the pane header"). The mode moved into the tooltip.
    void theCollapsedChipIsTheModelAlone()
    {
        modelrows::Context context = paneContext();
        QCOMPARE(modelrows::collapsedText(context), QStringLiteral("kimi-k3"));
        QVERIFY(modelrows::collapsedTooltip(context).isEmpty());
        context.mode = QStringLiteral("flash");
        QCOMPARE(modelrows::collapsedText(context), QStringLiteral("glm-5.3-flash"));
        QCOMPARE(modelrows::collapsedTooltip(context), QStringLiteral("This pane runs on the flash list."));
        context.mode = QStringLiteral("high");
        QCOMPARE(modelrows::collapsedText(context), QStringLiteral("gpt-6-astra"));
    }

    // A mode row's parentheses named what this pane would run; the classes replaced them, but
    // `modeModel` is still what the chip and the phone read.
    void modeModelNamesWhatThisPaneWouldRun()
    {
        modelrows::Context context = paneContext();
        QCOMPARE(modelrows::modeModel(context, QStringLiteral("flash")), QStringLiteral("glm-5.3-flash"));
        context.modePick.insert(QStringLiteral("flash"), QStringLiteral("kimi-code|kimi-k3"));
        QCOMPARE(modelrows::modeModel(context, QStringLiteral("flash")), QStringLiteral("kimi-k3"));
        // Nothing in the catalog and no pick: the worker's role summary answers, named by the one
        // rule — which is the ordinary case for `high` with no high list.
        modelrows::Context bare;
        bare.roleModel.insert(QStringLiteral("high"), QStringLiteral("openai/gpt-6-astra"));
        bare.now = 1;
        QCOMPARE(modelrows::modeModel(bare, QStringLiteral("high")), QStringLiteral("gpt-6-astra"));
    }

    // A local class only where this machine serves one, and guest rows under the classes.
    void localClassAndGuestRows()
    {
        modelrows::Context context = paneContext();
        // Nothing is ranked on the local list and this catalog serves nothing locally, so the
        // class has nothing to draw and is left out rather than drawn empty.
        context.classes << QStringLiteral("local");
        context.roleModel.insert(QStringLiteral("local"), QStringLiteral("bonsai-2-27b"));
        QVERIFY(!shapeOf(modelrows::build(context)).contains(QStringLiteral("local")));

        context.guests << QStringLiteral("claude");
        context.guestText.insert(QStringLiteral("claude"), QStringLiteral("Claude Code"));
        const QList<modelrows::Row> rows = modelrows::build(context);
        const int guest = modelrows::indexOf(rows, QStringLiteral("guest:claude"));
        QVERIFY(guest > 0);
        QVERIFY(rows.at(guest).separatorBefore);        // its own section under the classes
        QVERIFY(rows.at(guest).group.isEmpty());        // a guest is the pane's agent, not a class
        // A console passes none (#GH5T).
        QCOMPARE(modelrows::indexOf(modelrows::build(consoleContext()), QStringLiteral("guest:claude")), -1);
    }

    // Into a QComboBox: the same rows, the via column and the class on their own roles, the header
    // switched off, and the current row selected.
    void fillCarriesEverythingIntoTheBox()
    {
        QComboBox box;
        const int index = modelrows::fill(&box, paneContext());
        QVERIFY(index > 0);
        const int kimi = box.findData(QStringLiteral("pick:main|kimi-code|kimi-k3"));
        QCOMPARE(index, kimi);
        QCOMPARE(box.itemData(kimi, relay::kTrailingItemRole).toString(), QStringLiteral("kimi"));
        QCOMPARE(box.itemData(kimi, relay::kGroupItemRole).toString(), QStringLiteral("main"));
        QVERIFY(!box.itemData(kimi, relay::kHeaderItemRole).toBool());
        QVERIFY(box.model()->index(kimi, 0).flags().testFlag(Qt::ItemIsEnabled));
        const int head = box.findData(QStringLiteral("class:main"));
        QVERIFY(head >= 0);
        QVERIFY(box.itemData(head, relay::kHeaderItemRole).toBool());
        QVERIFY(!box.model()->index(head, 0).flags().testFlag(Qt::ItemIsEnabled));
    }

    // A main-tier role row is the model alone, in a console exactly as in a pane; the parentheses
    // on the other rows name the TIER, never a protocol role name. (The wording the phone's menu
    // and the tooltips still use.)
    void aMainTierRowIsJustTheModel()
    {
        using relay::modelrows::roleRowText;
        QCOMPARE(roleRowText(QStringLiteral("main"), QStringLiteral("kimi-k3")), QStringLiteral("kimi-k3"));
        QCOMPARE(roleRowText(QStringLiteral("switchboard"), QStringLiteral("kimi-k3")), QStringLiteral("kimi-k3"));
        QCOMPARE(roleRowText(QStringLiteral("planning"), QStringLiteral("glm-5.3")), QStringLiteral("glm-5.3 (high)"));
        QCOMPARE(roleRowText(QStringLiteral("high"), QStringLiteral("gpt-6-astra")),
                 QStringLiteral("gpt-6-astra (high)"));
        QCOMPARE(roleRowText(QStringLiteral("flash"), QStringLiteral("glm-5.3-flash")), QStringLiteral("glm-5.3-flash (flash)"));
    }

    // ----- a pane's picks in its saved layout node (card #MDL1) -------------------------------
    // A pane that was on "kimi-k3 · flash" must come back on it after a restart, not on rank 1 of
    // the flash list. The trip out and back is pure, so it can be stated here.
    void modePicksSurviveTheSavedLayout()
    {
        using relay::modelrows::ModePick;
        QHash<QString, ModePick> picks;
        picks.insert(QStringLiteral("flash"), ModePick{QStringLiteral("kimi-code|kimi-k3"), QStringLiteral("high")});
        picks.insert(QStringLiteral("high"), ModePick{QStringLiteral("glm-coding|glm-5.3"), QString()});
        // "main" is the pane's own model, saved as the node's `model`: never a pick.
        picks.insert(QStringLiteral("main"), ModePick{QStringLiteral("kimi-code|kimi-k3"), QString()});

        const QJsonObject saved = relay::modelrows::modePicksToJson(picks);
        QCOMPARE(saved.keys(), QStringList({QStringLiteral("flash"), QStringLiteral("high")}));
        QCOMPARE(saved.value(QStringLiteral("flash")).toObject(),
                 QJsonObject({{QStringLiteral("preset"), QStringLiteral("kimi-code")},
                              {QStringLiteral("model"), QStringLiteral("kimi-k3")},
                              {QStringLiteral("effort"), QStringLiteral("high")}}));
        // A pick with no level of its own carries none: the tier list answers for it.
        QVERIFY(!saved.value(QStringLiteral("high")).toObject().contains(QStringLiteral("effort")));

        const QHash<QString, ModePick> back = relay::modelrows::modePicksFromJson(saved);
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.value(QStringLiteral("flash")).key, QStringLiteral("kimi-code|kimi-k3"));
        QCOMPARE(back.value(QStringLiteral("flash")).effort, QStringLiteral("high"));
        QCOMPARE(back.value(QStringLiteral("high")).key, QStringLiteral("glm-coding|glm-5.3"));
        QVERIFY(back.value(QStringLiteral("high")).effort.isEmpty());
    }

    // Anything that is not that shape is dropped rather than guessed at: a layout file is written
    // by another version of Relay, or by hand.
    void modePicksFromJsonIsTotal()
    {
        using relay::modelrows::modePicksFromJson;
        QVERIFY(modePicksFromJson(QJsonValue()).isEmpty());
        QVERIFY(modePicksFromJson(QJsonValue(QStringLiteral("flash"))).isEmpty());
        QVERIFY(modePicksFromJson(QJsonArray{1, 2}).isEmpty());
        const QJsonObject mixed{
            {QStringLiteral("flash"), QJsonObject{{QStringLiteral("preset"), QStringLiteral("kimi-code")},
                                                  {QStringLiteral("model"), QStringLiteral("kimi-k3")}}},
            {QStringLiteral("high"), QJsonObject{{QStringLiteral("preset"), QStringLiteral("openai")}}},  // no model
            {QStringLiteral("lite"), QStringLiteral("glm-coding|glm-5.3")},                               // not an object
            {QStringLiteral("main"), QJsonObject{{QStringLiteral("preset"), QStringLiteral("kimi-code")},
                                                 {QStringLiteral("model"), QStringLiteral("kimi-k3")}}}};
        const QHash<QString, relay::modelrows::ModePick> read = modePicksFromJson(mixed);
        QCOMPARE(read.keys(), QStringList({QStringLiteral("flash")}));
    }

    // On restore, a pick whose entry has left the catalog or lost its key is dropped **silently**.
    // A spent subscription is not: it is a row the box greys and a reset brings back.
    void aRestoredPickThatCannotRunIsDropped()
    {
        using relay::modelrows::ModePick;
        Catalog catalog = catalogFrom(presets());
        QHash<QString, ModePick> picks;
        picks.insert(QStringLiteral("flash"), ModePick{QStringLiteral("kimi-code|kimi-k3"), QString()});
        picks.insert(QStringLiteral("high"), ModePick{QStringLiteral("anthropic|claude-opus-5"), QString()});
        picks.insert(QStringLiteral("local"), ModePick{QStringLiteral("local:gone|bonsai-2-27b"), QString()});
        const QHash<QString, ModePick> kept = relay::modelrows::usableModePicks(catalog, picks);
        QCOMPARE(kept.keys(), QStringList({QStringLiteral("flash")}));   // no key; not in the catalog

        // Exhausted stays: the pane comes back on it and the row is greyed until the reset.
        catalog.status.insert(QStringLiteral("kimi-code"), QStringLiteral("rejected"));
        QCOMPARE(relay::modelrows::usableModePicks(catalog, picks).keys(),
                 QStringList({QStringLiteral("flash")}));
    }

    // `/model <words>` resolves the same way in every composer.
    void resolveMatchesKeyModelLabelThenFilter()
    {
        const Catalog catalog = catalogFrom(presets());
        QCOMPARE(modelrows::resolve(catalog, QStringLiteral("glm-coding|glm-5.3-flash")),
                 QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(modelrows::resolve(catalog, QStringLiteral("kimi-k3")), QStringLiteral("kimi-code|kimi-k3"));
        QVERIFY(modelrows::resolve(catalog, QStringLiteral("nothing-like-this")).isEmpty());
        QVERIFY(modelrows::resolve(catalog, QString()).isEmpty());
        // A model taken out of step 2 is out of the lists and the box, not forbidden: typing its
        // name is asking for that model (card #MDL1, design 5.7).
        curation::setAvailable(QStringLiteral("glm-coding|glm-5.3-flash"), false, catalog);
        QCOMPARE(modelrows::resolve(catalog, QStringLiteral("glm-5.3-flash")),
                 QStringLiteral("glm-coding|glm-5.3-flash"));
        curation::resetAvailable();
    }

    // ----- the typed filter reaches every available model (owner, 2026-09-21; design 5.7) ------
    // "the text filter isnt working -- its supposed to show all available models, not just the
    // ones selected for the box picker."

    void theFilteredBoxOpensEveryClassAndAddsOtherModels()
    {
        // At rest main shows two of its three rows: the cutoff is step 4.
        const modelrows::Box rest = modelrows::box(paneContext());
        QVERIFY(!dataOf(rest.rows).contains(QStringLiteral("pick:main|openai|gpt-6-astra")));
        const modelrows::Box wide = modelrows::filtered(paneContext());
        // Every class whole: main's third row is there to be typed at.
        QVERIFY(dataOf(wide.rows).contains(QStringLiteral("pick:main|openai|gpt-6-astra")));
        // Every available model of this catalog is in one of the lists, so there is nothing left
        // for `other models` and the section is not drawn: an empty header is a promise of
        // nothing, here as in `box()`.
        QVERIFY2(modelrows::indexOf(wide.rows, QStringLiteral("class:") + modelrows::otherGroup()) < 0,
                 qPrintable(shapeOf(wide.rows).join(QLatin1Char('|'))));
        // The highlight still lands on this pane's own model, wherever the new rows pushed it.
        QCOMPARE(wide.rows.at(wide.current).data, QStringLiteral("pick:main|kimi-code|kimi-k3"));
    }

    // A model that is available (step 2) but in no list is what `other models` is for, and Enter
    // on it is a pick of this pane's own model: `pick:main|<key>`, main mode.
    void anAvailableModelInNoListComesUnderOtherModels()
    {
        // claude-opus-5 has no key: not usable, so not available, so never in the box.
        modelrows::Context context = paneContext();
        curation::addCustom(QStringLiteral("kimi-code"), QStringLiteral("kimi-k2-turbo"), context.catalog);
        context.catalog = catalogFrom(presets());
        const modelrows::Box wide = modelrows::filtered(context);
        const int head = modelrows::indexOf(wide.rows, QStringLiteral("class:") + modelrows::otherGroup());
        QVERIFY2(head > 0, qPrintable(shapeOf(wide.rows).join(QLatin1Char('|'))));
        QCOMPARE(wide.rows.at(head).text, QStringLiteral("other models"));
        QVERIFY(wide.rows.at(head).header);
        QVERIFY(!wide.rows.at(head).enabled);
        // The section sits with the models, above the action rows, never below them.
        QVERIFY(head < modelrows::indexOf(wide.rows, QStringLiteral("gear:picker")));
        const int at = modelrows::indexOf(wide.rows, QStringLiteral("pick:main|kimi-code|kimi-k2-turbo"));
        QVERIFY2(at > head, qPrintable(shapeOf(wide.rows).join(QLatin1Char('|'))));
        QCOMPARE(wide.rows.at(at).group, modelrows::otherGroup());
        QVERIFY(wide.rows.at(at).enabled);
        QCOMPARE(wide.rows.at(at).trailing, QStringLiteral("kimi"));
        // Right does not expand it: it is not a class, and it is already whole.
        QVERIFY(!modelrows::expandable(context, modelrows::otherGroup()));
        // Un-tick it and it leaves the filter too — step 2 is what the filter searches.
        curation::setAvailable(QStringLiteral("kimi-code|kimi-k2-turbo"), false, context.catalog);
        QVERIFY(modelrows::indexOf(modelrows::filtered(context).rows,
                                   QStringLiteral("pick:main|kimi-code|kimi-k2-turbo")) < 0);
        curation::resetAvailable();
        curation::removeCustom(QStringLiteral("kimi-code|kimi-k2-turbo"));
    }

    // With nothing typed the box is untouched: the filtered list is only ever asked for while
    // there is a query (relay::FilterPopup::onQueryRows).
    void theBoxAtRestIsUnchanged()
    {
        QCOMPARE(shapeOf(modelrows::box(paneContext()).rows),
                 QStringList({QStringLiteral("high"), QStringLiteral("  gpt-6-astra"),
                              QStringLiteral("main"), QStringLiteral("  kimi-k3"), QStringLiteral("  glm-5.3"),
                              QStringLiteral("flash"), QStringLiteral("  glm-5.3-flash"),
                              QStringLiteral("  more models…")}));
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ModelRowsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "modelrows_test.moc"
