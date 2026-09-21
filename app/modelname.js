// SPDX-License-Identifier: AGPL-3.0-or-later
// The one name a model has, on the phone (card #MDL1, rule 1 of docs/MODEL-PICKING-DESIGN.md).
//
// The owner, 2026-09-21: "we need to organize and reconcile how model names are listed across the
// app … model names should always be lowercase, no spaces." A model id is what the API takes and
// is not it: `MiniMax-M3`, `openai/gpt-5.6-sol`, `~openai/gpt-sol-latest` are all ids of models a
// person calls `minimax-m3` and `gpt-5.6-sol`.
//
// The worker computes the name (`presets.model_name`) and sends it on every event that names a
// model — `model_name`, `in_flight_model_name`, `from_model_name`, and `model_name` on a saved
// conversation's row — because it is the only side that holds the catalog: nothing here can know
// that the Kimi Coding Plan's `k3` is Kimi K3. `nameOf` is the last step of the rule, for a worker
// too old to send one and for a model id typed by hand, and it is the same derivation as
// `presets.derived_name` and `relay::models::nameOf`; tests/model_name_peer.mjs holds the three to
// each other.

// Everything up to the last "/" goes (a vendor prefix is not part of the name, and stripping it
// collides on none of OpenRouter's 446 ids), a leading "~" goes (OpenRouter writes a moving alias
// `~openai/gpt-sol-latest`), any whitespace becomes "-", and the rest is lower-cased. A serving
// variant keeps whatever marks it — `-highspeed`, `:batch`, `-pro` are different models to the
// person picking one.
export function nameOf(modelId) {
  if (typeof modelId !== 'string') return '';
  const text = modelId.trim().replace(/^~+/, '').split('/').pop().replace(/^~+/, '').trim();
  return text.toLowerCase().split(/\s+/).filter(Boolean).join('-');
}

// The name for a model an event or a row is about: the worker's own, else the derivation. `field`
// is the id's key, and the name's is that key plus "_name" — `model` / `model_name`,
// `in_flight_model` / `in_flight_model_name`, `from_model` / `from_model_name`.
export function modelName(object, field = 'model') {
  if (!object || typeof object !== 'object') return '';
  const sent = object[`${field}_name`];
  if (typeof sent === 'string' && sent) return sent;
  return nameOf(object[field]);
}
