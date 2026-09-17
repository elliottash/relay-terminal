// SPDX-License-Identifier: GPL-3.0-or-later
#include "VtCore.h"

#include "LibVtermCore.h"
#ifdef RELAY_HAVE_GHOSTTY
#include "GhosttyCore.h"
#endif

namespace relay {

QStringList availableVtCores()
{
    QStringList cores;
#ifdef RELAY_HAVE_GHOSTTY
    cores << QStringLiteral("ghostty");
#endif
    cores << QStringLiteral("libvterm");
    return cores;
}

std::unique_ptr<VtCore> createVtCore(const QString &name, int rows, int cols)
{
    const QString wanted = name.isEmpty() ? availableVtCores().value(0) : name;
#ifdef RELAY_HAVE_GHOSTTY
    if (wanted == QLatin1String("ghostty"))
        return std::make_unique<GhosttyCore>(rows, cols);
#endif
    if (wanted == QLatin1String("libvterm"))
        return std::make_unique<LibVtermCore>(rows, cols);
    return nullptr;
}

} // namespace relay
