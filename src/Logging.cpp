// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Logging.h"

#include "CrashLog.h"   // the crash report's copy in this file follows the level set here

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QtGlobal>

namespace relay::log {
namespace {

constexpr qint64 MaxBytes = 5 * 1024 * 1024;
constexpr int Backups = 3;

QMutex &mutex() {
    static QMutex instance;
    return instance;
}

int rank(Level level) {
    switch (level) {
    case Level::Off: return 0;
    case Level::Error: return 1;
    case Level::Info: return 2;
    case Level::Debug: return 3;
    case Level::Verbose: return 4;
    }
    return 2;
}

Level parse(const QString &name) {
    const QString value = name.trimmed().toLower();
    if (value == QStringLiteral("off")) return Level::Off;
    if (value == QStringLiteral("error")) return Level::Error;
    if (value == QStringLiteral("debug")) return Level::Debug;
    if (value == QStringLiteral("verbose")) return Level::Verbose;
    return Level::Info;
}

// Rotate relay.log -> relay.log.1 -> … -> relay.log.3, dropping the oldest. Called with the lock held.
void rotate(const QString &path) {
    QFile::remove(path + QStringLiteral(".%1").arg(Backups));
    for (int index = Backups - 1; index >= 1; --index) {
        const QString from = path + QStringLiteral(".%1").arg(index);
        if (QFile::exists(from)) QFile::rename(from, path + QStringLiteral(".%1").arg(index + 1));
    }
    QFile::rename(path, path + QStringLiteral(".1"));
}

} // namespace

QString directory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) return QString();
    const QString path = base + QStringLiteral("/relay/logs");
    QDir dir;
    if (!dir.exists(path) && !dir.mkpath(path)) return QString();
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

QString filePath() {
    const QString dir = directory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/relay.log");
}

Level level() {
    return parse(QSettings().value(QStringLiteral("logging/level"), QStringLiteral("info")).toString());
}

void setLevel(const QString &name) {
    QSettings().setValue(QStringLiteral("logging/level"), levelName(parse(name)));
}

QString levelName(Level value) {
    switch (value) {
    case Level::Off: return QStringLiteral("off");
    case Level::Error: return QStringLiteral("error");
    case Level::Info: return QStringLiteral("info");
    case Level::Debug: return QStringLiteral("debug");
    case Level::Verbose: return QStringLiteral("verbose");
    }
    return QStringLiteral("info");
}

QList<QStringList> levelChoices() {
    return {{QStringLiteral("off"), QStringLiteral("Off"), QStringLiteral("Write no log file at all")},
            {QStringLiteral("error"), QStringLiteral("Errors only"), QStringLiteral("Failures and stalls")},
            {QStringLiteral("info"), QStringLiteral("Normal"), QStringLiteral("Turns, tools, errors · the default")},
            {QStringLiteral("debug"), QStringLiteral("Detailed"), QStringLiteral("Every protocol event type as well")},
            {QStringLiteral("verbose"), QStringLiteral("Verbose (includes prompt text)"),
             QStringLiteral("Your prompts are written to the log file · off by default")}};
}

QString scrub(const QString &text) {
    static const QRegularExpression keys(QStringLiteral("\\bsk-[A-Za-z0-9_\\-]{12,}|\\bgsk_[A-Za-z0-9]{12,}"
                                                        "|\\bxai-[A-Za-z0-9]{12,}"));
    static const QRegularExpression bearer(QStringLiteral("\\bbearer\\s+[A-Za-z0-9._\\-]{8,}"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression named(QStringLiteral("\\b(api[_-]?key|authorization|token|secret|password)"
                                                          "\\s*[=:]\\s*\\S+"),
                                          QRegularExpression::CaseInsensitiveOption);
    QString out = text;
    out.replace(keys, QStringLiteral("<redacted>"));
    out.replace(bearer, QStringLiteral("<redacted>"));
    out.replace(named, QStringLiteral("\\1=<redacted>"));
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    out.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return out;
}

void write(Level messageLevel, const QString &message) {
    if (messageLevel == Level::Off) return;
    const Level configured = level();
    // Cheap enough to do on every line, and it is the only moment the crash handler can learn that
    // the level changed: it may not read QSettings from a signal.
    relay::crashlog::noteLogEnabled(configured != Level::Off);
    if (configured == Level::Off || rank(messageLevel) > rank(configured)) return;
    const QString path = filePath();
    if (path.isEmpty()) return;
    const QString line = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"))
                         + QLatin1Char(' ') + levelName(messageLevel).toUpper()
                         + QStringLiteral(" relay.gui ") + scrub(message) + QLatin1Char('\n');
    QMutexLocker locker(&mutex());
    if (QFileInfo(path).size() >= MaxBytes) rotate(path);
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) return;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.write(line.toUtf8());
    file.close();
}

void installMessageHandler() {
    static QtMessageHandler previous = nullptr;
    previous = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &text) {
        const Level mapped = type == QtDebugMsg ? Level::Debug
                           : type == QtInfoMsg ? Level::Info : Level::Error;
        write(mapped, QStringLiteral("qt category=%1 msg=\"%2\"")
                          .arg(QString::fromUtf8(context.category ? context.category : "default"), text));
        if (previous) previous(type, context, text);
    });
}

} // namespace relay::log
