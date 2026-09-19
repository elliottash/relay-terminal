// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Pane titles and tab labels (issue JRWQ). The worker writes a pane's title on a cheap chores-role
// side call (`session_title`); a tab's label is derived from the titles its panes already have, so
// it costs no extra title call. These are the pure rules — tidying a title, deciding whether two
// panes are on the same work without a model, joining and shortening the label — so they can be
// tested without a window. The "same work" rule mirrors relay_core/titles.py, which is what the
// worker uses when a model *is* configured to make the same judgement better.
#include <QString>
#include <QStringList>

namespace relay {
namespace titles {

// At most ~6 words in the header, per the issue; a hand-set name may be longer.
constexpr int kMaxTitle = 60;
constexpr int kMaxWords = 6;
constexpr int kMaxUserTitle = 200;
constexpr int kMaxLabel = 80;

// Whitespace collapsed to single spaces, trimmed.
QString collapse(const QString &text);

// A model reply or a user's typing reduced to a header-sized phrase: quotes and a leading
// "Title:" removed, a trailing period dropped, capped at `limit` characters on a word boundary
// and at `maxWords` words (0 keeps every word).
QString clean(const QString &text, int limit = kMaxTitle, int maxWords = kMaxWords);

// Non-empty titles, collapsed, in order, without case-insensitive repeats.
QStringList distinct(const QStringList &titles);

// The offline "are these panes on the same work?" answer: every title shares a content word with
// the first. Used until the worker answers, and whenever no model is configured.
bool relatedText(const QStringList &titles);

// The tab label: one phrase when the panes are related ("Fixing pane drag"), otherwise the pane
// titles joined with "; " ("Fixing pane drag; Release notes"), shortened to `limit` characters.
QString join(const QStringList &titles, bool related, const QString &phrase = QString(),
             int limit = kMaxLabel);

// Cut at a word boundary with an ellipsis, as sidecall.clip does in the worker.
QString clip(const QString &text, int limit);

}  // namespace titles
}  // namespace relay
