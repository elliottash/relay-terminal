// SPDX-License-Identifier: AGPL-3.0-or-later
// Which emulator core a terminal pane uses.
//
// Every pane runs Relay's own engine (relay::EngineBackend over engine/). KonsolePart was the
// other implementation until 2026-09-18, when the owner retired it: the engine had been the
// default long enough to be the only one. What is left to choose is the core underneath it:
//
//   --engine-core=ghostty|libvterm   which emulator core a pane uses
//   RELAY_ENGINE_CORE=...            same, from the environment
#pragma once

#include <QList>
#include <QString>

class QWidget;

namespace relay {

class TerminalBackend;

// The emulator core for new panes; empty means "the engine's default".
// commandLine wins over environment; anything else is ignored.
QString resolveEngineCore(const QString &commandLine, const QString &environment);

// Process-wide default for new panes, set once from the command line at startup.
QString defaultEngineCore();
void setDefaultEngineCore(const QString &core);

// Creates a terminal backend. Throws std::runtime_error when this build has no engine
// (RELAY_HAVE_ENGINE), which is only the case for the headless test builds.
TerminalBackend *createTerminalBackend(const QString &core, QWidget *parent);

// ----- the terminal pane's right-click menu (issue #X2F1) ---------------------------------------
//
// An entry the engine cannot do is left out rather than shown
// dead, except where it is only unavailable right now (nothing selected, no link under the
// pointer), when it is greyed. Described as data so the list can be checked without a window
// or a shell (tests/backends_test.cpp).

struct TerminalMenuItem {
    QString id;        // "copy", "paste", …; "-" is a separator
    QString label;
    bool enabled = true;
    bool isSeparator() const { return id == QLatin1String("-"); }
};

// What the pane can offer right now. The capability flags come from
// relay::TerminalBackend::capabilities(); the rest is pane state.
struct TerminalMenuState {
    bool hasSelection = false;       // something is selected (engines that can say so)
    bool selectionKnown = true;      // the engine can answer "is anything selected?"
    bool canSearch = false;          // TerminalBackend::Search
    bool canReadOutput = false;      // ScreenText or Scrollback: "Save output as…" has something
    bool canInject = false;          // DisplayInjection: clear-and-reset can be written
    bool canZoom = false;            // the engine can change its font size
    QString link;                    // the URL under the pointer, empty when there is none
    QString cardId;                  // a `#K7Q2` card reference under the pointer ("K7Q2"), or empty
    QString cardTitle;               // what the board calls that card, when it knows a title
    bool hasTurn = false;            // the pane has a finished agent turn to open
    bool canTakeControl = false;     // the terminal is running, so the keyboard can be handed over
    bool canClosePane = true;
    bool canEqualize = false;        // the tab holds another pane, so "Equalize pane sizes" moves something
    QString remoteHost;              // the host of the ssh/mosh session in the pane, or empty (#S5SH)
    QString localSession;            // the local holder session this pane's shell runs in, or empty (#87HB)
};

// The entries for one right-click, in order, with separators as items whose id is "-". Never
// starts or ends with a separator and never has two in a row.
QList<TerminalMenuItem> terminalContextMenu(const TerminalMenuState &state);

// ----- chords for a click on anything link-like in the output (cards #KKYC and #7BYT) --------------
//
// The owner's scheme (2026-09-25, retasked the same day): a plain click opens, Ctrl+click
// navigates the pane's shell there (folders: `cd` into it; files: `cd` to the folder holding
// it), Alt+click adds the link to the agent prompt box and Shift+click opens the system file
// manager / default app. A right-click opens the click menu; it has no modifier case here.
// The keyboard walk passes the same modifiers with the same meanings. Alt wins over Ctrl, Ctrl
// over Shift.
enum class ClickAction { Open, Navigate, AddToPrompt, External };
ClickAction clickActionForModifiers(Qt::KeyboardModifiers modifiers);

// The menu a right-click on a folder opens: "explorer", "navigate" (greyed when the pane has no
// shell to move), "prompt" (greyed when no pane's prompt box can take it), "external" and
// "copypath". Each action label carries its direct chord after a tab, which QMenu draws in its
// shortcut column, so the menu teaches the modifiers.
QList<TerminalMenuItem> folderClickMenu(bool canNavigate, bool canPrompt);

// ----- a click on a file in the output, the same modifiers as a folder's (#KKYC, #7BYT) -------------
//
// A plain click opens it in Relay, Ctrl+click `cd`s the pane's shell to the folder holding it,
// Alt+click adds `@path` to the prompt box and Shift+click opens it with the default app.
// A right-click opens this menu; "edit" is greyed out when the window has no editor, "navigate"
// when the pane has no shell to move, "prompt" when no pane's prompt box can take the mention.
QList<TerminalMenuItem> fileClickMenu(bool canEdit, bool canNavigate, bool canPrompt);

} // namespace relay
