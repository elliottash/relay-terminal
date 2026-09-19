// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelSettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
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
// Four rows — Main, Flash, Lite, Local — over one default provider, plus an Advanced disclosure with
// one row per job. Settings:
//   provider/preset            the default provider (also the pane's own model)
//   provider/model             the pane's own model, which is what the Main row's Model… sets
//   agent/effort               the pane's reasoning effort, which is what the Main row's effort sets
//   tiers/<flash|lite>/{preset,model,effort}   an override for that tier; unset = the provider default
//   tiers/local/preset         a saved local endpoint id; unset = the first one in the registry
//   roles/<role>/tier          "", "main", "flash", "lite" or "local"
//   roles/<role>/{preset,model,effort}         a provider of this job's own, exclusive with the tier
// The worker resolves all of it (backend/relay_core/roles.py) and reports what each tier and role
// landed on, which is what the rows display.
//
// The Main row is the exception, and the reason it stood empty until 2026-09-18 ("you also still
// cant pick the main model options"): `tiers.main` is rejected by roles.py on purpose, because the
// Main tier is not something the worker resolves — it is the pane. So that row writes nothing here.
// Its Model… and its effort go back to the pane through onMainModelChosen / onMainEffortChosen, and
// its provider is the Default provider box above, driving the same chooseProvider().

namespace {

// Relay's four reasoning levels, low to high. A provider offers some subset of them, which the
// `presets` event carries per preset as `efforts` (presets.py `effort_levels`).
QStringList allEfforts() {
    return {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("max")};
}

// The offered level a stored one lands on: itself when the provider offers it, otherwise the nearest
// of the four above by position, ties going up. This mirrors Pane::nearestEffort, which the pane's
// own picker and Alt+. / Alt+, already use, and for the same reason: a level stored while another
// provider was in use names a request this one cannot make, and showing "Model default" instead hid
// the effort the job would really run at (owner report, 2026-09-18). An empty `offered` means the
// provider has no effort knob at all, and then no picker is drawn in the first place.
QString nearestOffered(const QStringList &offered, const QString &level) {
    if (offered.isEmpty() || offered.contains(level)) return level;
    int want = allEfforts().indexOf(level);
    if (want < 0) want = allEfforts().indexOf(QStringLiteral("high"));
    QString best;
    int bestDistance = -1;
    for (const QString &candidate : offered) {
        const int distance = qAbs(allEfforts().indexOf(candidate) - want);
        if (bestDistance < 0 || distance < bestDistance
            || (distance == bestDistance && allEfforts().indexOf(candidate) > allEfforts().indexOf(best))) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
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
    return {QStringLiteral("main"), QStringLiteral("flash"), QStringLiteral("lite"),
            QStringLiteral("local")};
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
    resize(880, 780);   // wide enough for a row's four controls beside its text, and tall
                        // enough for the tiers plus a good part of the Advanced list
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

// Preset labels name the provider and its default model ("OpenRouter · DeepSeek V4.1 Flash"), which
// is the right thing in the keys modal — you hold a key per plan — and the wrong thing here: these
// rows choose a *provider*, and the model beside it is the tier's, not the preset's. So everything in
// this dialog names the company (presets.py `provider`): Kimi, Z.AI (GLM), OpenAI (ChatGPT)…
QString RolesDialog::providerName(const QString &presetId) const {
    for (const auto &value : std::as_const(m_presets)) {
        const QJsonObject preset = value.toObject();
        if (str(preset, "id") != presetId) continue;
        const QString name = str(preset, "provider");
        return name.isEmpty() ? presetLabel(presetId).split(QStringLiteral(" · ")).first() : name;
    }
    return presetId;
}

// The same company can be offered twice — a Kimi Code key and a Moonshot platform key are different
// keys on different endpoints — and then, and only then, the plan tells the two entries apart.
QString RolesDialog::providerChoice(const QString &presetId) const {
    const QString name = providerName(presetId);
    int sharing = 0;
    for (const auto &value : choosableProviders())
        if (providerName(str(value.toObject(), "id")) == name) ++sharing;
    if (sharing < 2) return name;
    const QString plan = [&] {
        for (const auto &value : std::as_const(m_presets))
            if (str(value.toObject(), "id") == presetId) return str(value.toObject(), "plan");
        return QString();
    }();
    return plan.isEmpty() ? name : name + QStringLiteral(" · ") + plan;
}

QString RolesDialog::shortProviderLabel(const QString &presetId) const {
    return providerChoice(presetId);
}

// Only providers you hold a key for are worth offering: picking one you cannot reach just moves the
// pane onto a row that falls back. The exception is the provider already in use (and, on a fresh
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

QJsonObject RolesDialog::tierDefault(const QString &tier) const {
    return m_catalog.value(QStringLiteral("providers")).toObject().value(m_provider).toObject()
        .value(tier).toObject();
}

void RolesDialog::chooseProvider(const QString &presetId) {
    if (presetId.isEmpty() || presetId == m_provider) return;
    const QString previous = m_provider;
    m_provider = presetId;
    // Switching the default provider re-derives the tiers that followed it, and drops an override that
    // named the *old* provider: its model does not exist on the new one. An override pointing somewhere
    // else is the whole point of the row — Main on Kimi with Flash on Z.AI — so it is kept.
    QSettings settings;
    for (const QString &tier : tierIds()) {
        // The Local tier never followed the default provider, so a new one cannot invalidate it —
        // even when the provider being left behind is itself a local endpoint (card #JH22).
        if (tier == QStringLiteral("local")) continue;
        const QString pinned = settings.value(tierSetting(tier, QStringLiteral("preset"))).toString();
        if (!pinned.isEmpty() && pinned != previous && pinned != presetId) continue;
        for (const QString &field : {QStringLiteral("preset"), QStringLiteral("model"), QStringLiteral("effort")})
            settings.remove(tierSetting(tier, field));
    }
    if (onProviderChosen) onProviderChosen(presetId);
    if (onRolesChanged) onRolesChanged();
    rebuild();
}

// ----- the rows ---------------------------------------------------------------------------------
//
// Every row is four cells of one grid — the text, Model…, the provider, the effort — so Main, Flash,
// Lite, Local and the vision row line up down the dialog. A row with nothing to put in a cell leaves
// it empty rather than shuffling the rest of the row left.

// The list of providers a row can be sent to. `neutral` is the row's "nothing of my own" entry — the
// tier's provider, or the default one — and is left out for the Main row, which always names one.
// A provider whose key has gone away is still listed while the row points at it: the row would
// otherwise read as the default while the worker quietly used something else.
void RolesDialog::fillProviders(QComboBox *box, const QString &neutral, const QString &selected) const {
    if (!neutral.isEmpty()) box->addItem(neutral, QString());
    for (const auto &value : std::as_const(m_presets)) {
        const QJsonObject item = value.toObject();
        const QString id = str(item, "id");
        const bool stored = usable(item);
        if (!stored && id != selected) continue;
        box->addItem(stored ? providerChoice(id) : providerChoice(id) + QStringLiteral("  (no key)"), id);
    }
    const int index = box->findData(selected);
    box->setCurrentIndex(index >= 0 ? index : 0);
    box->setMinimumWidth(140);
}

// The default provider's own list, which the box at the top of the dialog and the Main row below it
// both show: the same providers, the same selection, either one drives chooseProvider().
void RolesDialog::fillDefaultProviders(QComboBox *box) const {
    for (const auto &value : choosableProviders()) {
        const QJsonObject preset = value.toObject();
        const bool stored = usable(preset);
        const QString id = str(preset, "id");
        box->addItem(stored ? providerChoice(id) : providerChoice(id) + QStringLiteral("  (no key)"), id);
        box->setItemData(box->count() - 1, stored, Qt::UserRole + 1);
    }
    const int index = box->findData(m_provider);
    if (index >= 0) box->setCurrentIndex(index);
}

// One reasoning-effort picker, for a tier, a job or the pane itself. `presetId` is the provider that
// will serve the row: it decides both the levels offered and the note under them, because the levels
// are what that endpoint can actually be asked for (presets.py `effort_levels`). `stored` is the
// level in QSettings, which may well be one this provider does not have — the answer to that is
// nearestOffered(), not silence. Returns nullptr when the provider has no effort knob at all, and
// then the row simply has no effort cell.
QComboBox *RolesDialog::effortBox(const QString &presetId, const QString &stored, const QString &name,
                                  const QString &tip, bool allowDefault,
                                  std::function<void(const QString &)> onPick) {
    const QStringList levels = effortsFor(presetId);
    if (levels.isEmpty()) return nullptr;
    auto *box = new QComboBox;
    box->setAccessibleName(name);
    box->setMinimumWidth(110);
    QString tooltip = tip;
    // The provider's own line about the levels it does not have ("medium is sent as high."), so a
    // level that reads as one thing and is sent as another is never a mystery.
    const QString note = effortNoteFor(presetId);
    if (!note.isEmpty()) tooltip += QStringLiteral("\n%1: %2").arg(providerName(presetId), note);
    box->setToolTip(tooltip);
    if (allowDefault) box->addItem(QStringLiteral("Model default"), QString());
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

void RolesDialog::buildTierRow(QGridLayout *grid, int line, const QString &tier, const QJsonObject &spec) {
    QSettings settings;
    const QJsonObject resolved = m_resolvedTiers.value(tier).toObject();
    const QJsonObject fallback = tierDefault(tier);
    const QString label = str(spec, "label");
    const QString model = resolved.contains(QStringLiteral("model")) ? str(resolved, "model")
                                                                     : str(fallback, "model");
    const QString preset = resolved.contains(QStringLiteral("preset")) ? str(resolved, "preset")
                                                                       : str(fallback, "preset");

    const bool isMain = tier == QStringLiteral("main");
    QString detail = str(spec, "hint");
    // Main is not a tier the worker resolves — it *is* the pane — so the row says so, and the reader
    // is not left thinking its provider cell is a private override like Flash's and Lite's.
    if (isMain) detail += QStringLiteral(" · on the default provider above");
    else if (!preset.isEmpty() && preset != m_provider) detail += QStringLiteral(" · on ") + shortProviderLabel(preset);
    QWidget *text = rowText(QStringLiteral("%1 · %2").arg(label, model.isEmpty() ? QStringLiteral("—") : model),
                            {detail}, true, str(resolved, "note"));
    grid->addWidget(text, line, 0);

    if (tier == QStringLiteral("local")) {
        // A model server on this machine (card #24XJ): the only thing to choose is *which* saved
        // endpoint. No Model… and no effort box — a local server serves one model and an
        // OpenAI-compatible endpoint has no effort knob Relay can rely on (its `efforts` is empty).
        auto *endpoint = new QComboBox;
        endpoint->setAccessibleName(QStringLiteral("Local model"));
        endpoint->setMinimumWidth(140);
        endpoint->setToolTip(QStringLiteral("Which model server on this machine serves this tier. "
                                            "Unset, it is the first saved endpoint."));
        const QString override = settings.value(tierSetting(tier, QStringLiteral("preset"))).toString();
        for (const auto &value : std::as_const(m_presets)) {
            const QJsonObject item = value.toObject();
            if (!item.value(QStringLiteral("local")).toBool()) continue;
            endpoint->addItem(providerChoice(str(item, "id")), str(item, "id"));
        }
        if (endpoint->count() == 0) {
            endpoint->addItem(QStringLiteral("No local model is set up."));
            endpoint->setEnabled(false);
            text->setEnabled(false);
        } else {
            const int index = endpoint->findData(override);
            endpoint->setCurrentIndex(index >= 0 ? index : 0);
            connect(endpoint, QOverload<int>::of(&QComboBox::activated), this, [this, endpoint](int i) {
                QSettings().setValue(tierSetting(QStringLiteral("local"), QStringLiteral("preset")),
                                     endpoint->itemData(i).toString());
                if (onRolesChanged) onRolesChanged();
                rebuild();
            });
        }
        grid->addWidget(endpoint, line, 2);
        return;
    }

    // Model…: a model id on the provider this row already uses. For Main that is the pane's own
    // model, which no `tiers` entry can carry (roles.py rejects `tiers.main`), so it goes back to
    // the pane as a set_model instead of into QSettings here.
    auto *edit = new QPushButton(QStringLiteral("Model…"));
    edit->setAccessibleName(label + QStringLiteral(" model"));
    edit->setToolTip(isMain
        ? QStringLiteral("Model id on %1 for this pane; empty restores that provider's default model.")
              .arg(providerName(m_provider))
        : QStringLiteral("Model id on %1; empty restores the provider's default for this tier.")
              .arg(providerName(preset.isEmpty() ? m_provider : preset)));
    connect(edit, &QPushButton::clicked, this, [this, tier, label, isMain] {
        bool ok = false;
        const QString key = isMain ? QStringLiteral("provider/model") : tierSetting(tier, QStringLiteral("model"));
        const QString current = QSettings().value(key).toString();
        const QString value = QInputDialog::getText(this, label + QStringLiteral(" model"),
            isMain ? QStringLiteral("Model id (empty: the provider's default model)")
                   : QStringLiteral("Model id (empty: the provider's default for this tier)"),
            QLineEdit::Normal, current, &ok).trimmed();
        if (!ok) return;
        if (isMain) {
            if (onMainModelChosen) onMainModelChosen(value);
            rebuild();
            return;
        }
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
    grid->addWidget(edit, line, 1);

    auto *provider = new QComboBox;
    if (isMain) {
        provider->setAccessibleName(QStringLiteral("Main provider"));
        provider->setToolTip(QStringLiteral("Main runs on the default provider — it is this pane's own "
                                            "model — so this is the box at the top of the dialog, and "
                                            "changing either one changes both."));
        fillDefaultProviders(provider);
        provider->setMinimumWidth(140);
        connect(provider, QOverload<int>::of(&QComboBox::activated), this, [this, provider](int i) {
            chooseProvider(provider->itemData(i).toString());
        });
    } else {
        provider->setToolTip(QStringLiteral("Which provider serves this tier. Choosing one alone gives "
                                            "that provider's own %1 model.").arg(label));
        fillProviders(provider, QStringLiteral("Default provider"),
                      settings.value(tierSetting(tier, QStringLiteral("preset"))).toString());
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
    }
    grid->addWidget(provider, line, 2);

    // Reasoning effort. Providers whose OpenAI-compatible endpoint has no effort knob (Anthropic,
    // MiniMax) report an empty list, and then there is nothing to offer. Main has no "Model default"
    // entry: the pane always runs at some level, and that level is also `agent/effort`, the one
    // Options › Default reasoning effort shows.
    QComboBox *effort = nullptr;
    if (isMain) {
        effort = effortBox(m_provider,
                           QSettings().value(QStringLiteral("agent/effort"), QStringLiteral("high")).toString(),
                           QStringLiteral("Main effort"),
                           QStringLiteral("Reasoning effort for this pane, and the default for new ones."),
                           false, [this](const QString &level) {
                               if (!level.isEmpty() && onMainEffortChosen) onMainEffortChosen(level);
                           });
    } else {
        effort = effortBox(preset.isEmpty() ? m_provider : preset,
                           settings.value(tierSetting(tier, QStringLiteral("effort"))).toString(),
                           label + QStringLiteral(" effort"),
                           QStringLiteral("Reasoning effort for the %1 tier.").arg(label),
                           true, [this, tier](const QString &level) {
                               if (level.isEmpty()) QSettings().remove(tierSetting(tier, QStringLiteral("effort")));
                               else QSettings().setValue(tierSetting(tier, QStringLiteral("effort")), level);
                               if (onRolesChanged) onRolesChanged();
                           });
    }
    if (effort) grid->addWidget(effort, line, 3);
}

QStringList RolesDialog::effortsFor(const QString &presetId) const {
    for (const auto &value : std::as_const(m_presets)) {
        const QJsonObject preset = value.toObject();
        if (str(preset, "id") != presetId) continue;
        // A provider Relay has never heard of carries no `efforts` at all, and then all four are
        // offered — the same fallback the pane's own picker makes (Pane::effortsFor).
        if (!preset.contains(QStringLiteral("efforts"))) return allEfforts();
        QStringList levels;
        for (const auto &level : preset.value(QStringLiteral("efforts")).toArray()) levels << level.toString();
        return levels;
    }
    return {};
}

QString RolesDialog::effortNoteFor(const QString &presetId) const {
    for (const auto &value : std::as_const(m_presets))
        if (str(value.toObject(), "id") == presetId) return str(value.toObject(), "effort_note");
    return {};
}

// Which provider will serve one job: the one it is pinned to, else the one the worker says it
// resolved to, else the default. It decides the effort levels the row can offer, so it has to be
// the endpoint that will really take the call and not just the row's own setting.
QString RolesDialog::rolePreset(const QString &role) const {
    const QString pinned = QSettings().value(roleSetting(role, QStringLiteral("preset"))).toString();
    if (!pinned.isEmpty()) return pinned;
    const QString resolved = str(m_resolvedRoles.value(role).toObject(), "preset");
    return resolved.isEmpty() ? m_provider : resolved;
}

void RolesDialog::buildActionRow(QGridLayout *grid, int line, const QJsonObject &action) {
    const QString role = str(action, "role");
    const QString defaultTier = str(action, "tier");
    const QJsonObject resolved = m_resolvedRoles.value(role).toObject();
    // Every advanced row shows what it actually resolves to, e.g. "Flash · glm-5.3-flash — condensing
    // a long turn". Until the worker has resolved anything (no key yet) there is no model to name, and
    // then the row is just what the job is for, said once.
    const QString tier = str(resolved, "tier");
    const QString model = str(resolved, "model");
    QString effective = model;
    if (!tier.isEmpty() && !model.isEmpty())
        effective = QStringLiteral("%1 · %2").arg(tier.left(1).toUpper() + tier.mid(1), model);
    const QString what = effective.isEmpty() ? str(action, "hint")
                                             : effective + QStringLiteral(" — ") + str(action, "hint");
    grid->addWidget(rowText(str(action, "label"), {what, str(resolved, "note")}, false), line, 0);

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
    const bool pinned = !storedPreset.isEmpty();

    // The tier choice. Owner, 2026-09-18: "i might want to pick kimi k3 for main agents and glm 5.3
    // flash for subagents" — so a provider of this job's own is a choice beside the tiers, not a
    // wizard behind them. The two are exclusive (protocol 13.7 rejects the pair), which is why
    // picking either clears the other; "Own provider" is what this box shows while the other one is
    // in charge, and picking it changes nothing.
    auto *choice = new QComboBox;
    choice->setAccessibleName(str(action, "label"));
    choice->setMinimumWidth(150);
    choice->setToolTip(QStringLiteral("Which tier this job follows. Changing that tier then moves it, "
                                      "along with every other job that follows the same one."));
    const QString followLabel = defaultTier.isEmpty()
        ? QStringLiteral("Default")
        : QStringLiteral("Same as tier (%1)").arg(defaultTier.left(1).toUpper() + defaultTier.mid(1));
    choice->addItem(followLabel, QString());
    for (const QString &id : tierIds())
        choice->addItem(id.left(1).toUpper() + id.mid(1), id);
    if (pinned) choice->addItem(QStringLiteral("Own provider"), QStringLiteral("custom"));
    const int index = choice->findData(pinned ? QStringLiteral("custom") : storedTier);
    choice->setCurrentIndex(index >= 0 ? index : 0);
    connect(choice, QOverload<int>::of(&QComboBox::activated), this, [this, choice, role](int i) {
        const QString chosen = choice->itemData(i).toString();
        if (chosen == QStringLiteral("custom")) return;   // the provider box is in charge; nothing to do
        QSettings settings;
        if (chosen.isEmpty()) settings.remove(roleSetting(role, QStringLiteral("tier")));
        else settings.setValue(roleSetting(role, QStringLiteral("tier")), chosen);
        // A tier choice replaces any hand-picked endpoint for this role.
        settings.remove(roleSetting(role, QStringLiteral("preset")));
        settings.remove(roleSetting(role, QStringLiteral("model")));
        if (onRolesChanged) onRolesChanged();
        rebuild();
    });
    grid->addWidget(choice, line, 1);

    // Model…: only once the job has a provider of its own. A model id without one would have to be
    // read against whichever endpoint the tier lands on today, which is exactly the confusion the
    // tier/endpoint split exists to avoid.
    auto *edit = new QPushButton(QStringLiteral("Model…"));
    edit->setAccessibleName(str(action, "label") + QStringLiteral(" model"));
    edit->setEnabled(pinned);
    edit->setToolTip(pinned
        ? QStringLiteral("Model id on %1; empty gives that provider's default model.").arg(providerName(storedPreset))
        : QStringLiteral("Give this job a provider of its own first; until then it uses the tier's model."));
    connect(edit, &QPushButton::clicked, this, [this, role, action, storedPreset] {
        bool ok = false;
        const QString value = QInputDialog::getText(this, str(action, "label") + QStringLiteral(" model"),
            QStringLiteral("Model id on %1 (empty: that provider's default model)").arg(providerName(storedPreset)),
            QLineEdit::Normal, QSettings().value(roleSetting(role, QStringLiteral("model"))).toString(),
            &ok).trimmed();
        if (!ok) return;
        QSettings settings;
        if (value.isEmpty()) settings.remove(roleSetting(role, QStringLiteral("model")));
        else settings.setValue(roleSetting(role, QStringLiteral("model")), value);
        if (onRolesChanged) onRolesChanged();
        rebuild();
    });
    grid->addWidget(edit, line, 2);

    auto *provider = new QComboBox;
    provider->setAccessibleName(str(action, "label") + QStringLiteral(" provider"));
    provider->setToolTip(QStringLiteral("A provider for this job alone. Choosing one alone gives that "
                                        "provider's default model; it stops following any tier."));
    fillProviders(provider, QStringLiteral("Same as tier"), storedPreset);
    connect(provider, QOverload<int>::of(&QComboBox::activated), this, [this, provider, role](int i) {
        const QString chosen = provider->itemData(i).toString();
        QSettings settings;
        settings.remove(roleSetting(role, QStringLiteral("model")));   // that provider's own default
        if (chosen.isEmpty()) {
            settings.remove(roleSetting(role, QStringLiteral("preset")));
        } else {
            settings.setValue(roleSetting(role, QStringLiteral("preset")), chosen);
            settings.remove(roleSetting(role, QStringLiteral("tier")));   // an endpoint and a tier are exclusive
        }
        if (onRolesChanged) onRolesChanged();
        rebuild();
    });
    grid->addWidget(provider, line, 3);

    // A job that follows its built-in default has no entry in the `roles` object at all, so an effort
    // stored for it would never be sent (Pane::rolesObject). Rather than accept a setting that does
    // nothing, the box waits until the row names a tier or a provider of its own.
    const bool hasOwnTarget = pinned || tierIds().contains(storedTier);
    if (QComboBox *effort = effortBox(rolePreset(role),
                                      settings.value(roleSetting(role, QStringLiteral("effort"))).toString(),
                                      str(action, "label") + QStringLiteral(" effort"),
                                      QStringLiteral("Reasoning effort for this job."), true,
                                      [this, role](const QString &level) {
                                          if (level.isEmpty()) QSettings().remove(roleSetting(role, QStringLiteral("effort")));
                                          else QSettings().setValue(roleSetting(role, QStringLiteral("effort")), level);
                                          if (onRolesChanged) onRolesChanged();
                                      })) {
        if (!hasOwnTarget) {
            effort->setEnabled(false);
            effort->setToolTip(QStringLiteral("This job follows its tier's reasoning effort. Name a tier "
                                              "or a provider of its own to set one here."));
        }
        grid->addWidget(effort, line, 4);
    }
}

// Image context (issue EM1E): the vision model, chosen separately from the pane's own model.
//
// It sits with the three tiers rather than in Advanced, because it is a model the user picks, not a
// tier a job follows: a turn carrying an image goes here whenever the pane's model cannot read one.
// Left at "Automatic" it is the provider's own image model (GLM-5.3 → GLM-5.3-Flash); with nothing
// to fall back to, an image turn is refused with a message instead of failing at the provider.
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
    const bool pinned = !storedPreset.isEmpty();

    auto *edit = new QPushButton(QStringLiteral("Model…"));
    edit->setAccessibleName(QStringLiteral("Vision model"));
    edit->setEnabled(pinned);
    edit->setToolTip(pinned
        ? QStringLiteral("Model id on %1; empty gives that provider's default model.").arg(providerName(storedPreset))
        : QStringLiteral("Pick a provider for images first; Automatic uses your own provider's image model."));
    connect(edit, &QPushButton::clicked, this, [this, role, storedPreset] {
        bool ok = false;
        const QString value = QInputDialog::getText(this, QStringLiteral("Vision model"),
            QStringLiteral("Model id on %1 (empty: that provider's default model)").arg(providerName(storedPreset)),
            QLineEdit::Normal, QSettings().value(roleSetting(role, QStringLiteral("model"))).toString(),
            &ok).trimmed();
        if (!ok) return;
        QSettings settings;
        if (value.isEmpty()) settings.remove(roleSetting(role, QStringLiteral("model")));
        else settings.setValue(roleSetting(role, QStringLiteral("model")), value);
        if (onRolesChanged) onRolesChanged();
        rebuild();
    });
    grid->addWidget(edit, line, 1);

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
    grid->addWidget(choice, line, 2);
}

void RolesDialog::rebuild() {
    if (m_filling) return;
    m_filling = true;

    m_providerBox->clear();
    fillDefaultProviders(m_providerBox);

    // The recommendation names a plan to go and buy, so here — unlike the pick lists — the plan is
    // always spelled out, whether or not you happen to hold the other key from the same company.
    auto recommend = [this](const QString &id) {
        for (const auto &value : std::as_const(m_presets)) {
            const QJsonObject preset = value.toObject();
            if (str(preset, "id") != id || str(preset, "plan").isEmpty()) continue;
            return providerName(id) + QStringLiteral(" · ") + str(preset, "plan");
        }
        return providerName(id);
    };
    QStringList pairs;
    for (const auto &value : m_catalog.value(QStringLiteral("recommended")).toArray()) {
        const QJsonArray pair = value.toArray();
        if (pair.size() == 2)
            pairs << QStringLiteral("%1 + %2").arg(recommend(pair.at(0).toString()),
                                                   recommend(pair.at(1).toString()));
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
    // One grid per section, thrown away and rebuilt each time: the rows' cells are its children, so
    // deleting the panel takes every widget in it and the columns line up again from scratch.
    auto *panel = new QWidget;
    auto *grid = new QGridLayout(panel);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setColumnStretch(0, 1);
    int line = 0;
    for (const auto &value : m_catalog.value(QStringLiteral("tiers")).toArray())
        buildTierRow(grid, line++, str(value.toObject(), "id"), value.toObject());
    buildVisionRow(grid, line);   // image context (issue EM1E): a model for pictures, beside the tiers
    clear(m_tiers)->addWidget(panel);

    m_disclosure->setText(m_showAdvanced ? QStringLiteral("▾ Advanced options")
                                         : QStringLiteral("▸ Advanced options"));
    QVBoxLayout *advanced = clear(m_advanced);
    m_advanced->setVisible(m_showAdvanced);
    if (m_showAdvanced) {
        advanced->addWidget(hint(QStringLiteral(
            "Each job follows one of the tiers above until you give it a provider of its own. Changing a "
            "tier moves every job still following it; a provider here moves only this one.")));
        auto *jobs = new QWidget;
        auto *rows = new QGridLayout(jobs);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setHorizontalSpacing(8);
        rows->setColumnStretch(0, 1);
        int job = 0;
        // "vision" has its own row beside the tiers (image context), so it is not repeated here.
        for (const auto &value : std::as_const(m_actions)) {
            const QJsonObject action = value.toObject();
            if (str(action, "role") == QStringLiteral("vision")) continue;
            buildActionRow(rows, job++, action);
        }
        advanced->addWidget(jobs);
        advanced->addStretch(1);
    }
    m_filling = false;
}

}  // namespace relay
