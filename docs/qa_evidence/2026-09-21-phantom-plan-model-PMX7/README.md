# #PMX7 implementation evidence

## Reproduction

Session `19a7fd1b741f433ab903e0aea3b65703` in
`~/.local/share/relay/logs/worker.log` recorded:

```text
configured model=gpt-5.6-sol host=codex role=main
turn_start model=gpt-5.6-sol host=codex mode=plan effort=high
plan_route from_model=gpt-5.6-sol to_model=glm-5.3 host=api.z.ai effort=max source=configured
```

The corresponding global settings contained:

```ini
[roles]
planning\effort=max
planning\preset=glm-coding
```

That pre-#HR5E override prevented the restored own-model default from ever being reached.

## Verification

- `scripts/relay-build --target relay-jobstab-tests` — passed; focused target compiled.
- `ctest --test-dir build -R '^jobstab$' --output-on-failure` — 1/1 passed, including legacy cleanup, one-shot behavior, and preservation of a new explicit override.
- `PYTHONPATH=backend python3 -m unittest tests.test_roles tests.test_plan_turns` — 110 passed; endpoint and guest/Codex plan routing remain green.
- `scripts/relay-build --target relay` — passed; full application target linked.

The migration lives in the `rolestore::roleSetting` path used by `Pane::rolesObject`, so terminal
panes and console/helper workers cannot serialize the stale planning override before it is retired.
Shell submissions do not enter the agent plan route.
