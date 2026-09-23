// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// relay::modelrows — the rows of *the* model box (owner, 2026-09-20: "can you have the picker be
// the same as in the main terminal", card #PK5Q).
//
// There were two boxes over one idea. A terminal pane's (Pane::refreshPickers) drew the role rows
// — "glm-5.3", "glm-5.3-flash (flash)", a Local row where this machine serves one — then
// the per-model catalog in rank order, then "more models…" and the gear. The helper agent's
// (`relay::helpermodel`, retired with the panel by card #AGNT) drew Follow Main, Flash, Lite, one
// row per usable *provider* and a gear.
// Same control, same setting underneath, two different lists: which model a thing runs on read
// differently depending on which box you happened to be looking at.
//
// So the list is built here, once, and both boxes call it. What differs between a terminal pane
// and a helper panel is not the list: it is where the current row comes from (the pane's own
// model; the role the helper's worker resolved), and the two rows only a running pane can have —
// a guest running as a TUI in its shell, and the model serving this one turn. Those stay with the
// pane and are passed in.
//
// Nothing here draws, nothing here talks to a worker, and the `Context` is plain data, which is
// what makes tests/modelrows_test.cpp able to assert that the two boxes are the same list.
#include "ModelCatalog.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

class QComboBox;

namespace relay::modelrows {

// ----- the box since card #MDL1 (owner, 2026-09-21, second design: section 5.3) ---------------
//
// The first design put the modes at the top and paged between them with Left and Right. The owner
// replaced it the same day with one that shows every class at once:
//
//     high
//       gpt-6-astra        codex
//       glm-5.3            z.ai +1
//     main
//     ▌ kimi-k3            kimi           ← this pane's model, highlighted on open
//       glm-5.3            z.ai +1
//     flash
//       glm-5.3-flash      z.ai
//     local                               only where this machine serves one
//       bonsai-2-27b       spark
//     ───────────────────────
//     all models                          the box again, on every model (card #BXMS)
//     model settings                      the models pane
//
// So a class is a **header row** — a label, not a choice: "the class header rows are not selectable
// in the picker. thats redundant." — followed by that class's list, in list order, one row per
// model (`models::grouped` folds the providers that serve one name, and `trailing` says which of
// them the turn would go to), down to that class's **cutoff**, two by default
// (`models::curation::boxCutoff`). A class switched off (`boxShown`) is not drawn at all, and
// neither is one whose list has nothing left to draw.
//
// Two rulings shape what is *left out*. "exhausted models dont show up": a row whose every
// provider is spent, or has no key, is **dropped** here — which is the opposite of what the dialog
// and the tier lists do, and deliberately so. A list is an order the user wrote down and the dialog
// shows it whole; the box is the short answer to "what can I run right now", and a row that cannot
// take the turn is not part of that answer. And "no need to show the model class in the pane
// header": the collapsed chip is the model alone on every mode, with the mode in the tooltip.
//
// The one row that survives the cutoff whatever its rank is **this pane's own model** for its
// class: the box opens with it highlighted, so the highlight needs a home.
//
// Right on a model row expands its class to the whole list and Left collapses it (`Context::
// expanded`); the expansion lasts while the box is open and is the caller's state, not a setting.
// Typing filters across every listed model of every class, and a header stays while its class has
// a match — that is `FilterRow::group`, which this module fills in for every row.
// Enter on a model row switches the pane to that class **and** that model: `pick:<class>|<key>`,
// the same word the first design used, so `Pane::modelBoxPicked` is unchanged.

// What one pane picked for one mode, and the level it runs it at. The level is stored beside the
// key rather than read back off the tier list every time, because the list is the *machine's* and
// this is the pane's: re-ordering a list must not silently change the level a restored pane comes
// back on. An empty `effort` means "whatever the list, or the model, says".
struct ModePick {
    QString key;       // "<preset>|<model>"
    QString effort;    // a Relay level, or empty
    bool operator==(const ModePick &other) const { return key == other.key && effort == other.effort; }
};

// A pane's picks to and from the saved window layout (`RelayWindow::serializeNode`,
// `Pane::initRestore`): `{"flash": {"preset": …, "model": …, "effort": …}}`, the same three fields a
// tier-list entry has. "main" is never stored — main's model is the pane's own, saved as `model`.
// `modePicksFromJson` is total: anything that is not that shape is dropped rather than guessed at.
QJsonObject modePicksToJson(const QHash<QString, ModePick> &picks);
QHash<QString, ModePick> modePicksFromJson(const QJsonValue &saved);
// The picks this catalog can still run. An entry that has left the catalog or lost its key is
// dropped **silently** — the mode then reads rank 1 of its list again, which is what it would have
// done had the pick never been made. An exhausted subscription is *not* dropped: it comes back.
QHash<QString, ModePick> usableModePicks(const models::Catalog &catalog,
                                         const QHash<QString, ModePick> &picks);

// One row as the box wants it. `data` is what a pick means, and is the same word in every box:
//
//   class:<class>                         a header — a label, never a pick (see `header`)
//   pick:<class>|<preset>|<model>         that class **and** that model, in this pane
//   role:<role>                           put this pane on that mode; the phone's menu still sends it
//   entry:<preset>|<model>                that provider and that model, on the pane's own mode
//   guest:<id>                            Claude Code or Codex as a TUI in this pane's shell
//   gear:all                              "all models" — the box again, on every model (`filtered`)
//   gear:picker                           "model settings" — the models pane
//   gear:modelOptions                     Options › Models (no longer a row of the box)
struct Row {
    QString text;
    QString data;
    QString tooltip;             // the row's own tooltip (a tier's step-down note); usually empty
    QString trailing;            // the "via" column: the provider this row would run on
    // The class this row belongs to ("main"), on the header and on every model row under it, so a
    // filter can keep a header whose class still has a match and Left/Right know which class the
    // highlighted row would expand. Empty on the action rows.
    QString group;
    // A class label: not selectable, never highlighted, skipped by Up and Down (owner, 2026-09-21:
    // "the class header rows are not selectable in the picker. thats redundant.").
    bool header = false;
    bool separatorBefore = false;
    bool enabled = false;        // set by build(); false is a row that cannot be picked
};

// Everything a caller has to say. Every box reads the same `catalog`; the rest is who is asking.
struct Context {
    models::Catalog catalog;
    // The classes this box draws, in order (the design's high, main, flash, and local only where
    // this machine serves a model — card #JH22: a class that always resolved back to main would be
    // a promise the box cannot keep). "lite" is never a pane mode and is never one of them.
    QStringList classes{QStringLiteral("high"), QStringLiteral("main"), QStringLiteral("flash")};
    QString mode{QStringLiteral("main")};   // the class this pane is in: the row that opens highlighted
    // The classes Right has expanded to their whole list, while the box is open (design 5.3). The
    // caller owns it: it is not a setting, and closing the box forgets it.
    QSet<QString> expanded;
    // mode → the worker role that mode means *for this caller*. A console runs under a main-tier
    // role of its own ("switchboard"), and that role IS its main mode, so its box is the same list
    // as a terminal pane's (owner, 2026-09-21: not "(switchboard)", "those should be the same
    // systems"). Absent → the mode's own name, which is what a terminal pane wants.
    QHash<QString, QString> modeRole;
    // mode → the entry key this pane picked for that mode, when it picked one. "main" is the
    // pane's own model. A mode with no pick reads rank 1 of its list.
    QHash<QString, QString> modePick;
    QHash<QString, QString> roleModel;   // role → what the worker's role summary resolved it to
    QHash<QString, QString> roleNote;    // role → the tier's step-down note, as that row's tooltip
    // Claude Code and Codex as rows of their own (26.9): only a guest this machine has on PATH
    // that the worker cannot run as a harness, plus whichever one is actually in the pane — a
    // guest the worker *can* run is a catalog entry and is already one of the rows above. They
    // belong to the main page: a guest is the pane's own agent, not a tier. A console has none —
    // it works through Relay's own tools, which a guest does not take (#GH5T).
    QStringList guests;
    QHash<QString, QString> guestText;   // guest id → its row's text ("Claude Code · opus")
    // The `data` of the row that is current, when the caller knows better than this module does
    // (a guest in the pane's foreground, a helper pinned to one entry). Empty — the usual case —
    // and the page highlights the pane's own model for that page's mode.
    QString current;
    qint64 now = 0;                      // unix seconds for `exhausted`; 0 means the clock
};

// The whole box: every class's header and rows, the guest rows, the separator, "all models" and
// "model settings", and which row opens highlighted.
struct Box {
    QList<Row> rows;
    int current = -1;            // an index into `rows`; -1 when nothing there is the pane's
};

// The box for this pane, as design 5.3 draws it.
Box box(const Context &context);
// The box for this pane **while something is typed** (owner, 2026-09-21: "the text filter isnt
// working -- its supposed to show all available models, not just the ones selected for the box
// picker"; card #MDL1, design 5.7). Two things are wider than `box()`:
//
//   * every class whole, not down to its cutoff. The cutoff is what the box shows at rest — step
//     4 of the four — and a model you have ranked fifth in main is still a model you may type.
//   * one more section at the end of the classes, `other models`, holding every **available**
//     model (step 2, `models::shown`) that no class lists at all, folded one row per model as the
//     classes are. Its rows are `pick:main|<key>`: a model in no list is the pane's own model, on
//     its main mode, and the level comes from the Levels rule as it stands in the lists.
//
// Nothing is filtered here — the caller's popup does that, with the same rule it applies to every
// other row, so this hands over rows and not matches. It is also what "all models" opens (card
// #BXMS), so it leaves that row out: there is nothing wider for it to open.
Box filtered(const Context &context);
// The group id `other models`' header and rows carry. It is not a class: it has no list, no
// cutoff and no switch, Right does not expand it, and it exists only while the filter has text.
QString otherGroup();
// The data of the "all models" row, `gear:all`.
QString allModelsData();
// Its rows alone — what the phone's menu and the tests read.
QList<Row> build(const Context &context);

// `build`, then into the box: text, data, per-row tooltips, the via column, separators, the header
// and greyed rows switched off, and the current row selected. The caller blocks the box's signals
// and owns everything else about the widget. Returns the index the current row landed on, or -1.
int fill(QComboBox *box, const Context &context);

// Whether Right has anything to open on that class: its list holds more models than the box is
// drawing of it. False for a class that is off, or one the box is already showing whole.
bool expandable(const Context &context, const QString &klass);
// The entry key this pane would run in that mode: its own pick, else the first live entry of that
// tier's list. Empty when neither answers — then only the worker's role summary knows.
QString modeKey(const Context &context, const QString &mode);
// The model name to print for that mode: `modeKey`'s entry, else the role summary's model, run
// through the one naming rule (`models::nameOf`). Empty when nothing has said yet.
QString modeModel(const Context &context, const QString &mode);
// The models of one class, in list order and including the ones that cannot be used — what is
// dropped, and where, is `box`'s business. A class whose list is empty falls back to the whole
// shown catalog in rank order, which is what the box drew before the lists existed and is the only
// useful answer for a tier the user has never ranked.
QList<models::Entry> modeEntries(const Context &context, const QString &mode);
// What the collapsed chip says: **the model alone**, on every mode (owner, 2026-09-21: "no need
// to show the model class in the pane header"). The mode is in the box's tooltip instead. Empty
// only when no model is known at all — and then, as before, the mode's own name where it is not
// main, so the chip is never blank on a pane that is somewhere other than its own model.
QString collapsedText(const Context &context);
// The mode's own word for the chip's tooltip, empty on main: "running on the flash list".
QString collapsedTooltip(const Context &context);
// The index of the row carrying that `data`, or -1.
int indexOf(const QList<Row> &rows, const QString &data);

// A role row's text. The pane's own model is the model and nothing else — "glm-5.3" — and a row
// that is something else says what in parentheses: "glm-5.3-flash (flash)". The parentheses name
// the TIER the role runs on, never a protocol role name, so the Switchboard's agent (worker role
// "switchboard", main tier) reads "kimi-k3" exactly as a terminal pane does (owner, 2026-09-21:
// not "(switchboard)", and "i dont want it to say (main) either … those should be the same
// systems"). With no model known yet it is the role's name alone.
QString roleRowText(const QString &role, const QString &model);
QString roleTier(const QString &role);
// The one word for a tier or a worker role, everywhere one is printed to a person (card #MDL1,
// rule 1). Lower-case, the project's own vocabulary: "main", "flash", "high", "lite", "local", and
// a plain phrase for a worker-only role — "terminal use", "subagents", "helpers". This is the only
// table: Pane::roleLabel was a second, Title-Case one that disagreed with it, and now calls this.
QString roleLabel(const QString &role);

// What `/model <words>` picks, in both boxes' composers: an exact key, model id or label among the
// shown rows, then among every usable entry, then the picker's own filter over the shown rows.
// Empty when nothing matches — the caller says so and opens the picker.
QString resolve(const models::Catalog &catalog, const QString &words);

}  // namespace relay::modelrows
