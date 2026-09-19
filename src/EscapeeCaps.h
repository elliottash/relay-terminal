// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Capping the programs that leave their pane (card #Y4RX).
//
// Per-pane isolation (src/Isolation.h) puts each pane's shell and agent in their own transient
// systemd scope, and a runaway there is stopped inside the pane. Two programs get out from under
// that: **tmux** moves its server into `tmux-spawn-<uuid>.scope` and **Chrome** puts each app
// instance in `app-com.google.Chrome-<pid>.scope`, both directly under `app.slice`. A child may ask
// the user's systemd for a transient scope of its own over D-Bus, and a scope created that way is a
// sibling of the pane's, not a child: Relay cannot contain it from inside the pane's scope. Their
// memory therefore counts against no pane's limit, and an OOM kill in `app.slice` can land on any
// process there — including one another pane depends on. That hole is documented, not closed
// (docs/ARCHITECTURE.md section 13).
//
// What this file offers is the owner's opt-in mitigation, off by default: systemd user drop-ins on
// the two unit-name *prefixes*, which `man systemd.unit` specifies for any unit name containing
// dashes — "for a unit name foo-bar-baz.service not only the regular drop-in directory
// foo-bar-baz.service.d/ is searched but also both foo-bar-.service.d/ and foo-.service.d/".
// `tmux-spawn-.scope.d/` therefore covers every `tmux-spawn-<uuid>.scope`, and
// `app-com.google.Chrome-.scope.d/` every Chrome app scope, whoever starts them. Verified on this
// machine (systemd 255) against a throwaway `systemd-run --user --scope --unit=tmux-spawn-…` scope,
// which reported the drop-in's `MemoryMax` and named the file in `DropInPaths`.
//
// **The cap is machine-wide for those two programs, not per pane.** It applies to a tmux or Chrome
// the user starts outside Relay exactly as it does to one a pane started, because the unit name is
// all systemd gives us to match on: nothing in it says which pane, or whether Relay was involved.
// Say so wherever it is offered.
//
// Only files carrying marker() are ever written or removed, so a drop-in the user wrote by hand at
// one of these paths is left exactly as it is — and reported, rather than silently overwritten.

#include <QString>
#include <QStringList>

namespace escapees {

// The two caps a drop-in sets, as systemd sizes ("8G", "512M", "infinity").
struct Caps {
    QString memoryMax;
    QString swapMax;
};

// The unit names the drop-ins are written for: truncated-prefix names, `-` before `.scope`.
QStringList unitNames();

// The line that says a file is Relay's. remove() touches nothing without it.
QString marker();

// `<configRoot>/systemd/user/<unit>.d` and the `relay.conf` inside it. `configRoot` is
// `$XDG_CONFIG_HOME` (`~/.config`); a test passes a temporary directory.
QString dropInDir(const QString &configRoot, const QString &unitName);
QString dropInPath(const QString &configRoot, const QString &unitName);

// The file's contents for `caps`.
QString dropInText(const Caps &caps);

// Whether a file's text is one Relay wrote.
bool ours(const QString &text);

// Write both drop-ins. Returns the paths written; anything it refused to touch (a file without the
// marker) is appended to `skipped` and left alone.
QStringList install(const QString &configRoot, const Caps &caps, QStringList *skipped = nullptr);

// Remove exactly the files Relay wrote, and the `.d` directories if they are then empty. Returns
// the paths removed; a file without the marker is left alone and appended to `skipped`.
QStringList removeAll(const QString &configRoot, QStringList *skipped = nullptr);

// Whether Relay's drop-in is in place for every unit name.
bool installed(const QString &configRoot);

// `$XDG_CONFIG_HOME`, or `~/.config` when it is unset — where the user manager reads drop-ins from.
QString userConfigRoot();

// `systemctl --user daemon-reload`, so an install or a removal takes effect for the next scope.
// Returns false when systemctl is missing or the reload failed.
bool reload();

}  // namespace escapees
