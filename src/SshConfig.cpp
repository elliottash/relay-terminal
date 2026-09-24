// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SshConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#ifndef Q_OS_WIN
#include <glob.h>
#ifdef Q_OS_MACOS
#include <sys/sysctl.h>
#include <cstring>
#endif
#endif

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
#ifdef Q_OS_WIN
    // Expand one path component at a time, so Include conf.d/*/*.conf works as
    // well as a wildcard filename. Do not treat directory separators as wildcards.
    const QString path = QDir::fromNativeSeparators(pattern);
    int wildcard = -1;
    for (int i = 0; i < path.size(); ++i) {
        if (path.at(i) == QLatin1Char('*') || path.at(i) == QLatin1Char('?')
            || path.at(i) == QLatin1Char('[')) { wildcard = i; break; }
    }
    if (wildcard < 0) {
        if (QFileInfo::exists(path)) out << path;
        return out;
    }
    const int slash = int(path.lastIndexOf(QLatin1Char('/'), wildcard));
    const int nextSlash = int(path.indexOf(QLatin1Char('/'), wildcard));
    const QString base = slash < 0 ? QStringLiteral(".") : path.left(slash + 1);
    const QString component = nextSlash < 0 ? path.mid(slash + 1)
                                            : path.mid(slash + 1, nextSlash - slash - 1);
    const QString tail = nextSlash < 0 ? QString() : path.mid(nextSlash);
    const QDir directory(base);
    const auto entries = directory.entryList({component},
        QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        // POSIX glob's default excludes dot names unless explicitly requested.
        if (entry.startsWith(QLatin1Char('.')) && !component.startsWith(QLatin1Char('.'))) continue;
        const QString matched = directory.filePath(entry);
        if (tail.isEmpty()) out << matched;
        else if (QFileInfo(matched).isDir()) out += expandGlob(matched + tail);
    }
#else
    glob_t found{};
    const QByteArray local = QFile::encodeName(pattern);
    if (::glob(local.constData(), 0, nullptr, &found) == 0) {
        for (size_t k = 0; k < found.gl_pathc; ++k) out << QFile::decodeName(found.gl_pathv[k]);
    }
    ::globfree(&found);
#endif
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
                    else if (!QDir::isAbsolutePath(pattern)) pattern = sshDir + QLatin1Char('/') + pattern;
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

// `holder`: the line carried a holder remote command, so the wrapper ran it and each of these
// three keys on it belongs to the wrapper's set, whatever directory its ControlPath names.
bool relayShareOption(const QString &option, bool holder) {
    // What shell/integration.bash's ssh() adds: ControlMaster=auto, a ControlPath in relay-ssh,
    // ControlPersist. Only those three keys are ever taken out.
    const QString lower = option.toLower();
    return lower.startsWith(QLatin1String("controlmaster=")) || lower.startsWith(QLatin1String("controlpersist="))
           || (lower.startsWith(QLatin1String("controlpath="))
               && (holder || option.contains(QLatin1String("/relay-ssh/"))));
}

// The remote command the pane shell's wrapper appends to a plain login to hold it in a session on
// the host (card #XQ8F), as ssh leaves it: one final argv word, `sh -c '<holder script>'
// relay-holder <session> [<cwd>]`. Only a word of exactly that shape is ever taken for the
// wrapper's own; any other command after the destination is the user's.
bool holderCommandWord(const QString &word) {
    return word.startsWith(QStringLiteral("sh -c ")) && word.contains(QStringLiteral(" relay-holder "));
}

// The same holder command as mosh leaves it: the words after the line's `--`, still one word or
// already several. mosh-client's `-#` argument joins its arguments with spaces, so the same test
// on those space-split words holds, with the script's quotes as literal characters.
bool holderTail(const QStringList &words) {
    if (words.isEmpty()) return false;
    const QString &first = words.first();
    const bool shCommand = first == QLatin1String("sh") || first.startsWith(QLatin1String("sh "));
    return shCommand && words.join(QLatin1Char(' ')).contains(QStringLiteral(" relay-holder "));
}

// Where a mosh line's holder command starts: at the `--` that precedes it, at the `sh` word when
// there is no `--`, or nowhere.
int holderTailStart(const QStringList &words) {
    for (int i = 0; i < words.size(); ++i)
        if (words.at(i) == QLatin1String("--") && holderTail(words.mid(i + 1))) return i;
    for (int i = 0; i < words.size(); ++i)
        if (words.at(i) == QLatin1String("sh") && holderTail(words.mid(i))) return i;
    return -1;
}

// The holder words of any of the three shapes above: session name and start directory, or empty
// when the argv carries no holder command.
struct HolderWords {
    QString session;
    QString cwd;
};

HolderWords holderWords(const QStringList &argv) {
    // mosh-client keeps the original mosh line inside its -# argument; the IP and port that follow
    // in argv are no part of it and must not read as the cwd.
    QStringList tokens;
    for (const QString &word : argv) {
        if (!word.startsWith(QLatin1String("-#"))) { tokens << word; continue; }
        QString joined = word.mid(2).trimmed();
        if (joined.endsWith(QLatin1Char('|'))) joined.chop(1);
        tokens = joined.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        break;
    }
    // ssh's holder command is one argv word; mosh's several words are apart already.
    QStringList words;
    for (const QString &token : std::as_const(tokens)) words << token.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // A holder command always begins `sh -c`; an ordinary remote command that happens to mention
    // the marker is not one.
    bool shCommand = false;
    for (int i = 0; i + 1 < words.size(); ++i)
        if (words.at(i) == QLatin1String("sh") && words.at(i + 1) == QLatin1String("-c")) shCommand = true;
    if (!shCommand) return {};
    // The script precedes the marker, so the last marker with a session word after it is the real
    // one; a session name is only ever relay's safe characters.
    static const QRegularExpression sessionName(QStringLiteral("^[A-Za-z0-9_-]+$"));
    HolderWords found;
    for (int i = words.size() - 2; i >= 0; --i) {
        if (words.at(i) != QLatin1String("relay-holder")) continue;
        if (!sessionName.match(words.at(i + 1)).hasMatch()) continue;
        found.session = words.at(i + 1);
        found.cwd = words.mid(i + 2).join(QLatin1Char(' '));
        if (found.cwd.size() > 1 && found.cwd.startsWith(QLatin1Char('\'')) && found.cwd.endsWith(QLatin1Char('\'')))
            found.cwd = found.cwd.mid(1, found.cwd.size() - 2);
        return found;
    }
    return found;
}

QString rerunSsh(const QStringList &args, QString *host) {
    // The wrapper's options come as a set; take them out only when its ControlPath is among them,
    // so a ControlMaster the user wrote themselves stays. A holder remote command proves the
    // wrapper ran this line too, and carries the same set whatever its ControlPath names.
    bool holder = false;
    bool relayControlPath = false;
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (holderCommandWord(arg)) holder = true;
        QString value;
        if (arg == QLatin1String("-o") && i + 1 < args.size()) value = args.at(i + 1);
        else if (arg.startsWith(QLatin1String("-o"))) value = arg.mid(2);
        if (value.startsWith(QLatin1String("ControlPath="), Qt::CaseInsensitive) && value.contains(QLatin1String("/relay-ssh/")))
            relayControlPath = true;
    }
    const bool wrapped = holder || relayControlPath;
    QStringList kept;
    QString destination;
    bool options = true;
    int positionals = 0;  // the destination and any remote-command words after it
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
                if (relayShareOption(value, holder)) { if (arg == QLatin1String("-o")) ++i; continue; }
            }
            // The wrapper's own -t travels with the holder command, so it goes with it; a cluster
            // such as the user's -tA is left alone.
            if (holder && arg == QLatin1String("-t")) continue;
            kept << arg;
            if (takesNext && i + 1 < args.size()) kept << args.at(++i);
            continue;
        }
        // The holder command comes off: the rebuilt line reaches the wrapper as the plain login
        // the person typed, and the new pane's holder is the wrapper's to build.
        if (holderCommandWord(arg)) continue;
        if (destination.isEmpty()) destination = arg;
        options = false;
        ++positionals;
        kept << arg;
    }
    if (destination.isEmpty()) return {};
    if (host) *host = destination.section(QLatin1Char('@'), -1);
    // One word after the destination is the user's remote command and stays; two or more are the
    // words of a one-shot `ssh host cmd args…` line, which leaves nothing to open again and must
    // never be wrapped into a session.
    if (positionals > 2) return {};
    QStringList words{QStringLiteral("ssh")};
    for (const QString &word : std::as_const(kept)) words << shellQuote(word);
    return words.join(QLatin1Char(' '));
}

// `joined`: the words came from mosh-client's `-#` argument, where mosh joined its arguments with
// spaces, so a `--ssh="ssh -o …"` arrives as several words.
QString rerunMosh(QStringList args, QString *host, bool joined = false) {
    // A holder command at the end says the wrapper ran this mosh, so the --ssh that comes with it
    // is the wrapper's whatever its ControlPath names; the new pane's wrapper adds its own back.
    const bool holder = holderTailStart(args) >= 0;
    bool wrapped = false;
    static const QRegularExpression shareWord(QStringLiteral("^(-o)?Control(Master|Path|Persist)=.*$"),
                                              QRegularExpression::CaseInsensitiveOption);
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (!arg.startsWith(QLatin1String("--ssh="))) continue;
        if (!joined) {
            if (holder || arg.contains(QLatin1String("/relay-ssh/"))) { args.removeAt(i--); wrapped = true; }
            continue;
        }
        int end = i + 1;
        bool relay = holder;
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
    int at = -1;
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("--")) { destination = args.value(i + 1); at = i + 1; break; }
        if (arg == QLatin1String("-p")) { ++i; continue; }
        if (arg.startsWith(QLatin1Char('-'))) continue;
        destination = arg;
        at = i;
        break;
    }
    if (destination.isEmpty() || at < 0) return {};
    // The holder command comes off the end — from its `--`, or from its `sh` word when the line
    // has no `--` — so the rebuilt line reaches the wrapper as the plain login it started as.
    const int tail = holderTailStart(args);
    if (tail > at) args.erase(args.begin() + tail, args.end());
    if (host) *host = destination.section(QLatin1Char('@'), -1);
    // Words after the destination are a one-shot remote command, not a login to open again.
    if (args.size() - at > 2) return {};
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

bool hasLocalMosh() {
    return !QStandardPaths::findExecutable(QStringLiteral("mosh")).isEmpty();
}

QString persistentSession(const QString &target) {
    QString out;
    for (const QChar &c : target) {
        if (c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-') || c == QLatin1Char('_'))
            out += c;
        else if (c == QLatin1Char('@'))
            out += QLatin1Char('-');  // user@host -> user-host: one session per user and host
    }
    if (out.isEmpty()) out = QStringLiteral("relay");
    return QStringLiteral("relay-") + out;
}

QString persistentCommand(const QString &target, bool haveMosh) {
    const QString session = persistentSession(target);
    // One script for the host, run by its sh: re-attach to the session, creating it on first
    // connect; fall through to tmux's attach-or-create, then to a plain login shell. No exec on
    // the fallbacks: `command -v ... && exec` only execs when the program exists, and a missing
    // one leaves the line false so the next runs. Written without single quotes so the whole
    // script travels as one single-quoted word (shellQuote).
    const QString inner = QStringLiteral(
                              "command -v zellij >/dev/null 2>&1 && exec zellij attach --create %1; "
                              "command -v tmux >/dev/null 2>&1 && exec tmux new -A -s %1; "
                              "exec \"$SHELL\" -l")
                              .arg(session);
    const QString remote = QStringLiteral("sh -c ") + shellQuote(inner);
    // mosh's `--` ends its own options and starts the remote command; without a local mosh the
    // fallback needs -t: the multiplexer on the host is a full-screen program.
    if (haveMosh) return QStringLiteral("mosh ") + shellQuote(target) + QStringLiteral(" -- ") + remote;
    return QStringLiteral("ssh -t ") + shellQuote(target) + QStringLiteral(" ") + remote;
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
#ifdef Q_OS_MACOS
    int maxBytes = 0;
    size_t size = sizeof(maxBytes);
    if (::sysctlbyname("kern.argmax", &maxBytes, &size, nullptr, 0) != 0
        || maxBytes <= int(sizeof(int)) || maxBytes > 16 * 1024 * 1024) return {};
    QByteArray raw(maxBytes, '\0');
    size = size_t(raw.size());
    int mib[] = {CTL_KERN, KERN_PROCARGS2, pid};
    if (::sysctl(mib, 3, raw.data(), &size, nullptr, 0) != 0 || size < sizeof(int)) return {};
    raw.resize(int(size));
    int argc = 0;
    std::memcpy(&argc, raw.constData(), sizeof(argc));
    if (argc <= 0 || argc > raw.size()) return {};
    // The kernel prefixes argc and the executable path, then NUL padding, then argv.
    int offset = int(raw.indexOf('\0', sizeof(int)));
    if (offset < 0) return {};
    while (offset < raw.size() && raw.at(offset) == '\0') ++offset;
    QStringList argv;
    for (int index = 0; index < argc; ++index) {
        const int end = int(raw.indexOf('\0', offset));
        if (end < 0) return {};
        argv << QString::fromLocal8Bit(raw.constData() + offset, end - offset);
        offset = end + 1;
    }
    return argv; // Never expose the environment block following the argument vector.
#else
    QFile file(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray raw = file.readAll();
    if (raw.endsWith('\0')) raw.chop(1);
    if (raw.isEmpty()) return {};
    QStringList argv;
    for (const QByteArray &part : raw.split('\0')) argv << QString::fromLocal8Bit(part);
    return argv;
#endif
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

QString holderSession(const QStringList &argv) {
    return holderWords(argv).session;
}

QString holderCwd(const QStringList &argv) {
    return holderWords(argv).cwd;
}

QString killSessionCommand(const QString &session) {
    // "Close and end the remote session" (card #XQ8F): the holder server on the host is Relay's
    // own socket, so one line over the shared connection ends it and nothing on the host outlives
    // the pane.
    return QStringLiteral("tmux -L relay kill-session -t ") + shellQuote(session);
}

QString listSessionsCommand() {
    // "Remote sessions on this host…": a real tab between the fields is what keeps a session name
    // with a space from shifting the columns parseSessionList splits on, and an empty answer must
    // not be an error the pane sees.
    return QStringLiteral("tmux -L relay list-sessions -F '#{session_name}\t#{session_created}\t#{session_attached}'"
                          " 2>/dev/null");
}

QList<RemoteSession> parseSessionList(const QByteArray &output) {
    // The host answers over one shared connection and tmux writes one line per session; anything
    // else in it (a "no server" line, a truncated tail) must not surface as a session Relay could
    // offer to re-attach.
    QList<RemoteSession> sessions;
    for (QByteArray line : output.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        const QList<QByteArray> parts = line.split('\t');
        if (parts.isEmpty() || parts.first().isEmpty()) continue;
        const QString name = QString::fromLocal8Bit(parts.first());
        if (!name.startsWith(QStringLiteral("relay-"))) continue;  // not a Relay holder session
        RemoteSession session;
        session.name = name;
        if (parts.size() > 1) session.created = parts.at(1).toLongLong();
        if (parts.size() > 2) session.attached = parts.at(2).toInt();
        sessions << session;
    }
    return sessions;
}

QString reattachCommand(const QString &target, const QString &session) {
    // A saved or listed pane resumes as the plain login the person typed, plus the session it had:
    // the wrapper reads the prefix as the holder to attach to instead of minting a new one, so a
    // restored pane lands where it was, its programs still running.
    return QStringLiteral("RELAY_SSH_SESSION=") + shellQuote(session) + QStringLiteral(" ssh ") + shellQuote(target);
}

}  // namespace relay::ssh
