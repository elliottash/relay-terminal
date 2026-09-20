// SPDX-License-Identifier: AGPL-3.0-or-later
// One place that turns a relay::ViewportFrame into the JSON a phone renders.
//
// Two callers need it and they must not drift: relay-screen-bridge (a headless PTY, used by
// `remote.cli share`) and src/RemoteShare.cpp (the GUI sharing one of its own panes). The wire
// shape is docs/REMOTE-PROTOCOL.md section 6.5 — rows of style runs, so the client needs no index
// arithmetic and no second emulator.
#pragma once

#include "core/CellTypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace relay::screenjson {

inline QJsonObject cursorOf(const ViewportFrame &frame)
{
    QJsonObject cursor;
    cursor["row"] = frame.cursor.row;
    cursor["col"] = frame.cursor.col;
    cursor["visible"] = frame.cursor.visible && frame.cursorInViewport;
    cursor["shape"] = int(frame.cursor.shape);
    return cursor;
}

// One row as runs of identical style. A wide glyph's tail cell contributes nothing: the glyph
// before it already occupies two columns in a monospace grid.
inline QJsonObject rowOf(const Line &line, int row)
{
    QJsonArray segments;
    QString text;
    uint32_t fg = 0, bg = 0;
    uint16_t attrs = 0;
    bool open = false;

    const auto flush = [&]() {
        if (!open || text.isEmpty()) return;
        QJsonArray segment;
        segment.append(text);
        segment.append(double(fg));
        segment.append(double(bg));
        segment.append(int(attrs));
        segments.append(segment);
        text.clear();
    };

    for (const Cell &cell : line.cells) {
        if (cell.ch == kWideTail) continue;
        const uint16_t style = uint16_t(cell.attrs & ~AttrCluster);
        if (!open || cell.fg != fg || cell.bg != bg || style != attrs) {
            flush();
            fg = cell.fg;
            bg = cell.bg;
            attrs = style;
            open = true;
        }
        text += line.cellText(cell);
    }
    flush();

    QJsonObject object;
    object["row"] = row;
    object["segs"] = segments;
    if (line.marks) object["marks"] = int(line.marks);
    return object;
}

// A whole frame. `full` sends every row and the geometry; otherwise only the dirty rows.
//
// Every frame carries `base`, the absolute scrollback row of `lines[0]`, and `history`, how many
// scrollback rows exist. Without them a client holding a page of scrollback cannot tell where its
// rows stop and the live block begins: output pushes lines off the screen into the scrollback, the
// live block starts further down, and the rows in between belong to neither — a hole in the middle
// of the column (docs/REMOTE-PROTOCOL.md section 6.5).
// A scroll goes out as the shift itself plus the rows it could not carry over: `scroll` is
// `{top, bottom, by}` — rows `[top, bottom)` of the frame before this one moved up by `by`, a
// negative `by` moving them down — and `lines` holds only the rows that really changed. A
// streamed reply that was 166 snapshots of 7,747 B becomes 166 diffs of a row or two (#3H5T).
//
// `full` — a caller asking for everything, a resize, a clear, a colour change — always wins: a
// scroll is only ever an alternative to a repaint nobody needed. A client that does not
// understand `scroll` must never be sent one; that is negotiated once, in `hello`
// (docs/REMOTE-PROTOCOL.md section 6.5), and the hub sends such a client snapshots instead.
inline QJsonObject frameOf(const ViewportFrame &frame, bool full)
{
    QJsonArray lines;
    // Only worth describing when the shift carried rows over that would otherwise be resent. A
    // scroll that turns the whole screen over — 50 MB of `cat` does one of those per frame — has
    // nothing to carry, and saying so costs the 35 bytes of the `scroll` object for nothing.
    int dirtyRows = 0;
    for (uint8_t bit : frame.dirty) dirtyRows += bit ? 1 : 0;
    const bool scrolled = !full && frame.scrolledBy != 0 && dirtyRows < int(frame.lines.size());
    const bool everything = full || (frame.full && !scrolled);
    for (int row = 0; row < int(frame.lines.size()); ++row) {
        if (!everything && row < int(frame.dirty.size()) && !frame.dirty[size_t(row)]) continue;
        lines.append(rowOf(frame.lines[size_t(row)], row));
    }

    QJsonObject message;
    message["t"] = everything ? QStringLiteral("snapshot") : QStringLiteral("diff");
    message["cursor"] = cursorOf(frame);
    message["base"] = frame.viewportTop;
    message["history"] = frame.historyRows;
    message["lines"] = lines;
    if (scrolled) {
        message["scroll"] = QJsonObject{{"top", frame.scrollTop},
                                        {"bottom", frame.scrollBottom},
                                        {"by", frame.scrolledBy}};
    }
    if (everything) {
        message["rows"] = frame.rows;
        message["cols"] = frame.columns;
        message["alt"] = frame.altScreen;
    }
    return message;
}

} // namespace relay::screenjson
