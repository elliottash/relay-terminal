// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemoteSession.h"

#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <array>

namespace relay::remote {

bool isLoginProgram(const QString &program) {
    return program == QStringLiteral("ssh") || program == QStringLiteral("mosh") || program == QStringLiteral("mosh-client");
}

namespace {

// OpenSSH 9: options that take a value. Everything else in a cluster is a flag.
bool sshTakesValue(QChar c) { return QStringLiteral("BbcDEeFIiJLlmOoPpQRSWw").contains(c); }

struct Parsed {
    QStringList options;   // ssh options, ready for `ssh -G`
    QString destination;
};

Parsed parseSsh(const QStringList &args) {
    Parsed out;
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a == QStringLiteral("--")) { if (i + 1 < args.size()) out.destination = args[i + 1]; break; }
        if (!a.startsWith('-') || a.size() < 2) { out.destination = a; break; }
        // A cluster: "-tt", "-p22", "-4oBatchMode=yes". Walk it until a letter takes a value.
        bool consumedNext = false;
        for (int j = 1; j < a.size(); ++j) {
            if (!sshTakesValue(a[j])) continue;
            if (j + 1 == a.size()) consumedNext = true;   // the value is the next argument
            break;
        }
        out.options << a;
        if (consumedNext && i + 1 < args.size()) out.options << args[++i];
    }
    return out;
}

// mosh and mosh-client: the destination, and the ssh options mosh handed to its own ssh.
Parsed parseMosh(const QStringList &tokens) {
    static const QSet<QString> valued{QStringLiteral("--client"), QStringLiteral("--server"), QStringLiteral("--ssh"),
                                      QStringLiteral("--port"), QStringLiteral("--predict"), QStringLiteral("--family"),
                                      QStringLiteral("--bind-server"), QStringLiteral("--experimental-remote-ip")};
    Parsed out;
    for (int i = 0; i < tokens.size(); ++i) {
        const QString &t = tokens[i];
        if (t == QStringLiteral("--")) { if (i + 1 < tokens.size() && out.destination.isEmpty()) out.destination = tokens[i + 1]; break; }
        if (t == QStringLiteral("-o") && i + 1 < tokens.size()) { out.options << t << tokens[++i]; continue; }
        if (t.startsWith(QStringLiteral("--ssh="))) {
            // "--ssh=ssh -o ControlPath=… -p 2222": everything after the program is ssh's.
            const QStringList words = t.mid(6).split(' ', Qt::SkipEmptyParts);
            out.options << parseSsh(words.mid(1) << QStringLiteral("x")).options;
            continue;
        }
        if (t.startsWith(QStringLiteral("--"))) {
            if (!t.contains('=') && valued.contains(t) && i + 1 < tokens.size()) {
                if (t == QStringLiteral("--ssh")) {
                    const QStringList words = tokens[i + 1].split(' ', Qt::SkipEmptyParts);
                    out.options << parseSsh(words.mid(1) << QStringLiteral("x")).options;
                }
                ++i;
            }
            continue;
        }
        if (t == QStringLiteral("-p")) { ++i; continue; }   // mosh's UDP port, not ssh's
        if (t.startsWith('-')) continue;
        if (out.destination.isEmpty()) out.destination = t;
        else break;   // the remote command follows
    }
    return out;
}

Parsed parse(const QStringList &argv) {
    if (argv.isEmpty()) return {};
    const QString program = QFileInfo(argv.first()).fileName();
    if (program == QStringLiteral("ssh")) return parseSsh(argv.mid(1));
    if (program == QStringLiteral("mosh")) return parseMosh(argv.mid(1));
    if (program == QStringLiteral("mosh-client")) {
        // mosh execs `mosh-client "-# <the original mosh arguments> |" IP PORT`: one argument
        // holds the flag and the words (seen with mosh 1.4); a separate "-#" is read too.
        for (int i = 1; i < argv.size(); ++i) {
            if (!argv[i].startsWith(QStringLiteral("-#"))) continue;
            QString original = argv[i].size() > 2 ? argv[i].mid(2) : argv.value(i + 1);
            const int bar = original.lastIndexOf(QStringLiteral(" |"));
            if (bar >= 0) original.truncate(bar);
            return parseMosh(original.split(' ', Qt::SkipEmptyParts));
        }
        return {};
    }
    return {};
}

bool setsControlPath(const QStringList &options) {
    for (int i = 0; i < options.size(); ++i) {
        const QString &o = options[i];
        const QString value = o == QStringLiteral("-o") && i + 1 < options.size() ? options[i + 1]
                             : o.startsWith(QStringLiteral("-o")) ? o.mid(2) : QString();
        if (value.startsWith(QStringLiteral("controlpath"), Qt::CaseInsensitive)) return true;
        if (o == QStringLiteral("-S") || (o.startsWith(QStringLiteral("-S")) && o.size() > 2)) return true;
    }
    return false;
}

}  // namespace

QString destination(const QStringList &argv) { return parse(argv).destination; }

QStringList dumpArguments(const QStringList &argv, const QString &relaySocketDir) {
    Parsed parsed = parse(argv);
    if (parsed.destination.isEmpty()) return {};
    const QString program = QFileInfo(argv.first()).fileName();
    if (program != QStringLiteral("ssh") && !relaySocketDir.isEmpty() && !setsControlPath(parsed.options))
        parsed.options << QStringLiteral("-o") << QStringLiteral("ControlPath=%1/%C").arg(relaySocketDir);
    return QStringList{QStringLiteral("-G")} << parsed.options << parsed.destination;
}

Resolved parseDump(const QByteArray &dump, const QString &destination) {
    Resolved out;
    out.host = destination.contains('@') ? destination.section('@', -1) : destination;
    for (const QByteArray &raw : dump.split('\n')) {
        const QByteArray line = raw.trimmed();
        const int space = line.indexOf(' ');
        if (space <= 0) continue;
        const QByteArray key = line.left(space).toLower();
        const QString value = QString::fromUtf8(line.mid(space + 1).trimmed());
        if (key == "hostname") out.hostname = value;
        else if (key == "user") out.user = value;
        else if (key == "port") out.port = value.toInt() > 0 ? value.toInt() : 22;
        else if (key == "controlpath") out.controlPath = value == QStringLiteral("none") ? QString() : value;
        else if (key == "controlmaster") out.controlMaster = value;
    }
    out.ok = !out.hostname.isEmpty();
    if (out.host.isEmpty()) out.host = out.hostname;
    return out;
}

bool isLocalHost(const QString &host, const QString &localName) {
    if (host.isEmpty() || host == QStringLiteral("localhost")) return true;
    const QString mine = localName.section('.', 0, 0);
    return host.compare(localName, Qt::CaseInsensitive) == 0 || host.section('.', 0, 0).compare(mine, Qt::CaseInsensitive) == 0;
}

QByteArray gzip(const QByteArray &data) {
    // qCompress is a 4-byte length, then a zlib stream: a 2-byte header, raw deflate and a
    // 4-byte Adler-32. gzip wants the raw deflate between its own header and trailer.
    const QByteArray z = qCompress(data, 9);
    if (z.size() < 10) return {};
    const QByteArray deflate = z.mid(6, z.size() - 10);
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    quint32 crc = 0xFFFFFFFFu;
    for (const char ch : data) crc = table[(crc ^ quint8(ch)) & 0xFF] ^ (crc >> 8);
    crc ^= 0xFFFFFFFFu;
    QByteArray out("\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\x03", 10);   // deflate, no name, max compression, Unix
    out += deflate;
    const quint32 size = quint32(data.size());
    for (int i = 0; i < 4; ++i) out += char((crc >> (8 * i)) & 0xFF);
    for (int i = 0; i < 4; ++i) out += char((size >> (8 * i)) & 0xFF);
    return out;
}

int rowsFor(int column, int length, int columns) {
    if (columns <= 0) return 1;
    const int end = std::max(0, column) + std::max(1, length);
    return (end - 1) / columns + 1;
}

QString bootstrapLine(const QByteArray &script, int promptColumn, int columns) {
    const QString payload = QString::fromLatin1(gzip(script).toBase64());
    const auto line = [&](int rows) {
        return QStringLiteral(" RELAY_R=%1 eval \"$(printf %s '%2' | base64 -d | gzip -dc)\"").arg(rows).arg(payload);
    };
    // The row count is part of the line it counts; two passes settle it (the digits change once).
    int rows = rowsFor(promptColumn, line(1).size(), columns);
    rows = rowsFor(promptColumn, line(rows).size(), columns);
    return line(rows);
}

}  // namespace relay::remote
