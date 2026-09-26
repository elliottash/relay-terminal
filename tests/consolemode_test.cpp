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
#include "Images.h"
#include "RemoteShare.h"
#include "RemoteSettings.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPointer>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLayout>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QScreen>
#include <QSettings>

#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <QThread>
#include <QVector>

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
    QList<relay::agent::ContextCommand> slashCommands() const override { return commands; }
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
    QList<relay::agent::ContextCommand> commands;
    QStringList seen, finished, submitted;
    QList<int> kinds;
    bool swallow = false, takeSubmit = false;
};

}  // namespace

// Card #DSKT: the harness owns the processes its shell cases start, and finds none of them by
// name. Each shell case holds a harness::ProcessGuard for its pane: at case end — a failed CHECK
// still reaches scope end — the guard SIGKILLs the pane's foreground process group and the
// shell's own group and registers the pids. The exit scan (an atexit hook, so main()'s many
// return paths all pass through it) fails the run if a tracked pid is still alive after its
// guard's SIGKILL. A console-mode run leaves nothing behind.
namespace harness {

QVector<int> g_pids;   // shell pids and foreground pids this run started

void track(int pid)
{
    if (pid > 0 && !g_pids.contains(pid))
        g_pids.append(pid);
}

void killGroup(int pgid)
{
    if (pgid > 0)
        ::kill(pid_t(-pgid), SIGKILL);
}

// Case scope: kills what the pane's shell started, whatever the checks above it said. Declare it
// after the Pane so it destructs first.
struct ProcessGuard {
    explicit ProcessGuard(Pane &p) : pane(p) { track(p.shellPid()); }
    ~ProcessGuard()
    {
        track(pane.shellPid());
        track(pane.foregroundProcessId());
        const int foreground = pane.foregroundProcessGroup();
        const int shell = pane.shellPid();
        const int shellGroup = shell > 0 ? int(::getpgid(pid_t(shell))) : 0;
        killGroup(foreground);
        if (shellGroup != foreground)
            killGroup(shellGroup);
    }
    Pane &pane;
};

bool alive(int pid)
{
    int status = 0;
    ::waitpid(pid_t(pid), &status, WNOHANG);   // reap it if it is our zombie child: zombies answer kill()
    return ::kill(pid_t(pid), 0) == 0;
}

// Runs after main returns (and after its stack — the QApplication and every case's Pane — is
// gone). A tracked pid that still answers after the guards' SIGKILL plus this deadline is an
// orphan: say so and fail the run. A clean run exits with main()'s own code.
void scanAtExit()
{
    std::fflush(nullptr);
    for (int i = 0; i < 100; ++i) {
        bool any = false;
        for (const int pid : g_pids)
            any = alive(pid) || any;
        if (!any)
            return;
        QThread::msleep(25);
    }
    int orphans = 0;
    for (const int pid : g_pids) {
        if (alive(pid)) {
            ++orphans;
            std::fprintf(stderr, "FAIL orphaned pid %d is still alive after the harness SIGKILL\n", pid);
        }
    }
    std::fflush(nullptr);
    if (orphans)
        _exit(1);
}

[[maybe_unused]] const bool registered = [] {
    std::atexit(scanAtExit);
    return true;
}();

}   // namespace harness

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

    // A middle click on a pane's header closes it (#5Z6N). Nothing on the header takes a middle
    // press, so Qt carries it on up to the pane and its window; that must not undo the header's
    // claim on it, and a middle click on the terminal below closes nothing.
void aMiddleClickOnTheHeaderClosesThePane()
{
    QWidget window;
    window.resize(900, 600);
    auto *pane = new Pane(home->path(), home->path(), true);
    pane->setParent(&window);
    pane->setGeometry(0, 0, 900, 600);
    window.show();
    QApplication::processEvents();
    int closes = 0;
    pane->onHeaderClose = [&closes] { ++closes; };
    QWidget *header = pane->headerWidget();
    CHECK(header != nullptr && header->isVisible());
    if (!header) return;
    auto *title = header->findChild<QLabel *>();
    QWidget *target = title && title->isVisible() ? title : header;
    const auto click = [](QWidget *widget) {
        const QPoint at = widget->rect().center();
        const QPoint global = widget->mapToGlobal(at);
        QMouseEvent press(QEvent::MouseButtonPress, at, global, Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, at, global, Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &release);
    };
    click(target);
    CHECK_EQ(closes, 1);
    closes = 0;
    click(pane);
    CHECK_EQ(closes, 0);
}

    // The drawn box is QFrame#composer, and the theme sheet styles it by that name: the
    // surface fill, the rounded border, and the copper accent border when a relay is active
    // in the pane (#6JS0). The name is load-bearing in two more places - theme::polishWindow
    // and RelayWindow::repolishLeaf both find the box as the editor's parent frame - so the
    // box stays the editor's direct parent and stays named composer. It was briefly renamed
    // promptBox with an input-area widget between the two, and the box went black.
void theComposerKeepsItsThemeName()
{
    QWidget window;
    window.resize(900, 600);
    auto *pane = new Pane(home->path(), home->path(), true);
    pane->setParent(&window);
    pane->setGeometry(0, 0, 900, 600);
    window.show();
    QApplication::processEvents();
    auto *editor = pane->findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (!editor) return;
    auto *composer = qobject_cast<QFrame *>(editor->parentWidget());
    CHECK(composer != nullptr);
    if (!composer) return;
    CHECK_EQ(composer->objectName(), QStringLiteral("composer"));
    CHECK(qApp->styleSheet().contains(QStringLiteral("QFrame#composer")));
    // Copper, on demand: the accent border shows when a relay is active in the pane.
    composer->setProperty("relayActive", true);
    QApplication::processEvents();
    const QString evidence = qEnvironmentVariable("RELAY_COMPOSER_EVIDENCE");
    if (!evidence.isEmpty())
        pane->grab(QRect(composer->mapTo(pane, QPoint(0, 0)), composer->size()))
            .save(evidence + QStringLiteral("/composer-copper.png"));
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
    // nothing — RELAY.md's standing rule, on the id `relay::agent::Action::key` names
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
// Card #PBZ4: a context's slash commands join the `/` popup after Relay's rows, under
// "✦ <group>", a name Relay already has moved to the plugin's namespace; Enter on one sends the
// prompt it makes from the words after it, and nothing reaches the router.
void aContextsSlashCommandsJoinThePopup()
{
    StubContext context;
    context.workspace = home->path();
    QStringList asked;
    relay::agent::ContextCommand outline;
    outline.name = QStringLiteral("outline");
    outline.description = QStringLiteral("Outline the document.");
    outline.group = QStringLiteral("Markdown");
    outline.space = QStringLiteral("markdown");
    outline.prompt = [&asked](const QString &args) { asked << args; return QStringLiteral("Outline it: ") + args; };
    relay::agent::ContextCommand help = outline;
    help.name = QStringLiteral("help");          // a Relay built-in: offered as /markdown:help
    context.commands = {outline, help};
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.resize(800, 500);
    console.draftInComposer(QStringLiteral("/"));
    QListWidget *popup = nullptr;
    for (QListWidget *list : console.findChildren<QListWidget *>(QStringLiteral("atPicker")))
        if (list->isVisibleTo(&console)) popup = list;
    CHECK(popup != nullptr);
    QStringList names;
    for (int i = 0; popup && i < popup->count(); ++i) names << popup->item(i)->data(Qt::UserRole).toString();
    CHECK(names.contains(QStringLiteral("outline")));
    CHECK(names.contains(QStringLiteral("markdown:help")));
    CHECK(names.indexOf(QStringLiteral("outline")) > names.indexOf(QStringLiteral("help")));
    for (int i = 0; popup && i < popup->count(); ++i)
        if (popup->item(i)->data(Qt::UserRole).toString() == QStringLiteral("outline"))
            CHECK(popup->item(i)->text().contains(QStringLiteral("✦ Markdown · Outline the document.")));
    console.draftInComposer(QStringLiteral("/outline the intro"));
    console.interruptAgentWithPrompt();
    CHECK_EQ(asked, QStringList{QStringLiteral("the intro")});
    CHECK(console.composerText().isEmpty());
    CHECK(context.submitted.isEmpty());          // the context's own submit never saw it
    console.draftInComposer(QStringLiteral("/markdown:outline"));
    console.interruptAgentWithPrompt();
    CHECK_EQ(asked.size(), 2);
    }

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

    // The wedge of #X6XV, and what unwedges it. Busy follows agent_started/agent_finished,
    // but a refused ask carries the worker's `agent_busy` on an error event and a set_model
    // exclusive borrows `running` without a turn: in both cases no agent_finished will ever
    // arrive for the id the pane holds, so the pane spins busy with nothing in flight and
    // Esc's cancel — answered by an idle queue_changed — changed nothing. The queue is the
    // worker's truth, so an event with nothing running, queued or steering clears the flag.
void anIdleQueueChangedClearsABusyFlagNothingWillFinish()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.onWorkerLine = [](const QJsonObject &) {};
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("agent_started")},
                                           {QStringLiteral("id"), QStringLiteral("t1")}});
    CHECK(console.agentActive());
    // A later ask is refused — a model switch landed mid-turn — and the refusal says the
    // worker still counts an agent turn. Nothing will finish one now.
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("error")},
                                           {QStringLiteral("id"), QStringLiteral("ask-7")},
                                           {QStringLiteral("text"), QStringLiteral("An agent turn is already active.")},
                                           {QStringLiteral("agent_busy"), true}});
    CHECK(console.agentActive());
    // Esc's `cancel` on an idle queue is answered with exactly this event.
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("queue_changed")},
                                           {QStringLiteral("running"), QString()},
                                           {QStringLiteral("paused"), false},
                                           {QStringLiteral("items"), QJsonArray{}},
                                           {QStringLiteral("steering"), QJsonArray{}}});
    CHECK(!console.agentActive());
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

void ctrlEnterStartsADeferredGuestOnItsFirstPrompt()
{
    QSettings settings;
    const QVariant previous = settings.value(QStringLiteral("provider/preset"));
    settings.setValue(QStringLiteral("provider/preset"), QStringLiteral("guest:codex"));
    for (const QString prompt : {QStringLiteral("First request"), QString()}) {
        StubContext context;
        context.workspace = home->path();
        Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
        QList<QJsonObject> sent;
        console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
        console.deliverWorkerEvent(QJsonObject{{"event", "presets"}, {"presets", QJsonArray{
            QJsonObject{{"id", "guest:codex"}, {"label", "Codex"}, {"model", "codex"},
                        {"harness", true}, {"group", "guest"}}}}});
        CHECK(std::none_of(sent.cbegin(), sent.cend(), [](const QJsonObject &message) {
            return message.value("type") == QStringLiteral("configure");
        }));
        sent.clear();
        console.draftInComposer(prompt);
        console.interruptAgentWithPrompt();
        CHECK_EQ(sent.size(), 1);
        if (sent.size() == 1) {
            CHECK_EQ(sent[0].value("type").toString(), QStringLiteral("configure"));
            CHECK_EQ(sent[0].value("preset").toString(), QStringLiteral("guest:codex"));
        }
        CHECK_EQ(console.queuedPrompts(), 1);
        console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "codex"},
                                               {"preset", "guest:codex"}, {"agent_role", "main"}});
        QCoreApplication::processEvents();
        QList<QJsonObject> asks;
        for (const QJsonObject &message : sent)
            if (message.value("type") == QStringLiteral("ask")) asks << message;
        CHECK_EQ(asks.size(), 1);
        if (asks.size() == 1) CHECK_EQ(asks[0].value("text").toString(),
                                      prompt.isEmpty() ? QStringLiteral("Continue") : prompt);
    }
    if (previous.isValid()) settings.setValue(QStringLiteral("provider/preset"), previous);
    else settings.remove(QStringLiteral("provider/preset"));
}

void theContextGetsFirstRefusalOnLocalLinkKinds()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QStringList opened;
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent({{"event", "ready"}});
    console.deliverWorkerEvent({{"event", "configured"}, {"model", "test"}});
    sent.clear();
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

    // Local links go to the context first. A session link opens the saved conversation through
    // the worker, even when this console has a context of its own.
    context.swallow = false;
    console.openOutputTarget(option, -1, false);
    console.openOutputTarget(session, -1, false);
    console.openOutputTarget(card, -1, false);
    console.openOutputTarget(path, -1, false);
    CHECK_EQ(context.seen, QStringList({option, card, path}));
    CHECK_EQ(context.kinds, QList<int>({int(relay::links::Kind::Option),
                                        int(relay::links::Kind::Card), int(relay::links::Kind::Path)}));
    CHECK_EQ(opened, QStringList({QStringLiteral("option terminal/copy_on_select"),
                                  QStringLiteral("card K7Q2"),
                                  QStringLiteral("path ") + path}));
    CHECK_EQ(sent.size(), 1);
    if (sent.size() == 1) {
        CHECK_EQ(sent.first().value("type").toString(), QStringLiteral("conversation_open"));
        CHECK_EQ(sent.first().value("session_id").toString(), QStringLiteral("0f3a"));
    }

    // Swallowed by the context — Options revealing its own row, a card page zooming to itself —
    // and then nothing reaches the window: no second Options pane, no second card.
    context.swallow = true;
    context.seen.clear();
    context.kinds.clear();
    opened.clear();
    sent.clear();
    console.openOutputTarget(option, -1, false);
    console.openOutputTarget(session, -1, false);
    console.openOutputTarget(card, -1, false);
    console.openOutputTarget(path, -1, false);
    CHECK_EQ(context.seen.size(), 3);
    CHECK(opened.isEmpty());
    CHECK_EQ(sent.size(), 1); // the session bypasses the local context in both cases
    }


// ----- a board-write toast is a link to its card (#XC75) ----------------------------------------
//
// The toast that names a board write ("◆ #K7Q2 · created card …") is the one thing on screen
// that says a card just changed, and until #XC75 it was the one thing about a card that could
// not be clicked. The toast carries its card's id now: while one is up the label takes the
// mouse (every other toast stays transparent to it), and a click opens the card exactly the
// way a `#K7Q2` link in the output does — context first, then onOpenCard — and dismisses the
// toast: the click is the acknowledgement.
void aBoardToastClickOpensItsCard()
{
    StubContext context;
    context.workspace = home->path();
    context.swallow = false;
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.deliverWorkerEvent({{"event", "ready"}});
    QStringList opened;
    console.onOpenCard = [&opened](const QString &id) { opened << QStringLiteral("card ") + id; };

    // A board write, as the worker's Switchboard events carry it (protocol 17.5).
    console.deliverWorkerEvent({{"event", "board_activity"},
                                {"id", "K7Q2"},
                                {"summary", "created card \"Clickable toast\""}});
    QLabel *toast = console.findChild<QLabel *>(QStringLiteral("toast"));
    CHECK(toast);
    CHECK(toast->text().contains(QStringLiteral("#K7Q2")));
    CHECK(!toast->testAttribute(Qt::WA_TransparentForMouseEvents));   // the toast is a link
    CHECK(toast->cursor().shape() == Qt::PointingHandCursor);
    CHECK(opened.isEmpty());

    // A press and a release that land on the label open the card, and the click stands in for
    // the timer: the toast is down and out of the terminal's way again.
    const QPointF centre = QRectF(toast->rect()).center();
    QMouseEvent press(QEvent::MouseButtonPress, centre, toast->mapToGlobal(centre.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, centre, toast->mapToGlobal(centre.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(toast, &press);
    QApplication::sendEvent(toast, &release);
    CHECK_EQ(opened, QStringList({QStringLiteral("card K7Q2")}));
    CHECK(toast->isHidden());
    CHECK(toast->testAttribute(Qt::WA_TransparentForMouseEvents));

    // An ordinary toast never takes the mouse, so a click there falls through to the terminal.
    console.toast(QStringLiteral("Copied 12 characters"));
    CHECK(toast->testAttribute(Qt::WA_TransparentForMouseEvents));
    QApplication::sendEvent(toast, &press);
    QApplication::sendEvent(toast, &release);
    CHECK_EQ(opened.size(), 1);   // no second open from a toast that is not a link
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
    // Card #H2KQ: the line shares a row with its Take over / Take control button now; the row
    // is the pane's direct child, still above the composer and outside its frame.
    auto *busyRow = lineWidget->parentWidget();
    CHECK(busyRow != nullptr && busyRow->parentWidget() == &console);
    CHECK(!composer->isAncestorOf(lineWidget));
    CHECK(console.layout()->indexOf(busyRow) < console.layout()->indexOf(composer));
    // Owner, 2026-09-25: the mode chip sits at the right of that row, not in the prompt box's
    // corner, so the prompt text has the box's whole width.
    QToolButton *modeChip = nullptr;
    for (auto *button : console.findChildren<QToolButton *>(QStringLiteral("stripChip")))
        if (button->menu() && button->toolTip().startsWith(QStringLiteral("Where this line goes"))) modeChip = button;
    CHECK(modeChip != nullptr);
    if (modeChip) {
        CHECK(modeChip->parentWidget() == busyRow);
        CHECK(!composer->isAncestorOf(modeChip));
    }

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
    CHECK(terminalLine->parentWidget() != nullptr && terminalLine->parentWidget()->parentWidget() == &terminal);
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
    // CHECK_EQ does not return, and `sent.at(0)` on the empty list a failed precondition leaves
    // is undefined behaviour — under the redirected test-harness environment this read has been
    // segfaulting the whole suite instead of reporting the failure (signal ctest:consolemode).
    if (sent.size() != 1) return;
    CHECK_EQ(sent.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("queue_move"));
    CHECK_EQ(sent.at(0).value(QStringLiteral("item")).toString(), QStringLiteral("q1"));
    CHECK_EQ(sent.at(0).value(QStringLiteral("to")).toInt(), 1);
    CHECK_EQ(sent.at(0).value(QStringLiteral("surface")).toString(), QStringLiteral("card:K7Q2"));

    // Shift+Delete, or the × on the row: a card's prompts wait in that card's supervisor and not
    // in the tab's, so the op says which.
    sent.clear();
    CHECK(console.removeRow(QStringLiteral("item:q2")));
    CHECK_EQ(sent.size(), 1);
    if (sent.size() != 1) return;   // same precondition, same UB a row short of here
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
    if (sent.size() != 1) return;   // a failure, not a crash that hides every case after it
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

void effortMenuFollowsTheActiveRoleModel()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    const QJsonArray presets{
        QJsonObject{{"id", "glm-coding"}, {"label", "z.ai"}, {"model", "glm-5.3"},
                    {"has_stored_key", true}, {"efforts", QJsonArray{"low", "high", "max"}},
                    {"models", QJsonArray{QJsonObject{{"id", "glm-5.3"},
                                                      {"efforts", QJsonArray{"low", "high", "max"}}}}}},
        QJsonObject{{"id", "guest:codex"}, {"label", "Codex"}, {"harness", true},
                    {"efforts", QJsonArray{"low", "medium", "high", "xhigh", "max", "ultra"}},
                    {"models", QJsonArray{QJsonObject{{"id", "gpt-6-astra"},
                                                      {"efforts", QJsonArray{"low", "medium", "high", "xhigh", "max", "ultra"}}},
                                            QJsonObject{{"id", "gpt-6-sol"},
                                                        {"efforts", QJsonArray{"low", "medium", "high", "xhigh", "max", "ultra"}}}}}}
    };
    console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "glm-5.3"}});
    console.deliverWorkerEvent(QJsonObject{{"event", "presets"}, {"presets", presets}});
    console.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"preset", "glm-coding"},
                                           {"model", "glm-5.3"}, {"effort", "max"}});
    QComboBox *effort = nullptr;
    for (auto *box : console.findChildren<QComboBox *>())
        if (box->accessibleName() == QStringLiteral("Reasoning effort")) effort = box;
    CHECK(effort != nullptr);
    if (!effort) return;
    auto levels = [&] {
        QStringList result;
        for (int i = 0; i < effort->count(); ++i) result << effort->itemData(i).toString();
        return result;
    };
    CHECK_EQ(levels(), (QStringList{"low", "high", "max"}));
    console.setAgentRole(QStringLiteral("high"), false,
                         {QStringLiteral("guest:codex|gpt-6-astra"), QStringLiteral("max")});
    console.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"preset", "guest:codex"},
                                           {"model", "gpt-6-astra"}, {"agent_role", "high"}, {"effort", "max"}});
    CHECK_EQ(levels(), (QStringList{"low", "medium", "high", "xhigh", "max", "ultra"}));
    console.setAgentRole(QStringLiteral("high"), false,
                         {QStringLiteral("guest:codex|gpt-6-sol"), QStringLiteral("max")});
    console.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"preset", "guest:codex"},
                                           {"model", "gpt-6-sol"}, {"agent_role", "high"}, {"effort", "max"}});
    CHECK_EQ(levels(), (QStringList{"low", "medium", "high", "xhigh", "max", "ultra"}));
    console.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"preset", "glm-coding"},
                                           {"model", "glm-5.3"}, {"agent_role", "main"}, {"effort", "max"}});
    CHECK_EQ(levels(), (QStringList{"low", "high", "max"}));
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
    // Steering synchronizes terminal sharing before it sends the queued prompt.
    CHECK_EQ(sent.size(), 2);
    if (sent.size() != 2) return;
    CHECK_EQ(sent.at(0).value("type").toString(), QStringLiteral("terminal_context_update"));
    CHECK_EQ(sent.at(1).value("type").toString(), QStringLiteral("ask"));
    CHECK_EQ(sent.at(1).value("when").toString(), QStringLiteral("steer"));
    CHECK_EQ(sent.at(1).value("text").toString(), QStringLiteral("first queued prompt"));
    const QString steerId = sent.at(1).value("id").toString();
    CHECK_EQ(console.queuedPrompts(), 1);
    sent.clear();
    enter();
    CHECK_EQ(sent.size(), 1);
    if (sent.size() != 1) return;
    CHECK_EQ(sent.first().value("type").toString(), QStringLiteral("queue_unsteer"));
    CHECK_EQ(sent.first().value("request").toString(), steerId);
    CHECK_EQ(console.queuedPrompts(), 1);
}

// A picture named with `@` is an attachment on its way to the agent — one of the four ways in
// (src/Images.h: paste, drop, `@path`, screenshot) — so a draft holding one `@` image token and
// nothing else submits like any other prompt. The `@path`-on-its-own quick-open rule used to eat
// exactly that draft, which is what an image pasted into an empty box leaves behind, and open a
// preview pane instead of sending it: the attachment line under the box promises the agent.
void anImageDraftSubmitsToTheAgentRatherThanOpening()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QStringList opened;
    console.onOpenPath = [&opened](const QString &path, int) { opened << path; };
    console.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
    auto *editor = console.findChild<RichEditor *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (!editor) return;

    // A real PNG on disk, and the draft a paste of it into the empty box leaves behind.
    const QString image = relay::images::savePng(QImage(4, 4, QImage::Format_RGB32),
                                                 home->filePath(QStringLiteral("relay-shot.png")));
    CHECK(relay::images::isImageFile(image));
    if (image.isEmpty()) return;
    console.draftInComposer(QStringLiteral("@") + image);
    editor->onSubmit(QStringLiteral("auto"));
    CHECK(opened.isEmpty());                 // never opened in a pane
    CHECK_EQ(console.queuedPrompts(), 1);    // it queued for the agent instead
    QStringList queued;
    for (const Pane::QueueRow &row : console.queueRows())
        if (row.id.startsWith(QStringLiteral("entry:"))) queued.append(row.preview);
    CHECK_EQ(queued.size(), 1);
    if (queued.size() == 1)
        CHECK(queued.first().contains(image));   // the image rides the queued prompt as its token

    // The quick-open itself still works for anything that is not a picture.
    const QString notes = home->filePath(QStringLiteral("notes.txt"));
    QFile file(notes);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("hello");
    file.close();
    console.draftInComposer(QStringLiteral("@") + notes);
    editor->onSubmit(QStringLiteral("auto"));
    CHECK(opened == QStringList{notes});
}

void pendingQueueSurvivesPaneRestorePaused()
{
    StubContext context;
    context.workspace = home->path();
    Pane original(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    original.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
    original.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
    auto *editor = original.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (!editor) return;
    for (const QString &text : {QStringLiteral("first queued prompt"), QStringLiteral("second queued prompt")}) {
        original.draftInComposer(text);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &enter);
    }
    const QJsonArray saved = original.queueForRestore();
    CHECK_EQ(saved.size(), 2);

    Pane restored(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    restored.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    restored.initRestore(QJsonObject{{"queue", saved}});
    sent.clear();
    restored.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "test"}});
    CHECK_EQ(restored.queuedPrompts(), 2);
    CHECK(restored.queuePaused());
    CHECK_EQ(restored.queueForRestore(), saved);
    QStringList queued;
    for (const Pane::QueueRow &row : restored.queueRows())
        if (row.id.startsWith(QStringLiteral("entry:"))) queued.append(row.preview);
    CHECK_EQ(queued, (QStringList{QStringLiteral("first queued prompt"), QStringLiteral("second queued prompt")}));
    CHECK(std::none_of(sent.cbegin(), sent.cend(), [](const QJsonObject &message) {
        return message.value(QStringLiteral("type")).toString() == QStringLiteral("ask");
    })); // restoring and configuring must not start a saved prompt
    restored.resumeAgentQueue();
    CHECK(std::any_of(sent.cbegin(), sent.cend(), [](const QJsonObject &message) {
        return message.value(QStringLiteral("type")).toString() == QStringLiteral("ask")
               && message.value(QStringLiteral("text")).toString() == QStringLiteral("first queued prompt");
    }));
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
            QJsonObject{{"header", "Note"}, {"question", "Anything to add?"}},
            QJsonObject{{"header", "Detail"}, {"question", "Any details?"}}}}});
        sent.clear();
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
        // Card #XCXD: a blank Enter skips the current question (Esc no longer does) and does
        // nothing else — no steer, no resume, and nothing sent until the last question is done.
        enter();
        CHECK(sent.isEmpty());
        CHECK(console.questionOpen());
        CHECK_EQ(console.queuedPrompts(), 2);
        answer(QStringLiteral("please keep the tests"));
        CHECK(!console.questionOpen());
        CHECK_EQ(sent.size(), 1);
        if (sent.size() == 1) {
            CHECK_EQ(sent.first().value("type").toString(), QStringLiteral("question_answer"));
            CHECK_EQ(sent.first().value("id").toString(), QStringLiteral("question-1"));
            CHECK_EQ(sent.first().value("answers").toArray(),
                     (QJsonArray{QJsonArray{"Second"}, QJsonArray{}, QJsonArray{"please keep the tests"}}));
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


// ----- a memory suggestion waits in the transcript (#MEMS) --------------------------------------
//
// Owner, 2026-09-22: "new memories are suggestions that the user confirms. and if the user rejects,
// rejections are remembered". A finished `app_user_memory suggest` prints "Remember: … Keep · Edit ·
// No" under its call; Keep and No go to this pane's worker, the answer prints the outcome, and the
// same links clicked again send nothing. A declined repeat is a note with no links.
void aMemorySuggestionIsKeptEditedOrRejectedFromTheTranscript()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.resize(1000, 650);
    console.show();
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    QStringList edited;
    console.onOpenMemorySuggestion = [&edited](const QString &id) { edited << id; };
    int decided = 0;
    console.onMemorySuggestionDecided = [&decided] { ++decided; };
    console.deliverWorkerEvent({{"event", "configured"}, {"model", "test"}});
    console.deliverWorkerEvent({{"event", "agent_started"}, {"id", "t1"}});
    auto suggest = [&](const QString &call, const QJsonObject &result) {
        // The labels backend/relay_core/tool_labels.py gives this call.
        const QJsonObject label{{"kind", "other"}, {"running", "running app user memory"}, {"title", "app user memory suggest"}};
        QJsonObject done = label;
        done.insert("ok", true);
        console.deliverWorkerEvent({{"event", "tool_started"}, {"tool", "app_user_memory"}, {"call_id", call}, {"turn_id", "t1"},
                                    {"label", label}});
        console.deliverWorkerEvent({{"event", "tool_result"}, {"tool", "app_user_memory"}, {"call_id", call}, {"turn_id", "t1"},
                                    {"result", result}, {"label", done}, {"ms", 12}});
    };
    suggest(QStringLiteral("c1"), {{"status", "pending"}, {"id", "S1"}, {"fact", "Prefers terse answers"}, {"source", "agent"}});
    suggest(QStringLiteral("c2"), {{"status", "pending"}, {"id", "S2"}, {"fact", "Works in Zurich"}, {"source", "agent"}});
    suggest(QStringLiteral("c3"), {{"status", "declined"}, {"id", QJsonValue()}, {"fact", "Likes tabs"},
                                   {"matched", QJsonObject{{"kind", "rejected"}, {"date", "2026-09-21"}}}});
    { QEventLoop wait; QTimer::singleShot(100, &wait, &QEventLoop::quit); wait.exec(); }
    QString text = console.paneTextLines(2000).join('\n');
    if (qEnvironmentVariableIsSet("RELAY_MEMORY_DUMP")) std::fprintf(stderr, "%s\n", qPrintable(text));
    CHECK(text.contains(QStringLiteral("Remember: Prefers terse answers   Keep · Edit · No")));
    CHECK(text.contains(QStringLiteral("Not suggested: Likes tabs · you rejected it on 2026-09-21")));
    auto *backend = console.findChild<relay::EngineBackend *>();
    CHECK(backend != nullptr);
    if (backend) {
        const QString links = backend->replayableText(2000).join('\n');
        CHECK(links.contains(QStringLiteral("relay://memory/%1/keep/S1").arg(console.sessionToken())));
        CHECK(links.contains(QStringLiteral("relay://memory/%1/no/S2").arg(console.sessionToken())));
    }
    const QString capture = qEnvironmentVariable("RELAY_MEMORY_LINE_CAPTURE");
    if (!capture.isEmpty()) CHECK(console.grab().save(capture));
    auto target = [&](const QString &word, const QString &id) {
        return QStringLiteral("relay://memory/%1/%2/%3").arg(console.sessionToken(), word, id);
    };
    sent.clear();
    console.openOutputTarget(target(QStringLiteral("edit"), QStringLiteral("S1")), 0, true);
    CHECK_EQ(edited, QStringList({QStringLiteral("S1")}));
    CHECK(sent.isEmpty());
    console.openOutputTarget(target(QStringLiteral("keep"), QStringLiteral("S1")), 0, true);
    CHECK_EQ(sent.size(), 1);
    if (sent.size() != 1) return;
    CHECK_EQ(sent.first().value("type").toString(), QStringLiteral("globals_suggestion_accept"));
    CHECK_EQ(sent.first().value("sid").toString(), QStringLiteral("S1"));
    console.openOutputTarget(target(QStringLiteral("no"), QStringLiteral("S1")), 0, true);
    CHECK_EQ(sent.size(), 1);   // one decision in flight at a time
    console.deliverWorkerEvent({{"event", "globals_suggestion_accepted"}, {"id", sent.first().value("id")}, {"sid", "S1"},
                                {"record", QJsonObject{{"kind", "memory"}, {"key", "M1"}}}});
    CHECK_EQ(decided, 1);
    sent.clear();
    console.openOutputTarget(target(QStringLiteral("no"), QStringLiteral("S2")), 0, true);
    CHECK_EQ(sent.size(), 1);
    if (sent.size() != 1) return;
    CHECK_EQ(sent.first().value("type").toString(), QStringLiteral("globals_suggestion_reject"));
    console.deliverWorkerEvent({{"event", "globals_suggestion_rejected"}, {"id", sent.first().value("id")}, {"sid", "S2"}});
    { QEventLoop wait; QTimer::singleShot(100, &wait, &QEventLoop::quit); wait.exec(); }
    text = console.paneTextLines(2000).join('\n');
    CHECK(text.contains(QStringLiteral("Kept: Prefers terse answers · Globals › User memory")));
    CHECK(text.contains(QStringLiteral("Rejected — won't be suggested again: Works in Zurich")));
    // Decided: the old links answer from memory and send nothing.
    sent.clear();
    console.openOutputTarget(target(QStringLiteral("keep"), QStringLiteral("S1")), 0, true);
    console.openOutputTarget(target(QStringLiteral("keep"), QStringLiteral("S2")), 0, true);
    console.openOutputTarget(target(QStringLiteral("edit"), QStringLiteral("S2")), 0, true);
    CHECK(sent.isEmpty());
    CHECK_EQ(edited.size(), 1);
    // A guest's suggestion arrives as `memory_suggested` from the Relay bridge, and draws the same line.
    console.deliverWorkerEvent({{"event", "memory_suggested"}, {"result", QJsonObject{
        {"status", "pending"}, {"id", "S3"}, {"fact", "Uses zsh on every machine"}, {"source", "agent"}}}});
    { QEventLoop wait; QTimer::singleShot(100, &wait, &QEventLoop::quit); wait.exec(); }
    CHECK(console.paneTextLines(2000).join('\n').contains(QStringLiteral("Remember: Uses zsh on every machine   Keep · Edit · No")));
    console.openOutputTarget(target(QStringLiteral("keep"), QStringLiteral("S3")), 0, true);
    CHECK_EQ(sent.size(), 1);
    if (!sent.isEmpty()) CHECK_EQ(sent.first().value("sid").toString(), QStringLiteral("S3"));
    const QString after = qEnvironmentVariable("RELAY_MEMORY_OUTCOME_CAPTURE");
    if (!after.isEmpty()) CHECK(console.grab().save(after));
}
void openingActivityReplaysCompletedTurnsWithoutReprintingThem()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.deliverWorkerEvent({{"event", "configured"}, {"model", "test"}});
    console.deliverWorkerEvent({{"event", "agent_started"}, {"id", "t1"}});
    console.deliverWorkerEvent({{"event", "thinking_delta"}, {"turn_id", "t1"}, {"text", "earlier reasoning"}});
    console.deliverWorkerEvent({{"event", "thinking_done"}, {"turn_id", "t1"}, {"chars", 17}, {"elapsed_ms", 1000}});
    const QJsonObject label{{"kind", "run"}, {"running", "running pytest"}, {"title", "ran pytest"}};
    console.deliverWorkerEvent({{"event", "turn_summary"}, {"turn_id", "t1"}, {"elapsed_ms", 1000},
                                {"tools", QJsonArray{QJsonObject{{"call_id", "c1"}, {"name", "run_command"},
                                                                  {"ok", true}, {"label", label}}}}});
    const QString before = console.paneTextLines(2000).join('\n');
    {
        relay::AgentInternalsView activity;
        console.attachInternals(&activity);
        CHECK_EQ(activity.turnCount(), 1);
        CHECK_EQ(activity.toolRowCount(), 1);
        CHECK(activity.plainText().contains(QStringLiteral("earlier reasoning")));
        CHECK(activity.plainText().contains(QStringLiteral("ran pytest")));
        const QString capture = qEnvironmentVariable("RELAY_ACTIVITY_HISTORY_CAPTURE");
        if (!capture.isEmpty()) {
            activity.resize(900, 600);
            activity.show();
            QCoreApplication::processEvents();
            CHECK(activity.grab().save(capture));
        }
        console.detachInternals(&activity);
    }
    CHECK_EQ(console.paneTextLines(2000).join('\n'), before);
    relay::AgentInternalsView reopened;
    console.attachInternals(&reopened);
    CHECK_EQ(reopened.turnCount(), 1);
    CHECK_EQ(reopened.toolRowCount(), 1);
    console.detachInternals(&reopened);
}
// #TJBC: reasoning is machinery, not a message. The thinking fold sits with the tool rows in one
// single-spaced block, and exactly one blank line separates that block from the agent's prose on
// either side.
void thinkingRowsSitWithToolRowsAndApartFromProse()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.deliverWorkerEvent({{"event", "configured"}, {"model", "test"}});
    console.deliverWorkerEvent({{"event", "agent_started"}, {"id", "t1"}});
    console.deliverWorkerEvent({{"event", "thinking_delta"}, {"turn_id", "t1"}, {"text", "considering the request"}});
    console.deliverWorkerEvent({{"event", "thinking_done"}, {"turn_id", "t1"}, {"chars", 22}, {"elapsed_ms", 1000}});
    const QJsonObject pytest{{"kind", "run"}, {"running", "running pytest"}, {"title", "ran pytest"}};
    console.deliverWorkerEvent({{"event", "tool_started"}, {"call_id", "c1"}, {"turn_id", "t1"}, {"tool", "run_command"}, {"label", pytest}});
    console.deliverWorkerEvent({{"event", "tool_result"}, {"call_id", "c1"}, {"turn_id", "t1"}, {"label", pytest},
                                {"result", QJsonObject{{"exit_code", 0}}}});
    console.deliverWorkerEvent({{"event", "delta"}, {"text", "The tests pass.\n"}});
    const QJsonObject ctest{{"kind", "run"}, {"running", "running ctest"}, {"title", "ran ctest"}};
    console.deliverWorkerEvent({{"event", "tool_started"}, {"call_id", "c2"}, {"turn_id", "t1"}, {"tool", "run_command"}, {"label", ctest}});
    console.deliverWorkerEvent({{"event", "tool_result"}, {"call_id", "c2"}, {"turn_id", "t1"}, {"label", ctest},
                                {"result", QJsonObject{{"exit_code", 0}}}});
    console.deliverWorkerEvent({{"event", "turn_summary"}, {"turn_id", "t1"}, {"elapsed_ms", 2000},
                                {"tools", QJsonArray{QJsonObject{{"call_id", "c1"}, {"name", "run_command"},
                                                                  {"ok", true}, {"label", pytest}},
                                                     QJsonObject{{"call_id", "c2"}, {"name", "run_command"},
                                                                  {"ok", true}, {"label", ctest}}}}});
    const QString text = console.paneTextLines(2000).join('\n');
    if (qEnvironmentVariableIsSet("RELAY_SPACING_DUMP")) std::fprintf(stderr, "%s\n", qPrintable(text));
    const QString capture = qEnvironmentVariable("RELAY_SPACING_CAPTURE");
    if (!capture.isEmpty()) {
        console.resize(1000, 620);
        console.show();
        QCoreApplication::processEvents();
        CHECK(console.grab().save(capture));
    }
    // The fold row and the tool row are one single-spaced block, in both orders.
    CHECK(text.contains(QStringLiteral("✦ thought for 1 s\n▸ ran pytest")));
    CHECK(!text.contains(QStringLiteral("✦ thought for 1 s\n\n")));
    // Prose is set off by exactly one blank line on both sides.
    CHECK(text.contains(QStringLiteral("▸ ran pytest\n\nThe tests pass.")));
    CHECK(text.contains(QStringLiteral("The tests pass.\n\n▸ ran ctest")));
    // The turn's link row stays with the last tool row (unchanged #5AWD behaviour).
    CHECK(text.contains(QStringLiteral("▸ ran ctest\n✦ 2 tool calls · 2 s")));
}

// #265N: quitting must let the remote sidecar finish its own teardown. RemoteShare::shutdown()
// sends `stop`, closes the write channel (the stdin EOF remote/gui_host.py's read_forever treats
// as "the GUI is gone") and waits inside the owner's 2 s cap before killing — so a quit with the
// remote side on is graceful, and a quit with it off stays instant. The sidecar here is a stub
// with exactly that contract: it records every line it was sent and exits on EOF.
void remoteShutdownIsGraceful()
{
    QTemporaryDir scratch;
    QDir(scratch.path()).mkpath(QStringLiteral("remote"));
    const QString logPath = scratch.path() + QStringLiteral("/sidecar.log");
    QFile stub(scratch.path() + QStringLiteral("/remote/gui_host.py"));
    CHECK(stub.open(QIODevice::WriteOnly | QIODevice::Text));
    stub.write("import os, sys\n"
               "log = open(os.environ[\"RELAY_FAKE_SIDECAR_LOG\"], \"w\", buffering=1)\n"
               "log.write(\"up\\n\")\n"
               "for line in sys.stdin:\n"
               "    log.write(\"recv \" + line)\n"
               "log.write(\"eof\\n\")\n");
    stub.close();
    qputenv("RELAY_REMOTE_DIR", scratch.path().toUtf8());
    qputenv("RELAY_FAKE_SIDECAR_LOG", logPath.toUtf8());
    relay::remotesettings::setAlwaysOn(true);

    relay::RemoteShare::instance().startAtLaunch();
    // Wait for the stub to be up and holding the launch's `start` line, like a real sidecar
    // before its first share (ensureSidecar allows it 5 s to start; this poll is not that clock).
    // QProcess::write only reaches the pipe when the event loop pumps, and this filter never
    // calls exec — so each round pumps briefly before re-reading.
    const qint64 launch = QDateTime::currentMSecsSinceEpoch();
    bool up = false;
    while (QDateTime::currentMSecsSinceEpoch() - launch < 10000 && !up) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QFile log(logPath);
        if (log.open(QIODevice::ReadOnly)) {
            const QByteArray sent = log.readAll();
            up = sent.contains("up\n") && sent.contains("\"t\":\"start\"");
        }
        if (!up) QThread::msleep(25);
    }
    CHECK(up);

    const qint64 began = QDateTime::currentMSecsSinceEpoch();
    relay::RemoteShare::instance().shutdown();
    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - began;

    QFile log(logPath);
    CHECK(log.open(QIODevice::ReadOnly));
    const QString transcript = QString::fromUtf8(log.readAll());
    const int start = transcript.indexOf(QLatin1String("\"t\":\"start\""));
    const int stop = transcript.indexOf(QLatin1String("\"t\":\"stop\""));
    const int eof = transcript.indexOf(QLatin1String("eof"));
    // The order the protocol promises: started, told to stop, then the stdin EOF of the closed
    // write channel — the graceful exit, not a kill (a SIGKILLed child never writes "eof").
    CHECK(start >= 0);
    CHECK(stop >= 0);
    CHECK(eof >= 0);
    CHECK(start < stop);
    CHECK(stop < eof);
    CHECK(elapsedMs < 2000);   // inside the 2 s cap the owner picked on #265N
    relay::remotesettings::setAlwaysOn(false);
}

// ----- the card drawer (#6BY7) -------------------------------------------------------------------
//
// The claims chip no longer jumps to the Board: it toggles a read-only drawer of the card this
// pane is working, docked under the header. It is fed by the tab's board helper —
// `board_card_get` out through onBoardRequest, the answer back through handleBoardHelperEvent —
// and its one action is the Board's own Done, sent as the Board sends it, refusals rendered
// inline. The pane's conversation never writes the card's thread (#CTRN).

static void setupDrawerPane(Pane &console, StubContext &context, QStringList &opened,
                            QList<QJsonObject> &boardRequests, QList<QJsonObject> &sent)
{
    console.onOpenCard = [&opened](const QString &id) { opened << id; };
    console.onBoardRequest = [&boardRequests](const QJsonObject &request) { boardRequests << request; };
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent({{"event", "ready"}});
    console.deliverWorkerEvent({{"event", "configured"}, {"model", "test"}});
    console.deliverWorkerEvent(QJsonObject{{"event", "board"}, {"cards_total", 1},
        {"cards", QJsonArray{QJsonObject{{"id", "6BY7"},
                                         {"title", "Card drawer in a terminal pane"},
                                         {"status", "executing"}, {"type", "work"}}}}});
    // The prompt mentions the card, so its queue entry carries it (protocol 17.6) and the chip
    // names it — the same path a live pane's chip lights on.
    console.draftInComposer(QStringLiteral("Run #6BY7 from this pane"));
    console.interruptAgentWithPrompt();
    Q_UNUSED(context);
}

static QJsonObject cardAnswer(const QString &status)
{
    const QString body = QStringLiteral(
        "# Card drawer in a terminal pane\n\n## Issue\nShow the card inline.\n\n"
        "## Plan\nWrite the drawer.\n\n## Tasks\n- [x] CardDrawer widget\n- [ ] Ship it\n");
    return QJsonObject{{"event", "board_card"}, {"id", "drawer-card-6BY7"},
                       {"card_id", "6BY7"}, {"status", status},
                       {"title", "Card drawer in a terminal pane"},
                       {"body", body}, {"body_truncated", false}};
}

void theChipTogglesTheCardDrawer()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QStringList opened;
    QList<QJsonObject> boardRequests, sent;
    setupDrawerPane(console, context, opened, boardRequests, sent);
    auto *chip = console.findChild<QToolButton *>(QStringLiteral("paneCardChip"));
    CHECK(chip != nullptr);
    if (!chip) return;
    CHECK(!chip->isHidden());
    CHECK(opened.isEmpty());

    // One click opens the drawer and asks the helper for the card — the Board is not opened.
    chip->click();
    auto *drawer = console.findChild<QFrame *>(QStringLiteral("cardDrawer"));
    CHECK(drawer != nullptr);
    if (!drawer) return;
    CHECK(!drawer->isHidden());
    CHECK(opened.isEmpty());
    CHECK_EQ(boardRequests.size(), 1);
    if (boardRequests.size() == 1) {
        CHECK_EQ(boardRequests.first().value("kind").toString(), QStringLiteral("board_card_get"));
        CHECK_EQ(boardRequests.first().value("id").toString(), QStringLiteral("drawer-card-6BY7"));
        CHECK_EQ(boardRequests.first().value("card").toString(), QStringLiteral("6BY7"));
    }

    // The drawer's Board button keeps the chip's old behaviour exactly.
    auto *openBoard = console.findChild<QPushButton *>(QStringLiteral("cardDrawerOpen"));
    CHECK(openBoard != nullptr);
    if (openBoard) openBoard->click();
    CHECK_EQ(opened, QStringList{QStringLiteral("6BY7")});

    // A second click hides; a third reopens (and asks again, the card may have moved on).
    chip->click();
    CHECK(drawer->isHidden());
    boardRequests.clear();
    chip->click();
    CHECK(!drawer->isHidden());
    CHECK_EQ(boardRequests.size(), 1);

    // The ✕ hides it too, and never touches the Board.
    auto *close = console.findChild<QToolButton *>(QStringLiteral("cardDrawerClose"));
    CHECK(close != nullptr);
    if (close) close->click();
    CHECK(drawer->isHidden());
    CHECK_EQ(opened, QStringList{QStringLiteral("6BY7")});
    }

void theCardDrawerRendersTheHelperAnswerAndRefreshesOnChange()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QStringList opened;
    QList<QJsonObject> boardRequests, sent;
    setupDrawerPane(console, context, opened, boardRequests, sent);
    auto *chip = console.findChild<QToolButton *>(QStringLiteral("paneCardChip"));
    CHECK(chip != nullptr);
    if (!chip) { console.deleteLater(); return; }
    chip->click();
    auto *drawer = console.findChild<QFrame *>(QStringLiteral("cardDrawer"));
    CHECK(drawer != nullptr);
    if (!drawer) return;

    console.handleBoardHelperEvent(cardAnswer(QStringLiteral("executing")));
    auto *stage = drawer->findChild<QLabel *>(QStringLiteral("cardDrawerStage"));
    auto *title = drawer->findChild<QLabel *>(QStringLiteral("cardDrawerTitle"));
    auto *browser = drawer->findChild<QTextBrowser *>(QStringLiteral("cardDrawerBody"));
    CHECK(stage && title && browser);
    if (stage) CHECK_EQ(stage->text(), relay::board::statusTitle(QStringLiteral("executing")));
    if (title) CHECK_EQ(title->text(), QStringLiteral("Card drawer in a terminal pane"));
    if (browser) {
        CHECK(browser->isReadOnly());
        CHECK(browser->openExternalLinks() == false);
        const QString shown = browser->toPlainText();
        CHECK(shown.contains(QStringLiteral("Show the card inline.")));   // the body, title heading off
        CHECK(!shown.contains(QStringLiteral("Card drawer in a terminal pane")));
        CHECK(shown.contains(QStringLiteral("Plan")));
        CHECK(shown.contains(QStringLiteral("Tasks")));
        CHECK(shown.contains(QStringLiteral("✓ CardDrawer widget")));   // - [x] rendered as a tick
        CHECK(shown.contains(QStringLiteral("☐ Ship it")));
    }
    // No editable surface exists in the drawer.
    for (auto *edit : drawer->findChildren<QTextEdit *>()) CHECK(edit->isReadOnly());
    CHECK(drawer->findChildren<QLineEdit *>().isEmpty());

    // Another surface's answer (its request id, our card) passes through untouched.
    boardRequests.clear();
    console.handleBoardHelperEvent(QJsonObject{{"event", "board_card"}, {"id", "review-card-9"},
        {"card_id", "6BY7"}, {"status", "done"}, {"title", "Somewhere else"},
        {"body", QStringLiteral("# Somewhere else\n")}, {"body_truncated", false}});
    if (title) CHECK_EQ(title->text(), QStringLiteral("Card drawer in a terminal pane"));
    CHECK(boardRequests.isEmpty());

    // A board change naming the card refetches it through the helper, the stream the Board reads.
    console.handleBoardHelperEvent(QJsonObject{{"event", "board_changed"},
        {"upserts", QJsonArray{QJsonObject{{"id", "6BY7"}}}}});
    CHECK_EQ(boardRequests.size(), 1);
    if (boardRequests.size() == 1)
        CHECK_EQ(boardRequests.first().value("id").toString(), QStringLiteral("drawer-card-6BY7"));
    // One that names other cards does not.
    boardRequests.clear();
    console.handleBoardHelperEvent(QJsonObject{{"event", "board_changed"},
        {"upserts", QJsonArray{QJsonObject{{"id", "P2W8"}}}}});
    CHECK(boardRequests.isEmpty());
    }

void theCardDrawerDoneSendsBoardMoveAndShowsRefusals()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QStringList opened;
    QList<QJsonObject> boardRequests, sent;
    setupDrawerPane(console, context, opened, boardRequests, sent);
    auto *chip = console.findChild<QToolButton *>(QStringLiteral("paneCardChip"));
    CHECK(chip != nullptr);
    if (!chip) return;
    chip->click();
    auto *drawer = console.findChild<QFrame *>(QStringLiteral("cardDrawer"));
    CHECK(drawer != nullptr);
    if (!drawer) return;
    console.handleBoardHelperEvent(cardAnswer(QStringLiteral("executing")));

    // Done is the Board's move, over the helper, gated worker-side.
    auto *done = console.findChild<QPushButton *>(QStringLiteral("cardDrawerDone"));
    CHECK(done != nullptr);
    if (done) CHECK(!done->isHidden());
    boardRequests.clear();
    if (done) done->click();
    CHECK_EQ(boardRequests.size(), 1);
    if (boardRequests.size() == 1) {
        const QJsonObject move = boardRequests.first();
        CHECK_EQ(move.value("kind").toString(), QStringLiteral("board_move"));
        CHECK_EQ(move.value("id").toString(), QStringLiteral("drawer-card-6BY7"));
        CHECK_EQ(move.value("card").toString(), QStringLiteral("6BY7"));
        CHECK_EQ(move.value("status").toString(), QStringLiteral("done"));
        CHECK_EQ(move.value("section").toString(), QString());
        CHECK(move.value("reason").toString().contains(QStringLiteral("card drawer")));
    }

    // A gated move answers an error under the drawer's request id; it renders inline.
    auto *notice = console.findChild<QLabel *>(QStringLiteral("cardDrawerNotice"));
    CHECK(notice != nullptr);
    console.handleBoardHelperEvent(QJsonObject{{"event", "error"}, {"id", "drawer-card-6BY7"},
        {"text", QStringLiteral("card #6BY7 has an open Human QA question")}});
    if (notice) {
        CHECK(!notice->isHidden());
        CHECK(notice->text().contains(QStringLiteral("Human QA")));
    }
    // Someone else's error never reaches the drawer's notice line: the refusal still stands.
    console.handleBoardHelperEvent(QJsonObject{{"event", "error"}, {"id", "review-card-9"},
        {"text", QStringLiteral("not this surface's problem")}});
    if (notice) CHECK(notice->text().contains(QStringLiteral("Human QA")));

    // A card already done has no Done to press — there is nothing left to ask of the worker.
    console.handleBoardHelperEvent(cardAnswer(QStringLiteral("done")));
    if (done) CHECK(done->isHidden());
    }

void aPromptOnAWorkedCardWritesNothingToTheBoard()
{
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QStringList opened;
    QList<QJsonObject> boardRequests, sent;
    setupDrawerPane(console, context, opened, boardRequests, sent);
    CHECK(console.findChild<QToolButton *>(QStringLiteral("paneCardChip")) != nullptr);

    // The pane's conversation — user or agent — is never written to the card's thread (#CTRN):
    // every message the pane sent names no board write, and the helper saw no request at all.
    console.draftInComposer(QStringLiteral("A status update from the pane"));
    console.interruptAgentWithPrompt();
    CHECK(boardRequests.isEmpty());
    for (const QJsonObject &message : sent) {
        CHECK(!message.value("kind").toString().startsWith(QStringLiteral("board_")));
        CHECK(!message.value("type").toString().startsWith(QStringLiteral("board_")));
    }
    }
}  // namespace cases

#include "pane_waits.h"
#include "xcxd_queue_cases.h"
#include "xcxd_ui_cases.h"
#include "xcxd_review_cases.h"
#include "xcxd_context_cases.h"
#include "modelqueue_cases.h"
#include "h2kq_cases.h"
#include "234z_cases.h"
#include "recall_prompt_cases.h"
#include "jdn4_queue_expand_cases.h"

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
    QCoreApplication::setApplicationName(QStringLiteral("relay"));
    QApplication app(argc, argv);

    // --repeat N (card #8ABD): re-run an --X-only filter N times and print a summary, so a
    // stability check is one call rather than a shell `for` loop re-running the whole binary.
    int repeat = 1;
    const QStringList cliArgs = app.arguments();
    const int repeatAt = cliArgs.indexOf(QStringLiteral("--repeat"));
    if (repeatAt >= 0) {
        bool ok = false;
        const int parsed =
            repeatAt + 1 < cliArgs.size() ? cliArgs.at(repeatAt + 1).toInt(&ok) : 0;
        bool hasFilter = false;
        for (const QString &arg : cliArgs)
            if (arg.startsWith(QStringLiteral("--")) && arg.endsWith(QStringLiteral("-only")))
                hasFilter = true;
        if (!ok || parsed < 1 || !hasFilter) {
            std::fprintf(stderr,
                         "--repeat N: N must be a positive count and an --X-only filter must be "
                         "given\n");
            return 2;
        }
        repeat = qMin(parsed, 200);
    }

    // Runs one filter's cases. repeat == 1 keeps the single-run output byte-identical; with
    // --repeat N it counts the iterations that added no failures and prints
    // "<label>: P/N passed (<elapsed> ms)", exiting 1 if any iteration failed.
    auto runRepeated = [&repeat](const char *label, const char *passedLine, auto &&body) -> int {
        if (repeat == 1) {
            body();
            if (!failures && passedLine)
                std::fprintf(stdout, "%s\n", passedLine);
            return failures ? 1 : 0;
        }
        int passed = 0;
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < repeat; ++i) {
            const int before = failures;
            QElapsedTimer iter;
            iter.start();
            body();
            if (qEnvironmentVariableIsSet("RELAY_REPEAT_VERBOSE"))
                std::fprintf(stderr, "  iter %d: %lld ms%s\n", i + 1,
                             static_cast<long long>(iter.elapsed()),
                             failures == before ? "" : " (FAILED)");
            if (failures == before)
                ++passed;
        }
        std::fprintf(stdout, "%s: %d/%d passed (%lld ms)\n", label, passed, repeat,
                     static_cast<long long>(timer.elapsed()));
        return passed == repeat ? 0 : 1;
    };

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

    if (app.arguments().contains(QStringLiteral("--remote-shutdown-only"))) {
        // #265N: the quit path's graceful sidecar shutdown, driven by a stub sidecar. No theme:
        // the case touches RemoteShare and remotesettings, nothing that renders.
        cases::remoteShutdownIsGraceful();
        if (!failures) std::fprintf(stdout, "remoteshutdown: all cases passed\n");
        return failures ? 1 : 0;
    }

    if (app.arguments().contains(QStringLiteral("--recall-only"))) {
        return runRepeated("recall", "recall: all cases passed", [&app] {
            relay::theme::applyTheme(app);
            cases::recallPromptCases();
            cases::recallAfterAToolCallCases();
        });
    }

    if (app.arguments().contains(QStringLiteral("--xcxd-only"))) {
        return runRepeated("xcxd", "queuecontract: all cases passed", [&app] {
            relay::theme::applyTheme(app);
            cases::xcxdQueueCases();
            cases::xcxdReviewCases();
            cases::xcxdUiCases();
            cases::xcxdContextCases();
        });
    }
    if (app.arguments().contains(QStringLiteral("--jdn4-only"))) {
        return runRepeated("jdn4", "queueexpand: all cases passed", [&app] {
            relay::theme::applyTheme(app);
            cases::jdn4QueueExpandCases();
        });
    }
    if (app.arguments().contains(QStringLiteral("--model-queue-only"))) {
        return runRepeated("modelqueue", "modelqueue: all cases passed", [&app] {
            relay::theme::applyTheme(app);
            cases::modelQueueCases();
        });
    }
    if (app.arguments().contains(QStringLiteral("--h2kq-only"))) {
        return runRepeated("h2kq", "h2kq: all cases passed", [&app] {
            relay::theme::applyTheme(app);
            cases::h2kqCases();
            cases::h2kqQueueLabelCases();
        });
    }
    if (app.arguments().contains(QStringLiteral("--234z-only"))) {
        return runRepeated("234z", "234z: all cases passed", [&app] {
            relay::theme::applyTheme(app);
            cases::z234zCases();
        });
    }
    if (app.arguments().contains(QStringLiteral("--composer-only"))) {
        // Themed, because the box's face — surface fill, rounded border, copper accent when a
        // relay is active — comes from the theme sheet; the case exists to keep that wiring
        // intact (#6JS0).
        return runRepeated("composer", "composer: all cases passed", [&app] {
            relay::theme::applyTheme(app);
            cases::theComposerKeepsItsThemeName();
        });
    }
    if (app.arguments().contains(QStringLiteral("--memory-only"))) {
        return runRepeated("memory", nullptr, [] {
            cases::aMemorySuggestionIsKeptEditedOrRejectedFromTheTranscript();
        });
    }
    if (app.arguments().contains(QStringLiteral("--plan-click-only"))) {
        return runRepeated("plan-click", nullptr, [] {
            cases::enteringPlanSelectsHigh();
            cases::clickingPlanLeavesModeAndPreservesDraft();
            cases::planWhileConfiguringSelectsHighBeforeMode();
        });
    }
    if (app.arguments().contains(QStringLiteral("--activity-history-only"))) {
        return runRepeated("activity-history", nullptr, [] {
            cases::openingActivityReplaysCompletedTurnsWithoutReprintingThem();
        });
    }

    cases::aContextWithoutAShellStartsNoProgram();
    cases::thinkingRowsSitWithToolRowsAndApartFromProse();
    cases::recallPromptCases();
    cases::recallAfterAToolCallCases();
    cases::theTranscriptSurfaceIsStillThere();
    cases::theRoutingIsLockedToTheAgent();
    cases::aTerminalPaneIsUnchanged();
    cases::aMiddleClickOnTheHeaderClosesThePane();
    cases::relayingStatusSitsOutsideEveryPromptFrame();
    cases::theActionRowIsBuiltFromTheContext();
    cases::aChangedContextRebuildsTheRow();
    cases::queueRowArrowSendsOnlyThatPrompt();
    cases::anActionThatRebuildsItsOwnRowIsSafe();
    cases::aContextMaySwallowASubmit();
    cases::aContextsSlashCommandsJoinThePopup();
    cases::anIdleQueueChangedClearsABusyFlagNothingWillFinish();
    cases::ctrlEnterStartsADeferredGuestOnItsFirstPrompt();
    cases::everyChordReachesTheContextWithItsOwnRoute();
    cases::aClickedActionTeachesItsLetter();
    cases::theComposerSaysWhatTheContextSays();
    cases::theContextBlockAndTheAskFieldsAreTheContextsOwn();
    cases::theHostsHandlesWork();
    cases::aConsoleIsAnOrdinaryChildOfItsHost();
    cases::theContextGetsFirstRefusalOnLocalLinkKinds();
    cases::aBoardToastClickOpensItsCard();
    cases::shiftClickOnALocalPathOpensItExternally();
    cases::ctrlClickEditsTheActualFile();
    cases::aQueueChangedForThisSurfaceDrawsRowsTheConsoleNeverSubmitted();
    cases::aTerminalPaneIgnoresTheWorkersRowsEntirely();
    cases::aWorkerRowIsRemovedAndMovedWithItsSurface();
    cases::aLineThisConsoleSentComesBackWithUpAsAnUnsentDraft();
    cases::enterOnAnEmptyBoxResumesThisConsolesPausedQueue();
    cases::aTerminalPanesOwnQueueResumesOnEnterToo();
    cases::repeatedEnterKeepsTheFirstQueuedPrompt();
    cases::modelQueueCases();
    cases::anImageDraftSubmitsToTheAgentRatherThanOpening();
    cases::pendingQueueSurvivesPaneRestorePaused();
    cases::enteringPlanSelectsHigh();
    cases::effortMenuFollowsTheActiveRoleModel();
    cases::clickingPlanLeavesModeAndPreservesDraft();
    cases::planWhileConfiguringSelectsHighBeforeMode();
    cases::answersBypassQueuedPrompts();
    cases::proseQuestionHoldsTheQueueForItsReply();
    cases::rewindRemovesOnlyTheBranchAndLinksItsCompleteText();
    cases::aMemorySuggestionIsKeptEditedOrRejectedFromTheTranscript();
    cases::h2kqCases();
    cases::h2kqQueueLabelCases();
    cases::z234zCases();
    cases::theChipTogglesTheCardDrawer();
    cases::theCardDrawerRendersTheHelperAnswerAndRefreshesOnChange();
    cases::theCardDrawerDoneSendsBoardMoveAndShowsRefusals();
    cases::aPromptOnAWorkedCardWritesNothingToTheBoard();

    if (failures == 0)
    std::fprintf(stdout, "consolemode: 21 cases, all passed\n");
    return failures == 0 ? 0 : 1;
}
