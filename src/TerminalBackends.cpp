// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerminalBackends.h"

namespace relay {

namespace {
// Owner decision 2026-09-17: the Relay engine is the default while it is being tested.
// `--engine=konsole` or RELAY_ENGINE=konsole goes back.
EngineKind s_default = EngineKind::Relay;
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
    return engineAvailable() ? EngineKind::Relay : EngineKind::Konsole;
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

QList<TerminalMenuItem> terminalContextMenu(const TerminalMenuState &state)
{
    QList<TerminalMenuItem> items;
    auto add = [&items](const char *id, const QString &label, bool enabled = true) {
        items.append({QString::fromLatin1(id), label, enabled});
    };
    auto separate = [&items] {
        if (!items.isEmpty() && !items.last().isSeparator())
            items.append({QStringLiteral("-"), QString(), true});
    };

    // Relay's own entries come first: they are why this pane is not just a terminal.
    if (state.hasTurn)
        add("turn", QStringLiteral("Open last agent turn"));
    if (state.canTakeControl)
        add("takeControl", QStringLiteral("Take control"));
    add("tasks", QStringLiteral("Tasks"));

    separate();
    // An engine that cannot report a selection leaves Copy enabled: Konsole's copy is a no-op
    // without one, which is the same outcome as a greyed entry, and greying it wrongly would
    // hide a working action.
    add("copy", QStringLiteral("Copy"), state.selectionKnown ? state.hasSelection : true);
    add("paste", QStringLiteral("Paste"));
    add("selectAll", QStringLiteral("Select all"));

    if (!state.link.isEmpty() || !state.filePath.isEmpty()) {
        separate();
        if (!state.link.isEmpty()) {
            add("openLink", QStringLiteral("Open link"));
            add("copyLink", QStringLiteral("Copy link address"));
        }
        if (!state.filePath.isEmpty())
            add("openFile", QStringLiteral("Open “%1”").arg(state.filePath.section(QLatin1Char('/'), -1)));
    }

    separate();
    if (state.canSearch)
        add("find", QStringLiteral("Find…"));
    add("clearScrollback", QStringLiteral("Clear scrollback"));
    if (state.canInject)
        add("reset", QStringLiteral("Clear scrollback and reset"));
    if (state.canReadOutput)
        add("saveOutput", QStringLiteral("Save output as…"));

    if (state.canZoom) {
        separate();
        add("zoomIn", QStringLiteral("Zoom in"));
        add("zoomOut", QStringLiteral("Zoom out"));
        add("zoomReset", QStringLiteral("Reset zoom"));
    }

    separate();
    add("splitRight", QStringLiteral("New pane to the right"));
    add("splitDown", QStringLiteral("New pane below"));
    add("close", QStringLiteral("Close pane"), state.canClosePane);

    while (!items.isEmpty() && items.last().isSeparator())
        items.removeLast();
    return items;
}

} // namespace relay
