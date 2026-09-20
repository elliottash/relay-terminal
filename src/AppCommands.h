// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The GUI half of "the agent drives the app" (card #FEJQ, protocol §30): the catalog the window
// sends its workers, the executor that carries out an `app_command`, and the change log that makes
// every write visible and undoable.
//
// It is deliberately not part of RelayWindow. Everything here is rules over two plain catalogs —
// `SettingsSection`/`SettingRow` and `ActionItem`, which the window already builds for the Options
// and Actions panes — so it compiles into a small library and is tested headless
// (`tests/appcommands_test.cpp`), where a window would need the whole app. RelayWindow supplies the
// catalogs and the four callbacks below; it holds one AppCommands and both command sources — a
// pane's worker and the tab's helper worker — go through it (§30.3: one executor, two sources).
//
// What the owner decided, 2026-09-20, and where it lives here:
//  1. **Settable** is every value row (Toggle/Choice/Number/Text) except a secret — `catalog()`.
//  2. **agent_safe** is opt-in per action, the line being "undoable in one click" —
//     `appcommands::actionIsAgentSafe()` and `SettingRow::agentSafeButtons`.
//  3. One toggle in Options › Agent gates the helper and the pane agent together —
//     `writesEnabled`, re-checked here however the catalog was marked (§30.3: the catalog the
//     worker holds is a snapshot, the row is the truth).
//  6. Every change is announced and undoable in one step — `AppChange`, `undo()`, the notification
//     and `SettingsPane::markAgentChanged` (§30.6).
#include "SettingsPane.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <functional>

namespace relay {

// One thing an agent did, as the GUI's change log holds it. The log is the source of truth for
// Undo: it works with no agent in the loop, after the worker has stopped and after the
// conversation is gone (§30.6), which is why it is the window's and not the worker's.
struct AppChange {
    QString id;              // "c1", unique within the window
    bool action = false;     // an action that was run, rather than an option that was set
    QString key;             // SettingRow::id, or ActionItem::key for an action
    QString label;           // the row's or action's label, as the person sees it
    QJsonValue previous, value;   // an option's value before and after; unset for an action
    QDateTime when;
    QString who;             // which agent asked: a pane's session token, or "helper"
    QString turnId;          // the turn it happened in, when the worker said
    QString noteId;          // the notification that announced it, so Undo can amend it
    bool undone = false;
};

namespace appcommands {

// The answer to one `app_command`, ready to go straight back down the pipe it arrived on (§30.3).
//
// There are two pipes and one executor. A pane's worker sends its commands down the pane's own
// connection (`src/Pane.h`) and the tab's helper worker down the helper's (`src/RelayWindow.h`),
// and both are answered here so that the two routes cannot drift: until 2026-09-20 only the pane
// route existed, and every write the helper attempted sat out §30.3's 20-second deadline and came
// back `no_reply` while looking, on the GUI side, like nothing at all.
//
// `execute` is the window's executor for this connection, or unset when there is no window left to
// act in — then the call ends in an error the transcript can show rather than in that deadline.
QJsonObject answerFor(const QJsonObject &command,
                      const std::function<QJsonObject(const QJsonObject &)> &execute);

// `open {target: "conversation", conversation, item, new_pane}` (§30.3, §30.4): the row the
// window is to resume, and where. The worker resolved the id against the conversation index it
// owns and sent the whole row — the GUI holds no such index, and a resume needs the session's
// directory and, for a claude or codex row, the argv that respawns it (26.7) — so all this does
// is check that what arrived can be resumed and say where it goes.
//
// It is here rather than in RelayWindow so that the rule is testable without a window
// (tests/appcommands_test.cpp): the window's own part is one call to `Pane::openSavedSession`,
// which is the Sessions row's Enter.
//
// `*newPane` defaults to true — a conversation opened into the pane the person is sitting in
// replaces what that pane is holding, so "open it" means "beside it" unless the agent says
// otherwise. *error is a §30.3 word.
bool conversationToOpen(const QJsonObject &command, QJsonObject *item, bool *newPane,
                        QString *error);

// Whether an agent may run this action without being asked (owner decision 2, §30.2). It starts
// with the reversible ones — opening or revealing anything, testing a key, refreshing or detecting
// local servers, copying a page, reordering models, undo — and everything else is off until the
// owner says otherwise: resetting to defaults, removing a key or a server, deleting a session,
// pairing and sharing, quit and restart.
//
// It is one table rather than a flag typed at each of two hundred `actionItem()` calls, because
// the answer is a property of the *policy*, not of the call site: a new action is unsafe by
// default and stays unsafe until it is named here, which is what "opt-in" has to mean if adding an
// action is not to widen what agents may do by accident.
bool actionIsAgentSafe(const QString &key);

// The action key a Button/Buttons row of the Options pane gets in the action catalog. Owner
// decision 1: "Button rows are not values; they are actions" — so the Test-key button, Local
// models' Refresh and Detect, "copy this page" and the model reorder arrows are reachable through
// `run_action` and nowhere else. `button` is the index within a Buttons row; a Button row is its
// own index 0.
QString rowActionKey(const QString &sectionId, const QString &rowId, int button = 0);
// The value of a row as the wire carries it: a bool, a string or a number. Undefined for a row
// that holds no value (button, buttons, info, heading) and for a secret, whose value never leaves
// this process (§30.8).
QJsonValue rowValue(const SettingRow &row);
// "on" / "off" / the text / the number: how a value reads in a notification and in a row marker.
QString valueText(const QJsonValue &value);

}  // namespace appcommands

class AppCommands {
public:
    // ----- what the window supplies -------------------------------------------------------------
    std::function<QList<SettingsSection>()> sections;
    std::function<QList<ActionItem>()> actions;
    // Options › Agent, "Agents may change options and run actions" (owner decision 3). Unset reads
    // as on, which is the shipped default.
    std::function<bool()> writesEnabled;
    // `open` (§30.3). The window opens the pane and reveals the row; it answers false with one of
    // the §30.3 error words in *error when it cannot. Not gated by `writes_enabled`: opening a
    // pane changes nothing.
    std::function<bool(const QJsonObject &command, QString *error)> openTarget;
    // ----- the catalog (§30.2) ------------------------------------------------------------------
    // The whole `app` block, for `configure` and for an `app_catalog` refresh. It is rebuilt from
    // the live catalogs every time: the block carries current values, so a setting the person
    // changed by hand has to reach the agent that is about to describe it.
    //
    // `tab` is the persistent id of the tab this block is going to (§30.2). It is an argument
    // rather than a member because one window's workers sit in different tabs and one AppCommands
    // serves them all: the executor is the window's, the tab is the connection's.
    QJsonObject catalog(const QString &tab) const;

    // ----- executing a command (§30.3) ----------------------------------------------------------
    // Takes an `app_command` and answers the `app_command_result` to send back down the same pipe.
    // `who` says which agent asked — a pane's session token, or "helper" — and is only ever used
    // in the change log and the notification. It never refuses by *who*: the policy is
    // `writes_enabled`, `settable` and `agent_safe`, set once in Options (§30.8).
    QJsonObject execute(const QJsonObject &command, const QString &who);

    // ----- the change log (§30.6) ---------------------------------------------------------------
    QList<AppChange> changes() const { return m_changes; }
    // Put one change back, by the id the result carried, logging the revert as an entry of its
    // own. This is what the notification's Undo button and the `undo` command both do; it is
    // allowed however `writes_enabled` stands, since it can only revert a change an agent already
    // made (§30.4).
    bool undo(const QString &changeId, const QString &who = QStringLiteral("you"),
              QString *error = nullptr, QJsonValue *previous = nullptr, QJsonValue *value = nullptr);
    // The notification action id for a change, and the change id back out of one. The popup only
    // carries strings, so Undo survives the window that posted it being busy elsewhere.
    static QString undoActionId(const QString &changeId);
    static QString changeIdOfAction(const QString &actionId);
    // An "Undo" press in the notification popup. Unknown ids are ignored, so a window that did not
    // make the change says nothing rather than guessing.
    bool undoFromNotification(const QString &actionId);

    static constexpr int kMaxChanges = 200;

private:
    const AppChange *findChange(const QString &id) const;
    // Finds a row by id across the live sections; `sectionId` is filled in when it is found.
    bool findRow(const QString &rowId, SettingRow *row, QString *sectionId) const;
    // Finds an action by key: the action catalog, its submenus, and the Button rows of the options
    // catalog (`appcommands::rowActionKey`).
    bool findAction(const QString &key, ActionItem *item, bool *agentSafe) const;
    // Validates against the row's kind and invokes its writer; *error is a §30.3 word.
    bool writeRow(const SettingRow &row, const QJsonValue &value, QString *error) const;
    QString note(const AppChange &change) const;

    QList<AppChange> m_changes;
    int m_nextChange = 1;
};

}  // namespace relay
