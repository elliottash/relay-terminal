// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SkillRegistryView.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace relay::skills {

namespace {

// A path under the user's home is shown tilde-first, as the rest of the app does.
QString tildeHome(const QString &path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home) ? QStringLiteral("~") + path.mid(home.length()) : path;
}

// `pass_rate_30` is a 0–1 share or null (nothing decided yet); the list and the page must say
// the same thing, so this is one function for both.
QString passRateText(const QJsonObject &stats)
{
    const QJsonValue rate = stats.value(QStringLiteral("pass_rate_30"));
    if (rate.isNull() || rate.isUndefined())
        return QStringLiteral("—");
    return QStringLiteral("%1%").arg(qRound(rate.toDouble() * 100));
}

}  // namespace

QString importUrl(const QString &skillPath)
{
    QFile manifest(QFileInfo(skillPath).dir().absoluteFilePath(QStringLiteral("../.relay-import.json")));
    if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 65536)
        return {};
    return QJsonDocument::fromJson(manifest.readAll()).object().value(QStringLiteral("url")).toString();
}

QJsonObject askImport(QWidget *parent)
{
    QDialog ask(parent);
    ask.setWindowTitle(QStringLiteral("Import skills"));
    auto *form = new QFormLayout(&ask);
    auto *url = new QLineEdit;
    url->setObjectName(QStringLiteral("importUrl"));
    url->setPlaceholderText(QStringLiteral("https://github.com/org/skills"));
    auto *ref = new QLineEdit;
    ref->setPlaceholderText(QStringLiteral("default branch"));
    form->addRow(QStringLiteral("Repository URL"), url);
    form->addRow(QStringLiteral("Branch, tag or commit"), ref);
    form->addRow(new QLabel(QStringLiteral("Relay clones it into a temporary folder and shows the skills for review. Nothing is enabled until you confirm.")));
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Preview"));
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    if (ask.exec() != QDialog::Accepted || url->text().trimmed().isEmpty())
        return {};
    QJsonObject request{{"type", "import_skills_preview"}, {"url", url->text().trimmed()}};
    if (!ref->text().trimmed().isEmpty())
        request.insert(QStringLiteral("ref"), ref->text().trimmed());
    return request;
}

QJsonObject reviewImport(QWidget *parent, const QJsonObject &event, QString *status)
{
    const QString url = event.value(QStringLiteral("url")).toString();
    const QString commit = event.value(QStringLiteral("commit")).toString();
    const auto items = event.value(QStringLiteral("items")).toArray();
    const auto skippedItems = event.value(QStringLiteral("skipped")).toArray();
    if (!event.value(QStringLiteral("error")).toString().isEmpty() || items.isEmpty()) {
        *status = QStringLiteral("Import preview: %1").arg(event.value(QStringLiteral("error")).toString(QStringLiteral("no skills found in ") + url));
        return {};
    }
    QDialog review(parent);
    review.setObjectName(QStringLiteral("skillsImportReview"));
    review.setWindowTitle(QStringLiteral("Review skills to import"));
    review.resize(760, 460);
    auto *layout = new QVBoxLayout(&review);
    auto *head = new QLabel(QStringLiteral("%1 @ %2%3\nCheck the skills to import. Expand a skill to see its files; skills can include scripts the agent may run.%4")
                                .arg(url, commit.left(12), event.value(QStringLiteral("ref")).toString().isEmpty() ? QString() : QStringLiteral(" (") + event.value(QStringLiteral("ref")).toString() + ')',
                                     skippedItems.isEmpty() ? QString() : QStringLiteral("\n%1 folder(s) skipped (symlinks, missing descriptions or duplicates).").arg(skippedItems.size())));
    head->setWordWrap(true);
    layout->addWidget(head);
    auto *tree = new QTreeWidget;
    tree->setHeaderLabels({QStringLiteral("Skill"), QStringLiteral("Description")});
    tree->setColumnWidth(0, 260);
    for (const auto &value : items) {
        const QJsonObject skill = value.toObject();
        auto *row = new QTreeWidgetItem(tree);
        row->setText(0, skill.value(QStringLiteral("name")).toString());
        row->setText(1, skill.value(QStringLiteral("description")).toString());
        row->setToolTip(1, skill.value(QStringLiteral("description")).toString());
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(0, Qt::Checked);
        for (const auto &file : skill.value(QStringLiteral("files")).toArray()) {
            auto *child = new QTreeWidgetItem(row);
            child->setText(0, file.isObject() ? file.toObject().value(QStringLiteral("path")).toString() : file.toString());
            child->setFlags(Qt::ItemIsEnabled);
        }
    }
    layout->addWidget(tree, 1);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Import checked"));
    layout->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &review, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &review, &QDialog::reject);
    if (review.exec() != QDialog::Accepted) {
        *status = QStringLiteral("Import cancelled.");
        return {};
    }
    QStringList names;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->checkState(0) == Qt::Checked)
            names << tree->topLevelItem(i)->text(0);
    if (names.isEmpty()) {
        *status = QStringLiteral("Nothing selected to import.");
        return {};
    }
    *status = QStringLiteral("Importing %1 skill(s)…").arg(names.size());
    return {{"type", "import_skills_confirm"}, {"url", url}, {"commit", commit},
            {"names", QJsonArray::fromStringList(names)}};
}

QString updatesText(const QJsonObject &event)
{
    const QString current = event.value(QStringLiteral("current")).toString();
    const QString latest = event.value(QStringLiteral("latest")).toString();
    const QString url = event.value(QStringLiteral("url")).toString();
    if (latest.isEmpty())
        return QStringLiteral("Could not check %1.").arg(url);
    if (!event.value(QStringLiteral("update_available")).toBool(current != latest))
        return QStringLiteral("%1 is up to date (%2).").arg(url, current.left(10));
    return QStringLiteral("%1: pinned %2, latest %3. Import again to review and update.")
        .arg(url, current.left(10), latest.left(10));
}

QString reverifyPrompt(const QString &name)
{
    return QStringLiteral("/skill %1 serve one case of %1 and record it with board_case").arg(name);
}

QStringList excludedNames()
{
    return QSettings().value(QStringLiteral("skills/exclude")).toStringList();
}

void setExcluded(const QString &name, bool excluded)
{
    if (name.isEmpty())
        return;
    QStringList names = excludedNames();
    names.removeAll(name);
    if (excluded)
        names << name;
    names.removeDuplicates();
    names.sort();
    QSettings settings;
    if (names.isEmpty()) {
        settings.remove(QStringLiteral("skills/exclude"));
        settings.remove(QStringLiteral("skills/exclude_text"));
    } else {
        settings.setValue(QStringLiteral("skills/exclude"), names);
        settings.setValue(QStringLiteral("skills/exclude_text"), names.join(QStringLiteral(", ")));
    }
}

// ------------------------------------------------------------------------ the view

SkillRegistryView::SkillRegistryView(Scope scope, const QString &prefix, QWidget *parent)
    : QWidget(parent), m_scope(scope), m_prefix(prefix)
{
    setObjectName(name("SkillsPage"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 6, 0, 0);
    column->setSpacing(4);

    auto *tools = new QWidget(this);
    auto *toolRow = new QHBoxLayout(tools);
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(6);
    m_count = new QLabel(QStringLiteral("Skills"), tools);
    m_count->setObjectName(name("SkillCount"));
    m_filter = new QLineEdit(tools);
    m_filter->setObjectName(name("SkillFilter"));
    m_filter->setPlaceholderText(QStringLiteral("Filter skills"));
    m_filter->setClearButtonEnabled(true);
    m_refresh = new QToolButton(tools);
    m_refresh->setObjectName(name("SkillRefresh"));
    m_refresh->setText(QStringLiteral("Refresh"));
    m_refresh->setToolTip(QStringLiteral("Ask the worker for the registry again — versions, cases and staleness are recomputed"));
    toolRow->addWidget(m_count);
    toolRow->addWidget(m_filter, 1);
    // Globals › Skills is where global skills are managed (the owner's decision 2 and 3), so the
    // import and update actions SkillsDialog had sit in its toolbar. The Board's tab lists the
    // project's own skills, which are files in the repository, not imports.
    if (m_scope == Scope::Global) {
        m_import = new QToolButton(tools);
        m_import->setObjectName(name("SkillImport"));
        m_import->setText(QStringLiteral("Import from repository…"));
        m_import->setToolTip(QStringLiteral("Clone a git repository, review its skills and enable the ones you check"));
        m_updates = new QToolButton(tools);
        m_updates->setObjectName(name("SkillUpdates"));
        m_updates->setText(QStringLiteral("Check updates"));
        m_updates->setToolTip(QStringLiteral("Compare the pinned commit of the selected imported skill with its repository"));
        m_updates->setEnabled(false);
        toolRow->addWidget(m_import);
        toolRow->addWidget(m_updates);
    }
    toolRow->addWidget(m_refresh);
    column->addWidget(tools);
    m_status = new QLabel(this);
    m_status->setObjectName(name("SkillStatus"));
    m_status->setWordWrap(true);
    m_status->hide();
    column->addWidget(m_status);

    auto *split = new QSplitter(Qt::Vertical, this);
    split->setObjectName(name("SkillSplitter"));
    split->setChildrenCollapsible(false);
    m_list = new QTreeWidget(split);
    m_list->setObjectName(name("SkillList"));
    m_list->setHeaderLabels({QStringLiteral("Skill"), QStringLiteral("Source · version"),
                             QStringLiteral("Cases"), QStringLiteral("Pass"),
                             QStringLiteral("Last verified"), QStringLiteral("Stale")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setColumnWidth(0, 240);
    m_list->setColumnWidth(1, 170);

    auto *detail = new QWidget(split);
    detail->setObjectName(name("SkillDetail"));
    auto *detailColumn = new QVBoxLayout(detail);
    detailColumn->setContentsMargins(0, 6, 0, 0);
    detailColumn->setSpacing(4);
    const auto label = [detail, this](const char *suffix) {
        auto *made = new QLabel(detail);
        made->setObjectName(name(suffix));
        made->setWordWrap(true);
        return made;
    };
    m_title = label("SkillTitle");
    m_title->setTextFormat(Qt::RichText);
    m_trigger = label("SkillTrigger");
    m_profile = label("SkillProfile");
    m_provenance = label("SkillProvenance");
    m_provenance->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stats = label("SkillStats");
    m_stats->setTextFormat(Qt::RichText);
    auto *casesLabel = new QLabel(QStringLiteral("Cases"), detail);
    casesLabel->setObjectName(name("SkillCasesLabel"));
    m_cases = new QTreeWidget(detail);
    m_cases->setObjectName(name("SkillCases"));
    m_cases->setHeaderLabels({QStringLiteral("When"), QStringLiteral("Served by"),
                              QStringLiteral("Signal"), QStringLiteral("Verdict"),
                              QStringLiteral("Cost"), QStringLiteral("Input")});
    m_cases->setRootIsDecorated(false);
    m_cases->setUniformRowHeights(true);
    m_cases->setMaximumHeight(150);
    detailColumn->addWidget(m_title);
    detailColumn->addWidget(m_trigger);
    detailColumn->addWidget(m_profile);
    detailColumn->addWidget(m_provenance);
    detailColumn->addWidget(m_stats);
    detailColumn->addWidget(casesLabel);
    detailColumn->addWidget(m_cases);

    // The Linked panel (#EA37 (c)): one chip per card the registry names.
    m_linked = new QWidget(detail);
    m_linked->setObjectName(name("SkillLinked"));
    auto *linkedColumn = new QVBoxLayout(m_linked);
    linkedColumn->setContentsMargins(0, 0, 0, 0);
    linkedColumn->setSpacing(2);
    auto *linkedHead = new QLabel(QStringLiteral("Linked"), m_linked);
    linkedHead->setObjectName(name("SkillLinkedHead"));
    linkedColumn->addWidget(linkedHead);
    m_linkedLayout = new QVBoxLayout;
    m_linkedLayout->setContentsMargins(0, 0, 0, 0);
    m_linkedLayout->setSpacing(2);
    linkedColumn->addLayout(m_linkedLayout);
    detailColumn->addWidget(m_linked);

    auto *actions = new QWidget(detail);
    actions->setObjectName(name("SkillActions"));
    auto *actionRow = new QHBoxLayout(actions);
    actionRow->setContentsMargins(0, 4, 0, 0);
    actionRow->setSpacing(6);
    const auto button = [actions, actionRow, this](const QString &text, const char *suffix,
                                                   const QString &tip) {
        auto *made = new QPushButton(text, actions);
        made->setObjectName(name(suffix));
        made->setToolTip(tip);
        actionRow->addWidget(made);
        return made;
    };
    m_load = button(QStringLiteral("Load"), "SkillLoad",
                    QStringLiteral("Draft the skill's name into the console's composer — a draft, never a send"));
    m_reverify = button(QStringLiteral("Re-verify"), "SkillReverify",
                        QStringLiteral("Draft \"serve one case of this skill and record it with board_case\" into the console, with the skill loaded — a draft, never a send"));
    m_exclude = button(QString(), "SkillExclude",
                       QStringLiteral("Add this skill to, or remove it from, the exclusion list new agent sessions apply"));
    m_refine = button(QStringLiteral("Refine"), "SkillRefine",
                      QStringLiteral("Start a session that improves this skill from its own history"));
    m_open = button(QStringLiteral("Open file"), "SkillOpen", QStringLiteral("Open the SKILL.md in an editor"));
    actionRow->addStretch(1);
    detailColumn->addWidget(actions);
    detailColumn->addStretch(1);

    split->addWidget(m_list);
    split->addWidget(detail);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    column->addWidget(split, 1);

    connect(m_filter, &QLineEdit::textChanged, this, [this] { refill(); });
    connect(m_refresh, &QToolButton::clicked, this, [this] { reload(); });
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] {
        QTreeWidgetItem *item = m_list->currentItem();
        showSkill(item == nullptr ? QString() : item->data(0, Qt::UserRole).toString());
    });
    connect(m_load, &QPushButton::clicked, this, [this] {
        const QString skill = selectedName();
        if (!skill.isEmpty() && draft)
            draft(skill + QLatin1Char(' '));
    });
    connect(m_reverify, &QPushButton::clicked, this, [this] {
        const QString skill = selectedName();
        if (!skill.isEmpty() && draft)
            draft(reverifyPrompt(skill));
    });
    connect(m_exclude, &QPushButton::clicked, this, [this] {
        const QString skill = selectedName();
        if (skill.isEmpty())
            return;
        const bool exclude = !excludedNames().contains(skill);
        setExcluded(skill, exclude);
        note(exclude ? QStringLiteral("%1 excluded — new sessions skip it").arg(skill)
                     : QStringLiteral("%1 included").arg(skill));
        refill();
    });
    connect(m_refine, &QPushButton::clicked, this, [this] {
        const QString skill = selectedName();
        if (skill.isEmpty() || !send)
            return;
        m_refineRequest = send({{"type", "refine_skills"}, {"names", QJsonArray{skill}}});
        note(QStringLiteral("Refining %1…").arg(skill));
    });
    connect(m_open, &QPushButton::clicked, this, [this] {
        const QString path = rowById(m_selected).value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    if (m_import != nullptr) {
        connect(m_import, &QToolButton::clicked, this, [this] {
            const QJsonObject request = askImport(this);
            if (request.isEmpty() || !send)
                return;
            note(QStringLiteral("Cloning %1…").arg(request.value(QStringLiteral("url")).toString()));
            m_importRequest = send(request);
        });
        connect(m_updates, &QToolButton::clicked, this, [this] { checkUpdates(); });
    }
    showSkill(QString());
    syncActions();
}

void SkillRegistryView::syncActions()
{
    m_load->setVisible(bool(draft));
    m_reverify->setVisible(bool(draft));
}

QJsonObject SkillRegistryView::rowById(const QString &id) const
{
    for (const QJsonValue &value : m_items) {
        const QJsonObject row = value.toObject();
        if (row.value(QStringLiteral("id")).toString() == id)
            return row;
    }
    return {};
}

QString SkillRegistryView::selectedName() const
{
    return rowById(m_selected).value(QStringLiteral("name")).toString();
}

int SkillRegistryView::shownCount() const { return m_list->topLevelItemCount(); }

void SkillRegistryView::note(const QString &text)
{
    if (toast) {
        toast(text);
        return;
    }
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

void SkillRegistryView::ensureLoaded()
{
    if (!m_requested)
        reload();
}

void SkillRegistryView::reload()
{
    m_requested = true;
    m_linksFor.clear();   // a reloaded page asks for its reverse links again
    if (send)
        m_request = send({{"type", "skills_registry"}});
    if (!m_arrived)
        m_count->setText(QStringLiteral("Loading…"));
}

bool SkillRegistryView::handleEvent(const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("event")).toString(event.value(QStringLiteral("type")).toString());
    const QString id = event.value(QStringLiteral("id")).toString();
    if (type == QStringLiteral("skills_registry")) {
        // Another surface's reply on the same worker carries another id and is not ours.
        if (!m_request.isEmpty() && !id.isEmpty() && id != m_request)
            return false;
        m_items = event.value(QStringLiteral("items")).toArray();
        m_arrived = true;
        refill();
        return true;
    }
    if (type == QStringLiteral("skills_refined")) {
        // Ours or a SkillsDialog's elsewhere: the version and profile reflect the refined copy.
        if (m_requested)
            reload();
        if (!id.isEmpty() && id == m_refineRequest) {
            m_refineRequest.clear();
            note(QStringLiteral("Skill refined"));
        }
        return true;
    }
    if (id.isEmpty())
        return false;
    // The cards that name this skill without a case row (#EE42): `server:` on a card
    // (`built_by`) and `skill:<name>` in a card's prose (`mentioned_in`), from the board's link
    // index. Only the newest question is believed, and only for the page still showing.
    if (type == QStringLiteral("board_links") && id == m_linksRequest) {
        m_linksRequest.clear();
        const QJsonArray items = event.value(QStringLiteral("items")).toArray();
        const QJsonObject item = items.isEmpty() ? QJsonObject() : items.first().toObject();
        const QString address = item.value(QStringLiteral("address")).toString();
        QStringList ids;
        for (const QJsonValue &value : item.value(QStringLiteral("reverse")).toArray()) {
            const QString from = value.toObject().value(QStringLiteral("from")).toString();
            if (from.startsWith(QLatin1Char('#')) && !ids.contains(from.mid(1)))
                ids << from.mid(1);
        }
        m_linkedFrom = ids;
        m_linksFor = address.mid(int(qstrlen("skill:")));
        const QJsonObject row = rowById(m_selected);
        if (!row.isEmpty() && row.value(QStringLiteral("name")).toString() == m_linksFor)
            setLinkedCards(linkedCardIds(row), QStringLiteral("No cards name this skill yet."));
        return true;
    }
    if (type == QStringLiteral("error") && id == m_linksRequest) {
        m_linksRequest.clear();   // a worker without `board_links`: the ledger's cards only
        return true;
    }
    if (type == QStringLiteral("skills_import_preview") && id == m_importRequest) {
        m_importRequest.clear();
        QString status;
        const QJsonObject confirm = reviewImport(this, event, &status);
        note(status);
        if (!confirm.isEmpty() && send)
            m_confirmRequest = send(confirm);
        return true;
    }
    if (type == QStringLiteral("skills_imported") && id == m_confirmRequest) {
        m_confirmRequest.clear();
        QString text = QStringLiteral("Imported %1 skill(s)%2.")
                           .arg(event.value(QStringLiteral("items")).toArray().size())
                           .arg(event.value(QStringLiteral("reloaded")).toBool()
                                    ? QStringLiteral(" · the agent picked them up")
                                    : QStringLiteral(" · used by new agent sessions"));
        for (const auto &error : event.value(QStringLiteral("errors")).toArray())
            text += QStringLiteral("\n%1: %2").arg(error.toObject().value(QStringLiteral("name")).toString(),
                                                   error.toObject().value(QStringLiteral("error")).toString().left(200));
        note(text);
        reload();
        return true;
    }
    if (type == QStringLiteral("skills_updates") && id == m_updatesRequest) {
        m_updatesRequest.clear();
        note(updatesText(event));
        return true;
    }
    if (type == QStringLiteral("error")
        && (id == m_request || id == m_importRequest || id == m_confirmRequest
            || id == m_updatesRequest || id == m_refineRequest)) {
        if (id == m_request && !m_arrived)
            m_count->setText(QStringLiteral("Skills"));
        note(QStringLiteral("Error: ") + event.value(QStringLiteral("text")).toString().left(400));
        return true;
    }
    return false;
}

void SkillRegistryView::checkUpdates()
{
    const QString url = importUrl(rowById(m_selected).value(QStringLiteral("path")).toString());
    if (url.isEmpty()) {
        note(QStringLiteral("Only imported skills can be checked for updates."));
        return;
    }
    note(QStringLiteral("Checking %1…").arg(url));
    if (send)
        m_updatesRequest = send({{"type", "skills_check_updates"}, {"url", url}});
}

void SkillRegistryView::refill()
{
    const QString keep = m_selected;
    QSignalBlocker block(m_list);   // non-const: refill re-selects and unblocks on purpose
    m_list->clear();
    const QString filter = m_filter->text().trimmed();
    const QBrush dim = palette().color(QPalette::Disabled, QPalette::Text);
    const QStringList excluded = excludedNames();
    int shown = 0, excludedCount = 0;
    for (const QJsonValue &value : m_items) {
        const QJsonObject row = value.toObject();
        // The owner's decision 2: the Board lists project skills (`project: true` — `.relay/skills`
        // and the workspace's .claude/.codex/.warp), Globals every other source. The worker sends
        // all of them; the split is made here, per surface.
        if (row.value(QStringLiteral("project")).toBool() != (m_scope == Scope::Project))
            continue;
        const QString skill = row.value(QStringLiteral("name")).toString();
        const bool isExcluded = excluded.contains(skill);
        if (isExcluded)
            ++excludedCount;
        const QString haystack = QStringList{skill, row.value(QStringLiteral("id")).toString(),
                                             row.value(QStringLiteral("description")).toString(),
                                             row.value(QStringLiteral("source")).toString()}
                                     .join(QLatin1Char(' '));
        if (!filter.isEmpty() && !haystack.contains(filter, Qt::CaseInsensitive))
            continue;
        const QJsonObject stats = row.value(QStringLiteral("stats")).toObject();
        auto *item = new QTreeWidgetItem(m_list);
        item->setText(0, skill + (isExcluded ? QStringLiteral("  (excluded)") : QString()));
        const QString version = row.value(QStringLiteral("version_short")).toString();
        item->setText(1, row.value(QStringLiteral("source")).toString()
                             + (version.isEmpty() ? QString() : QStringLiteral(" · ") + version));
        item->setText(2, QString::number(stats.value(QStringLiteral("cases")).toInt()));
        item->setText(3, passRateText(stats));
        const QString lastVerified = row.value(QStringLiteral("last_verified")).toString();
        item->setText(4, lastVerified.isEmpty() ? QStringLiteral("never") : lastVerified);
        // Step 4: stale carries its reason; a skill with no rows is "no cases yet", never stale
        // (the ledger's rule — nothing to have gone off).
        if (stats.value(QStringLiteral("stale")).toBool()) {
            item->setText(5, QStringLiteral("stale"));
            item->setToolTip(5, row.value(QStringLiteral("stale_reason")).toString());
        } else if (stats.value(QStringLiteral("cases")).toInt() == 0) {
            item->setText(5, QStringLiteral("no cases yet"));
            item->setForeground(5, dim);
        }
        item->setToolTip(0, row.value(QStringLiteral("description")).toString());
        item->setData(0, Qt::UserRole, row.value(QStringLiteral("id")).toString());
        if (isExcluded) {
            QFont font = item->font(0);
            font.setItalic(true);
            item->setFont(0, font);
            item->setForeground(0, dim);
        }
        ++shown;
    }
    if (m_arrived)
        m_count->setText(QStringLiteral("%1 skill%2 · %3 excluded")
                             .arg(shown)
                             .arg(shown == 1 ? QString() : QStringLiteral("s"))
                             .arg(excludedCount));
    // The selection survives a refresh when the row is still there.
    QTreeWidgetItem *restore = nullptr;
    for (int i = 0; !keep.isEmpty() && i < m_list->topLevelItemCount(); ++i) {
        if (m_list->topLevelItem(i)->data(0, Qt::UserRole).toString() == keep) {
            restore = m_list->topLevelItem(i);
            break;
        }
    }
    if (restore == nullptr && shown > 0 && m_selected.isEmpty())
        restore = m_list->topLevelItem(0);
    if (restore != nullptr) {
        block.unblock();
        m_list->setCurrentItem(restore);
        showSkill(restore->data(0, Qt::UserRole).toString());
    } else if (!m_selected.isEmpty()) {
        showSkill(QString());
    }
}

void SkillRegistryView::selectSkill(const QString &id)
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        if (m_list->topLevelItem(i)->data(0, Qt::UserRole).toString() == id) {
            m_list->setCurrentItem(m_list->topLevelItem(i));
            return;
        }
    }
}

void SkillRegistryView::showSkill(const QString &id)
{
    m_selected = id;
    const QJsonObject row = rowById(id);
    const bool any = !row.isEmpty();
    for (QPushButton *action : {m_load, m_reverify, m_exclude, m_refine, m_open})
        action->setEnabled(any);
    if (m_updates != nullptr)
        m_updates->setEnabled(any && !importUrl(row.value(QStringLiteral("path")).toString()).isEmpty());
    if (!any) {
        m_title->setText(QStringLiteral("Select a skill"));
        m_trigger->clear();
        m_profile->clear();
        m_provenance->clear();
        m_stats->clear();
        m_cases->clear();
        m_exclude->setText(QStringLiteral("Exclude"));
        setLinkedCards(QStringList(), QStringLiteral("No skill selected."));
        return;
    }
    const QString skill = row.value(QStringLiteral("name")).toString();
    const bool excluded = excludedNames().contains(skill);
    m_title->setText(QStringLiteral("<b>%1</b> <span style=\"opacity:0.6\">%2 · %3</span>%4")
                         .arg(skill.toHtmlEscaped(),
                              row.value(QStringLiteral("version_short")).toString(),
                              row.value(QStringLiteral("source")).toString(),
                              excluded ? QStringLiteral(" <span style=\"opacity:0.6\">excluded</span>")
                                       : QString()));
    m_trigger->setText(row.value(QStringLiteral("description")).toString());
    // The profile strip: the same words the card's Verify strip uses, one line, and nothing
    // invented — a key the SKILL.md does not declare simply does not appear.
    const QJsonObject profile = row.value(QStringLiteral("profile")).toObject();
    static const char *const kProfileKeys[] = {"artifact", "primary", "human", "effort",
                                               "stakes",   "rot",     "money", "confidential"};
    QStringList strip;
    for (const char *key : kProfileKeys) {
        const QJsonValue value = profile.value(QLatin1String(key));
        if (value.isUndefined() || value.isNull())
            continue;
        const QString text = value.isBool() ? (value.toBool() ? QStringLiteral("yes") : QStringLiteral("no"))
                                            : value.toVariant().toString();
        strip << QStringLiteral("%1 %2").arg(QLatin1String(key), text);
    }
    const QJsonArray warnings = row.value(QStringLiteral("profile_warnings")).toArray();
    if (!warnings.isEmpty())
        strip << QStringLiteral("%1 warning%2").arg(warnings.size()).arg(warnings.size() == 1 ? QString() : QStringLiteral("s"));
    m_profile->setText(strip.isEmpty() ? QStringLiteral("No profile yet.") : strip.join(QStringLiteral("  ·  ")));
    // Provenance: where it lives, what its bytes are, what changed — all from the registry row.
    QStringList provenance;
    provenance << tildeHome(row.value(QStringLiteral("path")).toString());
    const QString version = row.value(QStringLiteral("version")).toString();
    if (!version.isEmpty())   // already `sha256:<hex>`, the ledger's `server_version` string
        provenance << version;
    for (const QJsonValue &line : row.value(QStringLiteral("changelog")).toArray())
        provenance << line.toString();
    m_provenance->setText(provenance.join(QStringLiteral("\n")));
    // Stats and staleness, reason attached: the row, this page and the list's Stale column all
    // read the same `stats`, so they cannot disagree.
    const QJsonObject stats = row.value(QStringLiteral("stats")).toObject();
    QStringList statLine;
    statLine << QStringLiteral("cases %1").arg(stats.value(QStringLiteral("cases")).toInt());
    statLine << QStringLiteral("pass %1").arg(passRateText(stats));
    const QString lastServed = stats.value(QStringLiteral("last_served")).toString();
    statLine << QStringLiteral("last served %1").arg(lastServed.isEmpty() ? QStringLiteral("never") : lastServed);
    const QString lastVerified = row.value(QStringLiteral("last_verified")).toString();
    statLine << QStringLiteral("last verified %1").arg(lastVerified.isEmpty() ? QStringLiteral("never") : lastVerified);
    if (stats.value(QStringLiteral("stale")).toBool()) {
        const QString reason = row.value(QStringLiteral("stale_reason")).toString();
        statLine << QStringLiteral("<b>stale</b>%1")
                        .arg(reason.isEmpty() ? QString() : QStringLiteral(" — %1").arg(reason.toHtmlEscaped()));
    } else if (stats.value(QStringLiteral("cases")).toInt() == 0) {
        statLine << QStringLiteral("no cases yet");   // the ledger's rule: no rows, never stale
    }
    m_stats->setText(statLine.join(QStringLiteral("  ·  ")));
    // Cases: the last ten, newest first, as the ledger allowed this workspace to see them —
    // confidential rows are ids-only and off-workspace inputs are already stripped worker-side.
    m_cases->clear();
    for (const QJsonValue &value : row.value(QStringLiteral("cases")).toArray()) {
        const QJsonObject entry = value.toObject();
        auto *item = new QTreeWidgetItem(m_cases);
        item->setText(0, entry.value(QStringLiteral("when")).toString());
        item->setText(1, entry.value(QStringLiteral("served_by")).toString());
        item->setText(2, entry.value(QStringLiteral("signal")).toString());
        item->setText(3, entry.value(QStringLiteral("verdict")).toString());
        item->setText(4, entry.value(QStringLiteral("cost")).toVariant().toString());
        item->setText(5, entry.value(QStringLiteral("input")).toString());
    }
    // Linked cards (#EA37 (c)): the cards whose ledger rows name this server, then (#EE42) the
    // cards whose `server:` or prose names it, which the board's link index answers. Only the
    // Board asks: Globals' skills are not about one board.
    setLinkedCards(linkedCardIds(row), QStringLiteral("No cards name this skill yet."));
    if (m_scope == Scope::Project && send && m_linksFor != skill) {
        m_linkedFrom.clear();
        m_linksFor = skill;
        m_linksRequest = send({{"type", "board_links"}, {"address", QStringLiteral("skill:") + skill}});
    }
    m_exclude->setText(excluded ? QStringLiteral("Include") : QStringLiteral("Exclude"));
}

QStringList SkillRegistryView::linkedCardIds(const QJsonObject &row) const
{
    QStringList cards;
    for (const QJsonValue &value : row.value(QStringLiteral("cards")).toArray())
        if (!cards.contains(value.toString()))
            cards << value.toString();
    if (m_linksFor == row.value(QStringLiteral("name")).toString())
        for (const QString &id : m_linkedFrom)
            if (!cards.contains(id))
                cards << id;
    return cards;
}

void SkillRegistryView::setLinkedCards(const QStringList &ids, const QString &emptyText)
{
    // count(), not isEmpty(): a chip row made a moment ago is not shown yet, and a layout calls
    // an unshown widget "empty" — the old row stayed and the chips were drawn twice.
    while (m_linkedLayout->count() > 0) {
        QLayoutItem *item = m_linkedLayout->takeAt(0);
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    if (ids.isEmpty()) {
        auto *empty = new QLabel(emptyText, m_linked);
        empty->setWordWrap(true);
        m_linkedLayout->addWidget(empty);
        return;
    }
    auto *chips = new QWidget(m_linked);
    chips->setObjectName(name("LinkedChips"));
    auto *row = new QHBoxLayout(chips);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    int shown = 0;
    for (const QString &id : ids) {
        // A card the host does not know (a ledger row from elsewhere, or one closed and dropped
        // from the model) links anyway: the card page says "(missing)" for it.
        const QString title = cardTitle ? cardTitle(id) : QString();
        const QString text = title.isEmpty() ? QStringLiteral("#%1").arg(id)
                                             : QStringLiteral("#%1 %2").arg(id, title);
        auto *chip = new QPushButton(text, chips);
        chip->setObjectName(name("LinkedChip"));
        if (openCard) {
            chip->setCursor(Qt::PointingHandCursor);
            chip->setToolTip(QStringLiteral("Open #%1 on the card page").arg(id));
            const QString target = id;
            connect(chip, &QPushButton::clicked, this, [this, target] {
                if (openCard)
                    openCard(target);
            });
        } else {
            chip->setFlat(true);
            chip->setToolTip(QStringLiteral("#%1 is a card on the board that names this skill").arg(id));
        }
        row->addWidget(chip);
        if (++shown == 8)   // the panel links; it does not mirror the board
            break;
    }
    row->addStretch(1);
    m_linkedLayout->addWidget(chips);
}

}  // namespace relay::skills
