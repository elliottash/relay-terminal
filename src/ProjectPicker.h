// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The project picker (card #916B): which project a tab that has none should attach to.
//
// Ctrl+Shift+S, `/card` and the palette reach for a Board. In a pane standing in a project
// that is the project's board; in `~/Downloads` or an admin folder there is no candidate at all,
// and this pane is what opens instead of a "no Board here" line. It lists the projects Relay
// knows (`projects::Registry::knownProjects()`, most recently attached first), fuzzy-filtered as
// the user types, with the "Default project for loose cards" preselected when one is set — and,
// always at the top, **"Initialize new project here"**, which makes the pane's own directory a
// project: the board folder through the ordinary init flow, and `git init` when the directory is
// not inside a repository already. Choosing that row *is* the explicit user action the consent
// rule asks for, so nothing asks a second time.
//
// A pane, not an overlay (owner's rule of 2026-09-18): it splits in beside the pane that asked,
// exactly as the Sessions pane does, and closes on Esc or once a project is picked. It knows
// nothing about tabs, the registry or the worker: the window feeds it the projects and takes the
// answer through `onPick` / `onInitHere`, so it is built and driven offscreen in
// tests/projectpicker_test.cpp.
#include "PaneView.h"
#include "Projects.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace relay::projects {

// ----- pure helpers, unit tested ------------------------------------------------------------

// The known projects in the order the picker shows them for `filter`: an empty filter keeps the
// registry's order (most recently attached first); otherwise every record whose name or path
// matches scores by `relayFuzzyScore` — the name's score first, the path's as a tie-break — and
// the rest are left out. A stable sort, so two equal scores keep the registry's order.
QList<Record> rankProjects(const QList<Record> &known, const QString &filter);

// "opened its Board", "chosen in the project picker", … — a `Record::reason` in words, for
// the picker's rows and the Options list. The raw reason when it is not one of the closed set.
QString reasonText(const QString &reason);

// "just now", "3 hours ago", "yesterday", "12 days ago", "12 Sep 2025" — how long ago `unixSeconds`
// was, at `now`. Empty for 0.
QString agoText(qint64 unixSeconds, qint64 now);

// ----- the pane ------------------------------------------------------------------------------

class ProjectPicker : public QWidget, public relay::PaneView {
    Q_OBJECT
public:
    explicit ProjectPicker(QWidget *parent = nullptr);

    // A known project was chosen: attach to it.
    std::function<void(const QString &path)> onPick;
    // "Initialize new project here" was chosen: `here()` is the directory.
    std::function<void()> onInitHere;
    // Esc, or the Close button.
    std::function<void()> onClose;

    // The registry's list, most recently attached first. Rebuilds the rows.
    void setProjects(const QList<Record> &known);
    // `projects::defaultProject()`: preselected while the filter is empty, and marked on its row.
    void setDefaultProject(const QString &path);
    // The directory "Initialize new project here" would initialise — the pane's live cwd. Empty
    // hides that row (there is no pane to look from).
    void setHere(const QString &cwd);
    QString here() const { return m_here; }

    QString filter() const;
    void setFilter(const QString &text);
    // What the list shows, top to bottom: an empty string for the init row, else the path.
    QStringList visiblePaths() const;
    // The selected row's path; empty when the init row is selected (or nothing is).
    QString selectedPath() const;
    bool initRowSelected() const;
    // Enter: the selected row.
    void accept();

    QString paneTitle() const override;
    void focusView() override;
    void setHeaderRightInset(int pixels) override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void rebuild();
    void step(int delta);
    void updateButtons();

    QWidget *m_inset = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_empty = nullptr, *m_hint = nullptr;
    QPushButton *m_attach = nullptr, *m_init = nullptr, *m_close = nullptr;
    QList<Record> m_known;
    QString m_default, m_here;
};

}  // namespace relay::projects
