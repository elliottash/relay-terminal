// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Which project's Switchboard a pane is looking at.
//
// The Switchboard is a per-project board: its cards are the files under the project's board
// folder — `switchboard/` on a board made from 2026-09-18 on, `issues/` on one filed before that
// — and that folder's `board.yaml` is the marker that says a directory is the root of one. A window can hold
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
// **Both spellings at each level, `switchboard/board.yaml` then `issues/board.yaml`** — the rule
// the worker follows (protocol 19.1). The nearest ancestor wins whatever its folder is called, so
// a project with an old `issues/` board inside a tree whose root has a `switchboard/` one is
// still its own board; a single directory holding both is its `switchboard/`, because that is the
// name Relay creates. The test of one directory is `projects::boardDirOf()`, so the GUI and the
// registry can never drift apart about what counts as a board.
QString boardRootFor(const QStringList &candidates);

}  // namespace relay
