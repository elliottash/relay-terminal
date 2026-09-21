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
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

#include <functional>

using namespace relay;
using namespace relay::models;

namespace {

// The picker's columns, as ModelPicker.cpp orders them.
enum Column { ColRank, ColAvail, ColBox, ColModel, ColVia, ColReasoning, ColIntelligence, ColSpeed, ColLeft };

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
                       {QStringLiteral("label"), QStringLiteral("anthropic · claude opus 5")},
                       {QStringLiteral("provider"), QStringLiteral("anthropic (claude)")},
                       {QStringLiteral("plan"), QStringLiteral("pay-as-you-go")},
                       {QStringLiteral("model"), QStringLiteral("claude-opus-5")}, {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("claude-opus-5"), QStringLiteral("claude opus 5"), QStringLiteral("main"), {}, 51)}}};
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

QStringList rowKeys(QTreeWidget *list) {
    QStringList out;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list->topLevelItem(i);
        if (item->isHidden()) continue;
        const QString key = item->data(0, Qt::UserRole).toString();
        if (!key.isEmpty()) out << key;
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
                                         QStringLiteral("anthropic|claude-opus-5")});
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

    // "tab = the served pane's mode (tier) on priorities" (design 5.8).
    void prioritiesOpensOnTheServedPanesOwnClass() {
        setList(QStringLiteral("flash"), {{QStringLiteral("glm-coding|glm-5.3-flash"), QStringLiteral("low")}});
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served, QStringLiteral("flash")));   // the pane is on /flash
        pane.showTab(ModelsPane::prioritiesTab());
        QCOMPARE(pane.tier(), QStringLiteral("flash"));
        QCOMPARE(rowKeys(pane.picker()->list()), QStringList{QStringLiteral("glm-coding|glm-5.3-flash")});
        // The class tabs are the picker's second row, and the flat tab is not among them: it is
        // this pane's own "available".
        QCOMPARE(tabs(pane.picker()->tabBar()), (QStringList{QStringLiteral("high"), QStringLiteral("main"),
                                                             QStringLiteral("flash"), QStringLiteral("lite")}));
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

    // Two rows of tabs, a key each. **Not Ctrl+Tab**: that is the window's Next tab (Keymap
    // `tab.next`) and never reaches a pane — the first Xvfb run of this pane pressed it three
    // times and stayed where it was (docs/qa_evidence/2026-09-21-models-pane).
    void altDigitsWalkTheThreeTabsAndCtrlTabIsTheWindows() {
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::providersTab());
        QTest::keyClick(&pane, Qt::Key_2, Qt::AltModifier);
        QCOMPARE(pane.currentTab(), QStringLiteral("available"));
        QTest::keyClick(&pane, Qt::Key_3, Qt::AltModifier);
        QCOMPARE(pane.currentTab(), QStringLiteral("priorities"));
        QTest::keyClick(pane.picker()->filter(), Qt::Key_1, Qt::AltModifier);
        QCOMPARE(pane.currentTab(), QStringLiteral("providers"));
        // Ctrl+Tab is left alone wherever it is pressed.
        QTest::keyClick(&pane, Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(pane.currentTab(), QStringLiteral("providers"));
        // ←/→ walk them too, but only where the picker is not using them for its class tabs.
        pane.showTab(ModelsPane::availableTab());
        QTest::keyClick(pane.picker()->filter(), Qt::Key_Right);
        QCOMPARE(pane.currentTab(), QStringLiteral("priorities"));
        QCOMPARE(pane.tier(), QStringLiteral("main"));
        QTest::keyClick(pane.picker()->filter(), Qt::Key_Right);   // now the class tabs
        QCOMPARE(pane.currentTab(), QStringLiteral("priorities"));
        QCOMPARE(pane.tier(), QStringLiteral("flash"));
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
                                         {QStringLiteral("anthropic|claude-opus-5"), QString()}});
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::prioritiesTab());
        pane.picker()->selectKey(QStringLiteral("anthropic|claude-opus-5"));
        QTest::keyClick(pane.picker()->list(), Qt::Key_Return);
        QCOMPARE(served.uses, 1);
        QCOMPARE(served.key, QStringLiteral("anthropic|claude-opus-5"));
        // The "use" button is the same door, and so is Enter in the filter line.
        pane.picker()->selectKey(QStringLiteral("glm-coding|glm-5.3"));
        QTest::keyClick(pane.picker()->filter(), Qt::Key_Return);
        QCOMPARE(served.uses, 2);
        QCOMPARE(served.key, QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(served.effort, QStringLiteral("max"));   // the level the list carries, with it
    }

    void aClickOnlyHighlights() {
        setList(QStringLiteral("main"), {{QStringLiteral("glm-coding|glm-5.3"), QString()},
                                         {QStringLiteral("anthropic|claude-opus-5"), QString()}});
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::prioritiesTab());
        QTreeWidget *list = pane.picker()->list();
        emit list->itemClicked(list->topLevelItem(1), ColModel);
        list->setCurrentItem(list->topLevelItem(1));
        QCOMPARE(served.uses, 0);
        QCOMPARE(pane.picker()->selectedKey(), QStringLiteral("anthropic|claude-opus-5"));
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
                                         {QStringLiteral("anthropic|claude-opus-5"), QString()}});
        Served served;
        ModelsPane pane(providerSections());
        pane.setTarget(targetFor(&served));
        pane.showTab(ModelsPane::prioritiesTab());
        ModelPicker *picker = pane.picker();
        picker->selectKey(QStringLiteral("anthropic|claude-opus-5"));
        QTest::keyClick(picker->list(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("anthropic|claude-opus-5"),
                                                                QStringLiteral("glm-coding|glm-5.3")}));
        QTest::keyClick(picker->list(), Qt::Key_Delete);
        QCOMPARE(listKeys(QStringLiteral("main")), QStringList{QStringLiteral("glm-coding|glm-5.3")});
        QTest::keyClick(picker->list(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(listKeys(QStringLiteral("main")), (QStringList{QStringLiteral("anthropic|claude-opus-5"),
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
        pane.picker()->setTier(QStringLiteral("flash"));
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

    // Alt+4 reaches it wherever the focus is in the pane, and it is handed the worker's report
    // rather than any state of its own — `Target::roleSummary` is the "runs on" column.
    void altFourIsTheJobsTabAndItDrawsTheWorkersReport() {
        Served served;
        ModelsPane pane(providerSections());
        ModelsPane::Target target = targetFor(&served);
        target.roleSummary = QJsonObject{
            {QStringLiteral("summaries"), QJsonObject{{QStringLiteral("preset"), QStringLiteral("glm-coding")},
                                                      {QStringLiteral("model"), QStringLiteral("glm-5.3-flash")},
                                                      {QStringLiteral("effort"), QStringLiteral("low")}}}};
        target.rolesChanged = [&served] { ++served.listEdits; };
        pane.setTarget(target);
        QKeyEvent alt4(QEvent::KeyPress, Qt::Key_4, Qt::AltModifier);
        QCoreApplication::sendEvent(&pane, &alt4);
        QCOMPARE(pane.currentTab(), ModelsPane::jobsTab());
        QVERIFY(pane.jobs() != nullptr);
        QCOMPARE(pane.jobs()->runsOn(QStringLiteral("summaries")), QStringLiteral("glm-5.3-flash · low"));
        // The keyboard lands on the list, not on a filter line the tab does not have.
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
        QVERIFY(rolestore::setOverride(QStringLiteral("subagent"), QStringLiteral("anthropic|claude-opus-5"),
                                       QString()));
        pane.setTarget(target);   // a worker report, or any re-read
        QCOMPARE(pane.jobs()->overrideText(QStringLiteral("subagent")), QStringLiteral("claude-opus-5"));
        QVERIFY(pane.jobs()->clearOverride());
        QCOMPARE(resends, 1);
        QCOMPARE(pane.jobs()->overrideText(QStringLiteral("subagent")), QStringLiteral("follows main"));
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
