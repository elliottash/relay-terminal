// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// What a pane is and what it is doing, for the chrome around it. Pure rules, no widgets: the
// painting is in src/PaneChrome.h, the polling and the tab icons in src/RelayWindow.h.
//
//  * Pane types (card #SPBN). A leaf in the splitter says what it is with one dynamic property,
//    `paneType` ("board", "options", "actions", "sessions", "subagent", ...). Special types get a header
//    band with a low-strength tint and a glyph; terminal panes and file panes stay plain. The
//    band's colours follow "appearance/pane_colours": by type, by group, or off (a neutral band).
//  * Pane states (card #XM0T). A terminal pane is in exactly one State at a time, read off facts
//    the pane already keeps. The pane header shows its own state; a tab shows the most urgent state
//    among its panes, so a background tab can say "needs you" without being opened.
//  * Remote sessions (#SPBN, owner 2026-09-18). A terminal whose foreground program is ssh, mosh
//    or telnet is typing into another machine. That is a safety signal, not decoration: it shows
//    whatever the pane-colour setting says.
#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

namespace relay::panestatus {

// ----- states (#XM0T) ---------------------------------------------------------------------------
// In urgency order, least urgent first: a tab shows the highest among its panes.
enum class State {
    Idle,        // shell prompt, nothing running
    Running,     // a command runs in the terminal
    Subagents,   // the main agent is idle, subagents it started still run
    Working,     // an agent turn is running
    Recommends,  // the agent put a command in the prompt box for you to run, and is not waiting on it
    Done,        // a turn finished while you were not looking
    Failed,      // a turn failed while you were not looking
    NeedsYou,    // blocked on you: a program asks for input, the agent waits on a handed command,
                 // or a turn you have not seen ended on a question
};

int urgency(State state);
State mostUrgent(const QList<State> &states);
// Stable ids ("needs-you"), for properties, logs and tests.
QString stateName(State state);
// What the glyph's tooltip says ("Agent needs you").
QString stateLabel(State state);

// Everything the state is decided from. Each field is state a Pane already keeps.
struct Facts {
    bool agentBusy = false;
    int liveSubagents = 0;
    bool processBusy = false;      // the terminal's foreground is not the shell
    bool programAsking = false;    // a program in the terminal waits for a line or a password
    bool handoffWaiting = false;   // run_in_terminal prefill with report_back: the agent's next turn waits on it
    bool handoffOffered = false;   // a prefill the agent does not wait on
    quint64 finishSerial = 0;      // bumps on every finished turn
    QString lastOutcome;           // of that turn: "done", "error" or "cancelled"
    bool lastAsked = false;        // the last done turn's reply ended on a question
};

// `seenSerial` is the finishSerial the user has already seen (the window records it whenever the
// pane is the one being looked at). A finish past it is news: Done, Failed or NeedsYou.
State resolve(const Facts &facts, quint64 seenSerial);

// Does the agent's reply end by asking the user something? The last non-empty line, stripped of
// Markdown emphasis and closing quotes or brackets, ends in "?".
bool endsWithQuestion(const QString &reply);

// ----- what the pane is waiting for (cards #V7QD, #KP4M) ----------------------------------------
// The line the prompt box shows while a pane's main ("orchestrator") agent is blocked on the
// background work it started: "waiting for 2 subagents, 1 job . . .". It lives here, beside the
// pane states, rather than on either model: it is a pure rule about what the pane is doing, and it
// is fed counts and flags by both `relay::SubagentModel` and `relay::JobsModel`.
//
// Each kind is waited on when some of it is live *and* either the main agent is explicitly blocked
// on that kind, or no turn of its own is running (its step is finished and only the background work
// is left). A turn that started background work and carried on working says nothing — otherwise the
// line would be up for most of every turn.
struct Waiting {
    int subagents = 0;         // live subagents (waiting or running)
    int jobs = 0;              // running jobs the model was handed back
    bool onSubagents = false;  // blocked on them: an `agent_wait` call, or a live foreground subagent
    bool onJobs = false;       // blocked on them: a `command_output` call, which waits on a job
    bool mainBusy = false;     // a turn of this pane's own agent is running
};

// "2 subagents, 1 job" — only the kinds actually waited on. Empty when nothing is.
QString waitingSubject(const Waiting &waiting);
bool isWaiting(const Waiting &waiting);

// "waiting for 2 subagents, 1 job . . .", the dots growing with `phase` (0-3) and the line padded to
// a constant width so the animation never moves the text next to it. `phase < 0` means "no
// animation", which draws the dots in full (see Pane::refreshBackgroundWait and
// QApplication::cursorFlashTime). Empty when nothing is waited on.
QString waitingLine(const Waiting &waiting, int phase);
// The same line and its shorter forms, longest first, for RichEditor::setPlaceholders in a narrow
// pane: the full line, then without "waiting for", then the bare counts (one kind) or "waiting".
QStringList waitingLines(const Waiting &waiting, int phase);

// ----- remote sessions --------------------------------------------------------------------------
bool isRemoteProgram(const QString &programName);
// The destination in a remote program's command line, as the user wrote it: "ssh -p 2222 me@box
// uptime" → "me@box", "ssh -l me box" → "me@box", "mosh box -- tmux" → "box", "telnet host 23" →
// "host". Empty when there is none (e.g. `ssh -V`).
QString remoteHost(const QString &commandLine);

// ----- pane types (#SPBN) -----------------------------------------------------------------------
enum class ColourMode { ByType, ByGroup, Off };
ColourMode colourModeFrom(const QString &value);   // "type" (default), "group", "off"
QString colourModeId(ColourMode mode);
QStringList colourModeIds();
QStringList colourModeLabels();

enum class Glyph { None, Switchboard, Options, Actions, Sessions, Subagent, Turn, Tool, Remote, Phone };

// The theme colours the tints are made from; the caller fills it from the live theme.
// `action` is the Actions pane's red-orange (owner, 2026-09-18); every theme file names it, and
// src/Theme.cpp derives one for a user theme that does not.
struct Tokens {
    QColor background, text, muted, shell, agent, success, warning, error, action;
};

struct TypeStyle {
    bool band = false;   // false: the pane stays plain (terminals and file panes)
    QString label;       // the band's name, in sentence case: "Switchboard"
    Glyph glyph = Glyph::None;
    QString group;       // "tools" or "agents"
    QColor fill;         // the band's ground: a low-strength tint of the hue on the pane's background
    QColor line;         // the hairline under the band
    QColor ink;          // the glyph, at least 3:1 on fill
    QColor text;         // the label, at least 4.5:1 on fill
};

// The type's style under `mode`. Unknown non-empty types are tools with a generic glyph and their
// own name as the label, so a new pane type is styled by setting the property and nothing else.
// `label` overrides the default label when not empty (the `paneLabel` property).
TypeStyle typeStyle(const QString &paneType, ColourMode mode, const Tokens &tokens, const QString &label = QString());
// The remote-session header: hatched ground, hairline and chip. Independent of the mode.
TypeStyle remoteStyle(const Tokens &tokens);
// The chip that says a phone is watching or driving the pane (Relay's "remote share").
TypeStyle phoneStyle(const Tokens &tokens);

// ----- the title-bar buttons that open a tool pane (owner, 2026-09-18) --------------------------
// "the sessions / actions / switchboard / options buttons at the top right should be highlighted
// when they are open (using the header colors). click again to close those panes."
//
// One table for the four of them, so a button's glyph, its light, its tooltip and what its second
// click closes cannot drift apart: each names the pane type it owns, and everything else is read
// from that type's own style.
struct ToolButton {
    QString paneType;    // the pane this button owns, spelled as the `paneType` property does
    QString action;      // the action that opens it (a Keymap action id)
    QString label;       // the tooltip while that pane is closed
    QString openLabel;   // ... and while it is open, because the next click will close it
    QString what;        // the noun the shortcut hint uses ("the Switchboard")
};
const QList<ToolButton> &toolButtons();

// How one of those buttons is painted while its pane is open: the pane's own header band, firmed
// up for a 26 px button. `band` is typeStyle() for that pane type under the live colour mode, so
// "pane colours off" arrives here as a neutral band and still comes out unmistakable — which panes
// are open is information, not decoration.
struct OpenButtonStyle {
    QColor fill;   // the ground behind the glyph
    QColor line;   // its hairline, the band's own
    QColor ink;    // the glyph, at least 3:1 on fill
};
OpenButtonStyle openButtonStyle(const TypeStyle &band, bool hovered);

// The keyboard-focus ring on a header button, on whatever ground that button ended up with.
QColor focusRing(const QColor &ground, const Tokens &tokens);
// The colour a state glyph is drawn in.
QColor stateInk(State state, const Tokens &tokens);

// WCAG contrast ratio, 1..21.
double contrast(const QColor &a, const QColor &b);
QColor mix(const QColor &a, const QColor &b, double weightOfA);
bool isLight(const QColor &background);

}  // namespace relay::panestatus
