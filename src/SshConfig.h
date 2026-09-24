// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// SSH hosts for "Connect to host…" and the ssh command line "Split on the same host" re-runs
// (card #S5SH, docs/SSH-AND-MOSH.md section 8). Rules only, no widgets, so all of it is testable
// against a temporary ~/.ssh.
//
//  * The concrete host aliases of ~/.ssh/config and every file it Includes: each word after a
//    `Host` keyword with no `*`, `?` or `!`, in file order, deduplicated, with the HostName, User
//    and Port written in a Host block that names it (first value wins, as in ssh). `Match` blocks
//    are not host lists and their settings are not shown. Includes follow ssh: a relative path is
//    relative to ~/.ssh, `~/` is the home directory, each argument may be a glob, nesting is
//    followed to a depth of 16 and a file already being read is not read again.
//  * The recently used hosts (QSettings `ssh/recent`, most recent first, at most 20).
//  * The command line of a running ssh or mosh, rebuilt from its argv as a shell-quoted line.
#include <QList>
#include <QString>
#include <QStringList>

namespace relay::ssh {

struct Host {
    QString alias;      // what `ssh <alias>` takes
    QString hostName;   // HostName, as written (may hold %h tokens)
    QString user;       // User
    QString port;       // Port
    // "user@hostname:port" from what is written; empty when the block says none of the three.
    QString detail() const;
};

// Every concrete host in `configPath` and its Includes. `sshDir` is what relative Include paths
// are resolved against and `home` what `~` means; both default to the user's.
QList<Host> parseConfig(const QString &configPath, const QString &sshDir = QString(), const QString &home = QString());
// ~/.ssh/config, or an empty list when there is none.
QList<Host> userHosts();

// Recently used hosts, most recent first.
constexpr int kRecentLimit = 20;
QStringList recentHosts();
void rememberHost(const QString &target);
// Pure form of the same rule, for tests: `target` moved to the front, duplicates dropped, capped.
QStringList withRecent(QStringList recent, const QString &target, int limit = kRecentLimit);

// One word for a POSIX shell: left bare when it holds only safe characters, else single-quoted.
QString shellQuote(const QString &word);

// `ssh <target>` for an alias or a `user@host`, quoted as needed.
QString connectCommand(const QString &target);

// True when this machine has a mosh client, for persistentCommand()'s transport choice.
bool hasLocalMosh();

// The zellij/tmux session name a persistent connection to `target` attaches to: `relay-` plus the
// target's safe characters, so the same host (and user) always lands on the same session.
QString persistentSession(const QString &target);

// A shell line that opens `target` on a session that outlives the connection (card #VD2M): mosh
// when this machine has it, `ssh -t` otherwise, and on the host `zellij attach --create <session>`
// or, without zellij, `tmux new -A -s <session>`, or a login shell when it has neither. Closing
// Relay or losing the network leaves the session running; running the line again re-attaches.
QString persistentCommand(const QString &target, bool haveMosh);

// What the Actions search box holds, read as a host to connect to: "user@host", or "ssh <host>"
// (with an optional user@). Empty when it does not look like one, so ordinary searches never
// grow a "connect to" row.
QString typedTarget(const QString &search);

// The argv of a process from /proc/<pid>/cmdline; empty when it cannot be read.
QStringList processArgv(int pid);

// A shell line that starts the same remote session again, or empty when `argv` is not an
// interactive ssh or mosh (only those two are re-run; ssh -O/-G/-V/-Q/-W are not sessions, and a
// second -N or -f tunnel would only collide with the first). The program is written as plain
// `ssh`/`mosh` so a Relay pane shell's wrapper applies, and the connection-sharing options that
// wrapper added (a ControlPath in Relay's relay-ssh directory) are taken out again so they are not
// doubled. A mosh session is usually seen as mosh-client, whose `-# <original arguments> |`
// argument is where the arguments come from. `host`, when given, receives the destination.
QString rerunCommand(const QStringList &argv, QString *host = nullptr);

}  // namespace relay::ssh
