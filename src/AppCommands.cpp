// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AppCommands.h"
#include "Notifications.h"

#include <QJsonArray>
#include <QSet>

namespace relay {
namespace appcommands {

bool actionIsAgentSafe(const QString &key) {
    // Owner decision 2 (2026-09-20), the line being "undoable in one click". Everything not named
    // here is off, including every action added after this list was written: opt-in has to mean
    // that adding an action does not widen what an agent may do.
    static const QSet<QString> safe = {
        // Open or reveal something. Nothing is changed and the pane closes in one click.
        QStringLiteral("app.settings"),        // Options
        QStringLiteral("palette.open"),        // Actions
        QStringLiteral("palette.agent"),
        QStringLiteral("board.open"),          // the Switchboard
        QStringLiteral("conversations.open"),  // the session manager
        QStringLiteral("closed.list"),
        QStringLiteral("files.explorer"),
        QStringLiteral("agent.info"),
        QStringLiteral("agent.internalsPane"),
        QStringLiteral("agent.requests"),
        QStringLiteral("agent.subagentPane"),
        QStringLiteral("agent.thinkingPanel"),
        QStringLiteral("agent.agentsMenu"),
        QStringLiteral("help.shortcuts"),
        QStringLiteral("find.inView"),
        QStringLiteral("links.step"),
        QStringLiteral("notifications.jump"),
        // Move the focus. Moving it back is the undo.
        QStringLiteral("pane.focusUp"), QStringLiteral("pane.focusDown"),
        QStringLiteral("pane.focusLeft"), QStringLiteral("pane.focusRight"),
        QStringLiteral("tab.next"), QStringLiteral("tab.previous"),
        QStringLiteral("window.next"), QStringLiteral("window.previous"),
        // Re-read a file that is already on disk: a refresh, like Local models' Detect.
        QStringLiteral("keybindings.reload"), QStringLiteral("theme.reload"),
        QStringLiteral("agents.reload"),
    };
    return safe.contains(key);
}

QString rowActionKey(const QString &sectionId, const QString &rowId, int button) {
    const QString key = QStringLiteral("row:") + sectionId + QLatin1Char('/') + rowId;
    return button > 0 ? key + QLatin1Char('#') + QString::number(button) : key;
}

QJsonValue rowValue(const SettingRow &row) {
    switch (row.kind) {
    case SettingRow::Toggle: return row.checked;
    case SettingRow::Choice: return row.current;
    case SettingRow::Text: return row.text;
    case SettingRow::Number: return row.number;
    default: return QJsonValue(QJsonValue::Undefined);
    }
}

QString valueText(const QJsonValue &value) {
    if (value.isBool()) return value.toBool() ? QStringLiteral("on") : QStringLiteral("off");
    if (value.isDouble()) return QString::number(value.toDouble());
    if (value.isString()) return value.toString().isEmpty() ? QStringLiteral("empty") : value.toString();
    return QStringLiteral("—");
}

namespace {

bool holdsValue(SettingRow::Kind kind) {
    return kind == SettingRow::Toggle || kind == SettingRow::Choice
        || kind == SettingRow::Text || kind == SettingRow::Number;
}

QString kindName(SettingRow::Kind kind) {
    switch (kind) {
    case SettingRow::Toggle: return QStringLiteral("toggle");
    case SettingRow::Choice: return QStringLiteral("choice");
    case SettingRow::Text: return QStringLiteral("text");
    case SettingRow::Number: return QStringLiteral("number");
    case SettingRow::Button: return QStringLiteral("button");
    case SettingRow::Buttons: return QStringLiteral("buttons");
    case SettingRow::Info: return QStringLiteral("info");
    case SettingRow::Heading: return QStringLiteral("heading");
    }
    return QStringLiteral("info");
}

// §30.1 writes the request id and the row id both as `id`, which no JSON object can carry at once,
// so the row id is read from whichever of these the worker used. The request id stays `id`, since
// that is what the result is matched by and every field of it is unambiguous.
QString target(const QJsonObject &command, const char *first, const char *second = nullptr) {
    for (const char *name : {first, second}) {
        if (!name) continue;
        const QJsonValue value = command.value(QLatin1String(name));
        if (value.isString() && !value.toString().isEmpty()) return value.toString();
    }
    const QJsonObject args = command.value(QStringLiteral("args")).toObject();
    for (const char *name : {first, second}) {
        if (!name) continue;
        const QString value = args.value(QLatin1String(name)).toString();
        if (!value.isEmpty()) return value;
    }
    return {};
}

}  // namespace
}  // namespace appcommands

// ----- the catalog (§30.2) ------------------------------------------------------------------------

QJsonObject AppCommands::catalog(const QString &tab) const {
    using namespace appcommands;
    QJsonObject app;
    app.insert(QStringLiteral("tab"), tab);
    app.insert(QStringLiteral("writes_enabled"), !writesEnabled || writesEnabled());

    QJsonArray options, actionRows;
    const QList<SettingsSection> catalogSections = sections ? sections() : QList<SettingsSection>();
    for (const SettingsSection &section : catalogSections) {
        for (const SettingRow &row : section.rows) {
            if (row.id.isEmpty()) continue;
            const bool value = holdsValue(row.kind);
            QJsonObject entry{{QStringLiteral("id"), row.id},
                              {QStringLiteral("section"), section.id},
                              {QStringLiteral("section_label"), section.title},
                              {QStringLiteral("label"), row.label},
                              {QStringLiteral("detail"), row.detail},
                              {QStringLiteral("kind"), kindName(row.kind)},
                              // Owner decision 1: every value row except a secret. A button is not
                              // a value; it is an action, and is listed as one below.
                              {QStringLiteral("settable"), value && !row.secret}};
            if (row.secret) entry.insert(QStringLiteral("secret"), true);
            // A secret row's value never leaves this process, in either direction (§30.8).
            if (value && !row.secret) entry.insert(QStringLiteral("value"), rowValue(row));
            if (row.kind == SettingRow::Choice) {
                QJsonArray choices;
                for (int i = 0; i < row.options.size(); ++i)
                    choices.append(QJsonObject{{QStringLiteral("value"), row.options.at(i)},
                                               {QStringLiteral("label"), i < row.optionLabels.size()
                                                    ? row.optionLabels.at(i) : row.options.at(i)}});
                entry.insert(QStringLiteral("choices"), choices);
            }
            if (row.kind == SettingRow::Number && row.maximum > row.minimum) {
                entry.insert(QStringLiteral("min"), row.minimum);
                entry.insert(QStringLiteral("max"), row.maximum);
            }
            options.append(entry);

            // Owner decision 1 again, from the other end: "Button rows are not values; they are
            // actions". Test a key, Local models' Refresh and Detect, copy this page and the model
            // reorder arrows are buttons on rows, not palette actions, and they are exactly the
            // reversible things decision 2 names — so they are listed in the action catalog under
            // a `row:` key and run through `run_action` like any other action.
            if (row.kind == SettingRow::Button && row.run) {
                actionRows.append(QJsonObject{
                    {QStringLiteral("key"), rowActionKey(section.id, row.id)},
                    {QStringLiteral("section"), section.title},
                    {QStringLiteral("label"), row.buttonText.isEmpty() ? row.label
                                                : row.label + QStringLiteral(" · ") + row.buttonText},
                    {QStringLiteral("detail"), row.detail},
                    {QStringLiteral("agent_safe"), row.agentSafeButtons.contains(0)}});
            } else if (row.kind == SettingRow::Buttons && row.onButton) {
                for (int i = 0; i < row.buttonTexts.size(); ++i)
                    actionRows.append(QJsonObject{
                        {QStringLiteral("key"), rowActionKey(section.id, row.id, i)},
                        {QStringLiteral("section"), section.title},
                        {QStringLiteral("label"), row.label + QStringLiteral(" · ") + row.buttonTexts.at(i)},
                        {QStringLiteral("detail"), row.detail},
                        {QStringLiteral("agent_safe"), row.agentSafeButtons.contains(i)}});
            }
        }
    }

    const auto appendAction = [&actionRows](const ActionItem &item) {
        if (item.key.isEmpty()) return;
        actionRows.append(QJsonObject{{QStringLiteral("key"), item.key},
                                      {QStringLiteral("section"), item.section},
                                      {QStringLiteral("label"), item.label},
                                      {QStringLiteral("detail"), item.detail},
                                      // The table is the policy and the field is the override, so a
                                      // submenu's children — built fresh each time the menu is read
                                      // — need no marking of their own.
                                      {QStringLiteral("agent_safe"),
                                       item.agentSafe || actionIsAgentSafe(item.key)}});
    };
    for (const ActionItem &item : actions ? actions() : QList<ActionItem>()) {
        appendAction(item);
        // A submenu is not runnable itself; its children are the actions. One level, exactly as
        // the Actions pane draws them.
        if (item.children) for (const ActionItem &child : item.children()) appendAction(child);
    }

    app.insert(QStringLiteral("options"), options);
    app.insert(QStringLiteral("actions"), actionRows);
    return app;
}

// ----- lookups ------------------------------------------------------------------------------------

bool AppCommands::findRow(const QString &rowId, SettingRow *row, QString *sectionId) const {
    if (rowId.isEmpty() || !sections) return false;
    for (const SettingsSection &section : sections())
        for (const SettingRow &candidate : section.rows) {
            if (candidate.id != rowId) continue;
            if (row) *row = candidate;
            if (sectionId) *sectionId = section.id;
            return true;
        }
    return false;
}

bool AppCommands::findAction(const QString &key, ActionItem *item, bool *agentSafe) const {
    if (key.isEmpty()) return false;
    // A Button/Buttons row of the Options pane, wrapped as an action (see catalog()).
    if (key.startsWith(QStringLiteral("row:")) && sections) {
        for (const SettingsSection &section : sections())
            for (const SettingRow &row : section.rows) {
                if (row.kind != SettingRow::Button && row.kind != SettingRow::Buttons) continue;
                const int buttons = row.kind == SettingRow::Button ? 1 : int(row.buttonTexts.size());
                for (int i = 0; i < buttons; ++i) {
                    if (appcommands::rowActionKey(section.id, row.id, i) != key) continue;
                    if (agentSafe) *agentSafe = row.agentSafeButtons.contains(i);
                    if (item) {
                        ActionItem wrapped;
                        wrapped.key = key;
                        wrapped.section = section.title;
                        wrapped.label = row.kind == SettingRow::Button
                            ? (row.buttonText.isEmpty() ? row.label : row.label + QStringLiteral(" · ") + row.buttonText)
                            : row.label + QStringLiteral(" · ") + row.buttonTexts.value(i);
                        wrapped.detail = row.detail;
                        wrapped.agentSafe = row.agentSafeButtons.contains(i);
                        if (row.kind == SettingRow::Button) wrapped.run = row.run;
                        else if (row.onButton) wrapped.run = [fn = row.onButton, i] { fn(i); };
                        *item = wrapped;
                    }
                    return true;
                }
            }
        return false;
    }
    if (!actions) return false;
    const auto match = [&](const ActionItem &candidate) {
        if (candidate.key != key || !candidate.run) return false;
        if (item) *item = candidate;
        if (agentSafe) *agentSafe = candidate.agentSafe || appcommands::actionIsAgentSafe(candidate.key);
        return true;
    };
    for (const ActionItem &candidate : actions()) {
        if (match(candidate)) return true;
        if (candidate.children) for (const ActionItem &child : candidate.children()) if (match(child)) return true;
    }
    return false;
}

// ----- writing a row ------------------------------------------------------------------------------

bool AppCommands::writeRow(const SettingRow &row, const QJsonValue &value, QString *error) const {
    const auto refuse = [error](const QString &word) { if (error) *error = word; return false; };
    switch (row.kind) {
    case SettingRow::Toggle:
        if (!value.isBool()) return refuse(QStringLiteral("invalid_value"));
        if (!row.onToggle) return refuse(QStringLiteral("failed"));
        row.onToggle(value.toBool());
        break;
    case SettingRow::Choice: {
        if (!value.isString()) return refuse(QStringLiteral("invalid_value"));
        const QString picked = value.toString();
        if (!row.options.contains(picked)) return refuse(QStringLiteral("invalid_value"));
        if (!row.onChoose) return refuse(QStringLiteral("failed"));
        row.onChoose(picked);
        break;
    }
    case SettingRow::Number: {
        if (!value.isDouble()) return refuse(QStringLiteral("invalid_value"));
        const int number = qRound(value.toDouble());
        // minimum == maximum is how an unbounded row is written; only a real range bounds it.
        if (row.maximum > row.minimum && (number < row.minimum || number > row.maximum))
            return refuse(QStringLiteral("invalid_value"));
        if (!row.onNumber) return refuse(QStringLiteral("failed"));
        row.onNumber(number);
        break;
    }
    case SettingRow::Text: {
        if (!value.isString()) return refuse(QStringLiteral("invalid_value"));
        const QString text = value.toString();
        for (const QChar &character : text)
            if (character.category() == QChar::Other_Control) return refuse(QStringLiteral("invalid_value"));
        if (!row.onText) return refuse(QStringLiteral("failed"));
        row.onText(text);
        break;
    }
    default:
        return refuse(QStringLiteral("not_settable"));
    }
    // Every Options pane alive redraws, exactly as it does for an edit made in another pane: the
    // value the person is looking at must never be one write behind (SettingsWatch).
    SettingsWatch::instance().notify();
    return true;
}

// ----- executing a command (§30.3) ------------------------------------------------------------------

QJsonObject AppCommands::execute(const QJsonObject &command, const QString &who) {
    using namespace appcommands;
    QJsonObject result{{QStringLiteral("type"), QStringLiteral("app_command_result")},
                       {QStringLiteral("id"), command.value(QStringLiteral("id")).toString()}};
    const auto refuse = [&result](const QString &error, const QString &sentence = QString()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), error);
        if (!sentence.isEmpty()) result.insert(QStringLiteral("message"), sentence);
        return result;
    };
    const QString what = command.value(QStringLiteral("command")).toString();
    const bool writes = !writesEnabled || writesEnabled();

    // `open` changes nothing, so it is offered whatever the Options › Agent toggle says (§30.4).
    if (what == QStringLiteral("open")) {
        QString error = QStringLiteral("unknown_target");
        if (!openTarget || !openTarget(command, &error)) return refuse(error);
        result.insert(QStringLiteral("ok"), true);
        return result;
    }

    if (what == QStringLiteral("undo")) {
        // Allowed however `writes_enabled` stands: it can only revert a change an agent already
        // made, and the toggle exists so an agent cannot change the app — never so a change it
        // made cannot be taken back (§30.4).
        const QString changeId = target(command, "change_id", "change");
        QString error;
        QJsonValue previous, value;
        if (!undo(changeId, who, &error, &previous, &value)) return refuse(error);
        result.insert(QStringLiteral("ok"), true);
        result.insert(QStringLiteral("previous"), previous);
        result.insert(QStringLiteral("value"), value);
        result.insert(QStringLiteral("change_id"), changeId);
        return result;
    }

    if (what == QStringLiteral("set_option")) {
        if (!writes) return refuse(QStringLiteral("writes_disabled"));
        const QString rowId = target(command, "row", "option");
        SettingRow row;
        QString sectionId;
        if (!findRow(rowId, &row, &sectionId)) return refuse(QStringLiteral("unknown_row"));
        // The catalog the worker holds is a snapshot; the row is the truth, so both markers are
        // checked again here (§30.3).
        if (row.secret) return refuse(QStringLiteral("secret"));
        if (!holdsValue(row.kind)) return refuse(QStringLiteral("not_settable"));
        const QJsonValue before = rowValue(row);
        QString error;
        if (!writeRow(row, command.value(QStringLiteral("value")), &error))
            return refuse(error, error == QStringLiteral("failed")
                                     ? QStringLiteral("%1 has no writer.").arg(row.label) : QString());
        // Read back, never echo the request: a writer may clamp, normalise or refuse a value, and
        // the transcript must say what the setting is now (§30.3).
        SettingRow after = row;
        findRow(rowId, &after, nullptr);

        AppChange change;
        change.id = QStringLiteral("c%1").arg(m_nextChange++);
        change.key = rowId;
        change.label = row.label;
        change.previous = before;
        change.value = rowValue(after);
        change.when = QDateTime::currentDateTime();
        change.who = who;
        change.turnId = command.value(QStringLiteral("turn_id")).toString();
        // The row carries a marker in every Options pane until the person touches it (§30.6).
        SettingsPane::markAgentChanged(rowId, valueText(change.previous), valueText(change.value));
        change.noteId = NotificationCenter::instance().postWithAction(
            QStringLiteral("Agent changed %1").arg(change.label),
            QStringLiteral("%1 → %2").arg(valueText(change.previous), valueText(change.value)),
            NotificationCenter::kindInfo, QString(), QStringLiteral("Undo"), undoActionId(change.id));
        m_changes.append(change);
        while (m_changes.size() > kMaxChanges) m_changes.removeFirst();

        result.insert(QStringLiteral("ok"), true);
        result.insert(QStringLiteral("previous"), change.previous);
        result.insert(QStringLiteral("value"), change.value);
        result.insert(QStringLiteral("change_id"), change.id);
        return result;
    }

    if (what == QStringLiteral("run_action")) {
        if (!writes) return refuse(QStringLiteral("writes_disabled"));
        const QString key = target(command, "key", "action");
        ActionItem item;
        bool agentSafe = false;
        if (!findAction(key, &item, &agentSafe)) return refuse(QStringLiteral("unknown_action"));
        if (!agentSafe) return refuse(QStringLiteral("not_agent_safe"));
        if (!item.run) return refuse(QStringLiteral("failed"), QStringLiteral("%1 cannot be run from here.").arg(item.label));
        item.run();

        AppChange change;
        change.id = QStringLiteral("c%1").arg(m_nextChange++);
        change.action = true;
        change.key = key;
        change.label = item.label;
        change.when = QDateTime::currentDateTime();
        change.who = who;
        change.turnId = command.value(QStringLiteral("turn_id")).toString();
        // No Undo on an action: what it did is its own to take back, and offering a button that
        // cannot keep its promise is worse than offering none (§30.6).
        change.noteId = NotificationCenter::instance().post(
            QStringLiteral("Agent ran %1").arg(change.label), item.detail);
        m_changes.append(change);
        while (m_changes.size() > kMaxChanges) m_changes.removeFirst();

        result.insert(QStringLiteral("ok"), true);
        return result;
    }

    return refuse(QStringLiteral("unknown_target"), QStringLiteral("No such command: %1.").arg(what));
}

// ----- the change log (§30.6) ---------------------------------------------------------------------

const AppChange *AppCommands::findChange(const QString &id) const {
    for (const AppChange &change : m_changes) if (change.id == id) return &change;
    return nullptr;
}

QString AppCommands::undoActionId(const QString &changeId) {
    return QStringLiteral("appundo:") + changeId;
}

QString AppCommands::changeIdOfAction(const QString &actionId) {
    return actionId.startsWith(QStringLiteral("appundo:")) ? actionId.mid(8) : QString();
}

bool AppCommands::undo(const QString &changeId, const QString &who, QString *error,
                       QJsonValue *previous, QJsonValue *value) {
    using namespace appcommands;
    const auto refuse = [error](const QString &word) { if (error) *error = word; return false; };
    const AppChange *found = findChange(changeId);
    if (!found) return refuse(QStringLiteral("unknown_change"));
    if (found->action) return refuse(QStringLiteral("unknown_change"));   // an action is not a value to put back
    if (found->undone) return refuse(QStringLiteral("unknown_change"));
    const AppChange original = *found;                                   // the list moves below

    SettingRow row;
    if (!findRow(original.key, &row, nullptr)) return refuse(QStringLiteral("unknown_row"));
    if (row.secret) return refuse(QStringLiteral("secret"));
    if (!holdsValue(row.kind)) return refuse(QStringLiteral("not_settable"));
    const QJsonValue before = rowValue(row);
    QString wrote;
    if (!writeRow(row, original.previous, &wrote)) return refuse(wrote);
    SettingRow after = row;
    findRow(original.key, &after, nullptr);

    for (AppChange &change : m_changes) if (change.id == changeId) change.undone = true;
    // The undo is itself an entry: the log is the history of what happened to the setting, not
    // only of what the agent did, so "who put this back and when" is answerable too.
    AppChange entry;
    entry.id = QStringLiteral("c%1").arg(m_nextChange++);
    entry.key = original.key;
    entry.label = original.label;
    entry.previous = before;
    entry.value = rowValue(after);
    entry.when = QDateTime::currentDateTime();
    entry.who = who;
    m_changes.append(entry);
    while (m_changes.size() > kMaxChanges) m_changes.removeFirst();

    // The row's marker goes back to what the undo made it, and the notification that offered the
    // Undo says it was taken rather than offering it again.
    SettingsPane::markAgentChanged(original.key, valueText(before), valueText(entry.value));
    if (!original.noteId.isEmpty())
        NotificationCenter::instance().amend(original.noteId,
            QStringLiteral("Undone: %1").arg(original.label),
            QStringLiteral("%1 → %2").arg(valueText(before), valueText(entry.value)), QString(), QString());

    if (previous) *previous = before;
    if (value) *value = entry.value;
    return true;
}

bool AppCommands::undoFromNotification(const QString &actionId) {
    const QString changeId = changeIdOfAction(actionId);
    if (changeId.isEmpty() || !findChange(changeId)) return false;
    return undo(changeId, QStringLiteral("you"));
}

}  // namespace relay
