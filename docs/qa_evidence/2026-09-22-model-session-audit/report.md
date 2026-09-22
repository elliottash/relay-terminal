# Model changes in ef7ab5c3ef21445ab80dea50fedfbb29

Analysis on 2026-09-22; no application code changed. Session maps to pane a352a041.
Evidence: adjacent events.log, extracted from local worker.log and relay.log; session JSON
at ~/.local/share/relay/sessions/83e6ce670835f99d/ef7ab5c3ef21445ab80dea50fedfbb29.json.

## Timeline (EDT; logs use UTC)

- 09:01:18: started on Codex / gpt-6-astra. First `1+1` cancelled at 09:01:27.
- 09:01:30: model_changed to glm-5.3-flash. Second `1+1` ran on Flash and answered `2`.
- 09:08:15: explicit pick glm-coding / glm-5.3 / max. Worker acknowledged old Main
  gpt-6-astra first, then glm-5.3, then effort. The 09:08:54 turn actually ran GLM.
- 09:08:55–09:09:18: six HTTP 429 retry waits, then failover to Muse on OpenRouter.
  Turn failed after 24.461 seconds. The final error retains only GLM's failure reason.
- 09:09:30: set_agent_role rejected with `Base URL must be an HTTPS URL without
  credentials, query, or fragment.` No successful model-change event followed.
- 09:09:43: `check here` still ran glm-5.3, with six more 429 retry waits, Muse fallback,
  and failure after 27.684 seconds.
- 09:10:18: explicit Kimi / kimi-k3 / high pick. Acknowledgments first named GLM,
  then Kimi. At 09:18:15 the next turn ran Kimi and subsequently made tool calls.

## Findings

1. **Guest models offered through a role pick reach the wrong transport.**
   Pane.h:modelBoxPicked sends a non-Main pick through setAgentRole/sendAgentRole.
   worker.py:set_agent_role resolves the guest config and calls Agent.set_model without
   injecting a guest provider. agent.py:_provider_for constructs ChatProvider for every
   non-hosted config. ChatProvider validates the guest's harness:// URL as HTTPS and fails.
   Reproduced locally without networking: RoleResolver.resolve_entry('high',
   {'preset':'guest:codex','model':'gpt-6-astra','effort':'high'}) with guest_check=True
   returns harness://codex; _provider_for(config, 60) raises precisely the logged ValueError.
   The incident log does not record the role payload, so the exact model/role clicked at
   09:09:30 cannot be recovered. The reproduced guest path is a strong explanation,
   not proof of that particular click's target.

2. **The failed role pick can leave selection state inconsistent.**
   Pane.h:setAgentRole writes m_agentRole and m_modePick and announces the choice before
   receiving a worker acknowledgment. worker.py:role_decide.apply_now likewise writes
   state['agent_role'] before agent.set_model succeeds. A transport-construction exception
   emits a generic error, not model_switch_refused; the generic UI handler does not restore
   m_agentRole. The actual provider/config remains GLM. This is a code-confirmed failure
   path; there is no screenshot proving exactly what this session's picker displayed.

3. **Cross-mode direct selection emits a misleading intermediate model change.**
   Main-page modelBoxPicked calls setAgentRole(main) before selectEntry(new model).
   These are separate requests, so old Main is acknowledged before the desired model.
   Both explicit picks in this session show that sequence. Their eventual selection worked;
   no logged turn after the Kimi acknowledgment used GLM. One atomic model-and-role change
   would eliminate the intermediate acknowledgment and possible transient UI bounce.

4. **Failover diagnostics discard the fallback failure.**
   Agent._failover_failure formats first_error and fallback names, discarding exc's text.
   Local reproduction with a synthetic GLM 429 and either a Muse 401 or Muse 400 yields
   the same final message: `glm-5.3 failed; Muse (openrouter) too: GLM HTTP 429`.
   Consequently the logs do not establish why Muse failed. Reporting both errors and
   recording every attempted provider would make this diagnosable.

5. **Repeated 429 delays match the existing #YJG7 issue.**
   This session spent roughly 52 seconds across two failed turns and retried GLM six times
   per turn before fallback. Earlier #YJG7 evidence identifies exhausted Coding Plan quota;
   this session's logs omit the response body, so that exact quota cause is not independently
   proven here. Failover returning to the selected GLM after each turn is intentional.

## Suggested repair order

1. Route guest role picks through guest startup, and make selection state transactional:
   acknowledge only after success or restore provider/model/role/effort on every failure.
2. Apply a direct model/role/effort selection atomically.
3. Keep each fallback's own error; address quota classification under #YJG7.

Analysis and no-network reproductions only; no GUI replay, provider calls, or fixes performed.
