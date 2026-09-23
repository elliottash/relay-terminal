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
#include <QScreen>

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
        spec.surface = surface;
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
    QString surface = QStringLiteral("stub");
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
    // Hidden widgets defer resize delivery. Realize the viewport before checking the
    // width-dependent placeholder, as a user sees it (also on headless CI fonts/styles).
    console.show();
    QApplication::processEvents();
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
    terminal.show();
    QApplication::processEvents();
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
void ctrlClickEditsTheActualFile()
{
    StubContext context;
    context.workspace = home->path();
    context.swallow = true;
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    const QString path = home->filePath(QStringLiteral("edit-click.md"));
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("# Heading\nbody\n");
    file.close();
    QString edited, external;
    int line = -1;
    console.onEditPath = [&](const QString &p, int l) { edited = p; line = l; };
    console.onOpenExternal = [&](const QString &p) { external = p; };
    console.openOutputTarget(path, 2, true, Qt::ControlModifier);
    CHECK_EQ(edited, path);
    CHECK_EQ(line, 2);
    CHECK(context.seen.isEmpty());
    edited.clear();
    console.openOutputTarget(path, 0, true);
    CHECK(edited.isEmpty());
    CHECK_EQ(context.seen, QStringList({path}));
    console.openOutputTarget(path, 2, true, Qt::ShiftModifier);
    CHECK_EQ(external, path);
    CHECK(edited.isEmpty());
    context.seen.clear();
    console.openOutputTarget(home->path(), 0, true, Qt::ControlModifier);
    CHECK(edited.isEmpty());
    CHECK_EQ(context.seen, QStringList({home->path()}));
}

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


// ----- the §12 queue strip reads the worker's queue (card #CTRN) --------------------------------
//
// A pane holds its own prompts back client-side and sends one `ask` at a time, so until this card
// the strip drew `m_entries` and nothing else — and a card's prompt never goes that way: it
// travels as `board_ask`, waits in that card's own supervisor and comes back as `queue_changed`.
// It queued and it ran, and there was no row on screen to say so (the owner's report on #CTRN,
// "the queue doesn't work like the main terminal"). One rule for every pane now: the worker's
// list for this console's surface is the truth, the pane's own pending items are the overlay in
// front of it, and a pane whose context names no surface has none of the first.
QJsonObject workerRow(const QString &id, const QString &preview, const QString &surface)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("preview"), preview},
                       {QStringLiteral("surface"), surface},
                       {QStringLiteral("origin"), QStringLiteral("user")},
                       {QStringLiteral("forced"), false}};
}

// Card #R3YN: status is chrome immediately above the input, never part of the editable frame.
// `Pane` is both implementations, so one hierarchy assertion covers the terminal and every
// shell-less helper console; the live checks below prove both constructors take that path.
void relayingStatusSitsOutsideEveryPromptFrame()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    auto *lineWidget = console.findChild<QWidget *>(QStringLiteral("paneBusyLine"));
    CHECK(editor != nullptr);
    CHECK(lineWidget != nullptr);
    if (!editor || !lineWidget) return;
    auto *composer = qobject_cast<QFrame *>(editor->parentWidget());
    CHECK(composer != nullptr);
    CHECK(lineWidget->parentWidget() == &console);
    CHECK(!composer->isAncestorOf(lineWidget));
    CHECK(console.layout()->indexOf(lineWidget) < console.layout()->indexOf(composer));

    const QString normalPlaceholder = editor->placeholderText();
    console.deliverWorkerEvent(QJsonObject{{"event", "subagent_started"}, {"id", "a1"},
                                           {"type", "explore"}, {"description", "inspect the layout"},
                                           {"background", true}});
    CHECK(!lineWidget->isHidden());
    CHECK(lineWidget->accessibleName().startsWith(QStringLiteral("Relaying · waiting for 1 subagent")));
    CHECK_EQ(lineWidget->property("statusState").toString(), QStringLiteral("idle"));
    CHECK_EQ(editor->placeholderText(), normalPlaceholder);   // system status never occupies input
    if (QApplication::cursorFlashTime() > 0) {
        auto *pulse = lineWidget->findChild<QTimer *>(QStringLiteral("paneBusyPulse"));
        CHECK(pulse != nullptr);
        CHECK(pulse && pulse->isActive());
    }

    Pane terminal(home->path(), home->path(), true);
    auto *terminalEditor = terminal.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    auto *terminalLine = terminal.findChild<QWidget *>(QStringLiteral("paneBusyLine"));
    CHECK(terminalEditor != nullptr);
    CHECK(terminalLine != nullptr);
    if (!terminalEditor || !terminalLine) return;
    auto *terminalComposer = qobject_cast<QFrame *>(terminalEditor->parentWidget());
    CHECK(terminalComposer != nullptr);
    CHECK(terminalLine->parentWidget() == &terminal);
    CHECK(!terminalComposer->isAncestorOf(terminalLine));

    // Outside the frame means it no longer inherits native-mode hiding. The explicit suppression
    // keeps the status from becoming an orphan when the whole prompt surface is handed away.
    auto *busyLine = static_cast<PaneBusyLine *>(terminalLine);
    busyLine->setBusy(relay::panestatus::State::Running, QStringLiteral("Relaying · sleep…"), QString());
    CHECK(!busyLine->isHidden());
    terminal.toggleNative();
    CHECK(busyLine->isHidden());
    terminal.toggleNative();
    CHECK(!busyLine->isHidden());
}

// Shift-click is a file action, not ordinary link routing: it keeps the real local path and
// hands it to the desktop opener before a context or Relay preview can consume it (#SFC1).
void shiftClickOnALocalPathOpensItExternally()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    const QString path = home->filePath(QStringLiteral("Dissertation-Report.docx"));
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.close();
    QStringList external, internal;
    console.onOpenExternal = [&external](const QString &opened) { external << opened; };
    console.onOpenPath = [&internal](const QString &opened, int) { internal << opened; };

    console.openOutputTarget(path, -1, true, Qt::ShiftModifier);

    CHECK_EQ(external, QStringList({path}));
    CHECK(internal.isEmpty());
    CHECK(context.seen.isEmpty());
}

QJsonObject queueChanged(const QJsonArray &items, const QJsonArray &steering = {})
{
    return QJsonObject{{QStringLiteral("event"), QStringLiteral("queue_changed")},
                       {QStringLiteral("running"), QStringLiteral("run-1")},
                       {QStringLiteral("paused"), false},
                       {QStringLiteral("items"), items},
                       {QStringLiteral("steering"), steering}};
}

void aQueueChangedForThisSurfaceDrawsRowsTheConsoleNeverSubmitted()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.onWorkerLine = [](const QJsonObject &) {};
    // Two rows of one worker: one this console's, one another console's of the same tab. The
    // consoles of a tab share a supervisor, so every row of one `queue_changed` is not this
    // console's to draw.
    console.deliverWorkerEvent(queueChanged({workerRow(QStringLiteral("q1"), QStringLiteral("trace the card please"), QStringLiteral("stub")),
                                             workerRow(QStringLiteral("q2"), QStringLiteral("not this console's"), QStringLiteral("options"))},
                                            {workerRow(QStringLiteral("s1"), QStringLiteral("and check the tests"), QStringLiteral("stub"))}));
    QStringList previews, ids, kinds;
    for (const Pane::QueueRow &row : console.queueRows()) {
        if (row.kind == QStringLiteral("running")) continue;
        previews << row.preview;
        ids << row.id;
        kinds << row.kind;
    }
    // Delivery order: what the turn takes at its next tool call, then what is queued behind it.
    CHECK_EQ(previews, QStringList({QStringLiteral("and check the tests"), QStringLiteral("trace the card please")}));
    CHECK_EQ(ids, QStringList({QStringLiteral("item:s1"), QStringLiteral("item:q1")}));
    CHECK_EQ(kinds, QStringList({QStringLiteral("steer"), QStringLiteral("agent")}));
    CHECK_EQ(console.queuedPrompts(), 1);
}

void aTerminalPaneIgnoresTheWorkersRowsEntirely()
{
    Pane pane(home->path(), home->path(), true);
    // A terminal context's surface is that pane's own session token, so a row of another
    // console's — or of no console at all — is never its to draw, and the one client of its
    // worker's queue is the pane itself, whose rows `heldHere` suppresses. Together that is
    // "a terminal pane's strip is byte-for-byte what it was".
    CHECK(!pane.contextSpec().surface.isEmpty());
    pane.deliverWorkerEvent(queueChanged({workerRow(QStringLiteral("q1"), QStringLiteral("someone else's"), QStringLiteral("switchboard")),
                                          workerRow(QStringLiteral("q2"), QStringLiteral("untagged"), QString())},
                                         {workerRow(QStringLiteral("s1"), QStringLiteral("another console's steer"), QStringLiteral("card:K7Q2"))}));
    CHECK(pane.queueRows().isEmpty());
    CHECK_EQ(pane.queuedPrompts(), 0);
}

// The optimistic overlay, and what it buys. A line typed into this console's own box travels as
// the context's message (`board_ask`), which carries no request id of the pane's, so the item it
// becomes is matched to what was typed in order. That is what lets ↑ take the row **back** with
// the whole prompt in it: the row itself carries a 120-character preview (`queue.PREVIEW`), and
// a draft built from that would silently truncate what the person wrote.
//
// ↑ is #QRC1's: it takes the head of the queue back as an unsent draft rather than selecting it
// in place. A worker row goes back the only way one can — `queue_remove` naming the card's own
// queue — and the row leaves the strip at once rather than at the worker's next `queue_changed`.
void aLineThisConsoleSentComesBackWithUpAsAnUnsentDraft()
{
    StubContext context;
    context.workspace = home->path();
    context.takeSubmit = true;
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) return;

    const QString typed = QStringLiteral("trace the card please, and say what the thread is missing");
    console.draftInComposer(typed);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &enter);
    CHECK_EQ(context.submitted.size(), 1);
    // A real context clears the box when it takes the line (`CardContext::submit` → the card
    // page's reply handler); this stub only records, so the box is emptied here.
    console.draftInComposer(QString());

    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("queued")},
                                           {QStringLiteral("id"), QStringLiteral("q1")},
                                           {QStringLiteral("request_id"), QStringLiteral("sb-1")},
                                           {QStringLiteral("surface"), QStringLiteral("stub")}});
    console.deliverWorkerEvent(queueChanged({workerRow(QStringLiteral("q1"), typed.left(20), QStringLiteral("stub"))}));
    CHECK_EQ(console.queuedPrompts(), 1);

    // ↑ takes it back: the op names this card's queue, the **whole** prompt is in the box — not
    // the preview the row was drawn from — and the row is off the strip.
    sent.clear();
    context.submitted.clear();
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &up);
    CHECK_EQ(sent.size(), 1);
    CHECK_EQ(sent.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("queue_remove"));
    CHECK_EQ(sent.at(0).value(QStringLiteral("item")).toString(), QStringLiteral("q1"));
    CHECK_EQ(sent.at(0).value(QStringLiteral("surface")).toString(), QStringLiteral("stub"));
    CHECK_EQ(console.composerText(), typed);
    CHECK_EQ(console.queuedPrompts(), 0);
    CHECK(context.submitted.isEmpty());   // a draft, never a send (#QRC1)

    // And Enter sends the edited draft the ordinary way, through the context.
    console.draftInComposer(typed + QStringLiteral(" (and the tests)"));
    QKeyEvent again(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &again);
    CHECK_EQ(context.submitted.size(), 1);
    CHECK(context.submitted.at(0).endsWith(QStringLiteral("(and the tests)")));
}

// A row this console did **not** send: ↑ keeps the selection rather than taking it back, because
// its text is the worker's 120-character preview and a draft built from that would truncate the
// prompt (#QRC1's rule for a row that cannot be recalled). What the selection can then do is the
// strip's own, and every op names the queue it is for.
void aWorkerRowIsRemovedAndMovedWithItsSurface()
{
    StubContext context;
    context.workspace = home->path();
    context.surface = QStringLiteral("card:K7Q2");
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(queueChanged({workerRow(QStringLiteral("q1"), QStringLiteral("first"), QStringLiteral("card:K7Q2")),
                                             workerRow(QStringLiteral("q2"), QStringLiteral("second"), QStringLiteral("card:K7Q2"))}));
    CHECK_EQ(console.queuedPrompts(), 2);

    // ↑ selects; Ctrl+↓ moves the selected row down **the worker's** queue, and `to` is a
    // position in that list rather than in the rows this console draws.
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) return;
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &up);
    QKeyEvent ctrlDown(QEvent::KeyPress, Qt::Key_Down, Qt::ControlModifier);
    QCoreApplication::sendEvent(editor, &ctrlDown);
    CHECK_EQ(sent.size(), 1);
    CHECK_EQ(sent.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("queue_move"));
    CHECK_EQ(sent.at(0).value(QStringLiteral("item")).toString(), QStringLiteral("q1"));
    CHECK_EQ(sent.at(0).value(QStringLiteral("to")).toInt(), 1);
    CHECK_EQ(sent.at(0).value(QStringLiteral("surface")).toString(), QStringLiteral("card:K7Q2"));

    // Shift+Delete, or the × on the row: a card's prompts wait in that card's supervisor and not
    // in the tab's, so the op says which.
    sent.clear();
    CHECK(console.removeRow(QStringLiteral("item:q2")));
    CHECK_EQ(sent.size(), 1);
    CHECK_EQ(sent.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("queue_remove"));
    CHECK_EQ(sent.at(0).value(QStringLiteral("item")).toString(), QStringLiteral("q2"));
    CHECK_EQ(sent.at(0).value(QStringLiteral("surface")).toString(), QStringLiteral("card:K7Q2"));
    CHECK_EQ(console.queuedPrompts(), 1);   // the row leaves at once, not at the worker's echo

    // Esc is the card's turn too, not the tab's: an untagged `cancel` stopped the wrong one.
    sent.clear();
    console.stopAgent();
    CHECK_EQ(sent.size(), 1);
    CHECK_EQ(sent.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("cancel"));
    CHECK_EQ(sent.at(0).value(QStringLiteral("surface")).toString(), QStringLiteral("card:K7Q2"));
}

// ----- Stop pauses the queue, Enter resumes it (card #7JD1) ------------------------------------
//
// Owner, 2026-09-21: "why don't we just copy the functionality and have enter resume." A Stop
// pauses the queue of the surface it named, and until this card the only way back was the strip's
// **Resume** button — which a card page has (it is this same `Pane`) and a phone has not. One rule
// everywhere instead: Enter on the empty prompt box resumes, with the op naming this console's own
// queue; Enter with text submits that prompt and the *worker* resumes the queue behind it, so
// nothing extra goes on the wire and a terminal pane's wire is byte-for-byte what it was.
void enterOnAnEmptyBoxResumesThisConsolesPausedQueue()
{
    StubContext context;
    context.workspace = home->path();
    context.surface = QStringLiteral("card:K7Q2");
    context.takeSubmit = true;
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) return;

    // Nothing paused: Enter on an empty box is what it always was, and sends nothing.
    console.deliverWorkerEvent(queueChanged({workerRow(QStringLiteral("q1"), QStringLiteral("and then the tests"), QStringLiteral("card:K7Q2"))}));
    CHECK(!console.queuePaused());
    QKeyEvent idle(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &idle);
    CHECK(sent.isEmpty());

    // A Stop on this card paused its queue: `queue_changed {paused}` with a row of this surface's.
    QJsonObject paused = queueChanged({workerRow(QStringLiteral("q1"), QStringLiteral("and then the tests"), QStringLiteral("card:K7Q2"))});
    paused.insert(QStringLiteral("paused"), true);
    paused.insert(QStringLiteral("running"), QString());
    console.deliverWorkerEvent(paused);
    CHECK(console.queuePaused());

    // Enter on the empty box resumes **that card's** queue, not the tab's.
    sent.clear();
    QKeyEvent resume(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &resume);
    CHECK_EQ(sent.size(), 1);
    CHECK_EQ(sent.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("resume_queue"));
    CHECK_EQ(sent.at(0).value(QStringLiteral("surface")).toString(), QStringLiteral("card:K7Q2"));
    CHECK(context.submitted.isEmpty());   // a resume, never a prompt

    // And Enter with text submits it through the context and sends **no** resume of its own: the
    // worker clears the pause as that `board_ask` goes past it (`TurnSupervisor.submit`).
    console.deliverWorkerEvent(paused);
    sent.clear();
    console.draftInComposer(QStringLiteral("carry on with the tests then"));
    QKeyEvent typed(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &typed);
    CHECK_EQ(context.submitted.size(), 1);
    CHECK(sent.isEmpty());
}

// A terminal pane's half of this is its own: it holds its queued prompts client side, so a Stop
// pauses `m_entries` and no `queue_changed` says so. Enter on the empty box resumes them, and the
// only message it sends is the `resume_queue` the Resume button already sent — nothing new when
// nothing is paused, which is the byte-for-byte promise.
void aTerminalPanesOwnQueueResumesOnEnterToo()
{
    Pane pane(home->path(), home->path(), true);
    QList<QJsonObject> sent;
    pane.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    auto *editor = pane.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) return;
    CHECK(!pane.queuePaused());
    QKeyEvent idle(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &idle);
    CHECK(sent.isEmpty());
}

void leavingPlanRestoresExactSelection();
void leavingPlanBeforeConfigurationRestoresRole();

void enteringPlanSelectsHigh()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}, {"agent_role", "main"}});
    sent.clear();
    console.setAgentMode(QStringLiteral("plan"));
    CHECK_EQ(sent.size(), 2);
    if (sent.size() != 2) return;
    CHECK_EQ(sent[0].value("type").toString(), QStringLiteral("set_agent_role"));
    CHECK_EQ(sent[0].value("role").toString(), QStringLiteral("high"));
    CHECK_EQ(sent[1].value("type").toString(), QStringLiteral("set_mode"));
    CHECK_EQ(sent[1].value("mode").toString(), QStringLiteral("plan"));
    console.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"model", "test"}, {"agent_role", "high"}});
    sent.clear();
    console.setAgentMode(QStringLiteral("plan"));
    CHECK_EQ(sent.size(), 1);
    CHECK_EQ(sent.last().value("type").toString(), QStringLiteral("set_mode"));
    sent.clear();
    console.setAgentMode(QStringLiteral("build"));
    CHECK_EQ(sent.size(), 2);
    CHECK_EQ(sent.first().value("type").toString(), QStringLiteral("set_agent_role"));
    CHECK_EQ(sent.first().value("role").toString(), QStringLiteral("main"));
    CHECK_EQ(sent.last().value("mode").toString(), QStringLiteral("build"));
    sent.clear();
    console.deliverWorkerEvent(QJsonObject{{"event", "mode_changed"}, {"mode", "build"}});
    CHECK(sent.isEmpty()); // The acknowledgement must not restore a second time.
    leavingPlanRestoresExactSelection();
    leavingPlanBeforeConfigurationRestoresRole();
}

void leavingPlanRestoresExactSelection()
{
    for (const QString role : {QStringLiteral("main"), QStringLiteral("flash"),
                               QStringLiteral("local"), QStringLiteral("high")}) {
        for (const bool workerExit : {false, true}) {
            StubContext context;
            context.workspace = home->path();
            Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
            QList<QJsonObject> sent;
            console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
            console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "original"}});
            console.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"model", "original"},
                {"preset", "original-provider"}, {"effort", "high"}});
            console.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"model", "original"},
                {"preset", "original-provider"}, {"agent_role", role}, {"effort", "high"}});
            const QString capture = qEnvironmentVariable("RELAY_PLAN_RESTORE_CAPTURE");
            const auto screenshot = [&](const QString &stage) {
                if (capture.isEmpty() || role != QStringLiteral("main") || workerExit) return;
                console.resize(950, 500);
                console.show();
                QApplication::processEvents();
                CHECK(console.grab().save(capture + QLatin1Char('/') + stage + QStringLiteral(".png")));
            };
            screenshot(QStringLiteral("01-before"));
            console.setAgentMode(QStringLiteral("plan"));
            console.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"model", "planner"},
                {"preset", "other-provider"}, {"agent_role", "high"}, {"effort", "max"}});
            console.deliverWorkerEvent(QJsonObject{{"event", "mode_changed"}, {"mode", "plan"}});
            screenshot(QStringLiteral("02-plan"));
            console.setAgentMode(QStringLiteral("plan")); // Must not replace the original snapshot.
            sent.clear();
            if (workerExit)
                console.deliverWorkerEvent(QJsonObject{{"event", "mode_changed"}, {"mode", "build"}});
            else
                console.setAgentMode(QStringLiteral("build"));
            CHECK_EQ(sent.size(), workerExit ? 1 : 2);
            CHECK_EQ(sent.first().value("type").toString(), role == QStringLiteral("main")
                ? QStringLiteral("set_model") : QStringLiteral("set_agent_role"));
            if (role != QStringLiteral("main")) CHECK_EQ(sent.first().value("role").toString(), role);
            CHECK_EQ(sent.first().value("preset").toString(), QStringLiteral("original-provider"));
            CHECK_EQ(sent.first().value("model").toString(), QStringLiteral("original"));
            CHECK_EQ(sent.first().value("effort").toString(), QStringLiteral("high"));
            // The switch request carries no new session, reset, or conversation mutation.
            CHECK(!sent.first().contains("session_id"));
            sent.clear();
            console.deliverWorkerEvent(QJsonObject{{"event", "mode_changed"}, {"mode", "build"}});
            CHECK(sent.isEmpty());
            console.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"model", "original"},
                {"preset", "original-provider"}, {"agent_role", role}, {"effort", "high"}});
            CHECK_EQ(console.paneMode(), role);
            screenshot(QStringLiteral("03-restored"));
        }
    }
}

void leavingPlanBeforeConfigurationRestoresRole()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(QJsonObject{{"event", "presets"}, {"presets", QJsonArray{
        QJsonObject{{"id", "test"}, {"model", "test"}, {"local", true},
                    {"base_url", "http://localhost:1/v1"}}}}});
    console.setAgentMode(QStringLiteral("plan"));
    console.setAgentMode(QStringLiteral("build"));
    CHECK_EQ(console.paneMode(), QStringLiteral("main"));
    sent.clear();
    console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}, {"agent_role", "main"}});
    CHECK(std::none_of(sent.cbegin(), sent.cend(), [](const QJsonObject &m) {
        return m.value("type").toString() == QStringLiteral("set_agent_role") &&
               m.value("role").toString() == QStringLiteral("high");
    }));
    CHECK_EQ(console.agentMode(), QStringLiteral("build"));
}

void clickingPlanLeavesModeAndPreservesDraft()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
    auto *chip = console.findChild<QToolButton *>(QStringLiteral("planChip"));
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(chip != nullptr && editor != nullptr);
    if (!chip || !editor) return;
    CHECK(chip->isHidden());
    console.resize(900, 600);
    console.show();
    console.activateWindow();
    console.deliverWorkerEvent(QJsonObject{{"event", "mode_changed"}, {"mode", "plan"}});
    QApplication::processEvents();
    CHECK(chip->isVisible());
    editor->setPlainText(QStringLiteral("Keep this draft"));
    sent.clear();
    const QPointF point = chip->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(chip, &press);
    QApplication::sendEvent(chip, &release);
    CHECK_EQ(sent.size(), 1);
    if (!sent.isEmpty()) {
        CHECK_EQ(sent.last().value("type").toString(), QStringLiteral("set_mode"));
        CHECK_EQ(sent.last().value("mode").toString(), QStringLiteral("build"));
    }
    console.deliverWorkerEvent(QJsonObject{{"event", "mode_changed"}, {"mode", "build"}});
    QApplication::processEvents();
    CHECK(chip->isHidden());
    CHECK_EQ(console.agentMode(), QStringLiteral("build"));
    CHECK_EQ(editor->toPlainText(), QStringLiteral("Keep this draft"));
    CHECK(editor->hasFocus());
}

void planWhileConfiguringSelectsHighBeforeMode()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    // A usable endpoint starts configure, but its reply has not arrived yet.
    console.deliverWorkerEvent(QJsonObject{{"event", "presets"}, {"presets", QJsonArray{
        QJsonObject{{"id", "test"}, {"model", "test"}, {"local", true},
                    {"base_url", "http://localhost:1/v1"}}}}});
    CHECK(std::any_of(sent.cbegin(), sent.cend(), [](const QJsonObject &m) {
        return m.value("type").toString() == QStringLiteral("configure");
    }));
    sent.clear();
    console.setAgentMode(QStringLiteral("plan"));
    CHECK_EQ(console.paneMode(), QStringLiteral("high"));
    CHECK(sent.isEmpty());
    // Configure was already in flight with Main: its reply must not lose the High choice.
    console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}, {"agent_role", "main"}});
    int roleIndex = -1, modeIndex = -1;
    for (int i = 0; i < sent.size(); ++i) {
        if (sent[i].value("type").toString() == QStringLiteral("set_agent_role") &&
            sent[i].value("role").toString() == QStringLiteral("high")) roleIndex = i;
        if (sent[i].value("type").toString() == QStringLiteral("set_mode") &&
            sent[i].value("mode").toString() == QStringLiteral("plan")) modeIndex = i;
    }
    CHECK(roleIndex >= 0);
    CHECK(modeIndex > roleIndex);
}

void repeatedEnterKeepsTheFirstQueuedPrompt()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (!editor) return;
    const auto enter = [&] {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &key);
    };
    console.draftInComposer(QStringLiteral("first queued prompt"));
    enter();
    console.draftInComposer(QStringLiteral("second queued prompt"));
    enter();
    CHECK_EQ(console.queuedPrompts(), 2);
    CHECK(console.composerText().isEmpty());
    sent.clear();
    enter();
    CHECK_EQ(sent.size(), 1);
    if (sent.size() != 1) return;
    CHECK_EQ(sent.first().value("type").toString(), QStringLiteral("ask"));
    CHECK_EQ(sent.first().value("when").toString(), QStringLiteral("steer"));
    CHECK_EQ(sent.first().value("text").toString(), QStringLiteral("first queued prompt"));
    const QString steerId = sent.first().value("id").toString();
    CHECK_EQ(console.queuedPrompts(), 1);
    sent.clear();
    enter();
    CHECK_EQ(sent.size(), 1);
    if (sent.size() != 1) return;
    CHECK_EQ(sent.first().value("type").toString(), QStringLiteral("queue_unsteer"));
    CHECK_EQ(sent.first().value("request").toString(), steerId);
    CHECK_EQ(console.queuedPrompts(), 1);
}

void queueRowArrowSendsOnlyThatPrompt()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.resize(1000, 650);
    console.show();
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent({{"event", "configured"}, {"model", "test"}});
    console.deliverWorkerEvent({{"event", "agent_started"}, {"id", "running"}});
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor);
    if (!editor) return;
    for (const QString &text : {QStringLiteral("First queued prompt"), QStringLiteral("Second queued prompt")}) {
        console.draftInComposer(text);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &enter);
    }
    console.draftInComposer(QStringLiteral("An unrelated draft stays here"));
    { QEventLoop wait; QTimer::singleShot(150, &wait, &QEventLoop::quit); wait.exec(); }
    auto *list = console.findChild<QListWidget *>(QStringLiteral("queueList"));
    CHECK(list && list->count() == 2);
    if (!list || list->count() != 2) return;
    const QModelIndex index = list->model()->index(1, 0);
    CHECK(index.data(QueueRowDelegate::SendNowRole).toBool());
    const QPoint point = QueueRowDelegate::sendNowRect(list->visualRect(index)).center();
    QHelpEvent hover(QEvent::ToolTip, point, list->viewport()->mapToGlobal(point));
    QCoreApplication::sendEvent(list->viewport(), &hover);
    CHECK_EQ(QToolTip::text(), QStringLiteral("Send now (%1)").arg(
        Keymap::instance().shortcutText(QStringLiteral("agent.interrupt"))
            .replace(QStringLiteral("Return"), QStringLiteral("Enter"))));
    const QString capture = qEnvironmentVariable("RELAY_QUEUE_ARROW_CAPTURE");
    if (!capture.isEmpty()) {
        { QEventLoop wait; QTimer::singleShot(100, &wait, &QEventLoop::quit); wait.exec(); }
        CHECK(console.screen()->grabWindow(0).save(capture));
    }
    QToolTip::hideText();
    sent.clear();
    QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(list->viewport(), &press);
    QMouseEvent click(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(list->viewport(), &click);
    QList<QJsonObject> asks;
    for (const auto &message : sent) if (message.value("type") == "ask") asks << message;
    CHECK_EQ(asks.size(), 1);
    if (!asks.isEmpty()) {
        CHECK_EQ(asks.first().value("text").toString(), QStringLiteral("Second queued prompt"));
        CHECK_EQ(asks.first().value("when").toString(), QStringLiteral("interrupt"));
    }
    CHECK_EQ(console.composerText(), QStringLiteral("An unrelated draft stays here"));
    CHECK_EQ(console.queuedPrompts(), 1);
    CHECK_EQ(list->item(0)->text(), QStringLiteral("First queued prompt"));
}

void answersBypassQueuedPrompts()
{
    for (int gesture = 0; gesture < 3; ++gesture) {
        StubContext context;
        context.workspace = home->path();
        Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
        QList<QJsonObject> sent;
        console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
        console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
        console.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
        auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
        CHECK(editor != nullptr);
        if (!editor) return;
        const auto enter = [&](Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, mods);
            QCoreApplication::sendEvent(editor, &key);
        };
        console.draftInComposer(QStringLiteral("queued first")); enter();
        console.draftInComposer(QStringLiteral("queued second")); enter();
        CHECK_EQ(console.queuedPrompts(), 2);
        const auto before = console.queueRows();
        console.showQuestion(QJsonObject{{"id", "question-1"}, {"questions", QJsonArray{
            QJsonObject{{"header", "Scope"}, {"question", "Which?"}, {"options", QJsonArray{
                QJsonObject{{"label", "First"}}, QJsonObject{{"label", "Second"}}}}},
            QJsonObject{{"header", "Detail"}, {"question", "Any details?"}}}}});
        sent.clear();
        enter(); // Empty Enter must not steer queued work while an ask is open.
        CHECK(sent.isEmpty());
        CHECK(console.questionOpen());
        // A card's explicit comment/shell chord is not an answer and still reaches its context.
        context.takeSubmit = true;
        console.draftInComposer(QStringLiteral("separate comment"));
        enter(Qt::ControlModifier | Qt::ShiftModifier);
        CHECK_EQ(context.submitted.last(), QStringLiteral("shell|separate comment"));
        CHECK(console.questionOpen());
        CHECK(sent.isEmpty());
        context.takeSubmit = false;
        const auto answer = [&](const QString &text) {
            console.draftInComposer(text);
            if (gesture == 2) console.interruptAgentWithPrompt(); // action/shortcut entry point
            else enter(gesture == 1 ? Qt::ControlModifier : Qt::NoModifier);
        };
        answer(QStringLiteral("2"));
        CHECK(sent.isEmpty()); // Multiple answers are sent together.
        CHECK(console.questionOpen());
        answer(QStringLiteral("please keep the tests"));
        CHECK(!console.questionOpen());
        CHECK_EQ(sent.size(), 1);
        if (sent.size() == 1) {
            CHECK_EQ(sent.first().value("type").toString(), QStringLiteral("question_answer"));
            CHECK_EQ(sent.first().value("id").toString(), QStringLiteral("question-1"));
            CHECK_EQ(sent.first().value("answers").toArray(),
                     (QJsonArray{QJsonArray{"Second"}, QJsonArray{"please keep the tests"}}));
        }
        CHECK_EQ(console.queuedPrompts(), 2);
        const auto after = console.queueRows();
        CHECK_EQ(before.size(), after.size());
        for (int i = 0; i < before.size() && i < after.size(); ++i) {
            CHECK_EQ(before[i].id, after[i].id);
            CHECK_EQ(before[i].preview, after[i].preview);
        }
    }
}

void proseQuestionHoldsTheQueueForItsReply()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (!editor) return;
    const auto enter = [&] {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &key);
    };
    console.draftInComposer(QStringLiteral("queued task")); enter();
    console.deliverWorkerEvent(QJsonObject{{"event", "delta"}, {"text", "Which option should I use?"}});
    sent.clear();
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_finished"}, {"id", "running"}, {"outcome", "done"}});
    QCoreApplication::processEvents();
    CHECK(console.queuePaused());
    CHECK_EQ(console.queuedPrompts(), 1);
    for (const auto &message : sent) CHECK(message.value("type") != "ask");
    sent.clear();
    console.draftInComposer(QStringLiteral("Use the second option")); enter();
    console.resumeAgentQueue(); // A resume during the worker's busy-event gap must not overtake the reply.
    QCoreApplication::processEvents();
    CHECK_EQ(console.queuedPrompts(), 1);
    QList<QJsonObject> asks;
    for (const auto &message : sent) if (message.value("type") == "ask") asks << message;
    CHECK_EQ(asks.size(), 1);
    if (!asks.isEmpty()) CHECK_EQ(asks.first().value("text").toString(), QStringLiteral("Use the second option"));
}


void rewindRemovesOnlyTheBranchAndLinksItsCompleteText()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.resize(960, 640);
    console.show();
    { QEventLoop wait; QTimer::singleShot(100, &wait, &QEventLoop::quit); wait.exec(); }
    const QString id(32, QLatin1Char('a'));
    console.adoptSessionText(id, home->path(), QString(), QString());
    relay::agent::Host &surface = console;
    surface.writeTerminal(QStringLiteral("retained sentinel\r\n✦ discarded ask\r\n\x1b[32mdiscarded reply\x1b[0m\r\n").toUtf8());
    console.deliverWorkerEvent({{"event", "rewound"}, {"turn", 2}, {"restore", "conversation"},
                               {"prompt", "discarded ask"}, {"rewound_n", 1}});
    { QEventLoop wait; QTimer::singleShot(100, &wait, &QEventLoop::quit); wait.exec(); }
    const QString text = console.paneTextLines(200000).join('\n');
    CHECK(text.contains(QStringLiteral("retained sentinel")));
    CHECK(!text.contains(QStringLiteral("discarded reply")));
    CHECK(!text.contains(QStringLiteral("✦ discarded ask")));
    CHECK(text.contains(QStringLiteral("\n\n2 lines rewound -- click to view\n\n")));
    CHECK_EQ(console.composerText(), QStringLiteral("discarded ask"));
    const QString path = relay::sessiontext::rewoundPath(home->path(), id, 1);
    QFile saved(path);
    CHECK(saved.open(QIODevice::ReadOnly));
    CHECK_EQ(saved.readAll(), QStringLiteral("✦ discarded ask\ndiscarded reply\n").toUtf8());
    // The count is actual output, not empty grid rows; styles are held by the terminal.
    const QString formatted = console.paneFormattedTextLines(200000).join('\n');
    CHECK(formatted.contains(QRegularExpression(QStringLiteral("\\x1b\\[[0-9;]*3[;m]"))));
    auto *backend = console.findChild<relay::EngineBackend *>();
    CHECK(backend != nullptr);
    if (backend) {
        QString target;
        auto *view = backend->view();
        for (int y = 0; y < view->height() && target.isEmpty(); y += 4) {
            const auto hit = view->linkAtPoint(QPoint(140, y));
            if (hit.target == path) target = hit.target;
        }
        CHECK_EQ(target, path);
        QString opened;
        console.onOpenPath = [&](const QString &file, int) { opened = file; };
        console.openOutputTarget(target, 0, true);
        CHECK_EQ(opened, path);
    }
    const QByteArray screenshot = qgetenv("RELAY_REWIND_SCREENSHOT");
    if (!screenshot.isEmpty()) CHECK(console.grab().save(QString::fromLocal8Bit(screenshot)));

    // Code-only rewind and a missing anchor must not clear any output.
    surface.writeTerminal(QStringLiteral("✦ keep this ask\r\nkeep this reply\r\n").toUtf8());
    CHECK(!console.saveRewoundText(2, QStringLiteral("keep this ask"), false));
    CHECK(console.paneTextLines(200000).join('\n').contains(QStringLiteral("keep this reply")));
    CHECK(!console.saveRewoundText(3, QStringLiteral("absent prompt"), true));
    CHECK(console.paneTextLines(200000).join('\n').contains(QStringLiteral("keep this reply")));

    // More than the normal 5,000-line persistence cap: every removed line remains readable.
    QByteArray longBranch = QStringLiteral("✦ long ask\r\n").toUtf8();
    for (int i = 0; i < 5100; ++i) longBranch += "line " + QByteArray::number(i) + "\r\n";
    surface.writeTerminal(longBranch);
    CHECK(console.saveRewoundText(4, QStringLiteral("long ask"), true));
    QFile longSaved(relay::sessiontext::rewoundPath(home->path(), id, 4));
    CHECK(longSaved.open(QIODevice::ReadOnly));
    const auto all = longSaved.readAll();
    CHECK(all.startsWith(QStringLiteral("✦ long ask\nline 0\n").toUtf8()));
    CHECK(all.endsWith("line 5099\n"));
    CHECK_EQ(all.count('\n'), 5101);

    // Earlier notices remain clickable after another rewind rebuilt their rows.
    if (backend) {
        { QEventLoop wait; QTimer::singleShot(100, &wait, &QEventLoop::quit); wait.exec(); }
        const QString snapshot = backend->replayableText(200000).join('\n');
        CHECK(snapshot.contains(QUrl::fromLocalFile(path).toString()));
        CHECK(snapshot.contains(QUrl::fromLocalFile(longSaved.fileName()).toString()));
    }

    // A file occupying the save directory forces failure without permissions assumptions.
    QFile obstacle(home->path() + QStringLiteral("/not-a-directory"));
    CHECK(obstacle.open(QIODevice::WriteOnly));
    obstacle.close();
    console.adoptSessionText(id, obstacle.fileName(), QString(), QString());
    surface.writeTerminal(QStringLiteral("✦ cannot save\r\nstill here\r\n").toUtf8());
    CHECK(!console.saveRewoundText(1, QStringLiteral("cannot save"), true));
    CHECK(console.paneTextLines(200000).join('\n').contains(QStringLiteral("still here")));
}

}  // namespace cases

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
    QCoreApplication::setApplicationName(QStringLiteral("relay"));
    QApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--queue-arrow-only"))) {
        QTemporaryDir queueScratch;
        CHECK(queueScratch.isValid());
        home = &queueScratch;
        relay::theme::applyTheme(app);
        cases::queueRowArrowSendsOnlyThatPrompt();
        return failures ? 1 : 0;
    }
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    qputenv("HOME", scratch.path().toUtf8());
    home = &scratch;

    if (app.arguments().contains(QStringLiteral("--plan-click-only"))) {
        cases::enteringPlanSelectsHigh();
        cases::clickingPlanLeavesModeAndPreservesDraft();
        cases::planWhileConfiguringSelectsHighBeforeMode();
        return failures ? 1 : 0;
    }

    cases::aContextWithoutAShellStartsNoProgram();
    cases::theTranscriptSurfaceIsStillThere();
    cases::theRoutingIsLockedToTheAgent();
    cases::aTerminalPaneIsUnchanged();
    cases::relayingStatusSitsOutsideEveryPromptFrame();
    cases::theActionRowIsBuiltFromTheContext();
    cases::aChangedContextRebuildsTheRow();
    cases::queueRowArrowSendsOnlyThatPrompt();
    cases::anActionThatRebuildsItsOwnRowIsSafe();
    cases::aContextMaySwallowASubmit();
    cases::everyChordReachesTheContextWithItsOwnRoute();
    cases::aClickedActionTeachesItsLetter();
    cases::theComposerSaysWhatTheContextSays();
    cases::theContextBlockAndTheAskFieldsAreTheContextsOwn();
    cases::theHostsHandlesWork();
    cases::aConsoleIsAnOrdinaryChildOfItsHost();
    cases::theContextGetsFirstRefusalOnEveryLinkKind();
    cases::shiftClickOnALocalPathOpensItExternally();
    cases::ctrlClickEditsTheActualFile();
    cases::aQueueChangedForThisSurfaceDrawsRowsTheConsoleNeverSubmitted();
    cases::aTerminalPaneIgnoresTheWorkersRowsEntirely();
    cases::aWorkerRowIsRemovedAndMovedWithItsSurface();
    cases::aLineThisConsoleSentComesBackWithUpAsAnUnsentDraft();
    cases::enterOnAnEmptyBoxResumesThisConsolesPausedQueue();
    cases::aTerminalPanesOwnQueueResumesOnEnterToo();
    cases::repeatedEnterKeepsTheFirstQueuedPrompt();
    cases::enteringPlanSelectsHigh();
    cases::clickingPlanLeavesModeAndPreservesDraft();
    cases::planWhileConfiguringSelectsHighBeforeMode();
    cases::answersBypassQueuedPrompts();
    cases::proseQuestionHoldsTheQueueForItsReply();
    cases::rewindRemovesOnlyTheBranchAndLinksItsCompleteText();

    if (failures == 0)
    std::fprintf(stdout, "consolemode: 20 cases, all passed\n");
    return failures == 0 ? 0 : 1;
}
