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

ClickAction clickActionForModifiers(Qt::KeyboardModifiers modifiers)
{
    if (modifiers & Qt::AltModifier) return ClickAction::AddToPrompt;
    if (modifiers & Qt::ControlModifier) return ClickAction::Navigate;
    if (modifiers & Qt::ShiftModifier) return ClickAction::External;
    return ClickAction::Open;
}

QList<TerminalMenuItem> folderClickMenu(bool canNavigate, bool canPrompt)
{
    return {
        {QStringLiteral("explorer"), QStringLiteral("Open in explorer\tClick"), true},
        {QStringLiteral("navigate"), QStringLiteral("Navigate here\tCtrl+click"), canNavigate},
        {QStringLiteral("prompt"), QStringLiteral("Add to prompt\tAlt+click"), canPrompt},
        {QStringLiteral("external"), QStringLiteral("Open in file manager\tShift+click"), true},
        {QStringLiteral("-"), QString(), true},
        {QStringLiteral("copypath"), QStringLiteral("Copy path"), true},
    };
}

QList<TerminalMenuItem> fileClickMenu(bool canEdit, bool canNavigate, bool canPrompt)
{
    return {
        {QStringLiteral("open"), QStringLiteral("Open\tClick"), true},
        {QStringLiteral("edit"), QStringLiteral("Edit"), canEdit},
        {QStringLiteral("navigate"), QStringLiteral("Navigate to its folder\tCtrl+click"), canNavigate},
        {QStringLiteral("prompt"), QStringLiteral("Add to prompt\tAlt+click"), canPrompt},
        {QStringLiteral("external"), QStringLiteral("Open with the default app\tShift+click"), true},
        {QStringLiteral("-"), QString(), true},
        {QStringLiteral("copypath"), QStringLiteral("Copy path"), true},
    };
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
    // An engine that cannot report a selection leaves Copy enabled: a copy with nothing selected
    // is a no-op, the same outcome as a greyed entry, and greying it wrongly would hide a
    // working action.
    add("copy", QStringLiteral("Copy"), state.selectionKnown ? state.hasSelection : true);
    add("paste", QStringLiteral("Paste"));
    add("selectAll", QStringLiteral("Select all"));

    if (!state.link.isEmpty() || !state.cardId.isEmpty()) {
        separate();
        if (!state.link.isEmpty()) {
            add("openLink", QStringLiteral("Open link"));
            add("copyLink", QStringLiteral("Copy link address"));
        }
        // A local path under the pointer no longer adds entries here: a right-click on one opens
        // that path's own menu instead (#KKYC) — folderClickMenu() or fileClickMenu().
        // A `#K7Q2` reference: the card it names, the reference itself, and the reference in the
        // prompt box — the same three things the Switchboard's own card detail offers (`t`, `y`).
        if (!state.cardId.isEmpty()) {
            const QString reference = QStringLiteral("#") + state.cardId;
            add("openCard", state.cardTitle.isEmpty()
                    ? QStringLiteral("Open %1").arg(reference)
                    : QStringLiteral("Open %1 “%2”").arg(reference, state.cardTitle));
            add("copyCard", QStringLiteral("Copy %1").arg(reference));
            add("cardToPrompt", QStringLiteral("Add to prompt\tAlt+click"));
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
    // That is where a split from a remote pane lands by default now (#XQ8F), so this one names the
    // host and the one below names the machine.
    if (!state.remoteHost.isEmpty()) {
        add("splitSameHost", QStringLiteral("New pane on %1").arg(state.remoteHost));
        add("splitLocal", QStringLiteral("New local pane"));
    }
    // pane.equalize: every splitter in the tab back to equal shares; greyed while this pane is
    // alone in its tab, because equalizing then has nothing to share the space with.
    add("equalize", QStringLiteral("Equalize pane sizes"), state.canEqualize);
    add("close", QStringLiteral("Close pane"), state.canClosePane);
    // Closing a pane on a host leaves its session running there (#XQ8F); this ends it too.
    if (!state.remoteHost.isEmpty())
        add("closeEndRemote", QStringLiteral("Close and end the remote session"), state.canClosePane);
    else if (!state.localSession.isEmpty())
        // The local twin (#87HB): closing leaves the holder session running by design, this ends it.
        add("closeEndRemote", QStringLiteral("Close and end this pane's session"), state.canClosePane);

    while (!items.isEmpty() && items.last().isSeparator())
        items.removeLast();
    return items;
}

} // namespace relay
