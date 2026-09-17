// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShellHighlighter.h"
#include "Theme.h"

#include <QRegularExpression>
#include <QTextDocument>

namespace relay {
namespace {

// One palette for both the caret and the tokens, so a shell line reads the same as the chip.
const QColor kCommand{0x3e, 0xc5, 0xf0};   // Relay accent: the command word
const QColor kFlag{0xe5, 0xc0, 0x7b};      // -r, --force
const QColor kString{0x7e, 0xc8, 0x8c};    // "quoted"
const QColor kPath{0x66, 0xd0, 0xc0};      // paths and globs
const QColor kOperator{0x80, 0x87, 0x96};  // | && > ;
const QColor kVariable{0xb4, 0x8e, 0xf7};  // $VAR, $(...)
const QColor kAgent{0xb4, 0x8e, 0xf7};     // agent destination
const QColor kToken{0x3e, 0xc5, 0xf0};     // @file and /command in an agent prompt

QTextCharFormat charFormat(const QColor &color, bool bold = false) {
    QTextCharFormat text;
    text.setForeground(color);
    if (bold) text.setFontWeight(QFont::DemiBold);
    return text;
}

}  // namespace

InputHighlighter::InputHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}

QColor InputHighlighter::colorFor(Destination destination) {
    switch (destination) {
    case Destination::Shell: return kCommand;
    case Destination::Agent: return kAgent;
    case Destination::Auto: break;
    }
    return relay::theme::Text;
}

void InputHighlighter::setDestination(Destination destination) {
    if (destination == m_destination) return;
    m_destination = destination;
    rehighlight();
}

void InputHighlighter::setKnownCommands(const QStringList &commands) {
    QSet<QString> known(commands.begin(), commands.end());
    if (known == m_known) return;
    m_known = known;
    if (m_destination == Destination::Shell) rehighlight();
}

void InputHighlighter::highlightBlock(const QString &text) {
    if (text.isEmpty()) return;
    switch (m_destination) {
    case Destination::Shell: highlightShell(text); break;
    case Destination::Agent: highlightAgent(text); break;
    case Destination::Auto: setFormat(0, text.size(), charFormat(relay::theme::Text)); break;
    }
}

void InputHighlighter::highlightShell(const QString &text) {
    setFormat(0, text.size(), charFormat(relay::theme::Text));

    // Quoted strings first: everything inside them is literal.
    static const QRegularExpression quoted(QStringLiteral("\"(\\\\.|[^\"\\\\])*\"?|'[^']*'?"));
    QVector<QPair<int, int>> literals;
    for (auto it = quoted.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), charFormat(kString));
        literals.append({match.capturedStart(), match.capturedEnd()});
    }
    auto inLiteral = [&literals](int position) {
        for (const auto &range : literals)
            if (position >= range.first && position < range.second) return true;
        return false;
    };

    static const QRegularExpression word(QStringLiteral("[^\\s|&;<>()]+"));
    static const QRegularExpression operators(QStringLiteral("\\|\\||&&|[|&;<>]+"));
    static const QRegularExpression variable(QStringLiteral("\\$\\{[^}]*\\}?|\\$\\([^)]*\\)?|\\$[A-Za-z_][A-Za-z0-9_]*"));

    for (auto it = operators.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        if (!inLiteral(match.capturedStart())) setFormat(match.capturedStart(), match.capturedLength(), charFormat(kOperator));
    }

    // The first word of the line, and of each pipeline stage, is a command name.
    bool expectCommand = true;
    for (auto it = word.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        const int start = match.capturedStart();
        if (inLiteral(start)) continue;
        const QString value = match.captured();
        if (expectCommand && !value.contains('=')) {
            // Always the command colour. Marking an unfamiliar word red was wrong as often as not
            // (builtins, aliases, anything installed since the pane started), and the pre-submit
            // check already reports a command that will not run (owner, 2026-09-17).
            setFormat(start, value.size(), charFormat(kCommand, true));
            expectCommand = false;
            continue;
        }
        if (value.startsWith('-') || value.startsWith('+')) setFormat(start, value.size(), charFormat(kFlag));
        else if (value.contains('/') || value.contains('~') || value.contains('*') || value.contains('?'))
            setFormat(start, value.size(), charFormat(kPath));
    }
    // A pipe or a semicolon starts a new command word.
    for (int i = 0; i + 1 < text.size(); ++i) {
        if (inLiteral(i)) continue;
        if (text.at(i) == '|' || text.at(i) == ';' || (text.at(i) == '&' && text.at(i + 1) == '&')) {
            static const QRegularExpression next(QStringLiteral("[^\\s|&;<>()]+"));
            const auto match = next.match(text, i + 1);
            if (match.hasMatch())
                setFormat(match.capturedStart(), match.capturedLength(), charFormat(kCommand, true));
        }
    }

    for (auto it = variable.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), charFormat(kVariable));
    }
}

void InputHighlighter::highlightAgent(const QString &text) {
    setFormat(0, text.size(), charFormat(relay::theme::Text));
    // Only Relay's own tokens are tinted: an attached path and a slash command.
    static const QRegularExpression token(QStringLiteral("(?:^|\\s)@[^\\s@\"]+|^/[A-Za-z][\\w-]*"));
    for (auto it = token.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        const int lead = match.captured().startsWith(' ') ? 1 : 0;
        setFormat(match.capturedStart() + lead, match.capturedLength() - lead, charFormat(kToken));
    }
}

}  // namespace relay
