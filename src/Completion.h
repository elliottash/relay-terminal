// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QStringList>

namespace relay {

// Tab completion for the prompt box. Relay sends whole lines, so Readline never sees the
// half-typed word and cannot complete it: paths and command names are completed here instead.
struct Completion {
    int start = 0;        // where the replaced token begins in the line
    int length = 0;       // characters the token occupies
    QString common;       // longest shared completion, shell-escaped and ready to insert
    QStringList inserts;  // candidates, shell-escaped; directories end in '/'
    QStringList labels;   // the same candidates as shown in the popup (unescaped, last segment)
    bool commands = false;  // command names rather than paths
};

// line: the text being edited; cursor: position within it. cwd: the pane's directory.
// commands: known command names, used only for the first word of a shell command.
Completion completeAt(const QString &line, int cursor, const QString &cwd, const QStringList &commands);

// Quote a path for the shell the way Readline does: backslash-escape the shell-special characters.
QString escapeToken(const QString &text);
// Undo escapeToken, so a half-typed `my\ file` matches the real name.
QString unescapeToken(const QString &text);

}  // namespace relay
