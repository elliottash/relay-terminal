// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QByteArray>

namespace relay {
enum class InlineInk { Agent, User, UserAgent, Tool, ToolOutput, DiffAdd, DiffRemove, Error, Note, Recap, RecapBody, Ask };

// Store palette references, never the theme's current RGB. Reverse video puts
// diffs on their meaning colour with the terminal ground as contrasting ink.
inline QByteArray inlineInkCode(InlineInk ink) {
    switch (ink) {
    case InlineInk::Agent: return "\x1b[97m";
    case InlineInk::User: return "\x1b[1;36m";
    case InlineInk::UserAgent: return "\x1b[1;35m";
    case InlineInk::DiffAdd: return "\x1b[7;32m";
    case InlineInk::DiffRemove: return "\x1b[7;31m";
    case InlineInk::Error: return "\x1b[31m";
    case InlineInk::Ask: return "\x1b[1;33m";
    case InlineInk::RecapBody: return "\x1b[3;90m";
    default: return "\x1b[90m";
    }
}
} // namespace relay
