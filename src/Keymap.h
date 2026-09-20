// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Every window-level shortcut as a named action: the defaults, the user's overrides in
// keybindings.json, the presets, and the reload that makes an edit take effect without a restart.
// Nothing outside main.cpp uses it, which is why it is a header beside main.cpp rather than one of
// the tested libraries in src/. Depends on Qt only.

#include "Voice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QFileSystemWatcher>
#include <QKeySequence>
#include <QTimer>

#include <functional>

// ----- keyboard shortcuts ----------------------------------------------------------------------
//
// Every window-level shortcut is a named action. Defaults live here; overrides live in
// ~/.config/RelayTerminal/relay/keybindings.json as {"version":1,"bindings":{"pane.close":["Ctrl+W"]},
// "program_keys":"shift-only"}. The file reloads automatically, and the agent's set_keybinding
// tool writes the same file.
//
// program_keys decides what happens while a program such as vim runs in the focused terminal:
//   "shift-only" (default): only Ctrl+Shift shortcuts and F-keys act; everything else reaches the program
//   "all": every shortcut acts, even inside programs
//   "none": no shortcut acts while a program has focus
struct ActionDef { QString id, description, category; QStringList defaults; };

class Keymap {
public:
    static Keymap &instance() { static Keymap map; return map; }

    const QList<ActionDef> &actions() const { return m_actions; }
    QString path() const { return m_path; }
    QString programKeys() const { return m_programKeys; }
    QString preset() const { return m_preset; }
    // Built-in presets. "relay" is the Chrome-style default; the others follow their programs'
    // Linux defaults where Relay has an equivalent action (see docs/KEYBINDING-PRESETS.md).
    static QList<QPair<QString, QString>> presets() {
        return {{QStringLiteral("relay"), QStringLiteral("Relay (Chrome-style)")}, {QStringLiteral("warp"), QStringLiteral("Warp")},
                {QStringLiteral("vscode"), QStringLiteral("VS Code")}, {QStringLiteral("konsole"), QStringLiteral("Konsole")}};
    }
    void setPreset(const QString &id) { writeSetting(QStringLiteral("preset"), id); }
    bool hasOverrides() const { return !m_overrides.isEmpty(); }
    void clearOverrides() {
        QJsonObject root = readObject();
        root.insert(QStringLiteral("bindings"), QJsonObject());
        writeObject(root);
    }
    QStringList keysFor(const QString &id) const { return m_bindings.value(id); }
    QString description(const QString &id) const {
        for (const auto &action : m_actions) if (action.id == id) return action.description;
        return id;
    }
    QString shortcutText(const QString &id) const {
        const auto keys = keysFor(id);
        return keys.isEmpty() ? QString() : QKeySequence::fromString(keys.first(), QKeySequence::PortableText).toString(QKeySequence::NativeText);
    }
    // Every key bound to an action, as the desktop writes them. An action whose one gesture needs
    // several spellings (help.shortcuts: Ctrl+? is Ctrl+Shift+/ on most keyboards) can then tell
    // the user which keys actually work instead of only the first one.
    QStringList shortcutTexts(const QString &id) const {
        QStringList texts;
        for (const auto &key : keysFor(id)) {
            const QString text = QKeySequence::fromString(key, QKeySequence::PortableText).toString(QKeySequence::NativeText);
            if (!text.isEmpty() && !texts.contains(text)) texts << text;
        }
        return texts;
    }
    QStringList conflicts() const { return m_conflicts; }

    // Returns the action bound to a key event, or an empty string.
    QString match(const QKeyEvent *event) const {
        int key = event->key();
        auto mods = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        if (key == Qt::Key_Backtab) { key = Qt::Key_Tab; mods |= Qt::ShiftModifier; }
        if (key == 0 || key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) return {};
        QString id = m_lookup.value(combo(key, mods));
        // Shifted symbols differ by keyboard layout: "(" needs Shift on US keyboards but not on
        // AZERTY. For symbol keys, accept the binding written with or without Shift.
        const bool symbol = (key >= 0x21 && key <= 0x2f) || (key >= 0x3a && key <= 0x40) || (key >= 0x5b && key <= 0x60) || (key >= 0x7b && key <= 0x7e);
        if (id.isEmpty() && symbol) id = m_lookup.value(combo(key, mods ^ Qt::ShiftModifier));
        return id;
    }

    // True when a shortcut should act even though a program has keyboard focus.
    bool actsInsidePrograms(const QKeyEvent *event) const {
        if (m_programKeys == QStringLiteral("all")) return true;
        if (m_programKeys == QStringLiteral("none")) return false;
        const auto mods = event->modifiers();
        const bool fkey = event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F35;
        // Alt+arrows move between panes. A terminal cannot send Ctrl+Shift+letter to a program, and
        // few TUIs use Alt+arrows, so these stay Relay's while a program owns the keyboard —
        // otherwise a full-screen program (Claude Code, vim) traps the keyboard in its pane
        // (owner report 2026-09-17).
        const bool arrow = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right
                           || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down;
        const bool altArrow = arrow && (mods & Qt::AltModifier) && !(mods & Qt::ControlModifier);
        return fkey || altArrow || ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier));
    }

    void setProgramKeys(const QString &mode) { writeSetting(QStringLiteral("program_keys"), mode); }

    // Preset table for one preset; actions missing from a table use the Relay default.
    static QHash<QString, QStringList> presetTable(const QString &preset) {
        QHash<QString, QStringList> table;
        const QJsonObject all = QJsonDocument::fromJson(presetJson()).object();
        const QJsonObject chosen = all.value(preset).toObject();
        for (auto it = chosen.begin(); it != chosen.end(); ++it) {
            QStringList keys;
            for (const auto &value : it.value().toArray()) keys << value.toString();
            table.insert(it.key(), keys);
        }
        return table;
    }

    // Writes a complete, editable file when none exists yet.
    void ensureFile() {
        if (QFileInfo::exists(m_path)) return;
        // Overrides start empty so switching presets keeps working; "reference" lists every
        // action with its current keys and is ignored when loading.
        QJsonObject reference;
        for (const auto &action : m_actions)
            reference.insert(action.id, QStringLiteral("%1 [%2]").arg(action.description, m_bindings.value(action.id).join(QStringLiteral(", "))));
        writeObject({{"version", 1}, {"preset", m_preset}, {"program_keys", m_programKeys}, {"bindings", QJsonObject()}, {"reference", reference}});
    }

    QJsonObject catalog() const {
        QJsonArray actions;
        for (const auto &action : m_actions)
            actions.append(QJsonObject{{"id", action.id}, {"description", action.description},
                                       {"keys", QJsonArray::fromStringList(m_bindings.value(action.id))}});
        return {{"path", m_path}, {"actions", actions}};
    }

    void listen(QObject *context, std::function<void()> callback) { m_listeners.append({context, std::move(callback)}); }

    void reload() {
        m_bindings.clear(); m_overrides.clear();
        m_programKeys = QStringLiteral("shift-only");
        m_preset = QStringLiteral("relay");
        QJsonObject root;
        QString problem;
        QFile file(m_path);
        if (file.exists()) {
            if (file.open(QIODevice::ReadOnly) && file.size() < 1024 * 1024) {
                QJsonParseError error;
                const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
                if (error.error != QJsonParseError::NoError || !doc.isObject()) problem = QStringLiteral("keybindings.json is not valid JSON; using defaults.");
                else {
                    root = doc.object();
                    const QString mode = root.value(QStringLiteral("program_keys")).toString();
                    if (mode == QStringLiteral("all") || mode == QStringLiteral("none") || mode == QStringLiteral("shift-only")) m_programKeys = mode;
                    const QString preset = root.value(QStringLiteral("preset")).toString();
                    for (const auto &known : presets()) if (known.first == preset) m_preset = preset;
                }
            } else problem = QStringLiteral("keybindings.json could not be read; using defaults.");
        }
        const auto table = presetTable(m_preset);
        for (const auto &action : m_actions) m_bindings.insert(action.id, table.contains(action.id) ? table.value(action.id) : action.defaults);
        const auto bindings = root.value(QStringLiteral("bindings")).toObject();
        for (auto it = bindings.begin(); it != bindings.end(); ++it) {
            // Earlier builds had separate agent and terminal palettes.
            const QString id = it.key() == QStringLiteral("palette.agent") ? QStringLiteral("palette.open") : it.key();
            if (!m_bindings.contains(id)) continue;
            QStringList keys;
            for (const auto &value : it.value().toArray())
                if (!value.toString().trimmed().isEmpty()) keys << value.toString().trimmed();
            m_bindings.insert(id, keys);
            m_overrides.insert(id);
        }
        m_lookup.clear(); m_conflicts.clear();
        for (const auto &action : m_actions) {
            for (const auto &text : m_bindings.value(action.id)) {
                const int code = parse(text);
                if (!code) { m_conflicts << QStringLiteral("%1: unrecognized key \"%2\"").arg(action.id, text); continue; }
                if (m_lookup.contains(code)) m_conflicts << QStringLiteral("%1 and %2 both use %3").arg(m_lookup.value(code), action.id, text);
                else m_lookup.insert(code, action.id);
            }
        }
        if (!problem.isEmpty()) m_conflicts.prepend(problem);
        for (auto &listener : m_listeners) if (listener.first) listener.second();
    }

private:
    Keymap() {
        auto add = [this](const char *id, const char *category, const char *description, QStringList keys) {
            m_actions.append({QString::fromLatin1(id), QString::fromUtf8(description), QString::fromLatin1(category), std::move(keys)});
        };
        add("window.new", "window", "New window", {QStringLiteral("Ctrl+N"), QStringLiteral("Ctrl+Shift+N")});
        add("window.next", "window", "Next Relay window", {QStringLiteral("Alt+Tab")});
        add("window.previous", "window", "Previous Relay window", {QStringLiteral("Alt+Shift+Tab")});
        add("tab.new", "tab", "New tab", {QStringLiteral("Ctrl+T"), QStringLiteral("Ctrl+Shift+T")});
        add("tab.next", "tab", "Next tab", {QStringLiteral("Ctrl+Tab")});
        add("tab.previous", "tab", "Previous tab", {QStringLiteral("Ctrl+Shift+Tab")});
        // SSH (#S5SH). No default keys: of the Ctrl+Shift letters still free, U belongs to the
        // input method's Unicode entry, Q quits other terminals and M says nothing about hosts.
        // Bind one in keybindings.json and the shortcut hints start teaching it.
        add("ssh.connect", "tab", "Connect to host…: a new tab running ssh to a host from ~/.ssh/config or a recent one", {});
        // Ctrl+E, not Ctrl+P: one-handed (owner, 2026-09-17). Ctrl+D is left alone because it is
        // end-of-input for a running program. Ctrl+Shift+E is the twin that programs cannot swallow.
        // One key is now the whole of "new pane": it makes one on the right, and Left, Up or Down
        // within two seconds re-docks it there instead (issue #78BN). The other three directions
        // keep actions of their own so they can be bound or run from the palette, but the separate
        // "new pane below" key (Ctrl+Alt+E) is dropped.
        add("pane.splitRight", "pane", "New pane to the right (then ← ↑ ↓ places it)",
            {QStringLiteral("Ctrl+E"), QStringLiteral("Ctrl+Shift+E")});
        add("pane.splitDown", "pane", "New pane below", {});
        add("pane.splitLeft", "pane", "New pane to the left", {});
        add("pane.splitUp", "pane", "New pane above", {});
        add("ssh.splitSameHost", "pane", "Split on the same host: a new pane running this pane's ssh or mosh command again", {});
        add("pane.focusLeft", "pane", "Focus pane to the left", {QStringLiteral("Alt+Left")});
        add("pane.focusRight", "pane", "Focus pane to the right", {QStringLiteral("Alt+Right")});
        add("pane.focusUp", "pane", "Focus pane above", {QStringLiteral("Alt+Up")});
        add("pane.focusDown", "pane", "Focus pane below", {QStringLiteral("Alt+Down")});
        add("pane.close", "pane", "Close pane, then tab, then window", {QStringLiteral("Ctrl+W"), QStringLiteral("Ctrl+Shift+W")});
        add("pane.moveLeft", "pane", "Move pane left (swap with or dock beside the neighbor; past the page edge, into a column of its own; then the Move-down key docks it beneath)",
            {QStringLiteral("Ctrl+Alt+Left")});
        add("pane.moveRight", "pane", "Move pane right (past the page edge, into a column of its own; then the Move-down key docks it beneath)", {QStringLiteral("Ctrl+Alt+Right")});
        add("pane.moveUp", "pane", "Move pane up (past the page edge, into a row of its own)", {QStringLiteral("Ctrl+Alt+Up")});
        add("pane.moveDown", "pane", "Move pane down (straight after a left/right move, beneath that neighbor; past the page edge, into a row of its own)", {QStringLiteral("Ctrl+Alt+Down")});
        add("pane.moveToNewTab", "pane", "Move pane to a new tab (keeps the shell and agent)", {});
        // Every splitter in the tab back to equal shares, top to bottom (owner report, 2026-09-19:
        // pane sizes jiggle) -- a way back to a tidy layout after drags leave the panes uneven,
        // digit 0 for "reset the sizes"; no other action binds a digit so this is free in all four
        // presets, and Ctrl+Alt matches the rest of the pane-arranging family (moveLeft and friends).
        add("pane.equalize", "pane", "Equalize pane sizes: every splitter in this tab back to equal shares", {QStringLiteral("Ctrl+Alt+0")});
        add("tab.moveToNewWindow", "tab", "Move tab to a new window (keeps its panes)", {});
        add("closed.restore", "pane", "Restore the last closed pane, tab or window", {QStringLiteral("Ctrl+Shift+Z")});
        add("closed.list", "pane", "Recently closed: the last 25 panes, tabs and windows, any of them reopened", {});
        add("windows.fresh", "window", "Start a fresh window set (forget the saved window layout)", {});
        // Jump to the newest notification's pane (#NQP9): 1 is the newest entry, again within the
        // window walks to the 2nd, and so on. Ctrl+Shift because a program that owns the terminal
        // must not swallow it (actsInsidePrograms) — the moment a bell entry matters is exactly
        // when the user is heads-down somewhere else. No letter is left: every Ctrl+Shift letter
        // is bound (M is agent.modelOptions, Options › Models, since 2026-09-20) or
        // reserved (C/V copy and paste, Q quits other terminals, U is the input method's Unicode
        // entry, D/P/Y are claimed in the preset tables), so the digit counts the walk: 1, 2, 3…
        // Free in the default table and all four presets.
        add("notifications.jump", "window", "Go to the newest notification's pane (again for the next older)",
            {QStringLiteral("Ctrl+Shift+1")});
        add("palette.open", "palette", "Actions: every action and its keys, in a filterable list (again to close it)", {QStringLiteral("Ctrl+Shift+A")});
        // One key opens and closes the explorer (issue #D60R). Ctrl+B is VS Code's sidebar key and
        // is free in all four Relay presets; Ctrl+Shift+B is the twin a program cannot swallow.
        add("files.explorer", "pane", "File explorer: open or close this pane's folder in an explorer pane",
            {QStringLiteral("Ctrl+B"), QStringLiteral("Ctrl+Shift+B")});
        add("files.open", "pane", "Open a file in a preview pane", {});
        add("board.open", "pane", "Switchboard: cards, threads and plans (again to close it)", {QStringLiteral("Ctrl+Shift+S")});
        // The helper agent's ask key (#FEJQ, protocol §30.7). **One** key for Options, Actions and
        // Sessions, because it is one helper — the tab's — wherever it is asked; the panel it
        // opens is the Switchboard's own panel, and the Switchboard keeps the bare `a` its list
        // page has had since #8YQ9, which it can have because that list takes no typing.
        // Alt+Q: free in the default table and in all four preset tables, and nothing else binds
        // an Alt+Q. It is not a Ctrl+Shift chord because it never has to reach past a program —
        // the three panes it works in hold none — and the panes' own search boxes swallow letters.
        add("helper.ask", "agent", "Ask the helper agent about this pane (Options, Actions, Sessions)",
            {QStringLiteral("Alt+Q")});
        // No key of its own: Ctrl+Shift+S opens it by itself in a tab with no project and no candidate (#916B).
        add("project.pick", "pane", "Projects: attach this tab to a project Relay knows, or initialize one here", {});
        add("control.human", "terminal", "Take control of the terminal (the only way keys reach it; works from the prompt box)", {QStringLiteral("Ctrl+H")});
        add("control.prompt", "terminal", "Back to the Relay prompt (the agent is in control)", {QStringLiteral("Ctrl+Shift+H")});
        add("program.delegate", "terminal", "Let the agent drive the program in this pane (with text in the prompt box, ask it now)",
            {QStringLiteral("Ctrl+Shift+J")});
        add("terminal.native", "terminal", "Toggle native terminal input (keys go straight to the terminal)", {QStringLiteral("F12")});
        add("pane.restartShell", "terminal", "Restart this pane's shell or agent after it stopped", {QStringLiteral("Ctrl+Shift+R")});
        add("terminal.interrupt", "terminal", "Interrupt the running command (Esc)", {});
        add("agent.newChat", "agent", "Start a new agent conversation and clear the terminal", {});
        // Keyboard walk over the files, folders and links in the output (issue GWXM). Free in
        // every preset (docs/KEYBINDING-PRESETS.md), so all four keep the Relay default.
        add("links.step", "terminal", "Step through files, folders and links in the output (Enter opens, Esc leaves)",
            {QStringLiteral("Ctrl+Shift+L")});
        add("agent.clearQueue", "agent", "Clear queued agent prompts", {});
        add("agent.resumeQueue", "agent", "Resume the paused agent queue", {});
        add("agent.stop", "agent", "Stop the agent turn", {});
        add("agent.stopAllSubagents", "agent", "Stop all running subagents", {QStringLiteral("Ctrl+Shift+X")});   // subagents UI
        add("agent.agentsMenu", "agent", "Agents: definitions and running subagents", {});
        // The tabbed subagent pane (card #WD83): from the prompt box it opens (or brings forward)
        // this pane's subagent pane; from the subagent pane it goes back to the main agent. A for
        // agents; no preset binds Alt+A, and Readline leaves M-a unbound.
        add("agent.subagentPane", "agent", "Subagents: open this pane's subagent tabs, or go back to the main agent from them",
            {QStringLiteral("Alt+A")});
        // Voice transcription: the hold key is its own setting (Settings › Voice), because a
        // push-to-talk key is held rather than pressed and is not a shortcut the keymap can bind.
        add("voice.toggle", "agent", "Voice transcription: start or finish recording", {});
        // Sharing a pane with a phone (#W5N2). No default shortcut: it is a deliberate act,
        // and the strip button beside the microphone is the usual way in.
        add("pane.share", "terminal", "Share this pane with a phone", {});
        // The Sharing pane (#W5N2): the ongoing "who is here, who is knocking, what is waiting
        // for me" surface. No default key either — it opens by itself when someone knocks, and
        // the share chip and the palette are the other two ways in.
        add("pane.sharing", "terminal", "Sharing: who is on your shared panes, and what is waiting for you", {});
        // Relay-to-Relay: a pane another desktop shares, opened here as one of your own devices. No
        // default key, like pane.share: pairing is a deliberate act and the palette is the way in.
        add("remote.openShared", "terminal", "Open a shared pane: a pane your other desktop shares, here as one of your devices", {});
        // Joining somebody else's share as a guest, with the meeting code and PIN they read out
        // (owner, 2026-09-18). No default key: /join CODE in any prompt box is the fast path.
        add("remote.join", "terminal", "Join a shared session: type the meeting code and PIN someone gave you", {});
        add("agent.interrupt", "agent", "Send to the agent; while it is busy, interrupt it and send now; on an empty box, continue a stopped turn (prompt box)",
            {QStringLiteral("Ctrl+Return"), QStringLiteral("Ctrl+Enter"), QStringLiteral("Ctrl+Alt+Return"), QStringLiteral("Ctrl+Alt+Enter")});
        add("agent.provider", "agent", "Provider and API keys (advanced endpoint settings)", {});
        add("agent.modelKeys", "agent", "API keys for model providers", {});
        add("agent.modelRoles", "agent", "Model roles: default provider and the Main / Flash / Lite models", {});
        // Settings › Local models (card #24XJ): the agent reads the local-model-setup skill and
        // serves a model on this machine. No default shortcut — it is a once-per-machine errand,
        // and Options › Local models is the way in.
        add("agent.localModelSetup", "agent", "Set up a local model with the agent (local-model-setup skill)", {});
        // O for options (owner, 2026-09-18). No plain Ctrl+O twin: one key per surface, and Ctrl+Shift
        // is the one a program cannot swallow. Ctrl+, stays as the key other apps taught.
        add("app.settings", "window", "Options: what persists, a tab per section (again to close it)",
            {QStringLiteral("Ctrl+Shift+O"), QStringLiteral("Ctrl+,")});
        // No default key: /update in the prompt box is the fast path, and a key nobody presses on
        // the average session is not worth claiming (the same reasoning as agent.localAgent).
        add("app.update", "window", "Update: download and install the latest Relay, then restart", {});
        add("agent.flashAgent", "agent", "Switch this pane between the Main agent and the Flash agent", {QStringLiteral("Alt+F")});   // model roles
        // M for models (owner, 2026-09-20: "models are more central than sessions"), three surfaces on
        // three chords: Alt+M drops the pane's model box open (the quick pick), Ctrl+Alt+M opens
        // the picker dialog with its filter, sort and reasoning level (what /model opens), and
        // Ctrl+Shift+M opens the model options — Options › Models (what /models opens). The two
        // Alt chords step aside for a program that owns the keyboard, like Alt+I; the Ctrl+Shift
        // one does not, so the options stay reachable from inside a full-screen program.
        add("agent.modelBox", "agent", "Models: drop this pane's model box open (the quick pick)", {QStringLiteral("Alt+M")});
        add("agent.model", "agent", "Models: the picker — every model with filter, sort and reasoning level (/model)", {QStringLiteral("Ctrl+Alt+M")});
        add("agent.modelOptions", "agent", "Model options: Options › Models — providers, which models the picker shows, their order (/models)", {QStringLiteral("Ctrl+Shift+M")});
        // No default key: /local in the prompt box is the fast path, and Alt+L is not worth
        // claiming for a switch most panes never make (card #JH22).
        add("agent.localAgent", "agent", "Switch this pane between the Main agent and the Local agent (a model served on this machine)", {});
        add("input.modeAuto", "agent", "Input mode: auto", {});
        add("input.modeTerminal", "agent", "Input mode: terminal", {});
        add("input.modeAgent", "agent", "Input mode: agent", {});
        add("input.toggle", "agent", "Cycle input: auto → terminal → agent (from the prompt box)",
            {QStringLiteral("Ctrl+I"), QStringLiteral("Ctrl+Shift+I")});
        add("agent.planToggle", "agent", "Toggle plan mode (from the prompt box)", {QStringLiteral("Shift+Tab")});
        add("agent.effortUp", "agent", "Raise reasoning effort (from the prompt box)", {QStringLiteral("Alt+.")});
        add("agent.effortDown", "agent", "Lower reasoning effort (from the prompt box)", {QStringLiteral("Alt+,")});
        add("agent.compact", "agent", "Compact the agent conversation", {});
        add("agent.rewind", "agent", "Rewind chat to an earlier turn; files are not changed (Esc Esc in an empty prompt box)", {});
        add("agent.rewindCode", "agent", "Rewind code: restore files the agent changed since an earlier turn (/rewind-code)", {});
        add("agent.fork", "agent", "Fork the conversation into a new pane", {});
        // Ctrl+Shift+Y: Warp's key for its conversations menu, and the free Ctrl+Shift letter with a
        // mnemonic (historY). It was Ctrl+Shift+M from 2026-09-19 to 2026-09-20, until the owner gave
        // M to models: "models are more central than sessions". The pane is titled "Sessions" again.
        add("agent.resume", "agent", "Sessions: resume a saved session, search, subagent threads (/resume)", {QStringLiteral("Ctrl+Shift+Y")});
        // The ⓘ view from the keyboard (owner, 2026-09-18: "the (i) view hotkey could be alt+i or
        // alt+1?"). Alt+I, because it says what it opens, and it is free: Relay's only other
        // Alt+letters are Alt+A (subagents), Alt+F (Flash) and Alt+R (reasoning); no preset table
        // binds an Alt+letter at all, so all four presets inherit this default (the konsole
        // preset's Ctrl+Alt+I and VS Code's Ctrl+Shift+Alt+I are different combinations and do not
        // collide); and Readline leaves M-i unbound, so a shell keeps the key — `\ei` has no
        // binding and `\eI` is only do-lowercase-version of it. No Ctrl+Shift twin: Ctrl+Shift+I is
        // input.toggle's, and like Alt+A and Alt+R this key steps aside for a program that has the
        // keyboard (actsInsidePrograms).
        add("agent.info", "agent", "Conversation info: model, tokens, file and history with subagent threads (/status, the ⓘ button)",
            {QStringLiteral("Alt+I")});
        // Conversation list with full-text search, and find-in-view for this pane.
        add("conversations.open", "agent", "Sessions: search every saved session and Relay's terminal history (/conversations)", {});
        add("find.inView", "agent", "Find in this pane: the conversation and the terminal scrollback (from the prompt box)",
            {QStringLiteral("Ctrl+F"), QStringLiteral("Ctrl+Shift+F")});
        add("agent.recap", "agent", "Recap this agent session", {});
        add("agent.requests", "agent", "Tasks: show or hide the agent's task list (/tasks)", {QStringLiteral("Ctrl+Shift+K")});
        // The reasoning fold from the keyboard (owner report, 2026-09-18: "need a keyboard
        // shortcut for showing / hiding the reasoning traces, maybe an F# key -- ... or alt+R").
        // Alt+R is his suggestion and no preset binds it: the only Alt+letter Relay has is Alt+F
        // (fast agent), and the four preset tables override no Alt+letter at all, so all of them
        // inherit this default. No Ctrl+Shift twin (Ctrl+Shift+R is pane.restartShell) and no
        // F-key twin yet: which F-keys Relay should claim is docs/F-KEYS.md's question, not a thing
        // to settle one action at a time.
        add("agent.thinkingPanel", "agent", "Reasoning: fold or unfold this pane's latest reasoning",
            {QStringLiteral("Alt+R")});
        // The Activity pane (card #QT8C, renamed from "agent internals" by #4X53): the reasoning
        // and the tool calls, live, in a pane
        // beside the terminal. Alt+R's shifted neighbour, because it is the reasoning fold's bigger
        // sibling; no preset binds Alt+Shift+R and no other action does.
        add("agent.internalsPane", "agent", "Activity: watch this pane's reasoning and tool calls in a pane beside it",
            {QStringLiteral("Alt+Shift+R")});
        add("agent.continue", "agent", "Continue the agent turn after a step limit or a cut-off restart (Ctrl+Enter on an empty box, or /continue)", {});
        add("agent.instructions", "agent", "Choose agent instruction files", {});
        add("agent.export", "agent", "Export the conversation as Markdown", {});
        // Image context: G for grab. Ctrl+Shift so it still works while a program owns the terminal,
        // which is exactly when a picture of the pane is worth sending (issue EM1E).
        add("agent.screenshotPane", "agent", "Screenshot this pane and attach it to the next prompt",
            {QStringLiteral("Ctrl+Shift+G")});
        // "Ctrl+?" is one gesture with several spellings. Qt reports the main-row key as
        // Key_Question on a US layout and as Key_Slash on others and on the keypad, with Shift
        // held either way, so all of them are bound: Ctrl+Shift+? reaches Ctrl+? and Ctrl+Shift+/
        // on a Key_Slash layout reaches Ctrl+/ through match()'s shifted-symbol fallback.
        add("help.shortcuts", "palette", "Actions: every action and the keys it answers to",
            {QStringLiteral("Ctrl+?"), QStringLiteral("Ctrl+Shift+/"), QStringLiteral("Ctrl+/"), QStringLiteral("F1")});
        add("keybindings.edit", "terminal", "Edit keyboard shortcuts", {});
        add("keybindings.reload", "terminal", "Reload keyboard shortcuts", {});
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        m_path = dir + QStringLiteral("/keybindings.json");
        QDir().mkpath(dir);
        // Watch the directory too: atomic replacement swaps the file out from under a file watch.
        m_watcher.addPath(dir);
        if (QFileInfo::exists(m_path)) m_watcher.addPath(m_path);
        auto onChange = [this] {
            if (QFileInfo::exists(m_path) && !m_watcher.files().contains(m_path)) m_watcher.addPath(m_path);
            QTimer::singleShot(100, [this] { reload(); });
        };
        QObject::connect(&m_watcher, &QFileSystemWatcher::fileChanged, onChange);
        QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged, onChange);
        reload();
    }

    static int combo(int key, Qt::KeyboardModifiers mods) { return int(mods) | key; }

    static int parse(const QString &text) {
        const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (sequence.count() != 1) return 0;
#if QT_VERSION_MAJOR >= 6
        int key = sequence[0].key(); Qt::KeyboardModifiers mods = sequence[0].keyboardModifiers();
#else
        int key = sequence[0] & ~int(Qt::KeyboardModifierMask); Qt::KeyboardModifiers mods(sequence[0] & int(Qt::KeyboardModifierMask));
#endif
        if (key == Qt::Key_Backtab) { key = Qt::Key_Tab; mods |= Qt::ShiftModifier; }
        if (key == 0 || key == Qt::Key_unknown) return 0;
        return combo(key, mods);
    }

    QJsonObject readObject() const {
        QFile file(m_path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        const auto doc = QJsonDocument::fromJson(file.readAll());
        return doc.isObject() ? doc.object() : QJsonObject();
    }

    void writeSetting(const QString &name, const QString &value) {
        QJsonObject root = readObject();
        if (!root.contains(QStringLiteral("version"))) root.insert(QStringLiteral("version"), 1);
        root.insert(name, value);
        writeObject(root);
    }

    void writeObject(const QJsonObject &root) {
        QSaveFile out(m_path);
        if (!out.open(QIODevice::WriteOnly)) return;
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (out.commit()) QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner);
        reload();
    }

    QList<ActionDef> m_actions;
    QHash<QString, QStringList> m_bindings;
    QHash<int, QString> m_lookup;
    QStringList m_conflicts;
    QString m_path, m_programKeys = QStringLiteral("shift-only"), m_preset = QStringLiteral("relay");
    QSet<QString> m_overrides;

    // Filled from docs/KEYBINDING-PRESETS.md research. Missing actions fall back to Relay defaults.
    static QByteArray presetJson() {
        return QByteArrayLiteral(R"PRESETS({"relay":{},"warp":{"agent.resume":["Ctrl+Shift+Y"],"app.settings":["Ctrl+Shift+O"],"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+T"],"tab.next":["Ctrl+PgDown","Ctrl+Tab"],"tab.previous":["Ctrl+PgUp","Ctrl+Shift+Tab"],"pane.splitRight":["Ctrl+Shift+D"],"pane.splitDown":[],"pane.splitLeft":[],"pane.splitUp":[],"pane.focusLeft":["Ctrl+Alt+Left"],"pane.focusRight":["Ctrl+Alt+Right"],"pane.focusUp":["Ctrl+Alt+Up"],"pane.focusDown":["Ctrl+Alt+Down"],"pane.moveLeft":[],"pane.moveRight":[],"pane.moveUp":[],"pane.moveDown":[],"pane.close":["Ctrl+Shift+W"],"closed.restore":["Ctrl+Alt+T"],"palette.open":["Ctrl+Shift+P"],"terminal.native":["F12"],"terminal.interrupt":[],"agent.newChat":[],"agent.stop":[],"agent.provider":[],"input.modeAuto":[],"input.modeTerminal":["Ctrl+Shift+I"],"input.modeAgent":[],"input.toggle":["Ctrl+I"],"keybindings.edit":["Ctrl+,"],"keybindings.reload":[],"agent.requests":[]},"vscode":{"app.settings":["Ctrl+Shift+O"],"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+~"],"tab.next":["Ctrl+PgDown","Ctrl+Tab"],"tab.previous":["Ctrl+PgUp","Ctrl+Shift+Tab"],"pane.splitRight":["Ctrl+Shift+%","Ctrl+\\"],"pane.splitDown":[],"pane.splitLeft":[],"pane.splitUp":[],"pane.focusLeft":["Alt+Left"],"pane.focusRight":["Alt+Right"],"pane.focusUp":["Alt+Up"],"pane.focusDown":["Alt+Down"],"pane.close":["Ctrl+W"],"closed.restore":["Ctrl+Shift+T"],"palette.open":["Ctrl+Shift+P"],"terminal.native":["Ctrl+`","F12"],"terminal.interrupt":[],"agent.newChat":["Ctrl+N"],"agent.stop":["Ctrl+Esc"],"agent.provider":["Ctrl+Alt+."],"input.modeAuto":[],"input.modeTerminal":[],"input.modeAgent":["Ctrl+Shift+Alt+I"],"keybindings.edit":["Ctrl+,"],"keybindings.reload":[],"agent.requests":[]},"konsole":{"app.settings":["Ctrl+Shift+O"],"window.new":["Ctrl+Shift+N"],"window.next":[],"window.previous":[],"tab.new":["Ctrl+Shift+T"],"tab.next":["Ctrl+PgDown"],"tab.previous":["Ctrl+PgUp"],"pane.splitRight":["Ctrl+Shift+(","Ctrl+("],"pane.splitDown":[],"pane.splitLeft":[],"pane.splitUp":[],"pane.focusLeft":["Ctrl+Shift+Left"],"pane.focusRight":["Ctrl+Shift+Right"],"pane.focusUp":["Ctrl+Shift+Up"],"pane.focusDown":["Ctrl+Shift+Down"],"pane.close":["Ctrl+Shift+W"],"closed.restore":[],"palette.open":["Ctrl+Alt+I"],"terminal.native":["F12"],"terminal.interrupt":[],"agent.newChat":[],"agent.stop":[],"agent.provider":[],"input.modeAuto":[],"input.modeTerminal":[],"input.modeAgent":[],"keybindings.edit":["Ctrl+Alt+,"],"keybindings.reload":[],"agent.requests":[]}})PRESETS");
    }
    QFileSystemWatcher m_watcher;
    QList<QPair<QPointer<QObject>, std::function<void()>>> m_listeners;
};

