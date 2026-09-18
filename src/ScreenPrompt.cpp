// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScreenPrompt.h"

#include <QRegularExpression>

#include <algorithm>

namespace relay::screen {
namespace {

// Screens come from an emulator, so they are already plain text; a stray escape sequence or
// control byte would still confuse the patterns (and the banner), so drop them.
QString sanitize(const QString &row) {
    static const QRegularExpression escapes(
        QStringLiteral("\x1B(?:\\[[0-9;?]*[ -/]*[@-~]|\\][^\x07\x1B]*(?:\x07|\x1B\\\\)?|[@-Z\\\\-_])"));
    QString text = row;
    text.remove(escapes);
    QString out;
    out.reserve(text.size());
    for (const QChar c : std::as_const(text)) {
        if (c == QLatin1Char('\t')) out.append(QLatin1Char(' '));
        else if (c.isPrint() || c.isSpace()) out.append(c);
    }
    return out;
}

QString rstrip(const QString &row) {
    int end = row.size();
    while (end > 0 && row.at(end - 1).isSpace()) --end;
    return row.left(end);
}

bool hasLetter(const QString &text) {
    return std::any_of(text.cbegin(), text.cend(), [](QChar c) { return c.isLetter(); });
}

// The question shown in the banner: one line, collapsed, elided from the left when very long
// (the end of the line is the part that asks).
QString shorten(const QString &line) {
    const QString text = line.simplified();
    if (text.size() <= kQuestionChars) return text;
    return QStringLiteral("…") + text.right(kQuestionChars - 1);
}

// The shell's own prompt. A trailing `$`, `#`, `%` or a Powerline-ish arrow, with something
// path- or host-shaped in front of it, or a prompt that is only the sigil. Deliberately narrow:
// `>>> ` (python) and `relay=# ` (psql) are programs waiting for a line, not shell prompts.
bool shellPrompt(const QString &line) {
    if (line.isEmpty()) return false;
    static const QString sigils = QStringLiteral("$#%❯➜»");
    if (!sigils.contains(line.back())) return false;
    if (line.size() <= 2) return true;   // "$", "# ", "❯"
    const QString head = line.left(line.size() - 1);
    static const QString shellish = QStringLiteral("@:~/-");
    return std::any_of(head.cbegin(), head.cend(), [](QChar c) { return shellish.contains(c); });
}

// "[sudo] password for elliott:", "Enter passphrase for key '…':", "Password:".
bool passwordLine(const QString &line) {
    static const QRegularExpression pattern(
        QStringLiteral("(?:^|[^A-Za-z])(?:password|passphrase|pass phrase)\\b[^:]{0,60}:\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(line).hasMatch();
}

// The last bracketed option list on the line: "[Y/n]", "(yes/no)", "(yes/no/[fingerprint])".
// Returns an empty string when there is none, or when real text follows it (so a sentence that
// merely mentions "(read/write)" in the middle of a paragraph does not count).
QString optionToken(const QString &line, QString *defaultAnswer) {
    static const QRegularExpression token(
        QStringLiteral("[\\[(]\\s*([A-Za-z]+(?:\\s*/\\s*(?:\\[[A-Za-z ]+\\]|[A-Za-z?]+))+)\\s*[\\])]"));
    static const QRegularExpression trailer(QStringLiteral("^[\\s?:.]*$"));
    QRegularExpressionMatch best;
    auto matches = token.globalMatch(line);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (trailer.match(line.mid(match.capturedEnd())).hasMatch()) best = match;
    }
    if (!best.hasMatch()) return {};
    if (defaultAnswer) {
        // The capitalized alternative is the one Enter picks: "[Y/n]" defaults to yes.
        for (const QString &part : best.captured(1).split(QLatin1Char('/'))) {
            const QString option = part.trimmed();
            if (option.isEmpty() || option.size() > 3 || !hasLetter(option)) continue;
            if (option == option.toUpper() && option != option.toLower()) {
                if (!defaultAnswer->isEmpty()) { defaultAnswer->clear(); break; }  // ambiguous
                *defaultAnswer = option.toLower();
            }
        }
    }
    return best.captured(0);
}

// "Press <enter> to keep the current choice[*], or type selection number:", "Your choice:".
bool choiceLine(const QString &line) {
    static const QRegularExpression pattern(
        QStringLiteral("\\b(?:selection|choice|option|number)\\b[^:]{0,40}:\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(line).hasMatch();
}

// A numbered menu above the question: "  1. All", "* 0   /usr/bin/vim.basic", "[3] custom".
bool numberedOption(const QString &line) {
    static const QRegularExpression pattern(QStringLiteral("^\\s*[*>]?\\s*\\[?\\d{1,3}\\]?[).:]?\\s+\\S"));
    return pattern.match(line).hasMatch();
}

bool pressKeyLine(const QString &line) {
    static const QRegularExpression pattern(
        QStringLiteral("(?:press|hit)\\s+(?:the\\s+)?[<\\[]?(?:enter|return|any\\s+key|space)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression pager(QStringLiteral("^--\\s*More\\s*--|\\(END\\)$"),
                                          QRegularExpression::CaseInsensitiveOption);
    return pattern.match(line).hasMatch() || pager.match(line).hasMatch();
}

// Any other line that ends where a program would leave the cursor waiting for a line. A short
// line needs no words: ">>> " (python), ":" (less) and "relay=# " (psql) are whole prompts.
bool freeTextLine(const QString &line) {
    static const QString enders = QStringLiteral(":?>#»");
    return !line.isEmpty() && enders.contains(line.back()) && (hasLetter(line) || line.size() <= 8);
}

double clamp(double value) { return std::clamp(value, 0.0, 1.0); }

// The /proc signals decide how much the pattern is worth. A pattern alone is a guess; canonical
// (line) input says the program really is reading lines, and a process blocked in read() on the
// terminal is proof. `sudo` hides the reader, so `programReading` is often false for the very
// cases this classifier exists for — hence it is a bonus, not a requirement.
double score(double base, const Signals &sig, bool password) {
    double value = base;
    if (password) {
        if (sig.mode == relay::input::TerminalMode::Secret) value += 0.20;
    } else if (sig.mode == relay::input::TerminalMode::Echoing) {
        value += 0.15;
    }
    if (sig.programReading) value += 0.15;
    if (!sig.programRunning) value -= 0.30;
    return clamp(value);
}

}  // namespace

bool isShellPrompt(const QString &line) { return shellPrompt(line.trimmed()); }

const char *kindName(Kind kind) {
    switch (kind) {
    case Kind::None: return "none";
    case Kind::ShellPrompt: return "shell_prompt";
    case Kind::YesNo: return "yes_no";
    case Kind::Choice: return "choice";
    case Kind::Password: return "password";
    case Kind::PressKey: return "press_key";
    case Kind::FreeText: return "free_text";
    }
    return "none";
}

QStringList lastRows(const QString &screen, int count) {
    if (screen.isEmpty() || count <= 0) return {};
    QStringList rows = screen.split(QLatin1Char('\n'));
    // A screen is as tall as the pane, so a three-line command leaves forty blank rows under it.
    // "The last rows" means the last rows with something on them.
    while (!rows.isEmpty() && rows.constLast().trimmed().isEmpty()) rows.removeLast();
    if (rows.size() > count) rows = rows.mid(rows.size() - count);
    return rows;
}

Detection detect(const QStringList &rows, const Signals &sig) {
    Detection detection;
    // No screen to read (KonsolePart on KF5): the /proc signals are all Relay has, and they are
    // the behaviour that shipped before this classifier existed.
    if (!sig.screenReadable || rows.isEmpty()) {
        if (!sig.programRunning || sig.altScreen) return detection;
        if (sig.mode == relay::input::TerminalMode::Secret) {
            detection.kind = Kind::Password;
            detection.masked = true;
            detection.confidence = 0.90;
        } else if (sig.mode == relay::input::TerminalMode::Echoing && sig.programReading) {
            detection.kind = Kind::FreeText;
            detection.confidence = 0.75;
        }
        return detection;
    }
    // A full-screen program draws its own interface; "the last line" means nothing there, and a
    // line answer would be wrong anyway. The take-control button already covers that case.
    if (sig.altScreen) return detection;

    QStringList tail;
    for (const QString &row : rows.mid(std::max(0, rows.size() - kInspectRows)))
        tail.append(rstrip(sanitize(row)));
    while (!tail.isEmpty() && tail.constLast().isEmpty()) tail.removeLast();
    if (tail.isEmpty()) return detection;

    const QString line = tail.constLast();
    const QString above = tail.size() > 1 ? tail.at(tail.size() - 2) : QString();

    // A password first: it is the one case where being wrong is expensive.
    if (passwordLine(line)
        || (sig.mode == relay::input::TerminalMode::Secret && sig.programRunning)) {
        detection.kind = Kind::Password;
        detection.masked = true;
        detection.question = shorten(line);
        detection.confidence = score(passwordLine(line) ? 0.80 : 0.70, sig, true);
        return detection;
    }
    QString defaultAnswer;
    const QString options = optionToken(line, &defaultAnswer);
    if (!options.isEmpty()) {
        detection.kind = Kind::YesNo;
        detection.options = options;
        detection.defaultAnswer = defaultAnswer;
        // "Do you want to continue?" on one row and "[Y/n]" on the next: read them together.
        detection.question = hasLetter(QString(line).remove(options)) || above.isEmpty()
            ? shorten(line) : shorten(above + QLatin1Char(' ') + line);
        detection.confidence = score(0.80, sig, false);
        return detection;
    }
    if (choiceLine(line)) {
        const bool menu = std::any_of(tail.cbegin(), tail.cend(), numberedOption);
        detection.kind = Kind::Choice;
        detection.question = shorten(line);
        detection.confidence = score(menu ? 0.75 : 0.55, sig, false);
        return detection;
    }
    if (pressKeyLine(line)) {
        detection.kind = Kind::PressKey;
        detection.question = shorten(line);
        detection.confidence = score(0.70, sig, false);
        return detection;
    }
    if (shellPrompt(line)) {
        detection.kind = Kind::ShellPrompt;
        detection.question = shorten(line);
        return detection;
    }
    if (freeTextLine(line)) {
        detection.kind = Kind::FreeText;
        detection.question = shorten(line);
        detection.confidence = score(0.45, sig, false);
        return detection;
    }
    return detection;
}

QString bannerText(const QString &program, const Detection &detection) {
    if (!detection.waiting()) return {};
    const QString who = program.isEmpty() ? QStringLiteral("The program") : program;
    switch (detection.kind) {
    case Kind::Password:
        return QStringLiteral("%1 is asking for a password").arg(who);
    case Kind::PressKey:
        return QStringLiteral("%1 is waiting: %2").arg(who, detection.question);
    default:
        break;
    }
    return detection.question.isEmpty() ? QStringLiteral("%1 is asking for input").arg(who)
                                        : QStringLiteral("%1 is asking: %2").arg(who, detection.question);
}

QString waitingLine(const QString &program, const Detection &detection) {
    if (!detection.waiting()) return {};
    const QString who = program.isEmpty() ? QStringLiteral("The program") : program;
    if (detection.kind == Kind::Password)
        return QStringLiteral("%1 is asking for a password · type it here, it never reaches Relay").arg(who);
    if (!detection.actionable())
        return QStringLiteral("%1 may be waiting for input").arg(who);
    if (detection.kind == Kind::PressKey)
        return QStringLiteral("%1 · Enter sends an empty line to it").arg(bannerText(program, detection));
    return QStringLiteral("%1 · Enter sends your answer to it").arg(bannerText(program, detection));
}

}  // namespace relay::screen
