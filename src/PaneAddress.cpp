// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PaneAddress.h"

#include <QRegularExpression>
#include <atomic>

namespace relay::paneaddress {

int mint() {
    static std::atomic<int> next{0};
    return ++next;
}

QString label(int handle) {
    return handle > 0 ? QStringLiteral("p%1").arg(handle) : QString();
}

int parse(const QString &text) {
    static const QRegularExpression shape(
        QStringLiteral(R"(^\s*(?:pane\s*)?p?(\d{1,6})(?:\s*(?:·|\(|").*)?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = shape.match(text);
    if (!match.hasMatch()) return 0;
    bool ok = false;
    const int handle = match.captured(1).toInt(&ok);
    return ok && handle > 0 ? handle : 0;
}

} // namespace relay::paneaddress
