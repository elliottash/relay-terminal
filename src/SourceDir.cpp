// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SourceDir.h"

#ifndef RELAY_SOURCE_DIR
#define RELAY_SOURCE_DIR "."
#endif

const char *relaySourceDir() { return RELAY_SOURCE_DIR; }
