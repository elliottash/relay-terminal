# RPR7 implementation evidence

Implemented personal, revocable Relay Pro access. Pro high/main map to GLM 5.3 and flash to GLM 5.3 Flash; lite remains Relay Free. Ultra is deferred. No production deployment, upstream credential installation or production code issuance was performed.

## Automated checks

- `backend-tests.txt`: 281 targeted tests passed across Pro client/gateway, Free gateway/hosted, presets, roles, ranking, key tests and session configuration.
- `effort-tests.txt`: final follow-up, 63 Pro/preset tests passed after limiting the Pro knob to low/medium/high, matching the documented gateway ceiling.
- `gateway-tests.txt`: independent root rerun, 39 gateway tests passed.
- `gui-tests.txt`: modelcatalog, modelpicker and modelsettings targeted C++ tests.
- `build.txt`: final working-tree build. Each C++ implementation commit also passed land.py's exact candidate-tree build.

Gateway tests use real HTTP to a local fake upstream. They cover invalid/missing/revoked codes, existing installation bearers, forged plan/model requests, digest-only persistence, cross-connection revocation, no upstream secret forwarding, missing configuration, shared quota/rate/concurrency/spend limits and unchanged Free requests. Client tests also cover background activation, no-generation access checks, current-code availability, removal, endpoint-override rejection before key lookup, partial-role configuration rejection, callback failures and safe error wording.

## Live app

`live-drive.py` starts the actual GUI and worker, actual gateway, and a fake OpenAI-shaped upstream on loopback, all under a fresh HOME/XDG profile and Xvfb. A local Secret Service stand-in stores only a generated test code; no real keyring or production credentials are used. GLM model IDs are the actual IDs captured by the fixture, not live GLM inference. Fixture prices are synthetic.

The first drive confirmed invalid activation stores nothing; valid activation enables Pro; default lists offer GLM high/main and GLM Flash; an actual Pro main turn returns the fixture's Hello world; revocation denies the next turn with zero additional upstream calls; the model catalog loses access; removing the code deletes the keyring entry. Screenshots and the final-repeat notes below record the final code path. The separate XJSN verification reused the isolated GUI but its own local endpoint and evidence folder.

## Deployment boundary

Production needs reviewed upstream model IDs/prices/credentials and gateway rollout using `gateway/README.md`. The gateway rejects Pro until roles and a valid personal code exist. Operator CLI: `python3 -m gateway.pro --db EXISTING_DB issue PERSON`, `list`, `revoke PERSON`. Codes are emitted once at issuance; the database stores only digests. Pro retains the existing installation quotas and spend ceilings; this change adds no billing or higher allowance.

This is implementation evidence; the card is handed to needs-verification for a separate verifier.

Final repeat on the rebuilt app passed all stages: `01-invalid.png` shows the clear access-denied message, `02-active.png` shows confirmed access, `03-defaults.png` shows GLM high/main and GLM Flash with low/medium/high levels, `04-turn.png` shows a completed Pro turn, `05-revoked.png` shows denial and no access, and `06-removed.png` shows no saved code. `live-result.json` asserts zero upstream requests after revocation and confirms removal. `provenance.txt` records the binary hash.
