// SPDX-License-Identifier: GPL-3.0-or-later
#include "ModelSettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace relay {

namespace {

QLabel *hint(const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("transcriptHeader"));
    return label;
}

QFrame *separator() {
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setObjectName(QStringLiteral("settingsRule"));
    return line;
}

QString str(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toString();
}

}  // namespace

// ===== SettingsWindow ===========================================================================

SettingsWindow::SettingsWindow(std::function<QList<SettingsSection>()> build, QWidget *parent)
    : QDialog(parent), m_build(std::move(build)) {
    setObjectName(QStringLiteral("settingsWindow"));
    setWindowTitle(QStringLiteral("Relay · Settings"));
    resize(820, 560);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_sections = new QListWidget;
    m_sections->setObjectName(QStringLiteral("settingsSections"));
    m_sections->setFixedWidth(180);
    m_sections->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(m_sections);
    auto *right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    m_pages = new QStackedWidget;
    right->addWidget(m_pages, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->setContentsMargins(12, 0, 12, 12);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    right->addWidget(buttons);
    layout->addLayout(right, 1);
    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    rebuild();
}

void SettingsWindow::showSection(const QString &id) {
    m_wanted = id;
    for (int i = 0; i < m_sections->count(); ++i)
        if (m_sections->item(i)->data(Qt::UserRole).toString() == id) { m_sections->setCurrentRow(i); return; }
}

void SettingsWindow::rebuild() {
    const QString current = m_sections->currentItem()
        ? m_sections->currentItem()->data(Qt::UserRole).toString() : m_wanted;
    const QList<SettingsSection> sections = m_build();
    {
        const QSignalBlocker blocker(m_sections);
        m_sections->clear();
        while (m_pages->count() > 0) {
            QWidget *page = m_pages->widget(0);
            m_pages->removeWidget(page);
            page->deleteLater();
        }
        for (const SettingsSection &section : sections) {
            auto *item = new QListWidgetItem(section.title, m_sections);
            item->setData(Qt::UserRole, section.id);
            auto *page = new QWidget;
            fillSection(page, section);
            auto *scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(page);
            m_pages->addWidget(scroll);
        }
    }
    int index = 0;
    for (int i = 0; i < m_sections->count(); ++i)
        if (m_sections->item(i)->data(Qt::UserRole).toString() == current) index = i;
    m_sections->setCurrentRow(index);
    m_pages->setCurrentIndex(index);
}

void SettingsWindow::fillSection(QWidget *page, const SettingsSection &section) {
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    auto *title = new QLabel(section.title);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    layout->addWidget(title);
    if (!section.blurb.isEmpty()) layout->addWidget(hint(section.blurb));

    // Every control writes through the row's callback and then asks the window to rebuild, so rows
    // that describe other rows ("Flash · glm-5.3-flash") never go stale.
    auto after = [this] { QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection); };
    for (const SettingRow &row : section.rows) {
        if (row.kind == SettingRow::Info) { layout->addWidget(hint(row.label)); continue; }
        auto *line = new QWidget;
        auto *box = new QHBoxLayout(line);
        box->setContentsMargins(0, 0, 0, 0);
        auto *text = new QVBoxLayout;
        text->setSpacing(1);
        auto *label = new QLabel(row.label);
        label->setWordWrap(true);
        text->addWidget(label);
        if (!row.detail.isEmpty()) text->addWidget(hint(row.detail));
        box->addLayout(text, 1);
        switch (row.kind) {
        case SettingRow::Toggle: {
            auto *check = new QCheckBox;
            check->setChecked(row.checked);
            check->setAccessibleName(row.label);
            QObject::connect(check, &QCheckBox::toggled, this, [fn = row.onToggle, after](bool on) {
                if (fn) fn(on);
                after();
            });
            box->addWidget(check);
            break;
        }
        case SettingRow::Choice: {
            auto *combo = new QComboBox;
            combo->setAccessibleName(row.label);
            for (int i = 0; i < row.options.size(); ++i)
                combo->addItem(i < row.optionLabels.size() ? row.optionLabels.at(i) : row.options.at(i),
                               row.options.at(i));
            const int index = combo->findData(row.current);
            if (index >= 0) combo->setCurrentIndex(index);
            QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), this,
                             [combo, fn = row.onChoose, after](int i) {
                                 if (fn) fn(combo->itemData(i).toString());
                                 after();
                             });
            box->addWidget(combo);
            break;
        }
        case SettingRow::Text: {
            auto *edit = new QLineEdit(row.text);
            edit->setPlaceholderText(row.placeholder);
            edit->setAccessibleName(row.label);
            edit->setMinimumWidth(220);
            QObject::connect(edit, &QLineEdit::editingFinished, this, [edit, fn = row.onText, after] {
                if (fn) fn(edit->text().trimmed());
                after();
            });
            box->addWidget(edit);
            break;
        }
        case SettingRow::Number: {
            auto *spin = new QSpinBox;
            spin->setRange(row.minimum, row.maximum);
            spin->setValue(row.number);
            spin->setAccessibleName(row.label);
            if (!row.suffix.isEmpty()) spin->setSuffix(row.suffix);
            QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                             [fn = row.onNumber](int value) { if (fn) fn(value); });
            box->addWidget(spin);
            break;
        }
        case SettingRow::Button: {
            auto *button = new QPushButton(row.buttonText.isEmpty() ? QStringLiteral("Open…") : row.buttonText);
            QObject::connect(button, &QPushButton::clicked, this, [fn = row.run, after] {
                if (fn) fn();
                after();
            });
            box->addWidget(button);
            break;
        }
        case SettingRow::Info:
            break;
        }
        layout->addWidget(line);
    }
    layout->addStretch(1);
}

// ===== KeysDialog ===============================================================================

namespace {
enum KeyColumn { KeyProvider, KeyStatus, KeyWhere };
const int PresetRole = Qt::UserRole + 1;
const int KeyUrlRole = Qt::UserRole + 2;
}  // namespace

KeysDialog::KeysDialog(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("keysDialog"));
    setWindowTitle(QStringLiteral("Relay · API keys"));
    resize(760, 520);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint(QStringLiteral(
        "Relay is bring-your-own-key. A key you add here goes straight into the desktop keyring "
        "(secret-tool, service org.relayterminal.Relay) and is sent to that provider and nowhere else. "
        "Keys are never written to Relay's settings files, never shown again and never logged. "
        "A RELAY_<PROVIDER>_API_KEY environment variable wins over the keyring.")));
    m_list = new QTreeWidget;
    m_list->setObjectName(QStringLiteral("keysList"));
    m_list->setHeaderLabels({QStringLiteral("Provider"), QStringLiteral("Key"), QStringLiteral("Where to get one")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setColumnWidth(KeyProvider, 300);
    m_list->setColumnWidth(KeyStatus, 190);
    m_list->header()->setStretchLastSection(true);
    layout->addWidget(m_list, 1);

    auto *buttons = new QHBoxLayout;
    m_add = new QPushButton(QStringLiteral("Add / replace…"));
    m_remove = new QPushButton(QStringLiteral("Remove"));
    m_test = new QPushButton(QStringLiteral("Test"));
    m_test->setToolTip(QStringLiteral("Sends one two-word prompt to this provider to check the key reaches it. "
                                      "The key itself is read inside the worker and never shown."));
    m_where = new QPushButton(QStringLiteral("Get a key…"));
    for (QPushButton *button : {m_add, m_remove, m_test, m_where}) buttons->addWidget(button);
    buttons->addStretch(1);
    auto *importWarp = new QPushButton(QStringLiteral("Import from Warp"));
    auto *importTools = new QPushButton(QStringLiteral("Import from Claude Code / Codex"));
    importTools->setToolTip(QStringLiteral("Copies an API key out of ~/.claude/settings.json or ~/.codex/auth.json. "
                                           "An OAuth login is not an API key and is never imported."));
    buttons->addWidget(importWarp);
    buttons->addWidget(importTools);
    connect(importWarp, &QPushButton::clicked, this, [this] { if (send) send({{"type", "import_warp"}}); });
    connect(importTools, &QPushButton::clicked, this, [this] { if (send) send({{"type", "import_agent_tools"}}); });
    layout->addLayout(buttons);
    m_status = hint(QString());
    layout->addWidget(m_status);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);

    auto selected = [this] {
        QTreeWidgetItem *item = m_list->currentItem();
        return item && !item->data(0, PresetRole).toString().isEmpty() ? item : nullptr;
    };
    connect(m_add, &QPushButton::clicked, this, [this, selected] {
        if (auto *item = selected()) addOrReplace(item->data(0, PresetRole).toString(), item->text(KeyProvider));
    });
    connect(m_remove, &QPushButton::clicked, this, [this, selected] {
        if (auto *item = selected()) remove(item->data(0, PresetRole).toString(), item->text(KeyProvider));
    });
    connect(m_test, &QPushButton::clicked, this, [this, selected] {
        if (auto *item = selected()) test(item->data(0, PresetRole).toString());
    });
    connect(m_where, &QPushButton::clicked, this, [this, selected] {
        if (auto *item = selected()) {
            const QString url = item->data(0, KeyUrlRole).toString();
            if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
        }
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        const QString id = item ? item->data(0, PresetRole).toString() : QString();
        if (!id.isEmpty()) addOrReplace(id, item->text(KeyProvider));
    });
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this, selected] {
        QTreeWidgetItem *item = selected();
        const bool env = item && item->data(0, Qt::UserRole + 3).toString() == QStringLiteral("env");
        for (QPushButton *button : {m_add, m_test, m_where}) button->setEnabled(item != nullptr);
        // A key from the environment is not ours to delete.
        m_remove->setEnabled(item != nullptr && !env
                             && !item->text(KeyStatus).startsWith(QStringLiteral("Not set")));
    });
}

void KeysDialog::setPresets(const QJsonArray &presets) {
    m_presets = presets;
    rebuild();
}

QTreeWidgetItem *KeysDialog::rowFor(const QString &id) const {
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_list->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j)
            if (group->child(j)->data(0, PresetRole).toString() == id) return group->child(j);
    }
    return nullptr;
}

void KeysDialog::rebuild() {
    const QString keep = m_list->currentItem() ? m_list->currentItem()->data(0, PresetRole).toString() : QString();
    m_list->clear();
    const QList<QPair<QString, QString>> groups{
        {QStringLiteral("subscription"), QStringLiteral("Subscriptions")},
        {QStringLiteral("aggregator"), QStringLiteral("Aggregator")},
        {QStringLiteral("payg"), QStringLiteral("Pay-as-you-go")}};
    for (const auto &group : groups) {
        auto *parent = new QTreeWidgetItem(m_list, {group.second});
        QFont bold = parent->font(0);
        bold.setBold(true);
        parent->setFont(0, bold);
        parent->setFirstColumnSpanned(true);
        parent->setFlags(Qt::ItemIsEnabled);
        for (const auto &value : std::as_const(m_presets)) {
            const QJsonObject preset = value.toObject();
            if (str(preset, "group") != group.first) continue;
            const QString source = str(preset, "key_source");
            const QString status = source == QStringLiteral("env")
                ? QStringLiteral("From RELAY_%1_API_KEY").arg(str(preset, "id").toUpper().replace('-', '_'))
                : source == QStringLiteral("keyring") ? QStringLiteral("Stored in keyring")
                                                      : QStringLiteral("Not set");
            auto *item = new QTreeWidgetItem(parent, {str(preset, "label"), status, str(preset, "key_url")});
            item->setData(0, PresetRole, str(preset, "id"));
            item->setData(0, KeyUrlRole, str(preset, "key_url"));
            item->setData(0, Qt::UserRole + 3, source);
            item->setToolTip(KeyProvider, str(preset, "note") + QStringLiteral("\n") + str(preset, "base_url"));
            item->setToolTip(KeyWhere, str(preset, "key_url"));
        }
        parent->setExpanded(true);
        if (parent->childCount() == 0) delete m_list->takeTopLevelItem(m_list->indexOfTopLevelItem(parent));
    }
    if (QTreeWidgetItem *item = rowFor(keep)) m_list->setCurrentItem(item);
    else if (m_list->topLevelItemCount() > 0 && m_list->topLevelItem(0)->childCount() > 0)
        m_list->setCurrentItem(m_list->topLevelItem(0)->child(0));
}

void KeysDialog::addOrReplace(const QString &id, const QString &label) {
    bool ok = false;
    // QInputDialog with Password echo: the key is never rendered and never leaves this call.
    const QString key = QInputDialog::getText(this, QStringLiteral("API key"),
        QStringLiteral("Key for %1.\nIt is saved to the desktop keyring and sent only to this provider.").arg(label),
        QLineEdit::Password, QString(), &ok).trimmed();
    if (!ok || key.isEmpty()) return;
    if (key.contains(QRegularExpression(QStringLiteral("\\s")))) {
        QMessageBox::warning(this, QStringLiteral("API key"), QStringLiteral("An API key cannot contain spaces."));
        return;
    }
    if (send) send({{"type", "store_key"}, {"preset", id}, {"api_key", key}});
    m_status->setText(QStringLiteral("Saving the key for %1…").arg(label));
}

void KeysDialog::remove(const QString &id, const QString &label) {
    if (QMessageBox::question(this, QStringLiteral("Remove key"),
                              QStringLiteral("Remove the stored key for %1 from the keyring?").arg(label))
        != QMessageBox::Yes)
        return;
    if (send) send({{"type", "remove_key"}, {"preset", id}});
}

void KeysDialog::test(const QString &id) {
    if (send) send({{"type", "test_key"}, {"preset", id}});
    m_status->setText(QStringLiteral("Testing %1…").arg(presetLabelFor(id)));
    if (QTreeWidgetItem *item = rowFor(id)) item->setText(KeyStatus, item->text(KeyStatus) + QStringLiteral(" · testing…"));
}

void KeysDialog::handleEvent(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type == QStringLiteral("presets")) {
        setPresets(event.value(QStringLiteral("presets")).toArray());
    } else if (type == QStringLiteral("key_stored")) {
        m_status->setText(QStringLiteral("Key saved to the keyring."));
        if (send) send({{"type", "presets"}});
        if (onKeysChanged) onKeysChanged();
    } else if (type == QStringLiteral("key_removed")) {
        m_status->setText(event.value(QStringLiteral("removed")).toBool()
                              ? QStringLiteral("Key removed from the keyring.")
                              : QStringLiteral("No keyring entry to remove."));
        if (send) send({{"type", "presets"}});
        if (onKeysChanged) onKeysChanged();
    } else if (type == QStringLiteral("key_tested")) {
        const bool ok = event.value(QStringLiteral("ok")).toBool();
        const QString id = event.value(QStringLiteral("preset")).toString();
        const int ms = event.value(QStringLiteral("elapsed_ms")).toInt();
        const bool truncated = event.value(QStringLiteral("truncated")).toBool();
        m_status->setText(ok ? QStringLiteral("%1: key works — %2 answered in %3 ms%4")
                                   .arg(presetLabelFor(id), event.value(QStringLiteral("model")).toString())
                                   .arg(ms)
                                   .arg(truncated ? QStringLiteral(" (it spent the test budget thinking, "
                                                                   "which still proves the key)")
                                                  : QStringLiteral("."))
                             : QStringLiteral("%1 failed: %2")
                                   .arg(presetLabelFor(id), event.value(QStringLiteral("error")).toString()));
        rebuild();
    } else if (type == QStringLiteral("warp_imported") || type == QStringLiteral("agent_tools_imported")) {
        const QJsonArray imported = event.value(QStringLiteral("imported")).toArray();
        QStringList names;
        for (const auto &item : imported) names << item.toObject().value(QStringLiteral("name")).toString();
        QStringList skipped;
        for (const auto &item : event.value(QStringLiteral("skipped")).toArray()) skipped << item.toString();
        m_status->setText(QStringLiteral("Imported %1 key(s)%2.%3").arg(imported.size())
                              .arg(names.isEmpty() ? QString() : QStringLiteral(": ") + names.join(QStringLiteral(", ")))
                              .arg(skipped.isEmpty() ? QString()
                                                     : QStringLiteral("  Skipped: ") + skipped.join(QStringLiteral("; "))));
        if (send) send({{"type", "presets"}});
        if (onKeysChanged) onKeysChanged();
    } else if (type == QStringLiteral("error")) {
        const QString text = event.value(QStringLiteral("text")).toString();
        if (!text.isEmpty()) m_status->setText(text);
    }
}

QString KeysDialog::presetLabelFor(const QString &id) const {
    for (const auto &value : std::as_const(m_presets))
        if (str(value.toObject(), "id") == id) return str(value.toObject(), "label");
    return id;
}

// ===== RolesDialog ==============================================================================
//
// Three rows — Main, Flash, Lite — over one default provider, plus an Advanced disclosure with one
// row per job. Settings:
//   provider/preset            the default provider (also the pane's own model)
//   tiers/<flash|lite>/{preset,model,effort}   an override for that tier; unset = the provider default
//   roles/<role>/tier          "", "main", "flash", "lite" or "custom"
//   roles/<role>/{preset,model,effort}         used when roles/<role>/tier is "custom"
// The worker resolves all of it (backend/relay_core/roles.py) and reports what each tier and role
// landed on, which is what the rows display.

QStringList RolesDialog::tierIds() {
    return {QStringLiteral("main"), QStringLiteral("flash"), QStringLiteral("lite")};
}

QString RolesDialog::tierSetting(const QString &tier, const QString &field) {
    return QStringLiteral("tiers/") + tier + '/' + field;
}

QString RolesDialog::roleSetting(const QString &role, const QString &field) {
    return QStringLiteral("roles/") + role + '/' + field;
}

RolesDialog::RolesDialog(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("rolesDialog"));
    setWindowTitle(QStringLiteral("Relay · Model roles"));
    resize(800, 780);   // tall enough for the three tiers plus the whole Advanced list
    auto *layout = new QVBoxLayout(this);

    auto *providerRow = new QWidget;
    auto *providerBox = new QHBoxLayout(providerRow);
    providerBox->setContentsMargins(0, 0, 0, 0);
    providerBox->addWidget(new QLabel(QStringLiteral("Default provider")));
    m_providerBox = new QComboBox;
    m_providerBox->setAccessibleName(QStringLiteral("Default provider"));
    m_providerBox->setMinimumWidth(320);
    providerBox->addWidget(m_providerBox, 1);
    auto *keys = new QPushButton(QStringLiteral("API keys…"));
    connect(keys, &QPushButton::clicked, this, [this] { if (openKeys) openKeys(); });
    providerBox->addWidget(keys);
    layout->addWidget(providerRow);
    m_recommended = hint(QString());
    layout->addWidget(m_recommended);
    connect(m_providerBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (!m_filling) chooseProvider(m_providerBox->itemData(index).toString());
    });

    layout->addWidget(separator());
    m_tiers = new QWidget;
    new QVBoxLayout(m_tiers);
    layout->addWidget(m_tiers);

    m_disclosure = new QPushButton;
    m_disclosure->setFlat(true);
    m_disclosure->setCursor(Qt::PointingHandCursor);
    // Once opened, Advanced options stays open: someone who cares about per-job models cares every
    // time. QSettings roles/advanced_open.
    m_showAdvanced = QSettings().value(QStringLiteral("roles/advanced_open"), false).toBool();
    connect(m_disclosure, &QPushButton::clicked, this, [this] {
        m_showAdvanced = !m_showAdvanced;
        QSettings().setValue(QStringLiteral("roles/advanced_open"), m_showAdvanced);
        rebuild();
    });
    layout->addWidget(m_disclosure, 0, Qt::AlignLeft);
    m_advanced = new QWidget;
    new QVBoxLayout(m_advanced);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_advanced);
    layout->addWidget(scroll, 1);

    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);
    rebuild();
}

void RolesDialog::setPresets(const QJsonArray &presets, const QJsonObject &tierCatalog, const QJsonArray &actions) {
    m_presets = presets;
    m_catalog = tierCatalog;
    m_actions = actions;
    rebuild();
}

void RolesDialog::setResolved(const QJsonObject &tiers, const QJsonObject &roles) {
    m_resolvedTiers = tiers;
    m_resolvedRoles = roles;
    rebuild();
}

void RolesDialog::setProvider(const QString &presetId) {
    if (presetId == m_provider) return;
    m_provider = presetId;
    rebuild();
}

bool RolesDialog::hasKey(const QString &presetId) const {
    for (const auto &value : std::as_const(m_presets)) {
        const QJsonObject preset = value.toObject();
        if (str(preset, "id") == presetId) return preset.value(QStringLiteral("has_stored_key")).toBool();
    }
    return false;
}

QString RolesDialog::presetLabel(const QString &presetId) const {
    for (const auto &value : std::as_const(m_presets))
        if (str(value.toObject(), "id") == presetId) return str(value.toObject(), "label");
    return presetId;
}

// Preset labels name the provider and its default model ("OpenRouter · DeepSeek V4.1 Flash"). A tier
// usually runs a different model on that provider, so rows and the recommended line name the provider
// and its plan only: "OpenRouter", "Z.AI Coding Plan".
QString RolesDialog::shortProviderLabel(const QString &presetId) const {
    const QStringList parts = presetLabel(presetId).split(QStringLiteral(" · "));
    if (parts.size() >= 3) return parts.first() + ' ' + parts.last();
    return parts.isEmpty() ? presetId : parts.first();
}

QJsonObject RolesDialog::tierDefault(const QString &tier) const {
    return m_catalog.value(QStringLiteral("providers")).toObject().value(m_provider).toObject()
        .value(tier).toObject();
}

void RolesDialog::chooseProvider(const QString &presetId) {
    if (presetId.isEmpty() || presetId == m_provider) return;
    m_provider = presetId;
    // Switching the default provider re-derives every tier, so hand-made tier overrides are dropped:
    // a Flash model from the old provider would not exist on the new one.
    QSettings settings;
    for (const QString &tier : tierIds())
        for (const QString &field : {QStringLiteral("preset"), QStringLiteral("model"), QStringLiteral("effort")})
            settings.remove(tierSetting(tier, field));
    if (onProviderChosen) onProviderChosen(presetId);
    if (onRolesChanged) onRolesChanged();
    rebuild();
}

void RolesDialog::buildTierRow(QVBoxLayout *into, const QString &tier, const QJsonObject &spec) {
    QSettings settings;
    const QJsonObject resolved = m_resolvedTiers.value(tier).toObject();
    const QJsonObject fallback = tierDefault(tier);
    const QString label = str(spec, "label");
    const QString model = resolved.contains(QStringLiteral("model")) ? str(resolved, "model")
                                                                     : str(fallback, "model");
    const QString preset = resolved.contains(QStringLiteral("preset")) ? str(resolved, "preset")
                                                                       : str(fallback, "preset");

    auto *row = new QWidget;
    auto *box = new QHBoxLayout(row);
    box->setContentsMargins(0, 2, 0, 2);
    auto *text = new QVBoxLayout;
    text->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("%1 · %2").arg(label, model.isEmpty() ? QStringLiteral("—") : model));
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    text->addWidget(title);
    QString detail = str(spec, "hint");
    if (!preset.isEmpty() && preset != m_provider) detail += QStringLiteral(" · on ") + shortProviderLabel(preset);
    text->addWidget(hint(detail));
    const QString note = str(resolved, "note");
    if (!note.isEmpty()) {
        auto *inline_ = hint(note);
        inline_->setObjectName(QStringLiteral("tierNote"));
        text->addWidget(inline_);
    }
    box->addLayout(text, 1);

    if (tier == QStringLiteral("main")) {
        auto *fixed = new QLabel(QStringLiteral("this pane's model"));
        fixed->setEnabled(false);
        box->addWidget(fixed);
    } else {
        auto *edit = new QPushButton(QStringLiteral("Model…"));
        edit->setToolTip(QStringLiteral("Model id on %1; empty restores the provider's default for this tier.")
                             .arg(presetLabel(m_provider)));
        connect(edit, &QPushButton::clicked, this, [this, tier, label] {
            bool ok = false;
            const QString current = QSettings().value(tierSetting(tier, QStringLiteral("model"))).toString();
            const QString value = QInputDialog::getText(this, label + QStringLiteral(" model"),
                QStringLiteral("Model id (empty: the provider's default for this tier)"),
                QLineEdit::Normal, current, &ok).trimmed();
            if (!ok) return;
            QSettings settings;
            if (value.isEmpty()) {
                settings.remove(tierSetting(tier, QStringLiteral("model")));
                settings.remove(tierSetting(tier, QStringLiteral("preset")));
            } else {
                settings.setValue(tierSetting(tier, QStringLiteral("model")), value);
                // The tier stays on whichever provider it already used, so a model id alone is enough.
                if (settings.value(tierSetting(tier, QStringLiteral("preset"))).toString().isEmpty())
                    settings.setValue(tierSetting(tier, QStringLiteral("preset")),
                                      str(tierDefault(tier), "preset"));
            }
            if (onRolesChanged) onRolesChanged();
            rebuild();
        });
        box->addWidget(edit);

        auto *provider = new QComboBox;
        provider->setToolTip(QStringLiteral("Which provider serves this tier."));
        provider->addItem(QStringLiteral("Default provider"), QString());
        for (const auto &value : std::as_const(m_presets)) {
            const QJsonObject item = value.toObject();
            if (!item.value(QStringLiteral("has_stored_key")).toBool()) continue;
            provider->addItem(str(item, "label"), str(item, "id"));
        }
        const QString override = settings.value(tierSetting(tier, QStringLiteral("preset"))).toString();
        const int index = provider->findData(override);
        provider->setCurrentIndex(index >= 0 ? index : 0);
        connect(provider, QOverload<int>::of(&QComboBox::activated), this, [this, provider, tier](int i) {
            const QString chosen = provider->itemData(i).toString();
            QSettings settings;
            if (chosen.isEmpty()) {
                settings.remove(tierSetting(tier, QStringLiteral("preset")));
                settings.remove(tierSetting(tier, QStringLiteral("model")));
            } else {
                settings.setValue(tierSetting(tier, QStringLiteral("preset")), chosen);
                settings.remove(tierSetting(tier, QStringLiteral("model")));   // that provider's own default
            }
            if (onRolesChanged) onRolesChanged();
            rebuild();
        });
        box->addWidget(provider);

        // Reasoning effort for this tier. Providers whose OpenAI-compatible endpoint has no effort
        // knob (Anthropic, MiniMax) report an empty list, and then there is nothing to offer.
        const QStringList levels = effortsFor(preset.isEmpty() ? m_provider : preset);
        if (!levels.isEmpty()) {
            auto *effort = new QComboBox;
            effort->setToolTip(QStringLiteral("Reasoning effort for this tier."));
            effort->addItem(QStringLiteral("Model default"), QString());
            for (const QString &level : levels) effort->addItem(level, level);
            const QString stored = settings.value(tierSetting(tier, QStringLiteral("effort"))).toString();
            const int chosen = effort->findData(stored);
            effort->setCurrentIndex(chosen >= 0 ? chosen : 0);
            connect(effort, QOverload<int>::of(&QComboBox::activated), this, [this, effort, tier](int i) {
                const QString value = effort->itemData(i).toString();
                if (value.isEmpty()) QSettings().remove(tierSetting(tier, QStringLiteral("effort")));
                else QSettings().setValue(tierSetting(tier, QStringLiteral("effort")), value);
                if (onRolesChanged) onRolesChanged();
                rebuild();
            });
            box->addWidget(effort);
        }
    }
    into->addWidget(row);
}

QStringList RolesDialog::effortsFor(const QString &presetId) const {
    for (const auto &value : std::as_const(m_presets)) {
        const QJsonObject preset = value.toObject();
        if (str(preset, "id") != presetId) continue;
        QStringList levels;
        for (const auto &level : preset.value(QStringLiteral("efforts")).toArray()) levels << level.toString();
        return levels;
    }
    return {};
}

void RolesDialog::buildActionRow(QVBoxLayout *into, const QJsonObject &action) {
    const QString role = str(action, "role");
    const QString defaultTier = str(action, "tier");
    const QJsonObject resolved = m_resolvedRoles.value(role).toObject();
    auto *row = new QWidget;
    auto *box = new QHBoxLayout(row);
    box->setContentsMargins(0, 2, 0, 2);
    auto *text = new QVBoxLayout;
    text->setSpacing(1);
    text->addWidget(new QLabel(str(action, "label")));
    // Every advanced row shows what it actually resolves to, e.g. "Flash · glm-5.3-flash".
    const QString tier = str(resolved, "tier");
    const QString model = str(resolved, "model");
    QString effective = model.isEmpty() ? str(action, "hint") : model;
    if (!tier.isEmpty() && !model.isEmpty())
        effective = QStringLiteral("%1 · %2").arg(tier.left(1).toUpper() + tier.mid(1), model);
    text->addWidget(hint(effective + QStringLiteral(" — ") + str(action, "hint")));
    const QString note = str(resolved, "note");
    if (!note.isEmpty()) text->addWidget(hint(note));
    box->addLayout(text, 1);

    if (role == QStringLiteral("route_assist")) {
        // Pinned on purpose: routing has a sub-second budget. Gemini 3.5 Flash-Lite measured a
        // 0.64 s median and 6/6 correct across the owner's examples, against 2.3-4.9 s for 3.8 Flash.
        // It is its own override, so changing the Lite tier never moves it by accident.
        auto *pinned = new QCheckBox(QStringLiteral("Pinned"));
        pinned->setChecked(QSettings().value(roleSetting(role, QStringLiteral("tier"))).toString().isEmpty());
        pinned->setToolTip(QStringLiteral(
            "Command routing stays on google/gemini-3.5-flash-lite: measured 0.64 s median and 6/6 correct, "
            "against 2.3-4.9 s for Gemini 3.8 Flash. The routing budget is under a second, so this is an "
            "explicit override and the Lite tier does not change it."));
        connect(pinned, &QCheckBox::toggled, this, [this, role](bool on) {
            QSettings settings;
            if (on) settings.remove(roleSetting(role, QStringLiteral("tier")));
            else settings.setValue(roleSetting(role, QStringLiteral("tier")), QStringLiteral("lite"));
            if (onRolesChanged) onRolesChanged();
            rebuild();
        });
        box->addWidget(pinned);
        into->addWidget(row);
        return;
    }
    if (!action.value(QStringLiteral("settable")).toBool()) {
        auto *fixed = new QLabel(QStringLiteral("this pane's model"));
        fixed->setEnabled(false);
        box->addWidget(fixed);
        into->addWidget(row);
        return;
    }

    QSettings settings;
    const QString pinnedPreset = settings.value(roleSetting(role, QStringLiteral("preset"))).toString();
    auto *choice = new QComboBox;
    choice->setAccessibleName(str(action, "label"));
    const QString followLabel = defaultTier.isEmpty()
        ? QStringLiteral("Default")
        : QStringLiteral("Same as tier (%1)").arg(defaultTier.left(1).toUpper() + defaultTier.mid(1));
    choice->addItem(followLabel, QString());
    for (const QString &id : tierIds())
        choice->addItem(id.left(1).toUpper() + id.mid(1), id);
    // A job can also be pinned to one provider and model, independently of the three tiers.
    if (!pinnedPreset.isEmpty())
        choice->addItem(QStringLiteral("Pinned · ") + shortProviderLabel(pinnedPreset),
                        QStringLiteral("pinned"));
    choice->addItem(QStringLiteral("Pin to a model…"), QStringLiteral("pin"));
    const QString stored = settings.value(roleSetting(role, QStringLiteral("tier"))).toString();
    const int index = choice->findData(pinnedPreset.isEmpty() ? stored : QStringLiteral("pinned"));
    choice->setCurrentIndex(index >= 0 ? index : 0);
    connect(choice, QOverload<int>::of(&QComboBox::activated), this, [this, choice, role](int i) {
        const QString chosen = choice->itemData(i).toString();
        if (chosen == QStringLiteral("pinned")) return;      // already pinned; nothing to change
        if (chosen == QStringLiteral("pin")) { pinRole(role); return; }
        QSettings settings;
        if (chosen.isEmpty()) settings.remove(roleSetting(role, QStringLiteral("tier")));
        else settings.setValue(roleSetting(role, QStringLiteral("tier")), chosen);
        // A tier choice replaces any hand-picked endpoint for this role.
        settings.remove(roleSetting(role, QStringLiteral("preset")));
        settings.remove(roleSetting(role, QStringLiteral("model")));
        if (onRolesChanged) onRolesChanged();
        rebuild();
    });
    box->addWidget(choice);
    into->addWidget(row);
}

// Pin one job to a provider and model of its own. Only providers with a stored key are offered,
// because a role whose key is missing just falls back to the main agent.
void RolesDialog::pinRole(const QString &role) {
    QStringList labels, ids;
    for (const auto &value : std::as_const(m_presets)) {
        const QJsonObject preset = value.toObject();
        if (!preset.value(QStringLiteral("has_stored_key")).toBool()) continue;
        labels << str(preset, "label");
        ids << str(preset, "id");
    }
    if (ids.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Pin a model"),
                                 QStringLiteral("No provider has a stored key yet. Add one under API keys…"));
        rebuild();
        return;
    }
    bool ok = false;
    const QString stored = QSettings().value(roleSetting(role, QStringLiteral("preset"))).toString();
    const int current = std::max(0, ids.indexOf(stored));
    const QString label = QInputDialog::getItem(this, QStringLiteral("Pin a model"),
                                                QStringLiteral("Provider for this job"), labels, current,
                                                false, &ok);
    if (!ok) { rebuild(); return; }
    const QString presetId = ids.at(labels.indexOf(label));
    const QString model = QInputDialog::getText(this, QStringLiteral("Pin a model"),
        QStringLiteral("Model id on %1 (empty: that provider's default model)").arg(label),
        QLineEdit::Normal, QSettings().value(roleSetting(role, QStringLiteral("model"))).toString(), &ok).trimmed();
    if (!ok) { rebuild(); return; }
    QSettings settings;
    settings.setValue(roleSetting(role, QStringLiteral("preset")), presetId);
    if (model.isEmpty()) settings.remove(roleSetting(role, QStringLiteral("model")));
    else settings.setValue(roleSetting(role, QStringLiteral("model")), model);
    settings.remove(roleSetting(role, QStringLiteral("tier")));   // a pin and a tier are exclusive
    if (onRolesChanged) onRolesChanged();
    rebuild();
}

void RolesDialog::rebuild() {
    if (m_filling) return;
    m_filling = true;

    m_providerBox->clear();
    for (const auto &value : std::as_const(m_presets)) {
        const QJsonObject preset = value.toObject();
        const bool stored = preset.value(QStringLiteral("has_stored_key")).toBool();
        m_providerBox->addItem(stored ? str(preset, "label")
                                      : str(preset, "label") + QStringLiteral("  (no key)"),
                               str(preset, "id"));
        m_providerBox->setItemData(m_providerBox->count() - 1, stored, Qt::UserRole + 1);
    }
    const int index = m_providerBox->findData(m_provider);
    if (index >= 0) m_providerBox->setCurrentIndex(index);

    QStringList pairs;
    for (const auto &value : m_catalog.value(QStringLiteral("recommended")).toArray()) {
        const QJsonArray pair = value.toArray();
        if (pair.size() == 2)
            pairs << QStringLiteral("%1 + %2").arg(shortProviderLabel(pair.at(0).toString()),
                                                   shortProviderLabel(pair.at(1).toString()));
    }
    m_recommended->setText(pairs.isEmpty()
        ? QString()
        : QStringLiteral("Recommended: %1. The second key covers the Lite tier and command routing; "
                         "without it those fall back to your own provider.").arg(pairs.join(QStringLiteral(", "))));

    auto clear = [](QWidget *host) {
        auto *layout = qobject_cast<QVBoxLayout *>(host->layout());
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        return layout;
    };
    QVBoxLayout *tiers = clear(m_tiers);
    for (const auto &value : m_catalog.value(QStringLiteral("tiers")).toArray())
        buildTierRow(tiers, str(value.toObject(), "id"), value.toObject());

    m_disclosure->setText(m_showAdvanced ? QStringLiteral("▾ Advanced options")
                                         : QStringLiteral("▸ Advanced options"));
    QVBoxLayout *advanced = clear(m_advanced);
    m_advanced->setVisible(m_showAdvanced);
    if (m_showAdvanced) {
        advanced->addWidget(hint(QStringLiteral(
            "Each job follows one of the three tiers above unless you pin it. Changing a tier moves every "
            "job that follows it.")));
        for (const auto &value : std::as_const(m_actions)) buildActionRow(advanced, value.toObject());
        advanced->addStretch(1);
    }
    m_filling = false;
}

}  // namespace relay
