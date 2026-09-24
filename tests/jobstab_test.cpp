// SPDX-License-Identifier: AGPL-3.0-or-later
// The jobs tab of the models pane (Alt+4, card #MDL1, design 5.9): the rows a person reads, the
// column that says where a job actually goes, and the one pick that moves it.
//
// **Ported from tests/modelsettings_test.cpp**, whose `RolesDialog` half is retired with the
// modal: the storage keys a job writes (`roles/<role>/{preset,model,effort}`), that a provider
// with no key is still offered, that a tier pinned by the old dialog still reads, and that
// clearing a job puts every one of those keys back. **Not ported**: everything about the tier box,
// the provider box and the "its own provider…" row, which this tab does not have — an override is
// a model, and the tier a job follows is the priorities lists' business.
//
// Nothing here needs a `Pane`, a `ModelsPane`, a window or a worker: `JobsTab::Data` is a catalog,
// two JSON objects the worker sent and two callbacks.
#include "JobsTab.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace relay;
using namespace relay::models;

namespace {

QJsonObject model(const QString &id, const QString &name, const QString &tier, const QStringList &efforts) {
    return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("label"), name},
                       {QStringLiteral("name"), name}, {QStringLiteral("tier"), tier},
                       {QStringLiteral("efforts"), QJsonArray::fromStringList(efforts)}};
}

// Two cloud providers and a guest harness, which is what the guest rule needs to be about.
QJsonArray presets() {
    QJsonArray out;
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("glm-coding")},
                       {QStringLiteral("label"), QStringLiteral("z.ai · glm-5.3 · coding plan")},
                       {QStringLiteral("provider"), QStringLiteral("z.ai (glm)")},
                       {QStringLiteral("model"), QStringLiteral("glm-5.3")},
                       {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("glm-5.3"), QStringLiteral("glm-5.3"), QStringLiteral("main"),
                                  {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")}),
                            model(QStringLiteral("glm-5.3-flash"), QStringLiteral("glm-5.3-flash"),
                                  QStringLiteral("flash"),
                                  {QStringLiteral("low"), QStringLiteral("high")})}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("kimi")},
                       {QStringLiteral("label"), QStringLiteral("kimi · k3")},
                       {QStringLiteral("provider"), QStringLiteral("kimi")},
                       {QStringLiteral("model"), QStringLiteral("kimi-k3")},
                       {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("kimi-k3"), QStringLiteral("kimi-k3"), QStringLiteral("main"),
                                  {QStringLiteral("low"), QStringLiteral("high")})}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("guest:claude")},
                       {QStringLiteral("label"), QStringLiteral("claude code")},
                       {QStringLiteral("provider"), QStringLiteral("claude code")},
                       {QStringLiteral("model"), QStringLiteral("opus")},
                       {QStringLiteral("guest"), true},
                       {QStringLiteral("harness"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("opus"), QStringLiteral("claude-opus-5-5"), QStringLiteral("main"),
                                  {QStringLiteral("high"), QStringLiteral("xhigh")})}}};
    return out;
}

// One role of the worker's `model_roles.roles` (roles.py Resolved::to_dict).
QJsonObject resolved(const QString &role, const QString &preset, const QString &modelId,
                     const QString &effort, const QString &tier) {
    return QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("preset"), preset},
                       {QStringLiteral("model"), modelId}, {QStringLiteral("effort"), effort},
                       {QStringLiteral("tier"), tier}, {QStringLiteral("source"), QStringLiteral("default")}};
}

struct Served {
    int resends = 0;
    int focusBacks = 0;
};

JobsTab::Data dataFor(Served *served) {
    JobsTab::Data data;
    data.catalog = catalogFrom(presets());
    data.now = 1;
    data.roles = QJsonObject{
        {QStringLiteral("main"), resolved(QStringLiteral("main"), QStringLiteral("kimi"),
                                          QStringLiteral("kimi-k3"), QStringLiteral("high"), QString())},
        {QStringLiteral("subagent"), resolved(QStringLiteral("subagent"), QStringLiteral("kimi"),
                                              QStringLiteral("kimi-k3"), QStringLiteral("high"),
                                              QStringLiteral("main"))},
        {QStringLiteral("summaries"), resolved(QStringLiteral("summaries"), QStringLiteral("glm-coding"),
                                               QStringLiteral("glm-5.3-flash"), QStringLiteral("low"),
                                               QStringLiteral("flash"))},
        {QStringLiteral("chores"), resolved(QStringLiteral("chores"), QStringLiteral("glm-coding"),
                                            QStringLiteral("glm-5.3-flash"), QString(),
                                            QStringLiteral("lite"))}};
    data.tiers = QJsonObject{
        {QStringLiteral("main"), resolved(QStringLiteral("main"), QStringLiteral("kimi"),
                                          QStringLiteral("kimi-k3"), QStringLiteral("high"), QStringLiteral("main"))},
        {QStringLiteral("lite"), resolved(QStringLiteral("lite"), QStringLiteral("glm-coding"),
                                          QStringLiteral("glm-5.3-flash"), QString(), QStringLiteral("lite"))}};
    data.rolesChanged = [served] { ++served->resends; };
    data.focusBack = [served] { ++served->focusBacks; };
    return data;
}

// The rows are flat: a heading carries the tier under Qt::UserRole+2 and a job its role id under
// Qt::UserRole+1 (a heading cannot be a *parent*, because Qt propagates a disabled parent's state
// to its children and the jobs would stop being selectable with it).
const int kRoleData = Qt::UserRole + 1;
const int kTierData = Qt::UserRole + 2;

QStringList groups(QTreeWidget *list) {
    QStringList out;
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        if (!list->topLevelItem(i)->data(0, kTierData).toString().isEmpty())
            out << list->topLevelItem(i)->text(0);
    return out;
}

QStringList jobsUnder(QTreeWidget *list, const QString &group) {
    QStringList out;
    bool inside = false;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list->topLevelItem(i);
        const QString tier = item->data(0, kTierData).toString();
        if (!tier.isEmpty()) {
            if (inside) break;
            inside = item->text(0) == group;
            continue;
        }
        if (inside) out << item->text(0).trimmed();
    }
    return out;
}

QTreeWidgetItem *headingFor(QTreeWidget *list, const QString &tier) {
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        if (list->topLevelItem(i)->data(0, kTierData).toString() == tier) return list->topLevelItem(i);
    return nullptr;
}

QTreeWidgetItem *rowFor(QTreeWidget *list, const QString &role) {
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        if (list->topLevelItem(i)->data(0, kRoleData).toString() == role) return list->topLevelItem(i);
    return nullptr;
}

QStringList offered(const JobsTab &tab, const QString &role) {
    QStringList out;
    for (const FilterRow &row : tab.overrideRows(role)) out << row.data;
    return out;
}

QString setting(const QString &role, const QString &field) {
    return QSettings().value(rolestore::roleSetting(role, field)).toString();
}

}  // namespace

class JobsTabTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void init() { QSettings().clear(); }

    // ----- the rows -----------------------------------------------------------------------------

    // "grouped by the TIER it follows, in this order: main, high, flash, lite, local, and the two
    // fixed ones" (the owner's shape for the tab).
    void theRowsAreGroupedByTierInThatOrder() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QCOMPARE(groups(tab.list()), (QStringList{QStringLiteral("main"), QStringLiteral("high"),
                                                  QStringLiteral("flash"), QStringLiteral("lite"),
                                                  QStringLiteral("local"), QStringLiteral("fixed")}));
        QCOMPARE(jobsUnder(tab.list(), QStringLiteral("main")),
                 (QStringList{QStringLiteral("agent turns"), QStringLiteral("subagents"),
                              QStringLiteral("helper agent")}));
        QCOMPARE(jobsUnder(tab.list(), QStringLiteral("flash")),
                 (QStringList{QStringLiteral("terminal driving"), QStringLiteral("/flash panes"),
                              QStringLiteral("summaries"), QStringLiteral("suggestions")}));
        QCOMPARE(jobsUnder(tab.list(), QStringLiteral("fixed")),
                 (QStringList{QStringLiteral("images"), QStringLiteral("command routing")}));
    }

    // Every job of roles.py ROLES has a row, and no row names a role the worker has never heard of.
    void everyRoleTheWorkerResolvesHasARow() {
        const QStringList roles{QStringLiteral("main"), QStringLiteral("terminal_use"), QStringLiteral("subagent"),
                                QStringLiteral("switchboard"), QStringLiteral("high"), QStringLiteral("flash"),
                                QStringLiteral("local"), QStringLiteral("planning"), QStringLiteral("summaries"),
                                QStringLiteral("suggestions"), QStringLiteral("chores"), QStringLiteral("audit"),
                                QStringLiteral("loop_check"), QStringLiteral("vision"), QStringLiteral("route_assist")};
        QStringList have;
        for (const JobsTab::Job &job : JobsTab::jobs()) have << job.role;
        QCOMPARE(have.size(), roles.size());
        for (const QString &role : roles) QVERIFY2(have.contains(role), qPrintable(role));
        // …and the words are lower-case (card #MDL1, rule 1), never the protocol name.
        for (const JobsTab::Job &job : JobsTab::jobs()) {
            QCOMPARE(job.name, job.name.toLower());
            QVERIFY2(!job.name.contains(QLatin1Char('_')), qPrintable(job.name));
        }
    }

    // ----- "runs on": the column that did not exist ----------------------------------------------

    void runsOnNamesTheModelTheWorkerResolvedWithItsLevel() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QCOMPARE(tab.runsOn(QStringLiteral("summaries")), QStringLiteral("glm-5.3-flash · low"));
        QCOMPARE(tab.runsOn(QStringLiteral("subagent")), QStringLiteral("kimi-k3 · high"));
        // No level reported: the name alone, never an empty "· ".
        QCOMPARE(tab.runsOn(QStringLiteral("chores")), QStringLiteral("glm-5.3-flash"));
        QCOMPARE(rowFor(tab.list(), QStringLiteral("summaries"))->text(2), QStringLiteral("glm-5.3-flash · low"));
    }

    void aJobTheWorkerHasNotReportedSaysSoRatherThanGuessing() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QVERIFY(tab.runsOn(QStringLiteral("audit")).isEmpty());
        QCOMPARE(rowFor(tab.list(), QStringLiteral("audit"))->text(2), QStringLiteral("—"));
    }

    // The column follows the next report: the tab never predicts a resolution, it redraws one.
    void theColumnFollowsTheNextWorkerReport() {
        Served served;
        JobsTab tab;
        JobsTab::Data data = dataFor(&served);
        tab.setData(data);
        QCOMPARE(tab.runsOn(QStringLiteral("summaries")), QStringLiteral("glm-5.3-flash · low"));
        data.roles.insert(QStringLiteral("summaries"),
                          resolved(QStringLiteral("summaries"), QStringLiteral("kimi"),
                                   QStringLiteral("kimi-k3"), QStringLiteral("low"), QString()));
        tab.setData(data);
        QCOMPARE(tab.runsOn(QStringLiteral("summaries")), QStringLiteral("kimi-k3 · low"));
        QCOMPARE(rowFor(tab.list(), QStringLiteral("summaries"))->text(2), QStringLiteral("kimi-k3 · low"));
    }

    // Since the owner took the lite section off the priorities tab, the group row here is the only
    // place a person sees what chores, the audit and the loop check run on.
    void theGroupRowSaysWhatTheTierItselfRunsOn() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QCOMPARE(tab.tierRunsOn(QStringLiteral("lite")), QStringLiteral("glm-5.3-flash"));
        QCOMPARE(tab.tierRunsOn(QStringLiteral("main")), QStringLiteral("kimi-k3 · high"));
        QVERIFY(tab.tierRunsOn(QStringLiteral("fixed")).isEmpty());
        QVERIFY(headingFor(tab.list(), QStringLiteral("lite")) != nullptr);
        QCOMPARE(headingFor(tab.list(), QStringLiteral("lite"))->text(2), QStringLiteral("glm-5.3-flash"));
    }

    // ----- the override -------------------------------------------------------------------------

    void aLegacyPlanningOverrideIsRetiredExactlyOnce() {
        // #PMX7's real settings had this pre-#HR5E shape. Seed raw keys: roleSetting() itself is
        // the migration boundary and would correctly remove them before returning.
        QSettings settings;
        settings.setValue(QStringLiteral("roles/planning/preset"), QStringLiteral("glm-coding"));
        settings.setValue(QStringLiteral("roles/planning/effort"), QStringLiteral("max"));
        QVERIFY(rolestore::migrateLegacyPlanningOverride());
        for (const char *field : {"preset", "model", "effort", "tier"})
            QVERIFY2(!settings.contains(QStringLiteral("roles/planning/") + QLatin1String(field)), field);

        // A current explicit choice is made after the marker. Later migration checks leave it
        // alone, so fixing an upgrade does not remove a supported user choice forever.
        QVERIFY(rolestore::setOverride(QStringLiteral("planning"),
                                       QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max")));
        QVERIFY(!rolestore::migrateLegacyPlanningOverride());
        QCOMPARE(setting(QStringLiteral("planning"), QStringLiteral("preset")),
                 QStringLiteral("glm-coding"));
        QCOMPARE(setting(QStringLiteral("planning"), QStringLiteral("model")), QStringLiteral("glm-5.3"));
    }

    void aFreshInstallMarksTheMigrationBeforeItsFirstOverride() {
        QVERIFY(!rolestore::migrateLegacyPlanningOverride());
        QVERIFY(rolestore::setOverride(QStringLiteral("planning"),
                                       QStringLiteral("kimi|kimi-k3"), QStringLiteral("high")));
        QVERIFY(!rolestore::migrateLegacyPlanningOverride());
        QCOMPARE(setting(QStringLiteral("planning"), QStringLiteral("preset")), QStringLiteral("kimi"));
        QCOMPARE(setting(QStringLiteral("planning"), QStringLiteral("model")), QStringLiteral("kimi-k3"));
    }

    void aJobWithNoOverrideSaysWhichTierItFollows() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QCOMPARE(tab.overrideText(QStringLiteral("summaries")), QStringLiteral("follows flash"));
        QCOMPARE(tab.overrideText(QStringLiteral("chores")), QStringLiteral("follows lite"));
        // The two that follow no tier.
        QCOMPARE(tab.overrideText(QStringLiteral("vision")), QStringLiteral("automatic"));
        QCOMPARE(tab.overrideText(QStringLiteral("route_assist")), QStringLiteral("automatic"));
        // Agent turns are the pane's own model by definition and cannot be overridden here.
        QCOMPARE(tab.overrideText(QStringLiteral("main")), QStringLiteral("this pane's model"));
        QVERIFY(tab.selectRole(QStringLiteral("main")));
        QVERIFY(!tab.openOverride());
    }

    void anOverrideWritesTheSameKeysTheRetiredDialogWrote() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QVERIFY(rolestore::setOverride(QStringLiteral("summaries"), QStringLiteral("kimi|kimi-k3"),
                                       QStringLiteral("low")));
        QCOMPARE(setting(QStringLiteral("summaries"), QStringLiteral("preset")), QStringLiteral("kimi"));
        QCOMPARE(setting(QStringLiteral("summaries"), QStringLiteral("model")), QStringLiteral("kimi-k3"));
        QCOMPARE(setting(QStringLiteral("summaries"), QStringLiteral("effort")), QStringLiteral("low"));
        tab.setData(dataFor(&served));
        QCOMPARE(tab.overrideText(QStringLiteral("summaries")), QStringLiteral("kimi-k3 · low"));
    }

    void clearingPutsEveryKeyBackIncludingATierTheOldDialogPinned() {
        Served served;
        QSettings().setValue(rolestore::roleSetting(QStringLiteral("summaries"), QStringLiteral("tier")),
                             QStringLiteral("lite"));
        JobsTab tab;
        tab.setData(dataFor(&served));
        // A tier pinned by the retired dialog is read and said plainly, not silently ignored.
        QCOMPARE(tab.overrideText(QStringLiteral("summaries")), QStringLiteral("follows lite"));
        QVERIFY(tab.selectRole(QStringLiteral("summaries")));
        QVERIFY(tab.clearOverride());
        QCOMPARE(served.resends, 1);
        for (const char *field : {"preset", "model", "effort", "tier"})
            QVERIFY2(!QSettings().contains(rolestore::roleSetting(QStringLiteral("summaries"),
                                                                  QLatin1String(field))), field);
        QCOMPARE(tab.overrideText(QStringLiteral("summaries")), QStringLiteral("follows flash"));
    }

    void clearingAJobThatHasNothingToClearDoesNothing() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QVERIFY(tab.selectRole(QStringLiteral("suggestions")));
        QVERIFY(!tab.clearOverride());
        QCOMPARE(served.resends, 0);
    }

    // Every write reaches the served pane's worker at once, which is what makes the column live.
    void everyWriteTellsTheServedPane() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QVERIFY(tab.selectRole(QStringLiteral("summaries")));
        QVERIFY(tab.openOverride());
        QVERIFY(tab.popup() != nullptr);
        const QList<FilterRow> rows = tab.popup()->rows();
        int at = -1;
        for (int i = 0; i < rows.size(); ++i)
            if (rows.at(i).data == QStringLiteral("glm-coding|glm-5.3")) at = i;
        QVERIFY(at >= 0);
        tab.popup()->onPicked(at);
        QCOMPARE(setting(QStringLiteral("summaries"), QStringLiteral("preset")), QStringLiteral("glm-coding"));
        QCOMPARE(setting(QStringLiteral("summaries"), QStringLiteral("model")), QStringLiteral("glm-5.3"));
        QCOMPARE(served.resends, 1);
    }

    void rankedJobOverridesMigrateSingletonsAndPreserveEffort() {
        QVERIFY(rolestore::setOverride(QStringLiteral("planning"),
                                       QStringLiteral("guest:claude|opus"), QStringLiteral("high")));
        const auto singleton = rolestore::rankedOverride(QStringLiteral("planning"));
        QCOMPARE(singleton.size(), 1);
        QCOMPARE(singleton.first().rank, 1);
        QCOMPARE(singleton.first().effort, QStringLiteral("high"));
        rolestore::setRankedOverride(QStringLiteral("planning"), {
            {QStringLiteral("guest:claude|opus"), QStringLiteral("high"), 1},
            {QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max"), 1}});
        QVERIFY(rolestore::rankedOverrideSet(QStringLiteral("planning")));
        QVERIFY(setting(QStringLiteral("planning"), QStringLiteral("preset")).isEmpty());
        const auto tied = rolestore::rankedOverride(QStringLiteral("planning"));
        QCOMPARE(tied.size(), 2);
        QCOMPARE(tied.at(1).rank, 1);
        QCOMPARE(tied.at(1).effort, QStringLiteral("max"));
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QVERIFY(tab.overrideText(QStringLiteral("planning")).contains(QStringLiteral("random at rank 1")));
        QVERIFY(tab.selectRole(QStringLiteral("planning")));
        QVERIFY(tab.clearOverride());
        QVERIFY(!rolestore::rankedOverrideSet(QStringLiteral("planning")));
        QCOMPARE(tab.overrideText(QStringLiteral("planning")), QStringLiteral("follows high"));
    }

    void rankedRouteIsEditedInlineAndEachActionPersists() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QVERIFY(tab.selectRole(QStringLiteral("subagent")));
        auto *panel = tab.findChild<QWidget *>(QStringLiteral("jobsRankedPanel"));
        auto *list = tab.findChild<QTreeWidget *>(QStringLiteral("jobsRankedList"));
        auto button = [&tab](const char *name) {
            return tab.findChild<QPushButton *>(QLatin1String(name));
        };
        QVERIFY(panel && !panel->isHidden());
        QVERIFY(list);
        QCOMPARE(list->topLevelItemCount(), 0);
        QVERIFY(rowFor(tab.list(), QStringLiteral("subagent"))->text(3).contains(QStringLiteral("inherited")));

        auto add = [&](const QString &key) {
            button("jobsRankedAdd")->click();
            QVERIFY(tab.popup());
            int at = -1;
            const auto choices = tab.popup()->rows();
            for (int i = 0; i < choices.size(); ++i)
                if (choices.at(i).data == key) at = i;
            QVERIFY(at >= 0);
            tab.popup()->onPicked(at);
        };
        add(QStringLiteral("kimi|kimi-k3"));
        add(QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(served.resends, 2);
        QVERIFY(rolestore::rankedOverrideSet(QStringLiteral("subagent")));
        QCOMPARE(list->topLevelItemCount(), 2);
        QVERIFY(!QApplication::activeModalWidget());

        list->setCurrentItem(list->topLevelItem(1));
        button("jobsRankedTie")->click();
        auto entries = rolestore::rankedOverride(QStringLiteral("subagent"));
        QCOMPARE(entries.at(0).rank, entries.at(1).rank);
        QVERIFY(tab.overrideText(QStringLiteral("subagent")).contains(QStringLiteral("random")));
        button("jobsRankedUntie")->click();
        entries = rolestore::rankedOverride(QStringLiteral("subagent"));
        QVERIFY(entries.at(0).rank != entries.at(1).rank);

        list->setCurrentItem(list->topLevelItem(1));
        const QString last = list->topLevelItem(1)->data(1, Qt::UserRole).toString();
        button("jobsRankedUp")->click();
        QCOMPARE(list->topLevelItem(0)->data(1, Qt::UserRole).toString(), last);
        button("jobsRankedEffort")->click();
        const auto levels = tab.popup()->rows();
        int high = -1;
        for (int i = 0; i < levels.size(); ++i)
            if (levels.at(i).data == QStringLiteral("high")) high = i;
        QVERIFY(high >= 0);
        tab.popup()->onPicked(high);
        entries = rolestore::rankedOverride(QStringLiteral("subagent"));
        QCOMPARE(entries.at(0).effort, QStringLiteral("high"));
        button("jobsRankedRemove")->click();
        QCOMPARE(rolestore::rankedOverride(QStringLiteral("subagent")).size(), 1);
        button("jobsRankedFollow")->click();
        QVERIFY(!rolestore::rankedOverrideSet(QStringLiteral("subagent")));
        QCOMPARE(list->topLevelItemCount(), 0);
        QVERIFY(rowFor(tab.list(), QStringLiteral("subagent"))->text(3).contains(QStringLiteral("inherited")));
    }

    // ----- one row per model, and the guest rule --------------------------------------------------

    void theListIsFollowsItsTierAndThenOneRowPerModelByName() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        const QList<FilterRow> rows = tab.overrideRows(QStringLiteral("subagent"));
        QVERIFY(rows.first().data.isEmpty());
        QCOMPARE(rows.first().text, QStringLiteral("follows main"));
        QStringList names;
        for (int i = 1; i < rows.size(); ++i) names << rows.at(i).text;
        QVERIFY2(names.contains(QStringLiteral("glm-5.3")), qPrintable(names.join(',')));
        QVERIFY(names.contains(QStringLiteral("claude-opus-5-5")));
        QCOMPARE(names.size(), QSet<QString>(names.begin(), names.end()).size());   // one row per model
    }

    // roles.py BACKGROUND_ROLES: a side call cannot be handed to a harness, so it is not offered —
    // rather than offered and then silently skipped by the worker.
    void aBackgroundJobIsNotOfferedAGuestHarness() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        for (const QString &role : {QStringLiteral("summaries"), QStringLiteral("terminal_use"),
                                    QStringLiteral("suggestions"), QStringLiteral("chores"),
                                    QStringLiteral("audit"), QStringLiteral("loop_check")}) {
            QVERIFY2(rolestore::background(role), qPrintable(role));
            for (const QString &key : offered(tab, role))
                QVERIFY2(!rolestore::isGuestKey(key), qPrintable(role + ": " + key));
        }
        // A pane's own mode, and the jobs on the main tier, may run on one.
        for (const QString &role : {QStringLiteral("flash"), QStringLiteral("subagent"),
                                    QStringLiteral("switchboard"), QStringLiteral("high")}) {
            QVERIFY2(!rolestore::background(role), qPrintable(role));
            QVERIFY2(offered(tab, role).contains(QStringLiteral("guest:claude|opus")), qPrintable(role));
        }
    }

    void writingAGuestOntoABackgroundJobIsRefusedWithAReason() {
        QString why;
        QVERIFY(!rolestore::setOverride(QStringLiteral("summaries"), QStringLiteral("guest:claude|opus"),
                                        QString(), &why));
        QVERIFY(!why.isEmpty());
        QVERIFY(!QSettings().contains(rolestore::roleSetting(QStringLiteral("summaries"),
                                                             QStringLiteral("preset"))));
        // The same model on a job that is a conversation of its own is accepted.
        QVERIFY(rolestore::setOverride(QStringLiteral("subagent"), QStringLiteral("guest:claude|opus"),
                                       QString(), &why));
        QCOMPARE(setting(QStringLiteral("subagent"), QStringLiteral("preset")), QStringLiteral("guest:claude"));
    }

    // The row says why, so the absence from the list is never a mystery.
    void aBackgroundJobsRowExplainsTheGuestRule() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        const QString tip = rowFor(tab.list(), QStringLiteral("summaries"))->toolTip(0);
        QVERIFY2(tip.contains(QStringLiteral("guest harness")), qPrintable(tip));
        QVERIFY(tip.contains(QStringLiteral("relay free")));
    }

    // ----- levels are the model's own (card #MDL1) -----------------------------------------------

    void theLevelListIsTheModelsOwnWords() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        QStringList levels;
        for (const FilterRow &row : tab.levelRows(QStringLiteral("subagent"), QStringLiteral("guest:claude|opus")))
            levels << row.text;
        QCOMPARE(levels, (QStringList{QStringLiteral("model default"), QStringLiteral("high"),
                                      QStringLiteral("xhigh")}));
        // A model with no knob asks for nothing: no second popup, and the override is the model.
        QVERIFY(tab.levelRows(QStringLiteral("subagent"), QStringLiteral("nowhere|nothing")).isEmpty());
    }

    // ----- the keyboard ---------------------------------------------------------------------------

    void deleteClearsTheHighlightedRowAndEscapeGoesBackToThePane() {
        Served served;
        JobsTab tab;
        QVERIFY(rolestore::setOverride(QStringLiteral("summaries"), QStringLiteral("kimi|kimi-k3"), QString()));
        tab.setData(dataFor(&served));
        QVERIFY(tab.selectRole(QStringLiteral("summaries")));
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QCoreApplication::sendEvent(tab.list(), &del);
        QVERIFY(!QSettings().contains(rolestore::roleSetting(QStringLiteral("summaries"),
                                                             QStringLiteral("preset"))));
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(&tab, &escape);
        QCOMPARE(served.focusBacks, 1);
    }

    // A group heading is a label, not a choice — the same ruling the model box got.
    void aGroupHeadingIsNotSelectable() {
        Served served;
        JobsTab tab;
        tab.setData(dataFor(&served));
        for (int i = 0; i < tab.list()->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = tab.list()->topLevelItem(i);
            if (item->data(0, kTierData).toString().isEmpty()) continue;
            QVERIFY(!(item->flags() & Qt::ItemIsSelectable));
            QVERIFY(!(item->flags() & Qt::ItemIsEnabled));   // …so Up and Down step over it
        }
        tab.focusList();
        QCOMPARE(tab.currentRole(), QStringLiteral("main"));
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("relay-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("jobstab"));
    JobsTabTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "jobstab_test.moc"
