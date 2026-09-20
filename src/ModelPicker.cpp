// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelPicker.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace relay {

using namespace models;

namespace {

enum Column { ColModel, ColProvider, ColReasoning, ColIntelligence, ColSpeed, ColLeft, ColCount };
constexpr int KeyRole = Qt::UserRole;
constexpr int SectionRole = Qt::UserRole + 1;

QString percent(double left) { return left < 0 ? QString() : QStringLiteral("%1%").arg(qRound(left)); }

}  // namespace

ModelPicker::ModelPicker(const Context &context, QWidget *parent) : QDialog(parent), m_context(context) {
    setWindowTitle(QStringLiteral("model"));
    setObjectName(QStringLiteral("modelPicker"));
    resize(900, 520);
    auto *layout = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    m_filter = new QLineEdit;
    m_filter->setObjectName(QStringLiteral("modelFilter"));
    m_filter->setPlaceholderText(QStringLiteral("filter · ↑↓ select · enter uses it"));
    m_filter->setClearButtonEnabled(true);
    top->addWidget(m_filter, 1);
    top->addWidget(new QLabel(QStringLiteral("sort")));
    m_sort = new QComboBox;
    m_sort->setObjectName(QStringLiteral("modelSort"));
    m_sort->setAccessibleName(QStringLiteral("sort models by"));
    for (Sort sort : allSorts()) m_sort->addItem(sortLabel(sort), sortId(sort));
    m_sort->setCurrentIndex(qMax(0, m_sort->findData(sortId(curation::sort()))));
    top->addWidget(m_sort);
    layout->addLayout(top);

    m_list = new QTreeWidget;
    m_list->setObjectName(QStringLiteral("modelList"));
    m_list->setHeaderLabels({QStringLiteral("model"), QStringLiteral("provider"), QStringLiteral("reasoning"),
                             QStringLiteral("intelligence"), QStringLiteral("tok/s"), QStringLiteral("left")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAllColumnsShowFocus(true);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(ColModel, QHeaderView::Stretch);
    for (int c = ColProvider; c < ColCount; ++c) m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_list->setTextElideMode(Qt::ElideRight);
    auto *lists = new QHBoxLayout;
    lists->addWidget(m_list, 1);
    // The level, as its own pick beside the models: the highlighted model's levels in the
    // provider's words, the lists' level for it preselected. Enter here uses model and level.
    auto *levelColumn = new QVBoxLayout;
    levelColumn->addWidget(new QLabel(QStringLiteral("reasoning")));
    m_levels = new QListWidget;
    m_levels->setObjectName(QStringLiteral("modelLevels"));
    m_levels->setAccessibleName(QStringLiteral("reasoning level"));
    m_levels->setFixedWidth(120);
    m_levels->setUniformItemSizes(true);
    levelColumn->addWidget(m_levels, 1);
    lists->addLayout(levelColumn);
    layout->addLayout(lists, 1);

    m_limits = new QLabel;
    m_limits->setObjectName(QStringLiteral("modelLimits"));
    m_limits->setWordWrap(true);
    layout->addWidget(m_limits);

    auto *buttons = new QHBoxLayout;
    m_favorite = new QPushButton(QStringLiteral("☆ favorite"));
    m_favorite->setObjectName(QStringLiteral("modelFavorite"));
    m_favorite->setToolTip(QStringLiteral("Pin this model at the top of the list"));
    buttons->addWidget(m_favorite);
    auto *customize = new QPushButton(QStringLiteral("customize…"));
    customize->setObjectName(QStringLiteral("modelCustomize"));
    customize->setToolTip(QStringLiteral("Options › Models: providers, which models this list shows, and their order (/models)"));
    buttons->addWidget(customize);
    buttons->addStretch(1);
    m_use = new QPushButton(QStringLiteral("use"));
    m_use->setDefault(true);
    buttons->addWidget(m_use);
    auto *cancel = new QPushButton(QStringLiteral("cancel"));
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(m_filter, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_sort, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        curation::setSort(sortFromId(m_sort->itemData(index).toString()));
        rebuild();
    });
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this] { onRowChanged(); });
    connect(m_list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (item && !item->data(0, SectionRole).toBool()) accept();
    });
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
    connect(m_levels, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { accept(); });
    m_levels->installEventFilter(this);
    connect(m_use, &QPushButton::clicked, this, [this] { accept(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    // Typing goes to the filter; arrows move the list even while the filter has the focus.
    m_filter->installEventFilter(this);
    rebuild();
    if (!m_context.currentKey.isEmpty()) selectKey(m_context.currentKey);
    if (!m_list->currentItem() && m_list->topLevelItemCount()) {
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            if (!m_list->topLevelItem(i)->data(0, SectionRole).toBool()) { m_list->setCurrentItem(m_list->topLevelItem(i)); break; }
    }
    m_filter->setFocus();
}

bool ModelPicker::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_filter && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down || key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown) {
            QCoreApplication::sendEvent(m_list, event);
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { accept(); return true; }
        if (key->key() == Qt::Key_Right && m_levels->count() > 0) { m_levels->setFocus(); return true; }   // → the level
    }
    if (watched == m_levels && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Left) { m_filter->setFocus(); return true; }                           // ← the models
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { accept(); return true; }
    }
    return QDialog::eventFilter(watched, event);
}

void ModelPicker::addSection(const QString &title) {
    auto *item = new QTreeWidgetItem(m_list, QStringList{title});
    item->setData(0, SectionRole, true);
    item->setFlags(Qt::ItemIsEnabled);   // not selectable
    item->setFirstColumnSpanned(true);
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
}

QTreeWidgetItem *ModelPicker::addRow(const Entry &entry) {
    // The level the pane would run at after this pick: the lists' entry for the model, else the
    // pane's own level moved to one the model offers. Information, not a control.
    QString reasoning;
    if (!entry.efforts.isEmpty()) {
        const QString listed = curation::listEffortFor(entry.key);
        reasoning = entry.efforts.contains(listed) ? listed
                  : entry.efforts.contains(m_context.currentEffort) ? m_context.currentEffort : entry.efforts.last();
    }
    const double speed = curation::speed(entry.key);
    QStringList columns{(curation::isFavorite(entry.key) ? QStringLiteral("★ ") : QString()) + entry.label,
                        entry.provider + (entry.plan.isEmpty() ? QString() : QStringLiteral(" · ") + entry.plan),
                        reasoning.isEmpty() ? QString() : entry.effortLabel(reasoning),
                        entry.intelligence >= 0 ? QString::number(entry.intelligence) : QString(),
                        speed > 0 ? QString::number(qRound(speed)) : QString(),
                        percent(percentLeft(m_context.catalog, entry.preset))};
    // An exhausted subscription (owner, 2026-09-20): the row stays, greyed, still selectable — the
    // user may insist — and "left" says when it comes back. The priority skips it meanwhile.
    const qint64 now = m_context.now > 0 ? m_context.now : QDateTime::currentSecsSinceEpoch();
    const qint64 until = exhaustedUntil(m_context.catalog, entry.preset, now);
    if (until >= 0)
        columns[ColLeft] = QStringLiteral("0%") + (until > 0 ? QStringLiteral(" · resets ") + resetText(until, now) : QString());
    auto *item = new QTreeWidgetItem(m_list, columns);
    item->setData(0, KeyRole, entry.key);
    item->setToolTip(0, entry.model + (entry.custom ? QStringLiteral(" (added by you)") : QString())
                     + (until >= 0 ? QStringLiteral(" · exhausted: skipped in the priority until it resets") : QString()));
    for (int c = ColReasoning; c < ColCount; ++c) item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    if (until >= 0)
        for (int c = 0; c < ColCount; ++c) item->setForeground(c, palette().color(QPalette::Disabled, QPalette::Text));
    if (entry.key == m_context.currentKey) {
        QFont font = item->font(0);
        font.setBold(true);
        for (int c = 0; c < ColCount; ++c) item->setFont(c, font);
        item->setText(ColModel, item->text(ColModel) + QStringLiteral("  · current"));
    }
    return item;
}

void ModelPicker::rebuild() {
    const QString keep = selectedKey();
    m_list->clear();
    const QString query = m_filter->text().trimmed();
    const Sort sort = sortFromId(m_sort->currentData().toString());
    QList<Entry> all = ordered(shown(m_context.catalog), sort, m_context.catalog);
    if (query.isEmpty() && sort == Sort::Priority) {
        // Sections, opencode's way: favorites, then the ten most recent, then everything by rank.
        // A model in an earlier section is not repeated below it.
        QStringList placed;
        QList<Entry> favorites, recent;
        for (const QString &key : curation::favorites())
            for (const Entry &entry : all) if (entry.key == key && !placed.contains(key)) { favorites << entry; placed << key; }
        for (const QString &key : curation::recent())
            for (const Entry &entry : all) if (entry.key == key && !placed.contains(key)) { recent << entry; placed << key; }
        if (!favorites.isEmpty()) { addSection(QStringLiteral("favorites")); for (const Entry &entry : favorites) addRow(entry); }
        if (!recent.isEmpty()) { addSection(QStringLiteral("recent")); for (const Entry &entry : recent) addRow(entry); }
        if (!favorites.isEmpty() || !recent.isEmpty()) addSection(QStringLiteral("all, by priority"));
        for (const Entry &entry : all) if (!placed.contains(entry.key)) addRow(entry);
    } else {
        for (const Entry &entry : all) if (matches(entry, query)) addRow(entry);
    }
    if (!keep.isEmpty()) selectKey(keep);
    if (!m_list->currentItem()) {
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            if (!m_list->topLevelItem(i)->data(0, SectionRole).toBool()) { m_list->setCurrentItem(m_list->topLevelItem(i)); break; }
    }
    onRowChanged();
}

QString ModelPicker::selectedKey() const {
    QTreeWidgetItem *item = m_list->currentItem();
    if (!item || item->data(0, SectionRole).toBool()) return QString();
    return item->data(0, KeyRole).toString();
}

QString ModelPicker::selectedEffort() const {
    QListWidgetItem *item = m_levels ? m_levels->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void ModelPicker::selectKey(const QString &key) {
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (item->data(0, KeyRole).toString() == key) { m_list->setCurrentItem(item); m_list->scrollToItem(item); return; }
    }
}

void ModelPicker::onRowChanged() {
    const QString key = selectedKey();
    const Entry *entry = key.isEmpty() ? nullptr : m_context.catalog.find(key);
    m_use->setEnabled(entry != nullptr);
    m_favorite->setEnabled(entry != nullptr);
    m_favorite->setText(entry && curation::isFavorite(key) ? QStringLiteral("★ unfavorite") : QStringLiteral("☆ favorite"));
    m_levels->clear();
    if (!entry) { m_limits->clear(); return; }
    if (entry->efforts.isEmpty()) {
        auto *none = new QListWidgetItem(QStringLiteral("no setting"), m_levels);
        none->setFlags(Qt::NoItemFlags);
    } else {
        const QString listed = curation::listEffortFor(key);
        const QString chosen = entry->efforts.contains(listed) ? listed
                             : entry->efforts.contains(m_context.currentEffort) ? m_context.currentEffort : entry->efforts.last();
        for (const QString &level : entry->efforts) {
            auto *item = new QListWidgetItem(entry->effortLabel(level), m_levels);
            item->setData(Qt::UserRole, level);
            if (level == chosen) m_levels->setCurrentItem(item);
        }
    }
    const QString limits = limitsText(m_context.catalog.limits.value(entry->preset),
                                      m_context.now > 0 ? m_context.now : QDateTime::currentSecsSinceEpoch());
    m_limits->setText(limits.isEmpty() ? QString() : entry->provider + QStringLiteral(": ") + limits);
}

void ModelPicker::accept() {
    const QString key = selectedKey();
    if (key.isEmpty()) return;
    m_pick.accepted = true;
    m_pick.key = key;
    m_pick.effort = selectedEffort();
    QDialog::accept();
}

ModelPick pickModel(QWidget *parent, const ModelPicker::Context &context, std::function<void()> openModelsPage) {
    ModelPicker dialog(context, parent);
    dialog.openModelsPage = std::move(openModelsPage);
    dialog.exec();
    return dialog.pick();
}

}  // namespace relay
