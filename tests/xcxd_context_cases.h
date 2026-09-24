// SPDX-License-Identifier: AGPL-3.0-or-later
// The terminal-context regressions for card #XCXD: an automatic snapshot is marked with
// refresh intent and an explicit pane/generation scope, so the worker resolves it when
// the turn actually starts instead of when the prompt queued. Pure records logic only:
// these cases run without a Pane, a worker or a clock; the pane-side marker sits in
// Pane.h's dispatch path.
// Parent: include after the CHECK macros exist, call cases::xcxdContextCases() from both
// the --xcxd-only and the default test main.
#pragma once

#include "../src/TerminalRecords.h"

namespace cases {

void xcxdContextCases() {
    using relay::terminalcontext::Records;

    // The scope a refresh-eligible snapshot carries identifies the pane and the shell
    // generation; it exists before any command and survives stores.
    Records records;
    CHECK(!records.pane().isEmpty());
    CHECK(!records.generation().isEmpty());
    const QString pane = records.pane();
    const QString generation = records.generation();
    records.begin(QStringLiteral("echo one"), QStringLiteral("/tmp"), QStringLiteral("host"));
    CHECK(records.pane() == pane);
    CHECK(records.generation() == generation);

    // A shell restart starts a new generation: a queued scope never resolves into the
    // next generation's records.
    records.resetGeneration();
    CHECK(records.generation() != generation);

    // The automatic selection is user-origin, current generation only: agent-run
    // terminal history never broadens it.
    records.begin(QStringLiteral("make test"), QStringLiteral("/src"), QStringLiteral("host"),
                  QStringLiteral("user"), false);
    records.append(QByteArrayLiteral("done\n"));
    records.finish(0);
    records.begin(QStringLiteral("tool run"), QStringLiteral("/src"), QStringLiteral("host"),
                  QStringLiteral("agent"), false);
    records.append(QByteArrayLiteral("agent output\n"));
    records.finish(0);
    const QJsonObject latest = records.latestUser();
    CHECK(latest.value(QStringLiteral("command")).toString() == QStringLiteral("make test"));
    CHECK(latest.value(QStringLiteral("generation")).toString() == records.generation());
    CHECK(latest.value(QStringLiteral("origin")).toString() == QStringLiteral("user"));
    const QJsonArray all = records.records();  // newest first: dispatch reads oldest first
    CHECK(all.size() == 3);  // the pre-restart record stays, out of this generation's scope
    CHECK(all.at(0).toObject().value(QStringLiteral("origin")).toString() == QStringLiteral("agent"));

    // A command still running at turn start is explicit: output growth already advances
    // the revision, and the exit status stays absent until the result exists.
    const QString id = records.begin(QStringLiteral("watch logs"), QStringLiteral("/var"),
                                     QStringLiteral("host"), QStringLiteral("user"), false);
    records.append(QByteArrayLiteral("line one\n"));
    const QJsonObject running = records.latestUser();
    CHECK(running.value(QStringLiteral("command_id")).toString() == id);
    CHECK(running.value(QStringLiteral("state")).toString() == QStringLiteral("running"));
    CHECK(running.value(QStringLiteral("exit_status")).isNull());
    CHECK(running.value(QStringLiteral("revision")).toInt() >= 2);
    records.finish(0);
    const QJsonObject done = records.latestUser();
    CHECK(done.value(QStringLiteral("state")).toString() == QStringLiteral("completed"));
    CHECK(done.value(QStringLiteral("exit_status")).toInt() == 0);
}

}  // namespace cases
