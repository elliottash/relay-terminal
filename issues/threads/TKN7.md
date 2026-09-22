<!-- relay:entry 20260921T105230Z-7q author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 -->
- ✦ agent created this card in Discussing · issues/changes/2026-09-21-adding-a-model-by-hand-to-a-tier-list-starts-it.md

<!-- relay:entry 20260921T105236Z-qd author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 -->
- ✦ agent updated this card · replaced `## Issue`

<!-- relay:entry 20260921T105236Z-qe author=agent kind=rewrite model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
issue: in the "main models" option, when i added gpt 5.6 sol, it defaulted effort to ultra reasoning, it should be medium. review more broadly how those defaults are set. is there a provider default we can use?
```

</details>

<details><summary>after</summary>

```
issue: in the "main models" option, when i added gpt 5.6 sol, it defaulted effort to ultra reasoning, it should be medium. review more broadly how those defaults are set. is there a provider default we can use?
```

</details>

<!-- relay:entry 20260921T105252Z-kp author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 -->
- ✦ agent updated this card · appended to `## Planning notes`

<!-- relay:entry 20260921T105302Z-93 author=agent kind=question model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 -->
When a model is added to a list by hand (`+ add a model…`), what level should the new entry start at?\n\n1. **The provider's own default** *(recommended)* — for Main, codex's `default_reasoning_level`: **low** for gpt-5.6-sol, medium for the other GPT-5.6 models; High still starts at the top level, Flash/Lite at the lowest. This is exactly what the `defaults` buttons already do (`tier_list_defaults`), and it is the \"provider default\" the request asks about — codex sends it per model as `default_effort`, Relay just never reads it in the GUI.\n2. **Medium for Main** — every hand-added Main entry starts at medium, whatever the provider's own default says (High top, Flash/Lite lowest).\n3. **Unset** — the new row starts on `default` (the option the row's own menu already offers) and the model's own default applies at run time; nothing is guessed at add time.\n\nOption 1 and the request's \"it should be medium\" disagree for gpt-5.6-sol alone: codex's own default for it is **low**. If medium is what is wanted regardless of the provider, option 2.

<!-- relay:entry 20260921T105302Z-fq author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260921T105710Z-42 author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 -->
- ✦ agent updated this card · appended to `## Decisions`

<!-- relay:entry 20260921T105714Z-hy author=agent kind=decision model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 -->
Owner, asked in the terminal what a hand-added entry should start at: "The provider's own default" — Main at the provider's own default level (codex's `default_reasoning_level`: low for gpt-5.6-sol, medium for the rest of the GPT-5.6 family), High at the level the `defaults` buttons use, Flash and Lite at the lowest.

<!-- relay:entry 20260921T105714Z-hz author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 -->
- ✦ agent claimed this card · assignee agent, Discussing → Executing, implemented_by deepseek/deepseek-v4.1-flash, session 5b274d0b

<!-- relay:entry 20260921T105714Z-i0 author=agent kind=progress model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 pane_token=5b274d0b-c776-4a74-8407-0ac72af2f124 -->
Claimed (5b274d0b) · working on it from a terminal pane

Give every model row a per-list starting level computed once in the backend, parse it in the GUI, and use it when a model is added to a list by hand.

<!-- relay:entry 20260921T105728Z-yc author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=5b274d0b turn=a70a56a8e1204af8b99a3dc05f8e8a95/1e61c56c387d4c1d8a93547343fe72a1 -->
- ✦ agent updated this card · waiting_on: owner → (unset); replaced `## Plan`

<!-- relay:entry 20260921T110427Z-9j author=owner kind=event pane=5b274d0b -->
Released (5b274d0b) · the pane closed

<!-- relay:entry 20260921T111802Z-yq author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=d694b4a2 turn=a70a56a8e1204af8b99a3dc05f8e8a95/75e33fd564bc43208cbfd4073339a1bc -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260921T111819Z-kd author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=d694b4a2 turn=a70a56a8e1204af8b99a3dc05f8e8a95/75e33fd564bc43208cbfd4073339a1bc -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260921T111830Z-te author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=d694b4a2 turn=a70a56a8e1204af8b99a3dc05f8e8a95/75e33fd564bc43208cbfd4073339a1bc -->
- ✦ agent updated this card · replaced `## QA checklist`

<!-- relay:entry 20260921T111836Z-p2 author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=d694b4a2 turn=a70a56a8e1204af8b99a3dc05f8e8a95/75e33fd564bc43208cbfd4073339a1bc -->
- ✦ agent updated this card · tasks: 6/7 done

<!-- relay:entry 20260921T112825Z-7a author=agent kind=event model=z-ai/glm-5.3-flashx pane=d694b4a2 turn=a70a56a8e1204af8b99a3dc05f8e8a95/bd284f2def044242856ee388dd2a8c6c -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260921T112832Z-yw author=agent kind=note model=z-ai/glm-5.3-flashx pane=d694b4a2 turn=a70a56a8e1204af8b99a3dc05f8e8a95/bd284f2def044242856ee388dd2a8c6c -->
Two things about how this landed, so nobody has to rediscover them:\n\n1. **`docs/ARCHITECTURE.md` is not in the commit.** I wrote the sentence for its five-lists paragraph, but two other sessions (ft77-copybtn, fable-prof) hold uncommitted edits *inside that same paragraph*, so the hunk could not be applied to main's moved tip (land.py: \"merge conflict\"). The clause is on this card and in the protocol doc; put it back in ARCHITECTURE.md when one of those sessions lands.\n2. **The commit message lost three words.** I passed it through a shell double-quoted string, so the backticked `ultra`, `low`, `defaults` and `tier_effort` were command-substituted away — 14efb78e reads \"landed in main at  (a delegation mode) rather than its own default .\". The words are here and in the evidence NOTES.md. I did not amend: sessions were landing on main around it and a rewrite of the tip races with land.py's swap.\n\nAlso of note: land.py's build gate compiled the *exact* landed tree (main + my hunks, without the other sessions' uncommitted edits) before swapping — `verify: the exact tree builds` — which is a stronger check than the working-tree build, since the working tree carries other sessions' code."

<!-- relay:entry 20260921T112838Z-c7 author=agent kind=event model=z-ai/glm-5.3-flashx pane=d694b4a2 turn=a70a56a8e1204af8b99a3dc05f8e8a95/bd284f2def044242856ee388dd2a8c6c -->
- ✦ agent moved this card · unchanged · Implemented, tested on both sides, live-checked against the real codex, and landed as 14efb78e; the Options page itself still wants a verifier's eyes (QA checklist). · evidence docs/qa_evidence/2026-09-21-hand-added-model-start-effort/ · implemented_by glm/glm-5.3-flashx

<!-- relay:entry 20260921T112846Z-8s author=agent kind=event model=z-ai/glm-5.3-flashx pane=d694b4a2 turn=a70a56a8e1204af8b99a3dc05f8e8a95/bd284f2def044242856ee388dd2a8c6c -->
- ✦ agent moved this card · Executing → Needs verification · Landed as 14efb78e and verified by tests; the page itself still wants a verifier's eyes (QA checklist). · evidence docs/qa_evidence/2026-09-21-hand-added-model-start-effort/ · implemented_by glm/glm-5.3-flashx
