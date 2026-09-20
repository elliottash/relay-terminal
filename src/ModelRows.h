// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// relay::modelrows — the rows of *the* model box (owner, 2026-09-20: "can you have the picker be
// the same as in the main terminal", card #PK5Q).
//
// There were two boxes over one idea. A terminal pane's (Pane::refreshPickers) drew the role rows
// — "glm-5.3 (main)", "glm-5.3 flash (flash)", a Local row where this machine serves one — then
// the per-model catalog in rank order, then "more models…" and the gear. The helper agent's
// (relay::helpermodel) drew Follow Main, Flash, Lite, one row per usable *provider* and a gear.
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
#include <QList>
#include <QString>
#include <QStringList>

class QComboBox;

namespace relay::modelrows {

// One row as the box wants it. `data` is what a pick means, and is the same word in both boxes:
//
//   role:main | role:flash | role:local   follow that tier
//   entry:<preset>|<model>                that provider and that model
//   guest:<id>                            Claude Code or Codex as a TUI in this pane's shell
//   gear:picker                           "more models…" — the relay::ModelPicker dialog
//   gear:modelOptions                     the gear — Options › Models
struct Row {
    QString text;
    QString data;
    QString tooltip;             // the row's own tooltip (a tier's step-down note); usually empty
    bool separatorBefore = false;
};

// Everything the two callers have to say. Both read the same `catalog`; the rest is who is asking.
struct Context {
    models::Catalog catalog;
    // The role rows, in order. "main" and "flash" always; "local" only where this machine serves
    // a model (card #JH22) — a row that always resolved back to Main would be a promise the box
    // cannot keep. A pane adds the role a restored session put it on, when that is another one.
    QStringList roles{QStringLiteral("main"), QStringLiteral("flash")};
    QHash<QString, QString> roleModel;   // role → the model it runs now; empty → the role's name alone
    QHash<QString, QString> roleNote;    // role → the tier's step-down note, as that row's tooltip
    // The entry the Main row already names, so the catalog below never repeats it. For a pane
    // that is its own model; for a helper, the model its Main tier landed on.
    QString mainKey;
    // Claude Code and Codex as rows of their own (26.9): only a guest this machine has on PATH
    // that the worker cannot run as a harness, plus whichever one is actually in the pane. A
    // helper has none — it works through Relay's own tools, which a guest does not take (#GH5T).
    QStringList guests;
    QHash<QString, QString> guestText;   // guest id → its row's text ("Claude Code · opus")
    QString current;                     // the `data` of the row that is current
    qint64 now = 0;                      // unix seconds for `exhausted`; 0 means the clock
};

// The rows, in order, with the separators marked.
QList<Row> build(const Context &context);

// `build`, then into the box: text, data, per-row tooltips, separators, and the current row. The
// caller blocks the box's signals and owns everything else about the widget. Returns the index the
// current row landed on, or -1.
int fill(QComboBox *box, const Context &context);

// A role row's text (owner, 2026-09-19): the model the row runs, then the role in parentheses —
// "glm-5.3 (main)". With no model known yet it is the role's name alone.
QString roleRowText(const QString &role, const QString &model);
QString roleLabel(const QString &role);

// What `/model <words>` picks, in both boxes' composers: an exact key, model id or label among the
// shown rows, then among every usable entry, then the picker's own filter over the shown rows.
// Empty when nothing matches — the caller says so and opens the picker.
QString resolve(const models::Catalog &catalog, const QString &words);

}  // namespace relay::modelrows
