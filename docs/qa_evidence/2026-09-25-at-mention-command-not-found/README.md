# @mention attachment no longer explains "command not found" (card #EB4A recurrence)

Owner report 2026-09-25 15:30 (screenshot `relay-paste-20260925-153056.png`): the prompt

```
remove this diamond prefix in agent prompts. just start at the beginning of the line
@/home/elliott/.cache/RelayTerminal/relay/images/relay-paste-20260925-153029.png
```

printed `command not found: remove` under its ✦ echo — while (correctly) being sent to the
agent (`relay.log` 19:30:29: `event type=route pane=e5ab99a6 reason=Not a runnable command
(command not found: remove) · sent to the agent`).

## Cause

The composer text pastes as two lines and the `@mention` sits on its own line.
`router.explain_invalid` splits the text into segments, and the `@/home/…png` segment was
judged by `_meant_as_command` a meant command — a name that is not a plain word, the same
reading `./run.sh` gets — so the prose gate stood aside and the note printed.

## Reproduce (before the fix, at `main`)

```
$ python3 - <<'EOF'
from relay_core.router import classify
t = ("remove this diamond prefix in agent prompts. just start at the beginning of the "
     "line\n@/home/elliott/.cache/RelayTerminal/relay/images/relay-paste.png")
d = classify(t, "auto")
print(d.route, d.explain_invalid, d.invalid_reason)
EOF
agent True command not found: remove
```

## After the fix

`explain_invalid` now skips `@path` / `@"path"` tokens (new `_is_attachment` in
`backend/relay_core/router.py`): they are Relay composer attachments, the shell never sees
them. Same input:

```
agent False command not found: remove
```

Real slips still explain themselves: `gti status` → `explain_invalid=True` in `auto` and
`agent`; `remove …\nmkae clean\n@…` keeps its `command not found` note.

## Tests

`PYTHONPATH=backend python3 -m pytest tests/test_router.py -q` → 49 passed, including the new
`test_an_at_mention_attachment_is_not_a_mistyped_command`.

Unrelated failure seen in the same sweep, filed as card #VJX7:
`tests/test_ssh_remote.py::test_command_shaped_lines_are_typed_on_the_host` fails on `main`
without this change ("cp a b" and "kubectl get pods in the namespace" → `agent`).
