<!-- relay:entry 20260920T122923Z-xw author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-20-changing-models-during-rate-limit-retries.md

<!-- relay:entry 20260920T123225Z-v7 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · title: changing models during rate limit retries → changing models during rate limit retries -- bug; replaced `## Issue`

<!-- relay:entry 20260920T123225Z-z7 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote title

<details><summary>before</summary>

```
changing models during rate limit retries
```

</details>

<details><summary>after</summary>

```
changing models during rate limit retries -- bug
```

</details>

<!-- relay:entry 20260920T123225Z-z8 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
changing models during rate limit retries
```

</details>

<details><summary>after</summary>

```
there is a bug  i think, where when a model gets a denial eg 429 and starts retrying, i cant switch the model. if i click a new one in the picker, it keeps retrying rather than move immediately. i have to esc to escape and interrupt the retries. that shouldnt happen -- it should just switch over.

too help with this, add /swap as a command that immediately swaps to your default fallback (or back to your first choice provider if you are on the fallback).
```

</details>

<!-- relay:entry 20260920T123227Z-fp author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T123227Z-j1 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260921T002115Z-6p author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260921T002115Z-cs author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planning · a Plan turn started

<!-- relay:entry 20260921T002435Z-ra author=agent kind=event model=kimi-k3 pane=switchboard turn=4ee17299f4c54e4b9f77c8e868f2684b/20ce4ab4a8e64291865165839b666189 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260921T002449Z-6x author=agent kind=comment mode=plan model=kimi-k3 turn=4ee17299f4c54e4b9f77c8e868f2684b/20ce4ab4a8e64291865165839b666189 -->
The code already references card #DC4J — part of this may be implemented already. Let me check both halves.



















**Plan written on #DC4J.** The notable finding: both halves are already implemented in the tree and cite this card — the retry preemption (`backend/relay_core/provider.py` + `agent.py`, with `RetryPreemptionTests` in `tests/test_model_switch.py`) and `/swap` (`src/Pane.h:7931,8279`), and the protocol docs are updated — but the card was never moved or the work verified.

So the plan is mostly audit-and-finish: confirm git/land state (it may be another session's uncommitted work), verify the implementation against the protocol spec, and add the one real gap — a shortcut hint teaching `/swap`, which the standing hint rule requires. Verification is the targeted pytest file plus a live 429 run under Xvfb.

<!-- relay:entry 20260921T002449Z-xy author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260921T003050Z-5f author=owner kind=comment mode=discuss -->
close this card

<!-- relay:entry 20260921T003127Z-8w author=agent kind=question model=kimi-k3 pane=switchboard turn=86ba80072ab84ea794618c4f344844da/7daf59534cb64f8ebe7ecdfb72109e7d -->
Owner asked to close the card, but the plan's audit found one open item: the `/swap` shortcut hint (`model.swap.slash`) is not in `src/Pane.h` (searched, no match), and the plan's verification (RetryPreemption pytest + live 429 run under Xvfb) was never run. Close anyway and accept the missing hint, or hand the card to Execute to finish the hint and run the verification first? Recommendation: Execute first — the hint is a one-line change and the verification is targeted.

<!-- relay:entry 20260921T003135Z-kf author=agent kind=event model=kimi-k3 pane=switchboard turn=86ba80072ab84ea794618c4f344844da/7daf59534cb64f8ebe7ecdfb72109e7d -->
- ✦ agent moved this card · Planned → Discussing · Owner asked to close, but the plan's one open item (the /swap shortcut hint) is confirmed missing and verification never ran — confirming before closing.

<!-- relay:entry 20260921T003147Z-v8 author=agent kind=comment mode=discuss model=kimi-k3 turn=86ba80072ab84ea794618c4f344844da/7daf59534cb64f8ebe7ecdfb72109e7d -->
**One thing first on #DC4J:** the plan's audit found the retry preemption and `/swap` already in the tree, but two things were never done — the `/swap` shortcut hint (`model.swap.slash`, confirmed still missing from `src/Pane.h`) and the plan's verification (the `RetryPreemption` pytest + a live 429 run under Xvfb).

Close it anyway and drop the hint, or hand it to **Execute** to add the one-line hint and run the verification first? I recommend Execute — it's small. I've put this question on the card and moved it to Discussing (#DC4J).

<!-- relay:entry 20260921T005008Z-ex author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Discussing → Executing, session 70a224e1

<!-- relay:entry 20260921T005008Z-ey author=owner kind=progress pane=switchboard pane_token=70a224e1-0f04-41bc-aa56-9168c8aeb723 -->
Claimed (70a224e1) · working on it from a terminal pane

<!-- relay:entry 20260921T005107Z-ch author=agent kind=progress model=kimi-k3 pane=70a224e1 turn=bd7e6392197a478bbe52368622bc8e1b/80b924b44eaa4d1c9d0d0d95e042d841 -->
Executing. Git state: retry preemption landed in 7824689d (#DC4J), /swap in c7dccfa0. Audit + tests first; `src/Pane.h` (where the /swap hint goes) is currently held by the active `agnt-console` subagent (idle 0m), so the hint edit waits until that hold clears.

<!-- relay:entry 20260921T010116Z-1y author=agent kind=event model=kimi-k3 pane=70a224e1 turn=bd7e6392197a478bbe52368622bc8e1b/80b924b44eaa4d1c9d0d0d95e042d841 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["6dceb819"], "evidence": ["docs/qa_evidence/2026-09-21…; appended to `## Execution Summary`

<!-- relay:entry 20260921T010128Z-c4 author=agent kind=event model=kimi-k3 pane=70a224e1 turn=bd7e6392197a478bbe52368622bc8e1b/80b924b44eaa4d1c9d0d0d95e042d841 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260921T010155Z-sk author=agent kind=event model=kimi-k3 pane=70a224e1 turn=bd7e6392197a478bbe52368622bc8e1b/80b924b44eaa4d1c9d0d0d95e042d841 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260921T010228Z-d1 author=agent kind=event model=kimi-k3 pane=70a224e1 turn=bd7e6392197a478bbe52368622bc8e1b/80b924b44eaa4d1c9d0d0d95e042d841 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260921T010404Z-we author=agent kind=event model=kimi-k3 pane=70a224e1 turn=bd7e6392197a478bbe52368622bc8e1b/80b924b44eaa4d1c9d0d0d95e042d841 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260921T010437Z-sk author=agent kind=event model=kimi-k3 pane=70a224e1 turn=bd7e6392197a478bbe52368622bc8e1b/80b924b44eaa4d1c9d0d0d95e042d841 -->
- ✦ agent moved this card · Executing → Needs verification · Audit confirmed retry preemption (7824689d) and /swap (c7dccfa0) landed and match the protocol spec; the one gap — the model.swap.slash hint — added in 6dceb819. tests.test_model_switch 19/19, tests.test_provider 69/69, model ctests 4/4, build clean. QA checklist on the card covers the live 429 run. · evidence docs/qa_evidence/2026-09-21-dc4j-swap-hint/ · implemented_by kimi/kimi-k3
