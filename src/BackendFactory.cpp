// SPDX-License-Identifier: GPL-3.0-or-later
// The one place that knows both backend implementations. Kept apart from
// TerminalBackends.cpp so the engine-selection logic can be unit-tested without
// KParts or the engine library.
#include "TerminalBackends.h"

#include "KonsoleBackend.h"

#include <QWidget>
#ifdef RELAY_HAVE_ENGINE
#include "EngineBackend.h"
#endif

#include <stdexcept>

namespace relay {

TerminalBackend *createTerminalBackend(EngineKind kind, const QString &core, QWidget *parent)
{
    if (kind == EngineKind::Relay) {
#ifdef RELAY_HAVE_ENGINE
        return new EngineBackend(core, parent);
#else
        Q_UNUSED(core);
        throw std::runtime_error("This build of Relay does not include its own terminal engine.");
#endif
    }
    Q_UNUSED(core);
    return new KonsoleBackend(parent);
}

} // namespace relay
