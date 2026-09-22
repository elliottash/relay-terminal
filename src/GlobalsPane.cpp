// SPDX-License-Identifier: AGPL-3.0-or-later
#include "GlobalsPane.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QUuid>
#include <QVBoxLayout>

namespace relay::globals {
GlobalsPane::GlobalsPane(QWidget *parent) : QWidget(parent) {
    setObjectName("globalsPane");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *intro = new QLabel(tr("Board HQ · memories, aliases and instructions across projects"));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_search = new QLineEdit;
    m_search->setObjectName("globalsSearch");
    m_search->setPlaceholderText(tr("Search global records…"));
    layout->addWidget(m_search);
    setFocusProxy(m_search);
    auto *actions = new QHBoxLayout;
    m_newMemory = new QPushButton(tr("New memory"));
    m_newAlias = new QPushButton(tr("New alias"));
    auto *reload = new QPushButton(tr("Refresh"));
    m_newMemory->setObjectName("globalsNewMemory");
    m_newAlias->setObjectName("globalsNewAlias");
    reload->setObjectName("globalsRefresh");
    actions->addWidget(m_newMemory); actions->addWidget(m_newAlias); actions->addStretch(); actions->addWidget(reload);
    layout->addLayout(actions);
    auto *split = new QSplitter(Qt::Vertical);
    m_list = new QListWidget;
    m_list->setObjectName("globalsList");
    m_list->setMinimumHeight(70);
    split->addWidget(m_list);
    auto *detail = new QWidget;
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    m_source = new QLabel(tr("Select a record to inspect or edit its source."));
    m_source->setObjectName("globalsSource");
    m_source->setTextFormat(Qt::PlainText);
    m_source->setWordWrap(true);
    m_source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_source);
    m_editor = new QPlainTextEdit;
    m_editor->setObjectName("globalsEditor");
    m_editor->setPlaceholderText(tr("Record source (Markdown)"));
    detailLayout->addWidget(m_editor, 1);
    auto *editActions = new QHBoxLayout;
    m_save = new QPushButton(tr("Save"));
    m_cancel = new QPushButton(tr("Cancel"));
    m_retire = new QPushButton(tr("Retire"));
    m_save->setObjectName("globalsSave"); m_cancel->setObjectName("globalsCancel"); m_retire->setObjectName("globalsRetire");
    m_retire->setToolTip(tr("Mark this record retired. Its file and history are kept."));
    editActions->addWidget(m_save); editActions->addWidget(m_cancel); editActions->addStretch(); editActions->addWidget(m_retire);
    detailLayout->addLayout(editActions);
    split->addWidget(detail);
    split->setStretchFactor(0, 1); split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);
    m_notice = new QLabel;
    m_notice->setObjectName("globalsNotice");
    m_notice->setTextFormat(Qt::PlainText);
    m_notice->setWordWrap(true);
    layout->addWidget(m_notice);
    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { selectRecord(); });
    connect(reload, &QPushButton::clicked, this, [this] { refresh(); });
    connect(m_newMemory, &QPushButton::clicked, this, [this] { newRecord("memory"); });
    connect(m_newAlias, &QPushButton::clicked, this, [this] { newRecord("alias"); });
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        m_dirty = m_editor->toPlainText() != m_original; updateButtons();
    });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        display(m_record); m_notice->clear(); rebuild();
    });
    connect(m_save, &QPushButton::clicked, this, [this] {
        QJsonObject body{{"type", "globals_save"}, {"kind", m_record.value("kind")},
                         {"key", m_record.value("key").toString()}, {"text", m_editor->toPlainText()},
                         {"base_hash", m_record.value("hash").toString()}};
        m_writeRequest = request(body); updateButtons();
    });
    connect(m_retire, &QPushButton::clicked, this, [this] {
        m_writeRequest = request({{"type", "globals_retire"}, {"kind", m_record.value("kind")},
            {"key", m_record.value("key")}, {"base_hash", m_record.value("hash")}});
        updateButtons();
    });
    updateButtons();
}
QString GlobalsPane::request(QJsonObject body) {
    const QString id = "globals-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    body.insert("id", id);
    body.insert("workspace", m_workspace);
    if (onRequest) onRequest(body);
    return id;
}
QString GlobalsPane::identity(const QJsonObject &record) const {
    return record.value("kind").toString() + ":" + record.value("key").toString();
}
void GlobalsPane::setWorkspace(const QString &workspace) {
    if (m_workspace == workspace) return;
    m_workspace = workspace;
    refresh();
}
void GlobalsPane::focusSearch() { m_search->setFocus(); }
QString GlobalsPane::agentScreen() const {
    return tr("Globals / Board HQ\nWorkspace: %1\nSearch: %2\nSelected: %3\nSource: %4\nUnsaved draft: %5")
        .arg(m_workspace, m_search->text(), m_record.value("title").toString(m_selected),
             m_record.value("path").toString(), m_dirty ? "yes" : "no");
}
bool GlobalsPane::protectDraft() {
    if (!m_dirty && m_writeRequest.isEmpty()) return false;
    m_notice->setText(m_writeRequest.isEmpty() ? tr("Save or cancel your draft before selecting another record.") : tr("Saving…"));
    return true;
}
void GlobalsPane::refresh() {
    m_listRequest = request({{"type", "globals_list"}});
    if (m_dirty) m_notice->setText(tr("Refreshing the list; your unsaved draft is preserved."));
}
void GlobalsPane::rebuild() {
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    const QString query = m_search->text().trimmed();
    for (const auto &value : m_records) {
        const auto record = value.toObject();
        const QString title = record.value("title").toString(record.value("key").toString());
        const QString kind = record.value("kind").toString();
        const QString status = record.value("status").toString();
        const QString label = title + " · " + kind + (status.isEmpty() ? QString() : " · " + status)
            + (record.value("shadowed").toBool() ? tr(" · project override") : QString());
        const QString haystack = label + " " + record.value("path").toString() + " " + record.value("key").toString();
        if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
        auto *item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, record);
        item->setToolTip(record.value("path").toString());
        if (identity(record) == m_selected) m_list->setCurrentItem(item);
    }
}
void GlobalsPane::selectRecord() {
    auto *item = m_list->currentItem();
    if (!item) return;
    const auto record = item->data(Qt::UserRole).toJsonObject();
    if (identity(record) == m_selected) return;
    if (protectDraft()) { rebuild(); return; }
    m_selected = identity(record);
    m_loading = true;
    m_notice->setText(tr("Loading…"));
    m_getRequest = request({{"type", "globals_get"}, {"kind", record.value("kind")}, {"key", record.value("key")}});
    updateButtons();
}
void GlobalsPane::display(const QJsonObject &record) {
    m_record = record;
    m_original = record.value("text").toString();
    m_dirty = false;
    m_loading = false;
    const QSignalBlocker blocker(m_editor);
    m_editor->setPlainText(m_original);
    m_selected = identity(record);
    QString source = record.value("path").toString();
    if (source.isEmpty()) source = tr("New global %1").arg(record.value("kind").toString());
    source += "\n" + tr("Scope: %1").arg(record.value("scope").toString("global"));
    if (record.value("shadowed").toBool()) source += tr(" · overridden in this project");
    if (record.value("kind").toString() == "instruction") source += tr(" · edits this source file in place");
    m_source->setText(source);
    updateButtons();
}
void GlobalsPane::newRecord(const QString &kind) {
    if (protectDraft()) return;
    m_getRequest.clear();
    display({{"kind", kind}, {"scope", "global"}});
    const QString text = kind == "memory"
        ? "---\ntype: memory\nstatus: active\nname: new-memory\nscope: user\npinned: true\npaths: []\n---\n# New memory\n\nRemember this fact.\n"
        : "---\ntype: alias\nstatus: active\nname: new-alias\nkind: command\n---\n# New alias\n\n## Run\n```sh\npwd\n```\n";
    m_editor->setPlainText(text);
    m_notice->setText(tr("Edit the name, title and content, then save."));
    rebuild();
    m_editor->setFocus();
}
void GlobalsPane::updateButtons() {
    const bool ready = m_writeRequest.isEmpty() && !m_loading;
    m_editor->setReadOnly(!ready || m_record.isEmpty());
    m_save->setEnabled(ready && m_dirty);
    m_cancel->setEnabled(ready && m_dirty);
    m_newMemory->setEnabled(ready); m_newAlias->setEnabled(ready);
    m_retire->setEnabled(ready && !m_dirty && !m_record.value("key").toString().isEmpty()
        && m_record.value("kind").toString() != "instruction" && m_record.value("status").toString() != "retired");
}
void GlobalsPane::handleEvent(const QJsonObject &event) {
    const QString type = event.value("event").toString(event.value("type").toString());
    const QString id = event.value("id").toString();
    if (type == "globals_state" && id == m_listRequest && !id.isEmpty()) {
        m_listRequest.clear();
        m_records = event.value("records").toArray();
        rebuild();
        QStringList problems;
        for (const auto &problem : event.value("problems").toArray()) {
            if (problem.isString()) problems << problem.toString();
            else {
                const auto detail = problem.toObject();
                const QString path = detail.value("path").toString();
                problems << (path.isEmpty() ? QString() : path + ": ") + detail.value("message").toString();
            }
        }
        if (!problems.isEmpty()) m_notice->setText(problems.join("\n"));
        else if (!m_dirty && !m_loading && m_writeRequest.isEmpty())
            m_notice->setText(m_records.isEmpty() ? tr("No global records yet. Create a memory or alias to begin.") : QString());
        // Refresh the selected source only when there is no draft to overwrite. A fresh hash
        // lets Cancel + Refresh recover naturally after an external-write conflict.
        if (!m_dirty && !m_loading && m_writeRequest.isEmpty() && !m_record.value("key").toString().isEmpty()) {
            for (const auto &value : m_records) {
                if (identity(value.toObject()) != m_selected) continue;
                m_loading = true;
                m_getRequest = request({{"type", "globals_get"}, {"kind", m_record.value("kind")}, {"key", m_record.value("key")}});
                updateButtons();
                break;
            }
        }
    } else if (type == "globals_record" && id == m_getRequest && !id.isEmpty()) {
        m_getRequest.clear(); display(event.value("record").toObject()); m_notice->clear();
    } else if (type == "globals_saved" && id == m_writeRequest && !id.isEmpty()) {
        m_writeRequest.clear(); display(event.value("record").toObject());
        m_notice->setText(tr("Saved.")); refresh();
    } else if (type == "globals_error" && !id.isEmpty()
               && (id == m_getRequest || id == m_writeRequest || id == m_listRequest)) {
        if (id == m_getRequest) { m_getRequest.clear(); m_loading = false; m_selected = identity(m_record); rebuild(); }
        if (id == m_writeRequest) m_writeRequest.clear();
        if (id == m_listRequest) m_listRequest.clear();
        m_notice->setText(event.value("message").toString(tr("Unable to update Globals.")));
        updateButtons();
    }
}
} // namespace relay::globals
