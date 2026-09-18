// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsPane.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QStackedWidget>
#include <QTest>

using relay::ActionItem;
using relay::SettingRow;
using relay::SettingsPane;
using relay::SettingsSection;

namespace {

// A small catalog with every row kind, whose values live in plain variables so a test can see
// exactly what a control wrote.
struct State {
    bool thinking = true;
    QString theme = QStringLiteral("dark");
    QString plans;
    int steps = 50;
    int buttonRuns = 0;
    QStringList ran;
};

QList<SettingsSection> catalog(State *state) {
    QList<SettingsSection> sections;
    SettingsSection general;
    general.id = QStringLiteral("general");
    general.title = QStringLiteral("General");
    general.blurb = QStringLiteral("What Relay shows while it works.");
    {
        SettingRow row;
        row.kind = SettingRow::Toggle; row.id = QStringLiteral("option:thinking");
        row.label = QStringLiteral("Show thinking"); row.detail = QStringLiteral("Stream reasoning above the prompt");
        row.checked = state->thinking;
        row.onToggle = [state](bool on) { state->thinking = on; };
        general.rows << row;
    }
    {
        SettingRow row;
        row.kind = SettingRow::Number; row.id = QStringLiteral("option:steps");
        row.label = QStringLiteral("Step limit per turn"); row.detail = QStringLiteral("Model calls");
        row.number = state->steps; row.minimum = 1; row.maximum = 500;
        row.onNumber = [state](int value) { state->steps = value; };
        general.rows << row;
    }
    sections << general;

    SettingsSection appearance;
    appearance.id = QStringLiteral("appearance");
    appearance.title = QStringLiteral("Appearance");
    {
        SettingRow row;
        row.kind = SettingRow::Choice; row.id = QStringLiteral("option:theme");
        row.label = QStringLiteral("Theme"); row.detail = QStringLiteral("Applies at once");
        row.options = QStringList{QStringLiteral("dark"), QStringLiteral("light")};
        row.optionLabels = QStringList{QStringLiteral("Relay Dark"), QStringLiteral("Relay Light")};
        row.current = state->theme;
        row.onChoose = [state](const QString &id) { state->theme = id; };
        appearance.rows << row;
    }
    sections << appearance;

    SettingsSection agent;
    agent.id = QStringLiteral("agent");
    agent.title = QStringLiteral("Agent");
    {
        SettingRow heading;
        heading.kind = SettingRow::Heading; heading.id = QStringLiteral("heading:limits"); heading.label = QStringLiteral("Turn limits");
        agent.rows << heading;
        SettingRow row;
        row.kind = SettingRow::Text; row.id = QStringLiteral("option:plans");
        row.label = QStringLiteral("Plans folder"); row.detail = QStringLiteral("Absolute folder for plans");
        row.aliases = QStringLiteral("planning spec design");
        row.text = state->plans;
        row.onText = [state](const QString &value) { state->plans = value; };
        agent.rows << row;
        SettingRow button;
        button.kind = SettingRow::Button; button.id = QStringLiteral("agent.instructions");
        button.label = QStringLiteral("Instructions"); button.detail = QStringLiteral("CLAUDE.md and friends");
        button.buttonText = QStringLiteral("Choose…");
        button.run = [state] { ++state->buttonRuns; };
        agent.rows << button;
        SettingRow info;
        info.kind = SettingRow::Info; info.id = QStringLiteral("info:agent"); info.label = QStringLiteral("An informational line.");
        agent.rows << info;
    }
    sections << agent;

    SettingsSection shortcuts;
    shortcuts.id = SettingsPane::actionsTabId();
    shortcuts.title = QStringLiteral("Actions");
    sections << shortcuts;
    return sections;
}

QList<ActionItem> actions(State *state) {
    QList<ActionItem> items;
    ActionItem model;
    model.key = QStringLiteral("menu:model"); model.section = QStringLiteral("Agent");
    model.label = QStringLiteral("Model"); model.detail = QStringLiteral("Kimi K3");
    model.children = [state] {
        QList<ActionItem> children;
        for (const QString &name : {QStringLiteral("Kimi K3"), QStringLiteral("DeepSeek V4")}) {
            ActionItem child;
            child.key = QStringLiteral("model:") + name; child.section = QStringLiteral("Model"); child.label = name;
            child.checked = name == QStringLiteral("Kimi K3");
            child.run = [state, name] { state->ran << QStringLiteral("model:") + name; };
            children << child;
        }
        return children;
    };
    items << model;
    ActionItem split;
    split.key = QStringLiteral("pane.splitRight"); split.section = QStringLiteral("Panes and tabs");
    split.label = QStringLiteral("New pane to the right"); split.shortcut = QStringLiteral("Ctrl+E");
    split.run = [state] { state->ran << QStringLiteral("pane.splitRight"); };
    items << split;
    ActionItem flash;
    flash.key = QStringLiteral("agent.flashAgent"); flash.section = QStringLiteral("Agent");
    flash.label = QStringLiteral("Flash agent for this pane"); flash.stayOpen = true;
    flash.run = [state] { state->ran << QStringLiteral("agent.flashAgent"); };
    items << flash;
    return items;
}

void press(QWidget *target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::keyClick(target, Qt::Key(key), modifiers);
}

}  // namespace

class SettingsPaneTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTests"));
        QCoreApplication::setApplicationName(QStringLiteral("settingspane"));
        QSettings().clear();
    }

    void tabsFollowTheCatalog() {
        State state;
        SettingsPane pane([&] { return catalog(&state); }, [&] { return actions(&state); });
        QCOMPARE(pane.tabIds(), (QStringList{QStringLiteral("general"), QStringLiteral("appearance"),
                                             QStringLiteral("agent"), QStringLiteral("shortcuts")}));
        auto *tabs = pane.findChild<QTabBar *>(QStringLiteral("settingsTabs"));
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 4);
        QCOMPARE(tabs->tabText(3), QStringLiteral("Actions"));
        QCOMPARE(pane.currentTab(), QStringLiteral("general"));
        // Headings and info lines are not rows the keyboard lands on.
        pane.showTab(QStringLiteral("agent"));
        QCOMPARE(pane.visibleRowIds(), (QStringList{QStringLiteral("option:plans"), QStringLiteral("agent.instructions")}));
    }

    void arrowsSwitchTabsAndRows() {
        State state;
        SettingsPane pane([&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.show();
        pane.focusSearch();
        auto *search = pane.findChild<QLineEdit *>(QStringLiteral("settingsSearch"));
        QVERIFY(search);
        press(search, Qt::Key_Right);
        QCOMPARE(pane.currentTab(), QStringLiteral("appearance"));
        press(search, Qt::Key_Left);
        press(search, Qt::Key_Left);
        QCOMPARE(pane.currentTab(), QStringLiteral("shortcuts"));   // wraps
        pane.showTab(QStringLiteral("general"));
        QCOMPARE(pane.currentRow(), -1);
        press(search, Qt::Key_Down);
        QCOMPARE(pane.currentRow(), 0);
        press(search, Qt::Key_Down);
        QCOMPARE(pane.currentRow(), 1);
        press(search, Qt::Key_Down);
        QCOMPARE(pane.currentRow(), 1);   // stops at the end, no wrap on a page
        press(search, Qt::Key_Up);
        QCOMPARE(pane.currentRow(), 0);
    }

    void enterTogglesTheHighlightedRow() {
        State state;
        SettingsPane pane([&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.show();
        pane.focusSearch();
        auto *search = pane.findChild<QLineEdit *>(QStringLiteral("settingsSearch"));
        press(search, Qt::Key_Down);
        press(search, Qt::Key_Return);
        QCOMPARE(state.thinking, false);
        // The rebuild that follows keeps the highlight on the same row and the tab in place.
        QTest::qWait(50);
        QCOMPARE(pane.currentTab(), QStringLiteral("general"));
        QCOMPARE(pane.currentRow(), 0);
        auto *check = pane.findChild<QCheckBox *>();
        QVERIFY(check);
        QCOMPARE(check->isChecked(), false);
        // Enter with nothing highlighted takes the first row.
        SettingsPane fresh([&] { return catalog(&state); }, [&] { return actions(&state); });
        fresh.show();
        fresh.focusSearch();
        press(fresh.findChild<QLineEdit *>(QStringLiteral("settingsSearch")), Qt::Key_Return);
        QCOMPARE(state.thinking, true);
    }

    void searchMixesSettingsAndActions() {
        State state;
        SettingsPane pane([&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.show();
        pane.setSearch(QStringLiteral("deep"));
        // A submenu entry is found through its parent: Model › DeepSeek V4.
        QCOMPARE(pane.visibleRowIds(), QStringList{QStringLiteral("model:DeepSeek V4")});
        QCOMPARE(pane.currentRow(), 0);
        pane.setSearch(QStringLiteral("plan"));
        QStringList ids = pane.visibleRowIds();
        QVERIFY(ids.contains(QStringLiteral("option:plans")));
        pane.setSearch(QStringLiteral("spec"));   // an alias word
        QCOMPARE(pane.visibleRowIds(), QStringList{QStringLiteral("option:plans")});
        pane.setSearch(QStringLiteral("pane"));
        ids = pane.visibleRowIds();
        QVERIFY(ids.contains(QStringLiteral("pane.splitRight")));
        QVERIFY(ids.contains(QStringLiteral("agent.flashAgent")));
        pane.setSearch(QStringLiteral("zzzz"));
        QVERIFY(pane.visibleRowIds().isEmpty());
        // Clearing the search returns to the tab that was selected.
        pane.showTab(QStringLiteral("agent"));
        pane.setSearch(QString());
        QCOMPARE(pane.visibleRowIds(), (QStringList{QStringLiteral("option:plans"), QStringLiteral("agent.instructions")}));
    }

    void enterRunsAnActionThroughTheOwner() {
        State state;
        SettingsPane pane([&] { return catalog(&state); }, [&] { return actions(&state); });
        QStringList handed;
        pane.onRun = [&](const ActionItem &item) { handed << item.key; };
        pane.show();
        pane.focusSearch();
        auto *search = pane.findChild<QLineEdit *>(QStringLiteral("settingsSearch"));
        QTest::keyClicks(search, QStringLiteral("right"));
        press(search, Qt::Key_Return);
        QCOMPARE(handed, QStringList{QStringLiteral("pane.splitRight")});
        QVERIFY(state.ran.isEmpty());   // the owner runs it, so it can close the pane first
        // Without an owner the pane runs it itself.
        pane.onRun = nullptr;
        press(search, Qt::Key_Return);
        QCOMPARE(state.ran, QStringList{QStringLiteral("pane.splitRight")});
    }

    void actionsTabListsRecentThenSections() {
        State state;
        QSettings().setValue(QStringLiteral("palette/recent"), QStringList{QStringLiteral("pane.splitRight")});
        SettingsPane pane([&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.showTab(SettingsPane::actionsTabId());
        const QStringList ids = pane.visibleRowIds();
        // Recent first, then Agent (the Model submenu opened inline, then the fast-agent toggle),
        // then Panes and tabs.
        QCOMPARE(ids, (QStringList{QStringLiteral("pane.splitRight"), QStringLiteral("model:Kimi K3"),
                                   QStringLiteral("model:DeepSeek V4"), QStringLiteral("agent.flashAgent"),
                                   QStringLiteral("pane.splitRight")}));
        QSettings().remove(QStringLiteral("palette/recent"));
    }

    void escapeClearsThenCloses() {
        State state;
        SettingsPane pane([&] { return catalog(&state); }, [&] { return actions(&state); });
        int closed = 0;
        pane.onClose = [&] { ++closed; };
        pane.show();
        pane.focusSearch();
        auto *search = pane.findChild<QLineEdit *>(QStringLiteral("settingsSearch"));
        QTest::keyClicks(search, QStringLiteral("theme"));
        press(search, Qt::Key_Escape);
        QVERIFY(search->text().isEmpty());
        QCOMPARE(closed, 0);
        press(search, Qt::Key_Escape);
        QCOMPARE(closed, 1);
        // Esc in a control goes back to the search first.
        pane.showTab(QStringLiteral("general"));
        auto *spin = pane.findChild<QSpinBox *>();
        QVERIFY(spin);
        spin->setFocus();
        QTest::keyClick(spin, Qt::Key_Escape);
        QCOMPARE(closed, 1);
        QCOMPARE(pane.focusWidget(), search);
    }

    void rebuildKeepsTabScrollAndFocus() {
        State state;
        SettingsPane pane([&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.resize(500, 160);   // short, so the pages scroll
        pane.show();
        pane.showTab(SettingsPane::actionsTabId());
        auto *pages = pane.findChild<QStackedWidget *>();
        QVERIFY(pages);
        auto *scroll = qobject_cast<QScrollArea *>(pages->currentWidget());
        QVERIFY(scroll);
        QTest::qWait(50);
        QVERIFY(scroll->verticalScrollBar()->maximum() >= 30);
        scroll->verticalScrollBar()->setValue(30);
        pane.rebuild();
        QTest::qWait(50);
        QCOMPARE(pane.currentTab(), SettingsPane::actionsTabId());
        auto *after = qobject_cast<QScrollArea *>(pane.findChild<QStackedWidget *>()->currentWidget());
        QVERIFY(after);
        QVERIFY(after != scroll);
        QVERIFY(after->verticalScrollBar()->maximum() >= 30);
        QCOMPARE(after->verticalScrollBar()->value(), 30);
        // A control that had focus has it again after the rebuild its own edit caused.
        pane.showTab(QStringLiteral("general"));
        auto *spin = pane.findChild<QSpinBox *>();
        spin->setFocus();
        pane.rebuild();
        QVERIFY(qobject_cast<QSpinBox *>(pane.focusWidget()));
    }

    void controlsWriteThroughTheirRows() {
        State state;
        SettingsPane pane([&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.show();
        auto *spin = pane.findChild<QSpinBox *>();
        spin->setValue(75);
        QCOMPARE(state.steps, 75);
        pane.showTab(QStringLiteral("appearance"));
        auto *combo = pane.findChild<QComboBox *>();
        QVERIFY(combo);
        combo->setCurrentIndex(1);
        Q_EMIT combo->activated(1);
        QCOMPARE(state.theme, QStringLiteral("light"));
        QTest::qWait(30);
        pane.showTab(QStringLiteral("agent"));
        QLineEdit *plans = nullptr;
        for (QLineEdit *edit : pane.findChildren<QLineEdit *>())
            if (edit->objectName() != QLatin1String("settingsSearch") && edit->isVisible()) plans = edit;
        QVERIFY(plans);
        plans->setText(QStringLiteral("/tmp/plans"));
        Q_EMIT plans->editingFinished();
        QCOMPARE(state.plans, QStringLiteral("/tmp/plans"));
        QTest::qWait(30);
        QCOMPARE(pane.currentTab(), QStringLiteral("agent"));
    }

    void fuzzyScoreOrdersDirectMatchesFirst() {
        QVERIFY(SettingsPane::fuzzyScore(QStringLiteral("theme"), QStringLiteral("Theme")) >
                SettingsPane::fuzzyScore(QStringLiteral("theme"), QStringLiteral("Reload themes")));
        QVERIFY(SettingsPane::fuzzyScore(QStringLiteral("thm"), QStringLiteral("Theme")) > 0);
        QCOMPARE(SettingsPane::fuzzyScore(QStringLiteral("xyz"), QStringLiteral("Theme")), 0);
    }
};

QTEST_MAIN(SettingsPaneTests)
#include "settingspane_test.moc"
