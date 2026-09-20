// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The helper agent's model box (#BRD3, and #FEJQ §30.7): the rows, the current one, and the
// tooltip, for every panel that carries one.
//
// The Switchboard's composer grew it first — a box in the page agent's composer row, pinned to the
// `switchboard` role, with Follow Main, the tiers, every usable provider and the Model roles gear.
// 2026-09-20 made that agent the *tab's* helper and put its panel in Options, Actions and Sessions
// as well, so there are now four boxes over one role and one worker. They must agree: a pick in
// any of them writes the same setting and reconfigures the same worker, so a box that built its
// rows its own way would be a box that disagrees about which model the helper is on.
//
// So the rows are built here and nowhere else. `BoardView::rebuildModelBox` calls `fill`, and so
// does the window when it builds a box for one of the embedded panels; what differs between them
// is the widget's accessible name and where it is reparented, not a line of the logic.
//
// It is state, not a widget: the caller owns its `CurrentTextComboBox` (the Switchboard's is built
// with the filter row, long before the panel it ends up in exists) and keeps whichever of the
// three events it sees. `State::take` is that bookkeeping — the events name no board and carry no
// root, because they are about the worker and not the cards.
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace relay::helpermodel {

// What the box is built from: the model half of the three worker events that carry it.
struct State {
    QJsonObject roles;     // `configured` / `model_roles`: the roles as the worker resolved them
    QJsonObject tiers;     // the same events: Main, Flash and Lite, and the model each landed on
    QJsonArray presets;    // the `presets` event's rows

    // Take one worker event if it is one of ours — `configured`, `model_roles` or `presets`.
    // True when it was, so a caller can hand this everything the worker says and rebuild on true.
    bool take(const QString &type, const QJsonObject &event);
};

// Fill `box` from that state and answer the tooltip it should carry. The item data is what a pick
// means on the way out: `tier:` (Follow Main), `tier:flash`, `tier:lite`, `preset:<id>`, or `gear`
// for the Model roles dialog. The signals are the caller's to block.
QString fill(QComboBox *box, const State &state);

// What is added to that tooltip while the agent is working. A pick reconfigures the worker and a
// worker refuses a configure mid-turn, so the box waits rather than errors.
QString busyNote();

}  // namespace relay::helpermodel
