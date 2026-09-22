# Ranked guest startup — fixed and verified

Fix commit: `d7c27b95ead2fe6c7b332a2a7627c8bac104e0ac` (#MDL1). Real catalog lookup (`codex debug models`, metadata only in catalog.json) lists gpt-6-astra and high; the earlier seed was valid. The GUI sent the top-level model correctly, but the harness reads guest.model. Deferred startup failed to put the ranked model and effort in that block and displayed the catalog default before startup.

Five added lines in Pane.h set the pending pane model and stage the selected model/effort in the guest request on the first prompt. No RelayWindow changes. The fixture in ../startup/drive.py now asserts the actual guest.model and guest.effort as well as the top-level model, deferred startup and one queued ask. It fails before this fix (regression-before.txt) and passes afterwards (regression-after.txt).

Live real guest proof, fresh isolated Xvfb/HOME/config with main priority guest:codex|gpt-6-astra|high: before-first-prompt.png shows gpt-6-astra/high before any configure; gui-sanitized.log shows deferred selection and configure on the prompt. worker-sanitized.log shows guest=codex model=gpt-6-astra effort=high and one successful 8649ms turn, zero tools. after-15s.png shows MDL1_OK and the same model/effort. This is a real installed signed-in guest, not the protocol fixture. The copied temporary auth file was removed on shutdown; no credentials are in evidence. One additional minimal generation call, bringing follow-up calls to three (one original hosted fallback, one pre-fix Codex, one fixed Codex).

Wrapper build 2026-09-21.21H.11 passed; land.txt records exact proposed-tree build success. ctest.txt records modelcatalog/windowstate/panestate: 3/3 passed. Shared binary launch briefly raced another session's relink (PermissionError); the successful retries and actual logs above are the verdict evidence. No full suite run.

Item 1 now passes. Overall #MDL1: 18 pass, 1 partial/blocked (actual paired-phone presentation only).
