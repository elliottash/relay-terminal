// SPDX-License-Identifier: AGPL-3.0-or-later
#include "JobsTab.h"

#include "ModelRows.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {

namespace {
const int kRoleRole = Qt::UserRole + 1;    // the protocol role id on a job row
const int kTierRole = Qt::UserRole + 2;    // the group id on a group row
enum Column { ColJob, ColWhat, ColRuns, ColOverride };

QString str(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toString();
}
}  // namespace

// ===== rolestore ================================================================================

namespace rolestore {

namespace {
QString rawRoleSetting(const QString &role, const QString &field) {
    return QStringLiteral("roles/") + role + '/' + field;
}
}  // namespace

bool migrateLegacyPlanningOverride() {
    QSettings settings;
    const QString marker = QStringLiteral("migrations/planning_own_model_v1");
    if (settings.value(marker, false).toBool()) return false;

    bool removed = false;
    for (const char *field : {"preset", "model", "effort", "tier"}) {
        const QString key = rawRoleSetting(QStringLiteral("planning"), QLatin1String(field));
        if (!settings.contains(key)) continue;
        settings.remove(key);
        removed = true;
    }
    // Mark even a fresh install: a planning model deliberately chosen through today's Jobs UI
    // must survive every later launch instead of looking like legacy state on the next one.
    settings.setValue(marker, true);
    return removed;
}

QString roleSetting(const QString &role, const QString &field) {
    // `Pane::rolesObject` reaches every terminal pane and every console/helper worker through this
    // function. Doing the one-shot migration here makes it happen before either serializes roles,
    // without a second startup path that one kind of agent could miss (card #PMX7).
    migrateLegacyPlanningOverride();
    return rawRoleSetting(role, field);
}

QString tierSetting(const QString &tier, const QString &field) {
    return QStringLiteral("tiers/") + tier + '/' + field;
}

QString overrideKey(const QString &role) {
    QSettings settings;
    const QString preset = settings.value(roleSetting(role, QStringLiteral("preset"))).toString();
    if (preset.isEmpty()) return QString();
    return models::Catalog::keyFor(preset, settings.value(roleSetting(role, QStringLiteral("model")))
                                               .toString().trimmed());
}

QString overrideEffort(const QString &role) {
    return QSettings().value(roleSetting(role, QStringLiteral("effort"))).toString();
}

QString overrideTier(const QString &role) {
    return QSettings().value(roleSetting(role, QStringLiteral("tier"))).toString();
}

bool supportsRanked(const QString &role) {
    return role == QStringLiteral("planning") || role == QStringLiteral("subagent")
           || role == QStringLiteral("switchboard");
}

bool rankedOverrideSet(const QString &role) {
    return supportsRanked(role) && QSettings().contains(roleSetting(role, QStringLiteral("candidates")));
}

QList<models::curation::TierEntry> rankedOverride(const QString &role) {
    QList<models::curation::TierEntry> out;
    if (!supportsRanked(role)) return out;
    if (!rankedOverrideSet(role)) {
        const QString key = overrideKey(role);
        if (!key.isEmpty()) out << models::curation::TierEntry{key, overrideEffort(role), 1};
        return out;
    }
    const QByteArray encoded = QSettings().value(roleSetting(role, QStringLiteral("candidates"))).toByteArray();
    const QJsonArray rows = QJsonDocument::fromJson(encoded).array();
    for (const auto &value : rows) {
        const QJsonObject row = value.toObject();
        const QString key = row.value(QStringLiteral("key")).toString();
        QString preset, model;
        if (!models::Catalog::splitKey(key, &preset, &model) || preset.isEmpty()) continue;
        const int rank = row.value(QStringLiteral("rank")).toInt(out.size() + 1);
        out << models::curation::TierEntry{key, row.value(QStringLiteral("effort")).toString(),
                                            qBound(1, rank, 1000)};
    }
    return out;
}

void setRankedOverride(const QString &role, const QList<models::curation::TierEntry> &entries) {
    if (!supportsRanked(role)) return;
    QSettings settings;
    for (const char *field : {"preset", "model", "effort", "tier"})
        settings.remove(roleSetting(role, QLatin1String(field)));
    if (entries.isEmpty()) {
        settings.remove(roleSetting(role, QStringLiteral("candidates")));
        return;
    }
    QJsonArray rows;
    for (int i = 0; i < entries.size(); ++i) {
        const auto &entry = entries.at(i);
        rows << QJsonObject{{QStringLiteral("key"), entry.key},
                            {QStringLiteral("effort"), entry.effort},
                            {QStringLiteral("rank"), entry.rank > 0 ? entry.rank : i + 1}};
    }
    settings.setValue(roleSetting(role, QStringLiteral("candidates")),
                      QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact)));
}

// The same rule as `roles.py BACKGROUND_ROLES`, derived rather than copied: the roles on the flash
// and lite tiers that are not a pane's own mode. `tests/test_roles.py` asserts the two agree, so a
// role added to either tier in the worker is covered here without a second list to keep.
bool background(const QString &role) {
    if (role == QStringLiteral("flash")) return false;
    const QString tier = modelrows::roleTier(role);
    return tier == QStringLiteral("flash") || tier == QStringLiteral("lite");
}

bool isGuestKey(const QString &key) {
    QString preset, model;
    if (!models::Catalog::splitKey(key, &preset, &model)) return false;
    return preset.startsWith(QStringLiteral("guest:"));
}

bool setOverride(const QString &role, const QString &key, const QString &effort, QString *why) {
    if (why) why->clear();
    QSettings settings;
    if (key.isEmpty()) {
        // Back to the built-in tier, whatever the job had been put on: the endpoint, its model,
        // its level and the tier the retired dialog could pin.
        for (const char *field : {"preset", "model", "effort", "tier", "candidates"})
            settings.remove(roleSetting(role, QLatin1String(field)));
        return true;
    }
    QString preset, model;
    if (!models::Catalog::splitKey(key, &preset, &model) || preset.isEmpty()) {
        if (why) *why = QStringLiteral("that is not a model this machine knows about.");
        return false;
    }
    if (background(role) && isGuestKey(key)) {
        if (why)
            *why = QStringLiteral("%1 is a side call into a conversation that is already running, and a "
                                  "guest harness is a whole agent of its own, so the worker would skip it "
                                  "and run the job somewhere else.").arg(modelrows::roleLabel(role));
        return false;
    }
    settings.setValue(roleSetting(role, QStringLiteral("preset")), preset);
    settings.remove(roleSetting(role, QStringLiteral("candidates")));
    if (model.isEmpty()) settings.remove(roleSetting(role, QStringLiteral("model")));
    else settings.setValue(roleSetting(role, QStringLiteral("model")), model);
    // An endpoint and a tier are exclusive (protocol 13.7 refuses the pair).
    settings.remove(roleSetting(role, QStringLiteral("tier")));
    if (effort.isEmpty()) settings.remove(roleSetting(role, QStringLiteral("effort")));
    else settings.setValue(roleSetting(role, QStringLiteral("effort")), effort);
    return true;
}

}  // namespace rolestore

// ===== the table ================================================================================
//
// The order is the owner's: the tier a job follows, main first, and inside a group the job a
// person is most likely to be looking for. The words are lower-case and name the job, never the
// protocol role — "helper agent", not "switchboard"; "terminal driving", not "terminal_use".

const QList<JobsTab::Job> &JobsTab::jobs() {
    static const QList<Job> table{
        {QStringLiteral("main"), QStringLiteral("agent turns"),
         QStringLiteral("the conversation in each agent pane"), QStringLiteral("main"), false},
        {QStringLiteral("subagent"), QStringLiteral("subagents"),
         QStringLiteral("agents the main agent starts"), QStringLiteral("main"), true},
        {QStringLiteral("switchboard"), QStringLiteral("system-pane agent"),
         QStringLiteral("the Board, Options, Actions, Sessions, and Models"),
         QStringLiteral("main"), true},

        {QStringLiteral("planning"), QStringLiteral("plan mode"),
         QStringLiteral("investigating and writing a plan; plan mode puts the pane on /high "
                        "unless overridden here"),
         QStringLiteral("high"), true},
        {QStringLiteral("high"), QStringLiteral("/high panes"),
         QStringLiteral("a pane put on the high tier, for the hardest turns"), QStringLiteral("high"), true},

        {QStringLiteral("terminal_use"), QStringLiteral("terminal driving"),
         QStringLiteral("answering a program's prompts and fixing a failed command"),
         QStringLiteral("flash"), true},
        {QStringLiteral("flash"), QStringLiteral("/flash panes"),
         QStringLiteral("a pane put on the flash tier"), QStringLiteral("flash"), true},
        {QStringLiteral("summaries"), QStringLiteral("summaries"),
         QStringLiteral("condensing a conversation, compaction and recaps"), QStringLiteral("flash"), true},
        {QStringLiteral("suggestions"), QStringLiteral("suggestions"),
         QStringLiteral("the next command and the next prompt; it sends recent output"),
         QStringLiteral("flash"), true},

        {QStringLiteral("chores"), QStringLiteral("chores"),
         QStringLiteral("small structured judgements: duplicate checks, labels, titles, note scans"),
         QStringLiteral("lite"), true},
        {QStringLiteral("audit"), QStringLiteral("request audit"),
         QStringLiteral("flags an ask that may be unaddressed when a turn ends"), QStringLiteral("lite"), true},
        {QStringLiteral("loop_check"), QStringLiteral("loop check"),
         QStringLiteral("asked only when a turn repeats itself, before it is stopped"),
         QStringLiteral("lite"), true},

        {QStringLiteral("local"), QStringLiteral("/local panes"),
         QStringLiteral("a model served on this machine; no key, and nothing leaves it"),
         QStringLiteral("local"), true},

        {QStringLiteral("vision"), QStringLiteral("images"),
         QStringLiteral("a prompt carrying an image, when the active model cannot read one"),
         QStringLiteral("fixed"), true},
        {QStringLiteral("route_assist"), QStringLiteral("command routing"),
         QStringLiteral("shell or agent for one line of input; its budget is under a second"),
         QStringLiteral("fixed"), true},
    };
    return table;
}

QStringList JobsTab::groupOrder() {
    return {QStringLiteral("main"), QStringLiteral("high"), QStringLiteral("flash"),
            QStringLiteral("lite"), QStringLiteral("local"), QStringLiteral("fixed")};
}

const JobsTab::Job *JobsTab::jobFor(const QString &role) {
    for (const Job &job : jobs())
        if (job.role == role) return &job;
    return nullptr;
}

QString JobsTab::groupLabel(const QString &tier) {
    if (tier == QStringLiteral("fixed")) return QStringLiteral("fixed");
    return modelrows::roleLabel(tier);
}

namespace {
// One line under each group heading, so the tab says what a tier *is* without a person having to
// have read the design. Lite's is the load-bearing one: since the owner took the lite section off
// the priorities tab ("remove the lite section"), this tab is the only place lite is visible.
QString groupNote(const QString &tier) {
    if (tier == QStringLiteral("main")) return QStringLiteral("the pane's own model, and what follows it");
    if (tier == QStringLiteral("high")) return QStringLiteral("the hardest turns");
    if (tier == QStringLiteral("flash")) return QStringLiteral("the quick work around a conversation");
    if (tier == QStringLiteral("lite"))
        return QStringLiteral("relay free unless a provider's lite model was filled in; there is no lite "
                              "list to edit — override a job here to move it");
    if (tier == QStringLiteral("local")) return QStringLiteral("a model served on this machine");
    return QStringLiteral("these two follow no tier: relay picks them, an override moves one");
}
}  // namespace

// ===== the widget ===============================================================================

JobsTab::JobsTab(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("jobsTab"));
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_blurb = new QLabel(QStringLiteral(
        "Every job relay does has a model. A job follows the tier it is grouped under — change that "
        "tier's list on priorities and every job under it moves — until you give the job a model of "
        "its own. Planning, subagents and helper can have ranked lists. “runs on” is what this "
        "pane's worker says it is using right now."));
    m_blurb->setObjectName(QStringLiteral("transcriptHeader"));
    m_blurb->setWordWrap(true);
    layout->addWidget(m_blurb);

    m_list = new QTreeWidget;
    m_list->setObjectName(QStringLiteral("jobsList"));
    m_list->setHeaderLabels({QStringLiteral("job"), QStringLiteral("what it does"),
                             QStringLiteral("runs on"), QStringLiteral("override")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(false);
    m_list->setAllColumnsShowFocus(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // "what it does" takes the slack and the other three keep a width: the column this tab exists
    // for must not be the one that goes off the edge when the pane is narrow, and the first Xvfb
    // run had "runs on" and the override behind a horizontal scrollbar with Options open beside it.
    m_list->setColumnWidth(ColJob, 130);
    m_list->setColumnWidth(ColRuns, 165);
    m_list->setColumnWidth(ColOverride, 300);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(ColJob, QHeaderView::Interactive);
    m_list->header()->setSectionResizeMode(ColWhat, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(ColRuns, QHeaderView::Interactive);
    m_list->header()->setSectionResizeMode(ColOverride, QHeaderView::Interactive);
    m_list->installEventFilter(this);
    layout->addWidget(m_list, 1);

    m_compactPanel = new QWidget;
    auto *compactBox = new QVBoxLayout(m_compactPanel);
    compactBox->setContentsMargins(0, 0, 0, 0);
    compactBox->setSpacing(3);
    m_compactDetails = new QLabel;
    m_compactDetails->setWordWrap(true);
    m_compactDetails->setMinimumHeight(3 * m_compactDetails->fontMetrics().lineSpacing() + 4);
    compactBox->addWidget(m_compactDetails);
    auto *compactActions = new QHBoxLayout;
    m_compactChoose = new QPushButton(QStringLiteral("choose model…"));
    m_compactChoose->setObjectName(QStringLiteral("jobsCompactChoose"));
    m_compactClear = new QPushButton(QStringLiteral("clear override"));
    compactActions->addWidget(m_compactChoose);
    compactActions->addWidget(m_compactClear);
    compactActions->addStretch(1);
    compactBox->addLayout(compactActions);
    m_compactPanel->hide();
    layout->addWidget(m_compactPanel);

    m_rankedPanel = new QWidget;
    m_rankedPanel->setObjectName(QStringLiteral("jobsRankedPanel"));
    auto *rankedBox = new QVBoxLayout(m_rankedPanel);
    rankedBox->setContentsMargins(0, 4, 0, 0);
    rankedBox->setSpacing(4);
    m_rankedStatus = new QLabel;
    m_rankedStatus->setWordWrap(true);
    rankedBox->addWidget(m_rankedStatus);
    m_rankedList = new QTreeWidget;
    m_rankedList->setObjectName(QStringLiteral("jobsRankedList"));
    m_rankedList->setHeaderLabels({QStringLiteral("order"), QStringLiteral("model"), QStringLiteral("effort")});
    m_rankedList->setRootIsDecorated(false);
    m_rankedList->setMaximumHeight(150);
    m_rankedList->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_rankedList->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_rankedList->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    rankedBox->addWidget(m_rankedList);
    auto *rankedActions = new QHBoxLayout;
    rankedActions->setSpacing(3);
    auto action = [](QHBoxLayout *row, const QString &name, const QString &label) {
        auto *button = new QPushButton(label);
        button->setObjectName(name);
        row->addWidget(button);
        return button;
    };
    m_rankedAdd = action(rankedActions, QStringLiteral("jobsRankedAdd"), QStringLiteral("Add model…"));
    m_rankedUp = action(rankedActions, QStringLiteral("jobsRankedUp"), QStringLiteral("↑"));
    m_rankedDown = action(rankedActions, QStringLiteral("jobsRankedDown"), QStringLiteral("↓"));
    m_rankedTie = action(rankedActions, QStringLiteral("jobsRankedTie"), QStringLiteral("Tie ↑"));
    rankedBox->addLayout(rankedActions);
    auto *moreActions = new QHBoxLayout;
    moreActions->setSpacing(3);
    m_rankedUntie = action(moreActions, QStringLiteral("jobsRankedUntie"), QStringLiteral("Untie"));
    m_rankedEffort = action(moreActions, QStringLiteral("jobsRankedEffort"), QStringLiteral("Effort…"));
    m_rankedRemove = action(moreActions, QStringLiteral("jobsRankedRemove"), QStringLiteral("Remove"));
    m_rankedFollow = action(moreActions, QStringLiteral("jobsRankedFollow"), QStringLiteral("Follow tier"));
    rankedBox->addLayout(moreActions);
    m_rankedUp->setToolTip(QStringLiteral("Move this rank group earlier."));
    m_rankedDown->setToolTip(QStringLiteral("Move this rank group later."));
    m_rankedTie->setToolTip(QStringLiteral("Give this model the preceding rank; tied models are chosen at random."));
    m_rankedUntie->setToolTip(QStringLiteral("Give this model its own rank."));
    m_rankedPanel->hide();
    layout->addWidget(m_rankedPanel);

    m_footer = new QLabel(QStringLiteral(
        "↑↓ select a job · Enter edits its route · Delete follows its tier · Esc back to the pane"));
    m_footer->setObjectName(QStringLiteral("transcriptHeader"));
    m_footer->setWordWrap(true);
    layout->addWidget(m_footer);

    connect(m_list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *, int) { openOverride(); });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) { openOverride(); });
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this] {
        updateCompactDetails();
        updateRankedPanel();
    });
    connect(m_compactChoose, &QPushButton::clicked, this, [this] { openOverride(); });
    connect(m_compactClear, &QPushButton::clicked, this, [this] { clearOverride(); });
    connect(m_rankedList, &QTreeWidget::currentItemChanged, this, [this] { updateRankedPanel(); });
    connect(m_rankedAdd, &QPushButton::clicked, this, [this] { addRankedModel(); });
    connect(m_rankedUp, &QPushButton::clicked, this, [this] { moveRanked(-1); });
    connect(m_rankedDown, &QPushButton::clicked, this, [this] { moveRanked(1); });
    connect(m_rankedTie, &QPushButton::clicked, this, [this] { tieRanked(true); });
    connect(m_rankedUntie, &QPushButton::clicked, this, [this] { tieRanked(false); });
    connect(m_rankedEffort, &QPushButton::clicked, this, [this] { chooseRankedEffort(); });
    connect(m_rankedRemove, &QPushButton::clicked, this, [this] { removeRanked(); });
    connect(m_rankedFollow, &QPushButton::clicked, this, [this] { clearOverride(); });
    rebuild();
}

void JobsTab::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    // The tree's viewport is laid out after its parent; use its final width for the columns.
    QTimer::singleShot(0, this, [this] { updateColumns(); });
}

void JobsTab::updateColumns() {
    if (!m_list) return;
    const bool compact = width() < 620;
    m_list->setColumnHidden(ColWhat, compact);
    m_list->setColumnHidden(ColOverride, compact);
    m_list->header()->setSectionResizeMode(ColRuns, compact ? QHeaderView::Stretch : QHeaderView::Interactive);
    m_compactPanel->setVisible(compact);
    m_blurb->setText(compact
        ? QStringLiteral("Each job follows its tier until given its own model or list. “runs on” shows the latest worker report.")
        : QStringLiteral("Every job relay does has a model. A job follows the tier it is grouped under — change that "
                         "tier's list on priorities and every job under it moves — until you give the job a model of "
                         "its own. Planning, subagents and helper can have ranked lists. “runs on” shows the latest worker report."));
    if (compact) {
        const int room = m_list->viewport()->width();
        m_list->setColumnWidth(ColJob, qBound(130, room * 45 / 100, 200));
    } else {
        m_list->setColumnWidth(ColJob, 130);
        m_list->setColumnWidth(ColOverride, 300);
        m_list->setColumnWidth(ColRuns, 165);
    }
    updateCompactDetails();
}

void JobsTab::updateCompactDetails() {
    if (!m_compactDetails) return;
    const Job *job = jobFor(currentRole());
    if (!job) {
        m_compactDetails->setText(QStringLiteral("Select a job to see its override."));
        m_compactChoose->setEnabled(false);
        m_compactClear->setVisible(false);
        return;
    }
    const bool custom = !rolestore::overrideKey(job->role).isEmpty()
        || !rolestore::overrideTier(job->role).isEmpty()
        || rolestore::rankedOverrideSet(job->role);
    m_compactDetails->setText(QStringLiteral("%1 — %2\n%3: %4")
        .arg(job->name, job->what,
             !job->settable ? QStringLiteral("pane route")
             : custom ? QStringLiteral("custom route") : QStringLiteral("inherited route"),
             overrideText(job->role)));
    m_compactChoose->setText(rolestore::supportsRanked(job->role)
        ? QStringLiteral("Edit ranked list") : QStringLiteral("Choose model…"));
    m_compactChoose->setEnabled(job->settable);
    m_compactClear->setVisible(job->settable &&
        (!rolestore::overrideKey(job->role).isEmpty() || !rolestore::overrideTier(job->role).isEmpty()
         || rolestore::rankedOverrideSet(job->role)));
}

void JobsTab::setData(const Data &data) {
    const QString keep = currentRole();
    m_data = data;
    rebuild();
    if (!keep.isEmpty()) selectRole(keep);
}

QString JobsTab::nameFor(const QString &preset, const QString &model) const {
    if (model.isEmpty() && preset.isEmpty()) return QString();
    // The catalog's own name wherever it has one — the worker computes it and it is the only name
    // a model has (rule 1) — resolved through `resolveKey`, so a guest reporting `claude-opus-5-5`
    // reads as the row it is (`guest:claude|opus`).
    const QString key = m_data.catalog.resolveKey(preset, model);
    if (const models::Entry *entry = m_data.catalog.find(key.isEmpty()
                                                             ? models::Catalog::keyFor(preset, model)
                                                             : key))
        if (!entry->name.isEmpty()) return entry->name;
    return model.isEmpty() ? preset : models::nameOf(model);
}

QString JobsTab::resolvedText(const QJsonObject &entry) const {
    if (entry.isEmpty()) return QString();
    const QString name = nameFor(str(entry, "preset"), str(entry, "model"));
    if (name.isEmpty()) return QString();
    const QString effort = str(entry, "effort");
    return effort.isEmpty() ? name : QStringLiteral("%1 · %2").arg(name, effort);
}

QString JobsTab::runsOn(const QString &role) const {
    return resolvedText(m_data.roles.value(role).toObject());
}

QString JobsTab::tierRunsOn(const QString &tier) const {
    if (tier == QStringLiteral("fixed")) return QString();
    return resolvedText(m_data.tiers.value(tier).toObject());
}

QString JobsTab::overrideText(const QString &role) const {
    const Job *job = jobFor(role);
    if (job && !job->settable) return QStringLiteral("each agent pane's active model");
    if (rolestore::rankedOverrideSet(role)) {
        const auto entries = rolestore::rankedOverride(role);
        if (!entries.isEmpty()) {
            const int firstRank = std::min_element(entries.begin(), entries.end(),
                [](const auto &a, const auto &b) { return a.rank < b.rank; })->rank;
            const int peers = std::count_if(entries.begin(), entries.end(),
                [firstRank](const auto &entry) { return entry.rank == firstRank; });
            return QStringLiteral("%1 model%2%3").arg(entries.size())
                .arg(entries.size() == 1 ? QString() : QStringLiteral("s"))
                .arg(peers > 1 ? QStringLiteral(" · random at rank 1") : QString());
        }
    }
    const QString key = rolestore::overrideKey(role);
    if (!key.isEmpty()) {
        QString preset, model;
        models::Catalog::splitKey(key, &preset, &model);
        const QString effort = rolestore::overrideEffort(role);
        const QString name = nameFor(preset, model);
        return effort.isEmpty() ? name : QStringLiteral("%1 · %2").arg(name, effort);
    }
    const QString stored = rolestore::overrideTier(role);
    if (!stored.isEmpty()) return QStringLiteral("follows %1").arg(stored);
    if (job && job->tier == QStringLiteral("fixed")) return QStringLiteral("automatic");
    return QStringLiteral("follows %1").arg(job ? job->tier : modelrows::roleTier(role));
}

models::Entry JobsTab::entryFor(const models::Group &group, bool background) const {
    const models::Entry preferred = group.preferred(m_data.catalog, m_data.now);
    if (!background || !preferred.guest) return preferred;
    // A background job cannot run on a harness, so the row stands for the next provider that
    // serves the same name — and where every provider of that name is a harness, for none.
    for (const models::Entry &entry : group.entries)
        if (!entry.guest) return entry;
    return {};
}

QList<FilterRow> JobsTab::overrideRows(const QString &role) const {
    QList<FilterRow> rows;
    const Job *job = jobFor(role);
    FilterRow follows;
    follows.text = job && job->tier == QStringLiteral("fixed")
                       ? QStringLiteral("automatic — relay picks it")
                       : QStringLiteral("follows %1").arg(job ? job->tier : modelrows::roleTier(role));
    follows.tooltip = QStringLiteral("No model of its own: this job takes the first entry of its tier's "
                                     "list that can serve it.");
    rows << follows;
    const bool side = rolestore::background(role);
    for (const models::Group &group : models::grouped(m_data.catalog, models::shown(m_data.catalog), m_data.now)) {
        const models::Entry entry = entryFor(group, side);
        if (entry.key.isEmpty()) continue;   // every provider of this name is a harness; not offered
        FilterRow row;
        row.text = group.name;
        row.data = entry.key;
        row.trailing = entry.provider.isEmpty() ? entry.preset : entry.provider;
        row.tooltip = entry.displayName();
        rows << row;
    }
    return rows;
}

QList<FilterRow> JobsTab::levelRows(const QString &role, const QString &key) const {
    Q_UNUSED(role);
    QList<FilterRow> rows;
    const models::Entry *entry = m_data.catalog.find(key);
    if (!entry || entry->effortFixed || entry->efforts.isEmpty()) return rows;
    FilterRow own;
    own.text = QStringLiteral("model default");
    own.tooltip = QStringLiteral("Whatever this model does when no level is asked for.");
    rows << own;
    for (const QString &level : entry->efforts) {
        FilterRow row;
        row.text = level;
        row.data = level;
        rows << row;
    }
    return rows;
}

// The rows are **flat**, headings included. A heading was a parent item at first, and Qt then
// propagates a disabled parent to its children (`QTreeWidgetItemPrivate::propagateDisabled`), so
// making the heading unselectable made every job under it unselectable too — no row could be
// current, and Enter and Delete reached nothing. A heading is a top-level row of its own now, with
// `NoItemFlags`, which is exactly what makes Up and Down step over it (the same ruling the model
// box got: "the class header rows are not selectable in the picker").
void JobsTab::rebuild() {
    m_list->clear();
    for (const QString &tier : groupOrder()) {
        QList<const Job *> members;
        for (const Job &job : jobs())
            if (job.tier == tier) members << &job;
        if (members.isEmpty()) continue;

        auto *group = new QTreeWidgetItem(m_list);
        group->setData(0, kTierRole, tier);
        group->setText(ColJob, groupLabel(tier));
        group->setText(ColWhat, groupNote(tier));
        group->setText(ColRuns, tierRunsOn(tier));
        QFont bold = group->font(ColJob);
        bold.setBold(true);
        group->setFont(ColJob, bold);
        group->setFlags(Qt::NoItemFlags);

        for (const Job *job : members) {
            auto *item = new QTreeWidgetItem(m_list);
            item->setData(0, kRoleRole, job->role);
            // Indented by two spaces rather than by a parent, for the reason above.
            item->setText(ColJob, QStringLiteral("  ") + job->name);
            item->setText(ColWhat, job->what);
            const QString runs = runsOn(job->role);
            item->setText(ColRuns, runs.isEmpty() ? QStringLiteral("—") : runs);
            const bool custom = !rolestore::overrideKey(job->role).isEmpty()
                || !rolestore::overrideTier(job->role).isEmpty()
                || rolestore::rankedOverrideSet(job->role);
            item->setText(ColOverride, QStringLiteral("%1 · %2")
                .arg(!job->settable ? QStringLiteral("pane")
                     : custom ? QStringLiteral("custom") : QStringLiteral("inherited"),
                     overrideText(job->role)));

            const QJsonObject resolved = m_data.roles.value(job->role).toObject();
            QStringList tip{job->what,
                            QStringLiteral("runs on: %1").arg(item->text(ColRuns)),
                            QStringLiteral("override: %1").arg(overrideText(job->role))};
            if (runs.isEmpty())
                tip << QStringLiteral("Nothing is resolved yet: no worker report has arrived.");
            if (!str(resolved, "note").isEmpty()) tip << str(resolved, "note");
            if (!str(resolved, "warning").isEmpty()) tip << str(resolved, "warning");
            if (!job->settable)
                tip << QStringLiteral("Agent turns use each agent pane's active model. Change it in that "
                                      "pane's model box.");
            else if (rolestore::background(job->role))
                tip << QStringLiteral("A side call into a conversation running somewhere else, so a guest "
                                      "harness (claude code, codex) cannot take it and is not offered here; "
                                      "with nothing in its tier able to serve it, it falls through to relay "
                                      "free rather than to your own model.");
            for (int column = 0; column < 4; ++column) item->setToolTip(column, tip.join(QLatin1Char('\n')));

            const bool overridden = !rolestore::overrideKey(job->role).isEmpty()
                                    || !rolestore::overrideTier(job->role).isEmpty()
                                    || rolestore::rankedOverrideSet(job->role);
            if (!job->settable || !overridden) {
                item->setForeground(ColOverride, m_list->palette().brush(QPalette::Disabled, QPalette::Text));
                continue;
            }
            // "A small × clears it." Only on a row that has something to clear.
            auto *cell = new QWidget;
            auto *box = new QHBoxLayout(cell);
            box->setContentsMargins(0, 0, 0, 0);
            box->setSpacing(4);
            box->addWidget(new QLabel(QStringLiteral("custom · %1").arg(overrideText(job->role))), 1);
            auto *clear = new QToolButton;
            clear->setObjectName(QStringLiteral("jobsClearOverride"));
            clear->setText(QStringLiteral("×"));
            clear->setAutoRaise(true);
            clear->setToolTip(QStringLiteral("Put %1 back on its tier.").arg(job->name));
            const QString role = job->role;
            connect(clear, &QToolButton::clicked, this, [this, role] {
                selectRole(role);
                clearOverride();
            });
            box->addWidget(clear, 0);
            item->setText(ColOverride, QString());
            m_list->setItemWidget(item, ColOverride, cell);
        }
    }
    updateCompactDetails();
}

QString JobsTab::currentRole() const {
    QTreeWidgetItem *item = m_list->currentItem();
    return item ? item->data(0, kRoleRole).toString() : QString();
}

bool JobsTab::selectRole(const QString &role) {
    if (role.isEmpty()) return false;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (item->data(0, kRoleRole).toString() != role) continue;
        m_list->setCurrentItem(item);
        return true;
    }
    return false;
}

void JobsTab::focusList() {
    if (currentRole().isEmpty())
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            if (!m_list->topLevelItem(i)->data(0, kRoleRole).toString().isEmpty()) {
                m_list->setCurrentItem(m_list->topLevelItem(i));
                break;
            }
    m_list->setFocus(Qt::OtherFocusReason);
}

// Where the model list drops: over the row's own override cell.
QWidget *JobsTab::anchorForCurrentRow() {
    if (!m_anchor) {
        m_anchor = new QWidget(m_list->viewport());
        m_anchor->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    QTreeWidgetItem *item = m_list->currentItem();
    const QRect rect = item ? m_list->visualItemRect(item) : QRect();
    const int anchorColumn = m_list->isColumnHidden(ColOverride) ? ColRuns : ColOverride;
    if (rect.isValid())
        m_anchor->setGeometry(m_list->columnViewportPosition(anchorColumn), rect.top(),
                              std::max(120, m_list->columnWidth(anchorColumn)), rect.height());
    else
        m_anchor->setGeometry(0, 0, m_list->viewport()->width(), 1);
    m_anchor->show();
    return m_anchor;
}

void JobsTab::updateRankedPanel() {
    const QString role = currentRole();
    const Job *job = jobFor(role);
    const bool ranked = job && rolestore::supportsRanked(role);
    m_rankedPanel->setVisible(ranked);
    if (!ranked) return;

    const int selected = m_rankedList->indexOfTopLevelItem(m_rankedList->currentItem());
    auto entries = rolestore::rankedOverride(role);
    std::stable_sort(entries.begin(), entries.end(),
                     [](const auto &a, const auto &b) { return a.rank < b.rank; });
    {
        QSignalBlocker blocked(m_rankedList);
        m_rankedList->clear();
        for (const auto &entry : entries) {
            QString preset, model;
            models::Catalog::splitKey(entry.key, &preset, &model);
            auto *row = new QTreeWidgetItem(m_rankedList);
            const int peers = std::count_if(entries.begin(), entries.end(),
                [&](const auto &other) { return other.rank == entry.rank; });
            row->setText(0, peers > 1 ? QStringLiteral("%1 · random").arg(entry.rank)
                                      : QString::number(entry.rank));
            row->setText(1, nameFor(preset, model));
            row->setText(2, entry.effort.isEmpty() ? QStringLiteral("default") : entry.effort);
            row->setData(1, Qt::UserRole, entry.key);
        }
        if (!entries.isEmpty())
            m_rankedList->setCurrentItem(m_rankedList->topLevelItem(qBound(0, selected, entries.size() - 1)));
    }
    const bool custom = !entries.isEmpty();
    m_rankedStatus->setText(custom
        ? QStringLiteral("%1 — custom route. Top models run first; tied models are chosen at random."
                         " The worker's current choice stays in ‘runs on’ above.").arg(job->name)
        : QStringLiteral("%1 — inherits %2. Add a model to make a custom ranked route.")
              .arg(job->name, rolestore::overrideTier(role).isEmpty()
                   ? job->tier : rolestore::overrideTier(role)));
    const int at = m_rankedList->indexOfTopLevelItem(m_rankedList->currentItem());
    const bool chosen = at >= 0 && at < entries.size();
    const int rank = chosen ? entries.at(at).rank : -1;
    const bool hasPreviousGroup = chosen && entries.first().rank < rank;
    const bool hasNextGroup = chosen && entries.last().rank > rank;
    const int peers = chosen ? std::count_if(entries.begin(), entries.end(),
        [rank](const auto &entry) { return entry.rank == rank; }) : 0;
    m_rankedUp->setEnabled(hasPreviousGroup);
    m_rankedDown->setEnabled(hasNextGroup);
    m_rankedTie->setEnabled(hasPreviousGroup);
    m_rankedUntie->setEnabled(peers > 1);
    m_rankedRemove->setEnabled(chosen);
    const auto *model = chosen ? m_data.catalog.find(entries.at(at).key) : nullptr;
    m_rankedEffort->setEnabled(model && !model->effortFixed && !model->efforts.isEmpty());
    m_rankedFollow->setVisible(rolestore::rankedOverrideSet(role) || custom
                               || !rolestore::overrideTier(role).isEmpty());
}

void JobsTab::saveRanked(const QString &role, QList<models::curation::TierEntry> entries) {
    std::stable_sort(entries.begin(), entries.end(),
                     [](const auto &a, const auto &b) { return a.rank < b.rank; });
    // Keep stored ranks dense while preserving equal ranks as one randomized group.
    int rank = 0, oldRank = -1;
    for (auto &entry : entries) {
        if (entry.rank != oldRank) { oldRank = entry.rank; ++rank; }
        entry.rank = rank;
    }
    rolestore::setRankedOverride(role, entries);
    if (m_data.rolesChanged) m_data.rolesChanged();
    rebuild();
    selectRole(role);
    updateRankedPanel();
}

void JobsTab::addRankedModel() {
    const QString role = currentRole();
    if (!rolestore::supportsRanked(role)) return;
    const auto entries = rolestore::rankedOverride(role);
    QList<FilterRow> choices;
    for (const auto &row : overrideRows(role)) {
        if (row.data.isEmpty()) continue;
        if (std::any_of(entries.begin(), entries.end(),
                        [&](const auto &entry) { return entry.key == row.data; })) continue;
        choices << row;
    }
    if (choices.isEmpty()) { m_rankedStatus->setText(QStringLiteral("Every available model is already listed.")); return; }
    if (!m_popup) m_popup = new FilterPopup(this);
    m_picking = role;
    m_popup->onPicked = [this, role](int index) {
        if (index < 0 || index >= m_popup->rows().size()) return;
        const QString key = m_popup->rows().at(index).data;
        auto rows = rolestore::rankedOverride(role);
        int nextRank = 1;
        for (const auto &entry : rows) nextRank = qMax(nextRank, entry.rank + 1);
        rows << models::curation::TierEntry{key, models::curation::listEffortFor(key), nextRank};
        saveRanked(role, rows);
        m_rankedList->setCurrentItem(m_rankedList->topLevelItem(rows.size() - 1));
        m_rankedList->setFocus(Qt::OtherFocusReason);
    };
    m_popup->onCancelled = [this] { m_picking.clear(); m_rankedList->setFocus(Qt::OtherFocusReason); };
    m_popup->setRows(choices, 0);
    m_popup->openFor(m_rankedAdd);
}

void JobsTab::moveRanked(int direction) {
    const QString role = currentRole();
    auto rows = rolestore::rankedOverride(role);
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.rank < b.rank; });
    const int at = m_rankedList->indexOfTopLevelItem(m_rankedList->currentItem());
    if (at < 0 || at >= rows.size()) return;
    const int rank = rows.at(at).rank;
    int otherRank = direction < 0 ? -1 : 1001;
    for (const auto &entry : rows)
        if (direction < 0 && entry.rank < rank) otherRank = qMax(otherRank, entry.rank);
        else if (direction > 0 && entry.rank > rank) otherRank = qMin(otherRank, entry.rank);
    if (otherRank < 0 || otherRank > 1000) return;
    const QString key = rows.at(at).key;
    for (auto &entry : rows) {
        if (entry.rank == rank) entry.rank = otherRank;
        else if (entry.rank == otherRank) entry.rank = rank;
    }
    saveRanked(role, rows);
    for (int i = 0; i < m_rankedList->topLevelItemCount(); ++i)
        if (m_rankedList->topLevelItem(i)->data(1, Qt::UserRole).toString() == key)
            m_rankedList->setCurrentItem(m_rankedList->topLevelItem(i));
}

void JobsTab::tieRanked(bool tie) {
    const QString role = currentRole();
    auto rows = rolestore::rankedOverride(role);
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.rank < b.rank; });
    const int at = m_rankedList->indexOfTopLevelItem(m_rankedList->currentItem());
    if (at < 0 || at >= rows.size()) return;
    const QString key = rows.at(at).key;
    const int rank = rows.at(at).rank;
    if (tie) {
        if (at == 0 || rows.at(at - 1).rank == rank) return;
        rows[at].rank = rows.at(at - 1).rank;
    } else {
        if (std::count_if(rows.begin(), rows.end(),
                [rank](const auto &entry) { return entry.rank == rank; }) < 2) return;
        for (auto &entry : rows) if (entry.rank > rank) ++entry.rank;
        rows[at].rank = rank + 1;
    }
    saveRanked(role, rows);
    for (int i = 0; i < m_rankedList->topLevelItemCount(); ++i)
        if (m_rankedList->topLevelItem(i)->data(1, Qt::UserRole).toString() == key)
            m_rankedList->setCurrentItem(m_rankedList->topLevelItem(i));
}

void JobsTab::removeRanked() {
    const QString role = currentRole();
    auto rows = rolestore::rankedOverride(role);
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.rank < b.rank; });
    const int at = m_rankedList->indexOfTopLevelItem(m_rankedList->currentItem());
    if (at < 0 || at >= rows.size()) return;
    rows.removeAt(at);
    saveRanked(role, rows);
}

void JobsTab::chooseRankedEffort() {
    const QString role = currentRole();
    const int at = m_rankedList->indexOfTopLevelItem(m_rankedList->currentItem());
    auto rows = rolestore::rankedOverride(role);
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.rank < b.rank; });
    if (at < 0 || at >= rows.size()) return;
    const QString key = rows.at(at).key;
    const auto choices = levelRows(role, key);
    if (choices.isEmpty()) return;
    if (!m_popup) m_popup = new FilterPopup(this);
    int current = 0;
    for (int i = 0; i < choices.size(); ++i)
        if (choices.at(i).data == rows.at(at).effort) { current = i; break; }
    m_popup->onPicked = [this, role, key](int index) {
        if (index < 0 || index >= m_popup->rows().size()) return;
        auto entries = rolestore::rankedOverride(role);
        for (auto &entry : entries) if (entry.key == key) entry.effort = m_popup->rows().at(index).data;
        saveRanked(role, entries);
    };
    m_popup->onCancelled = [this] { m_rankedList->setFocus(Qt::OtherFocusReason); };
    m_popup->setRows(choices, current);
    m_popup->openFor(m_rankedEffort);
}

bool JobsTab::openOverride() {
    const QString role = currentRole();
    const Job *job = jobFor(role);
    if (!job || !job->settable) return false;
    if (rolestore::supportsRanked(role)) {
        updateRankedPanel();
        if (m_rankedList->topLevelItemCount()) m_rankedList->setFocus(Qt::OtherFocusReason);
        else m_rankedAdd->setFocus(Qt::OtherFocusReason);
        return true;
    }
    m_picking = role;
    if (!m_popup) m_popup = new FilterPopup(this);
    const QList<FilterRow> rows = overrideRows(role);
    const QString current = rolestore::overrideKey(role);
    int at = 0;
    for (int i = 0; i < rows.size(); ++i)
        if (!rows.at(i).data.isEmpty() && rows.at(i).data == current) { at = i; break; }
    m_popup->onPicked = [this, rows](int index) {
        if (index < 0 || index >= m_popup->rows().size()) return;
        const QString role = m_picking;
        const QString key = m_popup->rows().at(index).data;
        if (key.isEmpty()) { applyOverride(role, QString(), QString()); return; }
        // A model with levels of its own asks for one next, so an override is a model *and* a
        // level in one pass of the keyboard — the level list is the same control, beside it.
        if (levelRows(role, key).isEmpty()) { applyOverride(role, key, QString()); return; }
        applyOverride(role, key, models::curation::listEffortFor(key));
        openLevels(role, key);
    };
    m_popup->onCancelled = [this] { m_picking.clear(); m_list->setFocus(Qt::OtherFocusReason); };
    m_popup->setRows(rows, at);
    m_popup->openFor(anchorForCurrentRow());
    return true;
}

void JobsTab::openLevels(const QString &role, const QString &key) {
    const QList<FilterRow> rows = levelRows(role, key);
    if (rows.isEmpty()) return;
    if (!m_popup) m_popup = new FilterPopup(this);
    const QString current = rolestore::overrideEffort(role);
    int at = 0;
    for (int i = 0; i < rows.size(); ++i)
        if (!rows.at(i).data.isEmpty() && rows.at(i).data == current) { at = i; break; }
    m_popup->onPicked = [this, role, key](int index) {
        if (index < 0 || index >= m_popup->rows().size()) return;
        applyOverride(role, key, m_popup->rows().at(index).data);
    };
    m_popup->onCancelled = [this] { m_picking.clear(); m_list->setFocus(Qt::OtherFocusReason); };
    m_popup->setRows(rows, at);
    m_popup->openFor(anchorForCurrentRow());
}

bool JobsTab::clearOverride() {
    const QString role = currentRole();
    const Job *job = jobFor(role);
    if (!job || !job->settable) return false;
    if (rolestore::overrideKey(role).isEmpty() && rolestore::overrideTier(role).isEmpty()
        && !rolestore::rankedOverrideSet(role)) return false;
    applyOverride(role, QString(), QString());
    return true;
}

void JobsTab::applyOverride(const QString &role, const QString &key, const QString &effort) {
    QString why;
    if (!rolestore::setOverride(role, key, effort, &why)) {
        if (m_footer && !why.isEmpty()) m_footer->setText(why);
        return;
    }
    m_picking.clear();
    // The worker is told at once, and its next `model_roles` repaints "runs on" — this tab never
    // guesses what a change resolves to, it shows what came back.
    if (m_data.rolesChanged) m_data.rolesChanged();
    rebuild();
    selectRole(role);
    m_list->setFocus(Qt::OtherFocusReason);
}

bool JobsTab::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::NoModifier) {
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                if (openOverride()) return true;
            } else if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) {
                if (clearOverride()) return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void JobsTab::keyPressEvent(QKeyEvent *event) {
    if (event->modifiers() == Qt::NoModifier) {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && openOverride()) {
            event->accept();
            return;
        }
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && clearOverride()) {
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape && m_data.focusBack) {
            m_data.focusBack();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

}  // namespace relay
