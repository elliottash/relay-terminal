# Expected — #MTCS staged restore (sealed; do not read before judging)

The staged pane comes back with, top to bottom:

1. four plain rows of build output (`$ ./scripts/build.sh --target demo` and the compiler
   lines) — exactly as saved;
2. a heading line ("A saved agent turn, restored: its heading sits in its own block.") in its
   own block;
3. the long indented paragraph, laid out to the pane's **current** width — not the 45
   columns it was saved wrapped at.

The one task is resizing the pane (drag the window edge, or split and drag the divider):

- Making the pane narrower re-wraps the paragraph to follow the edge: lines break earlier,
  the paragraph gets taller. Making it wider re-wraps it back out: fewer, longer lines.
- The plain build-output rows never move or re-wrap — shell output is a fixed record; only
  Relay's own output (heading, paragraph) re-wraps.
- The paragraph reads the same at every width: no word duplicated by a half-applied wrap, no
  word lost, no row shorter than it should be mid-paragraph, the indent consistent.

Fail looks like: the paragraph keeping its 45-column hard line breaks after restore (the
bug this card fixes), rows duplicated around a resize, words eaten at a wrap point, or the
plain build output re-flowing too.
