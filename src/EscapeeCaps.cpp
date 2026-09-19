// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EscapeeCaps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace escapees {

QStringList unitNames() {
    // tmux: `tmux-spawn-<uuid>.scope`; Chrome: `app-com.google.Chrome-<pid>.scope` (card #Y4RX).
    return {QStringLiteral("tmux-spawn-.scope"), QStringLiteral("app-com.google.Chrome-.scope")};
}

QString marker() {
    return QStringLiteral("# relay-managed: cap-escapees");
}

QString dropInDir(const QString &configRoot, const QString &unitName) {
    return configRoot + QStringLiteral("/systemd/user/") + unitName + QStringLiteral(".d");
}

QString dropInPath(const QString &configRoot, const QString &unitName) {
    return dropInDir(configRoot, unitName) + QStringLiteral("/relay.conf");
}

QString dropInText(const Caps &caps) {
    return marker() + QStringLiteral("\n"
           "# Written by Relay: Options > Terminal > \"Cap programs that leave their pane\".\n"
           "# tmux and Chrome move themselves into their own systemd scope under app.slice, outside\n"
           "# any pane's limit. This caps them wherever they are started from - it is machine-wide\n"
           "# for these programs, not per pane. Turning the option off removes this file.\n"
           "[Scope]\n"
           "MemoryMax=") + caps.memoryMax + QStringLiteral("\n"
           "MemorySwapMax=") + caps.swapMax + QStringLiteral("\n");
}

bool ours(const QString &text) {
    return text.contains(marker());
}

//: A file's text, or a null string when it cannot be read.
static QString textOf(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

QStringList install(const QString &configRoot, const Caps &caps, QStringList *skipped) {
    QStringList written;
    const QString text = dropInText(caps);
    for (const QString &unit : unitNames()) {
        const QString path = dropInPath(configRoot, unit);
        if (QFileInfo::exists(path) && !ours(textOf(path))) {
            if (skipped) *skipped << path;             // somebody else's drop-in: never touched
            continue;
        }
        if (!QDir().mkpath(dropInDir(configRoot, unit))) {
            if (skipped) *skipped << path;
            continue;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(text.toUtf8()) < 0 || !file.commit()) {
            if (skipped) *skipped << path;
            continue;
        }
        written << path;
    }
    return written;
}

QStringList removeAll(const QString &configRoot, QStringList *skipped) {
    QStringList removed;
    for (const QString &unit : unitNames()) {
        const QString path = dropInPath(configRoot, unit);
        if (!QFileInfo::exists(path)) continue;
        if (!ours(textOf(path))) {
            if (skipped) *skipped << path;             // not ours to remove
            continue;
        }
        if (!QFile::remove(path)) {
            if (skipped) *skipped << path;
            continue;
        }
        removed << path;
        // Leave the directory only if Relay's file was all that was in it.
        QDir dir(dropInDir(configRoot, unit));
        if (dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) dir.removeRecursively();
    }
    return removed;
}

bool installed(const QString &configRoot) {
    for (const QString &unit : unitNames())
        if (!ours(textOf(dropInPath(configRoot, unit)))) return false;
    return true;
}

QString userConfigRoot() {
    // $XDG_CONFIG_HOME, else ~/.config -- the same place the user manager reads drop-ins from.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return base.isEmpty() ? QDir::homePath() + QStringLiteral("/.config") : base;
}

bool reload() {
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (tool.isEmpty()) return false;
    QProcess reloader;
    reloader.start(tool, {QStringLiteral("--user"), QStringLiteral("daemon-reload")});
    if (!reloader.waitForFinished(5000)) {
        reloader.kill();
        return false;
    }
    return reloader.exitStatus() == QProcess::NormalExit && reloader.exitCode() == 0;
}

}  // namespace escapees
