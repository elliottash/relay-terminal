// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QMessageBox>
#include <QPushButton>

namespace relay::paneclose {
enum class Choice { Cancel, Stop, Background };

inline Choice ask(QWidget *parent, int count = 1) {
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Work is still running"),
        count == 1 ? QStringLiteral("This pane has active work. What would you like to do?")
                   : QStringLiteral("%1 panes have active work. What would you like to do?").arg(count),
        QMessageBox::NoButton, parent);
    box.setObjectName(QStringLiteral("activePaneClose"));
    box.setInformativeText(QStringLiteral("Background work stays available in Board → Background while Relay is running."));
    auto *stop = box.addButton(QStringLiteral("Close and stop job"), QMessageBox::DestructiveRole);
    stop->setObjectName(QStringLiteral("closeStopJob"));
    auto *background = box.addButton(QStringLiteral("Close and continue in background"), QMessageBox::ActionRole);
    background->setObjectName(QStringLiteral("closeBackgroundJob"));
    auto *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == stop) return Choice::Stop;
    if (box.clickedButton() == background) return Choice::Background;
    return Choice::Cancel;
}
} // namespace relay::paneclose
