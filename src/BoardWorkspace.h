// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Which project's Switchboard a pane is looking at.
//
// The Switchboard is a per-project board: its cards are the files under the project's board
// folder — `.switchboard/` on a board made from 2026-09-19 on, `switchboard/` or `issues/` on an
// older one (`projects::boardFolders()`, in that precedence order) — and that folder's `board.yaml`
// is the marker that says a directory is the root of one. A window can hold
// panes from several projects at once, so "which board" has to be answered from the pane that
// asked and from nothing else. It used to fall back to the window manager's workspace and to
// QDir::currentPath(), which are both the directory Relay itself was started in: with Relay
// launched from a project, every pane in every window "found" that project's board, and a card
// dragged in one of them was written into the wrong repository (owner report, 2026-09-18).
//
// Pure QtCore and no widgets, so the rule is tested on a QTemporaryDir instead of a window.
#include <QString>
#include <QStringList>

namespace relay {

// The nearest ancestor (starting at the candidate itself) that holds a board, for the first
// candidate that has one; an empty string when none of them does. Candidates are tried in order,
// so callers put the most specific directory — the pane's terminal directory — first. Empty and
// relative candidates are skipped, and the process's own current directory is never consulted.
//
// **Every spelling at each level, in `projects::boardFolders()` order** — the rule the worker
// follows (protocol 19.1, `board.BOARD_FOLDERS`). The nearest ancestor wins whatever its folder is
// called, so a project with an old `issues/` board inside a tree whose root has a `.switchboard/`
// one is still its own board; a single directory holding more than one is read as the first in that
// order. The test of one directory is `projects::boardDirOf()`, so the GUI and the registry can
// never drift apart about what counts as a board.
QString boardRootFor(const QStringList &candidates);

// A local Markdown card in this project's board, even before its card index has loaded.
// Board documentation, threads, and files outside this board return an empty id.
QString boardCardIdForFile(const QString &path, const QString &project);

}  // namespace relay
