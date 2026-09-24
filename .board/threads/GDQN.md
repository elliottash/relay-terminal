<!-- relay:entry 20260919T233259Z-ks author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260919T233259Z-kt author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260919T234747Z-7b author=agent kind=event model=glm-5.3 pane=ca8c73f6 turn=81c1314277944122976845ef2c2ad1e4/45608b52a6c642e5929418ca47a023a3 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["f878b6a", "a17a262"], "evidence": [], "related": [], … → {"plans": [], "commits": ["f878b6a", "a17a262", "ae206932", "ac7abe64", "9e628ce…; appended to `## QA checklist`; replaced `## Where it stands (2026-09-19 board sweep)`

<!-- relay:entry 20260919T234747Z-w7 author=agent kind=rewrite model=glm-5.3 pane=ca8c73f6 turn=81c1314277944122976845ef2c2ad1e4/45608b52a6c642e5929418ca47a023a3 -->
- ✦ rewrote ## Where it stands (2026-09-19 board sweep)

<details><summary>before</summary>

```
Not `ready` any more — half of it is built. `f878b6a` landed the two-way sync as a **headless
engine**: `backend/relay_core/forge_sync.py` and `forge_github.py`, 1150 lines of tests against an
in-process fake GitHub (`tests/fake_github.py`), with the message shapes in `docs/GITHUB-SYNC.md`.
`a17a262` then fixed the lost writes a back-end review of it found.

What the acceptance still needs is the half that commit's own message names: **it is not wired to
the worker**, so nothing in Relay drives it yet and no conflict is surfaced to a person. The wiring
note is at the end of `docs/GITHUB-SYNC.md`. #ZKR0 is the Switchboard-side surface for the same
work and is still open.
```

</details>

<details><summary>after</summary>

```
Built, wired and walked end to end; the acceptance run itself found and fixed one lost write.

- `f878b6a` (→ `ae206932` after the AGPL relicense rewrote the hashes): the engine,
  `backend/relay_core/forge_sync.py` + `forge_github.py`, provider-neutral, guarded for private
  cards, tested against the in-process `tests/fake_github.py`.
- `a17a262` (→ `ac7abe64`): the back-end review's fixes for the lost writes it found.
- `9e628ce3` (the 09-18/19 batch): **the worker wiring** the sweep said was missing —
  `forge_sync_plan` / `forge_sync_run` on `board_protocol.BoardCommands` (protocol 19.14), the
  thread, the busy guards, the scrubbed one-terminal-event errors, and the desktop-only marks in
  `remote/wire.py`.
- `1377a65a`: the acceptance walked through the worker messages found the listing trap — after a
  conflict leaves a card's baseline behind, the next quiet run read the unchanged (304) listing as
  "the issue equals the baseline" and pushed the local edit over the remote one. A per-card
  `current` marker in the state now gates that: a card whose baseline may lag gets its issue read
  again before anything is pushed. `ForgeSyncAcceptanceTests` walks both directions (body,
  comments, status, labels) and the whole conflict lifecycle through `dispatch`.

The person-facing surface (Sync action, plan-before-first-run, the conflict shown on the card with
keep-mine / keep-GitHub's) is #ZKR0, which follows this card. Nothing here points a real board at
a real repository: that is the owner's decision (`github: {repo: …}` in `board.yaml`).
```

</details>

<!-- relay:entry 20260919T234753Z-5m author=agent kind=progress model=glm-5.3 pane=ca8c73f6 turn=81c1314277944122976845ef2c2ad1e4/45608b52a6c642e5929418ca47a023a3 -->
Execute · `1377a65a`: the acceptance walked end to end through the worker messages (both directions: body, comments, status, labels; the link stamped in front matter; `board_changed` after a pull) — and the walk found a real lost write. After a conflict leaves a card's baseline behind, the next quiet sync read the 304 listing as "the issue equals the baseline" and pushed the local edit over the remote one. Fixed with a per-card `current` marker in the state: a card whose baseline may lag (conflict, errored plan or apply, mid-card rate limit) has its issue read once more — a real read — before anything is pushed, so the conflict is re-reported instead of resolved by accident. Engine regression test + `ForgeSyncAcceptanceTests` in `tests/test_board_protocol.py`; 147 + 145 tests green; evidence in `docs/qa_evidence/2026-09-19-github-issues-sync/`. `links.commits` now also carries the post-relicense hashes of the earlier commits (`ae206932`, `ac7abe64`, `9e628ce3` — the same work as `f878b6a`/`a17a262`, whose originals the AGPL relicense left dangling). The GUI surface is #ZKR0.

<!-- relay:entry 20260919T234757Z-n4 author=agent kind=event model=glm-5.3 pane=ca8c73f6 turn=81c1314277944122976845ef2c2ad1e4/45608b52a6c642e5929418ca47a023a3 -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · The worker half is built, wired and now proven by an acceptance-level walk through the worker messages; the walk's own finding (the post-conflict lost write) is fixed in 1377a65a with a regression test. The person-facing surface stays on #ZKR0. · evidence docs/qa_evidence/2026-09-19-github-issues-sync/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260919T234821Z-zf author=agent kind=event model=glm-5.3 pane=ca8c73f6 turn=81c1314277944122976845ef2c2ad1e4/45608b52a6c642e5929418ca47a023a3 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["f878b6a", "a17a262", "ae206932", "ac7abe64", "9e628ce… → {"commits": ["f878b6a", "a17a262", "ae206932", "ac7abe64", "9e628ce3", "1377a65a…
