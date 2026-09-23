// SPDX-License-Identifier: AGPL-3.0-or-later
// The models pane (Ctrl+Shift+M, card #MDL1 t:a11, design 5.8): the three tabs — providers,
// available, priorities — the class tab it opens on, Enter landing in the pane it *serves* rather
// than in the pane itself, Escape handing the focus back without closing anything, and the two
// re-hosted surfaces still being the surfaces they were.
//
// The pane it serves is a `std::function`, so nothing here needs a `Pane`, a `RelayWindow` or a
// worker: the pane's whole contract with the app is `ModelsPane::Target`.
//
// **Ported from tests/modelpicker_test.cpp** (which keeps the widget's own tests): "the tabs are
// the lists and it opens on the pane's own", the availability column, reorder/delete/undo, and
// what a pick hands back. **Not ported**, because the modal they were about is gone: nothing here
// calls `exec()`, there is no "cancel" and no `reject()`, and "customize…" no longer closes a
// dialog — `customizeAsksTheHostForTheProvidersPage` in the widget's own test is what is left of
// it, and `providersIsWhereCustomizeGoes` below is this pane's half.
#include "ModelsPane.h"
#include "PaneTabNavigation.h"

#include <QApplication>
#include <QCheckBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QRegularExpression>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

#include <functional>

using namespace relay;
using namespace relay::models;

namespace {

// The picker's columns, as ModelPicker.cpp orders them (ColMove is a listed row's ▲▼, #RKP3).
enum Column { ColRank, ColMove, ColAvail, ColBox, ColModel, ColVia, ColReasoning, ColIntelligence, ColSpeed, ColLeft };

QJsonObject model(const QString &id, const QString &label, const QString &tier, const QStringList &efforts, int intelligence = -1) {
    QJsonObject row{{QStringLiteral("id"), id}, {QStringLiteral("label"), label}, {QStringLiteral("tier"), tier},
                    {QStringLiteral("efforts"), QJsonArray::fromStringList(efforts)}};
    if (intelligence >= 0) row.insert(QStringLiteral("intelligence"), intelligence);
    return row;
}

QJsonArray presets() {
    QJsonArray out;
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("glm-coding")},
                       {QStringLiteral("label"), QStringLiteral("z.ai · glm-5.3 · coding plan")},
                       {QStringLiteral("provider"), QStringLiteral("z.ai (glm)")},
                       {QStringLiteral("plan"), QStringLiteral("coding plan")},
                       {QStringLiteral("model"), QStringLiteral("glm-5.3")}, {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("glm-5.3"), QStringLiteral("glm-5.3"), QStringLiteral("main"),
                                  {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 45),
                            model(QStringLiteral("glm-5.3-flash"), QStringLiteral("glm-5.3 flash"), QStringLiteral("flash"),
                                  {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}, 30)}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("anthropic")},
                       {QStringLiteral("label"), QStringLiteral("anthropic · claude opus 5.5")},
                       {QStringLiteral("provider"), QStringLiteral("anthropic (claude)")},
                       {QStringLiteral("plan"), QStringLiteral("pay-as-you-go")},
                       {QStringLiteral("model"), QStringLiteral("claude-opus-5-5")}, {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("claude-opus-5-5"), QStringLiteral("claude opus 5.5"), QStringLiteral("main"), {}, 51)}}};
    return out;
}

// The section the providers tab draws. The real one is RelayWindow::modelsSection(); what the pane
// owes it is a host, which is what this checks.
std::function<QList<SettingsSection>()> providerSections() {
    return [] {
        SettingsSection models;
        models.id = QStringLiteral("models");
        models.title = QStringLiteral("Models");
        SettingRow key;
        key.kind = SettingRow::Button;
        key.id = QStringLiteral("models.key:glm-coding");
        key.label = QStringLiteral("z.ai (glm)");
        key.buttonText = QStringLiteral("add key…");
        key.run = [] {};
        models.rows << key;
        return QList<SettingsSection>{models};
    };
}

QStringList tabs(QTabBar *bar) {
    QStringList out;
    for (int i = 0; i < bar->count(); ++i) out << bar->tabData(i).toString();
    return out;
}

// The rows drawn, without a class's "+ add" pool (#AVR8) — `poolKeys` reads that.
QStringList rowKeys(QTreeWidget *list) {
    QStringList out;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list->topLevelItem(i);
        if (item->isHidden() || item->data(0, Qt::UserRole + 4).toBool()) continue;
        const QString key = item->data(0, Qt::UserRole).toString();
        if (!key.isEmpty()) out << key;
    }
    return out;
}

// Every available model a class may hold and does not rank, under its "+ add" rule.
QStringList poolKeys(QTreeWidget *list, const QString &tier = QString()) {
    QStringList out;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list->topLevelItem(i);
        if (!item->data(0, Qt::UserRole + 4).toBool()) continue;
        if (!tier.isEmpty() && item->data(0, Qt::UserRole + 8).toString() != tier) continue;
        out << item->data(0, Qt::UserRole).toString();
    }
    return out;
}

QStringList listKeys(const QString &tier) {
    QStringList out;
    for (const curation::TierEntry &entry : curation::tierList(tier)) out << entry.key;
    return out;
}

void setList(const QString &tier, const QList<curation::TierEntry> &entries) { curation::setTierList(tier, entries); }

// What a served pane hands over, with the switch recorded rather than made.
struct Served {
    QString key, effort;
    int uses = 0;
    int focusBacks = 0;
    int listEdits = 0;
};

ModelsPane::Target targetFor(Served *served, const QString &tier = QStringLiteral("main"),
                             const QString &title = QStringLiteral("relay-terminal")) {
    ModelsPane::Target target;
    target.title = title;
    target.token = QStringLiteral("pane-1");
    target.catalog = catalogFrom(presets());
    target.currentKey = QStringLiteral("glm-coding|glm-5.3");
    target.currentEffort = QStringLiteral("high");
    target.tier = tier;
    target.now = 1;
    target.use = [served](const QString &key, const QString &effort) {
        served->key = key;
        served->effort = effort;
        ++served->uses;
    };
    target.listsChanged = [served] { ++served->listEdits; };
    target.focusBack = [served] { ++served->focusBacks; };
    return target;
}

}  // namespace

class ModelsPaneTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void init() {
        QSettings().clear();
        QSettings().setValue(QStringLiteral("models/priority"),
                             QStringList{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("glm-coding|glm-5.3-flash"),
                                         QStringLiteral("anthropic|claude-opus-5-5")});
    }

    // ----- the four tabs --------------------------------------------------------------------

    void theFourTabsAreTheStepsOfAvailabilityAndThenTheJobs() {
        ModelsPane pane(providerSections());
        QCOMPARE(tabs(pane.tabBar()), (QStringList{QStringLiteral("providers"), QStringLiteral("available"),
                                                   QStringLiteral("priorities"), QStringLiteral("jobs")}));
        QVERIFY(pane.providers() != nullptr);
        QVERIFY(pane.picker() != nullptr);
        // The providers tab is Options' own renderer, drawing the section it was handed.
        pane.showTab(ModelsPane::providersTab());
        QCOMPARE(pane.currentTab(), QStringLiteral("providers"));
        QVERIFY(pane.providers()->visibleRowIds().contains(QStringLiteral("models.key:glm-coding")));
        // …with its own tab row and footer out of the way: this pane draws those.
        QVERIFY(pane.providers()->embedded());
    }

    void theHeaderNamesThePaneItServes() {
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served, QStringLiteral("main"), QStringLiteral("~/src/relay")));
        QCOMPARE(pane.header()->text(), QStringLiteral("for: ~/src/relay"));
        QCOMPARE(pane.servedTitle(), QStringLiteral("~/src/relay"));
    }

    // "tab = the served pane's mode (tier) on priorities" (design 5.8) — which, since the page
    // became one set of sections, is where the **highlight** lands (owner, 2026-09-21: "in a pane,
    // i dont want separate tabs for the modes. they should just be in divided sections").
    void prioritiesOpensOnTheServedPanesOwnClass() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        setList(QStringLiteral("flash"), {{QStringLiteral("glm-coding|glm-5.3-flash"), QStringLiteral("low")}});
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served, QStringLiteral("flash")));   // the pane is on /flash
        pane.showTab(ModelsPane::prioritiesTab());
        QCOMPARE(pane.tier(), QStringLiteral("flash"));
        QCOMPARE(pane.picker()->selectedKey(), QStringLiteral("glm-coding|glm-5.3-flash"));
        // Every class is on the one page — both lists are drawn, main above flash — and there is
        // no second row of tabs above them.
        QCOMPARE(rowKeys(pane.picker()->list()), (QStringList{QStringLiteral("glm-coding|glm-5.3"),
                                                              QStringLiteral("glm-coding|glm-5.3-flash")}));
        QVERIFY(tabs(pane.picker()->tabBar()).isEmpty());
        QVERIFY(!pane.picker()->tabBar()->isVisibleTo(pane.picker()));
    }

    void availableIsTheFlatTabAndKeepsTheTickColumn() {
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::availableTab());
        QCOMPARE(pane.tier(), QStringLiteral("all"));
        QTreeWidget *list = pane.picker()->list();
        QVERIFY(!list->isColumnHidden(ColAvail));
        QCOMPARE(list->headerItem()->text(ColAvail), QStringLiteral("available"));
        QTreeWidgetItem *row = nullptr;
        for (int i = 0; i < list->topLevelItemCount() && !row; ++i)
            if (list->topLevelItem(i)->data(0, Qt::UserRole).toString().startsWith(QStringLiteral("glm-coding|glm-5.3")))
                row = list->topLevelItem(i);
        QVERIFY(row != nullptr);
        QCOMPARE(row->checkState(ColAvail), Qt::Checked);
        // The favorites button and the sort menu came with the flat tab (design 5.8).
        QVERIFY(pane.picker()->sortBox() != nullptr);
        QVERIFY(pane.picker()->findChild<QPushButton *>(QStringLiteral("modelFavorite")) != nullptr);
        // Going back to priorities returns to the class that was being looked at.
        pane.showTab(ModelsPane::prioritiesTab());
        QCOMPARE(pane.tier(), QStringLiteral("main"));
    }

    // Alt+digits are reserved for future use, even when there is no window shortcut handler.
    void altDigitsAreUnclaimedAndArrowsStillWalkTabs() {
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::providersTab());
        for (int key = Qt::Key_0; key <= Qt::Key_9; ++key) {
            QKeyEvent digit(QEvent::KeyPress, key, Qt::AltModifier);
            QCoreApplication::sendEvent(&pane, &digit);
            QVERIFY(!digit.isAccepted());
            QCOMPARE(pane.currentTab(), ModelsPane::providersTab());
        }
        for (auto *shortcut : pane.findChildren<QShortcut *>()) {
            const QString key = shortcut->key().toString(QKeySequence::PortableText);
            QVERIFY2(!QRegularExpression(QStringLiteral("^Alt\\+[0-9]$")).match(key).hasMatch(), qPrintable(key));
        }
        // Ctrl+Tab is left alone wherever it is pressed.
        QTest::keyClick(&pane, Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(pane.currentTab(), QStringLiteral("providers"));
        // ←/→ walk them too, and they keep walking: the priorities page has no class tabs left to
        // take the arrows for itself.
        pane.showTab(ModelsPane::availableTab());
        QTest::keyClick(pane.picker()->filter(), Qt::Key_Right);
        QCOMPARE(pane.currentTab(), QStringLiteral("priorities"));
        QTest::keyClick(pane.picker()->filter(), Qt::Key_Right);
        QCOMPARE(pane.currentTab(), ModelsPane::jobsTab());
    }

    void sharedTabNavigationUsesTheModelsBar() {
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::prioritiesTab());
        pane.resize(1000, 700);
        pane.show();
        QKeyEvent next(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QVERIFY(relay::paneTabs::handle(pane.picker()->filter(), &next));
        QCOMPARE(pane.currentTab(), ModelsPane::jobsTab());
        QKeyEvent previous(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        QVERIFY(relay::paneTabs::handle(pane.jobs()->list(), &previous));
        QCOMPARE(pane.currentTab(), ModelsPane::prioritiesTab());
    }

    // "fill from defaults" is two buttons the picker makes in its constructor, so a target that
    // gains the action has to rebuild it rather than re-read. A models pane restored with a saved
    // layout is exactly that case: it is pointed at a pane whose worker has not answered yet.
    void gainingFillFromDefaultsBringsItsButtons() {
        Served served;
        ModelsPane pane(providerSections());
        ModelsPane::Target early = targetFor(&served);   // no worker answer yet: no action
        pane.setTarget(early);
        pane.showTab(ModelsPane::prioritiesTab());
        QVERIFY(pane.picker()->defaultsButton(false) == nullptr);
        ModelsPane::Target ready = targetFor(&served);
        ready.fillFromDefaults = [](bool) { return true; };
        pane.setTarget(ready);
        QVERIFY(pane.picker()->defaultsButton(false) != nullptr);
        QVERIFY(pane.picker()->defaultsButton(true) != nullptr);
        QCOMPARE(pane.currentTab(), QStringLiteral("priorities"));
    }

    // A pane's catalog arrives late: with no providers there is nothing to draw until the worker
    // answers `presets`, and a re-read of the same pane has to bring the new rows with it.
    void aLateCatalogReachesTheRowsWithoutLosingTheTab() {
        Served served;
        ModelsPane pane(providerSections());
        ModelsPane::Target empty = targetFor(&served);
        empty.catalog = catalogFrom(QJsonArray());       // a first run: no providers at all
        empty.currentKey.clear();
        pane.setTarget(empty);
        pane.showTab(ModelsPane::availableTab());        // the tab that draws the catalog alone
        QVERIFY(rowKeys(pane.picker()->list()).isEmpty());
        pane.setTarget(targetFor(&served));              // …and then the worker answers
        QCOMPARE(pane.currentTab(), QStringLiteral("available"));
        QVERIFY(rowKeys(pane.picker()->list()).contains(QStringLiteral("glm-coding|glm-5.3")));
    }

    // A catalog that arrives late is searchable on **available**, where the whole catalog is
    // searched; ticking a model there puts it in every class's "+ add" pool on priorities (owner,
    // 2026-09-22: "i thought all available models would be in priority, so searching should
    // happen in the available pane").
    void lateProviderCatalogBecomesSearchableInAvailableAndJoinsPriorities() {
        Served served;
        ModelsPane pane(providerSections());
        auto target = targetFor(&served);
        pane.setTarget(target);
        pane.showTab(ModelsPane::availableTab());
        pane.picker()->filter()->setText(QStringLiteral("openrouter"));
        QVERIFY(rowKeys(pane.picker()->list()).isEmpty());
        QJsonArray fresh = presets();
        fresh << QJsonObject{{"id", "openrouter"}, {"provider", "openrouter"},
                            {"has_stored_key", true}, {"models", QJsonArray{
                                model("vendor/new-model", "new-model", "", {})}}};
        target.catalog = catalogFrom(fresh);
        pane.setTarget(target);
        QCOMPARE(pane.currentTab(), QStringLiteral("available"));
        QCOMPARE(pane.picker()->filter()->text(), QStringLiteral("openrouter"));
        QVERIFY(rowKeys(pane.picker()->list()).contains(QStringLiteral("openrouter|vendor/new-model")));
        // A provider listing one model is not open-ended, so that model is available by default
        // and priorities offers it in main's pool at once; ctrl+enter ranks it.
        pane.showTab(ModelsPane::prioritiesTab());
        QVERIFY(poolKeys(pane.picker()->list(), QStringLiteral("main")).contains(QStringLiteral("openrouter|vendor/new-model")));
        pane.picker()->filter()->setText(QStringLiteral("new-model"));
        pane.picker()->focusClass(QStringLiteral("main"));
        pane.picker()->addSelected();
        QVERIFY(listKeys(QStringLiteral("main")).contains(QStringLiteral("openrouter|vendor/new-model")));
        if (const QString path = qEnvironmentVariable("RELAY_VPR7_SCREENSHOT"); !path.isEmpty()) {
            pane.resize(1100, 720); pane.show(); QTest::qWait(80);
            QVERIFY(pane.grab().save(path));
        }
    }

    void codexSearchAddsToMainAndHighWithoutChangingGuestEligibility() {
        Served served;
        ModelsPane pane(providerSections());
        auto target = targetFor(&served);
        QJsonArray fresh = presets();
        // Match the worker payload: guest rows have a label but no provider field.
        fresh << QJsonObject{{"id", "guest:codex"}, {"label", "Codex"}, {"harness", true},
                            {"models", QJsonArray{model("gpt-6-astra", "gpt-6-astra", "", {"low", "high"})}}};
        target.catalog = catalogFrom(fresh);
        pane.setTarget(target);
        pane.showTab(ModelsPane::prioritiesTab());
        // Ranked nowhere; offered, untyped, where a harness can be a whole agent — main and high.
        QVERIFY(!rowKeys(pane.picker()->list()).contains(QStringLiteral("guest:codex|gpt-6-astra")));
        QCOMPARE(poolKeys(pane.picker()->list()).count(QStringLiteral("guest:codex|gpt-6-astra")), 2);
        pane.picker()->filter()->setText(QStringLiteral("codex"));
        QCOMPARE(poolKeys(pane.picker()->list()).count(QStringLiteral("guest:codex|gpt-6-astra")), 2);
        for (const QString &tier : {QStringLiteral("main"), QStringLiteral("high")}) {
            pane.picker()->filter()->setText(QStringLiteral("codex"));
            pane.picker()->focusClass(tier);
            QTest::keyClick(pane.picker()->filter(), Qt::Key_Return, Qt::ControlModifier);
            QVERIFY(listKeys(tier).contains(QStringLiteral("guest:codex|gpt-6-astra")));
        }
        QVERIFY(listKeys(QStringLiteral("flash")).isEmpty());
        QVERIFY(listKeys(QStringLiteral("lite")).isEmpty());
        if (const QString path = qEnvironmentVariable("RELAY_CDP7_SCREENSHOT"); !path.isEmpty()) {
            pane.resize(1100, 720); pane.show(); QTest::qWait(80);
            QVERIFY(pane.grab().save(path));
        }
    }

    void providersIsWhereCustomizeGoes() {
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::prioritiesTab());
        pane.picker()->findChild<QPushButton *>(QStringLiteral("modelCustomize"))->click();
        QCOMPARE(pane.currentTab(), QStringLiteral("providers"));
    }

    // ----- what Enter does, and where ---------------------------------------------------------

    void enterUsesTheRowInTheServedPane() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max")},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::prioritiesTab());
        pane.picker()->selectKey(QStringLiteral("anthropic|claude-opus-5-5"));
        QTest::keyClick(pane.picker()->list(), Qt::Key_Return);
        QCOMPARE(served.uses, 1);
        QCOMPARE(served.key, QStringLiteral("anthropic|claude-opus-5-5"));
        // The "use" button is the same door, and so is Enter in the filter line.
        pane.picker()->selectKey(QStringLiteral("glm-coding|glm-5.3"));
        QTest::keyClick(pane.picker()->filter(), Qt::Key_Return);
        QCOMPARE(served.uses, 2);
        QCOMPARE(served.key, QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(served.effort, QStringLiteral("max"));   // the level the list carries, with it
    }

    void aClickOnlyHighlights() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::prioritiesTab());
        QTreeWidget *list = pane.picker()->list();
        QTreeWidgetItem *opus = nullptr;
        for (int i = 0; i < list->topLevelItemCount() && !opus; ++i)
            if (list->topLevelItem(i)->data(0, Qt::UserRole).toString() == QStringLiteral("anthropic|claude-opus-5-5"))
                opus = list->topLevelItem(i);
        QVERIFY(opus != nullptr);
        emit list->itemClicked(opus, ColModel);
        list->setCurrentItem(opus);
        QCOMPARE(served.uses, 0);
        QCOMPARE(pane.picker()->selectedKey(), QStringLiteral("anthropic|claude-opus-5-5"));
    }

    // "Escape returns focus to the pane it serves and leaves it open" (design 5.8).
    void escapeHandsTheFocusBackAndLeavesThePaneOpen() {
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::prioritiesTab());
        QTest::keyClick(pane.picker()->filter(), Qt::Key_Escape);
        QCOMPARE(served.focusBacks, 1);
        QCOMPARE(served.uses, 0);
        QVERIFY(pane.picker() != nullptr);              // nothing closed, nothing rebuilt
        QCOMPARE(pane.currentTab(), QStringLiteral("priorities"));
        // …from the providers tab too, where the settings pane's own Esc is the same exit.
        pane.showTab(ModelsPane::providersTab());
        pane.providers()->onClose();
        QCOMPARE(served.focusBacks, 2);
    }

    // ----- the priorities tab is still the list ------------------------------------------------

    void prioritiesKeepsReorderDeleteAndUndo() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("anthropic|claude-opus-5-5"), QString()}});
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::prioritiesTab());
        ModelPicker *picker = pane.picker();
        picker->selectKey(QStringLiteral("anthropic|claude-opus-5-5"));
        QTest::keyClick(picker->list(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("anthropic|claude-opus-5-5"),
                                                                QStringLiteral("glm-coding|glm-5.3")}));
        QTest::keyClick(picker->list(), Qt::Key_Delete);
        QCOMPARE(listKeys(QStringLiteral("main")), QStringList{QStringLiteral("glm-coding|glm-5.3")});
        QTest::keyClick(picker->list(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("anthropic|claude-opus-5-5"),
                                                                QStringLiteral("glm-coding|glm-5.3")}));
        // Every one of those is a list edit the window has to hear about.
        QVERIFY(served.listEdits >= 3);
        QCOMPARE(served.uses, 0);   // and none of them switched the served pane
    }

    // ----- serving, re-serving and re-reading ---------------------------------------------------

    void aNewTokenRetargetsAndTheSameTokenOnlyReReads() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        setList(QStringLiteral("high"), {{QStringLiteral("glm-coding|glm-5.3"), QString()}});
        Served first, second;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&first, QStringLiteral("main"), QStringLiteral("pane one")));
        pane.showTab(ModelsPane::prioritiesTab());
        QCOMPARE(pane.tier(), QStringLiteral("main"));
        // The same pane again, with a fresh catalog: the class being looked at is kept.
        pane.picker()->focusClass(QStringLiteral("flash"));
        ModelsPane::Target again = targetFor(&first, QStringLiteral("main"), QStringLiteral("pane one"));
        pane.setTarget(again);
        QCOMPARE(pane.tier(), QStringLiteral("flash"));
        // Another pane: it is served instead, on *its* class, and Enter now lands there.
        ModelsPane::Target other = targetFor(&second, QStringLiteral("high"), QStringLiteral("pane two"));
        other.token = QStringLiteral("pane-2");
        pane.setTarget(other);
        QCOMPARE(pane.servedToken(), QStringLiteral("pane-2"));
        QCOMPARE(pane.header()->text(), QStringLiteral("for: pane two"));
        QCOMPARE(pane.tier(), QStringLiteral("high"));
        pane.picker()->selectKey(QStringLiteral("glm-coding|glm-5.3"));
        pane.picker()->use();
        QCOMPARE(first.uses, 0);
        QCOMPARE(second.uses, 1);
    }

    // ----- the fourth tab: what each job runs on (card #MDL1, design 5.9) ---------------------

    // The jobs tab is handed the worker's report rather than state of its own;
    // `Target::roleSummary` is the "runs on" column.
    void jobsTabDrawsTheWorkersReport() {
        Served served;
        ModelsPane pane(providerSections());
        ModelsPane::Target target = targetFor(&served);
        target.roleSummary = QJsonObject{
            {QStringLiteral("summaries"), QJsonObject{{QStringLiteral("preset"), QStringLiteral("glm-coding")},
                                                      {QStringLiteral("model"), QStringLiteral("glm-5.3-flash")},
                                                      {QStringLiteral("effort"), QStringLiteral("low")}}}};
        target.rolesChanged = [&served] { ++served.listEdits; };
        pane.setTarget(target);
        pane.showTab(ModelsPane::jobsTab());
        pane.focusFilter();
        QCOMPARE(pane.currentTab(), ModelsPane::jobsTab());
        QVERIFY(pane.jobs() != nullptr);
        QCOMPARE(pane.jobs()->runsOn(QStringLiteral("summaries")), QStringLiteral("glm-5.3-flash · low"));
        // Focusing the jobs page selects its main role.
        QCOMPARE(pane.jobs()->currentRole(), QStringLiteral("main"));
    }

    // An override written on the tab reaches the served pane at once, the same live update a
    // tier-list edit makes.
    void anOverrideOnTheJobsTabTellsTheServedPane() {
        Served served;
        int resends = 0;
        ModelsPane pane(providerSections());
        ModelsPane::Target target = targetFor(&served);
        target.rolesChanged = [&resends] { ++resends; };
        pane.setTarget(target);
        pane.showTab(ModelsPane::jobsTab());
        QVERIFY(pane.jobs()->selectRole(QStringLiteral("subagent")));
        QVERIFY(rolestore::setOverride(QStringLiteral("subagent"), QStringLiteral("anthropic|claude-opus-5-5"),
                                       QString()));
        pane.setTarget(target);   // a worker report, or any re-read
        QCOMPARE(pane.jobs()->overrideText(QStringLiteral("subagent")), QStringLiteral("claude-opus-5-5"));
        QVERIFY(pane.jobs()->clearOverride());
        QCOMPARE(resends, 1);
        QCOMPARE(pane.jobs()->overrideText(QStringLiteral("subagent")), QStringLiteral("follows main"));
    }

    // ----- the helper agent (owner, 2026-09-22: "there needs to be a helper agent on the model
    // page") ------------------------------------------------------------------------------------

    void theHelperIsOneRowUnderAllFourTabsAndBuildsOneConsole() {
        // With no factory — a pane with no window, which is every other test here — there is no
        // row at all: an ask row that does nothing is worse than none.
        ModelsPane unwired(providerSections());
        unwired.resize(700, 600);
        unwired.show();
        QVERIFY(QTest::qWaitForWindowExposed(&unwired));
        auto *unwiredPanel = unwired.findChild<QWidget *>(QStringLiteral("boardChatPanel"), Qt::FindDirectChildrenOnly);
        QVERIFY(unwiredPanel);
        QVERIFY(!unwiredPanel->isVisible());
        QVERIFY(!unwired.agentConsole());

        int builds = 0, focused = 0;
        relay::agent::Context *built = nullptr;
        QWidget *console = nullptr;
        ModelsPane pane(providerSections());
        pane.onCreateConsole = [&](relay::agent::Context *context, QWidget *parent) {
            ++builds;
            built = context;
            console = new QWidget(parent);
            relay::agent::ConsoleHandle handle;
            handle.widget = console;
            handle.focusComposer = [&] { ++focused; };
            return handle;
        };
        pane.setHelperTabId(QStringLiteral("tab-7"));
        pane.setHelperShortcut(QStringLiteral("models.ask"), QStringLiteral("Alt+Q"));
        Served served;
        pane.setTarget(targetFor(&served, QStringLiteral("flash")));
        pane.resize(700, 600);
        pane.show();
        QVERIFY(QTest::qWaitForWindowExposed(&pane));

        auto *panel = pane.findChild<QWidget *>(QStringLiteral("boardChatPanel"), Qt::FindDirectChildrenOnly);
        QVERIFY(panel);
        auto *ask = panel->findChild<QToolButton *>(QStringLiteral("boardChatAsk"));
        auto *body = panel->findChild<QWidget *>(QStringLiteral("boardChatBody"));
        QVERIFY(ask && body);
        QCOMPARE(ask->text(), QStringLiteral("Helper Agent (Alt+Q)"));
        QCOMPARE(panel->findChild<QLabel *>(QStringLiteral("boardChatHead"))->text(), QStringLiteral("Models helper"));

        // One row on every tab — the pane's own, under the tabs — and the providers tab's embedded
        // Options renderer has none of its own: one helper per pane.
        const auto visibleAskRows = [&pane] {
            int n = 0;
            for (QToolButton *button : pane.findChildren<QToolButton *>(QStringLiteral("boardChatAsk")))
                if (button->isVisible()) ++n;
            return n;
        };
        for (const QString &tab : ModelsPane::tabIds()) {
            pane.showTab(tab);
            QCoreApplication::processEvents();
            QVERIFY2(panel->isVisible(), qPrintable(tab));
            QVERIFY2(ask->isVisible(), qPrintable(tab));
            QCOMPARE(visibleAskRows(), 1);
        }
        QVERIFY(!pane.providers()->onCreateConsole);
        QVERIFY(!pane.providers()->findChild<QWidget *>(QStringLiteral("boardChatPanel"))->isVisible());
        QCOMPARE(builds, 0);   // nothing is built until somebody asks

        // A click builds the console once, unfolds it and hands it the cursor; asking again reuses it.
        ask->click();
        QCOMPARE(builds, 1);
        QVERIFY(body->isVisible());
        QVERIFY(!ask->isVisible());
        QVERIFY(body->isAncestorOf(console));
        QCOMPARE(focused, 1);
        QCOMPARE(built, pane.agentContext());
        pane.focusHelper();
        QCOMPARE(builds, 1);
        QCOMPARE(focused, 2);
        QVERIFY(!pane.providers()->agentConsole());

        // What the worker is told: the models context, kept per tab, with no shell.
        pane.showTab(ModelsPane::prioritiesTab());
        pane.setFilter(QStringLiteral("glm"));
        const relay::agent::ContextSpec spec = pane.agentContext()->spec();
        QCOMPARE(spec.name, QStringLiteral("models"));
        QCOMPARE(spec.surface, QStringLiteral("models"));
        QCOMPARE(spec.briefKey, QStringLiteral("models"));
        QCOMPARE(spec.briefTitle, QStringLiteral("Models helper"));
        QCOMPARE(spec.scope, QStringLiteral("console"));
        QCOMPARE(spec.persistScope, QStringLiteral("helper"));
        QCOMPARE(spec.persistKey, QStringLiteral("tab-7"));
        QCOMPARE(spec.routing, QStringLiteral("agent"));
        QVERIFY(!spec.shell);
        QCOMPARE(pane.agentContext()->placeholder(), QStringLiteral("Ask the Models helper…"));
        // And what is on screen: the tab, the pane it serves, the filter, the class in focus.
        QVERIFY2(spec.screen.contains(QStringLiteral("Models › priorities")), qPrintable(spec.screen));
        QVERIFY2(spec.screen.contains(QStringLiteral("Serving: relay-terminal")), qPrintable(spec.screen));
        QVERIFY2(spec.screen.contains(QStringLiteral("Filter: glm")), qPrintable(spec.screen));
        QVERIFY2(spec.screen.contains(QStringLiteral("Class in focus: ")), qPrintable(spec.screen));
        pane.showTab(ModelsPane::jobsTab());
        QVERIFY(pane.agentContext()->spec().screen.contains(QStringLiteral("Models › jobs")));

        // The fold puts it back to one row and keeps the console.
        auto *fold = body->findChild<QToolButton *>(QStringLiteral("boardChatFold"));
        QVERIFY(fold);
        fold->click();
        QVERIFY(!body->isVisible());
        QVERIFY(ask->isVisible());
        QCOMPARE(builds, 1);
    }

    void withNoTargetItSaysSoRatherThanPretending() {
        ModelsPane pane(providerSections());
        QVERIFY(pane.header()->text().contains(QStringLiteral("no pane")));
        pane.showTab(ModelsPane::prioritiesTab());
        pane.picker()->use();   // nothing to switch, and nothing crashes
        QVERIFY(pane.servedToken().isEmpty());
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("relay-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("modelspane"));
    ModelsPaneTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "modelspane_test.moc"
