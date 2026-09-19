// SPDX-License-Identifier: AGPL-3.0-or-later
// What a crash leaves behind.
//
// Relay died twice on 2026-09-19 (16:30:40 and 16:36:18) and left nothing to look at. The log
// simply stops mid-line — no `gui_quit`, no last words — and there is no core either:
// kernel.core_pattern on this machine pipes to apport, and apport drops anything that is not part
// of a package, which a locally built `build/relay` never is ("executable does not belong to a
// package, ignoring", /var/log/apport.log). So the one record of two crashes was their absence.
//
// This writes the faulting frames into relay.log as it happens, in the same file everything else
// about that run is in, so "it crashed" is a line you can read rather than a gap you have to
// notice. It is deliberately the small instrument: it names the function most of the time, and
// `scripts/relay-debug` (gdb) is there for when it does not — every thread, arguments and locals.
//
// A handler for a fatal signal may call only async-signal-safe functions: no malloc, no QString,
// no Qt. Everything this needs — the path to write to, the build id, the unwinder's first call —
// is therefore prepared in install() and only written out in the handler, with open/write/fsync
// and hand-rolled integer and date formatting. The report ends by restoring the default handler
// and re-raising, so the process still dies of the signal it was killed by: a debugger still
// catches it, apport still sees it, and the exit status is still the crash.
#pragma once
#include <QString>

namespace relay::crashlog {

// Install handlers for SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT and SIGSYS. Called once, after
// the log directory is known; `buildId` is copied now (relay::buildinfo::running().id) because the
// handler cannot ask for it later. A signal already ignored when Relay started is left ignored.
void install(const QString &buildId);

// Follow the logging level: relay::log::write() calls this, so turning logging off at any time
// also turns off the crash report's copy in the file (it still goes to stderr). The handler cannot
// read a QSettings value itself — nothing in it may allocate — so the answer is kept up to date
// here instead.
void noteLogEnabled(bool enabled);

// The report the handler would write to, fixed at install() — relay.log, or empty when the log
// directory could not be made, in which case the report goes to stderr alone.
QString reportPath();

} // namespace relay::crashlog
