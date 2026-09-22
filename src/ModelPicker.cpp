// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelPicker.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
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
constexpr int TierRole = Qt::UserRole + 8;     // the class list this row belongs to
constexpr int ClassHeadRole = Qt::UserRole + 9;  // a section's own header: its tick is the class switch

const QString kAll = QStringLiteral("all");
const QString kClasses = QStringLiteral("classes");
const QString kLite = QStringLiteral("lite");
const QString kMain = QStringLiteral("main");

// The one line under a class's name on the sectioned page: what running in that class means, so
// the four headers are not four bare words (owner, 2026-09-21: "a header line … and a one-line
// note such as 'new panes start on rank 1' for main").
QString classNote(const QString &tier) {
    if (tier == QStringLiteral("high")) return QStringLiteral("what /high runs on, and a plan turn");
    if (tier == kMain) return QStringLiteral("new panes start on rank 1");
    if (tier == QStringLiteral("flash")) return QStringLiteral("what /flash runs on, and the quick jobs");
    if (tier == QStringLiteral("local")) return QStringLiteral("the model servers on this machine");
    return QString();
}

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

ModelPicker::ModelPicker(const Context &context, QWidget *parent) : QWidget(parent), m_context(context) {
    setObjectName(QStringLiteral("modelPicker"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

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
    populateTabs();
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
    m_customize = new QPushButton(QStringLiteral("customize…"));
    m_customize->setObjectName(QStringLiteral("modelCustomize"));
    m_customize->setToolTip(QStringLiteral("Providers and keys — step 1 of the four (/models)"));
    buttons->addWidget(m_customize);
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
    // No "cancel": a pane is not a modal and there is nothing to cancel — every edit here is
    // already live and Ctrl+Z takes one back (design 5.8). Escape hands the focus back to the
    // pane this serves and leaves the pane open, which the host does.
    m_use = new QPushButton(QStringLiteral("use"));
    m_use->setDefault(true);
    m_use->setToolTip(QStringLiteral("Switch the pane this serves to the highlighted model and level (enter)"));
    buttons->addWidget(m_use);
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
        if (m_building || sectionsPage() || !boxClassTier(m_tier)) return;
        setClassShown(m_tier, on);
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
        use();
    });
    m_list->installEventFilter(this);
    connect(m_favorite, &QPushButton::clicked, this, [this] {
        const QString key = selectedKey();
        if (key.isEmpty()) return;
        curation::toggleFavorite(key);
        rebuild();
        selectKey(key);
    });
    connect(m_customize, &QPushButton::clicked, this, [this] { if (openModelsPage) openModelsPage(); });
    connect(m_vias, &QListWidget::currentRowChanged, this, [this](int) { onViaChanged(); });
    connect(m_levels, &QListWidget::currentRowChanged, this, [this](int) { onLevelChanged(); });
    connect(m_levels, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { use(); });
    m_vias->installEventFilter(this);
    m_levels->installEventFilter(this);
    m_tabs->installEventFilter(this);
    connect(m_use, &QPushButton::clicked, this, [this] { use(); });

    // Typing goes to the filter; arrows move the list even while the filter has the focus, so Tab
    // is what actually moves the focus along the four controls, in the order they are read.
    m_filter->installEventFilter(this);
    setTabOrder(m_filter, m_list);
    setTabOrder(m_list, m_vias);
    setTabOrder(m_vias, m_levels);
    setTabOrder(m_levels, m_favorite);
    // What it opens on: `classes` (the sectioned priorities page), `all`, or one class alone.
    const QStringList ids = tabIds();
    m_tier = m_context.tier == kClasses ? kClasses
           : ids.contains(m_context.tier) ? m_context.tier
           : ids.contains(kMain) ? kMain : ids.value(0, kAll);
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

// The tab row this widget draws. Since the priorities page became one scrolling page of sections
// (owner, 2026-09-21) there is nothing left for it to hold when hosted: the flat tab is the host's
// own **available** tab and the four classes are sections, not tabs. It survives for a caller that
// puts this widget on one class alone, and `syncTabBar` hides it everywhere else.
QStringList ModelPicker::tabBarIds() const {
    QStringList ids = tabIds();
    if (m_hosted) ids.clear();
    return ids;
}

bool ModelPicker::sectionsPage() const { return m_tier == kClasses; }

// high · main · flash, then local where this machine serves one. Never lite: it is not a pane mode,
// the box has never had a row for it (design 5.3), and its list is the jobs tab's business now.
QStringList ModelPicker::sectionTiers() const {
    QStringList out;
    for (const QString &id : tabIds())
        if (id != kAll && id != kLite) out << id;
    return out;
}

QString ModelPicker::rowTier(const QTreeWidgetItem *row) const {
    return row ? row->data(0, TierRole).toString() : QString();
}

QString ModelPicker::editTier() const { return rowTier(currentRow()); }

QString ModelPicker::currentClass() const {
    const QString tier = editTier();
    if (!tier.isEmpty()) return tier;
    if (!sectionsPage()) return m_tier == kAll ? QString() : m_tier;
    // The highlight is on no row of a class — an empty section, or nothing ranked yet. The class
    // the page was last pointed at is still where it is; the first section answers before anything
    // has pointed it anywhere.
    const QStringList sections = sectionTiers();
    return sections.contains(m_focusClass) ? m_focusClass : sections.value(0);
}

void ModelPicker::focusClass(const QString &tier) {
    if (!sectionsPage() || tier.isEmpty() || !sectionTiers().contains(tier)) return;
    m_focusClass = tier;
    // The pane's own model where this section holds it — the row you are most likely to want —
    // then rank 1, and failing both the section's own header, so an empty class is still scrolled
    // to and still says which class the page is on.
    for (int pass = 0; pass < 3; ++pass)
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = m_list->topLevelItem(i);
            if (rowTier(item) != tier) continue;
            const bool header = item->data(0, ClassHeadRole).toBool();
            if (pass < 2 && (header || item->data(0, SectionRole).toBool())) continue;
            if (pass == 0 && item->data(0, KeyRole).toString() != m_context.currentKey) continue;
            if (pass == 2 && !header) continue;
            m_list->setCurrentItem(item);
            m_list->scrollToItem(item);
            return;
        }
}

void ModelPicker::populateTabs() {
    const QSignalBlocker block(m_tabs);
    while (m_tabs->count() > 0) m_tabs->removeTab(0);
    for (const QString &id : tabBarIds()) {
        const int index = m_tabs->addTab(id);
        m_tabs->setTabData(index, id);
        m_tabs->setTabToolTip(index, id == kAll
            ? QStringLiteral("every model, one row each — favorites, then a section per provider, alphabetically")
            : curation::tierLabel(id) + QStringLiteral(" · rank 1 is what this tier runs on, the rest are its fallbacks"));
    }
}

void ModelPicker::setCatalog(const Catalog &catalog, const QString &currentKey,
                             const QString &currentEffort, qint64 now) {
    m_context.catalog = catalog;
    m_context.currentKey = currentKey;
    m_context.currentEffort = currentEffort;
    m_context.now = now;
    const QString selected = selectedKey();
    rebuild();
    // Whatever was highlighted, if the fresh catalog still has it; otherwise the pane's own model,
    // which is what a first draw would have selected. `rebuild()` has already put the highlight on
    // the class this page was pointed at, so a highlight sitting on a section's own header — an
    // empty class — is an answer and not a miss.
    if (!selected.isEmpty()) selectKey(selected);
    if (selectedKey().isEmpty() && !m_list->currentItem() && !currentKey.isEmpty()) selectKey(currentKey);
    if (!m_list->currentItem()) selectFirstRow();
}

void ModelPicker::setHosted(bool hosted) {
    if (hosted == m_hosted) return;
    m_hosted = hosted;
    // "customize…" was the way out of a modal to Options › Models. In the pane, providers are the
    // first tab, so the button says where it goes and the host switches the tab.
    if (m_customize) {
        m_customize->setText(hosted ? QStringLiteral("providers…") : QStringLiteral("customize…"));
        m_customize->setToolTip(hosted
            ? QStringLiteral("The providers tab: keys, logins and custom endpoints — step 1 of the four")
            : QStringLiteral("Providers and keys — step 1 of the four (/models)"));
    }
    populateTabs();
    syncTabBar();
    // The pane is narrower than the 980 px dialog was, and the side column is fixed width: at 210
    // it took a third of the rows' room. "z.ai (glm) · coding…" still fits at 170.
    if (hosted) { m_vias->setFixedWidth(170); m_levels->setFixedWidth(170); }
    updateFooter();
    rebuild();   // the columns a host hides are decided in rebuild()
}

void ModelPicker::syncTabBar() {
    // The sectioned page and the flat tab both draw their own headings; a row of class tabs above
    // either would be a second answer to "where am I" (owner, 2026-09-21: "in a pane, i dont want
    // separate tabs for the modes").
    m_tabs->setVisible(m_tabs->count() > 0 && !sectionsPage() && !(m_hosted && m_tier == kAll));
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabData(i).toString() == m_tier) {
            if (m_tabs->currentIndex() != i) { QSignalBlocker block(m_tabs); m_tabs->setCurrentIndex(i); }
            return;
        }
}

void ModelPicker::setTier(const QString &tier) {
    if (tier == m_tier || !(tier == kClasses || tabIds().contains(tier))) return;
    m_tier = tier;
    syncTabBar();
    m_filter->clear();
    rebuild();
}

void ModelPicker::stepTab(int delta) {
    const QStringList ids = tabBarIds();
    const int at = ids.indexOf(m_tier);
    if (at < 0 || ids.size() < 2) return;
    // Wrapping: with six tabs and ←/→ as the way through them, stopping dead at either end reads
    // as a broken key rather than as an edge.
    setTier(ids.at((at + delta % ids.size() + ids.size()) % ids.size()));
}

// ----- drawing -----------------------------------------------------------------------------------

void ModelPicker::addSection(const QString &title, const QString &tier) {
    // The title goes in column 0 because that is the column a spanned row draws.
    auto *item = new QTreeWidgetItem(m_list, QStringList{title});
    item->setData(0, SectionRole, true);
    if (!tier.isEmpty()) item->setData(0, TierRole, tier);
    item->setFlags(Qt::ItemIsEnabled);   // not selectable, not a drop target
    item->setFirstColumnSpanned(true);
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
}

// A class's own header on the sectioned page. It is **not** a spanned row, because it carries a
// control: the tick in the "in box" column is that class's "show this class in the box" switch
// (design 5.3), sitting directly above the cutoff ticks it governs — one column, one question,
// read down. The class and its note go in the model column, which is the one that stretches.
QTreeWidgetItem *ModelPicker::addClassHeader(const QString &tier) {
    const QString note = classNote(tier);
    auto *item = new QTreeWidgetItem(m_list, QStringList{
        QString(), QString(), QString(),
        note.isEmpty() ? tier : tier + QStringLiteral("   · ") + note});
    item->setData(0, SectionRole, true);        // not a model: never selected, never used, never moved
    item->setData(0, ClassHeadRole, true);
    item->setData(0, TierRole, tier);
    item->setFlags(Qt::ItemIsEnabled | (boxClassTier(tier) ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
    if (boxClassTier(tier))
        item->setCheckState(ColBox, curation::boxShown(tier) ? Qt::Checked : Qt::Unchecked);
    QFont font = item->font(ColModel);
    font.setBold(true);
    item->setFont(ColModel, font);
    const QString tip = boxClassTier(tier)
        ? QStringLiteral("%1 · %2. The tick is whether alt+m shows this class at all; the ticks below it "
                         "say how far down").arg(tier, note)
        : QStringLiteral("%1 · %2").arg(tier, note);
    for (int c = 0; c < ColCount; ++c) item->setToolTip(c, tip);
    return item;
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

QTreeWidgetItem *ModelPicker::addListRow(const QString &tier, int rank, const curation::TierEntry &item,
                                         const Entry *entry) {
    const qint64 now = nowSeconds();
    const qint64 until = entry ? exhaustedUntil(m_context.catalog, entry->preset, now) : -1;
    // Unusable and exhausted rows are greyed *in place*, with the reason where the figure goes —
    // never hidden. A list is an order the user wrote down; silently dropping a rank from it is
    // how "which model is selected first" stopped being answerable (design 1.3).
    const bool dead = !entry || !entry->usable || until >= 0;
    QString name = entry ? entry->name : item.key;
    if (curation::isFavorite(item.key)) name.prepend(QStringLiteral("★ "));
    if (rank == 1 && tier == kMain) name += QStringLiteral("   · new panes start here");
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
    row->setData(0, TierRole, tier);
    row->setData(0, ViaRole, QStringList{item.key});
    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled
                  | (boxClassTier(tier) ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
    // The cutoff, as a column of checkboxes: rank 1..cutoff checked, the rest not, and clicking
    // one moves the cutoff to it (design 5.3, `setBoxCutoffFromRow`).
    if (boxClassTier(tier))
        row->setCheckState(ColBox, curation::boxShown(tier) && rank <= curation::boxCutoff(tier)
                                       ? Qt::Checked : Qt::Unchecked);
    row->setToolTip(0, !entry ? QStringLiteral("%1 is not in the catalog right now: its provider has no key, or it left the listing")
                                    .arg(item.key)
                   : !entry->usable ? QStringLiteral("No key for %1 · skipped until you add one").arg(entry->provider)
                   : until >= 0 ? QStringLiteral("Exhausted · skipped until it resets")
                   : rank == 1 && tier == kMain
                       ? QStringLiteral("%1 · rank 1 of main: what a new pane and /swap run on").arg(entry->model)
                       : rank == 1 ? QStringLiteral("%1 · rank 1 of the %2 list").arg(entry->model, tier)
                       : QStringLiteral("%1 · fallback %2 of the %3 list").arg(entry->model).arg(rank - 1).arg(tier));
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

QTreeWidgetItem *ModelPicker::addGroupRow(const Group &group, bool addable, const QString &tier) {
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
    if (!tier.isEmpty()) row->setData(0, TierRole, tier);
    if (addable) row->setData(0, AddRole, true);
    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    QStringList others;
    for (const Entry &each : group.entries) if (each.key != entry.key) others << providerText(each);
    row->setToolTip(0, entry.model + (entry.custom ? QStringLiteral(" (added by you)") : QString())
                    + (others.isEmpty() ? QString() : QStringLiteral(" · also on ") + others.join(QStringLiteral(", ")) + QStringLiteral(" (→ chooses)"))
                    + (group.spent(m_context.catalog, now) ? QStringLiteral(" · every provider of it is spent") : QString())
                    + (addable ? QStringLiteral(" · ctrl+enter adds it to the %1 list").arg(tier) : QString()));
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
            if (curation::inTerminalList(each.key)) listed = each.key;
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

// The priorities page: every class, one under the other, in the order the box draws them (owner,
// 2026-09-21: "in a pane, i dont want separate tabs for the modes. they should just be in divided
// sections. remove the lite section"). A section header is a class and its rows are that class's
// list, so the four lists are one page you scroll rather than four tabs you have to know to visit.
void ModelPicker::buildSections(const QString &query) {
    for (const QString &tier : sectionTiers()) {
        addClassHeader(tier);
        buildTier(tier, query);
    }
}

// One class's rows: its list in order, then — while something is typed — what it does not list.
// Both halves are drawn **per section**, so "the section the highlight is in" is the list ctrl+enter
// adds to and there is no fourth place with a rule of its own to remember (owner, 2026-09-21:
// "ctrl+enter adds the typed model to the section the highlight is in"). A model addable to three
// classes therefore appears under all three, which is the true answer to "where can I put this".
void ModelPicker::buildTier(const QString &tier, const QString &query) {
    const QList<curation::TierEntry> list = curation::tierList(tier);
    QStringList inList;
    for (const curation::TierEntry &item : list) inList << item.key;
    int rank = 0, drawn = 0;
    for (const curation::TierEntry &item : list) {
        ++rank;
        const Entry *entry = m_context.catalog.find(item.key);
        if (!query.isEmpty() && !(entry ? matches(*entry, query) : item.key.contains(query, Qt::CaseInsensitive))) continue;
        addListRow(tier, rank, item, entry);
        ++drawn;
    }
    if (query.isEmpty()) {
        // One hint, and only where it is the answer: an empty list says how to fill it. The
        // sectioned page would otherwise carry four copies of a line the footer already says.
        if (list.isEmpty())
            addSection(QStringLiteral("nothing in %1 — type a model's name, then ctrl+enter adds it here").arg(tier), tier);
        else if (!sectionsPage())
            addSection(QStringLiteral("type a model's name to add it to this list"), tier);
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
        if (inList.contains(entry.key) || !addableToTier(entry, tier) || !matches(entry, query)) continue;
        (listed.contains(entry.key) ? rest : tail) << entry;
    }
    if (rest.isEmpty() && tail.isEmpty()) {
        if (drawn == 0)
            addSection(sectionsPage() ? QStringLiteral("nothing in %1 matches “%2”").arg(tier, query)
                                      : QStringLiteral("no model matches “%1”").arg(query), tier);
        return;
    }
    if (!rest.isEmpty()) {
        addSection(sectionsPage() ? QStringLiteral("not in %1 — ctrl+enter adds it here").arg(tier)
                                  : QStringLiteral("not in this list"), tier);
        for (const Group &group : grouped(m_context.catalog, rest, nowSeconds())) addGroupRow(group, true, tier);
    }
    if (!tail.isEmpty()) {
        addSection(tailRule(tail), tier);
        for (const Group &group : grouped(m_context.catalog, tail, nowSeconds())) addGroupRow(group, true, tier);
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
    // Favorites first — a section the user made, pinned where they put it — then one section per
    // provider, **alphabetically** (owner, 2026-09-21: "for available, remove the recent section.
    // i would order the sections alphabetically"). A group is a favorite when any of its entries
    // is (edge case 10): a model is one row wherever it sits.
    //
    // **There is no "recent" section.** This tab is a checklist — step 2, which of a provider's
    // models exist for the lists, the box and the filter — and the question it answers is asked of
    // one provider at a time ("i probably want to uncheck sonnet and haiku and gpt 5.5"). A ten-row
    // block of whatever was picked lately pulls those rows out of their provider and puts them
    // somewhere that moves under you between two looks, which is the opposite of what a checklist
    // is for. `models/recent` and `noteUse` stay: `noteUse` is also what counts a model's uses,
    // which is what the sort menu's "usage" reads, and the box's own recency is not this tab's.
    //
    // Alphabetical rather than the rank order the providers happened to come in: a list you are
    // ticking down is read by looking for a name, and the previous order — first model's rank —
    // moved every time a list was edited. Under any other sort, and while something is typed, the
    // list stays flat above: the sort *is* the order then.
    auto groupOf = [&groups](const QString &key) -> const Group * {
        for (const Group &group : groups)
            for (const Entry &entry : group.entries)
                if (entry.key == key) return &group;
        return nullptr;
    };
    QList<const Group *> placed, favorites;
    for (const QString &key : curation::favorites())
        if (const Group *group = groupOf(key); group && !placed.contains(group)) { favorites << group; placed << group; }
    if (!favorites.isEmpty()) { addSection(QStringLiteral("favorites")); for (const Group *group : favorites) addGroupRow(*group, false); }
    QStringList providers;
    QHash<QString, QList<const Group *>> byProvider;
    for (const Group &group : groups) {
        if (placed.contains(&group)) continue;
        const Entry entry = group.preferred(m_context.catalog, nowSeconds());
        const QString provider = entry.provider.isEmpty() ? entry.preset : entry.provider;
        if (!providers.contains(provider)) providers << provider;
        byProvider[provider] << &group;
    }
    // The provider's *shown* name — "z.ai (glm)" sorts under z — case-insensitively, because a row
    // from a worker that predates the lower-casing would otherwise sort above every other section.
    std::sort(providers.begin(), providers.end(), [](const QString &a, const QString &b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
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

bool ModelPicker::boxClassTier(const QString &tier) const {
    return !tier.isEmpty() && tier != kAll && tier != kClasses && curation::boxClasses().contains(tier);
}

void ModelPicker::syncClassSwitch() {
    if (!m_boxSwitch) return;
    // On the sectioned page every class carries its own switch on its own header, where it sits
    // over the cutoff ticks it governs; one control beside the filter could only mean one class.
    const bool single = !sectionsPage() && boxClassTier(m_tier);
    m_boxSwitch->setVisible(single);
    const QSignalBlocker block(m_boxSwitch);
    m_boxSwitch->setChecked(!single || curation::boxShown(m_tier));
}

void ModelPicker::setClassShown(const QString &tier, bool on) {
    if (!boxClassTier(tier) || on == curation::boxShown(tier)) return;
    curation::setBoxShown(tier, on);
    refreshBoxChecks();
    changed();
}

void ModelPicker::setBoxCutoffFromRow(const QString &tier, int rank, bool on) {
    if (!boxClassTier(tier) || rank < 1) return;
    // Checking rank n checks 1..n; unchecking n unchecks n and everything below it. Unchecking
    // rank 1 leaves nothing to show, which is the class switched off — the same statement said
    // from the other control, so the two can never disagree.
    if (on) {
        curation::setBoxCutoff(tier, rank);
        curation::setBoxShown(tier, true);
    } else if (rank == 1) {
        curation::setBoxShown(tier, false);
    } else {
        curation::setBoxCutoff(tier, rank - 1);
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
        const QString tier = rowTier(row);
        if (row->data(0, ClassHeadRole).toBool()) {
            if (boxClassTier(tier))
                row->setCheckState(ColBox, curation::boxShown(tier) ? Qt::Checked : Qt::Unchecked);
            continue;
        }
        if (!row->data(0, ListedRole).toBool()) continue;
        const int rank = row->text(ColRank).toInt();
        row->setCheckState(ColBox, boxClassTier(tier) && curation::boxShown(tier)
                                           && rank >= 1 && rank <= curation::boxCutoff(tier)
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
    if (column != ColBox) return;
    const QString tier = rowTier(item);
    // A section's own header: the tick is that class's switch, not a rank.
    if (item->data(0, ClassHeadRole).toBool()) {
        setClassShown(tier, item->checkState(ColBox) == Qt::Checked);
        return;
    }
    if (!item->data(0, ListedRole).toBool()) return;
    setBoxCutoffFromRow(tier, item->text(ColRank).toInt(), item->checkState(ColBox) == Qt::Checked);
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
            if (curation::inTerminalList(key)) listed = key;
        }
        applyAvailability(row, available, listed);
    }
    m_building = wasBuilding;
}

// One row's tick, its ink and its tooltip. `reason` is the key of a **terminal** list entry, when
// one of this row's providers is named by high, main, flash or local: the tick is then on whatever
// the setting says, because a rank the user wrote down that the box would not offer is a list that
// lies — and the tooltip is where that is said, since a checkbox cannot say it. The **lite** list
// does not pin (card #MDL1, `curation::inTerminalList`): lite is not a pane mode, so what it holds
// says nothing about what this tab offers a terminal agent.
void ModelPicker::applyAvailability(QTreeWidgetItem *row, bool available, const QString &reason) {
    row->setCheckState(ColAvail, available ? Qt::Checked : Qt::Unchecked);
    const QString tip = !reason.isEmpty()
        ? QStringLiteral("Available: your high, main, flash or local list names it, which keeps it available "
                         "whatever this box says. The lite list does not: those are chores, not panes")
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
    m_list->setColumnHidden(ColBox, !(sectionsPage() || boxClassTier(m_tier)));
    // Step 2 is edited in one place: the `all` tab. A tier tab is step 3 and the box column is
    // step 4, and three checkbox columns on one row would say nothing.
    m_list->setColumnHidden(ColAvail, !availabilityTab());
    // Hosted, the widget is half a window wide rather than a 980 px dialog, and nine columns left
    // the **model's name** — the one thing rule 1 says every surface must print — elided to
    // "claude-o…". Intelligence and tok/s are what the sort menu sorts by, not what is read while
    // picking, and every cell of the row already carries them in its tooltip, so they are the two
    // that go (card #MDL1 t:a11; docs/qa_evidence/2026-09-21-models-pane/e-available.png is the
    // run that showed it). "left" stays: it is where an exhausted row says why it is greyed.
    m_list->setColumnHidden(ColIntelligence, m_hosted);
    m_list->setColumnHidden(ColSpeed, m_hosted);
    // Dragging is how a list is reordered; on the flat tab there is no order to write down. On the
    // sectioned page a drag that crosses a header changes no list — `commitDragOrder` reads each
    // section's rows back and a section whose count has changed is not an order to store.
    m_list->setDragDropMode(all ? QAbstractItemView::NoDragDrop : QAbstractItemView::InternalMove);
    if (all) buildAll(query);
    else if (sectionsPage()) buildSections(query);
    else buildTier(m_tier, query);
    m_building = false;
    if (!keep.isEmpty()) selectKey(keep);
    // A page that does not hold the row you were on comes back to the **class** it was pointed at —
    // on the pane's own model where that class holds it, which is what `focusClass` prefers — then
    // to the pane's model anywhere, then to rank 1. The class comes first because a model can be in
    // three lists at once: without it a re-read of the flash section landed on the same model's row
    // in high, and the page changed class under the person every time a fresh catalog arrived.
    if (sectionsPage() && !currentRow() && !m_focusClass.isEmpty()) focusClass(m_focusClass);
    if (!currentRow() && !m_list->currentItem()) selectKey(m_context.currentKey);
    if (!currentRow() && !m_list->currentItem()) selectFirstRow();
    updateFooter();
    onRowChanged();
}

// ↑/↓ over the **rows**, stepping over the section headers. Qt's own cursor movement makes the
// current item whatever is next, header or not, and a header is `currentRow() == nullptr` — so on
// the sectioned page, where a header sits between every two classes, Down out of the last high row
// used to land on nothing at all and the level list emptied. It is the one key that has to cross a
// section, which is what the owner asked of it: "↑/↓ move across sections".
void ModelPicker::stepRow(int delta) {
    const int count = m_list->topLevelItemCount();
    if (count == 0 || delta == 0) return;
    const int at = m_list->indexOfTopLevelItem(m_list->currentItem());
    if (at < 0) { selectFirstRow(); return; }
    for (int i = at + delta; i >= 0 && i < count; i += delta) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (item->data(0, SectionRole).toBool()) continue;
        m_list->setCurrentItem(item);
        m_list->scrollToItem(item);
        return;
    }
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
        const QString tier = rowTier(row);
        const bool listed = row && row->data(0, ListedRole).toBool() && !tier.isEmpty();
        QString stored;
        if (listed) {
            for (const curation::TierEntry &item : curation::tierList(tier))
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
    const QString tier = rowTier(row);
    if (!row || tier.isEmpty() || !row->data(0, ListedRole).toBool()) return;
    const QString key = row->data(0, KeyRole).toString();
    const QString level = selectedEffort();
    bool moved = false;
    for (const curation::TierEntry &item : curation::tierList(tier))
        if (item.key == key) moved = moved || item.effort != level;
    if (!moved) return;   // nothing changed, or the key is no longer in the list
    pushUndo(tier);
    curation::setTierEffort(tier, key, level);
    // The word the list now holds is the provider's own, so it is what the column prints.
    row->setText(ColReasoning, level.isEmpty() ? QStringLiteral("default") : level);
    changed();
}

void ModelPicker::updateFooter() {
    // Hosted, this widget draws no tab row of its own: the host's tabs are ←→ and alt+1…, and the
    // priorities page has sections rather than class tabs, so ←→ belong to the host everywhere.
    const QString tabs = m_hosted ? QStringLiteral("←→ alt+1… tab · ") : QStringLiteral("←→ tab · ");
    QString text = m_tier == kAll
        ? tabs + QStringLiteral("↑↓ row · enter uses it in the pane · type to search every model, openrouter's "
                                "long tail included · tab, then → : the providers of a folded row, and the levels · "
                                "“available” is what the lists, the alt+m box and its filter may offer — un-tick one "
                                "to take it out everywhere, tick a row under “more from…” to bring one in")
        : tabs + QStringLiteral("↑↓ row, across the sections · enter uses it in the pane · alt+↑↓ moves it inside its "
                                "section · del removes it · type a name, ctrl+enter adds it to the section the "
                                "highlight is in · ctrl+z undoes")
              + (sectionsPage() || boxClassTier(m_tier)
                     ? QStringLiteral(" · “in box” is a cutoff: alt+m shows a class down to the last one ticked, and "
                                      "the tick on a section's own line is whether it shows that class at all")
                     : QString());
    if (onEscape) text += QStringLiteral(" · esc back to the pane");
    m_footer->setText(text);
}

// ----- the list edits ----------------------------------------------------------------------------

void ModelPicker::pushUndo(const QString &tier) {
    m_undo.append(UndoStep{tier, curation::tierList(tier)});
    while (m_undo.size() > 100) m_undo.removeFirst();
}

void ModelPicker::changed() {
    if (onListsChanged) onListsChanged();
}

// Each of the four edits reads the **row's** class rather than a tab: on the sectioned page four
// lists are on screen at once, so which list a key acts on is a property of the row under the
// highlight (`editTier`). On a single-class page that is the page's own class, unchanged.

void ModelPicker::moveSelected(int delta) {
    QTreeWidgetItem *row = currentRow();
    const QString tier = rowTier(row);
    if (!row || tier.isEmpty() || !row->data(0, ListedRole).toBool() || delta == 0) return;
    const QString key = row->data(0, KeyRole).toString();
    QList<curation::TierEntry> list = curation::tierList(tier);
    int at = -1;
    for (int i = 0; i < list.size(); ++i) if (list.at(i).key == key) at = i;
    const int to = at + delta;
    // Clamped inside its own section: alt+↓ on the last rank of flash does not push the model into
    // the next class, which would be a second edit nobody asked for.
    if (at < 0 || to < 0 || to >= list.size()) return;
    pushUndo(tier);
    list.move(at, to);
    curation::setTierList(tier, list);
    rebuild();
    selectKey(key);
    changed();
}

void ModelPicker::removeSelected() {
    QTreeWidgetItem *row = currentRow();
    const QString tier = rowTier(row);
    if (!row || tier.isEmpty() || !row->data(0, ListedRole).toBool()) return;
    const QString key = row->data(0, KeyRole).toString();
    // No confirmation (owner, design 5.2: "Delete takes it out of the list") — ctrl+z is the
    // answer to a wrong one, and the footer says so.
    pushUndo(tier);
    curation::removeFromTier(tier, key);
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
    const QString tier = rowTier(row);
    if (!row || tier.isEmpty() || row->data(0, ListedRole).toBool()) return;
    const QString key = row->data(0, KeyRole).toString();
    const Entry *entry = m_context.catalog.find(key);
    if (!entry || !addableToTier(*entry, tier)) return;
    pushUndo(tier);
    // The level a model starts at *in this list*, the same rule Options › Models' "+ add a
    // model…" uses (card #TKN7): main the provider's own default, high the level a plan turn
    // takes, flash and lite the lowest.
    curation::addToTier(tier, key, tierStartEffort(*entry, tier));
    m_filter->clear();   // the row is in the list now; clearing shows it where it landed
    rebuild();
    selectKey(key);
    changed();
}

void ModelPicker::undo() {
    if (m_undo.isEmpty()) return;
    const UndoStep step = m_undo.takeLast();
    curation::setTierList(step.tier, step.list);
    // On the sectioned page every class is already on screen, so there is nowhere to go; on a
    // single-class page the undone list is brought in front, or its edit would be invisible.
    if (!sectionsPage() && step.tier != m_tier && tabIds().contains(step.tier)) { m_tier = step.tier; syncTabBar(); }
    rebuild();
    changed();
}

void ModelPicker::commitDragOrder() {
    if (m_tier == kAll) return;
    // The rows as they now read, split by the section each one is in. A drag that crossed a header
    // changes two sections' counts, and a section whose count is not its list's length is not an
    // order to store — the same rule a filtered view has always had.
    QStringList order;
    QHash<QString, QList<QString>> rows;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (item->data(0, SectionRole).toBool() || !item->data(0, ListedRole).toBool()) continue;
        const QString tier = rowTier(item);
        if (tier.isEmpty()) continue;
        if (!order.contains(tier)) order << tier;
        rows[tier] << item->data(0, KeyRole).toString();
    }
    bool redraw = false, anyMoved = false;
    for (const QString &tier : std::as_const(order)) {
        const QList<curation::TierEntry> before = curation::tierList(tier);
        const QStringList keys = rows.value(tier);
        if (keys.size() != before.size()) { redraw = true; continue; }
        QHash<QString, QString> efforts;
        for (const curation::TierEntry &item : before) efforts.insert(item.key, item.effort);
        QList<curation::TierEntry> after;
        for (const QString &key : keys) {
            if (!efforts.contains(key)) { after.clear(); break; }
            after << curation::TierEntry{key, efforts.value(key)};
        }
        if (after.size() != before.size()) { redraw = true; continue; }
        bool moved = false;
        for (int i = 0; i < after.size(); ++i) moved = moved || after.at(i).key != before.at(i).key;
        if (!moved) continue;
        pushUndo(tier);
        curation::setTierList(tier, after);
        anyMoved = true;
    }
    if (!anyMoved) { if (redraw) rebuild(); return; }
    const QString key = selectedKey();
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
    // Ctrl+Tab walks this widget's own tabs — unless it is hosted in the models pane, where it is
    // not this widget's to answer at all: Ctrl+Tab is the window's **Next tab** (Keymap
    // `tab.next`), so it never reaches a pane. The host's three tabs are Alt+1/2/3 and ←/→ on the
    // flat tab; the class tabs keep ←/→.
    if (ctrl && (key == Qt::Key_Tab || key == Qt::Key_Backtab)) {
        if (m_hosted) return false;
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
    // Escape belongs to the host: "Escape returns focus to the pane it serves and leaves it open"
    // (design 5.8). Nothing is closed here, and with no host Escape falls through untouched.
    if (key == Qt::Key_Escape && !mods && onEscape) { onEscape(); return true; }
    return false;
}

void ModelPicker::keyPressEvent(QKeyEvent *event) {
    if (handleShortcut(event)) { event->accept(); return; }
    QWidget::keyPressEvent(event);
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
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { use(); return true; }
            // ←/→ walk the tabs from the filter, which is where the focus starts — unless there is
            // text and the caret is somewhere inside it, when they are a caret's arrows again.
            // Hosted on the flat tab this widget's class row is hidden, so ←/→ have no tab of
            // its own to walk: they fall through to the host's three (src/ModelsPane.cpp).
            // Hosted, this widget has no tab row at all any more: ←/→ are the host's three tabs.
            const bool ownTabs = !m_hosted && !sectionsPage();
            if (ownTabs && key->key() == Qt::Key_Left && (m_filter->text().isEmpty() || m_filter->cursorPosition() == 0)) { stepTab(-1); return true; }
            if (ownTabs && key->key() == Qt::Key_Right && (m_filter->text().isEmpty() || m_filter->cursorPosition() == m_filter->text().size())) { stepTab(1); return true; }
        }
        if (watched == m_list) {
            // The view swallows Enter (it emits activated), so the default button never sees it.
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { use(); return true; }
            if (!key->modifiers() && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
                stepRow(key->key() == Qt::Key_Down ? 1 : -1);
                return true;
            }
            if (key->key() == Qt::Key_Right) {                                   // → the providers, then the levels
                if (m_vias->isVisible() && m_vias->count()) { m_vias->setFocus(); return true; }
                if (m_levels->count()) { m_levels->setFocus(); return true; }
            }
            if (key->key() == Qt::Key_Left) { m_filter->setFocus(); return true; }
        }
        if (watched == m_vias) {
            if (key->key() == Qt::Key_Left) { m_list->setFocus(); return true; }
            if (key->key() == Qt::Key_Right && m_levels->count()) { m_levels->setFocus(); return true; }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { use(); return true; }
        }
        if (watched == m_levels) {
            if (key->key() == Qt::Key_Left) {
                if (m_vias->isVisible() && m_vias->count()) m_vias->setFocus(); else m_list->setFocus();
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { use(); return true; }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ModelPicker::use() {
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
    if (onUse) onUse(m_pick);
}

}  // namespace relay
