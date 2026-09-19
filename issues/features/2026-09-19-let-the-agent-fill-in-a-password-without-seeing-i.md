---
id: V2HM
type: work
status: ready
labels: [feature]
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
rank: zzzzzzk
created: '2026-09-19'
acceptance: the agent can satisfy a sudo, ssh or device password prompt by naming a stored secret, the prompt is answered without the person typing, and the secret's value never enters the model's context, the transcript, the session file or either log
source: 'conversation, 2026-09-19 (while planning #R5TC): "in general i want it to be easy for the agent to fill in passwords for example."'
links: {plans: [], commits: [], evidence: [], related: [C1HH, 9D0B, R5TC, S5SH], github: null}
---
# Let the agent fill in a password without ever seeing it

## Issue

in general i want it to be easy for the agent to fill in passwords for example.

## What stops it today

Relay refuses this twice over, deliberately, and the refusal is stated in three places:

- **The worker refuses.** `backend/relay_core/program_input.py:15-16`: "**Never a password.** A
  masked prompt is refused without asking the GUI, and the GUI refuses again. No password prompt is
  ever typed into by a model." The refusal sentence is at `:57`.
- **The GUI refuses.** `Pane::programGrant()` returns nothing while the screen is masked
  (`src/Pane.h:1680`), and `program_state` reports `granted: … && !masked` (`src/Pane.h:1161`), so
  `type_into_program` is not even offered for that turn.
- **The model is told.** `backend/relay_core/agent.py:163`: "Never type into a password or
  passphrase prompt."

Instead, the prompt box becomes a masked `QLineEdit` (`m_secretEdit`, `src/Pane.h:713-724`), kept
separate from the composer so the typed password never reaches its document, its history, its undo
stack or route assist. The person types it; the agent waits.

The rule is right about the thing it was protecting — **a model must never be handed a password** —
but it also blocks the thing the owner is asking for, which is different: the agent getting *past*
a sudo prompt on its own.

## The shape this should take

**Separate "the agent answers the prompt" from "the agent knows the secret".** Relay already has
the machinery for the second half: `backend/relay_core/keystore.py` stores provider keys through
`secret-tool` (libsecret) and hands out `lookup()`/`key_source()` — the worker uses a key it never
prints, and `RELAY_KEYRING=off` disables it wholesale for tests. A password should travel the same
way.

Recommended design, for the owner to confirm before it is built:

1. **Named secrets in the keystore.** Widen `keystore` past preset ids (`_check_id`,
   `keystore.py:49`) to user-named entries — `sudo`, `deploy-host`, `luks`. First use is the person
   storing one: the masked field that already appears at a password prompt gains "remember this as
   …", so a secret enters the store the only way it should, typed by its owner.
2. **The model names, Relay types.** The agent asks to answer the prompt *by name* — not with a
   value — and Relay resolves it and writes it to the program. The value never appears in the tool
   arguments, the tool result, the transcript, the session file, `relay.log` or `worker.log`. The
   model learns only whether the prompt was satisfied.
3. **A named secret unblocks the masked gate; a literal one never does.** Both refusals above stay
   exactly as they are for any attempt to type *text* into a masked prompt. What changes is that
   one narrow, valueless path opens through them.
4. **Unknown name → ask the person.** No stored secret under that name means the masked field comes
   up as it does today, with the agent waiting — the current behaviour is the fallback, not an
   error.
5. **Say it in the pane.** The line that already announces a handoff says which secret was used
   ("✦ answered the password prompt with `sudo`"), so the record shows what was unlocked and when
   without showing what it was.

This gives the owner the outcome — the agent gets through sudo without a person — while keeping the
one invariant that matters, and it reuses a store, a masked field and a refusal path that all exist.

## Tasks

- [ ] Owner: confirm the named-secret shape above, in particular that the model names a secret and <!-- t:a4 -->
      never receives its value
- [ ] `backend/relay_core/keystore.py`: named entries beside preset ids, with `_check_id` widened <!-- t:b6 -->
      and a separate namespace so a secret can never collide with a provider key
- [ ] The masked prompt box gains "remember this as …" so a secret is stored by the person who <!-- t:c8 -->
      owns it (`m_secretEdit`, `src/Pane.h:713-724`)
- [ ] The worker path: a named-secret answer in `program_input.py` that bypasses the `password` <!-- t:d1 -->
      refusal (`:57`, `:213`, `:280`) for that one case and nothing else
- [ ] The GUI path: `programGrant()` (`src/Pane.h:1680`) and `program_state`'s `granted` <!-- t:e3 -->
      (`src/Pane.h:1161`) allow a named-secret answer while a masked screen is up, and continue to
      refuse everything else
- [ ] `backend/relay_core/agent.py:163`: replace "Never type into a password or passphrase prompt" <!-- t:f5 -->
      with the rule as it will then be — one sentence per line, per the SYSTEM prompt's formatting
- [ ] Prove the secret does not leak: a test asserting it is absent from the tool arguments, the <!-- t:g7 -->
      tool result, the session file, `relay.log` and `worker.log`, beside `tests/logging_test.cpp`
      and `tests/test_keystore.py`
- [ ] Live check with a real prompt. Use `sudo -k` and `read -rsp` rather than the owner's real <!-- t:h2 -->
      credentials, and `RELAY_KEYRING=off` everywhere but the one intended run — a direct run
      otherwise writes over the owner's real identity key
- [ ] `docs/AGENT-SESSIONS-PROTOCOL.md` §21 and `docs/VALIDATION.md`'s security-boundaries section, <!-- t:j4 -->
      which currently records the blanket refusal

## Decisions

- 2026-09-19, agent: filed as its own card rather than folded into #R5TC. The owner's line was
  "in general", and #R5TC's own decision — that a turn *started by another pane* gets no
  `program_control` grant — was dropped later the same day, when the owner narrowed #R5TC to
  messages between panes rather than commands: no turn is caused in another pane any more, so there
  is no pane-origin turn to withhold anything from. Either way this card is unaffected — it is
  about a person's own agent answering a prompt in front of them.
- 2026-09-19, agent: the recommendation deliberately does not give the model the value. The
  existing rule guards against a password reaching a model's context, where it would be sent to a
  provider, kept in a conversation and written to a session file; nothing in the owner's request
  needs that, and the named-secret path gives the same outcome without it.
