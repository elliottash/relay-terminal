// SPDX-License-Identifier: GPL-3.0-or-later
#include "Aliases.h"

#include <QRegularExpression>

namespace relay {
namespace aliases {
namespace {

const QRegularExpression &nameRe() {
    static const QRegularExpression re(QStringLiteral("\\A[a-z0-9][a-z0-9_-]{0,31}\\z"));
    return re;
}

const QRegularExpression &placeholderRe() {
    static const QRegularExpression re(QStringLiteral("\\{\\{\\s*([A-Za-z_][A-Za-z0-9_]{0,63})\\s*\\}\\}"));
    return re;
}

// A parameter with no default shows its own name as placeholder text, so the composer reads as a
// command with a blank to fill rather than as `{{mustaches}}`.
QString startingText(const Alias &alias, const QString &name) {
    for (const Param &param : alias.params) {
        if (param.name == name && param.hasDefault)
            return param.value;
    }
    return name;
}

bool startsFilled(const Alias &alias, const QString &name) {
    for (const Param &param : alias.params) {
        if (param.name == name)
            return param.hasDefault;
    }
    return false;
}

}  // namespace

bool validName(const QString &name) {
    return nameRe().match(name).hasMatch();
}

QString slug(const QString &title, const QStringList &taken) {
    QString text;
    for (const QChar &ch : title.toLower()) {
        if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')))
            text.append(ch);
        else if (!text.endsWith(QLatin1Char('-')))
            text.append(QLatin1Char('-'));
    }
    while (text.startsWith(QLatin1Char('-'))) text.remove(0, 1);
    while (text.endsWith(QLatin1Char('-'))) text.chop(1);
    if (text.size() > kMaxName) {
        QString cut = text.left(kMaxName);
        const int dash = cut.lastIndexOf(QLatin1Char('-'));
        if (dash > kMaxName / 2) cut = cut.left(dash);
        while (cut.endsWith(QLatin1Char('-'))) cut.chop(1);
        text = cut;
    }
    if (text.isEmpty() || !validName(text)) text = QStringLiteral("alias");
    QString candidate = text;
    int n = 1;
    while (taken.contains(candidate)) {
        ++n;
        const QString suffix = QStringLiteral("-%1").arg(n);
        QString stem = text.left(kMaxName - suffix.size());
        while (stem.endsWith(QLatin1Char('-'))) stem.chop(1);
        candidate = stem + suffix;
    }
    return candidate;
}

Rendered render(const Alias &alias) {
    Rendered out;
    int cursor = 0;
    auto it = placeholderRe().globalMatch(alias.text);
    while (it.hasNext()) {
        const auto match = it.next();
        out.literals.append(alias.text.mid(cursor, match.capturedStart() - cursor));
        out.text += out.literals.last();
        const QString name = match.captured(1);
        const QString value = startingText(alias, name);
        Field field;
        field.name = name;
        field.start = out.text.size();
        field.length = value.size();
        field.filled = startsFilled(alias, name);
        out.text += value;
        out.fields.append(field);
        cursor = match.capturedEnd();
    }
    out.literals.append(alias.text.mid(cursor));
    out.text += out.literals.last();
    return out;
}

bool reparse(Rendered &rendered, const QString &text) {
    if (rendered.literals.size() != rendered.fields.size() + 1) return false;
    if (rendered.fields.isEmpty()) return text == rendered.literals.value(0);
    if (!text.startsWith(rendered.literals.first())) return false;
    QList<Field> fields = rendered.fields;
    int cursor = rendered.literals.first().size();
    for (int i = 0; i < fields.size(); ++i) {
        const QString &after = rendered.literals.at(i + 1);
        int end;
        if (after.isEmpty()) {
            // The last field runs to the end of the line; an empty literal in the middle would
            // make two fields indistinguishable, so only a trailing one is allowed.
            if (i != fields.size() - 1) return false;
            end = text.size();
        } else {
            end = text.indexOf(after, cursor);
            if (end < 0) return false;
        }
        fields[i].start = cursor;
        fields[i].length = end - cursor;
        const QString value = text.mid(cursor, end - cursor);
        fields[i].filled = !value.isEmpty() && value != fields[i].name;
        cursor = end + after.size();
    }
    if (cursor != text.size()) return false;
    rendered.text = text;
    rendered.fields = fields;
    return true;
}

int fieldAt(const QList<Field> &fields, int caret) {
    for (int i = 0; i < fields.size(); ++i) {
        const Field &field = fields[i];
        if (caret >= field.start && caret <= field.start + field.length)
            return i;
    }
    return -1;
}

int nextField(const QList<Field> &fields, int caret, bool forward) {
    if (fields.isEmpty()) return -1;
    if (forward) {
        for (int i = 0; i < fields.size(); ++i) {
            if (fields[i].start > caret) return i;
        }
        return 0;   // wrap
    }
    for (int i = fields.size() - 1; i >= 0; --i) {
        if (fields[i].start + fields[i].length < caret) return i;
    }
    return fields.size() - 1;
}

int setField(Rendered &rendered, int index, const QString &value) {
    if (index < 0 || index >= rendered.fields.size()) return -1;
    Field &field = rendered.fields[index];
    const int delta = value.size() - field.length;
    rendered.text.replace(field.start, field.length, value);
    field.length = value.size();
    field.filled = !value.isEmpty();
    for (int i = index + 1; i < rendered.fields.size(); ++i)
        rendered.fields[i].start += delta;
    return field.start + field.length;
}

QList<QPair<QString, QString>> values(const Rendered &rendered) {
    QList<QPair<QString, QString>> out;
    for (const Field &field : rendered.fields)
        out.append({field.name, rendered.text.mid(field.start, field.length)});
    return out;
}

QStringList unfilled(const Alias &alias, const Rendered &rendered) {
    QStringList out;
    for (const Field &field : rendered.fields) {
        if (field.filled || startsFilled(alias, field.name)) continue;
        const QString here = rendered.text.mid(field.start, field.length);
        if (here.isEmpty() || here == field.name) {
            if (!out.contains(field.name)) out.append(field.name);
        }
    }
    return out;
}

QStringList names(const QList<Alias> &aliases) {
    QStringList out;
    for (const Alias &alias : aliases) {
        if (!alias.shadowed && !out.contains(alias.name)) out.append(alias.name);
    }
    return out;
}

Invocation matchSlash(const QString &line, const QStringList &known, const QStringList &reserved) {
    Invocation out;
    if (!line.startsWith(QLatin1Char('/'))) return out;
    const QString body = line.mid(1);
    const int space = body.indexOf(QRegularExpression(QStringLiteral("\\s")));
    const QString name = space < 0 ? body : body.left(space);
    if (name.isEmpty() || reserved.contains(name) || !known.contains(name)) return out;
    out.matched = true;
    out.viaSlash = true;
    out.name = name;
    out.args = space < 0 ? QString() : body.mid(space + 1).trimmed();
    return out;
}

Invocation matchTyped(const QString &line, const QStringList &known) {
    Invocation out;
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) return out;
    // A prefix or a path is never an alias: `!` and `*` force a route, and anything that starts
    // with a separator is a command somewhere on disk.
    const QChar first = trimmed.at(0);
    if (first == QLatin1Char('!') || first == QLatin1Char('*') || first == QLatin1Char('/')
        || first == QLatin1Char('.') || first == QLatin1Char('~') || first == QLatin1Char('#'))
        return out;
    const int space = trimmed.indexOf(QRegularExpression(QStringLiteral("\\s")));
    const QString word = space < 0 ? trimmed : trimmed.left(space);
    // `FOO=bar cmd` is an assignment, not an alias, even when `FOO` happens to name one.
    if (word.contains(QLatin1Char('='))) return out;
    if (!known.contains(word)) return out;
    out.matched = true;
    out.name = word;
    out.args = space < 0 ? QString() : trimmed.mid(space + 1).trimmed();
    return out;
}

QString paletteDetail(const Alias &alias) {
    QStringList parts;
    parts << (alias.isPrompt() ? QStringLiteral("prompt") : QStringLiteral("command"));
    parts << (alias.scope == QLatin1String("global") ? QStringLiteral("global")
                                                     : QStringLiteral("this project"));
    if (!alias.params.isEmpty())
        parts << QStringLiteral("%1 parameter%2").arg(alias.params.size())
                     .arg(alias.params.size() == 1 ? QString() : QStringLiteral("s"));
    parts << QStringLiteral("/%1").arg(alias.name);
    return parts.join(QStringLiteral(" · "));
}

QString fastPathHint(const Alias &alias) {
    if (!validName(alias.name)) return {};
    if (alias.isPrompt())
        return QStringLiteral("Next time: type /%1").arg(alias.name);
    return QStringLiteral("Next time: type /%1, or just %1 in terminal mode").arg(alias.name);
}

}  // namespace aliases
}  // namespace relay
