# Expected result — Try it for `#4DS8` (sealed)

What a pass looks like on this machine (PipeWire, one USB webcam microphone, no `pactl`):

1. Options › Voice, Microphone row: a **dropdown**, not a text box. Its entries are exactly:
   - `Desktop default`
   - `alsa_input.usb-046d_HD_Pro_Webcam_C920_B38639BF-02.analog-stereo · C920 PRO HD Webcam Analog Stereo`
2. Choosing the webcam entry writes `device=alsa_input.usb-046d_HD_Pro_Webcam_C920_B38639BF-02.analog-stereo`
   under `[voice]` in the settings file, and choosing "Desktop default" removes the key again.
3. A voice recording made with the webcam chosen runs `pw-record --target=alsa_input.usb-046d_HD_Pro_Webcam_C920_B38639BF-02.analog-stereo …`.
4. If `voice/device` already holds a name that is not currently enumerated, the dropdown still
   lists it, labelled `… · not currently available`.

Failure looks like: the row is still a free-text box; the dropdown lists nothing (or devices from
a tool that is not the one recording); a chosen device is written but the recording still comes
from the default source; a saved-but-unplugged source silently disappears from the dropdown.
