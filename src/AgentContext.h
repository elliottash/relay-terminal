// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::agent::Context — what an agent is *about*, and nothing else.
//
// The owner drew the seam this file sits on in two sentences (2026-09-20, card #AGNT):
//
//   "an agent interface is the prompt box. it has a set of options and tools that vary according
//    to the setting/task, but in general they are shared systems."
//
//   "agents are specialized for the given pane context, but the general rule/approach is that
//    agents have access to all systems and can work across panes and contexts."
//
// Read together they split the agent in two. **How you talk to an agent is shared** — one
// composer, one queue, one transcript with its thinking bubbles and tool rows, one model box, one
// history, one Esc: that is the **console**, the surface, and a behaviour added to it
// appears everywhere at once. **What the agent is about varies** — the brief in front of the turn,
// the defaults (which role, whether there is a shell, how a line routes), the row of actions that
// need no typing, how a link in the answer resolves, where the conversation is kept, and what
// happens to a finished turn's output. That is a `Context`, and it is all a `Context` is.
//
// What a context is *not*:
//
// - **It is not a tool whitelist. There is no `tools()` method here, and there must never be
//   one.** A per-context allowlist would rebuild exactly the fence the owner's second sentence
//   took down: the Sessions helper could not open a pane until #H6VQ, because "open a pane" had
//   been marked unsafe somewhere else. The backend attaches the same tool set to every agent. The
//   only gates are the two that are already the owner's — the `settable` / `agent_safe` markers on
//   the catalog rows and the Options › Agent toggle "Agents may change options and run actions"
//   (#FEJQ decisions 1-3) — plus real constraints that are not fences: no board means no `board_*`
//   tools because there is nothing to act on, a guest harness cannot run Relay's tools (#GH5T),
//   and a card's Plan turn may only write `## Plan` (protocol 19.20's stage machine).
// - **It is not a widget and it holds no worker.** The surface draws; the window makes the console
//   and attaches it to the tab's worker (card #AGNT step 5). A context that wanted a different
//   transcript surface implements `relay::agent::Host` instead and touches nothing here.
//
// The contexts, as card #AGNT lists them
// (`issues/features/2026-09-20-the-queue-doesnt-work-like-the-main-terminal-and.md`, "The
// contexts, and what each supplies"). A seventh — the project-management page the owner named — is
// one more file implementing this interface, and nothing else:
//
//   name           host                    shell  persist key        action row
//   "terminal"     src/Pane.h              yes    the pane's session  -
//   "switchboard"  src/BoardPane.cpp       no     (workspace, tab)   Check (k), Clean up (u), Tests, Profile
//   "card"         src/BoardPane.cpp       no     (workspace, card)  Plan (p), Execute (x), Verify (v)
//   "options"      src/SettingsPane.cpp    no     (workspace, tab)   -
//   "actions"      src/SettingsPane.cpp    no     (workspace, tab)   -
//   "sessions"     src/Conversations.cpp   no     (workspace, tab)   -
//
// `ContextSpec` is the plain-data half, and it is the **only** part of a context that crosses to
// the worker: the console puts `spec().toJson()` into `configure`'s `context` block and
// `spec().askFields()` into each `ask` (protocol 30.7, and the new section card #AGNT step 4
// writes). The bytes are pinned by tests/agentcontext_test.cpp, so the C++ and the Python are
// tested against one shape rather than against each other.
//
// QtCore only, deliberately: `relay-board`, `relay-settings` and `relay-conversations` are static
// libraries that must be able to link a context without pulling in a window — a `Pane` exists only
// inside the `relay` executable's single translation unit.
#pragma once

#include "OutputLinks.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

// Only ever a pointer here. Declaring it keeps this header QtCore-only, which is what lets
// `relay-board`, `relay-settings` and `relay-conversations` link a context without a window.
class QWidget;

namespace relay::agent {

// The on-screen hint (`screen`) is cut at this many characters before it is sent, as protocol
// 30.7 has always cut the helper's: it is a hint about what is being read, not a context dump,
// and the catalog is never pasted into a prompt — the agent reads the rows live.
constexpr int kScreenLimit = 2000;

// ---------------------------------------------------------------------------------------------
// An action: something the agent can do here that needs no typing.
//
// The rules are the ones #PBX1 settled and the card page already follows (owner, 2026-09-20):
// "they are actions the agent can take that dont require typing… we put the 'clean up' button
// there for the main switchboard agent, for example", the row is "left-aligned, drop the label"
// and it is buttons and nothing else, and "it should have the letter hotkeys for each switchboard
// action as well". So every action carries one free letter, the label wears it as " (k)", and a
// narrow row sheds the keys before it cuts a word (`labelWithoutKey`).
struct Action {
    // The action's own name, stable across rebuilds: it becomes the button's `objectName` — which
    // tests and src/Theme.cpp rules key off — and the id of the shortcut hint the mouse path
    // teaches ("board.action." + key, WARP.md's standing rule). Never shown to the owner.
    QString key;
    // One letter, or empty for a keyless action. The row does not invent one, and it does not
    // answer a letter two actions claim: see `withUniqueLetters`.
    QString letter;
    // What the button says, without its letter: "Check", "Clean up", "Execute". `fullLabel()`
    // adds the " (k)".
    QString label;
    QString tooltip;
    // True for an action that **leaves the surface** the console is on — Execute and Verify hand
    // the card to a terminal pane. Those two wear the accent outline on the card page today, and
    // the flag is what carries that meaning to a row built from this list rather than by hand
    // (owner, 2026-09-20, of the row's shape: "make the buttons consistent, can you use the
    // styling from the card agent" — "(not the colors though)").
    bool leaves = false;
    // False greys the button out and makes its letter do nothing — the key can do no more than
    // the mouse can. The label is unaffected: a busy card still says "Execute (x)".
    bool enabled = true;
    std::function<void()> run;

    // "Check (k)", or just "Check" when the action is keyless. This is what goes in the button's
    // `fullLabel` property, which is what a row that runs out of room shortens *from*.
    QString fullLabel() const;
    bool keyed() const { return !letter.trimmed().isEmpty(); }
};

// The list a row may actually answer letters from. A letter is kept only when it is exactly one
// character and nobody earlier in the row has claimed it (case-insensitively); anything else is
// cleared, which leaves the action on the row, clickable, and simply keyless. Refusing the
// *letter* rather than the action is deliberate: a context that adds a button must never be able
// to make another context's button disappear.
QList<Action> withUniqueLetters(QList<Action> actions);

// The index in `actions` of the enabled action that answers `letter`, or -1. Compared
// case-insensitively, and a disabled action answers nothing — the helper panel's rule for its
// own tool row, which this replaces.
int actionForLetter(const QList<Action> &actions, const QString &letter);

// A label without its key: "Execute (x)" -> "Execute". What a narrow action row shows once the
// keys no longer fit (`CardDetail::fitButtons`); the key itself does not vanish, it is in the
// tooltip and the pane's key legend either way.
QString labelWithoutKey(const QString &label);

// The rungs of the composer's grey text, widest first, built from a context's `placeholder()`.
//
// `RichEditor` takes the first candidate that fits the box, so a two-rung ladder — the line and
// "…" — is all-or-nothing: a line one pixel too wide leaves the box saying nothing at all. That
// is what a card page's box did, and the three chords the owner asked to be said *there* (#VZ69)
// were then said nowhere near it.
//
// Every rung is a **prefix** of the line the context wrote, so no rung can claim something the
// context did not: drop the last comma-separated clause, then the next, then everything from the
// em dash on, and finally "…". A short one-clause line gives one rung and "…", which is what a
// context that wants no ladder gets for free.
QStringList placeholderRungs(const QString &line);

// ---------------------------------------------------------------------------------------------
// The data half: what the worker is told about this context.
//
// `toJson()` is the `context` block of `configure`; `askFields()` is what rides on each `ask`.
// Nothing else about a context reaches the backend, and the worker reads what it knows — the
// catalog is always the GUI's (protocol 30.8).
struct ContextSpec {
    // Which of the contexts above this is: "terminal", "switchboard", "card", "options",
    // "actions", "sessions" — or the name of one written later. It picks nothing by itself; it is
    // what an event, a log line and a `session_info` answer call this agent.
    QString name;
    // This console's own id, unique within the worker that serves it, echoed on every event of a
    // turn it asks so a worker serving several consoles can be told apart. Defaults to `name` in
    // `toJson()`/`askFields()` when a host leaves it empty, which is right for the common case of
    // one console per context per tab.
    QString surface;
    // The provider role the turn runs on (protocol 13.1): "main" for a terminal pane, "switchboard"
    // — labelled "Helper agent" in the UI — for every other context today. It selects a provider
    // and nothing else; it does not change the tool set or the prompt.
    QString agentRole;
    // The project the agent works in. Empty when the setting has none: a tab with no project
    // attached gets a board-less agent, which is a supported state and not an error (30.7).
    QString workspace;
    // The **named** tool scope the worker applies, rather than one it infers. Inference is the
    // accident card #AGNT found: `Agent.tools()` branches on whether a board has a `card_scope`
    // (`backend/relay_core/agent.py:979`), so a board-less helper silently took the *pane* branch
    // and got the full executor. Empty means the worker's own default for the role.
    QString scope;
    // Where the conversation is kept. `persistScope` is the project (30.7's workspace digest) and
    // `persistKey` the conversation's own id — the tab id for the four helper surfaces, the pane's
    // scrollback id for a terminal, the card id for a card. A spec with an empty `persistKey`
    // emits no `persist` block at all, which is how a client says "no store": that agent keeps one
    // conversation for as long as its worker lives and nothing is written down (30.7).
    //
    // The scope is part of the identity, never decoration — a board-less tab is keyed ("", tab)
    // and must not resolve to the same conversation as the same tab inside a project. See
    // `persistId()`.
    QString persistScope;
    QString persistKey;
    // Which brief goes in front of the agent, and what this surface is called. The paragraph
    // itself is the worker's (the console briefs of 30.7 and 33): the GUI sends the key that
    // picks it, and the title the host's own header shows — "Switchboard agent", "Options
    // helper". The brief belongs in the system prompt rather than in front of every prompt,
    // which is also what makes it visible to `session_info` (card #AGNT step 4 item 4).
    QString briefKey;
    QString briefTitle;
    // What is on screen in this console right now — the rows being read, the search in the box.
    // Cut at `kScreenLimit`, and it rides on the `ask`, not on `configure`: it is true for one
    // turn. It is called `screen` on the wire because `ask`'s `context` is already taken by the
    // program-context object (`queue.validate_context`).
    QString screen;
    // A turn that may look but not write. The survey's read-only turn is the case that exists
    // today (the board's import survey); it is a property of the turn, so it goes out with
    // `askFields()`.
    bool readonly = false;
    // The terminal context is the only one that spawns a shell. Everything else keeps the same
    // vterm transcript surface and simply never starts a program — the pty is one call at the end
    // of `Pane::startTerminal` (card #AGNT, Risk 3).
    bool shell = false;
    // "auto" — a typed line may be a command or a prompt, which is the terminal's routing — or
    // "agent", where everything typed is a prompt. Empty means the console's own default.
    QString routing;

    // The `context` block of `configure`. Always carries `name`, `surface`, `agent_role`,
    // `workspace`, `scope`, `shell` and `routing`; `persist` and `brief` appear only when they say
    // something. `screen` and `readonly` are **not** here — they are the turn's, not the
    // context's.
    QJsonObject toJson() const;
    // The inverse, for a test and for anything that reads a spec back off the wire. Unknown keys
    // are ignored and a missing key leaves its field at the default.
    static ContextSpec fromJson(const QJsonObject &json);

    // The fields that ride on `ask`: `surface` always, `screen` when there is one (cut at
    // `kScreenLimit`), `readonly` only when true — so a host that sets neither sends `{}` and the
    // worker behaves exactly as it does today.
    QJsonObject askFields() const;

    // The one string that names this conversation, scope included, for the "did the context move?"
    // test protocol 30.7 describes: a `configure` that moves either the workspace or the key drops
    // the live conversation so the next ask adopts the new one's, and one that moves neither — a
    // model swap, a keybinding reload — leaves it exactly where it was. Empty when there is no
    // store. The separator is a unit separator, which cannot occur in a path or an id, so two
    // specs collide here only when they really are the same conversation.
    QString persistId() const;

    bool operator==(const ContextSpec &other) const;
    bool operator!=(const ContextSpec &other) const { return !(*this == other); }
};

// ---------------------------------------------------------------------------------------------
// What a finished turn hands back to the context.
//
// Most contexts want nothing: the answer is already in the transcript, which is where it belongs.
// A card is the exception the owner kept (card #AGNT decision 2) — the answer is appended to
// `issues/threads/<ID>.md` with its provenance, because the thread is the record a verifier reads
// (POLICY rules 2, 4 and 7). The fields are the ones `_card_answer` writes today
// (`backend/relay_core/board_protocol.py:774-798`).
struct TurnRecord {
    QString id;        // the ask's id, which is also the turn id for a pane turn (`queue.py:445`)
    QString surface;   // the console that asked, `ContextSpec::surface`
    QString prompt;    // the owner's words as they were sent
    QString answer;    // the agent's text, tool fragments already stripped
    QString model;     // the `model=` provenance on the thread entry
    QString sessionId; // with `turnId`, the `turn=<session>/<turn>` provenance
    QString turnId;
    QString mode;      // a card turn's mode: "discuss", "plan", "execute", "verify"; else empty
    QString outcome;   // "done", "cancelled" or "error", as `agent_finished` reports it
    bool readonly = false;

    // "<session>/<turn>", or empty when either half is missing — the shape `_card_answer` passes
    // as `turn=`, so the thread entry reads the same whoever wrote it.
    QString turnRef() const;
};

// ---------------------------------------------------------------------------------------------
// The behaviour half. One implementation per setting; the host owns it and outlives the console
// it is handed to.
class Context {
  public:
    virtual ~Context() = default;

    // What the worker is told. Called fresh — before a `configure` and before every `ask` — so a
    // host may answer with today's `screen`, today's brief title and today's busy state without
    // pushing anything.
    virtual ContextSpec spec() const = 0;

    // The row of things that need no typing, left to right, above the box. Empty is the common
    // case: a terminal pane has no such actions and neither do Options, Actions or Sessions.
    // The console puts the list through `withUniqueLetters` and builds the row from it
    // (`Pane::rebuildActionRow`).
    virtual QList<Action> actions() const { return {}; }

    // A line the owner submitted in this console, offered to the context **before** the pane
    // routes it. True means "handled, send nothing": the card page's Enter travels as
    // `board_ask` rather than as an ordinary `ask`, because a card turn writes the owner's words
    // to `issues/threads/<ID>.md` and advances the stage before the model sees them (19.10, and
    // card #AGNT decision 2). `route` is which **key** was pressed, not where the line would
    // have gone: "auto" for Enter, "agent" for Ctrl+Enter, "shell" for Ctrl+Shift+Enter. A card's
    // three chords are exactly that distinction — Enter discusses, Ctrl+Enter plans,
    // Ctrl+Shift+Enter leaves a note with no model call — and a route resolved against the
    // surface's mode could not tell them apart, because a console's mode is locked to the agent.
    //
    // The composer is the context's while it is handling the line: a context that takes it
    // clears and remembers it, and one that refuses (a card asked while a cleanup runs) leaves
    // the words where the owner typed them. The pane touches neither.
    //
    // It exists so a host does not have to reach into the pane for its composer:
    // `findChild<RichEditor *>()->onSubmit` was what the card page did first, and it takes the
    // box away from everything else that speaks through it (history, the queue, the ask).
    virtual bool submit(const QString & /*route*/, const QString & /*text*/) { return false; }

    // A link the owner activated in the transcript, offered to the context **before** the
    // console's own handling: `option:sec/row` reveals a settings row, `session:<id>` reveals a
    // session, `card:`/`#ID` opens a card. True means "handled, stop"; false lets the console open
    // it the ordinary way (a path in a file pane, a URL in the browser).
    virtual bool resolveLink(const relay::links::Target &) { return false; }

    // A turn that has ended, with everything the context might want to record. Default: nothing —
    // the transcript already has it.
    virtual void turnFinished(const TurnRecord &) {}

    // The grey text in the empty composer. It is the one place the surface says what asking here
    // will do, so it is required rather than defaulted.
    virtual QString placeholder() const = 0;

    // The console sets this when it takes the context; the context calls `changed()` when
    // anything `spec()` or `actions()` would now answer differently has moved — a card going busy
    // and its Execute becoming "Executing (a1b2c3d4)", Options swapping mode, a project being
    // attached to the tab. There is no signal because this library is QtCore-only and a context is
    // not a QObject; a `std::function` is what the rest of these surfaces already use.
    std::function<void()> onChanged;
    void changed()
    {
        if (onChanged)
            onChanged();
    }
};

// ---------------------------------------------------------------------------------------------
// What a host gets back when it asks the window for a console. The pane libraries cannot name
// `Pane` (it lives only in the app's translation unit), so the window hands over the widget to
// embed and the handful of calls a host makes on it. The window owns the pane behind it; the
// handle dies with `widget` (a host must not call through it after the widget is destroyed).
struct ConsoleHandle {
    QWidget *widget = nullptr;
    std::function<void()> focusComposer;
    std::function<void(const QString &)> draftInComposer;
    std::function<QString()> composerText;
    std::function<void(bool)> setCollapsed;
    std::function<bool()> collapsed;
    std::function<bool(const QString &letter)> runActionLetter;
    explicit operator bool() const { return widget != nullptr; }
};

// Set by the window on every host that embeds a console. `context` is the host's and outlives
// the console; `parent` is the widget the console is embedded in.
using ConsoleFactory = std::function<ConsoleHandle(Context *context, QWidget *parent)>;

} // namespace relay::agent
