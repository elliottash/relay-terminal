// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelsPane.h"
#include "PaneTabNavigation.h"
#include "Theme.h"           // the collapsed row's ink

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QToolButton>
#include <QTreeWidget>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {

namespace {
const QString kAll = QStringLiteral("all");
}  // namespace

// ----- the helper agent's context (owner, 2026-09-22) ------------------------------------------
//
// "there needs to be a helper agent on the model page." What the agent here is *about*, and
// nothing else: which of the five tabs is in front, what is typed in its filter, the pane this one
// serves, and on priorities the class the highlight is in. The console — the prompt box, the
// queue, the transcript — is a no-shell `Pane` the window builds, the same surface Options and
// Sessions embed (src/AgentContext.h). It resolves no link of its own: a `models:` link does not
// exist, and an `option:` one is the window's, which opens Options on the row.
class ModelsContext final : public agent::Context {
  public:
    explicit ModelsContext(ModelsPane *pane) : m_pane(pane) {}

    agent::ContextSpec spec() const override {
        agent::ContextSpec spec;
        spec.name = QStringLiteral("models");
        spec.surface = spec.name;
        spec.agentRole = QStringLiteral("switchboard");
        spec.workspace = m_workspace;
        // Named, never inferred: a board-less helper that falls through the worker's inference
        // gets the *pane* branch and the full executor (§33.3, card #AGNT finding).
        spec.scope = QStringLiteral("console");
        // `persist.scope` is a wire enum — "", "pane" or "helper" — not a path. No tab id means no
        // store, which is a supported state and how a pane with no window says so.
        if (!m_tabId.isEmpty()) {
            spec.persistScope = QStringLiteral("helper");
            spec.persistKey = m_tabId;
        }
        spec.briefKey = spec.name;
        spec.briefTitle = title();
        spec.screen = screen();
        spec.shell = false;          // every context but the terminal's
        spec.routing = QStringLiteral("agent");   // everything typed here is a prompt
        return spec;
    }

    QString placeholder() const override { return QStringLiteral("Ask the Models agent…"); }

    QString title() const { return QStringLiteral("Models agent"); }

    void setTabId(const QString &tabId) { if (tabId != m_tabId) { m_tabId = tabId; changed(); } }
    void setWorkspace(const QString &workspace) {
        if (workspace != m_workspace) { m_workspace = workspace; changed(); }
    }

  private:
    // What is being read right now, for the "On screen now:" line above the prompt (§33). A hint,
    // not a dump: the tab, the filter and the class or job the
    // highlight is on. The lists themselves are never pasted in.
    QString screen() const {
        QStringList lines;
        const QString tab = m_pane->currentTab();
        lines << QStringLiteral("Models › %1").arg(tab);
        const QString filter = m_pane->filterText().trimmed();
        if (!filter.isEmpty()) lines << QStringLiteral("Filter: %1").arg(filter);
        if (tab == ModelsPane::prioritiesTab() || tab == ModelsPane::effortTab()) {
            const QString tier = m_pane->tier();
            if (!tier.isEmpty()) lines << QStringLiteral("Class in focus: %1").arg(tier);
        } else if (tab == ModelsPane::jobsTab() && m_pane->jobs()) {
            const QString role = m_pane->jobs()->currentRole();
            if (!role.isEmpty()) lines << QStringLiteral("Job in focus: %1").arg(role);
        } else if (tab == ModelsPane::providersTab() && m_pane->providers()) {
            // The provider rows are Options › Models' own, so their ids are what app_option_get
            // takes — as many as fit, as the Options helper does.
            QStringList ids;
            int room = agent::kScreenLimit - lines.join(QLatin1Char('\n')).size() - 16;
            for (const QString &id : m_pane->providers()->visibleRowIds()) {
                room -= id.size() + 2;
                if (room <= 0) { ids << QStringLiteral("…"); break; }
                ids << id;
            }
            if (!ids.isEmpty()) lines << QStringLiteral("Rows: %1").arg(ids.join(QStringLiteral(", ")));
        }
        return lines.join(QLatin1Char('\n'));
    }

    ModelsPane *m_pane = nullptr;
    QString m_tabId, m_workspace;
};

// The collapsed row's mark, painted as Options and Sessions paint it (owner, 2026-09-20: "it should
// have a question mark icon next to it"): a font's "?" at 14 px is a third of the word beside it.
static QIcon askIcon(const QColor &ink) {
    constexpr int kSize = 14;
    QPixmap pixmap(kSize * 2, kSize * 2);        // 2x, so it stays crisp on a scaled desktop
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(ink, 1.2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(1.2, 1.2, 11.6, 11.6));
    painter.drawArc(QRectF(4.3, 3.4, 5.4, 4.6), 200 * 16, -250 * 16);
    painter.drawLine(QPointF(7.0, 7.4), QPointF(7.0, 9.0));
    QPen dot(ink, 1.6);
    dot.setCapStyle(Qt::RoundCap);
    painter.setPen(dot);
    painter.drawPoint(QPointF(7.0, 10.8));
    painter.end();
    return QIcon(pixmap);
}

ModelsPane::ModelsPane(std::function<QList<SettingsSection>()> sections, QWidget *parent)
    : QWidget(parent), m_sections(std::move(sections)) {
    setObjectName(QStringLiteral("modelsPane"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 8);
    layout->setSpacing(6);

    // One quiet line saying what this pane is for. It named the pane it served until card #BXMS,
    // when picking a pane's model left this pane for the pane's own box.
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
    const QStringList labels = {QStringLiteral("Sources"), QStringLiteral("Enabled"),
                                QStringLiteral("Pick order"), QStringLiteral("Effort"),
                                QStringLiteral("Agent jobs")};
    const QStringList ids = tabIds();
    for (int i = 0; i < ids.size(); ++i) {
        const QString &id = ids.at(i);
        const int index = m_tabs->addTab(labels.at(i));
        m_tabs->setTabData(index, id);
    }
    m_tabs->setTabToolTip(0, QStringLiteral("Manage model providers and their keys"));
    m_tabs->setTabToolTip(1, QStringLiteral("Choose which models are enabled"));
    m_tabs->setTabToolTip(2, QStringLiteral("Set the model pick order, fallbacks, ties, and box cutoff"));
    m_tabs->setTabToolTip(3, QStringLiteral("Set reasoning effort for each model"));
    m_tabs->setTabToolTip(4, QStringLiteral("Choose what Planning, Subagents, and system-pane agents run on"));
    layout->addWidget(m_tabs);
    relay::paneTabs::registerTabs(this, m_tabs);

    m_pages = new QStackedWidget;
    layout->addWidget(m_pages, 1);

    // ----- the providers page: Options' own renderer over Options' own section -------------------
    m_providersPage = new QWidget;
    auto *providersBox = new QVBoxLayout(m_providersPage);
    providersBox->setContentsMargins(0, 0, 0, 0);
    m_providers = new SettingsPane(SettingsPane::Mode::Options,
                                   [this] {
                                       QList<SettingsSection> sections = m_sections ? m_sections() : QList<SettingsSection>();
                                       // The introductory paragraph is useful in Options, but
                                       // occupies most of a narrow Models pane before the first
                                       // provider row. The tab and its controls explain the page.
                                       for (SettingsSection &section : sections) section.blurb.clear();
                                       return sections;
                                   },
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

    // The helper agent, collapsed to one row at the bottom right under all five tabs — not inside
    // the providers page, whose embedded SettingsPane is never given a factory and so draws no row
    // of its own. The context outlives the console, which is what §33 requires of a host.
    m_context = new ModelsContext(this);
    buildHelperRow(layout);

    updateHeader();
    // Priorities is the tab a pick is made on, so it is where the key lands by default; the window
    // asks for providers on a first run, where there is nothing to pick yet.
    showTab(prioritiesTab());
}

// The picker, built around the target this pane serves now. It is rebuilt rather than reconfigured
// on a re-target because its Context is the served pane — the catalog, the model, the level and
// its undo stack holds edits made while that pane was in front. Defaults are in Options.
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
    m_picker = new ModelPicker(context, m_pickerPage);
    // Set before setHosted(), which redraws the footer: the footer only offers "esc back to the
    // pane" where Escape has somewhere to go.
    m_picker->onEscape = [this] { if (m_target.focusBack) m_target.focusBack(); };
    // No `onUse`: the picker then offers no "use" and Enter picks nothing (card #BXMS).
    m_picker->onListsChanged = [this] { if (m_target.listsChanged) m_target.listsChanged(); };
    m_picker->openModelsPage = [this] { showTab(providersTab()); };
    m_picker->setHosted(true);
    m_picker->installEventFilter(this);
    m_picker->filter()->installEventFilter(this);
    m_picker->list()->installEventFilter(this);
    qobject_cast<QVBoxLayout *>(m_pickerPage->layout())->addWidget(m_picker);
    m_pendingFilter.clear();
}

void ModelsPane::setTarget(const Target &target) {
    // A re-read keeps the picker; a re-target rebuilds it. So does a target that has gained or
    // Defaults are managed in Options, so gaining them does not rebuild the picker.
    const bool samePane = m_picker && !target.token.isEmpty() && target.token == m_target.token;
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
    if (!samePane && (currentTab() == prioritiesTab() || currentTab() == effortTab())) {
        m_picker->setTier(currentTab() == effortTab() ? ModelPicker::effortTier() : ModelPicker::classesTier());
        m_picker->focusClass(m_classTab);
    }
    updateHeader();
    // A different pane is served, so what `spec()` answers for `screen` moved with it.
    if (!samePane) helperScreenMoved();
}

void ModelsPane::setRoleSummaries(const QJsonObject &roles, const QJsonObject &tiers) {
    if (roles == m_target.roleSummary && tiers == m_target.tierSummary) return;
    m_target.roleSummary = roles;
    m_target.tierSummary = tiers;
    JobsTab::Data jobsData;
    jobsData.catalog = m_target.catalog;
    jobsData.roles = roles;
    jobsData.tiers = tiers;
    jobsData.now = m_target.now;
    jobsData.rolesChanged = m_target.rolesChanged;
    jobsData.focusBack = m_target.focusBack;
    m_jobs->setData(jobsData);
    if (currentTab() == jobsTab()) helperScreenMoved();
}

void ModelsPane::updateHeader() {
    // Settings for every pane, said once (card #BXMS): a pane's own model is its box's to pick.
    m_header->setText(QStringLiteral("Shared model settings · choose an individual pane's active model in its model box"));
    m_header->setToolTip(QStringLiteral("Sources, enabled models, pick order, effort and job routing apply across Relay. "
                                        "To change one pane's active model, use its model box."));
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
    const bool enteringSources = id == providersTab() && currentTab() != id;
    if (m_tabs->currentIndex() != index) {
        const QSignalBlocker block(m_tabs);
        m_tabs->setCurrentIndex(index);
    }
    // The page under the helper moves, so what `spec()` answers for `screen` moves with it.
    helperScreenMoved();
    if (id == providersTab()) {
        m_pages->setCurrentWidget(m_providersPage);
        if (enteringSources && onSourcesShown) onSourcesShown();
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
        if (m_picker->tier() != kAll) m_classTab = m_picker->currentClass();
        // One page, four sections — no class tabs (owner, 2026-09-21: "in a pane, i dont want
        // separate tabs for the modes. they should just be in divided sections"). The class the
        // served pane is in is where the highlight lands, which is what opening on its tab was.
        m_picker->setTier(id == effortTab() ? ModelPicker::effortTier() : ModelPicker::classesTier());
        m_picker->focusClass(m_classTab.isEmpty() ? QStringLiteral("main") : m_classTab);
    }
}

QString ModelsPane::filterText() const {
    const QString tab = currentTab();
    if (tab == providersTab()) return m_providers ? m_providers->search() : QString();
    if (tab == jobsTab()) return QString();
    return m_picker ? m_picker->filter()->text() : m_pendingFilter;
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

void ModelsPane::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // An ask row that does nothing is worse than no ask row (SettingsPane's rule), so the row is
    // there only once the window has given this pane a way to build a console. A library with no
    // window — the tests — simply has no helper.
    if (m_helper) m_helper->setVisible(bool(onCreateConsole));
}

void ModelsPane::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    const bool narrow = width() < 500;
    m_tabs->setTabText(0, QStringLiteral("Sources"));
    m_tabs->setTabText(1, QStringLiteral("Enabled"));
    m_tabs->setTabText(2, narrow ? QStringLiteral("Order") : QStringLiteral("Pick order"));
    m_tabs->setTabText(3, QStringLiteral("Effort"));
    m_tabs->setTabText(4, narrow ? QStringLiteral("Roles") : QStringLiteral("Agent jobs"));
    updateConsoleHeight();
}

// ----- the helper agent (owner, 2026-09-22) ------------------------------------------------------
//
// SettingsPane's helper, member for member (src/SettingsPane.cpp): the pane owns the *context* and
// the row; the window, the only place a `Pane` can be made, turns the context into a console
// through `onCreateConsole` on the first expand.

ModelsPane::~ModelsPane() {
    // The console's wrapper clears the context's callback when the console goes, so the console
    // must go first. Children are only destroyed in ~QWidget, after this body, so the foot of the
    // pane — and the console inside it — is deleted here, before the context (~BoardView's rule).
    delete m_helper;
    m_helper = nullptr;
    delete m_context;
}

relay::agent::Context *ModelsPane::agentContext() const { return m_context; }

void ModelsPane::setHelperTabId(const QString &tabId) {
    if (m_context) m_context->setTabId(tabId);
}

void ModelsPane::setHelperWorkspace(const QString &workspace) {
    if (m_context) m_context->setWorkspace(workspace);
}

void ModelsPane::setHelperShortcut(const QString &hintId, const QString &keys) {
    m_askHintId = hintId;
    m_askKeys = keys;
    updateHelperRow();
}

void ModelsPane::helperScreenMoved() {
    if (m_context) m_context->changed();
}

// Asking for the cursor is asking for the console: Alt+Q, a click on the row and a drafted request
// all mean the box has to be there to type in. Built here, on the first expand, so a Models pane
// nobody asks anything never pays for one.
void ModelsPane::focusHelper() {
    if (!onCreateConsole) return;
    ensureConsole();
    if (m_helperCollapsed) {
        m_helperCollapsed = false;
        applyHelperCollapsed();
    }
    if (m_console.focusComposer) m_console.focusComposer();
}

// A draft, never a send (owner, 2026-09-19: "draft you confirm").
void ModelsPane::helperDraft(const QString &text) {
    focusHelper();
    if (m_console.draftInComposer) m_console.draftInComposer(text);
}

// The row the pane is until the helper is asked for, and the fold back to it — the same object
// names as Options' and Sessions', so the theme's stylesheet draws all three alike.
void ModelsPane::buildHelperRow(QVBoxLayout *into) {
    m_helper = new QWidget(this);
    m_helper->setObjectName(QStringLiteral("boardChatPanel"));
    m_helper->setAttribute(Qt::WA_StyledBackground);
    auto *outer = new QVBoxLayout(m_helper);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- collapsed: one button at the bottom right ------------------------------------------
    m_askRow = new QWidget(m_helper);
    m_askRow->setObjectName(QStringLiteral("boardChatAskRow"));
    auto *askLine = new QHBoxLayout(m_askRow);
    askLine->setContentsMargins(10, 6, 10, 6);
    askLine->setSpacing(6);
    askLine->addStretch(1);
    m_ask = new QToolButton(m_askRow);
    m_ask->setObjectName(QStringLiteral("boardChatAsk"));
    m_ask->setIcon(askIcon(theme::TextMuted));
    m_ask->setIconSize(QSize(14, 14));
    m_ask->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_ask->setCursor(Qt::PointingHandCursor);
    m_ask->setFocusPolicy(Qt::StrongFocus);
    m_ask->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    askLine->addWidget(m_ask, 0);
    outer->addWidget(m_askRow);
    connect(m_ask, &QToolButton::clicked, this, [this] {
        // The mouse, not the key: Alt+Q comes in through `focusHelper()` and teaches nothing.
        if (onHelperHint && !m_askHintId.isEmpty()) onHelperHint();
        focusHelper();
    });

    // ---- expanded: the console, under a row that folds it away ------------------------------
    m_helperBody = new QWidget(m_helper);
    m_helperBody->setObjectName(QStringLiteral("boardChatBody"));
    auto *body = new QVBoxLayout(m_helperBody);
    body->setContentsMargins(10, 8, 10, 8);
    body->setSpacing(6);
    auto *headRow = new QHBoxLayout;
    headRow->setSpacing(6);
    m_helperHead = new QLabel(m_helperBody);
    m_helperHead->setObjectName(QStringLiteral("boardChatHead"));
    headRow->addWidget(m_helperHead, 0);
    headRow->addStretch(1);
    auto *fold = new QToolButton(m_helperBody);
    fold->setObjectName(QStringLiteral("boardChatFold"));
    fold->setText(QStringLiteral("⌄"));
    fold->setToolTip(QStringLiteral("Fold the helper back to one row. The conversation is kept — "
                                    "it opens where you left it."));
    fold->setCursor(Qt::PointingHandCursor);
    fold->setFocusPolicy(Qt::NoFocus);
    fold->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    headRow->addWidget(fold, 0);
    connect(fold, &QToolButton::clicked, this, [this] {
        if (m_helperCollapsed) return;
        m_helperCollapsed = true;
        applyHelperCollapsed();
    });
    body->addLayout(headRow);
    m_helperBody->setVisible(false);
    outer->addWidget(m_helperBody);

    into->addWidget(m_helper, 0);
    updateHelperRow();
}

void ModelsPane::ensureConsole() {
    if (m_console || !onCreateConsole || m_helperBody == nullptr) return;
    m_console = onCreateConsole(m_context, m_helperBody);
    if (!m_console) return;
    if (auto *body = qobject_cast<QVBoxLayout *>(m_helperBody->layout())) body->addWidget(m_console.widget, 1);
    m_console.widget->show();
    updateConsoleHeight();
}

// One row, or the row and the console. Nothing is destroyed either way: a helper folded back keeps
// its conversation, its draft and its queue.
void ModelsPane::applyHelperCollapsed() {
    if (m_askRow) m_askRow->setVisible(m_helperCollapsed);
    // Folding with the cursor in the composer: hand the focus to the row first, or Qt passes it on
    // as a Tab — onto the ask row, which would unfold what was just folded.
    if (m_helperCollapsed && m_ask && m_helperBody
        && m_helperBody->isAncestorOf(QApplication::focusWidget()))
        m_ask->setFocus(Qt::OtherFocusReason);
    if (m_helperBody) m_helperBody->setVisible(!m_helperCollapsed);
    if (m_console.setCollapsed) m_console.setCollapsed(m_helperCollapsed);
    if (!m_helperCollapsed) updateConsoleHeight();
}

// "Helper Agent (Alt+Q)" — the live key in the button's own text.
void ModelsPane::updateHelperRow() {
    if (m_ask) {
        m_ask->setText(m_askKeys.isEmpty() ? QStringLiteral("Agent")
                                           : QStringLiteral("Agent (%1)").arg(m_askKeys));
        m_ask->setToolTip(m_askKeys.isEmpty()
            ? QStringLiteral("Ask the Models agent about models and routing.")
            : QStringLiteral("Ask the Models agent about models and routing (%1).").arg(m_askKeys));
    }
    if (m_helperHead && m_context) m_helperHead->setText(m_context->title());
}

// At most ~40 % of the pane, and never so little that the transcript is a slot: SettingsPane's rule.
void ModelsPane::updateConsoleHeight() {
    if (m_helperBody == nullptr) return;
    const int line = QFontMetrics(font()).lineSpacing();
    const int cap = std::max(10 * line, height() * 2 / 5);
    m_helperBody->setMaximumHeight(cap);
    m_helperBody->setMinimumHeight(std::min(cap, 20 * line));
}

}  // namespace relay
