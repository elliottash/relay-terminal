"""Exec an interactive Bash on the PTY after Popen has created a new session."""
import fcntl
import os
import sys
import termios
import signal
# A parent started in the background may ignore SIGINT; ignored dispositions survive exec,
# so Ctrl+C would never reach commands run by this shell. Restore the defaults.
for sig in (signal.SIGINT, signal.SIGQUIT, signal.SIGTSTP, signal.SIGTTIN, signal.SIGTTOU):
    signal.signal(sig, signal.SIG_DFL)
fcntl.ioctl(0, termios.TIOCSCTTY, 0)
os.execve('/bin/bash', ['/bin/bash', *sys.argv[1:]], os.environ)
