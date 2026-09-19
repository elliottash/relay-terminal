// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AgentUi.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMap>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace relay::agentui {

namespace {
QString tilde(const QString &path) {
    const QString home = QDir::homePath();
    return path.startsWith(home + '/') ? QStringLiteral("~") + path.mid(home.size()) : path;
}

class FilterKeys final : public QObject {
public:
    FilterKeys(QTreeWidget *list, QObject *parent) : QObject(parent), m_list(list) {}
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() != QEvent::KeyPress) return false;
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up || key->key() == Qt::Key_PageDown || key->key() == Qt::Key_PageUp) {
            QCoreApplication::sendEvent(m_list, event);
            return true;
        }
        return false;
    }
private:
    QTreeWidget *m_list;
};
}  // namespace

PickerResult pick(QWidget *parent, const QString &title, const QString &hint, const QStringList &headers,
                  const QList<PickerRow> &rows, const QList<PickerAction> &actions) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(860, 460);
    auto *layout = new QVBoxLayout(&dialog);
    if (!hint.isEmpty()) {
        auto *label = new QLabel(hint); label->setWordWrap(true); label->setObjectName(QStringLiteral("pickerHint"));
        layout->addWidget(label);
    }
    auto *filter = new QLineEdit; filter->setPlaceholderText(QStringLiteral("Filter · ↑↓ select · Enter"));
    layout->addWidget(filter);
    auto *list = new QTreeWidget;
    list->setHeaderLabels(headers);
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setAlternatingRowColors(false);
    for (int i = 0; i < rows.size(); ++i) {
        auto *item = new QTreeWidgetItem(list, rows[i].columns);
        item->setData(0, Qt::UserRole, i);
        item->setToolTip(0, rows[i].detail);
    }
    // The widest text column (the prompt or title) stretches and elides; the others fit their contents.
    list->header()->setStretchLastSection(false);
    int stretch = 0, widest = -1;
    for (int c = 0; c < headers.size(); ++c) {
        int total = 0;
        for (const auto &row : rows) total += c < row.columns.size() ? int(row.columns.at(c).size()) : 0;
        if (total > widest) { widest = total; stretch = c; }
    }
    for (int c = 0; c < headers.size(); ++c)
        list->header()->setSectionResizeMode(c, c == stretch ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
    list->setTextElideMode(Qt::ElideRight);
    if (list->topLevelItemCount()) list->setCurrentItem(list->topLevelItem(0));
    layout->addWidget(list, 1);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    PickerResult result;
    QString primary;
    for (const auto &action : actions) {
        auto *button = new QPushButton(action.label);
        if (action.primary) { button->setDefault(true); primary = action.id; }
        QObject::connect(button, &QPushButton::clicked, &dialog, [&, id = action.id] {
            if (!list->currentItem()) return;
            result.row = list->currentItem()->data(0, Qt::UserRole).toInt();
            result.action = id;
            dialog.accept();
        });
        buttons->addWidget(button);
    }
    auto *cancel = new QPushButton(QStringLiteral("Cancel"));
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    if (rows.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("Nothing to show yet."));
        layout->insertWidget(layout->count() - 1, empty);
    }
    QObject::connect(filter, &QLineEdit::textChanged, &dialog, [list](const QString &text) {
        QTreeWidgetItem *first = nullptr;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = list->topLevelItem(i);
            bool match = text.isEmpty();
            for (int c = 0; !match && c < list->columnCount(); ++c) match = item->text(c).contains(text, Qt::CaseInsensitive);
            item->setHidden(!match);
            if (match && !first) first = item;
        }
        if (first) list->setCurrentItem(first);
    });
    QObject::connect(list, &QTreeWidget::itemActivated, &dialog, [&](QTreeWidgetItem *item) {
        if (!item || primary.isEmpty()) return;
        result.row = item->data(0, Qt::UserRole).toInt();
        result.action = primary;
        dialog.accept();
    });
    QObject::connect(filter, &QLineEdit::returnPressed, &dialog, [&] {
        if (!list->currentItem() || primary.isEmpty()) return;
        result.row = list->currentItem()->data(0, Qt::UserRole).toInt();
        result.action = primary;
        dialog.accept();
    });
    filter->installEventFilter(new FilterKeys(list, &dialog));
    filter->setFocus();
    if (dialog.exec() != QDialog::Accepted) return {};
    return result;
}

OnboardingResult chooseInstructions(QWidget *parent, const QList<InstructionFile> &found, bool projectAuto,
                                    const QString &relayMdPath) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Relay · Agent instructions"));
    dialog.resize(760, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(QStringLiteral(
        "Relay found instruction files from other coding tools. Choose which ones the agent should follow, "
        "or combine them into one global relay.md. You can change this later in Actions › Agent options › Instructions."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *list = new QTreeWidget;
    list->setHeaderLabels({QStringLiteral("File"), QStringLiteral("Scope"), QStringLiteral("Size")});
    list->setRootIsDecorated(true);
    QMap<QString, QTreeWidgetItem *> groups;
    int existing = 0;
    for (const auto &file : found) {
        if (!file.exists) continue;
        ++existing;
        QTreeWidgetItem *group = groups.value(file.tool);
        if (!group) {
            group = new QTreeWidgetItem(list, {file.tool});
            group->setFlags(Qt::ItemIsEnabled);
            group->setExpanded(true);
            groups.insert(file.tool, group);
        }
        auto *item = new QTreeWidgetItem(group, {tilde(file.path), file.scope, QLocale().formattedDataSize(file.bytes)});
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setCheckState(0, file.checked ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, file.path);
        item->setToolTip(0, file.path);
    }
    list->expandAll();
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(list, 1);
    if (!existing) {
        auto *none = new QLabel(QStringLiteral("No instruction files were found (CLAUDE.md, AGENTS.md, WARP.md, GEMINI.md, Cursor or Copilot rules)."));
        none->setWordWrap(true);
        layout->addWidget(none);
    }
    auto *project = new QCheckBox(QStringLiteral("Also include instruction files found in each project automatically (AGENTS.md, CLAUDE.md, WARP.md, …)"));
    project->setChecked(projectAuto);
    layout->addWidget(project);
    auto *synthesize = new QCheckBox(QStringLiteral("Create a global relay.md from the selected files (%1)").arg(tilde(relayMdPath)));
    synthesize->setToolTip(QStringLiteral("The agent combines the checked files into one document, which is then used instead of them."));
    layout->addWidget(synthesize);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *skip = new QPushButton(QStringLiteral("Not now"));
    auto *save = new QPushButton(QStringLiteral("Save")); save->setDefault(true);
    buttons->addWidget(skip); buttons->addWidget(save);
    layout->addLayout(buttons);
    QObject::connect(skip, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    OnboardingResult result;
    result.projectAuto = projectAuto;
    if (dialog.exec() != QDialog::Accepted) return result;
    result.accepted = true;
    result.projectAuto = project->isChecked();
    result.synthesize = synthesize->isChecked();
    for (auto *group : groups)
        for (int i = 0; i < group->childCount(); ++i)
            if (group->child(i)->checkState(0) == Qt::Checked) result.files << group->child(i)->data(0, Qt::UserRole).toString();
    return result;
}

}  // namespace relay::agentui
