<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Terms of service for Relay's hosted services

**DRAFT — for the owner's review. Not yet in force, and not legal advice.**
Facts about what the services do come from `docs/RELAY-FREE.md`, `gateway/README.md` and
`docs/REMOTE-PROTOCOL.md` section 8; bracketed items are decisions still to be made.

## 1. What these terms cover

Relay is a Linux terminal with a built-in coding agent. The program itself is free software under
the AGPL-3.0-or-later and runs on your own computer; these terms do not change that licence.
They cover the two small services that Relay can connect to, both operated by
`[operator name and address — Elliott Ash, Switzerland]` (the "operator"):

- **Relay Free**, the inference gateway at `api.relay-terminal.ai`. A fresh install uses it so
  the agent can answer before you have an API key of your own.
- **The hosted rendezvous** at `rv.relay-terminal.ai` and `join.relay-terminal.ai`, which lets a
  phone, tablet or browser join your desktop when you choose the `relay-terminal.ai` address in
  the share dialog.

You accept these terms by using either service. If you do not want them, use your own provider
key (Relay Free steps aside for any pane with a key) and keep the share dialog on a local, tailnet
or Cloudflare address of your own.

## 2. What Relay Free is

Relay Free is a daily token allowance, included with Relay, served by a gateway that holds the
provider keys. The three tiers Relay uses everywhere (Main, Flash, Lite) are mapped on the server
to third-party models; today they are served through OpenRouter, and the mapping can change
without a new Relay release. Limits are enforced on the server: tokens per installation per day,
requests per minute, concurrent requests, and global spending ceilings.

**The allowance may change, shrink or stop.** Relay Free is paid for by the operator. When a
spending ceiling is hit, the service answers "temporarily unavailable" and the terminal keeps
working; only the agent waits. We may change the daily allowance, the models behind each tier or
the limits at any time, and we may withdraw the service. Heavy use is what your own key is for.

## 3. What the hosted rendezvous is

The rendezvous routes bytes it cannot read. Everything a paired device exchanges with your desktop
is encrypted end to end with keys pinned when the device was paired; the server sees device
identifiers, addresses, timings and byte counts, and nothing else. It also forwards notification
payloads, which it cannot open, to your browser's push service. It is offered as a convenience;
running your own rendezvous is supported and documented.

## 4. What is sent to third parties

On Relay Free, your agent prompts, the conversation and the results of the agent's tools (command
output, file contents it reads) go to the gateway and on to the provider serving that tier. Those
providers' own terms and privacy policies apply to what they receive; today that is OpenRouter and,
through it, the model providers it routes to (GLM, DeepSeek and Gemini at the time of writing).

On your own key, nothing passes through the operator's servers.

## 5. Acceptable use

Use the services only through Relay or a client that keeps within the same limits. You must not:

- circumvent, probe or overload the quotas, spend ceilings or rate limits, or fabricate
  installation identities to obtain more than one allowance;
- use the services to break the law, to infringe someone's rights, or to attack other systems;
- resell the allowance or run a service of your own on top of Relay Free;
- use the rendezvous to relay traffic that is not Relay's own protocol.

Relay is open source, so nothing on the client is trusted; the operator may block an installation,
an address or a network that abuses the service, without notice.

## 6. Eligibility

You must be at least `[16 / 18]` years old, or the age at which you can agree to terms like these
where you live, to use the hosted services.

## 7. No warranty, and limits on liability

Both services are provided "as is" and "as available", free of charge, without any warranty of
availability, accuracy or fitness for a purpose. Model output can be wrong, and the agent runs
commands on your machine: you are responsible for what you run. To the extent the law allows, the
operator is not liable for loss or damage arising from the services or from model output. Nothing
here limits liability that cannot be limited by law.

## 8. Ending things

You can stop at any time: add your own key (Options › Models › API keys…), remove the Relay Free
row from the model list, choose a different share address, or uninstall Relay. The operator may
suspend or end your access, or the service itself, at any time, and will try to give notice on
`relay-terminal.ai` when a change affects everyone.

## 9. Changes to these terms

Changes are published at `[URL of this document on relay-terminal.ai]` with the date of the change.
Continuing to use the services after a change means you accept it.

## 10. Law and contact

These terms are governed by the law of `[Switzerland / canton]`, and disputes go to the courts of
`[place]`, unless mandatory law where you live says otherwise. Questions: `[contact address]`.

Last updated: `[date]`.
