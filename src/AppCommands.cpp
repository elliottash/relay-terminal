// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AppCommands.h"
#include "Notifications.h"

#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace relay {
namespace appcommands {

namespace {

// `closed:<id>` is the Recently-closed submenu's child: one named pane, tab or window out of the
// last 25 (src/ClosedStack.h). It cannot be a set entry, because the id is minted when the thing
// is closed and there is no list of them to write down — so it is matched by prefix, and by a
// prefix that `closed.list` and `closed.restore` cannot collide with (they use a dot).
bool isOneNamedClosedPane(const QString &key) {
    return key.startsWith(QStringLiteral("closed:")) && key.size() > 7;
}

// `model:<id>` and `effort:<level>` are the Model and Reasoning-effort submenus' children, and
// they have the same shape of problem as `closed:<id>`: the id is a stored provider preset and the
// level is whatever *that* provider offers, so there is no fixed set of them to write down and
// they are matched by prefix. A near miss — `models:foo`, or `model:` with nothing after it —
// matches neither the set nor the palette, so it stays `unknown_action` rather than becoming a
// safe key by accident. The catalog is what says which ids exist: a bogus `model:whatever` is not
// among the submenu's children and is never found (#AG7R group 3).
bool isOnePickedModel(const QString &key) {
    return key.startsWith(QStringLiteral("model:")) && key.size() > 6;
}
bool isOnePickedEffort(const QString &key) {
    return key.startsWith(QStringLiteral("effort:")) && key.size() > 7;
}

// The half of the safe table that changes nothing: it opens, reveals, focuses or restores a view
// and stops there. This is the set `writes_enabled` does not gate (see actionIsRead()).
const QSet<QString> &readActions() {
    static const QSet<QString> keys = {
        // Open or reveal something. Nothing is changed and the pane closes in one click.
        QStringLiteral("app.settings"),        // Options
        QStringLiteral("palette.open"),        // Actions
        QStringLiteral("board.open"),          // the Switchboard
        QStringLiteral("conversations.open"),  // the session manager
        QStringLiteral("projects.open"), QStringLiteral("globals.open"),
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
        // #AG7R group 3, owner 2026-09-20 ("groups 1-3 all yes"): reversible, in the catalog, and
        // named here only because the first table was written from a short list. Test suites is
        // the same pane class as the Switchboard and the explorer, which were already safe
        // (#7BM4); About is a dialog with one OK; the two folder actions hand a path to the
        // desktop's file manager and change nothing on either side.
        QStringLiteral("tests.open"),
        QStringLiteral("app.about"),
        QStringLiteral("logs.open"),
        QStringLiteral("theme.folder"),
        // Open a pane. Owner, 2026-09-20 — "the sessions helper can't open panes because it says
        // it's unsafe. can you change that": a new pane holds nothing of the person's until they
        // put something in it, and the × in its header (pane.close, Ctrl+W) is the one click that
        // takes it back, so a split is on the reversible side of decision 2's line. The helper
        // reached for these after `open {target: "conversation"}` — which was never gated (§30.4)
        // — because "open a pane" on its own has no conversation to name.
        QStringLiteral("pane.splitRight"), QStringLiteral("pane.splitDown"),
        QStringLiteral("pane.splitLeft"), QStringLiteral("pane.splitUp"),
        QStringLiteral("closed.restore"),  // puts a closed pane back; the undo of a close
        // …and `menu:closed` is the list of the last 25 to put back one by one. The submenu itself
        // has no `run` and cannot be run (§30.2); it is named here so that the agent reading the
        // catalog is not told the group is off while every child of it is on.
        QStringLiteral("menu:closed"),
        // Move the focus. Moving it back is the undo.
        QStringLiteral("pane.focusUp"), QStringLiteral("pane.focusDown"),
        QStringLiteral("pane.focusLeft"), QStringLiteral("pane.focusRight"),
        QStringLiteral("tab.next"), QStringLiteral("tab.previous"),
        QStringLiteral("window.next"), QStringLiteral("window.previous"),
    };
    return keys;
}

// The half of the safe table that *changes* something, so `writes_enabled` still gates it. Keeping
// the two sets apart is what stops the toggle from meaning "agents may do nothing at all" and from
// meaning "agents may do anything reversible" (#AG7R group 7).
//
// It was called `reversibleWriteActions()` until 2026-09-20, and the rename is the honest record of
// a decision: the owner's answer to #AG7R group 5 ("dont let the agent do 1, 4, 6, 7. others are
// ok") put things in here that are not reversible at all — a compaction cannot be un-compacted, a
// cleared queue was the person's own typing. Decision 2's founding line, "undoable in one click",
// no longer describes the contents, so the name no longer claims it does; what carries the weight
// instead is the notification, which says what happened and what it cost (see `note()`).
const QSet<QString> &writingActions() {
    static const QSet<QString> keys = {
        // Re-read a file that is already on disk: a refresh, like Local models' Detect. It is a
        // write because a theme or a keybinding file edited since the last read takes effect.
        QStringLiteral("keybindings.reload"), QStringLiteral("theme.reload"),
        QStringLiteral("agents.reload"),
        // #AG7R group 3, the two that are not merely views. A screenshot writes no setting but it
        // attaches an image to the pane's next prompt, so it changes what the person is about to
        // send; equalize moves every splitter in the tab, which a drag takes back but which is a
        // change to the layout all the same.
        QStringLiteral("agent.screenshotPane"),
        QStringLiteral("pane.equalize"),
        // #AG7R group 3's pane-scoped half, which waited for group 2: a command can now be aimed
        // at a pane (`pane`, §30.3), so "put this pane on the local model" is answerable and no
        // longer lands on whichever pane the person happens to be sitting in. Each of these is a
        // picker the person moves back in one click — but each *writes*: the model and the effort
        // are saved for the pane, the input mode decides where the next thing they type goes, and
        // plan mode changes what the agent is allowed to do. So they are here and not in
        // readActions(): the writes toggle still means "may change things" (group 7).
        QStringLiteral("menu:model"), QStringLiteral("menu:effort"), QStringLiteral("menu:mode"),
        QStringLiteral("input.modeAuto"), QStringLiteral("input.modeTerminal"),
        QStringLiteral("input.modeAgent"), QStringLiteral("input.toggle"),
        QStringLiteral("agent.planToggle"),
        // ----- #AG7R group 5, owner 2026-09-20 -------------------------------------------------
        // "group 5: dont let the agent do 1, 4, 6, 7. others are ok" — answering a list of seven
        // this table had held back. Four stay off and are named in `refusedByTheOwner()` below.
        // Everything else the review found is here, and most of it is *not* undoable: the line
        // this table was built on has moved, deliberately, by the person whose app it is.
        //
        // The layout. Moving it back is the undo, and a new tab or window closes with Ctrl+W
        // exactly as a new pane does — which is the argument that put the splits here first.
        QStringLiteral("tab.new"), QStringLiteral("window.new"),
        QStringLiteral("tab.moveToNewWindow"), QStringLiteral("pane.moveToNewTab"),
        QStringLiteral("pane.moveLeft"), QStringLiteral("pane.moveRight"),
        QStringLiteral("pane.moveUp"), QStringLiteral("pane.moveDown"),
        // The pane's own turn and its shell. These end work in flight, and three of them destroy
        // something outright — a compaction cannot be undone, a cleared queue was the person's own
        // typing, a cleared terminal was what they were reading. They are aimed (paneScopedActions)
        // so that "stop that pane" means that pane and not whichever one has the focus, and each
        // says in its notification what it cost (`lossNote`).
        QStringLiteral("agent.stop"), QStringLiteral("agent.interrupt"),
        QStringLiteral("agent.continue"), QStringLiteral("agent.recap"),
        QStringLiteral("agent.newChat"), QStringLiteral("agent.compact"),
        QStringLiteral("agent.clearQueue"), QStringLiteral("agent.resumeQueue"),
        QStringLiteral("agent.stopAllSubagents"),
        QStringLiteral("terminal.interrupt"), QStringLiteral("terminal.clear"),
        QStringLiteral("terminal.native"), QStringLiteral("pane.restartShell"),
        QStringLiteral("control.prompt"),
        // Sharing. The owner overruled the argument that publishing a pane to other people is not
        // an agent's to start ("others are ok"): `pane.share` opens the share window with
        // `dialog->show()` and `pane.sharing` opens a pane, so neither blocks — what either leads
        // to still needs a person to admit a device (#W5N2: no auto-admit), which is the floor the
        // refusal was protecting and which stands without this table.
        QStringLiteral("pane.share"), QStringLiteral("pane.sharing"),
        // The rest of the window and the app.
        QStringLiteral("app.update"), QStringLiteral("project.detach"),
        QStringLiteral("hints.reset"), QStringLiteral("conversations.rebuild"),
        QStringLiteral("helper.ask"), QStringLiteral("ssh.splitSameHost"),
    };
    return keys;
}

// Named, rather than merely absent, so that the four the owner said no to on 2026-09-20 cannot be
// added back by someone reading the table as a list of oversights. #AG7R group 5: "dont let the
// agent do 1, 4, 6, 7."
//
//  1. `voice.toggle` switches a microphone on. The cost of a wrong "on" is recording a room that
//     did not consent; the benefit is saving one click.
//  4. The human/agent control handoff. An agent granting itself control is circular: the thing
//     being decided is whether the agent is in charge.
//  6. `keybindings.clearOverrides` wipes every custom shortcut at once and the overrides file is
//     the only copy. This is not "agents may not change hotkeys" — `set_keybinding` has let them
//     change one deliberately since #GMCF. A bulk wipe is a different act.
//  7. `history.clear` deletes the prompt history every pane recalls with Up. Irreversible, and it
//     is the person's record rather than the app's state.
const QSet<QString> &refusedByTheOwner() {
    static const QSet<QString> keys = {
        QStringLiteral("voice.toggle"),
        QStringLiteral("control.human"), QStringLiteral("control.program.agent"),
        QStringLiteral("control.program.human"), QStringLiteral("program.delegate"),
        QStringLiteral("keybindings.clearOverrides"),
        QStringLiteral("history.clear"),
    };
    return keys;
}

// Allowed by the owner on 2026-09-20 and still off, for a reason that is not his: the handler
// enters a **nested event loop** and does not come back until the person answers a dialog. That is
// #AG7R group 4's finding and it is mechanical — `AppCommands::execute()` calls `item.run()` inline
// and the window returns its answer synchronously, so `run()` blocking means the
// `app_command_result` is never sent, §30.3's 20-second deadline expires, the agent is told
// `no_reply`, and the window sits frozen behind a dialog nobody asked for. Marking these would
// hand the person a hang instead of the feature.
//
// They go on as soon as group 4's non-blocking pass lands (`open()` with a finished-callback rather
// than `exec()`), and not before. Tracked on #AG7R; this set is the list that pass has to clear.
const QSet<QString> &waitingOnTheModalPass() {
    static const QSet<QString> keys = {
        // startFreshWindowSet() asks "Start a fresh window set?" with QMessageBox::question and
        // waits for the answer (src/RelayWindow.h).
        QStringLiteral("windows.fresh"),
        // closePane() asks before closing a Preview pane with unsaved edits (#SEJ2) — only in that
        // one case, but the executor cannot tell in advance which pane is dirty, and "usually
        // returns" is not a property a deadline can be built on. It also still closes the *focused*
        // leaf rather than a named pane, which `pane.close` needs before it means anything an agent
        // can aim.
        QStringLiteral("pane.close"),
    };
    return keys;
}

// The actions that act on one pane. `run_action`'s `pane` selects which (§30.3); everything not
// named here is aimed at the window or at the layout the person is looking at, and runs exactly as
// it did before a command could name a pane (#AG7R group 2).
const QSet<QString> &paneScopedActions() {
    static const QSet<QString> keys = {
        // Group 3's half above, plus `model:<id>` and `effort:<level>` by prefix below.
        QStringLiteral("menu:model"), QStringLiteral("menu:effort"), QStringLiteral("menu:mode"),
        QStringLiteral("input.modeAuto"), QStringLiteral("input.modeTerminal"),
        QStringLiteral("input.modeAgent"), QStringLiteral("input.toggle"),
        QStringLiteral("agent.planToggle"),
        // Already safe, and always were about one pane: the pane's own views. Aiming them is the
        // difference between "open the ⓘ view of the pane I am in" and "…of the pane the person
        // is looking at", which until now were the same call.
        QStringLiteral("agent.info"), QStringLiteral("agent.internalsPane"),
        QStringLiteral("agent.requests"), QStringLiteral("agent.thinkingPanel"),
        QStringLiteral("conversations.open"), QStringLiteral("find.inView"),
        QStringLiteral("links.step"), QStringLiteral("agent.screenshotPane"),
        // #AG7R group 5: the turn and shell controls the owner allowed. These are the keys for
        // which aiming matters most — "stop that pane" landing on whichever pane had the focus is
        // how an agent stops the wrong turn — so every one of them is named here, not only the
        // reversible ones.
        QStringLiteral("agent.stop"), QStringLiteral("agent.interrupt"),
        QStringLiteral("agent.continue"), QStringLiteral("agent.recap"),
        QStringLiteral("agent.newChat"), QStringLiteral("agent.compact"),
        QStringLiteral("agent.clearQueue"), QStringLiteral("agent.resumeQueue"),
        QStringLiteral("agent.stopAllSubagents"),
        QStringLiteral("terminal.interrupt"), QStringLiteral("terminal.clear"),
        QStringLiteral("terminal.native"), QStringLiteral("pane.restartShell"),
        QStringLiteral("control.prompt"),
        QStringLiteral("pane.share"), QStringLiteral("pane.sharing"),
    };
    return keys;
}

}  // namespace

bool actionIsRead(const QString &key) {
    return readActions().contains(key) || isOneNamedClosedPane(key);
}

bool actionIsAgentSafe(const QString &key) {
    // Owner decision 2 (2026-09-20). Everything not named in the two sets above is off, including
    // every action added after they were written: opt-in has to mean that adding an action does not
    // widen what an agent may do.
    //
    // `palette.agent` used to be here and is gone: src/Keymap.h:176 migrates it to `palette.open`
    // and it exists nowhere else in the tree, so the table was naming a key that could never
    // arrive (#AG7R group 1).
    //
    // The two refusals are tested *first* and not merely left out of the sets. Left out, a key is
    // off because nobody put it in; named, it is off because somebody decided, and a later reader
    // adding it to the set above gets a contradiction they have to resolve rather than a silent
    // widening. That is the whole point of an opt-in table, and both lists cost one lookup.
    if (refusedByTheOwner().contains(key) || waitingOnTheModalPass().contains(key)) return false;
    return actionIsRead(key) || writingActions().contains(key)
        || isOnePickedModel(key) || isOnePickedEffort(key);
}

QString lossNote(const QString &key) {
    // What a destructive action cost, in the person's terms, for the notification that announces
    // it (§30.6). Decision 2 used to guarantee that every agent action was undoable in one click,
    // so "Agent ran X" was enough; #AG7R group 5 ended that guarantee, and this is what replaced
    // it. A key with no entry here is one whose label already says what happened.
    static const QHash<QString, QString> lost = {
        {QStringLiteral("agent.compact"),
         QStringLiteral("The detail the compaction dropped is gone; the conversation carries on from the summary.")},
        {QStringLiteral("agent.clearQueue"),
         QStringLiteral("The prompts waiting in that pane were removed. They were yours, and they are not recoverable.")},
        {QStringLiteral("terminal.clear"),
         QStringLiteral("That pane's scrollback was cleared. The session log still holds the text.")},
        {QStringLiteral("agent.newChat"),
         QStringLiteral("The conversation that pane was holding is saved and can be reopened from Sessions.")},
        {QStringLiteral("agent.stop"),
         QStringLiteral("The turn that pane was running was stopped part-way.")},
        {QStringLiteral("agent.stopAllSubagents"),
         QStringLiteral("Every subagent of that pane was stopped; work in flight is lost.")},
        {QStringLiteral("pane.restartShell"),
         QStringLiteral("That pane's shell was restarted, so anything it was running is gone.")},
        {QStringLiteral("terminal.interrupt"),
         QStringLiteral("An interrupt was sent to whatever that pane was running.")},
        {QStringLiteral("project.detach"),
         QStringLiteral("The tab is no longer attached to a project. Attach it again from the Actions palette.")},
        {QStringLiteral("app.update"),
         QStringLiteral("Relay is updating and will restart, which ends every shell and agent in every window.")},
        {QStringLiteral("pane.share"),
         QStringLiteral("The share window is open. Nobody joins until you admit them.")},
    };
    return lost.value(key);
}

bool actionIsPaneScoped(const QString &key) {
    return paneScopedActions().contains(key) || isOnePickedModel(key) || isOnePickedEffort(key);
}

QStringList agentSafeActionKeys() {
    QStringList keys;
    for (const QString &key : readActions()) keys << key;
    for (const QString &key : writingActions()) keys << key;
    // Sorted: a QSet iterates in whatever order it likes, and the catalog must not reshuffle
    // between two reads of the same app (§30.2 — nothing is cached across a refresh, so a
    // reordered array reads as a changed app).
    keys.sort();
    return keys;
}

bool rowNamedLikeASecret(const SettingRow &row) {
    if (row.kind != SettingRow::Text || row.secret) return false;
    // Named like a credential but provably not one. Each entry is an existing row whose *name*
    // trips the rule, not a judgement that its value is harmless to send: add to this list only
    // after reading the row, and mark the row `secret` instead whenever the answer is "it might
    // hold one".
    static const QSet<QString> notSecretDespiteTheName = {
        // Options › Security, "Files the agent never reads": a list of regular expressions that
        // says which files hold secrets. It is the guard's own configuration, not a secret.
        QStringLiteral("option:security/secret_patterns"),
    };
    if (notSecretDespiteTheName.contains(row.id)) return false;
    static const QRegularExpression named(
        QStringLiteral("key|token|secret|password|passphrase|credential"),
        QRegularExpression::CaseInsensitiveOption);
    return named.match(row.id).hasMatch() || named.match(row.label).hasMatch();
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

// The sentence beside `unknown_pane`. It names the id the agent asked for, because the agent read
// that id out of a `list_panes` answer and the next thing it should do is read a fresh one.
QString unknownPaneMessage(const QJsonObject &command) {
    const QString asked = target(command, "pane");
    return QStringLiteral("Relay has no pane %1 in this window any more. List the panes again.")
        .arg(asked);
}

}  // namespace

QJsonObject answerFor(const QJsonObject &command,
                      const std::function<QJsonObject(const QJsonObject &)> &execute) {
    QJsonObject result = execute ? execute(command) : QJsonObject{};
    if (result.isEmpty())
        result = QJsonObject{{QStringLiteral("type"), QStringLiteral("app_command_result")},
                             {QStringLiteral("id"), command.value(QStringLiteral("id")).toString()},
                             {QStringLiteral("ok"), false},
                             {QStringLiteral("error"), QStringLiteral("failed")},
                             {QStringLiteral("message"),
                              QStringLiteral("There is no window left to act in.")}};
    return result;
}

bool conversationToOpen(const QJsonObject &command, QJsonObject *item, bool *newPane,
                        QString *error) {
    const auto fail = [error](const QString &word) { if (error) *error = word; return false; };
    const QJsonObject row = command.value(QStringLiteral("item")).toObject();
    // The id the tool named, for the case where the row did not travel (an older worker).
    const QString asked = command.value(QStringLiteral("conversation")).toString();
    const QString sessionId = row.value(QStringLiteral("session_id")).toString().isEmpty()
                                  ? asked : row.value(QStringLiteral("session_id")).toString();
    if (sessionId.isEmpty()) return fail(QStringLiteral("unknown_conversation"));
    QJsonObject resolved = row;
    resolved.insert(QStringLiteral("session_id"), sessionId);
    const QJsonValue wanted = command.value(QStringLiteral("new_pane"));
    if (!wanted.isUndefined() && !wanted.isNull() && !wanted.isBool())
        return fail(QStringLiteral("invalid_value"));
    if (item) *item = resolved;
    if (newPane) *newPane = wanted.isBool() ? wanted.toBool() : true;
    return true;
}

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
            // A row nobody remembered to mark. This is the one place every row passes through on
            // its way to an agent, so it is where the guard belongs: it fails safe (the value is
            // withheld, exactly as a marked secret's is) and says so once, naming the row, for
            // whoever added it (#AG7R group 6).
            const bool unmarkedSecret = rowNamedLikeASecret(row);
            if (unmarkedSecret) {
                static QSet<QString> told;
                if (!told.contains(row.id)) {
                    told.insert(row.id);
                    qWarning().noquote()
                        << QStringLiteral("app catalog: \"%1\" (%2) is named like a credential but does "
                                          "not set SettingRow::secret, so its value is being withheld "
                                          "from agents. Set row.secret, or name it in "
                                          "appcommands::rowNamedLikeASecret() if it holds no secret "
                                          "(protocol §30.8).").arg(row.label, row.id);
                }
            }
            const bool secret = row.secret || unmarkedSecret;
            QJsonObject entry{{QStringLiteral("id"), row.id},
                              {QStringLiteral("section"), section.id},
                              {QStringLiteral("section_label"), section.title},
                              {QStringLiteral("label"), row.label},
                              {QStringLiteral("detail"), row.detail},
                              {QStringLiteral("kind"), kindName(row.kind)},
                              // Owner decision 1: every value row except a secret. A button is not
                              // a value; it is an action, and is listed as one below.
                              {QStringLiteral("settable"), value && !secret}};
            if (secret) entry.insert(QStringLiteral("secret"), true);
            // A secret row's value never leaves this process, in either direction (§30.8).
            if (value && !secret) entry.insert(QStringLiteral("value"), rowValue(row));
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

    QSet<QString> listed;
    const auto appendAction = [&actionRows, &listed](const ActionItem &item) {
        if (item.key.isEmpty()) return;
        listed.insert(item.key);
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

    // The safe keys the palette has no row for. Until 2026-09-20 twelve of them were missing from
    // this array while #GMCF listed them from the worker side under section `Shortcuts` with
    // `agent_safe: false` — so one answer contradicted the policy table on the same key, and
    // running one answered `unknown_action` (#AG7R group 1). The GUI catalog is where the policy
    // lives, so it carries them; the worker's shortcut rows skip any key this array already holds,
    // and `findAction()` runs them through the same registry the keyboard dispatches through.
    if (registryLabel) {
        for (const QString &key : agentSafeActionKeys()) {
            if (listed.contains(key)) continue;
            const QString label = registryLabel(key);
            if (label.isEmpty()) continue;   // not a registered action either: nothing to offer
            listed.insert(key);
            actionRows.append(QJsonObject{{QStringLiteral("key"), key},
                                          {QStringLiteral("section"), QStringLiteral("Shortcuts")},
                                          {QStringLiteral("label"), label},
                                          {QStringLiteral("detail"), QString()},
                                          {QStringLiteral("agent_safe"), true}});
        }
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
    const auto match = [&](const ActionItem &candidate) {
        if (candidate.key != key || !candidate.run) return false;
        if (item) *item = candidate;
        if (agentSafe) *agentSafe = candidate.agentSafe || appcommands::actionIsAgentSafe(candidate.key);
        return true;
    };
    if (actions) {
        for (const ActionItem &candidate : actions()) {
            if (match(candidate)) return true;
            if (candidate.children) for (const ActionItem &child : candidate.children()) if (match(child)) return true;
        }
    }
    // Nothing in the catalog. The keybinding registry knows a great many keys the palette has no
    // row for, and twelve of them were in the safe table — `pane.focusLeft`, `window.next`,
    // `palette.open`, `help.shortcuts` — so the policy named keys the executor could not find and
    // every one of them answered `unknown_action` (#AG7R group 1). Running one here is what
    // pressing its shortcut does, through the same `runAction()`.
    //
    // The lookup is deliberately not filtered by the safe table: a registered key the table does
    // not name is found and then refused `not_agent_safe` by execute(), which tells the agent the
    // policy said no rather than that Relay has no such action.
    if (registryLabel && runRegistryAction) {
        const QString label = registryLabel(key);
        if (!label.isEmpty()) {
            if (agentSafe) *agentSafe = appcommands::actionIsAgentSafe(key);
            if (item) {
                ActionItem wrapped;
                wrapped.key = key;
                // The same section #GMCF gives these on the worker side, so a key does not move
                // between sections depending on which answer the agent is reading.
                wrapped.section = QStringLiteral("Shortcuts");
                wrapped.label = label;
                wrapped.agentSafe = appcommands::actionIsAgentSafe(key);
                wrapped.run = [run = runRegistryAction, key] { run(key); };
                *item = wrapped;
            }
            return true;
        }
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

    // Which pane this command is aimed at, by the pane's session token (§30.3, #AG7R group 2).
    // Three rules, and the first is the one that must ask nothing of the model:
    //
    //  * **a pane agent with no `pane`** means its own pane. `who` *is* that pane's token, so the
    //    executor resolves it without the agent having to name itself — the common case, and no
    //    change to anything a model writes.
    //  * **the helper with no `pane`** keeps today's behaviour: `who` is the word "helper", which
    //    names no pane, so the command lands on the pane the person is focused on. Said out loud
    //    here and in §30.3 because it is the case that surprises people.
    //  * **an explicit `pane`** names any pane of this window, and a pane that has been closed
    //    since the agent read the list is `unknown_pane` — never a silent landing on somebody
    //    else's pane, which is the whole fault this is fixing.
    //
    // `*named` says whether the command asked for a pane by name; the answer is the token to aim
    // at, empty for "wherever the focus is".
    QString paneError;
    const auto aimedPane = [&](bool *named) -> QString {
        const QString asked = target(command, "pane");
        if (named) *named = !asked.isEmpty();
        if (!asked.isEmpty()) {
            if (!paneExists || !paneExists(asked)) {
                paneError = QStringLiteral("unknown_pane");
                return {};
            }
            return asked;
        }
        return paneExists && paneExists(who) ? who : QString();
    };

    // `open` changes nothing, so it is offered whatever the Options › Agent toggle says (§30.4).
    // It is aimed too: `open {target: "conversation", new_pane: false}` means "load it into *my*
    // pane" when a pane agent asks, and the resolved token travels on as `pane` so the window's
    // opener needs no rule of its own. The other targets are window-level panes and ignore it.
    if (what == QStringLiteral("open")) {
        bool named = false;
        const QString aim = aimedPane(&named);
        if (named && aim.isEmpty()) return refuse(paneError, unknownPaneMessage(command));
        QJsonObject aimed = command;
        if (!aim.isEmpty()) aimed.insert(QStringLiteral("pane"), aim);
        QString error = QStringLiteral("unknown_target");
        if (!openTarget || !openTarget(aimed, &error)) return refuse(error);
        result.insert(QStringLiteral("ok"), true);
        return result;
    }

    // `list_panes`: the panes of this window, so that an agent can aim at one that is not its own
    // (§30.3). It is a round trip rather than a field of the `app` catalog because panes open and
    // close between two catalogs, and a list that is one pane out of date is worse than none: it
    // would have an agent name a pane that has gone. Like `open`, it changes nothing and is
    // offered whatever the toggle says.
    if (what == QStringLiteral("list_panes")) {
        if (!panes)
            return refuse(QStringLiteral("failed"),
                          QStringLiteral("There is no window left to list panes in."));
        QJsonArray rows = panes();
        // Which of them is the asking agent's own, so it never has to guess: `who` is its token.
        for (int i = 0; i < rows.size(); ++i) {
            QJsonObject row = rows.at(i).toObject();
            if (!who.isEmpty() && row.value(QStringLiteral("id")).toString() == who) {
                row.insert(QStringLiteral("you"), true);
                rows.replace(i, row);
            }
        }
        result.insert(QStringLiteral("ok"), true);
        result.insert(QStringLiteral("panes"), rows);
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
        // checked again here (§30.3). `rowNamedLikeASecret` is the unmarked case the catalog
        // already withheld the value of: the two halves have to agree, or a row would be listed
        // as not settable and then be written anyway (#AG7R group 6).
        if (row.secret || rowNamedLikeASecret(row)) return refuse(QStringLiteral("secret"));
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
        const QString key = target(command, "key", "action");
        // Owner, 2026-09-20 on #AG7R: "7 pass the toggle like app_open". Until then the toggle
        // refused every action, so with it off a helper could open a conversation into a new pane
        // through `app_open` and could not open an empty pane through `run_action` — the same act,
        // two answers. An action that only opens, reveals, focuses or restores a view changes
        // nothing, so it passes; everything else, including a key nothing answers to, still meets
        // the toggle first and answers `writes_disabled` exactly as before.
        if (!writes && !actionIsRead(key)) return refuse(QStringLiteral("writes_disabled"));
        bool named = false;
        const QString aim = aimedPane(&named);
        if (named && aim.isEmpty()) return refuse(paneError, unknownPaneMessage(command));
        ActionItem item;
        bool agentSafe = false;
        if (!findAction(key, &item, &agentSafe)) return refuse(QStringLiteral("unknown_action"));
        if (!agentSafe) return refuse(QStringLiteral("not_agent_safe"));
        if (!item.run) return refuse(QStringLiteral("failed"), QStringLiteral("%1 cannot be run from here.").arg(item.label));
        // A pane-scoped action goes to the pane it was aimed at; everything else runs the
        // catalog's own closure, which is where the palette's rows, the `row:` buttons and the
        // registry keys live (#AG7R group 2). With no aim — the helper, or a window with no
        // lookup at all — every action runs exactly as it did before there was a `pane` field.
        const bool aimed = actionIsPaneScoped(key) && !aim.isEmpty() && runActionAt;
        if (aimed) {
            // It could have been closed between the check above and this line.
            if (!runActionAt(key, aim))
                return refuse(QStringLiteral("unknown_pane"), unknownPaneMessage(command));
        } else {
            item.run();
        }

        AppChange change;
        change.id = QStringLiteral("c%1").arg(m_nextChange++);
        change.action = true;
        change.key = key;
        change.label = item.label;
        change.when = QDateTime::currentDateTime();
        change.who = who;
        change.turnId = command.value(QStringLiteral("turn_id")).toString();
        // No Undo on an action: what it did is its own to take back, and offering a button that
        // cannot keep its promise is worse than offering none (§30.6). What the note *must* do,
        // since #AG7R group 5 put actions here that destroy things, is say what it cost — so a
        // destructive key's own sentence replaces the action's blurb, which describes the button
        // rather than what just happened to you (`lossNote`).
        const QString cost = appcommands::lossNote(key);
        change.noteId = NotificationCenter::instance().post(
            QStringLiteral("Agent ran %1").arg(change.label), cost.isEmpty() ? item.detail : cost);
        m_changes.append(change);
        while (m_changes.size() > kMaxChanges) m_changes.removeFirst();

        result.insert(QStringLiteral("ok"), true);
        // Which pane it landed on, when that was a choice: a tool result says what happened, and
        // "the pane you asked for" is part of what happened here (§30.3).
        if (aimed) result.insert(QStringLiteral("pane"), aim);
        return result;
    }

    // `send_prompt` / `prefill_prompt`: one pane's agent putting a prompt into another pane
    // (§30.3, #AG7R group 8; owner, 2026-09-20: "allow sending messages and pre-filling messages
    // across panes"). A send submits it there as if the person had pressed Enter; a pre-fill
    // leaves it in that pane's composer for them to read and send. Both are writes — the first
    // makes another agent act — so both meet the toggle, and neither is in `readActions()`.
    if (what == QStringLiteral("send_prompt") || what == QStringLiteral("prefill_prompt")) {
        const bool send = what == QStringLiteral("send_prompt");
        if (!writes) return refuse(QStringLiteral("writes_disabled"));
        bool named = false;
        const QString aim = aimedPane(&named);
        // Unlike `run_action`, there is no sensible default: "put this prompt somewhere" is not a
        // request. The pane is named or the command is refused, and `list_panes` is where the ids
        // are.
        if (!named)
            return refuse(QStringLiteral("invalid_value"),
                          QStringLiteral("Name the pane to reach with `pane`; list_panes gives the ids."));
        if (aim.isEmpty()) return refuse(paneError, unknownPaneMessage(command));
        const QString text = command.value(QStringLiteral("text")).toString();
        if (text.trimmed().isEmpty())
            return refuse(QStringLiteral("invalid_value"), QStringLiteral("There is no prompt to send."));
        // The one-hop ring, and the first thing the loop guard has to refuse: an agent talking to
        // its own pane is talking to itself, which it can do by writing the words.
        if (aim == who)
            return refuse(QStringLiteral("would_loop"),
                          QStringLiteral("That is your own pane. Say it in your reply instead."));
        // The chain, for a send only: a pre-fill makes nobody act, and the person's Enter that
        // sends it is itself a person's prompt, which clears the count.
        int depth = 0;
        if (send) {
            depth = m_promptChain.value(who) + 1;
            if (depth > kMaxPromptChain)
                return refuse(QStringLiteral("would_loop"),
                              QStringLiteral("This prompt is %1 agents deep in a chain of agents "
                                             "prompting each other; Relay stops at %2. Answer in "
                                             "words instead.").arg(depth).arg(kMaxPromptChain));
            if (m_promptsSent.value(who) >= kMaxPromptsPerPane)
                return refuse(QStringLiteral("would_loop"),
                              QStringLiteral("This pane's agent has already sent %1 prompts to "
                                             "other panes since anyone typed one here. Tell the "
                                             "user what still needs doing.").arg(kMaxPromptsPerPane));
        }
        QJsonObject aimed = command;
        aimed.insert(QStringLiteral("pane"), aim);
        aimed.insert(QStringLiteral("send"), send);
        aimed.insert(QStringLiteral("from"), who);
        aimed.insert(QStringLiteral("from_label"), paneLabel(who));
        bool queued = false;
        QString error = QStringLiteral("failed");
        if (!deliverPrompt || !deliverPrompt(aimed, &queued, &error))
            return refuse(error, error == QStringLiteral("busy")
                                     ? QStringLiteral("That pane's composer already has something "
                                                      "in it; the person is typing. Send it, or try "
                                                      "again later.")
                                     : error == QStringLiteral("failed") && !deliverPrompt
                                           ? QStringLiteral("There is no window left to act in.")
                                           : QString());
        if (send) {
            // Counted only once it landed: a refused prompt has driven nobody.
            m_promptChain.insert(aim, depth);
            m_promptsSent.insert(who, m_promptsSent.value(who) + 1);
        }
        // The person sees it. A pre-fill sitting in a composer announces itself, but a *send*
        // does not: the other pane simply starts working, and without this the only trace that
        // it was an agent's doing would be the note in that pane's transcript (§30.6).
        const QString label = paneLabel(aim);
        AppChange change;
        change.id = QStringLiteral("c%1").arg(m_nextChange++);
        change.action = true;
        change.key = what;
        change.label = send ? QStringLiteral("Sent a prompt to %1").arg(label)
                            : QStringLiteral("Filled the composer in %1").arg(label);
        change.when = QDateTime::currentDateTime();
        change.who = who;
        change.turnId = command.value(QStringLiteral("turn_id")).toString();
        // No Undo: the prompt is already on its way to another agent, and a button that cannot
        // keep its promise is worse than no button (§30.6). A pre-fill is undone by clearing the
        // box, which is the box the person is looking at.
        change.noteId = NotificationCenter::instance().post(
            send ? QStringLiteral("Agent sent a prompt to %1").arg(label)
                 : QStringLiteral("Agent filled the composer in %1").arg(label),
            QStringLiteral("From %1 · %2").arg(paneLabel(who), text.simplified().left(120)));
        m_changes.append(change);
        while (m_changes.size() > kMaxChanges) m_changes.removeFirst();

        result.insert(QStringLiteral("ok"), true);
        result.insert(QStringLiteral("pane"), aim);
        result.insert(QStringLiteral("pane_title"), label);
        if (send) result.insert(QStringLiteral("queued"), queued);
        return result;
    }

    // `rename`: `/rename` and `/rename-tab` for an agent (§30.3, #AG7R group 8). Those are slash
    // commands typed into a composer and no agent can type into one, so "call this pane «deploy»"
    // could not be asked of one at all. It is a command and not a palette action because it takes
    // an argument and an `ActionItem` takes none — the safe table could only have offered "open
    // the rename editor", which parks a field in front of the person (group 4's own problem).
    // Renaming back is the undo, which is what puts it on the allowed side of decision 2.
    if (what == QStringLiteral("rename")) {
        if (!writes) return refuse(QStringLiteral("writes_disabled"));
        const QString thing = target(command, "what");
        if (thing != QStringLiteral("pane") && thing != QStringLiteral("tab"))
            return refuse(QStringLiteral("invalid_value"),
                          QStringLiteral("what must be \"pane\" or \"tab\"."));
        bool named = false;
        const QString aim = aimedPane(&named);
        if (named && aim.isEmpty()) return refuse(paneError, unknownPaneMessage(command));
        // A pane agent renames its own pane (or its own tab) with nothing named, exactly as
        // `run_action` aims at it; a helper has no pane of its own, so it has to say which.
        if (aim.isEmpty())
            return refuse(QStringLiteral("unknown_pane"),
                          QStringLiteral("Name the pane with `pane`; list_panes gives the ids."));
        QJsonObject aimed = command;
        aimed.insert(QStringLiteral("pane"), aim);
        QString previous;
        QString error = QStringLiteral("failed");
        if (!renameTarget || !renameTarget(aimed, &previous, &error))
            return refuse(error, error == QStringLiteral("failed") && !renameTarget
                                     ? QStringLiteral("There is no window left to act in.") : QString());
        const QString name = command.value(QStringLiteral("name")).toString();
        AppChange change;
        change.id = QStringLiteral("c%1").arg(m_nextChange++);
        change.action = true;
        change.key = QStringLiteral("rename.") + thing;
        change.label = name.isEmpty()
            ? QStringLiteral("Put the %1 name back to automatic").arg(thing)
            : QStringLiteral("Renamed the %1 to \u201c%2\u201d").arg(thing, name);
        change.when = QDateTime::currentDateTime();
        change.who = who;
        change.turnId = command.value(QStringLiteral("turn_id")).toString();
        change.noteId = NotificationCenter::instance().post(
            QStringLiteral("Agent renamed a %1").arg(thing),
            previous.isEmpty() ? name : QStringLiteral("%1 \u2192 %2").arg(previous, name.isEmpty()
                                            ? QStringLiteral("automatic") : name));
        m_changes.append(change);
        while (m_changes.size() > kMaxChanges) m_changes.removeFirst();

        result.insert(QStringLiteral("ok"), true);
        result.insert(QStringLiteral("previous"), previous);
        result.insert(QStringLiteral("pane"), aim);
        return result;
    }

    return refuse(QStringLiteral("unknown_target"), QStringLiteral("No such command: %1.").arg(what));
}

// The loop guard's reset (§30.3, #AG7R group 8). A person typing a prompt into a pane is the
// evidence the counters exist to look for: the chain of agents that reached this pane ends with a
// human in it, and its agent may send again. "helper" is cleared too — the tab's helper has no
// pane of its own to be typed into, and a person prompting anywhere in this window is the same
// evidence about it.
void AppCommands::notePersonPrompt(const QString &paneId) {
    if (paneId.isEmpty()) return;
    m_promptChain.remove(paneId);
    m_promptsSent.remove(paneId);
    m_promptsSent.remove(QStringLiteral("helper"));
    m_promptChain.remove(QStringLiteral("helper"));
}

// What the person calls a pane, for the notification and for the tool result. `list_panes` is
// already the one place a pane's name lives, so this reads it there rather than growing a second
// callback that could disagree with it.
QString AppCommands::paneLabel(const QString &paneId) const {
    if (paneId.isEmpty()) return QStringLiteral("a pane");
    if (paneId == QStringLiteral("helper")) return QStringLiteral("the tab helper");
    if (panes) {
        for (const QJsonValue &value : panes()) {
            const QJsonObject row = value.toObject();
            if (row.value(QStringLiteral("id")).toString() != paneId) continue;
            const QString title = row.value(QStringLiteral("title")).toString();
            return title.isEmpty() ? paneId : QStringLiteral("\u201c%1\u201d").arg(title);
        }
    }
    return paneId;
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

// Who the change log names when the *person* is the one who acted: the default `who` of undo()
// and what the notification's Undo button passes. An agent's own `app_undo` carries its pane
// token or "helper" instead, which is what tells the two apart.
static const QString kYou = QStringLiteral("you");

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

    // The row's marker. An agent undoing its own change is still the agent changing the row, so
    // the mark stays and says what the undo made it. A **person** pressing Undo has answered the
    // mark — that is what "until the person touches it" means (owner decision 6) — so it goes.
    // Leaving it there had the pane say "changed by the agent just now: on → off" about a revert
    // the person had just performed by hand, which is the one thing the marker must never do.
    if (who == kYou) {
        SettingsPane::clearAgentChanged(original.key);
    } else {
        SettingsPane::markAgentChanged(original.key, valueText(before), valueText(entry.value));
        // …and an agent putting a setting back is an agent changing a setting, so it is announced
        // like one and offers the way back from *it* (owner decision 6 on #FEJQ: "it should be
        // clear what's changed / done and reversion / undo should be easy"). Amending the first
        // entry alone left an `app_undo` the agent chose to make with no offer anywhere — the
        // person who wanted the value the agent had just taken away had nothing to press.
        entry.noteId = NotificationCenter::instance().postWithAction(
            QStringLiteral("Agent changed %1").arg(entry.label),
            QStringLiteral("%1 → %2").arg(valueText(before), valueText(entry.value)),
            NotificationCenter::kindInfo, QString(), QStringLiteral("Undo"), undoActionId(entry.id));
    }
    m_changes.append(entry);
    while (m_changes.size() > kMaxChanges) m_changes.removeFirst();

    // The notification that offered the Undo says it was taken rather than offering it again.
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
