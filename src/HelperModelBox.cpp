// SPDX-License-Identifier: AGPL-3.0-or-later
#include "HelperModelBox.h"

namespace relay::helpermodel {

namespace {

// One row per preset the helper agent could actually run on: a stored key, Relay Free (usable on
// this machine), or a local model server. Guest rows are harnesses, not endpoints —
// roles.validate_roles would refuse their ids — so they are never offered.
bool usable(const QJsonObject &preset)
{
    if (preset.value(QStringLiteral("group")).toString() == QStringLiteral("guest"))
        return false;
    return preset.value(QStringLiteral("has_stored_key")).toBool()
           || preset.value(QStringLiteral("local")).toBool()
           || (preset.value(QStringLiteral("hosted")).toBool()
               && preset.value(QStringLiteral("available")).toBool());
}

// A provider row's text, in the pane's model box's words (Pane::conciseModel): the service for
// Relay Free, whose gateway model ids name nothing a person recognises; the model id otherwise,
// with "· local" for a server on this machine.
QString modelText(const QJsonObject &preset)
{
    const QString label = preset.value(QStringLiteral("label")).toString();
    if (preset.value(QStringLiteral("hosted")).toBool())
        return label;
    const QString model = preset.value(QStringLiteral("model")).toString();
    if (model.isEmpty())
        return label;
    return model.section(QLatin1Char('/'), -1).toLower()
           + (preset.value(QStringLiteral("local")).toBool() ? QStringLiteral(" · local") : QString());
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

QString fill(QComboBox *box, const State &state)
{
    if (box == nullptr)
        return {};
    box->clear();
    // The role as the worker last resolved it decides the current row, and its warning or note
    // rides in the tooltip (a picked provider with no stored key falls back to Main, 13.7).
    const QJsonObject role = state.roles.value(QStringLiteral("switchboard")).toObject();
    const QString main = state.tiers.value(QStringLiteral("main")).toObject()
                             .value(QStringLiteral("model")).toString();
    box->addItem(main.isEmpty() ? QStringLiteral("Follow Main")
                                : QStringLiteral("Follow Main — %1").arg(main),
                 QStringLiteral("tier:"));
    // Flash and Lite, with the model each lands on right now. A tier whose provider has no key
    // steps towards Main — its `using` says so — and its row names the tier alone, with the
    // tier's note as the row's own tooltip.
    for (const QString &tier : {QStringLiteral("flash"), QStringLiteral("lite")}) {
        const QJsonObject entry = state.tiers.value(tier).toObject();
        if (entry.isEmpty())
            continue;
        const QString name = tier.left(1).toUpper() + tier.mid(1);
        const QString model = entry.value(QStringLiteral("model")).toString();
        const bool fellBack = entry.value(QStringLiteral("using")).toString() != tier;
        box->addItem(fellBack || model.isEmpty() ? name
                                                 : QStringLiteral("%1 — %2").arg(name, model),
                     QStringLiteral("tier:") + tier);
        const QString note = entry.value(QStringLiteral("note")).toString();
        if (!note.isEmpty())
            box->setItemData(box->count() - 1, note, Qt::ToolTipRole);
    }
    bool separated = false;
    for (const QJsonValue &value : state.presets) {
        const QJsonObject preset = value.toObject();
        if (!usable(preset))
            continue;
        if (!separated) {
            box->insertSeparator(box->count());
            separated = true;
        }
        box->addItem(modelText(preset),
                     QStringLiteral("preset:") + preset.value(QStringLiteral("id")).toString());
    }
    // The Model roles dialog, where this same role is a row (its Advanced list) with an effort
    // and a model id of its own — reachable with no provider at all, when it is needed most.
    box->insertSeparator(box->count());
    box->addItem(QString(QChar(0x2699)) + QStringLiteral("  Model roles…"),
                 QStringLiteral("gear"));

    // Which row is current: a tiered role reports the preset its tier landed on (`preset` is
    // where it answered, not what was picked), so the tier wins; only a role with no tier and a
    // preset of its own is pinned to a provider. Everything else follows Main.
    QString wanted = QStringLiteral("tier:");
    const QString preset = role.value(QStringLiteral("preset")).toString();
    const QString tier = role.value(QStringLiteral("tier")).toString();
    if (tier == QStringLiteral("flash") || tier == QStringLiteral("lite"))
        wanted = QStringLiteral("tier:") + tier;
    else if (tier.isEmpty()
             && role.value(QStringLiteral("source")).toString() == QStringLiteral("configured")
             && !preset.isEmpty())
        wanted = QStringLiteral("preset:") + preset;
    if (box->findData(wanted) < 0)
        wanted = QStringLiteral("tier:");
    box->setCurrentIndex(qMax(0, box->findData(wanted)));

    QString tip = QStringLiteral("The model the helper agent runs on — the Switchboard's agent "
                                 "and the helpers in Options, Actions and Sessions are one worker "
                                 "per tab. A pick writes the switchboard role, the same setting "
                                 "the Model roles dialog edits, and reconfigures every open "
                                 "board's worker.");
    const QString warning = role.value(QStringLiteral("warning")).toString();
    const QString note = role.value(QStringLiteral("note")).toString();
    if (!warning.isEmpty())
        tip += QStringLiteral("\n\n") + warning;
    if (!note.isEmpty())
        tip += QStringLiteral("\n\n") + note;
    return tip;
}

QString busyNote()
{
    return QStringLiteral("\n\nDisabled while the agent is working — a pick reconfigures its "
                          "worker, which cannot move mid-turn.");
}

}  // namespace relay::helpermodel
