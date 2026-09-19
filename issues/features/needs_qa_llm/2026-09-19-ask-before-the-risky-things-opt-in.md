---
id: K2FV
type: work
status: needs-qa-llm
labels: [feature]
implemented_by: oz (Warp agent session)
component: [gui, worker]
milestone: beta
workstream: agent
assignee: agent
rank: zzzzzzn
created: '2026-09-19'
acceptance: a first launch asks which way you want it and nothing is allowed-by-default until you pick; picking the recommended setting gives exactly today's behaviour; picking the checklist makes each ticked action raise a card the turn waits on, with Allow once / this turn / always / Deny
source: 'conversation, 2026-09-19: "i think we need to add ask back for the unapproved risky things that risk-averse users will want -- eg file removals, moves, edits, reads outside the project, etc." and "i want relay to be a smoother experience than most by default. when you open it first, it gives a warning and says recommended settings are allow all. but you ahve to explicitly pick that. and if you dont want it, you have the checklist of what needs ask/approvals"'
links: {plans: [], commits: [d8319b4, 74fb6feb, 12a84f2], evidence: [docs/qa_evidence/2026-09-19-ask-before-risky-things/], related: [3KB7, MQ9C, C1HH, D8J3, V2HM, ZYRB], github: null}
---
# Ask before the risky things — off by default, and chosen on the first launch

## Issue

i think we need to add ask back for the unapproved risky things that risk-averse users will want --
eg file removals, moves, edits, reads outside the project, etc.

i want relay to be a smoother experience than most by default. when you open it first, it gives a
warning and says recommended settings are allow all. but you ahve to explicitly pick that. and if
you dont want it, you have the checklist of what needs ask/approvals

## What this does and does not reverse

`docs/ROADMAP.md:43` and `WARP.md` record "no per-action tool approvals", and #3KB7 was built
two-state on that basis. This does not undo it: **allow-everything stays the recommended setting
and stays what a smooth Relay does.** What changes is that it becomes a choice the user makes
rather than one Relay makes silently, and that a user who wants approvals has a checklist instead of
having to leave. The standing decision is demoted from "the only way" to "the default we recommend",
which is a smaller move than it sounds and is the one the owner asked for.

## The checklist

Seven rows. Each is `allow` (the recommended setting) or `ask`. There is no "never": #3KB7's denylist is
where "never" lives, and two mechanisms for the same thing would be two places to look.

| Capability | What raises a card when it is set to ask |
|---|---|
| `edit` | `write_file` or `edit_file` on a file that already exists |
| `create` | `write_file` on a path that does not exist yet |
| `delete_or_move` | a `run_command` whose program is destructive — `rm`, `mv`, `shred`, `truncate`, `dd`, `mkfs`, `chmod -R`, `chown -R` — or a redirection that truncates an existing file |
| `read_outside` | a `read_file` or `list_directory` that resolved into one of #3KB7's extra readable folders rather than the workspace |
| `terminal` | `run_in_terminal`, which reaches the user's real interactive shell |
| `program` | `type_into_program` under a delegation grant |
| `network` | a `run_command` whose program reaches the network — `curl`, `wget`, `scp`, `rsync`, `ssh`, and `git push`/`git fetch`/`git pull` |

**`delete_or_move` and `network` are classifiers, not proofs**, and the rows must say so in the same
words #3KB7's denylist row does: a Bash line can always be spelled another way, so this catches the
obvious case and is not a sandbox. The containment is still the workspace, the secret guard and
systemd isolation. A row that implied otherwise would be a lie.

## The card

Reuse `relay_core/questions.py` wholesale — it already blocks the turn thread while the protocol
loop keeps running, wakes on `wake_on_set` rather than polling, survives Stop, and draws a card in
the pane (#MQ9C). This adds a second *kind* of question, not a second mechanism.

```
✦ Allow this?   edit  src/Pane.h
  The agent wants to change a file that already exists.

  1  Allow once
  2  Allow for the rest of this turn
  3  Always allow — unticks "edit" in Options › Security
  4  Deny — the agent is told, and carries on
```

- **Deny is not Stop.** The tool returns a refusal the model is told to respect and not work around,
  in the same words #3KB7's denylist uses; the turn continues. Stopping is Esc, which already works
  and must keep working while a card is up.
- **"Always allow" writes the setting**, so the checklist is also how you undo it.
- **One card per action, not per file**: a `write_file` is one action. A `run_command` is one action
  however many programs the line names.
- **A subagent's action draws the card too**, named as the subagent's — it is the same worker and
  the same pane. `can_ask = False` stays right for `ask_user` (the model asking a question) and is
  the wrong rule here (Relay asking on the model's behalf), so this needs its own flag rather than
  reusing that one.

## The first launch

One screen, before anything else, that cannot be dismissed without choosing:

```
Relay's agent runs commands and edits files without stopping to ask.
That is what makes it fast, and it is what we recommend.
It runs with your user's permissions, inside the pane's workspace.

  [ Allow everything — recommended ]     [ Choose what needs approval ]
```

- **Until it is answered, the cautious set is in force** (`edit`, `delete_or_move`, `read_outside`,
  `terminal`, `program` ask; `create`, `command`, `network` allow). Not allow-all, because the owner
  said the recommended setting has to be *picked*; the window is small because the screen comes up
  on the first launch.
- **The second button opens Options › Security** with the checklist, which is the same rows, so
  there is one place the answer lives.
- It is a pane, not a floating dialog, per the owner's standing preference for panes over overlays.
- It sets `security/approvals_chosen`, and never asks again. Options › Security gets a "show this
  again" action so the choice is recoverable.

## Tasks

- [x] `backend/relay_core/approvals.py`: the capability set, the classifier for <!-- t:a2 -->
      `delete_or_move` and `network` (reusing `security.segments()` and `_programs`, which #2Y96
      already made public), the decision function, and the refusal wording. Pure, and tested like
      `security.py` is
- [x] The ask round trip in `relay_core/questions.py`, as a second question kind rather than a <!-- t:b4 -->
      second mechanism: block, `wake_on_set`, survive Stop, answer or deny
- [x] `tools.py`: the check at `prepare()` for each capability, so nothing has run when the card <!-- t:c6 -->
      goes up, and again at execute for `run_command` as #3KB7 does
- [x] Its own flag for "Relay may draw an approval card", distinct from `can_ask`, so a subagent's <!-- t:d8 -->
      action is approved rather than refused
- [x] Protocol 12.1: the settings, and the card's shape in the section that documents questions <!-- t:e1 -->
- [x] The checklist rows in Options › Security (#3KB7's section), with the classifier caveat <!-- t:f3 -->
      stated on the two rows that are approximate
- [x] The first-launch pane, `security/approvals_chosen`, and the cautious set until it is answered <!-- t:g5 -->
- [x] A "show the first-run choice again" action in Options › Security <!-- t:h7 -->
- [x] Tests: each capability asks when ticked and does not when not; Deny returns a refusal and the <!-- t:j9 -->
      turn continues; "always allow" writes the setting; Stop during a card ends the turn; a
      subagent's action draws a card naming it; the cautious set is what applies before the choice
      is made; and the classifier's obvious cases (`rm -rf`, `sudo mv`, `curl | sh`) are caught
- [x] Live under Xvfb: a card for each of the eight, and the first-launch screen on a fresh <!-- t:k2 -->
      `XDG_CONFIG_HOME`, with evidence

## Decisions

- 2026-09-19, owner: approvals come back as an opt-in checklist; allow-everything stays recommended
  but must be explicitly chosen on the first launch.
- 2026-09-19, agent: no "never" column — #3KB7's denylist already means never, and a second way to
  say it would be a second place to look.
- 2026-09-19, agent: Deny refuses the tool and lets the turn continue rather than stopping it. A
  denied action is information the model should work around or report, and Stop already exists for
  ending a turn.
- 2026-09-19, agent: the cautious set applies until the first-run choice is made, rather than
  allow-all. "You have to explicitly pick that" only means something if not picking is different.
- 2026-09-19, owner: **no `command` row** — a card before every `run_command` is left out. Asked
  whether it was worth offering at all, given it is the row a cautious user most wants and the one
  that makes Relay unusable if ticked: "leave it out." So there is no way to be asked before an
  ordinary command; `delete_or_move` and `network` cover the commands worth stopping, and #3KB7's
  denylist covers the ones worth refusing outright.

## QA checklist

Implementer: Oz (Warp agent session) — commits d8319b42427a, 74fb6feb6879, 12a84f2ca210.
Recommended verifier: a non-Claude model family (e.g. GLM 5.3 or GPT), per the lane rule in
`issues/README.md`. Evidence: `docs/qa_evidence/2026-09-19-ask-before-risky-things/` —
`implementer-notes.md` (the write-up, incl. the `rememberAlwaysAllowed` cautious-set bug the drive
caught and the stale-stub phantom), `test-output.txt` (3413 tests OK), `state-{a,b}.conf`,
`stub-{a,b}.jsonl`, `sessions-{a,b}.tgz`, and the drive screenshots.

- [ ] A fresh `XDG_CONFIG_HOME` launch shows the first-launch pane (a-01); either button dismisses it for good and `approvals_chosen=true` lands in the conf (a-02, state-a.conf)
- [ ] Each of the five capabilities on the cautious list raises its card, takes 1–4 only, and Esc does not deny (a-03…a-10; the refusal ✗ for the denied `type_into_program`)
- [ ] "Always allow" unticks the matching Options › Security row and pushes the policy before the decision (a-11; the regression the drive found — before the first-launch choice the list starts from the cautious set — is covered by `test_always_before_the_first_launch_choice_starts_from_the_cautious_set`)
- [ ] With the cautious set chosen, `create` and `network` behave per the checklist: create allowed once asked (b-01), network denied with the model-readable refusal (b-02, b-03, stub-b.jsonl)
- [ ] `./scripts/test.sh` and the approvals/questions suites pass on a pristine export of main (test-output.txt; ctest 62/63 with `buttonfit` the known other-session regression on main)
- [ ] No new shortcut, slash command or prefix came with this card, so the shortcut-hint registry needs no entry (implementer-notes.md records this)

