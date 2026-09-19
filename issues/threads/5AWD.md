<!-- relay:entry 20260919T182953Z-xt author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-helpful-line-breaks-in-the-terminal.md

<!-- relay:entry 20260919T183351Z-cg author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T183351Z-g5 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
helpful line breaks in the terminal
```

</details>

<details><summary>after</summary>

```
add line breaks in the terminal between content types. 

there should be a line break between agent messages and tool calls for example. sequences of tool calls are single-spaced. ditto with user messsages, line breaks between them (could be half spaced if that looks better)
```

</details>

<!-- relay:entry 20260919T183424Z-sc author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260919T183447Z-7s author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260919T183447Z-vv author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260919T183447Z-vw author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260919T184114Z-mf author=agent kind=progress model=claude-fable-5-1 -->
Read the code. The transcript prints every block single-spaced: the ✦ line, the `▸ model` header, prose deltas, ▸ tool rows. Taking "done" as: one blank line between blocks of different kinds — prose vs tool rows, and before every ✦ line the user typed (including across turns, over the shell prompt) — none inside a run of tool rows, and the `▸ model` header sits directly on what follows it. Rule in src/TranscriptGaps.h (headless test), hooks in Pane.h's printInline/drawCallRow/turnHeader. Building now; live shots under Xvfb next.
