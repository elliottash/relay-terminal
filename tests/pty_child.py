"""Exec an interactive Bash on the PTY after Popen has created a new session."""
import fcntl
import os
import sys
import termios
fcntl.ioctl(0, termios.TIOCSCTTY, 0)
os.execve('/bin/bash', ['/bin/bash', *sys.argv[1:]], os.environ)
