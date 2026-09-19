// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// SSH and mosh sessions in a terminal pane (card #S5SH, docs/SSH-AND-MOSH.md). Pure rules, no
// widgets and no processes: the pane runs `ssh -G` and types the bootstrap; this file decides
// what to run, reads what came back, and builds the line that is typed.
//
//  * Which host. `ssh -G <the program's own arguments>` resolves aliases, Include, Match,
//    HostName, User, Port and the ControlPath exactly as the running ssh did, without touching
//    the network. mosh and mosh-client are reduced to the ssh arguments mosh would have used.
//  * Whether the agent can reach it. A live control socket at that ControlPath is the user's own
//    authenticated connection; `ssh -S <path>` reuses it with no second login.
//  * The remote integration (shell/remote-integration.sh), typed once per login as one line.
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace relay::remote {

// What `ssh -G` says about the destination, and where the program's arguments named it.
struct Resolved {
    QString host;          // the destination as typed: "filly", "me@box" → "box"
    QString hostname;      // after HostName and canonicalisation: "65.109.126.152"
    QString user;
    int port = 22;
    QString controlPath;   // empty when the config says "none"
    QString controlMaster; // "auto", "yes", "no", …: whether ssh shares its connection
    bool ok = false;       // the dump had at least a hostname
};

// The program is one Relay treats as a remote login (ssh, mosh, mosh-client).
bool isLoginProgram(const QString &program);

// Arguments for `ssh -G` that describe the same destination as the program's own argv (argv[0]
// included). Empty when there is no destination to resolve (`ssh -V`, a bare `mosh`).
// `relaySocketDir` is Relay's ControlPath directory: mosh's own ssh used it (the wrapper in
// shell/integration.bash), so it is passed along when mosh's arguments do not say otherwise.
QStringList dumpArguments(const QStringList &argv, const QString &relaySocketDir = QString());

// The destination argument as typed ("filly", "me@filly"), from the same argv.
QString destination(const QStringList &argv);

// Parse `ssh -G` output ("hostname 1.2.3.4\nuser me\n…"). `host` is filled from `destination`.
Resolved parseDump(const QByteArray &dump, const QString &destination = QString());

// A host name from OSC 7 names this machine (or no machine at all).
bool isLocalHost(const QString &host, const QString &localName);

// Remove the control sockets in `dir` that no ssh is listening on any more: a master killed
// outright leaves its socket behind, and under a shared /tmp it would outlive the machine's
// uptime. A socket someone answers on is never touched, and neither is anything that is not one.
// Returns how many were removed.
int pruneSockets(const QString &dir);

// gzip framing around raw deflate, so a remote `gzip -dc` can read what Qt compressed.
QByteArray gzip(const QByteArray &data);

// The line typed into the remote shell to load `script` (shell/remote-integration.sh):
//   " RELAY_R=<rows> eval \"$(printf %s '<base64>' | base64 -d | gzip -dc)\""
// The leading space keeps it out of history. RELAY_R is how many rows the old prompt and this
// echoed line take, given the column the prompt left the cursor at and the terminal's width, so
// the script can erase them and the session looks as if the prompt was simply redrawn.
QString bootstrapLine(const QByteArray &script, int promptColumn, int columns);

// The prompt a remote shell last drew, kept from the bytes it sent, so Relay can put it back
// after printing over it (a remote line editor without Relay's integration cannot be asked to
// redraw itself). Everything but text and colour is dropped: SGR ("\033[…m") is kept, every other
// escape, control byte and OSC is not, so what goes back is what the prompt looked like and
// nothing that could move the cursor or drive the terminal. `columns` receives how many columns
// the kept text occupies, which the caller compares with where the cursor really is.
QByteArray promptEcho(const QByteArray &raw, int *columns = nullptr);

// Rows a line of `length` characters takes when it starts at `column` of a `columns`-wide
// terminal, counting the row it starts on.
int rowsFor(int column, int length, int columns);

}  // namespace relay::remote
