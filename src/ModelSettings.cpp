// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelSettings.h"
#include "ModelCatalog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
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

// A model server on this machine (presets.py `local`, card #24XJ) serves without an API key, so it
// counts as usable wherever a stored key does. It never reaches the keys modal: its group is
// "local", which KeysDialog::rebuild() does not list. Relay Free (presets.py `hosted`) needs no key
// either, but only while the worker can reach it (`available`: python3-cryptography is installed).
bool usable(const QJsonObject &preset) {
    return preset.value(QStringLiteral("has_stored_key")).toBool()
           || preset.value(QStringLiteral("local")).toBool()
           || (preset.value(QStringLiteral("hosted")).toBool()
               && preset.value(QStringLiteral("available")).toBool());
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
    const bool included = item && item->data(0, HostedRole).toBool();
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
        // Relay Free holds no key: what a passing test proves there is that the service answers.
        bool included = false;
        for (const auto &value : std::as_const(m_presets))
            if (str(value.toObject(), "id") == id) included = hosted(value.toObject());
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

// ===== RolesDialog ==============================================================================
//
// "per-job models": one row per job (protocol 13.7) and the vision row. Nothing else.
//
// Until 2026-09-20 the top half of this dialog was a Default provider box, a recommended-pairs line
// and a row per tier (Main / Flash / Lite / Local: Model…, a provider, an effort). The owner moved
// all of that to Options › Models, which now holds five ordered lists — main, high, flash, lite,
// local (relay::models::curation::tierList, src/ModelCatalog.h). An entry there is a model plus a
// reasoning level, rank 1 is what the tier runs on, the rest are its fallbacks, and rank 1 of the
// main list is the default provider. Two places to say the same thing was one too many, so the
// rows went and what was behind "Advanced options" is now the whole dialog. Settings it writes:
//   roles/<role>/tier          "" (the job's built-in tier), "high", "main", "flash", "lite", "local"
//   roles/<role>/{preset,model,effort}         a provider of this job's own, exclusive with the tier
// tierIds() / tierSetting() stay because Pane::tiersObject and the Switchboard's model box build the
// protocol objects from them; this dialog no longer writes a `tiers/…` key.
// The worker resolves all of it (backend/relay_core/roles.py) and reports what each role landed
// on, which is what the rows display.

namespace {

// The words Relay has seen from any provider, lowest first. Relay owns no levels of its own since
// card #MDL1 (owner, 2026-09-21: "i want the effort options in relay to be determined by the
// model"): a row's levels are its model's `efforts`, and this ladder is the fallback for a
// provider that reports none at all, and the order `nearestEffort` snaps along.
QStringList allEfforts() { return relay::models::effortLadder(); }

// The offered level a stored one lands on: itself when the model takes it, otherwise the nearest by
// ladder position, ties going up (relay::models::nearestEffort). A level stored while another
// provider was in use names a request this one cannot make, and showing "Model default" instead hid
// the effort the job would really run at (owner report, 2026-09-18). An empty `offered` means the
// model has no effort knob at all, and then no picker is drawn in the first place.
QString nearestOffered(const QStringList &offered, const QString &level) {
    if (offered.isEmpty() || offered.contains(level)) return level;
    return relay::models::nearestEffort(offered, level);
}

// The left column of every row: a title and the lines under it, in one widget, so that rebuilding
// the dialog drops a whole row by deleting the widgets in its grid cells. `note` is the worker's
// inline remark about a tier that stepped down ("No stored key for the Flash model; using Main.").
QWidget *rowText(const QString &title, const QStringList &under, bool bold,
                 const QString &note = QString()) {
    auto *host = new QWidget;
    auto *box = new QVBoxLayout(host);
    box->setContentsMargins(0, 2, 0, 2);
    box->setSpacing(1);
    auto *label = new QLabel(title);
    label->setWordWrap(true);
    if (bold) {
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
    }
    box->addWidget(label);
    for (const QString &line : under)
        if (!line.isEmpty()) box->addWidget(hint(line));
    if (!note.isEmpty()) {
        auto *inline_ = hint(note);
        inline_->setObjectName(QStringLiteral("tierNote"));
        box->addWidget(inline_);
    }
    return host;
}

}  // namespace

QStringList RolesDialog::tierIds() {
    // high first (owner, 2026-09-20): plan mode's tier, main at max reasoning unless a model is picked.
    return {QStringLiteral("high"), QStringLiteral("main"), QStringLiteral("flash"), QStringLiteral("lite"),
            QStringLiteral("local")};
}

QString RolesDialog::tierSetting(const QString &tier, const QString &field) {
    return QStringLiteral("tiers/") + tier + '/' + field;
}

QString RolesDialog::roleSetting(const QString &role, const QString &field) {
    return QStringLiteral("roles/") + role + '/' + field;
}

void RolesDialog::writeRoleTier(const QString &role, const QString &tier) {
    QSettings settings;
    if (tier.isEmpty()) settings.remove(roleSetting(role, QStringLiteral("tier")));
    else settings.setValue(roleSetting(role, QStringLiteral("tier")), tier);
    // A tier choice replaces any hand-picked endpoint for this role (13.7: the pair is refused).
    settings.remove(roleSetting(role, QStringLiteral("preset")));
    settings.remove(roleSetting(role, QStringLiteral("model")));
}

void RolesDialog::writeRolePreset(const QString &role, const QString &presetId) {
    QSettings settings;
    settings.remove(roleSetting(role, QStringLiteral("model")));   // that provider's own default
    if (presetId.isEmpty()) {
        settings.remove(roleSetting(role, QStringLiteral("preset")));
    } else {
        settings.setValue(roleSetting(role, QStringLiteral("preset")), presetId);
        settings.remove(roleSetting(role, QStringLiteral("tier")));   // an endpoint and a tier are exclusive
    }
}

void RolesDialog::writeRoleEntry(const QString &role, const QString &presetId, const QString &model,
                                 const QString &effort) {
    writeRolePreset(role, presetId);   // the endpoint, and the tier it is exclusive with, first
    QSettings settings;
    if (presetId.isEmpty() || model.trimmed().isEmpty())
        settings.remove(roleSetting(role, QStringLiteral("model")));
    else
        settings.setValue(roleSetting(role, QStringLiteral("model")), model.trimmed());
    // An empty level is "whatever this model does by default", which is the absence of the key —
    // not a stored empty string, which `rolesObject` would drop anyway but a reader might not.
    if (effort.isEmpty()) settings.remove(roleSetting(role, QStringLiteral("effort")));
    else settings.setValue(roleSetting(role, QStringLiteral("effort")), effort);
}

RolesDialog::RolesDialog(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("rolesDialog"));
    setWindowTitle(QStringLiteral("Relay · per-job models"));
    resize(1120, 680);   // a pinned row is five cells wide: text, tier, provider, model, effort
    auto *layout = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    top->addWidget(hint(QStringLiteral(
        "Each job follows a tier — high, main, flash, lite or local — until you give it a provider of "
        "its own. The tiers themselves are the five lists on Options › Models: the first model in a "
        "list is what that tier runs on and the rest are its fallbacks, so changing a list moves every "
        "job that follows it. A provider picked here moves only that job.")), 1);
    auto *keys = new QPushButton(QStringLiteral("API keys…"));
    connect(keys, &QPushButton::clicked, this, [this] { if (openKeys) openKeys(); });
    top->addWidget(keys, 0, Qt::AlignTop);
    layout->addLayout(top);
    layout->addWidget(separator());

    m_rows = new QWidget;
    new QVBoxLayout(m_rows);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_rows);
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
    Q_UNUSED(tiers);   // the tiers are shown on Options › Models now; the rows here are per job
    m_resolvedRoles = roles;
    rebuild();
}

// The pane's own provider. Nothing here chooses it any more — that is rank 1 of the main list on
// Options › Models — but it is still where a job lands before the worker has resolved anything, so
// it decides which effort levels such a row can offer, and it stays listed among the providers
// even if its key went away.
void RolesDialog::setProvider(const QString &presetId) {
    if (presetId == m_provider) return;
    m_provider = presetId;
    rebuild();
}

QJsonObject RolesDialog::presetRow(const QString &presetId) const {
    for (const auto &value : std::as_const(m_presets))
        if (str(value.toObject(), "id") == presetId) return value.toObject();
    return {};
}

QString RolesDialog::presetLabel(const QString &presetId) const {
    const QJsonObject preset = presetRow(presetId);
    return preset.isEmpty() ? presetId : str(preset, "label");
}

// Preset labels name the provider and its default model ("OpenRouter · DeepSeek V4.1 Flash"), which
// is the right thing in the keys modal — you hold a key per plan — and the wrong thing here: a row's
// provider box chooses a *provider*, and the model is the box beside it. So everything in this
// dialog names the company (presets.py `provider`): Kimi, Z.AI (GLM), OpenAI (ChatGPT)…
QString RolesDialog::providerName(const QString &presetId) const {
    const QJsonObject preset = presetRow(presetId);
    if (preset.isEmpty()) return presetId;
    const QString name = str(preset, "provider");
    return name.isEmpty() ? presetLabel(presetId).split(QStringLiteral(" · ")).first() : name;
}

// The same company can be offered twice — a Kimi Code key and a Moonshot platform key are different
// keys on different endpoints — and then, and only then, the plan tells the two entries apart.
QString RolesDialog::providerChoice(const QString &presetId) const {
    const QString name = providerName(presetId);
    int sharing = 0;
    for (const auto &value : choosableProviders())
        if (providerName(str(value.toObject(), "id")) == name) ++sharing;
    if (sharing < 2) return name;
    const QString plan = str(presetRow(presetId), "plan");
    return plan.isEmpty() ? name : name + QStringLiteral(" · ") + plan;
}

// Only providers you hold a key for are worth offering: pinning a job to one you cannot reach just
// gives a row that falls back. The exception is the provider already in use (and, on a fresh
// install where nothing has a key, every provider — otherwise the dialog would offer nothing at all
// and the API keys… button beside it would have nothing to come back to).
QJsonArray RolesDialog::choosableProviders() const {
    QJsonArray keyed, all;
    for (const auto &value : std::as_const(m_presets)) {
        const QJsonObject preset = value.toObject();
        all.append(preset);
        if (usable(preset) || str(preset, "id") == m_provider) keyed.append(preset);
    }
    for (const auto &value : std::as_const(keyed))
        if (usable(value.toObject())) return keyed;
    return all;
}

// ----- the rows ---------------------------------------------------------------------------------
//
// Every row is five cells of one grid — the text, what the job follows, then its own provider, model
// and effort — so the rows line up down the dialog. A row with nothing to put in a cell leaves it
// empty rather than shuffling the rest of the row left.

// The list of providers a row can be sent to. `neutral` is the row's "nothing of my own" entry (the
// vision row's Automatic); a job row has none, because the way back is its tier box.
// A provider whose key has gone away is still listed while the row points at it: the row would
// otherwise read as something else while the worker quietly fell back.
void RolesDialog::fillProviders(QComboBox *box, const QString &neutral, const QString &selected) const {
    if (!neutral.isEmpty()) box->addItem(neutral, QString());
    const bool anyUsable = [this] {
        for (const auto &value : std::as_const(m_presets))
            if (usable(value.toObject())) return true;
        return false;
    }();
    for (const auto &value : std::as_const(m_presets)) {
        const QJsonObject item = value.toObject();
        const QString id = str(item, "id");
        const bool stored = usable(item);
        // With no key anywhere every provider is listed, marked, so the box is never empty.
        if (!stored && id != selected && anyUsable) continue;
        box->addItem(stored ? providerChoice(id) : providerChoice(id) + QStringLiteral("  (no key)"), id);
    }
    const int index = box->findData(selected);
    box->setCurrentIndex(index >= 0 ? index : 0);
    box->setMinimumWidth(165);
}

// The provider a job is pinned to when its tier box is moved to "its own provider…": the one it is
// already running on, so that the pick changes nothing until the boxes that then appear are used.
// Failing that (no key for it, or nothing resolved yet) the first provider that can be reached, and
// with no key at all the first one listed — the row then reads "(no key)", which is the truth.
QString RolesDialog::ownProviderFor(const QString &role) const {
    const QString current = rolePreset(role);
    if (usable(presetRow(current))) return current;
    const QJsonArray offered = choosableProviders();
    for (const auto &value : offered)
        if (usable(value.toObject())) return str(value.toObject(), "id");
    return offered.isEmpty() ? QString() : str(offered.first().toObject(), "id");
}

// One model of one provider, from the per-model catalog the preset row carries (`models`, presets.py
// MODEL_CATALOG: id, label, efforts, effort_labels…). An empty id is the provider's default model.
QJsonObject RolesDialog::modelRow(const QString &presetId, const QString &modelId) const {
    const QJsonObject preset = presetRow(presetId);
    const QString id = modelId.isEmpty() ? str(preset, "model") : modelId;
    for (const auto &value : preset.value(QStringLiteral("models")).toArray())
        if (str(value.toObject(), "id") == id) return value.toObject();
    return {};
}

// The levels one model can be asked for. The catalog row decides where there is one — Kimi documents
// reasoning_effort for kimi-k3 alone, so its other models carry [] — and the preset's own list is the
// answer for a model id the catalog has never heard of.
QStringList RolesDialog::effortsFor(const QString &presetId, const QString &modelId) const {
    const QJsonObject preset = presetRow(presetId);
    if (preset.isEmpty()) return {};
    const QJsonObject row = modelRow(presetId, modelId);
    const QJsonObject source = row.contains(QStringLiteral("efforts")) ? row : preset;
    // A provider Relay has never heard of carries no `efforts` at all, and then the whole ladder
    // is offered — the same fallback the pane's own box makes (Pane::offeredEfforts).
    if (!source.contains(QStringLiteral("efforts"))) return allEfforts();
    QStringList levels;
    for (const auto &level : source.value(QStringLiteral("efforts")).toArray()) levels << level.toString();
    return levels;
}

bool RolesDialog::effortFixedFor(const QString &presetId, const QString &modelId) const {
    const QString field = QStringLiteral("effort_fixed");
    const QJsonObject row = modelRow(presetId, modelId);
    if (row.contains(field)) return row.value(field).toBool();
    const QJsonObject preset = presetRow(presetId);
    if (preset.contains(field)) return preset.value(field).toBool();
    return effortsFor(presetId, modelId).isEmpty() || preset.value(QStringLiteral("hosted")).toBool();
}

QString RolesDialog::effortNoteFor(const QString &presetId) const {
    return str(presetRow(presetId), "effort_note");
}

// One reasoning-effort picker. `presetId` and `modelId` are what will serve the row: they decide the
// levels offered, the words shown for them and the note under them, because the levels are what that
// endpoint can actually be asked for. `stored` is the level in QSettings, which may well be one this
// model does not have — the answer to that is nearestOffered(), not silence. Returns nullptr when
// there is no effort knob at all, and then the row simply has no effort cell.
QComboBox *RolesDialog::effortBox(const QString &presetId, const QString &modelId, const QString &stored,
                                  const QString &name, const QString &tip,
                                  std::function<void(const QString &)> onPick) {
    const QStringList levels = effortsFor(presetId, modelId);
    // A model whose level is not this dialog's to set gets no cell at all: no knob, or Relay Free,
    // where the gateway picks the level for the role (owner, 2026-09-21). A row with no cell says
    // the same thing a greyed box does, in a dialog that has no room for a sentence per row.
    if (levels.isEmpty() || effortFixedFor(presetId, modelId)) return nullptr;
    auto *box = new QComboBox;
    box->setAccessibleName(name);
    box->setMinimumWidth(130);
    QString tooltip = tip;
    // The provider's own line about the levels it does not have ("medium is sent as high."), so a
    // level that reads as one thing and is sent as another is never a mystery.
    const QString note = effortNoteFor(presetId);
    if (!note.isEmpty()) tooltip += QStringLiteral("\n%1: %2").arg(providerName(presetId), note);
    box->setToolTip(tooltip);
    // The model's own words, in the provider's own order (card #MDL1, 2026-09-21): `xhigh` and
    // `ultra` are rows here on a codex model, and there is no Relay level behind them to label.
    box->addItem(QStringLiteral("model default"), QString());
    for (const QString &level : levels) box->addItem(level, level);
    int index = box->findData(stored);
    if (index < 0 && !stored.isEmpty()) index = box->findData(nearestOffered(levels, stored));
    box->setCurrentIndex(index >= 0 ? index : 0);
    connect(box, QOverload<int>::of(&QComboBox::activated), this, [this, box, onPick](int i) {
        onPick(box->itemData(i).toString());
        rebuild();
    });
    return box;
}

// The model cell of a row that has a provider of its own: that provider's catalog as a list, so a
// model is picked, not typed. Only a provider that sent no `models` — a local endpoint, a custom one,
// a worker older than the catalog — falls back to Model… and a free-text id.
QWidget *RolesDialog::modelCell(const QString &role, const QString &title, const QString &presetId) {
    const QString name = title + QStringLiteral(" model");
    const QString key = roleSetting(role, QStringLiteral("model"));
    const QString stored = QSettings().value(key).toString().trimmed();
    const QJsonArray models = presetRow(presetId).value(QStringLiteral("models")).toArray();
    if (models.isEmpty()) {
        auto *edit = new QPushButton(QStringLiteral("Model…"));
        edit->setAccessibleName(name);
        edit->setToolTip(QStringLiteral("%1 lists no models of its own, so type a model id; empty gives "
                                        "its default model.").arg(providerName(presetId))
                         + (stored.isEmpty() ? QString() : QStringLiteral("\nNow: ") + stored));
        connect(edit, &QPushButton::clicked, this, [this, key, name, presetId] {
            bool ok = false;
            const QString value = QInputDialog::getText(this, name,
                QStringLiteral("Model id on %1 (empty: that provider's default model)").arg(providerName(presetId)),
                QLineEdit::Normal, QSettings().value(key).toString(), &ok).trimmed();
            if (!ok) return;
            if (value.isEmpty()) QSettings().remove(key);
            else QSettings().setValue(key, value);
            if (onRolesChanged) onRolesChanged();
            rebuild();
        });
        return edit;
    }
    auto *box = new QComboBox;
    box->setAccessibleName(name);
    // The box stays a fixed, modest width so the row's text keeps its room — a catalog label can be
    // forty characters and OpenRouter's list runs to hundreds — and the popup is as wide as it needs.
    box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    box->setMinimumContentsLength(21);
    box->setMaxVisibleItems(18);
    box->setToolTip(QStringLiteral("Which of %1's models runs this job.").arg(providerName(presetId)));
    const QJsonObject standard = modelRow(presetId, QString());
    const QString standardId = str(presetRow(presetId), "model");
    const QString standardName = standard.isEmpty() || str(standard, "label").isEmpty() ? standardId
                                                                                        : str(standard, "label");
    box->addItem(standardName.isEmpty() ? QStringLiteral("provider default")
                                        : QStringLiteral("provider default (%1)").arg(standardName),
                 QString());
    for (const auto &value : models) {
        const QJsonObject row = value.toObject();
        const QString id = str(row, "id");
        if (id.isEmpty()) continue;
        box->addItem(str(row, "label").isEmpty() ? id : str(row, "label"), id);
        box->setItemData(box->count() - 1, id, Qt::ToolTipRole);
    }
    // An id typed before the catalog existed, or one the provider has since dropped, is still what
    // the job runs on: it is shown as itself instead of the box pretending to be on the default.
    if (!stored.isEmpty() && box->findData(stored) < 0) box->addItem(stored, stored);
    box->setCurrentIndex(std::max(0, box->findData(stored)));
    box->view()->setMinimumWidth(box->view()->sizeHintForColumn(0) + 32);
    box->view()->setTextElideMode(Qt::ElideMiddle);
    connect(box, QOverload<int>::of(&QComboBox::activated), this, [this, box, key](int i) {
        const QString chosen = box->itemData(i).toString();
        if (chosen.isEmpty()) QSettings().remove(key);
        else QSettings().setValue(key, chosen);
        if (onRolesChanged) onRolesChanged();
        rebuild();
    });
    return box;
}

// Which provider will serve one job: the one it is pinned to, else the one the worker says it
// resolved to, else the pane's. It decides the effort levels the row can offer, so it has to be
// the endpoint that will really take the call and not just the row's own setting.
QString RolesDialog::rolePreset(const QString &role) const {
    const QString pinned = QSettings().value(roleSetting(role, QStringLiteral("preset"))).toString();
    if (!pinned.isEmpty()) return pinned;
    const QString resolved = str(m_resolvedRoles.value(role).toObject(), "preset");
    return resolved.isEmpty() ? m_provider : resolved;
}

void RolesDialog::buildActionRow(QGridLayout *grid, int line, const QJsonObject &action) {
    const QString role = str(action, "role");
    const QString title = str(action, "label");
    const QString defaultTier = str(action, "tier");
    const QJsonObject resolved = m_resolvedRoles.value(role).toObject();
    // Every row shows what it actually resolves to, e.g. "flash · glm-5.3-flash — condensing a long
    // turn". Until the worker has resolved anything (no key yet) there is no model to name, and then
    // the row is just what the job is for, said once.
    const QString tier = str(resolved, "tier");
    const QString model = str(resolved, "model");
    QString effective = model;
    if (!tier.isEmpty() && !model.isEmpty()) effective = QStringLiteral("%1 · %2").arg(tier, model);
    const QString what = effective.isEmpty() ? str(action, "hint")
                                             : effective + QStringLiteral(" — ") + str(action, "hint");
    grid->addWidget(rowText(title, {what, str(resolved, "note")}, true), line, 0);

    if (role == QStringLiteral("route_assist")) {
        // Pinned on purpose: routing has a sub-second budget. Gemini 3.5 Flash-Lite measured a
        // 0.64 s median and 6/6 correct across the owner's examples, against 2.3-4.9 s for 3.8 Flash.
        // It is its own override, so changing the lite list never moves it by accident.
        auto *pinned = new QCheckBox(QStringLiteral("Pinned"));
        pinned->setChecked(QSettings().value(roleSetting(role, QStringLiteral("tier"))).toString().isEmpty());
        pinned->setToolTip(QStringLiteral(
            "Command routing stays on google/gemini-3.5-flash-lite: measured 0.64 s median and 6/6 correct, "
            "against 2.3-4.9 s for Gemini 3.8 Flash. The routing budget is under a second, so this is an "
            "explicit override and the lite tier does not change it."));
        connect(pinned, &QCheckBox::toggled, this, [this, role](bool on) {
            QSettings settings;
            if (on) settings.remove(roleSetting(role, QStringLiteral("tier")));
            else settings.setValue(roleSetting(role, QStringLiteral("tier")), QStringLiteral("lite"));
            if (onRolesChanged) onRolesChanged();
            rebuild();
        });
        grid->addWidget(pinned, line, 1);
        return;
    }
    if (!action.value(QStringLiteral("settable")).toBool()) {
        auto *fixed = new QLabel(QStringLiteral("this pane's model"));
        fixed->setEnabled(false);
        grid->addWidget(fixed, line, 1, 1, 4);
        return;
    }

    QSettings settings;
    const QString storedTier = settings.value(roleSetting(role, QStringLiteral("tier"))).toString();
    const QString storedPreset = settings.value(roleSetting(role, QStringLiteral("preset"))).toString();
    const QString storedModel = settings.value(roleSetting(role, QStringLiteral("model"))).toString().trimmed();
    const bool pinned = !storedPreset.isEmpty();

    // What the job follows. Owner, 2026-09-18: "i might want to pick kimi k3 for main agents and glm
    // 5.3 flash for subagents" — so a provider of this job's own is a choice beside the tiers, in the
    // same box. The two are exclusive (protocol 13.7 rejects the pair), which is why picking either
    // clears the other. The tier names are the lower-case ones Options › Models heads its lists with.
    auto *choice = new QComboBox;
    choice->setAccessibleName(title);
    choice->setMinimumWidth(165);
    choice->setToolTip(QStringLiteral("Which tier this job follows. Changing that tier's list on Options › "
                                      "Models then moves it, along with every other job that follows it."));
    choice->addItem(defaultTier.isEmpty() ? QStringLiteral("default")
                                          : QStringLiteral("default (%1)").arg(defaultTier),
                    QString());
    QHash<QString, QString> tierHints;
    for (const auto &value : m_catalog.value(QStringLiteral("tiers")).toArray())
        tierHints.insert(str(value.toObject(), "id"), str(value.toObject(), "hint"));
    for (const QString &id : tierIds()) {
        choice->addItem(id, id);
        if (!tierHints.value(id).isEmpty())
            choice->setItemData(choice->count() - 1, tierHints.value(id), Qt::ToolTipRole);
    }
    choice->addItem(QStringLiteral("its own provider…"), QStringLiteral("custom"));
    const int index = choice->findData(pinned ? QStringLiteral("custom") : storedTier);
    choice->setCurrentIndex(index >= 0 ? index : 0);
    connect(choice, QOverload<int>::of(&QComboBox::activated), this, [this, choice, role, pinned](int i) {
        const QString chosen = choice->itemData(i).toString();
        if (chosen == QStringLiteral("custom")) {
            if (pinned) return;   // already on a provider of its own; the boxes beside it are in charge
            const QString target = ownProviderFor(role);
            if (target.isEmpty()) {   // no provider is known at all: put the box back where it was
                rebuild();
                return;
            }
            writeRolePreset(role, target);
        } else {
            writeRoleTier(role, chosen);
        }
        if (onRolesChanged) onRolesChanged();
        rebuild();
    });
    grid->addWidget(choice, line, 1);

    // The provider and the model appear only once the job has a provider of its own. A model id
    // without one would have to be read against whichever endpoint the tier lands on today, which is
    // exactly the confusion the tier/endpoint split exists to avoid.
    if (pinned) {
        auto *provider = new QComboBox;
        provider->setAccessibleName(title + QStringLiteral(" provider"));
        provider->setToolTip(QStringLiteral("The provider for this job alone. Changing it gives that "
                                            "provider's default model; pick a tier on the left to go back."));
        fillProviders(provider, QString(), storedPreset);
        connect(provider, QOverload<int>::of(&QComboBox::activated), this, [this, provider, role](int i) {
            writeRolePreset(role, provider->itemData(i).toString());
            if (onRolesChanged) onRolesChanged();
            rebuild();
        });
        grid->addWidget(provider, line, 2);
        grid->addWidget(modelCell(role, title, storedPreset), line, 3);
    }

    // A job that follows its built-in default has no entry in the `roles` object at all, so an effort
    // stored for it would never be sent (Pane::rolesObject). Rather than accept a setting that does
    // nothing, the box waits until the row names a tier or a provider of its own.
    const bool hasOwnTarget = pinned || tierIds().contains(storedTier);
    if (QComboBox *effort = effortBox(rolePreset(role), pinned ? storedModel : model,
                                      settings.value(roleSetting(role, QStringLiteral("effort"))).toString(),
                                      title + QStringLiteral(" effort"),
                                      QStringLiteral("Reasoning effort for this job."),
                                      [this, role](const QString &level) {
                                          if (level.isEmpty()) QSettings().remove(roleSetting(role, QStringLiteral("effort")));
                                          else QSettings().setValue(roleSetting(role, QStringLiteral("effort")), level);
                                          if (onRolesChanged) onRolesChanged();
                                      })) {
        if (!hasOwnTarget) {
            effort->setEnabled(false);
            effort->setToolTip(QStringLiteral("This job follows its tier's reasoning level. Name a tier "
                                              "or a provider of its own to set one here."));
        }
        grid->addWidget(effort, line, 4);
    }
}

// Image context (issue EM1E): the vision model, chosen separately from the pane's own model.
//
// It is not a job that follows a tier: a turn carrying an image goes here whenever the pane's model
// cannot read one. Left at "Automatic" it is the provider's own image model (GLM-5.3 →
// GLM-5.3-Flash); with nothing to fall back to, an image turn is refused with a message instead of
// failing at the provider. So its first box is a provider, where a job's is a tier.
void RolesDialog::buildVisionRow(QGridLayout *grid, int line) {
    const QString role = QStringLiteral("vision");
    const QJsonObject resolved = m_resolvedRoles.value(role).toObject();
    const QString model = str(resolved, "model");
    const QString source = str(resolved, "source");
    const bool haveOne = !model.isEmpty() && source != QStringLiteral("main") && source != QStringLiteral("fallback");

    QWidget *text = rowText(QStringLiteral("Vision model · %1").arg(haveOne ? model : QStringLiteral("none")),
        {haveOne
             ? QStringLiteral("a prompt with an image runs here for that turn, then goes back to your model")
             : QStringLiteral("your model cannot read images and none is set, so a prompt with an image is refused")},
        true);
    text->setObjectName(QStringLiteral("visionRow"));
    grid->addWidget(text, line, 0);

    const QString storedPreset = QSettings().value(roleSetting(role, QStringLiteral("preset"))).toString();

    auto *choice = new QComboBox;
    choice->setAccessibleName(QStringLiteral("Vision provider"));
    choice->setToolTip(QStringLiteral("Which provider reads images. Automatic uses your own provider's image "
                                      "model where it has one."));
    fillProviders(choice, QStringLiteral("Automatic"), storedPreset);
    connect(choice, QOverload<int>::of(&QComboBox::activated), this, [this, choice, role](int i) {
        const QString chosen = choice->itemData(i).toString();
        QSettings settings;
        settings.remove(roleSetting(role, QStringLiteral("model")));
        settings.remove(roleSetting(role, QStringLiteral("tier")));   // vision follows no tier
        if (chosen.isEmpty()) settings.remove(roleSetting(role, QStringLiteral("preset")));
        else settings.setValue(roleSetting(role, QStringLiteral("preset")), chosen);
        if (onRolesChanged) onRolesChanged();
        rebuild();
    });
    grid->addWidget(choice, line, 1);
    // The model is read against a provider, so it waits for one — the same rule as a job's.
    if (!storedPreset.isEmpty()) grid->addWidget(modelCell(role, QStringLiteral("Vision"), storedPreset), line, 3);
}

void RolesDialog::rebuild() {
    if (m_filling) return;
    m_filling = true;
    auto *layout = qobject_cast<QVBoxLayout *>(m_rows->layout());
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (item->widget()) {
            // Hidden as well as retired: deleteLater waits for the event loop, and until then the old
            // rows would still paint underneath the new ones.
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    // One grid, thrown away and rebuilt each time: the rows' cells are its children, so deleting the
    // panel takes every widget in it and the columns line up again from scratch.
    auto *panel = new QWidget;
    auto *grid = new QGridLayout(panel);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setColumnStretch(0, 1);
    grid->setColumnMinimumWidth(0, 300);   // the job's name and its hint, which wraps
    int line = 0;
    // "vision" is in the worker's job list too, but it follows no tier, so it gets its own kind of
    // row at the end instead of a job row.
    for (const auto &value : std::as_const(m_actions)) {
        const QJsonObject action = value.toObject();
        if (str(action, "role") == QStringLiteral("vision")) continue;
        buildActionRow(grid, line++, action);
    }
    buildVisionRow(grid, line);
    layout->addWidget(panel);
    layout->addStretch(1);
    m_filling = false;
}

}  // namespace relay
