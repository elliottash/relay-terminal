// SPDX-License-Identifier: AGPL-3.0-or-later
// Incremental scanner for the few sequences a core processes internally but
// Relay needs as ordered events: OSC 133 prompt marks, Relay's own OSC 7772 row
// roles and alternate-screen switches (CSI ? 47/1047/1049 h/l). Cores without
// such callbacks (libghostty-vt) split feed() at the end of each hit and then
// query their state, so events arrive in stream order with the right cursor
// position, even when a program enters and leaves the alternate screen within
// one read.
//
// Fast path: memchr for ESC, so plain text costs almost nothing.
#pragma once

#include "CellTypes.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace relay {

class SequenceScanner {
public:
    struct Hit {
        enum Kind { PromptMark, RowRole, AltScreen, Erase } kind = PromptMark;
        size_t end = 0;    // offset just past the sequence within the chunk
        char mark = 0;     // PromptMark: 'A', 'B', 'C', 'D'; Erase: 'J' or 'K'
        int exitCode = -1; // PromptMark 'D'
        // Qualified: Hit's own Kind enumerator is named PromptMark and shadows
        // the type within this struct.
        ::relay::PromptMark role {}; // RowRole: MarkUserShell / MarkUserAgent
        int eraseParam = 0;          // Erase: the CSI parameter, 0 when absent
    };

    // Scan data[from, len). Returns true for the first complete hit ending in
    // this range; call again with from = hit.end.
    bool next(const char *data, size_t len, size_t from, Hit *hit)
    {
        size_t i = from;
        while (i < len) {
            const char c = data[i];
            switch (m_state) {
            case State::Ground: {
                const void *esc = std::memchr(data + i, 0x1b, len - i);
                if (!esc)
                    return false;
                i = size_t(static_cast<const char *>(esc) - data) + 1;
                m_state = State::Esc;
                break;
            }
            case State::Esc:
                m_buf.clear();
                m_state = c == ']' ? State::OscNumber : c == '[' ? State::Csi : c == 0x1b ? State::Esc : State::Ground;
                ++i;
                break;
            case State::OscNumber:
                if (c >= '0' && c <= '9' && m_buf.size() < 4) {
                    m_buf.push_back(c);
                    ++i;
                } else if (c == ';' && m_buf == "133") {
                    m_buf.clear();
                    m_state = State::Osc133;
                    ++i;
                } else if (c == ';' && m_buf == "7772") {
                    m_buf.clear();
                    m_state = State::Osc7772;
                    ++i;
                } else {
                    m_state = State::Ground; // other OSC: payload is skipped by the ESC search
                }
                break;
            case State::Osc133:
                if (c == 0x07) {
                    ++i;
                    if (finishOsc(hit, i))
                        return true;
                } else if (c == 0x1b) {
                    m_state = State::Osc133Esc;
                    ++i;
                } else {
                    if (m_buf.size() < 256)
                        m_buf.push_back(c);
                    ++i;
                }
                break;
            case State::Osc133Esc:
                if (c == '\\') {
                    ++i;
                    if (finishOsc(hit, i))
                        return true;
                } else {
                    m_state = State::Esc; // aborted; reinterpret this byte after ESC
                }
                break;
            case State::Osc7772:
                if (c == 0x07) {
                    ++i;
                    if (finishRowRole(hit, i))
                        return true;
                } else if (c == 0x1b) {
                    m_state = State::Osc7772Esc;
                    ++i;
                } else {
                    if (m_buf.size() < 16)
                        m_buf.push_back(c);
                    ++i;
                }
                break;
            case State::Osc7772Esc:
                if (c == '\\') {
                    ++i;
                    if (finishRowRole(hit, i))
                        return true;
                } else {
                    m_state = State::Esc; // aborted; reinterpret this byte after ESC
                }
                break;
            case State::Csi:
                if ((c >= '0' && c <= '9') || c == ';' || c == '?') {
                    if (m_buf.size() < 64)
                        m_buf.push_back(c);
                    ++i;
                } else if (c >= 0x40 && c <= 0x7e) {
                    ++i;
                    m_state = State::Ground;
                    if ((c == 'h' || c == 'l') && isAltScreenMode()) {
                        hit->kind = Hit::AltScreen;
                        hit->end = i;
                        return true;
                    }
                    // ED / EL: a core that keeps row roles outside the line
                    // (GhosttyCore) has to be told when a row was wiped, or the
                    // role outlives the text it described. Only the selectors
                    // that can clear a whole row are worth a hit — `CSI K` and
                    // `CSI 0 K` end at the cursor and readline sends them by
                    // the hundred, and `CSI 3 J` drops scrollback, whose refs
                    // go dead on their own. DECSED / DECSEL (`CSI ? … J`) may
                    // leave protected cells standing, so they are not this.
                    if ((c == 'J' || c == 'K') && m_buf.find('?') == std::string::npos) {
                        const int param = firstParam();
                        if (c == 'J' ? param <= 2 : param == 2) {
                            hit->kind = Hit::Erase;
                            hit->end = i;
                            hit->mark = c;
                            hit->exitCode = -1;
                            hit->eraseParam = param;
                            return true;
                        }
                    }
                } else if (c >= 0x20 && c <= 0x2f) {
                    ++i; // intermediates
                } else {
                    m_state = c == 0x1b ? State::Esc : State::Ground;
                    ++i;
                }
                break;
            }
        }
        return false;
    }

    void reset() { m_state = State::Ground; }

private:
    enum class State { Ground, Esc, OscNumber, Osc133, Osc133Esc, Osc7772, Osc7772Esc, Csi };

    bool isAltScreenMode() const
    {
        if (m_buf.empty() || m_buf[0] != '?')
            return false;
        size_t start = 1;
        while (start <= m_buf.size()) {
            size_t end = m_buf.find(';', start);
            if (end == std::string::npos)
                end = m_buf.size();
            const std::string p = m_buf.substr(start, end - start);
            if (p == "47" || p == "1047" || p == "1049")
                return true;
            start = end + 1;
        }
        return false;
    }

    // The CSI's first parameter, 0 when it is absent or empty (the default for
    // every selector this scanner reads).
    int firstParam() const
    {
        int value = 0;
        for (size_t k = 0; k < m_buf.size() && m_buf[k] != ';'; ++k) {
            if (m_buf[k] < '0' || m_buf[k] > '9')
                return 0;
            value = value * 10 + (m_buf[k] - '0');
            if (value > 9999)
                return 9999;
        }
        return value;
    }

    bool finishOsc(Hit *hit, size_t end)
    {
        m_state = State::Ground;
        if (m_buf.empty())
            return false;
        const char kind = m_buf[0];
        if (kind != 'A' && kind != 'B' && kind != 'C' && kind != 'D')
            return false;
        hit->kind = Hit::PromptMark;
        hit->end = end;
        hit->mark = kind;
        hit->exitCode = -1;
        if (kind == 'D' && m_buf.size() > 2 && m_buf[1] == ';') {
            int code = 0;
            bool any = false;
            for (size_t k = 2; k < m_buf.size() && m_buf[k] >= '0' && m_buf[k] <= '9'; ++k) {
                code = code * 10 + (m_buf[k] - '0');
                any = true;
            }
            if (any)
                hit->exitCode = code;
        }
        return true;
    }

    // OSC 7772;shell / ;agent — Relay's row role (CellTypes.h). Anything else
    // in the body marks nothing, exactly as in LibVtermCore's handler.
    bool finishRowRole(Hit *hit, size_t end)
    {
        m_state = State::Ground;
        if (m_buf == "shell")
            hit->role = MarkUserShell;
        else if (m_buf == "agent")
            hit->role = MarkUserAgent;
        else
            return false;
        hit->kind = Hit::RowRole;
        hit->end = end;
        hit->mark = 0;
        hit->exitCode = -1;
        return true;
    }

    State m_state = State::Ground;
    std::string m_buf;
};

} // namespace relay
