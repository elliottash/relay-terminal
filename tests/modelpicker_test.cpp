// SPDX-License-Identifier: AGPL-3.0-or-later
// The models widget (card #MDL1 t:a7, re-hosted as a plain widget at t:a11): the tier tabs that
// *are* the priority lists — numbered, reordered, added to, taken from, levelled and undone — the
// flat `all` tab with one row per model and its "via" column, the profile in the header, and what
// a pick hands back. It was a modal dialog on Ctrl+Alt+M until 2026-09-21; `use()` is what
// `accept()` was, and the modal's own behaviour (exec, reject, "cancel", "customize…" closing the
// dialog) is gone with it. The pane that hosts it is tests/modelspane_test.cpp.
#include "ModelPicker.h"

#include <QCheckBox>
#include <QApplication>
#include <QDateTime>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

using namespace relay;
using namespace relay::models;

namespace {

// The dialog's columns, as ModelPicker.cpp orders them.
// ColMove is a listed row's ▲▼ buttons (card #RKP3). ColBox is the "show in box" cutoff a tier
// tab gained with card #MDL1, design 5.3; ColAvail is the `all` tab's availability tick, step 2
// of the four (design 5.7).
enum Column { ColRank, ColMove, ColAvail, ColBox, ColModel, ColVia, ColReasoning, ColIntelligence, ColSpeed, ColLeft };

QJsonObject model(const QString &id, const QString &label, const QString &tier, const QStringList &efforts, int intelligence = -1) {
    QJsonObject row{{QStringLiteral("id"), id}, {QStringLiteral("label"), label}, {QStringLiteral("tier"), tier},
                    {QStringLiteral("efforts"), QJsonArray::fromStringList(efforts)}};
    if (intelligence >= 0) row.insert(QStringLiteral("intelligence"), intelligence);
    return row;
}

QJsonArray presets() {
    QJsonArray out;
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("glm-coding")}, {QStringLiteral("label"), QStringLiteral("z.ai · glm-5.3 · coding plan")},
                       {QStringLiteral("provider"), QStringLiteral("z.ai (glm)")}, {QStringLiteral("plan"), QStringLiteral("coding plan")},
                       {QStringLiteral("model"), QStringLiteral("glm-5.3")}, {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("glm-5.3"), QStringLiteral("glm-5.3"), QStringLiteral("main"), {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 45),
                                                             model(QStringLiteral("glm-5.3-flash"), QStringLiteral("glm-5.3 flash"), QStringLiteral("flash"), {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 30)}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("anthropic")}, {QStringLiteral("label"), QStringLiteral("anthropic · claude opus 5.5")},
                       {QStringLiteral("provider"), QStringLiteral("anthropic (claude)")}, {QStringLiteral("plan"), QStringLiteral("pay-as-you-go")},
                       {QStringLiteral("model"), QStringLiteral("claude-opus-5-5")}, {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("claude-opus-5-5"), QStringLiteral("claude opus 5.5"), QStringLiteral("main"), {}, 51)}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("guest:claude")}, {QStringLiteral("label"), QStringLiteral("Claude Code")},
                       {QStringLiteral("provider"), QStringLiteral("Claude Code")}, {QStringLiteral("model"), QString()},
                       {QStringLiteral("harness"), true},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("opus"), QStringLiteral("opus"), QString(), {QStringLiteral("low"), QStringLiteral("high")})}},
                       {QStringLiteral("limits"), QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("5h")}, {QStringLiteral("used_percent"), 38.0}, {QStringLiteral("resets_at"), 0}}}}};
    // A provider with no key: its rows never reach `shown()`, but a list may still name one — and
    // then the dialog has to say why that rank does nothing rather than drop it.
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("gemini")}, {QStringLiteral("label"), QStringLiteral("google · gemini")},
                       {QStringLiteral("provider"), QStringLiteral("google (gemini)")},
                       {QStringLiteral("model"), QStringLiteral("gemini-3.8-flash")},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("gemini-3.8-flash"), QStringLiteral("gemini 3.8 flash"), QStringLiteral("flash"), {})}}};
    return out;
}

// The same catalog plus gpt-6-sol from three providers, which is the design's own example of one
// model with several ways in (rule 2): codex the harness, the OpenAI API, and OpenRouter's slug.
QJsonArray solPresets() {
    QJsonArray out = presets();
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("guest:codex")}, {QStringLiteral("label"), QStringLiteral("Codex")},
                       {QStringLiteral("provider"), QStringLiteral("codex")}, {QStringLiteral("model"), QString()},
                       {QStringLiteral("harness"), true},
                       // Codex's own words (card #MDL1, 2026-09-21): it says xhigh where the API row says max.
                       {QStringLiteral("models"), QJsonArray{QJsonObject{
                           {QStringLiteral("id"), QStringLiteral("gpt-6-sol")},
                           {QStringLiteral("efforts"), QJsonArray{QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("xhigh")}}}}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("openai")}, {QStringLiteral("label"), QStringLiteral("openai")},
                       {QStringLiteral("provider"), QStringLiteral("openai")}, {QStringLiteral("plan"), QStringLiteral("pay-as-you-go")},
                       {QStringLiteral("model"), QStringLiteral("gpt-6-sol")}, {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("gpt-6-sol"), QStringLiteral("gpt-6-sol"), QStringLiteral("main"), {QStringLiteral("low"), QStringLiteral("high")}, 60)}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("openrouter")}, {QStringLiteral("label"), QStringLiteral("openrouter")},
                       {QStringLiteral("provider"), QStringLiteral("openrouter")}, {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("model"), QStringLiteral("openai/gpt-6-sol")},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("openai/gpt-6-sol"), QStringLiteral("openai: gpt-6 sol"), QString(), {QStringLiteral("low"), QStringLiteral("high")}, 60)}}};
    return out;
}

// The same catalog plus an open-ended provider: OpenRouter with a live listing of ten, which is
// what `shown()` holds back until something is typed (card #MDL1 t:a10, design 5.5).
QJsonArray tailPresets() {
    QJsonArray out = presets();
    QJsonArray rows;
    rows << model(QStringLiteral("meta/muse-spark-1.3"), QStringLiteral("meta: muse spark 1.3"), QString(), {});
    for (int i = 0; i < 9; ++i)
        rows << model(QStringLiteral("vendor/tail-%1").arg(i), QStringLiteral("vendor: tail %1").arg(i), QString(), {});
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("openrouter")}, {QStringLiteral("label"), QStringLiteral("openrouter")},
                       {QStringLiteral("provider"), QStringLiteral("openrouter")}, {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), rows}};
    return out;
}

// The levels-per-model catalog (card #MDL1, owner 2026-09-21): codex with its own six, the OpenAI
// API row with four, Relay Free with two it does not let anyone choose between, and the anthropic
// row of `presets()` with none at all.
QJsonArray codexPresets() {
    QJsonArray out = presets();
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("guest:codex")}, {QStringLiteral("label"), QStringLiteral("Codex")},
                       {QStringLiteral("provider"), QStringLiteral("codex")}, {QStringLiteral("model"), QString()},
                       {QStringLiteral("harness"), true},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("gpt-6-sol"), QStringLiteral("gpt-6-sol"), QString(),
                                                                   {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"),
                                                                    QStringLiteral("xhigh"), QStringLiteral("max"), QStringLiteral("ultra")})}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("openai")}, {QStringLiteral("label"), QStringLiteral("openai")},
                       {QStringLiteral("provider"), QStringLiteral("openai")}, {QStringLiteral("plan"), QStringLiteral("pay-as-you-go")},
                       {QStringLiteral("model"), QStringLiteral("gpt-6-astra")}, {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("gpt-6-astra"), QStringLiteral("gpt-6-astra"), QStringLiteral("main"),
                                                                   {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("max")}, 53)}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("relay-free")}, {QStringLiteral("label"), QStringLiteral("relay free")},
                       {QStringLiteral("provider"), QStringLiteral("relay free")}, {QStringLiteral("hosted"), true},
                       {QStringLiteral("available"), true}, {QStringLiteral("model"), QStringLiteral("relay-main")},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("relay-main"), QStringLiteral("relay-main"), QStringLiteral("main"),
                                                                   {QStringLiteral("low"), QStringLiteral("medium")})}}};
    return out;
}

ModelPicker::Context context(const QString &currentKey = QStringLiteral("glm-coding|glm-5.3"), const QString &effort = QStringLiteral("high")) {
    ModelPicker::Context ctx;
    ctx.catalog = catalogFrom(presets());
    ctx.currentKey = currentKey;
    ctx.currentEffort = effort;
    ctx.now = 1;
    return ctx;
}

ModelPicker::Context tailContext() {
    ModelPicker::Context ctx;
    ctx.catalog = catalogFrom(tailPresets());
    ctx.currentKey = QStringLiteral("glm-coding|glm-5.3");
    ctx.currentEffort = QStringLiteral("high");
    ctx.tier = QStringLiteral("all");
    ctx.now = 1;
    return ctx;
}

ModelPicker::Context solContext(const QString &currentKey = QString()) {
    ModelPicker::Context ctx;
    ctx.catalog = catalogFrom(solPresets());
    ctx.currentKey = currentKey;
    ctx.currentEffort = QStringLiteral("high");
    ctx.tier = QStringLiteral("all");
    ctx.now = 1;
    return ctx;
}

QStringList keys(QTreeWidget *list) {
    QStringList out;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list->topLevelItem(i);
        if (item->isHidden()) continue;
        const QString key = item->data(0, Qt::UserRole).toString();
        out << (key.isEmpty() ? QStringLiteral("[%1]").arg(item->text(0)) : key);
    }
    return out;
}

// The rows a tier tab draws, without the trailing "type a model's name…" rule.
QStringList rowKeys(QTreeWidget *list) {
    QStringList out;
    for (const QString &key : keys(list)) if (!key.startsWith(QLatin1Char('['))) out << key;
    return out;
}

// The sectioned priorities page as a list of lines: `#<class>` for a class's own header row,
// `[text]` for a rule, and the key for a model row.
QStringList pageRows(QTreeWidget *list) {
    QStringList out;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list->topLevelItem(i);
        if (item->data(0, Qt::UserRole + 9).toBool()) { out << QStringLiteral("#") + item->data(0, Qt::UserRole + 8).toString(); continue; }
        const QString key = item->data(0, Qt::UserRole).toString();
        out << (key.isEmpty() ? QStringLiteral("[%1]").arg(item->text(0)) : key);
    }
    return out;
}

QTreeWidgetItem *classHeader(QTreeWidget *list, const QString &tier) {
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list->topLevelItem(i);
        if (item->data(0, Qt::UserRole + 9).toBool() && item->data(0, Qt::UserRole + 8).toString() == tier) return item;
    }
    return nullptr;
}

QTreeWidgetItem *rowFor(QTreeWidget *list, const QString &key, const QString &tier = QString()) {
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() != key) continue;
        if (!tier.isEmpty() && item->data(0, Qt::UserRole + 8).toString() != tier) continue;
        return item;
    }
    return nullptr;
}

QStringList tabs(QTabBar *bar) {
    QStringList out;
    for (int i = 0; i < bar->count(); ++i) out << bar->tabData(i).toString();
    return out;
}

QStringList listKeys(const QString &tier) {
    QStringList out;
    for (const curation::TierEntry &entry : curation::tierList(tier)) out << entry.key;
    return out;
}

void setList(const QString &tier, const QList<curation::TierEntry> &entries) { curation::setTierList(tier, entries); }

}  // namespace

class ModelPickerTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void init() {
        QSettings().clear();
        // The order the rows come in on the flat tab, stated rather than assumed. It used to lean
        // on `provider/preset` — the last provider some pane switched to, which `ranked()` stopped
        // reading with card #MDL1 — and the catalog's own default order leads with each provider's
        // main model, so glm's flash row would no longer sit second.
        QSettings().setValue(QStringLiteral("models/priority"),
                             QStringList{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("glm-coding|glm-5.3-flash"),
                                         QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("guest:claude|opus")});
    }

    // ----- the tabs -----------------------------------------------------------------------------

    void theTabsAreTheListsAndItOpensOnThePanesOwn() {
        setList(QStringLiteral("flash"), {{QStringLiteral("glm-coding|glm-5.3-flash"), QStringLiteral("low")}});
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("flash");   // the pane is on /flash
        ModelPicker picker(ctx);
        // high leads (design 5.2), then the page's order; `local` is left out because this machine
        // serves nothing local and the list is empty; `all` is last.
        QCOMPARE(tabs(picker.tabBar()), (QStringList{QStringLiteral("high"), QStringLiteral("main"), QStringLiteral("flash"),
                                                     QStringLiteral("lite"), QStringLiteral("all")}));
        QCOMPARE(picker.tier(), QStringLiteral("flash"));
        QCOMPARE(picker.tabBar()->tabData(picker.tabBar()->currentIndex()).toString(), QStringLiteral("flash"));
        QCOMPARE(rowKeys(picker.list()), QStringList{QStringLiteral("glm-coding|glm-5.3-flash")});
        // A tier the catalog and the lists both know nothing about falls back to main.
        ctx.tier = QStringLiteral("local");
        ModelPicker fallback(ctx);
        QCOMPARE(fallback.tier(), QStringLiteral("main"));
    }

    void aLocalTabAppearsOnlyWhereSomethingIsServedLocally() {
        ModelPicker::Context ctx = context();
        {
            ModelPicker none(ctx);
            QVERIFY(!tabs(none.tabBar()).contains(QStringLiteral("local")));
        }
        // A list that names one is enough: the models may be off right now, the ranking is not.
        setList(QStringLiteral("local"), {{QStringLiteral("local:spark|bonsai-2-27b"), QString()}});
        ModelPicker served(ctx);
        QVERIFY(tabs(served.tabBar()).contains(QStringLiteral("local")));
    }

    void leftAndRightWalkTheTabsAndTheCaretKeepsTheArrowsWhileThereIsText() {
        ModelPicker picker(context());
        QCOMPARE(picker.tier(), QStringLiteral("main"));
        QTest::keyClick(picker.filter(), Qt::Key_Right);
        QCOMPARE(picker.tier(), QStringLiteral("flash"));
        QTest::keyClick(picker.filter(), Qt::Key_Left);
        QCOMPARE(picker.tier(), QStringLiteral("main"));
        QTest::keyClick(picker.filter(), Qt::Key_Left);
        QCOMPARE(picker.tier(), QStringLiteral("high"));
        QTest::keyClick(picker.filter(), Qt::Key_Left);          // wraps rather than dead-ending
        QCOMPARE(picker.tier(), QStringLiteral("all"));
        // With text typed, ← and → are the caret's again — until it reaches that end.
        picker.filter()->setText(QStringLiteral("glm"));
        picker.filter()->setCursorPosition(1);
        QTest::keyClick(picker.filter(), Qt::Key_Right);
        QCOMPARE(picker.tier(), QStringLiteral("all"));
        QCOMPARE(picker.filter()->cursorPosition(), 2);
        QTest::keyClick(picker.filter(), Qt::Key_Right);
        QCOMPARE(picker.filter()->cursorPosition(), 3);
        QTest::keyClick(picker.filter(), Qt::Key_Right);         // at the end: the tab moves
        QCOMPARE(picker.tier(), QStringLiteral("high"));
        QVERIFY(picker.filter()->text().isEmpty());              // and the filter goes with the tab
        // Ctrl+Tab does it wherever the focus is, including inside the list.
        QTest::keyClick(picker.list(), Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(picker.tier(), QStringLiteral("main"));
        QTest::keyClick(picker.levelList(), Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(picker.tier(), QStringLiteral("high"));
    }

    // ----- a tier tab is the list -----------------------------------------------------------------

    void aTierTabIsTheListNumberedInOrderAndRankOneSaysWhatItIs() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max")},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()},
                                         {QStringLiteral("guest:claude|opus"), QStringLiteral("high")}});
        ModelPicker picker(context());
        QCOMPARE(rowKeys(picker.list()), (QStringList{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("anthropic|claude-opus-5-5"),
                                                      QStringLiteral("guest:claude|opus")}));
        QTreeWidgetItem *first = picker.list()->topLevelItem(0);
        QCOMPARE(first->text(ColRank), QStringLiteral("1"));
        QCOMPARE(picker.list()->topLevelItem(2)->text(ColRank), QStringLiteral("3"));
        QVERIFY(first->text(ColModel).startsWith(QStringLiteral("glm-5.3")));
        QVERIFY(first->text(ColModel).contains(QStringLiteral("new panes start here")));
        QVERIFY(!picker.list()->topLevelItem(1)->text(ColModel).contains(QStringLiteral("new panes start here")));
        // One row per list entry, the model named once and the provider in "via" (rule 2).
        QCOMPARE(first->text(ColVia), QStringLiteral("z.ai (glm) · coding plan"));
        QCOMPARE(picker.list()->topLevelItem(2)->text(ColVia), QStringLiteral("claude code"));
        // The reasoning column is the level the entry carries *in this list*.
        QCOMPARE(first->text(ColReasoning), QStringLiteral("max"));
        QCOMPARE(picker.list()->topLevelItem(1)->text(ColReasoning), QString());       // no knob at all
        QCOMPARE(picker.list()->topLevelItem(2)->text(ColReasoning), QStringLiteral("high"));
        // …and the pane's own model is marked, wherever it sits.
        QVERIFY(first->text(ColModel).contains(QStringLiteral("current")));
        // Only main says "new panes start here": rank 1 of flash is what /flash runs on, not a default.
        setList(QStringLiteral("flash"), {{QStringLiteral("glm-coding|glm-5.3-flash"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("flash");
        ModelPicker flash(ctx);
        QVERIFY(!flash.list()->topLevelItem(0)->text(ColModel).contains(QStringLiteral("new panes start here")));
        QCOMPARE(flash.list()->topLevelItem(0)->text(ColReasoning), QStringLiteral("default"));   // no level of its own
    }

    void anUnusableOrExhaustedRankIsGreyedInPlaceWithTheReason() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("gemini|gemini-3.8-flash"), QString()},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()},
                                         {QStringLiteral("kimi|kimi-k3"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.now = QDateTime(QDate(2026, 9, 20), QTime(9, 0)).toSecsSinceEpoch();
        ctx.catalog.limits[QStringLiteral("anthropic")] =
            {LimitWindow{QStringLiteral("5h"), 100, QDateTime(QDate(2026, 9, 20), QTime(14, 30)).toSecsSinceEpoch()}};
        ModelPicker picker(ctx);
        // Every rank is still there, in its place: a list is an order the user wrote down.
        QCOMPARE(rowKeys(picker.list()).size(), 4);
        const QColor muted = picker.palette().color(QPalette::Disabled, QPalette::Text);
        QTreeWidgetItem *noKey = picker.list()->topLevelItem(1);
        QCOMPARE(noKey->text(ColRank), QStringLiteral("2"));
        QCOMPARE(noKey->text(ColLeft), QStringLiteral("no key"));
        QCOMPARE(noKey->foreground(ColModel).color(), muted);
        QVERIFY(noKey->toolTip(ColModel).contains(QStringLiteral("No key for google (gemini)")));
        QTreeWidgetItem *spent = picker.list()->topLevelItem(2);
        QCOMPARE(spent->text(ColLeft), QStringLiteral("0% · resets 14:30"));
        QCOMPARE(spent->foreground(ColLeft).color(), muted);
        QTreeWidgetItem *gone = picker.list()->topLevelItem(3);         // no such entry in the catalog
        QCOMPARE(gone->text(ColModel), QStringLiteral("kimi|kimi-k3"));
        QCOMPARE(gone->text(ColLeft), QStringLiteral("unavailable"));
        QCOMPARE(gone->foreground(ColModel).color(), muted);
        QTreeWidgetItem *live = picker.list()->topLevelItem(0);
        QVERIFY(live->foreground(ColModel).style() == Qt::NoBrush);
        // …and the user may still insist on the spent one.
        picker.selectKey(QStringLiteral("anthropic|claude-opus-5-5"));
        picker.use();
        QVERIFY(picker.pick().accepted);
        QCOMPARE(picker.pick().key, QStringLiteral("anthropic|claude-opus-5-5"));
    }

    // ----- prioritizing -----------------------------------------------------------------------------

    void altDownMovesARowAndTheListRemembers() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max")},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()},
                                         {QStringLiteral("guest:claude|opus"), QString()}});
        ModelPicker picker(context());
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3"));
        QTest::keyClick(picker.filter(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("glm-coding|glm-5.3"),
                                                                QStringLiteral("guest:claude|opus")}));
        QCOMPARE(told, 1);
        // The row moved with it, keeps its level, and is still the selected one.
        QCOMPARE(picker.list()->topLevelItem(1)->text(ColRank), QStringLiteral("2"));
        QCOMPARE(picker.list()->topLevelItem(1)->text(ColReasoning), QStringLiteral("max"));
        QCOMPARE(picker.selectedKey(), QStringLiteral("glm-coding|glm-5.3"));
        // "new panes start here" moved to the new rank 1.
        QVERIFY(picker.list()->topLevelItem(0)->text(ColModel).contains(QStringLiteral("new panes start here")));
        QTest::keyClick(picker.list(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(listKeys(QStringLiteral("main")).first(), QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(told, 2);
        // Off the end, nothing happens and nothing is written.
        QTest::keyClick(picker.list(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(told, 2);
    }

    void deleteTakesARowOutAndCtrlZPutsItBack() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max")},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        ModelPicker picker(context());
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        picker.selectKey(QStringLiteral("anthropic|claude-opus-5-5"));
        QTest::keyClick(picker.filter(), Qt::Key_Delete);
        QCOMPARE(listKeys(QStringLiteral("main")), QStringList{QStringLiteral("glm-coding|glm-5.3")});
        QCOMPARE(rowKeys(picker.list()), QStringList{QStringLiteral("glm-coding|glm-5.3")});
        QCOMPARE(told, 1);
        QTest::keyClick(picker.filter(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("anthropic|claude-opus-5-5")}));
        QCOMPARE(rowKeys(picker.list()).size(), 2);
        QCOMPARE(picker.list()->topLevelItem(0)->text(ColReasoning), QStringLiteral("max"));   // the level came back too
        QCOMPARE(told, 2);
        QCOMPARE(picker.undoDepth(), 0);
        QTest::keyClick(picker.filter(), Qt::Key_Z, Qt::ControlModifier);   // an empty stack is a no-op
        QCOMPARE(listKeys(QStringLiteral("main")).size(), 2);
        // Backspace does it only while the filter is empty, so it stays the key that rubs it out.
        picker.filter()->setText(QStringLiteral("claude"));
        QTest::keyClick(picker.filter(), Qt::Key_Backspace);
        QCOMPARE(listKeys(QStringLiteral("main")).size(), 2);
        QCOMPARE(picker.filter()->text(), QStringLiteral("claud"));
    }

    void typingFindsAModelThatIsNotInThisListAndCtrlEnterAddsIt() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        ModelPicker picker(context());
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        picker.filter()->setText(QStringLiteral("opus"));
        // The list's own matches first, then the rule, then the rest of the catalog, folded.
        QCOMPARE(keys(picker.list()), (QStringList{QStringLiteral("[not in this list]"), QStringLiteral("anthropic|claude-opus-5-5"),
                                                   QStringLiteral("guest:claude|opus")}));
        QVERIFY(picker.list()->topLevelItem(1)->text(ColRank).contains(QStringLiteral("+ add")));
        picker.selectKey(QStringLiteral("guest:claude|opus"));
        QTest::keyClick(picker.filter(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("guest:claude|opus")}));
        QCOMPARE(told, 1);
        QVERIFY(picker.filter()->text().isEmpty());
        QCOMPARE(rowKeys(picker.list()), (QStringList{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("guest:claude|opus")}));
        QCOMPARE(picker.selectedKey(), QStringLiteral("guest:claude|opus"));
        // Ctrl+Z takes the addition back.
        QTest::keyClick(picker.filter(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(listKeys(QStringLiteral("main")), QStringList{QStringLiteral("glm-coding|glm-5.3")});
        // A guest is a whole agent, not a side call: it is not offered to flash or lite, the same
        // rule Options › Models' "+ add a model…" applies.
        picker.setTier(QStringLiteral("lite"));
        picker.filter()->setText(QStringLiteral("opus"));
        QCOMPARE(rowKeys(picker.list()), QStringList{QStringLiteral("anthropic|claude-opus-5-5")});
        // Nothing at all matches: the rule says so instead of an empty box.
        picker.filter()->setText(QStringLiteral("zzz"));
        QCOMPARE(keys(picker.list()), QStringList{QStringLiteral("[no model matches “zzz”]")});
    }

    void theLevelOnAListedRowIsWrittenIntoTheList() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("low")}});
        ModelPicker picker(context());
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3"));
        // "default" heads the level list on a listed row — it is a state the list can store.
        QCOMPARE(picker.levelList()->count(), 4);
        QCOMPARE(picker.levelList()->item(0)->text(), QStringLiteral("default"));
        QCOMPARE(picker.selectedEffort(), QStringLiteral("low"));
        picker.levelList()->setCurrentRow(3);                       // max
        QCOMPARE(picker.selectedEffort(), QStringLiteral("max"));
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().effort, QStringLiteral("max"));
        QCOMPARE(picker.list()->topLevelItem(0)->text(ColReasoning), QStringLiteral("max"));
        QCOMPARE(told, 1);
        picker.levelList()->setCurrentRow(0);                       // back to the model's own default
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().effort, QString());
        QCOMPARE(picker.list()->topLevelItem(0)->text(ColReasoning), QStringLiteral("default"));
        // …and a pane still needs a level to run at, so "default" picks the one the row shows.
        picker.use();
        QCOMPARE(picker.pick().key, QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(picker.pick().effort, QStringLiteral("high"));     // the pane's own, the list naming none
        QCOMPARE(told, 2);
        QTest::keyClick(picker.filter(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().effort, QStringLiteral("max"));
    }

    void aDragRewritesTheListInTheOrderTheRowsNowRead() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("x")},
                                         {QStringLiteral("guest:claude|opus"), QStringLiteral("high")}});
        ModelPicker picker(context());
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        // The drop itself is the window manager's; what the dialog owns is reading the rows back.
        QTreeWidgetItem *moved = picker.list()->takeTopLevelItem(0);
        picker.list()->insertTopLevelItem(2, moved);
        picker.commitDragOrder();
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("guest:claude|opus"),
                                                                QStringLiteral("glm-coding|glm-5.3")}));
        QCOMPARE(curation::tierList(QStringLiteral("main")).at(1).effort, QStringLiteral("high"));   // levels travel with the rows
        QCOMPARE(told, 1);
    }

    // Card #RKP3: the ▲▼ of a listed row are the mouse's alt+↑↓ — the same edit, one click at a
    // time — and the button that would leave the list is disabled, so a click always does
    // something.
    void theMoveButtonsMoveARowAndAreDisabledAtTheEnds() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max")},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()},
                                         {QStringLiteral("guest:claude|opus"), QString()}});
        ModelPicker picker(context());
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        const auto buttons = [&picker](const QString &key) {
            QWidget *box = picker.list()->itemWidget(rowFor(picker.list(), key), ColMove);
            return box ? box->findChildren<QToolButton *>() : QList<QToolButton *>{};
        };
        const QList<QToolButton *> first = buttons(QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(first.size(), 2);
        QVERIFY(!first.at(0)->isEnabled());    // rank 1 has no up
        QVERIFY(first.at(1)->isEnabled());
        const QList<QToolButton *> last = buttons(QStringLiteral("guest:claude|opus"));
        QVERIFY(last.at(0)->isEnabled());
        QVERIFY(!last.at(1)->isEnabled());     // the last rank has no down
        QTest::mouseClick(first.at(1), Qt::LeftButton);
        QTest::qWait(20);                      // the click defers a turn: it rebuilds the row its button sits on
        QCOMPARE(listKeys(QStringLiteral("main")).at(1), QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(curation::tierList(QStringLiteral("main")).at(1).effort, QStringLiteral("max"));   // the level travels
        QCOMPARE(picker.selectedKey(), QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(told, 1);
        // …and after the rebuild the moved row's buttons say what they can do now.
        const QList<QToolButton *> moved = buttons(QStringLiteral("glm-coding|glm-5.3"));
        QVERIFY(moved.at(0)->isEnabled() && moved.at(1)->isEnabled());
    }

    // Card #RKP3: a drag across a section header is a move — out of the source list, into the
    // target list at the dropped rank. (It used to snap back in silence, which is what "the rank
    // can't be changed" was, #YX8Q.)
    void aDragAcrossASectionHeaderMovesTheModelBetweenLists() {
        setList(QStringLiteral("high"), {{QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max")},
                                         {QStringLiteral("guest:claude|opus"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = ModelPicker::classesTier();
        ModelPicker picker(ctx);
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        // Page: #high, opus, #main, glm, guest, #flash, hint. glm goes up under high's opus.
        QCOMPARE(pageRows(picker.list()).size(), 7);
        QTreeWidgetItem *moved = picker.list()->takeTopLevelItem(3);
        picker.list()->insertTopLevelItem(2, moved);
        picker.commitDragOrder();
        QCOMPARE(listKeys(QStringLiteral("high")), (QStringList{QStringLiteral("anthropic|claude-opus-5-5"),
                                                                QStringLiteral("glm-coding|glm-5.3")}));
        QCOMPARE(listKeys(QStringLiteral("main")), QStringList{QStringLiteral("guest:claude|opus")});
        QCOMPARE(curation::tierList(QStringLiteral("high")).at(1).effort, QStringLiteral("max"));   // the level travels across
        QCOMPARE(told, 1);
        // Both lists redraw from storage: the moved row sits in the high section now.
        QCOMPARE(pageRows(picker.list()), (QStringList{
            QStringLiteral("#high"), QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("glm-coding|glm-5.3"),
            QStringLiteral("#main"), QStringLiteral("guest:claude|opus"),
            QStringLiteral("#flash"), QStringLiteral("[nothing in flash — type a model's name, then ctrl+enter adds it here]")}));
    }

    // Card #RKP3: an empty section takes a drop too — that is how a class a model was never in
    // gets it — and the emptied one is stored empty, not forgotten.
    void aDropIntoAnEmptySectionAddsItThere() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = ModelPicker::classesTier();
        ModelPicker picker(ctx);
        // Page: #high, hint, #main, glm, #flash, hint. glm goes up into the empty high section.
        QCOMPARE(pageRows(picker.list()).size(), 6);
        QTreeWidgetItem *moved = picker.list()->takeTopLevelItem(3);
        picker.list()->insertTopLevelItem(1, moved);
        picker.commitDragOrder();
        QCOMPARE(listKeys(QStringLiteral("high")), QStringList{QStringLiteral("glm-coding|glm-5.3")});
        QVERIFY(curation::tierList(QStringLiteral("main")).isEmpty());
        QVERIFY(QSettings().contains(QStringLiteral("models/tier/main")));   // "nothing here" is a choice
    }

    // A drop while the filter hides listed rows is a view, not an order: nothing is stored, the
    // rows redraw as the list really reads, and the limits line says why instead of snapping back
    // in silence (#YX8Q).
    void aDropWithRowsFilteredAwayIsRefusedAndSaysWhy() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()},
                                         {QStringLiteral("guest:claude|opus"), QString()}});
        ModelPicker picker(context());
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        picker.filter()->setText(QStringLiteral("glm"));
        QTreeWidgetItem *moved = picker.list()->takeTopLevelItem(0);
        picker.list()->insertTopLevelItem(1, moved);   // among the "not in this list" rows now
        picker.commitDragOrder();
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("glm-coding|glm-5.3"),
                                                                QStringLiteral("anthropic|claude-opus-5-5"),
                                                                QStringLiteral("guest:claude|opus")}));
        QCOMPARE(told, 0);
        QCOMPARE(picker.findChild<QLabel *>(QStringLiteral("modelLimits"))->text(),
                 QStringLiteral("not stored — the filter is hiding rows, so the drop is not an order; clear it to reorder"));
    }

    // Card #RKP3: hosted, the footer's keys are the pane's — Tab / Shift+Tab, not the alt+digits
    // the window took back (#PNAV) — and the priorities line names what a rank change is now.
    void theFooterNamesTheRealKeys() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = ModelPicker::classesTier();
        ModelPicker picker(ctx);
        picker.setHosted(true);
        const QString text = picker.footer()->text();
        QVERIFY(!text.contains(QStringLiteral("alt+1")));
        QVERIFY(text.contains(QStringLiteral("tab / shift+tab")));
        QVERIFY(text.contains(QStringLiteral("▲▼")));
        QVERIFY(text.contains(QStringLiteral("into another section")));
    }


    // ----- the priorities page: one page, four sections (owner, 2026-09-21) --------------------
    // "tab 3: in a pane, i dont want separate tabs for the modes. they should just be in divided
    // sections. remove the lite section."

    void thePrioritiesPageIsOneSectionPerClassAndNeverLite() {
        setList(QStringLiteral("high"), {{QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("guest:claude|opus"), QString()}});
        setList(QStringLiteral("flash"), {{QStringLiteral("glm-coding|glm-5.3-flash"), QString()}});
        setList(QStringLiteral("lite"), {{QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = ModelPicker::classesTier();
        ModelPicker picker(ctx);
        QCOMPARE(picker.tier(), QStringLiteral("classes"));
        // high, main, flash — and no lite, and no local (nothing is served here).
        QCOMPARE(picker.sectionTiers(), (QStringList{QStringLiteral("high"), QStringLiteral("main"),
                                                     QStringLiteral("flash")}));
        QCOMPARE(pageRows(picker.list()), (QStringList{
            QStringLiteral("#high"), QStringLiteral("anthropic|claude-opus-5-5"),
            QStringLiteral("#main"), QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("guest:claude|opus"),
            QStringLiteral("#flash"), QStringLiteral("glm-coding|glm-5.3-flash")}));
        // The lite list is untouched underneath: only the page stopped drawing it.
        QCOMPARE(listKeys(QStringLiteral("lite")), QStringList{QStringLiteral("anthropic|claude-opus-5-5")});
        // Each header carries its class's "show this class in the box" switch and a one-line note.
        QTreeWidgetItem *mainHead = classHeader(picker.list(), QStringLiteral("main"));
        QVERIFY(mainHead != nullptr);
        QCOMPARE(mainHead->text(ColModel), QStringLiteral("main"));
        // The note goes in the wide column: the model column is the narrow one and elided it.
        QCOMPARE(mainHead->text(ColVia), QStringLiteral("new panes start on rank 1"));
        QCOMPARE(mainHead->checkState(ColBox), Qt::Checked);
        // Rank 1 of main says what it is once, on the section's header: the row said it too until
        // the two together cost the model column a third of its width and elided the name.
        QVERIFY(!rowFor(picker.list(), QStringLiteral("glm-coding|glm-5.3"))->text(ColModel)
                     .contains(QStringLiteral("new panes start here")));
        QVERIFY(rowFor(picker.list(), QStringLiteral("glm-coding|glm-5.3"))->toolTip(ColModel)
                    .contains(QStringLiteral("rank 1 of main")));
        // …and on a single-class page, which has no header, the row still carries it.
        picker.setTier(QStringLiteral("main"));
        QVERIFY(rowFor(picker.list(), QStringLiteral("glm-coding|glm-5.3"))->text(ColModel)
                    .contains(QStringLiteral("new panes start here")));
    }

    // "↑/↓ move across sections": the one key that has to cross a header, and it steps over them.
    void upAndDownWalkTheRowsAcrossTheSections() {
        setList(QStringLiteral("high"), {{QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        setList(QStringLiteral("flash"), {{QStringLiteral("glm-coding|glm-5.3-flash"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = ModelPicker::classesTier();
        ModelPicker picker(ctx);
        picker.selectKey(QStringLiteral("anthropic|claude-opus-5-5"));
        QCOMPARE(picker.currentClass(), QStringLiteral("high"));
        QTest::keyClick(picker.list(), Qt::Key_Down);
        QCOMPARE(picker.selectedKey(), QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(picker.currentClass(), QStringLiteral("main"));
        QTest::keyClick(picker.list(), Qt::Key_Down);
        QCOMPARE(picker.selectedKey(), QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(picker.currentClass(), QStringLiteral("flash"));
        QTest::keyClick(picker.list(), Qt::Key_Down);   // the last row: it stays there
        QCOMPARE(picker.selectedKey(), QStringLiteral("glm-coding|glm-5.3-flash"));
        QTest::keyClick(picker.list(), Qt::Key_Up);
        QCOMPARE(picker.selectedKey(), QStringLiteral("glm-coding|glm-5.3"));
        // …and from the filter line, which forwards them.
        QTest::keyClick(picker.filter(), Qt::Key_Up);
        QCOMPARE(picker.selectedKey(), QStringLiteral("anthropic|claude-opus-5-5"));
    }

    // Alt+↑/↓ reorders **inside** a section and stops at its edge: pushing the last flash row down
    // must not drop the model into another class.
    void altUpAndDownMoveARowInsideItsOwnSection() {
        setList(QStringLiteral("high"), {{QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("guest:claude|opus"), QString()}});
        setList(QStringLiteral("flash"), {{QStringLiteral("glm-coding|glm-5.3-flash"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = ModelPicker::classesTier();
        ModelPicker picker(ctx);
        picker.selectKey(QStringLiteral("guest:claude|opus"));
        QTest::keyClick(picker.list(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("guest:claude|opus"),
                                                                QStringLiteral("glm-coding|glm-5.3")}));
        // At the top of its own section it goes no further, and no other list moves.
        QTest::keyClick(picker.list(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("guest:claude|opus"),
                                                                QStringLiteral("glm-coding|glm-5.3")}));
        QCOMPARE(listKeys(QStringLiteral("high")), QStringList{QStringLiteral("anthropic|claude-opus-5-5")});
        // The only flash row cannot leave flash either.
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3-flash"));
        QTest::keyClick(picker.list(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(listKeys(QStringLiteral("flash")), QStringList{QStringLiteral("glm-coding|glm-5.3-flash")});
        // Delete reads the row's own class too.
        QTest::keyClick(picker.list(), Qt::Key_Delete);
        QVERIFY(listKeys(QStringLiteral("flash")).isEmpty());
        QCOMPARE(listKeys(QStringLiteral("main")).size(), 2);
        QTest::keyClick(picker.list(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(listKeys(QStringLiteral("flash")), QStringList{QStringLiteral("glm-coding|glm-5.3-flash")});
    }

    // "ctrl+enter adds the typed model to the section the highlight is in": each section draws its
    // own "not in <class>" block, so the highlight's section is the list it lands in and there is
    // no fourth place with a rule of its own.
    void ctrlEnterAddsIntoTheSectionTheHighlightIsIn() {
        setList(QStringLiteral("high"), {{QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        setList(QStringLiteral("flash"), {{QStringLiteral("glm-coding|glm-5.3-flash"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = ModelPicker::classesTier();
        ModelPicker picker(ctx);
        picker.filter()->setText(QStringLiteral("opus"));
        // claude-opus-5-5 is already in high, so high shows it as a rank; main and flash offer it.
        QCOMPARE(pageRows(picker.list()), (QStringList{
            QStringLiteral("#high"), QStringLiteral("anthropic|claude-opus-5-5"),
            QStringLiteral("[not in high — ctrl+enter adds it here]"), QStringLiteral("guest:claude|opus"),
            QStringLiteral("#main"), QStringLiteral("[not in main — ctrl+enter adds it here]"),
            QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("guest:claude|opus"),
            QStringLiteral("#flash"), QStringLiteral("[not in flash — ctrl+enter adds it here]"),
            QStringLiteral("anthropic|claude-opus-5-5")}));
        // A guest harness is offered where it can be a whole agent — high and main — and nowhere
        // else, so flash's block holds the API row alone. The rule is `addableToTier`, per section.
        QVERIFY(rowFor(picker.list(), QStringLiteral("guest:claude|opus"), QStringLiteral("flash")) == nullptr);
        // The flash one: the row under the flash header, and ctrl+enter puts it in flash.
        QTreeWidgetItem *offered = rowFor(picker.list(), QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("flash"));
        QVERIFY(offered != nullptr);
        picker.list()->setCurrentItem(offered);
        QCOMPARE(picker.currentClass(), QStringLiteral("flash"));
        QTest::keyClick(picker.list(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(listKeys(QStringLiteral("flash")), (QStringList{QStringLiteral("glm-coding|glm-5.3-flash"),
                                                                 QStringLiteral("anthropic|claude-opus-5-5")}));
        QCOMPARE(listKeys(QStringLiteral("main")), QStringList{QStringLiteral("glm-coding|glm-5.3")});
        QCOMPARE(listKeys(QStringLiteral("high")), QStringList{QStringLiteral("anthropic|claude-opus-5-5")});
    }

    // Each section's header tick is that class's switch and the ticks under it are its cutoff —
    // one column, one question, read down. Two classes on one page keep their own answers.
    void everySectionCarriesItsOwnSwitchAndCutoff() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()},
                                         {QStringLiteral("guest:claude|opus"), QString()}});
        setList(QStringLiteral("flash"), {{QStringLiteral("glm-coding|glm-5.3-flash"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = ModelPicker::classesTier();
        ModelPicker picker(ctx);
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        QVERIFY(!picker.list()->isColumnHidden(ColBox));
        QVERIFY(picker.classSwitch()->isHidden() || !picker.isVisible());   // it is on the headers now
        // Two per class by default: main's first two, flash's one.
        QCOMPARE(rowFor(picker.list(), QStringLiteral("guest:claude|opus"))->checkState(ColBox), Qt::Unchecked);
        QCOMPARE(rowFor(picker.list(), QStringLiteral("glm-coding|glm-5.3-flash"))->checkState(ColBox), Qt::Checked);
        // Checking rank 3 of main moves main's cutoff and leaves flash alone.
        rowFor(picker.list(), QStringLiteral("guest:claude|opus"))->setCheckState(ColBox, Qt::Checked);
        QCOMPARE(curation::boxCutoff(QStringLiteral("main")), 3);
        QCOMPARE(curation::boxCutoff(QStringLiteral("flash")), 2);
        QCOMPARE(told, 1);
        // The header's own tick switches the class off; the rows under it go blank, flash does not.
        classHeader(picker.list(), QStringLiteral("main"))->setCheckState(ColBox, Qt::Unchecked);
        QVERIFY(!curation::boxShown(QStringLiteral("main")));
        QVERIFY(curation::boxShown(QStringLiteral("flash")));
        QCOMPARE(rowFor(picker.list(), QStringLiteral("glm-coding|glm-5.3"))->checkState(ColBox), Qt::Unchecked);
        QCOMPARE(rowFor(picker.list(), QStringLiteral("glm-coding|glm-5.3-flash"))->checkState(ColBox), Qt::Checked);
        QCOMPARE(told, 2);
        classHeader(picker.list(), QStringLiteral("main"))->setCheckState(ColBox, Qt::Checked);
        QVERIFY(curation::boxShown(QStringLiteral("main")));
        QCOMPARE(rowFor(picker.list(), QStringLiteral("glm-coding|glm-5.3"))->checkState(ColBox), Qt::Checked);
    }

    // A drag inside one section rewrites that section; a drag past a header lands in the section
    // it was dropped into, at that rank — a move, not a refusal (card #RKP3; the silent snap-back
    // this test used to assert is what read as "the rank can't be changed", #YX8Q).
    void aDragInsideASectionRewritesItAndACrossingDragMovesItThere() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("high")}});
        setList(QStringLiteral("flash"), {{QStringLiteral("glm-coding|glm-5.3-flash"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = ModelPicker::classesTier();
        ModelPicker picker(ctx);
        const int mainAt = picker.list()->indexOfTopLevelItem(rowFor(picker.list(), QStringLiteral("glm-coding|glm-5.3")));
        QTreeWidgetItem *moved = picker.list()->takeTopLevelItem(mainAt);
        picker.list()->insertTopLevelItem(mainAt + 1, moved);
        picker.commitDragOrder();
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("anthropic|claude-opus-5-5"),
                                                                QStringLiteral("glm-coding|glm-5.3")}));
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().effort, QStringLiteral("high"));
        // Now drag main's last row past the flash header: it leaves main and lands at the end of
        // flash, in both directions stored.
        const int last = picker.list()->indexOfTopLevelItem(rowFor(picker.list(), QStringLiteral("glm-coding|glm-5.3")));
        QTreeWidgetItem *crossed = picker.list()->takeTopLevelItem(last);
        picker.list()->addTopLevelItem(crossed);
        picker.commitDragOrder();
        QCOMPARE(listKeys(QStringLiteral("main")), QStringList{QStringLiteral("anthropic|claude-opus-5-5")});
        QCOMPARE(listKeys(QStringLiteral("flash")), (QStringList{QStringLiteral("glm-coding|glm-5.3-flash"),
                                                                 QStringLiteral("glm-coding|glm-5.3")}));
    }

    // An empty class still has a header, still says how to fill it, and still says which class the
    // page is on — a page with nothing ranked would otherwise have no answer at all.
    void anEmptySectionKeepsItsHeaderAndItsPlace() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = ModelPicker::classesTier();
        ModelPicker picker(ctx);
        QCOMPARE(pageRows(picker.list()), (QStringList{
            QStringLiteral("#high"), QStringLiteral("[nothing in high — type a model's name, then ctrl+enter adds it here]"),
            QStringLiteral("#main"), QStringLiteral("glm-coding|glm-5.3"),
            QStringLiteral("#flash"), QStringLiteral("[nothing in flash — type a model's name, then ctrl+enter adds it here]")}));
        picker.focusClass(QStringLiteral("flash"));
        QCOMPARE(picker.currentClass(), QStringLiteral("flash"));
        QVERIFY(picker.selectedKey().isEmpty());   // a header is not a model
        picker.focusClass(QStringLiteral("main"));
        QCOMPARE(picker.selectedKey(), QStringLiteral("glm-coding|glm-5.3"));
    }

    // ----- the flat tab -----------------------------------------------------------------------------

    void theAllTabShowsOneRowPerModelAndRightPicksTheProvider() {
        ModelPicker picker(solContext(QStringLiteral("glm-coding|glm-5.3")));
        QCOMPARE(picker.tier(), QStringLiteral("all"));
        // gpt-6-sol is served by codex, the OpenAI API and OpenRouter: one row, "+2" beside it.
        const QStringList rows = rowKeys(picker.list());
        QCOMPARE(rows.count(QStringLiteral("openai|gpt-6-sol")) + rows.count(QStringLiteral("guest:codex|gpt-6-sol"))
                     + rows.count(QStringLiteral("openrouter|openai/gpt-6-sol")), 1);
        picker.selectKey(QStringLiteral("openai|gpt-6-sol"));
        QTreeWidgetItem *sol = picker.list()->currentItem();
        QCOMPARE(sol->text(ColModel), QStringLiteral("gpt-6-sol"));
        QVERIFY(sol->text(ColVia).endsWith(QStringLiteral("+2")));
        QCOMPARE(picker.viaList()->count(), 3);
        // → opens the providers; choosing one is what this row will run, and the levels follow it
        // into the provider's own words (codex says xhigh where the API says max).
        picker.viaList()->setCurrentRow(picker.viaList()->count() - 1);
        const QString chosen = picker.viaList()->currentItem()->data(Qt::UserRole).toString();
        QCOMPARE(picker.selectedKey(), chosen);
        QVERIFY(sol->text(ColVia).startsWith(picker.viaList()->currentItem()->text()));
        for (int i = 0; i < picker.viaList()->count(); ++i)
            if (picker.viaList()->item(i)->data(Qt::UserRole).toString() == QStringLiteral("guest:codex|gpt-6-sol")) {
                picker.viaList()->setCurrentRow(i);
                break;
            }
        QCOMPARE(picker.selectedKey(), QStringLiteral("guest:codex|gpt-6-sol"));
        QCOMPARE(picker.levelList()->count(), 3);
        QCOMPARE(picker.levelList()->item(2)->text(), QStringLiteral("xhigh"));   // the provider's word
        picker.use();
        QCOMPARE(picker.pick().key, QStringLiteral("guest:codex|gpt-6-sol"));
        // A row with one provider only has no "via" list to open.
        ModelPicker plain(solContext());
        plain.selectKey(QStringLiteral("anthropic|claude-opus-5-5"));
        QVERIFY(plain.viaList()->count() <= 1);
    }

    // Favorites, then one section per provider **alphabetically**, and no "recent" section at all
    // (owner, 2026-09-21: "for available, remove the recent section. i would order the sections
    // alphabetically"). `noteUse` still runs — it is what counts a model's uses for the sort menu
    // — and no longer puts a block of rows above the providers.
    void theAvailableTabIsFavoritesThenProvidersAlphabetically() {
        curation::toggleFavorite(QStringLiteral("guest:claude|opus"));
        curation::noteUse(QStringLiteral("anthropic|claude-opus-5-5"));
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        QCOMPARE(keys(picker.list()), (QStringList{QStringLiteral("[favorites]"), QStringLiteral("guest:claude|opus"),
                                                   QStringLiteral("[anthropic (claude)]"), QStringLiteral("anthropic|claude-opus-5-5"),
                                                   QStringLiteral("[z.ai (glm)]"), QStringLiteral("glm-coding|glm-5.3"),
                                                   QStringLiteral("glm-coding|glm-5.3-flash"),
                                                   QStringLiteral("[+ add a model by id…]")}));
        QVERIFY(picker.list()->topLevelItem(1)->text(ColModel).startsWith(QStringLiteral("★ ")));
        QVERIFY(picker.sortBox()->isVisibleTo(&picker));
        picker.filter()->setText(QStringLiteral("flash"));
        QCOMPARE(rowKeys(picker.list()), QStringList{QStringLiteral("glm-coding|glm-5.3-flash")});
        picker.filter()->setText(QStringLiteral("claude"));
        QCOMPARE(rowKeys(picker.list()), (QStringList{QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("guest:claude|opus")}));
        picker.filter()->clear();
        QCOMPARE(keys(picker.list()).size(), 8);   // …and "+ add a model by id…" at the end
        const int intelligence = picker.sortBox()->findData(QStringLiteral("intelligence"));
        picker.sortBox()->setCurrentIndex(intelligence);
        emit picker.sortBox()->activated(intelligence);
        QCOMPARE(rowKeys(picker.list()).first(), QStringLiteral("anthropic|claude-opus-5-5"));   // 51 beats 45
        QCOMPARE(curation::sort(), Sort::Intelligence);
        ModelPicker again(ctx);
        QCOMPARE(again.sortBox()->currentData().toString(), QStringLiteral("intelligence"));
        // The sort menu belongs to the flat tab: a tier tab's order is the list's own.
        again.setTier(QStringLiteral("main"));
        QVERIFY(!again.sortBox()->isVisibleTo(&again));
    }

    void theLevelListFollowsTheHighlightedModel() {
        curation::addToTier(QStringLiteral("main"), QStringLiteral("glm-coding|glm-5.3-flash"), QStringLiteral("low"));
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(picker.levelList()->count(), 3);                                 // no "default" off a list
        QCOMPARE(picker.selectedEffort(), QStringLiteral("low"));                 // the list's level
        QCOMPARE(picker.list()->currentItem()->text(ColReasoning), QStringLiteral("low"));
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(picker.selectedEffort(), QStringLiteral("high"));                // in no list: the pane's own
        picker.levelList()->setCurrentRow(2);
        QCOMPARE(picker.selectedEffort(), QStringLiteral("max"));                 // a separate pick
        QCOMPARE(curation::tierList(QStringLiteral("main")).first().effort, QStringLiteral("low"));   // and no list edit
        picker.selectKey(QStringLiteral("anthropic|claude-opus-5-5"));
        QCOMPARE(picker.selectedEffort(), QString());                             // no knob
        QVERIFY(picker.findChild<QLabel *>(QStringLiteral("modelLimits"))->text().isEmpty());
        picker.selectKey(QStringLiteral("guest:claude|opus"));
        QVERIFY(picker.findChild<QLabel *>(QStringLiteral("modelLimits"))->text().contains(QStringLiteral("5h 62% left")));
    }

    void aPickReturnsTheKeyAndTheLevel() {
        curation::addToTier(QStringLiteral("high"), QStringLiteral("glm-coding|glm-5.3-flash"), QStringLiteral("max"));
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3-flash"));
        picker.use();
        QVERIFY(picker.pick().accepted);
        QCOMPARE(picker.pick().key, QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(picker.pick().effort, QStringLiteral("max"));
    }

    // Owner, 2026-09-21: his pane went onto gemini flash lite while he was trying to un-tick it —
    // two quick clicks on the tick were read as a double click, and a double click "uses" the row.
    // A tick cell is a control: a double click there commits nothing.
    void aDoubleClickOnATickNeverUsesTheRow() {
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        picker.show();
        QVERIFY(QTest::qWaitForWindowExposed(&picker));
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3-flash"));
        QTreeWidgetItem *flash = picker.list()->currentItem();
        QVERIFY(flash);
        Q_EMIT picker.list()->itemDoubleClicked(flash, ColAvail);   // the "available" tick
        QVERIFY(!picker.pick().accepted);
        Q_EMIT picker.list()->itemDoubleClicked(flash, ColBox);     // the "in box" tick
        QVERIFY(!picker.pick().accepted);
        Q_EMIT picker.list()->itemDoubleClicked(flash, ColModel);   // the name — this one uses it
        QVERIFY(picker.pick().accepted);
        QCOMPARE(picker.pick().key, QStringLiteral("glm-coding|glm-5.3-flash"));
    }

    void aClickOnlyHighlights() {
        // Owner, 2026-09-20: clicking a row must not close the dialog, so the level can be picked
        // after the model. Enter commits; a double click is the shortcut for both.
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        picker.show();
        QVERIFY(QTest::qWaitForWindowExposed(&picker));
        // Row 0 of this tab is a provider rule now (design 5.7), so the row is found by its key
        // rather than by an index that says which section happens to be drawn first.
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3-flash"));
        QTreeWidgetItem *flash = picker.list()->currentItem();
        QCOMPARE(flash->data(0, Qt::UserRole).toString(), QStringLiteral("glm-coding|glm-5.3-flash"));
        const QRect rect = picker.list()->visualItemRect(flash);
        QTest::mouseClick(picker.list()->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QCOMPARE(picker.selectedKey(), QStringLiteral("glm-coding|glm-5.3-flash"));
        QVERIFY(!picker.pick().accepted);
        QVERIFY(picker.isVisible());
        // A desktop that activates on a single click must not commit either: the dialog no longer
        // listens to itemActivated at all.
        Q_EMIT picker.list()->itemActivated(flash, 0);
        Q_EMIT picker.levelList()->itemActivated(picker.levelList()->item(0));
        QVERIFY(!picker.pick().accepted);
        picker.levelList()->setCurrentRow(0);
        QTest::mouseClick(picker.levelList()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          picker.levelList()->visualItemRect(picker.levelList()->item(2)).center());
        QVERIFY(!picker.pick().accepted);
        QCOMPARE(picker.selectedEffort(), QStringLiteral("max"));
        QTest::keyClick(picker.list(), Qt::Key_Return);            // the list swallows Enter itself
        QVERIFY(picker.pick().accepted);
        QCOMPARE(picker.pick().key, QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(picker.pick().effort, QStringLiteral("max"));
    }

    // ----- the levels are the model's (card #MDL1, owner 2026-09-21) ---------------------------

    void levelsAreTheModelsOwnListInTheProvidersWords() {
        curation::addToTier(QStringLiteral("main"), QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max"));
        ModelPicker::Context ctx = context(QStringLiteral("anthropic|claude-opus-5-5"), QStringLiteral("max"));
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(picker.list()->currentItem()->text(ColReasoning), QStringLiteral("max"));
        QCOMPARE(picker.levelList()->currentItem()->text(), QStringLiteral("max"));
        QCOMPARE(picker.selectedEffort(), QStringLiteral("max"));
        QStringList offered;
        for (int i = 0; i < picker.levelList()->count(); ++i) offered << picker.levelList()->item(i)->text();
        QCOMPARE(offered, (QStringList{QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}));
    }

    // "so xhigh shows up for codex for example": six rows, in codex's order, `ultra` included.
    void aCodexRowListsXhighAndUltra() {
        ModelPicker::Context ctx = context(QStringLiteral("anthropic|claude-opus-5-5"));
        ctx.catalog = catalogFrom(codexPresets());
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        picker.selectKey(QStringLiteral("guest:codex|gpt-6-sol"));
        QStringList offered;
        for (int i = 0; i < picker.levelList()->count(); ++i) offered << picker.levelList()->item(i)->text();
        QCOMPARE(offered, (QStringList{QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"),
                                       QStringLiteral("xhigh"), QStringLiteral("max"), QStringLiteral("ultra")}));
        picker.levelList()->setCurrentRow(3);
        QCOMPARE(picker.selectedEffort(), QStringLiteral("xhigh"));
    }

    // "for no knob models, the effort box should be grayed out. same for relay free." In the
    // dialog that is an empty level list with the reason in it, and nothing to pick.
    void aRelayFreeRowListsNothingAndSaysWhy() {
        ModelPicker::Context ctx = context(QStringLiteral("anthropic|claude-opus-5-5"));
        ctx.catalog = catalogFrom(codexPresets());
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        picker.selectKey(QStringLiteral("relay-free|relay-main"));
        QCOMPARE(picker.levelList()->count(), 1);
        QCOMPARE(picker.levelList()->item(0)->text(), QStringLiteral("Relay Free sets the level for you"));
        QVERIFY(!(picker.levelList()->item(0)->flags() & Qt::ItemIsSelectable));
        QVERIFY(picker.selectedEffort().isEmpty());
        // A model with no knob at all says the other half of the same rule, naming the model.
        picker.selectKey(QStringLiteral("anthropic|claude-opus-5-5"));
        QCOMPARE(picker.levelList()->count(), 1);
        QCOMPARE(picker.levelList()->item(0)->text(), QStringLiteral("claude-opus-5-5 has no reasoning level"));
    }

    // Rule 3: a level the row does not take is preselected as the nearest one it does — the level
    // the pick would really run at — rather than being dropped.
    void aLevelTheRowDoesNotTakeSnapsInThePreselect() {
        ModelPicker::Context ctx = context(QStringLiteral("guest:codex|gpt-6-sol"), QStringLiteral("xhigh"));
        ctx.catalog = catalogFrom(codexPresets());
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        picker.selectKey(QStringLiteral("openai|gpt-6-astra"));   // low, medium, high, max
        QCOMPARE(picker.levelList()->currentItem()->text(), QStringLiteral("max"));
        QCOMPARE(picker.selectedEffort(), QStringLiteral("max"));
    }

    // ----- the header and the footer ------------------------------------------------------------

    void theProfileIsInTheHeaderAndSwitchingItSwapsEveryList() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        curation::saveProfile(QStringLiteral("ai work"));
        // The live lists and the current profile are one thing, so "admin work" is named first and
        // *then* given its own list — editing while "ai work" was current would have edited it.
        curation::saveProfile(QStringLiteral("admin work"));
        setList(QStringLiteral("main"), {{QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        ModelPicker picker(context());
        QVERIFY(picker.profileBox() != nullptr);
        QCOMPARE(picker.profileBox()->currentData().toString(), QStringLiteral("admin work"));
        QCOMPARE(rowKeys(picker.list()), QStringList{QStringLiteral("anthropic|claude-opus-5-5")});
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        const int other = picker.profileBox()->findData(QStringLiteral("ai work"));
        picker.profileBox()->setCurrentIndex(other);
        emit picker.profileBox()->activated(other);
        QCOMPARE(curation::currentProfile(), QStringLiteral("ai work"));
        QCOMPARE(rowKeys(picker.list()), QStringList{QStringLiteral("glm-coding|glm-5.3")});
        QCOMPARE(told, 1);
    }

    void withNoProfilesThereIsNoProfileBox() {
        ModelPicker picker(context());
        QCOMPARE(picker.profileBox(), nullptr);
    }

    void theFooterSpellsTheKeysOfTheTabYouAreOn() {
        ModelPicker picker(context());
        QVERIFY(picker.footer()->text().contains(QStringLiteral("alt+↑↓ moves a row")));
        QVERIFY(picker.footer()->text().contains(QStringLiteral("ctrl+z undoes")));
        picker.setTier(QStringLiteral("all"));
        QVERIFY(!picker.footer()->text().contains(QStringLiteral("alt+↑↓")));
        QVERIFY(picker.footer()->text().contains(QStringLiteral("the providers of a folded row")));
    }

    // ----- what the Alt+M box shows of each class (card #MDL1, design 5.3) ----------------------

    // "each row of a class tab gets a 'show in box' checkbox column that behaves as a CUTOFF:
    // checking row n checks 1..n, unchecking row n unchecks n..end".
    void theShowInBoxColumnIsACutoffBothWays() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("kimi|kimi-k3"), QString()},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()},
                                         {QStringLiteral("guest:claude|opus"), QString()}});
        ModelPicker picker(context());
        const auto checks = [&picker] {
            QString out;
            for (int i = 0; i < picker.list()->topLevelItemCount(); ++i) {
                QTreeWidgetItem *row = picker.list()->topLevelItem(i);
                if (row->data(0, Qt::UserRole).toString().isEmpty()) continue;
                out += row->checkState(ColBox) == Qt::Checked ? QLatin1Char('x') : QLatin1Char('.');
            }
            return out;
        };
        // Two per class by default, and the column is visible because main is one of the classes
        // the box draws.
        QVERIFY(!picker.list()->isColumnHidden(ColBox));
        QCOMPARE(checks(), QStringLiteral("xx.."));

        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        // Checking rank 4 checks 1..4.
        picker.list()->topLevelItem(3)->setCheckState(ColBox, Qt::Checked);
        QCOMPARE(curation::boxCutoff(QStringLiteral("main")), 4);
        QCOMPARE(checks(), QStringLiteral("xxxx"));
        QCOMPARE(told, 1);
        // Unchecking rank 2 unchecks 2..end.
        picker.list()->topLevelItem(1)->setCheckState(ColBox, Qt::Unchecked);
        QCOMPARE(curation::boxCutoff(QStringLiteral("main")), 1);
        QCOMPARE(checks(), QStringLiteral("x..."));
        QCOMPARE(told, 2);
        // Unchecking rank 1 leaves nothing to show, which is the class switched off — said from
        // the other control, so the two can never disagree.
        picker.list()->topLevelItem(0)->setCheckState(ColBox, Qt::Unchecked);
        QVERIFY(!curation::boxShown(QStringLiteral("main")));
        QCOMPARE(checks(), QStringLiteral("...."));
        QVERIFY(!picker.classSwitch()->isChecked());
        // And checking one again switches the class back on at that cutoff.
        picker.list()->topLevelItem(2)->setCheckState(ColBox, Qt::Checked);
        QVERIFY(curation::boxShown(QStringLiteral("main")));
        QCOMPARE(curation::boxCutoff(QStringLiteral("main")), 3);
        QCOMPARE(checks(), QStringLiteral("xxx."));
        QVERIFY(picker.classSwitch()->isChecked());
    }

    // "a class tab gets a 'show this class in the box' switch in its header" — and the tabs the
    // box never draws (lite, all) have neither it nor the column.
    void theClassSwitchIsOnlyOnTheTabsTheBoxDraws() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        ModelPicker picker(context());
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        QVERIFY(picker.classSwitch()->isVisible() || !picker.isVisible());   // shown on a class tab
        QVERIFY(picker.classSwitch()->isChecked());
        picker.classSwitch()->setChecked(false);
        QVERIFY(!curation::boxShown(QStringLiteral("main")));
        QCOMPARE(told, 1);
        // Nothing of the class is ticked any more; the cutoff it had is remembered.
        QCOMPARE(picker.list()->topLevelItem(0)->checkState(ColBox), Qt::Unchecked);
        picker.classSwitch()->setChecked(true);
        QVERIFY(curation::boxShown(QStringLiteral("main")));
        QCOMPARE(picker.list()->topLevelItem(0)->checkState(ColBox), Qt::Checked);

        // lite is never a pane mode, so the box never draws it: no switch, no column.
        picker.setTier(QStringLiteral("lite"));
        QVERIFY(picker.list()->isColumnHidden(ColBox));
        QVERIFY(picker.classSwitch()->isHidden() || !picker.isVisible());
        picker.setTier(QStringLiteral("all"));
        QVERIFY(picker.list()->isColumnHidden(ColBox));
    }

    // The two "fill the lists" buttons of Options › Models, moved into the dialog (design 5.5).
    // The action belongs to the caller — only a pane holds what its worker computed — so what is
    // asserted here is that each button presses it and that a "no defaults yet" answer is said
    // rather than swallowed.
    void fillFromDefaultsPressesTheCallersAction() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        ModelPicker::Context ctx = context();
        QList<bool> pressed;
        bool answer = true;
        ctx.fillFromDefaults = [&pressed, &answer](bool withOpenrouter) {
            pressed << withOpenrouter;
            if (answer) curation::setTierList(QStringLiteral("main"), {{QStringLiteral("kimi|kimi-k3"), QString()}});
            return answer;
        };
        ModelPicker picker(ctx);
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        QVERIFY(picker.defaultsButton(false) != nullptr);
        picker.defaultsButton(false)->click();
        QCOMPARE(pressed, QList<bool>({false}));
        QCOMPARE(rowKeys(picker.list()), QStringList{QStringLiteral("kimi|kimi-k3")});
        QCOMPARE(told, 1);
        picker.defaultsButton(true)->click();
        QCOMPARE(pressed, QList<bool>({false, true}));

        // Ctrl+Z takes one list back, so the fill is not a one-way door.
        picker.undo();
        QVERIFY(picker.undoDepth() > 0);

        // A caller that answers false has no defaults yet: nothing is written and the dialog says so.
        answer = false;
        const int depth = picker.undoDepth();
        picker.defaultsButton(false)->click();
        QCOMPARE(picker.undoDepth(), depth);
        QVERIFY(picker.findChild<QLabel *>(QStringLiteral("modelLimits"))->text().contains(QStringLiteral("No defaults yet")));

        // A caller that offers none gets no buttons at all.
        ModelPicker bare(context());
        QCOMPARE(bare.defaultsButton(false), nullptr);
        QCOMPARE(bare.defaultsButton(true), nullptr);
    }

    // ----- the long tail, and the id box that came with it (card #MDL1 t:a10, design 5.5) --------

    // `shown()` holds an open-ended provider's rows back — OpenRouter's live listing would
    // otherwise be most of every list — and typing is what reaches them now that Options › Models
    // has no per-provider id box. They come under their own rule, and Enter uses one.
    void theAllTabKeepsTheLongTailBehindTyping() {
        ModelPicker::Context ctx = tailContext();
        ModelPicker picker(ctx);
        const QStringList untyped = rowKeys(picker.list());
        QVERIFY2(!untyped.contains(QStringLiteral("openrouter|meta/muse-spark-1.3")), qPrintable(untyped.join(QLatin1Char(' '))));
        QVERIFY(untyped.contains(QStringLiteral("glm-coding|glm-5.3")));
        picker.filter()->setText(QStringLiteral("muse-spark"));
        const QStringList rows = keys(picker.list());
        QVERIFY2(rows.contains(QStringLiteral("[more from openrouter]")), qPrintable(rows.join(QLatin1Char(' '))));
        QVERIFY(rows.contains(QStringLiteral("openrouter|meta/muse-spark-1.3")));
        picker.selectKey(QStringLiteral("openrouter|meta/muse-spark-1.3"));
        picker.use();
        QCOMPARE(picker.pick().key, QStringLiteral("openrouter|meta/muse-spark-1.3"));
    }

    // The same on a tier tab: the tail sits below "not in this list" under its own rule, and
    // ctrl+enter puts one in the list — which is also what makes it a listed model from then on.
    void aTierTabsFilterReachesTheTailAndCtrlEnterAddsIt() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        ModelPicker::Context ctx = tailContext();
        ctx.tier = QStringLiteral("main");
        ModelPicker picker(ctx);
        picker.filter()->setText(QStringLiteral("muse-spark"));
        const QStringList rows = keys(picker.list());
        QVERIFY2(rows.contains(QStringLiteral("[more from openrouter]")), qPrintable(rows.join(QLatin1Char(' '))));
        picker.selectKey(QStringLiteral("openrouter|meta/muse-spark-1.3"));
        QTest::keyClick(picker.filter(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(listKeys(QStringLiteral("main")),
                 (QStringList{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("openrouter|meta/muse-spark-1.3")}));
        // A tier list names it now, so it is a listed model everywhere, with no typing.
        ModelPicker again(tailContext());
        QVERIFY(rowKeys(again.list()).contains(QStringLiteral("openrouter|meta/muse-spark-1.3")));
    }

    // "+ add a model by id…" is the last row of the `all` tab: the input that sat under every
    // open-ended provider on Options › Models. An id the provider serves but does not list is
    // remembered in `models/custom` and is an entry like any other from then on.
    void addAModelByIdIsTheLastRowOfTheAllTab() {
        ModelPicker picker(tailContext());
        const QStringList rows = keys(picker.list());
        QCOMPARE(rows.last(), QStringLiteral("[+ add a model by id…]"));
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        const QString key = picker.addModelById(QStringLiteral("openrouter"), QStringLiteral("  moonshotai/kimi-k3  "));
        QCOMPARE(key, QStringLiteral("openrouter|moonshotai/kimi-k3"));
        QVERIFY(curation::customKeys().contains(key));
        QCOMPARE(told, 1);
        QVERIFY(rowKeys(picker.list()).contains(key));
        // It survives the dialog: a fresh one lists it without typing, because it is yours.
        ModelPicker again(tailContext());
        QVERIFY(rowKeys(again.list()).contains(key));
        // An id the catalog already holds needs nothing stored — it is just selected.
        const QString had = picker.addModelById(QStringLiteral("openrouter"), QStringLiteral("meta/muse-spark-1.3"));
        QCOMPARE(had, QStringLiteral("openrouter|meta/muse-spark-1.3"));
        QVERIFY(!curation::customKeys().contains(had));
        QCOMPARE(picker.selectedKey(), had);
        // Nothing typed is nothing added.
        QVERIFY(picker.addModelById(QStringLiteral("openrouter"), QStringLiteral("   ")).isEmpty());
    }

    // ----- step 2: the availability column (owner, 2026-09-21; design 5.7) -------------------
    // "there need to be 4 steps of model availability: 1 add provider, 2 add model as available,
    // 3 add model to priority list, 4 include model in box picker." Step 2 is edited here, and
    // only here: a tier tab is step 3 and its "in box" column is step 4.

    void theAllTabHasAnAvailabilityColumnAndATierTabDoesNot() {
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        QVERIFY(!picker.list()->isColumnHidden(ColAvail));
        QVERIFY(picker.list()->isColumnHidden(ColBox));
        // Every model of a branded provider is available to begin with, so every row is ticked.
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(picker.list()->currentItem()->checkState(ColAvail), Qt::Checked);
        QVERIFY(picker.list()->currentItem()->toolTip(ColAvail).contains(QStringLiteral("Available")));
        QVERIFY(picker.footer()->text().contains(QStringLiteral("available")));
        // …and the column belongs to this tab alone.
        picker.setTier(QStringLiteral("main"));
        QVERIFY(picker.list()->isColumnHidden(ColAvail));
    }

    void unTickingAModelGreysItAndTakesItOutOfTheListsAndTheBox() {
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        int told = 0;
        picker.onListsChanged = [&told] { ++told; };
        const QString flash = QStringLiteral("glm-coding|glm-5.3-flash");
        picker.selectKey(flash);
        picker.list()->currentItem()->setCheckState(ColAvail, Qt::Unchecked);
        QCOMPARE(told, 1);
        QVERIFY(!curation::isAvailable(*ctx.catalog.find(flash)));
        bool inShown = false;
        for (const Entry &entry : shown(ctx.catalog)) inShown = inShown || entry.key == flash;
        QVERIFY(!inShown);
        // The row stays where it was, greyed, with an empty box to tick again.
        QTreeWidgetItem *row = picker.list()->currentItem();
        QCOMPARE(row->data(0, Qt::UserRole).toString(), flash);
        QCOMPARE(row->checkState(ColAvail), Qt::Unchecked);
        QCOMPARE(row->foreground(ColModel).color(), picker.palette().color(QPalette::Disabled, QPalette::Text));
        QVERIFY(row->toolTip(ColAvail).contains(QStringLiteral("Not available")));
        // A fresh dialog still draws it, still un-ticked: this is where it is ticked back.
        ModelPicker again(ctx);
        QVERIFY(rowKeys(again.list()).contains(flash));
        again.selectKey(flash);
        QCOMPARE(again.list()->currentItem()->checkState(ColAvail), Qt::Unchecked);
        again.list()->currentItem()->setCheckState(ColAvail, Qt::Checked);
        QVERIFY(curation::isAvailable(*ctx.catalog.find(flash)));
    }

    // The other half of the owner's rule: "for openrouter, you have to select specific models".
    // A tail row appears under "more from openrouter" when typed, un-ticked; ticking it is what
    // selects it, and it is a listed model from then on with no typing at all.
    void tickingATailRowUnderMoreFromOpenrouterSelectsIt() {
        ModelPicker picker(tailContext());
        const QString tail = QStringLiteral("openrouter|meta/muse-spark-1.3");
        QVERIFY(!rowKeys(picker.list()).contains(tail));
        picker.filter()->setText(QStringLiteral("muse-spark"));
        picker.selectKey(tail);
        QTreeWidgetItem *row = picker.list()->currentItem();
        QCOMPARE(row->checkState(ColAvail), Qt::Unchecked);
        row->setCheckState(ColAvail, Qt::Checked);
        const Catalog catalog = tailContext().catalog;
        QVERIFY(curation::isAvailable(*catalog.find(tail)));
        ModelPicker again(tailContext());
        QVERIFY(rowKeys(again.list()).contains(tail));
        // Its neighbours in the listing are not dragged in with it.
        QVERIFY(!rowKeys(again.list()).contains(QStringLiteral("openrouter|vendor/tail-3")));
    }

    // A model one of the **terminal** lists names is available whatever the box says: a rank the
    // user wrote down that the box would not offer is a list that lies, and the tooltip says so.
    void aModelATerminalListNamesStaysTicked() {
        const QString flash = QStringLiteral("glm-coding|glm-5.3-flash");
        setList(QStringLiteral("flash"), {{flash, QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        picker.selectKey(flash);
        QTreeWidgetItem *row = picker.list()->currentItem();
        QVERIFY(row->toolTip(ColAvail).contains(QStringLiteral("high, main, flash or local list names it")));
        row->setCheckState(ColAvail, Qt::Unchecked);
        // The setting took the un-tick; the list overrules it, and the box goes straight back on.
        QVERIFY(curation::isAvailable(*ctx.catalog.find(flash)));
        QCOMPARE(row->checkState(ColAvail), Qt::Checked);
    }

    // …and the **lite** list does not pin (card #MDL1, owner 2026-09-21: "it seems like i cant
    // disable gemini flash lite … this tab is only for terminal agents"). Lite is not a pane mode,
    // so the tick stays the user's and the un-tick sticks.
    void theLiteListDoesNotPinTheTick() {
        const QString flash = QStringLiteral("glm-coding|glm-5.3-flash");
        setList(QStringLiteral("lite"), {{flash, QString()}});
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("all");
        ModelPicker picker(ctx);
        picker.selectKey(flash);
        QTreeWidgetItem *row = picker.list()->currentItem();
        QCOMPARE(row->checkState(ColAvail), Qt::Checked);
        QVERIFY(!row->toolTip(ColAvail).contains(QStringLiteral("high, main, flash or local list names it")));
        row->setCheckState(ColAvail, Qt::Unchecked);
        QVERIFY(!curation::isAvailable(*ctx.catalog.find(flash)));
        QCOMPARE(row->checkState(ColAvail), Qt::Unchecked);
        // The lite list is untouched: the chores go on running it.
        QCOMPARE(curation::tierList(QStringLiteral("lite")).size(), 1);
    }

    // Options › Models' per-provider "models… (N of M available)" link: the `all` tab, opened
    // with that provider's name already typed, so step 2 is one click from step 1.
    void theDialogCanOpenFilteredToOneProvider() {
        ModelPicker::Context ctx = context();
        ctx.tier = QStringLiteral("all");
        ctx.filter = QStringLiteral("z.ai (glm)");
        ModelPicker picker(ctx);
        QCOMPARE(picker.filter()->text(), QStringLiteral("z.ai (glm)"));
        QCOMPARE(rowKeys(picker.list()), (QStringList{QStringLiteral("glm-coding|glm-5.3"),
                                                      QStringLiteral("glm-coding|glm-5.3-flash")}));
    }

    // "customize…" used to close the modal and open Options › Models. There is no modal to close:
    // it asks the host, which is the models pane switching to its providers tab.
    void customizeAsksTheHostForTheProvidersPage() {
        ModelPicker picker(context());
        bool opened = false;
        picker.openModelsPage = [&] { opened = true; };
        picker.findChild<QPushButton *>(QStringLiteral("modelCustomize"))->click();
        QVERIFY(opened);
        QVERIFY(!picker.pick().accepted);   // it is a door, not a pick
    }

    // Hosted in the models pane there is no tab row at all: the flat tab is the host's own
    // **available** and the classes are sections, not tabs. ←/→ and ctrl+tab both go to the host.
    void hostedTheTabRowGoesAndTheArrowsAreTheHosts() {
        ModelPicker picker(context());
        picker.setHosted(true);
        QVERIFY(tabs(picker.tabBar()).isEmpty());
        QVERIFY(!picker.tabBar()->isVisibleTo(&picker));
        QVERIFY(picker.tabIds().contains(QStringLiteral("all")));   // still a tier it can be put on
        const QString was = picker.tier();
        QTest::keyClick(picker.filter(), Qt::Key_Right);
        QCOMPARE(picker.tier(), was);                               // left for the host
        QTest::keyClick(picker.list(), Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(picker.tier(), was);
        picker.setTier(QStringLiteral("all"));
        QCOMPARE(picker.tier(), QStringLiteral("all"));
        picker.setTier(ModelPicker::classesTier());
        QCOMPARE(picker.tier(), QStringLiteral("classes"));
        QVERIFY(!picker.tabBar()->isVisibleTo(&picker));
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("relay-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("modelpicker"));
    ModelPickerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "modelpicker_test.moc"
