// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Inline images in the terminal grid (card #1MGS): the contract between the three parts.
//
//   session/ImageProtocol  reads kitty graphics (APC G), iTerm2 (OSC 1337 File=) and sixel (DCS q)
//                          out of the byte stream before the core sees it, keeps the image as a
//                          file (imageCacheDir(), or the program's own file for kitty t=f), and
//                          feeds the core placementBytes() instead.
//   the core               stores those bytes as ordinary cells: one visible cell per image row,
//                          each carrying an OSC 8 link whose URI is imageUri(). Nothing in either
//                          core knows about images.
//   view/ImageCache +      finds the image rows through the cells' link URIs (parseImageUri) and
//   TerminalView           paints the picture over them, scaled into rows x cols cells.
//
// Anchoring images with OSC 8 is what folds already do (kProsePrefix): the rows scroll, reflow,
// get trimmed out of scrollback, erased by a clear and saved and restored with the session exactly
// like text, in both cores, because to the core they *are* text. Relay's own writes
// (MarkdownAnsi's `![alt](path)`, prompt attachments) emit a kitty t=f escape through
// TerminalSession::writeToDisplay and take the same path.
//
// Each image row is one cell wide on purpose: a row of `cols` placeholders would soft-wrap when
// the pane narrows and tear the image. The picture's width comes from the URI, and the view
// scales it down when the pane is narrower.
#include <QByteArray>
#include <QSize>
#include <QString>

namespace relay {
namespace inlineimage {

// The OSC 8 URI prefix of an image row.
constexpr char kImagePrefix[] = "relay-image:";

// The one cell each image row holds. U+2800 BRAILLE PATTERN BLANK: draws as nothing, but it is not
// whitespace, so neither core nor the serializer trims it off the end of a row.
constexpr char16_t kRowCell = u'⠀';

// A generous cap on a single picture's size in cells.
constexpr int kMaxRows = 200;
constexpr int kMaxCols = 400;

// One image row, as the URI of its cell says.
struct ImageRef {
    QString path;   // absolute path of an image file on this machine
    int row = 0;    // which of the image's rows this cell is, 0 = top
    int rows = 1;   // the image's height in cells
    int cols = 1;   // the image's width in cells (the view scales down to fit the pane)
    bool operator==(const ImageRef &o) const
    {
        return path == o.path && row == o.row && rows == o.rows && cols == o.cols;
    }
};

// `relay-image:<row>/<rows>/<cols>/<percent-encoded path>`.
QString imageUri(const ImageRef &ref);
// False for any URI that is not a well-formed image URI (a hand-typed OSC 8 link included):
// rows and cols are clamped to [1, kMax*], row to [0, rows).
bool parseImageUri(const QString &uri, ImageRef *out);

// How many cells a picture of `pixels` takes: its natural size in cells, scaled down (keeping the
// aspect ratio) so it fits within maxCols x maxRows. `requested` is what the program asked for in
// cells (kitty c=/r=, iTerm2 width=/height= in cells); a zero component means "from the aspect
// ratio", both zero means natural size. Never less than 1 x 1.
QSize cellsFor(QSize pixels, QSize cellPixels, int maxCols, int maxRows, QSize requested = {});

// The bytes fed to the core in place of an image escape, with the cursor at the image's top-left
// cell. For each row: the link, kRowCell, the link closed, a backspace, and a line feed (so later
// rows stay in the same column and the screen scrolls at the bottom as a picture would make it).
// After the last row the cursor ends where kitty puts it: on the image's last row, just past its
// right edge. `moveCursor = false` (kitty C=1) brackets it in DECSC/DECRC instead.
//
// `startCol` is the cursor's column (0-based) when the caller knows it, and every caller that can
// should pass it: rows then return with CR, LF and a cursor-forward to that column. The backspace
// form, used when it is -1, drifts one column left when the image sits in the last column (the
// cell leaves the cursor pending a wrap there, so BS steps back from it) and goes to column 0 under
// newline mode (LNM), where LF also returns the carriage.
QByteArray placementBytes(const QString &path, QSize cells, bool moveCursor = true, int startCol = -1);

// $XDG_CACHE_HOME/relay/images/terminal, created on demand. Images a program sent as data are
// kept here as files named by their content hash, so a restored session finds them again.
QString imageCacheDir();
// Writes `encoded` (PNG, JPEG, GIF or WebP bytes) into imageCacheDir() under its content hash and
// returns the path; an existing file with that name is reused. Empty on failure.
QString storeImageBytes(const QByteArray &encoded, const QString &suffix = QStringLiteral("png"));
// Deletes files in imageCacheDir() older than `days`, oldest first, then more until the directory
// holds at most `maxBytes`. Called once per session start, never per image.
void pruneImageCache(int days = 30, qint64 maxBytes = qint64(512) << 20);

} // namespace inlineimage
} // namespace relay
