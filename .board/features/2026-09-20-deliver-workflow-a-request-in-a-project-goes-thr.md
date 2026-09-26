---
id: R9G7
type: work
status: needs-verification
labels: [feature, switchboard, agent]
assignee: agent
rank: i1
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [a7746e2dd130, d97bcf24e58b, 5fc80e3359e5, 6a8575f41995, a5e0dfbb7ab6, be5d9ec1078a, a1ec6290c1d4, baae6a63f969, d52916e47ada, ca6ebaadf4c6, 7bd892f22c0e, c82f171d963b, 4bedd4f195b9, fec0fd71fc8f, 61296f15eda9, e54d1e5fa490, cd8e7dfaba0f, e7ce0f13b8c2, 9d31b6aaf977, bbcf3b438801], evidence: [docs/qa_evidence/2026-09-20-deliver-claim], related: [], github: null}
---
# Deliver workflow: a request in a project goes through a card; sessions claim cards visibly; /deliver

## Issue
what do you think about, when you are in a project, the system prompt for the agent instructs it to use the switchboard workflow.

so when the user asks you to do something, you first check if its already done, then look for related cards, if so, attach to them, otherwise create one. then plan (if needed), then execute.

should this be a "deliver issue" skill for example.

re a session claiming a card, lets also implement taht more directly, where in the swtichboard, you see the linked session id that links to the session pane. (lets implement that as part of this job). then agents know if another agent already claimed it and can coordinate easily.

yes, tell claude md and agents md to read the relay system prompt

so if i want to activate the workflow manually, should it be /deliver?

## Decisions
- Rule in the always-on Switchboard policy (two or three lines); procedure in a bundled skill `deliver`, so `/deliver <request>` runs it by hand (owner, 2026-09-20).
- A pane claims a card with one tool, `board_claim`: front matter `session: <pane token>`, status executing, assignee agent, a progress entry linking to the pane. The Switchboard shows the session on the card and the link reveals the pane; another agent sees the claim in `board_read`.
- Guest agents (claude, codex) reach the same rules through the project's CLAUDE.md and AGENTS.md, which point at a generated `<board>/POLICY.md` (owner: "tell claude md and agents md to read the relay system prompt").

## Tasks

- [x] Backend: `board_claim`, pane token on `configure`, policy lines, bundled `deliver` skill, tests, protocol §19
- [x] GUI: `session` chip on card rows and detail, link reveals the pane, Execute records the session, Pane sends its token
- [x] Guest path: generated POLICY.md, CLAUDE.md/AGENTS.md paragraph at board init, applied to this repo

## Decisions (2026-09-20, later)
- "auto-release on "done" and on "closed"": a claim is dropped when the card moves to done/dropped (same write) and when its pane closes (the worker's shutdown branch) or moves to another project. Landed cd8e7dfa.
- Three tiers, owner "go": small (no card), medium (claimed, closed by the agent), large (full workflow). Policy v3, e54d1e5f.

## QA checklist
- [ ] Close a claimed card (done or dropped): its `session` is gone and the thread line says "session xxxxxxxx released".
- [ ] Close the pane that holds a claimed executing card: within about a second the Switchboard row loses the chip and the thread has "Released (xxxxxxxx) · the pane closed".
- [ ] A one-line fix asked in a pane gets no card; a two-turn change gets a card the agent closes to done itself; `/deliver` on the same request lands in needs-verification.

- [ ] In a project with a board, ask a pane agent for a small code change: it checks the code and `board_list` first, then `board_claim`s (or creates and claims) a card before editing, and the reply names `#ID`.
- [ ] Ask a question ("what does X do?"): no card is created or claimed.
- [ ] `/deliver <request>` in the composer previews as `SKILL · /deliver` and the agent follows the six steps.
- [ ] After a claim, the card row shows `⧉ <8 chars>` in the link colour and the card page shows `session ⧉ <8 chars>`; clicking it reveals the pane.
- [ ] Close that pane: the chip reads `⧉ <8 chars> closed`, muted, no link.
- [ ] From the Switchboard, press Execute on a card: one `board_claim` write (thread entry `Claimed (xxxxxxxx) · …`), and the note "Claimed #ID · Execute".
- [ ] In a second pane, ask the agent to work the same card: `board_claim` is refused with `board_claimed_elsewhere`; the agent comments instead of taking over.
- [ ] Initialize a board in a fresh project that has only a CLAUDE.md: `.switchboard/POLICY.md` exists, CLAUDE.md ends with the marked block, AGENTS.md was created starting with `@CLAUDE.md`, and Relay's Instructions dialog still loads CLAUDE.md's content.
- [ ] Start `claude` in a pane of that project: it reads the block, and POLICY.md tells it how to file, claim and move a card by editing files.
