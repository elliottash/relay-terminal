// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Completion.h"

namespace relay {

// Complete a token using the foreground program's own command vocabulary.
Completion completeProgram(const QString &program, const QString &line, int cursor);

} // namespace relay
