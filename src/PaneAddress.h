// SPDX-License-Identifier: AGPL-3.0-or-later
// A pane's address for cross-pane messages (card #R5TC): `p1`, `p2`, `p3`…
//
// Minted once per pane at construction and never handed out again, so a stale "pane 2" fails
// loudly instead of resolving to whichever pane now sits where it was. Not a position (a split
// renumbers those) and not a title (titles are model-written and nothing makes them unique). The
// shape copies remote/pane_state.py's `m<n>`/`s<n>`, not a card id's `#K7Q2`, which it would
// make ambiguous. `m_token` stays the internal routing key; this is the name a model types.
#pragma once
#include <QString>

namespace relay::paneaddress {

// The next handle for this process: 1, 2, 3… Never reused, never reset.
int mint();

// "p3". Empty for a handle below 1.
QString label(int handle);

// The handle a model or a person wrote, or 0. Accepts "p3", "P3", "3", "pane 3", "pane p3" and a
// row copied whole from pane_list ("p3 · \"Release notes\""), with surrounding spaces.
int parse(const QString &text);

} // namespace relay::paneaddress
