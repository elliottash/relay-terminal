// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShellHighlighter.h"
#include "Theme.h"

#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QTextDocument>

namespace relay {
namespace {

// One palette for both the caret and the tokens, so a shell line reads the same as the chip.
// These follow the selected theme ([syntax] in the theme file, issue 0JA7): they are read at
// highlight time and the constructor rehighlights when the theme changes.
const QColor &kCommand() { return relay::theme::SyntaxCommand; }   // the command word
const QColor &kUnknown() { return relay::theme::SyntaxUnknown; }   // Terminal mode only: does not resolve
const QColor &kFlag() { return relay::theme::SyntaxFlag; }         // -r, --force
const QColor &kString() { return relay::theme::SyntaxString; }     // "quoted"
const QColor &kPath() { return relay::theme::SyntaxPath; }         // paths and globs
const QColor &kOperator() { return relay::theme::SyntaxOperator; } // | && > ;
const QColor &kVariable() { return relay::theme::SyntaxVariable; } // $VAR, $(...)
const QColor &kAgent() { return relay::theme::SyntaxAgent; }       // agent destination
const QColor &kToken() { return relay::theme::SyntaxToken; }       // @file and /command in an agent prompt

QTextCharFormat charFormat(const QColor &color, bool bold = false) {
    QTextCharFormat text;
    text.setForeground(color);
    if (bold) text.setFontWeight(QFont::DemiBold);
    return text;
}

}  // namespace

InputHighlighter::InputHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {
    // Colour themes (issue 0JA7): a theme switch must recolour the line already in the box.
    QObject::connect(relay::theme::notifier(), &relay::theme::Notifier::themeChanged, this,
                     [this] { rehighlight(); });
}

// Bash builtins and keywords are commands even though they are not files on PATH; the shell's
// command list does not always carry them (`test one two` was being marked unknown).
static const QSet<QString> &shellBuiltins() {
    static const QSet<QString> builtins = [] {
        const QString words = QStringLiteral(
            "alias bg bind break builtin caller cd command compgen complete compopt continue declare dirs "
            "disown echo enable eval exec exit export false fc fg getopts hash help history jobs kill let "
            "local logout mapfile popd printf pushd pwd read readarray readonly return set shift shopt "
            "source suspend test times trap true type typeset ulimit umask unalias unset wait "
            "if then else elif fi for while until do done case esac function select time in");
        QSet<QString> out;
        for (const QString &word : words.split(' ', Qt::SkipEmptyParts)) out.insert(word);
        return out;
    }();
    return builtins;
}

// Relay's shell integration reports the shell's own command list, but it is opt-in and a pane may
// not have it. Falling back to a scan of PATH means Terminal mode can still tell a typo from a
// command (owner: "greckle" stayed cyan, 2026-09-17).
static const QSet<QString> &pathCommands() {
    static const QSet<QString> commands = [] {
        QSet<QString> out;
        const QString path = qEnvironmentVariable("PATH");
        for (const QString &dir : path.split(':', Qt::SkipEmptyParts)) {
            QDirIterator it(dir, QDir::Files | QDir::Executable | QDir::NoDotAndDotDot);
            while (it.hasNext()) { it.next(); out.insert(it.fileName()); }
        }
        return out;
    }();
    return commands;
}

QColor InputHighlighter::commandColor(const QString &word) const {
    if (!m_flagUnknown) return kCommand();
    if (word.contains('/') || word.startsWith('.') || word.contains('$')) return kCommand();
    if (m_known.contains(word) || shellBuiltins().contains(word) || pathCommands().contains(word)) return kCommand();
    return kUnknown();
}

QColor InputHighlighter::colorFor(Destination destination) {
    switch (destination) {
    case Destination::Shell: return kCommand();
    case Destination::Agent: return kAgent();
    case Destination::Auto: break;
    }
    return relay::theme::Text;
}

void InputHighlighter::setDestination(Destination destination) {
    if (destination == m_destination) return;
    m_destination = destination;
    rehighlight();
}

void InputHighlighter::setFlagUnknownCommands(bool flag) {
    if (flag == m_flagUnknown) return;
    m_flagUnknown = flag;
    if (m_destination == Destination::Shell) rehighlight();
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
        setFormat(match.capturedStart(), match.capturedLength(), charFormat(kString()));
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
        if (!inLiteral(match.capturedStart())) setFormat(match.capturedStart(), match.capturedLength(), charFormat(kOperator()));
    }

    // The first word of the line, and of each pipeline stage, is a command name.
    bool expectCommand = true;
    for (auto it = word.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        const int start = match.capturedStart();
        if (inLiteral(start)) continue;
        const QString value = match.captured();
        if (expectCommand && !value.contains('=')) {
            setFormat(start, value.size(), charFormat(commandColor(value), true));
            expectCommand = false;
            continue;
        }
        if (value.startsWith('-') || value.startsWith('+')) setFormat(start, value.size(), charFormat(kFlag()));
        else if (value.contains('/') || value.contains('~') || value.contains('*') || value.contains('?'))
            setFormat(start, value.size(), charFormat(kPath()));
    }
    // A pipe or a semicolon starts a new command word.
    for (int i = 0; i + 1 < text.size(); ++i) {
        if (inLiteral(i)) continue;
        if (text.at(i) == '|' || text.at(i) == ';' || (text.at(i) == '&' && text.at(i + 1) == '&')) {
            static const QRegularExpression next(QStringLiteral("[^\\s|&;<>()]+"));
            const auto match = next.match(text, i + 1);
            if (match.hasMatch())
                setFormat(match.capturedStart(), match.capturedLength(), charFormat(kCommand(), true));
        }
    }

    for (auto it = variable.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), charFormat(kVariable()));
    }
}

void InputHighlighter::highlightAgent(const QString &text) {
    setFormat(0, text.size(), charFormat(relay::theme::Text));
    // Only Relay's own tokens are tinted: an attached path and a slash command.
    static const QRegularExpression token(QStringLiteral("(?:^|\\s)@[^\\s@\"]+|^/[A-Za-z][\\w-]*"));
    for (auto it = token.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        const int lead = match.captured().startsWith(' ') ? 1 : 0;
        setFormat(match.capturedStart() + lead, match.capturedLength() - lead, charFormat(kToken()));
    }
}

}  // namespace relay
