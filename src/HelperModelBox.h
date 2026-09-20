// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The helper agent's model box (#BRD3, #FEJQ §30.7, #PK5Q): the current row and the tooltip, for
// every panel that carries one.
//
// The Switchboard's composer grew it first — a box in the page agent's composer row, pinned to the
// `switchboard` role. 2026-09-20 made that agent the *tab's* helper and put its panel in Options,
// Actions and Sessions as well, so there are now four boxes over one role and one worker. They
// must agree: a pick in any of them writes the same setting and reconfigures the same worker, so a
// box that built its rows its own way would be a box that disagrees about which model the helper
// is on.
//
// Since #PK5Q they agree with the terminal pane too. The owner, 2026-09-20: "can you have the
// picker be the same as in the main terminal." The *rows* are relay::modelrows' (src/ModelRows.h)
// and are built nowhere else; what is left here is the translation from the three worker events
// that carry a helper's state into that builder's plain data — which model each tier landed on,
// which row the resolved role is sitting on, and the tooltip the role's warning and note ride in.
//
// It is state, not a widget: the caller owns its `CurrentTextComboBox` (the Switchboard's is built
// with the filter row, long before the panel it ends up in exists) and keeps whichever of the
// three events it sees. `State::take` is that bookkeeping — the events name no board and carry no
// root, because they are about the worker and not the cards.
#include "ModelRows.h"

#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace relay::helpermodel {

// The role every helper box writes. One worker per tab serves the Switchboard *and* the Options,
// Actions and Sessions panels (protocol 30.7), and they all run on this one role.
inline const QString &kRole()
{
    static const QString role = QStringLiteral("switchboard");
    return role;
}

// What the box is built from: the model half of the three worker events that carry it.
struct State {
    QJsonObject roles;     // `configured` / `model_roles`: the roles as the worker resolved them
    QJsonObject tiers;     // the same events: Main, Flash, Lite and Local, and the model each landed on
    QJsonArray presets;    // the `presets` event's rows

    // Take one worker event if it is one of ours — `configured`, `model_roles` or `presets`.
    // True when it was, so a caller can hand this everything the worker says and rebuild on true.
    bool take(const QString &type, const QJsonObject &event);
};

// The rows this state draws, as the shared builder wants them. Public so the window can read the
// same catalog when it opens the ModelPicker dialog over a helper's box.
modelrows::Context context(const State &state);
// Which row the resolved `switchboard` role is sitting on: `role:main|flash|local`, or
// `entry:<preset>|<model>` for a role pinned to a provider and a model of its own.
QString currentRow(const State &state);

// Fill `box` from that state and answer the tooltip it should carry. The item data is what a pick
// means on the way out, in the terminal pane's own words: `role:main`, `role:flash`, `role:local`,
// `entry:<preset>|<model>`, `gear:picker` (the ModelPicker dialog) or `gear:modelOptions`
// (Options › Models). The signals are the caller's to block.
QString fill(QComboBox *box, const State &state);

// What is added to that tooltip while the agent is working. A pick reconfigures the worker and a
// worker refuses a configure mid-turn, so the box waits rather than errors.
QString busyNote();

// What a helper says when a guest harness row is picked for it (card #GH5T): the helper works
// through Relay's own tools, which a guest does not take, so the row is refused rather than stored.
QString guestRefusal(const QString &name);

}  // namespace relay::helpermodel
