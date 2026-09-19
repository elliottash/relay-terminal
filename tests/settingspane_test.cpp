// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SettingsPane.h"
#include "LocalModelsSettings.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
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
        row.changed = !state->thinking;       // as the real helpers do: the builder knows the default
        row.onToggle = [state](bool on) { state->thinking = on; };
        row.reset = [state] { state->thinking = true; };
        general.rows << row;
    }
    {
        SettingRow row;
        row.kind = SettingRow::Number; row.id = QStringLiteral("option:steps");
        row.label = QStringLiteral("Step limit per turn"); row.detail = QStringLiteral("Model calls");
        row.number = state->steps; row.minimum = 1; row.maximum = 500;
        row.changed = state->steps != 50;
        row.onNumber = [state](int value) { state->steps = value; };
        row.reset = [state] { state->steps = 50; };
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
        row.changed = state->theme != QStringLiteral("dark");
        row.onChoose = [state](const QString &id) { state->theme = id; };
        row.reset = [state] { state->theme = QStringLiteral("dark"); };
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
        row.changed = !state->plans.isEmpty();
        row.browse = true;                    // the Plans folder row, which is a folder on disk
        row.onText = [state](const QString &value) { state->plans = value; };
        row.reset = [state] { state->plans.clear(); };
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

    SettingsSection keyboard;
    keyboard.id = QStringLiteral("keyboard");
    keyboard.title = QStringLiteral("Keyboard");
    {
        // No `reset`: this stands in for a page where nothing has a default, which is a page with
        // no reset button (the real one is Local models, whose rows are the servers you saved).
        SettingRow row;
        row.kind = SettingRow::Toggle; row.id = QStringLiteral("option:hints");
        row.label = QStringLiteral("Shortcut hints"); row.detail = QStringLiteral("A brief tip");
        keyboard.rows << row;
    }
    sections << keyboard;
    return sections;
}

// What the end of RelayWindow::settingsSections() does: every page that has something to put back
// gets its own button, last on the page. `answer` stands in for the confirmation, which cannot be
// clicked headless, and `count` receives what the reset reported it had put back.
QList<SettingsSection> catalogWithResets(State *state, bool answer = true, int *count = nullptr) {
    QList<SettingsSection> sections = catalog(state);
    for (SettingsSection &section : sections) {
        SettingRow reset = relay::resetRow(section, [count](int put) { if (count) *count = put; },
                                           [answer](const QString &) { return answer; });
        if (!reset.id.isEmpty()) section.rows << reset;
    }
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
    ActionItem hosts;
    hosts.key = QStringLiteral("menu:ssh"); hosts.section = QStringLiteral("Panes and tabs");
    hosts.label = QStringLiteral("Connect to host…");
    hosts.children = [] { return QList<ActionItem>(); };
    hosts.typed = [state](const QString &search) {
        QList<ActionItem> rows;
        if (!search.contains(QLatin1Char('@'))) return rows;
        ActionItem row;
        row.key = QStringLiteral("ssh:") + search; row.section = QStringLiteral("Panes and tabs");
        row.label = QStringLiteral("ssh ") + search;
        row.run = [state, search] { state->ran << QStringLiteral("ssh:") + search; };
        rows << row;
        return rows;
    };
    items << hosts;
    return items;
}

void press(QWidget *target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::keyClick(target, Qt::Key(key), modifiers);
}

// ----- Settings › Local models (card #24XJ) ------------------------------------------------------
// The section has no widgets of its own: it answers the worker's events with rows, so a test drives
// it with the payloads of protocol section 23 and reads the rows back.
using relay::LocalModelsSettings;

// Every message the section sent, and the callbacks it fired.
struct Wire {
    QList<QJsonObject> sent;
    int changed = 0, presets = 0, setups = 0;
    bool ready = true;

    void attach(LocalModelsSettings *local) {
        local->send = [this](const QJsonObject &request) {
            if (!ready) return false;
            sent.append(request);
            return true;
        };
        local->onChanged = [this] { ++changed; };
        local->onPresetsChanged = [this] { ++presets; };
        local->onSetupWithAgent = [this] { ++setups; };
    }
    QJsonObject last(const QString &type) const {
        for (int i = sent.size() - 1; i >= 0; --i)
            if (sent.at(i).value(QStringLiteral("type")).toString() == type) return sent.at(i);
        return {};
    }
    QStringList idsOf(const QString &type) const {
        QStringList ids;
        for (const QJsonObject &request : sent)
            if (request.value(QStringLiteral("type")).toString() == type)
                ids << request.value(QStringLiteral("id")).toString();
        return ids;
    }
};

// A copy, not a pointer: a section is built fresh on every call and nothing here outlives it.
// An empty id means the section has no such row.
SettingRow rowById(const SettingsSection &section, const QString &id) {
    for (const SettingRow &row : section.rows)
        if (row.id == id) return row;
    return {};
}

bool hasRow(const SettingsSection &section, const QString &id) { return !rowById(section, id).id.isEmpty(); }

// The line drawn for a row id, on whichever page it lives (every page is built, shown or not).
QWidget *rowWidget(const SettingsPane &pane, const QString &id) {
    for (QWidget *candidate : pane.findChildren<QWidget *>(QStringLiteral("settingsRow")))
        if (candidate->property("rowId").toString() == id) return candidate;
    return nullptr;
}

// One of that line's own buttons: the ↺ (settingsRowReset) or the Browse… (settingsBrowse).
QPushButton *rowButton(const SettingsPane &pane, const QString &id, const QString &objectName) {
    QWidget *line = rowWidget(pane, id);
    return line ? line->findChild<QPushButton *>(objectName) : nullptr;
}

QJsonObject bonsai() {
    return QJsonObject{{"id", "local:bonsai"}, {"label", "bonsai-2-27b"}, {"base_url", "http://127.0.0.1:8080/v1"},
                       {"model", "bonsai-2-27b"}, {"server", "llamacpp"}, {"context_window", 131072},
                       {"local", true}, {"group", "local"}, {"tools", true}, {"thinking", true},
                       {"first_token_timeout", 300}, {"parallel_tool_calls", false},
                       {"tool_text_recovery", false}};
}

QJsonObject endpointsEvent() {
    return QJsonObject{{"event", "local_endpoints"}, {"id", "lm-endpoints"}, {"items", QJsonArray{bonsai()}}};
}

QJsonObject probedEvent(const QString &id, const QString &base, const QString &state,
                        const QStringList &models, const QString &error = QString()) {
    QJsonArray rows;
    for (const QString &model : models)
        rows.append(QJsonObject{{"id", model}, {"context_window", 131072}, {"tools", true}, {"thinking", true}});
    QJsonObject event{{"event", "local_probed"}, {"id", id}, {"base_url", base},
                      {"ok", state != QStringLiteral("down")}, {"server", "llamacpp"}, {"state", state},
                      {"context_window", 131072}, {"models", rows}};
    if (!error.isEmpty()) event.insert(QStringLiteral("error"), error);
    return event;
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
        SettingsPane pane(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
        QCOMPARE(pane.tabIds(), (QStringList{QStringLiteral("general"), QStringLiteral("appearance"),
                                             QStringLiteral("agent"), QStringLiteral("keyboard")}));
        auto *tabs = pane.findChild<QTabBar *>(QStringLiteral("settingsTabs"));
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 4);
        QCOMPARE(tabs->tabText(3), QStringLiteral("Keyboard"));
        QVERIFY(!tabs->isHidden());
        QCOMPARE(pane.currentTab(), QStringLiteral("general"));
        // Options holds no actions: no page lists one.
        for (const QString &id : pane.tabIds()) {
            pane.showTab(id);
            for (const QString &row : pane.visibleRowIds()) QVERIFY2(!row.startsWith(QStringLiteral("pane.")), qPrintable(row));
        }
        // Headings and info lines are not rows the keyboard lands on.
        pane.showTab(QStringLiteral("agent"));
        QCOMPARE(pane.visibleRowIds(), (QStringList{QStringLiteral("option:plans"), QStringLiteral("agent.instructions")}));
    }

    void arrowsSwitchTabsAndRows() {
        State state;
        SettingsPane pane(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.show();
        pane.focusSearch();
        auto *search = pane.findChild<QLineEdit *>(QStringLiteral("settingsSearch"));
        QVERIFY(search);
        press(search, Qt::Key_Right);
        QCOMPARE(pane.currentTab(), QStringLiteral("appearance"));
        press(search, Qt::Key_Left);
        press(search, Qt::Key_Left);
        QCOMPARE(pane.currentTab(), QStringLiteral("keyboard"));   // wraps
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
        SettingsPane pane(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
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
        SettingsPane fresh(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
        fresh.show();
        fresh.focusSearch();
        press(fresh.findChild<QLineEdit *>(QStringLiteral("settingsSearch")), Qt::Key_Return);
        QCOMPARE(state.thinking, true);
    }

    void optionsSearchFindsActionsToo() {
        State state;
        SettingsPane pane(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
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
        SettingsPane pane(SettingsPane::Mode::Actions, [&] { return catalog(&state); }, [&] { return actions(&state); });
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

    void aSubmenuAnswersWhatWasTyped() {
        State state;
        SettingsPane pane(SettingsPane::Mode::Actions, [&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.show();
        pane.setSearch(QStringLiteral("me@newbox"));
        QCOMPARE(pane.visibleRowIds().value(0), QStringLiteral("ssh:me@newbox"));
        pane.activateCurrent();
        QCOMPARE(state.ran, QStringList{QStringLiteral("ssh:me@newbox")});
        // A search the submenu does not recognise adds nothing.
        pane.setSearch(QStringLiteral("right"));
        for (const QString &id : pane.visibleRowIds()) QVERIFY2(!id.startsWith(QStringLiteral("ssh:")), qPrintable(id));
    }

    void actionsListsRecentThenSections() {
        State state;
        QSettings().setValue(QStringLiteral("palette/recent"), QStringList{QStringLiteral("pane.splitRight")});
        SettingsPane pane(SettingsPane::Mode::Actions, [&] { return catalog(&state); }, [&] { return actions(&state); });
        // One list and nothing to tab between.
        QCOMPARE(pane.tabIds(), QStringList{SettingsPane::actionsTabId()});
        QVERIFY(pane.findChild<QTabBar *>(QStringLiteral("settingsTabs"))->isHidden());
        QCOMPARE(pane.findChild<QLineEdit *>(QStringLiteral("settingsSearch"))->placeholderText(), QStringLiteral("Search actions"));
        const QStringList ids = pane.visibleRowIds();
        // Recent first, then Agent (the Model submenu opened inline, then the fast-agent toggle),
        // then Panes and tabs.
        QCOMPARE(ids, (QStringList{QStringLiteral("pane.splitRight"), QStringLiteral("model:Kimi K3"),
                                   QStringLiteral("model:DeepSeek V4"), QStringLiteral("agent.flashAgent"),
                                   QStringLiteral("pane.splitRight")}));
        QSettings().remove(QStringLiteral("palette/recent"));
    }

    // In Actions, an option is found but not shown as a control: its row opens Options on it.
    void anOptionFoundFromActionsOpensOptionsOnIt() {
        State state;
        SettingsPane pane(SettingsPane::Mode::Actions, [&] { return catalog(&state); }, [&] { return actions(&state); });
        int modeChanges = 0;
        pane.onModeChanged = [&] { ++modeChanges; };
        pane.show();
        pane.focusSearch();
        auto *search = pane.findChild<QLineEdit *>(QStringLiteral("settingsSearch"));
        QTest::keyClicks(search, QStringLiteral("theme"));
        QCOMPARE(pane.visibleRowIds(), QStringList{QStringLiteral("option-jump:appearance/option:theme")});
        QVERIFY(!pane.findChild<QComboBox *>());   // the control itself lives in Options
        press(search, Qt::Key_Return);
        QTest::qWait(50);
        QCOMPARE(pane.mode(), SettingsPane::Mode::Options);
        QCOMPARE(modeChanges, 1);
        QCOMPARE(pane.currentTab(), QStringLiteral("appearance"));
        QVERIFY(search->text().isEmpty());
        QCOMPARE(pane.visibleRowIds().value(pane.currentRow()), QStringLiteral("option:theme"));
        QCOMPARE(pane.focusWidget(), search);
        QCOMPARE(state.theme, QStringLiteral("dark"));   // finding it changed nothing
    }

    // A word that names both: the pane's own kind ranks first.
    void thePanesOwnKindRanksFirst() {
        State state;
        SettingsPane actionsPane(SettingsPane::Mode::Actions, [&] { return catalog(&state); }, [&] { return actions(&state); });
        actionsPane.setSearch(QStringLiteral("pla"));   // "Plans folder" (option), no action says "pla" directly
        QVERIFY(actionsPane.visibleRowIds().contains(QStringLiteral("option-jump:agent/option:plans")));
        // Several words, in any order, each matched where it fits: the section and the row.
        actionsPane.setSearch(QStringLiteral("appearance theme"));
        QCOMPARE(actionsPane.visibleRowIds(), QStringList{QStringLiteral("option-jump:appearance/option:theme")});
        actionsPane.setSearch(QStringLiteral("theme appearance"));
        QCOMPARE(actionsPane.visibleRowIds(), QStringList{QStringLiteral("option-jump:appearance/option:theme")});
        actionsPane.setSearch(QStringLiteral("theme zzzz"));
        QVERIFY(actionsPane.visibleRowIds().isEmpty());
        actionsPane.setSearch(QStringLiteral("limit"));   // only an option
        QCOMPARE(actionsPane.visibleRowIds(), QStringList{QStringLiteral("option-jump:general/option:steps")});
        SettingsPane optionsPane(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
        optionsPane.setSearch(QStringLiteral("p"));
        const QStringList ids = optionsPane.visibleRowIds();
        QVERIFY(ids.indexOf(QStringLiteral("option:plans")) >= 0);
        QVERIFY(ids.indexOf(QStringLiteral("pane.splitRight")) >= 0);
        QVERIFY(ids.indexOf(QStringLiteral("option:plans")) < ids.indexOf(QStringLiteral("pane.splitRight")));
    }

    void setModeSwapsInPlace() {
        State state;
        SettingsPane pane(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.show();
        pane.setSearch(QStringLiteral("theme"));
        pane.setMode(SettingsPane::Mode::Actions);
        QVERIFY(pane.search().isEmpty());
        QCOMPARE(pane.currentTab(), SettingsPane::actionsTabId());
        QVERIFY(pane.visibleRowIds().contains(QStringLiteral("pane.splitRight")));
        QCOMPARE(pane.findChild<QLineEdit *>(QStringLiteral("settingsSearch"))->placeholderText(), QStringLiteral("Search actions"));
        pane.setMode(SettingsPane::Mode::Options);
        QCOMPARE(pane.currentTab(), QStringLiteral("general"));
        QCOMPARE(pane.findChild<QLineEdit *>(QStringLiteral("settingsSearch"))->placeholderText(), QStringLiteral("Search options"));
        // A submenu's header is reachable from either mode.
        pane.scrollToGroup(QStringLiteral("menu:model"));
        QCOMPARE(pane.mode(), SettingsPane::Mode::Actions);
    }

    void escapeClearsThenCloses() {
        State state;
        SettingsPane pane(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
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
        SettingsPane pane(SettingsPane::Mode::Actions, [&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.resize(500, 160);   // short, so the pages scroll
        pane.show();
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
        pane.setMode(SettingsPane::Mode::Options);
        auto *spin = pane.findChild<QSpinBox *>();
        spin->setFocus();
        pane.rebuild();
        QVERIFY(qobject_cast<QSpinBox *>(pane.focusWidget()));
    }

    void controlsWriteThroughTheirRows() {
        State state;
        SettingsPane pane(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
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
        // Letters scattered across a sentence do not count; a tight abbreviation does.
        QCOMPARE(SettingsPane::fuzzyScore(QStringLiteral("reset"), QStringLiteral("Reasoning effort › low")), 0);
        QCOMPARE(SettingsPane::fuzzyScore(QStringLiteral("theme"), QStringLiteral("Cards, threads, plans and project memory")), 0);
        QVERIFY(SettingsPane::fuzzyScore(QStringLiteral("nwpn"), QStringLiteral("New pane to the right")) > 0);
    }

    // ----- Reset to defaults (owner, 2026-09-18) -------------------------------------------------

    void eachPageWithADefaultGetsOneResetRow() {
        State state;
        const QList<SettingsSection> sections = catalogWithResets(&state);
        const SettingRow row = rowById(sections.at(0), QStringLiteral("reset:general"));
        QVERIFY(!row.id.isEmpty());
        QCOMPARE(row.kind, SettingRow::Button);
        QCOMPARE(row.label, QStringLiteral("Reset to defaults"));
        QCOMPARE(row.buttonText, QStringLiteral("Reset…"));
        // It counts the rows it can put back, not the rows on the page.
        QVERIFY2(row.detail.startsWith(QStringLiteral("Puts the 2 options on this page")), qPrintable(row.detail));
        QCOMPARE(sections.at(0).rows.constLast().id, QStringLiteral("reset:general"));   // last, under what it undoes
        QVERIFY(hasRow(sections.at(1), QStringLiteral("reset:appearance")));
        // Agent: the text row only — its button and its info line stand for things, not values.
        const SettingRow one = rowById(sections.at(2), QStringLiteral("reset:agent"));
        QVERIFY2(one.detail.startsWith(QStringLiteral("Puts the one option on this page")), qPrintable(one.detail));
        // Keyboard declares no default anywhere, so it has no button at all.
        QVERIFY(!hasRow(sections.at(3), QStringLiteral("reset:keyboard")));
        for (const SettingsSection &section : sections)
            QVERIFY(!rowById(section, QStringLiteral("reset:") + section.id).reset);   // never resets itself
    }

    void aResetPutsEveryKindOfRowBackAndTouchesNoOtherPage() {
        State state;
        state.thinking = false;
        state.steps = 75;
        state.theme = QStringLiteral("light");
        state.plans = QStringLiteral("/tmp/plans");
        int count = 0;
        const QList<SettingsSection> sections = catalogWithResets(&state, true, &count);
        rowById(sections.at(0), QStringLiteral("reset:general")).run();
        QCOMPARE(state.thinking, true);                     // Toggle
        QCOMPARE(state.steps, 50);                          // Number
        QCOMPARE(count, 2);                                 // what the notice says
        QCOMPARE(state.theme, QStringLiteral("light"));     // Appearance is another page
        QCOMPARE(state.plans, QStringLiteral("/tmp/plans"));
        rowById(sections.at(1), QStringLiteral("reset:appearance")).run();
        QCOMPARE(state.theme, QStringLiteral("dark"));      // Choice
        QCOMPARE(count, 1);
        rowById(sections.at(2), QStringLiteral("reset:agent")).run();
        QVERIFY(state.plans.isEmpty());                     // Text
        QCOMPARE(state.buttonRuns, 0);                      // the Instructions button was never pressed
        QCOMPARE(state.ran, QStringList());                 // and no action ran
    }

    void theConfirmationNamesThePageAndCancelChangesNothing() {
        State state;
        state.thinking = false;
        QStringList asked;
        SettingsSection general = catalog(&state).constFirst();
        const SettingRow row = relay::resetRow(general, {}, [&asked](const QString &title) {
            asked << title;
            return false;
        });
        row.run();
        QCOMPARE(asked, QStringList{QStringLiteral("General")});   // the page, by the name on its tab
        QCOMPARE(state.thinking, false);
        // Answered no, so `after` never ran either: nothing is announced and no pane is redrawn.
        int count = -1;
        const QList<SettingsSection> sections = catalogWithResets(&state, false, &count);
        rowById(sections.constFirst(), QStringLiteral("reset:general")).run();
        QCOMPARE(state.thinking, false);
        QCOMPARE(count, -1);
    }

    void theResetRowIsReachableByKeyboardAndBySearch() {
        State state;
        state.thinking = false;
        state.theme = QStringLiteral("light");
        SettingsPane pane(SettingsPane::Mode::Options, [&] { return catalogWithResets(&state); },
                          [&] { return actions(&state); });
        pane.show();
        pane.showTab(QStringLiteral("general"));
        QCOMPARE(pane.visibleRowIds().constLast(), QStringLiteral("reset:general"));
        pane.showTab(QStringLiteral("keyboard"));
        for (const QString &id : pane.visibleRowIds())
            QVERIFY2(!id.startsWith(QStringLiteral("reset:")), qPrintable(id));
        pane.setSearch(QStringLiteral("reset defaults"));
        const QStringList found = pane.visibleRowIds();
        QVERIFY2(found.contains(QStringLiteral("reset:general")), qPrintable(found.join(QLatin1Char(' '))));
        QVERIFY(found.contains(QStringLiteral("reset:appearance")));
        pane.setSearch(QString());
        pane.showTab(QStringLiteral("general"));
        pane.focusSearch();
        auto *search = pane.findChild<QLineEdit *>(QStringLiteral("settingsSearch"));
        press(search, Qt::Key_Up);      // nothing highlighted yet: the last row on the page
        QCOMPARE(pane.visibleRowIds().value(pane.currentRow()), QStringLiteral("reset:general"));
        press(search, Qt::Key_Return);
        QCOMPARE(state.thinking, true);
        QCOMPARE(state.theme, QStringLiteral("light"));   // still another page's business
        QTest::qWait(30);                                 // the rebuild the button asks for
        QCOMPARE(pane.currentTab(), QStringLiteral("general"));
    }

    // The defaults themselves and the wiring live in RelayWindow::settingsSections(), which needs a
    // whole window to run; read it as text, the way localModelsComeRightAfterModels() does.
    void everyRowHelperDeclaresADefault() {
        QFile source(QStringLiteral(RELAY_SOURCE_DIR "/src/RelayWindow.h"));
        QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.fileName()));
        const QString text = QString::fromUtf8(source.readAll());
        QVERIFY2(text.contains(QStringLiteral("relay::resetRow(section")),
                 "settingsSections() no longer gives its pages a reset row");
        for (const QString &helper : {QStringLiteral("toggleRow"), QStringLiteral("numberRow"),
                                      QStringLiteral("textRow"), QStringLiteral("hostListRow"),
                                      QStringLiteral("choiceRow"), QStringLiteral("buttonRow")}) {
            const int start = text.indexOf(QStringLiteral("relay::SettingRow ") + helper + QLatin1Char('('));
            QVERIFY2(start > 0, qPrintable(helper));
            const int end = text.indexOf(QStringLiteral("return row;"), start);
            QVERIFY(end > start);
            const bool declares = text.mid(start, end - start).contains(QStringLiteral("row.reset"));
            // buttonRow builds a row that stands for a thing rather than a value: it must not.
            QVERIFY2(declares == (helper != QStringLiteral("buttonRow")), qPrintable(helper));
        }
    }

    // ----- Settings › Local models (card #24XJ) -------------------------------------------------

    // The placement is one line of RelayWindow::settingsSections(), which needs a whole window to
    // run; read it as text, the way tests/test_presets.py reads the preset mirror.
    void localModelsComeRightAfterModels() {
        QFile source(QStringLiteral(RELAY_SOURCE_DIR "/src/RelayWindow.h"));
        QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.fileName()));
        const QString text = QString::fromUtf8(source.readAll());
        const int models = text.indexOf(QStringLiteral("sections << models;"));
        QVERIFY2(models > 0, "the Models section is gone");
        const int next = text.indexOf(QStringLiteral("sections << "), models + 12);
        QVERIFY(next > models);
        QVERIFY2(text.mid(next).startsWith(QStringLiteral("sections << localModels().section()")),
                 qPrintable(text.mid(next, 60)));
    }

    // Nothing is probed until the section is put in front, and then once per endpoint.
    void theSectionProbesOnlyWhenItIsShown() {
        State state;
        LocalModelsSettings local;
        Wire wire;
        wire.attach(&local);
        SettingsPane pane(SettingsPane::Mode::Options, [&] {
            QList<SettingsSection> sections = catalog(&state);
            sections << local.section();
            return sections;
        }, [&] { return actions(&state); });
        pane.onSectionShown = [&](const QString &id) { if (id == LocalModelsSettings::sectionId()) local.refresh(); };
        QVERIFY(pane.tabIds().contains(LocalModelsSettings::sectionId()));
        QVERIFY(wire.sent.isEmpty());                 // General is the tab on screen
        pane.showTab(LocalModelsSettings::sectionId());
        QCOMPARE(wire.idsOf(QStringLiteral("local_endpoints")), QStringList{QStringLiteral("lm-endpoints")});
        local.handleEvent(endpointsEvent());
        QCOMPARE(wire.idsOf(QStringLiteral("local_probe")), QStringList{QStringLiteral("lm-ep:local:bonsai")});
        // Every control asks the pane to rebuild; that must not knock on the port again.
        pane.rebuild();
        pane.showTab(QStringLiteral("general"));
        pane.showTab(LocalModelsSettings::sectionId());
        QCOMPARE(wire.idsOf(QStringLiteral("local_probe")).size(), 1);
    }

    void anEndpointRowCarriesItsModelWindowAndState() {
        LocalModelsSettings local;
        Wire wire;
        wire.attach(&local);
        local.handleEvent(endpointsEvent());
        const SettingRow row = rowById(local.section(), QStringLiteral("local:bonsai"));
        QVERIFY(!row.id.isEmpty());
        QCOMPARE(row.kind, SettingRow::Buttons);
        QCOMPARE(row.label, QStringLiteral("bonsai-2-27b"));
        QCOMPARE(row.buttonTexts, (QStringList{QStringLiteral("Test"), QStringLiteral("Refresh"), QStringLiteral("Remove")}));
        QVERIFY2(row.detail.contains(QStringLiteral("131,072 tokens")), qPrintable(row.detail));
        QVERIFY(row.detail.contains(QStringLiteral("llama.cpp")));
        QVERIFY(row.detail.contains(QStringLiteral("bonsai-2-27b")));
        // No probe has answered yet, so the status word says so rather than inventing one.
        QVERIFY2(row.detail.endsWith(QStringLiteral("checking…")), qPrintable(row.detail));
    }

    void theStatusWordFollowsTheProbe() {
        LocalModelsSettings local;
        Wire wire;
        wire.attach(&local);
        local.handleEvent(endpointsEvent());
        const QList<QPair<QString, QString>> cases{{QStringLiteral("ready"), QStringLiteral("ready")},
                                                   {QStringLiteral("loading"), QStringLiteral("loading")},
                                                   {QStringLiteral("sleeping"), QStringLiteral("sleeping")},
                                                   {QStringLiteral("down"), QStringLiteral("not running")}};
        for (const auto &pair : cases) {
            local.handleEvent(probedEvent(QStringLiteral("lm-ep:local:bonsai"), QStringLiteral("http://127.0.0.1:8080/v1"),
                                          pair.first, {QStringLiteral("bonsai-2-27b")},
                                          pair.first == QStringLiteral("down")
                                              ? QStringLiteral("Nothing is listening on 127.0.0.1:8080. Start it with llama-server … --jinja")
                                              : QString()));
            const SettingsSection section = local.section();
            const SettingRow row = rowById(section, QStringLiteral("local:bonsai"));
            QVERIFY(!row.id.isEmpty());
            QVERIFY2(row.detail.endsWith(pair.second), qPrintable(row.detail + QStringLiteral(" != ") + pair.second));
            // Down: the worker's sentence, which already names the command that starts the server.
            const SettingRow error = rowById(section, QStringLiteral("local:bonsai/error"));
            if (pair.first == QStringLiteral("down")) {
                QVERIFY(!error.id.isEmpty());
                QVERIFY(error.label.contains(QStringLiteral("llama-server")));
            } else {
                QVERIFY(error.id.isEmpty());
            }
        }
    }

    void theRowsActionsReachTheWorker() {
        LocalModelsSettings local;
        Wire wire;
        wire.attach(&local);
        local.handleEvent(endpointsEvent());
        const SettingRow row = rowById(local.section(), QStringLiteral("local:bonsai"));
        QVERIFY(!row.id.isEmpty() && row.onButton);
        row.onButton(0);                                   // Test
        QCOMPARE(wire.last(QStringLiteral("test_key")).value(QStringLiteral("preset")).toString(),
                 QStringLiteral("local:bonsai"));
        local.handleEvent({{"event", "key_tested"}, {"preset", "local:bonsai"}, {"ok", true}, {"elapsed_ms", 812}});
        QVERIFY2(local.noteFor(QStringLiteral("local:bonsai")).contains(QStringLiteral("812 ms")),
                 qPrintable(local.noteFor(QStringLiteral("local:bonsai"))));
        QVERIFY(hasRow(local.section(), QStringLiteral("local:bonsai/note")));

        row.onButton(1);                                   // Refresh: the window is re-read
        const QJsonObject refresh = wire.last(QStringLiteral("local_endpoint_save"));
        QVERIFY(refresh.value(QStringLiteral("detect")).toBool());
        QCOMPARE(refresh.value(QStringLiteral("endpoint")).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("local:bonsai"));

        row.onButton(2);                                   // Remove
        const QJsonObject remove = wire.last(QStringLiteral("local_endpoint_delete"));
        QCOMPARE(remove.value(QStringLiteral("endpoint_id")).toString(), QStringLiteral("local:bonsai"));
        const int before = wire.presets;
        local.handleEvent({{"event", "local_endpoint_deleted"}, {"endpoint_id", "local:bonsai"}, {"removed", true}});
        QCOMPARE(wire.presets, before + 1);                 // every pane re-reads `presets`
        QVERIFY(local.endpoints().isEmpty());
    }

    void theRecoveryToggleSavesTheFlagAndReadsBackWhatWasStored() {
        LocalModelsSettings local;
        Wire wire;
        wire.attach(&local);
        local.handleEvent(endpointsEvent());
        const SettingRow recovery = rowById(local.section(), QStringLiteral("local:bonsai/tool_text_recovery"));
        QVERIFY(!recovery.id.isEmpty() && recovery.onToggle);
        QCOMPARE(recovery.kind, SettingRow::Toggle);
        QVERIFY(!recovery.checked);                        // off by default
        QVERIFY2(recovery.detail.startsWith(QStringLiteral("Runs a command out of the model's text.")),
                 qPrintable(recovery.detail));
        recovery.onToggle(true);
        const QJsonObject save = wire.last(QStringLiteral("local_endpoint_save"));
        QVERIFY(!save.contains(QStringLiteral("detect")));
        QVERIFY(save.value(QStringLiteral("endpoint")).toObject().value(QStringLiteral("tool_text_recovery")).toBool());
        QJsonObject stored = bonsai();
        stored.insert(QStringLiteral("tool_text_recovery"), true);
        local.handleEvent({{"event", "local_endpoint_saved"}, {"id", "lm-save:local:bonsai"}, {"endpoint", stored}});
        QVERIFY(rowById(local.section(), QStringLiteral("local:bonsai/tool_text_recovery")).checked);

        // The sibling field. The registry keeps the keys it knows and drops the rest, so a toggle
        // the backend has not learnt yet goes back to off: the row is drawn from what came back.
        const SettingRow objects = rowById(local.section(), QStringLiteral("local:bonsai/tool_arguments_as_object"));
        QVERIFY(!objects.id.isEmpty() && objects.onToggle);
        QVERIFY(!objects.checked);
        objects.onToggle(true);
        QVERIFY(wire.last(QStringLiteral("local_endpoint_save")).value(QStringLiteral("endpoint")).toObject()
                    .value(QStringLiteral("tool_arguments_as_object")).toBool());
        local.handleEvent({{"event", "local_endpoint_saved"}, {"id", "lm-save:local:bonsai"}, {"endpoint", stored}});
        QVERIFY(!rowById(local.section(), QStringLiteral("local:bonsai/tool_arguments_as_object")).checked);
    }

    void findServersKnocksOnFourPortsAndOffersToSaveWhatAnswered() {
        LocalModelsSettings local;
        Wire wire;
        wire.attach(&local);
        const SettingRow find = rowById(local.section(), QStringLiteral("local:find"));
        QVERIFY(!find.id.isEmpty() && find.run);
        find.run();
        QCOMPARE(wire.idsOf(QStringLiteral("local_probe")),
                 (QStringList{QStringLiteral("lm-find:11434"), QStringLiteral("lm-find:1234"),
                              QStringLiteral("lm-find:8080"), QStringLiteral("lm-find:8000")}));
        local.handleEvent(probedEvent(QStringLiteral("lm-find:8080"), QStringLiteral("http://127.0.0.1:8080/v1"),
                                      QStringLiteral("sleeping"), {QStringLiteral("bonsai-2-27b")}));
        local.handleEvent(probedEvent(QStringLiteral("lm-find:1234"), QStringLiteral("http://127.0.0.1:1234/v1"),
                                      QStringLiteral("down"), {}, QStringLiteral("Nothing is listening.")));
        const SettingsSection section = local.section();
        const SettingRow found = rowById(section, QStringLiteral("found:0"));
        QVERIFY(!found.id.isEmpty());
        QVERIFY(found.label.contains(QStringLiteral("http://127.0.0.1:8080/v1")));
        QVERIFY2(found.detail.contains(QStringLiteral("131,072 tokens")), qPrintable(found.detail));
        QVERIFY(found.detail.endsWith(QStringLiteral("sleeping")));
        QCOMPARE(found.buttonTexts, QStringList{QStringLiteral("Save")});
        QVERIFY(hasRow(section, QStringLiteral("local:find/silent")));
        found.onButton(0);
        const QJsonObject save = wire.last(QStringLiteral("local_endpoint_save"));
        QCOMPARE(save.value(QStringLiteral("id")).toString(), QStringLiteral("lm-find-save"));
        QVERIFY(save.value(QStringLiteral("detect")).toBool());
        const QJsonObject endpoint = save.value(QStringLiteral("endpoint")).toObject();
        QCOMPARE(endpoint.value(QStringLiteral("model")).toString(), QStringLiteral("bonsai-2-27b"));
        QCOMPARE(endpoint.value(QStringLiteral("base_url")).toString(), QStringLiteral("http://127.0.0.1:8080/v1"));
        const int before = wire.presets;
        local.handleEvent({{"event", "local_endpoint_saved"}, {"id", "lm-find-save"}, {"endpoint", bonsai()},
                           {"probe", probedEvent(QString(), QStringLiteral("http://127.0.0.1:8080/v1"),
                                                 QStringLiteral("ready"), {QStringLiteral("bonsai-2-27b")})}});
        QCOMPARE(wire.presets, before + 1);
        QCOMPARE(local.endpoints().size(), 1);
        QVERIFY(!hasRow(local.section(), QStringLiteral("found:0")));      // the offer is spent
        QVERIFY(rowById(local.section(), QStringLiteral("local:bonsai")).detail.endsWith(QStringLiteral("ready")));
    }

    void aServerWithSeveralModelsAsksWhichOneFirst() {
        LocalModelsSettings local;
        Wire wire;
        wire.attach(&local);
        rowById(local.section(), QStringLiteral("local:find")).run();
        local.handleEvent(probedEvent(QStringLiteral("lm-find:11434"), QStringLiteral("http://127.0.0.1:11434/v1"),
                                      QStringLiteral("ready"),
                                      {QStringLiteral("muse-glimmer:latest"), QStringLiteral("qwen3:8b")}));
        const SettingRow choice = rowById(local.section(), QStringLiteral("found:0/model"));
        QVERIFY(!choice.id.isEmpty() && choice.onChoose);
        QCOMPARE(choice.kind, SettingRow::Choice);
        QCOMPARE(choice.current, QStringLiteral("muse-glimmer:latest"));
        choice.onChoose(QStringLiteral("qwen3:8b"));
        rowById(local.section(), QStringLiteral("found:0")).onButton(0);
        QCOMPARE(wire.last(QStringLiteral("local_endpoint_save")).value(QStringLiteral("endpoint")).toObject()
                     .value(QStringLiteral("model")).toString(), QStringLiteral("qwen3:8b"));
    }

    void addByAddressDetectsThenSaves() {
        LocalModelsSettings local;
        Wire wire;
        wire.attach(&local);
        const SettingRow address = rowById(local.section(), QStringLiteral("local:address"));
        QVERIFY(!address.id.isEmpty() && address.onText);
        address.onText(QStringLiteral("http://127.0.0.1:8080"));
        rowById(local.section(), QStringLiteral("local:detect")).run();
        const QJsonObject probe = wire.last(QStringLiteral("local_probe"));
        QCOMPARE(probe.value(QStringLiteral("id")).toString(), QStringLiteral("lm-addr"));
        QCOMPARE(probe.value(QStringLiteral("base_url")).toString(), QStringLiteral("http://127.0.0.1:8080"));
        local.handleEvent(probedEvent(QStringLiteral("lm-addr"), QStringLiteral("http://127.0.0.1:8080/v1"),
                                      QStringLiteral("ready"), {QStringLiteral("bonsai-2-27b")}));
        const SettingRow save = rowById(local.section(), QStringLiteral("local:address/save"));
        QVERIFY(!save.id.isEmpty());
        QVERIFY(save.detail.contains(QStringLiteral("131,072 tokens")));
        save.onButton(0);
        QCOMPARE(wire.last(QStringLiteral("local_endpoint_save")).value(QStringLiteral("id")).toString(),
                 QStringLiteral("lm-addr-save"));
        // A detect that finds nothing says so in place, and offers nothing to save.
        local.handleEvent({{"event", "error"}, {"id", "lm-addr-save"}, {"text", "Nothing to detect at that address."}});
        QVERIFY(hasRow(local.section(), QStringLiteral("local:address/note")));
    }

    void theSetupButtonHandsThePromptToTheAgent() {
        LocalModelsSettings local;
        Wire wire;
        wire.attach(&local);
        const SettingRow row = rowById(local.section(), QStringLiteral("agent.localModelSetup"));
        QVERIFY(!row.id.isEmpty() && row.run);
        row.run();
        QCOMPARE(wire.setups, 1);
        QVERIFY(wire.sent.isEmpty());       // it is a prompt, not a worker message
    }

    // The new row kind draws one button per entry and Enter presses the first.
    void aButtonsRowDrawsEveryButton() {
        State state;
        LocalModelsSettings local;
        Wire wire;
        wire.attach(&local);
        local.handleEvent(endpointsEvent());
        SettingsPane pane(SettingsPane::Mode::Options, [&] {
            QList<SettingsSection> sections = catalog(&state);
            sections << local.section();
            return sections;
        }, [&] { return actions(&state); });
        pane.show();
        pane.showTab(LocalModelsSettings::sectionId());
        QVERIFY(pane.visibleRowIds().contains(QStringLiteral("local:bonsai")));
        QWidget *line = nullptr;
        for (QWidget *candidate : pane.findChildren<QWidget *>(QStringLiteral("settingsRow")))
            if (candidate->property("rowId").toString() == QLatin1String("local:bonsai")) line = candidate;
        QVERIFY(line);
        const auto buttons = line->findChildren<QPushButton *>();
        QCOMPARE(buttons.size(), 3);
        QCOMPARE(buttons.at(0)->text(), QStringLiteral("Test"));
        QCOMPARE(buttons.at(2)->text(), QStringLiteral("Remove"));
        const int sent = wire.sent.size();
        buttons.at(2)->click();
        QCOMPARE(wire.sent.size(), sent + 1);
        QCOMPARE(wire.last(QStringLiteral("local_endpoint_delete")).value(QStringLiteral("endpoint_id")).toString(),
                 QStringLiteral("local:bonsai"));
    }

    // ----- what a changed row says, and the way back from it (card #XZZB) -------------------------

    void aRowAwayFromItsDefaultCarriesAResetMarkAndPutsADotOnItsTab() {
        State state;
        state.thinking = false;                      // General: one row is not what Relay ships with
        SettingsPane pane(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.show();
        auto *tabs = pane.findChild<QTabBar *>(QStringLiteral("settingsTabs"));
        QVERIFY(tabs);
        QCOMPARE(tabs->tabText(0), QStringLiteral("General •"));
        QCOMPARE(tabs->tabText(1), QStringLiteral("Appearance"));     // nothing on that page was touched
        QVERIFY(rowButton(pane, QStringLiteral("option:thinking"), QStringLiteral("settingsRowReset")));
        QVERIFY(!rowButton(pane, QStringLiteral("option:steps"), QStringLiteral("settingsRowReset")));
        // Keyboard's row declares no default at all, so there is nothing to offer and no dot.
        QVERIFY(!rowButton(pane, QStringLiteral("option:hints"), QStringLiteral("settingsRowReset")));
        QCOMPARE(tabs->tabText(3), QStringLiteral("Keyboard"));

        // The mark is the way back: one click, no question asked, and the row stops being marked.
        rowButton(pane, QStringLiteral("option:thinking"), QStringLiteral("settingsRowReset"))->click();
        QCOMPARE(state.thinking, true);
        QTest::qWait(30);                            // the redraw the write asks for
        QVERIFY(!rowButton(pane, QStringLiteral("option:thinking"), QStringLiteral("settingsRowReset")));
        QCOMPARE(pane.findChild<QTabBar *>(QStringLiteral("settingsTabs"))->tabText(0), QStringLiteral("General"));
    }

    void theBrowseButtonOnAPathRowWritesWhatWasPicked() {
        State state;
        QStringList openedAt;
        SettingsPane::setFolderChooser([&openedAt](QWidget *, const QString &start) {
            openedAt << start;
            return QStringLiteral("/srv/plans");
        });
        SettingsPane pane(SettingsPane::Mode::Options, [&] { return catalog(&state); }, [&] { return actions(&state); });
        pane.show();
        pane.showTab(QStringLiteral("agent"));
        QVERIFY(!rowButton(pane, QStringLiteral("option:steps"), QStringLiteral("settingsBrowse")));   // not a path
        auto *browse = rowButton(pane, QStringLiteral("option:plans"), QStringLiteral("settingsBrowse"));
        QVERIFY(browse);
        browse->click();
        QCOMPARE(state.plans, QStringLiteral("/srv/plans"));
        QCOMPARE(openedAt, QStringList{QDir::homePath()});      // an empty box means home
        QTest::qWait(30);
        // Chosen once, it opens where the row now points; cancelling leaves the row as it is.
        SettingsPane::setFolderChooser([&openedAt](QWidget *, const QString &start) {
            openedAt << start;
            return QString();
        });
        rowButton(pane, QStringLiteral("option:plans"), QStringLiteral("settingsBrowse"))->click();
        QCOMPARE(openedAt.size(), 2);
        QCOMPARE(openedAt.at(1), QStringLiteral("/srv/plans"));
        QCOMPARE(state.plans, QStringLiteral("/srv/plans"));
        SettingsPane::setFolderChooser({});                     // the real picker again
    }

    // ----- a value is never one edit out of date (finding 3 of card #XZZB) ------------------------

    void everyOpenPaneRedrawsWhenAValueIsWrittenAnywhereElse() {
        State state;
        auto sections = [&] { return catalog(&state); };
        auto items = [&] { return actions(&state); };
        SettingsPane first(SettingsPane::Mode::Options, sections, items);
        SettingsPane second(SettingsPane::Mode::Options, sections, items);
        first.show();
        second.show();
        auto thinkingBox = [](const SettingsPane &pane) {
            QWidget *line = rowWidget(pane, QStringLiteral("option:thinking"));
            return line ? line->findChild<QCheckBox *>() : nullptr;
        };
        QVERIFY(thinkingBox(first) && thinkingBox(first)->isChecked());
        QVERIFY(thinkingBox(second)->isChecked());

        // An edit in one pane reaches the other — the second pane is in another tab, or another
        // window, and used to sit there showing what the setting used to be.
        thinkingBox(first)->toggle();
        QCOMPARE(state.thinking, false);
        QTest::qWait(30);
        QVERIFY(!thinkingBox(second)->isChecked());

        // And a write from outside both panes — a dialog, a page reset, a keymap reload — reaches
        // them the same way, through the watch.
        state.theme = QStringLiteral("light");
        relay::SettingsWatch::instance().notify();
        QTest::qWait(30);
        for (SettingsPane *pane : {&first, &second}) {
            pane->showTab(QStringLiteral("appearance"));
            QWidget *line = rowWidget(*pane, QStringLiteral("option:theme"));
            auto *combo = line ? line->findChild<QComboBox *>() : nullptr;
            QVERIFY(combo);
            QCOMPARE(combo->currentData().toString(), QStringLiteral("light"));
        }
    }

    void theWatchForgetsAPaneThatHasClosed() {
        State state;
        {
            SettingsPane closing(SettingsPane::Mode::Options, [&] { return catalog(&state); },
                                 [&] { return actions(&state); });
            closing.show();
        }
        relay::SettingsWatch::instance().notify();
        QTest::qWait(30);                            // nothing to deliver to: no dangling callback
        QVERIFY(true);
    }
};

QTEST_MAIN(SettingsPaneTests)
#include "settingspane_test.moc"
