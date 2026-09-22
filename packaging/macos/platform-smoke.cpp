// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RuntimeDirs.h"
#include "SshConfig.h"
#include "PaneUsage.h"
#include "CrashLog.h"
#include "Logging.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <QElapsedTimer>
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
        const rlimit noCore{0, 0};
        setrlimit(RLIMIT_CORE, &noCore);
        relay::log::setLevel(QStringLiteral("info"));
        relay::crashlog::install(app.arguments().last());
        raise(SIGABRT);
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
               "nested macOS SSH glob failed")) return 9;
    if (!check(write(config, (QStringLiteral("Include \"") + included + QStringLiteral("\"\n")).toUtf8()),
               "SSH absolute fixture write failed")) return 10;
    hosts = relay::ssh::parseConfig(config, dir.path(), dir.path());
    if (!check(hosts.size() == 1 && hosts.first().alias == QStringLiteral("native-glob"),
               "absolute macOS SSH include failed")) return 11;
    const auto owner = relay::runtimedirs::self();
    if (!check(dir.isValid() && owner.pid == getpid() && owner.startTime > 0,
               "native process identity unavailable")) return 1;
    if (!check(relay::runtimedirs::markOwned(dir.path())
               && relay::runtimedirs::ownerAlive(dir.path()), "runtime owner roundtrip failed")) return 2;
    auto wrong = owner;
    ++wrong.startTime;
    if (!check(!relay::runtimedirs::ownerAlive(wrong), "recycled process identity accepted")) return 3;
    const auto rows = relay::usage::walkTrees({qint64(getpid())});
    if (!check(!rows.isEmpty() && rows.first().pid == getpid()
               && rows.first().startTicks > 0 && rows.first().rssBytes > 0,
               "native process counters unavailable")) return 4;
    if (!check(relay::usage::processorCount() > 0 && relay::usage::totalMemoryBytes() > 0
               && relay::usage::clockTicksPerSecond() == 1000000000, "machine counters unavailable")) return 5;
    relay::runtimedirs::DirStamp stamp;
    if (!check(stamp.changed(dir.path()) && !stamp.changed(dir.path()),
               "native directory stamp failed")) return 16;
    QTemporaryDir orphan(dir.filePath(QStringLiteral("relay-XXXXXX")));
    if (!check(orphan.isValid()
               && write(orphan.filePath(relay::runtimedirs::ownerFileName()),
                        "relay-owner 1\npid 2147483647\nstarttime 1\n"),
               "orphan fixture failed")) return 17;
    const QString orphanPath = orphan.path();
    orphan.setAutoRemove(false);
    relay::runtimedirs::sweep(dir.path(), 0);
    if (!check(!QDir(orphanPath).exists(), "dead runtime directory not cleaned")) return 18;
    const auto args = relay::ssh::processArgv(int(getpid()));
    if (!check(!args.isEmpty() && QFileInfo(args.first()).fileName()
               == QFileInfo(QCoreApplication::applicationFilePath()).fileName(),
               "native argv unavailable")) return 12;
    QElapsedTimer timer;
    timer.start();
    volatile quint64 work = 0;
    while (timer.elapsed() < 50) ++work;
    const auto later = relay::usage::walkTrees({qint64(getpid())});
    if (!check(!later.isEmpty() && later.first().ticks > rows.first().ticks,
               "native CPU counter did not advance")) return 13;
    QProcess child;
    child.start(QStringLiteral("/bin/sleep"), {QStringLiteral("10")});
    if (!check(child.waitForStarted(), "process tree child failed to start")) return 14;
    const auto tree = relay::usage::walkTrees({qint64(getpid())});
    bool found = false;
    for (const auto &row : tree) if (row.pid == child.processId()) found = true;
    child.kill();
    child.waitForFinished();
    if (!check(found, "native child process missing from tree")) return 15;
    const QString marker = QStringLiteral("macos-platform-smoke-") + QUuid::createUuid().toString();
    QProcess crash;
    crash.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--crash"), marker});
    if (!check(crash.waitForFinished(10000) && crash.exitCode() != 0,
               "signal child did not terminate")) return 6;
    QFile log(relay::log::filePath());
    if (!check(log.open(QIODevice::ReadOnly)
               && log.readAll().contains((QStringLiteral("build=") + marker).toUtf8()),
               "native signal was not recorded")) return 7;
    std::puts("Native process ownership, usage and signal logging passed.");
    return 0;
}
