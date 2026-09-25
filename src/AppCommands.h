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
// catalogs and the callbacks below; it holds one AppCommands and both command sources — a
// pane's worker and the tab's helper worker — go through it (§30.3: one executor, two sources).
//
// What the owner decided, 2026-09-20, and where it lives here:
//  1. **Settable** is every value row (Toggle/Choice/Number/Text) except a secret — `catalog()`.
//  2. **agent_safe** was opt-in per action, the line being "undoable in one click". Since
//     2026-09-24 it is on for every action but the ones named as refused (card #FRVM) —
//     `appcommands::actionIsAgentSafe()` and `appcommands::rowButtonIsAgentSafe()`.
//  3. One toggle in Options › Agent gates the helper and the pane agent together —
//     `writesEnabled`, re-checked here however the catalog was marked (§30.3: the catalog the
//     worker holds is a snapshot, the row is the truth).
//  6. Every change is announced and undoable in one step — `AppChange`, `undo()`, the notification
//     and `SettingsPane::markAgentChanged` (§30.6).
#include "SettingsPane.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>
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

// Whether an agent may run this action (§30.2). Owner, 2026-09-24 on card #FRVM: "i think by
// default agents, should be able to control relay -- options, actions, etc". That reverses decision
// 2's opt-in table: every action is safe except the ones a person refused by name
// (`refusedByTheOwner()` in the .cpp — the microphone, the control handoff, the bulk shortcut wipe,
// the prompt-history wipe) and `pane.close`, which still closes the focused pane rather than one an
// agent can aim. A new action is therefore runnable by an agent the day it is added; the
// notification that says what it did (and, for a destructive one, what it cost) is the guard.
//
// What the old table did still matters, because it was an audit: `actionIsAudited()`. An audited
// action runs inline. One nobody audited is run on the next turn of the event loop, after the
// answer has gone back — its handler may open a modal dialog, and a nested event loop inside the
// executor would hold the answer past §30.3's deadline and freeze the window behind a dialog.
bool actionIsAgentSafe(const QString &key);
// The keys decision 2's table named (reads, the writing set, one picked model or effort): known to
// return without waiting on the person.
bool actionIsAudited(const QString &key);
// The same rule for a Button/Buttons row of Options: every button but the refused ones (pairing a
// phone admits a device, never an agent's to press). `SettingRow::agentSafeButtons` now marks
// buttons as audited — run inline — rather than as the only ones allowed.
bool rowButtonIsAgentSafe(const SettingRow &row, int button);
// Registered keys that are refused, sorted: listed in the catalog as `agent_safe: false`, so the
// worker, which sees the whole keybinding registry, can tell a refused key from a runnable one.
QStringList refusedActionKeys();

// The half of that table that only opens, reveals, focuses or restores a view — and so changes
// nothing at all. Owner, 2026-09-20 on card #AG7R: "7 pass the toggle like app_open". Options ›
// Agent's "Agents may change options and run actions" refused *every* `run_action`, so with the
// toggle off a helper could open a conversation into a new pane through `app_open` (never gated —
// §30.4, opening is not a write) and could not open an empty pane through `run_action`: the same
// act, two answers. These pass the toggle; the reversible-but-writing rest of the safe table
// (`keybindings.reload`, `theme.reload`, `agents.reload`, `pane.equalize`, `agent.screenshotPane`
// and every `row:` button) stays behind it, so the toggle still means "may change things".
//
// Every read action is agent-safe and audited.
bool actionIsRead(const QString &key);

// Whether an action acts on **a pane** rather than on the window or on the app as a whole: the
// pane's model, its reasoning effort, its input mode, its plan mode, its own views (ⓘ, Activity,
// requests, thinking, find-in-view, the sessions list bound to it, a screenshot of it).
//
// It is the set `run_action`'s `pane` means anything for (§30.3, #AG7R group 2). Everything else
// — the splits, the focus moves, the tab and window keys, Options, the Switchboard, a `row:`
// button — is aimed at the window or at the layout the person is looking at, and runs exactly as
// it did before an agent could name a pane at all.
bool actionIsPaneScoped(const QString &key);

// Every key the audited table names outright, sorted. `catalog()` uses it to list the safe keys the
// palette has no row for (#AG7R group 1); the prefix entries (`closed:<id>`) are not in it,
// because there is no fixed set of them to list. Registry keys outside it are safe too since
// #FRVM; the worker lists those from the keybinding registry it already holds.
QStringList agentSafeActionKeys();

// A value row that is named like a credential — `key`, `token`, `secret`, `password`,
// `credential` in its id or its label — but was never marked `SettingRow::secret`.
//
// Decision 1 is "every value row except a secret", and until 2026-09-20 no shipped row set the
// flag at all: the guard had never fired, and nothing but §30.8 stood between the next
// key-shaped row and an agent reading its value out of the catalog (#AG7R group 6). So the
// catalog fails safe — a row this answers true for is listed without its value and is not
// settable, exactly as a marked secret is — and says so on stderr, which is how the person who
// adds the row learns to mark it (or to name it below).
//
// Only `Text` rows are examined. A secret is always free text; a Toggle, Choice or Number cannot
// hold one, and the broad rule over every kind would have caught five innocent shipped rows on
// the word "key" alone (the keymap preset, the program-keys mode, the voice hold key, the
// first-token timeout, "Which Relay keys still act while vim runs").
bool rowNamedLikeASecret(const SettingRow &row);

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
    // The keybinding registry (`Keymap`, src/Keymap.h), for the keys it knows that the action
    // catalog has no row for. Twelve of the 33 keys the safe table named were registry-only —
    // `pane.focusLeft`, `window.next`, `palette.open`, `help.shortcuts`, `conversations.open`,
    // `agent.agentsMenu`, `agent.subagentPane`, `notifications.jump` — so an agent that read
    // §30.2, believed it might move the focus and tried it was told the action did not exist
    // (#AG7R group 1). The registry is what the keyboard itself dispatches through, so running a
    // key here is the same act as pressing it.
    //
    // `registryLabel` answers the action's description, or an empty string when the key is not a
    // registered action — that emptiness is what still makes a made-up key `unknown_action`. A
    // registered key that the table does *not* name is found and then refused `not_agent_safe`,
    // which is the right refusal: the policy says no, not "no such thing".
    //
    // Both unset is the tested, window-free case: then only the catalog's own rows resolve.
    std::function<QString(const QString &key)> registryLabel;
    std::function<void(const QString &key)> runRegistryAction;
    // ----- aiming a command at a pane (§30.3, #AG7R group 2) ------------------------------------
    // `RelayWindow::runAction()` opened with `Pane *pane = m_active` and `app_command` carried no
    // pane at all, so an action asked for by the agent in tab 2 landed on whichever pane the
    // *person* was focused on. Nothing pane-scoped could be made agent-safe until a command could
    // be aimed, which is why group 3's model, effort, input-mode and plan-mode keys waited for
    // this.
    //
    // A pane is named by its **session token** — the same string `who` carries for a pane agent,
    // so a pane agent's own pane is resolved without the model having to name itself, and the
    // change log and the aim agree about what a pane is called. `paneExists` answers whether that
    // token still names a live pane of this window (a pane that has been closed is
    // `unknown_pane`, never a silent landing somewhere else); `runActionAt` runs the key with that
    // pane as the action's target, answering false if it went in between; `panes` lists them for
    // `list_panes`, which is how an agent reads the id of a pane that is not its own.
    //
    // All three unset is the tested, window-free case: every command then lands where it always
    // did, on the focused pane.
    std::function<bool(const QString &paneId)> paneExists;
    std::function<bool(const QString &key, const QString &paneId)> runActionAt;
    std::function<QJsonArray()> panes;
    // ----- one pane talking to another (§30.3, #AG7R group 8) -----------------------------------
    // "8 allow sending messages and pre-filling messages across panes" (owner, 2026-09-20). No
    // agent could put a prompt anywhere but its own pane: the Sessions helper could open three
    // conversations and then say nothing to any of them.
    //
    // `deliverPrompt` takes the command with its `pane` already resolved, `send` saying which of
    // the two acts it is (true submits the prompt in that pane, false leaves it in the composer)
    // and `from_label` naming the pane it came out of, which the receiving pane writes into its
    // transcript. `*queued` says whether the prompt is waiting behind a turn already running
    // there; *error is a §30.3 word — `busy` for a composer the person has typed in, which a
    // pre-fill must never overwrite.
    //
    // `renameTarget` is `/rename` and `/rename-tab` for an agent: `what` is `pane` or `tab`,
    // `name` is the new one (empty puts it back to automatic) and `*previous` comes back so the
    // notification and the tool result can say what it was called before.
    //
    // Both unset is the tested, window-free case: the command then answers `failed` rather than
    // claiming to have done something.
    std::function<bool(const QJsonObject &command, bool *queued, QString *error)> deliverPrompt;
    std::function<bool(const QJsonObject &command, QString *previous, QString *error)> renameTarget;
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
    // `who` says which agent asked — a pane's session token, or "helper" — and names the asker in
    // the change log and the notification. Since #AG7R group 2 it does one thing more: a pane
    // agent's token *is* its pane's id, so a command with no `pane` of its own is aimed at the
    // pane that asked (§30.3). It still never refuses by *who*: the policy is `writes_enabled`,
    // `settable` and `agent_safe`, set once in Options (§30.8).
    QJsonObject execute(const QJsonObject &command, const QString &who);

    // ----- the loop guard (§30.3, #AG7R group 8) ------------------------------------------------
    // `send_prompt` is the first thing in Relay that lets one agent make another act, so it is
    // also the first that could run for ever with nobody asking: A prompts B, B prompts C, C
    // prompts A. Two counters stop that, and both are cleared by the person speaking in the pane
    // — which is the honest definition of "somebody is still asking for this".
    //
    //  * **The chain.** A prompt sent into a pane makes that pane's own sends one link deeper. A
    //    chain longer than `kMaxPromptChain` is refused `would_loop`, so a ring of any size dies
    //    after three hops. A pane may not send to itself at all: that is the one-hop ring, and
    //    an agent that wants to say something to itself can simply say it.
    //  * **The budget.** One pane's agent may send at most `kMaxPromptsPerPane` prompts before a
    //    person types a prompt into that pane again, so a fan-out of a hundred prompts into one
    //    pane's queue is refused too — the chain cap alone would allow it.
    //
    // `notePersonPrompt` is the reset, called from `Pane::submitAgent` when the prompt came from
    // the composer. It clears the helper's budget as well as the pane's: the helper has no pane
    // of its own to be typed into, and a person prompting anywhere in the window is the same
    // evidence that they are still there.
    void notePersonPrompt(const QString &paneId);
    static constexpr int kMaxPromptChain = 3;
    static constexpr int kMaxPromptsPerPane = 5;

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

    // The `app_catalog` echo brake (#J0VY): a `presets` event from any worker used to make the
    // window resend the whole catalog to every worker, whose echoes re-fired presets in turn —
    // ~50k `app_catalog_updated` events in three hours with ~37 panes, all handled on the GUI
    // thread. This compares a catalog with the last blob *that worker* received: unchanged
    // content sends nothing and leaves `lastSent` alone; changed content updates it and does.
    // A worker starts with a null `lastSent`, so its first catalog always goes through.
    static bool catalogChanged(QByteArray &lastSent, const QJsonObject &app);
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
    // What the person calls a pane: its `list_panes` title, or the token when there is no window
    // to ask. The helper names itself, since "helper" is not a pane id anyone would recognise.
    QString paneLabel(const QString &paneId) const;

    QList<AppChange> m_changes;
    int m_nextChange = 1;
    // The loop guard's two counters, keyed by pane session token (see notePersonPrompt above).
    QHash<QString, int> m_promptChain;    // pane -> how many links reached the prompt it is running
    QHash<QString, int> m_promptsSent;    // pane -> prompts its agent has sent since a person spoke
};

}  // namespace relay
