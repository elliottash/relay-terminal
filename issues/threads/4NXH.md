<!-- relay:entry 20260920T125058Z-kx author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=switchboard turn=9f3c40626beb405aa9f5fccedd3f5441/ac7af8a634384b0d866c3d1cce0c26a9 -->
- ✦ agent created this card in Discussing · issues/changes/2026-09-20-a-guest-session-codex-claude-code-cannot-use-the.md

<!-- relay:entry 20260920T125110Z-zh author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=switchboard turn=9f3c40626beb405aa9f5fccedd3f5441/ac7af8a634384b0d866c3d1cce0c26a9 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; appended to `## Findings`

<!-- relay:entry 20260920T125123Z-c0 author=agent kind=question model=deepseek/deepseek-v4.1-flash pane=switchboard turn=9f3c40626beb405aa9f5fccedd3f5441/ac7af8a634384b0d866c3d1cce0c26a9 -->
1. **How should a guest reach the board — give it the tools, or keep it tool-less and let Relay write?**
   (a) Serve Relay's board tools to the guest harness (the harness configures the guest with an MCP surface; the same `board_tools.py` guardrails and autonomy apply), so a verifier can call `board_read`, `board_comment` and `board_move_card` itself — the approach that matches how the briefs already read.
   (b) Keep guests without tools: rewrite the Execute/Verify briefs so a guest never needs a board call (it reports in its reply, and Relay or the Switchboard agent does the writes).
   *Recommendation: (a), read-only plus the writes must have (`board_read`, `board_comment`, `board_move_card`, `board_update_card`) — it is the only route that lets a guest verifier close its own card, and it reuses the guardrails that already exist. (b) is smaller but leaves every guest turn's board work to be hand-relayed.*

2. **Should this land inside #SSRQ (MCP server support, deferred) or as its own guest-only surface?**
   *Recommendation: its own surface — a guest is a Relay-owned harness, not a user-configured MCP server, and #SSRQ is about trusting third-party servers. Say so and I move this card out of `discussing`.*

<!-- relay:entry 20260921T111652Z-cb author=codex kind=comment -->
### Codex · 2026-09-21 11:16
Owner report:
> potential bug, codex just told me it had no board_ tools
>
> session 85d4ba99d2a54dcdb8f6332ff93b4925
>
> "The old behavior is still present. Card-ID copying also shares the label-copy
> helper, so I’ll separate those paths to preserve #ID copies while labels copy as
> label:name. I’ll use the policy’s file-edit fallback for card updates because this
> session has no board_* tools."

Confirmed against the saved Relay session: `guest: codex`, `model: gpt-6-astra`, guest session `01a0c3ac-2e03-77f2-bc32-f1828a6f5159`. `HarnessProvider.complete` in `backend/relay_core/guest_harness_provider.py` receives a `tools` argument but calls the harness with only the prompt, attachments, event callback and cancellation. Relay board tools are not forwarded. This is the existing guest-tool limitation tracked here; the fallback in `issues/POLICY.md` is intentional (commit `7bd892f2`, #R9G7). No runtime code changed and no takeover of this discussing card. A board-tool bridge remains unimplemented.

<!-- relay:entry 20260921T112852Z-dm author=owner kind=comment mode=discuss -->
give me your view on the best approach here

<!-- relay:entry 20260921T112915Z-17 author=agent kind=comment mode=discuss model=kimi-k3 turn=0316e663c4df4959b54b45258e8c5e7b/a70386c216004456bd78849c84b914d9 -->
My view: **(a) — give the guest a small Relay-owned MCP surface with the board tools, as its own thing, not inside #SSRQ.**

Reasoning:

1. **The guest *is* the verifier in the flow we designed.** The Verify brief asks it to read the card, record a verdict, and move the card. If we pick (b), every guest turn ends with Relay (or you) hand-transcribing its reply into the board. That's a permanent relay tax on every guest verification, and it breaks the moment the guest wants to do anything mid-turn (read the thread, comment on a blocker).

2. **It reuses what exists.** `board_tools.py` already enforces guardrails, autonomy levels, and rate limits. Serving those same tools over MCP to the guest harness adds a transport, not a new policy layer. The blast radius is a subset of what a Relay pane agent already has.

3. **Scope it to read + the four writes**: `board_read`, `board_list`, `board_comment`, `board_update_card`, `board_move_card`. No `board_create_card` initially — guests shouldn't mint cards; that stays with the owner or the Switchboard agent.

4. **Why not #SSRQ:** that card is about trusting arbitrary user-configured third-party MCP servers — discovery, auth, trust decisions. A guest harness is Relay-owned and ephemeral; it needs a fixed, pre-trusted tool set handed to it at spawn. Folding it into #SSRQ would hold this bug hostage to a much bigger design.

The main cost of (a) is that `HarnessProvider.complete` currently drops the `tools` argument (as the Codex report confirmed), so the work is: build a minimal in-process MCP server exposing the board tools, wire it into the guest harness spawn, and extend `guest_harness_provider.py` to actually forward tools. That's a real chunk — I'd put it at *large*.

If you agree, say so and I'll record the decision on the card and move it to `planning` (or you can hit **Plan** to have a plan written). If you'd rather keep guests tool-less for now, I'll rewrite the briefs under (b) instead.

<!-- relay:entry 20260921T114323Z-32 author=owner kind=comment mode=discuss -->
yes, i agree. and i am open to building SSRQ simultaneously

<!-- relay:entry 20260921T114351Z-fw author=agent kind=decision model=kimi-k3 pane=switchboard turn=271ff3d5c65b4fa0a32b20f2b0e4898e/06b3d5e01bfb4df9827c77ae58b0ce14 -->
Approach: "yes, i agree. and i am open to building SSRQ simultaneously" — i.e. option (a), a small Relay-owned MCP surface exposing the board tools to the guest harness (read + `board_comment`, `board_update_card`, `board_move_card`; no `board_create_card`), as its own surface rather than folded into #SSRQ. #SSRQ (generic third-party MCP support) may be built in parallel.

<!-- relay:entry 20260921T114351Z-xv author=agent kind=event model=kimi-k3 pane=switchboard turn=271ff3d5c65b4fa0a32b20f2b0e4898e/06b3d5e01bfb4df9827c77ae58b0ce14 -->
- ✦ agent updated this card · appended to `## Decisions`

<!-- relay:entry 20260921T114355Z-82 author=agent kind=event model=kimi-k3 pane=switchboard turn=271ff3d5c65b4fa0a32b20f2b0e4898e/06b3d5e01bfb4df9827c77ae58b0ce14 -->
- ✦ agent moved this card · Discussing → Planning · Owner agreed to option (a): guest board tools over a Relay-owned MCP surface, built alongside (not inside) #SSRQ. Decision recorded; ready for a plan.

<!-- relay:entry 20260921T114401Z-cz author=agent kind=comment mode=discuss model=kimi-k3 turn=271ff3d5c65b4fa0a32b20f2b0e4898e/06b3d5e01bfb4df9827c77ae58b0ce14 -->
Recording the decision and reading the card for its hash.





**Done:** Decision recorded on #4NXH and the card is moved to **planning**.

- Decision noted: option (a) — a small Relay-owned, pre-trusted MCP surface giving the guest `board_read`, `board_list`, `board_comment`, `board_update_card` and `board_move_card` (no `board_create_card`), reusing the `board_tools.py` guardrails; built as its own surface, with #SSRQ free to proceed in parallel.
- I also left a note on #SSRQ so it knows the owner is open to building it simultaneously — it can be un-deferred whenever you want a plan for it.

Hit **Plan** on #4NXH to have the plan written (minimal in-process MCP server, wiring into the guest harness spawn, and making `guest_harness_provider.py` forward tools).

<!-- relay:entry 20260921T115218Z-cp author=codex kind=progress -->
### Codex · 2026-09-21 11:52
Owner request: "can you plan 4NXH". Claimed planning as codex; keeping the card in planning for this plan-only request and clearing the answered waiting_on flag. Reviewing both harness adapters and the existing board dispatch before writing the plan.

<!-- relay:entry 20260921T115402Z-cq author=codex kind=plan -->
### Codex · 2026-09-21 11:54
Wrote the implementation plan and moved planning → planned. The five agreed tools use a per-harness MCP stdio proxy to the live worker, preserving native preparation/dispatch, identity, budgets and verification gates. Plan covers both adapters, startup-before-binding, lifecycle/retry safety, capability-aware policy, targeted tests and real-client acceptance. #SSRQ remains independent. Planning only; no runtime implementation or runtime test claim.

<!-- relay:entry 20260921T123008Z-cx author=codex kind=progress -->
### Codex · 2026-09-21 12:30
Claimed execution for owner request "deliver #4nxh". Following the approved five-tool bridge plan; runtime implementation is absent.

<!-- relay:entry 20260921T124259Z-cy author=codex kind=evidence -->
### Codex · 2026-09-21 12:42
Implemented the five-tool bridge and completed real Codex/Claude all-tool and resume-discovery checks on disposable boards. Added execution summary, test references and QA checklist. Evidence: docs/qa_evidence/2026-09-21-4nxh-guest-board-tools/.

<!-- relay:entry 20260921T124440Z-cz author=codex kind=decision -->
### Codex · 2026-09-21 12:44
Owner authorized landing the earlier session's draft: "Include the existing plan and thread". Moved to needs-verification with 496 passing targeted tests, live Codex/Claude all-tool and resume checks, and a clean tests_check (no findings, failing tests or blocking signals). Repository-wide board check still reports the two existing MDL1 ID errors.

<!-- relay:entry 20260921T124515Z-d0 author=codex kind=evidence -->
### Codex · 2026-09-21 12:45
Landed implementation, approved plan/thread and needs-verification evidence in 590829dc9a39fdcdea6f5c3e6d685e4c0f45c3ae; linked the implementation commit.

<!-- relay:entry 20260922T013953Z-1s author=agent kind=evidence -->
Check · 2 missing-evidence, 1 not-applicable, 5 passed; 3 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T150731Z-83 author=agent kind=evidence -->
Check · 3 missing-evidence, 1 not-applicable, 4 passed; 5 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T150909Z-8m author=agent kind=evidence -->
Check · 3 missing-evidence, 1 not-applicable, 4 passed; 5 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T151942Z-x9 author=agent kind=evidence -->
Check · 3 missing-evidence, 1 not-applicable, 4 passed; 5 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.
