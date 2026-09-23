// SPDX-License-Identifier: AGPL-3.0-or-later
#include "GlobalsPane.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

namespace relay::globals {
namespace {
// The section control's rows. Suggestions comes last so the older rows keep their places.
constexpr int kUserMemory = 0, kAliases = 1, kInstructions = 2, kAll = 3, kSuggestions = 4;
QString sourceWord(const QString &source) {
    if (source == QLatin1String("claude")) return QObject::tr("imported from Claude Code");
    if (source == QLatin1String("codex")) return QObject::tr("imported from Codex");
    if (source == QLatin1String("interview")) return QObject::tr("from an interview");
    return source.isEmpty() || source == QLatin1String("agent") ? QObject::tr("suggested by an agent")
                                                               : QObject::tr("suggested by %1").arg(source);
}
} // namespace
GlobalsPane::GlobalsPane(QWidget *parent) : QWidget(parent) {
    setObjectName("globalsPane");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    m_section = new QComboBox;
    m_section->setObjectName("globalsSection");
    m_section->addItems({tr("User memory"), tr("Aliases"), tr("Instructions"), tr("All records"), tr("Suggestions")});
    layout->addWidget(m_section);
    m_intro = new QLabel(tr("What Relay knows about you · saved facts and preferences across projects. "
        "Review or edit a memory below, or let the helper interview you. "
        "Applicable memories are sent to the model you choose. Retire stops future loading; it keeps the file and history."));
    m_intro->setWordWrap(true);
    layout->addWidget(m_intro);
    m_search = new QLineEdit;
    m_search->setObjectName("globalsSearch");
    m_search->setPlaceholderText(tr("Search memories, aliases or instructions…"));
    layout->addWidget(m_search);
    setFocusProxy(m_search);
    auto *actions = new QHBoxLayout;
    m_newMemory = new QPushButton(tr("New memory"));
    m_newAlias = new QPushButton(tr("New alias"));
    m_interview = new QPushButton(tr("Interview me"));
    m_interview->setObjectName("globalsInterview");
    m_interview->setToolTip(tr("Start a conversation about your work and preferences. You choose which answers to remember."));
    m_review = new QPushButton;
    m_review->setObjectName("globalsReview");
    m_review->setToolTip(tr("Facts agents or imports proposed. Nothing is remembered until you keep it."));
    auto *reload = new QPushButton(tr("Refresh"));
    m_newMemory->setObjectName("globalsNewMemory");
    m_newAlias->setObjectName("globalsNewAlias");
    reload->setObjectName("globalsRefresh");
    actions->addWidget(m_newMemory); actions->addWidget(m_newAlias); actions->addWidget(m_interview);
    actions->addWidget(m_review);
    actions->addStretch(); actions->addWidget(reload);
    layout->addLayout(actions);
    auto *split = new QSplitter(Qt::Vertical);
    m_list = new QListWidget;
    m_list->setObjectName("globalsList");
    m_list->setMinimumHeight(70);
    auto *lists = new QWidget;
    auto *listsLayout = new QVBoxLayout(lists);
    listsLayout->setContentsMargins(0, 0, 0, 0);
    listsLayout->addWidget(m_list, 1);
    // What was said no to stays said: read-only, folded away unless asked for.
    m_rejectedToggle = new QToolButton;
    m_rejectedToggle->setObjectName("globalsRejectedToggle");
    m_rejectedToggle->setCheckable(true);
    m_rejectedToggle->setAutoRaise(true);
    m_rejectedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_rejectedToggle->setArrowType(Qt::RightArrow);
    m_rejectedToggle->setToolTip(tr("Suggestions you rejected. A new suggestion that matches one is declined without asking."));
    listsLayout->addWidget(m_rejectedToggle);
    m_rejectedList = new QListWidget;
    m_rejectedList->setObjectName("globalsRejected");
    m_rejectedList->setSelectionMode(QAbstractItemView::NoSelection);
    m_rejectedList->setFocusPolicy(Qt::NoFocus);
    m_rejectedList->setVisible(false);
    listsLayout->addWidget(m_rejectedList, 1);
    split->addWidget(lists);
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
    m_keep = new QPushButton(tr("Keep"));
    m_edit = new QPushButton(tr("Edit"));
    m_reject = new QPushButton(tr("No"));
    m_keep->setObjectName("globalsKeep"); m_edit->setObjectName("globalsEdit"); m_reject->setObjectName("globalsReject");
    m_keep->setToolTip(tr("Save this fact to User memory, with any edit you made."));
    m_edit->setToolTip(tr("Reword the fact before keeping it."));
    m_reject->setToolTip(tr("Do not remember this. The rejection is kept, so the same fact is not suggested again."));
    editActions->addWidget(m_keep); editActions->addWidget(m_edit); editActions->addWidget(m_reject);
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
    connect(m_section, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        m_getRequest.clear();
        display({});
        rebuild(); updateButtons();
    });
    connect(m_interview, &QPushButton::clicked, this, [this] {
        if (protectDraft()) return;
        if (onInterview) onInterview();
        else m_notice->setText(tr("The helper is not available in this view."));
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { selectRecord(); });
    connect(m_review, &QPushButton::clicked, this, [this] { m_section->setCurrentIndex(kSuggestions); });
    connect(m_rejectedToggle, &QToolButton::toggled, this, [this](bool open) {
        m_rejectedToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        updateButtons();
    });
    connect(m_keep, &QPushButton::clicked, this, [this] { decide(true); });
    connect(m_reject, &QPushButton::clicked, this, [this] { decide(false); });
    connect(m_edit, &QPushButton::clicked, this, [this] {
        m_editor->setFocus();
        m_editor->selectAll();
    });
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
    rebuildRejected();
    updateSectionLabel();
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
    return tr("Globals / Board HQ / %6\nWorkspace: %1\nSearch: %2\nSelected: %3\nSource: %4\nUnsaved draft: %5")
        .arg(m_workspace, m_search->text(), m_record.value("title").toString(m_record.value("fact").toString(m_selected)),
             m_record.value("path").toString(), m_dirty ? "yes" : "no", m_section->currentText())
        + tr("\nSuggestions waiting: %1 · rejected: %2").arg(m_pending.size()).arg(m_rejected.size());
}
bool GlobalsPane::protectDraft() {
    if (!m_dirty && m_writeRequest.isEmpty()) return false;
    m_notice->setText(!m_writeRequest.isEmpty() ? tr("Saving…")
        : suggesting() ? tr("Keep your edit or cancel it before selecting another suggestion.")
                       : tr("Save or cancel your draft before selecting another record."));
    return true;
}
bool GlobalsPane::suggesting() const { return m_record.value("kind").toString() == QLatin1String("suggestion"); }
void GlobalsPane::showSuggestion(const QString &id) {
    if (protectDraft()) return;
    m_selected = id.isEmpty() ? QString() : QStringLiteral("suggestion:") + id;
    {
        const QSignalBlocker blocker(m_section);
        m_section->setCurrentIndex(kSuggestions);
    }
    m_getRequest.clear();
    m_record = {};
    rebuild();
    if (auto *item = m_list->currentItem()) display(item->data(Qt::UserRole).toJsonObject());
    else { display({}); m_selected = id.isEmpty() ? QString() : QStringLiteral("suggestion:") + id; }
    updateButtons();
    if (id.isEmpty()) m_list->setFocus(); else m_editor->setFocus();
}
void GlobalsPane::decide(bool keep) {
    if (!suggesting() || !m_writeRequest.isEmpty()) return;
    QJsonObject body{{"type", keep ? "globals_suggestion_accept" : "globals_suggestion_reject"},
                     {"sid", m_record.value("key").toString()}};
    if (keep && m_dirty) body.insert("text", m_editor->toPlainText().trimmed());
    m_writeRequest = request(body);
    m_notice->setText(keep ? tr("Keeping…") : tr("Rejecting…"));
    updateButtons();
}
void GlobalsPane::rebuildRejected() {
    m_rejectedList->clear();
    for (const auto &value : m_rejected) {
        const auto rejected = value.toObject();
        const QString date = rejected.value("date").toString();
        auto *item = new QListWidgetItem(rejected.value("fact").toString(rejected.value("title").toString())
            + "\n" + (date.isEmpty() ? tr("rejected") : tr("rejected %1").arg(date))
            + " · " + sourceWord(rejected.value("source").toString()), m_rejectedList);
        item->setToolTip(rejected.value("path").toString());
    }
    m_rejectedToggle->setText(tr("Rejected (%1)").arg(m_rejected.size()));
}
void GlobalsPane::updateSectionLabel() {
    m_section->setItemText(kSuggestions, m_pending.isEmpty() ? tr("Suggestions") : tr("Suggestions (%1)").arg(m_pending.size()));
    m_review->setText(m_pending.size() == 1 ? tr("Review 1 suggestion") : tr("Review %1 suggestions").arg(m_pending.size()));
}
void GlobalsPane::refresh() {
    m_suggestRequest = request({{"type", "globals_suggestions"}});
    m_listRequest = request({{"type", "globals_list"}});
    if (m_dirty) m_notice->setText(tr("Refreshing the list; your unsaved draft is preserved."));
}
void GlobalsPane::rebuild() {
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    const QString query = m_search->text().trimmed();
    if (m_section->currentIndex() == kSuggestions) {
        for (const auto &value : m_pending) {
            auto suggestion = value.toObject();
            suggestion.insert("kind", "suggestion");
            suggestion.insert("key", suggestion.value("id").toString());
            const QString fact = suggestion.value("fact").toString(suggestion.value("title").toString());
            const QString date = suggestion.value("date").toString();
            const QString meta = sourceWord(suggestion.value("source").toString()) + (date.isEmpty() ? QString() : " · " + date);
            if (!query.isEmpty() && !(fact + " " + meta + " " + suggestion.value("origin").toString()).contains(query, Qt::CaseInsensitive))
                continue;
            auto *item = new QListWidgetItem(fact + "\n" + meta, m_list);
            item->setData(Qt::UserRole, suggestion);
            item->setToolTip(suggestion.value("origin").toString(suggestion.value("path").toString()));
            if (identity(suggestion) == m_selected) m_list->setCurrentItem(item);
        }
        return;
    }
    for (const auto &value : m_records) {
        const auto record = value.toObject();
        const QString title = record.value("title").toString(record.value("key").toString());
        const QString kind = record.value("kind").toString();
        const QString status = record.value("status").toString();
        const int section = m_section->currentIndex();
        if (section == kUserMemory && (kind != "memory" || record.value("memory_scope").toString("user") != "user")) continue;
        if (section == kAliases && kind != "alias") continue;
        if (section == kInstructions && kind != "instruction") continue;
        const QString label = title + " · " + kind + (status.isEmpty() ? QString() : " · " + status)
            + (record.value("shadowed").toBool() ? tr(" · project override") : QString());
        const QString summary = record.value("summary").toString();
        const QString haystack = label + " " + summary + " " + record.value("path").toString() + " " + record.value("key").toString();
        if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
        auto *item = new QListWidgetItem(label + (summary.isEmpty() ? QString() : "\n" + summary), m_list);
        item->setData(Qt::UserRole, record);
        item->setToolTip(record.value("path").toString());
        if (identity(record) == m_selected) m_list->setCurrentItem(item);
    }
}
void GlobalsPane::selectRecord() {
    auto *item = m_list->currentItem();
    if (!item) return;
    const auto record = item->data(Qt::UserRole).toJsonObject();
    if (identity(record) == m_selected && identity(m_record) == m_selected) return;
    if (protectDraft()) { rebuild(); return; }
    m_selected = identity(record);
    if (record.value("kind").toString() == QLatin1String("suggestion")) {   // the list holds all of it
        m_getRequest.clear();
        display(record);
        m_notice->clear();
        return;
    }
    m_loading = true;
    m_notice->setText(tr("Loading…"));
    m_getRequest = request({{"type", "globals_get"}, {"kind", record.value("kind")}, {"key", record.value("key")}});
    updateButtons();
}
void GlobalsPane::display(const QJsonObject &record) {
    m_record = record;
    const bool suggestion = record.value("kind").toString() == QLatin1String("suggestion");
    m_original = suggestion ? record.value("fact").toString() : record.value("text").toString();
    m_dirty = false;
    m_loading = false;
    const QSignalBlocker blocker(m_editor);
    m_editor->setPlainText(m_original);
    m_selected = identity(record);
    if (suggestion) {
        const QString date = record.value("date").toString();
        QString source = sourceWord(record.value("source").toString()) + (date.isEmpty() ? QString() : " · " + date);
        if (!record.value("origin").toString().isEmpty()) source += "\n" + tr("From: %1").arg(record.value("origin").toString());
        source += "\n" + tr("Keep saves it to User memory as written below; edit it first if it is not quite right. "
                            "No remembers the rejection.");
        m_source->setText(source);
        updateButtons();
        return;
    }
    QString source = record.value("path").toString();
    if (source.isEmpty()) source = tr("New global %1").arg(record.value("kind").toString());
    source += "\n" + tr("Scope: %1").arg(record.value("scope").toString("global"));
    if (record.value("shadowed").toBool()) source += tr(" · overridden in this project");
    if (record.value("kind").toString() == "instruction") source += tr(" · edits this source file in place");
    if (record.value("kind").toString() == "memory") {
        source += "\n" + tr("Memory scope: %1").arg(record.value("memory_scope").toString("user"));
        source += record.value("pinned").toBool() ? tr(" · pinned across projects") : tr(" · uses workspace path matching");
        source += tr(" · subject to project overrides, supersession and the context size limit");
        if (record.value("status").toString() == "retired") source += tr("\nRetired: excluded from future memory context. File and history retained.");
    }
    m_source->setText(source);
    updateButtons();
}
void GlobalsPane::newRecord(const QString &kind) {
    if (protectDraft()) return;
    m_section->setCurrentIndex(kind == "memory" ? kUserMemory : kAliases);
    m_getRequest.clear();
    display({{"kind", kind}, {"scope", "global"}});
    const QString text = kind == "memory"
        ? "---\ntype: memory\nstatus: active\nname: new-memory\nscope: user\npinned: true\npaths: []\n---\n# New memory\n\nOne useful fact or preference about you, in your own words.\n"
        : "---\ntype: alias\nstatus: active\nname: new-alias\nkind: command\n---\n# New alias\n\n## Run\n```sh\npwd\n```\n";
    m_editor->setPlainText(text);
    m_notice->setText(tr("Edit the name, title and content, then save."));
    rebuild();
    m_editor->setFocus();
}
void GlobalsPane::updateButtons() {
    const bool ready = m_writeRequest.isEmpty() && !m_loading;
    const int section = m_section->currentIndex();
    const bool suggestions = section == kSuggestions;
    m_section->setEnabled(ready && !m_dirty);
    m_interview->setEnabled(ready && !m_dirty);
    m_newMemory->setVisible(section == kUserMemory || section == kAll);
    m_newAlias->setVisible(section == kAliases || section == kAll);
    m_interview->setVisible(section == kUserMemory);
    m_review->setVisible(section == kUserMemory && !m_pending.isEmpty());
    m_intro->setVisible(section == kUserMemory || suggestions);
    m_intro->setText(suggestions
        ? tr("Facts agents learned or imported from Claude Code and Codex. Nothing is remembered until you keep it. "
             "No is remembered too: the same fact is not suggested again.")
        : tr("What Relay knows about you · saved facts and preferences across projects. "
             "Review or edit a memory below, or let the helper interview you. "
             "Applicable memories are sent to the model you choose. Retire stops future loading; it keeps the file and history."));
    m_rejectedToggle->setVisible(suggestions);
    m_rejectedList->setVisible(suggestions && m_rejectedToggle->isChecked());
    const bool deciding = suggestions && suggesting();
    m_keep->setVisible(suggestions); m_edit->setVisible(suggestions); m_reject->setVisible(suggestions);
    m_keep->setEnabled(ready && deciding && !m_editor->toPlainText().trimmed().isEmpty());
    m_edit->setEnabled(ready && deciding);
    m_reject->setEnabled(ready && deciding);
    m_save->setVisible(!suggestions); m_retire->setVisible(!suggestions);
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
        else if (!m_dirty && !m_loading && m_writeRequest.isEmpty() && m_section->currentIndex() != kSuggestions)
            m_notice->setText(m_list->count() == 0 ?
                (m_section->currentIndex() == kUserMemory ? tr("No matching user memories. Add a memory or choose Interview me to begin.")
                                               : tr("No matching records.")) : QString());
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
    } else if (type == "globals_suggestions") {
        // The whole list each time, so a reply to anyone's request on this worker is current.
        if (id == m_suggestRequest) m_suggestRequest.clear();
        m_pending = event.value("pending").toArray();
        m_rejected = event.value("rejected").toArray();
        updateSectionLabel();
        rebuildRejected();
        if (m_section->currentIndex() == kSuggestions) {
            rebuild();
            if (suggesting() && !m_dirty && m_writeRequest.isEmpty()) {
                // Decided elsewhere (the transcript's Keep or No): the editor lets it go.
                bool present = false;
                for (const auto &value : m_pending) present = present || value.toObject().value("id").toString() == m_record.value("key").toString();
                if (!present) display({});
            } else if (!suggesting() && m_list->currentItem() && !m_dirty) {
                display(m_list->currentItem()->data(Qt::UserRole).toJsonObject());   // showSuggestion() before the list came
            }
            if (!m_dirty && m_writeRequest.isEmpty() && m_notice->text().isEmpty() && m_pending.isEmpty())
                m_notice->setText(tr("No suggestions waiting."));
        }
        updateButtons();
    } else if ((type == "globals_suggestion_accepted" || type == "globals_suggestion_rejected")
               && id == m_writeRequest && !id.isEmpty()) {
        m_writeRequest.clear();
        const bool kept = type == "globals_suggestion_accepted";
        const QString sid = m_record.value("key").toString();
        for (int i = m_pending.size() - 1; i >= 0; --i)
            if (m_pending.at(i).toObject().value("id").toString() == sid) m_pending.removeAt(i);
        display({});
        updateSectionLabel();
        rebuild();
        m_notice->setText(kept ? tr("Kept · saved to User memory.") : tr("Rejected · it won't be suggested again."));
        refresh();
    } else if (type == "globals_record" && id == m_getRequest && !id.isEmpty()) {
        m_getRequest.clear(); display(event.value("record").toObject()); m_notice->clear();
    } else if (type == "globals_saved" && id == m_writeRequest && !id.isEmpty()) {
        m_writeRequest.clear(); display(event.value("record").toObject());
        m_notice->setText(tr("Saved.")); refresh();
    } else if (type == "globals_error" && !id.isEmpty()
               && (id == m_getRequest || id == m_writeRequest || id == m_listRequest || id == m_suggestRequest)) {
        if (id == m_suggestRequest) { m_suggestRequest.clear(); if (m_section->currentIndex() != kSuggestions) return; }
        if (id == m_getRequest) { m_getRequest.clear(); m_loading = false; m_selected = identity(m_record); rebuild(); }
        if (id == m_writeRequest) m_writeRequest.clear();
        if (id == m_listRequest) m_listRequest.clear();
        m_notice->setText(event.value("message").toString(tr("Unable to update Globals.")));
        updateButtons();
    }
}
} // namespace relay::globals
