// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The **jobs tab** of the models pane (Alt+4, card #MDL1, design 5.9).
//
// Owner, 2026-09-21: "for the per-job models, i think that should be reviewed and improved and
// made a 4th tab. take a careful look at it to see how to improve it for that."
//
// What it replaces is `relay::RolesDialog` — a modal behind Options › Models › "per-job models
// (advanced)" that drew fifteen rows in the worker's protocol order, in Title Case, each one a
// tier box plus a provider box plus a model box plus a level box, with the model the job actually
// runs on squeezed into grey subtitle text under the row's name. The review that led to this file
// is written out in docs/MODEL-PICKING-DESIGN.md 5.9; its five findings in one line each:
//
//   1. it is a modal nobody finds, and it is the only surface that answers "where does a summary
//      actually go";
//   2. fifteen rows in protocol order, Title Case, against this very card's rule 1;
//   3. what a job **runs on** is a grey subtitle, blank until the worker has answered, and never
//      says the level;
//   4. the override is a tier box that re-says what the priorities lists already own, and picking
//      a model takes three boxes;
//   5. nothing says what may not be overridden — a background job silently skips a guest harness.
//
// So this tab is one row per job a person can reason about, **grouped by the tier it follows**, in
// lower-case words, with four columns:
//
//     job              what it does                        runs on              override
//     ─ main ────────────────────────────────────────────  kimi-k3 · high
//       agent turns    the conversation in this pane       kimi-k3 · high       this pane's model
//       subagents      agents the main agent starts        kimi-k3 · high       follows main
//       helper agent   options, actions, sessions…         kimi-k3 · high       glm-5.3 · high  ×
//     ─ high ────────────────────────────────────────────  gpt-6-astra · xhigh
//       plan mode      …
//
// **"runs on" is the point.** It is the worker's own answer (`model_roles.roles[<role>]`, protocol
// 13), by model name and level, refreshed every time the served pane's worker reports. Nothing in
// Relay showed a person where a summary went before this column.
//
// **An override is one pick: a model.** Not a tier — which tier a job follows is a property of the
// job, and moving the *tier* is what the priorities lists are for — so the tier box is gone. Enter
// on a row drops the same filter list the model box uses, over the available models by name; a
// model with levels then drops its level list. Delete clears the row back to "follows its tier".
// The storage is `roles/<role>/{preset,model,effort}` (`relay::rolestore`). A legacy
// `roles/<role>/tier` is read and shown as "follows <tier>", and the × clears it too. The one
// exception is the pre-#HR5E planning override: it is retired once because it silently defeats
// plan mode's restored own-model default; a model chosen after that migration remains explicit.
//
// **A background job may not be overridden onto a guest harness.** `roles.py BACKGROUND_ROLES` —
// terminal use, summaries, suggestions, chores, request audit, loop check — are side calls into a
// conversation running somewhere else, and a harness is a whole agent with its own transcript, so
// the worker skips a guest entry for them. The list this tab offers skips them too, and the row's
// tooltip says why, rather than offering a model the job would never use.
//
// It knows nothing about `Pane`, `RelayWindow` or the worker: `Data` is plain JSON plus a catalog
// plus two callbacks, which is what makes tests/jobstab_test.cpp able to drive the whole surface.
#include "FilterPopup.h"
#include "ModelCatalog.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

class QEvent;
class QKeyEvent;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace relay {

// ----- where a per-job override is stored -------------------------------------------------------
// These were `RolesDialog`'s statics and they outlive it: `Pane::rolesObject` builds the protocol
// `roles` object out of the same keys, so the reader and the writer stay one pair.
//
//   roles/<role>/preset    a provider of this job's own; its presence is what "overridden" means
//   roles/<role>/model     a model of that provider; empty = that provider's default model
//   roles/<role>/effort    the level, in the model's own word; absent = the model's own default
//   roles/<role>/tier      written by the retired dialog only: the tier the job follows instead of
//                          its built-in one. Never written now; read, shown and cleared.
//   tiers/<tier>/<field>   the one-entry-per-tier keys the five ordered lists replaced. Read by
//                          `Pane::tiersObject` on an install that has stored no list yet.
namespace rolestore {
// Clear the planning override left by releases whose default planner followed High, once. Called
// lazily by `roleSetting`, before Pane serializes any roles for a terminal or console worker.
// Returns true only when at least one obsolete value was removed (exposed for the settings test).
bool migrateLegacyPlanningOverride();
QString roleSetting(const QString &role, const QString &field);
QString tierSetting(const QString &tier, const QString &field);

// "<preset>|<model>" for an overridden job, empty when it follows its tier. A stored preset with
// no model answers "<preset>|" — the provider's own default model, which is what that has always
// meant — and `models::Catalog::find` returns nothing for it, so a caller prints the preset.
QString overrideKey(const QString &role);
QString overrideEffort(const QString &role);
QString overrideTier(const QString &role);            // the legacy key; empty in the ordinary case

// Whether this job is one of `roles.py BACKGROUND_ROLES`: a side call into a conversation running
// somewhere else, which a guest harness cannot take. Derived from the same rule the worker uses —
// the flash and lite tiers, less `flash` itself, which is a pane's own mode.
bool background(const QString &role);
bool isGuestKey(const QString &key);

// Write one override, or clear it with an empty `key`. Refused, with `why` set, when a background
// job is pointed at a guest harness: the worker would skip it and run the job somewhere else, and
// a setting that is ignored is worse than one that was never accepted. Clearing removes all four
// keys above, so a job goes back to its built-in tier whatever it had been put on.
bool setOverride(const QString &role, const QString &key, const QString &effort, QString *why = nullptr);
}  // namespace rolestore

class JobsTab final : public QWidget {
public:
    // One row. `tier` is the group it is drawn under, which is the tier it follows by default —
    // `fixed` for the two that follow none (images, command routing). The words are this table's,
    // not the worker's: `roles.py ACTIONS` is Title Case and in protocol order, and rule 1 of this
    // card says everything a person reads is lower-case.
    struct Job {
        QString role;        // the protocol role id (roles.py ROLES)
        QString name;        // lower-case, what a person calls the job
        QString what;        // one line: what it does
        QString tier;        // main | high | flash | lite | fixed — the group it is drawn under
        bool settable = true;   // false only for `main`, which is the pane's own model by definition
    };

    // Everything the tab draws, and the two things it can do to the world.
    struct Data {
        models::Catalog catalog;
        QJsonObject roles;      // `model_roles.roles`: what each job resolves to right now
        QJsonObject tiers;      // `model_roles.tiers`: what each tier resolves to, for the group rows
        qint64 now = 0;         // unix seconds, for "which provider is spent"
        // An override was written: the served pane persists it and re-sends `roles`/`tiers` to its
        // worker, exactly as a tier-list edit does. The worker's next `model_roles` repaints
        // "runs on", so the column follows a change without this tab predicting it.
        std::function<void()> rolesChanged;
        std::function<void()> focusBack;    // Escape, when no popup is open
    };

    // The rows, in the owner's order: main, high, flash, lite, local, then the two fixed ones.
    static const QList<Job> &jobs();
    static QStringList groupOrder();
    static const Job *jobFor(const QString &role);
    static QString groupLabel(const QString &tier);

    explicit JobsTab(QWidget *parent = nullptr);

    void setData(const Data &data);
    const Data &data() const { return m_data; }

    QTreeWidget *list() const { return m_list; }
    FilterPopup *popup() const { return m_popup; }

    // ----- what the cells say, for a test and for the row's own tooltip -------------------------
    // The model and level a job resolves to right now, by name: "glm-5.3-flash · low". Empty when
    // the worker has not reported yet, and then the cell reads "—".
    QString runsOn(const QString &role) const;
    QString overrideText(const QString &role) const;
    // What a tier itself resolves to, for its group row. This is where **lite** is visible: it is
    // no longer a list on the priorities tab (owner, 2026-09-21: "remove the lite section"), so
    // the only place a person sees what chores run on is here.
    QString tierRunsOn(const QString &tier) const;

    // The rows Enter would drop: "follows <tier>" first, then one per model by name. A background
    // job's list holds no guest harness (see `rolestore::background`).
    QList<FilterRow> overrideRows(const QString &role) const;
    // The levels that model offers, for the second popup; empty where the level is not ours to set.
    QList<FilterRow> levelRows(const QString &role, const QString &key) const;

    // ----- the keyboard ------------------------------------------------------------------------
    QString currentRole() const;
    bool selectRole(const QString &role);
    bool openOverride();          // Enter on a row: the model list. False when the row cannot be set.
    bool clearOverride();         // Delete on a row
    void focusList();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    void applyOverride(const QString &role, const QString &key, const QString &effort);
    void openLevels(const QString &role, const QString &key);
    QString nameFor(const QString &preset, const QString &model) const;
    QString resolvedText(const QJsonObject &entry) const;
    // The entry a group row stands for, honouring the guest rule: the provider this job would
    // really run on if it were pointed at that name.
    models::Entry entryFor(const models::Group &group, bool background) const;
    QWidget *anchorForCurrentRow();

    Data m_data;
    QLabel *m_blurb = nullptr;
    QTreeWidget *m_list = nullptr;
    QLabel *m_footer = nullptr;
    FilterPopup *m_popup = nullptr;
    // A one-cell-sized invisible child of the list's viewport, moved over the row being picked
    // for: `FilterPopup::openFor` drops under its anchor, and anchoring on the whole list put the
    // model list at the top-left corner of the tree, off the pane and away from the row the eye
    // was on. It paints nothing and takes no mouse events.
    QWidget *m_anchor = nullptr;
    // Which row a popup was opened for, so the answer lands on the right job even if the tree is
    // rebuilt by a worker report while the list is open.
    QString m_picking;
};

}  // namespace relay
