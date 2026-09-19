// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SlashCommands.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <vector>

namespace relay::slash {
namespace {

// A command name: letters, digits, `-` and `_`, starting with a letter. The built-ins are all
// [a-z-], but a name typed in the wrong case ("/Help") is still an attempt at a command, and
// aliases may carry digits or an underscore.
const QRegularExpression &namePattern() {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]*$"));
    return pattern;
}

// Optimal string alignment distance: insertions, deletions, substitutions and the transposition
// of two neighbours, each one slip. Bounded work — both strings are command names.
int distance(const QString &a, const QString &b) {
    const int n = int(a.size()), m = int(b.size());
    std::vector<std::vector<int>> d(size_t(n) + 1, std::vector<int>(size_t(m) + 1, 0));
    for (int i = 0; i <= n; ++i) d[size_t(i)][0] = i;
    for (int j = 0; j <= m; ++j) d[0][size_t(j)] = j;
    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            int best = std::min({d[size_t(i) - 1][size_t(j)] + 1,
                                 d[size_t(i)][size_t(j) - 1] + 1,
                                 d[size_t(i) - 1][size_t(j) - 1] + cost});
            if (i > 1 && j > 1 && a.at(i - 1) == b.at(j - 2) && a.at(i - 2) == b.at(j - 1))
                best = std::min(best, d[size_t(i) - 2][size_t(j) - 2] + 1);
            d[size_t(i)][size_t(j)] = best;
        }
    }
    return d[size_t(n)][size_t(m)];
}

}  // namespace

QString attemptedName(const QString &text, const Probe &exists) {
    const QString trimmed = text.trimmed();
    if (!trimmed.startsWith(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\n'))) return {};
    static const QRegularExpression space(QStringLiteral("\\s"));
    const QString body = trimmed.mid(1);
    const int cut = body.indexOf(space);
    const QString word = cut < 0 ? body : body.left(cut);
    // A long first word is a sentence or a path, not a command somebody meant to type.
    if (word.isEmpty() || word.size() > 40 || !namePattern().match(word).hasMatch()) return {};
    // `/bin`, `/tmp`, `/opt`: a single-segment absolute path that is really there belongs to the
    // shell. Checked last, because it is the only rule that touches the filesystem.
    const QString path = QLatin1Char('/') + word;
    if (exists ? exists(path) : QFileInfo::exists(path)) return {};
    return word;
}

QStringList closest(const QString &name, const QStringList &known, int max) {
    if (name.isEmpty() || max <= 0) return {};
    const QString lower = name.toLower();
    // One slip for a short name, two once there is enough of it for a slip not to be a coincidence.
    const int allowed = lower.size() <= 4 ? 1 : 2;
    struct Ranked { int score; int index; QString name; };
    QList<Ranked> ranked;
    for (int i = 0; i < known.size(); ++i) {
        const QString candidate = known.at(i).toLower();
        if (candidate.isEmpty()) continue;
        int score = -1;
        if (candidate.startsWith(lower) || lower.startsWith(candidate)) score = 0;   // still being typed
        else if (const int slips = distance(lower, candidate); slips <= allowed) score = slips;
        if (score >= 0) ranked.append({score, i, known.at(i)});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
        return a.score != b.score ? a.score < b.score : a.index < b.index;
    });
    QStringList out;
    for (const auto &entry : std::as_const(ranked)) {
        if (out.size() >= max) break;
        out << entry.name;
    }
    return out;
}

QString unknownLine(const QString &name, const QStringList &known) {
    QStringList suggestions;
    for (const QString &candidate : closest(name, known, 3)) suggestions << QLatin1Char('/') + candidate;
    QString line = QStringLiteral("Unknown command: /") + name;
    if (!suggestions.isEmpty()) {
        const QString last = suggestions.takeLast();
        const QString list = suggestions.isEmpty() ? last
                                                   : suggestions.join(QStringLiteral(", ")) + QStringLiteral(" or ") + last;
        line += QStringLiteral(" · did you mean %1?").arg(list);
    }
    return line + QStringLiteral(" · type / for every command, /help for the keys");
}

QStringList offered(const QStringList &names, const QStringList &hidden) {
    QStringList out;
    for (const QString &name : names)
        if (!hidden.contains(name)) out << name;
    return out;
}

QString resolve(const QString &typed, const QStringList &names, const QStringList &hidden, bool *exact) {
    if (exact) *exact = false;
    if (names.contains(typed)) {          // typing it in full is deliberate, hidden or not
        if (exact) *exact = true;
        return typed;
    }
    for (const QString &name : names)     // registry order decides which prefix match wins
        if (!hidden.contains(name) && name.startsWith(typed)) return name;
    return {};
}

}  // namespace relay::slash
