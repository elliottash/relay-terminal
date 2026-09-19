<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Privacy policy for Relay and its hosted services

**DRAFT — for the owner's review. Not yet in force, and not legal advice.**
Every statement below about what is stored comes from `docs/RELAY-FREE.md`, `gateway/README.md`,
`gateway/store.py`, `site/free.html`, `docs/REMOTE-PROTOCOL.md` section 8 and
`rendezvous/server.py`; bracketed items are still to be decided.

## Who is responsible

The controller for the hosted services is `[operator name and postal address — Elliott Ash,
Switzerland]`, reachable at `[contact address]`. The services run on a server rented from Hetzner in
Germany, reached through Cloudflare. The Swiss Federal Act on Data Protection applies; if you are in
the EU or EEA, the GDPR gives you the same rights, and they are listed below.

## Relay on your computer

Relay itself has no telemetry: no analytics, no crash reporting, no account. Shell commands you
run, and terminal output, are never sent anywhere on their own. API keys you add are kept in your
desktop keyring. On your own provider key, the only network traffic is to that provider.

## Relay Free (`api.relay-terminal.ai`)

**What is sent.** When a pane uses Relay Free, the agent's prompts, the conversation and the
results of the agent's tools (command output, file contents it reads) go to the gateway and on to
the third-party provider serving that tier. Today the tiers are served through OpenRouter, which
routes to the model providers named on `relay-terminal.ai/free`. Their privacy policies apply to what they receive.

**Identity.** Relay makes a keypair on first use. The private half stays in your desktop keyring
(or a file only you can read); the gateway derives an installation id from the public half. There
is no account, no e-mail and no name.

**What the gateway stores.**

- A registration record per installation: the installation id, its public key, a hash of the
  current one-hour token, the plan, first and last seen, and the Relay version. Kept while the
  installation is registered; `[the owner should set an inactivity cutoff — none is applied today]`.
- Daily usage per installation (requests and token counts), kept 35 days so the daily allowance
  can be enforced.
- One line of metadata per request: time, a hash of the installation id, the tier, the provider,
  the status, time to first token, total time, token counts and any error code. Never the text of a
  message, never file contents, never terminal output, never a provider key. Kept 7 days.
- Daily spend totals per provider and tier, with no installation id, kept as the operator's ledger.
- A hash of your IP address, counted per hour for two hours, to limit registrations per address.

Body logging exists only as an operator diagnostic switch, off by default and announced at
startup; it is turned off again as soon as the problem is found.

**Why.** To serve the allowance, enforce quotas and spending ceilings, keep the service up, and
prevent abuse. The legal basis is performance of the service you asked for and the operator's
legitimate interest in running it without being drained.

## The hosted rendezvous (`rv.relay-terminal.ai`, `join.relay-terminal.ai`)

**What it can see.** Only ciphertext passes through it: the content of your sessions is encrypted
between your desktop and your device with keys that never reach the server. The server sees which
device talks to which desktop, when, and how many bytes.

**What it stores.**

- A registry entry per desktop: a desktop id derived from its public key, the public key, a token
  hash, and first and last seen. Kept while the desktop is registered.
- Pairing rooms for five minutes; invite rooms for up to seven days, as the invite asks.
- Metadata (ids, addresses, timings, byte counts) for 7 days. No content, no analytics.
- The server's own signing key for Web Push. No push subscription keys: those go to your desktop,
  never here.

**Notifications.** When you turn notifications on for a paired device, your desktop hands the
rendezvous a sealed payload and the endpoint URL of your browser's push service (Google, Mozilla,
Apple or Microsoft, depending on the browser); the rendezvous posts it there without being able to
read it. That push service's privacy policy applies to delivery.

**Why.** To pair devices, route encrypted traffic and deliver notifications, as the service you asked for.

## What we do not do

We do not sell personal data, run analytics or advertising, or train models on your prompts.
Whether a third-party provider trains on what it receives is governed by its own terms. We share
data only with the providers named above, as needed to serve a request, and with authorities
where the law requires it.

## Your rights

You can ask for a copy of the data held about your installation or desktop, ask for it to be
corrected or deleted, object to its processing, and complain to a supervisory authority (in
Switzerland, the FDPIC; in the EU, your national authority). Because there is no account, please
send your installation id or desktop id with the request; Relay shows the installation id in
`[Options › Privacy]`. Requests go to `[contact address]` and are answered within 30 days.

## How to stop

- Add your own provider key: new panes use it and nothing passes through the gateway.
- Remove the Relay Free row from the model list.
- Choose a local, tailnet or Cloudflare address in the share dialog instead of `relay-terminal.ai`.
- Uninstall Relay. The 7-day metadata expires on its own; ask at the address above to have the
  registration record removed sooner.

## Changes

This policy is published at `[URL on relay-terminal.ai]` with the date of its last change.

Last updated: `[date]`.
