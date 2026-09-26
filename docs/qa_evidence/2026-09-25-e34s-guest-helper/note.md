# E34S — the helper agent on a guest harness

Landed: 98823eb9269244e0d11af016fddd2db3a5882e7c (the feature, 9 paths; verify slot built the exact tree).
Taken back: d87c23c0ff99 (a stray one-path landing of the agent.py comment, repaired by c0fcb08388ca).

## Tests — clean export of 98823eb, not the working tree

```
$ PYTHONPATH=backend:tests python3.12 -m unittest tests.test_guest_harness_provider tests.test_roles tests.test_card_model_selection
Ran 170 tests in 7.198s

OK
```

Also green on the same tree: tests.test_configure_provider + tests.test_board_protocol (198 tests) —
run in the checkout on identical bytes (the land digest pinned them).
