// SPDX-License-Identifier: GPL-3.0-or-later
#include "SshConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTextStream>

#include <glob.h>

namespace relay::ssh {

namespace {

constexpr int kIncludeDepth = 16;   // ssh's own READCONF_MAX_DEPTH

// One config line as ssh splits it: the keyword, then its arguments. `Key=Value`, `Key = Value`
// and `Key Value` all read the same; double or single quotes group a word; an unquoted `#` at the
// start of a word ends the line.
struct Line {
    QString keyword;   // lower case
    QStringList args;
};

bool splitLine(const QString &text, Line *line) {
    int i = 0;
    const int n = int(text.size());
    auto skipSpace = [&] { while (i < n && text.at(i).isSpace()) ++i; };
    skipSpace();
    if (i >= n || text.at(i) == QLatin1Char('#')) return false;
    const int keyStart = i;
    while (i < n && !text.at(i).isSpace() && text.at(i) != QLatin1Char('=')) ++i;
    line->keyword = text.mid(keyStart, i - keyStart).toLower();
    skipSpace();
    if (i < n && text.at(i) == QLatin1Char('=')) { ++i; skipSpace(); }
    line->args.clear();
    while (i < n) {
        skipSpace();
        if (i >= n) break;
        if (text.at(i) == QLatin1Char('#')) break;
        QString word;
        while (i < n && !text.at(i).isSpace()) {
            const QChar c = text.at(i);
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                const int close = int(text.indexOf(c, i + 1));
                if (close < 0) { word += text.mid(i + 1); i = n; break; }
                word += text.mid(i + 1, close - i - 1);
                i = close + 1;
            } else if (c == QLatin1Char('\\') && i + 1 < n) {
                word += text.at(i + 1);
                i += 2;
            } else {
                word += c;
                ++i;
            }
        }
        line->args << word;
    }
    return !line->keyword.isEmpty();
}

bool concrete(const QString &pattern) {
    return !pattern.isEmpty() && !pattern.contains(QLatin1Char('*')) && !pattern.contains(QLatin1Char('?'))
           && !pattern.startsWith(QLatin1Char('!'));
}

QStringList expandGlob(const QString &pattern) {
    QStringList out;
    glob_t found{};
    const QByteArray local = QFile::encodeName(pattern);
    if (::glob(local.constData(), 0, nullptr, &found) == 0) {
        for (size_t k = 0; k < found.gl_pathc; ++k) out << QFile::decodeName(found.gl_pathv[k]);
    }
    ::globfree(&found);
    return out;
}

struct Reader {
    QString sshDir, home;
    QList<Host> hosts;
    QHash<QString, int> index;   // alias → position in hosts
    QSet<QString> reading;       // files on the Include stack, canonical

    void read(const QString &path, int depth) {
        if (depth > kIncludeDepth) return;
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty() || reading.contains(canonical) || !QFileInfo(canonical).isFile()) return;
        QFile file(canonical);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        reading.insert(canonical);
        QStringList block;        // aliases of the Host block being read; empty in Match or `Host *`
        QTextStream stream(&file);
        Line line;
        while (!stream.atEnd()) {
            if (!splitLine(stream.readLine(), &line)) continue;
            if (line.keyword == QLatin1String("host")) {
                block.clear();
                for (const QString &pattern : std::as_const(line.args)) {
                    if (!concrete(pattern)) continue;
                    block << pattern;
                    if (!index.contains(pattern)) {
                        index.insert(pattern, int(hosts.size()));
                        hosts.append(Host{pattern, {}, {}, {}});
                    }
                }
            } else if (line.keyword == QLatin1String("match")) {
                block.clear();
            } else if (line.keyword == QLatin1String("include")) {
                for (const QString &arg : std::as_const(line.args)) {
                    QString pattern = arg;
                    if (pattern.startsWith(QLatin1String("~/"))) pattern = home + pattern.mid(1);
                    else if (!pattern.startsWith(QLatin1Char('/'))) pattern = sshDir + QLatin1Char('/') + pattern;
                    for (const QString &included : expandGlob(pattern)) read(included, depth + 1);
                }
            } else if (!block.isEmpty() && !line.args.isEmpty()) {
                QString Host::*field = nullptr;
                if (line.keyword == QLatin1String("hostname")) field = &Host::hostName;
                else if (line.keyword == QLatin1String("user")) field = &Host::user;
                else if (line.keyword == QLatin1String("port")) field = &Host::port;
                if (!field) continue;
                for (const QString &alias : std::as_const(block)) {
                    Host &host = hosts[index.value(alias)];
                    if ((host.*field).isEmpty()) host.*field = line.args.first();
                }
            }
        }
        reading.remove(canonical);
    }
};

bool safeWord(const QString &word) {
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_@%+=:,./-]+$"));
    return safe.match(word).hasMatch();
}

// ssh options that take a value (ssh(1)); anything else starting with '-' is a flag.
const QString kSshValued = QStringLiteral("BbcDEeFIiJLlmOoPpQRSWw");

bool relayShareOption(const QString &option) {
    // What shell/integration.bash's ssh() adds: ControlMaster=auto, a ControlPath in relay-ssh,
    // ControlPersist. Only those three keys are ever taken out.
    const QString lower = option.toLower();
    return lower.startsWith(QLatin1String("controlmaster=")) || lower.startsWith(QLatin1String("controlpersist="))
           || (lower.startsWith(QLatin1String("controlpath=")) && option.contains(QLatin1String("/relay-ssh/")));
}

QString rerunSsh(const QStringList &args, QString *host) {
    // The wrapper's options come as a set; take them out only when its ControlPath is among them,
    // so a ControlMaster the user wrote themselves stays.
    bool wrapped = false;
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        QString value;
        if (arg == QLatin1String("-o") && i + 1 < args.size()) value = args.at(i + 1);
        else if (arg.startsWith(QLatin1String("-o"))) value = arg.mid(2);
        if (value.startsWith(QLatin1String("ControlPath="), Qt::CaseInsensitive) && value.contains(QLatin1String("/relay-ssh/")))
            wrapped = true;
    }
    QStringList kept;
    QString destination;
    bool options = true;
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (options && arg == QLatin1String("--")) { kept << arg; options = false; continue; }
        if (options && arg.startsWith(QLatin1Char('-')) && arg.size() > 1) {
            // A cluster such as -tA or -p22: flags until one that takes a value.
            bool takesNext = false;
            for (int k = 1; k < arg.size(); ++k) {
                const QChar flag = arg.at(k);
                // Not a login: control commands, config/version/query, stdio forwarding, and the
                // tunnel-only -N and -f, which a second copy would only collide with.
                if (QStringLiteral("OGVQWNf").contains(flag)) return {};
                if (kSshValued.contains(flag)) { takesNext = k == arg.size() - 1; break; }
            }
            if (wrapped && (arg == QLatin1String("-o") || arg.startsWith(QLatin1String("-o")))) {
                const QString value = arg == QLatin1String("-o") ? args.value(i + 1) : arg.mid(2);
                if (relayShareOption(value)) { if (arg == QLatin1String("-o")) ++i; continue; }
            }
            kept << arg;
            if (takesNext && i + 1 < args.size()) kept << args.at(++i);
            continue;
        }
        if (destination.isEmpty()) destination = arg;
        options = false;
        kept << arg;
    }
    if (destination.isEmpty()) return {};
    if (host) *host = destination.section(QLatin1Char('@'), -1);
    QStringList words{QStringLiteral("ssh")};
    for (const QString &word : std::as_const(kept)) words << shellQuote(word);
    return words.join(QLatin1Char(' '));
}

// `joined`: the words came from mosh-client's `-#` argument, where mosh joined its arguments with
// spaces, so a `--ssh="ssh -o …"` arrives as several words.
QString rerunMosh(QStringList args, QString *host, bool joined = false) {
    // The wrapper's --ssh (ssh with Relay's connection sharing) comes out; the new pane's wrapper
    // adds it back.
    bool wrapped = false;
    static const QRegularExpression shareWord(QStringLiteral("^(-o)?Control(Master|Path|Persist)=.*$"),
                                              QRegularExpression::CaseInsensitiveOption);
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (!arg.startsWith(QLatin1String("--ssh="))) continue;
        if (!joined) {
            if (arg.contains(QLatin1String("/relay-ssh/"))) { args.removeAt(i--); wrapped = true; }
            continue;
        }
        int end = i + 1;
        bool relay = false;
        while (end < args.size() && (args.at(end) == QLatin1String("-o") || shareWord.match(args.at(end)).hasMatch())) {
            relay = relay || args.at(end).contains(QLatin1String("/relay-ssh/"));
            ++end;
        }
        if (relay) { args.erase(args.begin() + i, args.begin() + end); --i; wrapped = true; }
    }
    // With its --ssh the wrapper also picks the server-address mode that works over a shared
    // connection; the new pane's wrapper chooses it again.
    if (wrapped) args.removeAll(QStringLiteral("--experimental-remote-ip=remote"));
    if (args.isEmpty()) return {};
    QString destination;
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("--")) { destination = args.value(i + 1); break; }
        if (arg == QLatin1String("-p")) { ++i; continue; }
        if (arg.startsWith(QLatin1Char('-'))) continue;
        destination = arg;
        break;
    }
    if (destination.isEmpty()) return {};
    if (host) *host = destination.section(QLatin1Char('@'), -1);
    QStringList words{QStringLiteral("mosh")};
    for (const QString &word : std::as_const(args)) words << shellQuote(word);
    return words.join(QLatin1Char(' '));
}

}  // namespace

QString Host::detail() const {
    if (hostName.isEmpty() && user.isEmpty() && port.isEmpty()) return {};
    QString text = user.isEmpty() ? QString() : user + QLatin1Char('@');
    text += hostName.isEmpty() ? alias : hostName;
    if (!port.isEmpty()) text += QLatin1Char(':') + port;
    return text;
}

QList<Host> parseConfig(const QString &configPath, const QString &sshDir, const QString &home) {
    Reader reader;
    reader.home = home.isEmpty() ? QDir::homePath() : home;
    reader.sshDir = sshDir.isEmpty() ? reader.home + QStringLiteral("/.ssh") : sshDir;
    reader.read(configPath, 0);
    return reader.hosts;
}

QList<Host> userHosts() {
    const QString dir = QDir::homePath() + QStringLiteral("/.ssh");
    return parseConfig(dir + QStringLiteral("/config"), dir, QDir::homePath());
}

QStringList withRecent(QStringList recent, const QString &target, int limit) {
    const QString clean = target.trimmed();
    if (clean.isEmpty()) return recent;
    recent.removeAll(clean);
    recent.prepend(clean);
    return recent.mid(0, limit);
}

QStringList recentHosts() {
    return QSettings().value(QStringLiteral("ssh/recent")).toStringList().mid(0, kRecentLimit);
}

void rememberHost(const QString &target) {
    QSettings().setValue(QStringLiteral("ssh/recent"), withRecent(recentHosts(), target));
}

QString shellQuote(const QString &word) {
    if (!word.isEmpty() && safeWord(word)) return word;
    return QLatin1Char('\'') + QString(word).replace(QLatin1Char('\''), QStringLiteral("'\\''")) + QLatin1Char('\'');
}

QString connectCommand(const QString &target) {
    // `--` never: an alias cannot start with '-', and typedTarget refuses one.
    return QStringLiteral("ssh ") + shellQuote(target);
}

QString typedTarget(const QString &search) {
    QString text = search.trimmed();
    bool explicitSsh = false;
    if (text.startsWith(QLatin1String("ssh "), Qt::CaseInsensitive)) { text = text.mid(4).trimmed(); explicitSsh = true; }
    if (text.isEmpty() || text.contains(QLatin1Char(' ')) || text.startsWith(QLatin1Char('-'))) return {};
    static const QRegularExpression userAtHost(QStringLiteral("^[A-Za-z0-9._%+-]+@[A-Za-z0-9._:\\[\\]-]+$"));
    static const QRegularExpression bareHost(QStringLiteral("^(?:[A-Za-z0-9._%+-]+@)?[A-Za-z0-9._:\\[\\]-]+$"));
    if (userAtHost.match(text).hasMatch()) return text;
    if (explicitSsh && bareHost.match(text).hasMatch()) return text;
    return {};
}

QStringList processArgv(int pid) {
    if (pid <= 0) return {};
    QFile file(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray raw = file.readAll();
    if (raw.endsWith('\0')) raw.chop(1);
    if (raw.isEmpty()) return {};
    QStringList argv;
    for (const QByteArray &part : raw.split('\0')) argv << QString::fromLocal8Bit(part);
    return argv;
}

QString rerunCommand(const QStringList &argv, QString *host) {
    if (argv.isEmpty()) return {};
    QString program = QFileInfo(argv.first()).fileName();
    QStringList args = argv.mid(1);
    // mosh is a Perl script: while it starts up the foreground process reads `perl /usr/bin/mosh …`.
    if (program.startsWith(QLatin1String("perl")) && !args.isEmpty() && QFileInfo(args.first()).fileName() == QLatin1String("mosh")) {
        program = QStringLiteral("mosh");
        args.removeFirst();
    }
    if (program == QLatin1String("ssh")) return rerunSsh(args, host);
    if (program == QLatin1String("mosh")) return rerunMosh(args, host);
    if (program == QLatin1String("mosh-client")) {
        // mosh execs `mosh-client "-# <its own arguments> |" IP PORT`. The arguments were joined
        // with spaces there, so words are all that can be recovered.
        for (const QString &arg : std::as_const(args)) {
            if (!arg.startsWith(QLatin1String("-#"))) continue;
            QString original = arg.mid(2).trimmed();
            if (original.endsWith(QLatin1Char('|'))) original.chop(1);
            return rerunMosh(original.split(QLatin1Char(' '), Qt::SkipEmptyParts), host, true);
        }
    }
    return {};
}

}  // namespace relay::ssh
