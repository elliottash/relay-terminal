// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ProjectPicker.h"

#include "AppPaths.h"   // relayFuzzyScore

#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace relay::projects {

namespace {
constexpr int kPathRole = Qt::UserRole;
constexpr int kInitRole = Qt::UserRole + 1;

// `~` for the home directory, so a row is "~/src/widgetworks" rather than the whole path.
QString tilde(const QString &path)
{
    const QString home = QDir::homePath();
    if (!home.isEmpty() && (path == home || path.startsWith(home + QLatin1Char('/'))))
        return QLatin1Char('~') + path.mid(home.size());
    return path;
}
}  // namespace

QList<Record> rankProjects(const QList<Record> &known, const QString &filter)
{
    const QString needle = filter.trimmed();
    if (needle.isEmpty()) return known;
    struct Scored { int name, path; Record record; };
    QList<Scored> scored;
    for (const Record &record : known) {
        const int name = relayFuzzyScore(needle, record.name.isEmpty() ? nameFor(record.path) : record.name);
        const int path = relayFuzzyScore(needle, record.path);
        if (name <= 0 && path <= 0) continue;
        scored.append({name, path, record});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        if (a.name != b.name) return a.name > b.name;
        return a.path > b.path;
    });
    QList<Record> out;
    for (const Scored &entry : scored) out.append(entry.record);
    return out;
}

QString reasonText(const QString &reason)
{
    if (reason == QLatin1String(kReasonSwitchboard)) return QStringLiteral("opened its Switchboard");
    if (reason == QLatin1String(kReasonCardCommand)) return QStringLiteral("/card was run in it");
    if (reason == QLatin1String(kReasonCardPicker)) return QStringLiteral("a card was picked with #");
    if (reason == QLatin1String(kReasonExecuteCard)) return QStringLiteral("one of its cards was executed");
    if (reason == QLatin1String(kReasonPicker)) return QStringLiteral("chosen in the project picker");
    if (reason == QLatin1String(kReasonAgentWrite)) return QStringLiteral("an agent wrote to its board");
    if (reason == QLatin1String(kReasonRestored)) return QStringLiteral("brought back by a saved layout");
    if (reason == QLatin1String(kReasonRepoBoard)) return QStringLiteral("its board was already in the repository");
    if (reason == QLatin1String(kReasonInitCommand)) return QStringLiteral("initialized with /init");
    if (reason == QLatin1String(kReasonAgentCard)) return QStringLiteral("an agent filed a card against it");
    if (reason == QLatin1String(kReasonAgentWork)) return QStringLiteral("the agent was set to work in it");
    return reason;
}

QString agoText(qint64 unixSeconds, qint64 now)
{
    if (unixSeconds <= 0) return {};
    const qint64 seconds = std::max<qint64>(0, now - unixSeconds);
    if (seconds < 60) return QStringLiteral("just now");
    if (seconds < 3600) return QStringLiteral("%1 min ago").arg(seconds / 60);
    if (seconds < 86400) {
        const qint64 hours = seconds / 3600;
        return hours == 1 ? QStringLiteral("an hour ago") : QStringLiteral("%1 hours ago").arg(hours);
    }
    const qint64 days = seconds / 86400;
    if (days == 1) return QStringLiteral("yesterday");
    if (days < 30) return QStringLiteral("%1 days ago").arg(days);
    return QDateTime::fromSecsSinceEpoch(unixSeconds).date().toString(QStringLiteral("d MMM yyyy"));
}

ProjectPicker::ProjectPicker(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("projectPicker"));
    setFocusPolicy(Qt::StrongFocus);

    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("projectPickerSearch"));
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(QStringLiteral("Type to filter the projects Relay knows · Enter attaches · Esc closes"));
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    // The pane chrome's hover buttons sit over the top-right corner: keep the row's end clear.
    m_inset = new QWidget;
    m_inset->setFixedSize(0, 1);
    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(6);
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_inset);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("projectPickerList"));
    m_list->setFocusPolicy(Qt::NoFocus);   // the search field keeps the keyboard; arrows reach the list through it
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        m_list->setCurrentItem(item);
        accept();
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { m_list->setCurrentItem(item); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this] { updateButtons(); });

    m_empty = new QLabel(QStringLiteral("No project Relay knows matches. Enter initializes one here instead."));
    m_empty->setObjectName(QStringLiteral("dialogHint"));
    m_empty->setWordWrap(true);
    m_empty->setVisible(false);

    m_hint = new QLabel(QStringLiteral("↑ ↓ choose · Enter attaches this tab, or initializes a project here · Esc closes"));
    m_hint->setObjectName(QStringLiteral("dialogHint"));
    m_hint->setWordWrap(true);

    m_init = new QPushButton(QStringLiteral("Initialize here"));
    m_init->setObjectName(QStringLiteral("projectPickerInit"));
    m_init->setToolTip(QStringLiteral("Make this pane's directory a project: its Switchboard folder, and git init "
                                      "when it is not inside a repository yet"));
    connect(m_init, &QPushButton::clicked, this, [this] { if (onInitHere) onInitHere(); });
    m_attach = new QPushButton(QStringLiteral("Attach"));
    m_attach->setObjectName(QStringLiteral("projectPickerAttach"));
    m_attach->setDefault(true);
    connect(m_attach, &QPushButton::clicked, this, [this] { accept(); });
    m_close = new QPushButton(QStringLiteral("Close"));
    m_close->setObjectName(QStringLiteral("projectPickerClose"));
    connect(m_close, &QPushButton::clicked, this, [this] { if (onClose) onClose(); });
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(m_init);
    buttons->addStretch(1);
    buttons->addWidget(m_close);
    buttons->addWidget(m_attach);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(8, 8, 8, 8);
    box->setSpacing(6);
    box->addLayout(searchRow);
    box->addWidget(m_empty);
    box->addWidget(m_list, 1);
    box->addWidget(m_hint);
    box->addLayout(buttons);
    rebuild();
}

void ProjectPicker::setProjects(const QList<Record> &known)
{
    m_known = known;
    rebuild();
}

void ProjectPicker::setDefaultProject(const QString &path)
{
    m_default = normalize(path);
    rebuild();
}

void ProjectPicker::setHere(const QString &cwd)
{
    m_here = cwd;
    rebuild();
}

QString ProjectPicker::filter() const { return m_search->text(); }

void ProjectPicker::setFilter(const QString &text) { m_search->setText(text); }

QStringList ProjectPicker::visiblePaths() const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i) out << m_list->item(i)->data(kPathRole).toString();
    return out;
}

QString ProjectPicker::selectedPath() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(kPathRole).toString() : QString();
}

bool ProjectPicker::initRowSelected() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item && item->data(kInitRole).toBool();
}

void ProjectPicker::accept()
{
    const QListWidgetItem *item = m_list->currentItem();
    if (!item) return;
    if (item->data(kInitRole).toBool()) {
        if (onInitHere) onInitHere();
        return;
    }
    const QString path = item->data(kPathRole).toString();
    if (!path.isEmpty() && onPick) onPick(path);
}

QString ProjectPicker::paneTitle() const { return QStringLiteral("Projects"); }

void ProjectPicker::focusView()
{
    m_search->setFocus(Qt::OtherFocusReason);
}

void ProjectPicker::setHeaderRightInset(int pixels)
{
    m_inset->setFixedSize(std::max(0, pixels), 1);
    m_inset->setVisible(pixels > 0);
}

void ProjectPicker::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        event->accept();
        if (onClose) onClose();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        event->accept();
        accept();
        return;
    case Qt::Key_Up: event->accept(); step(-1); return;
    case Qt::Key_Down: event->accept(); step(1); return;
    default: break;
    }
    QWidget::keyPressEvent(event);
}

// The keys the list answers to while the search field has the keyboard, so "type, ↓, Enter" never
// leaves the field: the same shape as the `@` and `#` pickers in the composer.
bool ProjectPicker::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_search && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Escape:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            if (key->key() == Qt::Key_PageUp) step(-8);
            else if (key->key() == Qt::Key_PageDown) step(8);
            else keyPressEvent(key);
            return true;
        default: break;
        }
    }
    return QWidget::eventFilter(object, event);
}

void ProjectPicker::step(int delta)
{
    if (m_list->count() == 0) return;
    const int current = std::max(0, m_list->currentRow());
    m_list->setCurrentRow(std::clamp(current + delta, 0, m_list->count() - 1));
}

void ProjectPicker::updateButtons()
{
    const bool init = initRowSelected();
    m_attach->setEnabled(m_list->currentItem() != nullptr && !init);
    m_attach->setDefault(!init);
    m_init->setVisible(!m_here.isEmpty());
    m_init->setDefault(init);
}

void ProjectPicker::rebuild()
{
    const QString needle = m_search->text().trimmed();
    m_list->clear();
    // Always first, whatever is typed: it is the default answer in a directory nobody has made a
    // project of yet, and the fallback when nothing else matches.
    QListWidgetItem *initRow = nullptr;
    if (!m_here.isEmpty()) {
        initRow = new QListWidgetItem(QStringLiteral("＋  Initialize new project here    %1").arg(tilde(m_here)), m_list);
        initRow->setData(kPathRole, QString());
        initRow->setData(kInitRole, true);
        initRow->setToolTip(QStringLiteral("Creates %1/%2 and, when %1 is not inside a git repository, runs git init there")
                                .arg(m_here, newBoardFolder()));
        QFont bold = initRow->font();
        bold.setBold(true);
        initRow->setFont(bold);
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QListWidgetItem *defaultRow = nullptr, *firstProject = nullptr;
    for (const Record &record : rankProjects(m_known, needle)) {
        const QString name = record.name.isEmpty() ? nameFor(record.path) : record.name;
        QStringList notes;
        if (!record.reason.isEmpty()) notes << reasonText(record.reason);
        if (const QString ago = agoText(record.lastAttached, now); !ago.isEmpty()) notes << ago;
        const bool isDefault = !m_default.isEmpty() && record.path == m_default;
        if (isDefault) notes << QStringLiteral("default for loose cards");
        auto *row = new QListWidgetItem(QStringLiteral("%1    %2%3").arg(name, tilde(record.path),
                                                                          notes.isEmpty() ? QString()
                                                                                          : QStringLiteral("   ·   ") + notes.join(QStringLiteral(" · "))),
                                        m_list);
        row->setData(kPathRole, record.path);
        row->setData(kInitRole, false);
        row->setToolTip(record.path);
        if (!firstProject) firstProject = row;
        if (isDefault) defaultRow = row;
    }
    // What Enter does before anything is chosen: with nothing typed, the default project when one
    // is set, else "initialize here"; with a filter, its best match — so typing three letters and
    // Enter attaches — and the init row only when nothing matches.
    QListWidgetItem *select = nullptr;
    if (needle.isEmpty()) select = defaultRow ? defaultRow : initRow ? initRow : firstProject;
    else select = firstProject ? firstProject : initRow;
    if (select) m_list->setCurrentItem(select);
    m_empty->setVisible(!needle.isEmpty() && !firstProject);
    updateButtons();
}

}  // namespace relay::projects
