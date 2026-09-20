// SPDX-License-Identifier: AGPL-3.0-or-later
// app/notifykinds.js under Node: the two switches and the five kinds of REMOTE-PROTOCOL.md
// section 9.1, checked against remote/notify.py's list from the Python side.
//
// `node tests/notify_kinds_peer.mjs` prints the mapping and what each combination of switches
// sends as `kinds`, as JSON, for tests/test_remote_push.py.
import { NOTIFY_SWITCHES, ALL_KINDS, switchesFrom, kindsFor } from '../app/notifykinds.js';

const both = { finished: true, needs: true };
const neither = { finished: false, needs: false };

process.stdout.write(JSON.stringify({
  switches: NOTIFY_SWITCHES.map((group) => ({ name: group.name, label: group.label,
    kinds: group.kinds })),
  all: ALL_KINDS,
  sends: {
    both: kindsFor(both),
    finished_only: kindsFor({ finished: true, needs: false }),
    needs_only: kindsFor({ finished: false, needs: true }),
    neither: kindsFor(neither),
  },
  // Reading the desktop's stored list back into switch positions.
  reads: {
    all: switchesFrom(ALL_KINDS),
    // A phone that had turned the finishing group off.
    needs_only: switchesFrom(['waiting_input', 'password']),
    // One kind out of a group still lights the group: an older client's partial list, or a
    // record written before `kinds` existed, must not read as "neither on nor off".
    one_of_a_group: switchesFrom(['plan']),
    // Absent or empty means all of them (section 9.1), so both switches are on.
    empty: switchesFrom([]),
    absent: switchesFrom(undefined),
  },
}));
