// SPDX-License-Identifier: AGPL-3.0-or-later
// Incremental scanner for the few sequences a core processes internally but
// Relay needs as ordered events: OSC 133 prompt marks and alternate-screen
// switches (CSI ? 47/1047/1049 h/l). Cores without such callbacks
// (libghostty-vt) split feed() at the end of each hit and then query their
// state, so events arrive in stream order with the right cursor position, even
// when a program enters and leaves the alternate screen within one read.
//
// Fast path: memchr for ESC, so plain text costs almost nothing.
#pragma once

#include <cstddef>
#include <cstring>
#include <string>

namespace relay {

class SequenceScanner {
public:
    struct Hit {
        enum Kind { PromptMark, AltScreen } kind = PromptMark;
        size_t end = 0;    // offset just past the sequence within the chunk
        char mark = 0;     // PromptMark: 'A', 'B', 'C', 'D'
        int exitCode = -1; // PromptMark 'D'
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
    enum class State { Ground, Esc, OscNumber, Osc133, Osc133Esc, Csi };

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

    State m_state = State::Ground;
    std::string m_buf;
};

} // namespace relay
