// SPDX-License-Identifier: AGPL-3.0-or-later
// A pane with no shell is still the whole console (card #AGNT).
//
// The decision this file guards: a pane *is* the agent console, and the terminal is one routing
// of what is typed in it. So a helper surface is not a second widget — it is this same `Pane`
// with a context whose spec says `shell: false`. What must then be true is small and checkable:
// no program is started, the vterm is still there as the transcript, the line can only go to the
// agent, the mode chip is gone, the action row is built from the context, the composer says what
// the context says, and the handles an embedding host needs are there and work.
//
// It builds real `Pane`s, which is why it is its own executable: `Pane` lives only in the `relay`
// target's single translation unit, and `src/Pane.h` compiles on its own (CLAUDE.md).
//
// It is written without `Q_OBJECT` and without QTest's slot machinery on purpose. moc preprocesses
// a translation unit that declares `Q_OBJECT`, and preprocessing this one reaches src/Keymap.h,
// whose preset table is a raw string holding `"Ctrl+Shift+("` -- which moc's own preprocessor
// reads as an unbalanced macro call and refuses. `relay` never meets this because main.cpp
// declares no Q_OBJECT. A plain `main()` with one CHECK macro costs nothing here and keeps the
// gate honest: a failed check names its line and the run exits non-zero.

#include "Pane.h"

#include <QApplication>
#include <QPointer>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QPlainTextEdit>
#include <QToolButton>

#include <cstdio>

namespace {
int failures = 0;
}
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ++failures;                                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                          \
    } while (false)
#define CHECK_EQ(got, want)                                                                        \
    do {                                                                                           \
        const auto &g = (got);                                                                     \
        const auto &w = (want);                                                                    \
        if (!(g == w)) {                                                                           \
            ++failures;                                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s != %s\n", __FILE__, __LINE__, #got, #want);        \
        }                                                                                          \
    } while (false)

namespace {

// A context with no shell, the shape the Switchboard, Options and Sessions will supply.
class StubContext : public relay::agent::Context {
public:
    relay::agent::ContextSpec spec() const override
    {
        relay::agent::ContextSpec spec;
        spec.name = QStringLiteral("switchboard");
        spec.surface = QStringLiteral("stub");
        spec.agentRole = QStringLiteral("main");
        spec.workspace = workspace;
        spec.scope = QStringLiteral("console");
        spec.persistScope = QStringLiteral("helper");
        spec.persistKey = QStringLiteral("tab-1");
        spec.shell = false;
        spec.routing = QStringLiteral("agent");
        return spec;
    }
    QString placeholder() const override { return QStringLiteral("Ask about this board…"); }
    QList<relay::agent::Action> actions() const override { return rows; }
    bool resolveLink(const relay::links::Target &target) override
    {
        seen << target.target;
        kinds << int(target.kind);
        return swallow;
    }
    void turnFinished(const relay::agent::TurnRecord &record) override { finished << record.id; }

    bool submit(const QString &route, const QString &text) override
    {
        submitted << route + QLatin1Char('|') + text;
        return takeSubmit;
    }

    QString workspace;
    QList<relay::agent::Action> rows;
    QStringList seen, finished, submitted;
    QList<int> kinds;
    bool swallow = false, takeSubmit = false;
};

}  // namespace

// Every pane wants somewhere to put a runtime directory and a scrollback; none of these cases
// needs a provider, and none of them starts a turn.
static QTemporaryDir *home = nullptr;

namespace cases {

    // The whole of "no shell": no program is started, and the timers that watch one never run.
void aContextWithoutAShellStartsNoProgram()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    CHECK(!console.hasShell());
    CHECK_EQ(console.shellPid(), 0);
    CHECK_EQ(console.foregroundProcessId(), 0);
    // Through the Host seam, which is where a console makes these calls and where they are
    // public; on `Pane` itself they are the terminal half's own business.
    const relay::agent::Host &surface = console;
    CHECK_EQ(surface.terminalMode(), relay::input::TerminalMode::Unknown);
    }

    // …and the transcript surface is untouched, which is the trade card #AGNT made: one emulator
    // per console, and the whole ANSI / fold / OSC 8 transcript for nothing.
void theTranscriptSurfaceIsStillThere()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    CHECK(console.findChild<QWidget *>(QStringLiteral("agentActionRow")) != nullptr);
    CHECK(console.overlayArea().isValid());
    const relay::agent::Host &surface = console;
    CHECK(surface.bubbleRow() > 0);
    }

    // A line typed here can only go to the agent, and the chip that offers anything else is gone.
void theRoutingIsLockedToTheAgent()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    CHECK_EQ(console.mode(), QStringLiteral("agent"));
    }

    // A terminal pane is untouched by all of it: it has a shell, it routes as it always did, and
    // its action row is empty and hidden. This is the case that says the change costs the
    // terminal nothing.
void aTerminalPaneIsUnchanged()
{
    Pane pane(home->path(), home->path(), true);
    CHECK(pane.hasShell());
    CHECK(pane.context() != nullptr);
    CHECK_EQ(pane.contextSpec().name, QStringLiteral("terminal"));
    CHECK_EQ(pane.contextSpec().shell, true);
    CHECK_EQ(pane.contextSpec().routing, QStringLiteral("auto"));
    CHECK_EQ(pane.contextSpec().persistScope, QStringLiteral("pane"));
    CHECK(pane.contextActions().isEmpty());
    auto *row = pane.findChild<QWidget *>(QStringLiteral("agentActionRow"));
    CHECK(row != nullptr);
    CHECK(!row->isVisibleTo(&pane));
    }

    // The row above the box is the context's, left to right, each button wearing its letter, and
    // a letter two actions claim is refused rather than answered twice (#PBX1).
void theActionRowIsBuiltFromTheContext()
{
    StubContext context;
    context.workspace = home->path();
    int checked = 0;
    context.rows = {{QStringLiteral("check"), QStringLiteral("k"), QStringLiteral("Check"),
                     QString(), false, true, [&checked] { ++checked; }},
                    {QStringLiteral("cleanup"), QStringLiteral("u"), QStringLiteral("Clean up"),
                     QString(), false, true, [] {}},
                    // the same letter again: kept on the row, clickable, and keyless
                    {QStringLiteral("kites"), QStringLiteral("k"), QStringLiteral("Kites"),
                     QString(), false, true, [] {}}};
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    auto *row = console.findChild<QWidget *>(QStringLiteral("agentActionRow"));
    CHECK(row != nullptr);
    const auto buttons = row->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    CHECK_EQ(buttons.size(), 3);
    CHECK_EQ(buttons.at(0)->text(), QStringLiteral("Check (k)"));
    CHECK_EQ(buttons.at(1)->text(), QStringLiteral("Clean up (u)"));
    CHECK_EQ(buttons.at(2)->text(), QStringLiteral("Kites"));   // its letter was taken
    CHECK(row->isVisibleTo(&console));
    // The two properties src/Theme.cpp keys the row's face on: `actionRow` is the shape every
    // no-typing button over a prompt box shares, and `leaves` is the accent outline for the ones
    // that hand the card to a pane. Without them a card's Plan and Execute came out as bare
    // Fusion buttons — the rules that used to give them their face are id rules written for the
    // card page's old *push* buttons, and this row is tool buttons.
    for (auto *button : buttons) CHECK(button->property("actionRow").toBool());
    CHECK(!buttons.at(0)->property("leaves").toBool());
    // And the row never takes the keyboard from the box under it: "Tab stays between the reply
    // box and the card" (#PBX1). `NoFocus` is the whole of it — a focusable button on the row
    // would take Tab, and a focused button paints a ring, which is what the first action of a
    // card's row was read as (`docs/qa_evidence/2026-09-21-agents-are-consoles/punch/
    // b02-card.png`; the ring there was the missing `border-color` in src/Theme.cpp, and this is
    // the other half of "it must not look focused, because it cannot be").
    for (auto *button : buttons) CHECK_EQ(button->focusPolicy(), Qt::NoFocus);
    CHECK(!console.focusWidget() || !buttons.contains(qobject_cast<QToolButton *>(console.focusWidget())));
    CHECK(console.runActionLetter(QStringLiteral("k")));
    CHECK_EQ(checked, 1);
    CHECK(!console.runActionLetter(QStringLiteral("z")));
    // An action that **leaves the surface** — Execute and Verify hand the card to a pane — wears
    // the accent outline, and src/Theme.cpp keys that on this property.
    context.rows = {{QStringLiteral("exec"), QStringLiteral("x"), QStringLiteral("Execute"),
                     QString(), true, true, [] {}}};
    context.changed();
    const auto leaving = row->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    CHECK_EQ(leaving.size(), 1);
    if (!leaving.isEmpty()) {
        CHECK(leaving.first()->property("actionRow").toBool());
        CHECK(leaving.first()->property("leaves").toBool());
    }
    }

    // A context that says something moved is re-read, and the row follows it.
void aChangedContextRebuildsTheRow()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    auto *row = console.findChild<QWidget *>(QStringLiteral("agentActionRow"));
    CHECK(row->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly).isEmpty());
    context.rows = {{QStringLiteral("plan"), QStringLiteral("p"), QStringLiteral("Plan"),
                     QString(), false, true, [] {}}};
    context.changed();
    CHECK_EQ(row->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
    CHECK(row->isVisibleTo(&console));
    }

    // An action that moves its own row. "Clean up" becomes "Stop" the moment it starts, so
    // `Action::run` raises `Context::changed()` and the row is rebuilt from inside the button's
    // own `clicked` — with that button still on the stack, and its `std::function` still running.
    // Freeing either there is a SIGSEGV, and it is the **pane's** to avoid: a context must not
    // have to know when it is safe to say that something moved (card #AGNT step 6 met this).
void anActionThatRebuildsItsOwnRowIsSafe()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    int runs = 0;
    context.rows = {{QStringLiteral("cleanup"), QStringLiteral("u"), QStringLiteral("Clean up"),
                     QString(), false, true, [&context, &runs] {
                         ++runs;
                         context.rows = {{QStringLiteral("stop"), QStringLiteral("u"),
                                          QStringLiteral("Stop"), QString(), false, true, [] {}}};
                         context.changed();
                     }}};
    context.changed();
    auto *row = console.findChild<QWidget *>(QStringLiteral("agentActionRow"));
    auto buttons = row->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    CHECK_EQ(buttons.size(), 1);
    QPointer<QToolButton> first = buttons.isEmpty() ? nullptr : buttons.first();
    if (first) first->click();
    CHECK_EQ(runs, 1);
    CHECK(first);   // only queued for deletion, so the click it is still inside can return
    buttons = row->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    CHECK_EQ(buttons.size(), 1);
    if (!buttons.isEmpty()) CHECK_EQ(buttons.first()->text(), QStringLiteral("Stop (u)"));
    // The keyboard half runs the same action through the same rebuild.
    context.rows = {{QStringLiteral("cleanup"), QStringLiteral("u"), QStringLiteral("Clean up"),
                     QString(), false, true, [&context, &runs] {
                         ++runs;
                         context.rows = {{QStringLiteral("stop"), QStringLiteral("u"),
                                          QStringLiteral("Stop"), QString(), false, true, [] {}}};
                         context.changed();
                     }}};
    context.changed();
    CHECK(console.runActionLetter(QStringLiteral("u")));
    CHECK_EQ(runs, 2);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(!first);   // and it is freed once the event loop reaches it
    }

    // The composer's grey text is a **ladder** built from the context's own line, not that line
    // and "…" (`relay::agent::placeholderRungs`, which `agentcontext` tests rung by rung). What
    // this case holds down is the wiring: the pane asks the context, builds the ladder and hands
    // the whole of it to the editor, so a wide box says the context's whole sentence.
void theComposerSaysWhatTheContextSays()
{
    class Wordy final : public StubContext {
      public:
        QString placeholder() const override
        {
            return QStringLiteral("Reply \u2014 Enter discusses, Ctrl+Enter plans, Ctrl+Shift+Enter only comments");
        }
    } context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.resize(1400, 700);
    if (QLayout *layout = console.layout()) layout->activate();
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) return;
    CHECK_EQ(editor->placeholderText(),
             QStringLiteral("Reply \u2014 Enter discusses, Ctrl+Enter plans, Ctrl+Shift+Enter only comments"));
    // A terminal pane keeps RichEditor's own ladder: its context says the same words, and one
    // rung would stop it shortening as the pane narrows.
    Pane terminal(context.workspace, context.workspace, false, relay::defaultEngineCore());
    terminal.resize(1400, 700);
    if (QLayout *layout = terminal.layout()) layout->activate();
    auto *plain = terminal.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(plain != nullptr);
    if (plain) CHECK(plain->placeholderText().startsWith(QStringLiteral("Shell commands")));
    }

    // A click on an action button teaches its letter once, and pressing the letter teaches
    // nothing — WARP.md's standing rule, on the id `relay::agent::Action::key` names
    // ("board.action." + key). Before this the row was the one fast path in the console with no
    // hint at all: the letters were in the board's key legend and nowhere the mouse could find
    // them. A keyless action has nothing to teach and stays quiet.
void aClickedActionTeachesItsLetter()
{
    relay::ShortcutHints::instance().resetAll();
    StubContext context;
    context.workspace = home->path();
    context.rows = {{QStringLiteral("check"), QStringLiteral("k"), QStringLiteral("Check"),
                     QString(), false, true, [] {}},
                    {QStringLiteral("tests"), QString(), QStringLiteral("Tests"),
                     QString(), false, true, [] {}}};
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    auto *row = console.findChild<QWidget *>(QStringLiteral("agentActionRow"));
    CHECK(row != nullptr);
    const auto buttons = row->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    CHECK_EQ(buttons.size(), 2);
    if (buttons.size() != 2) return;

    buttons.at(0)->click();
    auto *toast = console.findChild<QLabel *>(QStringLiteral("toast"));
    CHECK(toast != nullptr);
    if (toast) CHECK_EQ(toast->text(), QStringLiteral("Next time: k · check"));
    CHECK_EQ(relay::ShortcutHints::instance().shownCount(QStringLiteral("board.action.check")), 1);

    // The keyless one has no letter to name, so it draws nothing rather than an empty hint.
    relay::ShortcutHints::instance().resetAll();
    buttons.at(1)->click();
    CHECK_EQ(relay::ShortcutHints::instance().shownCount(QStringLiteral("board.action.tests")), 0);

    // And the letter path teaches nothing: somebody who typed `k` knows what `k` does.
    relay::ShortcutHints::instance().resetAll();
    CHECK(console.runActionLetter(QStringLiteral("k")));
    CHECK_EQ(relay::ShortcutHints::instance().shownCount(QStringLiteral("board.action.check")), 0);
    }

    // A context that takes the line before the pane routes it: a card's Enter travels as
    // `board_ask`, not as an ordinary `ask` (19.10, owner decision 2). The pane clears and
    // remembers the draft either way, so the composer behaves the same.
void aContextMaySwallowASubmit()
{
    StubContext context;
    context.workspace = home->path();
    context.takeSubmit = true;
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.draftInComposer(QStringLiteral("discuss this card"));
    console.interruptAgentWithPrompt();   // Ctrl+Enter with an idle agent: the ordinary submit
    CHECK_EQ(context.submitted, QStringList{QStringLiteral("agent|discuss this card")});
    // The box is the context's while it handles the line: this stub took it and cleared nothing,
    // so the words are still there. A card that refuses an ask mid-cleanup relies on exactly
    // that — the pane clearing the box would throw away a prompt nobody sent.
    CHECK_EQ(console.composerText(), QStringLiteral("discuss this card"));
    }

    // All three chords, on a console with **no shell**, and after a turn. Ctrl+Shift+Enter is
    // the composer's "terminal, never the model" chord; on a card it is the comment that writes
    // the thread and calls no model, so a console that has no terminal must still hand it to its
    // context rather than refusing it for want of one. The live drive of card #AGNT could not
    // tell whether it was the page, a busy guard or its own aim — it is none of the three: the
    // route travels, and the drive was clicking a placeholder that a finished turn had replaced.
void everyChordReachesTheContextWithItsOwnRoute()
{
    StubContext context;
    context.workspace = home->path();
    context.takeSubmit = true;
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) return;

    auto chord = [&](Qt::KeyboardModifiers mods, const QString &text) {
        console.draftInComposer(text);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, mods);
        QCoreApplication::sendEvent(editor, &press);
    };
    chord(Qt::NoModifier, QStringLiteral("discuss"));
    chord(Qt::ControlModifier, QStringLiteral("plan"));
    chord(Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("comment"));
    CHECK_EQ(context.submitted,
             (QStringList{QStringLiteral("auto|discuss"), QStringLiteral("agent|plan"),
                          QStringLiteral("shell|comment")}));
    // And again after a turn has run: nothing about a finished turn takes the chord away, and
    // the pane's own mode is locked to the agent either way (`applyContextRouting`).
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("agent_finished")},
                                           {QStringLiteral("id"), QStringLiteral("1")},
                                           {QStringLiteral("outcome"), QStringLiteral("done")}});
    chord(Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("after the turn"));
    CHECK_EQ(context.submitted.last(), QStringLiteral("shell|after the turn"));
    }

    // What the worker is told, and what rides on an ask.
void theContextBlockAndTheAskFieldsAreTheContextsOwn()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    const QJsonObject block = console.contextBlock();
    CHECK_EQ(block.value(QStringLiteral("name")).toString(), QStringLiteral("switchboard"));
    CHECK_EQ(block.value(QStringLiteral("shell")).toBool(), false);
    CHECK_EQ(block.value(QStringLiteral("routing")).toString(), QStringLiteral("agent"));
    CHECK_EQ(block.value(QStringLiteral("scope")).toString(), QStringLiteral("console"));
    CHECK_EQ(console.contextAskFields().value(QStringLiteral("surface")).toString(),
             QStringLiteral("stub"));
    }

    // The handles an embedding host needs (steps 5-7), and the fold Options and Sessions put the
    // console behind.
void theHostsHandlesWork()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    CHECK_EQ(console.widget(), static_cast<QWidget *>(&console));
    console.draftInComposer(QStringLiteral("what is left on this board?"));
    CHECK_EQ(console.composerText(), QStringLiteral("what is left on this board?"));
    CHECK(!console.collapsed());
    console.setCollapsed(true);
    CHECK(console.collapsed());
    CHECK(!console.isVisibleTo(console.parentWidget()));
    console.setCollapsed(false);
    CHECK(!console.collapsed());
    }

    // A console is not a window's leaf. It registers itself nowhere on construction, so the only
    // way it can be walked into is a parent that is walked into — which is why step 5 has to stop
    // `RelayWindow::panesIn` descending into a `ToolPane`. What is checkable here is the half
    // that is this file's: an embedded console is an ordinary child of whatever holds it, it is
    // not a top-level window, and it goes when its host goes with nothing left pointing at it.
void aConsoleIsAnOrdinaryChildOfItsHost()
{
    StubContext context;
    context.workspace = home->path();
    QPointer<Pane> console;
    {
        QWidget host;
        console = new Pane(context.workspace, context.workspace, false,
                           relay::defaultEngineCore(), &context);
        console->setParent(&host);
        CHECK(!console->isWindow());
        CHECK_EQ(console->window(), &host);
        CHECK(!QApplication::topLevelWidgets().contains(console.data()));
    }
    CHECK(console.isNull());
    }

    // Every kind of link the transcript can carry is offered to the context **first**, with the
    // kind already worked out, and only what the context refuses travels on to the window
    // (#AGNT step 8, and the punch list's first item). The four forms are the four things an
    // answer can name: a row of Options, a saved conversation, a card, and a path.
    //
    // The kind matters as much as the target. `Context::resolveLink` implementations switch on
    // it — `BoardView::resolveAgentLink` opens a card, reveals a setting or shows a session by
    // `target.kind` alone — so a console that handed every link over as `Kind::Path` would have
    // every context refuse everything, silently, and the window would open the ones it could.
void theContextGetsFirstRefusalOnEveryLinkKind()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QStringList opened;
    console.onOpenOption = [&opened](const QString &section, const QString &row) {
        opened << QStringLiteral("option ") + section + QLatin1Char('/') + row;
    };
    console.onOpenSessions = [&opened](const QString &query) { opened << QStringLiteral("session ") + query; };
    console.onOpenCard = [&opened](const QString &id) { opened << QStringLiteral("card ") + id; };
    console.onOpenPath = [&opened](const QString &path, int) { opened << QStringLiteral("path ") + path; };

    const QString option = relay::links::optionTarget(QStringLiteral("terminal"),
                                                      QStringLiteral("copy_on_select"));
    const QString session = relay::links::sessionTarget(QStringLiteral("0f3a"));
    const QString card = relay::links::cardTarget(QStringLiteral("K7Q2"));
    const QString path = home->path();

    // Refused by the context: each one goes on to the window, at the thing it names.
    context.swallow = false;
    console.openOutputTarget(option, -1, false);
    console.openOutputTarget(session, -1, false);
    console.openOutputTarget(card, -1, false);
    console.openOutputTarget(path, -1, false);
    CHECK_EQ(context.seen, QStringList({option, session, card, path}));
    CHECK_EQ(context.kinds, QList<int>({int(relay::links::Kind::Option), int(relay::links::Kind::Session),
                                        int(relay::links::Kind::Card), int(relay::links::Kind::Path)}));
    CHECK_EQ(opened, QStringList({QStringLiteral("option terminal/copy_on_select"),
                                  QStringLiteral("session 0f3a"), QStringLiteral("card K7Q2"),
                                  QStringLiteral("path ") + path}));

    // Swallowed by the context — Options revealing its own row, a card page zooming to itself —
    // and then nothing reaches the window: no second Options pane, no second card.
    context.swallow = true;
    context.seen.clear();
    context.kinds.clear();
    opened.clear();
    console.openOutputTarget(option, -1, false);
    console.openOutputTarget(session, -1, false);
    console.openOutputTarget(card, -1, false);
    console.openOutputTarget(path, -1, false);
    CHECK_EQ(context.seen.size(), 4);
    CHECK(opened.isEmpty());
    }

}  // namespace cases

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
    QCoreApplication::setApplicationName(QStringLiteral("relay"));
    QApplication app(argc, argv);
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    qputenv("HOME", scratch.path().toUtf8());
    home = &scratch;

    cases::aContextWithoutAShellStartsNoProgram();
    cases::theTranscriptSurfaceIsStillThere();
    cases::theRoutingIsLockedToTheAgent();
    cases::aTerminalPaneIsUnchanged();
    cases::theActionRowIsBuiltFromTheContext();
    cases::aChangedContextRebuildsTheRow();
    cases::anActionThatRebuildsItsOwnRowIsSafe();
    cases::aContextMaySwallowASubmit();
    cases::everyChordReachesTheContextWithItsOwnRoute();
    cases::aClickedActionTeachesItsLetter();
    cases::theComposerSaysWhatTheContextSays();
    cases::theContextBlockAndTheAskFieldsAreTheContextsOwn();
    cases::theHostsHandlesWork();
    cases::aConsoleIsAnOrdinaryChildOfItsHost();
    cases::theContextGetsFirstRefusalOnEveryLinkKind();

    if (failures == 0)
    std::fprintf(stdout, "consolemode: 15 cases, all passed\n");
    return failures == 0 ? 0 : 1;
}
