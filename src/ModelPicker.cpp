// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelPicker.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFormLayout>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringListModel>
#include <QTabBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace relay {

using namespace models;

namespace {

// The rank column numbers a tier list and carries "+ add" on a row that is not in it yet, so the
// mouse has the affordance Ctrl+Enter is for the keyboard. On the `all` tab it is simply empty
// (and so, resized to its contents, a few pixels wide): hiding it would take the spanned section
// rows, which draw in column 0, with it.
// ColBox is the "show in box" cutoff of a tier tab (card #MDL1, design 5.3); it is hidden on the
// `all` and `lite` tabs, which the Alt+M box never draws.
// ColAvail is step 2 of the four, and belongs to the `all` tab alone (design 5.7). It sits second
// rather than first for one mechanical reason: a section rule is a `setFirstColumnSpanned` row,
// which draws out of column 0, so column 0 has to be one that is never hidden — and ColAvail is
// hidden on every tab but `all`. ColRank is empty on the `all` tab and resizes to a few pixels, so
// the tick is still the first thing on the row.
enum Column { ColRank, ColAvail, ColBox, ColModel, ColVia, ColReasoning, ColIntelligence, ColSpeed, ColLeft, ColCount };
constexpr int KeyRole = Qt::UserRole;          // the entry this row would use
constexpr int SectionRole = Qt::UserRole + 1;  // a rule, not a model
constexpr int ViaRole = Qt::UserRole + 2;      // the keys of every provider folded into this row
constexpr int ListedRole = Qt::UserRole + 3;   // this row is an entry of the tab's tier list
constexpr int AddRole = Qt::UserRole + 4;      // "not in this list": ctrl+enter puts it in
constexpr int GroupRole = Qt::UserRole + 5;    // the folded row's group id, for the via choice
constexpr int AddByIdRole = Qt::UserRole + 6;  // the `all` tab's "+ add a model by id…" row
constexpr int AvailRole = Qt::UserRole + 7;    // a model row of the `all` tab: its tick is step 2

const QString kAll = QStringLiteral("all");

QString percent(double left) { return left < 0 ? QString() : QStringLiteral("%1%").arg(qRound(left)); }

QString providerText(const Entry &entry) {
    return entry.provider + (entry.plan.isEmpty() ? QString() : QStringLiteral(" · ") + entry.plan);
}

// A local entry and a cloud one can share a name and never share a row (design 3.2), so the id a
// via choice is remembered under is the group's bucket, not its name.
QString groupId(const Group &group) {
    if (group.entries.isEmpty()) return group.name;
    return group.entries.first().local ? QStringLiteral("local\x1f") + group.name : group.name;
}

// Whether any of the five lists names this entry. `curation::inAnyList` is ModelCatalog.cpp's
// own; from out here the lists themselves are the way to ask. It decides one thing in this file:
// a listed model's availability tick is always on and says why (card #MDL1, design 5.7) — a rank
// the user wrote down that the box would not offer is a list that lies.
bool inAnyTierList(const QString &key) {
    for (const QString &tier : curation::tierIds())
        for (const curation::TierEntry &entry : curation::tierList(tier))
            if (entry.key == key) return true;
    return false;
}

// Which models a tier list may hold — the same rule Options › Models' "+ add a model…" applies, so
// the two doors offer the same models: local models in the local list and nowhere else, and a
// guest harness only where it can be a whole agent (its own pane, or a plan turn).
bool addableToTier(const Entry &entry, const QString &tier) {
    if ((tier == QStringLiteral("local")) != entry.local) return false;
    if (entry.guest && tier != QStringLiteral("main") && tier != QStringLiteral("high")) return false;
    return true;
}

// The rule the long tail appears under once something is typed. `shown()` holds back an
// open-ended provider's rows — OpenRouter's four hundred live ones — so naming the provider is
// what says why they were not there a moment ago (design 5.5).
QString tailRule(const QList<Entry> &tail) {
    QStringList providers;
    for (const Entry &entry : tail)
        if (!entry.provider.isEmpty() && !providers.contains(entry.provider)) providers << entry.provider;
    return providers.isEmpty() ? QStringLiteral("more models")
                               : QStringLiteral("more from %1").arg(providers.join(QStringLiteral(", ")));
}

// A QTreeWidget that says when a drag finished, so the tier list can be rewritten from what the
// rows now read. No signal of its own, so no Q_OBJECT and no moc for one callback.
class DragList final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void()> onDropped;

protected:
    void dropEvent(QDropEvent *event) override {
        QTreeWidget::dropEvent(event);
        if (onDropped) onDropped();
    }
};

}  // namespace

ModelPicker::ModelPicker(const Context &context, QWidget *parent) : QDialog(parent), m_context(context) {
    setWindowTitle(QStringLiteral("models"));
    setObjectName(QStringLiteral("modelPicker"));
    resize(980, 620);
    auto *layout = new QVBoxLayout(this);

    // ----- the header: which profile these lists belong to ---------------------------------------
    // Only when there is one to switch: an install with no profile has nothing to say here, and the
    // row would be a control that does nothing (owner's profiles, card #MDL1 / 2026-09-20 evening).
    const QStringList profileNames = curation::profiles();
    if (!profileNames.isEmpty()) {
        auto *header = new QHBoxLayout;
        m_profileLabel = new QLabel(QStringLiteral("profile"));
        header->addWidget(m_profileLabel);
        m_profile = new QComboBox;
        m_profile->setObjectName(QStringLiteral("modelProfile"));
        m_profile->setAccessibleName(QStringLiteral("model profile"));
        m_profile->setToolTip(QStringLiteral("A named set of these five lists. Switching one in swaps every list at once (also /profile)"));
        const QString current = curation::currentProfile();
        if (current.isEmpty()) m_profile->addItem(QStringLiteral("no profile"), QString());
        for (const QString &name : profileNames) m_profile->addItem(name, name);
        m_profile->setCurrentIndex(qMax(0, m_profile->findData(current)));
        header->addWidget(m_profile);
        header->addStretch(1);
        layout->addLayout(header);
        connect(m_profile, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            const QString name = m_profile->itemData(index).toString();
            if (name.isEmpty() || name == curation::currentProfile()) return;
            curation::applyProfile(name);
            m_viaChoice.clear();
            m_undo.clear();   // the stack held lists of the profile we just left
            rebuild();
            changed();
        });
    }

    // ----- the tabs: one per tier list, then the flat one ----------------------------------------
    m_tabs = new QTabBar;
    m_tabs->setObjectName(QStringLiteral("modelTabs"));
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(true);
    m_tabs->setFocusPolicy(Qt::StrongFocus);
    for (const QString &id : tabIds()) {
        const int index = m_tabs->addTab(id);
        m_tabs->setTabData(index, id);
        m_tabs->setTabToolTip(index, id == kAll
            ? QStringLiteral("every model, one row each — favorites, the ten most recent, and the sort menu")
            : curation::tierLabel(id) + QStringLiteral(" · rank 1 is what this tier runs on, the rest are its fallbacks"));
    }
    m_tier = m_tabs->count() ? m_tabs->tabData(0).toString() : kAll;
    layout->addWidget(m_tabs);

    auto *top = new QHBoxLayout;
    m_filter = new QLineEdit;
    m_filter->setObjectName(QStringLiteral("modelFilter"));
    m_filter->setPlaceholderText(QStringLiteral("filter every model · ↑↓ select · enter uses it · ctrl+enter adds it here"));
    m_filter->setClearButtonEnabled(true);
    top->addWidget(m_filter, 1);
    m_sortLabel = new QLabel(QStringLiteral("sort"));
    top->addWidget(m_sortLabel);
    m_sort = new QComboBox;
    m_sort->setObjectName(QStringLiteral("modelSort"));
    m_sort->setAccessibleName(QStringLiteral("sort models by"));
    for (Sort sort : allSorts()) m_sort->addItem(sortLabel(sort), sortId(sort));
    m_sort->setCurrentIndex(qMax(0, m_sort->findData(sortId(curation::sort()))));
    top->addWidget(m_sort);
    // "a class tab has a 'show this class in the box' switch" (design 5.3). Beside the filter,
    // where the tab's own settings belong; hidden on `all` and `lite`, which the box never draws.
    m_boxSwitch = new QCheckBox(QStringLiteral("show this class in the box"));
    m_boxSwitch->setObjectName(QStringLiteral("modelBoxClass"));
    m_boxSwitch->setToolTip(QStringLiteral("Whether Alt+M shows this class at all. The “in box” column says how far down it"));
    top->addWidget(m_boxSwitch);
    layout->addLayout(top);

    auto *dragList = new DragList;
    m_list = dragList;
    m_list->setObjectName(QStringLiteral("modelList"));
    m_list->setHeaderLabels({QString(), QStringLiteral("available"), QStringLiteral("in box"),
                             QStringLiteral("model"), QStringLiteral("via"),
                             QStringLiteral("reasoning"), QStringLiteral("intelligence"), QStringLiteral("tok/s"),
                             QStringLiteral("left")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAllColumnsShowFocus(true);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(ColModel, QHeaderView::Stretch);
    for (int c = 0; c < ColCount; ++c)
        if (c != ColModel) m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setDragDropOverwriteMode(false);
    m_list->setDefaultDropAction(Qt::MoveAction);
    // Tab leaves the view rather than walking its cells, because Tab is how the keyboard reaches
    // the right-hand column: ←/→ in the filter belong to the tabs, so the way to a row's
    // providers is filter → tab → list → →.
    m_list->setTabKeyNavigation(false);
    dragList->onDropped = [this] { commitDragOrder(); };

    auto *lists = new QHBoxLayout;
    lists->addWidget(m_list, 1);
    // The right-hand column: which provider this row will use (only when it folds more than one),
    // then the level, each its own pick. Enter anywhere uses the pair.
    auto *sideColumn = new QVBoxLayout;
    m_viaLabel = new QLabel(QStringLiteral("via"));
    sideColumn->addWidget(m_viaLabel);
    m_vias = new QListWidget;
    m_vias->setObjectName(QStringLiteral("modelVias"));
    m_vias->setAccessibleName(QStringLiteral("provider"));
    m_vias->setFixedWidth(210);          // "z.ai (glm) · coding plan" fits; the rest elide
    m_vias->setMaximumHeight(130);
    m_vias->setUniformItemSizes(true);
    m_vias->setTextElideMode(Qt::ElideRight);
    m_vias->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideColumn->addWidget(m_vias);
    sideColumn->addWidget(new QLabel(QStringLiteral("reasoning")));
    m_levels = new QListWidget;
    m_levels->setObjectName(QStringLiteral("modelLevels"));
    m_levels->setAccessibleName(QStringLiteral("reasoning level"));
    m_levels->setFixedWidth(210);
    // Tall enough for the longest level list anyone ships and no taller: stretched to the height
    // of the models it would be an empty well beside four or five words.
    m_levels->setMaximumHeight(240);
    m_levels->setUniformItemSizes(true);
    m_levels->setTextElideMode(Qt::ElideRight);
    m_levels->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideColumn->addWidget(m_levels);
    sideColumn->addStretch(1);
    lists->addLayout(sideColumn);
    layout->addLayout(lists, 1);

    m_limits = new QLabel;
    m_limits->setObjectName(QStringLiteral("modelLimits"));
    m_limits->setWordWrap(true);
    layout->addWidget(m_limits);

    // The footer spells the keys of the tab you are on, in the hints' voice: quiet, one line, and
    // always there — this dialog now has eight of them and none is guessable.
    m_footer = new QLabel;
    m_footer->setObjectName(QStringLiteral("modelKeys"));
    m_footer->setWordWrap(true);
    {
        QPalette quiet = m_footer->palette();
        quiet.setColor(QPalette::WindowText, palette().color(QPalette::Disabled, QPalette::Text));
        m_footer->setPalette(quiet);
    }
    layout->addWidget(m_footer);

    auto *buttons = new QHBoxLayout;
    m_favorite = new QPushButton(QStringLiteral("☆ favorite"));
    m_favorite->setObjectName(QStringLiteral("modelFavorite"));
    m_favorite->setToolTip(QStringLiteral("Pin this model at the top of the all tab"));
    buttons->addWidget(m_favorite);
    auto *customize = new QPushButton(QStringLiteral("customize…"));
    customize->setObjectName(QStringLiteral("modelCustomize"));
    customize->setToolTip(QStringLiteral("Options › Models: providers, keys, and which models these lists may hold (/models)"));
    buttons->addWidget(customize);
    // The two buttons that were Options › Models' "fill the lists" row (design 5.5: the lists are
    // edited here now, so the defaults that fill them are here too). The action is the caller's —
    // only a pane knows what its worker computed — so there is one copy of it, not two.
    if (m_context.fillFromDefaults) {
        m_defaults = new QPushButton(QStringLiteral("fill from defaults"));
        m_defaults->setObjectName(QStringLiteral("modelDefaults"));
        m_defaults->setToolTip(QStringLiteral("Replace every list with your own providers' models, best first"));
        buttons->addWidget(m_defaults);
        m_defaultsOpenrouter = new QPushButton(QStringLiteral("…with openrouter"));
        m_defaultsOpenrouter->setObjectName(QStringLiteral("modelDefaultsOpenrouter"));
        m_defaultsOpenrouter->setToolTip(QStringLiteral("The same, with each model's cheaper OpenRouter twin behind it, and OpenRouter leading lite (recommended)"));
        buttons->addWidget(m_defaultsOpenrouter);
        const auto fill = [this](bool withOpenrouter) {
            for (const QString &tier : curation::tierIds()) pushUndo(tier);
            if (!m_context.fillFromDefaults(withOpenrouter)) {
                m_limits->setText(QStringLiteral("No defaults yet — open a pane's agent first, so its worker can compute them."));
                for (int i = 0; i < curation::tierIds().size(); ++i) m_undo.removeLast();
                return;
            }
            m_viaChoice.clear();
            rebuild();
            changed();
        };
        connect(m_defaults, &QPushButton::clicked, this, [fill] { fill(false); });
        connect(m_defaultsOpenrouter, &QPushButton::clicked, this, [fill] { fill(true); });
    }
    buttons->addStretch(1);
    m_use = new QPushButton(QStringLiteral("use"));
    m_use->setDefault(true);
    buttons->addWidget(m_use);
    auto *cancel = new QPushButton(QStringLiteral("cancel"));
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        const QString id = m_tabs->tabData(index).toString();
        if (id.isEmpty() || id == m_tier) return;
        m_tier = id;
        m_filter->clear();   // a filter typed for one list means nothing in the next
        rebuild();
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_sort, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        curation::setSort(sortFromId(m_sort->itemData(index).toString()));
        rebuild();
    });
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this] { onRowChanged(); });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) { onCheckChanged(item, column); });
    connect(m_boxSwitch, &QCheckBox::toggled, this, [this](bool on) {
        if (m_building || !boxClassTab() || on == curation::boxShown(m_tier)) return;
        curation::setBoxShown(m_tier, on);
        refreshBoxChecks();
        changed();
    });
    // A click only highlights (owner, 2026-09-20: "don't pick immediately on click … so you can
    // pick effort as well"). Enter, the "use" button or a double click commit the pair. The two
    // cells that *are* controls — "+ add" and "via" — are the exception, and they add or open
    // rather than commit anything.
    connect(m_list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int column) {
        if (!item || item->data(0, SectionRole).toBool()) return;
        if (column == ColRank && item->data(0, AddRole).toBool()) { addSelected(); return; }
        if (column == ColVia && item->data(0, ViaRole).toStringList().size() > 1 && m_vias->isVisible()) m_vias->setFocus();
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (!item || item->data(0, SectionRole).toBool()) return;
        if (item->data(0, AddRole).toBool()) { addSelected(); return; }   // a double click on a "+ add" row adds it
        accept();
    });
    m_list->installEventFilter(this);
    connect(m_favorite, &QPushButton::clicked, this, [this] {
        const QString key = selectedKey();
        if (key.isEmpty()) return;
        curation::toggleFavorite(key);
        rebuild();
        selectKey(key);
    });
    connect(customize, &QPushButton::clicked, this, [this] {
        reject();
        if (openModelsPage) openModelsPage();
    });
    connect(m_vias, &QListWidget::currentRowChanged, this, [this](int) { onViaChanged(); });
    connect(m_levels, &QListWidget::currentRowChanged, this, [this](int) { onLevelChanged(); });
    connect(m_levels, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { accept(); });
    m_vias->installEventFilter(this);
    m_levels->installEventFilter(this);
    m_tabs->installEventFilter(this);
    connect(m_use, &QPushButton::clicked, this, [this] { accept(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    // Typing goes to the filter; arrows move the list even while the filter has the focus, so Tab
    // is what actually moves the focus along the four controls, in the order they are read.
    m_filter->installEventFilter(this);
    setTabOrder(m_filter, m_list);
    setTabOrder(m_list, m_vias);
    setTabOrder(m_vias, m_levels);
    setTabOrder(m_levels, m_favorite);
    const QStringList ids = tabIds();
    m_tier = ids.contains(m_context.tier) ? m_context.tier
           : ids.contains(QStringLiteral("main")) ? QStringLiteral("main") : ids.value(0, kAll);
    syncTabBar();
    // What the caller asked to be typed (Options › Models' per-provider "models…" link opens the
    // `all` tab filtered to that provider). Set before the first rebuild so the dialog never draws
    // the unfiltered list first and then replaces it.
    if (!m_context.filter.isEmpty()) { const QSignalBlocker block(m_filter); m_filter->setText(m_context.filter); }
    rebuild();
    if (!m_context.currentKey.isEmpty()) selectKey(m_context.currentKey);
    if (!m_list->currentItem()) selectFirstRow();
    m_filter->setFocus();
}

qint64 ModelPicker::nowSeconds() const {
    return m_context.now > 0 ? m_context.now : QDateTime::currentSecsSinceEpoch();
}

QStringList ModelPicker::tabIds() const {
    // The design's order (§5.2), off `curation::tierIds()` so a tier added there gets a tab: high
    // leads — it is the one you reach for deliberately — then the page's order, then the flat tab.
    QStringList ids;
    const QStringList tiers = curation::tierIds();
    if (tiers.contains(QStringLiteral("high"))) ids << QStringLiteral("high");
    for (const QString &tier : tiers) {
        if (tier == QStringLiteral("high")) continue;
        // Local is a tab only where this machine has something to serve, or the list already
        // names something: an empty tab for a feature nobody here uses is noise.
        if (tier == QStringLiteral("local")) {
            bool any = !curation::tierList(tier).isEmpty();
            for (const Entry &entry : m_context.catalog.entries) any = any || entry.local;
            if (!any) continue;
        }
        ids << tier;
    }
    ids << kAll;
    return ids;
}

void ModelPicker::syncTabBar() {
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabData(i).toString() == m_tier) {
            if (m_tabs->currentIndex() != i) { QSignalBlocker block(m_tabs); m_tabs->setCurrentIndex(i); }
            return;
        }
}

void ModelPicker::setTier(const QString &tier) {
    if (tier == m_tier || !tabIds().contains(tier)) return;
    m_tier = tier;
    syncTabBar();
    m_filter->clear();
    rebuild();
}

void ModelPicker::stepTab(int delta) {
    const QStringList ids = tabIds();
    const int at = ids.indexOf(m_tier);
    if (at < 0 || ids.size() < 2) return;
    // Wrapping: with six tabs and ←/→ as the way through them, stopping dead at either end reads
    // as a broken key rather than as an edge.
    setTier(ids.at((at + delta % ids.size() + ids.size()) % ids.size()));
}

// ----- drawing -----------------------------------------------------------------------------------

void ModelPicker::addSection(const QString &title) {
    // The title goes in column 0 because that is the column a spanned row draws.
    auto *item = new QTreeWidgetItem(m_list, QStringList{title});
    item->setData(0, SectionRole, true);
    item->setFlags(Qt::ItemIsEnabled);   // not selectable, not a drop target
    item->setFirstColumnSpanned(true);
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
}

QString ModelPicker::effectiveEffort(const Entry &entry) const {
    if (entry.efforts.isEmpty()) return QString();
    // A level stored in a list, or carried by the pane, may be one this model does not take —
    // `xhigh` from a codex row on a model whose provider stops at `max` (card #MDL1, 2026-09-21).
    // It snaps to the model's nearest rather than being dropped, so the row shows the level this
    // pick would really run at.
    const QString listed = curation::listEffortFor(entry.key);
    if (!listed.isEmpty()) return nearestEffort(entry.efforts, listed);
    if (!m_context.currentEffort.isEmpty()) return nearestEffort(entry.efforts, m_context.currentEffort);
    return entry.efforts.last();
}

QTreeWidgetItem *ModelPicker::addListRow(int rank, const curation::TierEntry &item, const Entry *entry) {
    const qint64 now = nowSeconds();
    const qint64 until = entry ? exhaustedUntil(m_context.catalog, entry->preset, now) : -1;
    // Unusable and exhausted rows are greyed *in place*, with the reason where the figure goes —
    // never hidden. A list is an order the user wrote down; silently dropping a rank from it is
    // how "which model is selected first" stopped being answerable (design 1.3).
    const bool dead = !entry || !entry->usable || until >= 0;
    QString name = entry ? entry->name : item.key;
    if (curation::isFavorite(item.key)) name.prepend(QStringLiteral("★ "));
    if (rank == 1 && m_tier == QStringLiteral("main")) name += QStringLiteral("   · new panes start here");
    QString left;
    if (!entry) left = QStringLiteral("unavailable");
    else if (!entry->usable) left = QStringLiteral("no key");
    else if (until >= 0) left = QStringLiteral("0%") + (until > 0 ? QStringLiteral(" · resets ") + resetText(until, now) : QString());
    else left = percent(percentLeft(m_context.catalog, entry->preset));
    QString level;
    if (entry && !entry->efforts.isEmpty())
        level = item.effort.isEmpty() ? QStringLiteral("default") : nearestEffort(entry->efforts, item.effort);
    const double speed = curation::speed(item.key);
    auto *row = new QTreeWidgetItem(m_list, QStringList{
        QString::number(rank), QString(), QString(), name, entry ? providerText(*entry) : QString(), level,
        entry && entry->intelligence >= 0 ? QString::number(entry->intelligence) : QString(),
        speed > 0 ? QString::number(qRound(speed)) : QString(), left});
    row->setData(0, KeyRole, item.key);
    row->setData(0, ListedRole, true);
    row->setData(0, ViaRole, QStringList{item.key});
    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled
                  | (boxClassTab() ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
    // The cutoff, as a column of checkboxes: rank 1..cutoff checked, the rest not, and clicking
    // one moves the cutoff to it (design 5.3, `setBoxCutoffFromRow`).
    if (boxClassTab())
        row->setCheckState(ColBox, curation::boxShown(m_tier) && rank <= curation::boxCutoff(m_tier)
                                       ? Qt::Checked : Qt::Unchecked);
    row->setToolTip(0, !entry ? QStringLiteral("%1 is not in the catalog right now: its provider has no key, or it left the listing")
                                    .arg(item.key)
                   : !entry->usable ? QStringLiteral("No key for %1 · skipped until you add one").arg(entry->provider)
                   : until >= 0 ? QStringLiteral("Exhausted · skipped until it resets")
                   : rank == 1 && m_tier == QStringLiteral("main")
                       ? QStringLiteral("%1 · rank 1 of main: what a new pane and /swap run on").arg(entry->model)
                       : QStringLiteral("%1 · fallback %2 of the %3 list").arg(entry->model).arg(rank - 1).arg(m_tier));
    for (int c = 0; c < ColCount; ++c) row->setToolTip(c, row->toolTip(0));
    row->setTextAlignment(ColRank, Qt::AlignRight | Qt::AlignVCenter);
    for (int c = ColReasoning; c < ColCount; ++c) row->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    if (dead)
        for (int c = 0; c < ColCount; ++c) row->setForeground(c, palette().color(QPalette::Disabled, QPalette::Text));
    if (item.key == m_context.currentKey) {
        QFont font = row->font(ColModel);
        font.setBold(true);
        for (int c = 0; c < ColCount; ++c) row->setFont(c, font);
        row->setText(ColModel, row->text(ColModel) + QStringLiteral("  · current"));
    }
    return row;
}

QTreeWidgetItem *ModelPicker::addGroupRow(const Group &group, bool addable) {
    const qint64 now = nowSeconds();
    Entry entry = group.preferred(m_context.catalog, now);
    QStringList vias;
    for (const Entry &each : group.entries) vias << each.key;
    // A provider chosen by hand for this row wins over the preferred one until the dialog closes.
    if (const QString chosen = m_viaChoice.value(groupId(group)); !chosen.isEmpty() && vias.contains(chosen))
        for (const Entry &each : group.entries) if (each.key == chosen) entry = each;
    if (entry.key.isEmpty()) return nullptr;
    QString name = entry.name.isEmpty() ? group.name : entry.name;
    bool favorite = false;
    for (const Entry &each : group.entries) favorite = favorite || curation::isFavorite(each.key);
    if (favorite) name.prepend(QStringLiteral("★ "));
    QString via = providerText(entry);
    if (group.entries.size() > 1) via += QStringLiteral("  +%1").arg(group.entries.size() - 1);
    const QString level = effectiveEffort(entry);
    const double speed = curation::speed(entry.key);
    const qint64 until = exhaustedUntil(m_context.catalog, entry.preset, now);
    QString leftText = percent(percentLeft(m_context.catalog, entry.preset));
    if (until >= 0) leftText = QStringLiteral("0%") + (until > 0 ? QStringLiteral(" · resets ") + resetText(until, now) : QString());
    auto *row = new QTreeWidgetItem(m_list, QStringList{
        addable ? QStringLiteral("+ add") : QString(), QString(), QString(), name, via,
        level,
        entry.intelligence >= 0 ? QString::number(entry.intelligence) : QString(),
        speed > 0 ? QString::number(qRound(speed)) : QString(), leftText});
    row->setData(0, KeyRole, entry.key);
    row->setData(0, ViaRole, vias);
    row->setData(0, GroupRole, groupId(group));
    if (addable) row->setData(0, AddRole, true);
    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    QStringList others;
    for (const Entry &each : group.entries) if (each.key != entry.key) others << providerText(each);
    row->setToolTip(0, entry.model + (entry.custom ? QStringLiteral(" (added by you)") : QString())
                    + (others.isEmpty() ? QString() : QStringLiteral(" · also on ") + others.join(QStringLiteral(", ")) + QStringLiteral(" (→ chooses)"))
                    + (group.spent(m_context.catalog, now) ? QStringLiteral(" · every provider of it is spent") : QString())
                    + (addable ? QStringLiteral(" · ctrl+enter adds it to the %1 list").arg(m_tier) : QString()));
    for (int c = 0; c < ColCount; ++c) row->setToolTip(c, row->toolTip(0));
    row->setTextAlignment(ColRank, Qt::AlignRight | Qt::AlignVCenter);
    for (int c = ColReasoning; c < ColCount; ++c) row->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    // Greyed only when every provider in the row is spent (rule 2): a subscription running out
    // moves the row to the next provider, it does not take the model away.
    if (group.spent(m_context.catalog, now))
        for (int c = 0; c < ColCount; ++c) row->setForeground(c, palette().color(QPalette::Disabled, QPalette::Text));
    // Step 2 (design 5.7): the `all` tab's tick, and the row greyed while it is empty. It covers
    // every provider of the row, because a row is one model — un-ticking sonnet un-ticks sonnet.
    if (availabilityTab()) {
        row->setData(0, AvailRole, true);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        bool available = false;
        QString listed;
        for (const Entry &each : group.entries) {
            available = available || curation::isAvailable(each);
            if (inAnyTierList(each.key)) listed = each.key;
        }
        applyAvailability(row, available, listed);
    }
    if (vias.contains(m_context.currentKey)) {
        QFont font = row->font(ColModel);
        font.setBold(true);
        for (int c = 0; c < ColCount; ++c) row->setFont(c, font);
        row->setText(ColModel, row->text(ColModel) + QStringLiteral("  · current"));
    }
    return row;
}

void ModelPicker::buildTier(const QString &query) {
    const QList<curation::TierEntry> list = curation::tierList(m_tier);
    QStringList inList;
    for (const curation::TierEntry &item : list) inList << item.key;
    int rank = 0, drawn = 0;
    for (const curation::TierEntry &item : list) {
        ++rank;
        const Entry *entry = m_context.catalog.find(item.key);
        if (!query.isEmpty() && !(entry ? matches(*entry, query) : item.key.contains(query, Qt::CaseInsensitive))) continue;
        addListRow(rank, item, entry);
        ++drawn;
    }
    if (query.isEmpty()) {
        addSection(list.isEmpty()
                       ? QStringLiteral("this list is empty — type a model's name, then ctrl+enter adds it")
                       : QStringLiteral("type a model's name to add it to this list"));
        return;
    }
    // Typing searches every model, not only this list: the rows of this list that match come
    // first, then the rest of the catalog under a rule, folded one row per model. "The rest" is
    // every *usable* entry, so the long tail `shown()` holds back is reachable here — this is the
    // door that replaced the per-provider id box on Options › Models (design 5.5) — and it comes
    // under its own rule so it is clear why those rows were not listed until you typed.
    QStringList listed;
    for (const Entry &entry : shown(m_context.catalog)) listed << entry.key;
    QList<Entry> rest, tail;
    for (const Entry &entry : allUsable(m_context.catalog)) {
        if (inList.contains(entry.key) || !addableToTier(entry, m_tier) || !matches(entry, query)) continue;
        (listed.contains(entry.key) ? rest : tail) << entry;
    }
    if (rest.isEmpty() && tail.isEmpty()) {
        if (drawn == 0) addSection(QStringLiteral("no model matches “%1”").arg(query));
        return;
    }
    if (!rest.isEmpty()) {
        addSection(QStringLiteral("not in this list"));
        for (const Group &group : grouped(m_context.catalog, rest, nowSeconds())) addGroupRow(group, true);
    }
    if (!tail.isEmpty()) {
        addSection(tailRule(tail));
        for (const Group &group : grouped(m_context.catalog, tail, nowSeconds())) addGroupRow(group, true);
    }
}

void ModelPicker::buildAll(const QString &query) {
    const Sort sort = sortFromId(m_sort->currentData().toString());
    // `curatable`, not `shown`: this tab is where step 2 is *edited*, so a model you un-ticked is
    // still drawn — greyed, with an empty box to tick again (design 5.7). What is not here is an
    // open-ended provider's long tail, which is behind typing as it has always been.
    const QList<Entry> listed = curatable(m_context.catalog);
    const QList<Entry> rows = ordered(listed, sort, m_context.catalog);
    const QList<Group> groups = grouped(m_context.catalog, rows, nowSeconds());
    if (!query.isEmpty() || sort != Sort::Priority) {
        for (const Group &group : groups) {
            bool hit = false;
            for (const Entry &entry : group.entries) hit = hit || query.isEmpty() || matches(entry, query);
            if (hit) addGroupRow(group, false);
        }
        // Typing reaches the one part of the catalog this tab holds back: an open-ended provider's
        // long tail, which would otherwise be the whole list. Untyped it stays out of the way;
        // typed it is right here, under its own rule, with the same availability tick — ticking
        // one is "select specific models" for OpenRouter (owner, 2026-09-21).
        if (!query.isEmpty()) {
            QStringList have;
            for (const Entry &entry : listed) have << entry.key;
            QList<Entry> tail;
            for (const Entry &entry : allUsable(m_context.catalog))
                if (!have.contains(entry.key) && matches(entry, query)) tail << entry;
            if (!tail.isEmpty()) {
                addSection(tailRule(tail));
                for (const Group &group : grouped(m_context.catalog, tail, nowSeconds())) addGroupRow(group, false);
            }
        }
        if (m_list->topLevelItemCount() == 0) addSection(QStringLiteral("no model matches “%1”").arg(query));
        addAddByIdRow();
        return;
    }
    // Sections, opencode's way: favorites, then the ten most recent, then everything by rank. A
    // group is a favorite when any of its entries is, and recent when any of them is (edge case
    // 10) — a model is one row wherever it sits.
    auto groupOf = [&groups](const QString &key) -> const Group * {
        for (const Group &group : groups)
            for (const Entry &entry : group.entries)
                if (entry.key == key) return &group;
        return nullptr;
    };
    QList<const Group *> placed, favorites, recent;
    for (const QString &key : curation::favorites())
        if (const Group *group = groupOf(key); group && !placed.contains(group)) { favorites << group; placed << group; }
    for (const QString &key : curation::recent())
        if (const Group *group = groupOf(key); group && !placed.contains(group)) { recent << group; placed << group; }
    if (!favorites.isEmpty()) { addSection(QStringLiteral("favorites")); for (const Group *group : favorites) addGroupRow(*group, false); }
    if (!recent.isEmpty()) { addSection(QStringLiteral("recent")); for (const Group *group : recent) addGroupRow(*group, false); }
    // The rest **by provider**, one rule per provider name (owner, 2026-09-21: the availability
    // column is "grouped by provider … with the provider's name as a rule"). Step 2 is a statement
    // about one provider's models at a time — "i probably want to uncheck sonnet and haiku and gpt
    // 5.5" — and reading it needs the provider's models together, which a flat rank order never
    // puts them in. The providers come in the order their first model does, so the rank order the
    // page was showing is still the order you read down. Under any other sort, and while something
    // is typed, the list stays flat above: the sort *is* the order then.
    QStringList providers;
    QHash<QString, QList<const Group *>> byProvider;
    for (const Group &group : groups) {
        if (placed.contains(&group)) continue;
        const Entry entry = group.preferred(m_context.catalog, nowSeconds());
        const QString provider = entry.provider.isEmpty() ? entry.preset : entry.provider;
        if (!providers.contains(provider)) providers << provider;
        byProvider[provider] << &group;
    }
    for (const QString &provider : providers) {
        addSection(provider);
        for (const Group *group : byProvider.value(provider)) addGroupRow(*group, false);
    }
    addAddByIdRow();
}

// The last row of the `all` tab: the "add a model by id" box that used to sit under every
// open-ended provider on Options › Models (design 5.5). Almost everything it was for is done by
// typing now — the filter reaches the whole catalog — so what is left is the case it was really
// for: an id the provider serves but does not list. Enter on the row asks for it.
void ModelPicker::addAddByIdRow() {
    // Column 0, spanned, like a rule — but selectable, because it is a row you press.
    auto *row = new QTreeWidgetItem(m_list, QStringList{QStringLiteral("+ add a model by id…")});
    row->setData(0, AddByIdRole, true);
    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    row->setFirstColumnSpanned(true);
    const QString tip = QStringLiteral("A model id your provider serves but does not list. Typing in the filter already "
                                       "finds every model it does list, this tab's long tail included");
    for (int c = 0; c < ColCount; ++c) row->setToolTip(c, tip);
    row->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
}

QString ModelPicker::addModelById(const QString &preset, const QString &id) {
    const QString model = id.trimmed();
    if (preset.isEmpty() || model.isEmpty()) return QString();
    const QString key = Catalog::keyFor(preset, model);
    // An id the provider already lists needs nothing stored: every usable entry is offered, and
    // the filter reaches the tail. One it does not list is remembered in `models/custom` and is an
    // entry like any other from then on — including in this dialog, whose catalog is a copy taken
    // when it opened, so the new row is added to it rather than waited for.
    if (!m_context.catalog.find(key)) m_context.catalog.entries << curation::addCustom(preset, model, m_context.catalog);
    m_filter->setText(model);
    rebuild();
    selectKey(key);
    changed();
    return key;
}

void ModelPicker::promptAddModelById() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("add a model by id"));
    auto *form = new QFormLayout(&dialog);
    auto *provider = new QComboBox;
    provider->setObjectName(QStringLiteral("addByIdProvider"));
    for (const QString &preset : m_context.catalog.presets()) {
        const QList<Entry> rows = m_context.catalog.ofPreset(preset);
        if (rows.isEmpty() || !rows.first().usable) continue;
        provider->addItem(providerText(rows.first()), preset);
    }
    if (provider->count() == 0) return;
    // The provider of the row you were on, where there was one: the likeliest answer.
    if (const Entry *entry = m_context.catalog.find(selectedKey()); entry != nullptr)
        provider->setCurrentIndex(qMax(0, provider->findData(entry->preset)));
    auto *id = new QLineEdit;
    id->setObjectName(QStringLiteral("addByIdModel"));
    id->setPlaceholderText(QStringLiteral("model id"));
    // The provider's own ids complete it, so the common case is a few keystrokes and the rare one
    // — an id it serves but does not list — is still typeable in full.
    auto *completer = new QCompleter(&dialog);
    auto *model = new QStringListModel(completer);
    completer->setModel(model);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    id->setCompleter(completer);
    const auto refill = [this, provider, model] {
        QStringList ids;
        for (const Entry &entry : m_context.catalog.ofPreset(provider->currentData().toString())) ids << entry.model;
        model->setStringList(ids);
    };
    refill();
    QObject::connect(provider, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [refill](int) { refill(); });
    form->addRow(QStringLiteral("provider"), provider);
    form->addRow(QStringLiteral("model id"), id);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    id->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;
    addModelById(provider->currentData().toString(), id->text());
}

bool ModelPicker::boxClassTab() const {
    return m_tier != kAll && curation::boxClasses().contains(m_tier);
}

void ModelPicker::syncClassSwitch() {
    if (!m_boxSwitch) return;
    m_boxSwitch->setVisible(boxClassTab());
    const QSignalBlocker block(m_boxSwitch);
    m_boxSwitch->setChecked(!boxClassTab() || curation::boxShown(m_tier));
}

void ModelPicker::setBoxCutoffFromRow(int rank, bool on) {
    if (!boxClassTab() || rank < 1) return;
    // Checking rank n checks 1..n; unchecking n unchecks n and everything below it. Unchecking
    // rank 1 leaves nothing to show, which is the class switched off — the same statement said
    // from the other control, so the two can never disagree.
    if (on) {
        curation::setBoxCutoff(m_tier, rank);
        curation::setBoxShown(m_tier, true);
    } else if (rank == 1) {
        curation::setBoxShown(m_tier, false);
    } else {
        curation::setBoxCutoff(m_tier, rank - 1);
    }
    // The ticks are moved in place rather than by a rebuild: this runs inside the itemChanged of
    // the row that was clicked, and clearing the list under it deletes the item mid-signal.
    refreshBoxChecks();
    changed();
}

void ModelPicker::refreshBoxChecks() {
    if (m_list == nullptr) return;
    const bool wasBuilding = m_building;
    m_building = true;   // our own setCheckState calls are not clicks
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *row = m_list->topLevelItem(i);
        if (!row->data(0, ListedRole).toBool()) continue;
        const int rank = row->text(ColRank).toInt();
        row->setCheckState(ColBox, boxClassTab() && curation::boxShown(m_tier)
                                           && rank >= 1 && rank <= curation::boxCutoff(m_tier)
                                       ? Qt::Checked : Qt::Unchecked);
    }
    m_building = wasBuilding;
    syncClassSwitch();
}

void ModelPicker::onCheckChanged(QTreeWidgetItem *item, int column) {
    if (m_building || item == nullptr) return;
    if (column == ColAvail && availabilityTab() && item->data(0, AvailRole).toBool()) {
        setRowAvailable(item->data(0, KeyRole).toString(), item->checkState(ColAvail) == Qt::Checked);
        return;
    }
    if (column != ColBox || !boxClassTab()) return;
    if (!item->data(0, ListedRole).toBool()) return;
    setBoxCutoffFromRow(item->text(ColRank).toInt(), item->checkState(ColBox) == Qt::Checked);
}

// ----- step 2: available (owner, 2026-09-21; design 5.7) --------------------------------------

bool ModelPicker::availabilityTab() const { return m_tier == kAll; }

void ModelPicker::setRowAvailable(const QString &groupKey, bool on) {
    if (groupKey.isEmpty()) return;
    // Every provider of the row, because a row is one model (rule 2). Which row is which is read
    // off the drawn rows rather than re-folded: `ViaRole` already holds exactly those keys.
    QStringList keys{groupKey};
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->data(0, KeyRole).toString() == groupKey) {
            const QStringList vias = m_list->topLevelItem(i)->data(0, ViaRole).toStringList();
            if (!vias.isEmpty()) keys = vias;
            break;
        }
    for (const QString &key : std::as_const(keys)) curation::setAvailable(key, on, m_context.catalog);
    refreshAvailability();
    changed();
}

// The tick and the greying again, in place. Nothing moves: an un-ticked row stays in this tab so
// it can be ticked back (`curatable`), and rebuilding from inside the itemChanged that delivered
// the click would delete the very item that is being clicked.
void ModelPicker::refreshAvailability() {
    if (m_list == nullptr || !availabilityTab()) return;
    const bool wasBuilding = m_building;
    m_building = true;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *row = m_list->topLevelItem(i);
        if (!row->data(0, AvailRole).toBool()) continue;
        QStringList keys = row->data(0, ViaRole).toStringList();
        if (keys.isEmpty()) keys << row->data(0, KeyRole).toString();
        bool available = false;
        QString listed;
        for (const QString &key : std::as_const(keys)) {
            const Entry *entry = m_context.catalog.find(key);
            available = available || (entry != nullptr && curation::isAvailable(*entry));
            if (inAnyTierList(key)) listed = key;
        }
        applyAvailability(row, available, listed);
    }
    m_building = wasBuilding;
}

// One row's tick, its ink and its tooltip. `reason` is the key of a tier list entry, when one of
// this row's providers is named by a list: the tick is then on whatever the setting says, because
// a rank the user wrote down that the box would not offer is a list that lies — and the tooltip
// is where that is said, since a checkbox cannot say it.
void ModelPicker::applyAvailability(QTreeWidgetItem *row, bool available, const QString &reason) {
    row->setCheckState(ColAvail, available ? Qt::Checked : Qt::Unchecked);
    const QString tip = !reason.isEmpty()
        ? QStringLiteral("Available: one of your lists names it, which keeps it available whatever this box says")
        : available
            ? QStringLiteral("Available: this model is in the lists, the alt+m box and its filter")
            : QStringLiteral("Not available: it is in no list, not in the box, and not in the box's filter. "
                             "Typing its name in this dialog still reaches it");
    row->setToolTip(ColAvail, tip);
    if (!available)
        for (int c = 0; c < ColCount; ++c) row->setForeground(c, palette().color(QPalette::Disabled, QPalette::Text));
}

void ModelPicker::rebuild() {
    const QString keep = selectedKey();
    const bool all = m_tier == kAll;
    m_building = true;
    m_list->clear();
    const QString query = m_filter->text().trimmed();
    m_sortLabel->setVisible(all);
    m_sort->setVisible(all);
    syncClassSwitch();
    m_list->setColumnHidden(ColBox, !boxClassTab());
    // Step 2 is edited in one place: the `all` tab. A tier tab is step 3 and the box column is
    // step 4, and three checkbox columns on one row would say nothing.
    m_list->setColumnHidden(ColAvail, !availabilityTab());
    // Dragging is how a list is reordered; on the flat tab there is no order to write down.
    m_list->setDragDropMode(all ? QAbstractItemView::NoDragDrop : QAbstractItemView::InternalMove);
    if (all) buildAll(query); else buildTier(query);
    m_building = false;
    if (!keep.isEmpty()) selectKey(keep);
    // A tab that does not hold the row you were on opens on the pane's own model where it has it —
    // the one row you are most likely to want — and on rank 1 otherwise.
    if (!currentRow()) selectKey(m_context.currentKey);
    if (!currentRow()) selectFirstRow();
    updateFooter();
    onRowChanged();
}

void ModelPicker::selectFirstRow() {
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (!m_list->topLevelItem(i)->data(0, SectionRole).toBool()) { m_list->setCurrentItem(m_list->topLevelItem(i)); return; }
}

QTreeWidgetItem *ModelPicker::currentRow() const {
    QTreeWidgetItem *item = m_list->currentItem();
    return item && !item->data(0, SectionRole).toBool() ? item : nullptr;
}

QString ModelPicker::selectedKey() const {
    QTreeWidgetItem *item = currentRow();
    return item ? item->data(0, KeyRole).toString() : QString();
}

QString ModelPicker::selectedEffort() const {
    QListWidgetItem *item = m_levels ? m_levels->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void ModelPicker::selectKey(const QString &key) {
    if (key.isEmpty()) return;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (item->data(0, SectionRole).toBool()) continue;
        if (item->data(0, KeyRole).toString() == key || item->data(0, ViaRole).toStringList().contains(key)) {
            m_list->setCurrentItem(item);
            m_list->scrollToItem(item);
            return;
        }
    }
}

void ModelPicker::onRowChanged() {
    QTreeWidgetItem *row = currentRow();
    const QString key = selectedKey();
    const Entry *entry = key.isEmpty() ? nullptr : m_context.catalog.find(key);
    m_use->setEnabled(entry != nullptr);
    m_favorite->setEnabled(entry != nullptr);
    m_favorite->setText(entry && curation::isFavorite(key) ? QStringLiteral("★ unfavorite") : QStringLiteral("☆ favorite"));
    m_filling = true;
    m_vias->clear();
    const QStringList vias = row ? row->data(0, ViaRole).toStringList() : QStringList();
    const bool several = vias.size() > 1;
    m_vias->setVisible(several);
    m_viaLabel->setVisible(several);
    if (several)
        for (const QString &each : vias) {
            const Entry *candidate = m_context.catalog.find(each);
            if (!candidate) continue;
            auto *item = new QListWidgetItem(providerText(*candidate), m_vias);
            item->setData(Qt::UserRole, each);
            item->setToolTip(providerText(*candidate) + QStringLiteral(" · ") + candidate->model);
            if (!candidate->usable || exhausted(m_context.catalog, candidate->preset, nowSeconds()))
                item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            if (each == key) m_vias->setCurrentItem(item);
        }
    m_levels->clear();
    if (!entry) { m_filling = false; m_limits->clear(); return; }
    if (entry->effortFixed || entry->efforts.isEmpty()) {
        // A model with no knob, or Relay Free, where the gateway picks the level for the role
        // whatever anyone asks for (owner, 2026-09-21). The list is empty and says which it is,
        // in the same sentence the pane's greyed box puts in its tooltip.
        const QString why = entry->effortFixedReason().isEmpty()
                                ? QStringLiteral("%1 has no reasoning level").arg(entry->name.isEmpty() ? entry->model : entry->name)
                                : entry->effortFixedReason();
        auto *none = new QListWidgetItem(why, m_levels);
        none->setFlags(Qt::NoItemFlags);
        none->setToolTip(why);
    } else {
        // On a row that is in this tab's list, the level list is the level the *entry carries in
        // the list* (TierEntry::effort), "default" included — picking one writes it there. On the
        // flat tab it is the level this pick would run at and nothing more.
        const bool listed = row && row->data(0, ListedRole).toBool() && m_tier != kAll;
        QString stored;
        if (listed) {
            for (const curation::TierEntry &item : curation::tierList(m_tier))
                if (item.key == key) stored = item.effort;
            auto *fallback = new QListWidgetItem(QStringLiteral("default"), m_levels);
            fallback->setData(Qt::UserRole, QString());
            fallback->setToolTip(QStringLiteral("The model's own default level — what it runs at when this list names none"));
            if (stored.isEmpty()) m_levels->setCurrentItem(fallback);
        }
        // The model's own levels, in the provider's order and the provider's words — `xhigh` and
        // `ultra` on a codex row, three on kimi (card #MDL1, 2026-09-21). There is no Relay level
        // behind them any more and so nothing to label.
        const QString chosen = listed ? stored : effectiveEffort(*entry);
        for (const QString &level : entry->efforts) {
            auto *item = new QListWidgetItem(level, m_levels);
            item->setData(Qt::UserRole, level);
            if (level == chosen) m_levels->setCurrentItem(item);
        }
        // A stored level this model does not take snaps to its nearest (`xhigh` → `max`), so the
        // preselected row is the one the pick would run at. A listed row with no level at all
        // stays on "default", which is exactly what it will run at.
        if (!m_levels->currentItem() && !chosen.isEmpty()) {
            const QString snapped = nearestEffort(entry->efforts, chosen);
            for (int i = 0; i < m_levels->count(); ++i)
                if (m_levels->item(i)->data(Qt::UserRole).toString() == snapped) m_levels->setCurrentRow(i);
        }
        if (!m_levels->currentItem() && m_levels->count()) m_levels->setCurrentRow(listed ? 0 : m_levels->count() - 1);
    }
    m_filling = false;
    const QString limits = limitsText(m_context.catalog.limits.value(entry->preset), nowSeconds());
    m_limits->setText(limits.isEmpty() ? QString() : entry->provider + QStringLiteral(": ") + limits);
}

void ModelPicker::onViaChanged() {
    if (m_filling) return;
    QTreeWidgetItem *row = currentRow();
    QListWidgetItem *item = m_vias->currentItem();
    if (!row || !item) return;
    const QString key = item->data(Qt::UserRole).toString();
    const Entry *entry = m_context.catalog.find(key);
    if (!entry || key == row->data(0, KeyRole).toString()) return;
    row->setData(0, KeyRole, key);
    const QString group = row->data(0, GroupRole).toString();
    if (!group.isEmpty()) m_viaChoice.insert(group, key);
    QString via = providerText(*entry);
    const int others = row->data(0, ViaRole).toStringList().size() - 1;
    if (others > 0) via += QStringLiteral("  +%1").arg(others);
    row->setText(ColVia, via);
    // The levels are the chosen entry's, in its own words (edge case 8: codex says xhigh where
    // others say max), so they are redrawn whenever "via" moves.
    const bool hadFocus = m_vias->hasFocus();
    onRowChanged();
    if (hadFocus) m_vias->setFocus();
}

void ModelPicker::onLevelChanged() {
    if (m_filling) return;
    QTreeWidgetItem *row = currentRow();
    if (!row || m_tier == kAll || !row->data(0, ListedRole).toBool()) return;
    const QString key = row->data(0, KeyRole).toString();
    const QString level = selectedEffort();
    bool moved = false;
    for (const curation::TierEntry &item : curation::tierList(m_tier))
        if (item.key == key) moved = moved || item.effort != level;
    if (!moved) return;   // nothing changed, or the key is no longer in the list
    pushUndo(m_tier);
    curation::setTierEffort(m_tier, key, level);
    // The word the list now holds is the provider's own, so it is what the column prints.
    row->setText(ColReasoning, level.isEmpty() ? QStringLiteral("default") : level);
    changed();
}

void ModelPicker::updateFooter() {
    m_footer->setText(m_tier == kAll
        ? QStringLiteral("←→ tab · ↑↓ row · enter uses it · type to search every model, openrouter's long tail "
                         "included · tab, then → : the providers of a folded row, and the levels · "
                         "“available” is what the lists, the alt+m box and its filter may offer — un-tick one to "
                         "take it out everywhere, tick a row under “more from…” to bring one in")
        : QStringLiteral("←→ tab · ↑↓ row · enter uses it · alt+↑↓ moves it · del removes it · type a name, "
                         "ctrl+enter adds it · ctrl+z undoes")
              + (boxClassTab() ? QStringLiteral(" · “in box” is a cutoff: alt+m shows this class down to the last one ticked")
                               : QString()));
}

// ----- the list edits ----------------------------------------------------------------------------

void ModelPicker::pushUndo(const QString &tier) {
    m_undo.append(UndoStep{tier, curation::tierList(tier)});
    while (m_undo.size() > 100) m_undo.removeFirst();
}

void ModelPicker::changed() {
    if (onListsChanged) onListsChanged();
}

void ModelPicker::moveSelected(int delta) {
    QTreeWidgetItem *row = currentRow();
    if (!row || m_tier == kAll || !row->data(0, ListedRole).toBool() || delta == 0) return;
    const QString key = row->data(0, KeyRole).toString();
    QList<curation::TierEntry> list = curation::tierList(m_tier);
    int at = -1;
    for (int i = 0; i < list.size(); ++i) if (list.at(i).key == key) at = i;
    const int to = at + delta;
    if (at < 0 || to < 0 || to >= list.size()) return;
    pushUndo(m_tier);
    list.move(at, to);
    curation::setTierList(m_tier, list);
    rebuild();
    selectKey(key);
    changed();
}

void ModelPicker::removeSelected() {
    QTreeWidgetItem *row = currentRow();
    if (!row || m_tier == kAll || !row->data(0, ListedRole).toBool()) return;
    const QString key = row->data(0, KeyRole).toString();
    // No confirmation (owner, design 5.2: "Delete takes it out of the list") — ctrl+z is the
    // answer to a wrong one, and the footer says so.
    pushUndo(m_tier);
    curation::removeFromTier(m_tier, key);
    const int at = m_list->indexOfTopLevelItem(row);
    rebuild();
    if (!m_list->currentItem() || selectedKey().isEmpty()) {
        if (at < m_list->topLevelItemCount()) m_list->setCurrentItem(m_list->topLevelItem(at));
        if (!currentRow()) selectFirstRow();
    }
    changed();
}

void ModelPicker::addSelected() {
    QTreeWidgetItem *row = currentRow();
    if (!row || m_tier == kAll || row->data(0, ListedRole).toBool()) return;
    const QString key = row->data(0, KeyRole).toString();
    const Entry *entry = m_context.catalog.find(key);
    if (!entry || !addableToTier(*entry, m_tier)) return;
    pushUndo(m_tier);
    // The level a model starts at *in this list*, the same rule Options › Models' "+ add a
    // model…" uses (card #TKN7): main the provider's own default, high the level a plan turn
    // takes, flash and lite the lowest.
    curation::addToTier(m_tier, key, tierStartEffort(*entry, m_tier));
    m_filter->clear();   // the row is in the list now; clearing shows it where it landed
    rebuild();
    selectKey(key);
    changed();
}

void ModelPicker::undo() {
    if (m_undo.isEmpty()) return;
    const UndoStep step = m_undo.takeLast();
    curation::setTierList(step.tier, step.list);
    if (step.tier != m_tier && tabIds().contains(step.tier)) { m_tier = step.tier; syncTabBar(); }
    rebuild();
    changed();
}

void ModelPicker::commitDragOrder() {
    if (m_tier == kAll) return;
    const QList<curation::TierEntry> before = curation::tierList(m_tier);
    QHash<QString, QString> efforts;
    for (const curation::TierEntry &item : before) efforts.insert(item.key, item.effort);
    QList<curation::TierEntry> after;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (item->data(0, SectionRole).toBool() || !item->data(0, ListedRole).toBool()) continue;
        const QString key = item->data(0, KeyRole).toString();
        if (!efforts.contains(key)) continue;
        after << curation::TierEntry{key, efforts.value(key)};
    }
    if (after.size() != before.size()) { rebuild(); return; }   // a filtered view: not an order to store
    bool moved = false;
    for (int i = 0; i < after.size(); ++i) moved = moved || after.at(i).key != before.at(i).key;
    if (!moved) return;
    const QString key = selectedKey();
    pushUndo(m_tier);
    curation::setTierList(m_tier, after);
    rebuild();
    selectKey(key);
    changed();
}

// ----- keys ---------------------------------------------------------------------------------------

bool ModelPicker::handleShortcut(QKeyEvent *event) {
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool ctrl = mods & Qt::ControlModifier;
    const bool alt = mods & Qt::AltModifier;
    if (ctrl && (key == Qt::Key_Tab || key == Qt::Key_Backtab)) {
        stepTab(key == Qt::Key_Backtab || (mods & Qt::ShiftModifier) ? -1 : 1);
        return true;
    }
    if (ctrl && key == Qt::Key_Z) { undo(); return true; }
    if (ctrl && (key == Qt::Key_Return || key == Qt::Key_Enter)) { addSelected(); return true; }
    if (alt && (key == Qt::Key_Up || key == Qt::Key_Down)) { moveSelected(key == Qt::Key_Up ? -1 : 1); return true; }
    // Delete takes the row out of the list wherever the focus is (owner, design 5.2) — including
    // while a filter is typed, which is how you find the row you want gone. Backspace only does it
    // with the filter empty, so it stays the key that rubs the filter out.
    if (key == Qt::Key_Delete) { removeSelected(); return true; }
    if (key == Qt::Key_Backspace && m_filter->text().isEmpty()) { removeSelected(); return true; }
    return false;
}

void ModelPicker::keyPressEvent(QKeyEvent *event) {
    if (handleShortcut(event)) { event->accept(); return; }
    QDialog::keyPressEvent(event);
}

bool ModelPicker::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress
        && (watched == m_filter || watched == m_list || watched == m_levels || watched == m_vias || watched == m_tabs)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (handleShortcut(key)) return true;
        if (watched == m_filter) {
            if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down || key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown) {
                QCoreApplication::sendEvent(m_list, event);
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { accept(); return true; }
            // ←/→ walk the tabs from the filter, which is where the focus starts — unless there is
            // text and the caret is somewhere inside it, when they are a caret's arrows again.
            if (key->key() == Qt::Key_Left && (m_filter->text().isEmpty() || m_filter->cursorPosition() == 0)) { stepTab(-1); return true; }
            if (key->key() == Qt::Key_Right && (m_filter->text().isEmpty() || m_filter->cursorPosition() == m_filter->text().size())) { stepTab(1); return true; }
        }
        if (watched == m_list) {
            // The view swallows Enter (it emits activated), so the default button never sees it.
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { accept(); return true; }
            if (key->key() == Qt::Key_Right) {                                   // → the providers, then the levels
                if (m_vias->isVisible() && m_vias->count()) { m_vias->setFocus(); return true; }
                if (m_levels->count()) { m_levels->setFocus(); return true; }
            }
            if (key->key() == Qt::Key_Left) { m_filter->setFocus(); return true; }
        }
        if (watched == m_vias) {
            if (key->key() == Qt::Key_Left) { m_list->setFocus(); return true; }
            if (key->key() == Qt::Key_Right && m_levels->count()) { m_levels->setFocus(); return true; }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { accept(); return true; }
        }
        if (watched == m_levels) {
            if (key->key() == Qt::Key_Left) {
                if (m_vias->isVisible() && m_vias->count()) m_vias->setFocus(); else m_list->setFocus();
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { accept(); return true; }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ModelPicker::accept() {
    // "+ add a model by id…" is a row you press, not a model you use: Enter on it asks for the id
    // and leaves the dialog open on what it added.
    if (QTreeWidgetItem *row = currentRow(); row != nullptr && row->data(0, AddByIdRole).toBool()) {
        promptAddModelById();
        return;
    }
    const QString key = selectedKey();
    if (key.isEmpty()) return;
    m_pick.accepted = true;
    m_pick.key = key;
    QString effort = selectedEffort();
    // "default" is a thing a *list* stores, not a level a pane can run at: the pane still needs
    // one, and it gets the level the row was showing.
    if (effort.isEmpty())
        if (const Entry *entry = m_context.catalog.find(key)) effort = effectiveEffort(*entry);
    m_pick.effort = effort;
    QDialog::accept();
}

ModelPick pickModel(QWidget *parent, const ModelPicker::Context &context, std::function<void()> openModelsPage,
                    std::function<void()> onListsChanged) {
    ModelPicker dialog(context, parent);
    dialog.openModelsPage = std::move(openModelsPage);
    dialog.onListsChanged = std::move(onListsChanged);
    dialog.exec();
    return dialog.pick();
}

}  // namespace relay
