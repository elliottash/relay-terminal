// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The two small widgets that sit around a pane: ToolPane, the non-terminal pane (folder explorer,
// file preview, plan, transcript, Switchboard, settings), and PaneChrome, the button row and drag
// grip in every pane's top-right corner. PaneChrome asks the leaf it is parented to for the room it
// needs, which is the one thing here that has to know what a Pane is -- hence the include.

#include "Pane.h"

#include "FilePanes.h"
#include "BoardPane.h"
#include "ScreenPrompt.h"
#include "TurnTranscript.h"
#include "SettingsPane.h"
#include "SubagentTranscript.h"
#include "OutputLinks.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QTreeView>

#include <functional>

// A non-terminal pane: a folder explorer or a file preview. Lives in the same splitter layout
// as terminal panes and is saved and restored as {"explorer": {"path"}} or {"preview": {"path"}}.
class ToolPane final : public QWidget {
public:
    enum class Kind { Explorer, Preview, Plan, Subagent, Turn, Board, Settings };

    ToolPane(Kind kind, const QString &path, bool planActions = true) : m_kind(kind) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        if (kind == Kind::Plan) {
            // Editable Markdown: agent plans (with Execute buttons) and documents such as relay.md.
            m_plan = new relay::PlanEditor;
            m_plan->setPlanActions(planActions);
            layout->addWidget(m_plan);
            m_plan->open(path);
        } else if (kind == Kind::Explorer) {
            m_explorer = new relay::FileExplorer(path);
            layout->addWidget(m_explorer);
        } else {
            m_preview = new relay::FilePreview;
            layout->addWidget(m_preview);
            m_preview->open(path);
        }
    }

    // subagents UI: one pane per main pane, a tab per subagent (card #WD83). Saved with its tabs'
    // text; the agents themselves end with the worker.
    ToolPane(relay::SubagentTabsView *view, const QString &cwd) : m_kind(Kind::Subagent), m_subagent(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }

    // The Switchboard: cards, threads and the card detail view (protocol 17). Saved and restored
    // by workspace and tab, not by path.
    ToolPane(relay::BoardView *view, const QString &cwd) : m_kind(Kind::Board), m_board(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }
    relay::BoardView *board() const { return m_board; }

    // The Actions pane or the Options pane (src/SettingsPane.h; its mode says which). Transient: not saved with the layout (node() is empty).
    ToolPane(relay::SettingsPane *view, const QString &cwd) : m_kind(Kind::Settings), m_settingsView(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }
    relay::SettingsPane *settings() const { return m_settingsView; }

    // A finished agent turn: tool calls and transcript, opened from the inline summary line.
    ToolPane(relay::TurnTranscriptView *view, const QString &cwd) : m_kind(Kind::Turn), m_turn(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }
    relay::TurnTranscriptView *turn() const { return m_turn; }

    Kind kind() const { return m_kind; }
    relay::FileExplorer *explorer() const { return m_explorer; }
    relay::FilePreview *preview() const { return m_preview; }
    relay::PlanEditor *plan() const { return m_plan; }
    relay::SubagentTabsView *subagent() const { return m_subagent; }
    QString path() const { return (m_subagent || m_turn || m_board || m_settingsView) ? QString() : m_explorer ? m_explorer->root() : m_plan ? m_plan->path() : m_preview->path(); }
    QString cwd() const { return (m_subagent || m_turn || m_board || m_settingsView) ? m_subagentCwd : m_explorer ? m_explorer->root() : QFileInfo(path()).absolutePath(); }
    QString title() const {
        if (m_settingsView) return m_settingsView->mode() == relay::SettingsPane::Mode::Actions ? QStringLiteral("Actions") : QStringLiteral("Options");
        if (m_board) return m_board->title();
        if (m_subagent) return m_subagent->title();
        if (m_turn) return m_turn->title();
        if (m_plan) return (m_plan->isDirty() ? QStringLiteral("● ") : QString()) + m_plan->title();
        const QString name = QFileInfo(path()).fileName();
        return name.isEmpty() ? path() : name;
    }
    QJsonObject node() const {
        // The rows board has no tabs to remember; what it keeps is which sections are collapsed and
        // which are unticked in the section checkboxes.
        if (m_board) return {{"board", QJsonObject{{"workspace", m_board->workspace()},
                                                   {"collapsed", m_board->collapsedSections()},
                                                   {"hidden", m_board->hiddenSections()}}}};
        if (m_subagent) return m_subagent->node();
        if (m_turn || m_settingsView) return {};
        if (m_plan) return {{"plan", QJsonObject{{"path", path()}}}};
        return {{m_explorer ? "explorer" : "preview", QJsonObject{{"path", path()}}}};
    }
    void focusInput() {
        if (m_settingsView) m_settingsView->focusSearch();
        else if (m_board) m_board->focusInput();
        else if (m_subagent) m_subagent->focusInput();
        else if (m_turn) m_turn->focusInput();
        else if (m_plan) m_plan->editor()->setFocus(Qt::OtherFocusReason);
        else if (m_explorer) m_explorer->view()->setFocus(Qt::OtherFocusReason);
        else m_preview->setFocus(Qt::OtherFocusReason);
    }

private:
    Kind m_kind;
    relay::FileExplorer *m_explorer = nullptr;
    relay::FilePreview *m_preview = nullptr;
    relay::PlanEditor *m_plan = nullptr;
    relay::SubagentTabsView *m_subagent = nullptr;
    relay::TurnTranscriptView *m_turn = nullptr;
    relay::BoardView *m_board = nullptr;
    relay::SettingsPane *m_settingsView = nullptr;
    QString m_subagentCwd;
};

// ----- pane chrome: button row and drag handle -----------------------------------------------
// A small overlay in each pane's top-right corner. The three a person reaches for — new pane,
// move to a tab of its own, close — are on screen in every pane at all times (owner, 2026-09-17:
// buttons that appear only under the mouse are buttons you have to go looking for). Pointing at
// the pane lifts the row onto its raised tile and adds the two it was hiding: the drag grip and
// "new pane below". Dragging the grip moves the pane, as does dragging a Pane's header.
class PaneChrome final : public QFrame {
public:
    std::function<void(const QString &action)> onAction;

    explicit PaneChrome(QWidget *leaf) : QFrame(leaf) {
        setObjectName(QStringLiteral("paneChrome"));
        setAttribute(Qt::WA_StyledBackground);
        auto *row = new QHBoxLayout(this); row->setContentsMargins(3, 2, 3, 2); row->setSpacing(1);
        // The + makes it obvious that these open a new pane (a new shell and chat), not a layout
        // toggle. Every button is here at all times: the row no longer grows, lifts onto a tile or
        // rearranges itself under the pointer (owner, 2026-09-18). The drag grip is gone with the
        // hover row — pressing anywhere on the header moves the pane.
        button(row, QStringLiteral("⬓+"), QStringLiteral("pane.splitDown"), QStringLiteral("New pane below"));
        button(row, QStringLiteral("◫+"), QStringLiteral("pane.splitRight"), QStringLiteral("New pane to the right"));
        button(row, QStringLiteral("⇱"), QStringLiteral("pane.moveToNewTab"), QStringLiteral("Move to new tab"));
        button(row, QStringLiteral("×"), QStringLiteral("pane.close"), QStringLiteral("Close pane"));
        // The header gives up exactly this much room for good, so the title and the folder line
        // never re-elide.
        adjustSize();
        m_fullWidth = width();
    }

    void place() {
        const auto *leaf = parentWidget();
        adjustSize();
        move(leaf->width() - width() - 6, 4);
        raise();
        syncHeaderInset();
    }

    // Pane title (issue JRWQ): the header's right-hand directory must not end up under these
    // buttons, so the header gives up exactly the room the full row takes.
    void syncHeaderInset() {
        if (auto *pane = dynamic_cast<Pane *>(parentWidget()))
            pane->setHeaderRightInset(isVisible() ? m_fullWidth + 10 : 0);
        // The Switchboard's first row is its tab bar, which these buttons would otherwise cover.
        // So is a preview's header, whose view button names the format and so is wide enough to
        // reach them ("Source (MD)", issue #VXTF), and an explorer's folder line.
        else if (auto *tool = dynamic_cast<ToolPane *>(parentWidget()); tool) {
            const int inset = isVisible() ? m_fullWidth + 4 : 0;
            if (tool->board()) tool->board()->setHeaderRightInset(inset);
            else if (tool->preview()) tool->preview()->setHeaderRightInset(inset);
            else if (tool->explorer()) tool->explorer()->setHeaderRightInset(inset);
            // Settings' search row and a subagent transcript's title row are their first rows too.
            else if (tool->settings()) tool->settings()->setHeaderRightInset(inset);
            else if (tool->subagent()) tool->subagent()->setHeaderRightInset(inset);
        }
    }

protected:
    void showEvent(QShowEvent *event) override { QFrame::showEvent(event); syncHeaderInset(); }
    void hideEvent(QHideEvent *event) override { QFrame::hideEvent(event); syncHeaderInset(); }

public:
    void refreshTooltips() {
        for (auto *b : findChildren<QToolButton *>()) {
            const QString keys = Keymap::instance().shortcutText(b->property("action").toString());
            b->setToolTip(b->property("label").toString() + (keys.isEmpty() ? QString() : QStringLiteral("  (") + keys + ')'));
        }
    }

private:
    QToolButton *button(QHBoxLayout *row, const QString &glyph, const QString &action, const QString &label) {
        auto *b = new QToolButton;
        b->setObjectName(QStringLiteral("paneChromeButton"));
        b->setText(glyph); b->setAutoRaise(true); b->setFocusPolicy(Qt::NoFocus);
        b->setProperty("action", action); b->setProperty("label", label);
        connect(b, &QToolButton::clicked, this, [this, action] { if (onAction) onAction(action); });
        row->addWidget(b);
        return b;
    }

    int m_fullWidth = 0;
};

