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
// thing it stands in for rather than like a log dump: the user's prompt behind its ✦, the reply
// as prose, one ▸ row per tool call. Tool output is left out — it is the bulk of a transcript and
// the least of it, and a resumed pane is not a place to re-read it.
//
// Pure: QtCore only, no pane, no terminal, no colours (the caller maps `Row::kind` onto its own
// inks), so tests/transcriptreplay_test.cpp can check what would be printed without a window.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace relay::transcriptreplay {

// What a row is, so the caller can ink it like the live line it stands for.
enum class Line {
    Prompt,   // what the user sent to the agent (a live turn's ✦ line)
    Reply,    // the agent's prose
    Call,     // one tool call, the ▸ row
    Note,     // Relay's own chrome: the turn separator
};

struct Row {
    Line kind = Line::Note;
    QString text;
};

// A tool-call row is one line however long its arguments were: a wrapped ▸ row would look like a
// fold that is not there.
inline constexpr int kCallWidth = 100;
// Entries whose kind is not one of these are not messages (tool and command output, and anything a
// later index adds), so they are not drawn.
inline bool isDrawn(const QString &kind) {
    return kind == QLatin1String("prompt") || kind == QLatin1String("reply")
           || kind == QLatin1String("tool_call") || kind == QLatin1String("command");
}

inline QString oneLine(const QString &text, int width) {
    QString flat = text.simplified();
    if (flat.size() <= width) return flat;
    return flat.left(width - 1) + QChar(0x2026);   // …
}

// The rows for one `conversation_get` answer, oldest first. `maxRows` is the same budget the saved
// text is clamped to, and the newest rows are the ones kept: a resumed pane should end where the
// conversation ended, exactly as the saved text does.
inline QVector<Row> render(const QJsonArray &items, int maxRows) {
    QVector<Row> rows;
    int lastTurn = -1;
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString kind = item.value(QStringLiteral("kind")).toString();
        if (!isDrawn(kind)) continue;
        const QString text = item.value(QStringLiteral("text")).toString();
        if (text.trimmed().isEmpty()) continue;
        const int turn = item.value(QStringLiteral("turn")).toInt();
        // One blank line between turns, the gap a live conversation leaves; none before the first.
        if (lastTurn >= 0 && turn != lastTurn) rows.append({Line::Note, QString()});
        lastTurn = turn;
        if (kind == QLatin1String("tool_call")) {
            rows.append({Line::Call, QStringLiteral("▸ ") + oneLine(text, kCallWidth)});
            continue;
        }
        const bool prompt = kind != QLatin1String("reply");
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            // The ✦ marks the prompt, once, the way the live line does; its later lines are the
            // same ink but carry no second marker.
            rows.append({prompt ? Line::Prompt : Line::Reply,
                         prompt && i == 0 ? QStringLiteral("✦ ") + lines.at(i) : lines.at(i)});
        }
    }
    // Trailing and leading blanks are the separator, not content.
    while (!rows.isEmpty() && rows.constLast().text.isEmpty()) rows.removeLast();
    if (maxRows > 0 && rows.size() > maxRows) rows = rows.mid(rows.size() - maxRows);
    while (!rows.isEmpty() && rows.constFirst().text.isEmpty()) rows.removeFirst();
    return rows;
}

}  // namespace relay::transcriptreplay
