// SPDX-License-Identifier: AGPL-3.0-or-later
// Clickable paths (#YZTK), card references (Switchboard design section 5) and the keyboard walk
// over them (#GWXM): which spans of a line of terminal output are links, what they resolve to
// against a pane's working directory and card index, and how the keyboard cursor moves over the
// ordered list.
//
// The filesystem is a stub (`probe`), so the rules are tested without touching disk: the same
// line is a link in one directory and nothing in another, which is exactly the behaviour the
// cards ask for ("a path that does not exist is not turned into a link"). The board is a stub
// the same way (`cards`), so an id is a link in one pane and plain text in another.
#include "OutputLinks.h"

#include <QMap>
#include <QSet>
#include <QTest>

using namespace relay::links;

namespace {

const QString kCwd = QStringLiteral("/home/dev/project");
const QString kHome = QStringLiteral("/home/dev");

// Absolute paths that "exist": the set that ends with '/' are directories.
Probe probe()
{
    static const QSet<QString> files = {
        QStringLiteral("/home/dev/project/src/main.cpp"),
        QStringLiteral("/home/dev/project/src/app.ts"),
        QStringLiteral("/home/dev/project/src/main.rs"),
        QStringLiteral("/home/dev/project/code.py"),
        QStringLiteral("/home/dev/project/notes.txt"),
        QStringLiteral("/home/dev/project/Makefile"),
        QStringLiteral("/home/dev/project/my file.txt"),
        QStringLiteral("/home/dev/project/tests/test_links.py"),
        QStringLiteral("/home/dev/project/src/Pane.h"),
        QStringLiteral("/home/dev/project/weird:name"),
        QStringLiteral("/home/dev/.bashrc"),
        QStringLiteral("/etc/hosts"),
    };
    static const QSet<QString> dirs = {
        QStringLiteral("/home/dev/project"),
        QStringLiteral("/home/dev/project/src"),
        QStringLiteral("/home/dev/project/tests"),
        QStringLiteral("/home/dev/project/remote"), // the #SFZC false positives: common
        QStringLiteral("/home/dev/project/build"),  // English words that name a directory
        QStringLiteral("/home/dev/project/data"),
        QStringLiteral("/home/dev"),
        QStringLiteral("/etc"),
    };
    return [](const QString &path) {
        if (dirs.contains(path)) return Entry::Directory;
        if (files.contains(path)) return Entry::File;
        return Entry::Missing;
    };
}

// The cards a pane has seen, the way src/main.cpp answers from its `relay::board::Model`.
CardLookup cards()
{
    static const QMap<QString, QString> known = {
        {QStringLiteral("K7Q2"), QStringLiteral("Voice transcription")},
        {QStringLiteral("GWXM"), QStringLiteral("Keyboard jump to output links")},
        {QStringLiteral("NAME"), QString()}, // a card the board knows but cannot name
    };
    return [](const QString &id, QString *title) {
        const auto it = known.constFind(id);
        if (it == known.constEnd())
            return false;
        *title = it.value();
        return true;
    };
}

QVector<Found> found(const QString &text) { return scan(text, kCwd, kHome, probe(), cards()); }

// The `#K7Q2`-shaped spans of a line. candidates() also offers every word as a possible path
// (the probe is what rejects those), so a card rule is read off this and not off the whole list.
QVector<Candidate> cardSpans(const QString &text)
{
    QVector<Candidate> out;
    for (const Candidate &c : candidates(text))
        if (c.kind == Kind::Card) out << c;
    return out;
}

// The `option:`/`session:` spans of a line, read off candidates() for the same reason cardSpans()
// is: every word is offered as a possible path too, and the probe is what rejects those.
QVector<Candidate> schemeSpans(const QString &text)
{
    QVector<Candidate> out;
    for (const Candidate &c : candidates(text))
        if (c.kind == Kind::Option || c.kind == Kind::Session) out << c;
    return out;
}

QStringList targets(const QString &text)
{
    QStringList out;
    for (const Found &f : found(text)) out << f.target.target;
    return out;
}

// The same line scanned the way an agent message is (#SFZC): Mode::Prose.
QVector<Found> proseFound(const QString &text)
{
    return scan(text, kCwd, kHome, probe(), cards(), Mode::Prose);
}

QStringList proseTargets(const QString &text)
{
    QStringList out;
    for (const Found &f : proseFound(text)) out << f.target.target;
    return out;
}

} // namespace

class OutputLinksTests : public QObject {
    Q_OBJECT

private slots:
    // ---- the path formats the card lists ------------------------------------------------

    void absolutePath()
    {
        const auto links = found(QStringLiteral("wrote /etc/hosts and /home/dev/.bashrc"));
        QCOMPARE(links.size(), 2);
        QCOMPARE(links[0].target.target, QStringLiteral("/etc/hosts"));
        QVERIFY(!links[0].target.directory);
        QCOMPARE(links[1].target.target, QStringLiteral("/home/dev/.bashrc"));
    }

    void relativePathResolvesAgainstTheWorkingDirectory()
    {
        QCOMPARE(targets(QStringLiteral("src/main.cpp")), {QStringLiteral("/home/dev/project/src/main.cpp")});
        QCOMPARE(targets(QStringLiteral("./src/main.cpp")), {QStringLiteral("/home/dev/project/src/main.cpp")});
        QCOMPARE(targets(QStringLiteral("../project/src/main.cpp")), {QStringLiteral("/home/dev/project/src/main.cpp")});
        // The same line in another directory is not a link at all.
        QVERIFY(scan(QStringLiteral("src/main.cpp"), QStringLiteral("/tmp"), kHome, probe()).isEmpty());
    }

    void barePathsFromLsOutput()
    {
        const auto links = found(QStringLiteral("code.py  Makefile  notes.txt  src  tests"));
        QCOMPARE(links.size(), 5);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/code.py"));
        QVERIFY(!links[0].target.directory);
        QCOMPARE(links[3].target.target, QStringLiteral("/home/dev/project/src"));
        QVERIFY(links[3].target.directory); // a folder: the explorer pane, not the preview
        QVERIFY(links[4].target.directory);
    }

    void tildePaths()
    {
        QCOMPARE(targets(QStringLiteral("edit ~/.bashrc")), {QStringLiteral("/home/dev/.bashrc")});
        QCOMPARE(targets(QStringLiteral("cd ~")), {QStringLiteral("/home/dev")});
        QCOMPARE(targets(QStringLiteral("~/project/code.py:3")), {QStringLiteral("/home/dev/project/code.py")});
    }

    void fileLineAndColumn()
    {
        // grep -n
        auto links = found(QStringLiteral("notes.txt:42:the matching text"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/notes.txt"));
        QCOMPARE(links[0].target.line, 42);
        QCOMPARE(links[0].target.column, -1);
        // gcc / clang, with the trailing colon
        links = found(QStringLiteral("src/main.cpp:42:17: error: expected ';'"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/src/main.cpp"));
        QCOMPARE(links[0].target.line, 42);
        QCOMPARE(links[0].target.column, 17);
        // cargo
        links = found(QStringLiteral("  --> src/main.rs:4:9"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/src/main.rs"));
        QCOMPARE(links[0].target.line, 4);
        QCOMPARE(links[0].target.column, 9);
    }

    void aNameThatReallyContainsAColonWinsOverFileLine()
    {
        const auto links = found(QStringLiteral("weird:name"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/weird:name"));
        QCOMPARE(links[0].target.line, -1);
    }

    void quotedPathWithSpaces()
    {
        // GNU ls quotes names with spaces on a terminal.
        auto links = found(QStringLiteral("code.py  'my file.txt'  notes.txt"));
        QCOMPARE(links.size(), 3);
        QCOMPARE(links[1].target.target, QStringLiteral("/home/dev/project/my file.txt"));
        // The underline covers the name, not the quotes.
        QCOMPARE(links[1].candidate.text, QStringLiteral("my file.txt"));
        links = found(QStringLiteral("cat \"my file.txt\":7"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/my file.txt"));
        QCOMPARE(links[0].target.line, 7);
    }

    void backslashEscapedSpaces()
    {
        const auto links = found(QStringLiteral("cp my\\ file.txt /tmp"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/my file.txt"));
    }

    void pythonTraceback()
    {
        const QString line = QStringLiteral("  File \"/home/dev/project/code.py\", line 12, in <module>");
        const auto links = found(line);
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/code.py"));
        QCOMPARE(links[0].target.line, 12);
        // The span is the path itself, so the underline sits under it.
        QCOMPARE(line.mid(links[0].candidate.start, links[0].candidate.length),
                 QStringLiteral("/home/dev/project/code.py"));
        // A relative one, single-quoted, resolved against the pane's directory.
        QCOMPARE(targets(QStringLiteral("  File 'code.py', line 3, in main")),
                 {QStringLiteral("/home/dev/project/code.py")});
    }

    void pytestNodeIdsAndFailureLines()
    {
        QCOMPARE(targets(QStringLiteral("FAILED tests/test_links.py::test_scan - AssertionError")),
                 {QStringLiteral("/home/dev/project/tests/test_links.py")});
        const auto links = found(QStringLiteral("tests/test_links.py:88: AssertionError"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.line, 88);
    }

    void tscAndMsvcParentheses()
    {
        const auto links = found(QStringLiteral("src/app.ts(12,5): error TS2304: Cannot find name 'x'."));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/src/app.ts"));
        QCOMPARE(links[0].target.line, 12);
        QCOMPARE(links[0].target.column, 5);
        // The underline covers `src/app.ts(12,5)`.
        QCOMPARE(links[0].candidate.length, 16);
    }

    void eslintStyleOutput()
    {
        // eslint prints the file on its own line, then "line:col  severity  message".
        QCOMPARE(targets(QStringLiteral("/home/dev/project/src/app.ts")),
                 {QStringLiteral("/home/dev/project/src/app.ts")});
        QVERIFY(found(QStringLiteral("  12:5  error  'x' is not defined  no-undef")).isEmpty());
    }

    void stackFrameInsideParentheses()
    {
        const auto links = found(QStringLiteral("    at Object.<anonymous> (/home/dev/project/src/app.ts:12:5)"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/src/app.ts"));
        QCOMPARE(links[0].target.line, 12);
        QCOMPARE(links[0].target.column, 5);
    }

    // ---- Switchboard card references (design section 5) ---------------------------------

    void aKnownCardIdIsALink()
    {
        const QString line = QStringLiteral("◆ #K7Q2 · moved to Needs QA (LLM)");
        const auto links = found(line);
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.kind, Kind::Card);
        QCOMPARE(links[0].target.target, QStringLiteral("relay://card/K7Q2"));
        // The title travels with the link, for the hover tooltip and the walk's status line.
        QCOMPARE(links[0].target.label, QStringLiteral("Voice transcription"));
        // The underline covers `#K7Q2`, the `#` included.
        QCOMPARE(line.mid(links[0].candidate.start, links[0].candidate.length), QStringLiteral("#K7Q2"));
        // A card the board knows but cannot name is still a link.
        QCOMPARE(targets(QStringLiteral("see #NAME")), {QStringLiteral("relay://card/NAME")});
    }

    void anUnknownCardIdIsPlainText()
    {
        QVERIFY(found(QStringLiteral("filed as #ABCD yesterday")).isEmpty());
        // The shape is right, so it is a candidate; only the board's answer makes it a link.
        const auto c = cardSpans(QStringLiteral("filed as #ABCD yesterday"));
        QCOMPARE(c.size(), 1);
        QCOMPARE(c[0].path, QStringLiteral("ABCD"));
        // A pane that has never seen a board links nothing, not even a real id.
        QVERIFY(scan(QStringLiteral("◆ #K7Q2 · done"), kCwd, kHome, probe()).isEmpty());
    }

    void cardIdsAtTheEdgesOfALineAndInsidePunctuation()
    {
        QCOMPARE(targets(QStringLiteral("#K7Q2 is the card")), {QStringLiteral("relay://card/K7Q2")});
        QCOMPARE(targets(QStringLiteral("the card is #K7Q2")), {QStringLiteral("relay://card/K7Q2")});
        QCOMPARE(targets(QStringLiteral("the card is #K7Q2.")), {QStringLiteral("relay://card/K7Q2")});
        QCOMPARE(targets(QStringLiteral("recap (#K7Q2): shipped")), {QStringLiteral("relay://card/K7Q2")});
        QCOMPARE(targets(QStringLiteral("\"#K7Q2\", it said")), {QStringLiteral("relay://card/K7Q2")});
        QCOMPARE(targets(QStringLiteral("#K7Q2, #GWXM")).size(), 2);
        // An item id links as far as its card; the `.a3` is left as text.
        const auto links = found(QStringLiteral("#K7Q2.a3 is the task"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].candidate.length, 5);
        // Crockford is case-insensitive, so a lower-cased reference reaches the same card.
        QCOMPARE(targets(QStringLiteral("see #k7q2")), {QStringLiteral("relay://card/K7Q2")});
    }

    void hashesThatAreNotCardReferences()
    {
        // A shell comment, which is what `#` means in a terminal pane.
        QVERIFY(cardSpans(QStringLiteral("echo hi   # note to self")).isEmpty());
        QVERIFY(cardSpans(QStringLiteral("#!/bin/bash")).isEmpty());
        // Not four characters, or not the whole word.
        QVERIFY(cardSpans(QStringLiteral("#K7Q #K7Q2X sha#K7Q2 ##K7Q2 #K7Q2_x")).isEmpty());
        // I, L, O and U are not Crockford base32, which is what keeps words out.
        QVERIFY(cardSpans(QStringLiteral("#TODO #FAIL #NOTE #BUIL")).isEmpty());
        // An all-digit `#1234` is a GitHub issue, never one of ours.
        QVERIFY(cardSpans(QStringLiteral("fixes #1234")).isEmpty());
        // A fragment inside a URL belongs to the URL.
        const auto links = found(QStringLiteral("https://relay.test/board#K7Q2"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.kind, Kind::Url);
    }

    void cardReferencesAndPathsShareALine()
    {
        const auto links = found(QStringLiteral("#K7Q2 · evidence in src/main.cpp:42"));
        QCOMPARE(links.size(), 2);
        QCOMPARE(links[0].target.kind, Kind::Card);
        QCOMPARE(links[1].target.target, QStringLiteral("/home/dev/project/src/main.cpp"));
        QCOMPARE(links[1].target.line, 42);
    }

    void cardTargetsRoundTrip()
    {
        QCOMPARE(cardTarget(QStringLiteral("K7Q2")), QStringLiteral("relay://card/K7Q2"));
        QCOMPARE(cardIdOf(QStringLiteral("relay://card/K7Q2")), QStringLiteral("K7Q2"));
        QVERIFY(cardIdOf(QStringLiteral("relay://turn/p1/t-41")).isEmpty());
        QVERIFY(cardIdOf(QStringLiteral("/home/dev/project/src/main.cpp")).isEmpty());
    }

    // ---- option: and session: (#FEJQ, card #AGNT step 8) --------------------------------
    //
    // The two schemes an agent's answer writes when it names something the app can show. They
    // need no board and no filesystem — the answer spelled the thing out — so unlike a card they
    // resolve in a pane that has seen nothing, and the host decides whether it can show it.

    void anOptionRowIsALinkWithOrWithoutAnAuthority()
    {
        const QString line = QStringLiteral("Turn it on in option:agent/allow_writes.");
        const auto links = found(line);
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.kind, Kind::Option);
        QCOMPARE(links[0].target.target, QStringLiteral("relay://option/agent/allow_writes"));
        // The underline covers the scheme word too, and stops before the sentence stop.
        QCOMPARE(line.mid(links[0].candidate.start, links[0].candidate.length),
                 QStringLiteral("option:agent/allow_writes"));
        // `option://agent/allow_writes` is the other spelling a model writes, and it reaches the
        // same row rather than the browser.
        const auto authority = found(QStringLiteral("see option://agent/allow_writes"));
        QCOMPARE(authority.size(), 1);
        QCOMPARE(authority[0].target.kind, Kind::Option);
        QCOMPARE(authority[0].target.target, QStringLiteral("relay://option/agent/allow_writes"));
        QCOMPARE(authority[0].candidate.text, QStringLiteral("option://agent/allow_writes"));
        // No board and no probe: a pane that has seen neither still links it.
        QCOMPARE(scan(QStringLiteral("option:agent/allow_writes"), kCwd, kHome, probe()).size(), 1);
    }

    void anOptionRowKeepsItsOwnSlashesAndASectionAloneIsALink()
    {
        // `option:models/provider/glm-coding` is one row: the split is at the *first* slash,
        // which is what RelayWindow's row ids need (`option:models/provider/<preset>`).
        QString section, row;
        QCOMPARE(targets(QStringLiteral("see option:models/provider/glm-coding")),
                 {QStringLiteral("relay://option/models/provider/glm-coding")});
        QVERIFY(optionOf(QStringLiteral("relay://option/models/provider/glm-coding"), &section, &row));
        QCOMPARE(section, QStringLiteral("models"));
        QCOMPARE(row, QStringLiteral("provider/glm-coding"));
        // A section on its own reveals the page — the empty row the helper's handler allowed.
        QCOMPARE(targets(QStringLiteral("it is under option:agent")), {QStringLiteral("relay://option/agent")});
        QVERIFY(optionOf(QStringLiteral("relay://option/agent"), &section, &row));
        QCOMPARE(section, QStringLiteral("agent"));
        QVERIFY(row.isEmpty());
    }

    void aSessionIdIsALinkWithOrWithoutAnAuthority()
    {
        // What `[title](session:<id>)` leaves in the transcript once MarkdownAnsi has drawn it:
        // the label, then the target in dim parentheses.
        const QString line = QStringLiteral("Porting the worker (session:008c2701cc448d7b)");
        const auto links = found(line);
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.kind, Kind::Session);
        QCOMPARE(links[0].target.target, QStringLiteral("relay://session/008c2701cc448d7b"));
        // The closing bracket is not part of the id.
        QCOMPARE(line.mid(links[0].candidate.start, links[0].candidate.length),
                 QStringLiteral("session:008c2701cc448d7b"));
        QCOMPARE(targets(QStringLiteral("resume session://008c2701cc448d7b, it has the log")),
                 {QStringLiteral("relay://session/008c2701cc448d7b")});
    }

    void bareSessionIdsLinkOnlyAsWholeTokens()
    {
        const QString relayId = QStringLiteral("008c2701cc448d7b008c2701cc448d7b");
        const QString guestId = QStringLiteral("3478d988-a618-47c5-a4f4-7196fdc7261f");
        QCOMPARE(targets(QStringLiteral("open ") + relayId + QStringLiteral(" and ") + guestId + QLatin1Char('.')),
                 (QStringList{sessionTarget(relayId), sessionTarget(guestId)}));
        QVERIFY(targets(QStringLiteral("https://example.test/") + guestId).size() == 1);
        QVERIFY(targets(QStringLiteral("prefix") + relayId).isEmpty());
        QVERIFY(targets(relayId + QStringLiteral("suffix")).isEmpty());
    }

    void schemesThatAreNotLinks()
    {
        // The typo: one letter more is not the scheme.
        QVERIFY(schemeSpans(QStringLiteral("options:agent/allow_writes")).isEmpty());
        QVERIFY(schemeSpans(QStringLiteral("sessions:008c2701cc448d7b")).isEmpty());
        // A bare word, and the word followed by a colon and nothing to name.
        QVERIFY(schemeSpans(QStringLiteral("that option is off")).isEmpty());
        QVERIFY(schemeSpans(QStringLiteral("no session: none running")).isEmpty());
        QVERIFY(schemeSpans(QStringLiteral("option: the agent one")).isEmpty());
        // The scheme has to open a word, so nothing is claimed mid-token or inside a URL — and
        // the URL stays one whole link, which is what the opener rule is for.
        QVERIFY(schemeSpans(QStringLiteral("myoption:agent/allow_writes")).isEmpty());
        const auto links = found(QStringLiteral("https://relay.test/docs/option:agent/allow_writes"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.kind, Kind::Url);
        QCOMPARE(links[0].target.target, QStringLiteral("https://relay.test/docs/option:agent/allow_writes"));
    }

    void optionAndSessionShareALineWithTheOtherKinds()
    {
        const auto links = found(QStringLiteral("#K7Q2 · option:agent/allow_writes · src/main.cpp:42"));
        QCOMPARE(links.size(), 3);
        QCOMPARE(links[0].target.kind, Kind::Card);
        QCOMPARE(links[1].target.kind, Kind::Option);
        QCOMPARE(links[2].target.target, QStringLiteral("/home/dev/project/src/main.cpp"));
        // An agent's own message scans in Prose mode; the scheme rule does not change there.
        QCOMPARE(proseTargets(QStringLiteral("it is option:agent/allow_writes")),
                 {QStringLiteral("relay://option/agent/allow_writes")});
    }

    void optionAndSessionTargetsRoundTrip()
    {
        QString section, row;
        QCOMPARE(optionTarget(QStringLiteral("agent"), QStringLiteral("allow_writes")),
                 QStringLiteral("relay://option/agent/allow_writes"));
        QCOMPARE(optionTarget(QStringLiteral("agent"), QString()), QStringLiteral("relay://option/agent"));
        QVERIFY(optionOf(QStringLiteral("relay://option/agent/allow_writes"), &section, &row));
        QCOMPARE(section, QStringLiteral("agent"));
        QCOMPARE(row, QStringLiteral("allow_writes"));
        // Every other target is not one, and the out-parameters are cleared before the refusal.
        QVERIFY(!optionOf(QStringLiteral("relay://card/K7Q2"), &section, &row));
        QVERIFY(section.isEmpty());
        QVERIFY(row.isEmpty());
        QVERIFY(!optionOf(QStringLiteral("relay://option/"), &section, &row));
        QVERIFY(!optionOf(QStringLiteral("/home/dev/project/src/main.cpp"), &section, &row));

        QCOMPARE(sessionTarget(QStringLiteral("008c2701cc448d7b")),
                 QStringLiteral("relay://session/008c2701cc448d7b"));
        QCOMPARE(sessionIdOf(QStringLiteral("relay://session/008c2701cc448d7b")),
                 QStringLiteral("008c2701cc448d7b"));
        QVERIFY(sessionIdOf(QStringLiteral("relay://card/K7Q2")).isEmpty());
        QVERIFY(sessionIdOf(QStringLiteral("relay://option/agent/allow_writes")).isEmpty());
        // A card target is still only a card target, whatever else is now in the enum.
        QVERIFY(cardIdOf(QStringLiteral("relay://option/agent")).isEmpty());
    }

    // ---- what must NOT become a link ----------------------------------------------------

    void urlsStayUrls()
    {
        auto links = found(QStringLiteral("see http://example.com/src/main.cpp for details"));
        QCOMPARE(links.size(), 1);
        QVERIFY(links[0].target.kind == Kind::Url);
        QCOMPARE(links[0].target.target, QStringLiteral("http://example.com/src/main.cpp"));
        // A trailing sentence stop is not part of the URL.
        links = found(QStringLiteral("docs at https://relay.test/x."));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("https://relay.test/x"));
        // Unknown schemes are still URLs, never relative paths.
        const auto c = candidates(QStringLiteral("ssh://host/src"));
        QCOMPARE(c.size(), 1);
        QVERIFY(c[0].kind == Kind::Url);
    }

    void flagsAreNotPaths()
    {
        QVERIFY(candidates(QStringLiteral("--flag -v --output=src/main.cpp")).isEmpty());
        QVERIFY(found(QStringLiteral("ls --color=always -la")).isEmpty());
    }

    void bareNumbersAndVersionsAreNotPaths()
    {
        QVERIFY(candidates(QStringLiteral("42 1.2.3 192.168.0.1 12:30:05 3,4")).isEmpty());
    }

    void proseThatLooksLikeAPathIsNotOne()
    {
        QVERIFY(found(QStringLiteral("choose either a/b or c/d")).isEmpty());
        QVERIFY(found(QStringLiteral("the src/missing.cpp file was deleted")).isEmpty());
    }

    // ---- prose mode (#SFZC): Relay-printed agent messages ----------------------------------
    //
    // An agent reply is prose (a relay://prose/ block, #R2WQ), and there a folder links only
    // when the token carries a `/`: every false positive — tests, docs, remote — is a bare
    // English word that happens to name a directory relative to the pane. A bare *file*
    // name keeps its link (owner, 2026-09-20: "file names still link").

    void proseBareFolderWordsAreNotLinks()
    {
        const QString line = QStringLiteral("the tests and build folders, and the remote one, stay plain text");
        // Program output: the same words are links — an `ls` of extension-less folders is
        // the commonest link-bearing line there is.
        QCOMPARE(found(line).size(), 3);
        // Prose: none of them is.
        QVERIFY(proseFound(line).isEmpty());
    }

    void proseFolderWithItsSlashLinks()
    {
        const auto links = proseFound(QStringLiteral("see tests/ and src/Pane.h for the rest"));
        QCOMPARE(links.size(), 2);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/tests"));
        QVERIFY(links[0].target.directory);
        QCOMPARE(links[1].target.target, QStringLiteral("/home/dev/project/src/Pane.h"));
        QVERIFY(!links[1].target.directory);
    }

    void proseBareFileNamesKeepTheirLinks()
    {
        const QStringList bare = proseTargets(QStringLiteral("fixed notes.txt and the Makefile"));
        QCOMPARE(bare.size(), 2);
        QCOMPARE(bare.at(0), QStringLiteral("/home/dev/project/notes.txt"));
        QCOMPARE(bare.at(1), QStringLiteral("/home/dev/project/Makefile"));
        // file:line works in prose as it does in a compiler's output.
        QCOMPARE(proseTargets(QStringLiteral("notes.txt:42 explains it")),
                 {QStringLiteral("/home/dev/project/notes.txt")});
    }

    void proseQuotedFolderNeedsItsSlashToo()
    {
        QVERIFY(!found(QStringLiteral("look in 'src' first")).isEmpty()); // terminal: a quoted folder links
        QVERIFY(proseFound(QStringLiteral("look in 'src' first")).isEmpty());
        QCOMPARE(proseTargets(QStringLiteral("look in 'src/' first")),
                 {QStringLiteral("/home/dev/project/src")});
    }

    void proseTracebacksAndUrlsKeepTheirRule()
    {
        // A traceback pasted into a reply is a program form: it links as in terminal output.
        QCOMPARE(proseTargets(QStringLiteral("  File \"code.py\", line 3, in main")),
                 {QStringLiteral("/home/dev/project/code.py")});
        QCOMPARE(proseTargets(QStringLiteral("docs at https://relay.test/x")),
                 {QStringLiteral("https://relay.test/x")});
    }

    // ---- resolution and probing ---------------------------------------------------------

    void splitLocationCases()
    {
        QString path;
        int line = 0, column = 0;
        QVERIFY(splitLocation(QStringLiteral("src/main.cpp:42:7"), &path, &line, &column));
        QCOMPARE(path, QStringLiteral("src/main.cpp"));
        QCOMPARE(line, 42);
        QCOMPARE(column, 7);
        QVERIFY(splitLocation(QStringLiteral("a.py:9:"), &path, &line, &column));
        QCOMPARE(path, QStringLiteral("a.py"));
        QCOMPARE(line, 9);
        QCOMPARE(column, -1);
        QVERIFY(splitLocation(QStringLiteral("plain.txt"), &path, &line, &column));
        QCOMPARE(path, QStringLiteral("plain.txt"));
        QCOMPARE(line, -1);
        QVERIFY(!splitLocation(QStringLiteral(":::"), &path, &line, &column));
    }

    void directoriesAreMarked()
    {
        const auto links = found(QStringLiteral("cd /home/dev/project/src"));
        QCOMPARE(links.size(), 1);
        QVERIFY(links[0].target.directory);
    }

    void severalLinksKeepReadingOrder()
    {
        const QString line = QStringLiteral("src/main.cpp:4 https://relay.test/x notes.txt");
        const auto links = found(line);
        QCOMPARE(links.size(), 3);
        QVERIFY(links[0].candidate.start < links[1].candidate.start);
        QVERIFY(links[1].candidate.start < links[2].candidate.start);
        QCOMPARE(links[1].target.kind, Kind::Url);
    }

    // ---- the keyboard cursor (#GWXM) -----------------------------------------------------

    void cursorStartsAtTheNewestLink()
    {
        Cursor cursor;
        cursor.setCount(3);
        QVERIFY(!cursor.active());
        QCOMPARE(cursor.step(-1), 2);
        QVERIFY(cursor.active());
        Cursor forwards;
        forwards.setCount(3);
        QCOMPARE(forwards.step(1), 2); // the first step lands on the newest either way
    }

    void cursorStepsAndWraps()
    {
        Cursor cursor;
        cursor.setCount(3);
        QCOMPARE(cursor.step(-1), 2);
        QCOMPARE(cursor.step(-1), 1);
        QCOMPARE(cursor.step(-1), 0);
        QCOMPARE(cursor.step(-1), 2); // wraps at the oldest
        QCOMPARE(cursor.step(1), 0);  // and at the newest
        QCOMPARE(cursor.step(0), 0);  // a re-read does not move
    }

    void cursorCancelsAndSurvivesAShrinkingList()
    {
        Cursor cursor;
        cursor.setCount(4);
        QCOMPARE(cursor.step(-1), 3);
        cursor.setCount(2);
        QCOMPARE(cursor.index(), 1);
        cursor.cancel();
        QVERIFY(!cursor.active());
        QCOMPARE(cursor.index(), -1);
        cursor.setCount(0);
        QCOMPARE(cursor.step(-1), -1);
        QVERIFY(!cursor.active());
    }
};

QTEST_MAIN(OutputLinksTests)
#include "outputlinks_test.moc"
