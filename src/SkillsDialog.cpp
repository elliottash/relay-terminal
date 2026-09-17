// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkillsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace relay {

namespace {
enum Column { Name, Source, Description };
const int PathRole = Qt::UserRole + 1;
const int SourceRole = Qt::UserRole + 2;

QString tilde(const QString &path) {
    const QString home = QDir::homePath();
    return path.startsWith(home + '/') ? QStringLiteral("~") + path.mid(home.size()) : path;
}

// Imported skills live in <imports>/<repo>@<commit>/<name>/SKILL.md next to .relay-import.json,
// which records the repository URL.
QString importUrl(const QString &skillPath) {
    QFile manifest(QFileInfo(skillPath).dir().absoluteFilePath(QStringLiteral("../.relay-import.json")));
    if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 65536) return {};
    return QJsonDocument::fromJson(manifest.readAll()).object().value(QStringLiteral("url")).toString();
}
}  // namespace

SkillsDialog::SkillsDialog(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("skillsDialog"));
    setWindowTitle(QStringLiteral("Skills"));
    resize(820, 480);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(QStringLiteral("Skills the agent can load by name. Unchecked skills are excluded from new agent sessions. "
                                            "Refining writes an editable copy; originals are never changed."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_list = new QTreeWidget;
    m_list->setObjectName(QStringLiteral("skillsList"));
    m_list->setHeaderLabels({QStringLiteral("Skill"), QStringLiteral("Source"), QStringLiteral("Description")});
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setUniformRowHeights(true);
    m_list->header()->setStretchLastSection(true);
    m_list->setColumnWidth(Name, 220);
    m_list->setColumnWidth(Source, 180);
    layout->addWidget(m_list, 1);
    m_status = new QLabel(QStringLiteral("Loading skills…"));
    m_status->setObjectName(QStringLiteral("skillsStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout;
    m_refine = new QPushButton(QStringLiteral("Refine selected"));
    m_refine->setToolTip(QStringLiteral("Ask the agent to write improved copies to ~/.config/relay/skills and open them for editing"));
    auto *import = new QPushButton(QStringLiteral("Import from repository…"));
    m_updates = new QPushButton(QStringLiteral("Check for updates"));
    m_updates->setToolTip(QStringLiteral("Compare the pinned commit of the selected imported skill with its repository"));
    auto *reload = new QPushButton(QStringLiteral("Reload"));
    auto *close = new QPushButton(QStringLiteral("Close"));
    buttons->addWidget(m_refine); buttons->addWidget(import); buttons->addWidget(m_updates);
    buttons->addStretch(1); buttons->addWidget(reload); buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(m_refine, &QPushButton::clicked, this, [this] {
        const QStringList names = selectedNames();
        if (names.isEmpty()) { m_status->setText(QStringLiteral("Select one or more skills to refine.")); return; }
        m_status->setText(QStringLiteral("Refining %1… the agent is rewriting a copy.").arg(names.join(QStringLiteral(", "))));
        if (send) send({{"type", "refine_skills"}, {"names", QJsonArray::fromStringList(names)}});
    });
    connect(import, &QPushButton::clicked, this, [this] { importFromRepository(); });
    connect(m_updates, &QPushButton::clicked, this, [this] { checkUpdates(); });
    connect(reload, &QPushButton::clicked, this, [this] { refresh(); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *, int column) {
        if (m_filling || column != Name) return;
        QStringList next = excludedNames();
        for (const QString &name : std::as_const(excluded)) if (!m_listed.contains(name) && !next.contains(name)) next << name;
        excluded = next;
        if (onExcludedChanged) onExcludedChanged(excluded);
        m_status->setText(QStringLiteral("%1 excluded · applies to new agent sessions").arg(excludedNames().size()));
    });
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto items = m_list->selectedItems();
        m_updates->setEnabled(items.size() == 1 && !importUrl(items.first()->data(Name, PathRole).toString()).isEmpty());
    });
    m_updates->setEnabled(false);
}

void SkillsDialog::refresh() {
    m_status->setText(QStringLiteral("Loading skills…"));
    if (send) send({{"type", "skills_list"}});
}

void SkillsDialog::handleEvent(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type == QStringLiteral("error")) m_status->setText(QStringLiteral("Error: ") + event.value(QStringLiteral("text")).toString().left(400));
    else if (type == QStringLiteral("skills")) showList(event);
    else if (type == QStringLiteral("skills_refined")) {
        const auto items = event.value(QStringLiteral("items")).toArray();
        QStringList paths;
        for (const auto &item : items) paths << tilde(item.toObject().value(QStringLiteral("path")).toString());
        QString text = QStringLiteral("Refined %1 skill(s): %2").arg(items.size()).arg(paths.join(QStringLiteral(", ")));
        for (const auto &error : event.value(QStringLiteral("errors")).toArray())
            text += QStringLiteral("\n%1: %2").arg(error.toObject().value(QStringLiteral("name")).toString(), error.toObject().value(QStringLiteral("error")).toString().left(200));
        m_note = text;
        refresh();
    } else if (type == QStringLiteral("skills_import_preview")) showImportPreview(event);
    else if (type == QStringLiteral("skills_imported")) {
        QString text = QStringLiteral("Imported %1 skill(s)%2.").arg(event.value(QStringLiteral("items")).toArray().size())
                           .arg(event.value(QStringLiteral("reloaded")).toBool() ? QStringLiteral(" · the agent picked them up") : QStringLiteral(" · used by new agent sessions"));
        for (const auto &error : event.value(QStringLiteral("errors")).toArray())
            text += QStringLiteral("\n%1: %2").arg(error.toObject().value(QStringLiteral("name")).toString(), error.toObject().value(QStringLiteral("error")).toString().left(200));
        m_note = text;
        refresh();
    } else if (type == QStringLiteral("skills_updates")) {
        const QString current = event.value(QStringLiteral("current")).toString(), latest = event.value(QStringLiteral("latest")).toString();
        const QString url = event.value(QStringLiteral("url")).toString();
        if (latest.isEmpty()) m_status->setText(QStringLiteral("Could not check %1.").arg(url));
        else if (!event.value(QStringLiteral("update_available")).toBool(current != latest)) m_status->setText(QStringLiteral("%1 is up to date (%2).").arg(url, current.left(10)));
        else m_status->setText(QStringLiteral("%1: pinned %2, latest %3. Import again to review and update.").arg(url, current.left(10), latest.left(10)));
    }
}

void SkillsDialog::showList(const QJsonObject &event) {
    m_filling = true;
    m_list->clear();
    m_listed.clear();
    const auto items = event.value(QStringLiteral("items")).toArray();
    for (const auto &value : items) {
        const QJsonObject skill = value.toObject();
        auto *row = new QTreeWidgetItem(m_list);
        const QString name = skill.value(QStringLiteral("name")).toString();
        QString source = skill.value(QStringLiteral("source")).toString();
        row->setText(Name, name);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        m_listed << name;
        row->setCheckState(Name, skill.value(QStringLiteral("excluded")).toBool() || excluded.contains(name) ? Qt::Unchecked : Qt::Checked);
        const QString from = skill.value(QStringLiteral("refined_from")).toString();
        const QString shadowedBy = skill.value(QStringLiteral("shadowed_by")).toString();
        row->setText(Source, source + (from.isEmpty() ? QString() : QStringLiteral(" · refined")) + (shadowedBy.isEmpty() ? QString() : QStringLiteral(" · overridden")));
        if (!shadowedBy.isEmpty()) {
            row->setForeground(Name, row->foreground(Name).color().darker(150));
            row->setToolTip(Source, QStringLiteral("Not used: %1 has the same name").arg(tilde(shadowedBy)));
        }
        row->setData(Source, SourceRole, source);
        row->setText(Description, skill.value(QStringLiteral("description")).toString());
        const QString path = skill.value(QStringLiteral("path")).toString();
        row->setData(Name, PathRole, path);
        row->setToolTip(Name, tilde(path) + (from.isEmpty() ? QString() : QStringLiteral("\nrefined from ") + tilde(from)));
        row->setToolTip(Description, skill.value(QStringLiteral("description")).toString());
    }
    m_filling = false;
    QString text = items.isEmpty() ? QStringLiteral("No skills found. Import some from a repository, or add folders with SKILL.md to ~/.config/relay/skills.")
                                   : QStringLiteral("%1 skill(s) · %2 excluded").arg(items.size()).arg(excludedNames().size());
    const auto skipped = event.value(QStringLiteral("skipped")).toArray();
    if (!skipped.isEmpty()) {
        text += QStringLiteral(" · %1 skipped (hover)").arg(skipped.size());
        QStringList lines;
        for (const auto &entry : skipped) lines << (entry.isString() ? entry.toString() : QString::fromUtf8(QJsonDocument(entry.toObject()).toJson(QJsonDocument::Compact)));
        m_status->setToolTip(lines.mid(0, 30).join('\n'));
    }
    // Keep the result of the refine or import that triggered this reload visible.
    m_status->setText(m_note.isEmpty() ? text : m_note + QStringLiteral("\n") + text);
    m_note.clear();
}

QStringList SkillsDialog::selectedNames() const {
    QStringList names;
    for (const auto *item : m_list->selectedItems()) names << item->text(Name);
    return names;
}

QStringList SkillsDialog::excludedNames() const {
    QStringList names;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->checkState(Name) == Qt::Unchecked) names << m_list->topLevelItem(i)->text(Name);
    return names;
}

void SkillsDialog::importFromRepository() {
    QDialog ask(this);
    ask.setWindowTitle(QStringLiteral("Import skills"));
    auto *form = new QFormLayout(&ask);
    auto *url = new QLineEdit; url->setObjectName(QStringLiteral("importUrl"));
    url->setPlaceholderText(QStringLiteral("https://github.com/org/skills"));
    auto *ref = new QLineEdit; ref->setPlaceholderText(QStringLiteral("default branch"));
    form->addRow(QStringLiteral("Repository URL"), url);
    form->addRow(QStringLiteral("Branch, tag or commit"), ref);
    form->addRow(new QLabel(QStringLiteral("Relay clones it into a temporary folder and shows the skills for review. Nothing is enabled until you confirm.")));
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Preview"));
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    if (ask.exec() != QDialog::Accepted || url->text().trimmed().isEmpty()) return;
    QJsonObject request{{"type", "import_skills_preview"}, {"url", url->text().trimmed()}};
    if (!ref->text().trimmed().isEmpty()) request.insert(QStringLiteral("ref"), ref->text().trimmed());
    m_status->setText(QStringLiteral("Cloning %1…").arg(url->text().trimmed()));
    if (send) send(request);
}

void SkillsDialog::showImportPreview(const QJsonObject &event) {
    const QString url = event.value(QStringLiteral("url")).toString();
    const QString commit = event.value(QStringLiteral("commit")).toString();
    const auto items = event.value(QStringLiteral("items")).toArray();
    const auto skippedItems = event.value(QStringLiteral("skipped")).toArray();
    if (!event.value(QStringLiteral("error")).toString().isEmpty() || items.isEmpty()) {
        m_status->setText(QStringLiteral("Import preview: %1").arg(event.value(QStringLiteral("error")).toString(QStringLiteral("no skills found in ") + url)));
        return;
    }
    QDialog review(this);
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
    connect(box, &QDialogButtonBox::accepted, &review, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &review, &QDialog::reject);
    if (review.exec() != QDialog::Accepted) { m_status->setText(QStringLiteral("Import cancelled.")); return; }
    QStringList names;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->checkState(0) == Qt::Checked) names << tree->topLevelItem(i)->text(0);
    if (names.isEmpty()) { m_status->setText(QStringLiteral("Nothing selected to import.")); return; }
    m_status->setText(QStringLiteral("Importing %1 skill(s)…").arg(names.size()));
    if (send) send({{"type", "import_skills_confirm"}, {"url", url}, {"commit", commit}, {"names", QJsonArray::fromStringList(names)}});
}

void SkillsDialog::checkUpdates() {
    const auto items = m_list->selectedItems();
    if (items.size() != 1) return;
    const QString url = importUrl(items.first()->data(Name, PathRole).toString());
    if (url.isEmpty()) { m_status->setText(QStringLiteral("Only imported skills can be checked for updates.")); return; }
    m_status->setText(QStringLiteral("Checking %1…").arg(url));
    if (send) send({{"type", "skills_check_updates"}, {"url", url}});
}

}  // namespace relay
