// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QWidget>
#include <functional>

class QLabel;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QToolButton;

namespace relay::skills { class SkillRegistryView; }

namespace relay::globals {
// What a finished `app_user_memory` call with action `suggest` asks of the transcript (#MEMS):
// a pending suggestion gets a "Remember: … Keep · Edit · No" line, a declined or duplicate one a
// quiet note. Free and pure so the pane and the Globals test read the same result the same way.
struct SuggestionNote {
    enum Kind { None, Pending, Declined, Duplicate } kind = None;
    QString id, fact, detail;
};
inline SuggestionNote suggestionFromToolResult(const QJsonObject &event) {
    SuggestionNote note;
    const QString tool = event.value(QStringLiteral("tool")).toString();
    if (tool != QStringLiteral("app_user_memory") && !tool.endsWith(QStringLiteral("__app_user_memory")))
        return note;
    QJsonObject result = event.value(QStringLiteral("result")).toObject();
    // A guest's call arrives as its text output; the same object is inside it.
    if (!result.contains(QStringLiteral("status")) && result.value(QStringLiteral("output")).isString())
        result = QJsonDocument::fromJson(result.value(QStringLiteral("output")).toString().toUtf8()).object();
    const QString status = result.value(QStringLiteral("status")).toString();
    note.id = result.value(QStringLiteral("id")).toString();
    note.fact = result.value(QStringLiteral("fact")).toString().simplified();
    if (status == QStringLiteral("pending") && !note.id.isEmpty() && !note.fact.isEmpty()) note.kind = SuggestionNote::Pending;
    else if (status == QStringLiteral("declined")) note.kind = SuggestionNote::Declined;
    else if (status == QStringLiteral("duplicate")) note.kind = SuggestionNote::Duplicate;
    else return note;
    // `matched` is the card this suggestion repeats: a rejection declines it, a memory or a
    // waiting suggestion makes it a duplicate.
    const QJsonObject matched = result.value(QStringLiteral("matched")).toObject();
    const QString date = matched.value(QStringLiteral("date")).toString();
    if (note.kind == SuggestionNote::Declined)
        note.detail = date.isEmpty() ? QStringLiteral("you rejected it before")
                                     : QStringLiteral("you rejected it on %1").arg(date);
    else if (note.kind == SuggestionNote::Duplicate)
        note.detail = matched.value(QStringLiteral("kind")).toString() == QStringLiteral("pending")
            ? QStringLiteral("already waiting for your review")
            : QStringLiteral("already in user memory");
    return note;
}
// The transcript line for each kind, without its links.
inline QString suggestionLine(const SuggestionNote &note) {
    if (note.kind == SuggestionNote::Pending) return QStringLiteral("Remember: ") + note.fact;
    const QString what = note.fact.isEmpty() ? QString() : QStringLiteral(": ") + note.fact;
    return note.kind == SuggestionNote::None ? QString() : QStringLiteral("Not suggested%1 · %2").arg(what, note.detail);
}
// The bell entry for protocol 34's `memory_import {claude, codex}`: false when nothing is new.
inline bool memoryImportNotice(const QJsonObject &event, QString *title, QString *body) {
    const int claude = event.value(QStringLiteral("claude")).toInt();
    const int codex = event.value(QStringLiteral("codex")).toInt();
    const int total = claude + codex;
    if (total <= 0) return false;
    const QString from = claude && codex ? QStringLiteral("Claude Code and Codex")
                       : claude ? QStringLiteral("Claude Code") : QStringLiteral("Codex");
    *title = total == 1 ? QStringLiteral("1 memory from %1 to review").arg(from)
                        : QStringLiteral("%1 memories from %2 to review").arg(total).arg(from);
    *body = QStringLiteral("Nothing is remembered until you keep it · Globals › Suggestions");
    return true;
}
inline QString suggestionOutcome(bool kept, const QString &fact) {
    return kept ? QStringLiteral("Kept: %1 · Globals › User memory").arg(fact)
                : QStringLiteral("Rejected — won't be suggested again: %1").arg(fact);
}

// The shared manager hosts this page and routes its requests through its existing worker.
class GlobalsPane final : public QWidget {
public:
    explicit GlobalsPane(QWidget *parent = nullptr);
    std::function<void(const QJsonObject &)> onRequest;
    std::function<void()> onInterview;
    // Globals › Skills' Load and Re-verify put their text in a console's composer (#9FX8 step 4):
    // the host hands in where. Without one the skill page shows neither button.
    void setDraftTarget(std::function<void(const QString &)> draft);
    void handleEvent(const QJsonObject &event);
    void refresh();
    // A transcript's Keep or No (#MEMS): every Globals pane on screen, in every window, stops
    // offering that suggestion. Found with dynamic_cast because this class has no Q_OBJECT (#C8SV).
    static void refreshVisible();
    // Globals › Suggestions with this suggestion selected in the editor, from a transcript's Edit;
    // an empty id opens the list with nothing selected.
    void showSuggestion(const QString &id);
    int pendingSuggestions() const { return m_pending.size(); }
    void setWorkspace(const QString &workspace);
    void focusSearch();
    QString agentScreen() const;
private:
    QString request(QJsonObject body);
    QString identity(const QJsonObject &record) const;
    void rebuild();
    void selectRecord();
    void display(const QJsonObject &record);
    void newRecord(const QString &kind);
    void updateButtons();
    bool protectDraft();
    bool suggesting() const;
    void decide(bool keep);
    void rebuildRejected();
    void updateSectionLabel();
    // The section control's rows carry a stable id (`kUserMemory…kSkills`) as item data, so a
    // row can sit where it reads best without renumbering the ones before it.
    int section() const;
    void setSection(int section);
    QLineEdit *m_search;
    QComboBox *m_section;
    QLabel *m_intro;
    QPushButton *m_interview;
    QListWidget *m_list;
    QLabel *m_source;
    QLabel *m_notice;
    QPlainTextEdit *m_editor;
    QPushButton *m_save;
    QPushButton *m_cancel;
    QPushButton *m_retire;
    QPushButton *m_newMemory;
    QPushButton *m_newAlias;
    QPushButton *m_review;
    QPushButton *m_keep;
    QPushButton *m_edit;
    QPushButton *m_reject;
    QToolButton *m_rejectedToggle;
    QListWidget *m_rejectedList;
    QWidget *m_detail;
    QSplitter *m_split;
    QPushButton *m_reload;
    relay::skills::SkillRegistryView *m_skills;   // Globals › Skills (#9FX8): the global half
    int m_projectMemories = 0;   // project-scoped memory rows not listed here (Board › Memories)
    QJsonArray m_records, m_pending, m_rejected;
    QJsonObject m_record;
    QString m_workspace, m_original, m_selected, m_listRequest, m_getRequest, m_writeRequest, m_suggestRequest;
    bool m_dirty = false;
    bool m_loading = false;
};
} // namespace relay::globals
