// SPDX-License-Identifier: AGPL-3.0-or-later
// The GUI-side rules behind aliases (issue G8DK): turning an alias's template into composer fields,
// walking them with Tab, and deciding whether a line the user typed names an alias. The store, the
// file format and the shell-safe substitution live in the worker and are tested in
// tests/test_aliases.py; nothing here runs anything.
#include "Aliases.h"

#include <QTest>

using namespace relay::aliases;

namespace {

Alias squash() {
    Alias alias;
    alias.name = QStringLiteral("squash");
    alias.kind = QStringLiteral("command");
    alias.title = QStringLiteral("Squash the last N commits together");
    alias.text = QStringLiteral("git reset --soft HEAD~{{num_commits}} && git commit -m \"{{message}}\"");
    alias.params = {{QStringLiteral("num_commits"), QStringLiteral("2"), true, {}},
                    {QStringLiteral("message"), {}, false, {}}};
    return alias;
}

Alias review() {
    Alias alias;
    alias.name = QStringLiteral("review");
    alias.kind = QStringLiteral("prompt");
    alias.title = QStringLiteral("Review the diff");
    alias.scope = QStringLiteral("global");
    alias.text = QStringLiteral("Review the changes under {{path}} and list what is wrong.");
    alias.params = {{QStringLiteral("path"), QStringLiteral("."), true, {}}};
    return alias;
}

}  // namespace

class AliasesTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void namesAreLowerCaseAndShort() {
        QVERIFY(validName(QStringLiteral("squash")));
        QVERIFY(validName(QStringLiteral("run-slow-tests")));
        QVERIFY(validName(QStringLiteral("k8s_deploy")));
        QVERIFY(!validName(QString()));
        QVERIFY(!validName(QStringLiteral("Squash")));
        QVERIFY(!validName(QStringLiteral("-leading")));
        QVERIFY(!validName(QStringLiteral("has space")));
        QVERIFY(!validName(QStringLiteral("has/slash")));
        QVERIFY(!validName(QString(40, QLatin1Char('x'))));
    }

    void slugMatchesTheWorker() {
        // The same answers as relay_core.aliases.slug, so a name proposed here and a name
        // proposed there never disagree.
        QCOMPARE(slug(QStringLiteral("Kill the process running on a port")),
                 QStringLiteral("kill-the-process-running-on-a"));
        QCOMPARE(slug(QStringLiteral("!!!")), QStringLiteral("alias"));
        QCOMPARE(slug(QStringLiteral("squash"), {QStringLiteral("squash")}),
                 QStringLiteral("squash-2"));
        QVERIFY(validName(slug(QString(200, QLatin1Char('A')))));
    }

    // ---- fields in the composer ---------------------------------------------------

    void renderPutsDefaultsInAndRemembersWhereTheyAre() {
        const Rendered out = render(squash());
        QCOMPARE(out.text,
                 QStringLiteral("git reset --soft HEAD~2 && git commit -m \"message\""));
        QCOMPARE(out.fields.size(), 2);
        QCOMPARE(out.fields[0].name, QStringLiteral("num_commits"));
        QCOMPARE(out.text.mid(out.fields[0].start, out.fields[0].length), QStringLiteral("2"));
        QVERIFY(out.fields[0].filled);
        QCOMPARE(out.fields[1].name, QStringLiteral("message"));
        QCOMPARE(out.text.mid(out.fields[1].start, out.fields[1].length), QStringLiteral("message"));
        QVERIFY(!out.fields[1].filled);
    }

    void aTemplateWithNoPlaceholdersHasNoFields() {
        Alias alias;
        alias.name = QStringLiteral("undo");
        alias.text = QStringLiteral("git reset HEAD~");
        const Rendered out = render(alias);
        QCOMPARE(out.text, alias.text);
        QVERIFY(out.fields.isEmpty());
        QCOMPARE(nextField(out.fields, 0), -1);
    }

    void aPlaceholderUsedTwiceGetsTwoFields() {
        Alias alias;
        alias.name = QStringLiteral("dup");
        alias.text = QStringLiteral("echo {{x}} {{x}}");
        alias.params = {{QStringLiteral("x"), QStringLiteral("hi"), true, {}}};
        const Rendered out = render(alias);
        QCOMPARE(out.text, QStringLiteral("echo hi hi"));
        QCOMPARE(out.fields.size(), 2);
    }

    void tabWalksTheFieldsAndWraps() {
        const Rendered out = render(squash());
        QCOMPARE(nextField(out.fields, 0), 0);
        QCOMPARE(nextField(out.fields, out.fields[0].start), 1);
        // past the last field, forward wraps to the first
        QCOMPARE(nextField(out.fields, out.text.size()), 0);
        // shift+tab walks back, and wraps to the last
        QCOMPARE(nextField(out.fields, out.text.size(), false), 1);
        QCOMPARE(nextField(out.fields, 0, false), 1);
    }

    void theCaretKnowsWhichFieldItIsIn() {
        const Rendered out = render(squash());
        QCOMPARE(fieldAt(out.fields, out.fields[0].start), 0);
        QCOMPARE(fieldAt(out.fields, out.fields[0].start + 1), 0);
        QCOMPARE(fieldAt(out.fields, 0), -1);
        QCOMPARE(fieldAt(out.fields, out.fields[1].start + 2), 1);
    }

    void typingIntoAFieldMovesTheLaterOnesAlong() {
        Rendered out = render(squash());
        const int caret = setField(out, 0, QStringLiteral("10"));
        QCOMPARE(out.text,
                 QStringLiteral("git reset --soft HEAD~10 && git commit -m \"message\""));
        QCOMPARE(caret, out.fields[0].start + 2);
        QCOMPARE(out.text.mid(out.fields[1].start, out.fields[1].length), QStringLiteral("message"));
        setField(out, 1, QStringLiteral("wip: the fix"));
        QCOMPARE(out.text,
                 QStringLiteral("git reset --soft HEAD~10 && git commit -m \"wip: the fix\""));
    }

    void shrinkingAFieldAlsoMovesTheLaterOnes() {
        Rendered out = render(squash());
        setField(out, 1, QStringLiteral("x"));
        QCOMPARE(out.text.mid(out.fields[1].start, out.fields[1].length), QStringLiteral("x"));
        setField(out, 0, QString());
        QCOMPARE(out.text, QStringLiteral("git reset --soft HEAD~ && git commit -m \"x\""));
        QCOMPARE(out.text.mid(out.fields[1].start, out.fields[1].length), QStringLiteral("x"));
    }

    void theValuesSentToTheWorkerAreWhatTheFieldsHold() {
        Rendered out = render(squash());
        setField(out, 1, QStringLiteral("wip"));
        const auto pairs = values(out);
        QCOMPARE(pairs.size(), 2);
        QCOMPARE(pairs[0].first, QStringLiteral("num_commits"));
        QCOMPARE(pairs[0].second, QStringLiteral("2"));
        QCOMPARE(pairs[1].second, QStringLiteral("wip"));
    }

    void aFieldStillShowingItsOwnNameCountsAsUnfilled() {
        const Alias alias = squash();
        Rendered out = render(alias);
        QCOMPARE(unfilled(alias, out), QStringList{QStringLiteral("message")});
        setField(out, 1, QStringLiteral("wip"));
        QVERIFY(unfilled(alias, out).isEmpty());
        setField(out, 1, QString());
        QCOMPARE(unfilled(alias, out), QStringList{QStringLiteral("message")});
    }

    void valuesAreReadBackOutOfAnEditedComposer() {
        Rendered out = render(squash());
        // The user types over both fields by hand, the way they would in the composer.
        const QString typed =
            QStringLiteral("git reset --soft HEAD~10 && git commit -m \"wip: it's fine\"");
        QVERIFY(reparse(out, typed));
        const auto pairs = values(out);
        QCOMPARE(pairs[0].second, QStringLiteral("10"));
        QCOMPARE(pairs[1].second, QStringLiteral("wip: it's fine"));
    }

    void anEmptiedFieldIsStillAField() {
        Rendered out = render(squash());
        QVERIFY(reparse(out, QStringLiteral("git reset --soft HEAD~ && git commit -m \"\"")));
        QCOMPARE(values(out)[0].second, QString());
        // Only the parameter with no default blocks: emptying one that has a default is a choice
        // the user made, not a blank they forgot.
        QCOMPARE(unfilled(squash(), out), QStringList{QStringLiteral("message")});
    }

    void rewritingTheCommandItselfStopsItBeingAnAlias() {
        Rendered out = render(squash());
        const QString before = out.text;
        QVERIFY(!reparse(out, QStringLiteral("rm -rf / # not the alias any more")));
        QVERIFY(!reparse(out, QStringLiteral("git reset --hard HEAD~2 && git commit -m \"x\"")));
        QCOMPARE(out.text, before);   // left alone, so the caller can route the line normally
    }

    void aTrailingFieldRunsToTheEndOfTheLine() {
        Alias alias;
        alias.name = QStringLiteral("say");
        alias.text = QStringLiteral("echo {{words}}");
        alias.params = {{QStringLiteral("words"), QStringLiteral("hi"), true, {}}};
        Rendered out = render(alias);
        QVERIFY(reparse(out, QStringLiteral("echo hello there, world")));
        QCOMPARE(values(out)[0].second, QStringLiteral("hello there, world"));
    }

    void aTemplateWithNoFieldsReparsesOnlyWhenUnchanged() {
        Alias alias;
        alias.name = QStringLiteral("undo");
        alias.text = QStringLiteral("git reset HEAD~");
        Rendered out = render(alias);
        QVERIFY(reparse(out, QStringLiteral("git reset HEAD~")));
        QVERIFY(!reparse(out, QStringLiteral("git reset HEAD~2")));
    }

    // ---- recognising an invocation ------------------------------------------------

    void slashRunsAnAliasByName() {
        const QStringList known{QStringLiteral("squash"), QStringLiteral("review")};
        auto out = matchSlash(QStringLiteral("/squash"), known);
        QVERIFY(out.matched);
        QVERIFY(out.viaSlash);
        QCOMPARE(out.name, QStringLiteral("squash"));
        QVERIFY(out.args.isEmpty());

        out = matchSlash(QStringLiteral("/squash 3 wip: the fix"), known);
        QVERIFY(out.matched);
        QCOMPARE(out.args, QStringLiteral("3 wip: the fix"));
    }

    void slashNeverShadowsABuiltInCommand() {
        const QStringList known{QStringLiteral("model")};
        const QStringList reserved{QStringLiteral("model"), QStringLiteral("skills")};
        QVERIFY(!matchSlash(QStringLiteral("/model gpt"), known, reserved).matched);
        QVERIFY(!matchSlash(QStringLiteral("/skills"), known, reserved).matched);
        QVERIFY(!matchSlash(QStringLiteral("/unknown"), known, reserved).matched);
        QVERIFY(!matchSlash(QStringLiteral("squash"), known).matched);
        QVERIFY(!matchSlash(QStringLiteral("/"), known).matched);
    }

    void typingTheNameInTerminalModeRunsIt() {
        const QStringList known{QStringLiteral("squash"), QStringLiteral("gs")};
        auto out = matchTyped(QStringLiteral("squash 3"), known);
        QVERIFY(out.matched);
        QVERIFY(!out.viaSlash);
        QCOMPARE(out.name, QStringLiteral("squash"));
        QCOMPARE(out.args, QStringLiteral("3"));

        out = matchTyped(QStringLiteral("  gs  "), known);
        QVERIFY(out.matched);
        QVERIFY(out.args.isEmpty());
    }

    void aTypedLineThatIsNotAnAliasIsLeftAlone() {
        const QStringList known{QStringLiteral("squash"), QStringLiteral("gs")};
        QVERIFY(!matchTyped(QStringLiteral("git status"), known).matched);
        QVERIFY(!matchTyped(QStringLiteral("squashfs-tools"), known).matched);
        QVERIFY(!matchTyped(QStringLiteral("./squash"), known).matched);
        QVERIFY(!matchTyped(QStringLiteral("/usr/bin/squash"), known).matched);
        QVERIFY(!matchTyped(QStringLiteral("~/squash"), known).matched);
        QVERIFY(!matchTyped(QStringLiteral("!squash"), known).matched);   // forced terminal prefix
        QVERIFY(!matchTyped(QStringLiteral("*squash"), known).matched);   // forced agent prefix
        QVERIFY(!matchTyped(QStringLiteral("#G8DK squash"), known).matched);
        QVERIFY(!matchTyped(QStringLiteral("gs=1 squash"), known).matched);
        QVERIFY(!matchTyped(QString(), known).matched);
        QVERIFY(!matchTyped(QStringLiteral("   "), known).matched);
    }

    void aShadowedGlobalAliasIsNotAName() {
        Alias local = squash();
        Alias global = squash();
        global.scope = QStringLiteral("global");
        global.shadowed = true;
        QCOMPARE(names({local, global}), QStringList{QStringLiteral("squash")});
        Alias other = review();
        QCOMPARE(names({local, global, other}),
                 (QStringList{QStringLiteral("squash"), QStringLiteral("review")}));
    }

    // ---- palette and hints --------------------------------------------------------

    void thePaletteRowSaysWhatItIsAndWhereItIsFrom() {
        QCOMPARE(paletteDetail(squash()),
                 QStringLiteral("command · this project · 2 parameters · /squash"));
        QCOMPARE(paletteDetail(review()),
                 QStringLiteral("prompt · global · 1 parameter · /review"));
    }

    void runningFromThePaletteTeachesTheFastPath() {
        QCOMPARE(fastPathHint(squash()),
                 QStringLiteral("Next time: type /squash, or just squash in terminal mode"));
        // A prompt only has the slash path: typing its name in terminal mode is a shell command.
        QCOMPARE(fastPathHint(review()), QStringLiteral("Next time: type /review"));
        Alias nameless;
        QVERIFY(fastPathHint(nameless).isEmpty());
    }
};

QTEST_MAIN(AliasesTests)
#include "aliases_test.moc"
