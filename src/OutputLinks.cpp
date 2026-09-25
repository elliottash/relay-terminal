// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OutputLinks.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace relay::links {
namespace {

const QLatin1String kCardScheme("relay://card/");
const QLatin1String kOptionScheme("relay://option/");
const QLatin1String kSessionScheme("relay://session/");
const QLatin1String kTestScheme("relay://test/");
const QLatin1String kPaneScheme("relay://pane/");

// `option:<section>/<row>` and `session:<id>` — the two schemes an agent's answer writes when it
// names something the app can show (#FEJQ; the backend tells it to, `board_chat.py:140`). Both
// spellings arrive, with and without an authority (`option:agent/allow_writes`,
// `option://agent/allow_writes`), because they are typed into prose by a model and Qt puts the
// first in the path and the second in the host; the helper panel's own link handler accepted both
// before card #AGNT retired it, and this keeps that, so every answer already in a transcript
// still links.
//
// Like the card rule, the scheme must *open* a word: the character before it is the start of the
// line or one of the openers below. That is what keeps `https://relay.test/option:a/b` whole —
// a `/` is not an opener, so nothing is claimed inside a URL — and what makes `options:a/b`, with
// its extra letter, no match at all. `<row>` keeps its slashes (`models/provider/glm-coding` is
// one row id), the id does not.
//
// `Mode` does not change them. The #SFZC rule exists because a bare English word looks like a
// path; a scheme word, a colon and a name is Relay's own vocabulary and nothing in program output
// means it by accident, so an answer keeps its links whether the surface printed it as a prose
// block, a recap line or an Activity row.
QString openers() { return QStringLiteral("(?:^|(?<=[\\s\"'`([{<,;]))"); }

const QRegularExpression &optionExpression()
{
    static const QRegularExpression re(
        openers() + QStringLiteral("option:(?://)?([A-Za-z0-9_][A-Za-z0-9_./-]*)"));
    return re;
}

const QRegularExpression &sessionExpression()
{
    static const QRegularExpression re(
        openers() + QStringLiteral("session:(?://)?([A-Za-z0-9_][A-Za-z0-9_.-]*)"));
    return re;
}

// `test:<runner>:<invocation>` (card #3B1B). The id must have its runner prefix — `test:foo` alone
// is ordinary English in a commit message — and keeps the `::` and `/` of a pytest node id; a
// bracket or a quote ends it, so `[name](test:ctest:x)` and `(test:ctest:x)` both stop in time.
const QRegularExpression &testExpression()
{
    static const QRegularExpression re(
        openers() + QStringLiteral("test:(?://)?([a-z][a-z0-9_-]*:[^\\s\"'`<>()\\[\\]{}|]+)"));
    return re;
}

// `pane:<token>`: a session token is a UUID, so the class is the session id's.
const QRegularExpression &paneExpression()
{
    static const QRegularExpression re(
        openers() + QStringLiteral("pane:(?://)?([A-Za-z0-9_][A-Za-z0-9_.-]*)"));
    return re;
}

// The character classes above are deliberately loose at the end — a row id may hold `.` and `-`
// — so a sentence stop, a trailing slash or a dash left hanging comes off here, the way the URL
// stage chops its own. Returns false when nothing is left to name.
bool trimSchemeTail(QString &target)
{
    while (!target.isEmpty() && QStringLiteral("./-,;:!?").contains(target.back()))
        target.chop(1);
    return !target.isEmpty();
}

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
             const QString &path, int line, int column, bool allowSpaces, bool bare = false)
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
    c.bare = bare;
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

QString optionTarget(const QString &section, const QString &row)
{
    return row.isEmpty() ? kOptionScheme + section : kOptionScheme + section + QLatin1Char('/') + row;
}

bool optionOf(const QString &target, QString *section, QString *row)
{
    *section = QString();
    *row = QString();
    if (!target.startsWith(kOptionScheme))
        return false;
    const QString rest = target.mid(kOptionScheme.size());
    if (rest.isEmpty())
        return false;
    const int slash = rest.indexOf(QLatin1Char('/')); // the *first*: a row id keeps its own slashes
    *section = slash < 0 ? rest : rest.left(slash);
    *row = slash < 0 ? QString() : rest.mid(slash + 1);
    return !section->isEmpty();
}

QString sessionTarget(const QString &id) { return kSessionScheme + id; }

QString sessionIdOf(const QString &target)
{
    return target.startsWith(kSessionScheme) ? target.mid(kSessionScheme.size()) : QString();
}

QString testTarget(const QString &id)
{
    return kTestScheme + QString::fromLatin1(QUrl::toPercentEncoding(id));
}

QString testIdOf(const QString &target)
{
    return target.startsWith(kTestScheme) ? QUrl::fromPercentEncoding(target.mid(kTestScheme.size()).toUtf8())
                                          : QString();
}

QString paneTarget(const QString &token) { return kPaneScheme + token; }

QString paneTokenOf(const QString &target)
{
    return target.startsWith(kPaneScheme) ? target.mid(kPaneScheme.size()) : QString();
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

    // 1. `option:` and `session:` (#FEJQ, card #AGNT step 8). First, before the URL stage, because
    //    the `option://sec/row` spelling *is* a URL to that regex and would be claimed whole; here
    //    the opener rule has already decided the span is not inside one.
    struct Scheme {
        const QRegularExpression &re;
        Kind kind;
    };
    for (const Scheme &scheme : {Scheme{optionExpression(), Kind::Option},
                                 Scheme{sessionExpression(), Kind::Session},
                                 Scheme{testExpression(), Kind::Test},
                                 Scheme{paneExpression(), Kind::Pane}}) {
        for (auto it = scheme.re.globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            QString name = m.captured(1);
            if (!trimSchemeTail(name))
                continue;
            // The span is the scheme word plus what is left of the name: `//` included when it was
            // written, so the underline sits under the same text the answer printed.
            const int length = m.capturedStart(1) - m.capturedStart() + name.size();
            if (!claim.free(m.capturedStart(), length))
                continue;
            Candidate c;
            c.start = m.capturedStart();
            c.length = length;
            c.kind = scheme.kind;
            c.text = text.mid(c.start, c.length);
            c.path = name;
            out.append(c);
            claim.take(c.start, c.length);
        }
    }

    // 2. URLs stay URLs. Any scheme, so `foo://bar` is never mistaken for a relative path.
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

    // Saved Relay sessions use 32 hex digits; Claude and Codex use dashed UUIDs. Only whole
    // tokens are offered, and activation checks the index before opening anything. Claim after
    // URLs so a UUID inside one remains part of that URL.
    static const QRegularExpression bareSessionRe(QStringLiteral(
        "(?:^|(?<=[\\s\"'`([{<,;]))([0-9a-fA-F]{32}|[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12})(?![0-9a-zA-Z_/-])"));
    for (auto it = bareSessionRe.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const int at = m.capturedStart(1);
        const QString id = m.captured(1);
        if (!claim.free(at, id.size())) continue;
        Candidate c;
        c.start = at;
        c.length = id.size();
        c.kind = Kind::Session;
        c.text = id;
        c.path = id;
        out.append(c);
        claim.take(at, id.size());
    }

    // 3. Card references: `#K7Q2` in a recap, in the agent's prose, or in a board-activity
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

    // 4. Python tracebacks and unittest output: File "/path/x.py", line 12, in <module>
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

    // 5. tsc, MSVC and Qt Creator style: src/app.ts(12,5): error TS2304
    static const QRegularExpression parenRe(QStringLiteral("([^\\s\"'`()\\[\\]{}<>|,]+)\\((\\d+)(?:,(\\d+))?\\)"));
    for (auto it = parenRe.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        addPath(out, claim, m.capturedStart(), m.capturedLength(), m.captured(), m.captured(1),
                m.captured(2).toInt(), m.captured(3).isEmpty() ? -1 : m.captured(3).toInt(), false);
    }

    // 6. Quoted paths, including names with spaces (`ls` quotes those, so does pytest).
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
        // Quoted without a `/` is as bare as an unquoted word (#SFZC): 'src' in prose is a
        // folder reference the same way `src` is. Tracebacks (stage 3) and file(line,col)
        // (stage 4) are program forms and keep their terminal rule.
        addPath(out, claim, at, length, inner, path, line, column, true, !path.contains(QLatin1Char('/')));
    }

    // 7. Bare tokens: ls output, gcc/clang/cargo `file:line:column`, pytest node ids,
    //    stack frames inside parentheses (the brackets are boundaries). A token with no `/`
    //    is marked `bare`: in prose (#SFZC) it may link only to a file, never to the
    //    directory an English word happens to name — resolve() holds that rule.
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
            addPath(out, claim, i, token.size(), token, path, line, column, true,
                    !path.contains(QLatin1Char('/')));
        i = end;
    }

    std::sort(out.begin(), out.end(), [](const Candidate &a, const Candidate &b) { return a.start < b.start; });
    return out;
}

Target resolve(const Candidate &candidate, const QString &cwd, const QString &home, const Probe &probe,
               const CardLookup &cards, Mode mode)
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
    if (candidate.kind == Kind::Option) {
        // No board, no probe: the answer spelled the row out, so the span is a link wherever it is
        // printed and the host decides whether it can show it. A section on its own is a link too —
        // `option:agent` reveals the page, which is what the helper's own handler did with an
        // empty row.
        const int slash = candidate.path.indexOf(QLatin1Char('/'));
        target.valid = true;
        target.target = slash < 0 ? optionTarget(candidate.path, QString())
                                  : optionTarget(candidate.path.left(slash), candidate.path.mid(slash + 1));
        return target;
    }
    if (candidate.kind == Kind::Session) {
        target.valid = true;
        target.target = sessionTarget(candidate.path);
        return target;
    }
    // Spelled out like `option:`, so no probe: the context on screen decides (card #3B1B).
    if (candidate.kind == Kind::Test) {
        target.valid = true;
        target.target = testTarget(candidate.path);
        return target;
    }
    if (candidate.kind == Kind::Pane) {
        target.valid = true;
        target.target = paneTarget(candidate.path);
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
        // #SFZC: in prose a bare word — one with no `/` — names a folder far too often by
        // accident (`tests`, `docs`, `remote` are all directories relative to the pane) to
        // link; there a folder carries its slash (`tests/`). A bare *file* name keeps its
        // link (owner, 2026-09-20: "file names still link") — it is the commonest openable
        // thing in a reply, and no false positive was ever a file.
        if (mode == Mode::Prose && candidate.bare && entry == Entry::Directory)
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
                    const CardLookup &cards, Mode mode)
{
    QVector<Found> found;
    for (const Candidate &candidate : candidates(text)) {
        const Target target = resolve(candidate, cwd, home, probe, cards, mode);
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
