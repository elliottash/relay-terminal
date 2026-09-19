// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PaneTitles.h"

#include <QRegularExpression>
#include <QSet>

namespace relay {
namespace titles {
namespace {

// Words that say nothing about which work a pane is on. Mirrors STOPWORDS in relay_core/titles.py.
const QSet<QString> &stopwords() {
    static const QSet<QString> words = [] {
        QSet<QString> set;
        const auto list = QStringLiteral(
            "a an and are as at be being by for from in into is it its of on onto or over than that the their then "
            "this to up via with without new old more less some any all other another use using used make making "
            "work working fix fixing add adding update updating change changing run running write writing set setting")
                              .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &word : list) set.insert(word);
        return set;
    }();
    return words;
}

QSet<QString> contentWords(const QString &title) {
    static const QRegularExpression token(QStringLiteral("[a-z0-9]+"));
    QSet<QString> out;
    auto matches = token.globalMatch(title.toLower());
    while (matches.hasNext()) {
        const QString word = matches.next().captured();
        if (word.size() > 2 && !stopwords().contains(word)) out.insert(word);
    }
    return out;
}

}  // namespace

QString collapse(const QString &text) {
    return text.simplified();
}

QString clip(const QString &text, int limit) {
    const QString collapsed = collapse(text);
    if (limit <= 0 || collapsed.size() <= limit) return collapsed;
    QString cut = collapsed.left(std::max(1, limit - 1));
    const int space = cut.lastIndexOf(QLatin1Char(' '));
    if (space > limit * 0.6) cut = cut.left(space);
    while (!cut.isEmpty() && cut.endsWith(QLatin1Char(' '))) cut.chop(1);
    return cut + QChar(0x2026);
}

QString clean(const QString &text, int limit, int maxWords) {
    QString value = collapse(text);
    // Quotes the model wrapped the title in, and the "Title: …" preamble it sometimes adds.
    static const QString quotes = QStringLiteral("\"'`“”‘’");
    while (!value.isEmpty() && quotes.contains(value.front())) value.remove(0, 1);
    while (!value.isEmpty() && quotes.contains(value.back())) value.chop(1);
    static const QRegularExpression preamble(QStringLiteral("^(?:title|label)\\s*[:\\-—]\\s*"),
                                             QRegularExpression::CaseInsensitiveOption);
    value.remove(preamble);
    value = value.trimmed();
    while (value.endsWith(QLatin1Char('.'))) value.chop(1);
    value = value.trimmed();
    if (maxWords > 0) {
        const QStringList words = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.size() > maxWords) value = words.mid(0, maxWords).join(QLatin1Char(' '));
    }
    return clip(value, limit);
}

QStringList distinct(const QStringList &titles) {
    QStringList out;
    QSet<QString> seen;
    for (const QString &title : titles) {
        const QString text = collapse(title);
        if (text.isEmpty() || seen.contains(text.toLower())) continue;
        seen.insert(text.toLower());
        out.append(text);
    }
    return out;
}

bool relatedText(const QStringList &titles) {
    const QStringList items = distinct(titles);
    if (items.size() < 2) return true;
    QList<QSet<QString>> sets;
    for (const QString &item : items) {
        const QSet<QString> words = contentWords(item);
        if (words.isEmpty()) return false;
        sets.append(words);
    }
    for (int i = 1; i < sets.size(); ++i)
        if (!sets.first().intersects(sets.at(i))) return false;
    return true;
}

QString join(const QStringList &titles, bool related, const QString &phrase, int limit) {
    const QStringList items = distinct(titles);
    if (items.isEmpty()) return {};
    if (related) {
        const QString text = phrase.isEmpty() ? items.first() : clean(phrase);
        return clip(text.isEmpty() ? items.first() : text, limit);
    }
    return clip(items.join(QStringLiteral("; ")), limit);
}

}  // namespace titles
}  // namespace relay
