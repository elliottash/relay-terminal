---
name: slide-deck
description: Build a talk's slide deck from an outline and figures, compile it, compare candidate layouts side by side, and leave it unverified until the person has rehearsed it. "make the slides", "slide deck for the talk".
short: 'Slide deck from an outline and figures; compiled, compared pairwise, verified only once rehearsed.'
profile: |
  artifact: visual
  primary: script
  also: ai-visual, pairwise
  deferred: until rehearsed aloud in the time slot
  human: required
  criteria: rehearsed: the talk fits the slot, every figure reads from the back of the room, one claim per slide
  sign_off: none
  effort: medium
  stakes: reputation
  blast: case
  regularity: mixed
  executable: yes
  rot: low
  confidential: no
  money: no
---

# Slide deck

1. Read the outline and list the figures it names; a figure that does not exist is a question
   for the person, not a placeholder.
2. Write one slide per claim, title as the claim in a full sentence, at most one figure or one
   table per slide.
3. Compile the deck; a compile error is yours to fix before anything else.
4. Render every slide to an image and look at each one: cut-off text, unreadable axis labels,
   a figure that says less than its title.
5. Where two layouts are plausible, render both and keep the one that reads better side by
   side; note the rejected one in the case.
6. Time the deck at one slide per minute against the slot; drop slides before shrinking text.
7. Hand the compiled deck and the rendered images to the person. The deck stays unverified
   until they have rehearsed it; what they cut in rehearsal is recorded on the case.
