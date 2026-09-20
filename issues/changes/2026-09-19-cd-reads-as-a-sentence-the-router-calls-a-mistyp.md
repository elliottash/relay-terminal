---
id: 09HE
type: work
status: discussing
labels: [bug, routing]
waiting_on: owner
rank: zzzzzzx
created: '2026-09-19'
source: terminal pane, 2026-09-19 (the line was `cd..`, typed just before; request R2 of session f8bdb76694be4b048baa9c8cc52f52f4)
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# `cd..` reads as a sentence: the router calls a mistyped command an agent request and drops its note

## Issue
that was an agent detect false positive, trace it

## Trace
`cd..` (the line typed just before, in auto mode) was routed to the agent, and the composer's
route line read `AGENT · Reads like a request · sent to the agent`. Both halves of that are the
false positive: it is a mistyped command (`cd ..` with the space lost), not a request, and the
`command not found: cd..` note the router is supposed to print for a mistyped command was dropped
as well, so nothing at all said the shell would have rejected the line.

Where it happens, end to end:

1. `src/Pane.h` sends `route {text, mode: "auto"}` to the worker.
2. `backend/worker.py:121` calls `router.classify(text, mode, known, path, cwd, remote=…)` and emits
   the whole `Decision` back (`route`, `reason`, `valid`, `invalid_reason`, `explain_invalid`,
   `agent_signal`).
3. `backend/relay_core/router.py:classify` — `cd..` is not `LOOP_ONLY`, not `LONE_REPLY`, does not
   match `NATURAL`, so it falls to `check_runnable`: bash `-n` accepts it, and `_check_words` ->
   `_resolve("cd..")` finds no program named `cd..`, so `valid=False`,
   `invalid_reason="command not found: cd.."`. The invalid fall-through at the end of `classify`
   then routes to the agent — that part is by design (a mistyped command goes to the agent to be
   fixed).
4. The `why` string is chosen by `explain_invalid(text, reason, …)` (`router.py:1113`). For `cd..`
   it returns **False**, so `why` becomes `"Reads like a request · sent to the agent"` instead of
   `"Not a runnable command (command not found: cd..) · sent to the agent"`.
5. `src/Pane.h:9451-9458` uses the same flag for the note: `explain_invalid` false means `why` is
   empty, so `submitAgent(text, true, "")` hands `cd..` to the agent with **no**
   `command not found: cd..` printed under it.

Why `explain_invalid` says no — the actual defect, in two lines of `router.py`:

- `_bare_word("cd..")` -> `("cd", True)`: `SENTENCE_TAIL` (`,.:;?!…"'”’)`) includes `.`, so the
  trailing dots are stripped as sentence punctuation and `punctuated=True`. Same for `cd.`, `ls..`,
  `make..`, `git..`.
- `_meant_as_command` (router.py:~790) then hits `if punctuated: return False` **before** it ever
  asks whether the stripped word (`cd`, a bash builtin) runs here or is one slip from a command
  that does. That early return was added for card #W954 ("yeah," / "ok." / "wait…" are sentences),
  where the words are replies; here it swallows a command name with a stray full stop.

With the `punctuated` bail-out out of the way, `_meant_as_command("cd..")` is True (`cd` is one edit
from `dd`/`cc` here, and the builtin check would also fire), so `explain_invalid` would return True
and the note would be shown. Verified by calling the functions directly (see Evidence).

## Evidence
Measured 2026-09-19 on this checkout (`backend` on `sys.path`, real PATH and cwd
`/home/elliott/repos/relay-terminal`):

```
'cd..'       -> route=agent  explain=False valid=False reason='Reads like a request · sent to the agent'
'cd ..'      -> route=shell  explain=True  valid=True  reason='Runnable shell command.'
'cd.'        -> route=agent  explain=False valid=False reason='Reads like a request · sent to the agent'
'ls..'       -> route=agent  explain=False valid=False reason='Reads like a request · sent to the agent'
'make..'     -> route=agent  explain=False valid=False reason='Reads like a request · sent to the agent'
'git..'      -> route=agent  explain=False valid=False reason='Reads like a request · sent to the agent'
'gti status' -> route=agent  explain=True  valid=False reason='Not a runnable command (command not found: gti) · sent to the agent'

_bare_word('cd..') = ('cd', True)
_meant_as_command('cd..') = False      _meant_as_command('cd') = True
_looks_mistyped('cd')     = True
check_runnable('cd..')    = (False, 'command not found: cd..', True, '')
```

The GUI's own log has the route event for the live pane
(`~/.local/share/relay/logs/relay.log`):

```
2026-09-19T23:08:34.022Z DEBUG relay.gui event type=route pane=cc105c90 reason=Reads like a request · sent to the agent
```

and the session file shows the line arriving as an agent request, not as shell input:
`~/.local/share/relay/sessions/83e6ce670835f99d/f8bdb76694be4b048baa9c8cc52f52f4.json`,
`requests.items[0] = {"id": "R1", "text": "cd..", "source": "ask", …}` with a turn of its own
(`relay_kind: "prompt"`).

Related, already fixed on the same code path: #W954 (`"yeah,"` is a sentence — the `punctuated`
bail-out and the `_bare_word` strip), #N3WC (a sentence that names a file), #8G17 (a syntax error in
a sentence). The existing test that covers this area,
`tests/test_router.py::test_sentence_punctuation_is_not_a_mistyped_command`, asserts the note for
`"gti stauts."` and `"cs .."` but has no case for a command name glued to its dots (`cd..`), which
is why the regression is invisible to the suite.
The same shape is misread at an ssh prompt, where the reason names the rule outright
(`_remote_prose`, `router.py:~955`, its own `if punctuated: return "sentence punctuation"`):

```
'cd..'   remote -> route=agent  reason='Reads like a request (sentence punctuation) · sent to the agent'
'ls..'   remote -> route=agent  reason='Reads like a request (sentence punctuation) · sent to the agent'
'make..' remote -> route=agent  reason='Reads like a request (sentence punctuation) · sent to the agent'
'cd ..'  remote -> route=shell  reason='Shell command · typed on filly.'
```

So a fix that only touches `_meant_as_command` leaves the ssh path reading `cd..` as a sentence.

## Proposed fix
The defect is one early return, in `_meant_as_command` (`backend/relay_core/router.py`, the
`if punctuated: return False` just above the capitalised-command test). It was written for replies
(`yeah,` `ok.` `wait…`) and needs to ask one more question before it gives up: is the word *with its
punctuation taken off* a command this machine has? Sketch (not applied):

```python
if punctuated:
    # "yeah," and "ok." are sentences; `cd..` and `ls..` are commands with their dots glued on.
    # Replies and loop builtins keep the sentence reading (`yes!`, `wait... what`, `done!`).
    if (lowered not in FUNCTION_WORDS and lowered not in LONE_REPLY
            and (lowered in known or lowered in BUILTINS or commands.has(lowered))):
        return True
    return False
```

`_looks_mistyped` must **not** be the test here: it would make `ok.` (`ok` is one edit from `od`)
and `no` (`nl`) into commands again, which is card #W954.

Whatever is chosen, the suite needs a case: `tests/test_router.py`
`test_sentence_punctuation_is_not_a_mistyped_command` covers `"gti stauts."` and `"cs .."` but no
command name glued to its own dots, so `cd..`, `cd.`, `ls..`, `make..` and `git..` are unguarded in
both directions (a note for a slip, no note for prose).
