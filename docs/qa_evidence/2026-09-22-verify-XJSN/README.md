# Independent XJSN verification

Root session reviewed implementation 2ae67cfc and reran `PYTHONPATH=backend python3 -m unittest tests.test_customproviders -v`: 27 pass (`tests.txt`).

Drove the actual built Relay GUI on isolated Xvfb :950 with a fresh HOME, independent of the implementer's session. Used the loopback capture-server.py from the implementation evidence (port 41868).

Added Root capture with two model IDs and temperature 0.25/top_p 0.85. The Test button sent both parameters (`live.jsonl`, first record). Reopened Edit and visually confirmed the saved JSON. Changed it to [] and clicked Save: the form stayed open with an actionable error (`form.png`). Cleared the box, saved and pressed Test again: neither parameter is in the second request. No production keys or endpoints involved.

Source review and tests also cover legacy omission versus explicit clearing, persistent reload and asynchronous model discovery. Verdict: pass; card closed by this verifying session.
