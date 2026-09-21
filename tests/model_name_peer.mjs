// SPDX-License-Identifier: AGPL-3.0-or-later
// app/modelname.js under Node: the one naming rule of card #MDL1, from the phone's side.
//
// `node tests/model_name_peer.mjs` prints what `nameOf` and `modelName` answer, as JSON, for
// tests/test_web_model_name.py — which checks the same ids against the worker's
// `presets.derived_name`, so the two derivations cannot drift apart.
import { nameOf, modelName } from '../app/modelname.js';

// Every shape section 1.2 of docs/MODEL-PICKING-DESIGN.md found in the wild.
const IDS = ['gpt-5.6-sol', 'openai/gpt-5.6-sol', '~openai/gpt-sol-latest', 'MiniMax-M3',
             'z-ai/glm-5.3-flashx', 'claude-opus-5', 'moonshotai/kimi-k3:batch',
             'kimi-for-coding-highspeed', 'Some Model', '', '  '];

process.stdout.write(JSON.stringify({
  derived: Object.fromEntries(IDS.map((id) => [id, nameOf(id)])),
  // A non-string id names nothing rather than throwing.
  not_a_string: nameOf(undefined),
  // The worker's own name wins wherever it sent one: nothing here can know that the Kimi Coding
  // Plan's "k3" is Kimi K3.
  prefers_the_worker: modelName({ model: 'k3', model_name: 'kimi-k3' }),
  falls_back: modelName({ model: 'openai/gpt-5.6-sol' }),
  other_field: modelName({ in_flight_model: 'MiniMax-M3' }, 'in_flight_model'),
  other_field_named: modelName({ in_flight_model: 'k3', in_flight_model_name: 'kimi-k3' },
                               'in_flight_model'),
  empty_object: modelName({}),
  not_an_object: modelName(null),
}));
