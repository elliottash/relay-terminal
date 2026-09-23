// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QUuid>

namespace relay::terminalcontext {

// In-memory PTY evidence, confined to the owning pane/thread. Returned JSON is a
// value snapshot. bytes_seen counts raw PTY bytes (including controls and echo),
// with a floor at rendered UTF-8 size when malformed input expands on decoding;
// the omission marker counts sanitized UTF-8 bytes removed from the middle.
class Records {
public:
    QString begin(QString command, QString cwd, QString host, QString origin = QStringLiteral("user"), bool expectEcho = true) {
        if (active()) close(0, QStringLiteral("interrupted"));
        resetParser();
        const QByteArray commandBytes = command.toUtf8();
        const bool unsupported = commandBytes.size() > 16384;
        command = QString::fromUtf8(prefix(commandBytes, 16384));
        cwd = QString::fromUtf8(prefix(cwd.toUtf8(), 4096));
        host = QString::fromUtf8(prefix(host.toUtf8(), 4096));
        Entry entry;
        const QString id = uuid();
        entry.json = {{"command_id", id}, {"pane_id", m_pane}, {"generation", m_generation},
                      {"sequence", double(++m_sequence)},
                      {"origin", origin == QStringLiteral("agent") ? origin : QStringLiteral("user")},
                      {"command", command}, {"cwd", cwd}, {"host", host},
                      {"started_at", double(QDateTime::currentMSecsSinceEpoch())},
                      {"ended_at", QJsonValue(QJsonValue::Null)}, {"state", "running"},
                      {"exit_status", QJsonValue(QJsonValue::Null)}, {"revision", 1},
                      {"availability", unsupported ? "unsupported" : "captured"}};
        m_entries.prepend(entry);
        while (m_entries.size() > 32) m_entries.removeLast();
        m_active = true;
        m_echo = command.toUtf8();
        m_echo.replace("\r", "");
        // Do not allocate another unbounded copy while testing for an echo.
        m_checkEcho = expectEcho && !m_echo.isEmpty() && m_echo.size() <= OutputLimit;
        if (m_checkEcho && !m_echo.endsWith('\n')) m_echo.append('\n');
        if (!m_checkEcho) m_echo.clear();
        m_stopped = unsupported;
        enforceBudget();
        return id;
    }

    void append(QByteArray raw) {
        if (!active() || raw.isEmpty()) return;
        Entry &entry = m_entries.first();
        entry.seen += raw.size();
        entry.json["revision"] = entry.json["revision"].toInt() + 1;
        for (unsigned char byte : raw) consume(byte);
        flushBatch();
        enforceBudget();
    }

    // An authenticated shell marker identifies the real output boundary for an
    // existing composer record. Keep its identity/origin/start time, discard echo.
    void outputStarted() {
        if (!active()) return;
        Entry &entry = m_entries.first();
        entry.head.clear(); entry.tail.clear();
        entry.clean = entry.seen = 0;
        entry.truncated = false;
        entry.json["revision"] = entry.json["revision"].toInt() + 1;
        m_batch.clear();
        resetParser();
        m_stopped = entry.json["availability"] == QStringLiteral("unsupported");
    }

    void finish(int exitStatus, QString availability = QStringLiteral("captured")) {
        if (active()) close(exitStatus, availability);
    }

    void resetGeneration() {
        if (active()) close(0, QStringLiteral("interrupted"));
        m_generation = uuid();
        resetParser();
    }

    QJsonArray records() const {
        QJsonArray result;
        for (const Entry &entry : m_entries) result.append(snapshot(entry));
        return result;
    }

    QJsonObject latestUser() const {
        for (const Entry &entry : m_entries)
            if (entry.json["origin"] == QStringLiteral("user") &&
                entry.json["generation"] == m_generation) return snapshot(entry);
        return {};
    }

    bool active() const { return m_active; }

private:
    static constexpr int OutputLimit = 64 * 1024;
    // Leave room for the ASCII omission marker, even with a 64-bit byte count.
    static constexpr int HalfLimit = (OutputLimit - 64) / 2;
    struct Entry {
        QJsonObject json;
        QByteArray head, tail;
        qint64 seen = 0, clean = 0;
        bool truncated = false;
    };
    enum class Parser { Text, Escape, EscapeIntermediate, Csi, Osc, OscEscape, String, StringEscape };
    QList<Entry> m_entries;
    const QString m_pane = uuid();
    QString m_generation = uuid();
    qint64 m_sequence = 0;
    bool m_active = false, m_stopped = false, m_checkEcho = false;
    Parser m_parser = Parser::Text;
    QByteArray m_osc, m_utf8, m_echo, m_echoPending, m_batch;
    int m_utf8Length = 0, m_controlUtf8Remaining = 0;

    static QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
    static bool continuation(unsigned char c) { return (c & 0xc0) == 0x80; }
    static QByteArray prefix(const QByteArray &bytes, int limit) {
        int end = qMin(limit, int(bytes.size()));
        if (end < bytes.size()) while (end > 0 && continuation(static_cast<unsigned char>(bytes[end]))) --end;
        return bytes.left(end);
    }
    static QByteArray suffix(const QByteArray &bytes, int limit) {
        int start = qMax(0, int(bytes.size()) - limit);
        while (start < bytes.size() && continuation(static_cast<unsigned char>(bytes[start]))) ++start;
        return bytes.mid(start);
    }
    static QJsonObject snapshot(const Entry &entry) {
        QJsonObject result = entry.json;
        QByteArray output = entry.head;
        if (entry.truncated) {
            const qint64 omitted = entry.clean - entry.head.size() - entry.tail.size();
            output += "\n[... " + QByteArray::number(omitted) + " bytes omitted ...]\n";
            output += entry.tail;
            if (result["availability"] == QStringLiteral("captured")) result["availability"] = "truncated";
        }
        result["output"] = QString::fromUtf8(output);
        result["bytes_seen"] = double(qMax(entry.seen, qint64(output.size())));
        return result;
    }
    void enforceBudget() {
        // Count the exact compact JSON, including escaped output, metadata,
        // punctuation and the array envelope; output caps alone are insufficient.
        qint64 bytes = 64; // Reserve the outer {mode,records} envelope too.
        for (const Entry &entry : m_entries)
            bytes += QJsonDocument(snapshot(entry)).toJson(QJsonDocument::Compact).size() + 1;
        while (bytes > 2 * 1024 * 1024 && m_entries.size() > 1) {
            bytes -= QJsonDocument(snapshot(m_entries.last())).toJson(QJsonDocument::Compact).size() + 1;
            m_entries.removeLast();
        }
    }
    void store(const QByteArray &bytes) {
        if (bytes.isEmpty()) return;
        Entry &entry = m_entries.first();
        entry.clean += bytes.size();
        if (!entry.truncated) {
            entry.head += bytes;
            if (entry.head.size() <= OutputLimit) return;
            entry.tail = suffix(entry.head, HalfLimit);
            entry.head = prefix(entry.head, HalfLimit);
            entry.truncated = true;
        } else {
            entry.tail += bytes;
            entry.tail = suffix(entry.tail, HalfLimit);
        }
    }
    void flushBatch() {
        store(m_batch);
        m_batch.clear();
    }
    void queueText(const QByteArray &bytes) {
        m_batch += bytes;
        if (m_batch.size() >= 4096) flushBatch();
    }
    void emitText(const QByteArray &bytes) {
        if (!m_checkEcho) { queueText(bytes); return; }
        m_echoPending += bytes;
        if (!m_echo.startsWith(m_echoPending)) {
            queueText(m_echoPending);
            m_checkEcho = false;
            m_echoPending.clear();
            m_echo.clear();
        } else if (m_echoPending == m_echo) {
            m_checkEcho = false;
            m_echoPending.clear();
            m_echo.clear();
        }
    }
    void flushUtf8() {
        if (!m_utf8.isEmpty()) emitText(QByteArray("\xef\xbf\xbd"));
        m_utf8.clear();
        m_utf8Length = 0;
    }
    void textByte(unsigned char byte) {
        if (!m_utf8.isEmpty()) {
            if (continuation(byte)) {
                m_utf8.append(char(byte));
                if (m_utf8.size() == m_utf8Length) {
                    // Qt validates overlong encodings, surrogates and out-of-range scalars.
                    const QString decoded = QString::fromUtf8(m_utf8);
                    if (decoded.size() != 1 || decoded.at(0).unicode() < 0x80 || decoded.at(0).unicode() > 0x9f)
                        emitText(decoded.toUtf8());
                    m_utf8.clear();
                }
                return;
            }
            flushUtf8();
        }
        if (byte >= 0xc2 && byte <= 0xf4) {
            m_utf8Length = byte < 0xe0 ? 2 : byte < 0xf0 ? 3 : 4;
            m_utf8.append(char(byte));
        } else if (byte >= 0x80) {
            emitText(QByteArray("\xef\xbf\xbd"));
        } else if (byte >= 0x20 && byte != 0x7f) {
            emitText(QByteArray(1, char(byte)));
        } else if (byte == '\n' || byte == '\t') {
            emitText(QByteArray(1, char(byte)));
        }
    }
    void endOsc() {
        if (m_osc == "133;A" || m_osc.startsWith("133;A;") ||
            m_osc == "133;D" || m_osc.startsWith("133;D;") ||
            m_osc == "7772;end-output" || m_osc.startsWith("7772;end-output;")) {
            m_stopped = true;
        }
        m_osc.clear();
        m_parser = Parser::Text;
    }
    void consume(unsigned char byte) {
        if (m_stopped) return;
        // A UTF-8 continuation byte can equal the legacy eight-bit ST control.
        // Track it inside discarded strings too, or a Unicode title can leak.
        const bool encodedControl = m_controlUtf8Remaining > 0 && continuation(byte);
        if (encodedControl) --m_controlUtf8Remaining;
        else m_controlUtf8Remaining = byte >= 0xc2 && byte <= 0xf4
            ? (byte < 0xe0 ? 1 : byte < 0xf0 ? 2 : 3) : 0;
        if ((byte == 0x18 || byte == 0x1a) && m_parser != Parser::Text) {
            m_parser = Parser::Text;
            m_osc.clear();
            return;
        }
        switch (m_parser) {
        case Parser::Text:
            if (byte == 0x1b) { flushUtf8(); m_parser = Parser::Escape; }
            else if (m_utf8.isEmpty() && byte == 0x9b) m_parser = Parser::Csi;
            else if (m_utf8.isEmpty() && byte == 0x9d) { m_osc.clear(); m_parser = Parser::Osc; }
            else if (m_utf8.isEmpty() && (byte == 0x90 || byte == 0x98 || byte == 0x9e || byte == 0x9f)) m_parser = Parser::String;
            else if (m_utf8.isEmpty() && byte >= 0x80 && byte <= 0x9f) {}
            else textByte(byte);
            break;
        case Parser::Escape:
            if (byte == '[') m_parser = Parser::Csi;
            else if (byte == ']') { m_osc.clear(); m_parser = Parser::Osc; }
            else if (byte == 'P' || byte == 'X' || byte == '^' || byte == '_') m_parser = Parser::String;
            else if (byte >= 0x20 && byte <= 0x2f) m_parser = Parser::EscapeIntermediate;
            else if (byte != 0x1b) m_parser = Parser::Text;
            break;
        case Parser::EscapeIntermediate:
            if (byte >= 0x30 && byte <= 0x7e) m_parser = Parser::Text;
            else if (byte == 0x1b) m_parser = Parser::Escape;
            break;
        case Parser::Csi:
            if (byte >= 0x40 && byte <= 0x7e) m_parser = Parser::Text;
            else if (byte == 0x1b) m_parser = Parser::Escape;
            break;
        case Parser::Osc:
            if (byte == 7 || (byte == 0x9c && !encodedControl)) endOsc();
            else if (byte == 0x1b) m_parser = Parser::OscEscape;
            else if (m_osc.size() < 64) m_osc.append(char(byte));
            break;
        case Parser::OscEscape:
            if (byte == '\\' || byte == 7 || byte == 0x9c) endOsc();
            else if (byte != 0x1b) { m_parser = Parser::Osc; if (m_osc.size() < 64) m_osc.append(char(byte)); }
            break;
        case Parser::String:
            if (byte == 0x9c && !encodedControl) m_parser = Parser::Text;
            else if (byte == 0x1b) m_parser = Parser::StringEscape;
            break;
        case Parser::StringEscape:
            m_parser = byte == '\\' ? Parser::Text : (byte == 0x1b ? Parser::StringEscape : Parser::String);
            break;
        }
    }
    void close(int status, const QString &availability) {
        flushUtf8();
        if (m_checkEcho) { queueText(m_echoPending); m_echoPending.clear(); }
        flushBatch();
        Entry &entry = m_entries.first();
        const bool interrupted = availability == QStringLiteral("interrupted");
        entry.json["state"] = interrupted ? "interrupted" : "completed";
        entry.json["exit_status"] = interrupted ? QJsonValue(QJsonValue::Null) : QJsonValue(status);
        entry.json["ended_at"] = qMax(double(QDateTime::currentMSecsSinceEpoch()),
                                           entry.json["started_at"].toDouble());
        entry.json["availability"] = entry.json["availability"] == "unsupported" && !interrupted
            ? QStringLiteral("unsupported") : (availability == "unsupported" || interrupted || availability == "truncated")
            ? availability : QStringLiteral("captured");
        entry.json["revision"] = entry.json["revision"].toInt() + 1;
        if (entry.json["availability"] == QStringLiteral("unsupported")) {
            entry.head.clear();
            entry.tail.clear();
            entry.clean = 0;
            entry.truncated = false;
        }
        if (entry.truncated) entry.tail = suffix(entry.tail, HalfLimit);
        m_active = false;
        resetParser();
        enforceBudget();
    }
    void resetParser() {
        m_parser = Parser::Text;
        m_stopped = m_checkEcho = false;
        m_utf8Length = m_controlUtf8Remaining = 0;
        m_osc.clear(); m_utf8.clear(); m_echo.clear(); m_echoPending.clear();
    }
};

// Route shell boundaries before deferred engine notifications: one PTY callback
// can contain multiple commands. Command markers require the connection token;
// OSC 133 completion remains an unauthenticated shell-attribution limitation.
class Stream {
public:
    bool append(Records &records, QByteArray bytes, QString token, QString host) {
        bool changed = false;
        QByteArray plain;
        auto flush = [&] { records.append(plain); plain.clear(); };
        for (char byte : bytes) {
            if (m_state == State::Text) {
                if (byte == '\x1b') m_state = State::Escape;
                else plain.append(byte);
            } else if (m_state == State::Escape) {
                if (byte == ']') {
                    flush();
                    m_wire = "\x1b]";
                    m_state = State::Osc;
                } else {
                    plain.append('\x1b');
                    if (byte != '\x1b') { plain.append(byte); m_state = State::Text; }
                }
            } else {
                const bool end = byte == '\a' || (m_state == State::OscEscape && byte == '\\');
                if (m_overflow) plain.append(byte);
                else m_wire.append(byte);
                if (end) {
                    if (m_overflow) flush();
                    else {
                        const int terminator = byte == '\a' ? 1 : 2;
                        changed = marker(records, m_wire.mid(2, m_wire.size() - 2 - terminator),
                                         token, host) || changed;
                    }
                    m_wire.clear(); m_overflow = false; m_state = State::Text;
                } else {
                    m_state = byte == '\x1b' ? State::OscEscape : State::Osc;
                    if (!m_overflow && m_wire.size() > 65536) {
                        // Forward a too-large OSC to the sanitizer, retaining no
                        // marker bytes until its terminator; never parse its suffix.
                        plain += m_wire;
                        m_wire.clear();
                        m_overflow = true;
                    }
                }
            }
            if (plain.size() >= 4096) flush();
        }
        flush();
        return changed;
    }

    void clear() { m_wire.clear(); m_state = State::Text; m_overflow = false; }

private:
    enum class State { Text, Escape, Osc, OscEscape };
    State m_state = State::Text;
    QByteArray m_wire;
    bool m_overflow = false;

    bool marker(Records &records, const QByteArray &mark, const QString &token, const QString &host) {
        if (mark.startsWith("777;notify;relay-command;")) {
            const auto fields = mark.split(';');
            if (fields.size() != 6 || token.isEmpty() || fields[3] != token.toUtf8()) return false;
            const QByteArray commandBytes = QByteArray::fromBase64(fields[4]);
            const QByteArray cwdBytes = QByteArray::fromBase64(fields[5]);
            // Qt's default decoder silently skips invalid bytes. A shell marker
            // must have canonical base64 and valid UTF-8, not a repaired command.
            if (commandBytes.toBase64() != fields[4] || cwdBytes.toBase64() != fields[5]) return false;
            const QString command = QString::fromUtf8(commandBytes);
            const QString cwd = QString::fromUtf8(cwdBytes);
            if (command.toUtf8() != commandBytes || cwd.toUtf8() != cwdBytes) return false;
            const auto latest = records.records();
            const bool same = records.active() && !latest.isEmpty()
                && latest.first().toObject()["command"].toString() == command;
            if (same) records.outputStarted();
            else {
                records.begin(command.isEmpty() ? QStringLiteral("[command text unavailable]") : command,
                              cwd, host, QStringLiteral("user"), false);
                if (command.isEmpty()) records.finish(-1, QStringLiteral("unsupported"));
            }
            return true;
        }
        if (mark.startsWith("133;D;")) {
            bool ok = false;
            const int code = mark.mid(6).toInt(&ok);
            if (ok && records.active()) { records.finish(code); return true; }
        }
        records.append(m_wire);
        return records.active() && (mark == "133;A" || mark.startsWith("133;A;") ||
            mark == "7772;end-output" || mark.startsWith("7772;end-output;"));
    }
};

} // namespace relay::terminalcontext
