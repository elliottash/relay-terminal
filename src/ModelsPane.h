// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The **models pane** (Ctrl+Shift+M, card #MDL1 t:a11, design 5.8).
//
// Owner, 2026-09-21: "lets build the models pane. and we can make it where, when you open relay
// for the first time, you have a pane at the left and models at the right. and just remove ctrl
// alt m, not worth the extra confusion … typing it again closes the pane (or esc as you
// mentioned)." So the modal dialog is gone and this is a `ToolPane` beside the pane it serves, the
// way Options and Sessions are hosted, with four tabs — the owner's four steps of availability
// (design 5.7), and then the step they were all in aid of (design 5.9):
//
//   providers    step 1: the provider rows and their keys, Options › Models' own section drawn by
//                Options' own machinery (`relay::SettingsPane` over one section, embedded), so a
//                key added here is a key added there. Profiles live here too.
//   available    step 2: the flat `all` tab — one row per model, the `available` tick column,
//                favorites, recents, the sort menu, OpenRouter's long tail behind typing, and
//                "+ add a model by id…".
//   priorities   steps 3 and 4: the five class lists — high · main · flash · lite · local — each
//                numbered and reorderable, with the level list, the "in box" cutoff column, the
//                class switch, "fill from defaults", the profile combo and undo.
//   jobs         what each **job** relay does runs on right now, grouped by the tier it follows,
//                and a per-job override. This is the retired "per-job models (advanced)" modal,
//                reviewed and rebuilt (`relay::JobsTab`, design 5.9).
//
// **Re-hosted, not rewritten.** available and priorities are one `relay::ModelPicker` — the modal's
// own guts, now a plain widget — put on the flat tab or on a class tab by this pane's tab bar. The
// providers tab is one `relay::SettingsPane`. Nothing about either was reimplemented here.
//
// **It serves a pane and switches nothing itself.** `Target` says which pane that is: its title
// for the header line, its catalog, its model, its level, its mode, and the `use` callback that
// switches it (`Pane::selectEntry` — the one door every pick takes). Enter or "use" on a row calls
// it; a click only highlights. Escape hands the focus back and leaves the pane open. The window
// opens one per window, re-targets it when Ctrl+Shift+M is pressed from another pane, and closes
// it when the key is pressed while it has the focus.
//
// The keys: ←/→ walk the class tabs on priorities, Alt+1…Alt+4 walk these four, and everything
// else is the hosted widget's own map (the picker's ↑↓, →, enter, alt+↑↓, delete, ctrl+enter,
// ctrl+z; the jobs tab's ↑↓, enter, delete).
//
// It knows nothing about `Pane`, `RelayWindow` or the worker — every one of those arrives as a
// `std::function` — so tests/modelspane_test.cpp drives the whole surface without a window.
#include "JobsTab.h"
#include "ModelCatalog.h"
#include "ModelPicker.h"
#include "PaneView.h"
#include "SettingsPane.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

class QEvent;
class QKeyEvent;
class QLabel;
class QStackedWidget;
class QTabBar;

namespace relay {

class ModelsPane final : public QWidget, public PaneView {
public:
    // The pane this one serves. Everything that would need a `Pane` is a callback, and every
    // callback is optional: a target with none is a readable pane that changes nothing.
    struct Target {
        QString title;             // what the header says after "for: " — the pane's title or cwd
        QString token;             // the served pane's session token; how a re-target is told from a refresh
        models::Catalog catalog;
        QString currentKey;        // the served pane's model now
        QString currentEffort;     // its level now
        QString tier = QStringLiteral("main");   // its mode, which is the class tab priorities opens on
        qint64 now = 0;            // unix seconds, for the limits line
        // Enter, "use" or a double click: the model and the level, for the served pane. This is
        // `Pane::selectEntry`; the pane never switches anything itself.
        std::function<void(const QString &key, const QString &effort)> use;
        // "fill from defaults" — only a pane's worker knows what it computed (design 5.5).
        std::function<bool(bool withOpenrouter)> fillFromDefaults;
        // A list edit landed: the window redraws Options and re-sends every worker its tiers.
        std::function<void()> listsChanged;
        // Escape: the focus goes back to the served pane and this pane stays open (design 5.8).
        std::function<void()> focusBack;
        // ----- the jobs tab (card #MDL1, design 5.9) --------------------------------------------
        // What the served pane's worker last reported: `model_roles.roles` and `.tiers`. The jobs
        // tab's "runs on" column is these and nothing else — it never predicts what a change will
        // resolve to, it shows what came back.
        QJsonObject roleSummary;
        QJsonObject tierSummary;
        // A per-job override was written: the served pane re-sends `roles`/`tiers` to its worker,
        // the same live update a tier-list edit makes (`Pane::rolesChanged`).
        std::function<void()> rolesChanged;
    };

    static QString providersTab() { return QStringLiteral("providers"); }
    static QString availableTab() { return QStringLiteral("available"); }
    static QString prioritiesTab() { return QStringLiteral("priorities"); }
    // Step 5, and the one the other three were always missing: what each **job** runs on
    // (card #MDL1, design 5.9; owner, 2026-09-21: "for the per-job models, i think that should be
    // reviewed and improved and made a 4th tab").
    static QString jobsTab() { return QStringLiteral("jobs"); }
    static QStringList tabIds() { return {providersTab(), availableTab(), prioritiesTab(), jobsTab()}; }

    // `sections` is what the providers tab draws — `RelayWindow::modelsSection()`, the same
    // section Options › Models draws, so it is one renderer with two hosts. With no callback the
    // providers tab is empty, which is what a test that is not about providers wants.
    explicit ModelsPane(std::function<QList<SettingsSection>()> sections, QWidget *parent = nullptr);

    // Point it at a pane. A target with a token this pane is already serving keeps the picker and
    // its undo stack and only re-reads; any other token rebuilds it around the new pane.
    void setTarget(const Target &target);
    const Target &target() const { return m_target; }
    QString servedToken() const { return m_target.token; }
    QString servedTitle() const { return m_target.title; }

    void showTab(const QString &id);
    QString currentTab() const;
    void setFilter(const QString &text);
    void focusFilter();
    // The class tab priorities is on. Reading it is how a test says "it opened on the served
    // pane's mode"; writing it is what the window does when it re-targets.
    QString tier() const;

    ModelPicker *picker() const { return m_picker; }
    JobsTab *jobs() const { return m_jobs; }
    SettingsPane *providers() const { return m_providers; }
    QTabBar *tabBar() const { return m_tabs; }
    QLabel *header() const { return m_header; }

    // ----- PaneView: what the ToolPane asks of any view it hosts --------------------------------
    QString paneTitle() const override { return QStringLiteral("Models"); }
    void focusView() override { focusFilter(); }
    void setHeaderRightInset(int pixels) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void buildPicker();
    void updateHeader();
    void stepTab(int delta);
    bool handleShortcut(QKeyEvent *event);

    std::function<QList<SettingsSection>()> m_sections;
    Target m_target;
    QLabel *m_header = nullptr;
    QTabBar *m_tabs = nullptr;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_providersPage = nullptr;
    SettingsPane *m_providers = nullptr;
    QWidget *m_pickerPage = nullptr;
    ModelPicker *m_picker = nullptr;
    JobsTab *m_jobs = nullptr;
    // The class tab priorities goes back to. It starts as the served pane's mode and then follows
    // whatever the person last looked at, so available → priorities does not throw the tab away.
    QString m_classTab = QStringLiteral("main");
    QString m_pendingFilter;
    // Whether the picker on screen was built with a "fill from defaults" action. The two buttons
    // are made in its constructor, so a target that gains the action — a restored pane, whose
    // worker had not answered `presets` when it was first pointed at a pane — needs a rebuild and
    // not a re-read (card #MDL1 t:a11).
    bool m_pickerHasFill = false;
};

}  // namespace relay
