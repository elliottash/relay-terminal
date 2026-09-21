// SPDX-License-Identifier: AGPL-3.0-or-later
// The one list every model box draws (#PK5Q, owner 2026-09-20: "can you have the picker be the
// same as in the main terminal"; #MDL1, owner 2026-09-21: modes first, then the models of the mode
// you are in). The point of the module is that a terminal pane's box and a console's box built
// from the same catalog are the *same rows*, so that is what is asserted here — plus what the
// modes say, what the collapsed chip says, and that a spent model keeps its row.
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
    curation::setTierList(QStringLiteral("main"), {entry(QStringLiteral("kimi-code|kimi-k3")),
                                                   entry(QStringLiteral("glm-coding|glm-5.3")),
                                                   entry(QStringLiteral("openrouter|z-ai/glm-5.3"))});
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

    // The shape the owner asked for: the modes first, each with the model *this pane* would run in
    // it in parentheses, then a separator, then the models of the mode the pane is in, then the two
    // action rows. The marker is on the mode the pane is in and nowhere else.
    void modesFirstThenTheModelsOfTheModeYouAreIn()
    {
        const modelrows::Page main = modelrows::page(paneContext(), QStringLiteral("main"));
        const QStringList data = dataOf(main.rows);
        QCOMPARE(data.mid(0, 3), QStringList({QStringLiteral("role:high"), QStringLiteral("role:main"),
                                              QStringLiteral("role:flash")}));
        QCOMPARE(data.at(data.size() - 2), QStringLiteral("gear:picker"));
        QCOMPARE(data.last(), QStringLiteral("gear:modelOptions"));
        // "high (gpt-6-astra)": rank 1 of the high list, because this pane has picked nothing there.
        QCOMPARE(main.rows.at(0).text, QStringLiteral("  high (gpt-6-astra)"));
        // The pane's own pick, with the marker: the mode it is in.
        QCOMPARE(main.rows.at(1).text, QString(QChar(0x2022)) + QStringLiteral(" main (kimi-k3)"));
        QCOMPARE(main.rows.at(2).text, QStringLiteral("  flash (glm-5.3-flash)"));
        // A separator opens the model list.
        QVERIFY(main.rows.at(3).separatorBefore);
    }

    // The models are the mode's list, in **list order** — not alphabetical (Claude proposed list
    // order and the owner took it): the order is the information, rank 1 is the default.
    void theModelsAreTheModesListInListOrder()
    {
        const modelrows::Context context = paneContext();
        const QStringList mainModels = textsOf(modelrows::page(context, QStringLiteral("main")).rows).mid(3, 2);
        QCOMPARE(mainModels.at(0), QStringLiteral("kimi-k3\tkimi"));
        // glm-5.3 is served by z.ai and by OpenRouter: one row, and it says both (rule 2).
        QCOMPARE(mainModels.at(1), QStringLiteral("glm-5.3\tz.ai (glm) +1"));
        QVERIFY(!mainModels.at(0).startsWith(QStringLiteral("glm")));   // list order, not alphabetical

        // The flash page is the flash list, and the high page the high list.
        QCOMPARE(dataOf(modelrows::page(context, QStringLiteral("flash")).rows).at(3),
                 QStringLiteral("pick:flash|glm-coding|glm-5.3-flash"));
        QCOMPARE(dataOf(modelrows::page(context, QStringLiteral("high")).rows).at(3),
                 QStringLiteral("pick:high|openai|gpt-6-astra"));
    }

    // Every page carries the same mode rows, so Left and Right only move the marker and the list
    // below it; and the page the pane is on highlights the pane's own model.
    void everyPageCarriesTheModesAndHighlightsThePanesModel()
    {
        const QList<modelrows::Page> pages = modelrows::pages(paneContext());
        QCOMPARE(pages.size(), 3);
        QCOMPARE(pages.at(0).mode, QStringLiteral("high"));
        for (const modelrows::Page &page : pages) {
            QCOMPARE(dataOf(page.rows).mid(0, 3), QStringList({QStringLiteral("role:high"),
                                                               QStringLiteral("role:main"),
                                                               QStringLiteral("role:flash")}));
            // The marker says which mode the pane is in, whatever page is being shown.
            QVERIFY(page.rows.at(1).text.startsWith(QChar(0x2022)));
        }
        // The main page opens on the pane's own model; the flash page on rank 1 of the flash list.
        const modelrows::Page main = pages.at(1);
        QCOMPARE(main.rows.at(main.current).data, QStringLiteral("pick:main|kimi-code|kimi-k3"));
        const modelrows::Page flash = pages.at(2);
        QCOMPARE(flash.rows.at(flash.current).data, QStringLiteral("pick:flash|glm-coding|glm-5.3-flash"));
    }

    // A mode row's parentheses name what *this pane* would run: its own pick where it has one,
    // rank 1 of the list where it has not.
    void theParenthesesNameWhatThisPaneWouldRun()
    {
        modelrows::Context context = paneContext();
        QCOMPARE(modelrows::modeModel(context, QStringLiteral("flash")), QStringLiteral("glm-5.3-flash"));
        context.modePick.insert(QStringLiteral("flash"), QStringLiteral("kimi-code|kimi-k3"));
        QCOMPARE(modelrows::modeModel(context, QStringLiteral("flash")), QStringLiteral("kimi-k3"));
        QCOMPARE(modelrows::page(context, QStringLiteral("main")).rows.at(2).text,
                 QStringLiteral("  flash (kimi-k3)"));
        // Nothing in the catalog and no pick: the worker's role summary answers, named by the one
        // rule — which is the ordinary case for `high` with no high list.
        modelrows::Context bare;
        bare.roleModel.insert(QStringLiteral("high"), QStringLiteral("openai/gpt-6-astra"));
        bare.now = 1;
        QCOMPARE(modelrows::modeModel(bare, QStringLiteral("high")), QStringLiteral("gpt-6-astra"));
    }

    // Owner, 2026-09-21: the Switchboard agent's box said "kimi-k3 (switchboard)"; "(main)" was not
    // wanted either. A console's box is the terminal pane's box, row for row — only the worker role
    // behind the main mode differs, and that is not something the box says.
    void aConsoleBuildsTheSameRowsAsAPaneOnMain()
    {
        const QList<modelrows::Row> pane = modelrows::build(paneContext());
        const QList<modelrows::Row> console = modelrows::build(consoleContext());
        QCOMPARE(textsOf(console), textsOf(pane));
        for (const modelrows::Row &row : console) QVERIFY(!row.text.contains(QStringLiteral("switchboard")));
        // The one difference is where a pick on the main mode row goes: the console's own role.
        QCOMPARE(dataOf(console).at(1), QStringLiteral("role:switchboard"));
        QCOMPARE(dataOf(pane).at(1), QStringLiteral("role:main"));
    }

    // The collapsed chip: the model alone on main, "<model> · <mode>" anywhere else (design 5.1).
    void theCollapsedChipSaysTheModeUnlessItIsMain()
    {
        modelrows::Context context = paneContext();
        QCOMPARE(modelrows::collapsedText(context), QStringLiteral("kimi-k3"));
        context.mode = QStringLiteral("flash");
        QCOMPARE(modelrows::collapsedText(context), QStringLiteral("glm-5.3-flash · flash"));
        context.mode = QStringLiteral("high");
        QCOMPARE(modelrows::collapsedText(context), QStringLiteral("gpt-6-astra · high"));
    }

    // A spent subscription is greyed **in place**, with the reason, never dropped (design 1.3):
    // the list the user ranked must not quietly lose a row because a plan ran out today.
    void aSpentModelKeepsItsRowGreyed()
    {
        modelrows::Context context = paneContext();
        context.catalog.status.insert(QStringLiteral("kimi-code"), QStringLiteral("rejected"));
        const modelrows::Page main = modelrows::page(context, QStringLiteral("main"));
        const int at = modelrows::indexOf(main.rows, QStringLiteral("pick:main|kimi-code|kimi-k3"));
        QVERIFY2(at > 0, "the spent row was dropped instead of greyed");
        QVERIFY(!main.rows.at(at).enabled);
        QVERIFY(main.rows.at(at).tooltip.contains(QStringLiteral("spent")));
        // glm-5.3 has two providers and only needs one of them, so it stays live.
        const int glm = modelrows::indexOf(main.rows, QStringLiteral("pick:main|glm-coding|glm-5.3"));
        QVERIFY(main.rows.at(glm).enabled);
        // And a model whose only provider has no key is greyed with that reason rather than hidden.
        const modelrows::Page high = modelrows::page(context, QStringLiteral("high"));
        const int opus = modelrows::indexOf(high.rows, QStringLiteral("pick:high|anthropic|claude-opus-5"));
        QVERIFY2(opus > 0, "the keyless row was dropped instead of greyed");
        QVERIFY(!high.rows.at(opus).enabled);
        QVERIFY(high.rows.at(opus).tooltip.contains(QStringLiteral("no stored key")));
        QVERIFY(high.rows.at(modelrows::indexOf(high.rows, QStringLiteral("pick:high|openai|gpt-6-astra"))).enabled);
    }

    // A Local mode only where this machine serves one, and guest rows only on the main page.
    void localModeAndGuestRows()
    {
        modelrows::Context context = paneContext();
        context.modes << QStringLiteral("local");
        context.roleModel.insert(QStringLiteral("local"), QStringLiteral("bonsai-2-27b"));
        context.roleNote.insert(QStringLiteral("local"), QStringLiteral("no local model: using Main"));
        context.guests << QStringLiteral("claude");
        context.guestText.insert(QStringLiteral("claude"), QStringLiteral("Claude Code"));
        const modelrows::Page main = modelrows::page(context, QStringLiteral("main"));
        // Nothing is ranked on the local list here, so the parentheses are what the *worker*
        // resolved the role to — never a cloud model guessed out of the catalog.
        QCOMPARE(main.rows.at(3).text, QStringLiteral("  local (bonsai-2-27b)"));
        QCOMPARE(main.rows.at(3).tooltip, QStringLiteral("no local model: using Main"));
        // And the local page offers no cloud model either ("local" is a promise about where the
        // text goes, design 3.2): it is the mode rows and the two action rows, nothing between.
        for (const modelrows::Row &row : modelrows::page(context, QStringLiteral("local")).rows)
            QVERIFY2(!row.data.startsWith(QStringLiteral("pick:")), qPrintable(row.data));
        QVERIFY(modelrows::indexOf(main.rows, QStringLiteral("guest:claude")) > 0);
        // Not on the flash page: a guest is the pane's own agent, not a tier.
        QCOMPARE(modelrows::indexOf(modelrows::page(context, QStringLiteral("flash")).rows,
                                    QStringLiteral("guest:claude")), -1);
        // A console passes none (#GH5T).
        QCOMPARE(modelrows::indexOf(modelrows::build(consoleContext()), QStringLiteral("guest:claude")), -1);
    }

    // Into a QComboBox: the same rows, the via column on its own role, a greyed row switched off,
    // and the current row selected.
    void fillCarriesEverythingIntoTheBox()
    {
        modelrows::Context context = paneContext();
        context.catalog.status.insert(QStringLiteral("kimi-code"), QStringLiteral("rejected"));
        QComboBox box;
        const int index = modelrows::fill(&box, context);
        QVERIFY(index > 0);
        const int kimi = box.findData(QStringLiteral("pick:main|kimi-code|kimi-k3"));
        QVERIFY(kimi > 0);
        QCOMPARE(box.itemData(kimi, relay::kTrailingItemRole).toString(), QStringLiteral("kimi"));
        QVERIFY(!box.model()->index(kimi, 0).flags().testFlag(Qt::ItemIsEnabled));
        QVERIFY(box.model()->index(box.findData(QStringLiteral("role:main")), 0)
                    .flags().testFlag(Qt::ItemIsEnabled));
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
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ModelRowsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "modelrows_test.moc"
