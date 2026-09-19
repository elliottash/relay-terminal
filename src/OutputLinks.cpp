// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OutputLinks.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace relay::links {
namespace {

const QLatin1String kCardScheme("relay://card/");

// A Switchboard card reference: `#K7Q2` (design section 5).
//
// Four Crockford-base32 characters, which is what backend/relay_core/board.py mints — no I, L,
// O or U, so barely any English word can be read as an id and a `#word` comment costs nothing
// to reject. `#` must open a word, so a shell comment (`# note`), `sha#K7Q2` and a fragment
// inside a URL (claimed before this runs) are left alone, and the four characters must be the
// whole word, so `#K7Q2X` is not a reference. An all-digit `#1234` is refused too: the ids
// avoid it so GitHub does not autolink them, and a bare issue number in output is not ours.
// An item id (`#K7Q2.a3`, tasks design) links as far as its card — the `.a3` stays text.
// Whether the id is *real* is resolve()'s business, not this one's.
const QRegularExpression &cardExpression()
{
    static const QRegularExpression re(
        QStringLiteral("(?:^|(?<=[\\s\"'`([{<,;]))#([0-9A-HJKMNP-TV-Za-hjkmnp-tv-z]{4})(?![0-9A-Za-z_#])"));
    return re;
}

// Where a bare token ends. Matches the word boundaries the terminal view uses for
// double-click, so hover, click and the keyboard walk all cover the same text.
bool isBoundary(QChar ch)
{
    return ch.isSpace() || QStringLiteral("\"'`()[]{}<>|").contains(ch);
}

// Sentence punctuation a path never ends with. `:` goes too: splitLocation() is what
// reads `file:line:column`, and it tolerates the trailing colon gcc prints.
void trimTrailingPunctuation(QString &token)
{
    while (!token.isEmpty() && QStringLiteral(".,;!?").contains(token.back()))
        token.chop(1);
}

QString unescapeSpaces(const QString &path)
{
    QString out = path;
    out.replace(QLatin1String("\\ "), QLatin1String(" "));
    return out;
}

// A token that could name a file. Everything here is cheap rejection of things that are
// plainly not paths; whether the path exists is decided later, by the probe.
bool looksLikePath(const QString &path, bool allowSpaces)
{
    if (path == QLatin1String("~")) // `cd ~`
        return true;
    if (path.size() < 2 && !path.contains(QLatin1Char('/')))
        return false;
    if (path.startsWith(QLatin1Char('-'))) // --flag, -v
        return false;
    static const QRegularExpression numeric(QStringLiteral("^[0-9.:,]+$"));
    if (numeric.match(path).hasMatch()) // 42, 1.2.3, 192.168.0.1, 12:30
        return false;
    static const QRegularExpression alnum(QStringLiteral("[A-Za-z0-9]"));
    if (!alnum.match(path).hasMatch())
        return false;
    // FOO=bar and --flag=value are not relative paths; /etc/x=y still is.
    if (path.contains(QLatin1Char('=')) && !path.startsWith(QLatin1Char('/'))
        && !path.startsWith(QLatin1Char('.')) && !path.startsWith(QLatin1Char('~')))
        return false;
    static const QRegularExpression forbidden(QStringLiteral("[\\x00-\\x1f*?\"'`<>|;&$]"));
    if (forbidden.match(path).hasMatch())
        return false;
    static const QRegularExpression space(QStringLiteral("\\s"));
    if (!allowSpaces && space.match(path).hasMatch())
        return false;
    return true;
}

struct Claim {
    std::vector<bool> taken;
    bool free(int start, int length) const
    {
        for (int i = start; i < start + length; ++i)
            if (i < 0 || i >= int(taken.size()) || taken[size_t(i)])
                return false;
        return true;
    }
    void take(int start, int length)
    {
        for (int i = std::max(0, start); i < std::min(int(taken.size()), start + length); ++i)
            taken[size_t(i)] = true;
    }
};

void addPath(QVector<Candidate> &out, Claim &claim, int start, int length, const QString &text,
             const QString &path, int line, int column, bool allowSpaces)
{
    if (length <= 0 || !claim.free(start, length))
        return;
    if (!looksLikePath(unescapeSpaces(path), allowSpaces))
        return;
    Candidate c;
    c.start = start;
    c.length = length;
    c.kind = Kind::Path;
    c.text = text;
    c.path = path;
    c.line = line;
    c.column = column;
    out.append(c);
    claim.take(start, length);
}

} // namespace

Probe systemProbe()
{
    return [](const QString &absolutePath) {
        const QFileInfo info(absolutePath);
        if (!info.exists())
            return Entry::Missing;
        return info.isDir() ? Entry::Directory : Entry::File;
    };
}

QString cardTarget(const QString &id) { return kCardScheme + id; }

QString cardIdOf(const QString &target)
{
    return target.startsWith(kCardScheme) ? target.mid(kCardScheme.size()) : QString();
}

bool splitLocation(const QString &token, QString *path, int *line, int *column)
{
    *line = -1;
    *column = -1;
    QString rest = token;
    while (rest.endsWith(QLatin1Char(':')))
        rest.chop(1);
    int numbers[2] = {-1, -1};
    int found = 0;
    static const QRegularExpression digits(QStringLiteral("^[0-9]{1,9}$"));
    while (found < 2) {
        const int colon = rest.lastIndexOf(QLatin1Char(':'));
        if (colon <= 0 || colon + 1 >= rest.size())
            break;
        if (!digits.match(rest.mid(colon + 1)).hasMatch())
            break;
        numbers[found++] = rest.mid(colon + 1).toInt();
        rest.truncate(colon);
    }
    if (found == 1)
        *line = numbers[0];
    else if (found == 2) {
        *column = numbers[0];
        *line = numbers[1];
    }
    *path = rest;
    return !rest.isEmpty();
}

QVector<Candidate> candidates(const QString &text)
{
    QVector<Candidate> out;
    Claim claim{std::vector<bool>(size_t(text.size()), false)};

    // 1. URLs stay URLs. Any scheme, so `foo://bar` is never mistaken for a relative path.
    static const QRegularExpression urlRe(QStringLiteral("[A-Za-z][A-Za-z0-9+.-]*://[^\\s\"'`<>|\\\\^{}]+"));
    for (auto it = urlRe.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        QString url = m.captured();
        while (!url.isEmpty() && QStringLiteral(".,;:!?)]}").contains(url.back()))
            url.chop(1);
        if (url.size() < 6 || !claim.free(m.capturedStart(), url.size()))
            continue;
        Candidate c;
        c.start = m.capturedStart();
        c.length = url.size();
        c.kind = Kind::Url;
        c.text = url;
        out.append(c);
        claim.take(c.start, c.length);
    }

    // 2. Card references: `#K7Q2` in a recap, in the agent's prose, or in a board-activity
    //    line (design section 5). Claimed before the path stages so the bare-token pass cannot
    //    read the span as a relative path, which also means an unknown id is left as plain text
    //    rather than probed as a file called `#ABCD`.
    static const QRegularExpression allDigits(QStringLiteral("^[0-9]+$"));
    for (auto it = cardExpression().globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString id = m.captured(1).toUpper();
        if (allDigits.match(id).hasMatch())
            continue;
        const int at = m.capturedStart(1) - 1; // the `#`
        const int length = id.size() + 1;
        if (!claim.free(at, length))
            continue;
        Candidate c;
        c.start = at;
        c.length = length;
        c.kind = Kind::Card;
        c.text = text.mid(at, length); // as written, so the underline sits under the same text
        c.path = id;
        out.append(c);
        claim.take(at, length);
    }

    // 3. Python tracebacks and unittest output: File "/path/x.py", line 12, in <module>
    static const QRegularExpression pyRe(
        QStringLiteral("\\bFile \"([^\"\\n]+)\", line (\\d+)|\\bFile '([^'\\n]+)', line (\\d+)"));
    for (auto it = pyRe.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const bool doubleQuoted = !m.captured(1).isNull();
        const QString path = doubleQuoted ? m.captured(1) : m.captured(3);
        const int at = doubleQuoted ? m.capturedStart(1) : m.capturedStart(3);
        const int line = (doubleQuoted ? m.captured(2) : m.captured(4)).toInt();
        addPath(out, claim, at, path.size(), path, path, line, -1, true);
    }

    // 4. tsc, MSVC and Qt Creator style: src/app.ts(12,5): error TS2304
    static const QRegularExpression parenRe(QStringLiteral("([^\\s\"'`()\\[\\]{}<>|,]+)\\((\\d+)(?:,(\\d+))?\\)"));
    for (auto it = parenRe.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        addPath(out, claim, m.capturedStart(), m.capturedLength(), m.captured(), m.captured(1),
                m.captured(2).toInt(), m.captured(3).isEmpty() ? -1 : m.captured(3).toInt(), false);
    }

    // 5. Quoted paths, including names with spaces (`ls` quotes those, so does pytest).
    static const QRegularExpression quotedRe(QStringLiteral("\"([^\"\\n]+)\"|'([^'\\n]+)'"));
    for (auto it = quotedRe.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const bool doubleQuoted = !m.captured(1).isNull();
        const QString inner = doubleQuoted ? m.captured(1) : m.captured(2);
        const int at = doubleQuoted ? m.capturedStart(1) : m.capturedStart(2);
        QString path;
        int line = -1, column = -1;
        if (!splitLocation(inner, &path, &line, &column))
            continue;
        int length = inner.size();
        if (line < 0) {
            // "my file.txt":7 — the location sits outside the closing quote.
            static const QRegularExpression after(QStringLiteral("^:(\\d+)(?::(\\d+))?"));
            const QRegularExpressionMatch tail = after.match(text.mid(m.capturedEnd()));
            if (tail.hasMatch()) {
                line = tail.captured(1).toInt();
                column = tail.captured(2).isEmpty() ? -1 : tail.captured(2).toInt();
                length = m.capturedEnd() + tail.capturedLength() - at;
            }
        }
        addPath(out, claim, at, length, inner, path, line, column, true);
    }

    // 6. Bare tokens: ls output, gcc/clang/cargo `file:line:column`, pytest node ids,
    //    stack frames inside parentheses (the brackets are boundaries).
    int i = 0;
    while (i < text.size()) {
        if (isBoundary(text[i]) || !claim.free(i, 1)) {
            ++i;
            continue;
        }
        int end = i;
        while (end < text.size() && claim.free(end, 1)) {
            const QChar ch = text[end];
            // A space escaped with a backslash belongs to the name (`my\ file.txt`).
            if (isBoundary(ch) && !(ch == QLatin1Char(' ') && end > i && text[end - 1] == QLatin1Char('\\')))
                break;
            ++end;
        }
        QString token = text.mid(i, end - i);
        trimTrailingPunctuation(token);
        QString path;
        int line = -1, column = -1;
        if (!token.isEmpty() && splitLocation(token, &path, &line, &column))
            addPath(out, claim, i, token.size(), token, path, line, column, true);
        i = end;
    }

    std::sort(out.begin(), out.end(), [](const Candidate &a, const Candidate &b) { return a.start < b.start; });
    return out;
}

Target resolve(const Candidate &candidate, const QString &cwd, const QString &home, const Probe &probe,
               const CardLookup &cards)
{
    Target target;
    target.kind = candidate.kind;
    if (candidate.kind == Kind::Url) {
        target.valid = true;
        target.target = candidate.text;
        return target;
    }
    if (candidate.kind == Kind::Card) {
        // Only ids the caller's board knows become links; everything else stays the text the
        // program printed, which is what keeps `#ABCD` and shell comments inert.
        QString title;
        if (!cards || !cards(candidate.path, &title))
            return target;
        target.valid = true;
        target.target = cardTarget(candidate.path);
        target.label = title;
        return target;
    }
    struct Attempt {
        QString raw;
        int line;
        int column;
    };
    QVector<Attempt> attempts;
    // A name that really contains a colon wins over the same text read as file:line.
    if (candidate.text != candidate.path)
        attempts.append({candidate.text, -1, -1});
    attempts.append({candidate.path, candidate.line, candidate.column});
    // grep -n prints `file:line:the matching text`, so the token keeps going after the
    // number: take the shortest prefix that ends in `:line[:column]`.
    if (candidate.line < 0) {
        static const QRegularExpression prefix(QStringLiteral("^(.+?):(\\d+)(?::(\\d+))?(?::|$)"));
        const QRegularExpressionMatch m = prefix.match(candidate.path);
        if (m.hasMatch())
            attempts.append({m.captured(1), m.captured(2).toInt(),
                             m.captured(3).isEmpty() ? -1 : m.captured(3).toInt()});
    }
    // pytest node ids: tests/test_x.py::test_case[param]
    const int nodeId = candidate.path.indexOf(QLatin1String("::"));
    if (nodeId > 0)
        attempts.append({candidate.path.left(nodeId), candidate.line, candidate.column});
    for (const Attempt &attempt : attempts) {
        QString path = unescapeSpaces(attempt.raw);
        if (path.isEmpty())
            continue;
        if (path == QLatin1String("~"))
            path = home;
        else if (path.startsWith(QLatin1String("~/")))
            path = home + path.mid(1);
        if (path.isEmpty())
            continue;
        const QString absolute = QDir::cleanPath(QDir::isAbsolutePath(path) ? path : QDir(cwd).filePath(path));
        const Entry entry = probe(absolute);
        if (entry == Entry::Missing)
            continue;
        target.valid = true;
        target.target = absolute;
        target.directory = entry == Entry::Directory;
        target.line = attempt.line;
        target.column = attempt.column;
        return target;
    }
    return target;
}

QVector<Found> scan(const QString &text, const QString &cwd, const QString &home, const Probe &probe,
                    const CardLookup &cards)
{
    QVector<Found> found;
    for (const Candidate &candidate : candidates(text)) {
        const Target target = resolve(candidate, cwd, home, probe, cards);
        if (target.valid)
            found.append({candidate, target});
    }
    return found;
}

void Cursor::setCount(int count)
{
    m_count = std::max(0, count);
    if (m_index >= m_count)
        m_index = m_count - 1;
}

int Cursor::step(int delta)
{
    if (m_count <= 0) {
        m_index = -1;
        return -1;
    }
    if (m_index < 0)
        m_index = m_count - 1; // the newest link, whichever way the first step goes
    else if (delta != 0)
        m_index = ((m_index + delta) % m_count + m_count) % m_count;
    return m_index;
}

} // namespace relay::links
