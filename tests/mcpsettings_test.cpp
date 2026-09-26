// SPDX-License-Identifier: AGPL-3.0-or-later
// Options › Security › MCP servers (card #9M96): the rows drawn from `mcp_config list --json`,
// which CLI call each button makes, and the Add form keeping env values out of argv. The CLI
// itself is tested in tests/test_mcp.py (OptionsCliTests).
#include "McpSettings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using relay::SettingRow;

namespace {

QJsonObject listed() {
    // Single quotes, turned into double ones below: Qt 5's moc loses the class after a raw string.
    QByteArray text =
        "{"
        "      'global_path': '/cfg/relay/mcp-servers.json', 'project_path': '/ws/.mcp.json',"
        "      'problems': ['/cfg/relay/mcp-servers.json: server `x` needs a command (stdio) or a url (HTTP).'],"
        "      'servers': ["
        "        {'name': 'gh', 'origin': 'global', 'transport': 'stdio', 'command': 'gh-mcp --stdio', 'url': '',"
        "         'env': ['GH_TOKEN'], 'headers': [], 'trust': 'trusted', 'enabled': true, 'source': '/cfg'},"
        "        {'name': 'docs', 'origin': 'project', 'transport': 'http', 'command': '', 'url': 'https://d/mcp',"
        "         'env': [], 'headers': ['Authorization'], 'trust': 'untrusted', 'enabled': true, 'source': '/ws/.mcp.json'},"
        "        {'name': 'repo', 'origin': 'project', 'transport': 'stdio', 'command': 'node srv.js', 'url': '',"
        "         'env': [], 'headers': [], 'trust': '', 'enabled': false, 'pending': true, 'source': '/ws/.mcp.json'},"
        "        {'name': 'off', 'origin': 'global', 'transport': '', 'command': '', 'url': '',"
        "         'env': [], 'headers': [], 'trust': '', 'enabled': false, 'pending': false, 'source': '/cfg'}"
        "      ]}";
    text.replace('\'', '"').replace('`', '\'');
    return QJsonDocument::fromJson(text).object();
}

const SettingRow *find(const QList<SettingRow> &rows, const QString &id) {
    for (const SettingRow &row : rows)
        if (row.id == id) return &row;
    return nullptr;
}

}  // namespace

class McpSettingsTest : public QObject {
    Q_OBJECT

private slots:
    void rowsAndButtons() {
        QList<QPair<QStringList, QString>> calls;
        int adds = 0, imports = 0;
        relay::mcp::Hooks hooks;
        hooks.run = [&](const QStringList &args, const QString &confirm) { calls.append({args, confirm}); };
        hooks.add = [&] { ++adds; };
        hooks.import = [&] { ++imports; };
        const auto rows = relay::mcp::rowsFor(listed(), QStringLiteral("/ws"), hooks);

        const SettingRow *gh = find(rows, QStringLiteral("mcp:global:gh"));
        QVERIFY(gh);
        QCOMPARE(gh->buttonTexts, (QStringList{"Untrust", "Disable", "Remove"}));
        QVERIFY(gh->detail.startsWith(QStringLiteral("trusted")));
        QVERIFY(gh->detail.contains(QStringLiteral("gh-mcp --stdio")));
        QVERIFY(gh->detail.contains(QStringLiteral("env: GH_TOKEN")));
        QVERIFY2(gh->agentSafeButtons.isEmpty(), "no agent may trust, enable or remove a server");
        gh->onButton(0);
        QCOMPARE(calls.last().first, (QStringList{"trust", "gh", "untrusted", "--workspace", "/ws"}));
        QVERIFY2(calls.last().second.isEmpty(), "untrusting needs no confirmation");
        gh->onButton(1);
        QCOMPARE(calls.last().first, (QStringList{"disable", "--global", "gh"}));
        gh->onButton(2);
        QCOMPARE(calls.last().first, (QStringList{"remove", "gh"}));
        QVERIFY(!calls.last().second.isEmpty());

        const SettingRow *docs = find(rows, QStringLiteral("mcp:project:docs"));
        QVERIFY(docs);
        QCOMPARE(docs->buttonTexts, (QStringList{"Trust", "Disable"}));
        docs->onButton(0);
        QCOMPARE(calls.last().first, (QStringList{"trust", "docs", "trusted", "--workspace", "/ws"}));
        QVERIFY2(calls.last().second.contains(QStringLiteral("without asking")), "trusting asks first");
        docs->onButton(1);
        QCOMPARE(calls.last().first, (QStringList{"disable", "docs", "--workspace", "/ws"}));

        const SettingRow *repo = find(rows, QStringLiteral("mcp:project:repo"));
        QVERIFY(repo);
        QVERIFY(repo->detail.startsWith(QStringLiteral("not enabled")));
        QCOMPARE(repo->buttonTexts, (QStringList{"Enable", "Enable as trusted"}));
        repo->onButton(1);
        QCOMPARE(calls.last().first, (QStringList{"enable", "repo", "--trust", "trusted", "--workspace", "/ws"}));
        QVERIFY2(calls.last().second.contains(QStringLiteral("node srv.js")),
                 "enabling a project server shows the command it would launch");

        const SettingRow *off = find(rows, QStringLiteral("mcp:global:off"));
        QVERIFY(off);
        QCOMPARE(off->buttonTexts, (QStringList{"Enable", "Remove"}));
        off->onButton(0);
        QCOMPARE(calls.last().first, (QStringList{"enable", "--global", "off"}));

        QVERIFY(find(rows, QStringLiteral("mcp:problem:0")));
        const SettingRow *manage = find(rows, QStringLiteral("mcp:manage"));
        QVERIFY(manage);
        manage->onButton(0);
        manage->onButton(1);
        QCOMPARE(adds, 1);
        QCOMPARE(imports, 1);
    }

    void emptyLoadingAndError() {
        relay::mcp::Hooks hooks;
        auto rows = relay::mcp::rowsFor({}, {}, hooks);
        QVERIFY(find(rows, QStringLiteral("mcp:none")));
        rows = relay::mcp::rowsFor({}, {}, hooks, true);
        QVERIFY(find(rows, QStringLiteral("mcp:status")));
        QVERIFY(!find(rows, QStringLiteral("mcp:none")));
        rows = relay::mcp::rowsFor({}, {}, hooks, false, QStringLiteral("boom"));
        QVERIFY(find(rows, QStringLiteral("mcp:status"))->label.contains(QStringLiteral("boom")));
    }

    void addFormKeepsValuesOutOfArgv() {
        relay::mcp::AddForm form;
        form.name = QStringLiteral("gh");
        form.target = QStringLiteral("npx -y \"@scope/server gh\" --stdio");
        form.pairs = QStringList{QStringLiteral("GH_TOKEN=sekrit"), QString(), QStringLiteral("MODE=a=b")};
        QByteArray input;
        QString problem;
        const QStringList args = relay::mcp::addArguments(form, &input, &problem);
        QCOMPARE(args, (QStringList{"add", "gh", "--command=npx", "--arg=-y", "--arg=@scope/server gh",
                                    "--arg=--stdio", "--trust", "untrusted", "--secrets-stdin"}));
        QVERIFY(!args.join(' ').contains(QStringLiteral("sekrit")));
        const QJsonObject sent = QJsonDocument::fromJson(input).object();
        QCOMPARE(sent.value("env").toObject().value("GH_TOKEN").toString(), QStringLiteral("sekrit"));
        QCOMPARE(sent.value("env").toObject().value("MODE").toString(), QStringLiteral("a=b"));

        form.url = true;
        form.target = QStringLiteral("https://d/mcp");
        form.pairs = QStringList{QStringLiteral("Authorization=Bearer x")};
        form.trusted = true;
        const QStringList url = relay::mcp::addArguments(form, &input, &problem);
        QVERIFY(url.contains(QStringLiteral("--url=https://d/mcp")));
        QVERIFY(url.contains(QStringLiteral("trusted")));
        QVERIFY(QJsonDocument::fromJson(input).object().contains(QStringLiteral("headers")));

        form.target = QStringLiteral("ftp://nope");
        QVERIFY(relay::mcp::addArguments(form, &input, &problem).isEmpty());
        QVERIFY(problem.contains(QStringLiteral("http")));
        form.name = QStringLiteral("bad name");
        QVERIFY(relay::mcp::addArguments(form, &input, &problem).isEmpty());
        form.name = QStringLiteral("ok");
        form.url = false;
        form.target = QStringLiteral("srv");
        form.pairs = QStringList{QStringLiteral("novalue")};
        QVERIFY(relay::mcp::addArguments(form, &input, &problem).isEmpty());
        QVERIFY(problem.contains(QStringLiteral("KEY=VALUE")));
    }
};

QTEST_MAIN(McpSettingsTest)
#include "mcpsettings_test.moc"
