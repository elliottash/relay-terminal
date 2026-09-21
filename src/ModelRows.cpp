// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelRows.h"

#include <QComboBox>

namespace relay::modelrows {

QString roleLabel(const QString &role)
{
    static const QHash<QString, QString> labels{
        {QStringLiteral("main"), QStringLiteral("Main agent")},
        {QStringLiteral("flash"), QStringLiteral("Flash agent")},
        {QStringLiteral("lite"), QStringLiteral("Lite agent")},
        {QStringLiteral("local"), QStringLiteral("Local agent")}};
    return labels.value(role, role);
}

// The tier a role runs on (the worker's roles.ROLE_TIERS): what the row's parentheses say. A role's
// protocol name is not a word for a person — the Switchboard's agent read "kimi-k3 (switchboard)"
// (owner, 2026-09-21: "i also dont like how it says switchboard in the picker").
QString roleTier(const QString &role)
{
    static const QHash<QString, QString> tiers{
        {QStringLiteral("switchboard"), QStringLiteral("main")}, {QStringLiteral("subagent"), QStringLiteral("main")},
        {QStringLiteral("planning"), QStringLiteral("high")},
        {QStringLiteral("terminal_use"), QStringLiteral("flash")}, {QStringLiteral("summaries"), QStringLiteral("flash")},
        {QStringLiteral("suggestions"), QStringLiteral("flash")},
        {QStringLiteral("chores"), QStringLiteral("lite")}, {QStringLiteral("audit"), QStringLiteral("lite")},
        {QStringLiteral("loop_check"), QStringLiteral("lite")}};
    return tiers.value(role, role);
}

QString roleRowText(const QString &role, const QString &model)
{
    if (model.isEmpty()) return roleLabel(role);
    // The pane's own model is just the model (owner, 2026-09-21: "i dont want it to say (main)
    // either"): the parentheses are for the rows that are something else — (flash), (local).
    const QString tier = roleTier(role);
    return tier == QStringLiteral("main") ? model : QStringLiteral("%1 (%2)").arg(model, tier);
}

QList<Row> build(const Context &context)
{
    QList<Row> rows;
    // The role rows first (owner direction, 2026-09-19): one flat list, no separate Main / Flash
    // section and no ticks — the collapsed box is the live row, which says it already. Owner
    // report, 2026-09-18: "the main use case for that is going to be swapping between the main and
    // flash models."
    for (const QString &role : context.roles) {
        Row row;
        row.text = roleRowText(role, context.roleModel.value(role));
        row.data = QStringLiteral("role:") + role;
        row.tooltip = context.roleNote.value(role);
        rows << row;
    }
    // Then the catalog's shown entries in rank order (owner, 2026-09-20): one row per model, not
    // per provider, so "glm-5.3 flash" sits beside "glm-5.3". The model the Main row already names
    // is not repeated — it is that row.
    for (const models::Entry &entry : models::shown(context.catalog)) {
        if (!context.mainKey.isEmpty() && entry.key == context.mainKey) continue;
        Row row;
        // An exhausted subscription keeps its row, marked, and the priority skips it.
        row.text = entry.displayName()
            + (models::exhausted(context.catalog, entry.preset, context.now) ? QStringLiteral(" · exhausted")
                                                                            : QString());
        row.data = QStringLiteral("entry:") + entry.key;
        rows << row;
    }
    // Guest agents (26.9), where the caller has any: Claude Code and Codex as rows like any model
    // — no tier, no key, no worker, so they are offered in a pane with no provider at all.
    bool first = true;
    for (const QString &id : context.guests) {
        Row row;
        row.text = context.guestText.value(id, id);
        row.data = QStringLiteral("guest:") + id;
        row.separatorBefore = first;
        first = false;
        rows << row;
    }
    // Last: the picker (every model with filter, sort and reasoning level — the same dialog
    // Ctrl+Alt+M opens) and Options › Models (providers, the checklist, the order). Both stay
    // reachable with no stored key, which is exactly when they are needed most.
    Row more;
    more.text = QStringLiteral("more models…");
    more.data = QStringLiteral("gear:picker");
    more.separatorBefore = true;
    rows << more;
    Row gear;
    gear.text = QString(QChar(0x2699)) + QStringLiteral("  customize…");
    gear.data = QStringLiteral("gear:modelOptions");
    rows << gear;
    return rows;
}

int fill(QComboBox *box, const Context &context)
{
    if (box == nullptr) return -1;
    box->clear();
    for (const Row &row : build(context)) {
        if (row.separatorBefore) box->insertSeparator(box->count());
        box->addItem(row.text, row.data);
        if (!row.tooltip.isEmpty()) box->setItemData(box->count() - 1, row.tooltip, Qt::ToolTipRole);
    }
    const int index = context.current.isEmpty() ? -1 : box->findData(context.current);
    if (index >= 0) box->setCurrentIndex(index);
    return index;
}

QString resolve(const models::Catalog &catalog, const QString &words)
{
    const QString query = words.trimmed();
    if (query.isEmpty()) return QString();
    const QList<models::Entry> rows = models::shown(catalog);
    // The same match the picker's filter makes, shown rows first: its key, its model id, or words
    // of its label and provider.
    const auto exact = [&query](const models::Entry &entry) {
        return entry.key.compare(query, Qt::CaseInsensitive) == 0
            || entry.model.compare(query, Qt::CaseInsensitive) == 0
            || entry.label.compare(query, Qt::CaseInsensitive) == 0;
    };
    for (const auto &entry : rows) if (exact(entry)) return entry.key;
    for (const auto &entry : catalog.entries) if (entry.usable && exact(entry)) return entry.key;
    for (const auto &entry : rows) if (models::matches(entry, query)) return entry.key;
    return QString();
}

}  // namespace relay::modelrows
