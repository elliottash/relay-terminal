// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RuntimeDirs.h"
#include "SshConfig.h"
#include "PaneUsage.h"
#include "CrashLog.h"
#include "Logging.h"
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
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
        relay::crashlog::install(app.arguments().last());
        RaiseException(0xE0424242, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 99;
    }
    QTemporaryDir dir;
    QDir(dir.path()).mkpath(QStringLiteral("conf.d/nested"));
    const auto write = [](const QString &path, const QByteArray &text) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
    };
    const QString config = dir.filePath(QStringLiteral("config"));
    const QString included = dir.filePath(QStringLiteral("conf.d/nested/host.conf"));
    if (!check(write(included, "Host native-glob\n")
               && write(config, "Include conf.d/*/*.conf\n"), "SSH fixture write failed")) return 8;
    auto hosts = relay::ssh::parseConfig(config, dir.path(), dir.path());
    if (!check(hosts.size() == 1 && hosts.first().alias == QStringLiteral("native-glob"),
               "nested Windows SSH glob failed")) return 9;
    if (!check(write(config, (QStringLiteral("Include \"") + included + QStringLiteral("\"\n")).toUtf8()),
               "SSH absolute fixture write failed")) return 10;
    hosts = relay::ssh::parseConfig(config, dir.path(), dir.path());
    if (!check(hosts.size() == 1 && hosts.first().alias == QStringLiteral("native-glob"),
               "absolute Windows SSH include failed")) return 11;
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
    const QString marker = QStringLiteral("windows-platform-smoke-") + QUuid::createUuid().toString();
    QProcess crash;
    crash.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--crash"), marker});
    if (!check(crash.waitForFinished(10000) && crash.exitCode() != 0,
               "exception child did not terminate")) return 6;
    QFile log(relay::log::filePath());
    if (!check(log.open(QIODevice::ReadOnly)
               && log.readAll().contains((QStringLiteral("build=") + marker).toUtf8()),
               "native exception was not recorded")) return 7;
    std::puts("Native process ownership, usage and exception logging passed.");
    return 0;
}
