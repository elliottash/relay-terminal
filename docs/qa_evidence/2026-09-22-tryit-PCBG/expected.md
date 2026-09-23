# Expected — #PCBG Try it (sealed until answered)

What should happen on the staged Relay window (one pane, shell owns `sleep 600`):

1. **Ctrl+W or the pane ×** opens a **"Work is still running"** dialog — not an immediate close —
   with three buttons: *Close and stop job*, *Close and continue in background*, *Cancel*.
   Cancel is the default and Escape cancels; the pane and its job survive.
2. **Close and continue in background** removes the pane but the `sleep 600` process keeps
   running; a notice points at Sessions → Background, where the session is listed as *Running*
   and *Reopen session* brings back the same live pane (the job's PID is unchanged).
3. **Close and stop job** closes the pane and the `sleep 600` process ends.
4. The dialog states background work lasts only while Relay runs.

Failure looks like: the pane closes with no dialog; the modal offers different choices; a
backgrounded job dies; reopening starts a new, unrelated shell.
