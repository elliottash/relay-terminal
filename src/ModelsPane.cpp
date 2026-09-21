// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelsPane.h"

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
    m_tabs->setTabToolTip(2, QStringLiteral("Steps 3 and 4 — the five class lists, their order, levels and box cutoffs"));
    layout->addWidget(m_tabs);

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

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        showTab(m_tabs->tabData(index).toString());
    });
    m_tabs->installEventFilter(this);
    updateHeader();
    showTab(availableTab());
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
    m_picker->setHosted(true);
    m_picker->installEventFilter(this);
    m_picker->filter()->installEventFilter(this);
    m_picker->list()->installEventFilter(this);
    qobject_cast<QVBoxLayout *>(m_pickerPage->layout())->addWidget(m_picker);
    m_pendingFilter.clear();
}

void ModelsPane::setTarget(const Target &target) {
    const bool samePane = m_picker && !target.token.isEmpty() && target.token == m_target.token;
    const QString wantTier = target.tier.isEmpty() ? m_classTab : target.tier;
    m_target = target;
    // The served pane's mode picks the class tab when it *becomes* the served pane; a re-read of
    // the pane already served keeps whichever list is being looked at.
    if (!samePane && !wantTier.isEmpty() && wantTier != kAll) m_classTab = wantTier;
    if (samePane) {
        // The same pane again — Ctrl+Shift+M pressed twice from it, or a fresh catalog after a
        // `presets` answer. Its lists, its undo stack and whatever is typed in the filter stay.
        m_picker->rebuild();
    } else {
        buildPicker();
    }
    if (!samePane && currentTab() == prioritiesTab()) m_picker->setTier(m_classTab);
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

QString ModelsPane::tier() const { return m_picker ? m_picker->tier() : m_classTab; }

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
    m_pages->setCurrentWidget(m_pickerPage);
    if (!m_picker) return;
    if (id == availableTab()) {
        // Leaving priorities: remember which class list to come back to.
        if (m_picker->tier() != kAll) m_classTab = m_picker->tier();
        m_picker->setTier(kAll);
    } else {
        m_picker->setTier(m_classTab.isEmpty() ? QStringLiteral("main") : m_classTab);
    }
}

void ModelsPane::setFilter(const QString &text) {
    if (!m_picker) { m_pendingFilter = text; return; }
    m_picker->filter()->setText(text);
}

void ModelsPane::focusFilter() {
    if (currentTab() == providersTab()) { m_providers->focusSearch(); return; }
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

bool ModelsPane::handleShortcut(QKeyEvent *event) {
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (!(mods & Qt::ControlModifier)) return false;
    if (event->key() != Qt::Key_Tab && event->key() != Qt::Key_Backtab) return false;
    // Two rows of tabs, one key each: ←/→ are the class tabs of the priorities page (the picker's
    // own), ctrl+tab is these three. The picker steps aside for ctrl+tab while it is hosted.
    stepTab(event->key() == Qt::Key_Backtab || (mods & Qt::ShiftModifier) ? -1 : 1);
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
