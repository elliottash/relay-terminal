// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Voice transcription (issue NY7Z, protocol section 16): hold the voice key (Right Alt by default)
// or press the microphone chip, speak, and the transcript is inserted into the composer.
//
// Relay records with whichever command-line capture tool the desktop already has — pw-record
// (PipeWire), parecord (PulseAudio), arecord (ALSA), then ffmpeg — so the app needs no audio
// library, no build-time dependency and no permission of its own beyond the microphone the tool
// opens. The clip is a 16 kHz mono WAV in a temporary directory; the worker reads it, sends it to
// OpenRouter and returns the text (backend/relay_core/voice.py), and the GUI deletes it.
//
// Everything except Capture is a pure rule so it can be tested without a microphone, a provider or
// a window: which tool to run and with what arguments, which key event is the hold key, where the
// transcript lands in the composer's text, and how a WAV written by a killed recorder is repaired.
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <functional>

class QProcess;
class QTimer;

namespace relay::voice {

// ----- capture tools ---------------------------------------------------------------------------
// In preference order: each one is asked for 16 kHz mono 16-bit WAV, which is what the models take
// and a twentieth of the bytes of CD-quality stereo.
QStringList captureTools();

// The command line for a tool, or an empty list for a tool that is not one of captureTools().
// `device` is the tool's own device/source name; empty means the desktop default.
QStringList captureArguments(const QString &tool, const QString &path, const QString &device = QString());

// The first tool in preference order that `available` accepts, or an empty string when there is
// none. A `preferred` tool that is available wins; one that is not falls back to the order.
QString chooseTool(const QString &preferred, const std::function<bool(const QString &)> &available);

// True when the tool is on PATH. The default for chooseTool() in the application.
bool toolOnPath(const QString &tool);

QString missingToolsMessage();

// ----- capture sources --------------------------------------------------------------------------
// The machine's capture sources as (name, description) pairs, in the current tool's own namespace:
// PulseAudio/PipeWire source names for pw-record, parecord and ffmpeg, ALSA PCM names for arecord.
// `name` is what captureArguments() passes as `device`; `description` is the human line the listing
// showed, empty when it carried none. All three are pure parsers over a tool's listing text, so a
// dropdown of microphones can be tested without audio hardware. `.monitor` sources are desktop
// outputs rather than microphones and are skipped.
QList<QPair<QString, QString>> sourcesFromPactl(const QString &text);    // `pactl list sources`
QList<QPair<QString, QString>> devicesFromArecord(const QString &text);  // `arecord -L`
QList<QPair<QString, QString>> sourcesFromPwDump(const QString &text);   // `pw-dump Node`

// The listing for the tool that would record, run synchronously with a short timeout. An unknown or
// empty tool, a missing listing binary, a failure or a timeout answers an empty list — the dropdown
// then offers only the desktop default. Like toolOnPath(), this is Linux audio only.
QList<QPair<QString, QString>> captureDevices(const QString &tool);

// ----- the hold key ----------------------------------------------------------------------------
// Settings values for `voice/hold_key`. Right Alt is what Warp uses (the owner's Warp settings have
// voice_input_toggle_key = "alt_right"); it is also AltGr on most non-US layouts, which is why it is
// a setting, why "off" exists, and why a recording is cancelled as soon as another key is pressed —
// holding AltGr to type é must never leave a recording running.
QStringList holdKeys();                          // "right-alt", "right-ctrl", "f9", "off"
QString holdKeyLabel(const QString &id);         // "Right Alt (AltGr on many layouts)"

// Whether a key event is the configured hold key. `virtualKey` and `scanCode` are the event's
// native fields: on X11 and Wayland the virtual key is the xkb keysym, which is the only way to
// tell the two Alt keys apart (Qt reports both as Qt::Key_Alt).
bool isHoldKey(const QString &setting, int key, quint32 virtualKey, quint32 scanCode);

// Keyboard layouts that type with AltGr, where Right Alt would be a poor default.
QStringList layoutsFromKeyboardConfig(const QString &fileText);   // /etc/default/keyboard
bool layoutTypesWithAltGr(const QStringList &layouts);
QString defaultHoldKey(const QStringList &layouts);               // "right-alt", or "off" for those

// ----- the transcript in the composer -----------------------------------------------------------
struct Insertion {
    QString text;
    int cursor = 0;
};
// Insert a transcript at the cursor, adding the spaces that keep it from running into the words
// around it. The cursor ends after the inserted text, as if it had been typed.
Insertion insertTranscript(const QString &existing, int cursor, const QString &transcript);

// ----- clips -------------------------------------------------------------------------------------
int clampSeconds(int seconds);                   // recording cap, 5..600, default 120
constexpr int kDefaultSeconds = 120;
constexpr qint64 kMinimumMs = 400;               // shorter than this is a tap, not speech

// A recorder that is killed mid-write leaves RIFF/data sizes that do not match the file, and a
// provider then decodes silence or rejects the clip. Rewrites both from the real length; returns
// false when the buffer is not a WAV at all. Sizes that are already right are left alone.
bool repairWav(QByteArray &wav);

// Milliseconds of audio in a WAV buffer, or -1 when it cannot be read from the header.
qint64 wavDurationMs(const QByteArray &wav);

// ----- recording ----------------------------------------------------------------------------------
struct Options {
    QString tool;            // empty: whichever of captureTools() is on PATH
    QString device;          // empty: the desktop default source
    int seconds = kDefaultSeconds;
};

// One recording. Owns the capture process and the temporary file; the file stays on disk after
// `ready` so the worker can read it, and the caller deletes it once transcribed.
class Capture final : public QObject {
    Q_OBJECT
public:
    explicit Capture(QObject *parent = nullptr);
    ~Capture() override;

    // Starts a recording. False with `error` set when no tool is available, the temporary file
    // cannot be created, or the tool fails to start.
    bool start(const Options &options, QString *error);
    void stop();      // finish the clip: `ready` follows, or `failed`
    void cancel();    // stop and delete the clip: neither signal follows

    bool recording() const { return m_recording; }
    QString tool() const { return m_tool; }
    qint64 elapsedMs() const;

Q_SIGNALS:
    void ready(const QString &path, qint64 milliseconds);
    void failed(const QString &message);
    void elapsed(qint64 milliseconds);   // once a second, for the chip

private:
    void finish(bool keep);
    void removeFile();

    QProcess *m_process = nullptr;
    QTimer *m_tick = nullptr;
    QString m_path, m_tool;
    qint64 m_started = 0;
    int m_seconds = kDefaultSeconds;
    bool m_recording = false;
    bool m_stopping = false;
};

}  // namespace relay::voice
