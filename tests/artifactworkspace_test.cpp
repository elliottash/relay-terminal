// SPDX-License-Identifier: AGPL-3.0-or-later
// The artifact workspace model (card #E85D): the group's JSON and schema, members that dangle after
// a pane closed or moved, the output state rules (file watching and builder statuses), the source
// revision the TeX builder also computes, the preview adapter registry and the layout presets.
// No window: src/ArtifactWorkspace is QtCore only.
#include "ArtifactWorkspace.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

using namespace relay::workspace;

namespace {
Group texGroup() {
    Group g;
    g.id = QStringLiteral("g1");
    g.kind = QStringLiteral("relay.tex");
    g.root = QStringLiteral("/p");
    g.layout = QStringLiteral("2:1");
    g.setMember(QStringLiteral("m-console"), Role::Console);
    g.setMember(QStringLiteral("m-editor"), Role::Editor);
    g.setMember(QStringLiteral("m-preview"), Role::Preview);
    g.addSource(QStringLiteral("/p/main.tex"));
    Output &pdf = g.ensureOutput(QStringLiteral("/p/main.pdf"), QStringLiteral("pdf"), Authority::Generated);
    pdf.generation = 4;
    pdf.sourceRevision = QStringLiteral("abc");
    pdf.state = OutputState::Live;
    pdf.signature = QStringLiteral("100:20");
    return g;
}

bool writeFile(const QString &path, const QByteArray &data, qint64 mtimeMs) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(data);
    file.close();
    if (!file.open(QIODevice::ReadWrite)) return false;
    return file.setFileTime(QDateTime::fromMSecsSinceEpoch(mtimeMs), QFileDevice::FileModificationTime);
}
}  // namespace

class ArtifactWorkspaceTests : public QObject {
    Q_OBJECT
private slots:
    void groupRoundTripsThroughJson() {
        const Group g = texGroup();
        const QJsonObject json = g.toJson();
        QCOMPARE(json.value(QStringLiteral("schema")).toInt(), kSchemaVersion);
        QCOMPARE(json.value(QStringLiteral("members")).toObject().value(QStringLiteral("m-editor")).toString(),
                 QStringLiteral("editor"));
        QString error;
        const Group back = Group::fromJson(json, &error);
        QVERIFY2(back.isValid(), qPrintable(error));
        QCOMPARE(back.id, g.id);
        QCOMPARE(back.kind, g.kind);
        QCOMPARE(back.root, g.root);
        QCOMPARE(back.layout, g.layout);
        QCOMPARE(back.members, g.members);
        QCOMPARE(back.sources, g.sources);
        QCOMPARE(back.outputs.size(), 1);
        const Output &pdf = back.outputs.first();
        QCOMPARE(pdf.path, QStringLiteral("/p/main.pdf"));
        QCOMPARE(pdf.adapter, QStringLiteral("pdf"));
        QCOMPARE(pdf.authority, Authority::Generated);
        QCOMPARE(pdf.generation, 4);
        QCOMPARE(pdf.sourceRevision, QStringLiteral("abc"));
        QCOMPARE(pdf.state, OutputState::Live);
        QCOMPARE(pdf.signature, QStringLiteral("100:20"));
        QCOMPARE(back.toJson(), json);
    }

    void unreadableGroupsAreRefused() {
        QString error;
        QVERIFY(!Group::fromJson({}, &error).isValid());
        QJsonObject newer = texGroup().toJson();
        newer.insert(QStringLiteral("schema"), kSchemaVersion + 1);
        QVERIFY(!Group::fromJson(newer, &error).isValid());
        QVERIFY(error.contains(QStringLiteral("schema")));
        QJsonObject noId = texGroup().toJson();
        noId.remove(QStringLiteral("id"));
        QVERIFY(!Group::fromJson(noId, &error).isValid());
    }

    void unknownRolesAndBadLayoutsAreDroppedNotFatal() {
        QJsonObject json = texGroup().toJson();
        QJsonObject members = json.value(QStringLiteral("members")).toObject();
        members.insert(QStringLiteral("m-odd"), QStringLiteral("sidebar"));
        json.insert(QStringLiteral("members"), members);
        json.insert(QStringLiteral("layout"), QStringLiteral("wide"));
        const Group g = Group::fromJson(json);
        QVERIFY(g.isValid());
        QCOMPARE(g.members.size(), 3);
        QVERIFY(!g.roleOf(QStringLiteral("m-odd")));
        QVERIFY(g.layout.isEmpty());
    }

    void aBuildRunningAtQuitDoesNotComeBackBuilding() {
        Group g = texGroup();
        g.outputs[0].state = OutputState::Building;
        QCOMPARE(Group::fromJson(g.toJson()).outputs.first().state, OutputState::Stale);
        g.outputs[0].generation = 0;
        QCOMPARE(Group::fromJson(g.toJson()).outputs.first().state, OutputState::Idle);
    }

    void aRoleHasOneMember() {
        Group g = texGroup();
        g.setMember(QStringLiteral("m-other"), Role::Editor);
        QCOMPARE(g.memberFor(Role::Editor), QStringLiteral("m-other"));
        QVERIFY(!g.roleOf(QStringLiteral("m-editor")));
        QCOMPARE(g.members.size(), 3);
    }

    // A pane that closed or moved to another tab is no longer a member; the group itself stays,
    // and nothing in it names a pane that is not there.
    void danglingMembersAreDroppedAndTheGroupKept() {
        Group g = texGroup();
        QVERIFY(g.reconcile({QStringLiteral("m-console"), QStringLiteral("m-preview")}));
        QVERIFY(g.isValid());
        QCOMPARE(g.members.size(), 2);
        QVERIFY(g.memberFor(Role::Editor).isEmpty());
        QVERIFY(!g.toJson().value(QStringLiteral("members")).toObject().contains(QStringLiteral("m-editor")));
        QVERIFY(!g.reconcile({QStringLiteral("m-console"), QStringLiteral("m-preview")}));
        // Every pane gone: an empty group, still the tab's.
        QVERIFY(g.reconcile({}));
        QVERIFY(g.members.isEmpty());
        QVERIFY(Group::fromJson(g.toJson()).isValid());
    }

    // Ctrl+Shift+Z brings a closed pane back with its member id: it takes its role back, unless
    // the role was given to another pane in the meantime.
    void aRestoredPaneTakesItsRoleBack() {
        Group g = texGroup();
        g.reconcile({QStringLiteral("m-console"), QStringLiteral("m-preview")});
        QVERIFY(g.reconcile({QStringLiteral("m-console"), QStringLiteral("m-preview"), QStringLiteral("m-editor")}));
        QCOMPARE(g.memberFor(Role::Editor), QStringLiteral("m-editor"));

        g.reconcile({QStringLiteral("m-console"), QStringLiteral("m-preview")});
        g.setMember(QStringLiteral("m-new"), Role::Editor);
        g.reconcile({QStringLiteral("m-console"), QStringLiteral("m-preview"), QStringLiteral("m-new"),
                     QStringLiteral("m-editor")});
        QCOMPARE(g.memberFor(Role::Editor), QStringLiteral("m-new"));
        QVERIFY(!g.roleOf(QStringLiteral("m-editor")));
    }

    void departedMembersAreNotSaved() {
        Group g = texGroup();
        g.reconcile({QStringLiteral("m-console")});
        const Group back = Group::fromJson(g.toJson());
        QVERIFY(back.departed.isEmpty());
        QCOMPARE(back.members.size(), 1);
    }

    // ----- output states ---------------------------------------------------------------------

    void aNewOutputFileIsANewLiveGeneration() {
        Output out;
        Observation now{QStringLiteral("2000:10"), 2000, 1500, QStringLiteral("r1")};
        QVERIFY(observe(out, now));
        QCOMPARE(out.generation, 1);
        QCOMPARE(out.state, OutputState::Live);
        QCOMPARE(out.sourceRevision, QStringLiteral("r1"));
        QVERIFY(!observe(out, now));
    }

    // The stale rule: a source saved after the output marks it stale, and the output (its
    // generation and file) stays what it was until a new file arrives.
    void savingASourceMakesTheLastOutputStaleUntilANewOneArrives() {
        Output out;
        observe(out, {QStringLiteral("2000:10"), 2000, 1500, QStringLiteral("r1")});
        QVERIFY(observe(out, {QStringLiteral("2000:10"), 2000, 2500, QStringLiteral("r2")}));
        QCOMPARE(out.state, OutputState::Stale);
        QCOMPARE(out.generation, 1);
        QCOMPARE(out.signature, QStringLiteral("2000:10"));
        // Saved back to what it was built from: live again, no new generation.
        QVERIFY(observe(out, {QStringLiteral("2000:10"), 2000, 2600, QStringLiteral("r1")}));
        QCOMPARE(out.state, OutputState::Live);
        QCOMPARE(out.generation, 1);
        observe(out, {QStringLiteral("2000:10"), 2000, 2700, QStringLiteral("r3")});
        QVERIFY(observe(out, {QStringLiteral("3000:12"), 3000, 2700, QStringLiteral("r3")}));
        QCOMPARE(out.generation, 2);
        QCOMPARE(out.state, OutputState::Live);
        QCOMPARE(out.sourceRevision, QStringLiteral("r3"));
    }

    // An output already on disk that is older than a source cannot claim to show it.
    void anOutputOlderThanItsSourcesArrivesStale() {
        Output out;
        observe(out, {QStringLiteral("1000:10"), 1000, 1500, QStringLiteral("r1")});
        QCOMPARE(out.generation, 1);
        QCOMPARE(out.state, OutputState::Stale);
        QVERIFY(out.sourceRevision.isEmpty());
        // And it stays stale whatever the revision, because what it was built from is unknown.
        observe(out, {QStringLiteral("1000:10"), 1000, 1500, QStringLiteral("r1")});
        QCOMPARE(out.state, OutputState::Stale);
    }

    void noOutputYetIsIdle() {
        Output out;
        QVERIFY(!observe(out, {QString(), 0, 1500, QStringLiteral("r1")}));
        QCOMPARE(out.state, OutputState::Idle);
        QCOMPARE(out.generation, 0);
    }

    // tex_build.BuildStatus.to_dict(), as the TeX workspace will hand it over.
    void builderStatusesDriveStateAndKeepTheLastGoodGeneration() {
        Output out;
        const QJsonObject gen7{{QStringLiteral("id"), 7}, {QStringLiteral("revision"), QStringLiteral("r7")}};
        QVERIFY(applyBuildStatus(out, {{QStringLiteral("state"), QStringLiteral("live")}, {QStringLiteral("seq"), 1},
                                       {QStringLiteral("generation"), gen7}}, QStringLiteral("r7")));
        QCOMPARE(out.state, OutputState::Live);
        QCOMPARE(out.generation, 7);
        QVERIFY(out.builder);

        QVERIFY(applyBuildStatus(out, {{QStringLiteral("state"), QStringLiteral("building")}, {QStringLiteral("seq"), 2},
                                       {QStringLiteral("generation"), gen7}, {QStringLiteral("building"), 8}}));
        QCOMPARE(out.state, OutputState::Building);
        QCOMPARE(out.building, 8);
        QCOMPARE(out.generation, 7);

        const QJsonObject failed{{QStringLiteral("revision"), QStringLiteral("r8")},
                                 {QStringLiteral("reason"), QStringLiteral("3 errors")}};
        QVERIFY(applyBuildStatus(out, {{QStringLiteral("state"), QStringLiteral("failed")}, {QStringLiteral("seq"), 3},
                                       {QStringLiteral("generation"), gen7}, {QStringLiteral("failed"), failed}}));
        QCOMPARE(out.state, OutputState::Failed);
        QCOMPARE(out.message, QStringLiteral("3 errors"));
        QCOMPARE(out.generation, 7);           // the last good PDF stays
        QCOMPARE(out.sourceRevision, QStringLiteral("r7"));

        // An older status (a late completion) is dropped.
        QVERIFY(!applyBuildStatus(out, {{QStringLiteral("state"), QStringLiteral("live")}, {QStringLiteral("seq"), 2},
                                        {QStringLiteral("generation"), gen7}}));
        QCOMPARE(out.state, OutputState::Failed);

        // "live" for a revision that is no longer the saved one is stale.
        QVERIFY(applyBuildStatus(out, {{QStringLiteral("state"), QStringLiteral("live")}, {QStringLiteral("seq"), 4},
                                       {QStringLiteral("generation"), gen7}}, QStringLiteral("r9")));
        QCOMPARE(out.state, OutputState::Stale);
        // A file change is the builder's to report: observe() does not start a generation for it.
        QVERIFY(!observe(out, {QStringLiteral("5:5"), 5, 1, QStringLiteral("r9")}));
        QCOMPARE(out.generation, 7);
        QVERIFY(observe(out, {QStringLiteral("5:5"), 5, 1, QStringLiteral("r7")}));
        QCOMPARE(out.state, OutputState::Live);
    }

    void unknownBuilderStatesAreIgnored() {
        Output out;
        QVERIFY(!applyBuildStatus(out, {{QStringLiteral("state"), QStringLiteral("exploded")}, {QStringLiteral("seq"), 1}}));
        QVERIFY(!out.builder);
    }

    // ----- the source revision is tex_build's ------------------------------------------------

    void sourceRevisionMatchesTheTexBuilder() {
        // backend: source_revision({'/p/main.tex': b'\\documentclass{article}\n',
        //                           '/p/ch/intro.tex': b'hello\n', '/p/refs.bib': None}, '/p')
        QMap<QString, std::optional<QByteArray>> contents;
        contents.insert(QStringLiteral("/p/main.tex"), QByteArray("\\documentclass{article}\n"));
        contents.insert(QStringLiteral("/p/ch/intro.tex"), QByteArray("hello\n"));
        contents.insert(QStringLiteral("/p/refs.bib"), std::nullopt);
        QCOMPARE(sourceRevision(contents, QStringLiteral("/p")), QStringLiteral("c2eaa01909c99d47de63"));
        QCOMPARE(sourceRevision({}, QStringLiteral("/p")), QStringLiteral("e3b0c44298fc1c149afb"));
    }

    void sourceRevisionOnDiskFollowsSavedContents() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = dir.filePath(QStringLiteral("a.md"));
        QVERIFY(writeFile(a, "one\n", 1000));
        const QString r1 = sourceRevisionOnDisk({a}, dir.path());
        QVERIFY(writeFile(a, "two\n", 2000));
        const QString r2 = sourceRevisionOnDisk({a}, dir.path());
        QVERIFY(r1 != r2);
        QVERIFY(writeFile(a, "one\n", 3000));
        QCOMPARE(sourceRevisionOnDisk({a}, dir.path()), r1);   // contents, not the clock
        QCOMPARE(fileSignature(a), QStringLiteral("3000:4"));
        QVERIFY(fileSignature(dir.filePath(QStringLiteral("missing"))).isEmpty());
    }

    // ----- adapters --------------------------------------------------------------------------

    void builtInAdaptersCoverTheExistingViews() {
        const AdapterRegistry registry;
        QCOMPARE(registry.resolve(QStringLiteral("/p/README.md"))->id, QStringLiteral("markdown"));
        QCOMPARE(registry.resolve(QStringLiteral("/p/fig.PNG"))->id, QStringLiteral("image"));
        QCOMPARE(registry.resolve(QStringLiteral("/p/main.pdf"))->id, QStringLiteral("pdf"));
        QVERIFY(!registry.resolve(QStringLiteral("/p/main.tex")));
        QCOMPARE(registry.adapterFor(QStringLiteral("/p/main.tex"))->id, QStringLiteral("text"));
        QCOMPARE(registry.resolve(QStringLiteral("/p/blob"), QString(), QStringLiteral("image/x-portable-pixmap"))->id,
                 QStringLiteral("image"));
        // Generated outputs are read only; Markdown is its own source.
        QCOMPARE(registry.byId(QStringLiteral("pdf"))->authority, Authority::Generated);
        QVERIFY(!registry.byId(QStringLiteral("pdf"))->canEdit);
        QVERIFY(!registry.byId(QStringLiteral("image"))->canEdit);
        QCOMPARE(registry.byId(QStringLiteral("markdown"))->authority, Authority::Editable);
        QVERIFY(registry.byId(QStringLiteral("markdown"))->canEdit);
        // The PDF adapter says whether this build can render one.
        QCOMPARE(registry.byId(QStringLiteral("pdf"))->available, pdfPreviewBuiltIn());
        if (!pdfPreviewBuiltIn()) QVERIFY(!registry.byId(QStringLiteral("pdf"))->unavailableReason.isEmpty());
    }

    // A plugin's adapter wins in its own groups only; everything else still resolves as before.
    void aPluginAdapterWinsInItsOwnGroups() {
        AdapterRegistry registry;
        registry.add({QStringLiteral("diagram"), QStringLiteral("Diagram"), {QStringLiteral("svg")}, {},
                      QStringLiteral("relay.diagram"), Authority::Editable, true, true, QString()});
        QCOMPARE(registry.resolve(QStringLiteral("/p/a.svg"), QStringLiteral("relay.diagram"))->id, QStringLiteral("diagram"));
        QCOMPARE(registry.resolve(QStringLiteral("/p/a.svg"), QStringLiteral("relay.tex"))->id, QStringLiteral("image"));
        QCOMPARE(registry.resolve(QStringLiteral("/p/a.svg"))->id, QStringLiteral("image"));
        QCOMPARE(registry.resolve(QStringLiteral("/p/a.pdf"), QStringLiteral("relay.diagram"))->id, QStringLiteral("pdf"));
        // Registering the same id and kind again replaces it.
        const int count = int(registry.adapters().size());
        registry.add({QStringLiteral("diagram"), QStringLiteral("Diagram v2"), {QStringLiteral("svg")}, {},
                      QStringLiteral("relay.diagram"), Authority::Generated, false, true, QString()});
        QCOMPARE(int(registry.adapters().size()), count);
        QCOMPARE(registry.byId(QStringLiteral("diagram"), QStringLiteral("relay.diagram"))->label, QStringLiteral("Diagram v2"));
    }

    void sourcesSuggestAKindAndAnOutput() {
        QCOMPARE(kindForSource(QStringLiteral("/p/main.tex")), QStringLiteral("relay.tex"));
        QCOMPARE(kindForSource(QStringLiteral("/p/notes.md")), QStringLiteral("plain"));
        QCOMPARE(defaultOutputFor(QStringLiteral("/p/paper.v2.tex")), QStringLiteral("/p/paper.v2.pdf"));
        QCOMPARE(defaultOutputFor(QStringLiteral("/p/notes.md")), QStringLiteral("/p/notes.md"));
    }

    // ----- presets -----------------------------------------------------------------------------

    void presetsPlaceTheRolesAsAsked() {
        QCOMPARE(presetWeights(QStringLiteral("1:1:1")), (QList<int>{1, 1, 1}));
        QCOMPARE(presetWeights(QStringLiteral("2:1")), (QList<int>{2, 1}));
        QVERIFY(presetWeights(QStringLiteral("0:1")).isEmpty());
        QVERIFY(presetWeights(QStringLiteral("wide")).isEmpty());
        // Terminal | TeX | PDF.
        QCOMPARE(presetColumns(QStringLiteral("1:1:1")),
                 (QList<QList<Role>>{{Role::Console}, {Role::Editor}, {Role::Preview}}));
        // TeX over terminal on the left, PDF on the right.
        QCOMPARE(presetColumns(QStringLiteral("2:1")),
                 (QList<QList<Role>>{{Role::Editor, Role::Console}, {Role::Preview}}));
        QVERIFY(presetColumns(QStringLiteral("x")).isEmpty());
    }

    void weightedSizesSumToTheTotal() {
        QCOMPARE(weightedSizes({1, 1, 1}, 1000), (QList<int>{334, 333, 333}));
        QCOMPARE(weightedSizes({2, 1}, 900), (QList<int>{600, 300}));
        QVERIFY(weightedSizes({}, 900).isEmpty());
        QVERIFY(weightedSizes({1, 1}, 0).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ArtifactWorkspaceTests)
#include "artifactworkspace_test.moc"
