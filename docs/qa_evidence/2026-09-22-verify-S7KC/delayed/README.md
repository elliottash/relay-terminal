# Invalid delayed-completion fixture attempt

The initial shim was present during SSH discovery, and the GUI never acquired shell_integration/reachable capability. No compgen subprocess was launched (no delayed-calls.txt). Therefore these screenshots do not establish stale asynchronous result handling; the unavailable messages are accurate for this fixture. A second attempt activates the shim only after an ordinary SSH session is established; see ../delayed2/.
