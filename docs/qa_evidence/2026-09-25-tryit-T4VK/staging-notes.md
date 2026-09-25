How this staging differs from real use: the "agent" is the scripted-provider harness from
tests/test_subagents.py — a fake main model whose responses are fixed (spawn a background
subagent, call agent_wait, finish) and a fake subagent gated open-and-shut, so no model, no
network and no API key are involved, and the steer is a library call at t+0.5s instead of your
typing. The app's own window is not opened: driving a real agent pane needs a model key, and
Try-it staging isolates the keyring away from your real one. The pane half of the change —
that a submitted prompt steers straight into the waiting turn, one Enter as in Claude Code, and
keeps its place behind anything already queued — is exercised by tests/queuesubmit_test.cpp
rather than by this timeline. Timestamps are seconds from the turn's start.
