# Background command spacing investigation — 2026-09-22

Reproduced against working tree based on 665814e74d71, using the real Bash PTY
harness in tests/test_shell.py, without launching apt or requiring sudo.

```python
import time
from tests.test_shell import BashSession
s = BashSession()
try:
    s.wait('ready')
    s.submit("(sleep 0.3; printf 'LATE_OUTPUT\\n') &")
    s.wait('ready')
    time.sleep(0.5)
    s.drain()
    start = len(s.output)
    s.submit("printf 'LS_OUTPUT\\n'")
    s.wait('ready')
    s.drain()
    print(repr(bytes(s.output[start:])))
finally:
    s.close()
```

Observed visible sequence (control sequences omitted):

```text
LATE_OUTPUT
[1]+  Done                    ( sleep 0.3; printf 'LATE_OUTPUT\n' )
printf 'LS_OUTPUT\n'

LS_OUTPUT
```

The captured bytes contain exactly one CRLF between the Done notification and
`printf`, then input row marking and another CRLF before LS_OUTPUT. Thus the
upper gap is absent and the lower gap survives, matching the reported snippet.

Cause: shell/integration.bash appends spacing to PS1 at shell startup. Late
background output arrives after that prompt, consuming its separation. Bash
prints the completion notification during the next bound Readline load/redraw;
__relay_load sets READLINE_LINE but does not restore separation after the
notification. __relay_debug supplies the lower gap immediately before execution.

This investigation does not establish a defect in agent prompt spacing, and does
not include a runtime fix or claim graphical verification.
