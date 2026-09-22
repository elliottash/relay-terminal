<!-- Board "Try it" brief v1 (docs/AGENT-SESSIONS-PROTOCOL.md 31.10, card #JNYN).
     Versioned here, beside board_policy.md and board_cleanup_brief.md, so evals can pin it and
     the owner can change what Try it does without touching code.  It is sent as the *prompt* of
     the Try it turn (relay_core.tryit_protocol.tryit_prompt), not as part of the system prompt,
     so an ordinary card chat never carries it.

     It generalises docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ — stage.py,
     ai-pass.sh, HUMAN-QA.md — and it is written to avoid that worked example's one recorded
     mistake: scenario 2 told the reviewer which test was flaky and then asked whether they could
     find it, which measures following instructions.  Hence the sealed expected.md.

     Owner, 2026-09-21: an app card is verified in three steps — (1) run the tests, (2) an AI
     simulator verifies it, (3) the person is put into a simulated environment that exercises the
     issue.  Try it is step 3.  Step 2 is Verify (#WC3E), and when its record already names a
     staged environment this turn REUSES it instead of staging and playing a second time. -->

You are preparing **Try it** for one card, because somebody pressed the Try it button on it.

An app card is checked in three steps: the tests run, an AI simulator verifies it, and then the
person is put into a simulated environment where the issue actually happens. **You are the third
step, and only the third.** You are not re-running the tests and you are not re-verifying the
work. You open the thing for the person and hand them **one task and one question** — the one
judgement a machine cannot make. The expected result is written down, but not where they can read
it before they answer.

You have this turn only. Work steadily, and if you cannot open it, say so and stop (step 7).

## 1. Read the card

`board_read` it. What matters is `## Issue` (what was asked, in the owner's own words),
`## Done means` (the expectations, written before the work) and `## Tests` (what is already
automated — you are not going to ask a person to run those). Read the `## QA checklist` and the
thread's last entries for what the verifying session found. Read the code the card names if the
card is thin.

From that, answer one question for yourself: **what is the thing to open?**

- **The app.** A disposable project — a throwaway directory with a git repository, a board,
  seeded files and whatever data makes the card's problem actually happen — with Relay open on it.
- **A backend behaviour.** A request and its response, or a reproduction of the failure, as one
  command that prints both.
- **A command-line tool.** A before and an after on a real file.

Nothing about the card's labels decides this; what the card asks for does. A card with no
`## Done means` is still a card you can do this for — read `## Issue` and say in the section what
you took it to mean.

## 2. Is it already staged?

The head of this prompt tells you, in one line, whether the verifying session left a staged
environment behind: a directory under `docs/qa_evidence/<date>-verify-<ID>/` with a rerunnable
`stage.sh` in it. That is the simulated environment the AI pass used, which means it is the
environment in which the issue is known to happen.

**When there is one, reuse it. Do not stage a second one, and do not replay the mechanical
steps** — that was step 2's job and it has been done.

- Run its `stage.sh` once, to be sure it still works and to see what it prints. Read
  `staging-notes.md` beside it, if there is one, for how that environment differs from real use.
- Open the thing it stages and take **one** capture — a screenshot, or the command's output —
  proving it opens and the situation is there. One, not one per step.
- Write `## Try it` (step 4) with **that** `stage.sh` (or the one line it prints) as the open
  line, and point the section's sealed file at *your* evidence directory.
- If `stage.sh` fails or stages something that no longer shows the problem, that is a finding:
  say so on the thread, and either fix the one obvious thing or fall through to step 3 and stage
  it yourself. Never hand a person a fixture you did not see work.

**When there is none**, the card was not verified that way — a backend or command-line card
often will not have been — and step 3 is yours.

## 3. Stage it yourself, and do the mechanical pass

The fixture is **disposable and rerunnable**, and it is not the owner's working tree.

- Put it under the short run directory the head of this prompt names (`/tmp/claude-…`). Short,
  because a socket path over 108 bytes breaks the app; disposable, because the person must be
  able to delete it without thinking about it.
- Seed it so the **problem is happening** — the wrong number is in the file, the queue is stuck,
  the eight cards are already there. A person put in front of an empty fixture has to build the
  situation themselves, which is the work you were meant to do.
- Where the card is about the app, name the binary: the one the card names if it names one,
  otherwise `build/relay` of this checkout. Say which, with its path, in the section.
- Write the whole staging as one script, **`<evidence dir>/stage.sh`**, that takes no arguments,
  needs no model and no network, and can be run twice. It creates the fixture and prints the one
  line that opens it. The person runs that script, or presses the button that runs it.
- Then write **`<evidence dir>/staging-notes.md`**: one paragraph saying how this staged
  environment differs from real use — the seeded data is invented, the binary is a build from
  before two wording fixes, the project is small, there is no second machine. One paragraph, and
  do not skip it: a person who finds a difference you did not name stops trusting the rest.

Then do every mechanical step yourself, **in the real thing** — not in a unit test and not in
your head. Stop only where a judgement starts.

For the app:

- An **isolated display and profile**, always: an `Xvfb` display nobody is using, `HOME`,
  `XDG_RUNTIME_DIR`, `TMPDIR`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME` all under
  a short sandbox directory of your own, and `RELAY_KEYRING=off` so the owner's real identity key
  is never touched. `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ai-pass.sh` is
  the recipe; copy it and change the steps.
- **Input is `xdotool` only** — clicks, chords and text typed into the app's own fields. Never
  type into a terminal pane inside Relay: that is a shell, and this is someone's machine.
- **One screenshot per step**, named in order, under the evidence directory.

For a backend behaviour or a command-line tool: run the command, and save its output as one
capture file per step under the same directory. A failure reproduction saves the failure.

Then **look at what you captured**. A screenshot of a window that never opened, or a capture of a
traceback you did not expect, is a finding — fix it if it is yours to fix, and say so in the
evidence entry either way. Do not hand a person a pass you did not read.

## 4. Write `## Try it` on the card

One `board_update_card` with `replace_section {heading: "Try it", …}`. Three parts, in this
order, and nothing else:

1. **How to open it** — *one* line: one command, or one path, or one `relay://` link. The board
   draws this line as a button, so it is one thing, not a setup procedure. Everything else the
   opening needs is inside `stage.sh`.
2. **The task** — one short paragraph in plain words, saying what to do in the staged thing. Two
   or three sentences. Not numbered steps; the steps are already done.
3. **The question** — exactly one, and one whose answer is a judgement: did this stop you, could
   you tell what it meant, would you use it. Never a question whose answer you have just put in
   the sentence above it.

Then a last line naming the sealed file: `Expected: <evidence dir>/expected.md (sealed until you
answer)`. **Write the expected result into that file, and nowhere on the card.** This is the
whole point: the person is given the problem, not the answer, and what they were supposed to see
is revealed under the section once they have answered. A card that tells them what to look for
measures whether they can follow instructions.

Keep the section short enough to read in under a minute. If something has to be explained at
length, it belongs in the staging notes.

## 5. Record what you saw

One `board_comment` of kind `evidence`: the evidence directory, whether you reused the verifying
session's staging or made your own, and what you actually saw when you opened it (not what you
expected). This is what makes the person's minute cheap — they are reading the one unanswered
question, not re-running anybody's work.

## 6. Do not

- **Do not change the code**, run the test suite, or move the card. Try it opens and hands over;
  it decides nothing.
- **Do not write `## QA checklist`, `## Verdict` or `## Human QA`.** The first two are other
  people's; `## Human QA` is generated from your section and the person's answer, and typing it
  yourself would be the same thing written twice.
- **Do not ask the person to run tests, attach evidence, filter a list or open a flame graph.**
  Those are mechanical. If one of them is genuinely part of the judgement, do it yourself first
  and ask about the result.
- **Do not touch `issues/bug_intake.txt` or `issues/feature_intake.txt`** — the owner's inboxes —
  and never run a git command that writes.

## 7. If you cannot open it

It fails for honest reasons: the binary will not build, the verifying session's `stage.sh` is
broken and the card is about a machine you do not have, the fixture needs a key nobody gave you,
opening it crashed and you could not get past it.

Then write **one `board_comment` of kind `note`** whose first words are exactly:

    Try it could not be staged: <why, in one or two sentences>

and **write no `## Try it` section at all**. A card that asks a person to review something that
was never staged is worse than a card that says it could not be staged. Say what you got as far
as, leave whatever you did capture in the evidence directory, and end the turn.

## Finish with a report

Your last message is what the person reads in the board's notice area. Three lines at most: what
you opened, where the evidence is, and the one question you are asking them.
