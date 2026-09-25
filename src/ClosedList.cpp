// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ClosedList.h"

#include "WindowState.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace relay {
namespace closed {
namespace {

// What a row is, in Qt::UserRole: a record row carries its id; a pane row carries the id of its
// saved text (filled in when it is first unfolded); everything else carries nothing.
constexpr int kRecordId = Qt::UserRole;
constexpr int kScrollbackId = Qt::UserRole + 1;
constexpr int kPreviewFilled = Qt::UserRole + 2;

QString paneLine(const PaneInfo &pane, const QString &home) {
    QString where = pane.cwd;
    if (!home.isEmpty() && where == home) where = QStringLiteral("~");
    else if (!home.isEmpty() && where.startsWith(home + QLatin1Char('/'))) where = QStringLiteral("~") + where.mid(home.size());
    const QString kind = pane.kind == QStringLiteral("pane") ? QString() : pane.kind.left(1).toUpper() + pane.kind.mid(1);
    QStringList parts;
    if (!kind.isEmpty()) parts << kind;
    if (!pane.title.isEmpty()) parts << pane.title;
    if (!where.isEmpty()) parts << where;
    return parts.join(QStringLiteral(" · "));
}

// A saved line as the text it draws: CSI sequences (SGR) and OSC strings (image-row links, #1MGS)
// dropped whole, any other escape with the character after it, and every other control.
QString plainLine(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (int at = 0; at < text.size(); ++at) {
        const ushort c = text.at(at).unicode();
        if (c == 0x1b && at + 1 < text.size() && text.at(at + 1) == QLatin1Char('[')) {
            at += 2;
            while (at < text.size() && !(text.at(at).unicode() >= 0x40 && text.at(at).unicode() <= 0x7e)) ++at;
        } else if (c == 0x1b && at + 1 < text.size() && text.at(at + 1) == QLatin1Char(']')) {
            at += 2;   // to BEL or ST
            while (at < text.size() && text.at(at).unicode() != 0x07
                   && !(text.at(at).unicode() == 0x1b && at + 1 < text.size() && text.at(at + 1) == QLatin1Char('\\')))
                ++at;
            if (at < text.size() && text.at(at).unicode() == 0x1b) ++at;
        } else if (c == 0x1b) {
            ++at;
        } else if (c == '\t' || (c >= 0x20 && c != 0x7f && !(c >= 0x80 && c < 0xa0))) {
            out += text.at(at);
        }
    }
    return out;
}

}  // namespace

QStringList previewTail(const QStringList &lines, int maxLines, int maxChars) {
    QStringList kept;
    bool blank = false;
    for (const QString &raw : lines) {
        QString line = plainLine(raw);
        while (!line.isEmpty() && line.at(line.size() - 1).isSpace()) line.chop(1);
        if (line.isEmpty()) { blank = !kept.isEmpty(); continue; }
        if (blank) kept << QString();
        blank = false;
        kept << (maxChars > 0 && line.size() > maxChars ? line.left(maxChars - 1) + QChar(0x2026) : line);
    }
    if (maxLines > 0 && kept.size() > maxLines) kept = kept.mid(kept.size() - maxLines);
    while (!kept.isEmpty() && kept.first().isEmpty()) kept.removeFirst();
    return kept;
}

ListView::ListView(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("closedList"));
    readText = [](const QString &id) { return windowstate::readScrollback(id, 400); };

    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("closedSearch"));
    m_search->setPlaceholderText(QStringLiteral("Filter by name or directory"));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) { setFilter(text); });

    // Which kinds to list: a check box each, beside the filter.
    auto *filters = new QHBoxLayout;
    filters->addWidget(m_search, 1);
    const struct { Record::Kind kind; const char *text; const char *name; } kinds[] = {
        {Record::Window, "Windows", "closedShowWindows"},
        {Record::Tab, "Tabs", "closedShowTabs"},
        {Record::Pane, "Panes", "closedShowPanes"},
    };
    for (const auto &kind : kinds) {
        auto *box = new QCheckBox(QString::fromLatin1(kind.text));
        box->setObjectName(QString::fromLatin1(kind.name));
        box->setChecked(true);
        box->setFocusPolicy(Qt::TabFocus);   // a click leaves the keyboard on the rows
        const Record::Kind which = kind.kind;
        connect(box, &QCheckBox::toggled, this, [this, which](bool on) { setKindShown(which, on); });
        m_kinds[which] = box;
        filters->addWidget(box);
    }

    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("closedTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({QStringLiteral("Closed"), QStringLiteral("Where"), QStringLiteral("When")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(false);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->installEventFilter(this);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) { if (!item->parent()) unfold(item); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) { reopenCurrent(); });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *, QTreeWidgetItem *) {
        m_reopen->setEnabled(!selectedId().isEmpty());
    });

    m_empty = new QLabel;
    m_empty->setObjectName(QStringLiteral("dialogHint"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);

    m_hint = new QLabel(QStringLiteral("Enter reopens it where it was, with its text and conversation and a new shell · → shows what was inside · Delete drops it"));
    m_hint->setObjectName(QStringLiteral("dialogHint"));
    m_hint->setWordWrap(true);

    m_reopen = new QPushButton(QStringLiteral("Reopen"));
    m_reopen->setEnabled(false);
    connect(m_reopen, &QPushButton::clicked, this, [this] { reopenCurrent(); });
    m_clear = new QPushButton(QStringLiteral("Clear list"));
    m_clear->setToolTip(QStringLiteral("Forget everything in this list and the terminal text saved for it"));
    connect(m_clear, &QPushButton::clicked, this, [this] { if (onClear) onClear(); });

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_hint, 1);
    buttons->addWidget(m_clear);
    buttons->addWidget(m_reopen);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addLayout(filters);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_empty, 1);
    layout->addLayout(buttons);

    // "5 min ago" goes stale while the pane sits open.
    m_ageTimer = new QTimer(this);
    m_ageTimer->setInterval(30'000);
    connect(m_ageTimer, &QTimer::timeout, this, [this] { refreshAges(); });
    m_ageTimer->start();
    rebuild();
}

void ListView::setRecords(const QList<Record> &records) {
    m_records = records;
    rebuild();
}

void ListView::setFilter(const QString &text) {
    if (m_filter == text) return;
    m_filter = text;
    if (m_search->text() != text) m_search->setText(text);
    rebuild();
}

void ListView::setKindShown(Record::Kind kind, bool shown) {
    if (m_shown[kind] == shown) return;
    m_shown[kind] = shown;
    if (m_kinds[kind]->isChecked() != shown) m_kinds[kind]->setChecked(shown);
    rebuild();
}

bool ListView::kindShown(Record::Kind kind) const { return m_shown[kind]; }

void ListView::focusInput() {
    if (m_tree->isVisible() && m_tree->topLevelItemCount() > 0) m_tree->setFocus(Qt::OtherFocusReason);
    else m_search->setFocus(Qt::OtherFocusReason);
}

// The tab was brought to the front (closed.list, or a click on it): the keyboard comes with it.
// Queued, because whoever showed the tab usually focuses the pane around it straight after.
void ListView::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this] { if (isVisible()) focusInput(); });
}

int ListView::visibleCount() const { return m_tree->topLevelItemCount(); }

QTreeWidgetItem *ListView::topOf(QTreeWidgetItem *item) const {
    while (item && item->parent()) item = item->parent();
    return item;
}

QString ListView::selectedId() const {
    QTreeWidgetItem *top = topOf(m_tree->currentItem());
    return top ? top->data(0, kRecordId).toString() : QString();
}

void ListView::rebuild() {
    // Keep the owner's place across a refresh: the list changes under them whenever anything
    // closes in any window.
    const QString selected = selectedId();
    QSet<QString> open;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        if (m_tree->topLevelItem(i)->isExpanded()) open.insert(m_tree->topLevelItem(i)->data(0, kRecordId).toString());

    // clear() announces a current item that is already half gone; nobody needs to hear of it.
    const QSignalBlocker quiet(m_tree);
    m_tree->clear();
    const QString home = QDir::homePath();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QTreeWidgetItem *reselect = nullptr;
    for (auto it = m_records.crbegin(); it != m_records.crend(); ++it) {
        const Record &record = *it;
        if (!m_shown[record.kind] || !matches(record, m_filter)) continue;
        auto *row = new QTreeWidgetItem(m_tree);
        row->setData(0, kRecordId, record.id);
        row->setText(0, QStringLiteral("%1 · %2").arg(kindName(record.kind), label(record)));
        row->setText(1, place(record, home));
        row->setText(2, age(record.closedAt, now));
        row->setToolTip(2, QDateTime::fromMSecsSinceEpoch(record.closedAt).toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        const QList<TabInfo> tabs = contents(record);
        for (int t = 0; t < tabs.size(); ++t) {
            QTreeWidgetItem *under = row;
            if (record.kind == Record::Window) {
                under = new QTreeWidgetItem(row);
                under->setText(0, tabs.at(t).name.isEmpty() ? QStringLiteral("Tab %1").arg(t + 1)
                                                            : QStringLiteral("Tab %1 · %2").arg(t + 1).arg(tabs.at(t).name));
            }
            for (const PaneInfo &pane : tabs.at(t).panes) {
                auto *paneRow = new QTreeWidgetItem(under);
                paneRow->setText(0, paneLine(pane, home));
                if (!pane.sessionId.isEmpty()) paneRow->setText(1, QStringLiteral("conversation"));
                paneRow->setData(0, kScrollbackId, pane.scrollback);
            }
            if (under != row) under->setExpanded(true);
        }
        // Signals are blocked here, so what was unfolded is filled by hand.
        if (open.contains(record.id)) { row->setExpanded(true); unfold(row); }
        if (record.id == selected) reselect = row;
    }
    const bool any = m_tree->topLevelItemCount() > 0;
    m_tree->setVisible(any);
    m_empty->setVisible(!any);
    m_empty->setText(m_records.isEmpty()
                         ? QStringLiteral("Nothing has been closed yet.\nPanes, tabs and windows you close are kept here, across restarts, until you clear the list.")
                         : !m_shown[Record::Window] && !m_shown[Record::Tab] && !m_shown[Record::Pane]
                         ? QStringLiteral("Tick Windows, Tabs or Panes to list what was closed.")
                         : m_filter.isEmpty()
                         ? QStringLiteral("Nothing of the ticked kinds has been closed.")
                         : QStringLiteral("Nothing closed matches “%1”.").arg(m_filter));
    m_clear->setEnabled(!m_records.isEmpty());
    if (!reselect && any) reselect = m_tree->topLevelItem(0);
    if (reselect) m_tree->setCurrentItem(reselect);
    m_reopen->setEnabled(!selectedId().isEmpty());
}

// The saved text is read the first time a record is unfolded, never for the whole list.
void ListView::unfold(QTreeWidgetItem *record) {
    const bool single = record->childCount() == 1 && record->child(0)->childCount() == 0
                        && !record->child(0)->data(0, kScrollbackId).toString().isEmpty();
    std::function<void(QTreeWidgetItem *)> fill = [&](QTreeWidgetItem *row) {
        fillPreview(row);
        for (int i = 0; i < row->childCount(); ++i) fill(row->child(i));
    };
    fill(record);
    // A lone pane's text shows at once; with several panes each keeps its own fold.
    if (single) record->child(0)->setExpanded(true);
}

void ListView::refreshAges() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *row = m_tree->topLevelItem(i);
        const QString id = row->data(0, kRecordId).toString();
        for (const Record &record : std::as_const(m_records))
            if (record.id == id) { row->setText(2, age(record.closedAt, now)); break; }
    }
}

void ListView::fillPreview(QTreeWidgetItem *paneRow) {
    if (!paneRow || paneRow->data(0, kPreviewFilled).toBool()) return;
    const QString id = paneRow->data(0, kScrollbackId).toString();
    if (id.isEmpty()) return;
    paneRow->setData(0, kPreviewFilled, true);
    const QStringList tail = previewTail(readText ? readText(id) : QStringList());
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (tail.isEmpty()) {
        auto *none = new QTreeWidgetItem(paneRow);
        none->setText(0, QStringLiteral("(no terminal text was saved for this pane)"));
        none->setDisabled(true);
        return;
    }
    for (const QString &line : tail) {
        auto *text = new QTreeWidgetItem(paneRow);
        text->setText(0, line);
        text->setFont(0, mono);
        text->setFirstColumnSpanned(true);
        text->setFlags(text->flags() & ~Qt::ItemIsSelectable);
    }
}

void ListView::reopenCurrent() {
    const QString id = selectedId();
    if (!id.isEmpty() && onReopen) onReopen(id);
}

void ListView::discardCurrent() {
    const QString id = selectedId();
    if (!id.isEmpty() && onDiscard) onDiscard(id);
}

bool ListView::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(watched, event);
    auto *key = static_cast<QKeyEvent *>(event);
    const bool plain = !(key->modifiers() & ~Qt::KeypadModifier);
    if (watched == m_search && plain) {
        // The filter keeps the keyboard: ↓ and ↑ walk the list from it, Enter reopens.
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
            QTreeWidgetItem *top = topOf(m_tree->currentItem());
            int index = top ? m_tree->indexOfTopLevelItem(top) : -1;
            index += key->key() == Qt::Key_Down ? 1 : -1;
            if (index >= 0 && index < m_tree->topLevelItemCount()) m_tree->setCurrentItem(m_tree->topLevelItem(index));
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { reopenCurrent(); return true; }
    }
    if (watched == m_tree && plain) {
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { reopenCurrent(); return true; }
        if (key->key() == Qt::Key_Delete) { discardCurrent(); return true; }
        // Typing on the rows is typing in the filter.
        const QString text = key->text();
        if (!text.isEmpty() && text.at(0).isPrint() && !text.at(0).isSpace()) {
            m_search->setFocus(Qt::OtherFocusReason);
            m_search->insert(text);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace closed
}  // namespace relay
