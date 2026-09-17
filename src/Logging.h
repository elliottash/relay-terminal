// SPDX-License-Identifier: GPL-3.0-or-later
// Rotating diagnostics log for the Relay window (issue SQAM).
//
// Relay used to write only to stderr, which is thrown away when it is started from a desktop
// launcher: after a stalled turn there was nothing to look at. This writes
// $XDG_DATA_HOME/relay/logs/relay.log (default ~/.local/share/relay/logs/), 5 MiB x 3 rotation,
// mode 0600 in a 0700 directory, in the same line format as the worker's worker.log.
//
// What must never be written here: prompt text, agent answers, reasoning, tool output, terminal
// output, file contents, API keys and anything typed in password mode. Log identifiers, event
// types, counts, durations and error types. scrub() masks credential-shaped text as a second line
// of defence, not as a licence. The opt-in "verbose" level is the single exception and only the
// backend uses it for prompt text.
#pragma once
#include <QString>
#include <QStringList>

namespace relay::log {

enum class Level { Off, Error, Info, Debug, Verbose };

// ~/.local/share/relay/logs, created 0700 on first use. Empty when it cannot be created.
QString directory();
QString filePath();

Level level();
void setLevel(const QString &name);
QString levelName(Level level);
// {id, label, detail} for the settings menu, in increasing detail.
QList<QStringList> levelChoices();

// One line, dropped when the configured level is lower. `message` should already be
// "<event> key=value key=value".
void write(Level level, const QString &message);
inline void error(const QString &message) { write(Level::Error, message); }
inline void info(const QString &message) { write(Level::Info, message); }
inline void debug(const QString &message) { write(Level::Debug, message); }

QString scrub(const QString &text);
// Route qWarning()/qCritical()/Qt internals into the same file, so a launcher-started Relay keeps them.
void installMessageHandler();

} // namespace relay::log
