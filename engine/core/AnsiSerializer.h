// SPDX-License-Identifier: AGPL-3.0-or-later
// Convert styled terminal lines into ANSI SGR escape sequences.
//
// The serializer walks the engine-neutral relay::Line cells and emits ESC [ ... m
// sequences for text attributes and colours. Only SGR is produced, plus OSC 8 links where
// the caller resolves them: no other OSC, no cursor movement, no alternate-screen switches.
// The saved form is safe to replay into a terminal through restorableAnsi(), which drops
// every escape sequence except CSI SGR and the OSC 8 links of inline image rows.
#pragma once

#include "CellTypes.h"

#include <QString>
#include <QStringList>
#include <vector>
#include <functional>

namespace relay {

// One line per entry, oldest first, with trailing blanks trimmed exactly the way
// Line::text() trims them. Empty lines stay empty.
QStringList linesToAnsi(const std::vector<Line> &lines);

// The ANSI representation of a single line. Cells with kWideTail are skipped; blanks
// become spaces. The line is reset to default attributes at the end if any SGR was
// emitted, so a following line starts from a known state.
// A trusted live snapshot may supply a resolver to retain OSC 8 links as well. Persisted
// scrollback uses the default (SGR only); never replay arbitrary file contents as OSC.
QString lineToAnsi(const Line &line, const std::function<QString(uint32_t, int)> &linkUri = {});

// The form a pane's text is saved in (src/WindowState.h): lineToAnsi() keeping the OSC 8 links
// of inline image rows (engine/core/InlineImage.h, card #1MGS) and no other link, so a restored
// pane draws its pictures again. `linkUri` resolves a cell's link id as for lineToAnsi().
QString lineToSavedAnsi(const Line &line, const std::function<QString(uint32_t, int)> &linkUri);

// A saved line made safe to replay: CSI SGR is kept, and an OSC 8 open/close pair only when
// its URI is an image row's (inlineimage::parseImageUri), so the most a hand-edited file can
// do is make the view paint a local image file. Every other escape introducer and control is
// dropped (the bytes after it stay as text); a well-formed stray close is dropped whole, and a
// line never ends inside an image link.
QString restorableAnsi(const QString &saved);

} // namespace relay
