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
#include <QString>
#include <QStringList>

class QComboBox;

namespace relay::modelrows {

// ----- the box since card #MDL1 (owner, 2026-09-21) --------------------------------------------
//
// "first it just says high, main, flash, with the first model in parens, eg high (gpt-6-astra).
// then it lists the models for the mode you are in … with your selected model highlighted. so it
// shows the models from main if a pane is on /main, or if its on /high, it shows the high models."
// And then: "great, left/right changes mode. add /high."
//
//     high (gpt-6-astra)
//   • main (kimi-k3)              ← the mode this pane is in
//     flash (glm-5.3-flash)
//     local (bonsai-2-27b)        only where this machine serves one
//     ─────────────────────
//     gpt-6-astra   codex
//   ▌ kimi-k3       kimi          ← this pane's model, highlighted when the box opens
//     glm-5.3       z.ai +1
//     ─────────────────────
//     more models…   customize…
//
// So the box is a small stack of **pages**, one per mode, and every page carries the same mode rows
// at the top with the marker on a different one. Left and Right turn between them in place
// (relay::FilterPopup::setPages); nothing reaches the worker until Enter. What a mode row's
// parentheses name is what **this pane** would run in that mode — its own pick when it has one,
// else rank 1 of that tier's list, else what the worker's role summary resolved. `lite` is not a
// pane mode and has no page.
//
// Below the separator are the models of that page's mode, **in list order** — the order is the
// information: rank 1 is the default and the rest is the failover order — and **one row per model**
// (rule 2): `relay::models::grouped` folds the providers that serve one name into a single row, and
// the row's right-hand `trailing` part says which of them the turn would go to. A row whose every
// provider is spent or unusable is greyed **in place**, with the reason in its tooltip, never
// dropped: a subscription running out must not make a model disappear from the list the user
// ranked (design 1.3, rule 2).

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
//   role:<role>                           put this pane on that mode (a mode row)
//   pick:<mode>|<preset>|<model>          that mode **and** that model, in this pane
//   entry:<preset>|<model>                that provider and that model, on the pane's own mode
//   guest:<id>                            Claude Code or Codex as a TUI in this pane's shell
//   gear:picker                           "more models…" — the relay::ModelPicker dialog
//   gear:modelOptions                     the gear — Options › Models
struct Row {
    QString text;
    QString data;
    QString tooltip;             // the row's own tooltip (a tier's step-down note); usually empty
    QString trailing;            // the "via" column: the provider this row would run on
    bool separatorBefore = false;
    bool enabled = false;        // set by build(); false is a row greyed in place, with the reason
};

// Everything a caller has to say. Every box reads the same `catalog`; the rest is who is asking.
struct Context {
    models::Catalog catalog;
    // The modes this box offers, in order (the design's high, main, flash, and local only where
    // this machine serves a model — card #JH22: a row that always resolved back to main would be a
    // promise the box cannot keep). "lite" is not a pane mode.
    QStringList modes{QStringLiteral("high"), QStringLiteral("main"), QStringLiteral("flash")};
    QString mode{QStringLiteral("main")};   // the mode this pane is in: the marker, and the page
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

// One page of the box: a mode, its rows, and the row that is current on it.
struct Page {
    QString mode;
    QList<Row> rows;
    int current = -1;            // an index into `rows`; -1 when nothing there is the pane's
};

// The page for one mode — the mode rows (marker on `context.mode`), that mode's models, the
// guests where the caller has any, then "more models…" and the gear.
Page page(const Context &context, const QString &mode);
// Every page, in `context.modes` order. This is what the popup is given.
QList<Page> pages(const Context &context);

// The rows of the page the caller is on (`context.mode`), with the separators marked.
QList<Row> build(const Context &context);

// `build`, then into the box: text, data, per-row tooltips, the via column, separators, greyed
// rows and the current row. The caller blocks the box's signals and owns everything else about the
// widget. Returns the index the current row landed on, or -1.
int fill(QComboBox *box, const Context &context);

// A mode row's text: "high (gpt-6-astra)", with the marker on the mode the pane is in and two
// spaces in its place on the others, so the names line up. With no model known it is the mode
// alone.
QString modeRowText(const QString &mode, const QString &model, bool current);
// The entry key this pane would run in that mode: its own pick, else the first live entry of that
// tier's list. Empty when neither answers — then only the worker's role summary knows.
QString modeKey(const Context &context, const QString &mode);
// The model name to print for that mode: `modeKey`'s entry, else the role summary's model, run
// through the one naming rule (`models::nameOf`). Empty when nothing has said yet.
QString modeModel(const Context &context, const QString &mode);
// The models of one mode, in list order and including the ones that cannot be used — the box greys
// them in place. A mode whose list is empty falls back to the whole shown catalog in rank order,
// which is what the box drew before the lists existed and is the only useful answer for a tier the
// user has never ranked.
QList<models::Entry> modeEntries(const Context &context, const QString &mode);
// What the collapsed chip says (design 5.1): the model alone on main, "<model> · <mode>" on any
// other mode. Empty only when no model is known at all.
QString collapsedText(const Context &context);
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
