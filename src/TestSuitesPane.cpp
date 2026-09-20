// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestSuitesPane.h"

#include "Theme.h"

#include <QAbstractTableModel>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

namespace relay::tests {

namespace {

// TestGrid's grid is "the last N runs, newest-left"; N is 20 because that is what the contract's
// TestRecord carries (`history` is at most 20) and what one column can show without a scrollbar.
constexpr int kGridCells = 20;
constexpr int kCellWidth = 9;
constexpr int kCellGap = 3;
constexpr int kCellHeight = 13;

constexpr int kResultsRole = Qt::UserRole + 1;
constexpr int kBadgesRole = Qt::UserRole + 2;
constexpr int kRunStateRole = Qt::UserRole + 3;
constexpr int kIdRole = Qt::UserRole + 4;
constexpr int kFailingRole = Qt::UserRole + 5;

// The grid's inks are the theme's own meanings: green passed, red did not, amber skipped, and an
// execution that never happened is an unlit slot — a hairline, exactly as a plain theme draws a
// jack ring (docs/SWITCHBOARD-AESTHETIC.md §3.4).
QColor cellColor(const QString &result) {
    if (result == QLatin1String("pass")) return theme::Success;
    if (result == QLatin1String("skip")) return theme::Warning;
    if (result.isEmpty()) return QColor();
    return theme::Error;   // fail, error, timeout
}

QColor badgeColor(const QString &badge) {
    if (badge == QLatin1String("gone")) return theme::Error;
    if (badge == QLatin1String("flaky") || badge == QLatin1String("slow")) return theme::Warning;
    return theme::TextMuted;
}

QString escaped(const QString &text) { return text.toHtmlEscaped(); }

}  // namespace

// ----- the table's rows -------------------------------------------------------------------------

// A thin adapter over TestSuitesModel: it owns no state of its own, so a filter, a sort and a
// worker event are all one beginResetModel().
class TestTableModel final : public QAbstractTableModel {
public:
    explicit TestTableModel(TestSuitesModel *model, QObject *parent)
        : QAbstractTableModel(parent), m_model(model) {}

    void refresh() {
        beginResetModel();
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : int(m_model->rows().size());
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : int(ColCount);
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= m_model->rows().size()) return {};
        const TestRow &row = m_model->rows().at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
            case ColName: return row.name;
            case ColGrid: return QVariant();
            case ColReliability: return row.reliabilityText();
            case ColP50: return row.p50Text();
            case ColP95: return row.p95Text();
            case ColRuns: return row.runsText();
            case ColLastRun: return row.lastRunText(m_model->clock ? m_model->clock()
                                                                  : QDateTime::currentDateTimeUtc());
            case ColCards: return row.cardsText();
            default: return QVariant();
            }
        case Qt::TextAlignmentRole:
            if (index.column() == ColReliability || index.column() == ColP50
                || index.column() == ColP95 || index.column() == ColRuns)
                return int(Qt::AlignRight | Qt::AlignVCenter);
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        case Qt::ForegroundRole:
            if (index.column() == ColCards && !row.cards.isEmpty()) return QVariant(theme::Link);
            if (row.failing()) return QVariant(theme::Error);
            if (row.neverRun()) return QVariant(theme::TextMuted);
            return QVariant(theme::Text);
        case Qt::ToolTipRole: {
            QStringList lines;
            lines << row.id;
            if (!row.file.isEmpty())
                lines << (row.hasLine ? QStringLiteral("%1:%2").arg(row.file).arg(row.line) : row.file);
            if (!row.labels.isEmpty()) lines << row.labels.join(QStringLiteral(", "));
            if (row.flakeScore > 0)
                lines << QStringLiteral("flake score %1").arg(row.flakeScore, 0, 'f', 1);
            return lines.join(QStringLiteral("\n"));
        }
        case kResultsRole: {
            QStringList results;
            for (const Execution &execution : row.history) {
                if (results.size() >= kGridCells) break;
                results << execution.result;
            }
            return results;
        }
        case kBadgesRole: return row.badges();
        case kRunStateRole: return int(row.runState);
        case kIdRole: return row.id;
        case kFailingRole: return row.failing();
        default: return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal) return {};
        if (role == Qt::DisplayRole) {
            switch (section) {
            case ColName: return QStringLiteral("Name");
            case ColGrid: return QStringLiteral("Recent runs");
            case ColReliability: return QStringLiteral("Reliability");
            case ColP50: return QStringLiteral("p50");
            case ColP95: return QStringLiteral("p95");
            case ColRuns: return QStringLiteral("Runs");
            case ColLastRun: return QStringLiteral("Last run");
            case ColCards: return QStringLiteral("Cards");
            default: return {};
            }
        }
        if (role == Qt::ToolTipRole) {
            switch (section) {
            case ColName: return QStringLiteral("The test, and what is true of it — sorts by name");
            case ColGrid: return QStringLiteral("The last %1 executions, newest on the left — "
                                                "sorts by flake score, the rows that flip pass↔fail "
                                                "most and most recently").arg(kGridCells);
            case ColReliability: return QStringLiteral("passed / (passed + failed) over the retained "
                                                       "window — sorts by flake score");
            case ColP50: return QStringLiteral("Median duration — sorts by p95, where a bimodal test "
                                               "shows up");
            case ColP95: return QStringLiteral("95th-percentile duration — sorts by p95");
            case ColRuns: return QStringLiteral("Executions in the retained window, since this test's "
                                                "source last changed — no sort of its own; the "
                                                "Display menu holds all five");
            case ColLastRun: return QStringLiteral("When it last ran — sorts by that");
            case ColCards: return QStringLiteral("The cards whose ## Tests section names this test — "
                                                 "no sort of its own; the Display menu holds all five");
            default: return {};
            }
        }
        if (role == Qt::TextAlignmentRole) {
            if (section == ColReliability || section == ColP50 || section == ColP95 || section == ColRuns)
                return int(Qt::AlignRight | Qt::AlignVCenter);
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
        return {};
    }

private:
    TestSuitesModel *m_model;
};

// ----- the delegate that paints the run grid and the name -----------------------------------------

namespace {

class RowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const QSize base = QStyledItemDelegate::sizeHint(option, index);
        const int height = std::max(base.height(), option.fontMetrics.height() + 9);
        if (index.column() == ColGrid)
            return {kGridCells * (kCellWidth + kCellGap) + 10, height};
        // The numeric columns size to their contents, so the gutter between them has to come from
        // here: without it "20" and "7 h ago" touch.
        return {base.width() + (index.column() == ColName ? 0 : 14), height};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString text = opt.text;
        // The row's band is drawn over the whole cell and the text inside a narrower rect, so the
        // selection stays one unbroken band while the columns keep a gutter between them.
        opt.text.clear();
        const QWidget *widget = option.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
        painter->save();
        painter->setClipRect(option.rect);
        if (index.column() == ColGrid) {
            paintGrid(painter, opt, index);
        } else if (index.column() == ColName) {
            paintName(painter, opt, index, text);
        } else {
            const bool right = (opt.displayAlignment & Qt::AlignRight) != 0;
            const QRect rect = opt.rect.adjusted(right ? 4 : 2, 0, right ? -9 : -4, 0);
            const QVariant ink = index.data(Qt::ForegroundRole);
            painter->setPen(ink.canConvert<QColor>() ? ink.value<QColor>() : theme::Text);
            painter->setFont(opt.font);
            const QFontMetrics fm(opt.font);
            painter->drawText(rect, int(opt.displayAlignment),
                              fm.elidedText(text, Qt::ElideRight, rect.width()));
        }
        painter->restore();
    }

private:
    void paintGrid(QPainter *painter, const QStyleOptionViewItem &option,
                   const QModelIndex &index) const {
        const QStringList results = index.data(kResultsRole).toStringList();
        painter->setRenderHint(QPainter::Antialiasing, false);
        const int top = option.rect.center().y() - kCellHeight / 2;
        int x = option.rect.left() + 5;
        for (int i = 0; i < kGridCells; ++i) {
            if (x + kCellWidth > option.rect.right() - 2) break;
            const QRect cell(x, top, kCellWidth, kCellHeight);
            const QString result = i < results.size() ? results.at(i) : QString();
            const QColor fill = cellColor(result);
            if (fill.isValid()) {
                painter->fillRect(cell, fill);
            } else {
                // Never run, or off the end of the history: an unlit slot, not a colour.
                painter->setPen(theme::Border);
                painter->drawRect(cell.adjusted(0, 0, -1, -1));
            }
            x += kCellWidth + kCellGap;
        }
    }

    void paintName(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index,
                   const QString &name) const {
        const auto state = RunState(index.data(kRunStateRole).toInt());
        const QStringList badges = index.data(kBadgesRole).toStringList();
        QRect rect = option.rect.adjusted(6, 0, -6, 0);
        const QFontMetrics fm(option.font);
        if (state != RunState::Idle) {
            const QString glyph = state == RunState::Running ? QStringLiteral("▶")
                                                             : QStringLiteral("·");
            painter->setPen(state == RunState::Running ? theme::Accent : theme::TextMuted);
            painter->drawText(QRect(rect.left(), rect.top(), 14, rect.height()),
                              Qt::AlignVCenter | Qt::AlignLeft, glyph);
            rect.setLeft(rect.left() + 16);
        }
        QFont small = theme::legible(option.font, theme::FloorPt);
        small.setPointSizeF(std::max(theme::FloorPt, option.font.pointSizeF() - 1.0));
        const QFontMetrics smallFm(small);
        int badgeWidth = 0;
        for (const QString &badge : badges) badgeWidth += smallFm.horizontalAdvance(badge) + 10;
        const int nameWidth = std::max(30, rect.width() - badgeWidth - 8);
        const QVariant ink = index.data(Qt::ForegroundRole);
        painter->setPen(ink.canConvert<QColor>() ? ink.value<QColor>() : theme::Text);
        painter->drawText(QRect(rect.left(), rect.top(), nameWidth, rect.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(name, Qt::ElideMiddle, nameWidth));
        if (badges.isEmpty()) return;
        painter->setFont(small);
        int x = rect.right();
        for (int i = badges.size() - 1; i >= 0; --i) {
            const QString badge = badges.at(i);
            const int w = smallFm.horizontalAdvance(badge) + 8;
            if (x - w < rect.left() + nameWidth) break;
            painter->setPen(badgeColor(badge));
            painter->drawText(QRect(x - w, rect.top(), w, rect.height()),
                              Qt::AlignVCenter | Qt::AlignRight, badge);
            x -= w + 2;
        }
    }
};

}  // namespace

// ----- the pane -----------------------------------------------------------------------------------

TestSuitesPane::TestSuitesPane(QWidget *parent) : QWidget(parent) {
    buildUi();
    m_model.onChanged = [this] { modelChanged(); };
    connect(theme::notifier(), &theme::Notifier::themeChanged, this, [this] { refreshTheme(); });
    refreshTheme();
    modelChanged();
}

TestSuitesPane::~TestSuitesPane() = default;

QString TestSuitesPane::paneTitle() const { return QStringLiteral("Test suites"); }

void TestSuitesPane::focusView() { m_table->setFocus(Qt::OtherFocusReason); }

void TestSuitesPane::setHeaderRightInset(int pixels) {
    if (m_title->contentsMargins().right() != pixels) m_title->setContentsMargins(0, 0, pixels, 0);
}

void TestSuitesPane::buildUi() {
    setObjectName(QStringLiteral("testSuitesPane"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 8, 8);
    layout->setSpacing(6);

    auto *headerRow = new QHBoxLayout;
    m_title = new QLabel(QStringLiteral("Test suites"));
    m_title->setTextFormat(Qt::PlainText);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    headerRow->addWidget(m_title, 1);
    layout->addLayout(headerRow);

    // nextest's header line, as the worker composed it (or as the model composes it here).
    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("panelKeys"));
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_summary);

    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(6);
    m_filter = new QLineEdit;
    m_filter->setObjectName(QStringLiteral("boardFilter"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setPlaceholderText(
        QStringLiteral("Filter — name, file or label, and is:slow is:flaky is:failed is:never is:stale"));
    m_filter->setToolTip(QStringLiteral("/ from the table focuses this · Esc clears it"));
    m_filter->installEventFilter(this);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) { m_model.setFilter(text); });
    filterRow->addWidget(m_filter, 1);
    m_display = new QToolButton;
    m_display->setText(QStringLiteral("Display ▾"));
    m_display->setToolTip(QStringLiteral("Which columns are shown, and what the table is sorted by"));
    m_display->setPopupMode(QToolButton::InstantPopup);
    filterRow->addWidget(m_display);
    auto *refresh = new QPushButton(QStringLiteral("Refresh"));
    refresh->setToolTip(QStringLiteral("Ask the worker to list the project's tests again"));
    connect(refresh, &QPushButton::clicked, this, [this] { requestList(); });
    filterRow->addWidget(refresh);
    layout->addLayout(filterRow);

    m_stack = new QStackedWidget;
    layout->addWidget(m_stack, 1);

    m_split = new QSplitter(Qt::Vertical);
    m_split->setChildrenCollapsible(false);
    m_table = new QTableView;
    m_table->setObjectName(QStringLiteral("testSuitesTable"));
    m_rows = new TestTableModel(&m_model, this);
    m_table->setModel(m_rows);
    m_table->setItemDelegate(new RowDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 9);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->installEventFilter(this);
    QHeaderView *header = m_table->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ColName, QHeaderView::Stretch);
    header->setSectionResizeMode(ColGrid, QHeaderView::Fixed);
    for (int column = ColReliability; column < ColCount; ++column)
        header->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(ColGrid, kGridCells * (kCellWidth + kCellGap) + 10);
    // The indicator has to say what the model is actually doing: flake score, worst first.
    header->setSortIndicator(ColGrid, Qt::DescendingOrder);
    connect(header, &QHeaderView::sectionClicked, this, [this](int column) { sortByColumn(column); });
    connect(m_table, &QTableView::customContextMenuRequested, this,
            [this](const QPoint &at) { showMenu(at); });
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex &) { openSelectedSource(); });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                m_selected = current.isValid() ? current.data(kIdRole).toString() : QString();
                updateDetail();
                updateButtons();
            });
    m_split->addWidget(m_table);

    m_detail = new QTextBrowser;
    m_detail->setObjectName(QStringLiteral("filePreviewMarkdown"));
    m_detail->setOpenLinks(false);
    m_detail->setOpenExternalLinks(false);
    m_detail->setMinimumHeight(90);
    connect(m_detail, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == QLatin1String("card")) {
            if (onOpenCard) onOpenCard(url.path());
        } else if (url.scheme() == QLatin1String("file")) {
            if (onOpenFile) onOpenFile(url.path(), url.fragment().toInt());
        }
    });
    m_split->addWidget(m_detail);
    m_split->setStretchFactor(0, 3);
    m_split->setStretchFactor(1, 2);
    m_stack->addWidget(m_split);

    auto *emptyPage = new QWidget;
    auto *emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(24, 24, 24, 24);
    emptyLayout->setSpacing(10);
    emptyLayout->addStretch(1);
    m_empty = new QLabel;
    m_empty->setObjectName(QStringLiteral("panelKeys"));
    m_empty->setWordWrap(true);
    m_empty->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    emptyLayout->addWidget(m_empty);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    m_emptyRun = new QPushButton(QStringLiteral("Run all"));
    m_emptyRun->setObjectName(QStringLiteral("primary"));
    connect(m_emptyRun, &QPushButton::clicked, this, [this] { runAll(); });
    buttonRow->addWidget(m_emptyRun);
    buttonRow->addStretch(1);
    emptyLayout->addLayout(buttonRow);
    emptyLayout->addStretch(2);
    m_stack->addWidget(emptyPage);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    m_run = new QPushButton(QStringLiteral("Run"));
    m_run->setToolTip(QStringLiteral("Run the selected test (Enter)"));
    connect(m_run, &QPushButton::clicked, this, [this] { runSelected(0); });
    actions->addWidget(m_run);
    m_repeat = new QPushButton(QStringLiteral("Rerun until fail"));
    m_repeat->setToolTip(QStringLiteral("Run it up to ten times and stop at the first failure — "
                                        "the flake finder"));
    connect(m_repeat, &QPushButton::clicked, this, [this] { runSelected(10); });
    actions->addWidget(m_repeat);
    m_stop = new QPushButton(QStringLiteral("Stop"));
    m_stop->setToolTip(QStringLiteral("Stop the run in flight"));
    connect(m_stop, &QPushButton::clicked, this, [this] { stopRun(); });
    actions->addWidget(m_stop);
    m_source = new QPushButton(QStringLiteral("Open source"));
    connect(m_source, &QPushButton::clicked, this, [this] { openSelectedSource(); });
    actions->addWidget(m_source);
    actions->addStretch(1);
    m_makeCard = new QPushButton(QStringLiteral("Make a card"));
    connect(m_makeCard, &QPushButton::clicked, this, [this] { makeCardForSelected(); });
    actions->addWidget(m_makeCard);
    m_attach = new QPushButton(QStringLiteral("Attach to card"));
    connect(m_attach, &QPushButton::clicked, this, [this] { attachSelectedToCard(); });
    actions->addWidget(m_attach);
    layout->addLayout(actions);

    // The Display menu: the columns, then the five sorts the model knows.
    auto *menu = new QMenu(this);
    for (int column = ColGrid; column < ColCount; ++column) {
        auto *action = menu->addAction(m_rows->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, this, [this, column](bool on) { setColumnVisible(column, on); });
    }
    menu->addSeparator();
    auto *sortMenu = menu->addMenu(QStringLiteral("Sort by"));
    const QList<QPair<QString, TestSuitesModel::Sort>> sorts{
        {QStringLiteral("Flake score"), TestSuitesModel::Sort::FlakeScore},
        {QStringLiteral("p95 duration"), TestSuitesModel::Sort::P95},
        {QStringLiteral("Name"), TestSuitesModel::Sort::Name},
        {QStringLiteral("Last failure"), TestSuitesModel::Sort::LastFailure},
        {QStringLiteral("Last run"), TestSuitesModel::Sort::LastRun}};
    for (const auto &entry : sorts) {
        const TestSuitesModel::Sort sort = entry.second;
        auto *action = sortMenu->addAction(entry.first);
        connect(action, &QAction::triggered, this,
                [this, sort] { m_model.setSort(sort, TestSuitesModel::defaultAscending(sort)); });
    }
    sortMenu->addSeparator();
    auto *ascending = sortMenu->addAction(QStringLiteral("Ascending"));
    ascending->setCheckable(true);
    connect(ascending, &QAction::toggled, this, [this](bool on) { m_model.setSort(m_model.sort(), on); });
    connect(sortMenu, &QMenu::aboutToShow, this, [this, ascending] {
        QSignalBlocker block(ascending);
        ascending->setChecked(m_model.ascending());
    });
    m_display->setMenu(menu);

    setFocusPolicy(Qt::StrongFocus);
}

// ----- painting and theme --------------------------------------------------------------------

void TestSuitesPane::refreshTheme() {
    const QString bg = theme::Surface.name();
    const QString border = theme::Border.name();
    const QString text = theme::Text.name();
    const QString muted = theme::TextMuted.name();
    m_table->setStyleSheet(
        QStringLiteral("QTableView#testSuitesTable { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 6px; outline: none; }"
                       "QTableView#testSuitesTable::item { padding: 0px 7px; }"
                       "QTableView#testSuitesTable QHeaderView::section { background: %1; color: %4; "
                       "border: none; border-bottom: 1px solid %3; padding: 4px 6px; }")
            .arg(bg, text, border, muted));
    QPalette pal = m_table->palette();
    pal.setColor(QPalette::Base, theme::Surface);
    pal.setColor(QPalette::Text, theme::Text);
    pal.setColor(QPalette::Highlight, theme::SurfaceRaised.lighter(135));
    pal.setColor(QPalette::HighlightedText, theme::Text);
    m_table->setPalette(pal);
    m_table->setFont(theme::legible(font(), theme::SecondaryPt));
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 9);
    updateDetail();
    m_table->viewport()->update();
}

void TestSuitesPane::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_summary) updateHeader();
    fitGridColumn();
}

// Two things give way as the pane narrows, in this order: the run grid shrinks to whole cells
// (never fewer than five), then the numeric columns leave from the least telling end — p50,
// Runs, Cards, Last run, p95 — until the Name column keeps a readable minimum. At the pane's
// default width (half the Switchboard's share) twenty cells and seven columns had every name
// eliding to "adde...lows". A column the user hid in the Display menu stays hidden; a column
// this hid comes back on its own when the room is there again. The delegate already paints
// only the cells that fit.
void TestSuitesPane::fitGridColumn() {
    if (!m_table || !m_table->horizontalHeader()) return;
    QHeaderView *header = m_table->horizontalHeader();
    constexpr int kNameMinimum = 170;
    constexpr int kMinCells = 5;
    const int cellStride = kCellWidth + kCellGap;
    static const int yieldOrder[] = {ColP50, ColRuns, ColCards, ColLastRun, ColP95};
    auto others = [&]() {
        int width = 0;
        for (int column = ColReliability; column < ColCount; ++column)
            if (!header->isSectionHidden(column)) width += header->sectionSize(column);
        return width;
    };
    const int available = m_table->viewport()->width() - kNameMinimum - 10;
    if (m_table->viewport()->width() < 200) return;   // not laid out yet; nothing to fit
    // Bring back what this function hid, widest need last, while the room allows it.
    for (int i = int(std::size(yieldOrder)) - 1; i >= 0; --i) {
        const int column = yieldOrder[i];
        if (!m_autoHidden.contains(column)) continue;
        m_table->setColumnHidden(column, false);
        if (others() + kMinCells * cellStride + 10 > available) {
            m_table->setColumnHidden(column, true);
            break;
        }
        m_autoHidden.remove(column);
    }
    for (int column : yieldOrder) {
        if (others() + kMinCells * cellStride + 10 <= available) break;
        if (header->isSectionHidden(column) || m_userSet.contains(column)) continue;
        m_table->setColumnHidden(column, true);
        m_autoHidden.insert(column);
    }
    const int room = available - others();
    const int cells = std::clamp(room / cellStride, kMinCells, kGridCells);
    const int width = cells * cellStride + 10;
    if (header->sectionSize(ColGrid) != width) m_table->setColumnWidth(ColGrid, width);
}

// ----- events in ------------------------------------------------------------------------------

void TestSuitesPane::handleEvent(const QJsonObject &event) { m_model.handle(event); }

void TestSuitesPane::send(const QJsonObject &request) {
    if (onSend) onSend(request);
}

void TestSuitesPane::requestList() { send(QJsonObject{{"type", "tests_list"}}); }

// ----- what changed ----------------------------------------------------------------------------

void TestSuitesPane::modelChanged() {
    const QString keep = m_selected;
    m_rows->refresh();
    const int index = keep.isEmpty() ? -1 : m_model.indexOf(keep);
    if (index >= 0) {
        m_table->selectionModel()->setCurrentIndex(m_rows->index(index, ColName),
                                                   QItemSelectionModel::ClearAndSelect
                                                       | QItemSelectionModel::Rows);
        m_selected = keep;
    } else {
        m_selected.clear();
    }
    updateHeader();
    updateEmptyState();
    updateDetail();
    updateButtons();
}

void TestSuitesPane::updateHeader() {
    QString line = m_model.summaryLine();
    const QString run = m_model.runLine();
    if (!run.isEmpty()) line += QStringLiteral("   ") + run;
    if (!m_model.filter().isEmpty() && m_model.attached())
        line += QStringLiteral("   %1 shown").arg(m_model.rows().size());
    m_summary->setToolTip(line);
    const int available = std::max(60, m_summary->width() - 4);
    m_summary->setText(m_summary->fontMetrics().elidedText(line, Qt::ElideRight, available));
}

void TestSuitesPane::updateEmptyState() {
    QString message;
    bool offerRun = false;
    if (!m_model.attached()) {
        message = QStringLiteral("No worker is attached yet, so there is nothing to list.");
    } else if (m_model.allRows().isEmpty()) {
        message = QStringLiteral("No tests were discovered in this project.");
    } else if (m_model.rows().isEmpty()) {
        message = QStringLiteral("Nothing matches this filter.");
    } else {
        bool everNone = true;
        for (const TestRow &row : m_model.allRows())
            if (!row.neverRun()) { everNone = false; break; }
        if (everNone) {
            message = QStringLiteral("These tests have been discovered but never run.");
            offerRun = true;
        }
    }
    m_empty->setText(message);
    m_emptyRun->setVisible(offerRun);
    m_stack->setCurrentIndex(message.isEmpty() ? 0 : 1);
}

QString TestSuitesPane::emptyStateText() const {
    return m_stack->currentIndex() == 1 ? m_empty->text() : QString();
}

const TestRow *TestSuitesPane::selectedRow() const {
    return m_selected.isEmpty() ? nullptr : m_model.row(m_selected);
}

void TestSuitesPane::updateButtons() {
    const TestRow *row = selectedRow();
    m_run->setEnabled(row != nullptr);
    m_repeat->setEnabled(row != nullptr);
    m_source->setEnabled(row != nullptr && !row->file.isEmpty());
    m_makeCard->setEnabled(row != nullptr);
    m_attach->setEnabled(row != nullptr);
    m_stop->setVisible(m_model.running());
}

// The detail: the executions, the last failure, the cards and the file — the four things a row
// cannot show. Everything the worker sent is escaped: a failure message is a test's output.
void TestSuitesPane::updateDetail() {
    const TestRow *row = selectedRow();
    if (!row) {
        m_detail->setHtml(QStringLiteral("<div style='color:%1'>Select a test for its history, its "
                                         "last failure and the cards that name it.</div>")
                              .arg(theme::TextMuted.name()));
        return;
    }
    // A row arrives with at most twenty executions; the full history is one round trip away, and
    // it is asked for once per test.
    if (m_askedHistoryFor != row->id && row->history.size() >= kGridCells) {
        m_askedHistoryFor = row->id;
        send(QJsonObject{{"type", "tests_history"}, {"id", row->id}, {"limit", 100}});
    }

    const QString mono = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    const QString muted = theme::TextMuted.name();
    const QString link = theme::Link.name();
    const QString error = theme::Error.name();
    QString html = QStringLiteral("<div style='font-family:sans-serif'>");
    html += QStringLiteral("<div style='font-size:medium'><b>%1</b> <span style='color:%2'>%3</span></div>")
                .arg(escaped(row->name), muted, escaped(row->runner));
    QStringList meta;
    if (!row->file.isEmpty()) {
        const QString shown = row->hasLine ? QStringLiteral("%1:%2").arg(row->file).arg(row->line) : row->file;
        meta << QStringLiteral("<a style='color:%1' href='file:%2#%3'>%4</a>")
                    .arg(link, QString(row->file).toHtmlEscaped(), QString::number(row->line),
                         escaped(shown));
    }
    if (!row->labels.isEmpty()) meta << escaped(row->labels.join(QStringLiteral(", ")));
    if (row->hasReliability)
        meta << QStringLiteral("%1 reliable over %2 run%3")
                    .arg(row->reliabilityText(), QString::number(row->runs))
                    .arg(row->runs == 1 ? QString() : QStringLiteral("s"));
    if (row->flakeScore > 0)
        meta << QStringLiteral("flake score %1").arg(row->flakeScore, 0, 'f', 1);
    if (!row->cards.isEmpty()) {
        QStringList cards;
        for (const QString &card : row->cards)
            cards << QStringLiteral("<a style='color:%1' href='card:%2'>#%2</a>").arg(link, escaped(card));
        meta << cards.join(QStringLiteral(" "));
    } else {
        meta << QStringLiteral("<span style='color:%1'>no card names this test</span>").arg(muted);
    }
    html += QStringLiteral("<div style='color:%1'>%2</div>").arg(muted, meta.join(QStringLiteral(" · ")));

    if (row->hasLastFailure) {
        html += QStringLiteral("<div style='margin-top:8px;color:%1'><b>Last failure</b> %2 %3</div>")
                    .arg(error, escaped(relativeTime(row->failureAt, m_model.clock
                                                                         ? m_model.clock()
                                                                         : QDateTime::currentDateTimeUtc())),
                         row->failureCommit.isEmpty()
                             ? QString()
                             : QStringLiteral("· %1").arg(escaped(row->failureCommit.left(12))));
        if (!row->failureMessage.isEmpty())
            html += QStringLiteral("<div style='font-family:\"%1\";white-space:pre-wrap'>%2</div>")
                        .arg(mono, escaped(row->failureMessage));
        if (!row->failureExcerpt.isEmpty())
            html += QStringLiteral("<div style='font-family:\"%1\";color:%2;white-space:pre-wrap'>%3</div>")
                        .arg(mono, muted, escaped(row->failureExcerpt));
    }

    const QVector<Execution> &history = row->history;
    if (history.isEmpty()) {
        html += QStringLiteral("<div style='margin-top:8px;color:%1'>No execution has been recorded "
                               "for this test.</div>").arg(muted);
    } else {
        html += QStringLiteral("<div style='margin-top:8px'><b>History</b></div>"
                               "<table cellpadding='2' cellspacing='0' style='font-family:\"%1\"'>")
                    .arg(mono);
        for (const Execution &execution : history) {
            const QColor colour = cellColor(execution.result);
            html += QStringLiteral("<tr>"
                                   "<td style='color:%1'>%2</td>"
                                   "<td style='color:%3'>%4</td>"
                                   "<td style='color:%1' align='right'>%5</td>"
                                   "<td style='color:%1'>%6</td>"
                                   "<td style='color:%1'>%7</td></tr>")
                        .arg(muted, escaped(execution.ts),
                             (colour.isValid() ? colour : theme::TextMuted).name(),
                             escaped(execution.result.isEmpty() ? QStringLiteral("—") : execution.result),
                             escaped(execution.durationText()),
                             escaped(execution.commit.left(12)), escaped(execution.host));
        }
        html += QStringLiteral("</table>");
    }
    html += QStringLiteral("</div>");
    m_detail->setHtml(html);
}

QString TestSuitesPane::detailText() const { return m_detail->toPlainText(); }
QString TestSuitesPane::summaryText() const { return m_summary->text(); }
int TestSuitesPane::visibleRowCount() const { return m_rows->rowCount(); }
QString TestSuitesPane::selectedId() const { return m_selected; }

void TestSuitesPane::selectRow(int index) {
    if (index < 0 || index >= m_rows->rowCount()) return;
    m_table->selectionModel()->setCurrentIndex(
        m_rows->index(index, ColName), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

bool TestSuitesPane::columnVisible(int column) const { return !m_table->isColumnHidden(column); }

void TestSuitesPane::setColumnVisible(int column, bool visible) {
    // The user's choice, so it is no longer this pane's to bring back or take away.
    m_autoHidden.remove(column);
    m_userSet.insert(column);
    m_table->setColumnHidden(column, !visible);
    fitGridColumn();
}

// ----- sorting ---------------------------------------------------------------------------------

void TestSuitesPane::sortByColumn(int column) {
    // The model's sort keys are the contract's five, so two columns carry none of their own: Runs
    // and Cards do not sort, and their header says so rather than pretending.
    TestSuitesModel::Sort sort = TestSuitesModel::Sort::FlakeScore;
    switch (column) {
    case ColName: sort = TestSuitesModel::Sort::Name; break;
    case ColGrid: case ColReliability: sort = TestSuitesModel::Sort::FlakeScore; break;
    case ColP50: case ColP95: sort = TestSuitesModel::Sort::P95; break;
    case ColLastRun: sort = TestSuitesModel::Sort::LastRun; break;
    default: return;
    }
    const bool same = m_model.sort() == sort;
    const bool ascending = same ? !m_model.ascending() : TestSuitesModel::defaultAscending(sort);
    m_model.setSort(sort, ascending);
    m_table->horizontalHeader()->setSortIndicator(column, ascending ? Qt::AscendingOrder
                                                                    : Qt::DescendingOrder);
}

// ----- the row actions ---------------------------------------------------------------------------

void TestSuitesPane::runSelected(int repeatUntilFail) {
    const TestRow *row = selectedRow();
    if (!row) return;
    const QString id = row->id;
    m_model.markRequested({id});
    send(QJsonObject{{"type", "tests_run"},
                     {"ids", QJsonArray{id}},
                     {"repeat_until_fail", repeatUntilFail}});
}

void TestSuitesPane::runAll() {
    QJsonArray ids;
    QStringList list;
    for (const TestRow &row : m_model.rows()) {
        ids.append(row.id);
        list << row.id;
    }
    if (ids.isEmpty()) return;
    m_model.markRequested(list);
    send(QJsonObject{{"type", "tests_run"}, {"ids", ids}, {"repeat_until_fail", 0}});
}

void TestSuitesPane::stopRun() {
    if (!m_model.running()) return;
    send(QJsonObject{{"type", "tests_stop"}, {"run_id", m_model.runId()}});
}

void TestSuitesPane::openSelectedSource() {
    const TestRow *row = selectedRow();
    if (!row || row->file.isEmpty() || !onOpenFile) return;
    onOpenFile(row->file, row->hasLine ? row->line : 0);
}

void TestSuitesPane::makeCardForSelected() {
    const TestRow *row = selectedRow();
    if (row && onMakeCard) onMakeCard(*row);
}

void TestSuitesPane::attachSelectedToCard() {
    const TestRow *row = selectedRow();
    if (row && onAttachToCard) onAttachToCard(*row);
}

void TestSuitesPane::showMenu(const QPoint &at) {
    const QModelIndex index = m_table->indexAt(at);
    if (index.isValid()) selectRow(index.row());
    const TestRow *row = selectedRow();
    if (!row) return;
    QMenu menu(this);
    const auto add = [this, &menu](const QString &text, void (TestSuitesPane::*slot)()) {
        QAction *action = menu.addAction(text);
        connect(action, &QAction::triggered, this, [this, slot] { (this->*slot)(); });
        return action;
    };
    QAction *run = menu.addAction(QStringLiteral("Run"));
    connect(run, &QAction::triggered, this, [this] { runSelected(0); });
    QAction *repeat = menu.addAction(QStringLiteral("Rerun until fail"));
    connect(repeat, &QAction::triggered, this, [this] { runSelected(10); });
    if (m_model.running()) add(QStringLiteral("Stop the run"), &TestSuitesPane::stopRun);
    menu.addSeparator();
    add(QStringLiteral("Open source"), &TestSuitesPane::openSelectedSource)->setEnabled(!row->file.isEmpty());
    QAction *copy = menu.addAction(QStringLiteral("Copy test id"));
    connect(copy, &QAction::triggered, this, [this] {
        if (const TestRow *selected = selectedRow()) QApplication::clipboard()->setText(selected->id);
    });
    menu.addSeparator();
    add(QStringLiteral("Make a card"), &TestSuitesPane::makeCardForSelected);
    add(QStringLiteral("Attach to card"), &TestSuitesPane::attachSelectedToCard);
    menu.exec(m_table->viewport()->mapToGlobal(at));
}

// ----- keys ---------------------------------------------------------------------------------------

void TestSuitesPane::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Slash:
        m_filter->setFocus(Qt::ShortcutFocusReason);
        m_filter->selectAll();
        return;
    case Qt::Key_Escape:
        if (!m_filter->text().isEmpty()) { m_filter->clear(); return; }
        break;
    case Qt::Key_Return: case Qt::Key_Enter:
        if (selectedRow()) { runSelected(0); return; }
        break;
    default: break;
    }
    QWidget::keyPressEvent(event);
}

bool TestSuitesPane::eventFilter(QObject *object, QEvent *event) {
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    const bool plain = (key->modifiers()
                        & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) == Qt::NoModifier;
    if (object == m_table && plain) {
        switch (key->key()) {
        case Qt::Key_Slash:
            m_filter->setFocus(Qt::ShortcutFocusReason);
            m_filter->selectAll();
            return true;
        case Qt::Key_Escape:
            if (!m_filter->text().isEmpty()) { m_filter->clear(); return true; }
            return false;
        case Qt::Key_Return: case Qt::Key_Enter:
            if (selectedRow()) { runSelected(0); return true; }
            return false;
        default: return false;
        }
    }
    if (object == m_filter) {
        switch (key->key()) {
        case Qt::Key_Escape:
            m_filter->clear();
            m_table->setFocus(Qt::ShortcutFocusReason);
            return true;
        case Qt::Key_Down: case Qt::Key_Return: case Qt::Key_Enter:
            if (visibleRowCount() > 0) {
                if (m_selected.isEmpty()) selectRow(0);
                m_table->setFocus(Qt::ShortcutFocusReason);
                return true;
            }
            return false;
        default: return false;
        }
    }
    return QWidget::eventFilter(object, event);
}

}  // namespace relay::tests
