# #XJSN implementer evidence

Verified 2026-09-22 with `scripts/relay-build --target relay` (passed) and
`PYTHONPATH=backend python3 -m unittest discover -s tests -p test_customproviders.py -v`
(27 tests passed; output in tests.txt). Pytest is not installed; the suite is unittest-native.

The tests cover old files, save/reload, omission preservation versus explicit clearing,
probe reconstruction, rejecting invalid extra before changing a stored key or file, preset
and role propagation, explicit configure replacement, model selection, and a real local HTTP
server capturing requests before and after clearing.

Live app: `build/relay --fresh`, Xvfb :168 at 1280x960, `env -i`, HOME
`/tmp/xjsn-home`, XDG_CONFIG_HOME under that directory, XDG_RUNTIME_DIR
`/tmp/xjsn-runtime`, RELAY_KEYRING=off. No real credentials were loaded. Endpoint:
`python3 capture-server.py /tmp/xjsn-requests.jsonl` on 127.0.0.1:41868.

From Models > providers > add > custom endpoint, entered JSON Capture, the local URL,
and model ids first, second. Each invalid Save stayed in the dialog and retained the inputs:
invalid-json.png (`{broken`), invalid-object.png (`[]`), unsupported-key.png (`model`).
No registry file existed after these three attempts. Corrected to temperature=0.7,
top_p=0.9 and reasoning.effort=low, then saved. The registry included those parameters
and the real GET /models probe added discovered without losing them.

Clicked the provider Test button: the first requests.jsonl record is the actual HTTP body,
including the three saved parameters and no Authorization header. Reopened Edit:
edit-prefill.png shows the saved indented object. Selected and deleted its whole content,
saved, and clicked Test again: the second HTTP record contains none of the extra keys.
cleared.json is the resulting registry, retaining discovered and storing explicit {}.

These are implementation checks; independent verification remains on the card.
