// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Speech.h"

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#ifdef RELAY_HAVE_QTTEXTTOSPEECH
#include <QTextToSpeech>
#endif

namespace relay::speech {

namespace {

// How long `spd-say -S` may take before it is killed.
constexpr int kStopDeadlineMs = 2000;

// Punctuation a block may already end in; anything else gets a full stop.
bool endsInPunctuation(const QString &text) {
    if (text.isEmpty()) return true;
    static const QString marks = QStringLiteral(".!?:;…");
    return marks.contains(text.back());
}

QString asSentence(const QString &text) {
    const QString trimmed = text.trimmed();
    return endsInPunctuation(trimmed) ? trimmed : trimmed + QLatin1Char('.');
}

// One line's worth of inline Markdown as words. Code spans are taken out first and put back
// last, so the emphasis and link rules never touch what is inside them (`my_var_name`).
QString inlineText(const QString &source) {
    QString text = source;
    QStringList spans;
    static const QRegularExpression code(QStringLiteral("(`+)(.+?)\\1"));
    for (auto match = code.match(text); match.hasMatch(); match = code.match(text, match.capturedStart())) {
        spans << match.captured(2).trimmed();
        text.replace(match.capturedStart(), match.capturedLength(),
                     QStringLiteral("\x01%1\x02").arg(spans.size() - 1));
    }
    static const QRegularExpression image(QStringLiteral("!\\[[^\\]]*\\](\\([^)]*\\)|\\[[^\\]]*\\])"));
    static const QRegularExpression link(QStringLiteral("\\[([^\\]]+)\\](\\([^)]*\\)|\\[[^\\]]*\\])"));
    static const QRegularExpression autolink(QStringLiteral("<(https?://|mailto:)[^>]+>"));
    static const QRegularExpression url(QStringLiteral("\\b(https?://|www\\.)\\S*[^\\s.,;:!?)\\]'\"]"));
    static const QRegularExpression tag(QStringLiteral("</?[A-Za-z][A-Za-z0-9-]*(\\s[^<>]*)?/?>"));
    static const QRegularExpression strong(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    static const QRegularExpression strongU(QStringLiteral("(?<!\\w)__(.+?)__(?!\\w)"));
    static const QRegularExpression strike(QStringLiteral("~~(.+?)~~"));
    static const QRegularExpression em(QStringLiteral("(?<![\\w*])\\*(?!\\s)(.+?)(?<!\\s)\\*(?![\\w*])"));
    static const QRegularExpression emU(QStringLiteral("(?<!\\w)_(?!\\s)(.+?)(?<!\\s)_(?!\\w)"));
    static const QRegularExpression escaped(QStringLiteral("\\\\([\\\\`*_{}\\[\\]()#+\\-.!|>~])"));
    static const QRegularExpression emptyParens(QStringLiteral("\\(\\s*\\)"));
    static const QRegularExpression spaceBefore(QStringLiteral("\\s+([,.;:!?)])"));
    text.replace(image, QString());
    text.replace(link, QStringLiteral("\\1"));
    text.replace(autolink, QString());
    text.replace(url, QString());
    text.replace(tag, QString());
    text.replace(strong, QStringLiteral("\\1"));
    text.replace(strongU, QStringLiteral("\\1"));
    text.replace(strike, QStringLiteral("\\1"));
    text.replace(em, QStringLiteral("\\1"));
    text.replace(emU, QStringLiteral("\\1"));
    text.replace(escaped, QStringLiteral("\\1"));
    for (int i = 0; i < spans.size(); ++i)
        text.replace(QStringLiteral("\x01%1\x02").arg(i), spans.at(i));
    text.replace(emptyParens, QString());
    text = text.simplified();
    text.replace(spaceBefore, QStringLiteral("\\1"));
    return text;
}

// Abbreviations whose full stop does not end a sentence.
bool isAbbreviation(const QString &word) {
    static const QSet<QString> words{QStringLiteral("e.g"), QStringLiteral("i.e"), QStringLiteral("mr"),
                                     QStringLiteral("mrs"), QStringLiteral("ms"), QStringLiteral("dr"),
                                     QStringLiteral("vs"), QStringLiteral("cf"), QStringLiteral("st"),
                                     QStringLiteral("jr"), QStringLiteral("sr"), QStringLiteral("prof")};
    return words.contains(word.toLower());
}

QStringList sentencesOf(const QString &line) {
    QStringList out;
    int start = 0;
    const int n = int(line.size());
    for (int i = 0; i < n; ++i) {
        const QChar c = line.at(i);
        if (c != QLatin1Char('.') && c != QLatin1Char('!') && c != QLatin1Char('?') && c != QChar(0x2026)) continue;
        int end = i + 1;
        static const QString closers = QStringLiteral(".!?)\"'”’…");
        while (end < n && closers.contains(line.at(end))) ++end;
        if (end < n && !line.at(end).isSpace()) { i = end - 1; continue; }   // "Pane.h", "3.5"
        if (c == QLatin1Char('.')) {
            int w = i;
            while (w > start && (line.at(w - 1).isLetter() || line.at(w - 1) == QLatin1Char('.'))) --w;
            if (isAbbreviation(line.mid(w, i - w))) { i = end - 1; continue; }
        }
        const QString sentence = line.mid(start, end - start).trimmed();
        if (!sentence.isEmpty()) out << sentence;
        start = end;
        i = end - 1;
    }
    const QString rest = line.mid(start).trimmed();
    if (!rest.isEmpty()) out << rest;
    return out;
}

}  // namespace

// ----- what is said -----------------------------------------------------------------------------
QString speakableText(const QString &markdown) {
    static const QRegularExpression rule(QStringLiteral("^([-*_])(\\s*\\1){2,}$"));
    static const QRegularExpression refDef(QStringLiteral("^\\[[^\\]]+\\]:\\s*\\S+"));
    static const QRegularExpression tableSep(QStringLiteral("^\\|?\\s*:?-+:?\\s*(\\|\\s*:?-+:?\\s*)*\\|?$"));
    static const QRegularExpression heading(QStringLiteral("^#{1,6}\\s+(.*?)(\\s+#+)?$"));
    static const QRegularExpression quote(QStringLiteral("^(>\\s?)+"));
    static const QRegularExpression setext(QStringLiteral("^=+$"));
    static const QRegularExpression item(QStringLiteral("^([-*+]|\\d{1,9}[.)])\\s+(\\[[ xX]\\]\\s+)?(.*)$"));

    QStringList blocks, paragraph;
    bool paragraphIsItem = false;
    auto flush = [&] {
        if (paragraph.isEmpty()) return;
        const QString text = inlineText(paragraph.join(QLatin1Char(' ')));
        if (!text.isEmpty()) blocks << (paragraphIsItem ? asSentence(text) : text);
        paragraph.clear();
        paragraphIsItem = false;
    };

    bool inFence = false, saidCode = false;
    QString fence;
    const QStringList lines = QString(markdown).replace(QStringLiteral("\r\n"), QStringLiteral("\n")).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString line = raw.trimmed();
        if (inFence) {
            if (line.startsWith(fence) && line.mid(fence.size()).trimmed().remove(fence.at(0)).isEmpty()) inFence = false;
            continue;
        }
        if (line.startsWith(QStringLiteral("```")) || line.startsWith(QStringLiteral("~~~"))) {
            flush();
            int length = 0;
            while (length < line.size() && line.at(length) == line.at(0)) ++length;
            fence = line.left(length);
            inFence = true;
            if (!saidCode) { blocks << QStringLiteral("Code block omitted."); saidCode = true; }
            continue;
        }
        line.remove(quote);
        line = line.trimmed();
        if (setext.match(line).hasMatch() && !paragraph.isEmpty()) {   // "Title\n=====" is a heading
            paragraphIsItem = true;
            flush();
            continue;
        }
        if (line.isEmpty() || rule.match(line).hasMatch() || refDef.match(line).hasMatch()) { flush(); continue; }
        if (line.contains(QLatin1Char('|')) && line.contains(QLatin1Char('-')) && tableSep.match(line).hasMatch()) {
            flush();
            continue;
        }
        if (line.startsWith(QLatin1Char('|'))) {
            flush();
            QString row = line.mid(1);
            if (row.endsWith(QLatin1Char('|'))) row.chop(1);
            QStringList cells;
            for (const QString &cell : row.split(QLatin1Char('|'))) {
                const QString text = inlineText(cell);
                if (!text.isEmpty()) cells << text;
            }
            if (!cells.isEmpty()) blocks << asSentence(cells.join(QStringLiteral(", ")));
            continue;
        }
        if (const auto match = heading.match(line); match.hasMatch()) {
            flush();
            const QString text = inlineText(match.captured(1));
            if (!text.isEmpty()) blocks << asSentence(text);
            continue;
        }
        if (const auto match = item.match(line); match.hasMatch()) {
            flush();
            paragraph << match.captured(3);
            paragraphIsItem = true;
            continue;
        }
        paragraph << line;
    }
    flush();
    return blocks.join(QLatin1Char('\n'));
}

QStringList chunks(const QString &speakable, int maxChars) {
    maxChars = qMax(20, maxChars);
    QStringList out;
    for (const QString &line : speakable.split(QLatin1Char('\n'))) {
        for (QString sentence : sentencesOf(line.trimmed())) {
            while (sentence.size() > maxChars) {
                int cut = int(sentence.lastIndexOf(QLatin1Char(' '), maxChars));
                if (cut <= 0) cut = maxChars;
                out << sentence.left(cut).trimmed();
                sentence = sentence.mid(cut).trimmed();
            }
            if (!sentence.isEmpty()) out << sentence;
        }
    }
    return out;
}

// ----- command-line speakers --------------------------------------------------------------------
Os currentOs() {
#if defined(Q_OS_WIN)
    return Os::Windows;
#elif defined(Q_OS_MACOS)
    return Os::Mac;
#else
    return Os::Linux;
#endif
}

QStringList cliTools(Os os) {
    switch (os) {
    case Os::Windows: return {QStringLiteral("powershell")};
    case Os::Mac: return {QStringLiteral("say"), QStringLiteral("espeak-ng"), QStringLiteral("espeak")};
    case Os::Linux: break;
    }
    return {QStringLiteral("spd-say"), QStringLiteral("espeak-ng"), QStringLiteral("espeak")};
}

QString chooseTool(const std::function<bool(const QString &)> &available, Os os) {
    if (!available) return {};
    for (const QString &tool : cliTools(os))
        if (available(tool)) return tool;
    return {};
}

bool toolOnPath(const QString &tool) {
    return !QStandardPaths::findExecutable(tool).isEmpty();
}

Invocation speakInvocation(const QString &tool, const QString &text) {
    // spd-say takes the text as one argument after "--", so a sentence starting with "-" is not an
    // option; -w waits until it has been spoken, which is what lets the next piece follow it.
    if (tool == QStringLiteral("spd-say"))
        return {{QStringLiteral("-w"), QStringLiteral("-N"), QStringLiteral("relay"), QStringLiteral("--"), text}, {}};
    if (tool == QStringLiteral("espeak-ng") || tool == QStringLiteral("espeak"))
        return {{QStringLiteral("--stdin")}, text.toUtf8()};
    if (tool == QStringLiteral("say"))
        return {{QStringLiteral("-f"), QStringLiteral("-")}, text.toUtf8()};
    if (tool == QStringLiteral("powershell")) {
        // A fixed script; the text arrives on stdin, never inside the command line.
        const QString script = QStringLiteral(
            "[Console]::InputEncoding = [Text.Encoding]::UTF8; "
            "Add-Type -AssemblyName System.Speech; "
            "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
            "$s.SetOutputToDefaultAudioDevice(); "
            "$s.Speak([Console]::In.ReadToEnd())");
        return {{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), script},
                text.toUtf8()};
    }
    return {};
}

QStringList stopArguments(const QString &tool) {
    if (tool == QStringLiteral("spd-say")) return {QStringLiteral("-S")};
    return {};
}

QString missingToolsMessage(Os os) {
    switch (os) {
    case Os::Windows:
        return QStringLiteral("Nothing can read aloud here: Windows PowerShell (System.Speech) was not found.");
    case Os::Mac:
        return QStringLiteral("Nothing can read aloud here: the macOS say command was not found.");
    case Os::Linux: break;
    }
    return QStringLiteral("Nothing can read aloud here. Install speech-dispatcher (spd-say) or espeak-ng.");
}

// ----- speaking ---------------------------------------------------------------------------------
Speaker &Speaker::instance() {
    static QPointer<Speaker> speaker;
    if (!speaker) speaker = new Speaker(QCoreApplication::instance());
    return *speaker;
}

Speaker::Speaker(QObject *parent) : QObject(parent) {}

Speaker::~Speaker() {
    if (m_process) m_process->kill();
}

QString Speaker::tool() const {
    return m_toolOverride.isEmpty() ? chooseTool(toolOnPath) : m_toolOverride;
}

bool Speaker::useQt() {
#ifdef RELAY_HAVE_QTTEXTTOSPEECH
    if (!m_toolOverride.isEmpty()) return false;
    if (!m_qtChecked) {
        m_qtChecked = true;
        // No plugin (a Linux without the speech-dispatcher plugin) means no Qt engine: the
        // command-line speakers are the fallback, exactly as if Qt had not been built with it.
        if (!QTextToSpeech::availableEngines().isEmpty()) {
            m_tts = new QTextToSpeech(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const auto broken = QTextToSpeech::Error;
#else
            const auto broken = QTextToSpeech::BackendError;
#endif
            m_qtUsable = m_tts->state() != broken;
            if (m_qtUsable) {
                connect(m_tts, &QTextToSpeech::stateChanged, this, [this, broken](QTextToSpeech::State state) {
                    if (!m_speaking) return;
                    if (state == QTextToSpeech::Speaking) { m_qtStarted = true; return; }
                    if (state == broken) { fail(QStringLiteral("The system voice stopped with an error.")); return; }
                    // Ready after Speaking is this piece done. A Ready that arrives before this
                    // piece started speaking is the stop of the utterance it replaced.
                    if (state == QTextToSpeech::Ready && m_qtStarted) next();
                });
            } else {
                delete m_tts;
                m_tts = nullptr;
            }
        }
    }
    return m_qtUsable;
#else
    return false;
#endif
}

QString Speaker::engine() const {
    auto *self = const_cast<Speaker *>(this);
    if (self->useQt()) return QStringLiteral("Qt TextToSpeech");
    return tool();
}

bool Speaker::speak(const QString &markdown, QObject *owner, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    stop();
    const QStringList pieces = chunks(speakableText(markdown));
    if (pieces.isEmpty()) return fail(QStringLiteral("Nothing in that reply to read aloud."));
    if (!useQt()) {
        m_tool = tool();
        if (m_tool.isEmpty()) return fail(missingToolsMessage());
    }
    ++m_generation;
    m_chunks = pieces;
    m_index = 0;
    m_owner = owner;
    m_speaking = true;
    Q_EMIT stateChanged();
    next();
    return true;
}

void Speaker::next() {
    if (!m_speaking) return;
    if (m_index >= m_chunks.size()) { finish(); return; }
    const QString text = m_chunks.at(m_index++);
#ifdef RELAY_HAVE_QTTEXTTOSPEECH
    if (m_qtUsable && m_tts) {
        m_qtStarted = false;
        m_tts->say(text);
        return;
    }
#endif
    const Invocation invocation = speakInvocation(m_tool, text);
    auto *process = new QProcess(this);
    m_process = process;
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    const quint64 generation = m_generation;
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, generation](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (generation != m_generation || m_process != process) return;   // stopped or replaced
        m_process = nullptr;
        if (status != QProcess::NormalExit || code != 0) {
            fail(QStringLiteral("%1 stopped with an error (exit %2).").arg(m_tool).arg(code));
            return;
        }
        next();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError code) {
        if (code != QProcess::FailedToStart) return;   // a crash also finishes, and is handled there
        process->deleteLater();
        if (generation != m_generation || m_process != process) return;
        m_process = nullptr;
        fail(QStringLiteral("%1 could not be started.").arg(m_tool));
    });
    // Written once it has started: a write before that is dropped, and the tool would read EOF.
    const QByteArray input = invocation.input;
    connect(process, &QProcess::started, process, [process, input] {
        if (!input.isEmpty()) process->write(input);
        process->closeWriteChannel();
    });
    process->start(m_tool, invocation.arguments);
}

void Speaker::stop() {
    if (!m_speaking) return;
    ++m_generation;          // the running piece's finished signal no longer starts the next one
    m_speaking = false;
    m_chunks.clear();
    if (m_process) {
        // Its finished handler deletes it; the generation keeps that handler from going on.
        m_process->kill();
        m_process = nullptr;
        const QStringList stopping = stopArguments(m_tool);
        if (!stopping.isEmpty()) {
            // A child with a deadline, not startDetached(): `spd-say -S` waits on the daemon, and
            // one that hangs (seen with a daemon left behind by a removed runtime directory) would
            // otherwise outlive every utterance.
            auto *stopper = new QProcess(this);
            stopper->setStandardOutputFile(QProcess::nullDevice());
            stopper->setStandardErrorFile(QProcess::nullDevice());
            connect(stopper, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), stopper, &QObject::deleteLater);
            connect(stopper, &QProcess::errorOccurred, stopper, [stopper](QProcess::ProcessError code) {
                if (code == QProcess::FailedToStart) stopper->deleteLater();
            });
            QTimer::singleShot(kStopDeadlineMs, stopper, [stopper] { stopper->kill(); });
            stopper->start(m_tool, stopping);
        }
    }
#ifdef RELAY_HAVE_QTTEXTTOSPEECH
    if (m_tts) m_tts->stop();
#endif
    m_owner.clear();
    Q_EMIT stateChanged();
}

// Said before finish() so that a listener can still tell whose utterance failed.
void Speaker::fail(const QString &message) {
    Q_EMIT failed(message);
    finish();
}

void Speaker::finish() {
    if (!m_speaking) return;
    ++m_generation;
    m_speaking = false;
    m_chunks.clear();
    m_owner.clear();
    Q_EMIT stateChanged();
}

}  // namespace relay::speech
