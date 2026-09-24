# expected.md — sealed expected result for the #PGBZ phone recheck

On the affected iPhone, pairing completes and the "this browser could not keep
the pairing key" error does not appear; after fully closing and reopening the
browser, the app reconnects to the desktop without asking to pair again. A
browser that genuinely cannot keep the key (fresh Private Window, or cleared
site data) still refuses with the clear, actionable error rather than hanging
or half-pairing.

The desktop's remote host serves app/rrp.js from this checkout, so the fix
reaches the phone once the host has been restarted after commit 9c6bea96 and
the phone has reloaded the page.
