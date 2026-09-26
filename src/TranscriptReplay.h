// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// relay::transcriptreplay: a saved conversation drawn the way its turns were printed (card #0TJ9).
//
// Opening a conversation from the sessions manager replays the terminal text saved beside it
// (`relay::sessiontext`, src/WindowState.h). Three kinds of conversation have no such file and
// never will: every one saved before that store existed, one whose Relay was killed before it
// could write, and one made from the phone, which has no terminal at all. What those still have
// is the transcript the worker keeps — `conversation_get` → `items: [{turn, kind, time, text,
// exit_status?}]` (protocol 14.4) — in one shape for an agent conversation and for a claude or
// codex guest alike, because conv_index.py indexes all three into the same table.
//
// This turns those entries into the lines a live turn prints, so the fallback reads like the
// thing it stands in for rather than like a log dump: the user's prompt, the reply
// as prose, one ▸ row per tool call, and the call's output behind it. Output is *not* left out:
// the owner's rule for a restored conversation is that it comes back the way it went in (card
// #HEY7), and a live turn folds its tool output under the ▸ row, so each output block is its own
// row, whole — the caller folds it under the ▸ row above it, collapsed, and nothing is cut here.
//
// Pure: QtCore only, no pane, no terminal, no colours (the caller maps `Row::kind` onto its own
// inks), so tests/transcriptreplay_test.cpp can check what would be printed without a window.

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

namespace relay::transcriptreplay {

// What a row is, so the caller can ink it like the live line it stands for.
enum class Line {
    Prompt,   // what the user sent to the agent (a live turn's first line)
    Reply,    // the agent's prose
    Call,     // one tool call, the ▸ row
    Output,   // one call's whole output block, folded under the ▸ row above it when drawn
    Note,     // Relay's own chrome: the turn separator
};

struct Row {
    Line kind = Line::Note;
    QString text;
    int turn = 0;   // the transcript turn the row belongs to (0 for the separator before none)
};

// A tool-call row is one line however long its arguments were: a wrapped ▸ row would look like a
// fold that is not there.
inline constexpr int kCallWidth = 100;
// Entries whose kind is not one of these are not conversation body — the sidecars (title,
// summary, terminal_text, rewound) and anything a later index adds — so they are not drawn.
// Tool and command output *is* drawn: as Output rows the caller folds, not as prose.
inline bool isDrawn(const QString &kind) {
    return kind == QLatin1String("prompt") || kind == QLatin1String("reply")
           || kind == QLatin1String("tool_call") || kind == QLatin1String("command")
           || kind == QLatin1String("tool_output") || kind == QLatin1String("command_output");
}

inline QString oneLine(const QString &text, int width) {
    QString flat = text.simplified();
    if (flat.size() <= width) return flat;
    return flat.left(width - 1) + QChar(0x2026);   // …
}

// The rows for one `conversation_get` answer, oldest first. `maxRows` is the same budget the saved
// text is clamped to, and the newest rows are the ones kept: a resumed pane should end where the
// conversation ended, exactly as the saved text does. `beforeTurn` >= 0 drops every item of that
// turn and later — the fill a truncated saved text needs is the turns *above* its window (#KDB4).
inline QVector<Row> render(const QJsonArray &items, int maxRows, int beforeTurn = -1) {
    QVector<Row> rows;
    int lastTurn = -1;
    QVector<int> pendingCalls;   // rows-positions of the turn's ▸ rows still waiting for output
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString kind = item.value(QStringLiteral("kind")).toString();
        if (!isDrawn(kind)) continue;
        const QString text = item.value(QStringLiteral("text")).toString();
        if (text.trimmed().isEmpty()) continue;
        const int turn = item.value(QStringLiteral("turn")).toInt();
        if (beforeTurn >= 0 && turn >= beforeTurn) continue;
        // One blank line between turns, the gap a live conversation leaves; none before the first.
        if (lastTurn >= 0 && turn != lastTurn) rows.append({Line::Note, QString(), turn});
        if (turn != lastTurn) pendingCalls.clear();   // output pairs with its own turn's call run
        lastTurn = turn;
        if (kind == QLatin1String("tool_call")) {
            rows.append({Line::Call, QStringLiteral("▸ ") + oneLine(text, kCallWidth), turn});
            pendingCalls.append(rows.size() - 1);
            continue;
        }
        if (kind == QLatin1String("tool_output") || kind == QLatin1String("command_output")) {
            // One row holds the whole block, lines and all: the caller folds it under the ▸ row
            // above, the way the live turn folds it, so it must not be flattened or cut here.
            // The index stores a turn's call run ahead of its output run (callA, callB, outA,
            // outB), so the next output belongs to the oldest ▸ row still without one. A block
            // with no ▸ row in reach — a terminal conversation's command_output, or a call the
            // budget cut away — goes after everything so far, for the caller to print plain:
            // it is output, not chrome.
            if (!pendingCalls.isEmpty()) {
                const int at = pendingCalls.takeFirst() + 1;
                rows.insert(at, {Line::Output, text, turn});
                for (int &pos : pendingCalls) ++pos;   // the insert shifted the rest of the run
            } else {
                rows.append({Line::Output, text, turn});
            }
            continue;
        }
        pendingCalls.clear();   // a prompt or a reply ends the call run its outputs pair with
        const bool prompt = kind != QLatin1String("reply");
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines)
            rows.append({prompt ? Line::Prompt : Line::Reply, line, turn});
    }
    // Trailing and leading blanks are the separator, not content.
    while (!rows.isEmpty() && rows.constLast().text.isEmpty()) rows.removeLast();
    if (maxRows > 0 && rows.size() > maxRows) rows = rows.mid(rows.size() - maxRows);
    while (!rows.isEmpty() && rows.constFirst().text.isEmpty()) rows.removeFirst();
    return rows;
}

// The saved text of a conversation is a window of its newest lines, not the conversation (the same
// clamp every scrollback has), so a conversation longer than the window restores cut off (#KDB4).
// This says where that window begins in the transcript's turns, so the fill can print the turns
// above it. -1 when the window's relationship to the transcript is not known: the caller prints
// the whole transcript so a recap-only saved file cannot hide the conversation.
//
// The precise signal is the first prompt row the saved text still holds: a turn's prompt, matched
// against the transcript's prompt items the way relay::sessiontext::turnStart matches one for a
// rewind — the row is cut at the pane's width, so the prompt's first line has to start with the
// row, not the other way round. Windows saved while prompts still carried the "✦ " glyph match
// the same way with the glyph stripped first. Everything the file holds is that turn and later.
// The same prompt
// text can open more than one turn ("Continue" opened two of the turns in the conversation this
// was measured on); the *last* such turn is the one whose boundary the row is, because the first
// would answer "turn A" for turn B's identical row and drop every turn between them from the fill
// for good, where the last can only duplicate one.
//
// A window deep enough to lose even its turn's prompt row — measured: the window opened at a
// turn's recap, its `Continue` prompt six hundred lines further up — has no prompt row to match.
// Replies
// carry it then: the pane prints an agent reply as wrapped prose rows, and a reply's opening,
// with every space and wrap taken out, is a substring of the window read the same way — wrapping
// breaks lines at spaces and mid-word both, and removing the whitespace of each side joins the
// halves back however they were cut. A reply cannot name its turn, only date the window, so the
// answer is the *newest* turn whose reply is found: the window is a contiguous tail, every turn
// after the one it opens inside is wholly in it, and the newest found is at or after the true
// boundary — the fill over-covers a turn the window already holds, a cosmetic repeat, and never
// drops one.
inline int coveredFrom(const QStringList &savedLines, const QJsonArray &items) {
    struct Anchor { QString text; int turn; bool prompt; };
    QVector<Anchor> anchors;   // prompts and replies, oldest first: the items arrive that way
    static const QString marker = QStringLiteral("✦ ");
    QStringList plain;         // the window, stripped of its ink once
    QString joined;            // …and of its whitespace, for the wrap-proof containment
    for (const QString &saved : savedLines) {
        QString line = saved;
        static const QRegularExpression osc8(
            QStringLiteral("\\x1b\\]8;[^\\x1b\\x07]*(?:\\x07|\\x1b\\\\)"));
        line.remove(osc8);
        static const QRegularExpression sgr(QStringLiteral("\\x1b\\[[0-9;:]*m"));
        line.remove(sgr);
        plain.append(line);
        for (const QChar &c : line)
            if (!c.isSpace()) joined.append(c);
    }
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString kind = item.value(QStringLiteral("kind")).toString();
        if (kind != QLatin1String("prompt") && kind != QLatin1String("reply")) continue;
        const QString first = item.value(QStringLiteral("text")).toString()
                                  .section(QLatin1Char('\n'), 0, 0).trimmed();
        if (first.isEmpty()) continue;
        anchors.append({first, item.value(QStringLiteral("turn")).toInt(),
                        kind == QLatin1String("prompt")});
    }
    if (anchors.isEmpty() || joined.isEmpty()) return -1;
    // Precise first: the topmost prompt row — a line that is a prompt's first line, cut at the
    // pane's width. Windows saved while prompts still carried the "✦ " glyph match the same way
    // with the glyph stripped first; the *newest* matching anchor wins, so two turns beginning
    // with the same line ("continue" twice) mean the later one and the window over-covers by
    // exactly the turn it answered, which is what the anchor is for.
    for (const QString &raw : plain) {
        QString line = raw;
        const bool marked = line.startsWith(marker);
        if (marked) line = line.mid(marker.size());
        const QString shown = line.trimmed();
        if (shown.isEmpty()) continue;
        int matched = -1;
        for (const Anchor &anchor : anchors) {
            if (!anchor.prompt || !anchor.text.startsWith(shown)) continue;
            // A marked row is a prompt by its glyph. An unmarked one only starts like the
            // prompt, so a short one could be a line of the agent's own prose ("ok",
            // "Continue"): take it when it is the whole first line or long enough that the
            // pane's width cut it.
            if (!marked && shown.size() < anchor.text.size() && shown.size() < 20) continue;
            matched = anchor.turn;
        }
        if (matched >= 0) return matched;
    }
    // No prompt row survived: the newest turn a reply dates.
    int newest = -1;
    for (const Anchor &anchor : anchors) {
        if (anchor.prompt) continue;
        QString squeezed;   // the reply's opening, wrapped the way the window would wrap it
        for (const QChar &c : anchor.text)
            if (!c.isSpace()) squeezed.append(c);
        if (squeezed.size() < 24) continue;   // too short to be distinctive
        if (joined.contains(squeezed)) newest = anchor.turn;
    }
    return newest;
}

}  // namespace relay::transcriptreplay
