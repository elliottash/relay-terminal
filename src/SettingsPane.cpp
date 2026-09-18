// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsPane.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace relay {

namespace {

// A row that toggles when clicked anywhere on it, not only on the 14 px box at its right.
class ClickRow final : public QFrame {
public:
    std::function<void()> onClick;
protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && onClick) onClick();
        QFrame::mouseReleaseEvent(event);
    }
};

void repolish(QWidget *widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QLabel *mutedLabel(const QString &text, const char *name) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setObjectName(QLatin1String(name));
    return label;
}

QLabel *keyCap(const QString &text) {
    auto *chip = new QLabel(text);
    chip->setObjectName(QStringLiteral("keyCap"));
    chip->setTextFormat(Qt::PlainText);
    return chip;
}

constexpr int kRecentRows = 6;
constexpr int kMaxResults = 60;

}  // namespace

// ----- construction ------------------------------------------------------------------------------

SettingsPane::SettingsPane(std::function<QList<SettingsSection>()> sections,
                           std::function<QList<ActionItem>()> actions, QWidget *parent)
    : QWidget(parent), m_sections(std::move(sections)), m_actions(std::move(actions)) {
    setObjectName(QStringLiteral("settingsPane"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 8);
    layout->setSpacing(8);

    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("SETTINGS"));
    title->setObjectName(QStringLiteral("settingsTitle"));
    header->addWidget(title);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("settingsSearch"));
    m_search->setPlaceholderText(QStringLiteral("Search settings and actions"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(QStringLiteral("Search settings and actions"));
    m_search->installEventFilter(this);
    header->addWidget(m_search, 1);
    // No close button of its own: the pane's × in the chrome row closes it, as it closes every
    // other pane. It used to keep the ✕ from its days as an overlay, and the two landed on top of
    // each other in the top-right corner (owner report, 2026-09-18).
    m_header = header;
    layout->addLayout(header);

    m_tabs = new QTabBar;
    m_tabs->setObjectName(QStringLiteral("settingsTabs"));
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideNone);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_tabs);

    m_pages = new QStackedWidget;
    layout->addWidget(m_pages, 1);

    m_footer = new QLabel(QStringLiteral("↑ ↓ move   ·   Enter changes or runs   ·   ← → tabs   ·   Esc closes"));
    m_footer->setObjectName(QStringLiteral("settingsFooter"));
    m_footer->setTextFormat(Qt::PlainText);
    layout->addWidget(m_footer);

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (m_building || index < 0) return;
        m_wantedTab = m_tabIds.value(index);
        if (m_search->text().trimmed().isEmpty()) {
            m_pages->setCurrentIndex(index);
            m_rows = m_pageRows.value(m_pages->widget(index));
            setCurrent(-1, false);
        }
    });
    connect(m_search, &QLineEdit::textChanged, this, [this] {
        const QString needle = m_search->text().trimmed();
        if (needle.isEmpty()) {
            const int index = std::max(0, m_tabs->currentIndex());
            m_pages->setCurrentIndex(index);
            m_rows = m_pageRows.value(m_pages->widget(index));
            setCurrent(-1, false);
            return;
        }
        buildResults(needle);
    });
    build();
}

// ----- catalog → widgets -------------------------------------------------------------------------

void SettingsPane::build() {
    m_building = true;
    const QString tab = m_wantedTab.isEmpty() ? m_tabIds.value(m_tabs->currentIndex()) : m_wantedTab;
    m_sectionCache = m_sections ? m_sections() : QList<SettingsSection>();
    m_actionCache = m_actions ? m_actions() : QList<ActionItem>();
    m_pageRows.clear();
    m_groups.clear();
    m_rows.clear();
    m_current = -1;
    while (m_pages->count() > 0) {
        QWidget *page = m_pages->widget(0);
        m_pages->removeWidget(page);
        page->deleteLater();
    }
    {
        const QSignalBlocker blocker(m_tabs);
        while (m_tabs->count() > 0) m_tabs->removeTab(0);
    }
    m_tabIds.clear();
    for (const SettingsSection &section : std::as_const(m_sectionCache)) {
        m_tabIds << section.id;
        m_tabs->addTab(section.title);
        auto *scroll = new QScrollArea;
        scroll->setObjectName(QStringLiteral("settingsPage"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body = new QWidget;
        body->setObjectName(QStringLiteral("settingsPageBody"));
        // Exactly as wide as the viewport, never wider: a long combo label must squeeze and a
        // long row label must wrap, or the controls at the right edge end up clipped.
        body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_rows.clear();
        buildPage(body, section);
        scroll->setWidget(body);
        m_pages->addWidget(scroll);
        m_pageRows.insert(scroll, m_rows);
    }
    m_results = new QScrollArea;
    m_results->setObjectName(QStringLiteral("settingsPage"));
    m_results->setWidgetResizable(true);
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pages->addWidget(m_results);
    m_rows.clear();

    int index = std::max(0, m_tabIds.indexOf(tab));
    {
        const QSignalBlocker blocker(m_tabs);
        m_tabs->setCurrentIndex(index);
    }
    m_wantedTab = m_tabIds.value(index);
    m_building = false;
    const QString needle = m_search->text().trimmed();
    if (needle.isEmpty()) {
        m_pages->setCurrentIndex(index);
        m_rows = m_pageRows.value(m_pages->widget(index));
        setCurrent(-1, false);
    } else {
        buildResults(needle);
    }
}

void SettingsPane::buildPage(QWidget *page, const SettingsSection &section) {
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(2, 6, 8, 12);
    layout->setSpacing(2);
    if (!section.blurb.isEmpty()) {
        layout->addWidget(mutedLabel(section.blurb, "settingsBlurb"));
        layout->addSpacing(6);
    }
    for (const SettingRow &row : section.rows) layout->addWidget(settingRow(row));
    if (section.id == actionsTabId()) addActionsList(layout);
    layout->addStretch(1);
}

QWidget *SettingsPane::groupHeader(const QString &text, const QString &key) {
    auto *label = mutedLabel(text.toUpper(), "settingsHeading");
    if (!key.isEmpty()) m_groups.insert(key, label);
    return label;
}

// The actions with their keys, under the shortcut settings: Recent first, then each section in
// the order the caller listed them, submenus opened inline under their own header.
void SettingsPane::addActionsList(QVBoxLayout *into) {
    into->addSpacing(8);
    const QStringList recent = QSettings().value(QStringLiteral("palette/recent")).toStringList();
    QList<ActionItem> recentItems;
    for (const QString &key : recent) {
        for (const ActionItem &item : std::as_const(m_actionCache))
            if (item.key == key && !item.children && recentItems.size() < kRecentRows) recentItems << item;
    }
    if (!recentItems.isEmpty()) {
        into->addWidget(groupHeader(QStringLiteral("Recent")));
        for (const ActionItem &item : std::as_const(recentItems)) into->addWidget(actionRow(item));
    }
    QStringList order;
    for (const ActionItem &item : std::as_const(m_actionCache))
        if (!order.contains(item.section)) order << item.section;
    for (const QString &section : std::as_const(order)) {
        into->addWidget(groupHeader(section));
        for (const ActionItem &item : std::as_const(m_actionCache)) {
            if (item.section != section) continue;
            if (!item.children) { into->addWidget(actionRow(item)); continue; }
            into->addWidget(groupHeader(item.label + (item.detail.isEmpty() ? QString() : QStringLiteral("  ·  ") + item.detail), item.key));
            for (const ActionItem &child : item.children()) into->addWidget(actionRow(child));
        }
    }
}

QWidget *SettingsPane::settingRow(const SettingRow &row) {
    if (row.kind == SettingRow::Heading) return groupHeader(row.label);
    if (row.kind == SettingRow::Info) {
        auto *info = mutedLabel(row.label, "settingsInfo");
        info->setContentsMargins(8, 4, 8, 4);
        return info;
    }
    auto *line = new ClickRow;
    line->setObjectName(QStringLiteral("settingsRow"));
    line->setAttribute(Qt::WA_StyledBackground);
    line->setProperty("rowId", row.id);
    auto *box = new QHBoxLayout(line);
    box->setContentsMargins(10, 6, 10, 6);
    box->setSpacing(12);
    auto *text = new QVBoxLayout;
    text->setSpacing(1);
    auto *label = new QLabel(row.label);
    label->setObjectName(QStringLiteral("settingsRowLabel"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    text->addWidget(label);
    if (!row.detail.isEmpty()) text->addWidget(mutedLabel(row.detail, "settingsRowDetail"));
    box->addLayout(text, 1);

    // Every control writes through the row's callback and then asks for a rebuild, so rows that
    // describe other rows ("Flash · glm-5.3-flash") never go stale. Focus and scroll survive it.
    auto after = [this] { QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection); };
    Row entry;
    entry.id = row.id;
    entry.widget = line;
    switch (row.kind) {
    case SettingRow::Toggle: {
        auto *check = new QCheckBox;
        check->setChecked(row.checked);
        check->setAccessibleName(row.label);
        check->setFocusPolicy(Qt::TabFocus);
        connect(check, &QCheckBox::toggled, this, [fn = row.onToggle, after](bool on) {
            if (fn) fn(on);
            after();
        });
        line->onClick = [check] { check->toggle(); };
        line->setCursor(Qt::PointingHandCursor);
        entry.activate = [check] { check->toggle(); };
        box->addWidget(check);
        break;
    }
    case SettingRow::Choice: {
        auto *combo = new QComboBox;
        combo->setAccessibleName(row.label);
        combo->setFocusPolicy(Qt::TabFocus);
        // Sized to its entries but capped, so a long option label (a voice model with its
        // price) cannot squeeze the label column; the popup still shows every entry in full.
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setMaximumWidth(340);
        for (int i = 0; i < row.options.size(); ++i)
            combo->addItem(i < row.optionLabels.size() ? row.optionLabels.at(i) : row.options.at(i), row.options.at(i));
        const int index = combo->findData(row.current);
        if (index >= 0) combo->setCurrentIndex(index);
        connect(combo, QOverload<int>::of(&QComboBox::activated), this, [combo, fn = row.onChoose, after](int i) {
            if (fn) fn(combo->itemData(i).toString());
            after();
        });
        entry.activate = [combo] { combo->setFocus(Qt::OtherFocusReason); combo->showPopup(); };
        box->addWidget(combo);
        break;
    }
    case SettingRow::Text: {
        auto *edit = new QLineEdit(row.text);
        edit->setPlaceholderText(row.placeholder);
        edit->setAccessibleName(row.label);
        edit->setMinimumWidth(200);
        edit->setMaximumWidth(360);
        connect(edit, &QLineEdit::editingFinished, this, [edit, fn = row.onText, after, was = row.text] {
            if (edit->text().trimmed() == was) return;
            if (fn) fn(edit->text().trimmed());
            after();
        });
        entry.activate = [edit] { edit->setFocus(Qt::OtherFocusReason); edit->selectAll(); };
        box->addWidget(edit);
        break;
    }
    case SettingRow::Number: {
        auto *spin = new QSpinBox;
        spin->setRange(row.minimum, row.maximum);
        spin->setValue(row.number);
        spin->setAccessibleName(row.label);
        spin->setKeyboardTracking(false);   // one write per finished edit or arrow press, not per keystroke
        if (!row.suffix.isEmpty()) spin->setSuffix(row.suffix);
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [fn = row.onNumber](int value) { if (fn) fn(value); });
        entry.activate = [spin] { spin->setFocus(Qt::OtherFocusReason); spin->selectAll(); };
        box->addWidget(spin);
        break;
    }
    case SettingRow::Button: {
        auto *button = new QPushButton(row.buttonText.isEmpty() ? QStringLiteral("Open…") : row.buttonText);
        button->setFocusPolicy(Qt::TabFocus);
        connect(button, &QPushButton::clicked, this, [fn = row.run, after] {
            if (fn) fn();
            after();
        });
        entry.activate = [button] { button->click(); };
        box->addWidget(button);
        break;
    }
    case SettingRow::Info:
    case SettingRow::Heading:
        break;
    }
    m_rows.append(entry);
    return line;
}

QWidget *SettingsPane::actionRow(const ActionItem &item, const QString &prefix) {
    auto *line = new ClickRow;
    line->setObjectName(QStringLiteral("settingsRow"));
    line->setAttribute(Qt::WA_StyledBackground);
    line->setProperty("rowId", item.key);
    line->setCursor(Qt::PointingHandCursor);
    auto *box = new QHBoxLayout(line);
    box->setContentsMargins(10, 5, 10, 5);
    box->setSpacing(12);
    auto *text = new QVBoxLayout;
    text->setSpacing(1);
    auto *label = new QLabel((item.checked ? QStringLiteral("✓  ") : QString()) + prefix + item.label);
    label->setObjectName(QStringLiteral("settingsRowLabel"));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    text->addWidget(label);
    if (!item.detail.isEmpty()) text->addWidget(mutedLabel(item.detail, "settingsRowDetail"));
    box->addLayout(text, 1);
    if (!item.shortcut.isEmpty()) box->addWidget(keyCap(item.shortcut));
    Row entry;
    entry.id = item.key;
    entry.widget = line;
    entry.activate = [this, item] { runAction(item); };
    line->onClick = entry.activate;
    m_rows.append(entry);
    return line;
}

// ----- search -------------------------------------------------------------------------------------

int SettingsPane::fuzzyScore(const QString &needle, const QString &haystack) {
    if (needle.isEmpty()) return 1;
    const QString n = needle.toLower(), h = haystack.toLower();
    const int direct = h.indexOf(n);
    if (direct >= 0) return 10000 - direct - (direct > 0 && h.at(direct - 1).isLetterOrNumber() ? 500 : 0);
    int pos = 0, first = -1, last = -1;
    for (const QChar c : n) {
        if (c.isSpace()) continue;
        pos = h.indexOf(c, pos);
        if (pos < 0) return 0;
        if (first < 0) first = pos;
        last = pos++;
    }
    return std::max(1, 5000 - (last - first) * 10 - first);
}

// One flat list, best match first: settings rows and actions side by side, each row saying
// where it lives ("General · …"), because the person typed a word, not a section.
void SettingsPane::buildResults(const QString &needle) {
    struct Hit { int score; int order; SettingRow row; ActionItem action; QString where; bool isAction; };
    QList<Hit> hits;
    int order = 0;
    for (const SettingsSection &section : std::as_const(m_sectionCache)) {
        for (const SettingRow &row : section.rows) {
            if (row.kind == SettingRow::Info || row.kind == SettingRow::Heading) continue;
            const int score = std::max({fuzzyScore(needle, row.label), fuzzyScore(needle, row.detail) / 3,
                                        fuzzyScore(needle, section.title) / 4,
                                        needle.size() >= 2 ? fuzzyScore(needle, row.aliases) / 2 : 0});
            if (score > 0) hits.append({score, order++, row, {}, section.title, false});
        }
    }
    QList<ActionItem> flat;
    for (const ActionItem &item : std::as_const(m_actionCache)) {
        if (!item.children) { flat << item; continue; }
        for (ActionItem child : item.children()) {
            child.label = item.label + QStringLiteral(" › ") + child.label;
            child.aliases += QLatin1Char(' ') + item.aliases;
            flat << child;
        }
    }
    for (const ActionItem &item : std::as_const(flat)) {
        const int score = std::max({fuzzyScore(needle, item.label), fuzzyScore(needle, item.detail) / 3,
                                    fuzzyScore(needle, item.section) / 4,
                                    needle.size() >= 2 ? fuzzyScore(needle, item.aliases) / 2 : 0});
        if (score > 0) hits.append({score, order++, {}, item, item.section, true});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score > b.score; });
    if (hits.size() > kMaxResults) hits = hits.mid(0, kMaxResults);

    QScrollArea *scroll = m_results;
    if (QWidget *old = scroll->takeWidget()) old->deleteLater();
    auto *body = new QWidget;
    body->setObjectName(QStringLiteral("settingsPageBody"));
    body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(2, 6, 8, 12);
    layout->setSpacing(2);
    m_rows.clear();
    if (hits.isEmpty()) {
        layout->addWidget(mutedLabel(QStringLiteral("Nothing matches “%1”.").arg(needle), "settingsBlurb"));
    }
    for (const Hit &hit : std::as_const(hits)) {
        if (hit.isAction) {
            ActionItem item = hit.action;
            item.detail = hit.where + (item.detail.isEmpty() ? QString() : QStringLiteral("  ·  ") + item.detail);
            layout->addWidget(actionRow(item));
        } else {
            SettingRow row = hit.row;
            row.detail = hit.where + (row.detail.isEmpty() ? QString() : QStringLiteral("  ·  ") + row.detail);
            layout->addWidget(settingRow(row));
        }
    }
    layout->addStretch(1);
    scroll->setWidget(body);
    m_pages->setCurrentWidget(m_results);
    setCurrent(m_rows.isEmpty() ? -1 : 0, true);
}

// ----- navigation ---------------------------------------------------------------------------------

QScrollArea *SettingsPane::currentScroll() const {
    return qobject_cast<QScrollArea *>(m_pages->currentWidget());
}

void SettingsPane::setCurrent(int index, bool scroll) {
    if (m_current >= 0 && m_current < m_rows.size() && m_rows[m_current].widget) {
        m_rows[m_current].widget->setProperty("current", false);
        repolish(m_rows[m_current].widget);
    }
    m_current = (index >= 0 && index < m_rows.size()) ? index : -1;
    if (m_current < 0) return;
    QWidget *widget = m_rows[m_current].widget;
    widget->setProperty("current", true);
    repolish(widget);
    if (scroll) {
        if (QScrollArea *area = currentScroll()) area->ensureWidgetVisible(widget, 0, 24);
    }
}

void SettingsPane::moveCurrent(int steps) {
    if (m_rows.isEmpty()) return;
    int index = m_current;
    if (index < 0) index = steps > 0 ? 0 : m_rows.size() - 1;
    else index = std::clamp(index + steps, 0, m_rows.size() - 1);
    setCurrent(index, true);
}

void SettingsPane::activateCurrent() {
    if (m_current < 0 || m_current >= m_rows.size()) return;
    if (m_rows[m_current].activate) m_rows[m_current].activate();
}

void SettingsPane::switchTab(int delta) {
    if (m_tabs->count() == 0) return;
    int index = (m_tabs->currentIndex() + delta) % m_tabs->count();
    if (index < 0) index += m_tabs->count();
    m_tabs->setCurrentIndex(index);
}

void SettingsPane::runAction(const ActionItem &item) {
    if (onRun) { onRun(item); return; }
    if (item.run) item.run();
    if (item.stayOpen) QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
}

// The pane chrome's buttons float over the top right of the pane; the search row gives up that room.
void SettingsPane::setHeaderRightInset(int pixels) {
    if (m_header && m_header->contentsMargins().right() != pixels) m_header->setContentsMargins(0, 0, pixels, 0);
}

void SettingsPane::closeRequested() {
    if (onClose) onClose();
}

bool SettingsPane::eventFilter(QObject *object, QEvent *event) {
    if (object != m_search) return QWidget::eventFilter(object, event);
    if (event->type() == QEvent::ShortcutOverride) {
        // Keep the navigation keys away from window shortcuts while the search has focus.
        const int k = static_cast<QKeyEvent *>(event)->key();
        if (k == Qt::Key_Escape || k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Up || k == Qt::Key_Down
            || k == Qt::Key_PageUp || k == Qt::Key_PageDown || k == Qt::Key_Left || k == Qt::Key_Right) {
            event->accept();
            return true;
        }
        return false;
    }
    if (event->type() != QEvent::KeyPress) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    const bool ctrl = key->modifiers() & Qt::ControlModifier;
    switch (key->key()) {
    case Qt::Key_Escape:
        if (!m_search->text().isEmpty()) m_search->clear();
        else closeRequested();
        return true;
    case Qt::Key_Return: case Qt::Key_Enter:
        if (m_current < 0 && !m_rows.isEmpty()) setCurrent(0, true);
        activateCurrent();
        return true;
    case Qt::Key_Down: moveCurrent(1); return true;
    case Qt::Key_Up: moveCurrent(-1); return true;
    case Qt::Key_PageDown: moveCurrent(8); return true;
    case Qt::Key_PageUp: moveCurrent(-8); return true;
    case Qt::Key_Left:
        if (!m_search->text().isEmpty()) return false;
        switchTab(-1); return true;
    case Qt::Key_Right:
        if (!m_search->text().isEmpty()) return false;
        switchTab(1); return true;
    default:
        if (ctrl && key->key() == Qt::Key_N) { moveCurrent(1); return true; }
        if (ctrl && key->key() == Qt::Key_P) { moveCurrent(-1); return true; }
        return false;
    }
}

// Esc anywhere else in the pane: back to the search first, then (from the search) close.
void SettingsPane::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        if (focusWidget() != m_search) { focusSearch(); event->accept(); return; }
        if (!m_search->text().isEmpty()) m_search->clear(); else closeRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ----- public surface ------------------------------------------------------------------------------

void SettingsPane::showTab(const QString &id) {
    const int index = m_tabIds.indexOf(id);
    if (index < 0) return;
    m_wantedTab = id;
    m_tabs->setCurrentIndex(index);
}

QString SettingsPane::currentTab() const { return m_tabIds.value(m_tabs->currentIndex()); }
QStringList SettingsPane::tabIds() const { return m_tabIds; }
void SettingsPane::setSearch(const QString &text) { m_search->setText(text); }
QString SettingsPane::search() const { return m_search->text(); }

void SettingsPane::focusSearch() {
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void SettingsPane::scrollToGroup(const QString &key) {
    showTab(actionsTabId());
    QWidget *header = m_groups.value(key);
    if (!header) return;
    QScrollArea *area = currentScroll();
    if (!area) return;
    // The page has just been shown; its layout settles on the next pass.
    QPointer<QWidget> guard(header);
    QTimer::singleShot(0, this, [this, guard, area] {
        if (!guard) return;
        area->ensureWidgetVisible(guard, 0, 0);
        area->verticalScrollBar()->setValue(guard->pos().y() - 6);
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].widget && m_rows[i].widget->pos().y() > guard->pos().y()) { setCurrent(i, false); break; }
    });
}

QStringList SettingsPane::visibleRowIds() const {
    QStringList ids;
    for (const Row &row : m_rows) ids << row.id;
    return ids;
}

void SettingsPane::rebuild() {
    // What to put back afterwards: the tab, the scroll offset, the highlighted row and the focus.
    QScrollArea *area = currentScroll();
    const int offset = area ? area->verticalScrollBar()->value() : 0;
    const QString currentId = (m_current >= 0 && m_current < m_rows.size()) ? m_rows[m_current].id : QString();
    QString focusedId;
    if (QWidget *focus = focusWidget(); focus && focus != m_search && isAncestorOf(focus)) {
        for (QWidget *w = focus; w && w != this; w = w->parentWidget())
            if (w->objectName() == QLatin1String("settingsRow")) { focusedId = w->property("rowId").toString(); break; }
    }
    build();
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!currentId.isEmpty() && m_rows[i].id == currentId) setCurrent(i, false);
        if (!focusedId.isEmpty() && m_rows[i].id == focusedId) {
            QWidget *row = m_rows[i].widget;
            const auto controls = row->findChildren<QWidget *>();
            for (QWidget *control : controls)
                if (control->focusPolicy() != Qt::NoFocus) { control->setFocus(Qt::OtherFocusReason); break; }
        }
    }
    if (QScrollArea *fresh = currentScroll(); fresh && offset > 0) {
        // The new page has not been laid out yet, so its scroll range is still 0: put the offset
        // back as soon as the range exists.
        QScrollBar *bar = fresh->verticalScrollBar();
        if (bar->maximum() >= offset) { bar->setValue(offset); return; }
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = connect(bar, &QScrollBar::rangeChanged, this, [bar, offset, connection](int, int maximum) {
            if (maximum <= 0) return;
            bar->setValue(std::min(offset, maximum));
            QObject::disconnect(*connection);
        });
    }
}

}  // namespace relay
