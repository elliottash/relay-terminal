// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BoardSections.h"

#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <utility>

namespace relay {
namespace board {
namespace {

QString joinTitles(const QStringList &statuses)
{
    QStringList out;
    for (const QString &status : statuses)
        out << statusTitle(status);
    return out.join(QStringLiteral(", "));
}

// The MIME type a section is dragged under: Relay's own, so a drop from another program — a file,
// a URL, a selection — is never mistaken for a section.
const char kSectionMime[] = "application/x-relay-board-section";

// Where an event happened, whichever Qt this is built against (`position()` from 6 on).
QPoint eventPoint(QMouseEvent *event)
{
#if QT_VERSION_MAJOR >= 6
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

QPoint eventPoint(QDropEvent *event)
{
#if QT_VERSION_MAJOR >= 6
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

QPoint eventPoint(QDragMoveEvent *event)
{
#if QT_VERSION_MAJOR >= 6
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

// Whether this drop is a section, and not the one this row already is.
bool isSectionDrop(QDropEvent *event, const QString &id)
{
    return event->mimeData()->hasFormat(kSectionMime)
           && QString::fromUtf8(event->mimeData()->data(kSectionMime)) != id;
}

}  // namespace

SectionRow::SectionRow(const QString &id, QWidget *parent) : QWidget(parent), m_id(id)
{
    setObjectName(QStringLiteral("boardSectionRow"));
    setAcceptDrops(true);
}

void SectionRow::dropHere(const QString &id, int y)
{
    if (id.isEmpty() || id == m_id)
        return;
    m_hover = false;
    update();
    if (onDrop)
        onDrop(id, y < height() / 2);
}

void SectionRow::mousePressEvent(QMouseEvent *event)
{
    if (draggable && event->button() == Qt::LeftButton)
        m_press = eventPoint(event);
    QWidget::mousePressEvent(event);
}

void SectionRow::mouseMoveEvent(QMouseEvent *event)
{
    if (!draggable || m_press.isNull() || !(event->buttons() & Qt::LeftButton))
        return;
    if ((eventPoint(event) - m_press).manhattanLength() < QApplication::startDragDistance())
        return;
    m_press = QPoint();
    auto *drag = new QDrag(this);
    auto *mime = new QMimeData;
    mime->setData(kSectionMime, m_id.toUtf8());
    drag->setMimeData(mime);
    drag->setPixmap(grab());            // the row itself, as the thing being carried
    drag->exec(Qt::MoveAction);
}

void SectionRow::dragEnterEvent(QDragEnterEvent *event)
{
    if (isSectionDrop(event, m_id))
        event->acceptProposedAction();
}

void SectionRow::dragMoveEvent(QDragMoveEvent *event)
{
    if (!isSectionDrop(event, m_id)) {
        event->ignore();
        return;
    }
    m_above = eventPoint(event).y() < height() / 2;
    m_hover = true;
    update();
    event->acceptProposedAction();
}

void SectionRow::dragLeaveEvent(QDragLeaveEvent *)
{
    m_hover = false;
    update();
}

void SectionRow::dropEvent(QDropEvent *event)
{
    if (!isSectionDrop(event, m_id)) {
        event->ignore();
        return;
    }
    const int y = eventPoint(event).y();
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    dropHere(QString::fromUtf8(event->mimeData()->data(kSectionMime)), y);
}

void SectionRow::paintEvent(QPaintEvent *)
{
    if (!m_hover)
        return;
    QPainter painter(this);
    painter.setPen(QPen(theme::Accent, 2));
    const int y = m_above ? 1 : height() - 2;
    painter.drawLine(0, y, width(), y);
}

// --------------------------------------------------------------------------- the plan

SectionPlan SectionPlan::from(const Model &model)
{
    SectionPlan plan;
    plan.m_columns = model.columns();
    plan.m_allStatuses = model.statusChoices();
    const QMap<QString, QString> titles = model.columnTitles();
    const QList<Column> sections = model.sections();
    for (const Column &section : sections) {
        Row row;
        row.id = section.id;
        row.name = titles.value(section.id);
        // `Column::title` is already the board's own name where it has one; the fallback is what
        // the row goes back to when the name is cleared. Done's fallback names dropped too, as
        // its section title does (Model::sectionTitle).
        if (section.id == verifiedSection())
            row.fallbackName = QStringLiteral("Verified");
        else if (section.id == doneSection())
            row.fallbackName = QStringLiteral("Done/Dropped");
        else
            row.fallbackName = statusTitle(section.id);
        row.statuses = section.statuses;
        row.configured = plan.m_columns.contains(section.id);
        // Verified is `done` plus a signature and collects no status of its own; Done is where a
        // card is closed to. Neither is a lane the section list owns, so neither can be taken
        // away — but both can be renamed, because a name is only a name.
        row.fixed = section.id == verifiedSection() || section.id == doneSection();
        plan.m_rows << row;
    }
    return plan;
}

int SectionPlan::indexOf(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).id == id)
            return i;
    return -1;
}

const SectionPlan::Row *SectionPlan::row(const QString &id) const
{
    const int at = indexOf(id);
    return at < 0 ? nullptr : &m_rows.at(at);
}

QString SectionPlan::whyNotRemove(const QString &id) const
{
    const Row *found = row(id);
    if (found == nullptr)
        return QStringLiteral("no such section");
    if (found->fixed)
        return QStringLiteral("Verified and Done are always the last two sections");
    if (!found->configured && !found->added)
        return QStringLiteral("this section is here because cards have that status, not because "
                              "board.yaml lists it — merge it into another one instead");
    return QString();
}

QString SectionPlan::whyNotMerge(const QString &id) const
{
    const Row *found = row(id);
    if (found == nullptr)
        return QStringLiteral("no such section");
    if (found->fixed)
        return QStringLiteral("Verified and Done are always the last two sections");
    if (found->statuses.isEmpty())
        return QStringLiteral("this section collects no status of its own");
    if (mergeTargets(id).isEmpty())
        return QStringLiteral("there is no other section to merge it into");
    return QString();
}

bool SectionPlan::canRemove(const QString &id) const
{
    return whyNotRemove(id).isEmpty();
}

bool SectionPlan::canMerge(const QString &id) const
{
    return whyNotMerge(id).isEmpty();
}

QStringList SectionPlan::mergeTargets(const QString &id) const
{
    QStringList out;
    for (const Row &candidate : m_rows) {
        if (candidate.id == id || candidate.fixed || candidate.statuses.isEmpty())
            continue;
        out << candidate.id;
    }
    return out;
}

QStringList SectionPlan::freeStatuses() const
{
    QStringList taken;
    for (const Row &row : m_rows)
        taken << row.statuses;
    QStringList out;
    for (const QString &status : m_allStatuses)
        if (!taken.contains(status))
            out << status;
    return out;
}

void SectionPlan::rename(const QString &id, const QString &name)
{
    const int at = indexOf(id);
    if (at < 0)
        return;
    const QString clean = name.simplified().left(40);
    if (clean == m_rows.at(at).name)
        return;
    const QString was = m_rows.at(at).title();
    m_rows[at].name = clean;
    m_changes << (clean.isEmpty() ? QStringLiteral("%1 goes back to its own name").arg(was)
                                  : QStringLiteral("%1 → %2").arg(was, clean));
    m_dirty = true;
}

void SectionPlan::remove(const QString &id)
{
    if (!canRemove(id))
        return;
    const int at = indexOf(id);
    const QString was = m_rows.at(at).title();
    // The statuses it collected are not lost and no card moves: with no section collecting them
    // they come back as a section each, which is what the footer promises.
    const QStringList freed = m_rows.at(at).statuses;
    m_rows.removeAt(at);
    m_columns.removeAll(id);
    m_changes << (freed.isEmpty()
                      ? QStringLiteral("%1 taken away").arg(was)
                      : QStringLiteral("%1 taken away (%2 gets a section of its own)")
                            .arg(was, joinTitles(freed)));
    m_dirty = true;
}

void SectionPlan::merge(const QString &from, const QString &into)
{
    if (!canMerge(from) || !mergeTargets(from).contains(into))
        return;
    const int source = indexOf(from);
    const int target = indexOf(into);
    const QString was = m_rows.at(source).title();
    const QString keeps = m_rows.at(target).title();
    QStringList statuses = m_rows.at(target).statuses;
    for (const QString &status : m_rows.at(source).statuses)
        if (!statuses.contains(status))
            statuses << status;
    m_rows[target].statuses = statuses;
    m_rows.removeAt(source);
    m_columns.removeAll(from);
    m_changes << QStringLiteral("%1 merged into %2").arg(was, keeps);
    m_dirty = true;
}

// `columns:` in the order the list draws. Everything the board configures that the plan has a row
// for moves with it; a configured section with no row — `done`, which the list keeps as its last
// section whatever the file says — keeps its place in the file, at the end.
void SectionPlan::syncColumns()
{
    QStringList out;
    for (const Row &row : std::as_const(m_rows))
        if (m_columns.contains(row.id) && !out.contains(row.id))
            out << row.id;
    for (const QString &id : std::as_const(m_columns))
        if (!out.contains(id))
            out << id;
    m_columns = out;
}

QString SectionPlan::whyNotMove(const QString &id, int delta) const
{
    const Row *found = row(id);
    if (found == nullptr)
        return QStringLiteral("no such section");
    if (found->fixed)
        return QStringLiteral("Verified and Done are always the last two sections");
    const int at = indexOf(id);
    const int target = at + delta;
    if (target < 0)
        return QStringLiteral("this is the first section");
    if (target >= m_rows.size() || m_rows.at(target).fixed)
        return QStringLiteral("this is the last section before Verified and Done");
    return QString();
}

bool SectionPlan::canMove(const QString &id, int delta) const
{
    return whyNotMove(id, delta).isEmpty();
}

void SectionPlan::move(const QString &id, int delta)
{
    if (!canMove(id, delta))
        return;
    const int at = indexOf(id);
    const QString other = m_rows.at(at + delta).title();
    const Row row = m_rows.takeAt(at);
    m_rows.insert(at + delta, row);
    syncColumns();
    m_changes << QStringLiteral("%1 moved %2 %3")
                     .arg(row.title(), delta < 0 ? QStringLiteral("above") : QStringLiteral("below"),
                          other);
    m_dirty = true;
}

bool SectionPlan::moveBefore(const QString &id, const QString &beforeId)
{
    const int at = indexOf(id);
    if (at < 0 || m_rows.at(at).fixed)
        return false;
    // The two that are always last stay last: a drop at or under them lands just above them, which
    // is what the row's own drag line shows while the section is held there.
    int limit = m_rows.size();
    while (limit > 0 && m_rows.at(limit - 1).fixed)
        --limit;
    int target = beforeId.isEmpty() ? limit : indexOf(beforeId);
    if (target < 0)
        return false;
    target = qBound(0, target, limit);
    if (target == at || target == at + 1)
        return false;                      // it is already where it was dropped
    const Row row = m_rows.takeAt(at);
    if (target > at)
        --target;                          // the row left before the insertion point
    m_rows.insert(target, row);
    syncColumns();
    m_changes << QStringLiteral("%1 moved %2")
                     .arg(row.title(),
                          target == 0 ? QStringLiteral("to the top")
                                      : QStringLiteral("below %1").arg(m_rows.at(target - 1).title()));
    m_dirty = true;
    return true;
}

QString SectionPlan::idFor(const QString &name)
{
    QString id;
    for (const QChar &character : name.simplified().toLower()) {
        if (character.isLetterOrNumber())
            id += character;
        else if (!id.isEmpty() && !id.endsWith(QLatin1Char('-')))
            id += QLatin1Char('-');
    }
    while (id.endsWith(QLatin1Char('-')))
        id.chop(1);
    // The worker takes lower-case letters, digits, - and _, starting with a letter or a digit.
    return id.left(40);
}

QString SectionPlan::addRefusal(const QString &name, const QStringList &statuses) const
{
    if (name.simplified().isEmpty())
        return QStringLiteral("A new section needs a name.");
    if (idFor(name).isEmpty())
        return QStringLiteral("A section's name needs a letter or a digit in it.");
    // No statuses picked is a manual section (#3XZV): one you fill by hand, parking cards in
    // it with a drop. Nothing is ever in it until something is.
    const QStringList free = freeStatuses();
    for (const QString &status : statuses)
        if (!free.contains(status))
            return QStringLiteral("%1 is already collected by another section, and a status "
                                  "belongs to one section.").arg(statusTitle(status));
    if (indexOf(idFor(name)) >= 0)
        return QStringLiteral("There is already a section called that.");
    return QString();
}

QString SectionPlan::add(const QString &name, const QStringList &statuses)
{
    if (!addRefusal(name, statuses).isEmpty())
        return QString();
    Row row;
    row.id = idFor(name);
    row.name = name.simplified().left(40);
    row.fallbackName = row.name;
    row.statuses = statuses;
    row.configured = true;
    row.added = true;

    // Before the two that are always last, and in `columns:` before a configured `done` so the
    // file reads in the order the list is drawn.
    int at = m_rows.size();
    while (at > 0 && m_rows.at(at - 1).fixed)
        --at;
    m_rows.insert(at, row);
    const int done = m_columns.indexOf(doneSection());
    m_columns.insert(done < 0 ? m_columns.size() : done, row.id);

    m_changes << (statuses.isEmpty()
                      ? QStringLiteral("%1 added, a section you fill by hand").arg(row.name)
                      : QStringLiteral("%1 added, collecting %2").arg(row.name, joinTitles(statuses)));
    m_dirty = true;
    return row.id;
}

QString SectionPlan::summary() const
{
    if (m_changes.isEmpty())
        return QString();
    return m_changes.join(QStringLiteral(" · "));
}

QJsonObject SectionPlan::message() const
{
    QJsonObject out;
    out.insert(QStringLiteral("columns"), QJsonArray::fromStringList(m_columns));

    QJsonObject statuses;
    for (const QString &id : m_columns) {
        const Row *found = row(id);
        // A configured section that is not drawn is Done: it keeps whatever it had, which is the
        // default, so it needs no override. A manual section is drawn with no statuses, and its
        // explicit empty list is the one thing that says it collects nothing — it must be
        // written even though it is empty (#3XZV).
        if (found == nullptr)
            continue;
        if (found->statuses.isEmpty() || found->statuses != defaultSectionStatuses(id))
            statuses.insert(id, QJsonArray::fromStringList(found->statuses));
    }
    out.insert(QStringLiteral("column_statuses"), statuses);

    QJsonObject titles;
    for (const Row &row : m_rows)
        if (!row.name.isEmpty())
            titles.insert(row.id, row.name);
    out.insert(QStringLiteral("column_titles"), titles);
    return out;
}

// --------------------------------------------------------------------------- the page

SectionEditor::SectionEditor(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("boardSectionEditor"));
    setFocusPolicy(Qt::StrongFocus);   // so Esc reaches keyPressEvent when the page opens
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    auto *intro = new QLabel(
        QStringLiteral("Sections are a view of the statuses: nothing here moves a card or changes "
                       "one. A status no section collects gets a section of its own."), this);
    intro->setObjectName(QStringLiteral("boardSectionEditorIntro"));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_rowsHost = new QWidget(scroll);
    m_rowsLayout = new QVBoxLayout(m_rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(6);
    scroll->setWidget(m_rowsHost);
    layout->addWidget(scroll, 1);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("boardSectionEditorSummary"));
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    // The board's folder: its name, and the one action that renames it (protocol 19.17). It sits
    // with the section list because both are "this board, as it is kept" — and nowhere else, so
    // the rename is never a click away from the cards. Hidden until setFolder() names a folder the
    // worker would move.
    m_folderRow = new QWidget(this);
    m_folderRow->setObjectName(QStringLiteral("boardFolderRow"));
    auto *folderCells = new QHBoxLayout(m_folderRow);
    folderCells->setContentsMargins(0, 4, 0, 0);
    folderCells->setSpacing(8);
    m_folderLabel = new QLabel(m_folderRow);
    m_folderLabel->setObjectName(QStringLiteral("boardFolderLabel"));
    m_folderLabel->setWordWrap(true);
    folderCells->addWidget(m_folderLabel, 1);
    m_folderButton = new QPushButton(m_folderRow);
    m_folderButton->setObjectName(QStringLiteral("boardFolderButton"));
    connect(m_folderButton, &QPushButton::clicked, this, [this] {
        if (onFolder)
            onFolder();   // always the same move: to `board/`
    });
    folderCells->addWidget(m_folderButton);
    m_folderRow->hide();
    layout->addWidget(m_folderRow);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("boardSectionEditorCancel"));
    cancel->setToolTip(QStringLiteral("Leave the sections as they are (Esc)"));
    connect(cancel, &QPushButton::clicked, this, [this] {
        if (onClose)
            onClose();
    });
    buttons->addWidget(cancel);
    m_save = new QPushButton(QStringLiteral("Save sections"), this);
    m_save->setObjectName(QStringLiteral("boardSectionEditorSave"));
    m_save->setToolTip(QStringLiteral("Write the section list to this board's board.yaml. It is "
                                      "one write, and Ctrl+Z takes it back."));
    connect(m_save, &QPushButton::clicked, this, [this] {
        if (m_plan.dirty() && onSave)
            onSave(m_plan.message());
    });
    buttons->addWidget(m_save);
    layout->addLayout(buttons);
}

void SectionEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        if (onClose)
            onClose();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SectionEditor::setModel(const Model &model)
{
    m_plan = SectionPlan::from(model);
    m_pendingStatuses.clear();
    m_pendingName.clear();
    rebuild();
}

void SectionEditor::refresh(const Model &model)
{
    setModel(model);
}

void SectionEditor::setFolder(const QString &folderName)
{
    m_folder = folderName;
    // Offered on the two older spellings and on nothing else: `board/` is already where the move
    // would take it, and `issues/` — this repository's own, and every other project that names it
    // in its scripts and hooks — is never moved (the worker refuses it too).
    const bool movable = m_folder == QLatin1String("board") ||
                         m_folder == QLatin1String(".switchboard") ||
                         m_folder == QLatin1String("switchboard");
    if (!movable) {
        // `board/`, `issues/`, or a folder the worker has not named yet: nothing to offer.
        m_folderRow->hide();
        return;
    }
    m_folderLabel->setText(QStringLiteral("This board is kept in %1/.").arg(m_folder));
    m_folderButton->setText(QStringLiteral("Move this board to .board/"));
    m_folderButton->setToolTip(
        QStringLiteral("Rename %1/ to .board/ — git mv in a checkout, a plain rename otherwise; "
                       "refused while a turn runs or a card has uncommitted text").arg(m_folder));
    m_folderRow->show();
}

void SectionEditor::updateFooter()
{
    const QString summary = m_plan.summary();
    m_summary->setText(summary.isEmpty()
                           ? QStringLiteral("Nothing changed yet.")
                           : QStringLiteral("Will write: %1").arg(summary));
    m_save->setEnabled(m_plan.dirty());
}

void SectionEditor::rebuild()
{
    while (QLayoutItem *item = m_rowsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<SectionPlan::Row> rows = m_plan.rows();
    for (const SectionPlan::Row &row : rows) {
        auto *line = new SectionRow(row.id, m_rowsHost);
        // Verified and Done stay last: they cannot be dragged, and their buttons are off.
        line->draggable = !row.fixed;
        line->onDrop = [this, section = row.id](const QString &dragged, bool above) {
            // Where the section goes: in front of this row, or in front of the one under it.
            const QList<SectionPlan::Row> order = m_plan.rows();
            QString before;
            for (int i = 0; i < order.size(); ++i) {
                if (order.at(i).id != section)
                    continue;
                const int next = above ? i : i + 1;
                before = next < order.size() ? order.at(next).id : QString();
                break;
            }
            if (m_plan.moveBefore(dragged, before))
                // Not from inside the drop: the rebuild deletes the row the drop landed on, which
                // Qt's drag machinery is still holding.
                QTimer::singleShot(0, this, [this] { rebuild(); });
            else
                updateFooter();
        };
        auto *cells = new QHBoxLayout(line);
        cells->setContentsMargins(0, 0, 0, 0);
        cells->setSpacing(6);

        // The handle: press it and drag to move the section. A QLabel takes no press of its own, so
        // the row underneath starts the drag.
        auto *handle = new QLabel(QStringLiteral("⠿"), line);
        handle->setObjectName(QStringLiteral("boardSectionHandle"));
        handle->setCursor(Qt::OpenHandCursor);
        handle->setToolTip(row.fixed
                               ? QStringLiteral("Verified and Done are always the last two sections")
                               : QStringLiteral("Drag this section to move it, or use ▲ and ▼"));
        handle->setVisible(!row.fixed);
        cells->addWidget(handle);

        auto *name = new QLineEdit(row.title(), line);
        name->setObjectName(QStringLiteral("boardSectionName"));
        name->setToolTip(QStringLiteral("What this section is called. The id (%1) does not change, "
                                        "so no card is touched; clear the box for Relay's own name.")
                             .arg(row.id));
        const QString id = row.id, fallback = row.fallbackName;
        connect(name, &QLineEdit::editingFinished, this, [this, name, id, fallback] {
            const QString typed = name->text().simplified();
            m_plan.rename(id, typed == fallback ? QString() : typed);
            updateFooter();
        });
        cells->addWidget(name, 1);

        // The status *ids*, not their titles: for a one-status section the titles only repeat the
        // name beside them ("Inbox  Inbox"), and the id is the thing a rename does not change —
        // which is the whole point of the column.
        auto *collects = new QLabel(row.statuses.isEmpty()
                                        ? (row.fixed ? QStringLiteral("closed and signed")
                                                     : QStringLiteral("manual"))
                                        : row.statuses.join(QStringLiteral(", ")), line);
        collects->setObjectName(QStringLiteral("boardSectionStatuses"));
        collects->setToolTip(row.statuses.isEmpty()
                                 ? (row.fixed
                                        ? QStringLiteral("Verified is `done` plus a signature: no "
                                                         "status of its own, and nothing can be "
                                                         "moved into it.")
                                        : QStringLiteral("A section you fill by hand: cards are "
                                                         "parked in it with a drop and their "
                                                         "status stays what it was (#3XZV)."))
                                 : QStringLiteral("The statuses this section collects: %1")
                                       .arg(joinTitles(row.statuses)));
        cells->addWidget(collects, 2);

        auto *merge = new QToolButton(line);
        merge->setObjectName(QStringLiteral("boardSectionMerge"));
        merge->setText(QStringLiteral("Merge…"));
        merge->setPopupMode(QToolButton::InstantPopup);
        merge->setEnabled(m_plan.canMerge(row.id));
        merge->setToolTip(m_plan.canMerge(row.id)
                              ? QStringLiteral("Put this section's statuses into another one and "
                                               "take this one away. No card moves.")
                              : m_plan.whyNotMerge(row.id));
        if (merge->isEnabled()) {
            auto *menu = new QMenu(merge);
            for (const QString &target : m_plan.mergeTargets(row.id)) {
                const SectionPlan::Row *into = m_plan.row(target);
                menu->addAction(QStringLiteral("into %1").arg(into->title()), this,
                                [this, id = row.id, target] {
                                    m_plan.merge(id, target);
                                    rebuild();
                                });
            }
            merge->setMenu(menu);
        }
        cells->addWidget(merge);

        // Moving the section in the list (owner, 2026-09-19: "with up and down buttons for moving
        // them"): the same verb as the drag, for a short move and for the keyboard.
        for (const int delta : {-1, 1}) {
            auto *button = new QToolButton(line);
            button->setObjectName(delta < 0 ? QStringLiteral("boardSectionUp")
                                            : QStringLiteral("boardSectionDown"));
            button->setText(delta < 0 ? QStringLiteral("▲") : QStringLiteral("▼"));
            button->setEnabled(m_plan.canMove(row.id, delta));
            button->setToolTip(m_plan.canMove(row.id, delta)
                                   ? QStringLiteral("Move this section %1 one place")
                                         .arg(delta < 0 ? QStringLiteral("up")
                                                        : QStringLiteral("down"))
                                   : m_plan.whyNotMove(row.id, delta));
            connect(button, &QToolButton::clicked, this, [this, id, delta] {
                m_plan.move(id, delta);
                rebuild();
            });
            cells->addWidget(button);
        }

        auto *remove = new QToolButton(line);
        remove->setObjectName(QStringLiteral("boardSectionRemove"));
        remove->setText(QStringLiteral("✕"));
        remove->setEnabled(m_plan.canRemove(row.id));
        remove->setToolTip(m_plan.canRemove(row.id)
                               ? QStringLiteral("Take this section away. Its cards stay exactly "
                                                "where they are and come back in a section of "
                                                "their own.")
                               : m_plan.whyNotRemove(row.id));
        connect(remove, &QToolButton::clicked, this, [this, id = row.id] {
            m_plan.remove(id);
            rebuild();
        });
        cells->addWidget(remove);
        m_rowsLayout->addWidget(line);
    }

    // The add row, last, because it is the only one that is not a section yet.
    const QStringList free = m_plan.freeStatuses();
    auto *adder = new QWidget(m_rowsHost);
    adder->setObjectName(QStringLiteral("boardSectionAddRow"));
    auto *cells = new QHBoxLayout(adder);
    cells->setContentsMargins(0, 6, 0, 0);
    cells->setSpacing(6);

    auto *name = new QLineEdit(m_pendingName, adder);
    name->setObjectName(QStringLiteral("boardSectionNewName"));
    name->setPlaceholderText(QStringLiteral("New section"));
    connect(name, &QLineEdit::textChanged, this,
            [this](const QString &text) { m_pendingName = text; });
    cells->addWidget(name, 1);

    auto *statuses = new QToolButton(adder);
    statuses->setObjectName(QStringLiteral("boardSectionNewStatuses"));
    statuses->setPopupMode(QToolButton::InstantPopup);
    statuses->setText(m_pendingStatuses.isEmpty()
                          ? QStringLiteral("Collects…")
                          : joinTitles(m_pendingStatuses));
    // Always enabled (#3XZV): picking none is a manual section, one you fill by hand, so the
    // choice is live even when every status is already collected somewhere.
    statuses->setToolTip(free.isEmpty()
                             ? QStringLiteral("Every status already belongs to a section. Pick "
                                              "none for a section you fill by hand, or take one "
                                              "away or merge two to free some up.")
                             : QStringLiteral("Which statuses the new section collects. A status "
                                              "belongs to one section, so only the free ones are "
                                              "here — pick none for a section you fill by hand."));
    if (!free.isEmpty()) {
        auto *menu = new QMenu(statuses);
        for (const QString &status : free) {
            QAction *action = menu->addAction(statusTitle(status));
            action->setCheckable(true);
            action->setChecked(m_pendingStatuses.contains(status));
            connect(action, &QAction::toggled, this, [this, status, statuses](bool on) {
                if (on && !m_pendingStatuses.contains(status))
                    m_pendingStatuses << status;
                else if (!on)
                    m_pendingStatuses.removeAll(status);
                statuses->setText(m_pendingStatuses.isEmpty()
                                      ? QStringLiteral("Collects…")
                                      : joinTitles(m_pendingStatuses));
            });
        }
        statuses->setMenu(menu);
    }
    cells->addWidget(statuses, 2);

    auto *add = new QPushButton(QStringLiteral("Add section"), adder);
    add->setObjectName(QStringLiteral("boardSectionAdd"));
    connect(add, &QPushButton::clicked, this, [this] {
        const QString refusal = m_plan.addRefusal(m_pendingName, m_pendingStatuses);
        if (!refusal.isEmpty()) {
            m_summary->setText(refusal);
            return;
        }
        m_plan.add(m_pendingName, m_pendingStatuses);
        m_pendingName.clear();
        m_pendingStatuses.clear();
        rebuild();
    });
    cells->addWidget(add);
    m_rowsLayout->addWidget(adder);
    m_rowsLayout->addStretch(1);
    updateFooter();
}

}  // namespace board
}  // namespace relay
