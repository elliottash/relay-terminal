// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Aliases: saved commands and prompts (issue G8DK, docs/AGENT-SESSIONS-PROTOCOL.md section 19).
//
// An alias is run three ways — from the actions palette, by typing `/name` in the composer, and by
// typing the name in terminal mode — and all three end at the same place: the composer holds the
// alias's text with its `{{parameter}}` placeholders turned into fields, Tab moves between them,
// and submitting sends the collected values to the worker, which does the substitution.
//
// The worker owns the files and the substitution, because the quoting rules that keep a parameter
// value *data* rather than shell syntax must live in exactly one place (relay_core/aliases.py).
// What lives here is what the composer needs and the worker cannot do: finding the placeholder
// spans in a template, moving the caret between them, replacing one as the user types, and
// deciding whether a line the user typed names an alias. Plain QtCore, so every rule is testable
// without a window (tests/aliases_test.cpp).
#include <QList>
#include <QString>
#include <QStringList>

namespace relay {
namespace aliases {

// Mirrors NAME_RE and PARAM_RE in relay_core/aliases.py.
constexpr int kMaxName = 32;

// One saved alias, as the worker's `aliases` event describes it.
struct Param {
    QString name;
    QString value;        // the default, or what the user has typed into the field
    bool hasDefault = false;
    QString description;
};

struct Alias {
    QString name;
    QString kind = QStringLiteral("command");   // "command" or "prompt"
    QString title;
    QString description;
    QString text;                                // the template, placeholders and all
    QList<Param> params;
    QString scope = QStringLiteral("local");     // "local" or "global"
    QStringList labels;
    bool shadowed = false;

    bool isPrompt() const { return kind == QLatin1String("prompt"); }
};

// A `{{name}}` occurrence in a template, or the text that has replaced it.
struct Field {
    QString name;
    int start = 0;        // offset into the rendered text
    int length = 0;       // length of what is there now
    bool filled = false;  // the user has typed into it (or it started from a default)
};

// What the user may type as an alias name: lower case, digits, `-` and `_`, 1..32 characters.
bool validName(const QString &name);

// A name for an alias made from a title, matching relay_core.aliases.slug.
QString slug(const QString &title, const QStringList &taken = {});

// ---- the composer's fields ----------------------------------------------------------------

// The template with every placeholder replaced by its default (or left as the bare parameter name
// when it has none), plus where each one landed. This is what goes into the composer: the user
// sees a runnable line rather than `{{mustaches}}`, and Tab walks the fields.
struct Rendered {
    QString text;
    QList<Field> fields;
    // The literal parts of the template around the fields (always fields.size() + 1 of them).
    // Kept so the values can be read back out of a composer the user has since edited.
    QStringList literals;
};
Rendered render(const Alias &alias);

// Re-read the field values out of `text` after the user has typed in the composer, by matching the
// literal parts of the template around them. Returns false — and leaves `rendered` alone — when the
// text no longer fits the template, which is how an alias the user has rewritten stops being an
// alias and goes to the router as an ordinary line.
bool reparse(Rendered &rendered, const QString &text);

// The field the caret is in or, when it is in none, the one that follows it. `forward` false walks
// backwards. Returns -1 when there are no fields.
int fieldAt(const QList<Field> &fields, int caret);
int nextField(const QList<Field> &fields, int caret, bool forward = true);

// Replace field `index` with `value`, moving every later field along. Returns the new caret
// position (the end of what was just written).
int setField(Rendered &rendered, int index, const QString &value);

// The values to send with `alias_run`: every field's current text, by parameter name.
QList<QPair<QString, QString>> values(const Rendered &rendered);

// Fields whose text is still the bare parameter name of a parameter with no default: the alias
// cannot run until they are filled.
QStringList unfilled(const Alias &alias, const Rendered &rendered);

// ---- recognising an invocation -------------------------------------------------------------

// What the user typed, split into an alias name and the rest.
struct Invocation {
    bool matched = false;
    QString name;
    QString args;
    bool viaSlash = false;
};

// `/name rest` in the composer. Only a name that is in `known` matches, so `/model` and the other
// built-in slash commands are never shadowed by an alias — the caller passes the built-ins in
// `reserved` and they win.
Invocation matchSlash(const QString &line, const QStringList &known, const QStringList &reserved = {});

// `name rest` typed in terminal mode. Matches only when the first word is exactly an alias name
// and nothing on the line makes it something else: a leading `!`, `/`, `.` or `~` is a path or a
// prefix, and an `=` in the first word is an assignment. The `known` list is the only source of
// names, so an alias can never be conjured out of a typo.
Invocation matchTyped(const QString &line, const QStringList &known);

// The names of `aliases`, for the two matchers.
QStringList names(const QList<Alias> &aliases);

// ---- the palette --------------------------------------------------------------------------

// The palette row's second column: what it is, where it came from, and the fast path to teach.
QString paletteDetail(const Alias &alias);

// The hint shown after running an alias the slow way, built from the live Keymap text the caller
// passes in. Empty when there is nothing faster to teach.
QString fastPathHint(const Alias &alias);

}  // namespace aliases
}  // namespace relay
