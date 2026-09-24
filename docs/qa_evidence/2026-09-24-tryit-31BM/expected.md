# Expected — #31BM (sealed until you answer)

When you run `/model local:small`, the pane first has to compact. While "Compacting the
conversation…" is up, the chip at the bottom of the pane (the one that normally reads
"N% left | <model>") reads **compacting… N% | small**, with N climbing over the ~24 s the
summary takes (in this fixture roughly 20% → 40%; it may jump, and it never reaches 100% —
it is clamped at 95% until the compaction finishes). If the summarising model were a reasoning
model, this same chip could instead read **compacting… thinking** while it reasons; this
fixture's summary is plain, so you should see only the percent. When the compaction lands the
chip goes back to **N% left | big**, because the fixture's fake `small` (12,000-token window)
is expected to refuse the takeover even after compaction — the pane prints that refusal as a
note, and it is part of the fixture, not the feature.

The number is an estimate, not a progress bar with a known end: the denominator is the
previous compaction's summary size if there was one, else a guess from the transcript length.
So a stall at one number for a few seconds is fine; what should NOT happen is the chip staying
at plain "compacting…" for the whole 24 s with no number ever appearing, or the percent not
moving at all between two glances several seconds apart.
