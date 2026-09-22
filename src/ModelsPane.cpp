// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelsPane.h"
#include "PaneTabNavigation.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

namespace relay {

namespace {
const QString kAll = QStringLiteral("all");
}  // namespace

ModelsPane::ModelsPane(std::function<QList<SettingsSection>()> sections, QWidget *parent)
    : QWidget(parent), m_sections(std::move(sections)) {
    setObjectName(QStringLiteral("modelsPane"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 8);
    layout->setSpacing(6);

    // "a header line naming the pane it serves" (design 5.8). One pane is served at a time and
    // which one is the whole reason Enter here changes something over there, so it is said in
    // words rather than left to the splitter's geometry.
    m_header = new QLabel;
    m_header->setObjectName(QStringLiteral("modelsPaneFor"));
    m_header->setTextFormat(Qt::PlainText);
    m_header->setWordWrap(true);
    {
        QPalette quiet = m_header->palette();
        quiet.setColor(QPalette::WindowText, palette().color(QPalette::Disabled, QPalette::Text));
        m_header->setPalette(quiet);
    }
    layout->addWidget(m_header);

    m_tabs = new QTabBar;
    m_tabs->setObjectName(QStringLiteral("modelsPaneTabs"));
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(true);
    m_tabs->setFocusPolicy(Qt::StrongFocus);
    for (const QString &id : tabIds()) {
        const int index = m_tabs->addTab(id);
        m_tabs->setTabData(index, id);
    }
    m_tabs->setTabToolTip(0, QStringLiteral("Step 1 — the providers this machine can reach, and their keys"));
    m_tabs->setTabToolTip(1, QStringLiteral("Step 2 — which models exist for the lists, the box and its filter"));
    m_tabs->setTabToolTip(2, QStringLiteral("Steps 3 and 4 — one section per class: its order, levels and box cutoffs"));
    m_tabs->setTabToolTip(3, QStringLiteral("What each job relay does runs on right now, and a model of its own for one"));
    layout->addWidget(m_tabs);
    relay::paneTabs::registerTabs(this, m_tabs);

    m_pages = new QStackedWidget;
    layout->addWidget(m_pages, 1);

    // ----- the providers page: Options' own renderer over Options' own section -------------------
    m_providersPage = new QWidget;
    auto *providersBox = new QVBoxLayout(m_providersPage);
    providersBox->setContentsMargins(0, 0, 0, 0);
    m_providers = new SettingsPane(SettingsPane::Mode::Options,
                                   [this] { return m_sections ? m_sections() : QList<SettingsSection>(); },
                                   [] { return QList<ActionItem>(); });
    m_providers->setEmbedded(true);
    m_providers->onClose = [this] { if (m_target.focusBack) m_target.focusBack(); };
    providersBox->addWidget(m_providers);
    m_pages->addWidget(m_providersPage);

    // ----- the picker page: available *and* priorities are one widget ---------------------------
    m_pickerPage = new QWidget;
    auto *pickerBox = new QVBoxLayout(m_pickerPage);
    pickerBox->setContentsMargins(0, 0, 0, 0);
    m_pages->addWidget(m_pickerPage);
    buildPicker();

    // ----- the jobs page: one row per job, grouped by the tier it follows (design 5.9) ----------
    // A fixed table of rows over the worker's last `model_roles`, so unlike the picker it is never
    // rebuilt on a re-target: `setTarget` hands it the new report and it redraws its own cells.
    m_jobs = new JobsTab;
    m_pages->addWidget(m_jobs);
    m_jobs->installEventFilter(this);
    m_jobs->list()->installEventFilter(this);

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        showTab(m_tabs->tabData(index).toString());
    });
    m_tabs->installEventFilter(this);
    updateHeader();
    // Priorities is the tab a pick is made on, so it is where the key lands by default; the window
    // asks for providers on a first run, where there is nothing to pick yet.
    showTab(prioritiesTab());
}

// The picker, built around the target this pane serves now. It is rebuilt rather than reconfigured
// on a re-target because its Context is the served pane — the catalog, the model, the level and
// the "fill from defaults" action are all that pane's, and its undo stack holds edits made while
// that pane was in front.
void ModelsPane::buildPicker() {
    const QString wantTier = m_picker ? m_picker->tier() : (m_target.tier.isEmpty() ? m_classTab : m_target.tier);
    const QString wantFilter = m_picker ? m_picker->filter()->text() : m_pendingFilter;
    if (m_picker) { m_picker->deleteLater(); m_picker = nullptr; }

    ModelPicker::Context context;
    context.catalog = m_target.catalog;
    context.currentKey = m_target.currentKey;
    context.currentEffort = m_target.currentEffort;
    context.tier = wantTier.isEmpty() ? QStringLiteral("main") : wantTier;
    context.filter = wantFilter;
    context.now = m_target.now;
    context.fillFromDefaults = m_target.fillFromDefaults;
    m_picker = new ModelPicker(context, m_pickerPage);
    // Set before setHosted(), which redraws the footer: the footer only offers "esc back to the
    // pane" where Escape has somewhere to go.
    m_picker->onEscape = [this] { if (m_target.focusBack) m_target.focusBack(); };
    m_picker->onUse = [this](const ModelPick &pick) {
        if (!pick.accepted || !m_target.use) return;
        m_target.use(pick.key, pick.effort);
    };
    m_picker->onListsChanged = [this] { if (m_target.listsChanged) m_target.listsChanged(); };
    m_picker->openModelsPage = [this] { showTab(providersTab()); };
    m_pickerHasFill = bool(m_target.fillFromDefaults);
    m_picker->setHosted(true);
    m_picker->installEventFilter(this);
    m_picker->filter()->installEventFilter(this);
    m_picker->list()->installEventFilter(this);
    qobject_cast<QVBoxLayout *>(m_pickerPage->layout())->addWidget(m_picker);
    m_pendingFilter.clear();
}

void ModelsPane::setTarget(const Target &target) {
    // A re-read keeps the picker; a re-target rebuilds it. So does a target that has gained or
    // lost "fill from defaults", because those two buttons exist only if the Context carried the
    // action when the widget was made — a models pane restored with a layout is pointed at a pane
    // whose worker has not answered `presets` yet, so it never had them until now.
    const bool samePane = m_picker && !target.token.isEmpty() && target.token == m_target.token
                          && bool(target.fillFromDefaults) == m_pickerHasFill;
    const QString wantTier = target.tier.isEmpty() ? m_classTab : target.tier;
    m_target = target;
    // The served pane's mode picks the class tab when it *becomes* the served pane; a re-read of
    // the pane already served keeps whichever list is being looked at.
    if (!samePane && !wantTier.isEmpty() && wantTier != kAll) m_classTab = wantTier;
    {
        JobsTab::Data jobsData;
        jobsData.catalog = m_target.catalog;
        jobsData.roles = m_target.roleSummary;
        jobsData.tiers = m_target.tierSummary;
        jobsData.now = m_target.now;
        jobsData.rolesChanged = m_target.rolesChanged;
        jobsData.focusBack = m_target.focusBack;
        m_jobs->setData(jobsData);
    }
    if (samePane) {
        // The same pane again — Ctrl+Shift+M pressed twice from it, or a fresh catalog after a
        // `presets` answer. Its lists, its undo stack and whatever is typed in the filter stay,
        // but the catalog is read again: on a first run there were no providers at all when this
        // pane was built, and the rows only exist once the worker has answered.
        m_picker->setCatalog(m_target.catalog, m_target.currentKey, m_target.currentEffort, m_target.now);
    } else {
        buildPicker();
    }
    if (!samePane && currentTab() == prioritiesTab()) {
        m_picker->setTier(ModelPicker::classesTier());
        m_picker->focusClass(m_classTab);
    }
    updateHeader();
}

void ModelsPane::updateHeader() {
    const QString who = m_target.title.trimmed();
    m_header->setText(who.isEmpty()
        ? QStringLiteral("for: no pane — enter would switch nothing")
        : QStringLiteral("for: %1").arg(who));
    m_header->setToolTip(who.isEmpty()
        ? QStringLiteral("This pane is not serving anything. Focus a pane and press the models key again.")
        : QStringLiteral("Enter, “use” or a double click switches %1 to the highlighted model and level.").arg(who));
}

QString ModelsPane::currentTab() const {
    const int index = m_tabs->currentIndex();
    return index < 0 ? QString() : m_tabs->tabData(index).toString();
}

// Which class list the priorities page is *in*. It was the class tab in front; since the page
// became one scrolling set of sections (card #MDL1, owner 2026-09-21) it is the class of the row
// under the highlight, which is the same question and the same answer.
QString ModelsPane::tier() const {
    if (!m_picker) return m_classTab;
    // The flat tab is not a class and answers as itself; the priorities page answers the class the
    // highlight is in, which is what the class tab in front used to answer.
    return m_picker->tier() == kAll ? kAll : m_picker->currentClass();
}

void ModelsPane::showTab(const QString &id) {
    const int index = tabIds().indexOf(id);
    if (index < 0) return;
    if (m_tabs->currentIndex() != index) {
        const QSignalBlocker block(m_tabs);
        m_tabs->setCurrentIndex(index);
    }
    if (id == providersTab()) {
        m_pages->setCurrentWidget(m_providersPage);
        return;
    }
    if (id == jobsTab()) {
        m_pages->setCurrentWidget(m_jobs);
        return;
    }
    m_pages->setCurrentWidget(m_pickerPage);
    if (!m_picker) return;
    if (id == availableTab()) {
        // Leaving priorities: remember which class the highlight was in, to come back to it.
        if (m_picker->tier() != kAll) m_classTab = m_picker->currentClass();
        m_picker->setTier(kAll);
    } else {
        // One page, four sections — no class tabs (owner, 2026-09-21: "in a pane, i dont want
        // separate tabs for the modes. they should just be in divided sections"). The class the
        // served pane is in is where the highlight lands, which is what opening on its tab was.
        m_picker->setTier(ModelPicker::classesTier());
        m_picker->focusClass(m_classTab.isEmpty() ? QStringLiteral("main") : m_classTab);
    }
}

void ModelsPane::setFilter(const QString &text) {
    if (!m_picker) { m_pendingFilter = text; return; }
    m_picker->filter()->setText(text);
}

void ModelsPane::focusFilter() {
    if (currentTab() == providersTab()) { m_providers->focusSearch(); return; }
    // The jobs tab has no filter line: its rows are a fixed table, so the keyboard lands on the
    // list itself and ↑↓ walk the jobs straight away.
    if (currentTab() == jobsTab()) { m_jobs->focusList(); return; }
    if (m_picker) m_picker->filter()->setFocus(Qt::OtherFocusReason);
}

void ModelsPane::setHeaderRightInset(int pixels) {
    // The pane chrome's buttons sit over the top-right corner, which is this pane's header line.
    m_header->setContentsMargins(0, 0, pixels, 0);
}

void ModelsPane::stepTab(int delta) {
    const QStringList ids = tabIds();
    const int at = ids.indexOf(currentTab());
    if (at < 0) return;
    showTab(ids.at((at + delta % ids.size() + ids.size()) % ids.size()));
    focusFilter();
}

// Left/Right retain the existing pane navigation wherever a text caret or model row
// does not need them. Tab navigation is registered with the shared pane convention.
bool ModelsPane::handleShortcut(QKeyEvent *event) {
    const Qt::KeyboardModifiers mods = event->modifiers();
    const int key = event->key();
    if (mods != Qt::NoModifier || (key != Qt::Key_Left && key != Qt::Key_Right)) return false;
    // Only where nothing below is using them: in a filter line with text in it they are the
    // caret's, and a row's → opens its providers (the picker answers both itself).
    if (currentTab() == jobsTab()) return false;   // ←/→ are the tree's own column keys
    if (m_picker && m_picker->list()->hasFocus()) return false;
    if (m_picker && m_picker->filter()->hasFocus() && !m_picker->filter()->text().isEmpty()) return false;
    stepTab(key == Qt::Key_Left ? -1 : 1);
    return true;
}

bool ModelsPane::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (handleShortcut(key)) return true;
        // Escape anywhere in the pane hands the focus back and leaves the pane open (design 5.8).
        // The picker answers it on its own controls; this catches the rest of the widget tree.
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier && m_target.focusBack) {
            m_target.focusBack();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ModelsPane::keyPressEvent(QKeyEvent *event) {
    if (handleShortcut(event)) { event->accept(); return; }
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier && m_target.focusBack) {
        m_target.focusBack();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

}  // namespace relay
