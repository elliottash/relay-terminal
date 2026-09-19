<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Contributing to Relay

Relay is written and maintained by one person, and its licence is AGPL-3.0-or-later. Outside
contributions are welcome on the terms below, which exist so that the project can stay
maintainable and its licensing can stay clear.

## Where work is tracked

The issue tracker is the `issues/` folder in this repository: one Markdown file per issue, named
`YYYY-MM-DD-short-slug.md`, with the format in `docs/SWITCHBOARD-FORMAT.md` and the conventions
in `issues/README.md`. It is Relay's own Switchboard — the same board Relay creates for any project
— so the app can open it. In short:

- Open and in-progress cards sit at the top of `issues/features/` (new capabilities) or
  `issues/changes/` (changes to existing behaviour, including bugs).
- A card that has landed moves to `needs_qa_llm/` in the same commit as the change, and waits there
  for a QA session by a different model family; `done/` holds closed cards with a resolution.
- The `## Issue` section holds the request in the words of whoever asked, never tidied. Never
  delete a card; append.
- `issues/bug_intake.txt` and `issues/feature_intake.txt` are the maintainer's inboxes. Do not
  commit changes to them.

Before starting anything larger than a fix, open a card, or comment on the one that exists, so the
work is visible. Commits that only add or triage cards use `issue: <short title>`.

## Building and testing

`WARP.md` is the working guide: how to build (`scripts/relay-build`), how the tests run (`ctest`,
and `pytest` under `RELAY_KEYRING=off`), and the protocol documents a change must keep in step.
Read it before you send anything.

## The sign-off, and what you are agreeing to

Every commit must carry a `Signed-off-by:` line with your real name and an address you can be
reached at (`git commit -s` adds it). By signing off you certify the **Developer Certificate of
Origin 1.1**, reproduced verbatim in the file `DCO` at the root of this repository: that you wrote
the change or have the right to submit it under the project's licence.

In addition to the DCO, and as a condition of a contribution being accepted, you agree that:

1. Your contribution is licensed to the project under **AGPL-3.0-or-later**, the licence in
   `LICENSE`.
2. You grant the maintainer, Elliott Ash, a perpetual, worldwide, non-exclusive, royalty-free
   licence to use, reproduce, modify, sublicense and distribute your contribution as part of Relay
   **under any licence the maintainer chooses**, including licences other than the AGPL, and to
   relicense the project as a whole together with it. You keep your copyright and every right to
   use your contribution elsewhere as you like.
3. You have the right to make this grant: your employer, or anyone else with a claim on your work,
   has agreed, or has no claim.

That second point is the one that matters. Relay has a single copyright holder today, which is what
makes it possible to change the licence, offer the code under other terms, or move it to a
foundation later. The grant keeps that possible after your change lands; without it every
contributor would have to be found and asked, and a contributor who could not be found would block
the change for everyone.

If you cannot agree to this — for example because of your employer's policy — say so on the card.
A bug report, a design, or a description of a fix that the maintainer then writes himself is still
a real contribution, and needs no grant.

## Pull requests

- Pull requests are accepted from **people**: a named human who signs off the commits and takes
  responsibility for them. Using an AI assistant to write or review the change is fine; say so in
  the commit message (Relay's own history does), and sign off yourself. A pull request opened by a
  bot, or signed off with a name that is not a person's, is closed.
- Keep a pull request to one card. Say which card it is, and move the card as the workflow above
  says.
- It must build, pass `ctest` and the Python tests, and keep the protocol documents it touches
  accurate.
- New source files carry `SPDX-License-Identifier: AGPL-3.0-or-later` at the top, in the comment
  style of the file.
- The maintainer may edit, split or decline a pull request; a declined one is not a judgement on
  the work, and the card records why.

## Reporting a security problem

Do not open a public card for a vulnerability in the remote-access protocol or the hosted
services. Write to `[security contact address]`.
