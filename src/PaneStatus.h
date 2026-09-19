// SPDX-License-Identifier: AGPL-3.0-or-later
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
//  * Live states (card #V8KT, owner 2026-09-19: "its not clear enough if a pane agent or program
//    is running"). Running, Working and Subagents are work happening now: their glyph is the
//    Relay mark itself, blinking (pulseScale, card #4E13), in the work's own colour — blue for
//    terminal work, violet for agent work — with the state's word beside the title (stateText).
//    The same word sits above the prompt box, left-aligned with the prompt text and in the normal
//    weight (#HQ2B): "Relaying <action>…", the action a gerund of
//    what the agent is doing ("thinking", "reading src/Pane.h"), or the program's name for
//    terminal work. A tab with anything live carries a blinking dot in that colour (liveMarker),
//    whatever news its icon is showing. The desktop's reduce-motion signal (a cursor flash time
//    of 0) stills every one of these marks.
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
                 // the agent asked you a question and is waiting on the answer (#MQ9C), or a turn
                 // you have not seen ended on a question
};

int urgency(State state);
State mostUrgent(const QList<State> &states);
// Stable ids ("needs-you"), for properties, logs and tests.
QString stateName(State state);
// What the glyph's tooltip says ("Agent needs you").
QString stateLabel(State state);
// The same word for a header row that has run out of room: "Command running" → "Running",
// "Subagents working" → "Subagents". A live pane's word sits between the glyph and the title
// with the ssh, phone, subagent and usage chips beside it, and at three panes to a window the
// row cannot hold every one of them: the word is the piece that gives way, and a word that
// shortens reads, where "Command" cut out of "Command running" only looks broken.
QString stateLabelShort(State state);

// ----- live states (cards #V8KT, #4E13) ----------------------------------------------------------
// Work happening in the pane right now: a command runs, an agent turn runs, or subagents run.
// These are the states whose marks move — the Relay mark blinking in the header, a word beside
// it, a blinking dot on the tab — because "is anything running over there?" is answered by
// motion, not by a shape you have to know. The news states (done, failed, needs you) stay still:
// they pull the eye by being news. Owner, 2026-09-19: "the icons / anims should use blue for
// terminal work happening and violet for agent work happening", and "a blue blinking relay icon
// (terminal program running) or purple blinking relay icon (agent working)" (#4E13).
bool isLive(State state);
// What a tab's live mark says: Working (the agent's violet) when any agent work is live in the
// tab — a turn or subagents — Running (the terminal's blue) when only commands run, Idle when
// nothing is. Agent work wins the tie, as it does in the urgency order.
State liveMarker(const QList<State> &states);
// How a live mark blinks: full size and a small step alternating (1.0, 0.62) on the caller's
// clock, a blink the eye catches without waiting for a breath to turn. A scale, never an
// opacity: the ink keeps its contrast at every step. `phase < 0` is "no animation" — the
// desktop's reduce-motion signal — and draws the mark at full size.
qreal pulseScale(int phase);

// ----- one cadence for every live mark (owner, 2026-09-19) ---------------------------------------
// The step is read off the wall clock, so it is the same number in every widget, every window and
// every process on this machine at any instant: the pane glyph (PaneStateGlyph), the tab's live
// dot (RelayWindow::applyTabIcons) and anything added later all step together. It deliberately
// does not depend on how often the caller looks — the tab dot used to count its own steps off the
// 400 ms status poll, so two windows blinked on whatever beat each had started polling on, and a
// tab could be dark while the glyph in the pane below it was lit.
inline constexpr qint64 kPulseStepMs = 600;   // four steps of it, which pulseScale reads as two levels

// The step `msSinceEpoch` falls in: 0, 1, 2 or 3, to hand straight to pulseScale().
int pulsePhaseAt(qint64 msSinceEpoch);
int pulsePhaseNow();
// Milliseconds from `msSinceEpoch` to the next step boundary. Always 1..kPulseStepMs, never 0, so
// a timer armed with it lands in the next step instead of firing twice inside this one.
int msToNextPulseStepAt(qint64 msSinceEpoch);
int msToNextPulseStep();

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
    bool questionOpen = false;     // an `ask_user` card is up in the pane, waiting to be answered
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
    QColor background, text, muted, shell, agent, success, warning, error, action, tool;
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
// The colour the state's word is written in: the state's own ink, lifted to at least 4.5:1 on the
// ground it sits on (a word is text; the glyph keeps the glyph's 3:1).
QColor stateText(State state, const QColor &ground, const Tokens &tokens);

// ----- the subagent badge (card #YMSR) ---------------------------------------------------------
// How many agents a pane's own agent has running, as a badge in the pane header (owner,
// 2026-09-19: "in the pane header, add a badge with a number for number of subagents, if
// applicable"). The count is of *live* subagents — waiting or running — the same number
// `Facts::liveSubagents` resolves a state from, so the badge and the state's own mark can never
// disagree about the pane. An agent that finished is not work happening now, and the strip under
// the composer is where the ones that ended are read.
//
// Zero is not a "0": it is no badge at all. That is what "if applicable" asks for, and it is what
// keeps a pane that has never started a subagent looking exactly as it did before. The number is
// the whole badge — the state's word beside it already says whose work it counts.
QString subagentBadgeText(int live);
// What the badge says on hover: the count in words, and the key that opens the pane showing those
// agents. `keys` is the live Keymap text of `agent.subagentPane`, empty when it is unbound; the
// caller reads it, as SubagentsPanel::setFolded does, so a rebound key is right here too.
QString subagentBadgeTooltip(int live, const QString &keys);

// The badge's ground, hairline and ink on `ground` — the header's own colour, or the remote band's
// while an ssh session is up. The agent's violet, tinted the way a chip is; a painter takes all
// three together, as it does for a type band's TypeStyle.
struct BadgeStyle {
    QColor fill;
    QColor line;
    QColor ink;
};
BadgeStyle subagentBadgeStyle(const QColor &ground, const Tokens &tokens);

// WCAG contrast ratio, 1..21.
double contrast(const QColor &a, const QColor &b);
QColor mix(const QColor &a, const QColor &b, double weightOfA);
bool isLight(const QColor &background);

}  // namespace relay::panestatus
