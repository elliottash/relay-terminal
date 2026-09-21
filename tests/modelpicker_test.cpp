// SPDX-License-Identifier: AGPL-3.0-or-later
// The model picker dialog (owner, 2026-09-20): rows from the catalog, the filter, the sort menu,
// the reasoning level beside the model, favorites and recent sections, and what a pick returns.
#include "ModelPicker.h"

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

    void theLevelListFollowsTheHighlightedModel() {
        curation::addToTier(QStringLiteral("main"), QStringLiteral("glm-coding|glm-5.3-flash"), QStringLiteral("low"));
        ModelPicker picker(context());
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(picker.levelList()->count(), 3);
        QCOMPARE(picker.selectedEffort(), QStringLiteral("low"));                 // the list's level
        QCOMPARE(picker.list()->currentItem()->text(2), QStringLiteral("low"));
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(picker.selectedEffort(), QStringLiteral("high"));                // in no list: the pane's own
        picker.levelList()->setCurrentRow(2);
        QCOMPARE(picker.selectedEffort(), QStringLiteral("max"));                 // a separate pick
        picker.selectKey(QStringLiteral("anthropic|claude-opus-5"));
        QCOMPARE(picker.selectedEffort(), QString());                             // no knob
        QVERIFY(picker.findChild<QLabel *>(QStringLiteral("modelLimits"))->text().isEmpty());
        picker.selectKey(QStringLiteral("guest:claude|opus"));
        QVERIFY(picker.findChild<QLabel *>(QStringLiteral("modelLimits"))->text().contains(QStringLiteral("5h 62% left")));
    }

    void aPickReturnsTheKeyAndTheLevel() {
        curation::addToTier(QStringLiteral("high"), QStringLiteral("glm-coding|glm-5.3-flash"), QStringLiteral("max"));
        ModelPicker picker(context());
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3-flash"));
        picker.accept();
        QVERIFY(picker.pick().accepted);
        QCOMPARE(picker.pick().key, QStringLiteral("glm-coding|glm-5.3-flash"));
        QCOMPARE(picker.pick().effort, QStringLiteral("max"));
    }

    void aClickOnlyHighlights() {
        // Owner, 2026-09-20: clicking a row must not close the dialog, so the level can be picked
        // after the model. Enter commits; a double click is the shortcut for both.
        ModelPicker picker(context());
        picker.show();
        QVERIFY(QTest::qWaitForWindowExposed(&picker));
        QTreeWidgetItem *flash = picker.list()->topLevelItem(1);
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

    void anExhaustedSubscriptionIsGreyedStillSelectableAndSaysWhenItResets() {
        ModelPicker::Context ctx = context();
        ctx.now = QDateTime(QDate(2026, 9, 20), QTime(9, 0)).toSecsSinceEpoch();
        const qint64 resets = QDateTime(QDate(2026, 9, 20), QTime(14, 30)).toSecsSinceEpoch();
        ctx.catalog.limits[QStringLiteral("anthropic")] = {LimitWindow{QStringLiteral("5h"), 100, resets}};
        ModelPicker picker(ctx);
        // The row is still there, in its rank…
        QCOMPARE(keys(picker.list()).at(2), QStringLiteral("anthropic|claude-opus-5"));
        QTreeWidgetItem *spent = picker.list()->topLevelItem(2);
        QCOMPARE(spent->text(5), QStringLiteral("0% · resets 14:30"));
        const QColor muted = picker.palette().color(QPalette::Disabled, QPalette::Text);
        QCOMPARE(spent->foreground(0).color(), muted);
        QCOMPARE(spent->foreground(5).color(), muted);
        QVERIFY(spent->toolTip(0).contains(QStringLiteral("exhausted")));
        // …a live row is not muted…
        QTreeWidgetItem *live = picker.list()->topLevelItem(0);
        QCOMPARE(live->text(5), QString());   // glm has no figures
        QVERIFY(live->foreground(0).color() != muted || live->foreground(0).style() == Qt::NoBrush);
        QCOMPARE(picker.list()->topLevelItem(3)->text(5), QStringLiteral("62%"));   // the guest's 5h window
        // …and the user may still insist on it.
        picker.selectKey(QStringLiteral("anthropic|claude-opus-5"));
        picker.accept();
        QVERIFY(picker.pick().accepted);
        QCOMPARE(picker.pick().key, QStringLiteral("anthropic|claude-opus-5"));
        // The limits line under the list says the same in words.
        ModelPicker again(ctx);
        again.selectKey(QStringLiteral("anthropic|claude-opus-5"));
        QCOMPARE(again.findChild<QLabel *>(QStringLiteral("modelLimits"))->text(), QStringLiteral("anthropic (claude): 5h 0% left, resets 14:30"));
        // Spent with no reset time known: "0%" alone, still greyed.
        ctx.catalog.limits[QStringLiteral("anthropic")] = {LimitWindow{QStringLiteral("5h"), 100, 0}};
        ModelPicker unknown(ctx);
        QCOMPARE(unknown.list()->topLevelItem(2)->text(5), QStringLiteral("0%"));
        QCOMPARE(unknown.list()->topLevelItem(2)->foreground(0).color(), muted);
        // Past its reset: an ordinary row again, with the figure the window still reports.
        ctx.catalog.limits[QStringLiteral("anthropic")] = {LimitWindow{QStringLiteral("5h"), 100, ctx.now - 1}};
        ModelPicker back(ctx);
        QCOMPARE(back.list()->topLevelItem(2)->text(5), QStringLiteral("0%"));
        QVERIFY(back.list()->topLevelItem(2)->foreground(0).style() == Qt::NoBrush);
    }

    void levelsAreShownInTheProvidersWords() {
        curation::addToTier(QStringLiteral("main"), QStringLiteral("glm-coding|glm-5.3"), QStringLiteral("max"));
        ModelPicker::Context ctx = context(QStringLiteral("anthropic|claude-opus-5"), QStringLiteral("max"));
        for (Entry &entry : ctx.catalog.entries)
            if (entry.key == QStringLiteral("glm-coding|glm-5.3")) entry.effortLabels.insert(QStringLiteral("max"), QStringLiteral("xhigh"));
        ModelPicker picker(ctx);
        picker.selectKey(QStringLiteral("glm-coding|glm-5.3"));
        QCOMPARE(picker.list()->currentItem()->text(2), QStringLiteral("xhigh"));   // the provider's word
        QCOMPARE(picker.levelList()->currentItem()->text(), QStringLiteral("xhigh"));
        QCOMPARE(picker.selectedEffort(), QStringLiteral("max"));                   // Relay's level is what is returned
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
