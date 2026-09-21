// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The Switchboard pane: one scrolling list of every open card, in a collapsible section per
// status, with drag and drop, quick add, a filter box and a card detail view with the body, the
// `## Tasks` checklist, the links and the thread with a reply box. No tabs (owner decision,
// 2026-09-18): "bug" and "feature" are labels, done is a status, and the filter box is the way
// to slice the list.
// Design: docs/SWITCHBOARD-DESIGN.md sections 4 and 4.6. Protocol: docs/AGENT-SESSIONS-PROTOCOL.md 19.
//
// The view holds no process: it hands JSON commands to `onSend` and is fed events through
// `handleEvent`, so it can be driven by a per-window Switchboard worker or, in tests, by hand.
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "BoardModel.h"
#include "BoardSections.h"
#include "BoardSignals.h"   // signals: the machine's own faults as rows this list draws (#AQ6X)
#include "AgentContext.h"    // what the agent on this surface is about (#AGNT steps 2, 6)

class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QSplitter;
class QFileSystemWatcher;
class QTextBrowser;
class QToolButton;
class QVBoxLayout;
class RichEditor;

namespace relay {

class CardDetail;
class ColumnHeader;
class RowList;

namespace board {

// The two contexts this file supplies (card #AGNT step 6). The list page's agent is the
// Switchboard agent and the open card's is the card's own; each is a `relay::agent::Context` and
// nothing more — the console that draws them is a no-shell `Pane` the window makes. They are
// defined in BoardPane.cpp because they are the view's own and nothing outside it may name one.
class BoardContext;
class CardContext;

// Below this width the open card takes the whole Switchboard pane instead of squeezing the list
// (BoardView::updateDetailLayout); at or above it the list and the card sit side by side. It
// lives here, out of BoardPane.cpp, because the pane layout asks for it too (card #BXCN): this is
// the width "equalize" gives a board pane before dividing the rest, and the floor Execute and
// Verify keep the board pane above while the panes beside it shrink. A hand drag is still free to
// go below it — it is what the organize and dock arithmetic ask for, not a minimumSizeHint.
inline constexpr int kCardSplitWidth = 900;

// The fix request a clicked problem drafts for the agent's composer (#8YQ9). One wording for the
// banner over the list and for the Check/triage findings above the console, so the two paths
// cannot drift: it names the card when the path has an id in it, quotes the checker, and asks —
// the owner reads it and presses Enter, or does not. Never a send (owner, 2026-09-19: "draft you
// confirm").
//
// It lived in `src/HelperChat.h` while the helper was a panel, and moved here with card #AGNT
// step 9 when that file was retired: both callers are in this view.
inline QString fixRequest(const QString &path, const QString &message, int total = 1)
{
    const QString file = path.trimmed();
    // A card file is `<something>/<id>-<slug>.md` or carries `#ID` in the message; the checker
    // names the file, which is what the agent needs to open it either way.
    QString request = file.isEmpty()
        ? QStringLiteral("The board's check reports: %1").arg(message)
        : QStringLiteral("Fix %1: %2").arg(file, message);
    request += QStringLiteral(" \u2014 please fix this card.");
    if (total > 1)
        request += QStringLiteral(" (%1 problems are reported in all; the Check button lists "
                                  "them.)").arg(total);
    return request;
}

}  // namespace board

class BoardView : public QWidget {
public:
    explicit BoardView(const QString &workspace, QWidget *parent = nullptr);
    // The consoles hold a pointer to their context and clear its `onChanged` as they go, so the
    // widgets come down before the contexts do. `~QWidget` would run the other way round.
    ~BoardView() override;

    // ---- wiring
    std::function<void(const QJsonObject &)> onSend;         // a worker protocol message (board_*, presets, …)
    // `onModelPick` and `onSlashCommand` were here: the view owned two model boxes and two prompt
    // boxes, and had to hand a pick and a `/model` back to the window because it held no
    // QSettings. Both prompt boxes are consoles now — a `Pane` — so the model box, the picker and
    // `/model` are the pane's own in both of them (#PK5Q, card #AGNT step 9). The window keeps
    // `reconfigureBoardWorkers()` for the roles dialog, which is the other writer of that role.
    std::function<void(const QString &reference)> onSendToTerminal;  // `t`: insert #ID in the composer
    std::function<void(const QString &path)> onOpenFile;     // `o`: open the card file in a pane
    // The two link schemes the helper answers with that are not the board's (#FEJQ, §30.4):
    // `option:<section>/<row>` reveals that row in Options, `session:<id>` opens that
    // conversation in the manager. The agent answering here is the tab's helper, so an answer
    // about a setting or a session is as ordinary as one about a card, and should be one click
    // from what it names. `card:` and a file path are the view's own and stay above.
    std::function<void(const QString &sectionId, const QString &rowId)> onOpenOption;
    std::function<void(const QString &sessionId)> onOpenSession;
    // The Tests button on the helper panel's tool row (card #7BM4, design 4.13: board-wide
    // buttons live in that row). The pane it opens is the window's — a splitter pane beside this
    // one, on the same tab's board worker — so the view only says that it was pressed.
    std::function<void()> onOpenTestSuites;
    // The Profile button beside it (#7BM4 phase 5). The window owns the target menu — it has to
    // be anchored under the button, so the button is what is handed over — and the result pane it
    // opens. Unset, the button does nothing.
    std::function<void(QWidget *anchor)> onProfile;
    // A thread entry's pane link (#HKAP): reveal the pane with this session token, in whatever
    // window it lives in. No pane has it (closed, or another machine's board) → inert.
    std::function<void(const QString &token)> onFocusPane;
    // Execute (`x`, #XS6Q): open a terminal pane beside the board whose agent is handed `task`
    // with card `id` attached. The board has already moved the card to In progress. Returns the
    // pane's session token — empty when no pane could be opened — so the hand-off note can name
    // and link it (#HKAP).
    std::function<QString(const QString &id, const QString &task)> onExecuteCard;
    // Whether a pane with this session token is still open in this window (#R9G7). A claimed
    // card wears its pane's token as a chip; a closed pane's chip says so and stops linking.
    // Unset — a test, or a window that cannot look — means every token reads as live.
    std::function<bool(const QString &token)> paneExists;
    // A signal thread's history (#AQ6X phase 3): `threadId` and the owner session it is saved
    // beside, the same two the Sessions manager's `onOpenThread` hands over. A signal a thread
    // claimed wears the *thread's* id as its chip, so the chip opens a thread rather than
    // revealing a pane — no pane has that token. Unset means the chip does nothing.
    std::function<void(const QString &threadId, const QString &ownerSession)> onOpenThread;
    // Verify (`v`, #T71W): open a terminal pane beside the board on `runner` — "guest:codex",
    // "guest:claude" or "preset:<id>", the verifier the worker recommends for this card — and hand
    // it `task`, the QA brief. The card keeps its QA status: only the verifier's verdict moves it.
    // Returns the pane's session token — empty when no pane could be opened — so the hand-off
    // note can name and link it (#HKAP).
    std::function<QString(const QString &id, const QString &runner, const QString &task)> onVerifyCard;
    // A card turn ended (protocol 19.16): `id` the card, `mode` "discuss" or "plan", `outcome`
    // "done", "error" or "cancelled". The window turns the outcomes worth a bell into a
    // notification (#NQP9); the view itself never moves focus, and a cleanup run never gets here
    // (it is not a card turn, so it is not in m_cardTurns — its own panel reports it).
    std::function<void(const QString &id, const QString &mode, const QString &outcome)> onTurnEnded;
    std::function<void(const QString &)> onTitleChanged;
    std::function<void(const QString &)> onStatus;           // one line for the pane's status area
    std::function<void(const QString &id, const QString &text)> onHint;  // shortcut hints
    // The agent console, made by the window and embedded here (card #AGNT steps 5 and 6). The
    // pane libraries cannot construct a `Pane` — it lives only in the app's translation unit — so
    // the window hands over the widget to embed and the handful of calls a host makes on it. The
    // view calls this lazily, the first time a console is actually shown, and with no factory set
    // (a test, or relay-board linked on its own) it simply shows no console and everything else
    // on the board goes on working.
    relay::agent::ConsoleFactory onCreateConsole;

    void handleEvent(const QJsonObject &event);
    void focusInput();
    void reload();                                           // board_open

    QString workspace() const { return m_workspace; }
    QString title() const;
    const board::Model &model() const { return m_model; }
    board::Model &model() { return m_model; }
    // What the list shows right now, headers and cards in order (design 4.6). Public so a test
    // can read the list without walking widgets.
    const QList<board::Row> &rows() const { return m_rows; }

    // Which sections are folded, for the layout node. A new pane starts with every section
    // folded; saved panes restore their exact set with the rest of the window's state.
    QJsonArray collapsedSections() const;
    void setCollapsedSections(const QJsonArray &state);

    // The sections whose "N closed by the agent" row is open (#93WR): the cards the agent
    // finished and closed itself stand behind one row at the end of their section, folded by
    // default. Same shape and the same home in the layout node as the folded set above
    // (`{"board": {"self_closed": [...]}}`), so a pane comes back with the same rows showing.
    QJsonArray openSelfClosed() const;
    void setOpenSelfClosed(const QJsonArray &state);
    // By value, for the reason toggleSection is: every caller names a section out of `m_rows`.
    void toggleSelfClosed(QString columnId);

    // ---- signals (#AQ6X, src/BoardSignals.h, protocol §32)
    //
    // The machine's own faults — a failing test, a broken build — as two toggles and a row each,
    // spliced in **above the first section header** rather than inside a section. They are the
    // board's, not a status's: every section of a new pane starts folded, so a signals row inside
    // one would be invisible exactly when it matters, and a signal has no status the owner moves.
    // A filter is about cards, so the block gets out of its way, as a folded section does.
    // Not called `signals()`: Qt's moc keyword macro makes that word `public:`.
    const board::SignalsState &signalsState() const { return m_signalsState; }
    // The page one signal opens, in the card detail's place. Public so a test drives the four
    // actions through the same path the owner's click takes.
    board::SignalDetail *signalDetail() const { return m_signalDetail; }
    // The signal threads this pane has heard of (#AQ6X phase 3). Public for the same reason the
    // state above is: a test drives it with fake `signal_thread` events.
    const board::SignalThreadsState &signalThreads() const { return m_signalThreads; }
    // Whether a claim's session token names something that is still there: a pane in this window,
    // or a signal thread that is still working. The one answer the chip, its tooltip and its
    // click all go by — a signal thread's token belongs to no pane, so `paneExists` alone would
    // read every working thread as closed.
    bool tokenLive(const QString &token) const;
    // Which of the two rows are open, for the layout node: `["signals"]`, `["signals",
    // "dismissed"]`. Same shape and the same home as the folded set and `self_closed`
    // (`{"board": {"signals": [...]}}`); default folded, so an empty array is the default and an
    // id this version does not know is ignored.
    QJsonArray openSignals() const;
    void setOpenSignals(const QJsonArray &state);
    // "signals" or "dismissed"; by value, for the reason toggleSection's argument is.
    void toggleSignalFold(QString which);
    // Which of the two toggles the selection stands on ("signals", "dismissed"), or empty. As
    // with `selectedFold()`, a card or a signal selected by any other path wins.
    QString selectedSignalFold() const;
    // The signal row the selection stands on, or empty. A signal row is not a card, so
    // `selectedCard()` is empty while it is selected and every card action is inert.
    QString selectedSignal() const;
    void selectSignal(const QString &key);
    // Enter or a click on a signal row: its page, in the card detail's place. A key the board does
    // not have does nothing.
    void openSignal(const QString &key);
    void closeSignal();
    bool signalOpen() const;
    // The `## Signal` section of the open card, as its strip shows it; empty on a card that has
    // none — which is every card but a promoted signal's (plan step 6).
    QString cardSignalStrip() const;
    // Which section's fold row the selection is standing on, or empty. A fold row is not a card,
    // so `selectedCard()` is empty while it is selected and every card action is inert — and a
    // card selected by any other path wins, which is what keeps the two from both being set.
    QString selectedFold() const { return m_selected.isEmpty() ? m_selectedFold : QString(); }
    // By value, not by reference: every caller names a section out of `m_rows`, which the
    // rebuild below replaces.
    void toggleSection(QString columnId);

    // The gear at the end of the section checkboxes: the section list itself, as a page in this
    // pane. Add, remove, merge and rename go out as one `board_sections` message.
    void openSections();
    void closeSections();
    bool sectionsOpen() const { return m_sectionsOpen; }

    // Which sections the checkboxes at the top of the list page are keeping off it:
    // `["deferred", "done"]`, same shape and same home in the layout node as the folded set
    // above. Unchecked, not folded: the section is not on the page at all and its cards are out
    // of the counts. Saved and restored with the rest of the window's state.
    QJsonArray hiddenSections() const;
    void setHiddenSections(const QJsonArray &state);

    // The order of the cards inside each section (board::Sort), as the id the layout node keeps:
    // "manual", "newest", "oldest", "updated", "updated-oldest", "title", "title-desc",
    // "priority" or "priority-low". Saved and restored with the folded set above, so a pane keeps
    // its sort across restarts. Unknown ids read as Manual.
    QString sortOrder() const { return board::sortId(m_model.sort()); }
    void setSortOrder(const QString &id);

    // The labels whose chips beside the section checkboxes are ticked (#VKFV): a card is on the
    // page only when it carries every one of them. Same shape and same home in the layout node
    // as the hidden sections, saved and restored with them.
    QJsonArray labelFilter() const;
    void setLabelFilter(const QJsonArray &state);

    // ---- actions, also reachable from the palette and the keymap
    void quickAdd();
    void quickAddIn(const QString &columnId);
    void openSelected();
    void editSelected();             // `e`: edit the open card's title and issue text
    // `p` / `x` / `v` on the open or the selected card: Plan it, Execute it (#XS6Q) or hand it to
    // its recommended verifier (#T71W). "plan", "execute" or "verify"; Discuss is the reply box's
    // Enter.
    void cardAction(const QString &action);
    void closeDetail();
    // One click on a row's priority flag (#VKFV): `step` is +1 (left click) or −1 (right click),
    // clamped at −1…+3, written through the `board_priority` message and shown at once. The id
    // is by value on purpose: this rebuilds the list, and the caller passes a string owned by
    // `m_rows`, which the rebuild replaces (the section handlers copy for the same reason).
    void setCardPriority(QString id, int step);
    bool detailOpen() const;
    void focusFilter();
    // "Clean up" (protocol 19.9): hands the whole board to the agent to tidy — merge or split
    // sections and cards, review statuses. The first click is always a **preview** (`dry_run`),
    // because a cleanup rewrites many of the owner's files; the result panel then offers Apply.
    // While a run is going the same button is Stop and sends `cancel`.
    void requestCleanup();
    bool cleanupRunning() const { return !m_cleanupRun.isEmpty(); }
    // The Switchboard agent (#8YQ9; card #AGNT step 6): the conversation about the whole board,
    // in a console at the bottom of the list page. `a` puts the keyboard in its composer from
    // anywhere on the board; Check on its action row and every section's ⚠ run the board's own
    // `check()` and show what they find as lines that draft a fix request into that composer.
    void focusHelper();
    void focusChat() { focusHelper(); }   // the name the window has called it by since #8YQ9
    // Which tab this board is in: the key its helper conversation is kept under (§30.7, and
    // `ContextSpec::persistKey`). The window supplies it — a view has no idea what a tab is — and
    // a board with none keeps one conversation for as long as its worker lives, which is what an
    // empty key means on the wire.
    void setTabId(const QString &tabId);
    QString tabId() const { return m_tabId; }
    // The console on whichever page is showing, or null before one has been made. For a test and
    // for the window's "which prompt box is the keyboard in" walk.
    QWidget *listConsole() const { return m_console; }
    QWidget *cardConsole() const;
    // The model box the keyboard should reach from wherever the cursor is (#PK5Q). Null since
    // #AGNT step 6: both prompt boxes are consoles now and a console carries the *pane's* own
    // picker, which answers Alt+M itself. Kept so the window's walk compiles until step 5 stops
    // asking; see the report on `ConsoleHandle`.
    QComboBox *focusedModelBox() const { return nullptr; }
    // `board_check {section}` — a section's ⚠ — or the unscoped Check button when `columnId` is
    // empty. The answer arrives as `board_problems {items, section}`.
    void requestCheck(const QString &columnId = QString());
    void moveSelected();            // the `m` popup
    // Delete (card #CYM9): the open or selected card, after a confirm, through `board_delete`.
    // The owner's action alone — an agent closes a card by moving it to done or dropped.
    void deleteSelected();
    void doneSelected();           // Done (d), through the normal undoable status move
    void undoLast();                // Ctrl+Z: board_undo of this pane's last write
    void copyReference();
    void copyCardReference(const QString &id);
    // A label badge or meta label copies a label: filter term (#S53Z).
    void copyTag(const QString &tag);
    // Zoom to a card by id (#3ZAP): a `#ID` reference in a card's text, and the cleanup panel's
    // `card:` anchors, land here.
    void openCard(const QString &id);
    void sendSelectionToTerminal();
    void openSelectedFile();
    void selectCard(const QString &id);
    QString selectedCard() const { return m_selected; }
    std::function<void()> onNavigationChanged;
    QJsonObject navigationState() const;
    void restoreNavigation(const QJsonObject &state);
    // The notice line over the list: "Moved #K7Q2 to Ready · Undo", or a refused write.
    QString notice() const;
    // One line in that same notice area for a tool the *window* drives — the Profile run's
    // progress (#7BM4 phase 5), which goes exactly where Clean up's does. The view owns the
    // notice, so nothing outside it touches the widget.
    void showToolNotice(const QString &text, bool error = false) { showNotice(text, error); }
    // A clipboard copy says so the way the copy-on-highlight does in a terminal pane (#Y2F4): the
    // same small fading popup, bottom-right and over the board, so a click on a label or reference announces
    // itself where the eye already is. The notice line still carries "Copied #K7Q2" — the toast is
    // the one a reader skimming a card page actually catches.
    void toast(const QString &text, int milliseconds = 1600);

    // Room kept free at the right of the filter row while the pane's hover buttons sit there.
    void setHeaderRightInset(int pixels);

    // Re-render from the model; public so a test can drive it without the worker.
    void rebuild();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void buildChrome(QVBoxLayout *layout);
    // The top of the list page: the count, the filter, "+ New card", and under them one checkbox
    // per section. They belong to the list, not to the pane's header, so an open card is not
    // looking at the list's tools (owner, 2026-09-18). Clean up left this row for the agent's
    // action row (card #AGNT step 6, and #PBX1 before it).
    void buildListTools(QVBoxLayout *layout);
    // The cleanup's result, in the list page itself rather than over it: outcome, counts, every
    // change with its card as a link, the refusals, the agent's report and the changelog.
    void buildCleanupPanel(QVBoxLayout *layout);
    // The Switchboard agent's area, pinned under the list and deliberately outside the splitter
    // (see the comment where it is built). Three things live in it, top to bottom: the Check
    // findings, the survey offer — both **board** widgets, because they are lists to act on and
    // not turns of a conversation — and the console itself, which is the agent (card #AGNT).
    void buildChatArea(QVBoxLayout *layout);
    // Ask the window for the list page's console and embed it, once. Called the first time the
    // area is shown rather than from the constructor: a tab nobody asks anything from never pays
    // for one (§30.7, and the owner's decision 5 on this card).
    void ensureConsole();
    void syncChatVisible();
    // The console is sized to the pane it is in, the way Options and Sessions size theirs: at
    // most ~40 % of the pane's height, and never so little that the transcript is a slot rather
    // than a conversation. Without the floor the column takes the console's own size hint — a
    // `Pane`'s, which is a terminal's — and the list page gave it three rows: the integration
    // drive of card #AGNT read a whole answer, a thinking fold, a tool row and the §12 queue
    // strip as missing, when what was missing was the room to draw them in.
    void updateConsoleHeight();
    // Put a request in the console's composer and focus it — a **draft**, never sent (owner,
    // 2026-09-19: "draft you confirm"). What the problems banner and every finding row do.
    void draftForAgent(const QString &text);
    // Triage and Check findings, from `board_problems {items, section}`; `section` empty is the
    // unscoped Check. A clickable list above the console, and a click drafts `board::fixRequest`.
    // Returns the number shown.
    int showFindings(const QJsonArray &items, const QString &section);
    // `board_survey {root, project, hints, counts, proposals, git}` (19.18): what `project_probe`
    // found offline and what an import would create. The agent narrates the same data in its
    // opening turn — this is the part the owner has to *act* on, so it is checkboxes and a button
    // above the console rather than prose inside it.
    void showSurvey(const QJsonObject &event);
    void hideSurvey();
    void applyImport();                       // `board_import_apply {keys}` for the ticked rows
    void lookForIssues();                     // `forge_sync_plan`: what a sync would do, writing nothing
    void showForgePlan(const QJsonObject &event);
    void showForgeError(const QJsonObject &event);

    // ---- what the two contexts ask of the view (card #AGNT step 6) --------------------------
    // What is on screen in this pane right now — the filter, the sections and their counts, the
    // open card. It rides on each `ask` as `screen` and is cut at 2 000 characters by the spec;
    // it is a hint about what is being read, never a dump of the catalog (§30.7).
    QString screenHint() const;
    // A link the owner activated in an answer, offered to the context first: a `card:` or `#ID`
    // zooms on this board, an `option:` reveals that row and a `session:` opens that conversation
    // (#FEJQ, §30.4, and the link kinds step 8 landed). True means handled.
    bool resolveAgentLink(const relay::links::Target &target);
    QString openCardId() const;
    QList<relay::agent::Action> cardActions() const;
    // Enter, Ctrl+Enter and Ctrl+Shift+Enter in the card page's console, offered to `CardContext`
    // before the pane routes the line (#AGNT step 5): a card's submit travels as `board_ask`
    // (19.10), which writes the thread and advances the stage. Always true.
    bool cardSubmitFromConsole(const QString &route);
    // A card turn started or ended, or a cleanup did: the row's Plan, Execute and Verify wait
    // while one runs. One name for what used to be `syncModelBoxEnabled`, which disabled the two
    // model boxes for the same reason.
    void cardBusyChanged();
    // Tell both contexts that something `spec()` or `actions()` would now answer differently has
    // moved — on the **next turn of the event loop**, never inside the call that noticed.
    // `Context::changed()` makes the console rebuild its action row, and a rebuild frees the very
    // button whose click is still on the stack (src/Pane.h, `rebuildActionRow`, deletes each
    // widget outright), so a Clean up that turns into Stop would delete itself mid-click.
    void refreshContexts();
    bool m_contextRefresh = false;
    // Ask the window for the open card's console and embed it in the card page, once.
    void ensureCardConsole();
    // What the board's key legend adds for the console's action row: `" &nbsp; <b>k</b> check"`
    // per keyed action, in row order. Read off the context rather than written out, so an action
    // a later session adds gets its entry in the line for free.
    QString agentActionKeyLine() const;
    // A card turn ended. The worker wrote the thread entry (19.10, `_card_answer`), so this only
    // makes sure the page showing that thread has read it back.
    void cardTurnFinished(const relay::agent::TurnRecord &record);
    void startCleanup(bool dryRun);
    void endCleanup();                        // the run is over, whatever ended it
    void updateCleanupButton();
    void showCleanupSummary(const QJsonObject &summary);
    void hideCleanupPanel();
    // One line about the run in the notice area: the step it is on and what it has written or
    // proposed so far. It stays up until the run ends — a cleanup takes minutes.
    void showCleanupProgress(const QString &step);
    // A turn event, a board_activity or a summary belonging to a cleanup (19.9: `cleanup: true`,
    // a `run_id` and never a `card_id`). Returns true when it was one, so an open card's thread
    // never sees it.
    bool handleCleanupEvent(const QString &type, const QJsonObject &event);
    void buildQuickAdd(QVBoxLayout *layout);
    void closeQuickAdd();
    // One checkbox per section the model has right now, rebuilt only when that set changes.
    void syncSectionChecks();
    void syncLabelChecks();       // the label chips under them, ditto (#VKFV)
    void applyRightInset();         // keeps the pane's hover buttons off whichever row is on top
    // The pane's hover buttons take their room out of the top row for good, so in a narrow pane
    // the two buttons drop to a line of their own rather than eliding to "…".
    void layoutListTools();
    void updateCounts();
    // The list's column header says what is on (board::Sort): which cell wears the arrow.
    void syncColumnHeader();
    void refill();
    void moveCard(const QString &id, const QString &columnId, const QString &beforeId,
                  const QString &afterId);
    void moveToTab(const QString &id, const QString &tabId);
    // The delete both paths land in (the Del key, the button, the `m` menu entry): ask, then
    // send `board_delete`. `deletedHere`/`forgetDeleted` track the cards this pane has asked to
    // delete, so their own removal events leave the "Deleted #ID · Undo" toast standing.
    void deleteCard(const QString &id);
    bool deletedHere(const QString &card) const;
    void forgetDeleted(const QString &card);
    // One `board_update` for what the card detail's editor changed, against the hash the card
    // was read at. The worker writes the file; a stale hash comes back as `board_conflict`.
    void saveCardEdit(const QJsonObject &patch, const QString &baseHash);
    void executeCard(const QString &note);
    void verifyCard(const QString &note);
    void send(QJsonObject message);
    QString nextRequestId();
    // `canOverride` puts the Check gate's "Override…" on the notice (#7BM4): it is there only
    // for a move the worker refused because the card's `## Tests` do not prove it yet.
    void showNotice(const QString &text, bool error, const QString &undoWriteId = QString(),
                    bool canOverride = false);
    // Ask for the reason and re-send the refused move with `override` on it.
    void overrideGatedMove();
    void placeNotice();
    // The toast's own corner: bottom-right of the pane, above the keys row the notice also keeps
    // clear of, and re-anchored on every resize while it is up.
    void placeToast();
    void watchIssues();
    // The card files worth a watch of their own (#N5JJ): the one the page is open on, its
    // thread, and the cards an agent is executing while the watch budget lasts. A directory
    // watch does not fire on an in-place write, and a watched file is dropped the moment it is
    // replaced, so this is called again after every fire and whenever the open card changes.
    void watchCardFiles();
    // One debounced `board_refresh` because the pane is being looked at again (#N5JJ): on show,
    // and when the focus arrives. What a directory watch missed is picked up here.
    void catchUp();
    // Ask the worker about the filter's plain words (#7M6E). Debounced by `m_searchTimer`;
    // `startSearch` restarts it, `sendSearch` is what fires. Nothing is sent for a filter with
    // no plain words in it — the scoped terms are answered here, from the rows.
    void startSearch();
    void sendSearch();
    // The pane's own empty area. `retry` puts a Retry button under the words, which nothing but
    // a worker failure does; every other state clears it.
    void setEmptyText(const QString &text, bool retry = false);
    void showProblems(const QJsonArray &problems);
    void step(int delta);                  // Up/Down, across section breaks
    void reorder(int delta);               // Alt+Shift+Up/Down, inside the section
    void shiftSection(int delta);          // Alt+Shift+Left/Right, to the next status
    void foldSelected();                   // Left
    void unfoldNearest();                  // Right
    void selectRow(int index);
    bool handleBoardKey(QKeyEvent *key);
    void updateDetailLayout();
    void autoScrollDuringDrag();
    QStringList columnIds() const;
    QString sectionTitle(const QString &columnId) const;
    // Where quick add files a new card: the first category folder board.yaml names. With no
    // tabs there is no "current" category, and `m` re-files a card that belongs elsewhere.
    QString defaultCategory() const;
    bool sectionTakesNewCards(const QString &columnId) const;
    int rowHeight() const;

    QString m_workspace;
    // The `issues` directory the worker that owns this view is reading, learned from the `board`
    // event's `root` and then used to refuse events from another project's worker (see
    // handleEvent). Empty while no event has carried one — older workers send none.
    QString m_root;
    board::Model m_model;
    QString m_selected;
    QJsonObject m_restoreNavigation;
    // The section whose fold row the selection is on (#93WR), or empty. Exactly one of this and
    // m_selected is ever set: the fold row is a selectable row that is not a card.
    QString m_selectedFold;
    QSet<QString> m_selfClosedOpen;   // sections whose self-closed cards are showing (#93WR)
    // ---- signals (#AQ6X). The state is the worker's, replaced whole by every `signals_changed`;
    // `m_signalFolds` holds "signals" and "dismissed" for the two toggles that are open, and
    // exactly one of m_selected / m_selectedFold / m_selectedSignal / m_selectedSignalFold is
    // ever set — a signal row is a row that is not a card.
    board::SignalsState m_signalsState;
    QSet<QString> m_signalFolds;
    QString m_selectedSignal, m_selectedSignalFold;
    board::SignalDetail *m_signalDetail = nullptr;
    bool m_signalsAsked = false;      // `signals_list` has gone out for this view
    // request id -> the signal it was sent about, so a refusal lands on the page that asked
    // rather than as a notice over a list nobody is looking at.
    QHash<QString, QString> m_signalRequests;
    // The signal threads (#AQ6X phase 3) and the notification entry each one posted, so the
    // `finished` event amends the entry the `started` event put up instead of posting a second.
    board::SignalThreadsState m_signalThreads;
    QHash<QString, QString> m_signalThreadNotices;   // thread id -> notification entry id
    // What this pane claims signals under. The Switchboard pane runs no agent of its own, so it
    // claims under its own view token: two board panes never chase one test, and a signal thread
    // (#AQ6X phase 3) will claim under its thread id instead. An unknown token reads `closed` on
    // the chip, which is honest — there is no pane to reveal.
    QString paneClaimToken() const;
    // A claim chip was activated: reveal the pane it names, or open the signal thread's history
    // when the token is a thread's (#AQ6X phase 3).
    void revealClaim(const QString &token);
    // One `signal_thread` event as a notification: posted on `started`, amended in place on
    // `finished`, so the bell never shows two lines about one fault.
    void announceSignalThread(const board::SignalThreadRun &run);
    // Ask the worker for the signal state once, when the board has loaded and the pane is on
    // screen (§32.2). The worker pushes after every change from then on.
    void askSignalsOnce();
    // One `signals_*` write about the signal the page is open on (protocol §32.2): the key and a
    // request id of this pane's own, so the worker's refusal comes back to the page that asked
    // rather than to a notice over a list nobody is looking at.
    void sendSignal(const QString &type, const QJsonObject &fields);
    // Which of the two toggles holds this signal: "dismissed" for a dismissed one, "signals"
    // otherwise. Empty for a key the board does not have.
    QString signalFoldOf(const QString &key) const;
    // The card turns running right now, by card id (protocol 19.16). Several cards can be
    // planning or discussing at once and only one of them is on screen, so the *fact* of each
    // turn is held here rather than in the card view: the strip, the list's working marker, the
    // Execute/Verify refusal and the end-of-turn bell all read it. What the turn has *said* is
    // not here any more — since card #CTRN it is an ordinary console turn and its text lives in
    // that card's own console, which is the only console its events are delivered to.
    struct CardTurn {
        QString mode;                   // "discuss" or "plan"
        QString unsent;                 // the question in flight, to put back if it is refused
    };
    QHash<QString, CardTurn> m_cardTurns;
    QString m_busyCard;                 // the card told "a cleanup is running", to un-tell it
    bool m_open = false;

    // The pane's header row. It holds nothing at all while the list is on screen — the list's
    // own tools sit at the top of the list page — and one "← Back to board" control while a
    // card is open (owner, 2026-09-18).
    QWidget *m_head = nullptr;
    QHBoxLayout *m_tools = nullptr;     // the header row's layout, for the hover-button inset
    QToolButton *m_back = nullptr;
    QHBoxLayout *m_listTools = nullptr; // the count, the filter and "+ New card"
    QWidget *m_toolsWrapRow = nullptr;  // where those two buttons go when the row is too narrow
    QHBoxLayout *m_toolsWrap = nullptr;
    QToolButton *m_add = nullptr;
    // The list's own column header (board::Sort): the Card, Created and Updated cells a click
    // sorts by, over the rows and under the tools.
    ColumnHeader *m_columnHeader = nullptr;
    bool m_toolsWrapped = false;
    QWidget *m_checks = nullptr;        // the section checkboxes, wrapping in a narrow pane
    QLayout *m_checksLayout = nullptr;
    QStringList m_checkIds;             // the sections the boxes stand for, in order
    QStringList m_checkTitles;          // and what they were called when the boxes were built
    // The label chips under them (#VKFV): one per label the board carries, ticked to keep only
    // the cards that have it. Rebuilt when the board's set of labels changes, like the boxes.
    QWidget *m_labelChecks = nullptr;
    QLayout *m_labelChecksLayout = nullptr;
    QStringList m_labelIds;             // the labels the chips stand for, in order
    QSet<QString> m_labelPicked;        // the ticked ones: every one must be on a shown card
    // The gear at the end of that row, and the page it opens in this pane.
    board::SectionEditor *m_sections = nullptr;
    bool m_sectionsOpen = false;
    QJsonObject m_config;               // the last config block, to notice a section change
    int m_rightInset = 0;
    QLabel *m_count = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *m_problems = nullptr;
    // The fix request the problems banner drafts when it is clicked (#8YQ9): the first error's
    // card or file and its message, ready for the page agent's composer.
    QString m_problemFix;
    QFrame *m_notice = nullptr;
    QLabel *m_noticeText = nullptr;
    QToolButton *m_noticeUndo = nullptr;
    // The Check gate (#7BM4): the button on the refusal notice, the moves this pane has sent
    // (by request id, so a refusal can find the one it answers), and the refused move itself,
    // waiting for a reason.
    QToolButton *m_noticeOverride = nullptr;
    QHash<QString, QJsonObject> m_pendingMoves;
    QJsonObject m_gatedMove;
    QTimer *m_noticeTimer = nullptr;
    // The hash-copy toast (#Y2F4): one label built on first use, the same `toast` object name a
    // pane's own popup wears, so the theme styles it without a rule of its own.
    QLabel *m_toast = nullptr;
    QTimer *m_toastTimer = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_keys = nullptr;
    QWidget *m_listPane = nullptr;      // the quick-add field over the list; hidden when stacked
    RowList *m_list = nullptr;
    QSplitter *m_splitter = nullptr;
    CardDetail *m_detail = nullptr;
    QWidget *m_quickAddRow = nullptr;
    QPointer<QLineEdit> m_quickAdd;
    QString m_quickAddColumn;

    // ---- the Switchboard agent, under the list on the list page (card #AGNT step 6).
    //
    // `m_chatArea` is the column: the findings list, the survey offer and then the console. The
    // first two are the board's own widgets — an owner acts on them, they are not a conversation
    // — and they draft into the console exactly as they drafted into the old panel's composer.
    QWidget *m_chatArea = nullptr;
    QWidget *m_findings = nullptr;
    QVBoxLayout *m_findingsLayout = nullptr;
    QWidget *m_survey = nullptr;
    QVBoxLayout *m_surveyLayout = nullptr;
    QToolButton *m_import = nullptr;         // "Import 7 cards", rebuilt with the survey
    QStringList m_importKeys;
    QToolButton *m_forgeLook = nullptr;      // "Look for issues on GitHub" (#GDQN, offer only)
    QLabel *m_forgeResult = nullptr;
    QString m_forgeRepo, m_forgeRequest;
    // The list page's console and what the window handed back with it. The context outlives the
    // console and the console outlives nothing: `~BoardView` takes the widgets down first.
    QWidget *m_console = nullptr;
    relay::agent::ConsoleHandle m_consoleHandle;
    board::BoardContext *m_boardContext = nullptr;
    board::CardContext *m_cardContext = nullptr;
    // The tab this board is in: the key the helper conversation is kept under (§30.7).
    QString m_tabId;
    // The contexts read the view's own state — the filter, the cleanup, the open card — to
    // answer `spec()` and `actions()`. They are the view's, made with it and freed with it.
    friend class board::BoardContext;
    friend class board::CardContext;

    // ---- the cleanup run (protocol 19.9). One at a time, on the whole board.
    QWidget *m_cleanupPanel = nullptr;      // the result, in the list page
    QLabel *m_cleanupHead = nullptr;
    QTextBrowser *m_cleanupBody = nullptr;
    QToolButton *m_cleanupApply = nullptr, *m_cleanupLog = nullptr, *m_cleanupDismiss = nullptr;
    // Empty when nothing is running. It is set to a placeholder on the click, before
    // `board_cleanup_started` names the real run, so the button says Stop from the click and a
    // card's ask is refused in this pane rather than by the worker.
    QString m_cleanupRun;
    QString m_cleanupRequest;               // the id this pane sent the message under
    QString m_cleanupChangelog;             // where the run says it will write its changelog
    bool m_cleanupDry = false;
    int m_cleanupWrites = 0, m_cleanupCards = 0;
    QElapsedTimer m_cleanupClock;
    // A worker that dies mid-run would otherwise leave the button on Stop for good: the last turn
    // event starts this, and it ends the run if no summary follows.
    QTimer *m_cleanupGuard = nullptr;
    // What the list shows: one entry per visible row, in order, so the widget's row i is this
    // list's entry i. The delegate and the drop logic read it; nothing else holds card data.
    QList<board::Row> m_rows;
    QSet<QString> m_collapsed;          // the folded sections
    QSet<QString> m_hidden;             // the sections whose checkbox is unticked
    // Done and Deferred fold themselves the first time the list is drawn, unless the window's
    // saved state has already said which sections are folded.
    bool m_collapsedSeeded = false;
    // Requests this view sent carry an id with this prefix, so the worker's answer to them (a
    // write, an undo, a refusal) is told apart from another pane's or an agent's.
    QString m_requestPrefix;
    int m_requestSeq = 0;
    QString m_lastWrite;            // write_id of this pane's last write, for Undo
    QHash<QString, QString> m_pendingNotes;   // request id -> "Moved #K7Q2 to Ready"
    // request id -> the card this pane asked to delete (#CYM9), until the write lands, is
    // refused, or the card comes back.
    QHash<QString, QString> m_pendingDeletes;
    // A drag runs a nested event loop inside the list's startDrag(); refilling the list there
    // would delete the items under it, so a rebuild waits for the drag to end.
    bool m_dragActive = false, m_rebuildPending = false;
    bool m_detailSized = false;     // the split was sized for the open card already
    // Which of the two pages that sizing was for (#AQ6X): the card's or the signal's, so opening
    // the other one re-divides the splitter instead of leaving it sized for the first.
    bool m_sizedForSignal = false;
    bool m_replyOnOpen = false;     // `c` before the card arrived: focus its reply box then
    bool m_editOnOpen = false;      // `e` before the card arrived: start editing it then
    // That edit is a card the quick-add field has just made, whose `## Issue` is the one line
    // typed there. The editor offers that line selected rather than as settled text (#VZ69).
    bool m_editOnOpenFresh = false;
    QString m_actionOnOpen;         // `p` / `x` / `v` before the card arrived: "plan", "execute", "verify"
    QTimer *m_follow = nullptr;     // the open card follows the selection, debounced
    QTimer *m_dragScroll = nullptr; // scrolls the list while a card is dragged near an edge
    // The cards are files: a write from a pane agent, a collaborator's `git pull` or an editor
    // all reach the pane the same way (protocol 19.2, board_refresh).
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_refresh = nullptr;
    // The filter bar's plain words, asked of the worker (#7M6E, `board_search`): the rows no
    // longer carry each card's text, so the words go down the pipe instead of scanning 2.2 MB on
    // this thread per keystroke. Debounced, so typing never queues a search per key, and only
    // the newest request id is believed, so an answer that arrives out of order is dropped.
    QTimer *m_searchTimer = nullptr;
    QString m_searchRequest;        // the request id of the search in flight, or empty
    QString m_searchAsked;          // the words that request asks about
    // False once a worker has refused `board_search` — one too old to know it. The filter then
    // matches the row's own fields, which is what it did before the message existed, and the
    // pane stops asking rather than collecting one refusal per burst of typing.
    bool m_searchSupported = true;
    // What the worker last said about itself when it was not a card event: a crash, an overflow
    // or an exit. It goes in the pane's own empty area, with a Retry, when the board has never
    // loaded — the status bar it used to go to is not shown in this layout (#7M6E).
    QString m_workerError;
    QWidget *m_emptyRetryRow = nullptr;
    QStringList m_emptySections;    // the section names the empty board draws its jacks from
};

}  // namespace relay
