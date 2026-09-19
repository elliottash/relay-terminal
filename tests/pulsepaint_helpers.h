// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the two translation units of the pulse test (card #V8KT): declarations here, the
// painting (which includes PaneChrome.h) in pulsepaint_paint.cpp, the moc'd test class in
// pulsepaint_test.cpp.
#pragma once

#include "PaneStatus.h"

#include <QImage>

// The tab's icon at 16 logical pixels, painted as relay::chrome::tabIcon paints it.
QImage tabAt(relay::panestatus::State state, relay::panestatus::State live, int phase, qreal dpr);
// The pane header's glyph, painted as PaneStateGlyph paints it.
QImage headAt(relay::panestatus::State state, int phase, qreal dpr);
// The subagent badge (card #YMSR), painted as PaneSubagentBadge paints it: an 18 px-high box of the
// widget's own width on the header's ground — or on the ssh band's fill, when `remote` — carrying
// the count `live`. A count of zero paints nothing, and the box is still a one-digit badge's size,
// so a test can assert that "nothing" instead of comparing two empty pictures.
QImage badgeAt(int live, qreal dpr, bool remote);
// The same badge painted into a wider surface with its box `offset` logical pixels along, which is
// what any caller that is not the widget itself does. Everything inside the badge is measured from
// the box, so this is the badge of badgeAt() shifted and nothing else.
QImage badgeOffsetAt(int live, qreal dpr, int offset);
// The two grounds the badge can land on, read from the live theme: the pane header's own background
// and the ssh band's fill. They live here because the tokens are behind PaneChrome.h.
QColor headerGround();
QColor sshBandFill();
