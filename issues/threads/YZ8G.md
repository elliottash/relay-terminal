<!-- relay:entry 20260919T233753Z-ky author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-systematic-qa-skilling-needed.md

<!-- relay:entry 20260919T233815Z-q1 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T233815Z-q2 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
systematic QA skilling needed
```

</details>

<details><summary>after</summary>

```
relay should help users with human QA. deep research and scoping needed on this.
```

</details>

<!-- relay:entry 20260920T035910Z-km author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T035910Z-pw author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +2

<!-- relay:entry 20260920T035911Z-s2 author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +3

<!-- relay:entry 20260920T035911Z-s3 author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +3

<!-- relay:entry 20260920T035912Z-5t author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +3

<!-- relay:entry 20260920T035913Z-67 author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +3

<!-- relay:entry 20260920T035914Z-nn author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +2

<!-- relay:entry 20260920T035914Z-no author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T035914Z-qf author=owner kind=event pane=switchboard -->
- ✦ owner cleared this card's priority flag

<!-- relay:entry 20260920T040915Z-fv author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T041537Z-p3 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Discussing · moved in the Switchboard

<!-- relay:entry 20260920T114941Z-3r author=owner kind=comment mode=discuss -->
a first example of this woudl be a button after "verify", available once you are in needs verification or later. the agent would then run your app in a way that illusrates the feature or fix

<!-- relay:entry 20260920T115103Z-0q author=agent kind=event model=glm-5.3 pane=switchboard turn=5dcd479c6d0e42cea8452f23fbf7f3da/da2931677cab47fa8e05f20ce6ed147a -->
- ✦ agent updated this card · labels: (unset) → ["feature", "switchboard", "qa"]; waiting_on: (unset) → owner; appended to `## Decisions`

<!-- relay:entry 20260920T115108Z-rj author=agent kind=decision model=glm-5.3 pane=switchboard turn=5dcd479c6d0e42cea8452f23fbf7f3da/da2931677cab47fa8e05f20ce6ed147a -->
First example scoped (owner): a button after "Verify" in the card view, enabled from needs-verification onward. "a first example of this woudl be a button after \"verify\", available once you are in needs verification or later. the agent would then run your app in a way that illusrates the feature or fix"

<!-- relay:entry 20260920T115115Z-db author=agent kind=question model=glm-5.3 pane=switchboard turn=5dcd479c6d0e42cea8452f23fbf7f3da/da2931677cab47fa8e05f20ce6ed147a -->
1. What should the demo leave behind? Recommendation: screenshots plus a short screen recording under `docs/qa_evidence/YYYY-MM-DD-<slug>/`, linked in the card's evidence — reuses the existing evidence convention instead of a new store.
2. Who drives the run? Recommendation: the button hands the card to a pane with a canned "illustrate this change" prompt and the agent works out how to launch the app (for Relay itself: the `scripts/relay-build` binary under Xvfb with an isolated `XDG_CONFIG_HOME`). Deterministic, replayable demo scripts would come later as the "systematic" layer.
3. How far does "or later" go? Recommendation: enabled in needs-verification, needs-qa-llm and needs-qa-human, and left on in done so a closed card can still be demoed.

<!-- relay:entry 20260920T115120Z-pv author=agent kind=comment mode=discuss model=glm-5.3 turn=5dcd479c6d0e42cea8452f23fbf7f3da/da2931677cab47fa8e05f20ce6ed147a -->
**Recorded on #YZ8G** — your example is now the card's first decision, and I labeled it `feature`/`switchboard`/`qa`.

My read: this slots in cleanly. The Verify button already lives in the card detail view (`tests/boardmodel_test.cpp` drives it by name), and the enable condition maps to existing statuses — needs-verification, needs-qa-llm, needs-qa-human. A "demo" button beside it would hand the card to a pane with a canned prompt; the agent launches the app and illustrates the change, which for Relay itself is the built binary under Xvfb with an isolated `XDG_CONFIG_HOME`.

Three questions waiting on the card, each with a recommendation:

1. Demo output → screenshots + short recording under `docs/qa_evidence/<slug>/`, linked as card evidence.
2. Who drives it → agent with a canned prompt first; replayable demo scripts later as the systematic layer.
3. "Or later" → enabled through the QA lanes and left on in `done`.

Once those settle, the deep research and scoping the issue calls for belongs in a **Plan** turn — send it there when you're ready.

<!-- relay:entry 20260920T124214Z-03 author=owner kind=comment mode=discuss -->
something else -- we can do the same linking of the QA-support agent pane and the switchbaod card we did with execute and verify: #HKAP

<!-- relay:entry 20260920T124408Z-07 author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=switchboard turn=5fbd6ee3fe3b453a93a3d5ae1d962de7/3b3c1bb9ca9f45478659ea588e339b78 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": [], "evidence": [], "related": ["HKAP"], "github": null…; appended to `## Decisions`

<!-- relay:entry 20260920T124409Z-0e author=agent kind=decision model=deepseek/deepseek-v4.1-flash pane=switchboard turn=5fbd6ee3fe3b453a93a3d5ae1d962de7/3b3c1bb9ca9f45478659ea588e339b78 -->
2026-09-20, owner: "something else -- we can do the same linking of the QA-support agent pane and the switchbaod card we did with execute and verify: #HKAP" — the pane the demo runs in gets Execute's and Verify's treatment: its hand-off entry names it (`Demoing (<first 8 of the pane token>) · …`) and carries `pane_token`, so clicking the entry reveals that pane in whatever window it lives in, and is inert if the pane has been closed. No status move with it — the card stays where it is in the QA lane, as Verify's entry does. This is reuse, not new machinery: `pane_token` on a thread entry, the `relay-pane:` anchor and `focusPane` all landed in #HKAP (56b921f6 for Execute, 31468891 for Verify).

<!-- relay:entry 20260920T124414Z-8d author=agent kind=question model=deepseek/deepseek-v4.1-flash pane=switchboard turn=5fbd6ee3fe3b453a93a3d5ae1d962de7/3b3c1bb9ca9f45478659ea588e339b78 -->
4. Which pane is "the QA-support agent pane"? Two readings, and they differ in scope: (a) the demo pane from decision 1 — the one the new button hands the card to; (b) the verifier pane Verify already opens. Recommendation: (a), and note that if you meant (b) it is already done — #HKAP landed `pane_token` on Verify's hand-off entry in 31468891, so `Verifying (<first 8>)` is already a link to the verifier's pane. A third reading, a pane the card keeps a standing link to for the life of the card, is new machinery and belongs with the systematic layer rather than the demo button.

Questions 1–3 (demo output, who drives it, how far "or later" goes) are still open, each with a recommendation in the earlier question entry.

<!-- relay:entry 20260920T124419Z-69 author=agent kind=comment mode=discuss model=deepseek/deepseek-v4.1-flash turn=5fbd6ee3fe3b453a93a3d5ae1d962de7/3b3c1bb9ca9f45478659ea588e339b78 -->
**Recorded on #YZ8G** — your comment is the card's second decision, and I linked the card to #HKAP (`related`).

The pane linking is reuse, not new work: `pane_token` on a thread entry, the `relay-pane:` anchor and `focusPane` all shipped in #HKAP (56b921f6 for Execute, 31468891 for Verify). So the demo hand-off just writes the token into its entry — `Demoing (<first 8>) · …` — and clicking it reveals the pane, inert once the pane is closed, with no status move.

One question added (4), because "QA-support agent pane" reads two ways: the demo pane from decision 1, or the verifier pane Verify already opens — and if you meant the latter, it is already linked as of 31468891. Recommendation: the demo pane. Questions 1–3 still wait on you, each with a recommendation.

Card stays in **Discussing**, waiting on you. The deep research and scoping the issue asks for belongs in a **Plan** turn when you are ready.
<!-- relay:entry 20260920T225518Z-n7 author=claude-code kind=note -->
### Claude Code (#7BM4) · 2026-09-20 22:55
Owner, on #7BM4: "help me do human QA on it because i dont really understand this issue. lets use this as a scaffold for the human QA support agent". The scaffold is `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/HUMAN-QA.md`: three paragraphs of what the feature is for someone who did not follow the work; which binary and profile to test with and why not the obvious one; numbered steps in four groups, each with a concrete "you should see" and a blank column for what was seen; steps that use the project's own board and make the feature verify itself (#7BM4 lists its own tests and gates its own move); where the verdict goes; what is already known. Its last section lists the six things the demo agent of decision 1 should produce for any card. Two things the exercise found that a QA agent would need to handle: the binary question (a clean export of main crashed at startup, #561P — the walkthrough had to name a different binary), and a Check that answered with 235 per-test lines before grouping (fixed in a646b1d1); a QA agent should run the walkthrough itself once before handing it over, as this session did under Xvfb.
<!-- relay:entry 20260921T022849Z-d8 author=claude-code kind=decision -->
### Claude Code (#7BM4) · 2026-09-21 02:28
Owner, three statements while QA-ing #7BM4: "in my mind, QA support is 'human QA', as in a user is put into a test case that simualtes the problem the issue was designed to address"; "thats good to design for an AI to try simulating first, if we dont have that as well."; "i think verification should involve that (typically)". Recorded in `## Decisions`. The earlier note on this thread described a feature tour; that was the wrong shape and is superseded by the staged scenario under `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/`. What the first pass taught, for the Plan turn: the scenario is data with an actor per step; the AI stops where judgement starts and hands over one question; its pass found three real things before a person spent a minute (a dead binary #561P, an unreadable gate notice, panes too short to read); and it needs to drive Relay by name rather than by pixel.
<!-- relay:entry 20260921T114924Z-dc author=claude-code kind=decision -->
### Claude Code (#7BM4) · 2026-09-21 11:49
Owner: "i tend to agree" — human QA is its own card section beside `## QA checklist`, not part of it. And: "i have been taking an app-focused view here. but many things people build dont have users … can you give me a wider view, potentially with opus-agent research, on how QA is done across different fields with code/computing. so we can take a broader approach to his issue." Both in `## Decisions`; four research passes started (software along the UX spectrum; systems with no users; data analysis, econometrics and ML; the theory of oracles, verification and validation).
<!-- relay:entry 20260921T130633Z-de author=claude-code kind=decision -->
### Claude Code · 2026-09-21 13:06
Owner: "re 3 econometrics -- thats of course my personal interest, but its niche and we should think about this more broadly as for example scientific / research  / knowledge-work automation" and "great, and i want to phase this -- my main worry is that, this approach could get clunky very fast". Both in `## Decisions`; the second is the constraint the plan is written under.
<!-- relay:entry 20260921T130633Z-pf author=claude-code kind=plan -->
### Claude Code · 2026-09-21 13:06
`## Plan` written from five research passes (software with users; systems without users; analysis and ML; the theory and a classification; knowledge work and research automation), kept whole under `docs/research/qa-across-fields/` and synthesised in `docs/QA-ACROSS-FIELDS-RESEARCH.md`. One idea across every field: manufacture an asymmetry between maker and checker and let the right judge decide. A card carries one word — check, diff, measure, scenario, read — proposed by the planning agent. Six phases, each useful alone; the recommendation is to build through 3 and then use it before deciding the rest.
<!-- relay:entry 20260921T130633Z-qg author=claude-code kind=question -->
### Claude Code · 2026-09-21 13:06
1. The five shape words — check, diff, measure, scenario, read — are those the names? Recommendation: yes; short, and each says what the person will be handed.
2. May the AI verifier ever move a card on its own? Recommendation: no — it produces evidence and a recommendation only, until phase 5 gives it a measured record.
3. Build through phase 3 and then pause to use it? Recommendation: yes; phases 4–6 should be decided by where you were asked something you did not need to be asked.
4. "The implementer may not verify or write the checklist" — adopt as a rule now? It costs nothing (Relay already recommends a different model family) but it means the implementing agent hands the checklist to the verifier instead of writing it, as this session did on #7BM4. Recommendation: yes.
<!-- relay:entry 20260921T222650Z-dg author=claude-code kind=decision -->
### Claude Code · 2026-09-21 22:26
Owner answered the four questions ("1 OK", "2 i think cards need to be moveable by agents. are you saying, the verifier is a separate agent?", "3 yes", "4 what do you mean by that? separate LLMs?") and asked whether quiet time in QA is evidence, "eg usage logs indicating usage of features that the card touches". Recorded in `## Decisions`; proven-in-use added to phase 2 of `## Plan`. Card stays in discussing until the owner confirms the plan's phase 2 and 3 scope.
<!-- relay:entry 20260921T222812Z-dh author=claude-code kind=decision -->
### Claude Code · 2026-09-21 22:28
Owner: "i was not looking at knowledge work as a metaphor for verification. i was asking, how will knowlege workers use relay terminal". Recorded; the product question (a researcher's cards, their "done", what the tooling row becomes) is now the open one, with a product pass on three people proposed, economist first.
<!-- relay:entry 20260921T230206Z-cx author=claude-code kind=plan -->
### Claude Code · 2026-09-21 23:02
Owner: "can you query codex cli to give you a skeptical take and to see if this is forced … then have codex give you the product simulations as well". Codex (gpt-6-astra, high) said the shape-per-card taxonomy is forced and gave a five-line design and a smaller slice; its three simulations (an economist, a policy analyst, a computational scientist) agree on one output record, one acceptance record, and Results as a separate list. Both are under `docs/research/qa-across-fields/` (f, g). `## Plan` rewritten to that slice; the previous plan is superseded and summarised in `docs/QA-ACROSS-FIELDS-RESEARCH.md` §5. Three questions for the owner replace the earlier four.
<!-- relay:entry 20260921T233702Z-ex author=claude-code kind=decision -->
### Claude Code · 2026-09-21 23:37
Owner: "i agree with all, go ahead with it". Plan adopted; card claimed; steps 1–3 filed as #PR4Q, #WC3E, #JNYN with their expectations written first. One Opus subagent per card; a separate verifier session writes each checklist.
<!-- relay:entry 20260921T234633Z-df author=claude-code kind=decision -->
### Claude Code · 2026-09-21 23:46
Owner: "for apps, the agents should: 1) run tests 2) verify with an AI simulator 3) open the app for the human in a simulated environment that tests that issue. does this map onto our approach? does verify do #2? what button should be added to the board for #3?" Answer recorded in `## Decisions`: Verify did not do (2) — it ran commands and read code; now it does; Try it is the button for (3). The #WC3E and #JNYN agents were told mid-flight; both cards' `## Done means` updated.
<!-- relay:entry 20260922T003829Z-dj author=claude-code kind=decision -->
### Claude Code · 2026-09-22 00:38
Owner: "how do i test out the new QA approach. is the agent going to design simulation? i think the QA agent should also always think about each step of the user experience , and apps should be built for live simulated drives by agents and humans -- the QA agent tries to optimize this to conserve scarce human tester time". Recorded; #74Y5 filed with its expectations.
