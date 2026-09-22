// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>
#include <QTextEdit>
#include <QVariant>

namespace relay::auxiliaryZoom {
inline bool isTranscript(QWidget *widget) {
    if (auto *view = qobject_cast<QPlainTextEdit *>(widget)) return view->isReadOnly();
    if (auto *view = qobject_cast<QTextEdit *>(widget)) return view->isReadOnly();
    return false;
}

// Prefer the focused text surface; an Ask field or header still means the visible transcript.
inline QWidget *target(QWidget *leaf, QWidget *focus) {
    if (!leaf) return nullptr;
    if (focus && (focus == leaf || leaf->isAncestorOf(focus)))
        for (auto *widget = focus; widget && widget != leaf; widget = widget->parentWidget())
            if (isTranscript(widget)) return widget;
    for (auto *widget : leaf->findChildren<QWidget *>())
        if (isTranscript(widget) && widget->isVisibleTo(leaf)) return widget;
    return nullptr;
}

// Observe wheel zoom before Qt handles it, so reset also undoes wheel-only changes.
inline void rememberFont(QWidget *view) {
    if (view && !view->property("auxiliaryZoomFont").isValid())
        view->setProperty("auxiliaryZoomFont", view->font());
}

inline bool zoom(QWidget *leaf, QWidget *focus, int step) {
    QWidget *view = target(leaf, focus);
    if (!view) return false;
    rememberFont(view);
    if (!step) view->setFont(qvariant_cast<QFont>(view->property("auxiliaryZoomFont")));
    else if (auto *plain = qobject_cast<QPlainTextEdit *>(view)) plain->zoomIn(step);
    else if (auto *rich = qobject_cast<QTextEdit *>(view)) rich->zoomIn(step);
    return true;
}
} // namespace relay::auxiliaryZoom
