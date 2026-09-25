# #YC0T — a second Z.AI Coding Plan / Kimi Code subscription

- `tests.txt` — `tests/test_key_accounts.py` (registry, keyring id rule, resolution through
  roles / resolve_preset / keytest / configure, per-account quota poll and weighting, a quota
  refusal on `glm-coding` finishing the turn on `glm-coding:ethz`) with the neighbouring
  `test_usage_refresh.py` and `test_provider_errors.py`.
- `yc0t-realworker.txt` — the real `backend/worker.py`, isolated home, key from
  `RELAY_GLM_CODING_ETHZ_API_KEY`: `key_account_save` → account `glm-coding:ethz`, `presets` row
  with its own key state, `accounts_allowed` on the plan row, registry without a key.
- Live Xvfb drive (`drive.py` + fixture `worker.py`, verify-slot build of this change):
  `1-sources-before.png` "add account…" on both plan rows; `3-key-dialog.png` the masked key box;
  `4-sources-after-add.png` the new "z.ai · glm-5.3 · coding plan (ethz)" row with replace key /
  test / remove. `result.json`: the save request (preset, label, key length only).
- Failing identically on a clean HEAD export (not this change): test_provider_limits tied-rank,
  test_keytest_guest logged-in rows, two test_presets GUI-mirror checks.
