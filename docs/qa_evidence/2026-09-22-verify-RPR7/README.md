# Independent verification: #RPR7

Verdict: PASS for the local implementation; no blocker found. Production rollout and live GLM inference are excluded. Verification performed independently by Relay verifier a5 on 2026-09-22 UTC. No source, card, or thread changes.

Reviewed the gateway authorization and digest-only store, desktop provider controls, hosted transport endpoint restrictions, client availability invalidation, and tests. Implementation commits: `1cf0f5f6`, `380d8c66`, `6668999e`, `cdaf887e`, `f8937979`, `eb2a2671`, `0263794b`. Starting checkout tip: `8e6094d9ee145228b4e5896a98a2c11ed8360f06`. Shared working checkout contains unrelated edits; this run used the actual existing `build/relay`, not a clean-export rebuild. Binary SHA-256: `7bc2e701869238ba12c98cafe7b60c62b9f0598bb1bc94ebdc1bbcea2e093109`.

## QA checklist against Done means

- [x] A valid personal code enables Pro high/main and flash: live provider activation showed active access and 3/3 models; model priorities showed GLM 5.3 for high/main and GLM 5.3 Flash for flash. Actual Pro main turn reached the local upstream as `glm-5.3` and rendered “Hello world.” Gateway tests exercise all three role routes.
- [x] Missing, invalid, revoked, and forged client access cannot call Pro: focused real-HTTP tests cover absent/bad codes, forged plan/body fields, independent people, invalid installation identity, and an already-issued token after revocation. Live GUI revocation denied the next Pro turn with no additional upstream requests.
- [x] Free and BYOK continue working: focused tests exercise Free after Pro revocation/removal and shared quotas/concurrency; the GUI produced two background Free calls alongside the successful Pro call. An additional direct BYOK probe completed against the fake upstream with its synthetic BYOK credential and no Pro header.
- [x] Provider row reflects entitlement: `activated.png` shows active access; `revoked.png` shows “Relay Pro access is not active” after the refusal. Client tests cover background refresh, changed-code invalidation, and prevention of stale validation reactivation.
- [x] Lite stays Free: `test_models_effort_tiers_and_free_lite` checks both default ordering and role resolution. Ultra remains deferred; unsupported Pro role requests are rejected.
- [x] Credentials remain separate: generated test code was absent from the isolated GUI config, Relay log and captured upstream bodies; tests check public events, config objects, gateway logs, digest-only database storage, and upstream headers. No real keys were used. Screenshots show no code or credential.

## Tests

`PYTHONPATH=.:backend python3 -m unittest tests.test_relay_pro tests.test_gateway_pro -v`

22 tests passed in 1.639 seconds. Full output and the extra successful BYOK probe result are in `tests.log`.

## Live GUI procedure and evidence

Copied `docs/qa_evidence/2026-09-22-RPR7/live-drive.py` to `/tmp/rpr7-verify-a5-drive.py`, replacing only its control-pointer filename with `/tmp/rpr7-verify-a5-path`. Launched it with `PYTHONPATH=.:backend`. It started the actual gateway, fake upstream, actual Relay binary, fresh HOME/config/data, a synthetic keyring stand-in, and Xvfb display `:951`. The original implementer's pointer and display were not touched.

Opened Models with Ctrl+Shift+M, activated a freshly generated personal code through the masked dialog, filled priorities from defaults, selected Pro main, and submitted a prompt with Ctrl+Return. Visually inspected each screenshot before proceeding. Revoked the person through the fixture control file, submitted another Pro turn, and inspected the denial and inactive provider row. Upstream count was 3 before and 3 after the refused turn: one `glm-5.3` call and two `fake-model` Free background calls. `upstream.json` retains only routing/count metadata, omitting the long worker instruction bodies. Shut down the fixture with its `quit` control, which removes generated code files.

- `activated.png`: activation confirmation, active provider, 3/3 models.
- `success.png`: successful Pro main response and GLM model priorities.
- `revoked.png`: denied follow-up and inactive provider row.
- `upstream.json`: unchanged upstream count after revocation.

Relay board MCP tools were not exposed by tool discovery. No delegation or board mutations were attempted, as requested. The root agent owns card updates and production rollout remains excluded.
