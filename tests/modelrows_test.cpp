// SPDX-License-Identifier: AGPL-3.0-or-later
// The one list both model boxes draw (#PK5Q, owner 2026-09-20: "can you have the picker be the
// same as in the main terminal"). The point of the module is that a terminal pane's box and a
// helper agent's box built from the same catalog are the *same rows*, so that is what is asserted
// here: same texts, same data, same order, same separators.
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
    // A provider with no key: never a row, in either box.
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("openai")},
                       {QStringLiteral("label"), QStringLiteral("openai · gpt-6")},
                       {QStringLiteral("provider"), QStringLiteral("openai")},
                       {QStringLiteral("model"), QStringLiteral("gpt-6")}};
    return out;
}

// What a terminal pane hands the builder: its own model as the Main row, the role it is on.
modelrows::Context paneContext()
{
    modelrows::Context context;
    context.catalog = catalogFrom(presets());
    context.roleModel.insert(QStringLiteral("main"), QStringLiteral("glm-5.3"));
    context.roleModel.insert(QStringLiteral("flash"), QStringLiteral("glm-5.3-flash"));
    context.mainKey = QStringLiteral("glm-coding|glm-5.3");
    context.current = QStringLiteral("role:main");
    context.now = 1;
    return context;
}

// What a helper panel hands it: the same catalog, the models its tiers landed on.
modelrows::Context helperContext()
{
    modelrows::Context context = paneContext();   // deliberately the same inputs
    return context;
}

QStringList textsOf(const QComboBox &box)
{
    QStringList out;
    for (int i = 0; i < box.count(); ++i) out << box.itemText(i) + QLatin1Char('\t') + box.itemData(i).toString();
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
    }

    // The role rows, then the catalog in rank order, then the two gear rows.
    void rowsAreRolesThenCatalogThenGears()
    {
        const QList<modelrows::Row> rows = modelrows::build(paneContext());
        QStringList data;
        for (const modelrows::Row &row : rows) data << row.data;
        QCOMPARE(data.first(), QStringLiteral("role:main"));
        QCOMPARE(data.at(1), QStringLiteral("role:flash"));
        QCOMPARE(data.at(data.size() - 2), QStringLiteral("gear:picker"));
        QCOMPARE(data.last(), QStringLiteral("gear:modelOptions"));
        // The Main row already names glm-5.3, so the catalog never repeats it.
        QVERIFY(!data.contains(QStringLiteral("entry:glm-coding|glm-5.3")));
        QVERIFY(data.contains(QStringLiteral("entry:glm-coding|glm-5.3-flash")));
        QVERIFY(data.contains(QStringLiteral("entry:kimi-code|kimi-k3")));
        // A provider with no key is not offered anywhere.
        for (const QString &id : std::as_const(data)) QVERIFY(!id.contains(QStringLiteral("openai")));
        QCOMPARE(rows.first().text, QStringLiteral("glm-5.3 (main)"));
        QCOMPARE(rows.at(1).text, QStringLiteral("glm-5.3-flash (flash)"));
    }

    // Owner, 2026-09-21: the Switchboard agent's box said "kimi-k3 (switchboard)". The parentheses
    // name the tier the role runs on; a protocol role name never reaches a person.
    void aRoleRowNamesItsTierNotItsProtocolName()
    {
        using relay::modelrows::roleRowText;
        QCOMPARE(roleRowText(QStringLiteral("switchboard"), QStringLiteral("kimi-k3")), QStringLiteral("kimi-k3 (main)"));
        QCOMPARE(roleRowText(QStringLiteral("planning"), QStringLiteral("glm-5.3")), QStringLiteral("glm-5.3 (high)"));
        QCOMPARE(roleRowText(QStringLiteral("flash"), QStringLiteral("glm-5.3-flash")), QStringLiteral("glm-5.3-flash (flash)"));
    }

    // The point of the card: the pane's box and a helper's box are the same list.
    void paneAndHelperBoxesAreTheSameList()
    {
        QComboBox pane, helper;
        modelrows::fill(&pane, paneContext());
        modelrows::fill(&helper, helperContext());
        QVERIFY(pane.count() > 4);
        QCOMPARE(textsOf(pane), textsOf(helper));
        QCOMPARE(pane.currentIndex(), 0);
        QCOMPARE(pane.currentData().toString(), QStringLiteral("role:main"));
    }

    // A Local row only where this machine serves one, and a pinned helper sits on its entry row.
    void localRowAndPinnedCurrentRow()
    {
        modelrows::Context context = helperContext();
        context.roles << QStringLiteral("local");
        context.roleModel.insert(QStringLiteral("local"), QStringLiteral("bonsai-2-27b"));
        context.roleNote.insert(QStringLiteral("local"), QStringLiteral("no local model: using Main"));
        context.current = QStringLiteral("entry:kimi-code|kimi-k3");
        QComboBox box;
        const int index = modelrows::fill(&box, context);
        QCOMPARE(box.itemText(2), QStringLiteral("bonsai-2-27b (local)"));
        QCOMPARE(box.itemData(2, Qt::ToolTipRole).toString(), QStringLiteral("no local model: using Main"));
        QVERIFY(index > 2);
        QCOMPARE(box.currentData().toString(), QStringLiteral("entry:kimi-code|kimi-k3"));
    }

    // An exhausted subscription keeps its row, marked, in whichever box draws it.
    void exhaustedEntryIsMarked()
    {
        modelrows::Context context = paneContext();
        context.catalog.status.insert(QStringLiteral("kimi-code"), QStringLiteral("rejected"));
        QComboBox box;
        modelrows::fill(&box, context);
        const int row = box.findData(QStringLiteral("entry:kimi-code|kimi-k3"));
        QVERIFY(row > 0);
        QVERIFY(box.itemText(row).endsWith(QStringLiteral(" · exhausted")));
    }

    // Guest rows are the caller's: a terminal pane passes them, a helper passes none (#GH5T).
    void guestRowsComeFromTheCaller()
    {
        modelrows::Context pane = paneContext();
        pane.guests << QStringLiteral("claude");
        pane.guestText.insert(QStringLiteral("claude"), QStringLiteral("Claude Code"));
        QComboBox box;
        modelrows::fill(&box, pane);
        QVERIFY(box.findData(QStringLiteral("guest:claude")) > 0);
        QComboBox helperBox;
        modelrows::fill(&helperBox, helperContext());
        QCOMPARE(helperBox.findData(QStringLiteral("guest:claude")), -1);
    }

    // `/model <words>` resolves the same way in both composers.
    void resolveMatchesKeyModelLabelThenFilter()
    {
        const Catalog catalog = catalogFrom(presets());
        QCOMPARE(modelrows::resolve(catalog, QStringLiteral("glm-coding|glm-5.3-flash")),
                 QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(modelrows::resolve(catalog, QStringLiteral("kimi-k3")), QStringLiteral("kimi-code|kimi-k3"));
        QCOMPARE(modelrows::resolve(catalog, QStringLiteral("glm-5.3 flash")),
                 QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(modelrows::resolve(catalog, QStringLiteral("flash")), QStringLiteral("glm-coding|glm-5.3-flash"));
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
