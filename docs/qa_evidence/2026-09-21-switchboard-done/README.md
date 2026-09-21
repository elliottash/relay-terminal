# Done (d) implementer evidence

Built relay-boardpane-tests through scripts/relay-build. All 7 Qt test cases passed under Xvfb with XDG_CONFIG_HOME=/tmp/codex-done-config.

The new test exercises the card button, list and document d keys, successful-write toast and Undo write ID, filter typing protection, and already-done no-op. The worker retains its existing status-move validation and undo semantics.

[Live widget capture](done.png) shows Done (d) and the acknowledged undo notice. This uses a fixture board and simulated worker acknowledgements, not the owner's card files.
