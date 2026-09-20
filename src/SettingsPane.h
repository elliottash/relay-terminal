// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The Actions pane and the Options pane: one widget, two modes, a full pane in the splitter layout
// (never a floating strip). The owner's line between them (2026-09-18): an option persists — it is
// a default, written to QSettings, true in every pane after a restart; an action is something you
// do now, to this pane, conversation or window, and may do again or undo an hour later.
//
//  * Actions (Ctrl+Shift+A, Ctrl+?): a search box over one filterable list — Recent first, then
//    every action under its section with its keys. Enter runs the highlighted one.
//  * Options (Ctrl+Shift+O, Ctrl+, and the gear in the title bar): one sub-tab per section, the
//    rows drawn as real controls, changed in place.
//
// Pressing the other pane's key swaps the mode in place, so there is never a second pane. The same
// key, Esc on an empty search, or the pane's × in the chrome row put it away and hand focus back.
// The search box covers both catalogs in either mode, best matches of the pane's own kind first:
// in Actions an option shows as an "Options › …" row that opens Options on that control.
//
// Two catalogs feed it and both are the caller's (RelayWindow builds them):
//
//  * SettingsSection / SettingRow — one row per setting with its own reader and writer, so
//    QSettings stays the single source of truth and nothing here knows how a value is used.
//  * ActionItem — every runnable action, grouped by section and with an optional submenu.
//
// What the reference apps do, and what was taken from each (2026-09-18):
//  * Warp: a settings window with a section list, a search box, instant apply and sub-tabs inside
//    Appearance. Taken: search-first, sections as tabs, no Save button.
//  * Claude Code: /config and /status share one tabbed panel; a short list of options changed in
//    place with Enter; Esc closes and focus returns to the prompt. Taken: Enter changes the
//    highlighted row, Esc closes, focus goes back where it was.
//  * opencode: no settings UI; ctrl+p lists commands and the toggles among them persist. Taken:
//    one search reaches both, so "Ctrl+Shift+A, type, Enter" always lands somewhere.
//  * JetBrains Find Action (Ctrl+Shift+A) and VS Code's palette: a flat list with keys, recent
//    first, separate from Settings. Taken: that separation.
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QHBoxLayout;
class QVBoxLayout;

namespace relay {

// One row of a settings section. The caller supplies the reader (the current value fields) and
// the writer, so QSettings stays the single source of truth.
struct SettingRow {
    enum Kind { Toggle, Choice, Text, Number, Button, Buttons, Info, Heading };
    Kind kind = Toggle;
    QString id;                 // stable identity: keeps focus and scroll across rebuilds
    QString label, detail;
    QString aliases;            // extra search terms
    bool checked = false;                                   // Toggle
    std::function<void(bool)> onToggle;
    QStringList options, optionLabels;                      // Choice
    QString current;
    std::function<void(const QString &)> onChoose;
    QString text, placeholder;                              // Text
    std::function<void(const QString &)> onText;
    int number = 0, minimum = 0, maximum = 0;               // Number
    QString suffix;
    std::function<void(int)> onNumber;
    QString buttonText;                                     // Button
    std::function<void()> run;
    // Buttons: several on one row, left to right, for a row that stands for a thing rather than a
    // value — one saved local model server with Test · Refresh · Remove (card #24XJ). The callback
    // gets the index pressed; Enter on the row presses the first.
    QStringList buttonTexts;
    std::function<void(int index)> onButton;
    // What this row is when Relay ships, put back into effect — not only forgotten, because the
    // theme, the log level and the keymap are on screen and would otherwise stay until the next
    // start. Setting it is what puts the row under its page's "Reset to defaults" button, so the
    // row helpers set it for every row they build and a row written out by hand sets its own. A
    // row that stands for a thing rather than a value — a button, a saved server, an info line —
    // leaves it empty and is never touched by a reset.
    std::function<void()> reset;
    // Whether the value on this row is *not* what Relay ships with. Whoever builds the row knows
    // its default and says so here; the pane then draws a ↺ between the row's words and its
    // control — the changed indicator and the way back in one mark — and a dot on the section's
    // tab. A row with no `reset` has nothing to go back to and is never marked.
    bool changed = false;
    // Text rows: a Browse… button beside the box that picks a folder, for a row whose value is a
    // path (the Plans folder). A path is mistyped far more easily than it is browsed to.
    bool browse = false;
    // ----- what an agent may do with this row (card #FEJQ, protocol §30.2) ----------------------
    // The keyring holds this value — an API key, a token. Its value is never put in the catalog an
    // agent is sent and it can never be set by one (owner decision 1, 2026-09-20: "every value row
    // … except secrets, which no agent may set"). It is marked *here*, where the row is built and
    // whoever writes it knows what it holds; guessing it from the id downstream is how a key ends
    // up in a transcript the day someone renames one.
    bool secret = false;
    // Which of this row's buttons an agent may press (owner decision 2: opt-in, the line being
    // "undoable in one click"). A Button row's own button is index 0; a Buttons row's are its
    // indices, so a provider row can offer Test and withhold Remove. Empty — the default — means
    // no agent may press anything on this row. relay::AppCommands lists these in the action
    // catalog under a `row:<section>/<id>[#n]` key, because a button is an action, not a value.
    QList<int> agentSafeButtons;
};

struct SettingsSection {
    QString id, title, blurb;
    QList<SettingRow> rows;
};

// The "Reset to defaults" row of one page (owner, 2026-09-18: "a reset to defaults button on
// options pages"). It is built from the section itself, so it can only reach the rows on that page
// — it is per page, never one button that resets the app — and a section where nothing declares a
// default (Local models, whose rows are saved servers) gets an empty row back and no button at all.
// `after` is told how many rows were put back, for the notice and for redrawing the other open
// panes; `ask` answers the confirmation and exists for tests, since a QMessageBox cannot be clicked
// headless. Append the result to the section's rows: last on the page, under everything it undoes.
SettingRow resetRow(const SettingsSection &section, std::function<void(int count)> after,
                    std::function<bool(const QString &sectionTitle)> ask = {});

// A setting was written somewhere other than the pane in front of you: an Options pane in another
// tab or another window, a dialog, a page reset, a keymap or theme reload. Every SettingsPane alive
// in this process listens here and redraws itself, so a value on screen is never one edit behind
// (finding 3 of card #XZZB — until 2026-09-19 a pane only rebuilt on its own edits, and the others
// sat there showing what the setting used to be).
//
// A notification is delivered on the event loop and several in a row collapse into one delivery, so
// it is safe to call from inside a control's own signal handler: the rebuild it causes deletes that
// control, and it must not happen while the control is still emitting.
class SettingsWatch {
public:
    static SettingsWatch &instance();
    // `context` owns the subscription: a pane that closes is dropped rather than called.
    void listen(QObject *context, std::function<void()> changed);
    void notify();

private:
    SettingsWatch() = default;
    QList<QPair<QPointer<QObject>, std::function<void()>>> m_listeners;
    bool m_scheduled = false;
};

// A runnable action, or a submenu of them. `checked` marks the current choice; `stayOpen` says
// running it changes state the pane should show at once rather than closing. `typed` lets a
// submenu answer what was typed in the search box with rows of its own, listed first: "Connect to
// host…" offers `ssh me@newbox` for a host that is in no list yet (#S5SH). It returns nothing for
// a search it does not recognise.
struct ActionItem {
    QString key, section, label, detail, shortcut, aliases;
    bool checked = false, stayOpen = false;
    // Whether an agent may run this action (card #FEJQ, protocol §30.2). Off by default, so an
    // action added tomorrow is not something an agent may do today; the catalog builder sets it
    // from relay::appcommands::actionIsAgentSafe(), which is the owner's list of the reversible
    // ones (open or reveal anything, test a key, refresh a server, copy a page, reorder, undo).
    bool agentSafe = false;
    std::function<void()> run;
    std::function<QList<ActionItem>()> children;
    std::function<QList<ActionItem>(const QString &search)> typed;
};

class SettingsPane final : public QWidget {
public:
    enum class Mode { Options, Actions };

    SettingsPane(Mode mode, std::function<QList<SettingsSection>()> sections,
                 std::function<QList<ActionItem>()> actions, QWidget *parent = nullptr);

    // The one page of Actions mode, as a tab id, so currentTab() and tabIds() read the same way.
    static QString actionsTabId() { return QStringLiteral("actions"); }

    std::function<void()> onClose;                          // Esc on an empty search, the pane's own key
    std::function<void(const ActionItem &)> onRun;          // an action was chosen; the caller runs it
    std::function<void()> onModeChanged;                    // the pane's title follows the mode
    // A section's tab came to the front (once per arrival, not on every rebuild). A section whose
    // rows cost something to fill — Local models probes loopback ports — reads this instead of
    // polling on a timer.
    std::function<void(const QString &sectionId)> onSectionShown;

    Mode mode() const { return m_mode; }
    // Swap in place: the search is cleared, the catalogs re-read, the first page shown.
    void setMode(Mode mode);
    // Options mode: show the section's tab with this row highlighted and in view. From Actions
    // mode it switches to Options first, which is what an "Options › …" search row does.
    void revealOption(const QString &sectionId, const QString &rowId);

    void showTab(const QString &id);
    QString currentTab() const;
    void setSearch(const QString &text);
    QString search() const;
    void focusSearch();
    void scrollToGroup(const QString &key);                 // an action submenu, in Actions mode
    // Room kept free at the right of the search row for the pane chrome's buttons (PaneChrome).
    void setHeaderRightInset(int pixels);
    // Re-read both catalogs and redraw, keeping the tab, the scroll position and the focused row.
    void rebuild();

    QStringList tabIds() const;
    // The rows on screen right now, top to bottom: setting ids and action keys. Tests read this.
    QStringList visibleRowIds() const;
    int currentRow() const { return m_current; }
    void moveCurrent(int steps);
    void activateCurrent();

    static int fuzzyScore(const QString &needle, const QString &haystack);

    // ----- rows an agent changed (card #FEJQ, protocol §30.6) ---------------------------------
    // Owner, 2026-09-20: "it should be clear what's changed / done". A row an agent wrote carries
    // a muted note under it — "changed by the agent just now: off → on" — in every Options pane
    // there is, until the person touches that row. The marks are process-wide, like SettingsWatch
    // and for the same reason: the pane that shows a row is not the pane, or the window, the
    // change came through.
    //
    // `previous` and `value` are the values as they read to a person ("off", "on", "collapse"),
    // not JSON: the note is a sentence, and the pane has no business decoding a wire value.
    static void markAgentChanged(const QString &rowId, const QString &previous, const QString &value);
    // The person edited the row: the mark has been answered and goes. Every control on a row calls
    // this before it notifies, so a hand edit anywhere clears it everywhere.
    static void clearAgentChanged(const QString &rowId);
    // The note for a row, or empty when nothing marked it. Tests read this.
    static QString agentChangeNote(const QString &rowId);
    static void forgetAgentChanges();     // tests, and "the session is over"

    // What the Browse… button of a `browse` row opens, given what is in the box (empty for the
    // home folder); it answers with the folder chosen, or an empty string if nobody chose one. It
    // is a folder picker by default and exists as a seam because a QFileDialog cannot be clicked
    // headless — the same reason resetRow() takes its `ask`.
    static void setFolderChooser(std::function<QString(QWidget *parent, const QString &start)> chooser);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Row {                    // one focusable line on screen
        QString id;
        QWidget *widget = nullptr;
        std::function<void()> activate;
    };
    void build();
    void buildPage(QWidget *page, const SettingsSection &section);
    void buildResults(const QString &needle);
    QWidget *settingRow(const SettingRow &row);
    QWidget *actionRow(const ActionItem &item, const QString &prefix = QString());
    QWidget *optionJumpRow(const SettingsSection &section, const SettingRow &row);
    void applyMode();
    QWidget *groupHeader(const QString &text, const QString &key = QString());
    void addActionsList(QVBoxLayout *into);
    void setCurrent(int index, bool scroll);
    void switchTab(int delta);
    void closeRequested();
    void announceTab();
    QScrollArea *currentScroll() const;
    void runAction(const ActionItem &item);

    std::function<QList<SettingsSection>()> m_sections;
    std::function<QList<ActionItem>()> m_actions;
    Mode m_mode = Mode::Options;
    QLineEdit *m_search = nullptr;
    QTabBar *m_tabs = nullptr;
    QStackedWidget *m_pages = nullptr;
    QScrollArea *m_results = nullptr;
    QLabel *m_footer = nullptr;
    QHBoxLayout *m_header = nullptr;
    QStringList m_tabIds;
    QString m_wantedTab;
    QString m_shownTab;             // the last tab onSectionShown was told about
    QList<SettingsSection> m_sectionCache;   // the catalogs as of the last build()
    QList<ActionItem> m_actionCache;
    QHash<QWidget *, QList<Row>> m_pageRows; // every page's rows, so a tab switch needs no rebuild
    QList<Row> m_rows;              // rows of the page on screen, in order
    QHash<QString, QWidget *> m_groups;   // action submenu key → its header on the Actions tab
    int m_current = -1;
    bool m_building = false;
};

}  // namespace relay
