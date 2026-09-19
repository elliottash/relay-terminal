// SPDX-License-Identifier: AGPL-3.0-or-later
// The one place that knows the backend implementation. Kept apart from TerminalBackends.cpp so
// the core-selection logic can be unit-tested without linking the engine library.
#include "TerminalBackends.h"

#include <QWidget>
#ifdef RELAY_HAVE_ENGINE
#include "EngineBackend.h"
#endif

#include <stdexcept>

namespace relay {

TerminalBackend *createTerminalBackend(const QString &core, QWidget *parent)
{
#ifdef RELAY_HAVE_ENGINE
    return new EngineBackend(core, parent);
#else
    Q_UNUSED(core);
    Q_UNUSED(parent);
    throw std::runtime_error("This build of Relay does not include its terminal engine.");
#endif
}

} // namespace relay
