// SPDX-License-Identifier: AGPL-3.0-or-later
// Two switches over the five notification kinds (docs/REMOTE-PROTOCOL.md section 9.1).
//
// The protocol's unit is a kind — `agent_finished`, `waiting_input`, `password`, `failed`,
// `plan` — and it stays that way: the desktop stores the list, `remote/notify.py` decides per
// kind, and nothing here changes what a push is or when one is sent. What changes is the
// question the phone asks.
//
// Five checkboxes were a settings screen pretending to be a decision. Standing at a bus stop,
// nobody is deciding separately about `plan` and `agent_finished`; they are deciding whether
// this phone should buzz when the work reaches an end, and whether it should buzz when the work
// has stopped until they do something. Claude Code's remote control ships exactly that split
// ("when Claude decides" / "when actions required") and it is the right one, so it is the one
// copied here.
//
// Kept in its own module because it is the whole contract between the two switches and the five
// kinds, and because a pure mapping is worth testing without a browser (tests/notify_kinds_peer.mjs).

export const NOTIFY_SWITCHES = [
  {
    name: 'finished',
    label: 'When an agent finishes or fails',
    // A turn that outlasted you, a turn that died, and a plan waiting to be read: three ways for
    // the work to have reached a stopping point you did not watch.
    kinds: ['agent_finished', 'failed', 'plan'],
  },
  {
    name: 'needs',
    label: 'When something needs me',
    // The two where nothing more happens until the person answers.
    kinds: ['waiting_input', 'password'],
  },
];

// Every kind the two switches cover, in the protocol's own order. The desktop refuses a kind it
// does not know (`remote/notify.py:clean_kinds`), so a switch that grew a kind the protocol never
// added would fail loudly rather than silently — which is what makes this list safe to flatten.
export const ALL_KINDS = NOTIFY_SWITCHES.flatMap((group) => group.kinds);

// Which switches are on, given what the desktop says it is storing.
//
// A group is on when **any** of its kinds is stored. A record written by an older client with
// three of the five ticked, or one written before `kinds` existed at all, then reads as the
// groups it belongs to rather than as neither on nor off — and the first change the person makes
// writes back a whole group, so the half-state does not survive being touched.
export function switchesFrom(kinds) {
  const wanted = Array.isArray(kinds) && kinds.length ? kinds : ALL_KINDS;
  return Object.fromEntries(NOTIFY_SWITCHES.map((group) =>
    [group.name, group.kinds.some((kind) => wanted.includes(kind))]));
}

// What to send as `kinds` for a set of switches. Empty means the person wants none, which is
// unsubscribing rather than a subscription that can never fire — the caller handles that, and it
// is the one case this returns an empty list for.
export function kindsFor(on) {
  return NOTIFY_SWITCHES.filter((group) => on[group.name]).flatMap((group) => group.kinds);
}
