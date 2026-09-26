<!-- relay:entry 20260923T225409Z-hz author=agent kind=event model=gpt-6-sol pane=e0e12343 turn=57fbc290e77c491b8c6cfbfb7fffa484/c4404e71f9984070b9f19b193bed7727 -->
- ✦ agent created this card in Planned · issues/features/2026-09-23-build-the-tex-to-pdf-document-workspace.md

<!-- relay:entry 20260923T225432Z-j7 author=agent kind=event model=gpt-6-sol pane=e0e12343 turn=57fbc290e77c491b8c6cfbfb7fffa484/c4404e71f9984070b9f19b193bed7727 -->
- ✦ agent updated this card · replaced `## Discussion points`

<!-- relay:entry 20260923T225432Z-j8 author=agent kind=event model=gpt-6-sol pane=e0e12343 turn=57fbc290e77c491b8c6cfbfb7fffa484/c4404e71f9984070b9f19b193bed7727 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260923T225432Z-j9 author=agent kind=event model=gpt-6-sol pane=e0e12343 turn=57fbc290e77c491b8c6cfbfb7fffa484/c4404e71f9984070b9f19b193bed7727 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T225433Z-42 author=agent kind=event model=gpt-6-sol pane=e0e12343 turn=57fbc290e77c491b8c6cfbfb7fffa484/c4404e71f9984070b9f19b193bed7727 -->
- ✦ agent updated this card · tasks: 0/6 done

<!-- relay:entry 20260923T232639Z-yn author=agent kind=event model=claude-opus-5-5 pane=086dc38a turn=57fbc290e77c491b8c6cfbfb7fffa484/dc6d44eba1044b9bb3a793265e09c415 -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by anthropic/claude-opus-5-5 via claude-code, session 086dc38a

<!-- relay:entry 20260923T232639Z-yo author=agent kind=progress model=claude-opus-5-5 pane=086dc38a turn=57fbc290e77c491b8c6cfbfb7fffa484/dc6d44eba1044b9bb3a793265e09c415 pane_token=086dc38a-db43-4ebd-9edc-99ef969a30f8 -->
Claimed (086dc38a) · working on it from a terminal pane

Wave 1 via subagent: backend latexmk runner with generation ids, log diagnostics parser, SyncTeX wrapper, TeX fixture.

<!-- relay:entry 20260924T000454Z-4s author=agent kind=question model=claude-opus-5-5 pane=086dc38a turn=57fbc290e77c491b8c6cfbfb7fffa484/dc6d44eba1044b9bb3a793265e09c415 -->
Wave 1 landed in 01e4e216: `tex_build.py` (latexmk builder with atomic generations, log/blg diagnostics, SyncTeX both ways, a Transport interface for remote builds) plus a fixture; 34 tests pass. Two decisions: (1) latexmk runs a project `latexmkrc` as Perl, so builds use `-norc` by default and `trust_project_rc=True` opts in. Recommendation: keep it off by default and offer it in the same explicit-enable step as project plugins (#C0Q8). (2) Where remote builds write output. Recommendation: `~/.cache/relay/tex/<hash>` on the host, never inside the project. The xelatex path is untested because it isn't installed here.

<!-- relay:entry 20260924T025148Z-bk author=owner kind=event pane=086dc38a -->
Released (086dc38a) · the pane closed

<!-- relay:entry 20260926T043113Z-a4 author=claude-code kind=comment -->
### Claude Code · 2026-09-26 04:31
I audited this card, which was stale (its executing claim was released on 2026-09-24), and rewrote `## Plan` and `## Tasks` to match the code. **Verdict: still valuable, about half landed.** Landed: the builder `01e4e216` (latexmk, generations, diagnostics, SyncTeX, `-norc` by default, local transport only); the worker runtime and `tex_*` tools `13909113`; the group model with presets and the "Open externally" fallback, #E85D `4ad5fe87`; the chain, #R660 code in `def2cf0b`; the docked agent with `/build`, #PBZ4 `b70c33bf`; Qt PDF in Debian, #9Y7X `f9c7d590`. Missing: the GUI never consumes the worker's `tex_status` (`rg tex_status src/` is empty), and it cannot start a build, so save does not build and only the agent's `tex_build` does. Also missing: SyncTeX in the GUI, diagnostic rows, included files joining the group, and SSH remote builds. The plan now owns only that TeX-specific wiring. Group, chain, agent, buffer and PDF packaging stay with #E85D, #R660, #PBZ4, #F8R7, #9Y7X and #7WGJ. Moved Executing → Discussing (waiting on owner) for the questions below. The 2026-09-24 questions were never answered.

<!-- relay:entry 20260926T043113Z-a5 author=claude-code kind=question -->
### Claude Code · 2026-09-26 04:31
Questions before delivery:
1. Should remote SSH builds (t:en) split into their own follow-up card, so the local workspace ships first? *Recommendation: yes, split. If they stay here, output goes to `~/.cache/relay/tex/<hash>` on the host, never inside the project.*
2. Should a project's `latexmkrc` (Perl) keep running with `-norc` by default, so it only runs after the project's explicit plugin enable (#C0Q8)? *Recommendation: yes.*
3. Are review comments and annotations on the PDF (#MEPR decision 6) outside this card, so it covers only source, build, preview and SyncTeX? *Recommendation: yes, out of scope. File a separate card if you want them.*

<!-- relay:entry 20260926T050057Z-0k author=agent kind=decision model=gpt-6-sol pane=dc713c52 turn=54948a7060344df2abef877a514ad9bf/77d64610668645e0819475f4bc9fcb8b -->
Owner: "yes to all." Decisions: split SSH TeX builds into a follow-up card; ignore project latexmkrc unless project plugins are explicitly enabled; PDF review comments and annotations are outside this card.

<!-- relay:entry 20260926T050149Z-bq author=agent kind=event model=gpt-6-sol pane=dc713c52 turn=54948a7060344df2abef877a514ad9bf/77d64610668645e0819475f4bc9fcb8b -->
- ✦ agent updated this card · waiting_on: owner → (unset); appended to `## Decisions`

<!-- relay:entry 20260926T050200Z-p6 author=agent kind=event model=gpt-6-sol pane=dc713c52 turn=54948a7060344df2abef877a514ad9bf/77d64610668645e0819475f4bc9fcb8b -->
- ✦ agent moved this card · Discussing → Planned · Owner answered the 2026-09-26 scope questions; refreshed plan is ready for delivery or its listed prerequisite work.

<!-- relay:entry 20260926T050240Z-hd author=agent kind=event mention=CWDZ model=gpt-6-sol pane=dc713c52 turn=54948a7060344df2abef877a514ad9bf/77d64610668645e0819475f4bc9fcb8b -->
mentioned in #CWDZ · 2026-09-26 · agent

<!-- relay:entry 20260926T050257Z-43 author=agent kind=event model=gpt-6-sol pane=dc713c52 turn=54948a7060344df2abef877a514ad9bf/77d64610668645e0819475f4bc9fcb8b -->
- ✦ agent updated this card · tasks: 3/8 done

<!-- relay:entry 20260926T050636Z-9b author=agent kind=event mention=SJ00 model=gpt-6-sol pane=dc713c52 turn=54948a7060344df2abef877a514ad9bf/77d64610668645e0819475f4bc9fcb8b -->
mentioned in #SJ00 · 2026-09-26 · agent

<!-- relay:entry 20260926T051436Z-cy author=agent kind=event model=gpt-6-sol pane=dc713c52 turn=54948a7060344df2abef877a514ad9bf/77d64610668645e0819475f4bc9fcb8b -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260926T051607Z-e0 author=agent kind=event model=gpt-6-sol pane=dc713c52 turn=54948a7060344df2abef877a514ad9bf/77d64610668645e0819475f4bc9fcb8b -->
- ✦ agent updated this card · blocked_by: (unset) → ["E85D"]
