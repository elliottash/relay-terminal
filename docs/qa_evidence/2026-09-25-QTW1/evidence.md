# #QTW1 evidence — "no key" first in via

Before (offscreen render of the priorities page with the owner's settings and key state):
`1 | In | kimi-k3 | kimi · pay-as-you-go · no …` — reason elided; read as the Kimi coding plan.

After (`priorities-after.png`, same settings):

    4 | Out | kimi-k3                  | no key · kimi · pay-as-you-go   (high)
    1 | In  | kimi-k3                  | no key · kimi · pay-as-you-go   (main)
    3 | In  | kimi-k2.7-code-highspeed | no key · kimi · pay-as-you-go   (flash)

Keys: `kimi` (pay-as-you-go) none, `kimi-code` (coding plan) keyring.
Routing: 1,567 logged draws, Kimi never a candidate (keyless row skipped, as designed).

Tests: `QT_QPA_PLATFORM=offscreen build/relay-modelpicker-tests` — 62 passed, 0 failed,
including `aKeylessPlanBesideAKeyedOneSaysNoKeyFirst`.
