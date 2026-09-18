// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Settings pane: a full pane in the splitter layout (never a floating strip), with a search
// box over every setting and every action, one sub-tab per section, and the rows drawn as real
// controls. Ctrl+Shift+A, the gear in the title bar and Ctrl+, open it; the same key, Esc on an
// empty search, or the pane's × in the chrome row put it away and hand focus back.
//
// Two catalogs feed it and both are the caller's (RelayWindow builds them):
//
//  * SettingsSection / SettingRow — one row per setting with its own reader and writer, so
//    QSettings stays the single source of truth and nothing here knows how a value is used.
//  * ActionItem — every runnable action (the actions palette's items), grouped by section and
//    with an optional submenu; the Actions tab lists them with their keys and Enter runs one.
//
// What the reference apps do, and what was taken from each (2026-09-18):
//  * Warp: a settings window with a section list, a search box, instant apply and sub-tabs inside
//    Appearance. Taken: search-first, sections as tabs, no Save button.
//  * Claude Code: /config and /status share one tabbed panel; a short list of options changed in
//    place with Enter; Esc closes and focus returns to the prompt. Taken: Enter changes the
//    highlighted row, Esc closes, focus goes back where it was.
//  * opencode: no settings UI; ctrl+p lists commands and the toggles among them persist. Taken:
//    actions and settings share one search, so the key that used to open the palette still ends
//    in "type, Enter".
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
    enum Kind { Toggle, Choice, Text, Number, Button, Info, Heading };
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
};

struct SettingsSection {
    QString id, title, blurb;
    QList<SettingRow> rows;
};

// A runnable action, or a submenu of them. `checked` marks the current choice; `stayOpen` says
// running it changes state the pane should show at once rather than closing.
struct ActionItem {
    QString key, section, label, detail, shortcut, aliases;
    bool checked = false, stayOpen = false;
    std::function<void()> run;
    std::function<QList<ActionItem>()> children;
};

class SettingsPane final : public QWidget {
public:
    SettingsPane(std::function<QList<SettingsSection>()> sections,
                 std::function<QList<ActionItem>()> actions, QWidget *parent = nullptr);

    // The tab that lists actions with their keys; it also carries the shortcut settings rows.
    static QString actionsTabId() { return QStringLiteral("shortcuts"); }

    std::function<void()> onClose;                          // Esc on an empty search, Ctrl+Shift+A
    std::function<void(const ActionItem &)> onRun;          // an action was chosen; the caller runs it

    void showTab(const QString &id);
    QString currentTab() const;
    void setSearch(const QString &text);
    QString search() const;
    void focusSearch();
    void scrollToGroup(const QString &key);                 // an action submenu on the Actions tab
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
    QWidget *groupHeader(const QString &text, const QString &key = QString());
    void addActionsList(QVBoxLayout *into);
    void setCurrent(int index, bool scroll);
    void switchTab(int delta);
    void closeRequested();
    QScrollArea *currentScroll() const;
    void runAction(const ActionItem &item);

    std::function<QList<SettingsSection>()> m_sections;
    std::function<QList<ActionItem>()> m_actions;
    QLineEdit *m_search = nullptr;
    QTabBar *m_tabs = nullptr;
    QStackedWidget *m_pages = nullptr;
    QScrollArea *m_results = nullptr;
    QLabel *m_footer = nullptr;
    QHBoxLayout *m_header = nullptr;
    QStringList m_tabIds;
    QString m_wantedTab;
    QList<SettingsSection> m_sectionCache;   // the catalogs as of the last build()
    QList<ActionItem> m_actionCache;
    QHash<QWidget *, QList<Row>> m_pageRows; // every page's rows, so a tab switch needs no rebuild
    QList<Row> m_rows;              // rows of the page on screen, in order
    QHash<QString, QWidget *> m_groups;   // action submenu key → its header on the Actions tab
    int m_current = -1;
    bool m_building = false;
};

}  // namespace relay
