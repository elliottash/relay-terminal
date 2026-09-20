// SPDX-License-Identifier: AGPL-3.0-or-later
// The helper agent's model box (#BRD3, #FEJQ, #PK5Q). The owner, 2026-09-20: "can you have the
// picker be the same as in the main terminal." So the assertion this file exists for is that a
// helper's box, built from what its worker said, holds the same rows a terminal pane's box holds
// for the same catalog — and that the row the resolved role sits on is the right one.
#include "HelperChat.h"
#include "HelperModelBox.h"
#include "ModelRows.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

using namespace relay;

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
    // A guest harness the worker can run: a row in every box, and never the helper's model.
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("guest:claude")},
                       {QStringLiteral("label"), QStringLiteral("Claude Code")},
                       {QStringLiteral("provider"), QStringLiteral("claude code")},
                       {QStringLiteral("harness"), true},
                       {QStringLiteral("models"), QJsonArray{
                            model(QStringLiteral("opus"), QStringLiteral("opus"), QString())}}};
    return out;
}

QJsonObject tier(const QString &id, const QString &preset, const QString &modelId, const QString &using_)
{
    QJsonObject out{{QStringLiteral("tier"), id}, {QStringLiteral("preset"), preset},
                    {QStringLiteral("model"), modelId}};
    if (!using_.isEmpty()) out.insert(QStringLiteral("using"), using_);
    return out;
}

// The worker's `configured`, as a helper actually receives it.
helpermodel::State state(const QJsonObject &role = {})
{
    helpermodel::State out;
    out.take(QStringLiteral("presets"), QJsonObject{{QStringLiteral("presets"), presets()}});
    out.take(QStringLiteral("configured"),
             QJsonObject{{QStringLiteral("tiers"), QJsonObject{
                              {QStringLiteral("main"), tier(QStringLiteral("main"), QStringLiteral("glm-coding"), QStringLiteral("glm-5.3"), QString())},
                              {QStringLiteral("flash"), tier(QStringLiteral("flash"), QStringLiteral("glm-coding"), QStringLiteral("glm-5.3-flash"), QStringLiteral("flash"))}}},
                         {QStringLiteral("roles"), QJsonObject{{helpermodel::kRole(), role}}}});
    return out;
}

QStringList rowsOf(const QComboBox &box)
{
    QStringList out;
    for (int i = 0; i < box.count(); ++i) out << box.itemText(i) + QLatin1Char('\t') + box.itemData(i).toString();
    return out;
}

}  // namespace

class HelperModelBoxTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTest"));
        QCoreApplication::setApplicationName(QStringLiteral("helpermodelbox"));
        QSettings().clear();
    }

    // The card, in one assertion: what a helper draws is what a terminal pane draws.
    void helperBoxIsThePaneBox()
    {
        QComboBox helper;
        helpermodel::fill(&helper, state());

        // What a terminal pane on glm-5.3 hands the same builder.
        modelrows::Context pane;
        pane.catalog = models::catalogFrom(presets());
        pane.roleModel.insert(QStringLiteral("main"), QStringLiteral("glm-5.3"));
        pane.roleModel.insert(QStringLiteral("flash"), QStringLiteral("glm-5.3-flash"));
        pane.mainKey = QStringLiteral("glm-coding|glm-5.3");
        pane.current = QStringLiteral("role:main");
        QComboBox paneBox;
        modelrows::fill(&paneBox, pane);

        QCOMPARE(rowsOf(helper), rowsOf(paneBox));
        QCOMPARE(helper.currentData().toString(), QStringLiteral("role:main"));
        // The rows the old helper box never had: the per-model catalog and the picker.
        QVERIFY(helper.findData(QStringLiteral("entry:kimi-code|kimi-k3")) > 0);
        QVERIFY(helper.findData(QStringLiteral("gear:picker")) > 0);
        QVERIFY(helper.findData(QStringLiteral("gear:modelOptions")) > 0);
        // A guest harness is a row like any other; what refuses it is the pick (#GH5T).
        QVERIFY(helper.findData(QStringLiteral("entry:guest:claude|opus")) > 0);
    }

    // Which row the box sits on, per shape of the resolved role.
    void currentRowFollowsTheResolvedRole()
    {
        QCOMPARE(helpermodel::currentRow(state()), QStringLiteral("role:main"));
        QCOMPARE(helpermodel::currentRow(state(QJsonObject{{QStringLiteral("tier"), QStringLiteral("main")},
                                                           {QStringLiteral("source"), QStringLiteral("configured")}})),
                 QStringLiteral("role:main"));
        QCOMPARE(helpermodel::currentRow(state(QJsonObject{{QStringLiteral("tier"), QStringLiteral("flash")},
                                                           {QStringLiteral("source"), QStringLiteral("configured")}})),
                 QStringLiteral("role:flash"));
        // Pinned to a provider *and* a model: the entry row, which is what #PK5Q added.
        QCOMPARE(helpermodel::currentRow(state(QJsonObject{{QStringLiteral("source"), QStringLiteral("configured")},
                                                           {QStringLiteral("preset"), QStringLiteral("kimi-code")},
                                                           {QStringLiteral("model"), QStringLiteral("kimi-k3")}})),
                 QStringLiteral("entry:kimi-code|kimi-k3"));
        // A role that fell back to the main agent is on the Main row, whatever it reports having
        // answered on.
        QCOMPARE(helpermodel::currentRow(state(QJsonObject{{QStringLiteral("source"), QStringLiteral("fallback")},
                                                           {QStringLiteral("preset"), QStringLiteral("kimi-code")},
                                                           {QStringLiteral("model"), QStringLiteral("kimi-k3")}})),
                 QStringLiteral("role:main"));
    }

    // A pinned role's row is selected, and the Main row's own entry is still not repeated below.
    void pinnedRoleSelectsItsEntryRow()
    {
        QComboBox box;
        helpermodel::fill(&box, state(QJsonObject{{QStringLiteral("source"), QStringLiteral("configured")},
                                                  {QStringLiteral("preset"), QStringLiteral("kimi-code")},
                                                  {QStringLiteral("model"), QStringLiteral("kimi-k3")}}));
        QCOMPARE(box.currentData().toString(), QStringLiteral("entry:kimi-code|kimi-k3"));
        QCOMPARE(box.findData(QStringLiteral("entry:glm-coding|glm-5.3")), -1);
    }

    // The tooltip still carries the role's warning and note (13.7, and #GH5T's main_note).
    void tooltipCarriesTheRolesWarningAndNote()
    {
        QComboBox box;
        const QString tip = helpermodel::fill(&box, state(QJsonObject{
            {QStringLiteral("warning"), QStringLiteral("no stored key for kimi-code")},
            {QStringLiteral("note"), QStringLiteral("Main is Claude Code, a guest session")}}));
        QVERIFY(tip.contains(QStringLiteral("no stored key for kimi-code")));
        QVERIFY(tip.contains(QStringLiteral("Main is Claude Code")));
    }

    void guestRefusalNamesTheGuest()
    {
        QVERIFY(helpermodel::guestRefusal(QStringLiteral("Claude Code"))
                    .startsWith(QStringLiteral("The helper agent cannot run on Claude Code.")));
    }

    // The panel is where the keyboard finds the box (#PK5Q): the first combo put in its composer
    // strip is the model box, and Alt+M asks the panel for it.
    void thePanelKnowsItsModelBox()
    {
        HelperChatPanel panel;
        QCOMPARE(panel.modelBox(), nullptr);
        auto *box = new QComboBox;
        helpermodel::fill(box, state());
        panel.addComposerWidget(box);
        QCOMPARE(panel.modelBox(), box);
        panel.addComposerWidget(new QComboBox);   // a second combo is not the model box
        QCOMPARE(panel.modelBox(), box);
    }

    // `/model` typed in a helper composer is the fast path to that box, not a prompt. Anything
    // the window does not answer is sent as it always was.
    void slashCommandsAreOfferedBeforeThePromptIsSent()
    {
        HelperChatPanel panel;
        QStringList sent;
        panel.onSend = [&sent](const QJsonObject &message) {
            sent << message.value(QStringLiteral("text")).toString();
        };
        QStringList seen;
        panel.onSlashCommand = [&seen](const QString &name, const QString &args) {
            seen << name + QLatin1Char('\t') + args;
            return name == QStringLiteral("model") || name == QStringLiteral("models");
        };
        panel.prefill(QStringLiteral("/model kimi k3"));
        panel.submitComposer();
        QCOMPARE(seen, QStringList{QStringLiteral("model\tkimi k3")});
        QVERIFY(sent.isEmpty());
        QVERIFY(panel.draft().isEmpty());

        panel.prefill(QStringLiteral("/nonsense please"));
        panel.submitComposer();
        QCOMPARE(sent, QStringList{QStringLiteral("/nonsense please")});
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    HelperModelBoxTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "helpermodelbox_test.moc"
