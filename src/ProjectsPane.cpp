// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ProjectsPane.h"
#include "ProjectPicker.h"
#include <QDateTime>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QHash>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace relay::projects {
namespace {
constexpr int Path = Qt::UserRole, Kind = Path + 1, Session = Path + 2;
QString stamp(qint64 seconds) {
    return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds).toLocalTime().toString(QStringLiteral("d MMM yyyy, HH:mm")) : QStringLiteral("never");
}
}
ProjectsPane::ProjectsPane(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("projectsPane"));
    m_pinned = QSettings().value(QStringLiteral("projects/pinned")).toStringList();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *top = new QHBoxLayout;
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("projectsSearch"));
    m_search->setPlaceholderText(QStringLiteral("Search projects…"));
    m_search->setClearButtonEnabled(true);
    setFocusProxy(m_search);
    top->addWidget(m_search, 1);
    auto *browse = new QPushButton(QStringLiteral("Open folder…"));
    browse->setObjectName(QStringLiteral("projectsBrowse"));
    auto *init = new QPushButton(QStringLiteral("Initialize…"));
    init->setObjectName(QStringLiteral("projectsInit"));
    top->addWidget(browse); top->addWidget(init); layout->addLayout(top);
    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("projectsTree"));
    m_tree->setHeaderLabels({QStringLiteral("Project / active session"), QStringLiteral("Location / status")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_tree, 1);
    m_details = new QLabel(QStringLiteral("Choose a project to see its details."));
    m_details->setObjectName(QStringLiteral("projectsDetails"));
    m_details->setTextFormat(Qt::PlainText); m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_details);
    auto *actions = new QGridLayout;
    int actionIndex = 0;
    const auto button = [actions, &actionIndex](const QString &text, const QString &name) {
        auto *b = new QPushButton(text); b->setObjectName(name);
        actions->addWidget(b, actionIndex / 3, actionIndex % 3); ++actionIndex; return b;
    };
    m_open = button(QStringLiteral("Open in new tab"), QStringLiteral("projectsOpen"));
    m_attach = button(QStringLiteral("Attach this tab"), QStringLiteral("projectsAttach"));
    m_board = button(QStringLiteral("Switchboard"), QStringLiteral("projectsBoard"));
    m_history = button(QStringLiteral("Sessions"), QStringLiteral("projectsSessions"));
    m_resume = button(QStringLiteral("Go to session"), QStringLiteral("projectsResume"));
    m_allow = button(QStringLiteral("Allow initialization"), QStringLiteral("projectsUndecline"));
    m_more = new QToolButton; m_more->setText(QStringLiteral("More"));
    m_more->setObjectName(QStringLiteral("projectsMore"));
    m_more->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_more); m_more->setMenu(menu);
    auto *pin = menu->addAction(QStringLiteral("Pin project"));
    auto *forget = menu->addAction(QStringLiteral("Forget project"));
    forget->setToolTip(QStringLiteral("Remove from this list; keep files and attached tabs."));
    actions->addWidget(m_more, 2, 2, Qt::AlignRight); layout->addLayout(actions);
    connect(menu, &QMenu::aboutToShow, this, [this, pin] { pin->setText(m_pinned.contains(selectedPath()) ? QStringLiteral("Unpin project") : QStringLiteral("Pin project")); });
    connect(pin, &QAction::triggered, this, [this] {
        const QString path = selectedPath(); if (path.isEmpty()) return;
        if (m_pinned.contains(path)) m_pinned.removeAll(path); else m_pinned.append(path);
        QSettings().setValue(QStringLiteral("projects/pinned"), m_pinned); rebuild();
    });
    connect(forget, &QAction::triggered, this, [this] { if (onForget) onForget(selectedPath()); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] { selectionChanged(); });
    connect(m_open, &QPushButton::clicked, this, [this] { if (onOpenProject) onOpenProject(selectedPath()); });
    connect(m_attach, &QPushButton::clicked, this, [this] { if (onAttachProject) onAttachProject(selectedPath()); });
    connect(m_board, &QPushButton::clicked, this, [this] { if (onOpenBoard) onOpenBoard(selectedPath()); });
    connect(m_history, &QPushButton::clicked, this, [this] { if (onShowSessions) onShowSessions(selectedPath()); });
    connect(m_allow, &QPushButton::clicked, this, [this] { if (onUndecline) onUndecline(selectedPath()); });
    connect(m_resume, &QPushButton::clicked, this, [this] {
        if (auto *row = m_tree->currentItem(); row && onResume) onResume(row->data(0, Session).toJsonObject());
    });
    connect(browse, &QPushButton::clicked, this, [this] { if (onBrowse) onBrowse(); });
    connect(init, &QPushButton::clicked, this, [this] { if (onInit) onInit(); });
    rebuild();
}
void ProjectsPane::setProjects(const QList<Record> &projects) { m_projects = projects; rebuild(); }
void ProjectsPane::setDeclined(const QStringList &paths) { m_declined = paths; rebuild(); }
void ProjectsPane::setActiveSessions(const QJsonArray &sessions) { if (m_sessions == sessions) return; m_sessions = sessions; rebuild(); }
void ProjectsPane::focusSearch() { m_search->setFocus(); m_search->selectAll(); }
QString ProjectsPane::selectedPath() const { auto *row = m_tree->currentItem(); return row ? row->data(0, Path).toString() : QString(); }
QString ProjectsPane::agentScreen() const {
    return QStringLiteral("Projects view\nSearch: %1\nSelected: %2\n%3").arg(m_search->text(), selectedPath(), m_details->text());
}
void ProjectsPane::rebuild() {
    const QString keep = selectedPath();
    const auto *old = m_tree->currentItem();
    const QString keepKind = old ? old->data(0, Kind).toString() : QString();
    const QString keepSession = old ? old->data(0, Session).toJsonObject().value(QStringLiteral("session_id")).toString() : QString();
    QSet<QString> collapsed;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *row = m_tree->topLevelItem(i);
        if (!row->isExpanded()) collapsed.insert(row->data(0, Path).toString());
    }
    m_tree->clear();
    auto ranked = rankProjects(m_projects, m_search->text());
    std::stable_sort(ranked.begin(), ranked.end(), [this](const Record &a, const Record &b) {
        if (m_pinned.contains(a.path) != m_pinned.contains(b.path)) return m_pinned.contains(a.path);
        return m_search->text().trimmed().isEmpty() && a.lastAttached > b.lastAttached;
    });
    QTreeWidgetItem *selected = nullptr;
    const auto add = [&](const QString &name, const QString &path, const QString &kind, const QString &detail) {
        auto *row = new QTreeWidgetItem(m_tree, {name, detail});
        row->setData(0, Path, path); row->setData(0, Kind, kind);
        row->setExpanded(!collapsed.contains(path));
        if (path == keep && kind == keepKind) selected = row;
        return row;
    };
    QHash<QString, QTreeWidgetItem *> rows;
    for (const auto &r : ranked) rows.insert(r.path, add((m_pinned.contains(r.path) ? QStringLiteral("★ ") : QString()) + (r.name.isEmpty() ? nameFor(r.path) : r.name), r.path, QStringLiteral("project"), r.path));
    auto *none = add(QStringLiteral("No project"), {}, QStringLiteral("none"), QStringLiteral("Sessions outside known projects"));
    for (const auto &value : m_sessions) {
        const auto s = value.toObject();
        const QString path = s.value(QStringLiteral("project_path")).toString(s.value(QStringLiteral("workspace")).toString());
        auto *parent = path.isEmpty() ? none : rows.value(path, nullptr);
        if (!parent) continue;
        auto *row = new QTreeWidgetItem(parent, {s.value(QStringLiteral("title")).toString(QStringLiteral("Untitled session")), s.value(QStringLiteral("status")).toString(QStringLiteral("Open"))});
        row->setData(0, Path, path); row->setData(0, Kind, QStringLiteral("session")); row->setData(0, Session, s);
        if (keepKind == QLatin1String("session") && s.value(QStringLiteral("session_id")).toString() == keepSession) selected = row;
    }
    for (const auto &path : m_declined) {
        if (!m_search->text().isEmpty() && !path.contains(m_search->text(), Qt::CaseInsensitive)) continue;
        add(nameFor(path), path, QStringLiteral("declined"), QStringLiteral("Initialization declined · %1").arg(path));
    }
    if (selected) m_tree->setCurrentItem(selected);
    selectionChanged();
}
void ProjectsPane::selectionChanged() {
    auto *row = m_tree->currentItem();
    const QString kind = row ? row->data(0, Kind).toString() : QString();
    const bool project = kind == QLatin1String("project"), session = kind == QLatin1String("session");
    m_open->setEnabled(project); m_attach->setEnabled(project); m_board->setEnabled(project);
    m_more->setEnabled(project); m_history->setEnabled(project || session || kind == QLatin1String("none"));
    m_resume->setVisible(session); m_allow->setVisible(kind == QLatin1String("declined"));
    m_details->setText(row ? row->text(1) : QStringLiteral("Choose a project to see its details. Open folder to add one."));
    if (project) for (const auto &r : m_projects) if (r.path == selectedPath()) {
        m_details->setText(QStringLiteral("%1\n%2 · Last attached: %3\nKnown because %4 · %5")
            .arg(r.path, r.board == QLatin1String(kBoardRepo) ? QStringLiteral("Switchboard available") : QStringLiteral("No Switchboard yet"), stamp(r.lastAttached), reasonText(r.reason), stamp(r.knownSince)));
        break;
    }
}
}
