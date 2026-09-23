// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The **models pane** (Ctrl+Shift+M, card #MDL1 t:a11, design 5.8).
//
// Owner, 2026-09-21: "lets build the models pane. and we can make it where, when you open relay
// for the first time, you have a pane at the left and models at the right. and just remove ctrl
// alt m, not worth the extra confusion … typing it again closes the pane (or esc as you
// mentioned)." So the modal dialog is gone and this is a `ToolPane` beside the pane it serves, the
// way Options and Sessions are hosted, with five tabs — the owner's availability steps
// (design 5.7), and then the step they were all in aid of (design 5.9):
//
//   providers    step 1: the provider rows and their keys, Options › Models' own section drawn by
//                Options' own machinery (`relay::SettingsPane` over one section, embedded), so a
//                key added here is a key added there. Profiles sit first, then guest agents,
//                subscription keys and pay-as-you-go keys. Defaults live in Options.
//   available    step 2: the flat `all` tab — one row per model, the `available` tick column,
//                favorites, then a section per provider alphabetically, the sort menu, OpenRouter's
//                long tail behind typing, and "+ add a model by id…". No "recent" section.
//   priorities   steps 3 and 4: **one scrolling page of sections**, one per class — high, main,
//                flash, and local where this machine serves one — each a header line (the class,
//                its "show this class in the box" switch and a one-line note) over that class's
//                numbered rows, with the "in box" cutoff column, the profile
//                combo and undo. No class tabs and no lite section (owner,
//                2026-09-21: "in a pane, i dont want separate tabs for the modes. they should just
//                be in divided sections. remove the lite section"); lite's storage stays and the
//                jobs tab is where a chore's model is set.
//   effort       reasoning levels for models ranked in the class lists.
//   jobs         what each **job** relay does runs on right now, grouped by the tier it follows,
//                and a per-job override. This is the retired "per-job models (advanced)" modal,
//                reviewed and rebuilt (`relay::JobsTab`, design 5.9).
//
// **Re-hosted, not rewritten.** available, priorities and effort are one `relay::ModelPicker` — the modal's
// own guts, now a plain widget — put on the flat tab or on a class tab by this pane's tab bar. The
// providers tab is one `relay::SettingsPane`. Nothing about either was reimplemented here.
//
// **It edits settings and never picks a pane's model** (owner, 2026-09-23, card #BXMS: "the
// models pane should not select models for specific panes. that should be done with the box
// picker"). `Target` is the pane it was opened from — its catalog, its model (highlighted), its
// mode (the class tab it opens on) and where Escape hands the focus back — and there is no callback
// that switches it: Enter and a double click do nothing but highlight. Escape leaves the pane open. The window
// opens one per window, re-targets it when Ctrl+Shift+M is pressed from another pane, and closes
// it when the key is pressed while it has the focus.
//
// The keys: Alt+1…Alt+5 and ←/→ walk these five tabs — the priorities page has no class tabs left
// to want the arrows — and everything else is the hosted widget's own map (the picker's ↑↓ across
// its sections, →, enter, alt+↑↓ inside a section, delete, ctrl+enter into the highlighted section,
// ctrl+z; the jobs tab's ↑↓, enter, delete).
//
// It knows nothing about `Pane`, `RelayWindow` or the worker — every one of those arrives as a
// `std::function` — so tests/modelspane_test.cpp drives the whole surface without a window.
//
// **A helper agent at its foot, across all five tabs** (owner, 2026-09-22: "there needs to be a
// helper agent on the model page"). It is the Options and Sessions helper, name for name: the
// collapsed "Helper Agent (Alt+Q)" row at the bottom right, a console the window builds through
// `onCreateConsole` on the first expand, the fold back to one row, and the same height rules
// (src/SettingsPane.cpp). The context is `ModelsContext` in ModelsPane.cpp — what this pane is
// showing — and the providers tab's embedded `SettingsPane` is never given a factory, so it has
// no row of its own: one helper per pane.
#include "AgentContext.h"
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
class QResizeEvent;
class QShowEvent;
class QStackedWidget;
class QTabBar;
class QToolButton;
class QVBoxLayout;

namespace relay {

// What this pane's helper agent is about: the tab, the filter, the pane it serves. Defined in
// ModelsPane.cpp, as `OptionsContext` is in SettingsPane.cpp.
class ModelsContext;

class ModelsPane final : public QWidget, public PaneView {
public:
    // The pane this one serves. Everything that would need a `Pane` is a callback, and every
    // callback is optional: a target with none is a readable pane that changes nothing.
    struct Target {
        QString title;             // the pane it was opened from, for the helper; never a pick target
        QString token;             // the served pane's session token; how a re-target is told from a refresh
        models::Catalog catalog;
        QString currentKey;        // the served pane's model now
        QString currentEffort;     // its level now
        QString tier = QStringLiteral("main");   // its mode, which is the class tab priorities opens on
        qint64 now = 0;            // unix seconds, for the limits line
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
    static QString effortTab() { return QStringLiteral("effort"); }
    // Step 5, and the one the other three were always missing: what each **job** runs on
    // (card #MDL1, design 5.9; owner, 2026-09-21: "for the per-job models, i think that should be
    // reviewed and improved and made a 4th tab").
    static QString jobsTab() { return QStringLiteral("jobs"); }
    static QStringList tabIds() { return {providersTab(), availableTab(), prioritiesTab(), effortTab(), jobsTab()}; }

    // `sections` is what the providers tab draws — `RelayWindow::modelsSection()`, the same
    // section Options › Models draws, so it is one renderer with two hosts. With no callback the
    // providers tab is empty, which is what a test that is not about providers wants.
    explicit ModelsPane(std::function<QList<SettingsSection>()> sections, QWidget *parent = nullptr);
    ~ModelsPane() override;

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
    // The class the priorities page's highlight is in. Reading it is how a test says "it opened on
    // the served pane's mode"; it was the class tab in front until the page became sections.
    QString tier() const;
    // The filter line's text on the tab in front: the picker's on available and priorities, the
    // embedded Options search on providers, nothing on jobs (it has no filter line).
    QString filterText() const;

    ModelPicker *picker() const { return m_picker; }
    JobsTab *jobs() const { return m_jobs; }
    SettingsPane *providers() const { return m_providers; }
    QTabBar *tabBar() const { return m_tabs; }
    QLabel *header() const { return m_header; }

    // ----- PaneView: what the ToolPane asks of any view it hosts --------------------------------
    QString paneTitle() const override { return QStringLiteral("Models"); }
    void focusView() override { focusFilter(); }
    void setHeaderRightInset(int pixels) override;

    // ----- the helper agent (owner, 2026-09-22) ------------------------------------------------
    // The same seam as `SettingsPane`'s and `SessionManager`'s, member for member, so the window
    // wires it with the same `wireConsoleHost` lines. With no factory there is no row at all,
    // which is what keeps this library and its tests free of a window.
    relay::agent::ConsoleFactory onCreateConsole;
    // Where the conversation is kept: the tab's id, sent as `persist {scope: "helper", key}`.
    void setHelperTabId(const QString &tabId);
    // The project this pane's agent works in; empty is a board-less helper, which is supported.
    void setHelperWorkspace(const QString &workspace);
    // The live key for the collapsed row's text, and the hint a *click* on the row earns.
    void setHelperShortcut(const QString &hintId, const QString &keys);
    std::function<void()> onHelperHint;
    // Open the helper and put the cursor in it — Alt+Q, and what a click on the row does.
    void focusHelper();
    void helperDraft(const QString &text);
    const relay::agent::ConsoleHandle &agentConsole() const { return m_console; }
    relay::agent::Context *agentContext() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildPicker();
    void updateHeader();
    void stepTab(int delta);
    bool handleShortcut(QKeyEvent *event);
    // The helper's own half, as SettingsPane has it: the row, the console, the fold between.
    void buildHelperRow(QVBoxLayout *into);
    void ensureConsole();               // builds it, once, on the first expand
    void applyHelperCollapsed();
    void updateHelperRow();             // the live key in the button's own text
    void updateConsoleHeight();         // ~40 % of the pane, never less than a few lines
    void helperScreenMoved();           // what `screen` answers has changed

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
    // The class the priorities page goes back to. It starts as the served pane's mode and then
    // follows whatever the person last looked at, so available → priorities keeps your place.
    QString m_classTab = QStringLiteral("main");
    QString m_pendingFilter;
    // ----- the helper agent (owner, 2026-09-22) ------------------------------------------------
    ModelsContext *m_context = nullptr;    // owned; outlives the console, as §33 requires
    relay::agent::ConsoleHandle m_console;
    QWidget *m_helper = nullptr;           // the foot of the pane: the row and the console
    QWidget *m_askRow = nullptr;           // collapsed: one button, bottom right
    QWidget *m_helperBody = nullptr;       // expanded: the fold row and the console
    QToolButton *m_ask = nullptr;
    QLabel *m_helperHead = nullptr;
    QString m_askKeys;
    QString m_askHintId;   // the hint a click on the row earns (onHelperHint)
    bool m_helperCollapsed = true;
};

}  // namespace relay
