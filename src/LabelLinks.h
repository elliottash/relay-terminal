// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::labellink: a markdown link's *label* carrying its target, inside a block of Relay's own
// output that is already one OSC 8 run (card #MDKN).
//
// `[LABEL](target)` prints as `LABEL (target)`, the label in the link ink — underlined dark green,
// the ink every clickable thing in the transcript wears. Until 2026-09-21 only the `(target)` after
// it was clickable, because that is text and `relay::links` scans text; the label was paint. Two
// automated drives clicked the label and read the result as the feature working, and so does a
// person (owner, 2026-09-21: "yes, add the linking").
//
// Making the label the link means an OSC 8 run around it, and the block it sits in is already one:
// `relay://prose/<pane>/<n>`, the anchor the view re-wraps the block from (#R2WQ). OSC 8 runs do
// not nest — but they do not have to. The pane already switches the anchor mid-line and switches
// back, for the `#K7Q2` segment of a tool-call row (`Pane::drawCallRow`, card #1NW3). What the
// label's cells carry is the block's own URI with the target as a **fragment**:
//
//     relay://prose/p4/7#l=option%3Aagent%2Fallow_writes
//
// The fragment is why this is cheap. The label's cells are still inside the block's URI namespace,
// so `hyperlinkRuns(kProsePrefix)` returns them with the rest of the block and its row range stays
// whole even when a label is the only thing on the block's first or last grid row — the view merges
// the pieces by `anchorOf()`. And the link layer reads `targetOf()` and resolves it through
// `relay::links` exactly as it resolves a span of text, so every kind (`option:`, `session:`,
// `card:`/`#ID`, a path with `:line`, an http(s) URL) opens through the one road every other link
// takes, `Pane::openOutputTarget`.
//
// The anchor need not be a prose block: a thinking bubble's fold URI works the same way, and there
// the label's target reaches the view as `FoldSpan::link` rather than as a cell (src/CallLines.cpp).
//
// Header-only and QtCore-only. It lives in relay-markdown because that is where the URI is minted
// (`MarkdownAnsi::setLinkAnchor`) and because the engine already depends on this library for the
// shared word-wrap rule, never the other way round.
#pragma once

#include <QString>
#include <QUrl>

namespace relay::labellink {

// What separates the anchor from the target it carries. A fragment, so a URI that has one is still
// the block's URI to anything that only looks at the prefix.
inline constexpr QLatin1String kTag("#l=");

// The URI a label's cells carry, or empty when there is no anchor to hang it from (which is how a
// renderer with no anchor set emits no OSC 8 at all). The target is percent-encoded, so a `#`, a
// `;` or a space in it cannot end the sequence or the fragment.
inline QString uriFor(const QString &anchorUri, const QString &target)
{
    if (anchorUri.isEmpty() || target.isEmpty() || anchorUri.contains(kTag))
        return QString();
    return anchorUri + kTag + QString::fromLatin1(QUrl::toPercentEncoding(target));
}

// True for a URI minted above, false for a bare anchor — which is the distinction every caller
// needs: an anchor is not a link (a prose run is the re-wrap handle, a fold URI opens the fold).
inline bool isLabelUri(const QString &uri) { return uri.contains(kTag); }

// The target as the agent wrote it in the markdown, or empty.
inline QString targetOf(const QString &uri)
{
    const int at = uri.indexOf(kTag);
    return at < 0 ? QString() : QUrl::fromPercentEncoding(uri.mid(at + kTag.size()).toUtf8());
}

// The anchor the label sits in: the URI unchanged when it carries no target, so this is safe to
// call on every OSC 8 URI the grid holds.
inline QString anchorOf(const QString &uri)
{
    const int at = uri.indexOf(kTag);
    return at < 0 ? uri : uri.left(at);
}

}  // namespace relay::labellink
