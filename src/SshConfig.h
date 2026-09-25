// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// SSH hosts for "Connect to host…" and the ssh command line "Split on the same host" re-runs
// (card #S5SH, docs/SSH-AND-MOSH.md section 8), and the holder sessions that keep a remote pane
// alive on its host across disconnects, pane closes and Relay restarts (card #XQ8F). Rules only,
// no widgets, so all of it is testable against a temporary ~/.ssh.
//
//  * The concrete host aliases of ~/.ssh/config and every file it Includes: each word after a
//    `Host` keyword with no `*`, `?` or `!`, in file order, deduplicated, with the HostName, User
//    and Port written in a Host block that names it (first value wins, as in ssh). `Match` blocks
//    are not host lists and their settings are not shown. Includes follow ssh: a relative path is
//    relative to ~/.ssh, `~/` is the home directory, each argument may be a glob, nesting is
//    followed to a depth of 16 and a file already being read is not read again.
//  * The recently used hosts (QSettings `ssh/recent`, most recent first, at most 20).
//  * The command line of a running ssh or mosh, rebuilt from its argv as a shell-quoted line —
//    minus what the pane shell's wrapper added to it for the holder.
//  * The holder of a persistent remote pane: the session name inside a wrapped login, and the
//    tmux lines that list, end and re-attach the holder sessions of a host.
#include <QByteArray>
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
// Superseded by the per-pane holder sessions of #XQ8F (holderSession below); kept until nothing
// calls it.
QString persistentSession(const QString &target);

// A shell line that opens `target` on a session that outlives the connection (card #VD2M): mosh
// when this machine has it, `ssh -t` otherwise, and on the host `zellij attach --create <session>`
// or, without zellij, `tmux new -A -s <session>`, or a login shell when it has neither. Closing
// Relay or losing the network leaves the session running; running the line again re-attaches.
// Superseded by the wrapper-built holder of #XQ8F, which covers every ssh pane and not only this
// one line; kept until nothing calls it.
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
// `ssh`/`mosh` so a Relay pane shell's wrapper applies again from scratch, so what that wrapper
// added to the line comes out with it: the connection-sharing options (a ControlPath in Relay's
// relay-ssh directory), the `-t` a holder login travels with, and the holder remote command
// itself (`sh -c … relay-holder <session> [<cwd>]`, ssh and mosh shapes, card #XQ8F). A mosh
// session is usually seen as mosh-client, whose `-# <original arguments> |` argument is where the
// arguments come from. `host`, when given, receives the destination.
QString rerunCommand(const QStringList &argv, QString *host = nullptr);

// The session name and the start directory the pane shell's wrapper wrote into a login's remote
// command (`sh -c '<holder>' relay-holder <session> [<cwd>]`, card #XQ8F): read from whichever
// shape the running process shows them in — ssh's single final word, mosh's words after `--`, or
// mosh-client's `-#` line, where the whole command was joined with spaces and the script's quotes
// are literal characters — so a pane's argv alone says which holder it is in, whatever transport
// carried it. Empty when the argv carries no holder command.
QString holderSession(const QStringList &argv);
QString holderCwd(const QStringList &argv);

// One session on a host's holder server (`tmux -L relay`), as listSessionsCommand reports it.
struct RemoteSession {
    QString name;        // `relay-<pane id>` or the custom name a reattachCommand set
    qint64 created = 0;  // unix seconds; 0 when the line does not carry it
    int attached = 0;    // attached clients; 0 when the line does not carry it
};

// Ends one holder session on the host ("Close and end the remote session"): the socket is the
// holder server's own, so nothing else the person runs in tmux there is ever touched.
QString killSessionCommand(const QString &session);

// Lists the holder sessions of a host with the fields parseSessionList splits on; no sessions yet
// is an empty answer, not an error the pane should see.
QString listSessionsCommand();

// Reads listSessionsCommand's answer: Relay's `relay-` sessions only, tolerating short and junk
// lines, because a session that cannot be re-attached must not be offered as one that can.
QList<RemoteSession> parseSessionList(const QByteArray &output);

// The line a saved or listed remote pane resumes with: the plain login the person typed plus the
// session it had, which the pane shell's wrapper reads as the holder to attach to instead of
// minting a new one — so a restored pane lands where it was, its programs still running.
QString reattachCommand(const QString &target, const QString &session);

// A no-op ssh over the master behind a mosh login (card #XQ8F): mosh's own ssh exits as soon as
// the connection is up, so nothing of Relay's holds the socket open and ControlPersist expires it
// ten idle minutes after the agent last used it — the host tools go with it. Run detached every
// four minutes it resets that clock. An ssh login needs no such thing: its own session keeps its
// master. Empty when either part is missing, for a login with no master to keep.
QStringList keepaliveArgv(const QString &controlPath, const QString &host);
QString keepaliveCommand(const QString &controlPath, const QString &host);

// The values the pane shell's environment carries for its ssh/mosh wrapper (card #XQ8F). Pure so
// the rules are testable without a pane: persistence is armed only when the wrapper itself can
// share a connection at all; only "ssh" and "mosh" are transports the wrapper knows how to route,
// anything else falls back to plain ssh; and the never list is what the wrapper compares the
// destination's host against, trimmed and emptied of blank entries.
QString sshPersistValue(bool wrapSsh, bool persist);
QString sshLinkValue(const QString &setting);
QStringList sshNeverList(const QStringList &hosts);

}  // namespace relay::ssh
