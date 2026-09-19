// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BoardSections.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

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

}  // namespace

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
        // the row goes back to when the name is cleared.
        row.fallbackName = section.id == verifiedSection() ? QStringLiteral("Verified")
                                                           : statusTitle(section.id);
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
    m_changes << QStringLiteral("%1 taken away (%2 gets a section of its own)")
                     .arg(was, joinTitles(freed));
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
    if (statuses.isEmpty())
        return QStringLiteral("A new section needs at least one status to collect — otherwise "
                              "nothing would ever be in it.");
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

    m_changes << QStringLiteral("%1 added, collecting %2").arg(row.name, joinTitles(statuses));
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
        // default, so it needs no override.
        if (found == nullptr || found->statuses.isEmpty())
            continue;
        if (found->statuses != defaultSectionStatuses(id))
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
        auto *line = new QWidget(m_rowsHost);
        line->setObjectName(QStringLiteral("boardSectionRow"));
        auto *cells = new QHBoxLayout(line);
        cells->setContentsMargins(0, 0, 0, 0);
        cells->setSpacing(6);

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
                                        ? QStringLiteral("closed and signed")
                                        : row.statuses.join(QStringLiteral(", ")), line);
        collects->setObjectName(QStringLiteral("boardSectionStatuses"));
        collects->setToolTip(row.statuses.isEmpty()
                                 ? QStringLiteral("Verified is `done` plus a signature: no status "
                                                  "of its own, and nothing can be moved into it.")
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
    statuses->setEnabled(!free.isEmpty());
    statuses->setToolTip(free.isEmpty()
                             ? QStringLiteral("Every status already belongs to a section. Take one "
                                              "away or merge two to free some up.")
                             : QStringLiteral("Which statuses the new section collects. A status "
                                              "belongs to one section, so only the free ones are "
                                              "here."));
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
