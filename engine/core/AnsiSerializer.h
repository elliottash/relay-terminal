// SPDX-License-Identifier: AGPL-3.0-or-later
// Convert styled terminal lines into ANSI SGR escape sequences.
//
// The serializer walks the engine-neutral relay::Line cells and emits ESC [ ... m
// sequences for text attributes and colours. Only SGR is produced: no OSC, no cursor
// movement, no alternate-screen switches. The output is safe to replay into a terminal
// through a filter that drops every escape sequence except CSI SGR.
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

} // namespace relay
