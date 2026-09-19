// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// What the Activity pane (#QT8C, named by #4X53) took from the terminal while it was open, so the terminal can be
// given it back when the pane closes (card #QT8C).
//
// The owner's decision: "i did mean that the hidden rows should be reprinted on close". While the
// pane is open its owner prints no reasoning row and no tool-call row; when the pane closes,
// everything it took is printed into the terminal at the cursor, settled and collapsed, grouped
// per turn under a muted rule carrying that turn's request. That means something has to remember
// the rows, in order, per turn, for one open period — and that is the whole of this file, kept
// away from the pane so the rules can be proven without a window (tests/internalsledger_test.cpp).
//
// A row here is only what the terminal needs to draw it again: the anchor its OSC 8 link carries
// (so a click unfolds it exactly as a live row does — the reasoning folds answer from the pane's
// own buffer and the call folds through `tool_output_get`) and the two inks of its text. Nothing
// about widgets, colours or bytes.
//
// Pure QtCore: no widget, no theme, no terminal.
#include <QString>
#include <QVector>

namespace relay::internals {

// One row the pane took instead of the terminal printing it.
struct HiddenRow {
    enum class Kind {
        Thinking,   // "✦ thought for 4 s" — its fold answers from the pane's own m_turnThinking
        Call,       // "ran pytest · 212 lines · exit 1 · 8 s", or a merged run's one row
    };
    Kind kind = Kind::Call;
    QString anchor;      // relay://call/… or relay://open-call/…, as the live row would have carried
    QString title;       // the first part, in the tool ink (the error ink when `failed`)
    QString rest;        // " · 212 lines · 8 s", muted; may be empty
    bool failed = false;
};

// One turn's worth of them, in the order they happened.
struct HiddenTurn {
    QString turnId;
    QString request;     // the request's first line, for the muted rule above the block
    QVector<HiddenRow> rows;
};

// The rows of one open period. `take()` hands them over and forgets them, so opening and closing
// the pane again cannot print the same rows twice.
class Ledger {
public:
    // The worker keeps detail for the last 50 turns, so a reprint of an older one would be a row
    // whose fold can no longer be filled. Older turns collapse to one muted line instead.
    static constexpr int kMaxTurns = 50;

    // The request this turn was started with, as the rule above its block should read. The first
    // non-empty line for a turn wins; a turn with no request keeps an empty one and the caller
    // decides what to draw.
    void setRequest(const QString &turnId, const QString &request);

    // A row the pane took. `replaceLast` is the LineCursor's "this rewrites the row on screen",
    // which happens when a run of reads grows ("read 5 files" becomes "read 6 files"): the row
    // that stood for the run is replaced by the one that stands for it now, anchor included. It
    // only ever replaces a call row of this same turn at the end of the ledger; anything else
    // appends, so a rewrite that arrives out of order cannot eat somebody else's row.
    void add(const QString &turnId, const HiddenRow &row, bool replaceLast = false);

    // Everything taken since the last handover, oldest turn first — and forgotten. Turns past
    // kMaxTurns were dropped from the front as they arrived; how many comes back in `droppedTurns`
    // (which this resets), for the muted "… N earlier turns" row the caller draws above the block.
    // A turn that never got a row of its own is left out: a rule with nothing under it is noise.
    QVector<HiddenTurn> take(int *droppedTurns = nullptr);

    // Drop everything without handing it over: the owner is closing, and nothing is reprinted.
    void clear();

    bool isEmpty() const { return m_turns.isEmpty(); }
    int turnCount() const { return int(m_turns.size()); }
    int rowCount() const;
    // Turns that fell off the front of the 50 and are not coming back. take() hands this over and
    // resets it; this reads it without.
    int droppedTurns() const { return m_dropped; }
    // The turns as they stand, without handing them over (for tests and for a caller that wants to
    // know whether the reprint is worth waiting for).
    const QVector<HiddenTurn> &turns() const { return m_turns; }

private:
    HiddenTurn &turnFor(const QString &turnId);
    QVector<HiddenTurn> m_turns;
    int m_dropped = 0;
};

}  // namespace relay::internals
