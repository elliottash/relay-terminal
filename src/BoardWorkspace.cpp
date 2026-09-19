// SPDX-License-Identifier: GPL-3.0-or-later
#include "BoardWorkspace.h"

#include <QDir>
#include <QFileInfo>

namespace relay {

QString boardRootFor(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        // A relative candidate would be resolved against the process's current directory, which is
        // the launch directory this rule exists to stop consulting; there is no pane it could
        // honestly stand for, so it is skipped rather than guessed at.
        if (candidate.isEmpty() || !QDir::isAbsolutePath(candidate))
            continue;
        for (QDir dir(candidate); ; ) {
            if (QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("issues/board.yaml"))))
                return dir.absolutePath();
            if (!dir.cdUp())
                break;
        }
    }
    return QString();
}

}  // namespace relay
