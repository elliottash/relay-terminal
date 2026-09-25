// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Voice.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>

#ifndef Q_OS_WIN
#include <signal.h>
#include <sys/types.h>
#endif

namespace relay::voice {

namespace {

// 16 kHz mono 16-bit: what the speech models take, and a twentieth of the bytes of CD stereo.
constexpr int kRate = 16000;

// X11/xkb keysyms. Qt reports both Alt keys as Qt::Key_Alt, so the keysym is the only way to tell
// them apart; on layouts that use the right Alt as a third level it arrives as ISO_Level3_Shift.
constexpr quint32 kAltR = 0xffea, kAltL = 0xffe9, kLevel3 = 0xfe03, kLevel5 = 0xfe11;
constexpr quint32 kControlR = 0xffe4, kControlL = 0xffe3;
// evdev scan codes, used when a platform reports no keysym.
constexpr quint32 kScanAltR = 108, kScanControlR = 105;

QString variantOf(const QString &layout) {
    const int open = layout.indexOf(QLatin1Char('('));
    return open < 0 ? QString() : layout.mid(open + 1, layout.size() - open - 2);
}

QString baseOf(const QString &layout) {
    const int open = layout.indexOf(QLatin1Char('('));
    return (open < 0 ? layout : layout.left(open)).trimmed().toLower();
}

// A chunked RIFF file: the offset of a chunk's size field, or -1.
int chunkSizeOffset(const QByteArray &wav, const char *id) {
    int at = 12;   // past "RIFF", size, "WAVE"
    while (at + 8 <= wav.size()) {
        const int size = int(quint8(wav[at + 4])) | (int(quint8(wav[at + 5])) << 8)
                         | (int(quint8(wav[at + 6])) << 16) | (int(quint8(wav[at + 7])) << 24);
        if (wav.mid(at, 4) == QByteArray(id)) return at + 4;
        if (size < 0) return -1;                       // a size that overflows: give up
        at += 8 + size + (size & 1);                   // chunks are word-aligned
    }
    return -1;
}

quint32 readLe32(const QByteArray &data, int at) {
    if (at < 0 || at + 4 > data.size()) return 0;
    return quint32(quint8(data[at])) | (quint32(quint8(data[at + 1])) << 8)
           | (quint32(quint8(data[at + 2])) << 16) | (quint32(quint8(data[at + 3])) << 24);
}

quint32 readLe16(const QByteArray &data, int at) {
    if (at < 0 || at + 2 > data.size()) return 0;
    return quint32(quint8(data[at])) | (quint32(quint8(data[at + 1])) << 8);
}

void writeLe32(QByteArray &data, int at, quint32 value) {
    for (int i = 0; i < 4; ++i) data[at + i] = char((value >> (8 * i)) & 0xff);
}

}  // namespace

// ----- capture tools ---------------------------------------------------------------------------
QStringList captureTools() {
    return {QStringLiteral("pw-record"), QStringLiteral("parecord"), QStringLiteral("arecord"),
            QStringLiteral("ffmpeg")};
}

QStringList captureArguments(const QString &tool, const QString &path, const QString &device) {
    const QString rate = QString::number(kRate);
    if (tool == QStringLiteral("pw-record")) {
        QStringList args{QStringLiteral("--rate=") + rate, QStringLiteral("--channels=1"),
                         QStringLiteral("--format=s16")};
        if (!device.isEmpty()) args << QStringLiteral("--target=") + device;
        return args << path;
    }
    if (tool == QStringLiteral("parecord")) {
        QStringList args{QStringLiteral("--rate=") + rate, QStringLiteral("--channels=1"),
                         QStringLiteral("--format=s16le"), QStringLiteral("--file-format=wav")};
        if (!device.isEmpty()) args << QStringLiteral("--device=") + device;
        return args << path;
    }
    if (tool == QStringLiteral("arecord")) {
        QStringList args{QStringLiteral("-q"), QStringLiteral("-t"), QStringLiteral("wav"),
                         QStringLiteral("-f"), QStringLiteral("S16_LE"), QStringLiteral("-r"), rate,
                         QStringLiteral("-c"), QStringLiteral("1")};
        if (!device.isEmpty()) args << QStringLiteral("-D") << device;
        return args << path;
    }
    if (tool == QStringLiteral("ffmpeg")) {
        // Last resort: PulseAudio's API is what PipeWire also serves, so "-f pulse" covers both.
        return {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                QStringLiteral("-nostdin"), QStringLiteral("-y"),
                QStringLiteral("-f"), QStringLiteral("pulse"),
                QStringLiteral("-i"), device.isEmpty() ? QStringLiteral("default") : device,
                QStringLiteral("-ar"), rate, QStringLiteral("-ac"), QStringLiteral("1"),
                QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), path};
    }
    return {};
}

QString chooseTool(const QString &preferred, const std::function<bool(const QString &)> &available) {
    if (!available) return {};
    const QStringList tools = captureTools();
    if (!preferred.isEmpty() && tools.contains(preferred) && available(preferred)) return preferred;
    for (const QString &tool : tools)
        if (available(tool)) return tool;
    return {};
}

bool toolOnPath(const QString &tool) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    // The supported capture arguments use PipeWire, PulseAudio or ALSA; an installed
    // ffmpeg.exe does not make those Linux audio inputs available on Windows or macOS.
    Q_UNUSED(tool);
    return false;
#else
    return !QStandardPaths::findExecutable(tool).isEmpty();
#endif
}

QString missingToolsMessage() {
#ifdef Q_OS_WIN
    return QStringLiteral("Microphone capture is not available in this Windows beta. "
                          "Relay's current recorder integration requires Linux audio services.");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Microphone capture is not available in this macOS beta. "
                          "Relay's current recorder integration requires Linux audio services.");
#else
    return QStringLiteral("No microphone recorder found. Install pipewire-utils (pw-record), "
                          "pulseaudio-utils (parecord) or alsa-utils (arecord).");
#endif
}

// ----- capture sources --------------------------------------------------------------------------
QList<QPair<QString, QString>> sourcesFromPactl(const QString &text) {
    // `pactl list sources` prints one block per source: a "Source #47" line, then tab-indented
    // fields, of which Name is the source capture tools take and Description the human line. The
    // Properties list inside a block carries lower-case look-alikes (device.description, node.name)
    // that must not shadow the fields.
    QList<QPair<QString, QString>> sources;
    QString name, description;
    bool inBlock = false;
    auto flush = [&] {
        // .monitor sources are the desktop's outputs, not microphones: not offered.
        if (inBlock && !name.isEmpty() && !name.endsWith(QStringLiteral(".monitor")))
            sources.append({name, description});
        name.clear();
        description.clear();
    };
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QStringLiteral("Source #"))) {
            flush();
            inBlock = true;
            continue;
        }
        if (!inBlock) continue;
        if (line.startsWith(QStringLiteral("Name:")))
            name = line.mid(QStringLiteral("Name:").size()).trimmed();
        else if (line.startsWith(QStringLiteral("Description:")))
            description = line.mid(QStringLiteral("Description:").size()).trimmed();
    }
    flush();
    return sources;
}

QList<QPair<QString, QString>> devicesFromArecord(const QString &text) {
    // `arecord -L` lists PCM names at the left margin, each followed by one or more indented
    // description lines. The indented lines are joined with a space.
    QList<QPair<QString, QString>> devices;
    QString name, description;
    auto flush = [&] {
        if (!name.isEmpty()) devices.append({name, description});
        name.clear();
        description.clear();
    };
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        const bool indented = raw.startsWith(QLatin1Char(' ')) || raw.startsWith(QLatin1Char('\t'));
        if (indented) {
            if (!name.isEmpty()) {
                if (!description.isEmpty()) description += QLatin1Char(' ');
                description += line;
            }
        } else if (line.contains(QLatin1Char(' '))) {
            // A message at the margin, not a PCM name: arecord's names never contain spaces.
            continue;
        } else {
            flush();
            name = line;
        }
    }
    flush();
    return devices;
}

QList<QPair<QString, QString>> sourcesFromPwDump(const QString &text) {
    // `pw-dump Node` prints one JSON array of the graph's nodes, full of properties nobody needs
    // here. A source is a node whose media.class is Audio/Source; the name pw-record takes as
    // --target is node.name, and node.description is the human line (a monitor node carries
    // device.class "monitor" and often a ".monitor" name — the desktop's own output, not a mic).
    QList<QPair<QString, QString>> sources;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isArray()) return sources;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject props = value.toObject().value(QStringLiteral("info")).toObject()
                                      .value(QStringLiteral("props")).toObject();
        if (props.value(QStringLiteral("media.class")).toString() != QStringLiteral("Audio/Source"))
            continue;
        const QString name = props.value(QStringLiteral("node.name")).toString();
        if (name.isEmpty()) continue;
        if (name.endsWith(QStringLiteral(".monitor"))
            || props.value(QStringLiteral("device.class")).toString() == QStringLiteral("monitor"))
            continue;
        QString description = props.value(QStringLiteral("node.description")).toString();
        if (description.isEmpty())
            description = props.value(QStringLiteral("device.description")).toString();
        sources.append({name, description});
    }
    return sources;
}

QList<QPair<QString, QString>> captureDevices(const QString &tool) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    // toolOnPath() already answers false for these: there is no Linux audio namespace to list.
    Q_UNUSED(tool);
    return {};
#else
    const bool alsa = tool == QStringLiteral("arecord");
    const bool pulse = tool == QStringLiteral("pw-record") || tool == QStringLiteral("parecord")
                       || tool == QStringLiteral("ffmpeg");
    if (!alsa && !pulse) return {};  // no recorder on PATH, or a name captureArguments() would not run
    // A wedged listing must not wedge the settings page open: ~2 s each to start and to finish.
    auto runListing = [](const QString &program, const QStringList &args, QString *out) {
        QProcess listing;
        listing.start(program, args);
        if (!listing.waitForStarted(2000) || !listing.waitForFinished(2000)) {
            listing.kill();
            listing.waitForFinished(500);
            return false;
        }
        if (listing.exitStatus() != QProcess::NormalExit || listing.exitCode() != 0) return false;
        *out = QString::fromUtf8(listing.readAllStandardOutput());
        return true;
    };
    QString text;
    if (alsa) {
        if (toolOnPath(QStringLiteral("arecord")) && runListing(QStringLiteral("arecord"), {QStringLiteral("-L")}, &text))
            return devicesFromArecord(text);
        return {};
    }
    // pw-record, parecord and ffmpeg all capture from the PulseAudio namespace that PipeWire also
    // serves, so pactl's listing is the shared source of names for them.
    if (toolOnPath(QStringLiteral("pactl"))
        && runListing(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("sources")}, &text))
        return sourcesFromPactl(text);
    // A PipeWire machine need not have pulseaudio-utils, so pactl can be missing (or fail without
    // pipewire-pulse) while pw-record works: the graph's own dump names the same nodes.
    if (tool == QStringLiteral("pw-record") && toolOnPath(QStringLiteral("pw-dump"))
        && runListing(QStringLiteral("pw-dump"), {QStringLiteral("Node")}, &text))
        return sourcesFromPwDump(text);
    return {};
#endif
}

// ----- the hold key ----------------------------------------------------------------------------
QStringList holdKeys() {
    return {QStringLiteral("right-alt"), QStringLiteral("right-ctrl"), QStringLiteral("f9"),
            QStringLiteral("off")};
}

QString holdKeyLabel(const QString &id) {
    if (id == QStringLiteral("right-alt")) return QStringLiteral("Right Alt (AltGr on many layouts)");
    if (id == QStringLiteral("right-ctrl")) return QStringLiteral("Right Ctrl");
    if (id == QStringLiteral("f9")) return QStringLiteral("F9");
    return QStringLiteral("Off (microphone button only)");
}

bool isHoldKey(const QString &setting, int key, quint32 virtualKey, quint32 scanCode) {
    if (setting == QStringLiteral("off")) return false;
    if (setting == QStringLiteral("f9")) return key == Qt::Key_F9;
    if (setting == QStringLiteral("right-ctrl")) {
        if (virtualKey == kControlR) return true;
        if (virtualKey == kControlL) return false;
        if (virtualKey != 0) return false;
        return key == Qt::Key_Control && scanCode == kScanControlR;
    }
    // Right Alt, however the layout names it: Alt_R, or the third-level shift it becomes on the
    // layouts where it types characters.
    if (virtualKey == kAltR || virtualKey == kLevel3 || virtualKey == kLevel5) return true;
    if (virtualKey == kAltL) return false;
    if (virtualKey != 0) return false;
    if (key == Qt::Key_AltGr) return true;
    return (key == Qt::Key_Alt || key == Qt::Key_Meta) && scanCode == kScanAltR;
}

QStringList layoutsFromKeyboardConfig(const QString &fileText) {
    QStringList layouts, variants;
    const auto lines = fileText.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('#'))) continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) continue;
        const QString name = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\''))))
            value = value.mid(1, value.size() - 2);
        if (name == QStringLiteral("XKBLAYOUT")) layouts = value.split(QLatin1Char(','));
        else if (name == QStringLiteral("XKBVARIANT")) variants = value.split(QLatin1Char(','));
    }
    QStringList result;
    for (int i = 0; i < layouts.size(); ++i) {
        const QString layout = layouts.at(i).trimmed();
        if (layout.isEmpty()) continue;
        const QString variant = i < variants.size() ? variants.at(i).trimmed() : QString();
        result << (variant.isEmpty() ? layout : layout + QLatin1Char('(') + variant + QLatin1Char(')'));
    }
    return result;
}

bool layoutTypesWithAltGr(const QStringList &layouts) {
    // Only plain US and its shuffled-letter variants leave the right Alt free; everywhere else it
    // is the third-level shift that types €, é, ß and the rest.
    static const QStringList safeVariants{QStringLiteral(""), QStringLiteral("dvorak"), QStringLiteral("colemak"),
                                          QStringLiteral("workman"), QStringLiteral("norman"), QStringLiteral("mac")};
    for (const QString &layout : layouts) {
        if (baseOf(layout) != QStringLiteral("us")) return true;
        if (!safeVariants.contains(variantOf(layout).toLower())) return true;
    }
    return false;
}

QString defaultHoldKey(const QStringList &layouts) {
    return layoutTypesWithAltGr(layouts) ? QStringLiteral("off") : QStringLiteral("right-alt");
}

// ----- the transcript in the composer -----------------------------------------------------------
Insertion insertTranscript(const QString &existing, int cursor, const QString &transcript) {
    const QString text = transcript.trimmed();
    const int at = qBound(0, cursor, existing.size());
    if (text.isEmpty()) return {existing, at};
    const QChar before = at > 0 ? existing.at(at - 1) : QChar();
    const QChar after = at < existing.size() ? existing.at(at) : QChar();
    // A space before, unless the line is empty there or already ends in a space, a newline or an
    // opener the speech should follow directly.
    const bool space = at > 0 && !before.isSpace() && before != QLatin1Char('(')
                       && before != QLatin1Char('[') && before != QLatin1Char('@');
    // A space after only when there is a word right there; punctuation the speech runs into keeps
    // its place.
    const bool trailing = at < existing.size() && !after.isSpace()
                          && after != QLatin1Char(')') && after != QLatin1Char(']')
                          && after != QLatin1Char(',') && after != QLatin1Char('.');
    const QString inserted = (space ? QStringLiteral(" ") : QString()) + text
                             + (trailing ? QStringLiteral(" ") : QString());
    QString result = existing;
    result.insert(at, inserted);
    return {result, at + int(inserted.size()) - (trailing ? 1 : 0)};
}

// ----- clips -------------------------------------------------------------------------------------
int clampSeconds(int seconds) {
    if (seconds <= 0) return kDefaultSeconds;
    return qBound(5, seconds, 600);
}

bool repairWav(QByteArray &wav) {
    if (wav.size() < 44 || wav.left(4) != QByteArray("RIFF") || wav.mid(8, 4) != QByteArray("WAVE"))
        return false;
    bool changed = false;
    const quint32 riff = quint32(wav.size() - 8);
    if (readLe32(wav, 4) != riff) { writeLe32(wav, 4, riff); changed = true; }
    const int dataSize = chunkSizeOffset(wav, "data");
    if (dataSize > 0) {
        const quint32 actual = quint32(wav.size() - (dataSize + 4));
        if (readLe32(wav, dataSize) != actual) { writeLe32(wav, dataSize, actual); changed = true; }
    }
    return changed || dataSize > 0;
}

qint64 wavDurationMs(const QByteArray &wav) {
    if (wav.size() < 44 || wav.left(4) != QByteArray("RIFF") || wav.mid(8, 4) != QByteArray("WAVE"))
        return -1;
    const int fmt = chunkSizeOffset(wav, "fmt ");
    const int data = chunkSizeOffset(wav, "data");
    if (fmt < 0 || data < 0) return -1;
    const quint32 channels = readLe16(wav, fmt + 4 + 2);
    const quint32 rate = readLe32(wav, fmt + 4 + 4);
    const quint32 bits = readLe16(wav, fmt + 4 + 14);
    if (rate == 0 || channels == 0 || bits == 0) return -1;
    // The stored size can be wrong (a killed recorder); the bytes that are really there cannot.
    const qint64 stored = readLe32(wav, data);
    const qint64 present = wav.size() - (data + 4);
    const qint64 bytes = (stored > 0 && stored <= present) ? stored : present;
    return bytes * 1000 / qint64(rate * channels * (bits / 8));
}

// ----- recording ----------------------------------------------------------------------------------
Capture::Capture(QObject *parent) : QObject(parent) {}

Capture::~Capture() {
    if (m_recording) cancel();
}

bool Capture::start(const Options &options, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (m_recording) return fail(QStringLiteral("A recording is already running."));
    m_tool = chooseTool(options.tool, toolOnPath);
    if (m_tool.isEmpty()) return fail(missingToolsMessage());
    // A named temporary file, kept after close: the recorder writes it and the caller deletes it
    // once the worker has read it.
    QTemporaryFile file(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("relay-voice-XXXXXX.wav")));
    file.setAutoRemove(false);
    if (!file.open()) return fail(QStringLiteral("Could not create a file for the recording."));
    m_path = file.fileName();
    file.close();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { finish(m_stopping); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError code) {
        if (!m_recording) return;
        const QString tool = m_tool;
        const bool missing = code == QProcess::FailedToStart;
        m_recording = false;
        m_stopping = false;
        if (m_tick) m_tick->stop();
        if (m_process) { m_process->deleteLater(); m_process = nullptr; }
        removeFile();
        Q_EMIT failed(missing ? QStringLiteral("%1 could not be started.").arg(tool)
                              : QStringLiteral("%1 stopped unexpectedly.").arg(tool));
    });
    m_process->start(m_tool, captureArguments(m_tool, m_path, options.device));
    if (!m_process->waitForStarted(3000)) {
        removeFile();
        delete m_process;
        m_process = nullptr;
        return fail(QStringLiteral("%1 could not be started.").arg(m_tool));
    }
    m_recording = true;
    m_stopping = false;
    m_started = QDateTime::currentMSecsSinceEpoch();
    m_seconds = clampSeconds(options.seconds);
    if (!m_tick) {
        m_tick = new QTimer(this);
        m_tick->setInterval(1000);
        connect(m_tick, &QTimer::timeout, this, [this] {
            if (!m_recording) return;
            Q_EMIT elapsed(elapsedMs());
            if (elapsedMs() >= qint64(m_seconds) * 1000) stop();   // the recording cap
        });
    }
    m_tick->start();
    return true;
}

qint64 Capture::elapsedMs() const {
    return m_started ? QDateTime::currentMSecsSinceEpoch() - m_started : 0;
}

void Capture::stop() {
    if (!m_recording || m_stopping) return;
    m_stopping = true;
    if (m_tick) m_tick->stop();
    // SIGINT, not terminate(): every one of these tools finalizes the WAV header on an interrupt,
    // and repairWav() covers the one that does not.
#ifdef Q_OS_WIN
    if (m_process) m_process->terminate();
#else
    const qint64 pid = m_process ? m_process->processId() : 0;
    if (pid > 0) ::kill(pid_t(pid), SIGINT);
#endif
    QTimer::singleShot(2000, this, [this] {
        if (m_recording && m_process && m_process->state() != QProcess::NotRunning) m_process->terminate();
    });
    QTimer::singleShot(3000, this, [this] {
        if (m_recording && m_process && m_process->state() != QProcess::NotRunning) m_process->kill();
    });
}

void Capture::cancel() {
    if (!m_recording) return;
    m_recording = false;
    m_stopping = false;
    if (m_tick) m_tick->stop();
    if (m_process) {
#ifdef Q_OS_WIN
        m_process->terminate();
#else
        const qint64 pid = m_process->processId();
        if (pid > 0) ::kill(pid_t(pid), SIGINT);
#endif
        m_process->waitForFinished(1000);
        if (m_process->state() != QProcess::NotRunning) m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    removeFile();
}

void Capture::finish(bool keep) {
    if (!m_recording) return;
    m_recording = false;
    m_stopping = false;
    if (m_tick) m_tick->stop();
    const QString tool = m_tool;
    if (m_process) { m_process->deleteLater(); m_process = nullptr; }
    if (!keep) {
        removeFile();
        Q_EMIT failed(QStringLiteral("%1 stopped before the recording was finished.").arg(tool));
        return;
    }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        removeFile();
        Q_EMIT failed(QStringLiteral("The recording could not be read."));
        return;
    }
    QByteArray wav = file.readAll();
    file.close();
    if (repairWav(wav) && file.open(QIODevice::WriteOnly)) {
        file.write(wav);
        file.close();
    }
    const qint64 duration = wavDurationMs(wav);
    if (wav.size() < 512 || duration == 0) {
        removeFile();
        Q_EMIT failed(QStringLiteral("Nothing was recorded. Check that the microphone is not muted."));
        return;
    }
    if (duration > 0 && duration < kMinimumMs) {
        removeFile();
        Q_EMIT failed(QStringLiteral("Too short — hold the key while you speak."));
        return;
    }
    Q_EMIT ready(m_path, duration > 0 ? duration : elapsedMs());
    m_path.clear();
}

void Capture::removeFile() {
    if (!m_path.isEmpty()) QFile::remove(m_path);
    m_path.clear();
}

}  // namespace relay::voice
