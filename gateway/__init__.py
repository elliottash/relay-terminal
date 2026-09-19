# SPDX-License-Identifier: AGPL-3.0-or-later
"""relay-gateway: the hosted inference service behind Relay Free.

A fresh install has no provider key, so its first agent turn goes here: the desktop registers an
installation (proof of possession of an X25519 key, as with the rendezvous), takes a short-lived
bearer token, and posts OpenAI-shaped chat completions with ``model`` set to a *role* —
``relay-main``, ``relay-flash``, ``relay-lite`` — never an upstream model name. The gateway rebuilds
the request from validated fields, adds the operator's own provider key, streams the reply back,
and counts tokens against a per-installation daily allowance and the operator's spend ceilings.

It keeps metadata only (docs/RELAY-FREE.md): installation ids, counts, times and token totals.
Never a message, never a key.
"""
