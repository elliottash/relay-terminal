// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Exact card-code lookup for the Actions palette. This only reads a card's bounded front matter
// and title; full-text Board search remains with the Board worker.
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QString>

#include <optional>

namespace relay::palettecards {

struct Card {
    QString id;
    QString title;
    QString session;
    QString path;
};

inline QString code(const QString &query)
{
    QString value = query.trimmed();
    if (value.startsWith(QLatin1Char('#'))) value.remove(0, 1);
    if (value.size() != 4) return {};
    value = value.toUpper();
    bool letter = false;
    for (const QChar ch : value) {
        if (!QStringLiteral("0123456789ABCDEFGHJKMNPQRSTVWXYZ").contains(ch)) return {};
        letter |= ch.isLetter();
    }
    return letter ? value : QString();
}

inline std::optional<Card> find(const QString &boardRoot, const QString &id)
{
    if (boardRoot.isEmpty() || code(id) != id) return std::nullopt;
    QDirIterator files(boardRoot, {QStringLiteral("*.md")}, QDir::Files, QDirIterator::Subdirectories);
    const QDir root(boardRoot);
    while (files.hasNext()) {
        const QString path = files.next();
        const QString relative = root.relativeFilePath(path);
        if (relative.startsWith(QStringLiteral("threads/"))) continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        if (file.readLine().trimmed() != "---") continue;
        Card card;
        card.path = path;
        bool ended = false;
        for (int line = 0; line < 80 && !file.atEnd(); ++line) {
            const QString text = QString::fromUtf8(file.readLine()).trimmed();
            if (text == QLatin1String("---")) { ended = true; break; }
            if (text.startsWith(QStringLiteral("id: "))) card.id = text.mid(4).trimmed();
            else if (text.startsWith(QStringLiteral("session: "))) card.session = text.mid(9).trimmed();
        }
        if (!ended || card.id != id) continue;
        for (int line = 0; line < 12 && !file.atEnd(); ++line) {
            const QString text = QString::fromUtf8(file.readLine()).trimmed();
            if (text.startsWith(QStringLiteral("# "))) {
                card.title = text.mid(2);
                return card;
            }
        }
    }
    return std::nullopt;
}

} // namespace relay::palettecards
