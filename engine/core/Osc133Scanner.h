// SPDX-License-Identifier: GPL-3.0-or-later
// Incremental scanner that finds complete OSC 133 (semantic prompt) sequences
// in a PTY byte stream, across chunk boundaries. Cores that track OSC 133
// internally but do not report it as an event (libghostty-vt) use it to split
// feed() at the end of each sequence and emit VtCore::Events::promptMark with
// the cursor position the core has right after processing it.
//
// Fast path: memchr for ESC, so plain text costs almost nothing.
#pragma once

#include <cstddef>
#include <cstring>
#include <string>

namespace relay {

class Osc133Scanner {
public:
    struct Hit {
        size_t end = 0;   // offset just past the terminator within the chunk
        char kind = 0;    // 'A', 'B', 'C', 'D'
        int exitCode = -1; // for 'D'
    };

    // Scan data[from, len). Returns true and fills `hit` for the first complete
    // OSC 133 sequence that ends in this range; call again with from = hit.end.
    bool next(const char *data, size_t len, size_t from, Hit *hit)
    {
        size_t i = from;
        while (i < len) {
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
                m_state = data[i] == ']' ? State::OscNumber : State::Ground;
                m_number.clear();
                if (data[i] == 0x1b)
                    m_state = State::Esc;
                ++i;
                break;
            case State::OscNumber: {
                const char c = data[i];
                if (c >= '0' && c <= '9' && m_number.size() < 4) {
                    m_number.push_back(c);
                    ++i;
                } else if (c == ';' && m_number == "133") {
                    m_payload.clear();
                    m_state = State::Payload;
                    ++i;
                } else {
                    m_state = State::Ground; // not ours; the terminator is found by Ground's ESC search or ignored (BEL)
                }
                break;
            }
            case State::Payload: {
                const char c = data[i];
                if (c == 0x07) {
                    ++i;
                    if (finish(hit, i))
                        return true;
                } else if (c == 0x1b) {
                    m_state = State::PayloadEsc;
                    ++i;
                } else {
                    if (m_payload.size() < 256)
                        m_payload.push_back(c);
                    ++i;
                }
                break;
            }
            case State::PayloadEsc:
                if (data[i] == '\\') {
                    ++i;
                    if (finish(hit, i))
                        return true;
                } else {
                    m_state = State::Esc; // aborted; reinterpret this byte after ESC
                }
                break;
            }
        }
        return false;
    }

    void reset() { m_state = State::Ground; }

private:
    enum class State { Ground, Esc, OscNumber, Payload, PayloadEsc };

    bool finish(Hit *hit, size_t end)
    {
        m_state = State::Ground;
        if (m_payload.empty())
            return false;
        const char kind = m_payload[0];
        if (kind != 'A' && kind != 'B' && kind != 'C' && kind != 'D')
            return false;
        hit->end = end;
        hit->kind = kind;
        hit->exitCode = -1;
        if (kind == 'D' && m_payload.size() > 2 && m_payload[1] == ';') {
            int code = 0;
            bool any = false;
            for (size_t k = 2; k < m_payload.size() && m_payload[k] >= '0' && m_payload[k] <= '9'; ++k) {
                code = code * 10 + (m_payload[k] - '0');
                any = true;
            }
            if (any)
                hit->exitCode = code;
        }
        return true;
    }

    State m_state = State::Ground;
    std::string m_number;
    std::string m_payload;
};

} // namespace relay
