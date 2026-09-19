// SPDX-License-Identifier: AGPL-3.0-or-later
#include "KeyMapper.h"

#include <QKeyEvent>

namespace relay {

uint8_t mapModifiers(Qt::KeyboardModifiers m, const KeyMapperOptions &options)
{
    uint8_t out = ModNone;
    if (m & Qt::ShiftModifier)
        out |= ModShift;
#if defined(Q_OS_MACOS)
    // Qt swaps them on macOS: ControlModifier is Command, MetaModifier is Control.
    if (m & Qt::MetaModifier)
        out |= ModCtrl;
    if (m & Qt::ControlModifier)
        out |= ModSuper;
    if ((m & Qt::AltModifier) && options.optionIsAlt)
        out |= ModAlt;
#else
    (void)options;
    if (m & Qt::ControlModifier)
        out |= ModCtrl;
    if (m & Qt::AltModifier)
        out |= ModAlt;
    if (m & Qt::MetaModifier)
        out |= ModSuper;
#endif
    return out;
}

bool mapKeyEvent(int k, Qt::KeyboardModifiers modifiers, const QString &text, bool autoRepeat, KeyInput *out,
                 const KeyMapperOptions &options)
{
    *out = KeyInput();
    out->modifiers = mapModifiers(modifiers, options);
    out->repeat = autoRepeat;
    // Super (Cmd on macOS, Meta/Windows key elsewhere) combinations belong to
    // the application and the desktop, not the program in the terminal.
    // TODO: pass them through when a program enables the kitty keyboard protocol.
    if (out->modifiers & ModSuper)
        return false;
    const bool keypad = modifiers & Qt::KeypadModifier;

    switch (k) {
    case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Meta: case Qt::Key_Alt: case Qt::Key_AltGr:
    case Qt::Key_CapsLock: case Qt::Key_NumLock: case Qt::Key_ScrollLock: case Qt::Key_Super_L: case Qt::Key_Super_R:
    case Qt::Key_Hyper_L: case Qt::Key_Hyper_R: case Qt::Key_Mode_switch:
        return false;
    case Qt::Key_Return: out->key = Key::Enter; return true;
    case Qt::Key_Enter: out->key = keypad ? Key::KpEnter : Key::Enter; return true;
    case Qt::Key_Tab: out->key = Key::Tab; return true;
    case Qt::Key_Backtab: out->key = Key::Tab; out->modifiers |= ModShift; return true;
    case Qt::Key_Backspace: out->key = Key::Backspace; return true;
    case Qt::Key_Escape: out->key = Key::Escape; return true;
    case Qt::Key_Up: out->key = Key::Up; return true;
    case Qt::Key_Down: out->key = Key::Down; return true;
    case Qt::Key_Left: out->key = Key::Left; return true;
    case Qt::Key_Right: out->key = Key::Right; return true;
    case Qt::Key_Insert: out->key = Key::Insert; return true;
    case Qt::Key_Delete: out->key = Key::Delete; return true;
    case Qt::Key_Home: out->key = Key::Home; return true;
    case Qt::Key_End: out->key = Key::End; return true;
    case Qt::Key_PageUp: out->key = Key::PageUp; return true;
    case Qt::Key_PageDown: out->key = Key::PageDown; return true;
    default:
        break;
    }
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24) {
        out->key = Key(int(Key::F1) + (k - Qt::Key_F1));
        return true;
    }
    if (keypad) {
        if (k >= Qt::Key_0 && k <= Qt::Key_9) {
            out->key = Key(int(Key::Kp0) + (k - Qt::Key_0));
            out->text = text;
            return true;
        }
        switch (k) {
        case Qt::Key_Asterisk: out->key = Key::KpMultiply; out->text = text; return true;
        case Qt::Key_Plus: out->key = Key::KpPlus; out->text = text; return true;
        case Qt::Key_Comma: out->key = Key::KpComma; out->text = text; return true;
        case Qt::Key_Minus: out->key = Key::KpMinus; out->text = text; return true;
        case Qt::Key_Period: out->key = Key::KpPeriod; out->text = text; return true;
        case Qt::Key_Slash: out->key = Key::KpDivide; out->text = text; return true;
        case Qt::Key_Equal: out->key = Key::KpEqual; out->text = text; return true;
        default: break;
        }
    }

    // Text keys. Qt's key code is the (upper-case) character for Latin keys and
    // most layouts; text is what the key produced with modifiers applied.
    char32_t cp = 0;
    if (k >= 0x20 && k < 0x110000 && k != Qt::Key_unknown && !(k & 0x01000000))
        cp = QChar::toLower(uint(k));
    if (k == Qt::Key_Space)
        cp = U' ';
    out->codepoint = cp;
    out->text = text;
    // AltGr / Option compositions arrive as text with Ctrl+Alt (Windows) or no
    // modifiers (X11): prefer the text and drop the synthetic modifiers.
    if (!text.isEmpty() && text.at(0).isPrint() && (out->modifiers & ModCtrl) && (out->modifiers & ModAlt))
        out->modifiers &= uint8_t(~(ModCtrl | ModAlt));
    if (cp == 0 && text.isEmpty())
        return false;
    return true;
}

bool mapKeyEvent(const QKeyEvent *e, KeyInput *out, const KeyMapperOptions &options)
{
    return mapKeyEvent(e->key(), e->modifiers(), e->text(), e->isAutoRepeat(), out, options);
}

} // namespace relay
