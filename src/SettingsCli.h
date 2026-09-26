// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QString>

// Card #05J2: the headless path behind --export-settings / --import-settings. Lives outside
// main.cpp so relay-settings-export-tests can exercise the exit codes directly (the GUI app
// target cannot be linked into a test). Returns the process exit code: 0 on success, 1 on a
// bad invocation or unreadable bundle, 2 when the import has unresolved conflicts or
// unaccepted attention items — in which case nothing on disk was changed.
namespace relay {

int settingsTransferCli(bool exportMode, const QString &path, bool includeMemories,
                        const QString &resolve, bool acceptAttention);

}  // namespace relay
