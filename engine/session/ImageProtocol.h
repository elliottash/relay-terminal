// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Kitty graphics, iTerm2 inline images and sixel, read out of the terminal byte stream (card
// #1MGS). See core/InlineImage.h for the contract.
//
// relay::ImageProtocol sits in front of the core in TerminalSession: every byte of program output
// (and of writeToDisplay) goes through feed(). Bytes that are not an image escape come out of
// `output` untouched and in order; an image escape is swallowed whole and, if it displays a
// picture, replaced in the same place by inlineimage::placementBytes(). Sequences may be split
// across feed() calls at any byte, a lone ESC at the end of a chunk included.
//
//   kitty    APC  ESC _ G <keys> ; <base64> ESC \          a=t/T/p/d/q, t=d/f/t (s refused),
//                                                          f=24/32/100, o=z, m=1 chunks, replies
//   iTerm2   OSC  ESC ] 1337 ; File=<args>:<base64> BEL|ST  inline=1 shown, inline=0 swallowed;
//                 MultipartFile= / FilePart= / FileEnd
//   sixel    DCS  ESC P <params> q <data> ESC \             decoded to a PNG here
//
// Every other APC, OSC and DCS passes through unchanged. A sequence body is bounded
// (kMaxSequenceBytes); past that it is dropped, and an ESC that does not start ST (or a CAN/SUB)
// ends it the way it ends any string in a VT parser, so a never-terminated escape cannot eat the
// rest of the output.
//
// Not thread-safe; TerminalSession calls it with its mutex held. `screen` is asked for the cursor
// only after every byte before the image has gone to `output`, so it reports the right column.
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

#include <cstddef>
#include <functional>

namespace relay {

class ImageProtocol {
public:
    // What the interceptor needs to know about the screen when it places an image.
    struct Screen {
        int cursorRow = 0;
        int cursorCol = 0;
        int rows = 24;
        int columns = 80;
        QSize cellPixels{8, 16};
        bool altScreen = false;
    };

    // Encoded image bytes (PNG/JPEG/... as sent, or the file read for kitty t=f) above this are
    // refused. A picture wider or taller than kMaxPixelSide, or a raw/sixel picture with more than
    // kMaxPixelArea pixels, is refused too.
    static constexpr qint64 kMaxEncodedBytes = qint64(50) << 20;
    static constexpr int kMaxPixelSide = 10000;
    static constexpr qint64 kMaxPixelArea = qint64(25) * 1000 * 1000; // 100 MB of RGBA
    // One escape sequence's body, and a chunked kitty transfer's accumulated base64: 50 MB of
    // data is 4/3 of that in base64, plus the keys.
    static constexpr qint64 kMaxSequenceBytes = kMaxEncodedBytes / 3 * 4 + 4096;
    // Kitty images kept for a=p, oldest dropped first.
    static constexpr int kMaxStoredImages = 256;

    std::function<void(const char *, size_t)> output;  // bytes for the core
    std::function<Screen()> screen;                    // asked just before a placement
    std::function<void(const char *, size_t)> reply;   // kitty replies to the program; may be empty

    void feed(const char *data, size_t len);
    void feed(const QByteArray &bytes) { feed(bytes.constData(), size_t(bytes.size())); }
    // False while a possible image escape is held back (a lone ESC, a prefix, a body).
    bool atGround() const { return m_state == State::Ground; }

    // A sixel body (the bytes after 'q', before ST) as an image; `params` are the DCS parameters
    // (P1;P2;P3). Null when it draws nothing or is over the caps.
    static QImage decodeSixel(const QByteArray &params, const QByteArray &data);

private:
    enum class State { Ground, Esc, ApcIntro, OscIntro, DcsIntro, Body, BodyEsc };
    enum class Kind { None, Kitty, ITermFile, ITermMultipart, ITermPart, ITermEnd, Sixel };

    struct Kitty {
        char a = 't', t = 'd', o = 0, d = 'a';
        int f = 32, s = 0, v = 0, S = 0, O = 0, i = 0, I = 0, p = 0, c = 0, r = 0, C = 0, q = 0, x = 0, y = 0,
            w = 0, h = 0, m = 0, U = 0;
    };
    struct Stored {
        QString path;
        QSize pixels;
    };
    struct Loaded {
        QString path;        // a file to show as it is (kitty t=f PNG), or empty
        QByteArray encoded;  // else the encoded bytes to store
        QString suffix;      // their format, for storeImageBytes
        QImage decoded;      // raw formats and sixel: the picture itself
        QSize pixels;
        QByteArray error;    // kitty error reply, e.g. "EINVAL:bad base64"
    };

    void emitBytes(const char *data, size_t len);
    void emitBytes(const QByteArray &bytes) { emitBytes(bytes.constData(), size_t(bytes.size())); }
    void bodyByte(const char *data, size_t len);
    void finishBody();
    void abortBody();

    static Kitty parseKittyKeys(const QByteArray &control);
    void handleKitty(const QByteArray &body, bool overflow);
    void processKitty(Kitty k, const QByteArray &payload);
    void kittyReply(const Kitty &k, const QByteArray &message);
    Loaded loadKitty(const Kitty &k, const QByteArray &payload) const;
    void storeKitty(int id, const Stored &image);
    void forgetKitty(int id);
    void handleITerm(Kind kind, const QByteArray &body, bool overflow);
    void showITerm(const QByteArray &args, const QByteArray &base64);
    void handleSixel(const QByteArray &body);

    // Stores or keeps the picture as a file and returns it with its pixel size; empty on failure.
    // A non-null `crop` (in pixels) cuts that rectangle out first.
    Stored materialise(Loaded image, const QRect &crop = {}) const;
    Screen currentScreen() const;
    void place(const Stored &image, QSize requestedCells, bool moveCursor);

    State m_state = State::Ground;
    Kind m_kind = Kind::None;
    QByteArray m_prefix;      // OSC / DCS introducer bytes seen so far (without ESC ] / ESC P)
    QByteArray m_body;        // the current sequence body
    bool m_overflow = false;  // m_body passed kMaxSequenceBytes: drop it at its end

    // A chunked kitty transfer (m=1) in progress.
    bool m_chunking = false;
    bool m_chunkOverflow = false;
    Kitty m_chunkKeys;
    QByteArray m_chunkData;

    // iTerm2 MultipartFile in progress.
    bool m_multipart = false;
    bool m_multipartOverflow = false;
    QByteArray m_multipartArgs;
    QByteArray m_multipartData;

    // Kitty images transmitted with an id or number, for a=p. a=d forgets them here; rows already
    // on screen keep showing their file (the core holds them as text, see core/InlineImage.h).
    QHash<int, Stored> m_images;
    QList<int> m_imageOrder;      // oldest first
    QHash<int, int> m_numbers;    // I= image number -> the id it was given
    int m_nextId = 1 << 24;       // ids the terminal assigns for I= transmissions
};

} // namespace relay
