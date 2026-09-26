// SPDX-License-Identifier: AGPL-3.0-or-later
#include "McpSettings.h"

#include "AppPaths.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

namespace relay::mcp {

namespace {

QString joined(const QJsonValue &value) {
    QStringList out;
    for (const QJsonValue &item : value.toArray()) out << item.toString();
    return out.join(QStringLiteral(", "));
}

// One line saying what the server is, without any value that could be a secret: the CLI only
// ever returns env and header *names*, and a URL with its query and userinfo redacted.
QString describe(const QJsonObject &row) {
    QStringList parts;
    const QString origin = row.value(QStringLiteral("origin")).toString();
    parts << (origin == QLatin1String("project") ? QStringLiteral("this project's .mcp.json")
                                                 : QStringLiteral("global"));
    const QString where = row.value(QStringLiteral("command")).toString().isEmpty()
                              ? row.value(QStringLiteral("url")).toString()
                              : row.value(QStringLiteral("command")).toString();
    if (!where.isEmpty()) parts << where;
    if (!row.value(QStringLiteral("env")).toArray().isEmpty())
        parts << QStringLiteral("env: ") + joined(row.value(QStringLiteral("env")));
    if (!row.value(QStringLiteral("headers")).toArray().isEmpty())
        parts << QStringLiteral("headers: ") + joined(row.value(QStringLiteral("headers")));
    return parts.join(QStringLiteral(" · "));
}

}  // namespace

QList<SettingRow> rowsFor(const QJsonObject &listed, const QString &workspace, const Hooks &hooks,
                          bool loading, const QString &error) {
    QList<SettingRow> rows;
    {
        SettingRow heading;
        heading.kind = SettingRow::Heading;
        heading.id = QStringLiteral("heading:MCP servers");
        heading.label = QStringLiteral("MCP servers");
        heading.aliases = QStringLiteral("mcp model context protocol tools servers claude codex warp");
        rows << heading;
    }
    {
        SettingRow info;
        info.kind = SettingRow::Info;
        info.id = QStringLiteral("info:mcp");
        QString path = listed.value(QStringLiteral("global_path")).toString();
        info.label = QStringLiteral(
            "Pane agents can use these servers' tools. A trusted server's tools run without asking; an "
            "untrusted one's put up an ask naming the server before each call, and \"Always allow\" there "
            "trusts it. A project's .mcp.json never starts a command until you enable its server here.")
            + (path.isEmpty() ? QString() : QStringLiteral(" Saved in ") + path + QStringLiteral("."));
        info.aliases = QStringLiteral("mcp trust untrusted approval .mcp.json");
        rows << info;
    }
    if (!error.isEmpty() || loading) {
        SettingRow line;
        line.kind = SettingRow::Info;
        line.id = QStringLiteral("mcp:status");
        line.label = error.isEmpty() ? QStringLiteral("Reading the MCP configuration…")
                                     : QStringLiteral("Could not read the MCP configuration: ") + error;
        rows << line;
    }
    const QJsonArray servers = listed.value(QStringLiteral("servers")).toArray();
    const QStringList scoped = workspace.isEmpty() ? QStringList{}
                                                   : QStringList{QStringLiteral("--workspace"), workspace};
    for (const QJsonValue &value : servers) {
        const QJsonObject server = value.toObject();
        const QString name = server.value(QStringLiteral("name")).toString();
        const QString origin = server.value(QStringLiteral("origin")).toString();
        const bool project = origin == QLatin1String("project");
        const bool pending = server.value(QStringLiteral("pending")).toBool();
        const bool enabled = server.value(QStringLiteral("enabled")).toBool();
        const bool trusted = server.value(QStringLiteral("trust")).toString() == QLatin1String("trusted");

        SettingRow row;
        row.kind = SettingRow::Buttons;
        row.id = QStringLiteral("mcp:%1:%2").arg(origin, name);
        row.label = name;
        row.strong = true;
        row.aliases = QStringLiteral("mcp server ") + origin;
        QString state = pending ? QStringLiteral("not enabled")
                      : !enabled ? QStringLiteral("disabled")
                      : trusted ? QStringLiteral("trusted") : QStringLiteral("untrusted, asks first");
        row.detail = state + QStringLiteral(" · ") + describe(server);
        row.tooltip = server.value(QStringLiteral("source")).toString();
        // No agent may press these (SettingRow::agentSafeButtons stays empty): trusting a server is
        // what lets its tools run unasked, and enabling a project server launches its command.
        QList<std::function<void()>> actions;
        if (pending) {
            const QString what = describe(server);
            const QString ask = QStringLiteral("Enable the MCP server \"%1\" from this project's .mcp.json?\n\n%2\n\n"
                                               "Relay will start it for pane agents in this project. A change "
                                               "to its entry disables it again until you re-enable it.")
                                    .arg(name, what);
            row.buttonTexts << QStringLiteral("Enable") << QStringLiteral("Enable as trusted");
            actions << [hooks, name, scoped, ask] {
                hooks.run(QStringList{QStringLiteral("enable"), name} + scoped, ask);
            };
            actions << [hooks, name, scoped, ask] {
                hooks.run(QStringList{QStringLiteral("enable"), name, QStringLiteral("--trust"),
                                      QStringLiteral("trusted")} + scoped,
                          ask + QStringLiteral("\n\nTrusted: its tools will run without asking."));
            };
        } else if (!enabled) {
            row.buttonTexts << QStringLiteral("Enable") << QStringLiteral("Remove");
            actions << [hooks, name] {
                hooks.run({QStringLiteral("enable"), QStringLiteral("--global"), name}, {});
            };
            actions << [hooks, name] {
                hooks.run({QStringLiteral("remove"), name},
                          QStringLiteral("Remove the MCP server \"%1\" from Relay's configuration?").arg(name));
            };
        } else {
            row.buttonTexts << (trusted ? QStringLiteral("Untrust") : QStringLiteral("Trust"))
                            << QStringLiteral("Disable");
            actions << [hooks, name, scoped, trusted] {
                hooks.run(QStringList{QStringLiteral("trust"), name,
                                      trusted ? QStringLiteral("untrusted") : QStringLiteral("trusted")} + scoped,
                          trusted ? QString()
                                  : QStringLiteral("Trust \"%1\"? Its tools will run without asking you first.").arg(name));
            };
            actions << [hooks, name, scoped, project] {
                hooks.run(project ? QStringList{QStringLiteral("disable"), name} + scoped
                                  : QStringList{QStringLiteral("disable"), QStringLiteral("--global"), name}, {});
            };
            if (!project) {
                row.buttonTexts << QStringLiteral("Remove");
                actions << [hooks, name] {
                    hooks.run({QStringLiteral("remove"), name},
                              QStringLiteral("Remove the MCP server \"%1\" from Relay's configuration?").arg(name));
                };
            }
        }
        row.onButton = [actions](int index) {
            if (index >= 0 && index < actions.size()) actions.at(index)();
        };
        rows << row;
    }
    if (servers.isEmpty() && !loading && error.isEmpty()) {
        SettingRow none;
        none.kind = SettingRow::Info;
        none.id = QStringLiteral("mcp:none");
        none.label = QStringLiteral("No MCP servers yet. Add one, or import the ones Claude Code, Codex or Warp have.");
        rows << none;
    }
    const QJsonArray problems = listed.value(QStringLiteral("problems")).toArray();
    for (int i = 0; i < problems.size(); ++i) {
        SettingRow problem;
        problem.kind = SettingRow::Info;
        problem.id = QStringLiteral("mcp:problem:%1").arg(i);
        problem.label = QStringLiteral("Problem: ") + problems.at(i).toString();
        rows << problem;
    }
    {
        SettingRow manage;
        manage.kind = SettingRow::Buttons;
        manage.id = QStringLiteral("mcp:manage");
        manage.label = QStringLiteral("Add or import");
        manage.detail = QStringLiteral("Add a server by command or URL, or preview the servers Claude Code, "
                                       "Codex and Warp have and pick which to bring in");
        manage.aliases = QStringLiteral("mcp add import claude code codex warp .claude.json config.toml");
        manage.buttonTexts = QStringList{QStringLiteral("Add server…"), QStringLiteral("Import…")};
        manage.onButton = [hooks](int index) {
            if (index == 0 && hooks.add) hooks.add();
            if (index == 1 && hooks.import) hooks.import();
        };
        rows << manage;
    }
    return rows;
}

QStringList addArguments(const AddForm &form, QByteArray *stdinJson, QString *problem) {
    const QString name = form.name.trimmed();
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.-]{1,40}$"));
    if (!valid.match(name).hasMatch()) {
        *problem = QStringLiteral("The name must be 1-40 letters, digits, '_', '-' or '.'.");
        return {};
    }
    QStringList args{QStringLiteral("add"), name};
    const QString target = form.target.trimmed();
    if (form.url) {
        if (!target.startsWith(QLatin1String("http://")) && !target.startsWith(QLatin1String("https://"))) {
            *problem = QStringLiteral("The URL must start with http:// or https://.");
            return {};
        }
        args << QStringLiteral("--url=") + target;
    } else {
        const QStringList words = QProcess::splitCommand(target);
        if (words.isEmpty()) {
            *problem = QStringLiteral("Give the command that starts the server.");
            return {};
        }
        args << QStringLiteral("--command=") + words.first();
        for (int i = 1; i < words.size(); ++i) args << QStringLiteral("--arg=") + words.at(i);
    }
    QJsonObject pairs;
    for (const QString &raw : form.pairs) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            *problem = QStringLiteral("Each variable line must be KEY=VALUE: \"%1\".").arg(line.left(eq > 0 ? eq : 20));
            return {};
        }
        pairs.insert(line.left(eq).trimmed(), line.mid(eq + 1));
    }
    args << QStringLiteral("--trust") << (form.trusted ? QStringLiteral("trusted") : QStringLiteral("untrusted"));
    args << QStringLiteral("--secrets-stdin");
    *stdinJson = QJsonDocument(QJsonObject{{form.url ? QStringLiteral("headers") : QStringLiteral("env"), pairs}})
                     .toJson(QJsonDocument::Compact);
    return args;
}

// ----- the live block: cache, processes, dialogs ------------------------------------------------

namespace {

struct Cache {
    QString workspace, stamp, error;
    QJsonObject listed;
    bool loaded = false, loading = false;
};

Cache &cache() {
    static Cache instance;
    return instance;
}

QString globalPath() {
    const QString override = qEnvironmentVariable("RELAY_MCP_CONFIG");
    if (!override.isEmpty()) return override;
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/relay/mcp-servers.json");
}

qint64 mtime(const QString &path) {
    const QFileInfo info(path);
    return info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
}

// What makes the cached list stale: either file changing, or another project in front.
QString stampFor(const QString &workspace) {
    QString project = cache().listed.value(QStringLiteral("project_path")).toString();
    if (project.isEmpty() || cache().workspace != workspace)
        project = workspace.isEmpty() ? QString() : workspace + QStringLiteral("/.mcp.json");
    return QStringLiteral("%1|%2|%3").arg(workspace).arg(mtime(globalPath())).arg(project.isEmpty() ? 0 : mtime(project));
}

// `python -m relay_core.<module> args…`, stdin written and closed, `done` with the exit code and
// both streams. A CLI that has not answered in 30 seconds is killed and reported as failed.
void runCli(QObject *owner, const QString &module, const QStringList &args, const QByteArray &input,
            std::function<void(int code, const QByteArray &out, const QString &err)> done) {
    QString backend;
    try {
        backend = dataRoot() + QStringLiteral("/backend");
    } catch (const std::exception &exc) {
        done(-1, {}, QString::fromUtf8(exc.what()));
        return;
    }
    const QString python = relayPython();
    if (python.isEmpty()) {
        done(-1, {}, QStringLiteral("no Python interpreter was found"));
        return;
    }
    auto *process = new QProcess(owner);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString path = env.value(QStringLiteral("PYTHONPATH"));
    env.insert(QStringLiteral("PYTHONPATH"), path.isEmpty() ? backend : backend + QDir::listSeparator() + path);
    process->setProcessEnvironment(env);
    auto finished = std::make_shared<bool>(false);
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), owner, [process, done, finished](int code, QProcess::ExitStatus status) {
        if (*finished) return;
        *finished = true;
        done(status == QProcess::NormalExit ? code : -1, process->readAllStandardOutput(),
             QString::fromUtf8(process->readAllStandardError()).trimmed());
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, owner, [process, done, finished](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *finished) return;
        *finished = true;
        done(-1, {}, process->errorString());
        process->deleteLater();
    });
    QTimer::singleShot(30000, process, [process] {
        if (process->state() != QProcess::NotRunning) process->kill();
    });
    process->start(python, QStringList{QStringLiteral("-m"), QStringLiteral("relay_core.") + module} + args);
    process->write(input);
    process->closeWriteChannel();
}

// The last line of a CLI's stderr, without argparse's "usage:" preamble or the "error: " prefix.
QString failure(const QString &err, int code) {
    QStringList lines = err.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString last = lines.isEmpty() ? QStringLiteral("exit %1").arg(code) : lines.last().trimmed();
    const int colon = last.indexOf(QLatin1String("error: "));
    return colon >= 0 ? last.mid(colon + 7) : last;
}

void refresh(QObject *owner, const QString &workspace) {
    Cache &c = cache();
    if (c.loading) return;
    c.loading = true;
    c.workspace = workspace;
    c.stamp = stampFor(workspace);
    QStringList args{QStringLiteral("list"), QStringLiteral("--json")};
    if (!workspace.isEmpty()) args << QStringLiteral("--workspace") << workspace;
    runCli(owner, QStringLiteral("mcp_config"), args, {}, [workspace](int code, const QByteArray &out, const QString &err) {
        Cache &c = cache();
        c.loading = false;
        c.loaded = true;
        const QJsonDocument doc = QJsonDocument::fromJson(out);
        if (code == 0 && doc.isObject()) {
            c.listed = doc.object();
            c.error.clear();
        } else {
            c.error = failure(err, code);
        }
        // The project path is only known now; take the stamp again so the next draw agrees.
        if (c.workspace == workspace) c.stamp = stampFor(workspace);
        SettingsWatch::instance().notify();
    });
}

void addDialog(QWidget *parent, std::function<void(const QString &)> say, std::function<void()> changed) {
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Add MCP server"));
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(dialog);
    name->setPlaceholderText(QStringLiteral("github"));
    auto *kind = new QComboBox(dialog);
    kind->addItems({QStringLiteral("Command (stdio)"), QStringLiteral("URL (streamable HTTP)")});
    auto *target = new QLineEdit(dialog);
    target->setPlaceholderText(QStringLiteral("npx -y @modelcontextprotocol/server-github"));
    auto *pairs = new QPlainTextEdit(dialog);
    pairs->setPlaceholderText(QStringLiteral("GITHUB_TOKEN=${GITHUB_TOKEN}\n(one KEY=VALUE per line)"));
    pairs->setFixedHeight(80);
    auto *pairsLabel = new QLabel(QStringLiteral("Environment"), dialog);
    auto *trusted = new QCheckBox(QStringLiteral("Trusted: run its tools without asking"), dialog);
    auto *message = new QLabel(dialog);
    message->setWordWrap(true);
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Kind"), kind);
    form->addRow(QStringLiteral("Command"), target);
    form->addRow(pairsLabel, pairs);
    form->addRow(QString(), trusted);
    form->addRow(QString(), message);
    auto *targetLabel = qobject_cast<QLabel *>(form->labelForField(target));
    QObject::connect(kind, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [=](int index) {
        const bool url = index == 1;
        if (targetLabel) targetLabel->setText(url ? QStringLiteral("URL") : QStringLiteral("Command"));
        target->setPlaceholderText(url ? QStringLiteral("https://example.com/mcp")
                                       : QStringLiteral("npx -y @modelcontextprotocol/server-github"));
        pairsLabel->setText(url ? QStringLiteral("Headers") : QStringLiteral("Environment"));
        pairs->setPlaceholderText(url ? QStringLiteral("Authorization=Bearer ${API_TOKEN}\n(one KEY=VALUE per line)")
                                      : QStringLiteral("GITHUB_TOKEN=${GITHUB_TOKEN}\n(one KEY=VALUE per line)"));
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Add"));
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        AddForm fields;
        fields.name = name->text();
        fields.url = kind->currentIndex() == 1;
        fields.target = target->text();
        fields.pairs = pairs->toPlainText().split(QLatin1Char('\n'));
        fields.trusted = trusted->isChecked();
        QByteArray input;
        QString problem;
        const QStringList args = addArguments(fields, &input, &problem);
        if (args.isEmpty()) {
            message->setText(problem);
            return;
        }
        buttons->setEnabled(false);
        runCli(dialog, QStringLiteral("mcp_config"), args, input,
               [=](int code, const QByteArray &out, const QString &err) {
            buttons->setEnabled(true);
            if (code != 0) {
                message->setText(failure(err, code));
                return;
            }
            say(QString::fromUtf8(out).trimmed());
            changed();
            dialog->accept();
        });
    });
    auto *layout = new QVBoxLayout(dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    dialog->resize(560, dialog->sizeHint().height());
    dialog->open();
}

void importDialog(QWidget *parent, const QString &workspace, std::function<void(const QString &)> say,
                  std::function<void()> changed) {
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Import MCP servers"));
    auto *layout = new QVBoxLayout(dialog);
    auto *intro = new QLabel(QStringLiteral("Reading Claude Code, Codex and Warp…"), dialog);
    intro->setWordWrap(true);
    auto *list = new QListWidget(dialog);
    auto *trusted = new QCheckBox(QStringLiteral("Add them as trusted (run their tools without asking)"), dialog);
    auto *message = new QLabel(dialog);
    message->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Add checked"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    layout->addWidget(intro);
    layout->addWidget(list);
    layout->addWidget(trusted);
    layout->addWidget(message);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QStringList base;
    if (!workspace.isEmpty()) base << QStringLiteral("--workspace") << workspace;
    base << QStringLiteral("--json");
    runCli(dialog, QStringLiteral("mcp_import"), base, {}, [=](int code, const QByteArray &out, const QString &err) {
        const QJsonDocument doc = QJsonDocument::fromJson(out);
        if (code != 0 || !doc.isObject()) {
            intro->setText(QStringLiteral("Could not read them: ") + failure(err, code));
            return;
        }
        const QJsonArray rows = doc.object().value(QStringLiteral("rows")).toArray();
        int fresh = 0;
        for (const QJsonValue &value : rows) {
            const QJsonObject row = value.toObject();
            const QString status = row.value(QStringLiteral("status")).toString();
            QString text = QStringLiteral("%1  ·  %2  ·  %3").arg(row.value(QStringLiteral("name")).toString(),
                                                                 row.value(QStringLiteral("source")).toString(),
                                                                 row.value(QStringLiteral("where")).toString());
            if (!row.value(QStringLiteral("env")).toArray().isEmpty())
                text += QStringLiteral("  ·  env: ") + joined(row.value(QStringLiteral("env")));
            if (status != QLatin1String("new")) {
                const QString detail = row.value(QStringLiteral("detail")).toString();
                text += QStringLiteral("  (%1%2)").arg(status == QLatin1String("same") ? QStringLiteral("already added")
                                                                                       : status,
                                                       detail.isEmpty() ? QString() : QStringLiteral(": ") + detail);
            }
            auto *item = new QListWidgetItem(text, list);
            item->setData(Qt::UserRole, row.value(QStringLiteral("name")).toString());
            if (status == QLatin1String("new")) {
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
                ++fresh;
            } else {
                item->setFlags(Qt::NoItemFlags);
            }
        }
        intro->setText(rows.isEmpty()
                           ? QStringLiteral("No MCP servers found in Claude Code, Codex or Warp.")
                           : QStringLiteral("Tick the servers to add. Env and header values are copied as "
                                            "they are and never shown here; only their names are."));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(fresh > 0);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        QStringList chosen;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->checkState() == Qt::Checked) chosen << list->item(i)->data(Qt::UserRole).toString();
        if (chosen.isEmpty()) {
            message->setText(QStringLiteral("Tick at least one server."));
            return;
        }
        buttons->setEnabled(false);
        runCli(dialog, QStringLiteral("mcp_import"),
               base + QStringList{QStringLiteral("--add"), chosen.join(QLatin1Char(',')), QStringLiteral("--trust"),
                                  trusted->isChecked() ? QStringLiteral("trusted") : QStringLiteral("untrusted")},
               {}, [=](int code, const QByteArray &out, const QString &err) {
            buttons->setEnabled(true);
            const QJsonDocument doc = QJsonDocument::fromJson(out);
            if (code != 0 || !doc.isObject()) {
                message->setText(failure(err, code));
                return;
            }
            QStringList added;
            for (const QJsonValue &v : doc.object().value(QStringLiteral("added")).toArray()) added << v.toString();
            say(QStringLiteral("Imported %1 MCP server(s): %2").arg(added.size()).arg(added.join(QStringLiteral(", "))));
            changed();
            dialog->accept();
        });
    });
    dialog->resize(720, 420);
    dialog->open();
}

}  // namespace

QList<SettingRow> settingsRows(const QString &workspace, QWidget *parent, std::function<void(const QString &)> say) {
    Cache &c = cache();
    if (!c.loading && (!c.loaded || c.workspace != workspace || c.stamp != stampFor(workspace)))
        refresh(parent, workspace);
    QPointer<QWidget> owner(parent);
    auto changed = [owner, workspace] {
        cache().stamp.clear();                    // re-read even if the mtime did not move
        if (owner) refresh(owner, workspace);
    };
    Hooks hooks;
    hooks.run = [owner, say, changed](const QStringList &args, const QString &confirm) {
        if (!owner) return;
        if (!confirm.isEmpty()
            && QMessageBox::question(owner, QStringLiteral("MCP servers"), confirm) != QMessageBox::Yes)
            return;
        runCli(owner, QStringLiteral("mcp_config"), args, {}, [say, changed](int code, const QByteArray &out, const QString &err) {
            QString said = QString::fromUtf8(out).trimmed().split(QLatin1Char('\n')).last();
            say(code == 0 ? said : QStringLiteral("MCP: ") + failure(err, code));
            changed();
        });
    };
    hooks.add = [owner, say, changed] { if (owner) addDialog(owner, say, changed); };
    hooks.import = [owner, workspace, say, changed] { if (owner) importDialog(owner, workspace, say, changed); };
    return rowsFor(c.workspace == workspace ? c.listed : QJsonObject{}, workspace, hooks,
                   !c.loaded || (c.loading && c.workspace != workspace), c.workspace == workspace ? c.error : QString());
}

}  // namespace relay::mcp
