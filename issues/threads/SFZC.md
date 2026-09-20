<!-- relay:entry 20260919T183603Z-5y author=owner kind=event pane=switchboard -->
- ✦ owner created this card in In progress · issues/features/2026-09-19-common-word-folder-names-get-highlighted-in-agen.md

<!-- relay:entry 20260919T183633Z-mq author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T183633Z-p1 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
common-word folder names get highlighted in agent messages
```

</details>

<details><summary>after</summary>

```
common-word folder names keep getting highlighted in agent messages, eg tests and remote. any good way to fix that?
```

</details>

<!-- relay:entry 20260919T200248Z-wh author=agent kind=event model=glm-5.3 pane=switchboard turn=e17598d07151419d86ddfea89de60412/c1c5a69cae214f50bedb9384a2802d81 -->
- ✦ agent updated this card · labels: (unset) → ["bug", "gui"]

<!-- relay:entry 20260919T200304Z-rh author=agent kind=event model=glm-5.3 pane=switchboard turn=e17598d07151419d86ddfea89de60412/c1c5a69cae214f50bedb9384a2802d81 -->
- ✦ agent moved this card · In progress → Inbox · Created straight into In progress by the intake bug fixed in #5G43 (new cards target the intake section now); no commits, evidence or thread work since creation, so nobody is working it — it goes back to Inbox for triage.

<!-- relay:entry 20260920T040847Z-m3 author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T041450Z-7v author=owner kind=comment mode=discuss -->
just glancing at some terminal output, i think a quick way to fix this is to 

1) highlight folder names in terminal output

2) in agent messages, highlight folders only with "/" after them

3) prompt the agent that, when it mentions folders, add a "/" after.

<!-- relay:entry 20260920T041450Z-xj author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260920T041742Z-cz author=agent kind=event model=glm-5.3 pane=switchboard turn=b608b1258e634ceb9e89b2540ea14821/085071de51044f969ef9b2a667a3f86f -->
- ✦ agent updated this card · waiting_on: (unset) → owner; appended to `## Direction`

<!-- relay:entry 20260920T041745Z-q9 author=agent kind=question model=glm-5.3 pane=switchboard turn=b608b1258e634ceb9e89b2540ea14821/085071de51044f969ef9b2a667a3f86f -->
1. In agent messages, should a bare *file* name (`README.md` in backticks) keep its link, or does the `/` rule apply to files too? Recommendation: keep files linking — every false positive is a directory (an English word), and a bare filename in backticks is the commonest openable thing in a reply; the `/` requirement stays for folders only.

<!-- relay:entry 20260920T041754Z-hp author=agent kind=comment mode=discuss model=glm-5.3 turn=b608b1258e634ceb9e89b2540ea14821/085071de51044f969ef9b2a667a3f86f -->
Let me look at how the path highlighting actually works before responding.

`src/OutputLinks.*` looks like the clickable-paths highlighter. Reading it.







Now let me see where agent messages are scanned — `Pane.h:13651` uses the probe:









The terminal side is clear now. The card is about *agent messages*, which arrive as prose blocks — let me check how the host builds those:







The picture is complete. Quick check where the agent's system prompt lives, for point 3:

The mechanism is confirmed. Now recording the direction on the card:



**Your sketch is right, and half of it is already built.** What I found on #SFZC:

1. **Terminal output already does this** — bare folder names link there by design (`restLinkColumns` in `engine/view/TerminalView.cpp`: an `ls` of extension-less folders is "the commonest link-bearing line"). Nothing to do.
2. **The bug's mechanism:** the bare-token pass in `relay::links::candidates` (`src/OutputLinks.cpp`) probes every word against the pane's cwd — so `tests`, `remote` (and `docs`, `build`, `data`, `site`) resolve as directories and light up in prose. A prose mode there — token needs a `/` to be a folder candidate — fixes hover, at-rest colour and the Ctrl+Shift+L walk in one place, since they share that scan. The `relay://prose/` anchors already mark which rows are Relay-printed, so the view can tell prose from program output.
3. **Agreed** — one line in the pane agent's prompt (`backend/relay_core/agent.py`, the formatting block) covers it. It's a nudge though; rule 2 is what holds for guests and other models.

I wrote this up in a `## Direction` section on #SFZC and left one question there: should a bare *file* name (`README.md` in backticks) keep linking in agent messages? My recommendation is yes — every false positive is a directory, and bare filenames are the commonest openable thing in a reply.

Answer that and this is ready for **Execute** — the change is small enough that it doesn't need a Plan turn.

<!-- relay:entry 20260920T052527Z-2g author=owner kind=comment mode=discuss -->
file names still link

<!-- relay:entry 20260920T052556Z-08 author=agent kind=decision model=kimi-k3 pane=switchboard turn=e2f7301433ac47d680c65daeeac2d5b3/bf9949ca5b50471984b9dff4cc187ead -->
"file names still link" — in agent prose, a bare *file* name keeps its link; the `/` requirement applies to folder candidates only. This resolves the open question in the Direction.

<!-- relay:entry 20260920T052556Z-f2 author=agent kind=event model=kimi-k3 pane=switchboard turn=e2f7301433ac47d680c65daeeac2d5b3/bf9949ca5b50471984b9dff4cc187ead -->
- ✦ agent updated this card · waiting_on: owner → (unset); appended to `## Decisions`

<!-- relay:entry 20260920T052609Z-37 author=agent kind=event model=kimi-k3 pane=switchboard turn=e2f7301433ac47d680c65daeeac2d5b3/bf9949ca5b50471984b9dff4cc187ead -->
- ✦ agent updated this card · replaced `## Direction`

<!-- relay:entry 20260920T052609Z-rf author=agent kind=rewrite model=kimi-k3 pane=switchboard turn=e2f7301433ac47d680c65daeeac2d5b3/bf9949ca5b50471984b9dff4cc187ead -->
- ✦ rewrote ## Direction

<details><summary>before</summary>

```
Owner sketch (2026-09-20), read against the code:

1. **Terminal output keeps highlighting bare folder names** — already today's behaviour and deliberate (`restLinkColumns`, engine/view/TerminalView.cpp: an `ls` of extension-less folders is the commonest link-bearing line). Nothing to do.
2. **Agent messages: a folder links only when the token carries a `/`** (`tests/`, `src/Pane.h`). The false positives are all bare English words that happen to be directories relative to the pane's cwd — `tests`, `remote`, and equally `docs`, `build`, `data`, `site`. Mechanism today: the bare-token pass in `relay::links::candidates` (src/OutputLinks.cpp stage 6) plus the filesystem probe. Fix locus: a prose mode there (bare token must contain `/` to be a folder candidate), applied on surfaces the view knows are Relay-printed prose — the `relay://prose/` anchor runs (#R2WQ) already mark those rows; hover, at-rest colouring and the Ctrl+Shift+L walk all share the one scan, so they follow for free. Tests: tests/outputlinks_test.cpp.
3. **Teach the agent the habit**: one line in the pane agent's system prompt (backend/relay_core/agent.py, the formatting block ~line 188) — name a folder with a trailing `/`. Polish, not the fix: a prompt is a nudge, the scanner rule is what holds for guests and other models too.

Open: whether a bare *file* name in agent prose keeps its link — see the question in the thread.
```

</details>

<details><summary>after</summary>

```
Owner sketch (2026-09-20), read against the code:

1. **Terminal output keeps highlighting bare folder names** — already today's behaviour and deliberate (`restLinkColumns`, engine/view/TerminalView.cpp: an `ls` of extension-less folders is the commonest link-bearing line). Nothing to do.
2. **Agent messages: a folder links only when the token carries a `/`** (`tests/`, `src/Pane.h`). The false positives are all bare English words that happen to be directories relative to the pane's cwd — `tests`, `remote`, and equally `docs`, `build`, `data`, `site`. Mechanism today: the bare-token pass in `relay::links::candidates` (src/OutputLinks.cpp stage 6) plus the filesystem probe. Fix locus: a prose mode there (bare token must contain `/` to be a folder candidate), applied on surfaces the view knows are Relay-printed prose — the `relay://prose/` anchor runs (#R2WQ) already mark those rows; hover, at-rest colouring and the Ctrl+Shift+L walk all share the one scan, so they follow for free. Tests: tests/outputlinks_test.cpp.
3. **Teach the agent the habit**: one line in the pane agent's system prompt (backend/relay_core/agent.py, the formatting block ~line 188) — name a folder with a trailing `/`. Polish, not the fix: a prompt is a nudge, the scanner rule is what holds for guests and other models too.

Decided (2026-09-20): a bare *file* name in agent prose keeps its link — the `/` requirement is for folders only.
```

</details>

<!-- relay:entry 20260920T052614Z-gs author=agent kind=event model=kimi-k3 pane=switchboard turn=e2f7301433ac47d680c65daeeac2d5b3/bf9949ca5b50471984b9dff4cc187ead -->
- ✦ agent moved this card · Discussing → Planned · Direction agreed, open question answered (files keep linking; `/` rule is folders-only). Small, well-scoped change — no Plan turn needed; ready for Execute.

<!-- relay:entry 20260920T052617Z-jz author=agent kind=comment mode=discuss model=kimi-k3 turn=e2f7301433ac47d680c65daeeac2d5b3/bf9949ca5b50471984b9dff4cc187ead -->
**Done:** recorded your call on #SFZC — bare *file* names keep linking; the `/` requirement is for folders only. It's in a `## Decisions` section and the Direction's open question is resolved. Moved the card **Discussing → Planned** — hit **Execute** to hand it to a terminal pane.

<!-- relay:entry 20260920T052633Z-1b author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T052633Z-hq author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T052633Z-v3 author=owner kind=progress pane=switchboard pane_token=9befb1d8-dcf8-4af4-97b1-63a271e82c37 -->
Executing (9befb1d8) · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T054242Z-md author=agent kind=event model=glm-5.3 pane=9befb1d8 turn=6cb8b743fde24d21a997d224210407e3/af319297d7e14e488535260d788e0da3 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["a7fc3ca2"], "evidence": ["docs/qa_evidence/2026-09-20…

<!-- relay:entry 20260920T054242Z-r5 author=agent kind=progress model=glm-5.3 pane=9befb1d8 turn=6cb8b743fde24d21a997d224210407e3/af319297d7e14e488535260d788e0da3 -->
Landed `a7fc3ca2` — links: a bare folder word is not a link in agent prose (#SFZC), signed `Implemented-By: glm/glm-5.3`. `Mode::Prose` in relay::links (bare candidate resolves only to a file) applied at all three surfaces in TerminalView (hover, at-rest colour, walk) via the relay://prose/ anchors. Tests: 5 new OutputLinks cases (35 pass) and `ViewTest::proseBareFolderWordsAreNotLinks` on real files (41 pass, pixel-level at-rest assertions); `scripts/relay-build` clean; Xvfb isolated-config smoke OK.

Provenance quirk on the prompt line: it was edited in the worktree first and the #B9V4 session's land.py sweep of agent.py carried it onto main inside their commit `9628872d`, word-for-word, one copy, worktree clean — so it needs no second commit (and rewriting theirs was not an option). Evidence: docs/qa_evidence/2026-09-20-prose-bare-folder-links/.

<!-- relay:entry 20260920T054251Z-9j author=agent kind=event model=glm-5.3 pane=9befb1d8 turn=6cb8b743fde24d21a997d224210407e3/af319297d7e14e488535260d788e0da3 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260920T054255Z-ym author=agent kind=event model=glm-5.3 pane=9befb1d8 turn=6cb8b743fde24d21a997d224210407e3/af319297d7e14e488535260d788e0da3 -->
- ✦ agent moved this card · Executing → Needs verification · Fix landed in a7fc3ca2 (prompt line via 9628872d): Mode::Prose in relay::links applied at hover, at-rest colour and the walk; targeted suites and ViewTest pixel assertions green, Xvfb smoke OK. Evidence and QA checklist on the card. · evidence docs/qa_evidence/2026-09-20-prose-bare-folder-links/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260920T054326Z-rn author=agent kind=event model=glm-5.3 pane=9befb1d8 turn=6cb8b743fde24d21a997d224210407e3/af319297d7e14e488535260d788e0da3 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["a7fc3ca2"], "evidence": ["docs/qa_evidence/2026-09-20… → {"commits": ["a7fc3ca2", "281c81e8"], "evidence": ["docs/qa_evidence/2026-09-20-…
