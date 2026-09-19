// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// A view that a ToolPane hosts without knowing its type: the session info view (ⓘ) and the
// session manager. The pane asks it for its title, hands it focus and tells it how much of its
// first row the pane chrome's buttons cover. Header-only; nothing here has Q_OBJECT.
#include <QString>

namespace relay {

class PaneView {
public:
    virtual ~PaneView() = default;
    virtual QString paneTitle() const = 0;
    virtual void focusView() = 0;
    virtual void setHeaderRightInset(int pixels) = 0;
};

}  // namespace relay
