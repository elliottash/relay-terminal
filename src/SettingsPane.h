// SPDX-License-Identifier: GPL-3.0-or-later
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
};

struct SettingsSection {
    QString id, title, blurb;
    QList<SettingRow> rows;
};

// A runnable action, or a submenu of them. `checked` marks the current choice; `stayOpen` says
// running it changes state the pane should show at once rather than closing. `typed` lets a
// submenu answer what was typed in the search box with rows of its own, listed first: "Connect to
// host…" offers `ssh me@newbox` for a host that is in no list yet (#S5SH). It returns nothing for
// a search it does not recognise.
struct ActionItem {
    QString key, section, label, detail, shortcut, aliases;
    bool checked = false, stayOpen = false;
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
