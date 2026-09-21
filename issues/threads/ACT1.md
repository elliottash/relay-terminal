<!-- relay:entry 20260921T224028Z-a1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 22:40
Filed from reading `isUsableNode` after the models pane fell into the same trap (bb5fba2b): the Activity pane is saved as `internals` but the gate did not know it. Fixed with a test; not driven live — the models-pane drive shows the mechanism.
