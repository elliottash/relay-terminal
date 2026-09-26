<!-- relay:entry 20260925T014222Z-p9 author=agent kind=event model=glm-5.3 pane=55c67355 turn=ac598e144c144e34a442ba28dd6ed4c3/cf97cb837f124a6ea024b5c15109cb92 -->
- ✦ agent created this card in Planned · .board/changes/2026-09-24-try-it-hands-the-person-a-stale-build-relay-the.md

<!-- relay:entry 20260925T014250Z-r5 author=agent kind=event model=glm-5.3 pane=55c67355 turn=ac598e144c144e34a442ba28dd6ed4c3/cf97cb837f124a6ea024b5c15109cb92 -->
- ✦ agent updated this card · labels: ["bug", "build", "qa"] → ["bug", "build", "qa", "try-it"]; links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"related": ["SZHQ", "XQ8F"]}; verify: (unset) → {"artifact": "code", "primary": "script", "also": ["probe"], "human": "none", "c…; replaced `## ## Issue`

<!-- relay:entry 20260925T014258Z-42 author=agent kind=event model=glm-5.3 pane=55c67355 turn=ac598e144c144e34a442ba28dd6ed4c3/cf97cb837f124a6ea024b5c15109cb92 -->
- ✦ agent updated this card · replaced `## ## Done means`

<!-- relay:entry 20260925T040132Z-46 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T040132Z-47 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Planning · a Plan turn started

<!-- relay:entry 20260925T040615Z-ep author=agent kind=event model=glm-5.3 pane=switchboard turn=d7eadc359fbd65d04a09f34499685ce0/9f95ae22cc7447f8a29bf28bbb1c42c2 -->
- ✦ agent updated this card · replaced `## ## Done means`

<!-- relay:entry 20260925T040615Z-wb author=agent kind=rewrite model=glm-5.3 pane=switchboard turn=d7eadc359fbd65d04a09f34499685ce0/9f95ae22cc7447f8a29bf28bbb1c42c2 -->
- ✦ rewrote ## ## Done means

<details><summary>before</summary>

```
- `relay-qa-run` and the Try-it staging both build through `scripts/relay-build` and check the binary holds the change before a person or a QA run sees it.
- A stale binary produces a named failure ("binary predates the change", with the binary's build id and the commit) instead of a silent wrong result.
- A test builds a stale binary on purpose and asserts the failure.
```

</details>

<details><summary>after</summary>

```
- A Try-it run and a `relay-qa-run` probe hand over only a binary proven to hold the landed change: both build through `scripts/relay-build` and pass `relay-build --check`/`--check-only` with a marker from the change before a person or QA sees anything.
- A stale binary produces the named failure `binary predates the change`, with the binary's build id and the commit, and the hand-off stops there — never a silently wrong result presented as the change.
- A test builds a stale binary on purpose (fake `relay.build-id`, marker absent) and asserts that named failure in both `relay-build` and `relay-qa-run`; `tests/test_tryit_protocol.py` asserts the Try-it prompt carries the rule.
- Recognising failure: a person says "the feature is missing" or a QA probe contradicts the landed change, and `build/relay.build-id` turns out to predate the commit — exactly what these checks must make impossible to miss.
```

</details>

<!-- relay:entry 20260925T040631Z-6v author=agent kind=event model=glm-5.3 pane=switchboard turn=d7eadc359fbd65d04a09f34499685ce0/9f95ae22cc7447f8a29bf28bbb1c42c2 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T040646Z-gc author=agent kind=comment mode=plan model=glm-5.3 turn=d7eadc359fbd65d04a09f34499685ce0/9f95ae22cc7447f8a29bf28bbb1c42c2 -->
I'll read the card and the related cards, then find the code involved.

Card and related cards read. Now let me find the code: the Try-it stager, `relay-qa-run`, `scripts/relay-build`, and `scripts/build-id.py`.



`relay-qa-run` runs whatever command it's given — it doesn't build at all. `build-id.py` stamps `relay.build-id` beside the binary at link time. Now the key file, `scripts/relay-build`, and the Try-it stager.

The protocol lives in `backend/relay_core/tryit_protocol.py` — that's the Try-it staging prompt an agent follows. Let me read it.

The brief the staging agent follows is in the middle of that file. Let me read it.

Now the brief itself — `tryit_prompt` and the "For the app" instructions the staging agent gets.



The brief text itself is `backend/relay_core/board_tryit_brief.md` — let me read it.

The brief names `build/relay` with no build/staleness requirement. Now the two scripts' details and existing tests.

Now let me re-read `scripts/build-id.py` and the `--check` part of `relay-build`, and find the existing tests.

Now the exact `--check` behaviour in `relay-build`, and how `test_event_report.py` invokes `relay-qa-run`.

I have the two scripts. Now the test seams and the verify brief, plus a quick re-read of the two related cards' front matter for accurate cross-references.

The harness copies the wrapper into a throwaway project — a fake-stale-binary test fits it. Let me see the existing `--check` test and `card_brief`'s file list.

Now `card_brief` around line 1015 to see the brief file map.







No verify brief file — the Try-it brief is the one. Last read: what `tests/test_tryit_protocol.py` pins, so the plan names the exact assertions to update.

I have the full picture. One last check — where `build-id.py` is wired in, and the exact head of `relay-qa-run`:

All findings are in. Writing `## Done means` first, then the plan.

Need the card's current hash — re-reading.



Done means written. Now the plan.

**Done:** #J6MF now carries its `## Done means` (a stale binary can never reach a person or QA silently — named failure, build id, commit) and a full `## Plan`.

The plan closes the gap at both hand-off paths: `relay-qa-run` gains `--check`/`--check-binary` and builds through `scripts/relay-build` (which gains `--check-only` and the `binary predates the change` message with build id + HEAD), and the Try-it prompt/brief v2 require the same build-and-prove step before staging. Nothing needs a decision from you — the only open question (whether Windows hand-offs matter yet) is noted in Risks as out of scope. Ready for **Run**.

<!-- relay:entry 20260925T040646Z-gd author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T215459Z-c0 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Running, session 6b643199

<!-- relay:entry 20260925T215459Z-n4 author=owner kind=progress pane=switchboard pane_token=6b643199-f90f-42f0-9d2c-29a59f627ad0 -->
Claimed (6b643199) · working on it from a terminal pane

<!-- relay:entry 20260925T220249Z-32 author=agent kind=event model=claude-opus-5-5 pane=6b643199 turn=b98d3dc113db4c91ae2f899a765042b5/746a94bcb9694edbbd45ed88e983304b -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260925T220255Z-vc author=agent kind=evidence model=claude-opus-5-5 pane=6b643199 turn=b98d3dc113db4c91ae2f899a765042b5/746a94bcb9694edbbd45ed88e983304b -->
Landed in 6b8f4fea. Evidence: docs/qa_evidence/2026-09-24-j6mf/README.md.

Live against this checkout's build/relay:
- `relay-build --check-only --check definitely-absent-marker-j6mf` printed `binary predates the change: …/build/relay does not contain '…' (build id 2026-09-25.17H.02; HEAD 98bca3a5 …)` and exited 5.
- `relay-qa-run --check definitely-absent-marker-j6mf ./build/relay --version` first rebuilt build/relay through relay-build (210s; build id went 17H.02 → 18H.01), then printed the named failure and `not running ./build/relay`, and exited 5.
- `--check-only --check 'The guest agent'` (a literal the source has) passed with exit 0.
- A later relay-qa-run found the shared tree broken by another session's half-written tests/pane_waits.h. It printed `build failed; nothing was run` and exited 4.

Found along the way: relay-build's usage example `--check "Fold thinking"` no longer matches the source. I replaced it with a placeholder. The CLAUDE.md example is not mine to edit, so I left it.

Scope notes: tryit_protocol.py also had another session's uncommitted `_run_dir`/scratch edit. land.py left it out of this commit. No evals pinned the v1 brief text.

<!-- relay:entry 20260925T220256Z-e6 author=agent kind=event model=claude-opus-5-5 pane=6b643199 turn=b98d3dc113db4c91ae2f899a765042b5/746a94bcb9694edbbd45ed88e983304b -->
- ✦ agent moved this card · Running → Needs verification · Landed 6b8f4fea: relay-build --check-only + named stale failure, relay-qa-run build-and-check gate, Try-it prompt/brief v2; 68 targeted tests pass · evidence docs/qa_evidence/2026-09-24-j6mf/ · implemented_by anthropic/claude-opus-5-5 via claude-code
