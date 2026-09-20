// SPDX-License-Identifier: AGPL-3.0-or-later
#include "HelperModelBox.h"

#include "ModelRows.h"

#include <QDateTime>

namespace relay::helpermodel {

namespace {

// A model id in the words a terminal pane's box uses for it (Pane::conciseModel): the service for
// Relay Free and for a guest, whose ids name nothing a person recognises; the model id otherwise,
// with "· local" for a server on this machine. The rows have to *read* the same, not only mean the
// same — a live run caught the Main row saying "stub (main)" beside the pane's "stub · local
// (main)", which is two boxes disagreeing about one model.
QString conciseModel(const QJsonArray &presets, const QString &presetId, const QString &model)
{
    for (const QJsonValue &value : presets) {
        const QJsonObject preset = value.toObject();
        if (preset.value(QStringLiteral("id")).toString() != presetId) continue;
        if (preset.value(QStringLiteral("group")).toString() == QStringLiteral("guest")
            || preset.value(QStringLiteral("hosted")).toBool() || model.isEmpty())
            return preset.value(QStringLiteral("label")).toString();
        return model.section(QLatin1Char('/'), -1).toLower()
             + (preset.value(QStringLiteral("local")).toBool() ? QStringLiteral(" · local") : QString());
    }
    return model.section(QLatin1Char('/'), -1).toLower();
}

// The model a tier landed on right now, for its role row. A tier whose provider has no key steps
// towards Main — its `using` says so — and then the row names the role alone, with the tier's note
// as the row's own tooltip, exactly as a terminal pane's Local row does when nothing is served.
QString tierModel(const QJsonArray &presets, const QJsonObject &tiers, const QString &tier)
{
    const QJsonObject entry = tiers.value(tier).toObject();
    if (entry.isEmpty()) return {};
    if (tier != QStringLiteral("main")
        && entry.value(QStringLiteral("using")).toString() != tier) return {};
    const QString model = entry.value(QStringLiteral("model")).toString();
    // Only the Main row goes through the concise wording, because only the pane's does
    // (Pane::roleRowModel): the Flash and Local rows print the model id the worker resolved, and
    // "stub · local (local)" would say local twice. The rows have to match, not merely be tidy.
    if (tier != QStringLiteral("main")) return model;
    return conciseModel(presets, entry.value(QStringLiteral("preset")).toString(), model);
}

QString tierNote(const QJsonObject &tiers, const QString &tier)
{
    return tiers.value(tier).toObject().value(QStringLiteral("note")).toString();
}

// Whether this machine serves a model at all: a `presets` row with `local: true` (card #24XJ).
// The Local row is offered only then, for the reason a pane offers it only then — a row that
// always resolved back to Main would be a promise the box cannot keep.
bool hasLocalEndpoint(const QJsonArray &presets)
{
    for (const QJsonValue &value : presets)
        if (value.toObject().value(QStringLiteral("local")).toBool()) return true;
    return false;
}

}  // namespace

bool State::take(const QString &type, const QJsonObject &event)
{
    if (type == QStringLiteral("configured") || type == QStringLiteral("model_roles")) {
        roles = event.value(QStringLiteral("roles")).toObject();
        tiers = event.value(QStringLiteral("tiers")).toObject();
        return true;
    }
    if (type == QStringLiteral("presets")) {
        presets = event.value(QStringLiteral("presets")).toArray();
        return true;
    }
    return false;
}

QString currentRow(const State &state)
{
    // A tiered role reports the preset its tier landed on (`preset` is where it answered, not what
    // was picked), so the tier wins; only a role with no tier and a preset of its own is pinned to
    // a provider and a model. Everything else follows Main.
    const QJsonObject role = state.roles.value(kRole()).toObject();
    const QString tier = role.value(QStringLiteral("tier")).toString();
    if (tier == QStringLiteral("flash") || tier == QStringLiteral("local"))
        return QStringLiteral("role:") + tier;
    if (tier.isEmpty() && role.value(QStringLiteral("source")).toString() == QStringLiteral("configured")) {
        const QString preset = role.value(QStringLiteral("preset")).toString();
        const QString model = role.value(QStringLiteral("model")).toString();
        if (!preset.isEmpty() && !model.isEmpty())
            return QStringLiteral("entry:") + models::Catalog::keyFor(preset, model);
    }
    return QStringLiteral("role:main");
}

modelrows::Context context(const State &state)
{
    modelrows::Context rows;
    rows.catalog = models::catalogFrom(state.presets);
    rows.now = QDateTime::currentSecsSinceEpoch();
    if (hasLocalEndpoint(state.presets)) rows.roles << QStringLiteral("local");
    for (const QString &role : std::as_const(rows.roles)) {
        rows.roleModel.insert(role, tierModel(state.presets, state.tiers, role));
        rows.roleNote.insert(role, tierNote(state.tiers, role));
    }
    // The entry the Main row names — the model the helper's Main tier landed on. A terminal pane
    // passes its own model here for the same reason: the row above says it, so the catalog below
    // does not say it again.
    const QJsonObject main = state.tiers.value(QStringLiteral("main")).toObject();
    const QString mainPreset = main.value(QStringLiteral("preset")).toString();
    const QString mainModel = main.value(QStringLiteral("model")).toString();
    if (!mainPreset.isEmpty() && !mainModel.isEmpty())
        rows.mainKey = models::Catalog::keyFor(mainPreset, mainModel);
    // No guest rows: a guest agent can be a *pane's* own agent, never the helper's. The helper
    // works through Relay's own `board_*` and `app_*` tools, which a guest does not take (#GH5T,
    // #4NXH). A guest harness the worker offers as a preset is still in the catalog above, and
    // still a row — the owner asked for the same list — but picking it is refused with the
    // sentence guest_harness_provider.helper_refusal writes, rather than quietly missing.
    rows.current = currentRow(state);
    return rows;
}

QString fill(QComboBox *box, const State &state)
{
    if (box == nullptr) return {};
    modelrows::fill(box, context(state));

    QString tip = QStringLiteral("The model the helper agent runs on — the Switchboard's agent "
                                 "and the helpers in Options, Actions and Sessions are one worker "
                                 "per tab. A pick writes the switchboard role, the same setting "
                                 "the Model roles dialog edits, and reconfigures every open "
                                 "board's worker.");
    const QJsonObject role = state.roles.value(kRole()).toObject();
    const QString warning = role.value(QStringLiteral("warning")).toString();
    const QString note = role.value(QStringLiteral("note")).toString();
    if (!warning.isEmpty()) tip += QStringLiteral("\n\n") + warning;
    if (!note.isEmpty()) tip += QStringLiteral("\n\n") + note;
    return tip;
}

QString busyNote()
{
    return QStringLiteral("\n\nDisabled while the agent is working — a pick reconfigures its "
                          "worker, which cannot move mid-turn.");
}

QString guestRefusal(const QString &name)
{
    // The sentence backend/relay_core/guest_harness_provider.helper_refusal writes, said here
    // instead — the pick is refused before it is stored, so the worker never has to raise it.
    return QStringLiteral("The helper agent cannot run on %1. Add a provider under Options › "
                          "Models, or pick a model for the helper in its model box.")
        .arg(name.isEmpty() ? QStringLiteral("a guest session") : name);
}

}  // namespace relay::helpermodel
