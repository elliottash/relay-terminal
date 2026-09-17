// SPDX-License-Identifier: GPL-3.0-or-later
// The rules behind voice transcription (issue NY7Z): which capture tool runs and how, which key
// event is the hold key, where a transcript lands in the composer's text, and the WAV repair that
// makes a clip from an interrupted recorder readable. No microphone and no provider are involved.
#include "Voice.h"

#include <QTest>

using namespace relay::voice;

namespace {

// A minimal 16 kHz mono 16-bit WAV with `samples` frames of silence. `riff` and `data` override the
// two size fields, the way a recorder killed mid-write leaves them.
QByteArray wav(int samples, int riffSize = -1, int dataSize = -1) {
    const int bytes = samples * 2;
    QByteArray out;
    auto le32 = [&out](quint32 value) {
        for (int i = 0; i < 4; ++i) out.append(char((value >> (8 * i)) & 0xff));
    };
    auto le16 = [&out](quint16 value) {
        out.append(char(value & 0xff));
        out.append(char((value >> 8) & 0xff));
    };
    out.append("RIFF");
    le32(riffSize < 0 ? quint32(36 + bytes) : quint32(riffSize));
    out.append("WAVE");
    out.append("fmt ");
    le32(16);
    le16(1);        // PCM
    le16(1);        // mono
    le32(16000);    // sample rate
    le32(32000);    // byte rate
    le16(2);        // block align
    le16(16);       // bits
    out.append("data");
    le32(dataSize < 0 ? quint32(bytes) : quint32(dataSize));
    out.append(QByteArray(bytes, '\0'));
    return out;
}

auto only(const QString &tool) {
    return [tool](const QString &candidate) { return candidate == tool; };
}

}  // namespace

class VoiceTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ----- capture tools ----------------------------------------------------------------------
    void everyToolAsksFor16kMonoWav() {
        for (const QString &tool : captureTools()) {
            const QStringList args = captureArguments(tool, QStringLiteral("/tmp/clip.wav"));
            QVERIFY2(!args.isEmpty(), qPrintable(tool));
            QVERIFY2(args.contains(QStringLiteral("/tmp/clip.wav")), qPrintable(tool));
            QVERIFY2(args.join(QLatin1Char(' ')).contains(QStringLiteral("16000")), qPrintable(tool));
        }
        // The path is the last argument for the tools that take it positionally.
        QCOMPARE(captureArguments(QStringLiteral("pw-record"), QStringLiteral("/tmp/c.wav")).last(), QStringLiteral("/tmp/c.wav"));
        QCOMPARE(captureArguments(QStringLiteral("arecord"), QStringLiteral("/tmp/c.wav")).last(), QStringLiteral("/tmp/c.wav"));
        QVERIFY(captureArguments(QStringLiteral("sox"), QStringLiteral("/tmp/c.wav")).isEmpty());
    }

    void aDeviceIsPassedInEachToolsOwnSpelling() {
        QVERIFY(captureArguments(QStringLiteral("pw-record"), QStringLiteral("/tmp/c.wav"), QStringLiteral("mic"))
                    .contains(QStringLiteral("--target=mic")));
        QVERIFY(captureArguments(QStringLiteral("parecord"), QStringLiteral("/tmp/c.wav"), QStringLiteral("mic"))
                    .contains(QStringLiteral("--device=mic")));
        QVERIFY(captureArguments(QStringLiteral("arecord"), QStringLiteral("/tmp/c.wav"), QStringLiteral("hw:1"))
                    .contains(QStringLiteral("hw:1")));
        // No device: nothing is passed, so the tool uses the desktop's default source.
        QVERIFY(!captureArguments(QStringLiteral("parecord"), QStringLiteral("/tmp/c.wav")).join(QLatin1Char(' '))
                     .contains(QStringLiteral("--device")));
    }

    void theFirstAvailableToolWinsUnlessOneIsChosen() {
        auto all = [](const QString &) { return true; };
        QCOMPARE(chooseTool(QString(), all), QStringLiteral("pw-record"));          // preference order
        QCOMPARE(chooseTool(QStringLiteral("arecord"), all), QStringLiteral("arecord"));
        // A chosen tool that is not installed falls back to the order rather than failing.
        QCOMPARE(chooseTool(QStringLiteral("arecord"), only(QStringLiteral("parecord"))), QStringLiteral("parecord"));
        // An unknown name is not run, whatever `available` says.
        QCOMPARE(chooseTool(QStringLiteral("rm"), only(QStringLiteral("rm"))), QString());
        QCOMPARE(chooseTool(QString(), [](const QString &) { return false; }), QString());
    }

    // ----- the hold key -----------------------------------------------------------------------
    void rightAltIsTheHoldKeyAndLeftAltIsNot() {
        const QString setting = QStringLiteral("right-alt");
        QVERIFY(isHoldKey(setting, Qt::Key_Alt, 0xffea, 108));          // Alt_R
        QVERIFY(isHoldKey(setting, Qt::Key_AltGr, 0xfe03, 108));        // ISO_Level3_Shift
        QVERIFY(!isHoldKey(setting, Qt::Key_Alt, 0xffe9, 64));          // Alt_L
        // No keysym (a platform that reports none): the scan code decides.
        QVERIFY(isHoldKey(setting, Qt::Key_Alt, 0, 108));
        QVERIFY(!isHoldKey(setting, Qt::Key_Alt, 0, 64));
        // Ordinary typing is never the hold key.
        QVERIFY(!isHoldKey(setting, Qt::Key_A, 0x0061, 38));
    }

    void theHoldKeyIsConfigurableAndCanBeOff() {
        QVERIFY(isHoldKey(QStringLiteral("right-ctrl"), Qt::Key_Control, 0xffe4, 105));
        QVERIFY(!isHoldKey(QStringLiteral("right-ctrl"), Qt::Key_Control, 0xffe3, 37));   // Control_L
        QVERIFY(!isHoldKey(QStringLiteral("right-ctrl"), Qt::Key_Alt, 0xffea, 108));
        QVERIFY(isHoldKey(QStringLiteral("f9"), Qt::Key_F9, 0xffc6, 75));
        for (const QString &key : holdKeys())
            QVERIFY(!holdKeyLabel(key).isEmpty());
        // "off" answers no to every key, including the ones the other settings accept.
        QVERIFY(!isHoldKey(QStringLiteral("off"), Qt::Key_Alt, 0xffea, 108));
        QVERIFY(!isHoldKey(QStringLiteral("off"), Qt::Key_F9, 0xffc6, 75));
    }

    void layoutsThatTypeWithAltGrDoNotGetRightAltByDefault() {
        const QString us = QStringLiteral("XKBLAYOUT=\"us\"\nXKBVARIANT=\"\"\nBACKSPACE=\"guess\"\n");
        QCOMPARE(layoutsFromKeyboardConfig(us), QStringList{QStringLiteral("us")});
        QCOMPARE(defaultHoldKey(layoutsFromKeyboardConfig(us)), QStringLiteral("right-alt"));
        // A second layout counts even when the first is safe: the user switches between them.
        const QString both = QStringLiteral("XKBLAYOUT=\"us,de\"\nXKBVARIANT=\",nodeadkeys\"\n");
        QCOMPARE(layoutsFromKeyboardConfig(both), (QStringList{QStringLiteral("us"), QStringLiteral("de(nodeadkeys)")}));
        QCOMPARE(defaultHoldKey(layoutsFromKeyboardConfig(both)), QStringLiteral("off"));
        // US International types é with the right Alt, so it is not safe either.
        QVERIFY(layoutTypesWithAltGr({QStringLiteral("us(intl)")}));
        QVERIFY(!layoutTypesWithAltGr({QStringLiteral("us(dvorak)")}));
        // An unreadable or missing file leaves no layouts, and the Warp default applies.
        QCOMPARE(defaultHoldKey(layoutsFromKeyboardConfig(QString())), QStringLiteral("right-alt"));
        QVERIFY(layoutsFromKeyboardConfig(QStringLiteral("# XKBLAYOUT=\"de\"\n")).isEmpty());
    }

    // ----- the transcript in the composer ------------------------------------------------------
    void aTranscriptIsSpacedIntoWhateverIsAlreadyTyped() {
        // Empty composer: no leading space, cursor after the text.
        Insertion fresh = insertTranscript(QString(), 0, QStringLiteral("list the files"));
        QCOMPARE(fresh.text, QStringLiteral("list the files"));
        QCOMPARE(fresh.cursor, 14);
        // After a word: one space, never two.
        QCOMPARE(insertTranscript(QStringLiteral("git"), 3, QStringLiteral("status")).text, QStringLiteral("git status"));
        QCOMPARE(insertTranscript(QStringLiteral("git "), 4, QStringLiteral("status")).text, QStringLiteral("git status"));
        // Mid-text: a space on both sides, with the cursor left before the trailing one.
        Insertion middle = insertTranscript(QStringLiteral("git log"), 4, QStringLiteral("--oneline"));
        QCOMPARE(middle.text, QStringLiteral("git --oneline log"));
        QCOMPARE(middle.cursor, 13);
        // @ and ( are openers the speech follows directly.
        QCOMPARE(insertTranscript(QStringLiteral("@"), 1, QStringLiteral("README")).text, QStringLiteral("@README"));
        // A transcript of nothing leaves the draft exactly as it was.
        QCOMPARE(insertTranscript(QStringLiteral("git"), 3, QStringLiteral("  ")).text, QStringLiteral("git"));
        // A cursor outside the text cannot corrupt the draft.
        QCOMPARE(insertTranscript(QStringLiteral("git"), 99, QStringLiteral("status")).text, QStringLiteral("git status"));
    }

    // ----- clips --------------------------------------------------------------------------------
    void aClipsLengthIsReadFromItsHeader() {
        QCOMPARE(wavDurationMs(wav(16000)), 1000);          // one second at 16 kHz mono
        QCOMPARE(wavDurationMs(wav(1600)), 100);
        QCOMPARE(wavDurationMs(QByteArray("not a wav at all")), -1);
    }

    void aKilledRecordersSizesAreRepairedFromTheRealLength() {
        // arecord streaming to a pipe writes 0xffffffff, ffmpeg killed mid-write writes 0.
        QByteArray broken = wav(16000, 0xffffffff, 0xffffffff);
        QVERIFY(repairWav(broken));
        QCOMPARE(wavDurationMs(broken), 1000);
        QByteArray zeroed = wav(16000, 0, 0);
        QVERIFY(repairWav(zeroed));
        QCOMPARE(wavDurationMs(zeroed), 1000);
        // A clip whose sizes are already right is left byte for byte as it is.
        QByteArray good = wav(16000), copy = good;
        repairWav(good);
        QCOMPARE(good, copy);
        // Not a WAV: nothing is rewritten.
        QByteArray other("ID3\x04junk");
        QVERIFY(!repairWav(other));
        QCOMPARE(other, QByteArray("ID3\x04junk"));
    }

    void theRecordingCapIsBounded() {
        QCOMPARE(clampSeconds(0), kDefaultSeconds);      // unset in QSettings
        QCOMPARE(clampSeconds(-5), kDefaultSeconds);
        QCOMPARE(clampSeconds(30), 30);
        QCOMPARE(clampSeconds(99999), 600);
        QCOMPARE(clampSeconds(1), 5);
    }
};

QTEST_MAIN(VoiceTests)
#include "voice_test.moc"
