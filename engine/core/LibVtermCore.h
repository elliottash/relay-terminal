// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "VtCore.h"

#include <memory>

namespace relay {

// VtCore over Relay's vendored, patched libvterm 0.3.3 (engine/third_party/libvterm).
// libvterm only models the visible screen, so this adapter owns scrollback
// (with reflow), the viewport, selection, search and OSC 7/8/52/133 parsing.
class LibVtermCore final : public VtCore {
public:
    LibVtermCore(int rows, int cols);
    ~LibVtermCore() override;

    const char *name() const override { return "libvterm"; }

    void feed(const char *data, size_t len) override;
    void resize(int rows, int cols, int cellWidthPx, int cellHeightPx) override;
    int rows() const override;
    int columns() const override;
    void setScrollbackLines(int lines) override;
    void setCollectEvicted(bool on) override;
    void takeEvicted(std::vector<Line> *lines, std::vector<int> *clears) override;
    void evictAll() override;
    quint64 changeCount() const override;
    bool atGround() const override;

    bool updateFrame(ViewportFrame *frame, bool force) override;

    QString screenText() const override;
    QStringList historyText(int maxLines) const override;
    int historyLines(int fromRow, int count, std::vector<Line> *out) const override;
    bool altScreen() const override;
    MouseTracking mouseTracking() const override;
    bool bracketedPaste() const override;
    QString title() const override;
    CursorState activeCursor() const override;

    int historyRows() const override;
    int viewportTop() const override;
    bool viewportAtBottom() const override;
    void scrollViewport(int deltaRows) override;
    void scrollViewportToTop() override;
    void scrollViewportToBottom() override;
    void scrollViewportToRow(int row) override;
    bool scrollToPrompt(int direction) override;

    QString hyperlinkAt(int row, int col) const override;
    QString hyperlinkUri(uint32_t id, int row, int col) const override;
    std::vector<HyperlinkRun> hyperlinkRuns(const QString &prefix) const override;

    void selectionBegin(int row, int col, SelectionUnit unit, bool rectangle) override;
    void selectionExtend(int row, int col) override;
    void selectionClear() override;
    bool hasSelection() const override;
    QString selectedText() const override;
    void selectAll() override;

    int searchSet(const QString &needle) override;
    int searchStep(bool backwards) override;
    int searchMatchCount() const override;
    int searchMatchesInRows(int fromRow, int toRow) const override;
    int searchCurrentRow() const override;

    void sendKey(const KeyInput &key) override;
    void sendText(const QString &text) override;
    void sendMouse(const MouseInput &mouse) override;
    void paste(const QString &text) override;
    void focusChanged(bool focused) override;
    void setCellPixelSize(int, int, int, int) override {}

    void clearScrollback() override;
    void reset() override;
    void setColors(uint32_t fg, uint32_t bg, const uint32_t *palette16) override;
    void setClipboardWriteAllowed(bool allowed) override;

    // libvterm-specific knobs (tests)
    // Work done, for the tests that assert on operation counts rather than on
    // wall time (#6W0Z, #PPR4): cells converted into stored scrollback lines,
    // and rows visited by hyperlinkRuns(). Both are counted a line at a time.
    quint64 storedCells() const;
    quint64 linkRowsWalked() const;
    void setReflow(bool enabled);
    void setGraphemeClusters(bool enabled);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace relay
