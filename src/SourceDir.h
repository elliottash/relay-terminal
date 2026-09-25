// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// The source tree this binary was built from: the last place Relay looks for its backend, theme
// and remote sidecar when it runs uninstalled from build/.
//
// It is a function, defined in the one translation unit compiled with RELAY_SOURCE_DIR (card
// #V52P), and not a macro on every compile line: the path differs between the checkout and each
// land.py verify slot, and a string that differs on the command line of Pane.h's includers made
// every one of them a compiler-cache miss in every tree.
const char *relaySourceDir();
