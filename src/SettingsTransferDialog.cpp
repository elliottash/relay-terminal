// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SettingsTransferDialog.h"

#include "SettingsExport.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QJsonArray>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace relay::settingsexport;

namespace relay {

namespace {
QString statusText(const PlanItem &item)
{
    switch (item.status) {
    case ItemStatus::Auto: return item.reason.isEmpty() ? QStringLiteral("merges") : item.reason;
    case ItemStatus::Conflict: return QStringLiteral("conflict");
    case ItemStatus::Attention: return item.reason;
    case ItemStatus::Skip: return item.reason;
    }
    return {};
}
} // namespace

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

SettingsExportDialog::SettingsExportDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Export settings"));
    resize(560, 240);

    auto *layout = new QVBoxLayout(this);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_memories = new QCheckBox(QStringLiteral("Include global memories and instructions"), this);
    m_memories->setToolTip(QStringLiteral(
        "Global memories ($XDG_CONFIG_HOME/relay/switchboard/memory) and the global instructions "
        "file are personal: they ride along only when you ask."));
    layout->addWidget(m_memories);

    auto *buttons = new QHBoxLayout;
    auto *choose = new QPushButton(QStringLiteral("Choose file…"), this);
    buttons->addWidget(choose);
    buttons->addStretch(1);
    auto *exportButton = new QPushButton(QStringLiteral("Export"), this);
    exportButton->setDefault(true);
    buttons->addWidget(exportButton);
    layout->addLayout(buttons);

    connect(choose, &QPushButton::clicked, this, [this] {
        const QString suggested = QDir::home().filePath(
            QStringLiteral("relay-settings-%1.json")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
        const QString picked = QFileDialog::getSaveFileName(this, QStringLiteral("Export settings to"),
                                                            suggested, QStringLiteral("Settings bundle (*.json)"));
        if (!picked.isEmpty()) {
            m_path = picked;
            refreshSummary();
        }
    });
    connect(exportButton, &QPushButton::clicked, this, &SettingsExportDialog::doExport);
    connect(m_memories, &QCheckBox::toggled, this, [this] { refreshSummary(); });
    refreshSummary();
}

void SettingsExportDialog::refreshSummary()
{
    const QJsonObject bundle = collectBundle({m_memories && m_memories->isChecked()});
    const int settingsCount = bundle.value(QStringLiteral("settings")).toObject().size();
    const int hotkeyCount = bundle.value(QStringLiteral("hotkeys")).toObject().size();
    const int aliasCount = bundle.value(QStringLiteral("aliases")).toArray().size();
    const int endpointCount = bundle.value(QStringLiteral("endpoints")).toArray().size();
    const int themeCount = bundle.value(QStringLiteral("themes")).toArray().size();
    const int memoryCount = bundle.value(QStringLiteral("memories")).toArray().size();

    QStringList lines;
    lines << QStringLiteral("This bundle carries your customized preferences only — never API keys, "
                            "tokens, pairing identity, usage history or machine paths:");
    lines << QStringLiteral("· %1 setting%2, %3 custom hotkey%4, %5 theme%6")
                     .arg(settingsCount)
                     .arg(settingsCount == 1 ? QString() : QStringLiteral("s"))
                     .arg(hotkeyCount)
                     .arg(hotkeyCount == 1 ? QString() : QStringLiteral("s"))
                     .arg(themeCount)
                     .arg(themeCount == 1 ? QString() : QStringLiteral("s"));
    lines << QStringLiteral("· %1 global alias%2, %3 local endpoint%4")
                     .arg(aliasCount)
                     .arg(aliasCount == 1 ? QString() : QStringLiteral("es"))
                     .arg(endpointCount)
                     .arg(endpointCount == 1 ? QString() : QStringLiteral("s"));
    if (m_memories && m_memories->isChecked())
        lines << QStringLiteral("· %1 global memor%2 and global instructions (opted in)")
                         .arg(memoryCount)
                         .arg(memoryCount == 1 ? QStringLiteral("y") : QStringLiteral("ies"));
    lines << (m_path.isEmpty() ? QStringLiteral("Choose a file to write the bundle to.")
                               : QStringLiteral("Writing to %1").arg(m_path));
    m_summary->setText(lines.join(QChar('\n')));
}

void SettingsExportDialog::doExport()
{
    if (m_path.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export settings"),
                                 QStringLiteral("Choose a file first."));
        return;
    }
    QString error;
    if (!writeBundleToFile(collectBundle({m_memories->isChecked()}), m_path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Export settings"), error);
        return;
    }
    QMessageBox::information(this, QStringLiteral("Export settings"),
                             QStringLiteral("Saved %1.").arg(m_path));
    accept();
}

// ---------------------------------------------------------------------------
// Import review
// ---------------------------------------------------------------------------

SettingsImportDialog::SettingsImportDialog(const QString &path, QWidget *parent)
    : QDialog(parent), m_path(path)
{
    setWindowTitle(QStringLiteral("Import settings"));
    resize(860, 560);

    auto *layout = new QVBoxLayout(this);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("Preference"), QStringLiteral("This install"),
                             QStringLiteral("In bundle"), QStringLiteral("Decision / status")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(false);
    layout->addWidget(m_tree);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *bulk = new QHBoxLayout;
    auto *keepAll = new QPushButton(QStringLiteral("Keep current for all conflicts"), this);
    auto *useAll = new QPushButton(QStringLiteral("Use imported for all conflicts"), this);
    bulk->addWidget(keepAll);
    bulk->addWidget(useAll);
    bulk->addStretch(1);
    layout->addLayout(bulk);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    m_apply = new QPushButton(QStringLiteral("Apply"), this);
    m_apply->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(m_apply);
    layout->addLayout(buttons);

    connect(keepAll, &QPushButton::clicked, this, [this] { applyBulk(0); });
    connect(useAll, &QPushButton::clicked, this, [this] { applyBulk(1); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &SettingsImportDialog::doApply);

    QString error;
    m_bundle = readBundle(m_path, &error);
    if (m_bundle.isEmpty()) {
        m_status->setText(error);
        m_apply->setEnabled(false);
        return;
    }
    rebuild();
}

void SettingsImportDialog::rebuild()
{
    const ImportPlan plan = planImport(m_bundle, m_path, hooks);

    m_tree->clear();
    for (const PlanItem &item : plan.items) {
        auto *row = new QTreeWidgetItem(m_tree);
        row->setText(0, item.label);
        row->setText(1, item.currentDisplay.isEmpty() ? QStringLiteral("—") : item.currentDisplay);
        row->setText(2, item.incomingDisplay.isEmpty() ? QStringLiteral("—") : item.incomingDisplay);
        if (item.status == ItemStatus::Conflict) {
            auto *choice = new QComboBox(this);
            choice->addItem(QStringLiteral("Keep current"));
            choice->addItem(QStringLiteral("Use imported"));
            choice->addItem(QStringLiteral("(decide)"));
            choice->setCurrentIndex(2);
            const QString id = item.id;
            connect(choice, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, id](int index) {
                m_decisions.insert(id, index == 0 ? Resolution::KeepCurrent
                                                  : index == 1 ? Resolution::UseImported
                                                               : Resolution::Unresolved);
                rowDecided(id, index < 2);
            });
            m_tree->addTopLevelItem(row);
            m_tree->setItemWidget(row, 3, choice);
            row->setBackground(1, QColor(0xff, 0xf3, 0xcd));
            row->setBackground(2, QColor(0xff, 0xf3, 0xcd));
        } else if (item.status == ItemStatus::Attention) {
            auto *accept = new QCheckBox(QStringLiteral("Apply anyway"), this);
            const QString id = item.id;
            connect(accept, &QCheckBox::toggled, this, [this, id](bool on) {
                m_decisions.insert(id, on ? Resolution::UseImported : Resolution::Unresolved);
                rowDecided(id, on);
            });
            m_tree->addTopLevelItem(row);
            m_tree->setItemWidget(row, 3, accept);
        } else {
            row->setText(3, statusText(item));
            if (item.status == ItemStatus::Skip) {
                for (int col = 0; col < 4; ++col) row->setForeground(col, QColor(0x8a, 0x8a, 0x8a));
            }
        }
    }

    updateStatusLine();
}

void SettingsImportDialog::rowDecided(const QString &id, bool decided)
{
    Q_UNUSED(decided);
    Q_UNUSED(id);
    updateStatusLine();
}

void SettingsImportDialog::updateStatusLine()
{
    const ImportPlan plan = planFromUi();
    QStringList parts;
    const int conflicts = plan.count(ItemStatus::Conflict);
    const int attention = plan.count(ItemStatus::Attention);
    const int skipped = plan.count(ItemStatus::Skip);
    if (conflicts) parts << QStringLiteral("%1 conflict(s) to decide").arg(conflicts);
    if (attention) parts << QStringLiteral("%1 needing attention").arg(attention);
    if (skipped) parts << QStringLiteral("%1 skipped").arg(skipped);
    if (plan.hasUnresolvedConflicts() || plan.hasUnacceptedAttention()) {
        parts << QStringLiteral("Nothing is written until every conflict is decided — Cancel applies nothing.");
        m_apply->setEnabled(false);
    } else {
        parts << QStringLiteral("Ready to apply.");
        m_apply->setEnabled(true);
    }
    m_status->setText(parts.join(QStringLiteral(" · ")));
}

ImportPlan SettingsImportDialog::planFromUi() const
{
    ImportPlan plan = planImport(m_bundle, m_path, hooks);
    for (PlanItem &item : plan.items) {
        if (!m_decisions.contains(item.id)) continue;
        item.resolution = m_decisions.value(item.id);
    }
    return plan;
}

void SettingsImportDialog::applyBulk(int resolutionIndex)
{
    ImportPlan plan = planImport(m_bundle, m_path, hooks);
    const Resolution resolution = resolutionIndex == 0 ? Resolution::KeepCurrent : Resolution::UseImported;
    for (const PlanItem &item : plan.items) {
        if (item.status == ItemStatus::Conflict) m_decisions.insert(item.id, resolution);
        if (item.status == ItemStatus::Attention && resolution == Resolution::UseImported)
            m_decisions.insert(item.id, Resolution::UseImported);
        if (item.status == ItemStatus::Attention && resolution == Resolution::KeepCurrent)
            m_decisions.remove(item.id);
    }
    rebuild();
}

void SettingsImportDialog::doApply()
{
    const ImportPlan plan = planFromUi();
    const ApplyResult result = applyImport(plan);
    if (!result.ok) {
        QMessageBox::warning(this, QStringLiteral("Import settings"), result.error);
        return;
    }
    if (afterApply) afterApply();
    QStringList lines;
    lines << QStringLiteral("Applied %1 change(s).").arg(result.applied.size());
    if (!result.skipped.isEmpty()) lines << QStringLiteral("Skipped: %1.").arg(result.skipped.join(QStringLiteral(", ")));
    lines << QStringLiteral("Backup: %1").arg(result.backupPath);
    QMessageBox::information(this, QStringLiteral("Import settings"), lines.join(QChar('\n')));
    accept();
}

} // namespace relay
