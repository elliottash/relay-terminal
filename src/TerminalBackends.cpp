// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TerminalBackends.h"

namespace relay {

namespace {
QString s_defaultCore;
} // namespace

QString resolveEngineCore(const QString &commandLine, const QString &environment)
{
    for (const QString &value : {commandLine, environment}) {
        const QString core = value.trimmed().toLower();
        if (core == QLatin1String("ghostty") || core == QLatin1String("libvterm"))
            return core;
    }
    return {};
}

QString defaultEngineCore() { return s_defaultCore; }
void setDefaultEngineCore(const QString &core) { s_defaultCore = core; }

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
    // An engine that cannot report a selection leaves Copy enabled: a copy with nothing selected
    // is a no-op, the same outcome as a greyed entry, and greying it wrongly would hide a
    // working action.
    add("copy", QStringLiteral("Copy"), state.selectionKnown ? state.hasSelection : true);
    add("paste", QStringLiteral("Paste"));
    add("selectAll", QStringLiteral("Select all"));

    if (!state.link.isEmpty() || !state.filePath.isEmpty() || !state.cardId.isEmpty()) {
        separate();
        if (!state.link.isEmpty()) {
            add("openLink", QStringLiteral("Open link"));
            add("copyLink", QStringLiteral("Copy link address"));
        }
        if (!state.filePath.isEmpty())
            add("openFile", QStringLiteral("Open “%1”").arg(state.filePath.section(QLatin1Char('/'), -1)));
        // A `#K7Q2` reference: the card it names, the reference itself, and the reference in the
        // prompt box — the same three things the Switchboard's own card detail offers (`t`, `y`).
        if (!state.cardId.isEmpty()) {
            const QString reference = QStringLiteral("#") + state.cardId;
            add("openCard", state.cardTitle.isEmpty()
                    ? QStringLiteral("Open %1").arg(reference)
                    : QStringLiteral("Open %1 “%2”").arg(reference, state.cardTitle));
            add("copyCard", QStringLiteral("Copy %1").arg(reference));
            add("cardToPrompt", QStringLiteral("%1 → prompt").arg(reference));
        }
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
    // The same ssh command line again, beside this one; with connection sharing, no second login.
    if (!state.remoteHost.isEmpty())
        add("splitSameHost", QStringLiteral("New pane on %1").arg(state.remoteHost));
    // pane.equalize: every splitter in the tab back to equal shares; greyed while this pane is
    // alone in its tab, because equalizing then has nothing to share the space with.
    add("equalize", QStringLiteral("Equalize pane sizes"), state.canEqualize);
    add("close", QStringLiteral("Close pane"), state.canClosePane);

    while (!items.isEmpty() && items.last().isSeparator())
        items.removeLast();
    return items;
}

} // namespace relay
