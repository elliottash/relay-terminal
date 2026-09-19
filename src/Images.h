// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Image context for agent prompts (issue EM1E, docs/AGENT-SESSIONS-PROTOCOL.md section 17).
//
// Relay has four ways to put a picture in front of the agent: paste it into the prompt box, drop it
// there, name its path with `@`, and "Screenshot this pane". All four end at the same place — an
// image file on disk whose path goes into the composer as an `@` token — so the worker's existing
// attachment plumbing carries it and there is one code path to reason about.
//
// These are the rules only: where captures are written, what counts as an image, how a capture is
// named, and what a paste or a drop is carrying. No window and no terminal, so they are testable
// (tests/images_test.cpp).
#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>
#include <QStringList>

class QMimeData;

namespace relay {
namespace images {

// Mirrors provider.MAX_IMAGE_BYTES in backend/relay_core/provider.py: the worker refuses anything
// larger, so the GUI says so before the prompt is sent rather than after.
constexpr qint64 kMaxImageBytes = 3 * 1024 * 1024;
// Captures older than this are swept when a new one is written; a pasted screenshot is scratch.
constexpr int kKeepDays = 7;

// $XDG_CACHE_HOME/relay/images (created on demand). Pasted, dropped and captured images live here;
// an image the user named by path is used where it already is and never copied.
QString cacheDir();

// The media type of an image from its first bytes, or an empty string. Bytes, not the file name:
// the worker sniffs the same way and the provider must be told what is actually sent.
QString sniff(const QByteArray &head);
QString mediaTypeOf(const QString &path);
inline bool isImageFile(const QString &path) { return !mediaTypeOf(path).isEmpty(); }

// The `@` token for a path, quoted when it contains a space, matching the composer's own parser.
QString composerToken(const QString &path);

// `<dir>/relay-<what>-<yyyyMMdd-hhmmss>.png`, with a counter when that name is taken.
QString newCapturePath(const QString &dir, const QString &what, const QDateTime &when);

// Writes `image` as a PNG at `path`. Returns the path, or an empty string when it could not be
// written or came out larger than kMaxImageBytes (in which case nothing is left behind).
QString savePng(const QImage &image, const QString &path);

// The image files a paste or a drop is carrying: image files it names by URL first, then any
// inline image data, which is written into `dir`. Empty means "no image here", which is the
// composer's signal to insert the text instead.
QStringList fromMimeData(const QMimeData *data, const QString &dir);

// True when a paste or drop is worth asking fromMimeData about. Cheap; no files are read.
bool hasImage(const QMimeData *data);

// Deletes relay-*.png captures in `dir` older than `days`. Returns how many went.
int pruneCache(const QString &dir, int days, const QDateTime &now);

}  // namespace images
}  // namespace relay
