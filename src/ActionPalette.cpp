// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ActionPalette.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {

namespace {

constexpr int kWidth = 640;
constexpr int kRecentRows = 8;
constexpr int kMaxResults = 80;
// Set on a group dropdown and its submenus: their entries carry an item key in QAction::data().
constexpr const char *kEntriesProperty = "actionPaletteEntries";

// The label is the item's text, so a test or a screen reader reads a row the ordinary way.
enum Role {
    LabelRole = Qt::DisplayRole,
    DetailRole = Qt::UserRole + 1,
    RightRole,      // the `/slash` spelling and the shortcut, drawn at the far end
    HeaderRole,     // bool: a section label rather than a choice
};

// An item with no section still gets a group button, so everything in the catalog is reachable.
QString sectionOf(const ActionItem &item)
{
    return item.section.isEmpty() ? QStringLiteral("Other") : item.section;
}

bool isWordStart(const QString &text, int index)
{
    return index == 0 || !text.at(index - 1).isLetterOrNumber();
}

// How well one typed word matches one field: 100 at the start, 80 at the start of a later word,
// 50 inside a word, and — where `subsequence` allows it — up to 40 for its letters in order.
// relayFuzzyScore (src/AppPaths.h) lives in the window's translation unit, so this is its own.
int fieldScore(const QString &token, const QString &text, bool subsequence)
{
    if (token.isEmpty() || text.isEmpty()) return 0;
    int best = 0;
    for (int from = 0;;) {
        const int at = text.indexOf(token, from, Qt::CaseInsensitive);
        if (at < 0) break;
        best = std::max(best, at == 0 ? 100 : isWordStart(text, at) ? 80 : 50);
        if (best == 100) break;
        from = at + 1;
    }
    if (best > 0 || !subsequence) return best;
    // Letters in order, rewarded for landing on word starts and for running on from each other.
    int score = 0, pos = 0, previous = -2;
    for (const QChar c : token) {
        const int at = text.indexOf(c, pos, Qt::CaseInsensitive);
        if (at < 0) return 0;
        score += 1 + (isWordStart(text, at) ? 3 : 0) + (at == previous + 1 ? 2 : 0);
        previous = at;
        pos = at + 1;
    }
    return std::clamp(10 + score * 30 / (int(token.size()) * 6), 10, 40);
}

// Every typed word must match some field; the label counts most and ranks first on a tie.
int scoreItem(const QString &query, const ActionItem &item)
{
    const QString aliases = item.aliases + QLatin1Char(' ') + actionSlashCommands(item.key);
    int total = 0;
    for (const QString &token : query.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const int best = std::max({fieldScore(token, item.label, true) * 10,
                                   fieldScore(token, aliases, true) * 8,
                                   fieldScore(token, sectionOf(item), false) * 5,
                                   fieldScore(token, item.shortcut, false) * 5,
                                   fieldScore(token, item.detail, false) * 4});
        if (best == 0) return 0;
        total += best;
    }
    if (item.label.startsWith(query, Qt::CaseInsensitive)) total += 500;
    return total;
}

QString rightText(const ActionItem &item)
{
    QStringList parts;
    const QString slash = actionSlashCommands(item.key);
    if (!slash.isEmpty()) parts << slash;
    if (!item.shortcut.isEmpty()) parts << item.shortcut;
    return parts.join(QStringLiteral("   "));
}

// A menu reads `&` as a mnemonic; a label means it literally.
QString menuText(const ActionItem &item)
{
    QString text = item.label;
    text.replace(QLatin1Char('&'), QStringLiteral("&&"));
    if (!item.shortcut.isEmpty()) text += QLatin1Char('\t') + item.shortcut;
    return text;
}

// One row: label, then the detail in the placeholder ink, the `/slash` and shortcut at the far end;
// a header is a small label in that same dim ink. The colours are the window's palette, handed in
// at open: the view's own palette is whatever the style sheet over it resolved, which is not it.
class RowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QPalette colors;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QFontMetrics fm(option.font);
        const bool header = index.data(HeaderRole).toBool();
        return {option.rect.width(), header ? fm.height() + 8 : fm.height() + 12};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QPalette &pal = colors;
        const QRect r = option.rect.adjusted(6, 0, -6, 0);
        if (index.data(HeaderRole).toBool()) {
            QFont font = option.font;
            font.setPointSizeF(font.pointSizeF() * 0.86);
            font.setBold(true);
            painter->setFont(font);
            painter->setPen(pal.color(QPalette::PlaceholderText));
            painter->drawText(r.adjusted(2, 2, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                              QFontMetrics(font).elidedText(index.data(LabelRole).toString(), Qt::ElideRight, r.width()));
            painter->restore();
            return;
        }
        const bool current = option.state.testFlag(QStyle::State_Selected);
        if (current) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(pal.color(QPalette::Highlight));
            painter->drawRoundedRect(option.rect.adjusted(2, 1, -2, -1), 5, 5);
        }
        const QColor ink = current ? pal.color(QPalette::HighlightedText) : pal.color(QPalette::Text);
        QColor dim = pal.color(QPalette::PlaceholderText);
        if (current) dim = QColor::fromRgbF(ink.redF(), ink.greenF(), ink.blueF(), 0.7);
        const QFontMetrics fm(option.font);
        const QRect text = r.adjusted(6, 0, -6, 0);

        const QString right = index.data(RightRole).toString();
        const int rightWidth = right.isEmpty() ? 0 : std::min(fm.horizontalAdvance(right) + 2, text.width() / 3);
        if (!right.isEmpty()) {
            painter->setPen(dim);
            const bool fits = fm.horizontalAdvance(right) < rightWidth;
            painter->drawText(text, Qt::AlignRight | Qt::AlignVCenter, fits ? right : fm.elidedText(right, Qt::ElideLeft, rightWidth));
        }
        const int room = text.width() - rightWidth - (right.isEmpty() ? 0 : 16);
        const QString label = fm.elidedText(index.data(LabelRole).toString(), Qt::ElideRight, room);
        painter->setPen(ink);
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, label);
        const int used = fm.horizontalAdvance(label) + 12;
        const QString detail = index.data(DetailRole).toString();
        if (!detail.isEmpty() && room - used > 24) {
            painter->setPen(dim);
            painter->drawText(text.adjusted(used, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                              fm.elidedText(detail, Qt::ElideRight, room - used));
        }
        painter->restore();
    }
};

} // namespace

ActionPalette::ActionPalette(QWidget *window, std::function<QList<ActionItem>()> catalog,
                             std::function<QList<ActionItem>()> forThisPane, std::function<QStringList()> recentKeys,
                             std::function<void(const QString &key)> chosen)
    : QFrame(window), m_window(window), m_catalog(std::move(catalog)), m_forThisPane(std::move(forThisPane)),
      m_recentKeys(std::move(recentKeys)), m_chosen(std::move(chosen))
{
    setObjectName(QStringLiteral("actionPalette"));
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_StyledBackground);
    hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 8);
    layout->setSpacing(6);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("actionPaletteSearch"));
    m_search->setPlaceholderText(QStringLiteral("Search actions, options and slash commands…"));
    m_search->setClearButtonEnabled(false);
    m_search->installEventFilter(this);
    layout->addWidget(m_search);

    m_buttonRow = new QWidget(this);
    m_buttonLines = new QVBoxLayout(m_buttonRow);
    m_buttonLines->setContentsMargins(0, 0, 0, 0);
    m_buttonLines->setSpacing(2);
    layout->addWidget(m_buttonRow);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("actionPaletteList"));
    m_list->setFocusPolicy(Qt::NoFocus);   // the search box keeps the keyboard; the list follows it
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setUniformItemSizes(false);
    m_list->setMouseTracking(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = m_list->row(m_list->itemAt(pos));
        if (row < 0 || row >= m_rows.size() || !m_rows.at(row).header.isEmpty()) return;
        m_list->setCurrentRow(row);
        showShortcutMenu(m_rows.at(row).item.key, m_list->viewport()->mapToGlobal(pos));
    });
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_delegate = new RowDelegate(m_list);
    m_list->setItemDelegate(m_delegate);
    layout->addWidget(m_list, 1);

    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_list, &QListWidget::itemEntered, this, [this](QListWidgetItem *item) {
        if (item != nullptr && item->flags().testFlag(Qt::ItemIsSelectable)) m_list->setCurrentItem(item);
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item == nullptr || !item->flags().testFlag(Qt::ItemIsSelectable)) return;
        m_list->setCurrentItem(item);
        activateCurrent(QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier));
    });

    if (m_window) m_window->installEventFilter(this);
}

ActionPalette::~ActionPalette()
{
    if (qApp != nullptr) qApp->removeEventFilter(this);
}

bool ActionPalette::isOpen() const
{
    return m_open;
}

void ActionPalette::setToggleKeys(const QList<QKeySequence> &keys)
{
    m_toggleKeys = keys;
}

void ActionPalette::setEditShortcut(std::function<void(const QString &key)> edit)
{
    m_editShortcut = std::move(edit);
}

QMenu *ActionPalette::shortcutMenu() const
{
    return m_shortcutMenu;
}

void ActionPalette::showShortcutMenu(const QString &key, const QPoint &globalPos)
{
    if (!m_editShortcut || key.isEmpty()) return;
    if (m_shortcutMenu) {
        m_shortcutMenu->hide();
        m_shortcutMenu->deleteLater();
    }
    m_shortcutMenu = new QMenu(this);
    m_shortcutMenu->setObjectName(QStringLiteral("actionPaletteShortcutMenu"));
    QAction *change = m_shortcutMenu->addAction(QStringLiteral("Change shortcut…"));
    connect(change, &QAction::triggered, this, [this, key] {
        // Copied first: closing hands focus back, and the editor the caller opens should keep it.
        const auto edit = m_editShortcut;
        close();
        if (edit) edit(key);
    });
    m_shortcutMenu->popup(globalPos);
}

void ActionPalette::toggle()
{
    if (m_open) close();
    else open();
}

void ActionPalette::open()
{
    if (m_open) {
        m_search->setFocus(Qt::ShortcutFocusReason);
        m_search->selectAll();
        return;
    }
    QWidget *focus = QApplication::focusWidget();
    m_returnFocus = (focus != nullptr && focus != this && !isAncestorOf(focus)) ? focus : nullptr;
    // Everything is read fresh: shortcuts rebound, contextual rows, a theme picked since last time.
    m_items = m_catalog ? m_catalog() : QList<ActionItem>();
    m_flat.reset();
    m_submenu.reset();
    applyPalette();
    rebuildButtons();
    {
        const QSignalBlocker block(m_search);
        m_search->clear();
    }
    m_open = true;
    rebuild();
    show();
    raise();
    qApp->installEventFilter(this);
    m_search->setFocus(Qt::ShortcutFocusReason);
}

void ActionPalette::close()
{
    if (!m_open) return;
    hide();   // hideEvent does the rest, so a plain hide() from elsewhere closes it just as well
    if (m_returnFocus && m_returnFocus->isVisible()) m_returnFocus->setFocus(Qt::OtherFocusReason);
    m_returnFocus = nullptr;
}

void ActionPalette::hideEvent(QHideEvent *event)
{
    QFrame::hideEvent(event);
    if (event->spontaneous()) return;
    m_open = false;
    qApp->removeEventFilter(this);
    // Every dropdown, its open submenus and a right-click menu: all are the palette's children.
    for (QMenu *menu : findChildren<QMenu *>()) menu->hide();
}

// ----- the parts ----------------------------------------------------------------------------------

void ActionPalette::applyPalette()
{
    // The window's palette, read at open time: Relay's theme writes its tokens into it, and a
    // theme picked since the last open shows at the next one.
    const QPalette pal = m_window ? m_window->palette() : palette();
    setPalette(pal);
    setStyleSheet(QStringLiteral(
                      "QFrame#actionPalette { background: %1; border: 1px solid %2; border-radius: 8px; }"
                      "QLineEdit#actionPaletteSearch { background: %3; color: %4; border: 1px solid %2;"
                      " border-radius: 5px; padding: 6px 8px; }"
                      "QLineEdit#actionPaletteSearch:focus { border-color: %5; }"
                      "QToolButton#actionPaletteGroup { background: transparent; color: %6; border: 1px solid transparent;"
                      " border-radius: 4px; padding: 2px 7px; }"
                      "QToolButton#actionPaletteGroup:hover, QToolButton#actionPaletteGroup:focus { color: %4; border-color: %5; }"
                      "QListWidget#actionPaletteList { background: transparent; color: %4; border: none; outline: none; }")
                      .arg(pal.color(QPalette::AlternateBase).name(), pal.color(QPalette::Mid).name(),
                           pal.color(QPalette::Base).name(), pal.color(QPalette::Text).name(),
                           pal.color(QPalette::Highlight).name(), pal.color(QPalette::PlaceholderText).name()));
    static_cast<RowDelegate *>(m_delegate)->colors = pal;
}

void ActionPalette::rebuildButtons()
{
    QStringList sections;
    for (const ActionItem &item : std::as_const(m_items))
        if (!sections.contains(sectionOf(item))) sections << sectionOf(item);
    if (sections == m_sections && m_buttons.size() == sections.size()) return;
    m_sections = sections;
    qDeleteAll(m_buttons);
    m_buttons.clear();
    for (int i = 0; i < m_sections.size(); ++i) {
        auto *button = new QToolButton(m_buttonRow);
        button->setObjectName(QStringLiteral("actionPaletteGroup"));
        button->setText(QString(m_sections.at(i)).replace(QLatin1Char('&'), QStringLiteral("&&")) + QStringLiteral(" ▾"));
        button->setToolTip(m_sections.at(i));
        button->setFocusPolicy(Qt::TabFocus);
        button->setAutoRaise(true);
        button->installEventFilter(this);
        connect(button, &QToolButton::clicked, this, [this, i] { popupGroup(i); });
        m_buttons << button;
    }
    m_lastButton = 0;
    m_buttonWidth = -1;
}

// The buttons fill a line and wrap onto the next, so nine groups fit in 640 px.
void ActionPalette::layoutButtons(int width)
{
    if (width == m_buttonWidth && m_buttonLines->count() > 0) return;
    m_buttonWidth = width;
    while (QLayoutItem *line = m_buttonLines->takeAt(0)) {
        if (QLayout *inner = line->layout())
            while (QLayoutItem *child = inner->takeAt(0)) delete child;   // the buttons themselves stay
        delete line;
    }
    QHBoxLayout *line = nullptr;
    int used = 0;
    for (QToolButton *button : std::as_const(m_buttons)) {
        const int w = button->sizeHint().width();
        if (line == nullptr || (used > 0 && used + w > width)) {
            if (line != nullptr) line->addStretch(1);
            line = new QHBoxLayout;
            line->setSpacing(2);
            m_buttonLines->addLayout(line);
            used = 0;
        }
        line->addWidget(button);
        used += w + line->spacing();
    }
    if (line != nullptr) line->addStretch(1);
    m_buttonRow->setVisible(!m_buttons.isEmpty());
}

QList<QAbstractButton *> ActionPalette::groupButtons() const
{
    QList<QAbstractButton *> out;
    for (QToolButton *button : m_buttons) out << button;
    return out;
}

QMenu *ActionPalette::groupMenu(int index)
{
    if (index < 0 || index >= m_sections.size()) return nullptr;
    if (m_menu) {
        m_menu->hide();
        m_menu->deleteLater();
    }
    m_menu = new QMenu(this);
    m_menu->setObjectName(QStringLiteral("actionPaletteMenu"));
    m_menu->setToolTipsVisible(true);
    m_menu->setProperty(kEntriesProperty, true);
    QList<ActionItem> items;
    for (const ActionItem &item : std::as_const(m_items))
        if (sectionOf(item) == m_sections.at(index)) items << item;
    populateMenu(m_menu, items);
    return m_menu;
}

void ActionPalette::populateMenu(QMenu *menu, const QList<ActionItem> &items)
{
    for (const ActionItem &item : items) {
        if (item.children) {
            // Filled when it is first shown: a submenu's children may cost something to list.
            QMenu *sub = menu->addMenu(menuText(item));
            sub->setToolTipsVisible(true);
            sub->setProperty(kEntriesProperty, true);
            sub->menuAction()->setData(item.key);
            connect(sub, &QMenu::aboutToShow, this, [this, sub, item] {
                if (sub->property("filled").toBool()) return;
                sub->setProperty("filled", true);
                populateMenu(sub, item.children());
            });
            continue;
        }
        QAction *action = menu->addAction(menuText(item));
        action->setData(item.key);
        action->setToolTip(item.detail);
        if (item.checked) {
            action->setCheckable(true);
            action->setChecked(true);
        }
        connect(action, &QAction::triggered, this, [this, item] { runItem(item, false); });
    }
}

void ActionPalette::popupGroup(int index)
{
    QMenu *menu = groupMenu(index);
    if (menu == nullptr) return;
    m_lastButton = index;
    QToolButton *button = m_buttons.at(index);
    menu->popup(button->mapToGlobal(QPoint(0, button->height())));
    if (!menu->actions().isEmpty()) menu->setActiveAction(menu->actions().constFirst());
}

void ActionPalette::focusButton(int index)
{
    if (m_buttons.isEmpty()) return;
    index = (index % int(m_buttons.size()) + int(m_buttons.size())) % int(m_buttons.size());
    m_lastButton = index;
    m_buttons.at(index)->setFocus(Qt::TabFocusReason);
}

int ActionPalette::focusedButton() const
{
    for (int i = 0; i < m_buttons.size(); ++i)
        if (m_buttons.at(i)->hasFocus()) return i;
    return -1;
}

void ActionPalette::reposition()
{
    if (!m_window) return;
    const int width = std::min(kWidth, std::max(240, m_window->width() - 32));
    const QMargins margins = layout()->contentsMargins();
    layoutButtons(width - margins.left() - margins.right());
    const int maxHeight = m_window->height() * 6 / 10;
    int chrome = margins.top() + margins.bottom() + m_search->sizeHint().height() + layout()->spacing();
    if (!m_buttons.isEmpty()) chrome += m_buttonLines->sizeHint().height() + layout()->spacing();
    int content = 4;
    for (int i = 0; i < m_list->count(); ++i) content += m_list->sizeHintForRow(i);
    const int listHeight = std::max(std::min(content, maxHeight - chrome), m_list->count() > 0 ? 32 : 0);
    m_list->setFixedHeight(listHeight);
    setFixedSize(width, chrome + listHeight);
    const int top = std::clamp(m_window->height() / 10, 8, std::max(8, m_window->height() - height() - 8));
    move((m_window->width() - width) / 2, top);
}

// ----- rows ---------------------------------------------------------------------------------------

QList<ActionItem> ActionPalette::flatCatalog()
{
    if (m_flat) return *m_flat;
    QList<ActionItem> flat;
    for (const ActionItem &item : std::as_const(m_items)) {
        flat << item;
        if (!item.children) continue;
        for (ActionItem child : item.children()) {
            child.label = item.label + QStringLiteral(" › ") + child.label;
            child.aliases += QLatin1Char(' ') + item.aliases;
            if (child.section.isEmpty()) child.section = item.section;
            flat << child;
        }
    }
    m_flat = flat;
    return flat;
}

void ActionPalette::rebuild()
{
    if (!m_open) return;
    const QString query = m_search->text().trimmed();
    const QString previous = currentKey();
    QList<Row> rows;
    auto header = [&rows](const QString &text) { rows.append({ActionItem(), text, QString()}); };
    auto scored = [&query](const QList<ActionItem> &items, bool tagged) {
        struct Hit { int score; int order; ActionItem item; };
        QList<Hit> hits;
        for (int i = 0; i < items.size(); ++i)
            if (const int score = scoreItem(query, items.at(i)); score > 0) hits.append({score, i, items.at(i)});
        std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score > b.score; });
        QList<Row> out;
        for (const Hit &hit : std::as_const(hits).mid(0, kMaxResults))
            out.append({hit.item, QString(), tagged ? sectionOf(hit.item) : QString()});
        return out;
    };
    // Rows made from the search text itself come first: the text was typed to mean exactly them.
    auto typedRows = [&query](const ActionItem &parent) {
        QList<Row> out;
        if (!parent.typed || query.isEmpty()) return out;
        for (ActionItem row : parent.typed(query)) {
            row.label = parent.label + QStringLiteral(" › ") + row.label;
            out.append({row, QString(), sectionOf(parent)});
        }
        return out;
    };

    if (m_submenu) {
        const QList<ActionItem> children = m_submenu->children ? m_submenu->children() : QList<ActionItem>();
        header(m_submenu->label);
        rows += typedRows(*m_submenu);
        if (query.isEmpty())
            for (const ActionItem &child : children) rows.append({child, QString(), QString()});
        else
            rows += scored(children, false);
    } else if (query.isEmpty()) {
        const QStringList recentKeys = m_recentKeys ? m_recentKeys() : QStringList();
        QList<Row> recent;
        QSet<QString> seen;
        for (const QString &key : recentKeys) {
            if (recent.size() >= kRecentRows) break;
            if (key.isEmpty() || seen.contains(key)) continue;
            seen.insert(key);
            // Top level first, so no submenu is asked for its children unless it has to be.
            auto match = [&key](const ActionItem &item) { return item.key == key && !item.children; };
            auto it = std::find_if(m_items.cbegin(), m_items.cend(), match);
            if (it != m_items.cend()) { recent.append({*it, QString(), QString()}); continue; }
            const QList<ActionItem> flat = flatCatalog();
            auto deep = std::find_if(flat.cbegin(), flat.cend(), match);
            if (deep != flat.cend()) recent.append({*deep, QString(), QString()});
        }
        if (!recent.isEmpty()) {
            header(QStringLiteral("Recent"));
            rows += recent;
        }
        const QList<ActionItem> here = m_forThisPane ? m_forThisPane() : QList<ActionItem>();
        if (!here.isEmpty()) {
            header(QStringLiteral("For this pane"));
            for (const ActionItem &item : here) rows.append({item, QString(), QString()});
        }
        for (const QString &section : std::as_const(m_sections)) {
            header(section);
            for (const ActionItem &item : std::as_const(m_items))
                if (sectionOf(item) == section) rows.append({item, QString(), QString()});
        }
    } else {
        for (const ActionItem &item : std::as_const(m_items)) rows += typedRows(item);
        rows += scored(flatCatalog(), true);
    }
    if (!query.isEmpty() && std::none_of(rows.cbegin(), rows.cend(), [](const Row &row) { return row.header.isEmpty(); }))
        header(QStringLiteral("Nothing matches “%1”.").arg(query));

    m_rows = rows;
    m_list->clear();
    int first = -1, keep = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        auto *item = new QListWidgetItem(m_list);
        if (!row.header.isEmpty()) {
            item->setData(LabelRole, row.header);
            item->setData(HeaderRole, true);
            item->setFlags(Qt::NoItemFlags);
            continue;
        }
        QString label = row.item.label;
        if (row.item.checked) label = QStringLiteral("✓  ") + label;
        if (row.item.children) label += QStringLiteral("  ›");
        QString detail = row.item.detail;
        if (!row.tag.isEmpty() && row.tag != row.item.label)
            detail = detail.isEmpty() ? row.tag : row.tag + QStringLiteral("  ·  ") + detail;
        item->setData(LabelRole, label);
        item->setData(DetailRole, detail);
        item->setData(RightRole, rightText(row.item));
        item->setToolTip(row.item.detail);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (first < 0) first = i;
        if (keep < 0 && !previous.isEmpty() && row.item.key == previous && query.isEmpty()) keep = i;
    }
    reposition();   // sized first, so the scroll below is measured against the list's real height
    setCurrentRow(keep >= 0 ? keep : first);
}

QString ActionPalette::currentKey() const
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_rows.size() || !m_rows.at(row).header.isEmpty()) return {};
    return m_rows.at(row).item.key;
}

void ActionPalette::setCurrentRow(int row)
{
    if (row < 0 || row >= m_list->count()) {
        m_list->setCurrentRow(-1);
        return;
    }
    m_list->setCurrentRow(row);
    // The first choice brings the headers above it into view too ("Recent" on open); any other
    // row brings the header straight above it, when there is one.
    const bool first = std::all_of(m_rows.cbegin(), m_rows.cbegin() + row, [](const Row &r) { return !r.header.isEmpty(); });
    if (first) m_list->scrollToTop();
    else if (!m_rows.at(row - 1).header.isEmpty()) m_list->scrollToItem(m_list->item(row - 1));
    m_list->scrollToItem(m_list->item(row));
}

void ActionPalette::moveCurrent(int steps)
{
    QList<int> selectable;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).header.isEmpty()) selectable << i;
    if (selectable.isEmpty()) return;
    const int at = selectable.indexOf(m_list->currentRow());
    int next;
    if (at < 0) next = steps > 0 ? 0 : int(selectable.size()) - 1;
    else if (std::abs(steps) == 1) next = (at + steps + int(selectable.size())) % int(selectable.size());   // one step wraps
    else next = std::clamp(at + steps, 0, int(selectable.size()) - 1);                                    // a page does not
    setCurrentRow(selectable.at(next));
}

void ActionPalette::activateCurrent(bool stayOpen)
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_rows.size() || !m_rows.at(row).header.isEmpty()) return;
    runItem(m_rows.at(row).item, stayOpen);
}

void ActionPalette::runItem(const ActionItem &item, bool stayOpen)
{
    if (item.children) {
        enterSubmenu(item);
        return;
    }
    // Copies, because running may close the window and this palette with it.
    const QPointer<ActionPalette> self(this);
    const auto run = item.run;
    const auto chosen = m_chosen;
    const QString key = item.key;
    const bool keep = stayOpen || item.stayOpen;
    // Closed before the run, so whatever the action focuses keeps its focus.
    if (!keep) close();
    if (run) run();
    if (chosen) chosen(key);
    if (!keep || !self || !self->m_open) return;
    // What ran may have changed what the rows say (a tick, a pane's state): read them again.
    m_items = m_catalog ? m_catalog() : QList<ActionItem>();
    m_flat.reset();
    rebuildButtons();
    rebuild();
}

void ActionPalette::enterSubmenu(const ActionItem &item)
{
    if (!m_open) open();
    m_submenu = item;
    const QSignalBlocker block(m_search);
    m_search->clear();
    rebuild();
    m_search->setFocus(Qt::OtherFocusReason);
}

bool ActionPalette::leaveSubmenu()
{
    if (!m_submenu) return false;
    m_submenu.reset();
    const QSignalBlocker block(m_search);
    m_search->clear();
    rebuild();
    return true;
}

// ----- keys and clicks ----------------------------------------------------------------------------

bool ActionPalette::isToggleKey(const QKeyEvent *event) const
{
    if (m_toggleKeys.isEmpty()) return false;
    // Built the way RelayWindow names a chord, which compiles on Qt 5 and Qt 6 alike.
    const QKeySequence pressed(int(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier))
                               | event->key());
    return std::any_of(m_toggleKeys.cbegin(), m_toggleKeys.cend(),
                       [&](const QKeySequence &key) { return key.matches(pressed) == QKeySequence::ExactMatch; });
}

void ActionPalette::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape || isToggleKey(event)) {
        close();
        return;
    }
    QFrame::keyPressEvent(event);
}

bool ActionPalette::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (watched == m_window && type == QEvent::Resize) {
        if (m_open) reposition();
        return false;
    }
    if (!m_open) return false;

    // A right-click on a dropdown's entry offers to change its shortcut. QMenu runs an entry on
    // the release of *any* button, so both halves of the click are taken, and the context-menu
    // event the press also produces is dropped, so the offer is made once.
    if (m_editShortcut && watched->property(kEntriesProperty).toBool()
        && (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease || type == QEvent::ContextMenu
            || type == QEvent::KeyPress)) {
        auto *menu = static_cast<QMenu *>(watched);
        if (type == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            const bool menuKey = key == Qt::Key_Menu
                                 || (key == Qt::Key_F10 && static_cast<QKeyEvent *>(event)->modifiers() == Qt::ShiftModifier);
            if (!menuKey || menu->activeAction() == nullptr) return false;
            QAction *action = menu->activeAction();
            showShortcutMenu(action->data().toString(), menu->mapToGlobal(menu->actionGeometry(action).center()));
            return true;
        }
        if (type == QEvent::ContextMenu) return true;
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::RightButton) return false;
        if (type == QEvent::MouseButtonRelease) {
            const QPoint at = mouse->pos();
            if (QAction *action = menu->actionAt(at)) showShortcutMenu(action->data().toString(), menu->mapToGlobal(at));
        }
        return true;
    }

    // A press anywhere else in the window closes the palette and still reaches what was clicked.
    // A dropdown is its own window, so a press in one is not "elsewhere".
    if (type == QEvent::MouseButtonPress) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget != nullptr && widget->window() == m_window && widget != this && !isAncestorOf(widget)) close();
        return false;
    }

    // The application filter sees every event while the palette is open: rule most out cheaply.
    if (type != QEvent::KeyPress && type != QEvent::ShortcutOverride) return false;
    auto *button = qobject_cast<QToolButton *>(watched);
    const bool onSearch = watched == m_search;
    const bool onButton = button != nullptr && m_buttons.contains(button);
    if (!onSearch && !onButton) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    const bool ctrl = key->modifiers().testFlag(Qt::ControlModifier);

    // Keys the palette answers are claimed before any window shortcut can take them.
    const bool ours = isToggleKey(key) || key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return
                      || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Up || key->key() == Qt::Key_Down
                      || key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab
                      || (onButton && (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right || key->key() == Qt::Key_Space));
    if (type == QEvent::ShortcutOverride) {
        if (ours) key->accept();
        return false;
    }

    if (isToggleKey(key)) {
        close();
        return true;
    }
    if (onSearch) {
        switch (key->key()) {
        case Qt::Key_Up: moveCurrent(-1); return true;
        case Qt::Key_Down: moveCurrent(1); return true;
        case Qt::Key_PageUp: moveCurrent(-8); return true;
        case Qt::Key_PageDown: moveCurrent(8); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter: activateCurrent(ctrl); return true;
        case Qt::Key_Tab: focusButton(m_lastButton); return true;
        case Qt::Key_Backtab: focusButton(int(m_buttons.size()) - 1); return true;
        case Qt::Key_Escape:
            if (!leaveSubmenu()) close();
            return true;
        case Qt::Key_Menu:
        case Qt::Key_F10: {
            if (!m_editShortcut || (key->key() == Qt::Key_F10 && key->modifiers() != Qt::ShiftModifier)) return false;
            QListWidgetItem *current = m_list->currentItem();
            if (current == nullptr || currentKey().isEmpty()) return true;
            const QRect rect = m_list->visualItemRect(current);
            showShortcutMenu(currentKey(), m_list->viewport()->mapToGlobal(QPoint(rect.left() + 24, rect.bottom())));
            return true;
        }
        case Qt::Key_Backspace:
            if (m_search->text().isEmpty() && leaveSubmenu()) return true;
            return false;
        default: return false;
        }
    }
    const int index = m_buttons.indexOf(button);
    switch (key->key()) {
    case Qt::Key_Left: focusButton(index - 1); return true;
    case Qt::Key_Right: focusButton(index + 1); return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Down:
    case Qt::Key_Space: popupGroup(index); return true;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Up:
    case Qt::Key_Escape:
        m_lastButton = index;
        m_search->setFocus(Qt::OtherFocusReason);
        return true;
    default:
        // Typing on the button row types into the search box, which is where it was meant to go.
        if (!ctrl && !key->text().isEmpty() && key->text().at(0).isPrint()) {
            m_search->setFocus(Qt::OtherFocusReason);
            m_search->insert(key->text());
            return true;
        }
        return false;
    }
}

} // namespace relay
