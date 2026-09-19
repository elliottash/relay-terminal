// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Pane titles and tab labels (issue JRWQ). The worker writes a pane's title on a cheap chores-role
// side call (`session_title`); a tab's label names where the tab is (owner, 2026-09-19: the repo
// name of the associated project, otherwise the folder of the active pane). These are the pure
// rules — tidying a title, a tab's place name, and the offline join that still labels a tab with
// no terminal pane in it — so they can be tested without a window. The "same work" rule no longer
// has a worker twin: nothing asks a model about a tab label any more.
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
// the first. It labels only a tab with no terminal pane, whose label is still built from titles.
bool relatedText(const QStringList &titles);

// The tab label: one phrase when the panes are related ("Fixing pane drag"), otherwise the pane
// titles joined with "; " ("Fixing pane drag; Release notes"), shortened to `limit` characters.
QString join(const QStringList &titles, bool related, const QString &phrase = QString(),
             int limit = kMaxLabel);

// Cut at a word boundary with an ellipsis, as sidecall.clip does in the worker.
QString clip(const QString &text, int limit);

// The automatic tab title: where the tab is, not what it is doing. `project` is the pane's
// candidate project (`projects::candidateFor`, empty when the directory is in none); a project
// answers its directory name — the repo name, the same thing `projects::nameFor` gives — and no
// project answers the folder of `cwd` itself, "~" at the home directory. Empty in, empty out.
QString placeTitle(const QString &cwd, const QString &project);

}  // namespace titles
}  // namespace relay
