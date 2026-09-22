# Current clean-tree reproduction of #561P

2026-09-22 UTC: the original startup crash does **not** reproduce at committed tree
82acbc04993af406b9b091f659165e6ba356241c (the tree compiled by its land build gate).
No speculative model-catalog code change was made. This proves current startup under the
recorded conditions; it does not identify the write that corrupted the old a2204ba4 heap.

Binary: `/tmp/claude-1000/land/1cxd-b/verify/build/relay`.
Matching backend/data: `/tmp/claude-1000/land/1cxd-b/verify/src`.
Binary SHA256: `c285932e3cf5dbb925c9f1b78b53a53e5b7af737779296f6f3ab14e9ecc00ca9`.

Launched under a private `xvfb-run -a` display with `--clean-shell --fresh --workspace <sandbox>`.
HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME pointed to new empty directories;
RELAY_KEYRING=off, RELAY_NO_ISOLATION=1, RELAY_DATA_DIR selected matching clean sources.
No model prompt was sent. The worker reached ready and processed model configuration.
`timeout 15s` deliberately stopped it: exit 124, `gui_quit reason=signal`, uptime 14 seconds,
no `gui_crash` or allocator error. The worker's code-15 shutdown is the intentional stop.

`clean-start.log` is that run. `shared-start.log` independently shows 11 seconds alive on the
shared build before the same deliberate stop; it is supporting evidence only. The committed-tree
run establishes that clean-export QA no longer needs the old f72960e2 binary for this case.

Historical cause remains unestablished. A separate verifier should repeat startup before closing
this card, rather than treating a non-reproduction as a diagnosed memory-safety fix.
