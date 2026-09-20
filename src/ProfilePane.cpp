// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ProfilePane.h"

#include "Theme.h"

#include <QAbstractTableModel>
#include <QAction>
#include <QCursor>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QStringList>
#include <QTableView>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace relay::profile {

namespace {

//: How many progress lines the log under the table keeps. A build prints one line a step; this is
//: a tail, not a transcript — the whole of it is in the evidence directory's own logs.
constexpr int kMaxLines = 200;

QString seconds(double value) {
    if (value >= 60.0)
        return QStringLiteral("%1m %2s").arg(int(value / 60)).arg(int(value) % 60);
    if (value >= 1.0)
        return QStringLiteral("%1 s").arg(value, 0, 'f', 1);
    return QStringLiteral("%1 ms").arg(value * 1000.0, 0, 'f', 0);
}

QString percent(double value) { return QStringLiteral("%1%").arg(value, 0, 'f', 1); }

}  // namespace

// ----- the menu ----------------------------------------------------------------------------------

// Mirrors `backend/relay_core/profile_protocol.py`'s TARGETS: same four ids, same labels, same
// one-line descriptions. The worker refuses anything else by name, so a drift here is a refusal
// rather than a wrong profile.
const QVector<ProfileTarget> &profileTargets() {
    static const QVector<ProfileTarget> targets{
        {QStringLiteral("build"), QStringLiteral("Build (this machine)"),
         QStringLiteral("Per-target compile times, from a Ninja build of its own — never the "
                        "build directory the sessions share. Minutes cold, seconds warm.")},
        {QStringLiteral("build-remote"), QStringLiteral("Build (sphinxpad)"),
         QStringLiteral("The same table for the committed tree, built on the second runner. "
                        "About four minutes cold.")},
        {QStringLiteral("tests"), QStringLiteral("Python tests"),
         QStringLiteral("py-spy over the unittest suite, or cProfile where py-spy is missing. "
                        "As long as the tests take.")},
        {QStringLiteral("app"), QStringLiteral("The app"),
         QStringLiteral("Relay itself under perf, started fresh. Use it, then press Stop.")},
    };
    return targets;
}

const ProfileTarget *profileTarget(const QString &id) {
    for (const ProfileTarget &target : profileTargets())
        if (target.id == id) return &target;
    return nullptr;
}

// A menu entry is two lines — the label, and under it what it does and how long — which a plain
// QAction cannot draw, so each one is a small widget in a QWidgetAction. The hover highlight is
// the action's own, so it still reads as a menu.
void showTargetMenu(QWidget *anchor, std::function<void(const QString &)> chosen,
                    bool running, std::function<void()> stop) {
    auto *menu = new QMenu(anchor);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setToolTipsVisible(true);
    for (const ProfileTarget &target : profileTargets()) {
        auto *action = new QWidgetAction(menu);
        auto *row = new QWidget;
        auto *layout = new QVBoxLayout(row);
        layout->setContentsMargins(12, 6, 12, 6);
        layout->setSpacing(1);
        auto *label = new QLabel(target.label, row);
        QFont bold = label->font();
        bold.setBold(true);
        label->setFont(bold);
        layout->addWidget(label);
        auto *detail = new QLabel(target.detail, row);
        detail->setObjectName(QStringLiteral("panelKeys"));
        detail->setWordWrap(true);
        // A word-wrapped label's size hint is as narrow as the layout will let it be, and a menu
        // beside a pane 460 px wide squeezed these to five lines each and ran off the screen. The
        // wrap width is fixed instead, so every entry is two lines and the menu is a menu.
        detail->setFixedWidth(330);
        layout->addWidget(detail);
        action->setDefaultWidget(row);
        action->setToolTip(target.detail);
        const QString id = target.id;
        QObject::connect(action, &QAction::triggered, menu, [chosen, id] { if (chosen) chosen(id); });
        // The labels ignore mouse events, so the press reaches the menu and it triggers the
        // action the pointer is over, exactly as it does for an ordinary entry.
        row->setCursor(Qt::PointingHandCursor);
        menu->addAction(action);
    }
    if (running) {
        menu->addSeparator();
        QAction *stopAction = menu->addAction(QStringLiteral("Stop the profile that is running"));
        QObject::connect(stopAction, &QAction::triggered, menu, [stop] { if (stop) stop(); });
    }
    menu->popup(anchor ? anchor->mapToGlobal(QPoint(0, anchor->height())) : QCursor::pos());
}

// ----- the table's rows --------------------------------------------------------------------------

class ProfileTableModel final : public QAbstractTableModel {
public:
    explicit ProfileTableModel(const QVector<ProfileRow> *rows, const QString *kind, QObject *parent)
        : QAbstractTableModel(parent), m_rows(rows), m_kind(kind) {}

    void refresh() {
        beginResetModel();
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : int(m_rows->size());
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : int(ColCount);
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= m_rows->size()) return {};
        const ProfileRow &row = m_rows->at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
            case ColName: return row.name;
            case ColSelf: return seconds(row.self);
            case ColTotal: return seconds(row.total);
            case ColShare: return percent(row.selfPct);
            default: return {};
            }
        case Qt::TextAlignmentRole:
            return index.column() == ColName ? int(Qt::AlignLeft | Qt::AlignVCenter)
                                             : int(Qt::AlignRight | Qt::AlignVCenter);
        case Qt::ForegroundRole:
            // The rows that are most of the time are the point of the table, so they are the
            // only ones that are not muted: a fifth of the whole is where to look first.
            if (index.column() != ColName && row.selfPct < 1.0) return QVariant(theme::TextMuted);
            if (row.selfPct >= 20.0) return QVariant(theme::Warning);
            return QVariant(theme::Text);
        case Qt::ToolTipRole: {
            QStringList lines{row.name};
            if (!row.file.isEmpty())
                lines << (row.line > 0 ? QStringLiteral("%1:%2").arg(row.file).arg(row.line) : row.file);
            lines << QStringLiteral("self %1 (%2), total %3 (%4)")
                         .arg(seconds(row.self), percent(row.selfPct),
                              seconds(row.total), percent(row.totalPct));
            return lines.join(QStringLiteral("\n"));
        }
        default: return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal) return {};
        const bool build = m_kind && *m_kind == QLatin1String("build");
        if (role == Qt::DisplayRole) {
            switch (section) {
            case ColName: return build ? QStringLiteral("Output") : QStringLiteral("Function");
            case ColSelf: return build ? QStringLiteral("Compile") : QStringLiteral("Self");
            case ColTotal: return build ? QStringLiteral("Wall") : QStringLiteral("Total");
            case ColShare: return QStringLiteral("Share");
            default: return {};
            }
        }
        if (role == Qt::ToolTipRole) {
            switch (section) {
            case ColName: return build ? QStringLiteral("The build step's output file")
                                       : QStringLiteral("The function, and the file it is in");
            case ColSelf: return build ? QStringLiteral("How long this step took")
                                       : QStringLiteral("Time in this function itself");
            case ColTotal: return build ? QStringLiteral("The same, for a build step")
                                        : QStringLiteral("Time in it and everything it called");
            case ColShare: return build ? QStringLiteral("Share of the summed step time")
                                        : QStringLiteral("Share of the whole profile, by self time");
            default: return {};
            }
        }
        if (role == Qt::TextAlignmentRole)
            return section == ColName ? int(Qt::AlignLeft | Qt::AlignVCenter)
                                      : int(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    }

private:
    const QVector<ProfileRow> *m_rows;
    const QString *m_kind;
};

// ----- the pane -----------------------------------------------------------------------------------

ProfilePane::ProfilePane(QWidget *parent) : QWidget(parent) { buildUi(); }
ProfilePane::~ProfilePane() = default;

QAbstractItemModel *ProfilePane::tableModel() const { return m_model; }

void ProfilePane::buildUi() {
    setObjectName(QStringLiteral("profilePane"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 8, 8);
    layout->setSpacing(6);

    m_title = new QLabel(QStringLiteral("Profile"));
    m_title->setTextFormat(Qt::PlainText);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    layout->addWidget(m_title);

    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("panelKeys"));
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_summary);

    m_table = new QTableView;
    m_table->setObjectName(QStringLiteral("profileTable"));
    m_model = new ProfileTableModel(&m_rows, &m_kind, this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 9);
    QHeaderView *header = m_table->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionsClickable(false);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ColName, QHeaderView::Stretch);
    for (int column = ColSelf; column < ColCount; ++column)
        header->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 3);

    // The script's own lines while it runs — "configuring", "building target relay" — because a
    // build is minutes and a pane that says nothing for minutes looks broken.
    m_log = new QTextBrowser;
    m_log->setObjectName(QStringLiteral("filePreviewMarkdown"));
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setMinimumHeight(60);
    m_log->setMaximumHeight(120);
    layout->addWidget(m_log, 1);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    m_flameButton = new QPushButton(QStringLiteral("Open flame graph"));
    m_flameButton->setToolTip(QStringLiteral("scripts/relay-speedscope — the raw profile in the "
                                             "system browser, from the local bundle; nothing is uploaded"));
    connect(m_flameButton, &QPushButton::clicked, this, [this] {
        if (onOpenFlameGraph && !m_flame.isEmpty()) onOpenFlameGraph(m_flame);
    });
    buttons->addWidget(m_flameButton);
    m_attach = new QPushButton(QStringLiteral("Attach to card…"));
    m_attach->setToolTip(QStringLiteral("Append this table under a card's `## Profile`, and the "
                                        "evidence directory to its links.evidence"));
    connect(m_attach, &QPushButton::clicked, this, [this] {
        if (onAttachToCard && !m_markdown.isEmpty()) onAttachToCard(m_markdown, m_out);
    });
    buttons->addWidget(m_attach);
    buttons->addStretch(1);
    m_stop = new QPushButton(QStringLiteral("Stop"));
    m_stop->setToolTip(QStringLiteral("Stop the run: the app quits cleanly, a build stops where "
                                      "it is"));
    connect(m_stop, &QPushButton::clicked, this, [this] { stopRun(); });
    buttons->addWidget(m_stop);
    layout->addLayout(buttons);

    updateChrome();
}

QString ProfilePane::paneTitle() const {
    return m_label.isEmpty() ? QStringLiteral("Profile")
                             : QStringLiteral("Profile: %1").arg(m_label);
}

void ProfilePane::focusView() {
    if (m_rows.isEmpty()) m_log->setFocus(Qt::OtherFocusReason);
    else m_table->setFocus(Qt::OtherFocusReason);
}

void ProfilePane::setHeaderRightInset(int pixels) {
    if (m_title) m_title->setContentsMargins(0, 0, qMax(0, pixels), 0);
}

void ProfilePane::startWaitingFor(const QString &target) {
    m_target = target;
    const ProfileTarget *known = profileTarget(target);
    m_label = known ? known->label : target;
    m_state = QStringLiteral("started");
    m_message.clear();
    m_line.clear();
    m_markdown.clear();
    m_out.clear();
    m_flame.clear();
    m_lines.clear();
    m_rows.clear();
    m_kind.clear();
    if (m_log) m_log->clear();
    if (m_model) m_model->refresh();
    updateChrome();
}

void ProfilePane::handleEvent(const QJsonObject &event) {
    if (event.value(QStringLiteral("event")).toString() != QLatin1String("profile")) return;
    const QString target = event.value(QStringLiteral("target")).toString();
    // A refusal that names no target (a bad request) still belongs to whoever is waiting.
    if (!target.isEmpty() && !m_target.isEmpty() && target != m_target) return;
    if (!target.isEmpty()) {
        m_target = target;
        if (const ProfileTarget *known = profileTarget(target)) m_label = known->label;
        else if (!event.value(QStringLiteral("label")).toString().isEmpty())
            m_label = event.value(QStringLiteral("label")).toString();
    }
    const QString state = event.value(QStringLiteral("state")).toString();
    if (state.isEmpty()) return;
    m_state = state;
    if (event.contains(QStringLiteral("out")))
        m_out = event.value(QStringLiteral("out")).toString();
    if (event.contains(QStringLiteral("message")))
        m_message = event.value(QStringLiteral("message")).toString();
    if (state == QLatin1String("progress")) {
        appendLine(event.value(QStringLiteral("line")).toString());
    } else if (state == QLatin1String("started")) {
        appendLine(event.value(QStringLiteral("command")).toString());
    } else {
        const QJsonObject summary = event.value(QStringLiteral("summary")).toObject();
        if (!summary.isEmpty()) applySummary(summary);
        if (!m_message.isEmpty()) appendLine(m_message);
    }
    updateChrome();
    if (onNotice) onNotice(statusText());
}

void ProfilePane::applySummary(const QJsonObject &summary) {
    m_kind = summary.value(QStringLiteral("kind")).toString();
    m_line = summary.value(QStringLiteral("line")).toString();
    m_markdown = summary.value(QStringLiteral("markdown")).toString();
    m_flame = summary.value(QStringLiteral("flame")).toString();
    if (summary.contains(QStringLiteral("out")))
        m_out = summary.value(QStringLiteral("out")).toString();
    m_rows.clear();
    const QJsonArray rows = summary.value(QStringLiteral("rows")).toArray();
    m_rows.reserve(rows.size());
    for (const QJsonValue &value : rows) {
        const QJsonObject object = value.toObject();
        ProfileRow row;
        row.name = object.value(QStringLiteral("name")).toString();
        row.file = object.value(QStringLiteral("file")).toString();
        row.line = object.value(QStringLiteral("line")).toInt();
        row.self = object.value(QStringLiteral("self")).toDouble();
        row.total = object.value(QStringLiteral("total")).toDouble();
        row.selfPct = object.value(QStringLiteral("self_pct")).toDouble();
        row.totalPct = object.value(QStringLiteral("total_pct")).toDouble();
        if (!row.name.isEmpty()) m_rows.append(row);
    }
    if (m_model) m_model->refresh();
    // A build step's `total` is its `self` — there is no call stack — so the column would be the
    // same number twice, and the output paths are what need the room.
    if (m_table) m_table->setColumnHidden(ColTotal, m_kind == QLatin1String("build"));
}

void ProfilePane::appendLine(const QString &line) {
    const QString text = line.trimmed();
    if (text.isEmpty()) return;
    m_lines.append(text);
    while (m_lines.size() > kMaxLines) m_lines.removeFirst();
    if (m_log) {
        m_log->setPlainText(m_lines.join(QStringLiteral("\n")));
        m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
    }
}

QString ProfilePane::headerText() const { return paneTitle(); }

QString ProfilePane::statusText() const {
    if (m_state.isEmpty()) return {};
    if (running())
        return QStringLiteral("Profiling %1 — %2")
            .arg(m_label, m_lines.isEmpty() ? QStringLiteral("starting") : m_lines.last());
    if (m_state == QLatin1String("error"))
        return QStringLiteral("Profile of %1 failed: %2").arg(m_label, m_message);
    if (m_state == QLatin1String("stopped"))
        return QStringLiteral("Profile of %1 stopped").arg(m_label);
    return QStringLiteral("Profile of %1 — %2").arg(m_label, m_line.isEmpty() ? m_message : m_line);
}

void ProfilePane::updateChrome() {
    if (m_title) m_title->setText(paneTitle());
    if (m_summary) {
        QString text = m_line;
        if (text.isEmpty()) text = m_message;
        if (text.isEmpty()) text = running() ? QStringLiteral("Starting…") : QString();
        if (!m_out.isEmpty()) text += QStringLiteral("\n%1").arg(m_out);
        m_summary->setText(text);
    }
    if (m_stop) {
        m_stop->setVisible(running());
        m_stop->setEnabled(running());
    }
    if (m_flameButton) m_flameButton->setEnabled(!m_flame.isEmpty());
    if (m_attach) m_attach->setEnabled(!m_markdown.isEmpty());
    // The log is the whole pane while there is no table yet, and a tail once there is.
    if (m_log) m_log->setMaximumHeight(m_rows.isEmpty() ? 16777215 : 120);
}

void ProfilePane::stopRun() {
    if (!running() || !onSend) return;
    onSend(QJsonObject{{QStringLiteral("type"), QStringLiteral("profile_stop")}});
}

void ProfilePane::refreshTheme() {
    if (m_model) m_model->refresh();
    update();
}

}  // namespace relay::profile
