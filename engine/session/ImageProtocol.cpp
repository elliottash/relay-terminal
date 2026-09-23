// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ImageProtocol.h"

#include "core/InlineImage.h"

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRect>

#include <algorithm>
#include <cstring>
#include <vector>

// o=z payloads are inflated with a hard cap on the output. zlib is not on the engine's link line,
// but it is loaded in-process on Unix (Qt and libpng use it), so its three entry points are
// looked up at runtime. Without it, qUncompress is the fallback; that one grows its buffer
// without a cap, which is why it is not the first choice.
#if defined(Q_OS_UNIX) && __has_include(<zlib.h>) && __has_include(<dlfcn.h>)
#define RELAY_RUNTIME_ZLIB 1
#include <dlfcn.h>
#include <zlib.h>
#endif

namespace relay {

namespace {

#ifdef RELAY_RUNTIME_ZLIB
struct Zlib {
    decltype(&::inflateInit_) init = nullptr;
    decltype(&::inflate) inflate = nullptr;
    decltype(&::inflateEnd) end = nullptr;
    bool ok() const { return init && inflate && end; }
};

const Zlib &zlib()
{
    static const Zlib z = [] {
        Zlib found;
        auto load = [&found](void *handle) {
            found.init = reinterpret_cast<decltype(found.init)>(dlsym(handle, "inflateInit_"));
            found.inflate = reinterpret_cast<decltype(found.inflate)>(dlsym(handle, "inflate"));
            found.end = reinterpret_cast<decltype(found.end)>(dlsym(handle, "inflateEnd"));
        };
        load(RTLD_DEFAULT);
        for (const char *name : {"libz.so.1", "libz.1.dylib", "libz.dylib", "libz.so"}) {
            if (found.ok())
                break;
            if (void *handle = dlopen(name, RTLD_LAZY | RTLD_LOCAL))
                load(handle);
        }
        return found;
    }();
    return z;
}
#endif

// Inflates a zlib stream into at most `limit` bytes; false for bad data or more output than that.
// `hint` is the expected size when known (raw pixels), else 0.
bool inflateBounded(const QByteArray &in, qint64 hint, qint64 limit, QByteArray *out)
{
#ifdef RELAY_RUNTIME_ZLIB
    const Zlib &z = zlib();
    if (z.ok()) {
        z_stream s;
        std::memset(&s, 0, sizeof s);
        if (z.init(&s, ZLIB_VERSION, int(sizeof(z_stream))) != Z_OK)
            return false;
        s.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
        s.avail_in = uInt(in.size());
        QByteArray result;
        result.resize(int(std::clamp<qint64>(hint > 0 ? hint : qint64(in.size()) * 4, 4096, limit)));
        bool done = false, failed = false;
        while (!done && !failed) {
            s.next_out = reinterpret_cast<Bytef *>(result.data()) + s.total_out;
            s.avail_out = uInt(qint64(result.size()) - qint64(s.total_out));
            const int rc = z.inflate(&s, Z_NO_FLUSH);
            if (rc == Z_STREAM_END) {
                done = true;
            } else if (rc != Z_OK && rc != Z_BUF_ERROR) {
                failed = true;
            } else if (s.avail_out == 0) {
                if (result.size() >= limit)
                    failed = true; // more output than the cap: a bomb or an oversize image
                else
                    result.resize(int(std::min<qint64>(limit, qint64(result.size()) * 2)));
            } else if (s.avail_in == 0 || rc == Z_BUF_ERROR) {
                failed = true; // truncated stream
            }
        }
        const qint64 total = qint64(s.total_out);
        z.end(&s);
        if (failed)
            return false;
        result.resize(int(total));
        *out = result;
        return true;
    }
#endif
    const quint32 expected = quint32(std::clamp<qint64>(hint > 0 ? hint : qint64(in.size()) * 4, 1, limit));
    QByteArray framed;
    framed.reserve(in.size() + 4);
    framed.append(char(expected >> 24)).append(char(expected >> 16)).append(char(expected >> 8)).append(char(expected));
    framed.append(in);
    const QByteArray result = qUncompress(framed);
    if (result.isEmpty() || result.size() > limit)
        return false;
    *out = result;
    return true;
}

bool decodeBase64(QByteArray data, QByteArray *out, bool stripWhitespace = false)
{
    if (stripWhitespace) {
        data.replace('\n', QByteArray()).replace('\r', QByteArray()).replace(' ', QByteArray());
    }
    const QByteArray::FromBase64Result result =
        QByteArray::fromBase64Encoding(data, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!result)
        return false;
    *out = *result;
    return true;
}

// The formats inlineimage::storeImageBytes keeps as they are; anything else is re-encoded as PNG.
QString storableSuffix(const QByteArray &format)
{
    if (format == "png" || format == "gif" || format == "webp")
        return QString::fromLatin1(format);
    if (format == "jpeg" || format == "jpg")
        return QStringLiteral("jpg");
    return {};
}

bool withinCaps(QSize pixels)
{
    return pixels.width() > 0 && pixels.height() > 0 && pixels.width() <= ImageProtocol::kMaxPixelSide
           && pixels.height() <= ImageProtocol::kMaxPixelSide;
}

// Paths a program may name with kitty t=f / t=t: absolute, an existing regular readable file, not
// under /proc, /sys or /dev (other than /dev/shm, where some clients put their temp files).
QByteArray refuseFile(const QString &path)
{
    if (path.isEmpty() || !QDir::isAbsolutePath(path))
        return "EINVAL:the file path must be absolute";
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty())
        return "ENOENT:no such file";
    for (const QString &p : {QDir::cleanPath(path), canonical}) {
        for (const char *system : {"/proc/", "/sys/", "/dev/"}) {
            if (p.startsWith(QLatin1String(system)) && !p.startsWith(QLatin1String("/dev/shm/")))
                return "EPERM:files under /proc, /sys and /dev are not read";
        }
    }
    const QFileInfo info(canonical);
    if (!info.isFile() || !info.isReadable())
        return "EBADF:not a regular readable file";
    return {};
}

// kitty deletes a t=t file after reading it, but only one that is plainly a temp file made for
// this: in a temp directory and named with tty-graphics-protocol.
bool deletableTempFile(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty() || !canonical.contains(QLatin1String("tty-graphics-protocol")))
        return false;
    for (const QString &dir : {QFileInfo(QDir::tempPath()).canonicalFilePath(), QStringLiteral("/tmp"), QStringLiteral("/dev/shm")}) {
        if (!dir.isEmpty() && canonical.startsWith(dir + QLatin1Char('/')))
            return true;
    }
    return false;
}

QRect cropFor(int x, int y, int w, int h)
{
    if (!x && !y && !w && !h)
        return {};
    return QRect(std::max(0, x), std::max(0, y), w > 0 ? w : ImageProtocol::kMaxPixelSide,
                 h > 0 ? h : ImageProtocol::kMaxPixelSide);
}

} // namespace

// ---- the byte stream ---------------------------------------------------------------------------

void ImageProtocol::emitBytes(const char *data, size_t len)
{
    if (len && output)
        output(data, len);
}

void ImageProtocol::feed(const char *data, size_t len)
{
    size_t i = 0;
    while (i < len) {
        const char c = data[i];
        switch (m_state) {
        case State::Ground: {
            const void *esc = std::memchr(data + i, 0x1b, len - i);
            const size_t end = esc ? size_t(static_cast<const char *>(esc) - data) : len;
            emitBytes(data + i, end - i);
            if (!esc)
                return;
            i = end + 1;
            m_state = State::Esc;
            break;
        }
        case State::Esc:
            // Anything but an image introducer: hand the ESC on and look at this byte again.
            if (c == '_') {
                m_state = State::ApcIntro;
                ++i;
            } else if (c == ']' || c == 'P') {
                m_prefix.clear();
                m_state = c == ']' ? State::OscIntro : State::DcsIntro;
                ++i;
            } else {
                emitBytes("\x1b", 1);
                m_state = State::Ground;
            }
            break;
        case State::ApcIntro:
            if (c == 'G') {
                m_kind = Kind::Kitty;
                m_body.clear();
                m_overflow = false;
                m_state = State::Body;
                ++i;
            } else {
                emitBytes("\x1b_", 2);
                m_state = State::Ground;
            }
            break;
        case State::OscIntro: {
            static const struct {
                const char *prefix;
                Kind kind;
            } kITerm[] = {{"1337;File=", Kind::ITermFile},
                          {"1337;MultipartFile=", Kind::ITermMultipart},
                          {"1337;FilePart=", Kind::ITermPart},
                          {"1337;FileEnd", Kind::ITermEnd}};
            const QByteArray next = m_prefix + c;
            bool possible = false;
            for (const auto &candidate : kITerm) {
                if (next == candidate.prefix) {
                    m_kind = candidate.kind;
                    m_body.clear();
                    m_overflow = false;
                    m_state = State::Body;
                    possible = true;
                    break;
                }
                possible |= QByteArray(candidate.prefix).startsWith(next);
            }
            if (m_state == State::Body) {
                ++i;
            } else if (possible) {
                m_prefix = next;
                ++i;
            } else {
                emitBytes("\x1b]", 2);
                emitBytes(m_prefix);
                m_state = State::Ground;
            }
            break;
        }
        case State::DcsIntro:
            if ((c >= '0' && c <= '9') || c == ';') {
                if (m_prefix.size() < 64) {
                    m_prefix += c;
                    ++i;
                    break;
                }
            } else if (c == 'q') {
                m_kind = Kind::Sixel;
                m_body.clear();
                m_overflow = false;
                m_state = State::Body;
                ++i;
                break;
            }
            emitBytes("\x1bP", 2);
            emitBytes(m_prefix);
            m_state = State::Ground;
            break;
        case State::Body: {
            // ESC may start ST; CAN and SUB cancel the string; BEL ends an OSC.
            const bool osc = m_kind != Kind::Kitty && m_kind != Kind::Sixel;
            size_t j = i;
            while (j < len) {
                const char b = data[j];
                if (b == 0x1b || b == 0x18 || b == 0x1a || (osc && b == 0x07))
                    break;
                ++j;
            }
            bodyByte(data + i, j - i);
            i = j;
            if (j == len)
                break;
            ++i;
            if (data[j] == 0x1b)
                m_state = State::BodyEsc;
            else if (data[j] == 0x07)
                finishBody();
            else
                abortBody();
            break;
        }
        case State::BodyEsc:
            if (c == '\\') {
                ++i;
                finishBody();
            } else {
                // An ESC inside a string that is not ST ends the string and starts a new sequence,
                // as in any VT parser: drop the image, look at this ESC again.
                abortBody();
                m_state = State::Esc;
            }
            break;
        }
    }
}

void ImageProtocol::bodyByte(const char *data, size_t len)
{
    if (!len || m_overflow)
        return;
    if (qint64(m_body.size()) + qint64(len) > kMaxSequenceBytes) {
        // Keep the start (kitty's keys are there), drop the rest and wait for the end.
        m_overflow = true;
        return;
    }
    m_body.append(data, int(len));
}

void ImageProtocol::abortBody()
{
    m_state = State::Ground;
    m_kind = Kind::None;
    m_body.clear();
    m_overflow = false;
}

void ImageProtocol::finishBody()
{
    m_state = State::Ground;
    const Kind kind = m_kind;
    const bool overflow = m_overflow;
    QByteArray body;
    body.swap(m_body);
    m_kind = Kind::None;
    m_overflow = false;
    switch (kind) {
    case Kind::Kitty:
        handleKitty(body, overflow);
        break;
    case Kind::Sixel:
        if (!overflow)
            handleSixel(body);
        break;
    case Kind::ITermFile:
    case Kind::ITermMultipart:
    case Kind::ITermPart:
    case Kind::ITermEnd:
        handleITerm(kind, body, overflow);
        break;
    case Kind::None:
        break;
    }
}

// ---- kitty -------------------------------------------------------------------------------------

ImageProtocol::Kitty ImageProtocol::parseKittyKeys(const QByteArray &control)
{
    Kitty k;
    for (const QByteArray &pair : control.split(',')) {
        if (pair.size() < 3 || pair.at(1) != '=')
            continue;
        const QByteArray value = pair.mid(2);
        const char letter = value.at(0);
        const int n = int(std::clamp<qlonglong>(value.toLongLong(), -(1 << 30), 1 << 30));
        switch (pair.at(0)) {
        case 'a': k.a = letter; break;
        case 't': k.t = letter; break;
        case 'o': k.o = letter; break;
        case 'd': k.d = letter; break;
        case 'f': k.f = n; break;
        case 's': k.s = n; break;
        case 'v': k.v = n; break;
        case 'S': k.S = n; break;
        case 'O': k.O = n; break;
        case 'i': k.i = n; break;
        case 'I': k.I = n; break;
        case 'p': k.p = n; break;
        case 'c': k.c = n; break;
        case 'r': k.r = n; break;
        case 'C': k.C = n; break;
        case 'q': k.q = n; break;
        case 'x': k.x = n; break;
        case 'y': k.y = n; break;
        case 'w': k.w = n; break;
        case 'h': k.h = n; break;
        case 'm': k.m = n; break;
        case 'U': k.U = n; break;
        default: break; // X, Y, z and the animation keys do not change what a cell shows
        }
    }
    return k;
}

void ImageProtocol::handleKitty(const QByteArray &body, bool overflow)
{
    const int semi = body.indexOf(';');
    const QByteArray control = semi < 0 ? body : body.left(semi);
    const Kitty k = parseKittyKeys(control);
    const QByteArray payload = semi < 0 ? QByteArray() : body.mid(semi + 1);
    if (m_chunking) {
        // A continuation chunk: as in kitty, only its m and q keys count.
        if (overflow || qint64(m_chunkData.size()) + payload.size() > kMaxSequenceBytes) {
            m_chunkOverflow = true;
            m_chunkData.clear();
        } else if (!m_chunkOverflow) {
            m_chunkData += payload;
        }
        if (control.startsWith("q=") || control.contains(",q="))
            m_chunkKeys.q = k.q;
        if (k.m == 1)
            return;
        m_chunking = false;
        const Kitty keys = m_chunkKeys;
        QByteArray data;
        data.swap(m_chunkData);
        if (m_chunkOverflow)
            kittyReply(keys, "EFBIG:the image is too large");
        else
            processKitty(keys, data);
        m_chunkOverflow = false;
        return;
    }
    if (k.m == 1 && k.t == 'd') {
        m_chunking = true;
        m_chunkOverflow = overflow;
        m_chunkKeys = k;
        m_chunkData = overflow ? QByteArray() : payload;
        return;
    }
    if (overflow) {
        kittyReply(k, "EFBIG:the image is too large");
        return;
    }
    processKitty(k, payload);
}

void ImageProtocol::kittyReply(const Kitty &k, const QByteArray &message)
{
    // kitty answers only a command that named its image, and q=1 / q=2 silence OK / everything.
    if (!reply || (!k.i && !k.I))
        return;
    const bool ok = message == "OK";
    if (k.q >= 2 || (ok && k.q >= 1))
        return;
    QByteArrayList keys;
    if (k.i)
        keys << "i=" + QByteArray::number(k.i);
    if (k.I)
        keys << "I=" + QByteArray::number(k.I);
    if (k.p)
        keys << "p=" + QByteArray::number(k.p);
    const QByteArray out = "\x1b_G" + keys.join(',') + ';' + message + "\x1b\\";
    reply(out.constData(), size_t(out.size()));
}

void ImageProtocol::storeKitty(int id, const Stored &image)
{
    if (m_images.contains(id))
        m_imageOrder.removeOne(id);
    m_images.insert(id, image);
    m_imageOrder.append(id);
    while (m_imageOrder.size() > kMaxStoredImages)
        m_images.remove(m_imageOrder.takeFirst());
}

void ImageProtocol::forgetKitty(int id)
{
    if (m_images.remove(id))
        m_imageOrder.removeOne(id);
}

void ImageProtocol::processKitty(Kitty k, const QByteArray &payload)
{
    if (k.i && k.I) {
        kittyReply(k, "EINVAL:i and I cannot both be given");
        return;
    }
    switch (k.a) {
    case 'd':
        // The rows already on screen stay: to the core they are text (core/InlineImage.h). What
        // goes is the stored image, so a later a=p of it fails as it would in kitty.
        switch (k.d) {
        case 'a': case 'A':
            m_images.clear();
            m_imageOrder.clear();
            m_numbers.clear();
            break;
        case 'i': case 'I':
            if (k.i)
                forgetKitty(k.i);
            break;
        case 'n': case 'N':
            if (k.I && m_numbers.contains(k.I))
                forgetKitty(m_numbers.take(k.I));
            break;
        default:
            break; // by placement, cell, row, column or z-index: nothing is stored per placement
        }
        return;
    case 'p': {
        const int id = k.i ? k.i : m_numbers.value(k.I);
        const auto found = m_images.constFind(id);
        if (!id || found == m_images.constEnd()) {
            kittyReply(k, "ENOENT:no image with this id");
            return;
        }
        Stored image = *found;
        const QRect crop = cropFor(k.x, k.y, k.w, k.h);
        if (!crop.isNull()) {
            Loaded source;
            source.path = image.path;
            source.pixels = image.pixels;
            image = materialise(source, crop);
            if (image.path.isEmpty()) {
                kittyReply(k, "EINVAL:the source rectangle is outside the image");
                return;
            }
        }
        place(image, QSize(std::max(0, k.c), std::max(0, k.r)), k.C != 1);
        k.i = id; // an image found by number is answered with its id as well, as kitty does
        kittyReply(k, "OK");
        return;
    }
    case 't': case 'T': case 'q':
        break;
    default:
        kittyReply(k, "EINVAL:unsupported action");
        return;
    }

    const Loaded loaded = loadKitty(k, payload);
    if (!loaded.error.isEmpty()) {
        kittyReply(k, loaded.error);
        return;
    }
    if (k.a == 'q') {
        kittyReply(k, "OK"); // a query displays and keeps nothing
        return;
    }
    const Stored image = materialise(loaded);
    if (image.path.isEmpty()) {
        kittyReply(k, "EIO:the image could not be stored");
        return;
    }
    if (k.I) {
        // An image number gets an id from the terminal; the reply carries both.
        k.i = m_nextId++;
        if (m_numbers.contains(k.I))
            forgetKitty(m_numbers.value(k.I));
        m_numbers.insert(k.I, k.i);
    }
    if (k.i)
        storeKitty(k.i, image);
    if (k.a == 'T') {
        if (k.U == 1) {
            // Unicode placeholders need the program's U+10EEEE cells to be drawn as the image:
            // not supported, so say so and let the program fall back.
            kittyReply(k, "EINVAL:unicode placeholders are not supported");
            return;
        }
        Stored shown = image;
        const QRect crop = cropFor(k.x, k.y, k.w, k.h);
        if (!crop.isNull()) {
            Loaded source = loaded;
            shown = materialise(source, crop);
            if (shown.path.isEmpty()) {
                kittyReply(k, "EINVAL:the source rectangle is outside the image");
                return;
            }
        }
        place(shown, QSize(std::max(0, k.c), std::max(0, k.r)), k.C != 1);
    }
    kittyReply(k, "OK");
}

ImageProtocol::Loaded ImageProtocol::loadKitty(const Kitty &k, const QByteArray &payload) const
{
    Loaded l;
    QByteArray data;
    switch (k.t) {
    case 'd':
        if (!decodeBase64(payload, &data)) {
            l.error = "EINVAL:bad base64 data";
            return l;
        }
        break;
    case 'f':
    case 't': {
        QByteArray name;
        if (!decodeBase64(payload, &name)) {
            l.error = "EINVAL:bad base64 file name";
            return l;
        }
        const QString path = QString::fromUtf8(name);
        l.error = refuseFile(path);
        if (!l.error.isEmpty())
            return l;
        const QString canonical = QFileInfo(path).canonicalFilePath();
        const bool asIs = k.t == 'f' && k.f == 100 && !k.o && !k.S && !k.O;
        if (asIs) {
            // The program's own file, shown where it lies: only its header is read here.
            if (QFileInfo(canonical).size() > kMaxEncodedBytes) {
                l.error = "EFBIG:the image file is too large";
                return l;
            }
            QImageReader reader(canonical);
            l.pixels = reader.size();
            if (!l.pixels.isValid()) {
                l.error = "EBADPNG:not an image file";
            } else if (!withinCaps(l.pixels)) {
                l.error = "EFBIG:the image is too large";
            } else if (storableSuffix(reader.format()).isEmpty()) {
                l.error = "EBADPNG:not a PNG image";
            } else {
                l.path = canonical;
            }
            return l;
        }
        QFile file(canonical);
        if (!file.open(QIODevice::ReadOnly) || (k.O > 0 && !file.seek(k.O))) {
            l.error = "EBADF:the file could not be read";
            return l;
        }
        const qint64 want = k.S > 0 ? qint64(k.S) : file.size() - std::max(0, k.O);
        if (want > kMaxEncodedBytes) {
            l.error = "EFBIG:the image file is too large";
            return l;
        }
        data = file.read(want);
        file.close();
        if (k.t == 't' && deletableTempFile(canonical))
            QFile::remove(canonical);
        break;
    }
    case 's':
        l.error = "EINVAL:shared memory transmission is not supported";
        return l;
    default:
        l.error = "EINVAL:unknown transmission medium";
        return l;
    }
    if (data.size() > kMaxEncodedBytes) {
        l.error = "EFBIG:the image is too large";
        return l;
    }

    const int bytesPerPixel = k.f == 24 ? 3 : k.f == 32 ? 4 : 0;
    if (bytesPerPixel) {
        if (k.s <= 0 || k.v <= 0) {
            l.error = "EINVAL:raw pixel data needs s and v";
            return l;
        }
        if (!withinCaps(QSize(k.s, k.v)) || qint64(k.s) * k.v > kMaxPixelArea) {
            l.error = "EFBIG:the image is too large";
            return l;
        }
    } else if (k.f != 100) {
        l.error = "EINVAL:unknown format";
        return l;
    }
    const qint64 rawSize = qint64(k.s) * k.v * bytesPerPixel;
    if (k.o == 'z') {
        QByteArray inflated;
        if (!inflateBounded(data, rawSize, bytesPerPixel ? rawSize + 4096 : kMaxEncodedBytes, &inflated)) {
            l.error = "EINVAL:bad or oversized zlib data";
            return l;
        }
        data.swap(inflated);
    } else if (k.o) {
        l.error = "EINVAL:unknown compression";
        return l;
    }

    if (bytesPerPixel) {
        if (data.size() < rawSize) {
            l.error = "ENODATA:insufficient image data";
            return l;
        }
        l.decoded = QImage(reinterpret_cast<const uchar *>(data.constData()), k.s, k.v, k.s * bytesPerPixel,
                           bytesPerPixel == 3 ? QImage::Format_RGB888 : QImage::Format_RGBA8888)
                        .copy();
        l.pixels = l.decoded.size();
        if (l.decoded.isNull())
            l.error = "ENOMEM:the image could not be decoded";
        return l;
    }
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    l.pixels = reader.size();
    if (!l.pixels.isValid()) {
        l.error = "EBADPNG:not a PNG image";
        return l;
    }
    if (!withinCaps(l.pixels)) {
        l.error = "EFBIG:the image is too large";
        return l;
    }
    l.suffix = storableSuffix(reader.format());
    if (l.suffix.isEmpty()) {
        l.error = "EBADPNG:not a PNG image";
        return l;
    }
    l.encoded = data;
    return l;
}

// ---- storing and placing -----------------------------------------------------------------------

ImageProtocol::Stored ImageProtocol::materialise(Loaded image, const QRect &crop) const
{
    if (!crop.isNull()) {
        QImage source = image.decoded;
        if (source.isNull() && !image.path.isEmpty())
            source = QImage(image.path);
        if (source.isNull() && !image.encoded.isEmpty())
            source = QImage::fromData(image.encoded);
        const QRect rect = crop.intersected(source.rect());
        if (source.isNull() || rect.isEmpty())
            return {};
        image.decoded = source.copy(rect);
        image.path.clear();
        image.encoded.clear();
    }
    if (!image.path.isEmpty())
        return {image.path, image.pixels};
    if (!image.decoded.isNull()) {
        QBuffer buffer(&image.encoded);
        buffer.open(QIODevice::WriteOnly);
        if (!image.decoded.save(&buffer, "PNG"))
            return {};
        image.suffix = QStringLiteral("png");
        image.pixels = image.decoded.size();
    }
    const QString path = inlineimage::storeImageBytes(image.encoded, image.suffix);
    if (path.isEmpty())
        return {};
    return {path, image.pixels};
}

ImageProtocol::Screen ImageProtocol::currentScreen() const
{
    Screen s = screen ? screen() : Screen{};
    if (s.cellPixels.width() <= 0 || s.cellPixels.height() <= 0)
        s.cellPixels = QSize(8, 16);
    return s;
}

void ImageProtocol::place(const Stored &image, QSize requestedCells, bool moveCursor)
{
    const Screen s = currentScreen();
    // On the normal screen a picture may take three quarters of the height, so a tall one never
    // pushes the command that printed it out of sight; a full-screen program places its own.
    const int maxCols = std::max(1, s.columns - s.cursorCol);
    const int maxRows = s.altScreen ? std::max(1, s.rows - s.cursorRow) : std::max(1, s.rows * 3 / 4);
    const QSize cells = inlineimage::cellsFor(image.pixels, s.cellPixels, maxCols, maxRows, requestedCells);
    emitBytes(inlineimage::placementBytes(image.path, cells, moveCursor));
}

// ---- iTerm2 ------------------------------------------------------------------------------------

void ImageProtocol::handleITerm(Kind kind, const QByteArray &body, bool overflow)
{
    switch (kind) {
    case Kind::ITermFile: {
        if (overflow)
            return;
        const int colon = body.indexOf(':');
        if (colon >= 0)
            showITerm(body.left(colon), body.mid(colon + 1));
        return;
    }
    case Kind::ITermMultipart:
        m_multipart = !overflow;
        m_multipartOverflow = false;
        m_multipartArgs = body;
        m_multipartData.clear();
        return;
    case Kind::ITermPart:
        if (!m_multipart || m_multipartOverflow)
            return;
        if (overflow || qint64(m_multipartData.size()) + body.size() > kMaxSequenceBytes) {
            m_multipartOverflow = true;
            m_multipartData.clear();
            return;
        }
        m_multipartData += body;
        return;
    case Kind::ITermEnd:
        if (m_multipart && !m_multipartOverflow)
            showITerm(m_multipartArgs, m_multipartData);
        m_multipart = m_multipartOverflow = false;
        m_multipartArgs.clear();
        m_multipartData.clear();
        return;
    default:
        return;
    }
}

void ImageProtocol::showITerm(const QByteArray &args, const QByteArray &base64)
{
    QHash<QByteArray, QByteArray> arg;
    for (const QByteArray &pair : args.split(';')) {
        const int eq = pair.indexOf('=');
        if (eq > 0)
            arg.insert(pair.left(eq).trimmed(), pair.mid(eq + 1).trimmed());
    }
    if (arg.value("inline") != "1")
        return; // a download: iTerm2 saves it, Relay drops it
    QByteArray data;
    if (!decodeBase64(base64, &data, true) || data.isEmpty() || data.size() > kMaxEncodedBytes)
        return;
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    Loaded l;
    l.pixels = reader.size();
    if (!withinCaps(l.pixels))
        return;
    l.suffix = storableSuffix(reader.format());
    if (!l.suffix.isEmpty()) {
        l.encoded = data;
    } else {
        if (qint64(l.pixels.width()) * l.pixels.height() > kMaxPixelArea)
            return;
        l.decoded = reader.read();
        if (l.decoded.isNull())
            return;
    }
    const Stored image = materialise(l);
    if (image.path.isEmpty())
        return;

    // width= / height=: N cells, Npx, N% of the session, or auto.
    const Screen s = currentScreen();
    auto cells = [](const QByteArray &value, int cellPx, int total) {
        if (value.isEmpty() || value == "auto")
            return 0;
        if (value.endsWith("px"))
            return (std::max(0, value.chopped(2).toInt()) + cellPx - 1) / cellPx;
        if (value.endsWith('%'))
            return total * std::clamp(value.chopped(1).toInt(), 0, 100) / 100;
        return std::max(0, value.toInt());
    };
    const int w = cells(arg.value("width"), s.cellPixels.width(), s.columns);
    const int h = cells(arg.value("height"), s.cellPixels.height(), s.rows);
    QSize requested(w, h);
    if (w > 0 && h > 0 && arg.value("preserveAspectRatio") != "0") {
        // Fit inside the w x h box, keeping the picture's shape.
        const QSize byWidth =
            inlineimage::cellsFor(image.pixels, s.cellPixels, inlineimage::kMaxCols, inlineimage::kMaxRows, QSize(w, 0));
        requested = byWidth.height() <= h ? QSize(w, 0) : QSize(0, h);
    }
    place(image, requested, arg.value("doNotMoveCursor") != "1");
}

// ---- sixel -------------------------------------------------------------------------------------

void ImageProtocol::handleSixel(const QByteArray &body)
{
    const QImage decoded = decodeSixel(m_prefix, body);
    if (decoded.isNull())
        return;
    Loaded l;
    l.decoded = decoded;
    l.pixels = decoded.size();
    const Stored image = materialise(l);
    if (!image.path.isEmpty())
        place(image, {}, true);
}

QImage ImageProtocol::decodeSixel(const QByteArray &params, const QByteArray &data)
{
    const QList<QByteArray> p = params.split(';');
    const bool transparent = p.size() > 1 && p.at(1).toInt() == 1;

    constexpr int kRegisters = 1024;
    // The VT340's default palette, in percent.
    static const unsigned char kVt340[16][3] = {
        {0, 0, 0},    {20, 20, 80}, {80, 13, 13}, {20, 80, 20}, {80, 20, 80}, {20, 80, 80}, {80, 80, 20}, {53, 53, 53},
        {26, 26, 26}, {33, 33, 60}, {60, 26, 26}, {33, 60, 33}, {60, 33, 60}, {33, 60, 60}, {60, 60, 33}, {80, 80, 80}};
    auto percent = [](int v) { return (std::clamp(v, 0, 100) * 255 + 50) / 100; };
    std::vector<QRgb> palette(kRegisters, qRgb(0, 0, 0));
    for (int i = 0; i < 16; ++i)
        palette[size_t(i)] = qRgb(percent(kVt340[i][0]), percent(kVt340[i][1]), percent(kVt340[i][2]));
    const QRgb background = transparent ? qRgba(0, 0, 0, 0) : palette[0];

    // Two passes over the same bytes: the first measures, the second paints.
    int width = 0, height = 0;
    QImage image;
    for (int pass = 0; pass < 2; ++pass) {
        const char *c = data.constData();
        const char *const end = c + data.size();
        int x = 0, y = 0, color = 0;
        bool over = false;
        auto number = [&]() {
            int n = 0;
            while (c < end && *c >= '0' && *c <= '9') {
                n = std::min(n * 10 + (*c - '0'), 10000000);
                ++c;
            }
            return n;
        };
        auto numbers = [&](int *out, int max) {
            int count = 0;
            for (;;) {
                const int n = number();
                if (count < max)
                    out[count] = n;
                ++count;
                if (c < end && *c == ';')
                    ++c;
                else
                    break;
            }
            return std::min(count, max);
        };
        auto paint = [&](int bits, int repeat) {
            if (x + repeat > kMaxPixelSide || y + 6 > kMaxPixelSide + 6) {
                over = true;
                return;
            }
            if (bits) {
                if (pass == 0) {
                    width = std::max(width, x + repeat);
                    int top = 5;
                    while (!(bits & (1 << top)))
                        --top;
                    height = std::max(height, y + top + 1);
                } else {
                    const QRgb rgb = palette[size_t(color)];
                    for (int b = 0; b < 6; ++b) {
                        if (!(bits & (1 << b)) || y + b >= image.height())
                            continue;
                        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y + b));
                        for (int xx = x, stop = std::min(x + repeat, image.width()); xx < stop; ++xx)
                            line[xx] = rgb;
                    }
                }
            }
            x += repeat;
        };
        while (c < end && !over) {
            const char ch = *c++;
            if (ch >= '?' && ch <= '~') {
                paint(ch - '?', 1);
            } else if (ch == '!') {
                const int repeat = std::max(1, number());
                if (c < end && *c >= '?' && *c <= '~')
                    paint(*c++ - '?', repeat);
            } else if (ch == '$') {
                x = 0;
            } else if (ch == '-') {
                x = 0;
                y += 6;
                if (y > kMaxPixelSide)
                    over = true;
            } else if (ch == '#') {
                int n[5] = {0, 0, 0, 0, 0};
                const int count = numbers(n, 5);
                color = std::clamp(n[0], 0, kRegisters - 1);
                if (count >= 5 && pass == 1) {
                    if (n[1] == 2) {
                        palette[size_t(color)] = qRgb(percent(n[2]), percent(n[3]), percent(n[4]));
                    } else if (n[1] == 1) {
                        // DEC's HLS puts blue at 0 degrees and red at 120: turn it to the usual wheel.
                        const QColor hsl = QColor::fromHslF(((std::clamp(n[2], 0, 360) + 240) % 360) / 360.0,
                                                           std::clamp(n[4], 0, 100) / 100.0,
                                                           std::clamp(n[3], 0, 100) / 100.0);
                        palette[size_t(color)] = hsl.rgb();
                    }
                }
            } else if (ch == '"') {
                int n[4] = {0, 0, 0, 0};
                numbers(n, 4);
                if (pass == 0) {
                    width = std::max(width, std::min(n[2], kMaxPixelSide + 1));
                    height = std::max(height, std::min(n[3], kMaxPixelSide + 1));
                }
            }
            // Anything else (line breaks the sender wrapped the data with) is ignored.
        }
        if (over)
            return {};
        if (pass == 0) {
            if (!withinCaps(QSize(width, height)) || qint64(width) * height > kMaxPixelArea)
                return {};
            image = QImage(width, height, QImage::Format_ARGB32);
            if (image.isNull())
                return {};
            image.fill(background);
        }
    }
    return image;
}

} // namespace relay
