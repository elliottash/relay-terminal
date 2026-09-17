// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerminalBackends.h"

namespace relay {

namespace {
EngineKind s_default = EngineKind::Konsole;
QString s_defaultCore;
} // namespace

QString engineKindName(EngineKind kind)
{
    return kind == EngineKind::Relay ? QStringLiteral("relay") : QStringLiteral("konsole");
}

bool parseEngineKind(const QString &text, EngineKind *out)
{
    const QString name = text.trimmed().toLower();
    if (name == QLatin1String("konsole") || name == QLatin1String("kpart")) {
        if (out)
            *out = EngineKind::Konsole;
        return true;
    }
    if (name == QLatin1String("relay") || name == QLatin1String("vterm") || name == QLatin1String("engine")
        || name == QLatin1String("own")) {
        if (out)
            *out = EngineKind::Relay;
        return true;
    }
    return false;
}

EngineKind resolveEngineKind(const QString &commandLine, const QString &environment, QString *warning)
{
    for (const QString &value : {commandLine, environment}) {
        if (value.trimmed().isEmpty())
            continue;
        EngineKind kind = EngineKind::Konsole;
        if (parseEngineKind(value, &kind)) {
            if (kind == EngineKind::Relay && !engineAvailable()) {
                if (warning)
                    *warning = QStringLiteral("This build has no Relay engine; using KonsolePart.");
                return EngineKind::Konsole;
            }
            return kind;
        }
        if (warning)
            *warning = QStringLiteral("Unknown engine \"%1\"; expected konsole or relay.").arg(value.trimmed());
    }
    return EngineKind::Konsole;
}

QString resolveEngineCore(const QString &commandLine, const QString &environment)
{
    for (const QString &value : {commandLine, environment}) {
        const QString core = value.trimmed().toLower();
        if (core == QLatin1String("ghostty") || core == QLatin1String("libvterm"))
            return core;
    }
    return {};
}

EngineKind defaultEngineKind() { return s_default; }
void setDefaultEngineKind(EngineKind kind) { s_default = kind; }
QString defaultEngineCore() { return s_defaultCore; }
void setDefaultEngineCore(const QString &core) { s_defaultCore = core; }

bool engineAvailable()
{
#ifdef RELAY_HAVE_ENGINE
    return true;
#else
    return false;
#endif
}

} // namespace relay
