// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::ContextDock — the "Agent (Alt+Q)" foot of a system pane (card #3B1B).
//
// Options, Sessions and Models each grew the same foot by hand (src/ModelsPane.cpp says "member
// for member"): one collapsed row at the bottom right until the agent is asked for, then the
// console under a head row that folds it back, at most ~40 % of the pane. The Test suites and
// Sharing panes needed it too, so it is one widget here rather than a fourth and a fifth copy.
//
// It is the `RelayWindow::wireConsoleHost` seam, member for member: the window sets
// `onCreateConsole`, the live Alt+Q text and the click hint, and the console is built through that
// factory on the **first expand**, so a pane nobody asks anything pays for nothing. The context is
// the host pane's and must outlive the console: a host deletes its dock in its own destructor,
// before the context goes (the rule `~ModelsPane` and `~BoardView` keep).
#pragma once

#include "AgentContext.h"

#include <QString>
#include <QWidget>

#include <functional>

class QEvent;
class QLabel;
class QToolButton;

namespace relay {

class ContextDock final : public QWidget {
  public:
    // `context` is not owned. `title` heads the expanded console ("Tests agent"); `about` finishes
    // the row's tooltip ("Ask the Tests agent about these tests").
    ContextDock(relay::agent::Context *context, const QString &title, const QString &about,
                QWidget *parent = nullptr);
    ~ContextDock() override;

    // ----- the console host seam (RelayWindow::wireConsoleHost) -------------------------------
    relay::agent::ConsoleFactory onCreateConsole;
    // The tab and its project are the window's to put into the spec (`TabConsoleContext`).
    void setHelperTabId(const QString &) {}
    void setHelperWorkspace(const QString &) {}
    void setHelperShortcut(const QString &hintId, const QString &keys);
    std::function<void()> onHelperHint;
    void focusHelper();                 // Alt+Q and a click on the row: build, open, focus
    void helperDraft(const QString &text);
    void fold();                        // back to the one row; the conversation is kept
    bool expanded() const { return !m_collapsed; }
    const relay::agent::ConsoleHandle &agentConsole() const { return m_console; }
    relay::agent::Context *agentContext() const { return m_context; }
    // The row's own text, "Agent (Alt+Q)", for a test.
    QString rowText() const;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void ensureConsole();
    void applyCollapsed();
    void updateRow();
    void updateHeight();

    relay::agent::Context *m_context = nullptr;
    relay::agent::ConsoleHandle m_console;
    QWidget *m_askRow = nullptr, *m_body = nullptr;
    QToolButton *m_ask = nullptr;
    QLabel *m_head = nullptr;
    QString m_title, m_about, m_askKeys, m_askHintId;
    bool m_collapsed = true;
};

}  // namespace relay
