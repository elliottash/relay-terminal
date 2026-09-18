// SPDX-License-Identifier: GPL-3.0-or-later
// relay::slash: what counts as an attempt at a Relay slash command, and what Relay says when
// the command does not exist.
//
// The pane owns the list of names (the built-ins in `Pane::slashCommands()` plus this window's
// aliases); these functions only decide whether a submitted line is a command attempt at all
// and, if the name is unknown, which real names it is closest to. Kept out of the Pane so the
// rules can be tested without a window, and so the one rule that matters for safety — a line
// that is really a path (`/usr/bin/foo`, `/tmp`) is never treated as a command — has a test.
// See issues/changes/needs_qa_llm/2026-09-18-unknown-slash-command.md.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace relay::slash {

// Does this path exist on the machine? Injected so `/tmp` and `/usr` can be tested anywhere.
using Probe = std::function<bool(const QString &)>;

// The name a submitted line is trying to invoke ("compact" for `/compact focus`), or an empty
// string when the line is not an attempt at a Relay command. It is not an attempt when:
//   * it does not start with `/`, or is `/` alone;
//   * the first word holds another `/`, or anything that is not a name character — `/usr/bin/foo`,
//     `/etc/hosts`, `/home/me/run.sh --flag` are paths, and the shell keeps them;
//   * the first word names something that exists on disk (`/tmp`, `/bin`), which is a path too;
//   * the draft has more than one line, which is prose or a paste, not a command.
// The caller decides whether the name is real: an unknown one is answered by `unknownLine()`.
QString attemptedName(const QString &text, const Probe &exists = {});

// The real names closest to `name`, best first, at most `max`. A name the user was completing
// (`/conv` for `/conversations`) ranks first, then one or two typing slips away — an optimal
// string alignment distance, so a transposition ("comapct") counts as the one slip it is.
// Comparison ignores case; the names come back spelled the way the registry spells them.
QStringList closest(const QString &name, const QStringList &known, int max = 3);

// The line Relay prints instead of letting the shell answer with "command not found".
// It names the command, suggests the closest real ones, and points at `/` and `/help`.
QString unknownLine(const QString &name, const QStringList &known);

}  // namespace relay::slash
