// SPDX-License-Identifier: AGPL-3.0-or-later
// `relay::agent::Context`, `ContextSpec` and `Action` (card #AGNT step 2): what an agent is about,
// tested without a window, a worker or a widget — which is the point of the interface being
// QtCore-only.
//
// The golden `context` block is the load-bearing case. Those bytes are what `configure` carries to
// `backend/worker.py` (protocol 30.7 and the section card #AGNT step 4 writes), so the C++ and the
// Python are checked against **one written-down shape** rather than against each other; a field
// renamed on either side fails here with the diff in the message.
#include "AgentContext.h"
#include "ArtifactContext.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace relay::agent;

namespace {

// A terminal pane's context: the only one with a shell, and the only one that routes `auto`.
ContextSpec terminalSpec()
{
    ContextSpec spec;
    spec.name = QStringLiteral("terminal");
    spec.agentRole = QStringLiteral("main");
    spec.workspace = QStringLiteral("/home/dev/project");
    spec.persistScope = QStringLiteral("/home/dev/project");
    spec.persistKey = QStringLiteral("s-7f3a");
    spec.shell = true;
    spec.routing = QStringLiteral("auto");
    return spec;
}

// A card's: the helper role, no shell, the board tool scope, and a surface id of its own because a
// tab may have two cards open on one worker.
ContextSpec cardSpec()
{
    ContextSpec spec;
    spec.name = QStringLiteral("card");
    spec.surface = QStringLiteral("card:AGNT");
    spec.agentRole = QStringLiteral("switchboard");
    spec.workspace = QStringLiteral("/home/dev/project");
    spec.scope = QStringLiteral("board");
    spec.persistScope = QStringLiteral("/home/dev/project");
    spec.persistKey = QStringLiteral("AGNT");
    spec.briefKey = QStringLiteral("card");
    spec.briefTitle = QStringLiteral("Card #AGNT");
    spec.routing = QStringLiteral("agent");
    return spec;
}

QString compact(const QJsonObject &json)
{
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

// The card page's row, as `CardContext::actions()` will hand it over.
QList<Action> cardActions()
{
    Action plan;
    plan.key = QStringLiteral("boardPlan");
    plan.letter = QStringLiteral("p");
    plan.label = QStringLiteral("Plan");
    plan.tooltip = QStringLiteral("The agent reads the code and writes the card's plan");
    Action execute;
    execute.key = QStringLiteral("boardExecute");
    execute.letter = QStringLiteral("x");
    execute.label = QStringLiteral("Execute");
    execute.leaves = true;   // it hands the card to a terminal pane
    Action verify;
    verify.key = QStringLiteral("boardVerify");
    verify.letter = QStringLiteral("v");
    verify.label = QStringLiteral("Verify");
    verify.leaves = true;
    return {plan, execute, verify};
}

// A context written the way a host will write one: everything defaulted but the two pure virtuals.
class FakeContext final : public Context {
  public:
    ContextSpec spec() const override { return m_spec; }
    QString placeholder() const override { return QStringLiteral("Ask about this card"); }
    QList<Action> actions() const override { return m_actions; }
    bool resolveLink(const relay::links::Target &target) override
    {
        m_lastLink = target.target;
        return target.kind == relay::links::Kind::Card;
    }
    void turnFinished(const TurnRecord &record) override { m_turns.append(record); }

    ContextSpec m_spec = cardSpec();
    QList<Action> m_actions;
    QString m_lastLink;
    QList<TurnRecord> m_turns;
};

} // namespace

class AgentContextTests : public QObject {
    Q_OBJECT

private slots:
    // ---- construction and defaults ------------------------------------------------------------

    void aFreshSpecIsEmptyAndHasNoShell()
    {
        const ContextSpec spec;
        QVERIFY(spec.name.isEmpty());
        QVERIFY(spec.surface.isEmpty());
        QVERIFY(spec.workspace.isEmpty());
        QVERIFY(spec.scope.isEmpty());
        QVERIFY(spec.routing.isEmpty());
        QVERIFY(!spec.shell);       // only the terminal spawns one
        QVERIFY(!spec.readonly);
        QVERIFY(spec.persistId().isEmpty());   // no key means no store (30.7)
        // No `persist` and no `brief` blocks, and nothing invented for the rest.
        QCOMPARE(compact(spec.toJson()),
                 QStringLiteral(R"({"agent_role":"","name":"","routing":"","scope":"",)"
                                R"("shell":false,"surface":"","workspace":""})"));
        QVERIFY(spec.askFields().isEmpty());
    }

    void aFreshActionIsKeylessEnabledAndStaysOnItsSurface()
    {
        const Action action;
        QVERIFY(!action.keyed());
        QVERIFY(action.enabled);
        QVERIFY(!action.leaves);
        QVERIFY(!action.run);
        QVERIFY(action.fullLabel().isEmpty());
    }

    void aContextDefaultsToNoActionsNoLinksAndNoTurnHandling()
    {
        // The defaults a terminal context takes: it adds nothing to the console's own behaviour.
        class Bare final : public Context {
          public:
            ContextSpec spec() const override { return terminalSpec(); }
            QString placeholder() const override { return QStringLiteral("Type a command"); }
        } bare;
        QVERIFY(bare.actions().isEmpty());
        relay::links::Target card;
        card.valid = true;
        card.kind = relay::links::Kind::Card;
        card.target = QStringLiteral("relay://card/AGNT");
        QVERIFY(!bare.resolveLink(card));   // false: the console opens it the ordinary way
        bare.turnFinished(TurnRecord{});    // a no-op, not a crash
        QCOMPARE(bare.placeholder(), QStringLiteral("Type a command"));
    }

    void aContextTellsItsConsoleWhenItHasMoved()
    {
        FakeContext context;
        int changes = 0;
        context.onChanged = [&changes] { ++changes; };
        context.changed();
        context.m_spec.briefTitle = QStringLiteral("Card #AGNT — executing");
        context.changed();
        QCOMPARE(changes, 2);
        // And a context nobody has taken calls nothing rather than crashing.
        FakeContext orphan;
        orphan.changed();
    }

    // ---- an action's label ---------------------------------------------------------------------

    void aKeyedActionWearsItsLetterAndANarrowRowShedsIt()
    {
        Action check;
        check.key = QStringLiteral("boardChatCheck");
        check.letter = QStringLiteral("k");
        check.label = QStringLiteral("Check");
        QVERIFY(check.keyed());
        QCOMPARE(check.fullLabel(), QStringLiteral("Check (k)"));
        // `fullLabel` is what the button keeps in its property and what a row that runs out of
        // room shortens *from* — it sheds the keys before it cuts a word (CardDetail::fitButtons).
        QCOMPARE(labelWithoutKey(check.fullLabel()), QStringLiteral("Check"));

        Action cleanup;
        cleanup.letter = QStringLiteral("u");
        cleanup.label = QStringLiteral("Clean up");
        QCOMPARE(cleanup.fullLabel(), QStringLiteral("Clean up (u)"));
        QCOMPARE(labelWithoutKey(cleanup.fullLabel()), QStringLiteral("Clean up"));

        // A keyless action's label is its label, and shedding it changes nothing.
        Action tests;
        tests.label = QStringLiteral("Tests");
        QVERIFY(!tests.keyed());
        QCOMPARE(tests.fullLabel(), QStringLiteral("Tests"));
        QCOMPARE(labelWithoutKey(tests.fullLabel()), QStringLiteral("Tests"));

        // A label that merely contains a bracketed word keeps it: only a trailing "(…)" is a key.
        QCOMPARE(labelWithoutKey(QStringLiteral("Planning (a1b2c3d4)")), QStringLiteral("Planning"));
        QCOMPARE(labelWithoutKey(QStringLiteral("Clean (up) now")), QStringLiteral("Clean (up) now"));
        QCOMPARE(labelWithoutKey(QStringLiteral("(x)")), QStringLiteral("(x)"));
    }

    // ---- the letters a row may answer -----------------------------------------------------------

    // The composer's grey text sheds clauses instead of collapsing. `RichEditor` takes the
    // first candidate that fits, so a line and "…" is all-or-nothing: the card page's box, one
    // pixel too narrow for its line, said nothing at all where the owner asked for the three
    // chords (#VZ69). Every rung is a prefix of what the context wrote, so a narrow box can say
    // less but never something else.
    void thePlaceholderSheddsItsClausesRatherThanVanishing()
    {
        const QStringList card = placeholderRungs(
            QStringLiteral("Reply \u2014 Enter discusses, Ctrl+Enter plans, Ctrl+Shift+Enter only comments"));
        QCOMPARE(card, (QStringList{
                           QStringLiteral("Reply \u2014 Enter discusses, Ctrl+Enter plans, Ctrl+Shift+Enter only comments"),
                           QStringLiteral("Reply \u2014 Enter discusses, Ctrl+Enter plans"),
                           QStringLiteral("Reply \u2014 Enter discusses"),
                           QStringLiteral("Reply"),
                           QStringLiteral("\u2026")}));
        // Every rung is a prefix of the line, so none of them can promise anything else.
        for (const QString &rung : card)
            if (rung != QStringLiteral("\u2026")) QVERIFY2(card.first().startsWith(rung), qPrintable(rung));

        // The Board's, which has one comma and one dash.
        QCOMPARE(placeholderRungs(QStringLiteral("Ask the Board agent \u2014 Enter sends, a second prompt queues")),
                 (QStringList{QStringLiteral("Ask the Board agent \u2014 Enter sends, a second prompt queues"),
                              QStringLiteral("Ask the Board agent \u2014 Enter sends"),
                              QStringLiteral("Ask the Board agent"),
                              QStringLiteral("\u2026")}));
        // One short clause: one rung and "…", which is the two-rung ladder this replaces.
        QCOMPARE(placeholderRungs(QStringLiteral("Ask the Options helper\u2026")),
                 (QStringList{QStringLiteral("Ask the Options helper\u2026"), QStringLiteral("\u2026")}));
        // Nothing to say: nothing, so the pane leaves RichEditor's own ladder alone.
        QVERIFY(placeholderRungs(QString()).isEmpty());
        QVERIFY(placeholderRungs(QStringLiteral("   ")).isEmpty());
        // A dash that opens the line is not a clause boundary: "— go" would be a rung of nothing.
        QCOMPARE(placeholderRungs(QStringLiteral("\u2014 go")),
                 (QStringList{QStringLiteral("\u2014 go"), QStringLiteral("\u2026")}));
    }

    void aDuplicateLetterIsRefusedAndItsActionStays()
    {
        Action check;
        check.key = QStringLiteral("boardChatCheck");
        check.letter = QStringLiteral("k");
        check.label = QStringLiteral("Check");
        Action keep = check;           // a second session's button, same letter
        keep.key = QStringLiteral("boardKeep");
        keep.label = QStringLiteral("Keep");
        Action shout = check;
        shout.key = QStringLiteral("boardShout");
        shout.label = QStringLiteral("Shout");
        shout.letter = QStringLiteral("K");   // the same letter in another case
        Action chord;
        chord.key = QStringLiteral("boardChord");
        chord.label = QStringLiteral("Chord");
        chord.letter = QStringLiteral("Ctrl+K");   // not a row letter at all

        const QList<Action> row = withUniqueLetters({check, keep, shout, chord});
        QCOMPARE(row.size(), 4);                       // the *letter* is refused, never the action
        QCOMPARE(row.at(0).letter, QStringLiteral("k"));
        QVERIFY(!row.at(1).keyed());
        QVERIFY(!row.at(2).keyed());
        QVERIFY(!row.at(3).keyed());
        QCOMPARE(row.at(1).label, QStringLiteral("Keep"));   // still on the row, still clickable
        QCOMPARE(row.at(1).fullLabel(), QStringLiteral("Keep"));
    }

    void aLetterFindsItsActionAndADisabledOneAnswersNothing()
    {
        QList<Action> row = withUniqueLetters(cardActions());
        QCOMPARE(row.size(), 3);
        QCOMPARE(actionForLetter(row, QStringLiteral("p")), 0);
        QCOMPARE(actionForLetter(row, QStringLiteral("x")), 1);
        QCOMPARE(actionForLetter(row, QStringLiteral("V")), 2);   // case-insensitive
        QCOMPARE(actionForLetter(row, QStringLiteral("z")), -1);
        QCOMPARE(actionForLetter(row, QString()), -1);
        QCOMPARE(actionForLetter(row, QStringLiteral("px")), -1);
        // Busy: the key can do no more than the mouse can, so a greyed Execute answers nothing.
        row[1].enabled = false;
        QCOMPARE(actionForLetter(row, QStringLiteral("x")), -1);
        QCOMPARE(row.at(1).fullLabel(), QStringLiteral("Execute (x)"));   // the label is unchanged
        QVERIFY(row.at(1).leaves);      // and it still wears the accent outline
        QVERIFY(!row.at(0).leaves);     // Plan stays on the board
    }

    // ---- the block that crosses to the worker -----------------------------------------------------

    void theContextBlockOfATerminalPaneIsTheseBytes()
    {
        QCOMPARE(compact(terminalSpec().toJson()),
                 QStringLiteral(R"({"agent_role":"main","name":"terminal",)"
                                R"("persist":{"key":"s-7f3a","scope":"/home/dev/project"},)"
                                R"("routing":"auto","scope":"","shell":true,"surface":"terminal",)"
                                R"("workspace":"/home/dev/project"})"));
    }

    void theContextBlockOfACardIsTheseBytes()
    {
        QCOMPARE(compact(cardSpec().toJson()),
                 QStringLiteral(R"({"agent_role":"switchboard",)"
                                R"("brief":{"key":"card","title":"Card #AGNT"},"name":"card",)"
                                R"("persist":{"key":"AGNT","scope":"/home/dev/project"},)"
                                R"("routing":"agent","scope":"board","shell":false,)"
                                R"("surface":"card:AGNT","workspace":"/home/dev/project"})"));
    }

    void everySpecRoundTrips()
    {
        for (const ContextSpec &spec : {terminalSpec(), cardSpec(), ContextSpec{}}) {
            const ContextSpec back = ContextSpec::fromJson(spec.toJson());
            QCOMPARE(back.name, spec.name);
            QCOMPARE(back.agentRole, spec.agentRole);
            QCOMPARE(back.workspace, spec.workspace);
            QCOMPARE(back.scope, spec.scope);
            QCOMPARE(back.persistScope, spec.persistScope);
            QCOMPARE(back.persistKey, spec.persistKey);
            QCOMPARE(back.briefKey, spec.briefKey);
            QCOMPARE(back.briefTitle, spec.briefTitle);
            QCOMPARE(back.shell, spec.shell);
            QCOMPARE(back.routing, spec.routing);
            // `surface` comes back filled in even when the host left it empty, because that is
            // what went on the wire: the default *is* the name.
            QCOMPARE(back.surface, spec.surface.isEmpty() ? spec.name : spec.surface);
        }
        // An unknown key is ignored and a missing one leaves its field at the default, so a GUI
        // reading a newer worker's block does not fall over.
        QJsonObject json = cardSpec().toJson();
        json.insert(QStringLiteral("something_later"), 7);
        json.remove(QStringLiteral("brief"));
        const ContextSpec back = ContextSpec::fromJson(json);
        QCOMPARE(back.name, QStringLiteral("card"));
        QVERIFY(back.briefKey.isEmpty());
        QVERIFY(back.briefTitle.isEmpty());
    }

    void aSpecWithNoWorkspaceSaysSoAndStillKeepsItsConversation()
    {
        // A tab with no project attached: board-less, keyed ("", tab), and a supported state.
        ContextSpec spec;
        spec.name = QStringLiteral("options");
        spec.agentRole = QStringLiteral("switchboard");
        spec.persistKey = QStringLiteral("t0a1b2c3d4e5");
        spec.briefKey = QStringLiteral("options");
        spec.briefTitle = QStringLiteral("Options helper");
        spec.routing = QStringLiteral("agent");
        const QJsonObject json = spec.toJson();
        QCOMPARE(json.value(QStringLiteral("workspace")).toString(), QString());
        QCOMPARE(json.value(QStringLiteral("persist")).toObject().value(QStringLiteral("scope")).toString(),
                 QString());
        QCOMPARE(json.value(QStringLiteral("persist")).toObject().value(QStringLiteral("key")).toString(),
                 QStringLiteral("t0a1b2c3d4e5"));
        QVERIFY(!spec.persistId().isEmpty());
    }

    void theSameTabInsideAndOutsideAProjectAreTwoConversations()
    {
        // 30.7: a board-less tab is keyed ("", tab). The scope is part of the identity, so the
        // same tab id must not resolve to the conversation the project's tab is keeping — nor may
        // any pair of (scope, key) be able to spell the same string as another pair.
        ContextSpec loose;
        loose.persistKey = QStringLiteral("t0a1b2c3d4e5");
        ContextSpec attached = loose;
        attached.persistScope = QStringLiteral("/home/dev/project");
        QVERIFY(loose.persistId() != attached.persistId());

        ContextSpec split;                              // the separator cannot be typed into either
        split.persistScope = QStringLiteral("/home/dev");
        split.persistKey = QStringLiteral("project/t0a1b2c3d4e5");
        ContextSpec whole;
        whole.persistScope = QStringLiteral("/home/dev/project");
        whole.persistKey = QStringLiteral("t0a1b2c3d4e5");
        QVERIFY(split.persistId() != whole.persistId());

        // And the move test protocol 30.7 describes: a model swap leaves the conversation where it
        // is, a new tab does not.
        ContextSpec swapped = attached;
        swapped.agentRole = QStringLiteral("main");
        QCOMPARE(swapped.persistId(), attached.persistId());
        QVERIFY(swapped != attached);
        ContextSpec moved = attached;
        moved.persistKey = QStringLiteral("t9f8e7d6c5b4");
        QVERIFY(moved.persistId() != attached.persistId());
    }

    // ---- what rides on the ask -------------------------------------------------------------------

    void theAskCarriesTheSurfaceTheScreenAndNothingElseByDefault()
    {
        ContextSpec spec = cardSpec();
        QCOMPARE(compact(spec.askFields()), QStringLiteral(R"({"surface":"card:AGNT"})"));

        spec.screen = QStringLiteral("  In progress · 4 cards · search: queue  ");
        spec.readonly = true;
        QCOMPARE(compact(spec.askFields()),
                 QStringLiteral(R"({"readonly":true,"screen":"In progress · 4 cards · )"
                                R"(search: queue","surface":"card:AGNT"})"));
        // Neither is in the `context` block: they are the turn's, not the context's.
        const QJsonObject configure = spec.toJson();
        QVERIFY(!configure.contains(QStringLiteral("screen")));
        QVERIFY(!configure.contains(QStringLiteral("readonly")));

        // A host that leaves the surface empty still names one: the context's own name.
        ContextSpec terminal = terminalSpec();
        QCOMPARE(compact(terminal.askFields()), QStringLiteral(R"({"surface":"terminal"})"));
    }

    void anOnScreenHintIsCutAtTwoThousandCharacters()
    {
        ContextSpec spec = cardSpec();
        spec.screen = QString(kScreenLimit + 500, QLatin1Char('r'));
        const QString sent = spec.askFields().value(QStringLiteral("screen")).toString();
        QCOMPARE(sent.size(), kScreenLimit);
        // Whitespace-only is no hint at all.
        spec.screen = QStringLiteral("   \n  ");
        QVERIFY(!spec.askFields().contains(QStringLiteral("screen")));
    }

    // ---- a finished turn ---------------------------------------------------------------------------

    void aFinishedTurnReachesTheContextWithItsProvenance()
    {
        FakeContext context;
        TurnRecord record;
        record.id = QStringLiteral("8f2c1a");
        record.surface = QStringLiteral("card:AGNT");
        record.prompt = QStringLiteral("what is left on this card?");
        record.answer = QStringLiteral("Steps 3 and 5 are not landed yet.");
        record.model = QStringLiteral("kimi-k3");
        record.sessionId = QStringLiteral("20260921-0045");
        record.turnId = QStringLiteral("7");
        record.mode = QStringLiteral("discuss");
        record.outcome = QStringLiteral("done");
        context.turnFinished(record);
        QCOMPARE(context.m_turns.size(), 1);
        // `turn=<session>/<turn>`, the shape board_protocol._card_answer writes on the thread.
        QCOMPARE(context.m_turns.first().turnRef(), QStringLiteral("20260921-0045/7"));

        TurnRecord bare;
        QVERIFY(bare.turnRef().isEmpty());
        QVERIFY(!bare.readonly);
        bare.sessionId = QStringLiteral("20260921-0045");
        QVERIFY(bare.turnRef().isEmpty());   // half a reference is no reference
    }

    void aContextSeesALinkBeforeTheConsoleDoes()
    {
        FakeContext context;
        relay::links::Target card;
        card.valid = true;
        card.kind = relay::links::Kind::Card;
        card.target = QStringLiteral("relay://card/AGNT");
        QVERIFY(context.resolveLink(card));            // handled here, the console stops
        QCOMPARE(context.m_lastLink, QStringLiteral("relay://card/AGNT"));

        relay::links::Target path;
        path.valid = true;
        path.kind = relay::links::Kind::Path;
        path.target = QStringLiteral("/home/dev/project/src/AgentContext.h");
        QVERIFY(!context.resolveLink(path));           // not this context's: the console opens it
        QCOMPARE(context.m_lastLink, path.target);
    }

    void theCardsActionRowIsTheOnePbx1Settled()
    {
        FakeContext context;
        context.m_actions = cardActions();
        const QList<Action> row = withUniqueLetters(context.actions());
        QCOMPARE(row.size(), 3);
        // Plan, then Execute, then Verify — the reading order is the workflow — each with its
        // letter in its label, and the two that leave the board marked as doing so.
        QCOMPARE(row.at(0).fullLabel(), QStringLiteral("Plan (p)"));
        QCOMPARE(row.at(1).fullLabel(), QStringLiteral("Execute (x)"));
        QCOMPARE(row.at(2).fullLabel(), QStringLiteral("Verify (v)"));
        QCOMPARE(row.at(0).key, QStringLiteral("boardPlan"));
        QVERIFY(!row.at(0).leaves);
        QVERIFY(row.at(1).leaves && row.at(2).leaves);
    }

    void anActionRunsWhatItsContextGaveIt()
    {
        int ran = 0;
        Action check;
        check.key = QStringLiteral("boardChatCheck");
        check.letter = QStringLiteral("k");
        check.label = QStringLiteral("Check");
        check.run = [&ran] { ++ran; };
        const QList<Action> row = withUniqueLetters({check});
        const int at = actionForLetter(row, QStringLiteral("k"));
        QCOMPARE(at, 0);
        QVERIFY(row.at(at).run);
        row.at(at).run();
        QCOMPARE(ran, 1);
    }

    // ---- artifact consoles (card #PBZ4) -------------------------------------------------------

    // The golden block of a docked file agent: `file` and `plugin` are additive, so they appear
    // only on this context and every other context's bytes above stay what they were.
    void anArtifactContextNamesItsFileAndPlugin()
    {
        ArtifactContext context;
        context.setFile(QStringLiteral("/home/dev/project/README.md"));
        ArtifactPlugin plugin;
        plugin.id = QStringLiteral("relay.markdown");
        plugin.name = QStringLiteral("Markdown");
        context.setPlugin(plugin);
        context.state = [] {
            ArtifactState s;
            s.line = 12;
            s.column = 3;
            s.dirty = true;
            s.editable = true;
            s.mode = QStringLiteral("markdown source");
            s.firstLine = 10;
            s.lastLine = 11;
            s.selection = QStringLiteral("two lines\nof text");
            return s;
        };
        const ContextSpec spec = context.spec();
        QCOMPARE(compact(spec.toJson()),
                 QStringLiteral(R"({"agent_role":"switchboard","brief":{"key":"artifact","title":"README.md agent"},)"
                                R"("file":"/home/dev/project/README.md","name":"artifact",)"
                                R"("persist":{"key":"file:/home/dev/project/README.md","scope":"helper"},)"
                                R"("plugin":"relay.markdown","routing":"agent","scope":"console","shell":false,)"
                                R"("surface":"file:/home/dev/project/README.md","workspace":""})"));
        QCOMPARE(ContextSpec::fromJson(spec.toJson()).file, spec.file);
        QCOMPARE(ContextSpec::fromJson(spec.toJson()).plugin, spec.plugin);
        QCOMPARE(spec.screen,
                 QStringLiteral("File: /home/dev/project/README.md (markdown source, editing, unsaved edits)\n"
                                "Plugin: Markdown (relay.markdown)\n"
                                "Cursor: line 12, column 3\n"
                                "Selection (lines 10-11):\ntwo lines\nof text"));
        QCOMPARE(spec.askFields().value(QStringLiteral("screen")).toString(), spec.screen);
    }

    void aLongPathIsKeyedByItsDigest()
    {
        const QString path = QStringLiteral("/home/dev/") + QString(200, QLatin1Char('x')) + QStringLiteral(".md");
        const QString key = artifactKey(path, 80);
        QVERIFY(key.startsWith(QStringLiteral("file:sha1:")));
        QCOMPARE(key.size(), 26);
        QCOMPARE(artifactKey(path, 80), key);                                  // stable
        QVERIFY(artifactKey(path + QLatin1Char('y'), 80) != key);
        QCOMPARE(artifactKey(QStringLiteral("/a.md"), 80), QStringLiteral("file:/a.md"));
    }

    void pluginCommandsCollideIntoTheirNamespace()
    {
        ContextCommand outline;
        outline.name = QStringLiteral("outline");
        outline.space = QStringLiteral("markdown");
        outline.prompt = [](const QString &) { return QStringLiteral("o"); };
        ContextCommand model = outline;
        model.name = QStringLiteral("model");                     // a Relay built-in
        ContextCommand bare = outline;
        bare.name = QStringLiteral("help");
        bare.space.clear();                                        // collides, and has nowhere to go
        ContextCommand inert;
        inert.name = QStringLiteral("nothing");                   // neither prompt nor run
        const QList<ContextCommand> offered =
            offeredSlashCommands({outline, model, bare, inert}, {QStringLiteral("Model"), QStringLiteral("help")});
        QCOMPARE(offered.size(), 2);
        QCOMPARE(offered.at(0).name, QStringLiteral("outline"));
        QCOMPARE(offered.at(1).name, QStringLiteral("markdown:model"));
        QCOMPARE(findSlashCommand(offered, QStringLiteral("/outline")), 0);
        QCOMPARE(findSlashCommand(offered, QStringLiteral("markdown:outline")), 0);   // always reachable
        QCOMPARE(findSlashCommand(offered, QStringLiteral("/markdown:model")), 1);
        QCOMPARE(findSlashCommand(offered, QStringLiteral("model")), -1);             // Relay's, not ours
        QCOMPARE(findSlashCommand(offered, QStringLiteral("/")), -1);
    }

    void aManifestsCommandsBecomeEditorActions()
    {
        const QJsonObject manifest = QJsonDocument::fromJson(R"({ )" R"("schema_version": 2, "id": "relay.markdown", "name": "Markdown", )" R"("activation": {"files": ["*.md", "notes.txt"]}, )" R"("commands": [ )" R"({"name": "outline", "description": "Outline it.", "action": {"kind": "prompt", "prompt": "Outline {file}."}, )" R"("when": {"roles": ["editor"]}}, )" R"({"name": "build", "description": "Build.", "action": {"kind": "tool", "tool": "md_build"}}, )" R"({"name": "shellonly", "description": "x", "action": {"kind": "prompt", "prompt": "y"}, "when": {"roles": ["console"]}}, )" R"({"name": "line", "description": "x", "action": {"kind": "program_line", "line": "ls"}}, )" R"({"name": "Bad", "description": "x", "action": {"kind": "prompt", "prompt": "y"}}, )" R"({"name": "tighten", "description": "Tighten.", "args": [{"name": "focus"}, {"name": "style", "required": true}], )" R"("action": {"kind": "prompt", "prompt": "Tighten {file}."}} )" R"(]} )").object();
        const ArtifactPlugin plugin = readPluginManifest(manifest, QStringLiteral("bundled"));
        QVERIFY(plugin.valid());
        QCOMPARE(plugin.space(), QStringLiteral("markdown"));
        QStringList names;
        for (const PluginCommand &c : plugin.commands) names << c.name;
        QCOMPARE(names, QStringList({QStringLiteral("outline"), QStringLiteral("build"), QStringLiteral("tighten")}));
        QCOMPARE(plugin.commands.at(2).args, QStringLiteral("[focus] <style>"));

        ArtifactContext context;
        context.setFile(QStringLiteral("/p/README.md"));
        context.setPlugin(plugin);
        QString sent;
        QVERIFY(context.slashCommands().isEmpty());               // nowhere to send a prompt yet
        context.sendPrompt = [&sent](const QString &text) { sent = text; };
        const QList<ContextCommand> slash = context.slashCommands();
        QCOMPARE(slash.size(), 3);
        QCOMPARE(slash.at(0).group, QStringLiteral("Markdown"));
        QCOMPARE(slash.at(0).prompt(QString()), QStringLiteral("Outline /p/README.md."));
        QCOMPARE(slash.at(1).prompt(QStringLiteral("fast")), QStringLiteral("Run the md_build tool for /p/README.md with: fast"));
        QCOMPARE(slash.at(2).prompt(QStringLiteral("the intro")), QStringLiteral("Tighten /p/README.md.\n\nthe intro"));

        // The row: the plugin's commands with the first free letter of their name, then the
        // editor's own Save (s) and Revert (r), which only light up with something to save.
        context.save = [] { return true; };
        context.revert = [] { return true; };
        const QList<Action> row = withUniqueLetters(context.actions());
        QStringList labels;
        for (const Action &a : row) labels << a.fullLabel();
        QCOMPARE(labels, QStringList({QStringLiteral("Outline (o)"), QStringLiteral("Build (b)"),
                                      QStringLiteral("Tighten (t)"), QStringLiteral("Save (s)"),
                                      QStringLiteral("Revert (r)")}));
        QVERIFY(!row.at(3).enabled && !row.at(4).enabled);        // a clean buffer
        row.at(0).run();
        QCOMPARE(sent, QStringLiteral("Outline /p/README.md."));
        QVERIFY(context.placeholder().startsWith(QStringLiteral("Ask about README.md, or / for Markdown commands")));
    }

    void activationGlobsFollowFnmatch()
    {
        QVERIFY(activationMatches(QStringLiteral("*.md"), QStringLiteral("/p/docs/README.md")));
        QVERIFY(!activationMatches(QStringLiteral("*.md"), QStringLiteral("/p/README.mdx")));
        QVERIFY(activationMatches(QStringLiteral("*.[ch]"), QStringLiteral("/p/x.h")));
        QVERIFY(!activationMatches(QStringLiteral("*.[!ch]"), QStringLiteral("/p/x.h")));
        QVERIFY(activationMatches(QStringLiteral("docs/") + QStringLiteral("*.txt"), QStringLiteral("/p/docs/a.txt"), QStringLiteral("/p")));
        QVERIFY(!activationMatches(QStringLiteral("docs/") + QStringLiteral("*.txt"), QStringLiteral("/q/docs/a.txt"), QStringLiteral("/p")));
        QVERIFY(activationMatches(QStringLiteral("*.md"), QStringLiteral("ssh://host/srv/notes.md")));
    }

    // Discovery, precedence and enablement, against package folders on disk.
    void theFilesPluginIsFoundByPrecedenceAndEnablement()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.path();
        const auto write = [](const QString &file, const QByteArray &bytes) {
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile out(file);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(bytes);
        };
        const QByteArray md = R"({"schema_version": 2, "id": "relay.markdown", "name": "Bundled MD", )" R"("activation": {"files": ["*.md"]}, )" R"("commands": [{"name": "outline", "description": "o", "action": {"kind": "prompt", "prompt": "o"}}]} )";
        write(root + QStringLiteral("/bundled/markdown/plugin.json"), md);
        write(root + QStringLiteral("/bundled/tex/plugin.json"),
              R"({"schema_version": 2, "id": "relay.tex", "name": "TeX", "activation": {"files": ["*.tex"]}})");
        write(root + QStringLiteral("/project/.git/HEAD"), "ref: refs/heads/main\n");
        write(root + QStringLiteral("/project/README.md"), "# hi\n");
        PluginSearch search;
        search.bundled = root + QStringLiteral("/bundled");
        search.global = root + QStringLiteral("/global");
        search.state = root + QStringLiteral("/plugins.json");

        const QString file = root + QStringLiteral("/project/README.md");
        QCOMPARE(pluginForFile(file, search).name, QStringLiteral("Bundled MD"));
        QVERIFY(!pluginForFile(root + QStringLiteral("/project/notes.txt"), search).valid());
        QCOMPARE(pluginForFile(QStringLiteral("ssh://host/srv/a.md"), search).id, QStringLiteral("relay.markdown"));

        // A global package of the same id shadows the bundled one.
        write(root + QStringLiteral("/global/md/plugin.json"),
              QByteArray(md).replace("Bundled MD", "Global MD"));
        QCOMPARE(pluginForFile(file, search).name, QStringLiteral("Global MD"));

        // A project package is not offered until it is enabled for the project, and it holds its
        // id meanwhile — the worker's `discover` keeps the project record and its enablement
        // says no, so the file has no plugin rather than a shadowed one.
        write(root + QStringLiteral("/project/.relay/plugins/md/plugin.json"),
              QByteArray(md).replace("Bundled MD", "Project MD"));
        QVERIFY(!pluginForFile(file, search).valid());
        const QString project = QFileInfo(root + QStringLiteral("/project")).canonicalFilePath();
        const auto state = [&](const QByteArray &origin, bool on) {
            write(search.state, QJsonDocument(QJsonObject{{QStringLiteral("projects"), QJsonObject{{project,
                QJsonObject{{QStringLiteral("relay.markdown"), QJsonObject{{QString::fromLatin1(origin),
                    QJsonObject{{QStringLiteral("enabled"), on}}}}}}}}}}).toJson());
        };
        state("project", true);
        QCOMPARE(pluginForFile(file, search).name, QStringLiteral("Project MD"));
        // …and one switched off leaves the file with no plugin rather than falling through to a
        // shadowed package of the same id.
        state("project", false);
        QVERIFY(!pluginForFile(file, search).valid());
    }

    void aLinkToTheOpenFileLandsInTheEditor()
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("a.md"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        ArtifactContext context;
        context.setFile(path);
        int went = 0;
        context.goToLine = [&went](int line) { went = line; };
        relay::links::Target here;
        here.valid = true;
        here.kind = relay::links::Kind::Path;
        here.target = path;
        here.line = 7;
        QVERIFY(context.resolveLink(here));
        QCOMPARE(went, 7);
        relay::links::Target other = here;
        other.target = tmp.path();
        QVERIFY(!context.resolveLink(other));
    }
};

QTEST_MAIN(AgentContextTests)
#include "agentcontext_test.moc"
