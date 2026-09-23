// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Read aloud (card #MDA7; owner, 2026-09-22: "read-aloud ... definitely, add that now"): an agent
// reply spoken with the system's own voice. `/speak` reads the pane's last reply, `/speak stop` or
// Esc stops it, and Options › Voice › "Read replies aloud automatically" reads each one as it
// finishes. The shape is ChatGPT's "Read aloud" under a reply: the OS default voice and rate, no
// voice picker.
//
// Two engines. Where QtTextToSpeech was found at configure time (RELAY_HAVE_QTTEXTTOSPEECH) and
// the platform has a speech plugin, QTextToSpeech speaks with the OS voices. Otherwise the first
// command-line speaker on PATH does: spd-say (speech-dispatcher), espeak-ng, espeak, `say` on
// macOS, PowerShell's System.Speech on Windows. The text goes in on stdin or as one argument of an
// argument vector, never through a shell.
//
// Like Voice.h, everything except Speaker is a pure rule, so it is tested without a sound card:
// what of a reply's Markdown is worth saying, where a long reply is cut so the first sentence
// starts at once and Stop takes effect between pieces, and which tool runs with what arguments.
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>

class QProcess;
class QTextToSpeech;

namespace relay::speech {

// ----- what is said -----------------------------------------------------------------------------
// A reply's Markdown as text a voice can read, one block (paragraph, heading, list item, table
// row) per line. Fenced code blocks are dropped and the first one becomes "Code block omitted." —
// said once per reply, not once per block. Link and image syntax, bare URLs, reference-link
// definitions, table pipes and separator rows, rules and HTML tags are dropped; a link keeps its
// text, inline code keeps its content, emphasis markers go. A heading or a list item that does
// not end in punctuation gets a full stop, so the voice pauses where the eye would.
QString speakableText(const QString &markdown);

// The pieces spoken one after another: one sentence each, never across a line of
// speakableText(), and a sentence longer than `maxChars` split at the last space before the
// limit. Empty pieces are never returned.
QStringList chunks(const QString &speakable, int maxChars = 300);

// ----- command-line speakers --------------------------------------------------------------------
enum class Os { Linux, Mac, Windows };
Os currentOs();

// In preference order for a platform: spd-say, espeak-ng, espeak on Linux; say, then the two
// espeaks (Homebrew) on macOS; powershell on Windows.
QStringList cliTools(Os os = currentOs());

// The first of cliTools(os) that `available` accepts, or an empty string.
QString chooseTool(const std::function<bool(const QString &)> &available, Os os = currentOs());

// True when the tool is on PATH (read at the time of the call).
bool toolOnPath(const QString &tool);

struct Invocation {
    QStringList arguments;
    QByteArray input;        // written to the tool's stdin, which is then closed; empty: none
};
// How `tool` speaks `text` and exits when it has finished. Empty arguments for an unknown tool.
Invocation speakInvocation(const QString &tool, const QString &text);

// What stops a tool's speech besides killing it: spd-say only hands the text to a daemon, which
// goes on speaking after the client is gone, so it is told to stop. Empty for the others.
QStringList stopArguments(const QString &tool);

QString missingToolsMessage(Os os = currentOs());

// ----- speaking ---------------------------------------------------------------------------------
// One utterance at a time, app-wide: speak() stops whatever was being read, in any pane. The
// owner is the pane that asked, so only that pane shows "Reading aloud · Esc to stop".
class Speaker final : public QObject {
    Q_OBJECT
public:
    static Speaker &instance();

    // Starts reading `markdown`. False with `error` set when there is nothing to say or no engine.
    bool speak(const QString &markdown, QObject *owner, QString *error = nullptr);
    void stop();

    bool speaking() const { return m_speaking; }
    QObject *owner() const { return m_owner.data(); }

    // "Qt TextToSpeech", "spd-say", …; empty when nothing can speak here.
    QString engine() const;

    // Test seam: the command-line tool to use instead of choosing one; empty restores the choice.
    void setToolForTesting(const QString &tool) { m_toolOverride = tool; }

Q_SIGNALS:
    void stateChanged();                  // started or stopped (finished, stopped, failed)
    void failed(const QString &message);

private:
    explicit Speaker(QObject *parent);
    ~Speaker() override;
    void next();
    void finish();
    bool useQt();
    QString tool() const;

    QStringList m_chunks;
    int m_index = 0;
    quint64 m_generation = 0;             // a stopped utterance's late signals are ignored
    bool m_speaking = false;
    QPointer<QObject> m_owner;
    QProcess *m_process = nullptr;
    QString m_tool, m_toolOverride;
    QTextToSpeech *m_tts = nullptr;       // only with RELAY_HAVE_QTTEXTTOSPEECH
    bool m_qtChecked = false, m_qtUsable = false, m_qtStarted = false;
};

}  // namespace relay::speech
