// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The skills registry's list and page (#9FX8), one widget for both surfaces that show it: the
// Board's Skills tab (project skills, decision 2) and Globals › Skills (every other source). Both
// read the same `skills_registry` rows (protocol §11) and draw the same page — profile strip,
// provenance, stats with the stale reason, the last cases and the linked cards — so the two can
// never disagree about a skill. The host supplies the worker (`send`) and, where it has one, a
// console to draft into (`draft`) and a card page to open (`openCard`); a surface without them
// simply shows no Load/Re-verify buttons and plain card ids.
//
// The import-from-repository and Check-updates flows (SkillsDialog's, protocol §11) live here
// too, as free functions SkillsDialog calls and as buttons the Globals scope puts in the toolbar.
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QVBoxLayout;

namespace relay::skills {

// The repository URL an imported skill came from (`<imports>/<repo>@<commit>/.relay-import.json`),
// or empty for a skill that was not imported.
QString importUrl(const QString &skillPath);
// Asks for a repository URL and ref; returns the `import_skills_preview` request, or {} if cancelled.
QJsonObject askImport(QWidget *parent);
// Shows a preview for review; returns the `import_skills_confirm` request for the checked skills,
// or {} with `*status` saying why nothing is imported.
QJsonObject reviewImport(QWidget *parent, const QJsonObject &preview, QString *status);
// The sentence a `skills_updates` event means.
QString updatesText(const QJsonObject &event);
// Step 4's Re-verify draft: the skill loaded (`/skill <name>`) and one case asked for. A draft,
// never a send — the person presses Enter.
QString reverifyPrompt(const QString &name);
// The exclusion list SkillsDialog's caller keeps in QSettings (`skills/exclude`), and the write
// that keeps both keys in step (the joined `skills/exclude_text` copy the settings page shows).
QStringList excludedNames();
void setExcluded(const QString &name, bool excluded);

class SkillRegistryView final : public QWidget {
public:
    // Which half of the registry this surface lists, by the row's `project` flag.
    enum class Scope { Project, Global };
    // `prefix` names the widgets (`<prefix>SkillsPage`, `<prefix>SkillList`, …) so the Board keeps
    // its `boardSkill*` names and Globals gets `globalsSkill*`.
    SkillRegistryView(Scope scope, const QString &prefix, QWidget *parent = nullptr);

    // Sends a request to the worker and returns the id it went under (the host assigns ids).
    std::function<QString(const QJsonObject &)> send;
    // Put text in a console's composer. Unset: no Load or Re-verify on the page.
    std::function<void(const QString &)> draft;
    // Open a card by id; unset: linked cards are shown as plain ids.
    std::function<void(const QString &)> openCard;
    // A card's title for its chip, or empty when the host does not know it.
    std::function<QString(const QString &)> cardTitle;
    // A transient message; unset, it goes to the page's status line.
    std::function<void(const QString &)> toast;

    // Draft hooks change which buttons show: call after assigning `draft`.
    void syncActions();
    // Ask for the registry the first time only (a surface nobody opens pays for nothing).
    void ensureLoaded();
    void reload();
    // Takes the events this view asked for or cares about; false for anything else.
    bool handleEvent(const QJsonObject &event);
    void selectSkill(const QString &id);
    QString selectedId() const { return m_selected; }
    QString selectedName() const;
    int shownCount() const;

private:
    void refill();
    void showSkill(const QString &id);
    void setLinkedCards(const QStringList &ids, const QString &emptyText);
    void note(const QString &text);
    void checkUpdates();
    QJsonObject rowById(const QString &id) const;
    QString name(const char *suffix) const { return m_prefix + QLatin1String(suffix); }

    Scope m_scope;
    QString m_prefix;
    QLineEdit *m_filter = nullptr;
    QLabel *m_count = nullptr, *m_status = nullptr;
    QTreeWidget *m_list = nullptr, *m_cases = nullptr;
    QToolButton *m_refresh = nullptr, *m_import = nullptr, *m_updates = nullptr;
    QLabel *m_title = nullptr, *m_trigger = nullptr, *m_profile = nullptr, *m_provenance = nullptr,
           *m_stats = nullptr;
    QWidget *m_linked = nullptr;
    QVBoxLayout *m_linkedLayout = nullptr;
    QPushButton *m_load = nullptr, *m_exclude = nullptr, *m_refine = nullptr, *m_open = nullptr,
                *m_reverify = nullptr;
    QJsonArray m_items;               // the registry rows, as they arrived
    QString m_selected;               // id of the row whose page is showing
    QString m_request, m_importRequest, m_confirmRequest, m_updatesRequest, m_refineRequest;
    bool m_requested = false;         // asked at least once
    bool m_arrived = false;           // an answer came
};

}  // namespace relay::skills
