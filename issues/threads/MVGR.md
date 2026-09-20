<!-- relay:entry 20260920T030405Z-23 author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-when-a-recap-shows-up-after-an-agent-finishes-in.md

<!-- relay:entry 20260920T030426Z-52 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T030426Z-aq author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
when a recap shows up after an agent finishes, include at the beginning, finished at [time]
```

</details>

<details><summary>after</summary>

```
when a recap shows up after an agent finishes, include at the beginning, 

[end of meassage]

finished at [time]

[recap]
```

</details>

<!-- relay:entry 20260920T041044Z-hz author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T041823Z-w1 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T041823Z-w2 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T042153Z-qj author=agent kind=event model=glm-5.3 pane=switchboard turn=6884a14006494fe3a32063802334d70e/1119a55ad15547b196d603807b1a5d85 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T042158Z-2b author=agent kind=comment mode=plan model=glm-5.3 turn=6884a14006494fe3a32063802334d70e/1119a55ad15547b196d603807b1a5d85 -->
The card is about the recap block printed when you return to a finished agent. Let me pull the exact printing code in `src/Pane.h`, the away-recap trigger, the protocol doc and the tests.









One more look at the protocol doc's changelog shape and `checkpoints.span`, so the plan's steps name real anchors.

I have the full picture now. Writing the plan onto the card.

Plan written to #MVGR. The recap block (worker adds a `finished_text` field off the recorded turn-end stamp, `src/Pane.h` prints `[end of message]` / `finished at HH:MM` ahead of the `Recap ·` header, protocol doc §5 and the recap-span tests updated) — two owner calls are flagged in Risks: whether the three parts should be spaced as separate paragraphs rather than adjacent lines, and whether the preamble applies to every recap or only the away one.

Also noted in Findings, not fixed here: the phone's recap hook sends `reason: "remote"`, which the worker rejects — that deserves its own bug card.

<!-- relay:entry 20260920T042158Z-fa author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260920T052317Z-jd author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T052317Z-nr author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T052318Z-k5 author=owner kind=progress pane=switchboard pane_token=55b12d04-7f65-4c13-aaa3-2fc1f9a24ae1 -->
Executing (55b12d04) · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T054200Z-m7 author=agent kind=progress model=glm-5.3 -->
Implemented per the plan. `span_fields()` adds `finished_text` — the span's end in the local
clock, present only when the last stamped turn carries `ended`; `src/Pane.h` prints
`[end of message]` / `finished at …` ahead of the `Recap ·` header for every recap reason;
protocol §5 has the new "Recap finish" bullet; the RecapSpan tests cover today/dated/running and
the protocol test's resume recap carries the field. `scripts/relay-build` green (45 s),
`transcriptgaps` green, all recap tests green.

Live under Xvfb (loopback stub, no key, `RELAY_RECAP_AWAY_SECONDS=2`): the away recap after a
turn that finished while the window was away reads `[end of message]` / `finished at 01:38` /
`Recap · 01:38 → 01:38 · <1m`; manual `/recap` prints the same preamble; a `/recap` asked while
a turn still runs prints the marker with no finish time. 7/7 checks PASS — evidence and
reproducer in `docs/qa_evidence/2026-09-20-recap-finished-at/`.

Finding while driving it, not a fault of this change: while the agent is busy the prompt box is a
queue, and Ctrl+Return escalates a steer — it interrupted the running turn instead of running
`/recap`. The slash list's own Enter runs `/recap` mid-run fine. Noting it because the first
drive attempt tripped over it (`run-first-attempt.log`).

<!-- relay:entry 20260920T054200Z-m8 author=agent kind=event model=glm-5.3 -->
- ✦ agent moved this card · Executing → Needs verification · landed, evidence under docs/qa_evidence/2026-09-20-recap-finished-at/
