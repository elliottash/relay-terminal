// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::agent::ArtifactContext — the docked agent on a file editor (card #PBZ4, slice 7 of
// #P2W8).
//
// The owner, 2026-09-25: "then there are artifact panes … where the content is an editable object
// you are interacting with, rather than a console. … in an artifact pane, you have the agent
// system prompt docked at the bottom, same as a console pane. it would have special tools in it
// … it could also have commands that can be slashed or auto-detected, and there would be a
// plug-in spec defining that."
//
// So an open file gets exactly what a card page has (`CardContext`, src/BoardPane.cpp): a
// `Context` saying what the agent is about — this file, the task plugin its name activates, where
// the cursor and the selection are and whether the buffer is dirty — the plugin's commands as the
// action row and in the `/` popup, and a link rule that sends `file:line` for this file to the
// editor rather than to a new pane. The console is the ordinary no-shell `Pane` the window makes
// (`RelayWindow::createAgentConsole`); nothing here is a widget.
//
// What the agent's *edits* do is not this file's: a write to a file open in Relay already goes
// through the editor as one undo step (#F8R7, protocol §35, `FilePreview::answerBufferRequest`).
// The record of a file artifact is those undo steps plus a per-turn change list (owner decision
// U5), which the host draws; `turnFinished` is how it learns a turn ended.
//
// QtCore only, like the rest of `relay-agentcontext`, so the plugin reader and the spec are tested
// without a window (tests/agentcontext_test.cpp).
#pragma once

#include "AgentContext.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace relay::agent {

// ---------------------------------------------------------------------------------------------
// The task plugin a file activates, as far as the editor needs to know it: who it is and the slash
// commands its manifest (schema v2, `commands`, #6FDD) offers in the editor role. Discovery,
// enablement and activation are the worker's (`backend/relay_core/task_plugins.py`); this is a
// read-only mirror of the same three rules — the origins in precedence order, `activation.files`,
// and the enablement state file — so the `/` popup can list the commands without a round trip.

struct PluginCommand {
    QString name;          // "outline"
    QString description;
    QString args;          // "<focus>" / "[focus]" / "" — from the manifest's `args`
    QString kind;          // "prompt" or "tool" (a `program_line` has no program in an editor)
    QString target;        // the prompt text, or the tool name
};

struct ArtifactPlugin {
    QString id;            // "relay.markdown"; empty when no plugin claims the file
    QString name;          // "Markdown"
    QString origin;        // "project", "global" or "bundled"
    QStringList files;     // `activation.files`, the globs that select a file
    QList<PluginCommand> commands;
    bool valid() const { return !id.isEmpty(); }
    // The namespace a colliding slash name moves into: the id's last segment ("markdown").
    QString space() const;
};

// Where plugins are looked for. `bundled` is the directory `plugins_bundled/` ships in, `global`
// is `$XDG_CONFIG_HOME/relay/plugins`, and `state` is the enablement file (`plugins.json`,
// `RELAY_PLUGIN_STATE`). Project packages are `<dir>/.relay/plugins` for the file's folder and
// each parent up to the git root, nearest first, as `task_plugins.project_plugin_dirs` has them.
struct PluginSearch {
    QString bundled;
    QString global;
    QString state;
    // The defaults: `bundled` from `RELAY_BACKEND_DIR` or beside the executable is the caller's to
    // fill (only the app knows where it is installed); `global` and `state` follow the XDG rules.
    static PluginSearch defaults(const QString &bundledDir);
};

// The first enabled plugin whose `activation.files` select `path`, by origin precedence (project,
// global, bundled) and then id, or an invalid one. A remote `ssh://` file is matched by name
// against the global and bundled packages only: its project folder is on another machine.
ArtifactPlugin pluginForFile(const QString &path, const PluginSearch &search);

// One manifest, read: the fields above, or an invalid plugin when the JSON is not a manifest.
// `role` filters the commands by their `when.roles` ("editor" for a file artifact).
ArtifactPlugin readPluginManifest(const QJsonObject &manifest, const QString &origin,
                                  const QString &role = QStringLiteral("editor"));

// Does a manifest's `activation.files` glob select this file? A glob without '/' is matched against
// the file name, one with '/' against the path relative to `root` (the project) — fnmatch's rules,
// which `QRegularExpression::wildcardToRegularExpression` follows for `*`, `?` and `[...]`.
bool activationMatches(const QString &glob, const QString &path, const QString &root = QString());

// ---------------------------------------------------------------------------------------------
// What the editor tells the context about the buffer, asked fresh before every `configure` and
// every `ask`.
struct ArtifactState {
    int line = 0;          // 1-based cursor line, 0 when unknown
    int column = 0;        // 1-based
    int firstLine = 0;     // the selection's lines, 0 when nothing is selected
    int lastLine = 0;
    QString selection;     // the selected text, cut to fit `screen`
    bool dirty = false;    // unsaved edits
    bool editable = false; // an editor rather than a read-only preview
    QString mode;          // "text", "markdown", "markdown source", "plan"
};

class ArtifactContext final : public Context {
  public:
    ArtifactContext() = default;

    // The file the agent is docked on. Setting it again with another path moves the conversation
    // (`persistKey` is per file), which is what a preview pane opening another file should do.
    void setFile(const QString &path);
    QString file() const { return m_file; }
    void setPlugin(const ArtifactPlugin &plugin);
    const ArtifactPlugin &plugin() const { return m_plugin; }

    // The host's half, all optional. `state` answers the cursor, selection and dirty flag;
    // `save` / `revert` are the editor's own; `goToLine` is how a `file:line` link for this file
    // lands; `sendPrompt` sends a plugin command's prompt to this console (the host has the
    // `ConsoleHandle`); `onTurnFinished` refreshes the per-turn change list.
    std::function<ArtifactState()> state;
    std::function<bool()> save;
    std::function<bool()> revert;
    std::function<void(int line)> goToLine;
    std::function<void(const QString &prompt)> sendPrompt;
    std::function<void(const TurnRecord &)> onTurnFinished;

    ContextSpec spec() const override;
    QList<Action> actions() const override;
    QList<ContextCommand> slashCommands() const override;
    bool resolveLink(const relay::links::Target &target) override;
    void turnFinished(const TurnRecord &record) override;
    QString placeholder() const override;

    // "README.md agent" — what the console's header says.
    QString title() const;
    // The prompt a plugin command sends for `args` (the words typed after it): the manifest's
    // prompt with `{file}` filled in, or an instruction to run the tool on this file.
    QString promptFor(const PluginCommand &command, const QString &args) const;
    // The `screen` hint, exactly as it goes out: file, plugin, cursor, selection, dirty.
    QString screen() const;

  private:
    QString m_file;
    ArtifactPlugin m_plugin;
};

// `file:<path>` for a short path, `file:sha1:<16 hex>` for one that would not fit the worker's
// 128-character persist key or 64-character surface once the tab id is in front of it.
QString artifactKey(const QString &path, int limit);

} // namespace relay::agent
