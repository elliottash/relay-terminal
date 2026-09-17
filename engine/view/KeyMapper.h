// SPDX-License-Identifier: GPL-3.0-or-later
// Qt key events -> engine-neutral relay::KeyInput. The VtCore encodes KeyInput
// into bytes for the current terminal modes (cursor/keypad application mode,
// kitty keyboard protocol with libghostty-vt, ...).
#pragma once

#include "core/CellTypes.h"

#include <Qt>

class QKeyEvent;

namespace relay {

struct KeyMapperOptions {
    // macOS: treat Option as Alt/Meta (true) or let it compose characters (false).
    bool optionIsAlt = false;
};

// Returns false for events that must not reach the program (pure modifiers,
// dead keys, unknown keys without text).
bool mapKeyEvent(int qtKey, Qt::KeyboardModifiers modifiers, const QString &text, bool autoRepeat, KeyInput *out,
                 const KeyMapperOptions &options = {});
bool mapKeyEvent(const QKeyEvent *event, KeyInput *out, const KeyMapperOptions &options = {});

uint8_t mapModifiers(Qt::KeyboardModifiers modifiers, const KeyMapperOptions &options = {});

} // namespace relay
