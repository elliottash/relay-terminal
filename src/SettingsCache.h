// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// relay::settings — the one way to read a setting on a hot path (card #057J).
//
// A default-constructed QSettings is not cheap: Qt rebuilds the whole XDG fallback chain and stats
// every candidate file, eight `statx` a construction here. That is invisible in a dialog and
// ruinous in an event filter — the profile of 2026-09-20 measured **417 statx and 170 faccessat on
// every key press and release** (voice/enabled and voice/hold_key, read per key by the app-wide
// filter), 75 statx/s at one idle pane for one boolean (appearance/pane_usage, read by the 400 ms
// status poll), and 9.6 % of the GUI thread's cycles in a tool-heavy turn (agent/show_tool_output
// and logging/level, read per event). All of it for values that change when somebody opens Options.
//
// So a value read per key, per event, per poll or per paint is read through here instead. The
// first read builds a QSettings; every later one is a hash lookup, until something says the
// settings changed. Cold paths — a dialog opening, a pane starting, Options building its rows —
// keep using QSettings directly: they want the file, not a cache, and they cost nothing.
//
// **What drops the cache.** relay::SettingsWatch::notify(), which every control in the Options
// pane already calls after it writes (SettingsPane.cpp: the `after` lambda), and which the agent's
// own option writes and the presets events call too. So a change made in Options takes effect on
// the very next read, exactly as it did when every read went to the file.
//
// **If you write one of these keys from somewhere else**, without going through an Options control,
// call invalidate() after the write — as relay::log::setLevel() does. Writing a setting and not
// saying so is the one way to get a stale answer out of this.
//
// Nothing here watches the settings *file*: Relay has never noticed a second Relay process editing
// relay.conf under it (every reader takes its value once, at startup or at the moment it needs it),
// and this cache does not change that either way.

#include <QString>
#include <QVariant>

namespace relay::settings {

// The value of `key`, read once per generation. The fallback is the one the *first* reader of that
// key passes, so a key read from two places must be given the same fallback in both — which is
// true of every key here, each having exactly one helper that reads it.
QVariant value(const QString &key, const QVariant &fallback = QVariant());
bool boolValue(const QString &key, bool fallback);
int intValue(const QString &key, int fallback);
QString stringValue(const QString &key, const QString &fallback = QString());

// A setting was written: every cached value is read again the next time it is asked for.
void invalidate();

// Bumped by every invalidate(). For a caller whose hot-path value costs more than a QSettings read
// to rebuild and cannot be expressed as one key — Pane::voiceHoldKey() derives its answer from
// voice/hold_key *and* /etc/default/keyboard, and that file must not be opened per key event.
// Keep the generation you computed at and recompute when it moves.
quint64 generation();

// How many QSettings objects this cache has built since the process started. This is what the
// tests count: the promise is a number that stops growing however many times a value is read.
qint64 reads();

}  // namespace relay::settings
