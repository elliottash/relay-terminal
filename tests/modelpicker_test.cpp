// SPDX-License-Identifier: AGPL-3.0-or-later
// The model picker dialog (owner, 2026-09-20): rows from the catalog, the filter, the sort menu,
// the reasoning level beside the model, favorites and recent sections, and what a pick returns.
#include "ModelPicker.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace relay;
using namespace relay::models;

namespace {

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
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("anthropic")}, {QStringLiteral("label"), QStringLiteral("anthropic · claude opus 5")},
                       {QStringLiteral("provider"), QStringLiteral("anthropic (claude)")}, {QStringLiteral("plan"), QStringLiteral("pay-as-you-go")},
                       {QStringLiteral("model"), QStringLiteral("claude-opus-5")}, {QStringLiteral("has_stored_key"), true},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("claude-opus-5"), QStringLiteral("claude opus 5"), QStringLiteral("main"), {}, 51)}}};
    out << QJsonObject{{QStringLiteral("id"), QStringLiteral("guest:claude")}, {QStringLiteral("label"), QStringLiteral("Claude Code")},
                       {QStringLiteral("provider"), QStringLiteral("Claude Code")}, {QStringLiteral("model"), QString()},
                       {QStringLiteral("harness"), true},
                       {QStringLiteral("models"), QJsonArray{model(QStringLiteral("opus"), QStringLiteral("opus"), QString(), {QStringLiteral("low"), QStringLiteral("high")})}},
                       {QStringLiteral("limits"), QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("5h")}, {QStringLiteral("used_percent"), 38.0}, {QStringLiteral("resets_at"), 0}}}}};
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

}  // namespace

class ModelPickerTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void init() {
        QSettings().clear();
        QSettings().setValue(QStringLiteral("provider/preset"), QStringLiteral("glm-coding"));
    }

    void rowsFollowRankAndTheCurrentOneIsSelected() {
        ModelPicker picker(context());
        QCOMPARE(keys(picker.list()), (QStringList{QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("glm-coding|glm-5.3-flash"),
                                                   QStringLiteral("anthropic|claude-opus-5"), QStringLiteral("guest:claude|opus")}));
        QCOMPARE(picker.selectedKey(), QStringLiteral("glm-coding|glm-5.3"));
        QVERIFY(picker.list()->currentItem()->text(0).contains(QStringLiteral("current")));
        // Lower-case everywhere, the guest included.
        QCOMPARE(picker.list()->topLevelItem(3)->text(1), QStringLiteral("claude code"));
        QCOMPARE(picker.list()->topLevelItem(0)->text(3), QStringLiteral("45"));
    }

    void theReasoningRowFollowsTheHighlightedModel() {
        ModelPicker picker(context());
        QCOMPARE(picker.effortGroup()->buttons().size(), 3);
        QCOMPARE(picker.selectedEffort(), QStringLiteral("high"));   // the pane's level, offered here
        picker.selectKey(QStringLiteral("anthropic|claude-opus-5"));
        QCOMPARE(picker.effortGroup()->buttons().size(), 0);         // no knob
        QCOMPARE(picker.selectedEffort(), QString());
        curation::setEffortFor(QStringLiteral("guest:claude|opus"), QStringLiteral("low"));
        picker.rebuild();   // the column reads the memory when the rows are drawn
        picker.selectKey(QStringLiteral("guest:claude|opus"));
        QCOMPARE(picker.selectedEffort(), QStringLiteral("low"));    // remembered per model
        QCOMPARE(picker.list()->currentItem()->text(2), QStringLiteral("low"));
        QVERIFY(picker.findChild<QLabel *>(QStringLiteral("modelLimits"))->text().contains(QStringLiteral("5h 62% left")));
    }

    void aPickReturnsTheKeyAndTheLevelAndRemembersIt() {
        ModelPicker picker(context());
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3-flash"));
        for (QAbstractButton *button : picker.effortGroup()->buttons())
            if (button->property("level").toString() == QStringLiteral("max")) button->click();
        picker.accept();
        QVERIFY(picker.pick().accepted);
        QCOMPARE(picker.pick().key, QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(picker.pick().effort, QStringLiteral("max"));
        QCOMPARE(curation::effortFor(QStringLiteral("glm-coding|glm-5.3-flash")), QStringLiteral("max"));
    }

    void typingFiltersToOneFlatList() {
        curation::toggleFavorite(QStringLiteral("guest:claude|opus"));
        curation::noteUse(QStringLiteral("anthropic|claude-opus-5"));
        ModelPicker picker(context());
        QCOMPARE(keys(picker.list()), (QStringList{QStringLiteral("[favorites]"), QStringLiteral("guest:claude|opus"),
                                                   QStringLiteral("[recent]"), QStringLiteral("anthropic|claude-opus-5"),
                                                   QStringLiteral("[all, by priority]"), QStringLiteral("glm-coding|glm-5.3"),
                                                   QStringLiteral("glm-coding|glm-5.3-flash")}));
        QVERIFY(picker.list()->topLevelItem(1)->text(0).startsWith(QStringLiteral("★ ")));
        picker.filter()->setText(QStringLiteral("flash"));
        QCOMPARE(keys(picker.list()), QStringList{QStringLiteral("glm-coding|glm-5.3-flash")});
        QCOMPARE(picker.selectedKey(), QStringLiteral("glm-coding|glm-5.3-flash"));
        picker.filter()->setText(QStringLiteral("claude"));
        QCOMPARE(keys(picker.list()), (QStringList{QStringLiteral("anthropic|claude-opus-5"), QStringLiteral("guest:claude|opus")}));
        picker.filter()->clear();
        QCOMPARE(keys(picker.list()).size(), 7);
    }

    void theSortMenuReordersAndPersists() {
        ModelPicker picker(context());
        const int intelligence = picker.sortBox()->findData(QStringLiteral("intelligence"));
        picker.sortBox()->setCurrentIndex(intelligence);
        emit picker.sortBox()->activated(intelligence);
        QCOMPARE(keys(picker.list()).first(), QStringLiteral("anthropic|claude-opus-5"));   // 51 beats 45
        QCOMPARE(curation::sort(), Sort::Intelligence);
        const int alpha = picker.sortBox()->findData(QStringLiteral("alpha"));
        picker.sortBox()->setCurrentIndex(alpha);
        emit picker.sortBox()->activated(alpha);
        QCOMPARE(keys(picker.list()).first(), QStringLiteral("anthropic|claude-opus-5"));   // "anthropic" < "claude code"
        QCOMPARE(keys(picker.list()).at(1), QStringLiteral("guest:claude|opus"));
        // A second picker opens on the saved sort.
        ModelPicker again(context());
        QCOMPARE(again.sortBox()->currentData().toString(), QStringLiteral("alpha"));
    }

    void customizeClosesAndOpensThePage() {
        ModelPicker picker(context());
        bool opened = false;
        picker.openModelsPage = [&] { opened = true; };
        picker.findChild<QPushButton *>(QStringLiteral("modelCustomize"))->click();
        QVERIFY(opened);
        QVERIFY(!picker.pick().accepted);
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
