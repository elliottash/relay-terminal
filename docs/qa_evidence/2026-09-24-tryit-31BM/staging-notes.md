How this differs from real use: the models are a fake loopback server (no key, no network, no
real model) whose "big" replies with two invented 30,000-character essays and whose compaction
summary deliberately streams over ~24 s so there is time to watch the chip — a real summary
often finishes faster, and its percent is an estimate (clamped below 100% until it lands), so
expect the number to jump rather than glide. The profile is a throwaway jail under
/tmp/claude-1000/tryit/31BM (nothing of yours is touched); the pending switch to a 12,000-token
window is chosen so the switch has to compact first, and `small` refusing the takeover after
the compaction is part of the fixture, not the thing under test. First-run panes elsewhere may
open as guest Claude harnesses; here `/model local:big` is typed for you by the script.
