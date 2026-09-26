---
id: BE1W
type: work
status: executing
labels: [feature, remote, phone]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude:ashe-ethz-ch
session: 47171b5e-f82e-4bcd-a055-bf22f59d39e7
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [ai-visual, person], human: optional, criteria: 'At 390x844: a #ID in the pane''s output is a link; a tap opens that card on the phone Board and Back returns to the pane; a long press shows Insert in prompt / Open card / Copy; Insert opens the write sheet with the code at the cursor; a scroll that starts on a code does neither; #1234 is not linked.', sign_off: none, effort: low, stakes: nuisance, blast: capability}
source: terminal pane 47171b5e, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [8R3V], github: null}
---
# Phone: tap a card code in a pane to open it, long-press for Insert in prompt / Open / Copy

## Issue
Card codes (`#K7Q2`) in a pane's terminal output are plain text on the phone today. Make them links:
- a tap opens the card on the phone's Board, as a click does on the desktop, and Back returns to the pane;
- a long press opens a small menu with Insert in prompt, Open card and Copy;
- Insert opens the write sheet with the code added at the cursor.

Owner agreed with this design over single tap to insert / double tap to open (the tap-delay, link-convention and hidden-draft problems). The owner declined the optional "tap inserts while the write sheet is open" variant.

> clicking a card code in a console pane should copy it into the prompt. double clicking zooms to the card. will that be intuitive? … i agree with your recommendation. no on the optional item. put this on a card, and build and commit, but dont merge.
> — elliott · [session:126f58f09e8e410e9141b406f427a4e8](relay://session/126f58f09e8e410e9141b406f427a4e8) · 2026-09-26

## Done means
On the phone, a card code such as `#K7Q2` in a pane's terminal output reads as a link. A tap opens that card on the phone's Board, and Back returns to the pane. A long press opens a small menu:
- **Insert in prompt** opens the write sheet with `#K7Q2 ` at the cursor;
- **Open card** opens it;
- **Copy** copies `#K7Q2`.

A drag that starts on a code scrolls, and does neither. Text that only looks like a code, such as a bare `#1234`, stays plain unless the board knows it as a card.

Failure: codes stay plain text, a tap edits the prompt, a long press selects text or opens the system callout instead of the menu, or Back from the card leaves the pane for the inbox.
