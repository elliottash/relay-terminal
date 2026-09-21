// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RuntimeDirs.h"
#include "PaneUsage.h"
#include "CrashLog.h"
#include "Logging.h"
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <windows.h>
#include <cstdio>

static bool check(bool condition, const char *message) {
    if (!condition) std::fprintf(stderr, "%s\n", message);
    return condition;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("RelaySmoke"));
    QCoreApplication::setApplicationName(QStringLiteral("NativePlatform"));
    QStandardPaths::setTestModeEnabled(true);
    if (app.arguments().contains(QStringLiteral("--crash"))) {
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
        relay::log::setLevel(QStringLiteral("info"));
        relay::crashlog::install(QStringLiteral("windows-platform-smoke"));
        RaiseException(0xE0424242, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 99;
    }
    QTemporaryDir dir;
    const auto owner = relay::runtimedirs::self();
    if (!check(dir.isValid() && owner.pid == GetCurrentProcessId() && owner.startTime > 0,
               "native process identity unavailable")) return 1;
    if (!check(relay::runtimedirs::markOwned(dir.path())
               && relay::runtimedirs::ownerAlive(dir.path()), "runtime owner roundtrip failed")) return 2;
    auto wrong = owner;
    ++wrong.startTime;
    if (!check(!relay::runtimedirs::ownerAlive(wrong), "recycled process identity accepted")) return 3;
    const auto rows = relay::usage::walkTrees({qint64(GetCurrentProcessId())});
    if (!check(!rows.isEmpty() && rows.first().pid == GetCurrentProcessId()
               && rows.first().startTicks > 0 && rows.first().rssBytes > 0,
               "native process counters unavailable")) return 4;
    if (!check(relay::usage::processorCount() > 0 && relay::usage::totalMemoryBytes() > 0
               && relay::usage::clockTicksPerSecond() == 10000000, "machine counters unavailable")) return 5;
    QProcess crash;
    crash.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--crash")});
    if (!check(crash.waitForFinished(10000) && crash.exitCode() != 0,
               "exception child did not terminate")) return 6;
    QFile log(relay::log::filePath());
    if (!check(log.open(QIODevice::ReadOnly)
               && log.readAll().contains("build=windows-platform-smoke"),
               "native exception was not recorded")) return 7;
    std::puts("Native process ownership, usage and exception logging passed.");
    return 0;
}
