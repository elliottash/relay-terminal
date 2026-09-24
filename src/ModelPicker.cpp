#include "PaneTabNavigation.h"
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelPicker.h"
#include "Hints.h"

#include <algorithm>
#include <tuple>

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStringListModel>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
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
// ColMove is the ▲▼ of a listed row (card #RKP3): the mouse's version of alt+↑↓, because a drag
// nobody discovers and a key nobody guesses both read as "the rank can't be changed". Hidden on
// the `all` tab, which has no order to write down.
// ColAvail is step 2 of the four, and belongs to the `all` tab alone (design 5.7). It sits third
// rather than first for one mechanical reason: a section rule is a `setFirstColumnSpanned` row,
// which draws out of column 0, so column 0 has to be one that is never hidden — and ColAvail is
// hidden on every tab but `all`. ColRank is empty on the `all` tab and resizes to a few pixels, so
// the tick is still the first thing on the row.
enum Column { ColRank, ColMove, ColAvail, ColBox, ColModel, ColVia, ColReasoning, ColIntelligence, ColSpeed, ColLeft, ColCount };
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
constexpr int AddByIdPresetRole = Qt::UserRole + 10;  // the provider a section's "+ add model" row adds to

int listPosition(const QString &tier, const QString &key) {
    int position = 0;
    for (const curation::TierEntry &entry : curation::tierList(tier)) {
        ++position;
        if (entry.key == key) return position;
    }
    return 0;
}

const QString kAll = QStringLiteral("all");
const QString kClasses = QStringLiteral("classes");
const QString kEffort = QStringLiteral("effort");
const QString kLite = QStringLiteral("lite");
const QString kMain = QStringLiteral("main");

// The one line under a class's name on the sectioned page: what running in that class means, so
// the four headers are not four bare words (owner, 2026-09-21: "a header line … and a one-line
// note such as 'new panes start on rank 1' for main").
// Short enough to fit the column beside the model's name at pane width — the first run of the
// evidence script drew "what /high runs on, and…" and said nothing.
QString classNote(const QString &tier) {
    if (tier == QStringLiteral("high")) return QStringLiteral("/high, and plan mode");
    if (tier == kMain) return QStringLiteral("new panes start on rank 1");
    if (tier == QStringLiteral("flash")) return QStringLiteral("/flash, and quick jobs");
    if (tier == QStringLiteral("local")) return QStringLiteral("this machine's own models");
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
    m_tabs = new QTabBar(this);
    relay::paneTabs::registerTabs(this, m_tabs);
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
    m_filter->setPlaceholderText(QStringLiteral("filter every model · ↑↓ select · ctrl+enter adds it here"));
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
    m_list->setHeaderLabels({QString(), QString(), QStringLiteral("enabled"), QStringLiteral("in box"),
                             QStringLiteral("model"), QStringLiteral("via"),
                             QStringLiteral("reasoning"), QStringLiteral("intelligence"), QStringLiteral("tok/s"),
                             QStringLiteral("left")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAllColumnsShowFocus(true);
    m_list->header()->setStretchLastSection(false);
    // **Two** stretch columns, not one. `via` sized to its contents and the model column took what
    // was left, which on a pane half a window wide was about 145 px — so the one thing rule 1 says
    // every surface must print was drawn as "claude-opu…" while "anthropic (claude) · pay-as-you-go"
    // had room to spare (docs/qa_evidence/2026-09-21-models-pane-review is the run that showed it).
    // Sharing the space elides the provider first, which is the column whose text repeats down the
    // page and whose tooltip says it in full.
    m_list->header()->setSectionResizeMode(ColModel, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(ColVia, QHeaderView::Stretch);
    for (int c = 0; c < ColCount; ++c)
        if (c != ColModel && c != ColVia) m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setDragDropOverwriteMode(false);
    m_list->setDefaultDropAction(Qt::MoveAction);
    // Tab leaves the view rather than walking its cells, because Tab is how the keyboard reaches
    // the right-hand column: ←/→ in the filter belong to the tabs, so the way to a row's
    // providers is filter → tab → list → →.
    m_list->setTabKeyNavigation(false);
    dragList->onDropped = [this] { commitDragOrder(); };

    m_listsLayout = new QGridLayout;
    m_listsLayout->setContentsMargins(0, 0, 0, 0);
    m_listsLayout->setColumnStretch(0, 1);
    m_listsLayout->setRowStretch(0, 1);
    m_listsLayout->addWidget(m_list, 0, 0);
    // The right-hand column: which provider this row will use (only when it folds more than one),
    // then the level, each its own pick. Enter anywhere uses the pair.
    m_sidePanel = new QWidget;
    auto *sideColumn = new QVBoxLayout(m_sidePanel);
    sideColumn->setContentsMargins(0, 0, 0, 0);
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
    m_levelsLabel = new QLabel(QStringLiteral("reasoning"));
    sideColumn->addWidget(m_levelsLabel);
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
    m_listsLayout->addWidget(m_sidePanel, 0, 1);
    m_orderHelp = new QLabel(QStringLiteral("Top to bottom: first choice, then fallbacks. Equal ranks draw randomly. "
                                            "Alt+M shows checked rows through each class's cutoff."));
    m_orderHelp->setObjectName(QStringLiteral("modelOrderHelp"));
    m_orderHelp->setWordWrap(true);
    layout->addWidget(m_orderHelp);
    m_orderActions = new QWidget(this);
    m_orderActions->setObjectName(QStringLiteral("modelOrderActions"));
    auto *orderLayout = new QVBoxLayout(m_orderActions);
    orderLayout->setContentsMargins(0, 0, 0, 0);
    orderLayout->setSpacing(2);
    m_orderSelection = new QLabel(QStringLiteral("Select a model to edit"), m_orderActions);
    m_orderSelection->setObjectName(QStringLiteral("modelOrderSelection"));
    m_orderSelection->setTextFormat(Qt::PlainText);
    m_orderSelection->setWordWrap(true);
    orderLayout->addWidget(m_orderSelection);
    auto *moveRow = new QHBoxLayout;
    auto *editRow = new QHBoxLayout;
    for (QHBoxLayout *row : {moveRow, editRow}) {
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
    }
    const auto action = [this](QHBoxLayout *row, const QString &label, const QString &name) {
        auto *button = new QPushButton(label, m_orderActions);
        button->setObjectName(name);
        row->addWidget(button);
        return button;
    };
    m_orderUp = action(moveRow, QStringLiteral("Move up"), QStringLiteral("modelOrderUp"));
    m_orderDown = action(moveRow, QStringLiteral("Move down"), QStringLiteral("modelOrderDown"));
    m_orderTie = action(editRow, QStringLiteral("Tie with above"), QStringLiteral("modelOrderTie"));
    m_orderRemove = action(editRow, QStringLiteral("Remove"), QStringLiteral("modelOrderRemove"));
    m_orderAdd = action(editRow, QStringLiteral("Add to list"), QStringLiteral("modelOrderAdd"));
    moveRow->addStretch(1);
    editRow->addStretch(1);
    orderLayout->addLayout(moveRow);
    orderLayout->addLayout(editRow);
    layout->addWidget(m_orderActions);
    connect(m_orderUp, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_orderDown, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(m_orderTie, &QPushButton::clicked, this, [this] {
        QTreeWidgetItem *row = currentRow();
        if (row && row->data(0, ListedRole).toBool()) toggleTie(rowTier(row), row->data(0, KeyRole).toString());
    });
    connect(m_orderRemove, &QPushButton::clicked, this, [this] { removeSelected(); });
    connect(m_orderAdd, &QPushButton::clicked, this, [this] {
        QTreeWidgetItem *row = currentRow();
        if (row && row->data(0, AddRole).toBool()) { addSelected(); return; }
        m_filter->setFocus();
        m_filter->selectAll();
    });
    layout->addLayout(m_listsLayout, 1);

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
        // "+ add model" is a button in a row's clothes: one click asks, as a button would.
        if (item->data(0, AddByIdRole).toBool()) {
            const QString preset = item->data(0, AddByIdPresetRole).toString();
            QTimer::singleShot(0, this, [this, preset] { promptAddModelById(preset); });
            return;
        }
        if (column == ColVia && item->data(0, ViaRole).toStringList().size() > 1 && m_vias->isVisible()) m_vias->setFocus();
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int column) {
        if (!item || item->data(0, SectionRole).toBool()) return;
        if (effortPage()) {
            if (auto *choice = m_list->itemWidget(item, ColReasoning)) choice->setFocus();
            return;
        }
        // A tick is a control: two quick clicks on it are two toggles (or one that would not take,
        // on a row a list pins), never "use". The owner's pane went onto gemini flash lite that
        // way on 2026-09-21 while he was trying to un-tick it.
        if (column == ColAvail || column == ColBox) return;
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
    connect(m_levels, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { if (!effortPage()) use(); });
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
    m_tier = m_context.tier == kClasses || m_context.tier == kEffort ? m_context.tier
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

bool ModelPicker::sectionsPage() const { return m_tier == kClasses || m_tier == kEffort; }
bool ModelPicker::effortPage() const { return m_tier == kEffort; }

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
    // While something is typed, a class that ranks nothing matching lands on its first "+ add"
    // offer instead, so ctrl+enter adds what was typed; untyped, the offers are not what it runs.
    const bool typed = !m_filter->text().trimmed().isEmpty();
    for (int pass = 0; pass < 4; ++pass)
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = m_list->topLevelItem(i);
            if (rowTier(item) != tier) continue;
            const bool header = item->data(0, ClassHeadRole).toBool();
            const bool offer = item->data(0, AddRole).toBool();
            if (pass < 3 && (header || item->data(0, SectionRole).toBool())) continue;
            if (pass < 2 && offer) continue;
            if (pass == 0 && item->data(0, KeyRole).toString() != m_context.currentKey) continue;
            if (pass == 2 && (!typed || !offer)) continue;
            if (pass == 3 && !header) continue;
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
    updateCompactLayout();
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
    if (tier == m_tier || !(tier == kClasses || tier == kEffort || tabIds().contains(tier))) return;
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
    // The class in the model column and its note in **via**: the note is a sentence and the model
    // column is the narrow one, so a note written there was drawn as "main · new pane…" while the
    // wide column beside it sat empty.
    const QString note = classNote(tier);
    auto *item = new QTreeWidgetItem(m_list, QStringList{QString(), QString(), QString(), QString(), tier, note});
    item->setData(0, SectionRole, true);        // not a model: never selected, never used, never moved
    item->setData(0, ClassHeadRole, true);
    item->setData(0, TierRole, tier);
    item->setFlags(Qt::ItemIsEnabled | (boxClassTier(tier) ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
    if (boxClassTier(tier) && !effortPage())
        item->setCheckState(ColBox, curation::boxShown(tier) ? Qt::Checked : Qt::Unchecked);
    if (boxClassTier(tier) && !effortPage())
        item->setText(ColBox, curation::boxShown(tier) ? QStringLiteral("On") : QStringLiteral("Off"));
    QFont font = item->font(ColModel);
    font.setBold(true);
    item->setFont(ColModel, font);
    item->setFont(ColVia, font);
    // "divided sections" (owner, 2026-09-21) — a band across the header row, a step off the list's
    // own background rather than a colour of its own, so it follows a theme change and is visible
    // in a light one and a dark one alike.
    const QColor base = m_list->palette().color(QPalette::Base);
    const QColor band = base.lightness() < 128 ? base.lighter(155) : base.darker(110);
    for (int c = 0; c < ColCount; ++c) item->setBackground(c, band);
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
    // Un-ticked on Available (#AVR8) is the same: greyed where it is ranked, so ticking it again
    // puts it back at this rank, and "not available" where the figure goes.
    const bool unticked = !curation::isAvailableKey(item.key, curation::availableKeys());
    const bool dead = !entry || !entry->usable || until >= 0 || unticked;
    QString name = entry ? entry->name : item.key;
    if (curation::isFavorite(item.key)) name.prepend(QStringLiteral("★ "));
    // …and on the sectioned page main's own header already says it, so the row does not: the two
    // together cost the model column a third of its width and elided the name (rule 1).
    if (rank == 1 && tier == kMain && !sectionsPage()) name += QStringLiteral("   · new panes start here");
    QString left;
    if (!entry) left = QStringLiteral("unavailable");
    else if (unticked) left = QStringLiteral("not available");
    else if (!entry->usable) left = entry->preset == QStringLiteral("relay-pro")
        ? QStringLiteral("no access") : QStringLiteral("no key");
    else if (until >= 0) left = QStringLiteral("0%") + (until > 0 ? QStringLiteral(" · resets ") + resetText(until, now) : QString());
    else left = percent(percentLeft(m_context.catalog, entry->preset));
    QString level;
    if (entry && !entry->efforts.isEmpty())
        level = item.effort.isEmpty() ? QStringLiteral("default") : nearestEffort(entry->efforts, item.effort);
    const double speed = curation::speed(item.key);
    // The sectioned page has no column to spare for "left" (rule 1 protects the model column
    // first): a row with something actually wrong says so in "via" instead, right beside the
    // provider it's wrong about. A healthy row's plain percentage stays out of sight here — it is
    // still in ColLeft (hidden on this page) and in the tooltip — since nobody needs "87% left"
    // repeated down a page of rows that are all fine.
    QString via = entry ? providerText(*entry) : QString();
    if (sectionsPage() && dead && !left.isEmpty())
        via = via.isEmpty() ? left : via + QStringLiteral(" · ") + left;
    // Ranked but past this class's "in box" cutoff (`setBoxCutoffFromRow`): the checkbox says so,
    // but a checkbox column alone read as nine independent ticks rather than one cutoff line, so
    // the row is muted the same way an unusable one is — a second, glance-able signal for the
    // same fact.
    const bool outOfBox = boxClassTier(tier) && curation::boxShown(tier) && rank > curation::boxCutoff(tier);
    auto *row = new QTreeWidgetItem(m_list, QStringList{
        QString::number(rank), QString(), QString(), QString(), name, via, level,
        entry && entry->intelligence >= 0 ? QString::number(entry->intelligence) : QString(),
        speed > 0 ? QString::number(qRound(speed)) : QString(), left});
    row->setData(0, KeyRole, item.key);
    row->setData(0, ListedRole, true);
    row->setData(0, TierRole, tier);
    row->setData(0, ViaRole, QStringList{item.key});
    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled
                  | (boxClassTier(tier) ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
    // The ▲▼ (card #RKP3): the same edit alt+↑↓ makes, one click at a time, for the mouse hand
    // that never found the drag. The button that would leave the list is disabled, so a click
    // always does something. Deferred a turn: the click rebuilds the rows, which deletes the
    // very button whose signal is running.
    if (!(m_hosted && sectionsPage())) {
        auto *moveBox = new QWidget;
        auto *moveLayout = new QHBoxLayout(moveBox);
        moveLayout->setContentsMargins(0, 0, 0, 0);
        moveLayout->setSpacing(0);
        const QList<curation::TierEntry> ranked = curation::tierList(tier);
        const int listSize = ranked.size();
        const QString key = item.key;
        for (const auto &[delta, arrow, tip] : {std::tuple{-1, Qt::UpArrow, QStringLiteral("move up (alt+↑)")},
                                                std::tuple{1, Qt::DownArrow, QStringLiteral("move down (alt+↓)")}}) {
            auto *button = new QToolButton(moveBox);
            button->setArrowType(arrow);
            button->setAutoRaise(true);
            button->setFixedSize(18, 18);
            button->setToolTip(tip);
            button->setEnabled(delta < 0 ? rank > 1 : rank < listSize);
            QObject::connect(button, &QToolButton::clicked, this, [this, tier, key, delta] {
                QTimer::singleShot(0, this, [this, tier, key, delta] {
                    moveKey(tier, key, delta);
                    // The standing hint rule (WARP.md): the buttons are the slow path, alt+↑↓ the
                    // fast one. The limits line carries it — the picker has no toast queue — and
                    // alt+↑↓ is a picker key, not a Keymap action, so the text is a literal.
                    if (ShortcutHints::instance().shouldShow(QStringLiteral("models.move.buttons")))
                        m_limits->setText(ShortcutHints::nextTime(QStringLiteral("Alt+↑ / Alt+↓"),
                                                                  QStringLiteral("move a row")));
                });
            });
            moveLayout->addWidget(button);
        }
        const int ownRank = item.rank > 0 ? item.rank : rank;
        const int previousRank = rank > 1 ? (ranked.at(rank - 2).rank > 0 ? ranked.at(rank - 2).rank : rank - 1) : -1;
        const int nextRank = rank < listSize ? (ranked.at(rank).rank > 0 ? ranked.at(rank).rank : rank + 1) : -1;
        const bool tied = ownRank == previousRank || ownRank == nextRank;
        auto *tie = new QToolButton(moveBox);
        tie->setObjectName(QStringLiteral("modelTie"));
        tie->setText(tied ? QStringLiteral("≠") : QStringLiteral("="));
        tie->setAccessibleName(tied ? QStringLiteral("untie model") : QStringLiteral("tie with previous model"));
        tie->setToolTip(tied ? QStringLiteral("Untie this model: give it its own priority (equal ranks draw randomly)")
                             : QStringLiteral("Tie with the model above: equal ranks draw randomly"));
        tie->setAutoRaise(true);
        tie->setFixedSize(20, 18);
        tie->setEnabled(tied || rank > 1);
        QObject::connect(tie, &QToolButton::clicked, this, [this, tier, key] {
            QTimer::singleShot(0, this, [this, tier, key] { toggleTie(tier, key); });
        });
        moveLayout->addWidget(tie);
        auto *remove = new QToolButton(moveBox);
        remove->setObjectName(QStringLiteral("modelRemove"));
        remove->setText(QStringLiteral("×"));
        remove->setAccessibleName(QStringLiteral("remove model from this list"));
        remove->setToolTip(QStringLiteral("Remove from this priority list (Delete); Ctrl+Z undoes"));
        remove->setAutoRaise(true);
        remove->setFixedSize(20, 18);
        QObject::connect(remove, &QToolButton::clicked, this, [this, tier, key] {
            QTimer::singleShot(0, this, [this, tier, key] {
                for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
                    QTreeWidgetItem *candidate = m_list->topLevelItem(i);
                    if (candidate->data(0, ListedRole).toBool() && rowTier(candidate) == tier
                        && candidate->data(0, KeyRole).toString() == key) {
                        m_list->setCurrentItem(candidate);
                        removeSelected();
                        return;
                    }
                }
            });
        });
        moveLayout->addWidget(remove);
        m_list->setItemWidget(row, ColMove, moveBox);
    }
    // The cutoff, as a column of checkboxes: rank 1..cutoff checked, the rest not, and clicking
    // one moves the cutoff to it (design 5.3, `setBoxCutoffFromRow`).
    if (boxClassTier(tier)) {
        row->setCheckState(ColBox, curation::boxShown(tier) && rank <= curation::boxCutoff(tier)
                                       ? Qt::Checked : Qt::Unchecked);
        row->setText(ColBox, curation::boxShown(tier) && rank <= curation::boxCutoff(tier)
                                 ? QStringLiteral("In") : QStringLiteral("Out"));
    }
    QString tip = !entry ? QStringLiteral("%1 is not in the catalog right now: its provider has no key, or it left the listing")
                               .arg(item.key)
                : !entry->usable ? QStringLiteral("No key for %1 · skipped until you add one").arg(entry->provider)
                : until >= 0 ? QStringLiteral("Exhausted · skipped until it resets")
                : rank == 1 && tier == kMain
                    ? QStringLiteral("%1 · rank 1 of main: what a new pane and /swap run on").arg(entry->model)
                    : rank == 1 ? QStringLiteral("%1 · rank 1 of the %2 list").arg(entry->model, tier)
                    : QStringLiteral("%1 · fallback %2 of the %3 list").arg(entry->model).arg(rank - 1).arg(tier);
    if (outOfBox)
        tip += QStringLiteral(" · not shown in the box (alt+m) — rank %1 and below are cut off there")
                   .arg(curation::boxCutoff(tier) + 1);
    row->setToolTip(0, tip);
    for (int c = 0; c < ColCount; ++c) row->setToolTip(c, tip);
    if (boxClassTier(tier))
        row->setToolTip(ColBox, row->checkState(ColBox) == Qt::Checked
            ? QStringLiteral("Shown in Alt+M. Untick to move this class's cutoff above this row")
            : QStringLiteral("Outside Alt+M. Tick to extend this class's cutoff through this row"));
    row->setTextAlignment(ColRank, Qt::AlignRight | Qt::AlignVCenter);
    for (int c = ColReasoning; c < ColCount; ++c) row->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    if (dead || outOfBox)
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
    // Same fold as a listed row's (addListRow): the sectioned page has no ColLeft to spare, so an
    // offer that would be spent if added says so right in "via" instead of a column of its own.
    if (sectionsPage() && until >= 0) via += QStringLiteral(" · ") + leftText;
    auto *row = new QTreeWidgetItem(m_list, QStringList{
        addable ? QStringLiteral("+ add") : QString(), QString(), QString(), QString(), name, via,
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
        QTreeWidgetItem *row = addListRow(tier, rank, item, entry);
        row->setText(ColRank, QString::number(item.rank > 0 ? item.rank : rank));
        row->setToolTip(ColRank, QStringLiteral("Lower ranks run first; equal ranks draw randomly. Use = or ≠ beside the move arrows."));
        if (effortPage()) decorateEffortRow(row, tier, item, entry);
        ++drawn;
    }
    if (effortPage()) {
        if (drawn == 0)
            addSection(QStringLiteral("no ranked models in %1%2").arg(
                tier, query.isEmpty() ? QString() : QStringLiteral(" match this filter")), tier);
        return;
    }
    // Pick order's four classes no longer repeat the whole available catalog as a long + add
    // pool. The labeled action above the list focuses search; matching offers appear below the
    // appropriate class as soon as the user types.
    if (m_hosted && sectionsPage() && query.isEmpty()) return;
    // Offers come from the available pool, filtered within this class. The full catalog,
    // including OpenRouter's long tail, is searched on Enabled; searching it once per class here
    // froze the pane on every keystroke.
    QList<Entry> rest;
    for (const Entry &entry : shown(m_context.catalog)) {
        if (inList.contains(entry.key) || !addableToTier(entry, tier)) continue;
        if (!query.isEmpty() && !matches(entry, query)) continue;
        rest << entry;
    }
    if (rest.isEmpty()) {
        if (!query.isEmpty() && drawn == 0)
            addSection(sectionsPage() ? QStringLiteral("nothing in %1 matches “%2”").arg(tier, query)
                                      : QStringLiteral("no model matches “%1”").arg(query), tier);
        else if (list.isEmpty())
            addSection(QStringLiteral("nothing in %1 — tick models on available to offer them here").arg(tier), tier);
        return;
    }
    addSection(sectionsPage() ? QStringLiteral("not in %1 — + add puts it here").arg(tier)
                              : QStringLiteral("not in this list — + add puts it here"), tier);
    for (const Group &group : grouped(m_context.catalog, rest, nowSeconds())) addGroupRow(group, true, tier);
}

void ModelPicker::decorateEffortRow(QTreeWidgetItem *row, const QString &tier,
                                    const curation::TierEntry &item, const Entry *entry) {
    if (!row) return;
    row->setText(ColRank, QString());
    if (!entry) {
        row->setText(ColModel, row->text(ColModel) + QStringLiteral("\nmodel unavailable"));
        row->setText(ColReasoning, QStringLiteral("—"));
        row->setSizeHint(ColModel, QSize(0, 48));
        return;
    }
    const bool fixed = entry->effortFixed || entry->efforts.isEmpty();
    const QString support = fixed
        ? (entry->effortFixedReason().isEmpty() ? QStringLiteral("no reasoning level")
                                                : entry->effortFixedReason())
        : QStringLiteral("supports: %1").arg(entry->efforts.join(QStringLiteral(" · ")));
    row->setText(ColModel, row->text(ColModel) + QLatin1Char('\n') + support);
    row->setToolTip(ColModel, row->toolTip(ColModel) + QLatin1Char('\n') + support);
    row->setSizeHint(ColModel, QSize(0, fixed ? 48 : 66));
    if (fixed) {
        row->setText(ColReasoning, QStringLiteral("fixed"));
        row->setToolTip(ColReasoning, support);
        return;
    }
    auto *choice = new QComboBox(m_list);
    choice->setObjectName(QStringLiteral("modelEffortChoice"));
    choice->setAccessibleName(QStringLiteral("%1 reasoning level in %2").arg(entry->model, tier));
    choice->addItem(QStringLiteral("default"), QString());
    for (const QString &level : entry->efforts) choice->addItem(level, level);
    int selected = choice->findData(item.effort);
    if (selected < 0) {
        choice->addItem(QStringLiteral("%1 (unsupported)").arg(item.effort), item.effort);
        selected = choice->count() - 1;
    }
    choice->setCurrentIndex(selected);
    choice->setToolTip(QStringLiteral("Stored level: %1. Supported: %2. Choose here to change this list entry.")
        .arg(item.effort.isEmpty() ? QStringLiteral("default") : item.effort,
             entry->efforts.join(QStringLiteral(", "))));
    choice->installEventFilter(this);  // Ctrl+Z and Escape keep their page actions while focused here.
    connect(choice, QOverload<int>::of(&QComboBox::activated), this, [this, row](int index) {
        auto *selector = qobject_cast<QComboBox *>(m_list->itemWidget(row, ColReasoning));
        if (!selector || index < 0) return;
        m_list->setCurrentItem(row);
        const QString level = selector->itemData(index).toString();
        for (int i = 0; i < m_levels->count(); ++i)
            if (m_levels->item(i)->data(Qt::UserRole).toString() == level) {
                m_levels->setCurrentRow(i);  // the existing write path keeps undo and storage intact
                return;
            }
    });
    m_list->setItemWidget(row, ColReasoning, choice);
}

void ModelPicker::buildAll(const QString &query) {
    const Sort sort = sortFromId(m_sort->currentData().toString());
    // `curatable`, not `shown`: this tab is where step 2 is *edited*, so a model you un-ticked is
    // still drawn — greyed, with an empty box to tick again (design 5.7). What is not here is an
    // open-ended provider's long tail, which is behind typing as it has always been.
    const QList<Entry> listed = curatable(m_context.catalog);
    const QList<Entry> rows = ordered(listed, sort, m_context.catalog);
    const QList<Group> groups = grouped(m_context.catalog, rows, nowSeconds());
    if (!query.isEmpty()) {
        // Keep search results under provider names too. A flat list makes the provider behind a
        // folded row hard to scan, especially when a query matches several model families.
        QHash<QString, QList<const Group *>> byProvider;
        QStringList providers;
        const auto addMatch = [&](const Group &group) {
            const Entry entry = group.preferred(m_context.catalog, nowSeconds());
            const QString provider = entry.provider.isEmpty() ? entry.preset : entry.provider;
            if (!byProvider.contains(provider)) providers << provider;
            byProvider[provider] << &group;
        };
        for (const Group &group : groups) {
            bool hit = false;
            for (const Entry &entry : group.entries) hit = hit || matches(entry, query);
            if (hit) addMatch(group);
        }
        QStringList have;
        for (const Entry &entry : listed) have << entry.key;
        QList<Entry> tail;
        for (const Entry &entry : allUsable(m_context.catalog))
            if (!have.contains(entry.key) && matches(entry, query)) tail << entry;
        const QList<Group> tailGroups = grouped(m_context.catalog, tail, nowSeconds());
        for (const Group &group : tailGroups) addMatch(group);
        std::sort(providers.begin(), providers.end(), [](const QString &a, const QString &b) {
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });
        for (const QString &provider : std::as_const(providers)) {
            addSection(provider);
            for (const Group *group : byProvider.value(provider)) addGroupRow(*group, false);
        }
        if (providers.isEmpty()) addSection(QStringLiteral("no model matches “%1”").arg(query));
        addAddByIdRow();
        return;
    }
    if (sort != Sort::Priority) {
        for (const Group &group : groups) {
            addGroupRow(group, false);
        }
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
    // A provider that has nothing listed yet — OpenRouter before any of its models is ticked — is
    // still a section, so its "+ add model" row is there to press.
    QHash<QString, QString> presetOf;
    for (const QString &preset : m_context.catalog.presets()) {
        const QList<Entry> rows = m_context.catalog.ofPreset(preset);
        // A guest harness and Relay Free run the models they come with; there is nothing to add.
        if (rows.isEmpty() || !rows.first().usable || rows.first().guest || rows.first().hosted) continue;
        const QString provider = rows.first().provider.isEmpty() ? preset : rows.first().provider;
        if (!presetOf.contains(provider)) presetOf.insert(provider, preset);
        if (!providers.contains(provider)) providers << provider;
    }
    std::sort(providers.begin(), providers.end(), [](const QString &a, const QString &b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    for (const QString &provider : providers) {
        addSection(provider);
        for (const Group *group : byProvider.value(provider)) addGroupRow(*group, false);
        // Under every provider rather than once at the foot of the page (owner, 2026-09-22: "the
        // text box is hard to find, make it show up in the provider sections, as a +add model").
        if (const QString preset = presetOf.value(provider); !preset.isEmpty()) addAddByIdRow(preset);
    }
}

// The last row of the `all` tab: the "add a model by id" box that used to sit under every
// open-ended provider on Options › Models (design 5.5). Almost everything it was for is done by
// typing now — the filter reaches the whole catalog — so what is left is the case it was really
// for: an id the provider serves but does not list. Enter on the row asks for it.
void ModelPicker::addAddByIdRow(const QString &preset) {
    // Column 0, spanned, like a rule — but selectable, because it is a row you press. Under a
    // provider it is that provider's, and the prompt opens on it.
    auto *row = new QTreeWidgetItem(m_list, QStringList{preset.isEmpty() ? QStringLiteral("+ add a model by id…")
                                                                         : QStringLiteral("+ add model")});
    row->setData(0, AddByIdRole, true);
    row->setData(0, AddByIdPresetRole, preset);
    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    row->setFirstColumnSpanned(true);
    const QString tip = QStringLiteral("Add one of this provider's models — its whole list completes as you type, "
                                       "and an id it serves but does not list is accepted too");
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

void ModelPicker::promptAddModelById(const QString &preset) {
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
    // The section's provider when the row was a section's; else the provider of the row you were
    // on, where there was one: the likeliest answer.
    if (!preset.isEmpty())
        provider->setCurrentIndex(qMax(0, provider->findData(preset)));
    else if (const Entry *entry = m_context.catalog.find(selectedKey()); entry != nullptr)
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
    const QStringList available = curation::availableKeys();
    const qint64 now = nowSeconds();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *row = m_list->topLevelItem(i);
        const QString tier = rowTier(row);
        if (row->data(0, ClassHeadRole).toBool()) {
            if (boxClassTier(tier)) {
                row->setCheckState(ColBox, curation::boxShown(tier) ? Qt::Checked : Qt::Unchecked);
                row->setText(ColBox, curation::boxShown(tier) ? QStringLiteral("On") : QStringLiteral("Off"));
            }
            continue;
        }
        if (!row->data(0, ListedRole).toBool()) continue;
        const int rank = listPosition(tier, row->data(0, KeyRole).toString());
        const bool inBox = boxClassTier(tier) && curation::boxShown(tier)
                           && rank >= 1 && rank <= curation::boxCutoff(tier);
        row->setCheckState(ColBox, inBox ? Qt::Checked : Qt::Unchecked);
        row->setText(ColBox, inBox ? QStringLiteral("In") : QStringLiteral("Out"));
        row->setToolTip(ColBox, inBox ? QStringLiteral("Shown in Alt+M. Untick to move this class's cutoff above this row")
                                      : QStringLiteral("Outside Alt+M. Tick to extend this class's cutoff through this row"));
        const Entry *entry = m_context.catalog.find(row->data(0, KeyRole).toString());
        const bool dead = !entry || !entry->usable || exhausted(m_context.catalog, entry->preset, now)
                          || !curation::isAvailableKey(row->data(0, KeyRole).toString(), available);
        const bool outOfBox = boxClassTier(tier) && curation::boxShown(tier) && !inBox;
        for (int c = 0; c < ColCount; ++c)
            row->setForeground(c, dead || outOfBox ? QBrush(palette().color(QPalette::Disabled, QPalette::Text)) : QBrush());
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
    setBoxCutoffFromRow(tier, listPosition(tier, item->data(0, KeyRole).toString()),
                        item->checkState(ColBox) == Qt::Checked);
}

// ----- step 2: available (owner, 2026-09-21; design 5.7) --------------------------------------

bool ModelPicker::availabilityTab() const { return m_tier == kAll; }

void ModelPicker::setRowAvailable(const QString &groupKey, bool on) {
    if (groupKey.isEmpty()) return;
    // Every provider of the row, because a row is one model (rule 2). Which row is which is read
    // off the drawn rows rather than re-folded: `ViaRole` already holds exactly those keys.
    QStringList keys{groupKey};
    QTreeWidgetItem *clickedRow = currentRow();
    if (clickedRow == nullptr || clickedRow->data(0, KeyRole).toString() != groupKey) {
        clickedRow = nullptr;
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            if (m_list->topLevelItem(i)->data(0, KeyRole).toString() == groupKey) {
                clickedRow = m_list->topLevelItem(i);
                break;
            }
    }
    if (clickedRow) {
        const QStringList vias = clickedRow->data(0, ViaRole).toStringList();
        if (!vias.isEmpty()) keys = vias;
    }
    for (const QString &key : std::as_const(keys)) curation::setAvailable(key, on, m_context.catalog);
    refreshAvailability(clickedRow);
    // The host's modelsCurated() already batches the expensive cross-pane fan-out. Signal it for
    // every persisted edit, including one just before this picker is replaced.
    changed();
}

// The tick and the greying again, in place. Nothing moves: an un-ticked row stays in this tab so
// it can be ticked back (`curatable`), and rebuilding from inside the itemChanged that delivered
// the click would delete the very item that is being clicked.
void ModelPicker::refreshAvailability(QTreeWidgetItem *row) {
    if (row == nullptr || !availabilityTab()) return;
    const curation::ReadScope reads;
    const bool wasBuilding = m_building;
    m_building = true;
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
    row->setText(ColAvail, available ? QStringLiteral("On") : QStringLiteral("Off"));
    const QString tip = !reason.isEmpty()
        ? (available ? QStringLiteral("Available: ranked in your priorities. Un-ticking greys it there and "
                                      "nothing runs it, but it keeps its place for when you tick it again")
                     : QStringLiteral("Not available: still ranked in your priorities, greyed and skipped. "
                                      "Tick it to put it back at its place"))
        : available
            ? QStringLiteral("Available: this model is in the lists, the alt+m box and its filter")
            : QStringLiteral("Not available: it is in no list, not in the box, and not in the box's filter. "
                             "Typing its name in this dialog still reaches it");
    row->setToolTip(ColAvail, tip);
    bool spent = true;
    for (const QString &key : row->data(0, ViaRole).toStringList()) {
        const Entry *entry = m_context.catalog.find(key);
        if (entry && entry->usable && !exhausted(m_context.catalog, entry->preset, nowSeconds())) {
            spent = false;
            break;
        }
    }
    for (int c = 0; c < ColCount; ++c)
        row->setForeground(c, available && !spent ? QBrush() : QBrush(palette().color(QPalette::Disabled, QPalette::Text)));
}

void ModelPicker::rebuild() {
    const curation::ReadScope reads;   // one settings read per key for the whole redraw
    const QString keep = selectedKey();
    const bool all = m_tier == kAll;
    m_building = true;
    m_list->clear();
    m_list->headerItem()->setText(ColRank, all || effortPage() ? QString() : QStringLiteral("rank"));
    m_list->headerItem()->setText(ColReasoning, effortPage() ? QStringLiteral("selected level")
                                                        : QStringLiteral("reasoning"));
    m_list->headerItem()->setText(ColMove, all || effortPage() ? QString() : QStringLiteral("edit"));
    m_list->headerItem()->setText(ColBox, all || effortPage() ? QString() : QStringLiteral("Alt+M"));
    const QString query = m_filter->text().trimmed();
    m_sortLabel->setVisible(all);
    m_sort->setVisible(all);
    m_orderHelp->setVisible(!all && !effortPage());
    m_orderActions->setVisible(m_hosted && sectionsPage());
    m_orderHelp->setText(QStringLiteral("Top to bottom: first choice, then fallbacks. Equal ranks draw randomly.")
                         + (sectionsPage() || boxClassTier(m_tier)
                                ? QStringLiteral(" Alt+M shows checked rows through each class's cutoff.") : QString()));
    m_favorite->setVisible(!effortPage());
    m_use->setVisible(!effortPage() && !m_hosted);
    m_sidePanel->setVisible(!effortPage());
    m_list->setWordWrap(effortPage());
    m_list->setUniformRowHeights(!effortPage());
    m_list->header()->setSectionResizeMode(ColRank, effortPage() ? QHeaderView::Fixed
                                                                  : QHeaderView::ResizeToContents);
    if (effortPage()) m_list->setColumnWidth(ColRank, 4);
    m_list->header()->setSectionResizeMode(ColReasoning, effortPage() ? QHeaderView::Fixed
                                                                       : QHeaderView::ResizeToContents);
    if (effortPage()) m_list->setColumnWidth(ColReasoning, 126);
    syncClassSwitch();
    m_list->setColumnHidden(ColBox, effortPage() || !(sectionsPage() || boxClassTier(m_tier)));
    // The ▲▼ belong where an order is written down; the flat tab has none (same rule as the drag
    // below, card #RKP3).
    m_list->setColumnHidden(ColMove, all || effortPage() || (m_hosted && sectionsPage()));
    // Step 2 is edited in one place: the `all` tab. A tier tab is step 3 and the box column is
    // step 4, and three checkbox columns on one row would say nothing.
    m_list->setColumnHidden(ColAvail, !availabilityTab());
    // Hosted, the widget is half a window wide rather than a 980 px dialog, and nine columns left
    // the **model's name** — the one thing rule 1 says every surface must print — elided to
    // "claude-o…". Intelligence and tok/s are what the sort menu sorts by, not what is read while
    // picking, and every cell of the row already carries them in its tooltip, so they are the two
    // that go (card #MDL1 t:a11; docs/qa_evidence/2026-09-21-models-pane/e-available.png is the
    // run that showed it). "left" stays on `all`: it is where an exhausted row says why it is
    // greyed there; the sectioned page folds the same reason into "via" instead (below).
    m_list->setColumnHidden(ColIntelligence, m_hosted || effortPage());
    m_list->setColumnHidden(ColSpeed, m_hosted || effortPage());
    // "left" is folded into "via" on the sectioned page (addListRow, addGroupRow) rather than
    // drawn in its own column: the reason a row is greyed sits right beside the provider it's
    // wrong about, and a healthy row's plain percentage is not worth a column nobody compares
    // across a page of already-ranked rows. ColLeft keeps its text for tooltips and callers that
    // read it directly; only the header hides.
    m_list->setColumnHidden(ColLeft, sectionsPage());
    // Dragging is how a list is reordered — and, across a header, how a model moves to another
    // section (card #RKP3); on the flat tab there is no order to write down.
    m_list->setDragDropMode(all || effortPage() ? QAbstractItemView::NoDragDrop : QAbstractItemView::InternalMove);
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
    // onRowChanged() first: it ends by setting m_limits to the highlighted row's own status (or
    // clearing it), and a hint updateFooter() sets there — due only once in a while — must be the
    // last write, or it is overwritten in the same breath it was shown.
    onRowChanged();
    updateFooter();
    updateCompactLayout();
}

void ModelPicker::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateCompactLayout();
}

void ModelPicker::updateCompactLayout() {
    if (!m_listsLayout || !m_sidePanel || !m_list) return;
    const bool compact = m_hosted && width() < 620;
    if (compact != m_compact) {
        m_compact = compact;
        m_listsLayout->removeWidget(m_sidePanel);
        m_listsLayout->addWidget(m_sidePanel, compact ? 1 : 0, compact ? 0 : 1);
        m_listsLayout->setColumnStretch(1, 0);
        for (QListWidget *list : {m_vias, m_levels}) {
            if (compact) {
                list->setMinimumWidth(0);
                list->setMaximumWidth(QWIDGETSIZE_MAX);
            } else {
                list->setFixedWidth(m_hosted ? 170 : 210);
            }
        }
        m_vias->setMaximumHeight(compact ? 84 : 130);
        m_levels->setMaximumHeight(compact ? 110 : 240);
        onRowChanged();
        updateFooter();
    }
    m_list->setColumnHidden(ColVia, compact || effortPage());
    m_list->setColumnHidden(ColReasoning, !effortPage() && (compact || m_hosted));
    if (compact) m_list->setColumnHidden(ColLeft, true);
    else m_list->setColumnHidden(ColLeft, sectionsPage());
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
    // A ranked row before a "+ add" offer of the same model: on the priorities page every class
    // offers what it does not rank, so the first row with the key is often another class's offer.
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = m_list->topLevelItem(i);
            if (item->data(0, SectionRole).toBool()) continue;
            if (pass == 0 && item->data(0, AddRole).toBool()) continue;
            if (item->data(0, KeyRole).toString() == key || item->data(0, ViaRole).toStringList().contains(key)) {
                m_list->setCurrentItem(item);
                m_list->scrollToItem(item);
                return;
            }
        }
}

void ModelPicker::updateOrderActions() {
    if (!m_orderActions) return;
    QTreeWidgetItem *row = currentRow();
    const bool listed = row && row->data(0, ListedRole).toBool();
    const bool offered = row && row->data(0, AddRole).toBool();
    for (QPushButton *button : {m_orderUp, m_orderDown, m_orderTie, m_orderRemove})
        button->setVisible(listed);
    m_orderAdd->setVisible(m_hosted && sectionsPage());
    m_orderAdd->setText(offered ? QStringLiteral("Add to %1").arg(rowTier(row))
                                : QStringLiteral("Find model to add"));
    if (!row || (!listed && !offered)) {
        m_orderSelection->setText(QStringLiteral("Select a model to edit"));
        return;
    }
    const QString tier = rowTier(row);
    const QString key = row->data(0, KeyRole).toString();
    const Entry *entry = m_context.catalog.find(key);
    const QString name = entry ? entry->name : key;
    if (offered) {
        m_orderSelection->setText(QStringLiteral("%1 · %2 · not ranked").arg(tier, name));
        return;
    }
    const QList<curation::TierEntry> list = curation::tierList(tier);
    int at = -1;
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).key == key) { at = i; break; }
    const int rank = at >= 0 ? (list.at(at).rank > 0 ? list.at(at).rank : at + 1) : 0;
    const int previous = at > 0 ? (list.at(at - 1).rank > 0 ? list.at(at - 1).rank : at) : -1;
    const int next = at >= 0 && at + 1 < list.size()
                         ? (list.at(at + 1).rank > 0 ? list.at(at + 1).rank : at + 2) : -1;
    const bool tied = rank == previous || rank == next;
    m_orderSelection->setText(QStringLiteral("%1 · %2 · rank %3%4")
                                  .arg(tier, name).arg(rank)
                                  .arg(tied ? QStringLiteral(" (random tie)") : QString()));
    m_orderUp->setEnabled(at > 0);
    m_orderDown->setEnabled(at >= 0 && at + 1 < list.size());
    m_orderTie->setText(tied ? QStringLiteral("Untie") : QStringLiteral("Tie with above"));
    m_orderTie->setEnabled(tied || at > 0);
}

void ModelPicker::onRowChanged() {
    QTreeWidgetItem *row = currentRow();
    const QString key = selectedKey();
    const Entry *entry = key.isEmpty() ? nullptr : m_context.catalog.find(key);
    m_use->setEnabled(entry != nullptr);
    m_favorite->setEnabled(entry != nullptr);
    m_favorite->setText(entry && curation::isFavorite(key) ? QStringLiteral("★ unfavorite") : QStringLiteral("☆ favorite"));
    updateOrderActions();
    m_filling = true;
    m_vias->clear();
    const QStringList vias = row ? row->data(0, ViaRole).toStringList() : QStringList();
    const bool showVias = vias.size() > 1 || (m_compact && !vias.isEmpty());
    m_vias->setVisible(showVias);
    m_viaLabel->setVisible(showVias);
    if (showVias)
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
    if (m_hosted && !effortPage()) {
        m_levelsLabel->hide();
        m_levels->hide();
        m_filling = false;
        m_limits->clear();
        return;
    }
    if (!entry) {
        m_filling = false;
        m_limits->clear();
        // Nothing is highlighted (a section header, an empty page): the sectioned page has no room
        // for a rail that would say nothing either way.
        if (sectionsPage()) { m_levelsLabel->hide(); m_levels->hide(); }
        else { m_levelsLabel->show(); m_levels->show(); }
        return;
    }
    const bool noKnob = entry->effortFixed || entry->efforts.isEmpty();
    if (noKnob && sectionsPage()) {
        // On the sectioned page there is no width to spare for a rail that holds one disabled
        // line (the truncated "has no rea…" a review run caught, docs/qa_evidence/2026-09-22-VPR7):
        // the row's own tooltip already says why, so the rail is hidden instead of filled with a
        // sentence nobody can read past the third word.
        m_levelsLabel->hide();
        m_levels->hide();
    } else if (noKnob) {
        m_levelsLabel->show();
        m_levels->show();
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
        m_levelsLabel->show();
        m_levels->show();
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
    if (effortPage())
        if (auto *choice = qobject_cast<QComboBox *>(m_list->itemWidget(row, ColReasoning))) {
            const QSignalBlocker block(choice);
            choice->setCurrentIndex(choice->findData(level));
        }
    changed();
}

void ModelPicker::updateFooter() {
    // Hosted, this widget draws no tab row of its own: the host's tabs are Tab / Shift+Tab (#PNAV)
    // and the priorities page has sections rather than class tabs, so ←→ belong to the host
    // everywhere. (It used to say alt+1… — those digits are the window's, and the lie sent the
    // owner hunting for a key that did nothing, card #RKP3.)
    const QString tabs = m_hosted ? QStringLiteral("tab / shift+tab: the pane's tabs · ") : QStringLiteral("←→ tab · ");
    // "enter uses it" only where Enter picks: never in the models pane (card #BXMS).
    const bool picks = !m_hosted;
    const QString enterUses = picks ? QStringLiteral(" · enter uses it") : QString();
    QString text;
    if (m_tier == kAll) {
        text = tabs + QStringLiteral("↑↓ row") + (picks ? QStringLiteral(" · enter uses it in the pane") : QString())
             + QStringLiteral(" · type to search every model, openrouter's long tail included · tab, then → : the providers of a folded row · "
                                     "“available” is what the lists, the alt+m box and its filter may offer — un-tick one "
                                     "to take it out everywhere, tick a searched provider row to bring one in");
    } else if (effortPage()) {
        text = tabs + QStringLiteral("Each row shows supported levels · choose its selected level on that row · ctrl+z undo");
    } else if (sectionsPage()) {
        // Everything this page can also do — reorder, add, remove, undo — used to be spelled out
        // here every time, which was the wall of text card #RKP3 was already trying to shorten.
        // It is taught once instead, the first few times the page is opened, through the limits
        // line's own hint mechanism (`ShortcutHints`, the same one "models.move.buttons" already
        // uses) — the one fact worth saying every time is the one no control on the row spells
        // out on its own: what the cutoff means.
        text = tabs + QStringLiteral("“in box”: ranks above the line show in alt+m — the tick on a row moves the "
                                     "line, the tick on a section's own line is whether it shows at all");
        // Deferred a turn, the same way the move buttons' hint is (card #RKP3): `rebuild()` calls
        // this and then often `selectKey()` right after (the constructor, moveKey, addSelected…),
        // whose `currentItemChanged` runs `onRowChanged()` and overwrites `m_limits` with the row's
        // own status. Set here and now, the hint would be erased before anyone saw it.
        if (ShortcutHints::instance().shouldShow(QStringLiteral("models.priorities.controls")))
            QTimer::singleShot(0, this, [this] {
                m_limits->setText(QStringLiteral("Tip: drag, ▲▼ or alt+↑/↓ reorders (drag across a section to move "
                                                 "it there) · type a name + ctrl+enter adds it here · del removes, ctrl+z undoes"));
            });
    } else {
        text = tabs + QStringLiteral("▲▼ or alt+↑↓ moves a row · drag to reorder — or into another section to move it "
                                     "there · del removes · “+ add” or ctrl+enter ranks an available model in its section "
                                     "· typing filters this page; search every model on available · ctrl+z undoes")
             + (boxClassTier(m_tier)
                    ? QStringLiteral(" · “in box”: how far down a class alt+m shows, and the tick on a section's "
                                     "own line is whether it shows at all")
                    : QString());
    }
    if (onEscape) text += QStringLiteral(" · esc back to the pane");
    m_footer->setToolTip(text);
    if (m_compact)
        text = m_tier == kAll
            ? QStringLiteral("search · tick available%1 · tab / shift+tab switch page").arg(picks ? QStringLiteral(" · enter use") : QString())
            : effortPage() ? QStringLiteral("choose a level on each row · ctrl+z undo")
            : QStringLiteral("ctrl+enter add · del remove · alt+↑/↓ move · ctrl+z undo%1").arg(picks ? QStringLiteral(" · enter use") : QString());
    m_footer->setText(text);
    // Only available searches the whole catalog (#AVR8); a list page filters what it draws.
    m_filter->setPlaceholderText(effortPage() ? QStringLiteral("filter ranked models") : m_tier == kAll
        ? QStringLiteral("search every model · ↑↓ select") + enterUses
        : QStringLiteral("filter this page · ↑↓ select%1 · ctrl+enter adds it here").arg(enterUses));
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

void ModelPicker::moveKey(const QString &tier, const QString &key, int delta) {
    if (tier.isEmpty() || delta == 0) return;
    QList<curation::TierEntry> list = curation::tierList(tier);
    int at = -1;
    for (int i = 0; i < list.size(); ++i) if (list.at(i).key == key) at = i;
    const int to = at + delta;
    // Clamped inside its own section: alt+↓ on the last rank of flash does not push the model into
    // the next class, which would be a second edit nobody asked for. (Across the sections is the
    // drag's job — there the target and the rank are both pointed at.)
    if (at < 0 || to < 0 || to >= list.size()) return;
    pushUndo(tier);
    // Moving between distinct priorities exchanges their rank slots. A move inside a tied
    // group only changes its display order; both models still take the same random draw.
    std::swap(list[at].rank, list[to].rank);
    list.move(at, to);
    curation::setTierList(tier, list);
    rebuild();
    selectKey(key);
    changed();
}

void ModelPicker::moveSelected(int delta) {
    QTreeWidgetItem *row = currentRow();
    if (!row || !row->data(0, ListedRole).toBool()) return;
    moveKey(rowTier(row), row->data(0, KeyRole).toString(), delta);
}

void ModelPicker::toggleTie(const QString &tier, const QString &key) {
    QList<curation::TierEntry> list = curation::tierList(tier);
    int at = -1;
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).key == key) { at = i; break; }
    if (at < 0) return;
    // Old entries may have no explicit rank. Materialize their positional rank before changing
    // one group; setTierList continues to write the same |rank= storage as before.
    for (int i = 0; i < list.size(); ++i)
        if (list[i].rank <= 0) list[i].rank = i + 1;
    const int rank = list.at(at).rank;
    const bool tiedBefore = at > 0 && list.at(at - 1).rank == rank;
    const bool tiedAfter = at + 1 < list.size() && list.at(at + 1).rank == rank;
    if (!tiedBefore && !tiedAfter && at == 0) return;
    pushUndo(tier);
    if (tiedBefore || tiedAfter) {
        // Split this row from either neighbour. Rows tied after it move past the new rank,
        // preserving their tie with each other; later fallback groups keep their order.
        for (int i = 0; i < list.size(); ++i) {
            if (i == at) continue;
            if ((i > at && list[i].rank == rank) || list[i].rank > rank) list[i].rank += 2;
        }
        list[at].rank = rank + 1;
    } else {
        list[at].rank = list.at(at - 1).rank;
    }
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
    // The rows as they now read, grouped by the section each one is **physically** in: walking top
    // to bottom, a class header opens its section (card #RKP3). The TierRole cannot be trusted
    // here — a dragged row keeps the role of the list it came from, so grouping by the role read
    // a cross-section drag as "nothing moved" and the row snapped back where it started (#YX8Q).
    QString section;
    QHash<QString, QStringList> rows;
    bool stray = false;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (item->data(0, ClassHeadRole).toBool()) { section = rowTier(item); continue; }
        if (item->data(0, SectionRole).toBool() || !item->data(0, ListedRole).toBool()) continue;
        const QString tier = sectionsPage() ? section : rowTier(item);
        if (tier.isEmpty()) { stray = true; continue; }   // dropped above the first header
        rows[tier] << item->data(0, KeyRole).toString();
    }
    // Every list on the page is read back, not only the ones rows were seen in: a section whose
    // last row was dragged away is now empty, and that is an order too.
    const QStringList tiers = sectionsPage() ? sectionTiers() : QStringList{m_tier};
    QHash<QString, QStringList> before;
    QHash<QString, QString> efforts;   // a key keeps the level it carried, whichever list it moves to
    QHash<QString, QList<int>> ranks;
    for (const QString &tier : tiers)
        for (const curation::TierEntry &item : curation::tierList(tier)) {
            before[tier] << item.key;
            efforts.insert(item.key, item.effort);
            ranks[tier] << item.rank;
        }
    // The drop is an order only when the drawn rows are exactly the listed rows: only listed rows
    // can drag, so a difference is a view, not an order — a filter hiding part of a list, or a row
    // left above the first header. Store nothing, redraw what the lists really say, and *say* why:
    // a silent snap-back is how "the rank can't be changed" happened (#YX8Q).
    QStringList drawnAll, listedAll;
    for (const QString &tier : tiers) { drawnAll << rows.value(tier); listedAll << before.value(tier); }
    drawnAll.sort(); listedAll.sort();
    if (stray || drawnAll != listedAll) {
        rebuild();
        if (!m_filter->text().trimmed().isEmpty())
            m_limits->setText(QStringLiteral("not stored — the filter is hiding rows, so the drop is not an order; clear it to reorder"));
        return;
    }
    // A drag across a header is a move: out of the source list, into the target list at the rank
    // it was dropped at — which is also how a section a model was never in gets it (#RKP3).
    bool anyMoved = false;
    for (const QString &tier : tiers) {
        const QStringList drawn = rows.value(tier);
        if (drawn == before.value(tier)) continue;
        QList<curation::TierEntry> after;
        for (int i = 0; i < drawn.size(); ++i) {
            const QString &key = drawn.at(i);
            // Existing slots retain their ranks, including ties; a cross-section move
            // creates a new sequential order in the changed-size lists.
            const int rank = drawn.size() == ranks.value(tier).size() ? ranks.value(tier).at(i) : i + 1;
            after << curation::TierEntry{key, efforts.value(key), rank};
        }
        pushUndo(tier);
        curation::setTierList(tier, after);
        anyMoved = true;
    }
    if (!anyMoved) return;   // the rows read as the lists already do
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
    // `tab.next`), so it never reaches a pane. The host's tabs use Tab/Shift+Tab and ←/→ on the
    // flat tab; the class tabs keep ←/→.
    if (ctrl && (key == Qt::Key_Tab || key == Qt::Key_Backtab)) {
        if (m_hosted) return false;
        stepTab(key == Qt::Key_Backtab || (mods & Qt::ShiftModifier) ? -1 : 1);
        return true;
    }
    if (ctrl && key == Qt::Key_Z) { undo(); return true; }
    // Effort edits a ranked row's level. Ranking keys here would silently change Priorities.
    if (effortPage() && ((ctrl && (key == Qt::Key_Return || key == Qt::Key_Enter))
                         || (alt && (key == Qt::Key_Up || key == Qt::Key_Down))
                         || key == Qt::Key_Delete || key == Qt::Key_Backspace)) return true;
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
        && (watched == m_filter || watched == m_list || watched == m_levels || watched == m_vias
            || watched == m_tabs || watched->objectName() == QStringLiteral("modelEffortChoice"))) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (watched->objectName() == QStringLiteral("modelEffortChoice")
            && key->key() == Qt::Key_Z && (key->modifiers() & Qt::ControlModifier)) {
            QTimer::singleShot(0, this, [this] { undo(); });  // rebuilding deletes this combo
            return true;
        }
        if (handleShortcut(key)) return true;
        if (watched == m_filter) {
            if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down || key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown) {
                QCoreApplication::sendEvent(m_list, event);
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                if (effortPage()) m_list->setFocus(); else use();
                return true;
            }
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
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                if (effortPage()) {
                    if (auto *row = currentRow())
                        if (auto *choice = m_list->itemWidget(row, ColReasoning)) choice->setFocus();
                }
                else use();
                return true;
            }
            if (!key->modifiers() && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
                stepRow(key->key() == Qt::Key_Down ? 1 : -1);
                return true;
            }
            if (key->key() == Qt::Key_Right) {                                   // → the providers, then the levels
                if (effortPage()) {
                    if (auto *row = currentRow())
                        if (auto *choice = m_list->itemWidget(row, ColReasoning)) choice->setFocus();
                    return true;
                }
                if (m_vias->isVisible() && m_vias->count()) { m_vias->setFocus(); return true; }
                if (m_levels->isVisible() && m_levels->count()) { m_levels->setFocus(); return true; }
            }
            if (key->key() == Qt::Key_Left) { m_filter->setFocus(); return true; }
        }
        if (watched == m_vias) {
            if (key->key() == Qt::Key_Left) { m_list->setFocus(); return true; }
            if (key->key() == Qt::Key_Right && m_levels->isVisible() && m_levels->count()) { m_levels->setFocus(); return true; }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                if (effortPage()) { if (m_levels->isVisible()) m_levels->setFocus(); }
                else use();
                return true;
            }
        }
        if (watched == m_levels) {
            if (key->key() == Qt::Key_Left) {
                if (m_vias->isVisible() && m_vias->count()) m_vias->setFocus(); else m_list->setFocus();
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { if (!effortPage()) use(); return true; }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ModelPicker::use() {
    // "+ add a model by id…" is a row you press, not a model you use: Enter on it asks for the id
    // and leaves the dialog open on what it added.
    if (QTreeWidgetItem *row = currentRow(); row != nullptr && row->data(0, AddByIdRole).toBool()) {
        promptAddModelById(row->data(0, AddByIdPresetRole).toString());
        return;
    }
    // Hosted in the models pane there is nothing to pick for: that pane picks no pane's model
    // (card #BXMS) — the pane's own model box does. Enter and a double click only highlight.
    if (m_hosted) return;
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
