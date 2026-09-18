// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// What a pane is and what it is doing, for the chrome around it. Pure rules, no widgets: the
// painting is in src/PaneChrome.h, the polling and the tab icons in src/RelayWindow.h.
//
//  * Pane types (card #SPBN). A leaf in the splitter says what it is with one dynamic property,
//    `paneType` ("board", "options", "actions", "sessions", "subagent", ...). Special types get a header
//    band with a low-strength tint and a glyph; terminal panes and file panes stay plain. The
//    band's colours follow "appearance/pane_colours": by type, by group, or off.
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
struct Tokens {
    QColor background, text, muted, shell, agent, success, warning, error;
};

struct TypeStyle {
    bool band = false;   // false: the pane stays plain (terminals, files, and every type with "off")
    QString label;       // engraved in the band: "SWITCHBOARD"
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
// The colour a state glyph is drawn in.
QColor stateInk(State state, const Tokens &tokens);

// WCAG contrast ratio, 1..21.
double contrast(const QColor &a, const QColor &b);
QColor mix(const QColor &a, const QColor &b, double weightOfA);
bool isLight(const QColor &background);

}  // namespace relay::panestatus
