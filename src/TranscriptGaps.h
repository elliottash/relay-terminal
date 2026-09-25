// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Blank lines between the kinds of thing the terminal transcript prints (#5AWD).
//
// A turn's transcript is a sequence of blocks: the user's ✦ line, the "▸ model" header, the
// agent's prose, the ▸ tool-call rows, the "✦ N tool calls" link that sums a run of them up, and
// a "Recap ·" block. The owner asked for a blank line between content types — prose and tool
// calls, one user message and the next — and none inside a run of tool calls. The link and the
// recap read as content types too (owner, 2026-09-19: "in between agent messages, user messages,
// or tool calls, there should be a blank line"): the link is set off from the prose above it,
// and the recap from whatever printed before it.
// Reasoning is machinery, not a message (owner, 2026-09-20, #TJBC): a thinking fold counts as a
// call row, so thoughts and tool rows form one single-spaced block and only the agent's prose is
// set off by blank lines on either side.
//
// The pane keeps the kind of the last block it printed and asks this before starting the next.
// Nothing here writes anything; the pane does, and tests/transcriptgaps_test.cpp checks the rule
// headless.
namespace relay::gaps {

enum class Block {
    None,     // nothing printed yet, or the screen was cleared
    User,     // a ✦ line the user typed (a prompt, a steer, an answer)
    Header,   // the turn's "▸ model" line, or a "── request ──" separator
    Agent,    // the agent's prose
    Call,     // a ▸ tool-call row, a thinking fold, a subagent line, a turn-limit line, the ✦ N tool calls link
    Recap,    // a "Recap ·" header and the summary, Next · and Open · lines under it
};

// True when a blank line goes before a block of kind `next` that follows one of kind `prev`.
// Never before the first block, never after a header (the header introduces what follows it),
// and never between two blocks of the same kind: a run of calls stays single-spaced, and so do
// two consecutive prose deltas.
inline bool gapBefore(Block prev, Block next)
{
    if (prev == Block::None || next == Block::None) return false;
    if (prev == Block::Header) return false;
    return prev != next;
}

}  // namespace relay::gaps
