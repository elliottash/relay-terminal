// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Options › Security › MCP servers (card #9M96, the rows card #3KB7 reserved for card #SSRQ).
//
// The worker owns MCP configuration: `backend/relay_core/mcp_config.py` holds the schema, the
// project-enablement digests and the lock on the global file, and `mcp_import.py` reads Claude
// Code, Codex and Warp. This block never writes a config file itself. It reads
// `mcp_config list --json` (cached, re-read when the global file or the project's `.mcp.json`
// changes) and every button runs the same CLI a person or the bundled `mcp-servers` skill would.
// Server env and header *values* never come back from the CLI; the Add dialog sends the ones it
// is given on the CLI's stdin, never in argv, where any user on the machine could read them.

#include "SettingsPane.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace relay::mcp {

// What the rows ask their owner to do. Kept apart from the process and the dialogs so the rows
// can be tested headless (tests/mcpsettings_test.cpp).
struct Hooks {
    // Run `python -m relay_core.mcp_config <args>`; `confirm`, when set, is asked first.
    std::function<void(const QStringList &args, const QString &confirm)> run;
    std::function<void()> add;        // the Add server dialog
    std::function<void()> import;     // the Import preview dialog
};

// The block's rows from one `list --json` document. `error` (the CLI could not be run) replaces
// the server rows with one line saying so; `loading` says the first read is still out.
QList<SettingRow> rowsFor(const QJsonObject &listed, const QString &workspace, const Hooks &hooks,
                          bool loading = false, const QString &error = {});

// The Add dialog's fields, and the CLI call they become.
struct AddForm {
    QString name;
    bool url = false;                 // false: `target` is a command line; true: an http(s) URL
    QString target;                   // the command (with its arguments, shell-quoted) or the URL
    QStringList pairs;                // KEY=VALUE lines: env for a command, headers for a URL
    bool trusted = false;
};
// `mcp_config add …` arguments; the KEY=VALUE pairs go into `*stdinJson`, with --secrets-stdin.
// Returns an empty list and sets `*problem` when the form cannot become a server.
QStringList addArguments(const AddForm &form, QByteArray *stdinJson, QString *problem);

// The whole block for Options › Security: the cached read, the processes and the two dialogs.
// `say` puts one line in the window's status bar.
QList<SettingRow> settingsRows(const QString &workspace, QWidget *parent,
                               std::function<void(const QString &)> say);

}  // namespace relay::mcp
