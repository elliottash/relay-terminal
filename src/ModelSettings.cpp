// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelSettings.h"
#include "ModelCatalog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace relay {

namespace {

QLabel *hint(const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("transcriptHeader"));
    return label;
}

QString str(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toString();
}

bool hosted(const QJsonObject &preset) { return preset.value(QStringLiteral("hosted")).toBool(); }

// "73% left today" from {limit, used}; empty when the allowance is unknown or nonsensical.
QString allowanceLeft(const QJsonObject &quota) {
    const double limit = quota.value(QStringLiteral("limit")).toDouble();
    const double used = quota.value(QStringLiteral("used")).toDouble();
    if (limit <= 0) return QString();
    // Rounded down, so one call spent never reads as "100% left".
    const double left = std::max(0.0, 100.0 * (limit - used) / limit);
    return QStringLiteral("%1% left today").arg(left > 0 && left < 10 ? QString::number(std::floor(left * 10) / 10, 'f', 1)
                                                                     : QString::number(std::floor(left), 'f', 0));
}

}  // namespace

// ===== KeysDialog ===============================================================================

namespace {
enum KeyColumn { KeyProvider, KeyStatus, KeyWhere };
const int PresetRole = Qt::UserRole + 1;
const int KeyUrlRole = Qt::UserRole + 2;
const int HostedRole = Qt::UserRole + 4;   // true on the Relay Free row (Qt::UserRole + 3 is key_source)
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
        "A RELAY_<PROVIDER>_API_KEY environment variable wins over the keyring. "
        "Relay Free needs no key: its prompts go through Relay's hosted service to the model provider, "
        "and Relay keeps request metadata only.")));
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
        // Relay Free has no key to type: a double click on its row is not a request for one.
        if (!id.isEmpty() && !item->data(0, HostedRole).toBool()) addOrReplace(id, item->text(KeyProvider));
    });
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });
}

// Which of the four buttons the selected row can use. Relay Free (`hosted`) holds no key, so Add and
// Remove have nothing to do on it, its link is the page about the service rather than a key page,
// and Test stays: the worker makes one real call through the gateway.
void KeysDialog::updateButtons() {
    QTreeWidgetItem *item = m_list->currentItem();
    if (item && item->data(0, PresetRole).toString().isEmpty()) item = nullptr;
    const bool env = item && item->data(0, Qt::UserRole + 3).toString() == QStringLiteral("env");
    const bool included = item && item->data(0, PresetRole).toString() == QStringLiteral("relay-free");
    const bool pro = item && item->data(0, PresetRole).toString() == QStringLiteral("relay-pro");
    m_add->setText(pro ? QStringLiteral("Add / replace code…") : QStringLiteral("Add / replace…"));
    m_test->setText(pro ? QStringLiteral("Check access") : QStringLiteral("Test"));
    m_add->setEnabled(item != nullptr && !included);
    m_test->setEnabled(item != nullptr);
    m_where->setEnabled(item != nullptr);
    m_where->setText(included ? QStringLiteral("About Relay Free…") : QStringLiteral("Get a key…"));
    // A key from the environment is not ours to delete.
    m_remove->setEnabled(item != nullptr && !env && !included
                         && !item->text(KeyStatus).startsWith(QStringLiteral("Not set")));
}

// The status column of the Relay Free row: what stands in for "Stored in keyring" there.
QString KeysDialog::hostedStatus(const QJsonObject &preset) const {
    if (str(preset, "id") == QStringLiteral("relay-pro"))
        return preset.value(QStringLiteral("access_note")).toString(QStringLiteral("Enter your personal access code"));
    if (!preset.value(QStringLiteral("available")).toBool()) return QStringLiteral("Needs python3-cryptography");
    const QJsonObject quota = m_hostedQuota.isEmpty() ? preset.value(QStringLiteral("quota")).toObject()
                                                      : m_hostedQuota;
    const QString left = allowanceLeft(quota);
    return QStringLiteral("Included · ") + (left.isEmpty() ? QStringLiteral("no key needed") : left);
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
    // The same order as presets.py GROUPS: what comes with Relay first, then what you pay for.
    const QList<QPair<QString, QString>> groups{
        {QStringLiteral("included"), QStringLiteral("Included")},
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
            const QString status = hosted(preset) ? hostedStatus(preset)
                : source == QStringLiteral("env")
                ? QStringLiteral("From RELAY_%1_API_KEY").arg(str(preset, "id").toUpper().replace('-', '_'))
                : source == QStringLiteral("keyring") ? QStringLiteral("Stored in keyring")
                                                      : QStringLiteral("Not set");
            auto *item = new QTreeWidgetItem(parent, {str(preset, "label"), status, str(preset, "key_url")});
            item->setData(0, PresetRole, str(preset, "id"));
            item->setData(0, KeyUrlRole, str(preset, "key_url"));
            item->setData(0, Qt::UserRole + 3, source);
            item->setData(0, HostedRole, hosted(preset));
            item->setToolTip(KeyProvider, str(preset, "note") + QStringLiteral("\n") + str(preset, "base_url"));
            item->setToolTip(KeyWhere, str(preset, "key_url"));
        }
        parent->setExpanded(true);
        if (parent->childCount() == 0) delete m_list->takeTopLevelItem(m_list->indexOfTopLevelItem(parent));
    }
    if (QTreeWidgetItem *item = rowFor(keep)) m_list->setCurrentItem(item);
    else if (m_list->topLevelItemCount() > 0 && m_list->topLevelItem(0)->childCount() > 0)
        m_list->setCurrentItem(m_list->topLevelItem(0)->child(0));
    updateButtons();   // the current row may be the same item as before, which emits no change
}

void KeysDialog::addOrReplace(const QString &id, const QString &label) {
    bool ok = false;
    // QInputDialog with Password echo: the key is never rendered and never leaves this call.
    const bool pro = id == QStringLiteral("relay-pro");
    const QString key = QInputDialog::getText(this, pro ? QStringLiteral("Relay Pro access code") : QStringLiteral("API key"),
        pro ? QStringLiteral("Your personal access code.\nRelay checks it before saving it to the desktop keyring.")
            : QStringLiteral("Key for %1.\nIt is saved to the desktop keyring and sent only to this provider.").arg(label),
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
    } else if (type == QStringLiteral("hosted_quota")) {
        // Protocol 13.9: after every gateway call, and in reply to a `hosted_quota` request. The
        // Relay Free row's status column follows it live; the `presets` row's `quota` is the
        // fallback before the first one arrives.
        m_hostedQuota = QJsonObject{{QStringLiteral("limit"), event.value(QStringLiteral("limit"))},
                                    {QStringLiteral("used"), event.value(QStringLiteral("used"))},
                                    {QStringLiteral("resets_at"), event.value(QStringLiteral("resets_at"))}};
        for (const auto &value : std::as_const(m_presets)) {
            const QJsonObject preset = value.toObject();
            if (!hosted(preset)) continue;
            if (QTreeWidgetItem *item = rowFor(str(preset, "id"))) item->setText(KeyStatus, hostedStatus(preset));
        }
    } else if (type == QStringLiteral("key_stored")) {
        m_status->setText(event.value(QStringLiteral("preset")).toString() == QStringLiteral("relay-pro")
            ? QStringLiteral("Relay Pro access confirmed; code saved to the keyring.")
            : QStringLiteral("Key saved to the keyring."));
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
        // Relay Free holds no key: what a passing test proves there is that the service answers.
        bool included = false;
        for (const auto &value : std::as_const(m_presets))
            if (str(value.toObject(), "id") == id) included = hosted(value.toObject());
        if (id == QStringLiteral("relay-pro")) {
            m_status->setText(ok ? QStringLiteral("Relay Pro access is active.")
                                : QStringLiteral("Relay Pro: %1").arg(event.value(QStringLiteral("error")).toString()));
            if (send) send({{"type", "presets"}});
            return;
        }
        m_status->setText(ok ? (included ? QStringLiteral("%1 works — %2 answered in %3 ms%4")
                                         : QStringLiteral("%1: key works — %2 answered in %3 ms%4"))
                                   .arg(presetLabelFor(id),
                                        relay::models::nameOf(event.value(QStringLiteral("model")).toString()))
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

}  // namespace relay
