<!-- relay:entry 20260921T000001Z-a1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21
Filed from the owner's report in the terminal. Reproduced by reading the path: a guest pane's
`config.model` is `sonnet`/`opus`, `presets.model_supports_vision` is keyed on API ids, so
`Agent._begin_vision_turn` refused. Fixing in `backend/relay_core/agent.py` with tests in
`tests/test_images.py`.

<!-- relay:entry 20260921T000002Z-a2 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21
`_begin_vision_turn` returns early on a `harness://` pane: the guest takes its own images and is
never routed off. `tests.test_images tests.test_presets tests.test_roles
tests.test_guest_harness_provider tests.test_plan_turns` — 227 tests, OK; the two new tests fail
on the previous code. Moved to done (medium: one backend file and its tests, no decision needed).
