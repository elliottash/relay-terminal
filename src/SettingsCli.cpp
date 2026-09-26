// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SettingsCli.h"

#include "Keymap.h"
#include "SettingsExport.h"

#include <QJsonObject>
#include <QSet>
#include <QSettings>

#include <cstdio>

namespace relay {

// Card #05J2: the headless path behind --export-settings / --import-settings. Export writes
// the bundle; import plans the merge, prints it, and — unless the caller bulk-resolves the
// conflicts — stops with exit 2 and reports instead of choosing for you.
int settingsTransferCli(bool exportMode, const QString &path, bool includeMemories,
                        const QString &resolve, bool acceptAttention)
{
    using namespace settingsexport;

    if (exportMode) {
        ExportOptions options;
        options.includeMemories = includeMemories;
        const QJsonObject bundle = collectBundle(options);
        QString error;
        if (!writeBundleToFile(bundle, path, &error)) {
            fprintf(stderr, "relay: %s\n", qPrintable(error));
            return 1;
        }
        printf("exported %d setting(s), %d hotkey(s), %d alias(es), %d endpoint(s), %d theme(s)%s -> %s\n",
               bundle.value(QStringLiteral("settings")).toObject().size(),
               bundle.value(QStringLiteral("hotkeys")).toObject().size(),
               bundle.value(QStringLiteral("aliases")).toArray().size(),
               bundle.value(QStringLiteral("endpoints")).toArray().size(),
               bundle.value(QStringLiteral("themes")).toArray().size(),
               includeMemories ? "" : " (--include-memories skips memories)",
               qPrintable(path));
        return 0;
    }

    QString error;
    const QJsonObject bundle = readBundle(path, &error);
    if (bundle.isEmpty()) {
        fprintf(stderr, "relay: %s\n", qPrintable(error));
        return 1;
    }

    Hooks hooks;
    hooks.isKnownAction = [](const QString &id) {
        for (const ActionDef &action : Keymap::instance().actions())
            if (action.id == id) return true;
        return false;
    };
    // Without the workers running, "known to this install" means configured here:
    // ids already referenced by roles, tiers or the models group.
    QSet<QString> knownModels;
    {
        QSettings settings;
        for (const QString &key : settings.allKeys()) {
            const bool relevant = key.startsWith(QStringLiteral("roles/"))
                                  || key.startsWith(QStringLiteral("tiers/"))
                                  || key.startsWith(QStringLiteral("models/priority"))
                                  || key.startsWith(QStringLiteral("models/favorites"))
                                  || key.startsWith(QStringLiteral("models/custom"))
                                  || key.startsWith(QStringLiteral("models/profiles/"))
                                  || key == QStringLiteral("models/fallback")
                                  || key == QStringLiteral("models/fallbacks");
            if (!relevant) continue;
            const QVariant value = settings.value(key);
            for (const QString &id : value.toStringList()) if (!id.isEmpty()) knownModels.insert(id);
            if (!value.toString().isEmpty()) knownModels.insert(value.toString());
        }
    }
    hooks.isKnownModel = [knownModels](const QString &id) { return knownModels.contains(id); };

    ImportPlan plan = planImport(bundle, path, hooks);
    if (!resolve.isEmpty()) {
        if (resolve != QStringLiteral("keep") && resolve != QStringLiteral("imported")) {
            fprintf(stderr, "relay: --import-resolve must be keep or imported\n");
            return 1;
        }
        plan.resolveAllConflicts(resolve == QStringLiteral("keep") ? Resolution::KeepCurrent
                                                                   : Resolution::UseImported);
    }
    // --import-accept-attention is the headless form of the review screen's "Apply anyway"
    // checkboxes: items this install cannot verify (unknown model ids) apply on explicit say-so.
    if (acceptAttention) {
        for (PlanItem &item : plan.items)
            if (item.status == ItemStatus::Attention) item.resolution = Resolution::UseImported;
    }

    auto statusName = [](ItemStatus status) {
        switch (status) {
        case ItemStatus::Auto: return "auto";
        case ItemStatus::Conflict: return "CONFLICT";
        case ItemStatus::Attention: return "attention";
        case ItemStatus::Skip: return "skip";
        }
        return "?";
    };
    printf("Import plan for %s:\n", qPrintable(path));
    for (const PlanItem &item : plan.items) {
        const QString reason = item.reason.isEmpty() ? QString() : QStringLiteral("  (%1)").arg(item.reason);
        printf("  %-9s %-38s %s -> %s%s\n", statusName(item.status), qPrintable(item.id),
               qPrintable(item.currentDisplay.isEmpty() ? QStringLiteral("-") : item.currentDisplay),
               qPrintable(item.incomingDisplay.isEmpty() ? QStringLiteral("-") : item.incomingDisplay),
               qPrintable(reason));
    }
    if (plan.hasUnresolvedConflicts() || plan.hasUnacceptedAttention()) {
        fprintf(stderr, "relay: %d conflict(s) and %d item(s) needing attention; nothing was changed.\n"
                        "        Decide in Options > General > Import settings… (the review screen), or pass\n"
                        "        --import-resolve keep|imported (--import-accept-attention for the latter).\n",
                plan.count(ItemStatus::Conflict), plan.count(ItemStatus::Attention));
        return 2;
    }

    const ApplyResult result = applyImport(plan);
    if (!result.ok) {
        fprintf(stderr, "relay: %s\n", qPrintable(result.error));
        return 1;
    }
    printf("applied %d change(s); backup: %s\n", result.applied.size(), qPrintable(result.backupPath));
    return 0;
}

}  // namespace relay
