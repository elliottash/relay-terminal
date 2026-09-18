// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Shortcut hints, Superhuman-style: when the user takes the slow path (mouse, palette, long
// prefix) and a faster keyboard path exists, show a short hint. Each hint has a show limit and a
// cooldown; counts persist in QSettings under hints/. A global setting turns hints off.
//
// Adding a feature with a shortcut? Add a hint id and call Pane::hint()/RelayWindow::hint() from
// its slow path. See docs/ARCHITECTURE.md "Shortcut hints" and WARP.md.
#include <QString>
#include <QStringList>

namespace relay {

class ShortcutHints {
public:
    static ShortcutHints &instance();

    bool enabled() const;
    void setEnabled(bool on);

    // True when the hint `id` may be shown now. `limit` is the total number of times this hint is
    // ever shown; `cooldownSeconds` the minimum gap between two showings of it. A global gap keeps
    // hints from stacking up. Records nothing: a hint counts as shown only when recordShown() says
    // it reached the screen, so one that waits behind other toasts, or never appears, costs nothing.
    bool mayShow(const QString &id, int limit = 3, int cooldownSeconds = 600) const;
    // The hint `id` is on screen now: count it, and start its cooldown and the global gap.
    void recordShown(const QString &id);
    // mayShow() and, when it passes, recordShown() at once: for a hint drawn the moment it is
    // allowed (the placement prompt's "Next time" line). Toast hints go through Pane::hint(),
    // which records when the queued toast actually appears.
    bool shouldShow(const QString &id, int limit = 3, int cooldownSeconds = 600);

    // How many times `id` has been shown.
    int shownCount(const QString &id) const;
    void resetAll();

    // "Next time: Ctrl+T" or "Next time: Ctrl+T · new tab"; empty when the shortcut is unbound.
    static QString nextTime(const QString &shortcut, const QString &what = QString());

    // Idle tips rotated after a finished agent turn when the prompt box sits empty. Picks the next
    // tip that mayShow() (limit 3, cooldown kIdleTipCooldownSeconds) and advances the rotation;
    // the caller calls recordShown(tip.id) once the tip is on screen.
    struct Tip { QString id, text; };
    Tip nextIdleTip(const QList<Tip> &tips);

    static constexpr int kGlobalGapSeconds = 20;
    static constexpr int kIdleTipLimit = 3;
    static constexpr int kIdleTipCooldownSeconds = 1800;
};

}  // namespace relay
