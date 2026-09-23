// SPDX-License-Identifier: AGPL-3.0-or-later
#include "JobsTab.h"

#include "ModelRows.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QSettings>
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
        for (const char *field : {"preset", "model", "effort", "tier"})
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
         QStringLiteral("the conversation in this pane"), QStringLiteral("main"), false},
        {QStringLiteral("subagent"), QStringLiteral("subagents"),
         QStringLiteral("agents the main agent starts"), QStringLiteral("main"), true},
        {QStringLiteral("switchboard"), QStringLiteral("helper agent"),
         QStringLiteral("the switchboard, and the helper in options, actions and sessions"),
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
         QStringLiteral("a prompt carrying an image, when this pane's model cannot read one"),
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
        "its own. “runs on” is what this pane's worker says it is using right now."));
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
    m_list->setColumnWidth(ColOverride, 175);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(ColJob, QHeaderView::Interactive);
    m_list->header()->setSectionResizeMode(ColWhat, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(ColRuns, QHeaderView::Interactive);
    m_list->header()->setSectionResizeMode(ColOverride, QHeaderView::Interactive);
    m_list->installEventFilter(this);
    layout->addWidget(m_list, 1);

    m_footer = new QLabel(QStringLiteral(
        "↑↓ a job · enter picks the model it runs on · delete puts it back on its tier · esc back to the pane"));
    m_footer->setObjectName(QStringLiteral("transcriptHeader"));
    m_footer->setWordWrap(true);
    layout->addWidget(m_footer);

    connect(m_list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *, int) { openOverride(); });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) { openOverride(); });
    rebuild();
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
    if (job && !job->settable) return QStringLiteral("this pane's model");
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
            item->setText(ColOverride, overrideText(job->role));

            const QJsonObject resolved = m_data.roles.value(job->role).toObject();
            QStringList tip{job->what};
            if (runs.isEmpty())
                tip << QStringLiteral("Nothing is resolved yet: this pane's worker has not reported.");
            if (!str(resolved, "note").isEmpty()) tip << str(resolved, "note");
            if (!str(resolved, "warning").isEmpty()) tip << str(resolved, "warning");
            if (!job->settable)
                tip << QStringLiteral("Agent turns are this pane's own model by definition; pick it in the "
                                      "model box or on priorities.");
            else if (rolestore::background(job->role))
                tip << QStringLiteral("A side call into a conversation running somewhere else, so a guest "
                                      "harness (claude code, codex) cannot take it and is not offered here; "
                                      "with nothing in its tier able to serve it, it falls through to relay "
                                      "free rather than to your own model.");
            for (int column = 0; column < 4; ++column) item->setToolTip(column, tip.join(QLatin1Char('\n')));

            const bool overridden = !rolestore::overrideKey(job->role).isEmpty()
                                    || !rolestore::overrideTier(job->role).isEmpty();
            if (!job->settable || !overridden) {
                item->setForeground(ColOverride, m_list->palette().brush(QPalette::Disabled, QPalette::Text));
                continue;
            }
            // "A small × clears it." Only on a row that has something to clear.
            auto *cell = new QWidget;
            auto *box = new QHBoxLayout(cell);
            box->setContentsMargins(0, 0, 0, 0);
            box->setSpacing(4);
            box->addWidget(new QLabel(overrideText(job->role)), 1);
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
    if (rect.isValid())
        m_anchor->setGeometry(m_list->columnViewportPosition(ColOverride), rect.top(),
                              std::max(120, m_list->columnWidth(ColOverride)), rect.height());
    else
        m_anchor->setGeometry(0, 0, m_list->viewport()->width(), 1);
    m_anchor->show();
    return m_anchor;
}

bool JobsTab::openOverride() {
    const QString role = currentRole();
    const Job *job = jobFor(role);
    if (!job || !job->settable) return false;
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
    if (rolestore::overrideKey(role).isEmpty() && rolestore::overrideTier(role).isEmpty()) return false;
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
