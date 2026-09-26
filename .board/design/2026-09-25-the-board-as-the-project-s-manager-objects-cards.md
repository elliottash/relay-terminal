---
id: EA37
type: work
status: needs-verification
labels: [feature, design, board, switchboard, skills, research]
component: [gui, worker, board]
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: decision, primary: person, also: [ai-text], human: required, criteria: 'the design page answers which object kinds get a tab, how links are addressed and stored, and what changes on #9FX8 step 2; the owner''s seven decisions are recorded', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'owner, Relay conversation, 2026-09-25, while #9FX8 was executing'
links: {commits: [0157252fdc6f, b48afea250ef, 579c7d5cab51], evidence: [docs/PROJECT-BOARD-DESIGN.md, docs/research/project-board/a-single-project-object-models.md, docs/research/project-board/b-agent-era-project-state.md, docs/research/project-board/c-link-models-and-backlinks.md], github: null, plans: [], related: [9FX8, 1QKM, FVVY, V3R3, P2W8, 7BM4, G9ZD, Y0QQ, EE42, TBRH]}
---
# The Board as the project's manager: objects (cards, skills, memories, artifacts), a Live strip instead of a pane list, and computed links among them

## Issue
re the board, i think it could be a broader project manager: cards, pane list (to compare with sessions, maybe not needed), artifacts (maybe too redundant with files), skills, memories, and links among them.

do we have research already on project management that we can use, or should we send out more research explorers? you can look at docs/GLOBAL-PROJECT-BOARD-RESEARCH.md for relevant work that is at managing multiple projects, but we also need something for specific projects. thats closer to how trello / notion etc are used, but also a github repo.

do some deeper design and research as appropriate

## Plan
**Goal.** Decide what the per-project Board is made of beyond cards, and how its parts point at each other, so #9FX8's tab strip is the first three of a set. The design is `docs/PROJECT-BOARD-DESIGN.md`; the three research passes behind it are `docs/research/project-board/{a,b,c}-*.md` (single-project object models; agent-era project state; link models and backlinks). Landed together in one commit.

**Findings.**
- Existing research covered the portfolio layer (`docs/GLOBAL-PROJECT-BOARD-RESEARCH.md`, #V3R3) and the tooling hub (`docs/BOARD-TOOLING-RESEARCH.md`, #7BM4); the object vocabulary is #1QKM §4. What was missing was the layer *inside one project*: how a GitHub repository, Trello, Notion, Linear and the agent-era tools hold several object kinds and link them. Three explorers were sent for that gap; their reports are the sourced basis of the page.
- Relay's Board is already the GitHub-repository shape (a fixed set of kinds as tabs, one id syntax in prose, procedures as versioned files), not Trello's (one kind, attachments) or Notion's (databases with typed relations). Every agent-era vendor is migrating toward committed files for durable knowledge.
- Every file-based system stores forward links only and computes the reverse; stored two-way links drift across a boundary (Atlassian's own KB). GitHub's append-only cross-reference event is the one stored reverse that merges in git.
- The typed vocabulary that survives is blocks, parent, duplicate, related, plus Beads' discovered-from; larger vocabularies go unfilled.
- Memory cards already carry four-character ids, so `#ID` addresses them; no `memory:` prefix.

**The design in one paragraph.** A kind gets a tab only when a person acts on it as a set: Cards, Skills, Memories now; Artifacts once #FVVY's `runs.jsonl` exists; no Cases tab (agent-facing); no pane tab but a computed **Live** strip on Cards. One address form per kind (`#ID`, `skill:x`, `case:c-…`, `run:r-…#path`, `path@sha`, a sha; a pane is never a stored target). Forward links stay field-name-is-the-type on the source, with `duplicate_of`, `discovered_from` and `supersedes` added to work cards because each does something. The reverse is an uncommitted index rebuilt from the files on every refresh (`board_links`), plus one append-only `mentioned in` line in the target's thread — the `rg`-readable backlink. No graph view. Artifacts are the run outputs, not files: append-only versions with a Canonical pointer and the run's `dirty` flag. Memories get an expired section from `reviewed` against their `paths`' newest commit.

**Steps.**
1. Owner answers the seven questions in the thread (page §10).
2. #9FX8 step 2 absorbs phase 1: the strip accepts a fourth entry, the Memories tab has an expired section, the card and skill pages draw a Linked panel from what step 1 returns plus front matter. Noted on #9FX8's thread.
3. #FVVY adds `dirty` to the run row (noted on its thread) and keeps `run:<id>#<path>` as the artifact address.
4. Phase 2 (`board_links`, the thread line, the three new fields, `dangling_link`, filter prefixes) and phase 3 (Live strip) become their own cards once decisions 4–6 are in; phase 5 (Artifacts tab) after #FVVY lands.
5. `docs/BOARD-DESIGN.md` §4 and `docs/ARCHITECTURE.md` §10a get one paragraph each pointing at the page when phase 1 lands (with #9FX8's docs step).

**Risks.** The page's phase 1 asks #9FX8 for three small extras beyond its plan; if the implementing session has already built a fixed three-way control, the fourth entry is a follow-up rather than a rework. The `mentioned in` line writes to threads of cards the writer did not touch, which must stay a worker-side append under the board lock. Research claims marked *unverified* in the passes are not relied on here.

**Verify.** A person reads the page and the seven questions and records decisions on this card (`human: required`); the three research files exist with their comparison tables; #9FX8 and #FVVY threads carry the notes.

## Decisions
Owner, 2026-09-25 (thread): 1/3/4/5/6/7 per the recommendation; 2 split to its own card.
1. **Artifacts as a fourth tab, gated on #FVVY's runs ledger — yes.** The tab card (phase 5) files after #FVVY lands.
2. **Cases — split to #Y0QQ** (deferred): undrawn as a set for now; rows on the skill page and card page only. Revisit on #FVVY's non-software pilot.
3. **Computed Live strip on the Cards tab instead of a pane tab — yes** (phase 3, #TBRH, blocked on #9FX8); the Projects page keeps attach/reveal/filter.
4. **`duplicate_of`, `discovered_from`, `supersedes` on work cards — yes**, in phase 2 (#EE42); nothing beyond the four-plus-one vocabulary.
5. **One append-only `mentioned in #X · date · who` line in the target's thread per cross-reference — yes**; the in-memory index wins on disagreement.
6. **Phase 2 filed now — yes** (#EE42), so #9FX8's Linked panels are built against the `board_links` request shape.
7. **No project home page — not now**; tab counts and the expired-memories section carry it. Revisit at four tabs.

## Human QA
1. Do the recorded decisions stand as made — 1/3/4/5/6/7 per the recommendation, cases split to #Y0QQ — completing this design card?
   Answer: "can we split off cases as a separate card? i want to do everything else first. 1 / 3 / 4 / 5 / 6 / 7 follow your rec" — owner, this card's thread, 2026-09-25, recorded verbatim.
