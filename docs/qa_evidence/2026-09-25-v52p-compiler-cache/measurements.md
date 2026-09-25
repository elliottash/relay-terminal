# #V52P compiler cache measurement (2026-09-25, aarch64, 20 cores, ccache 4.9.1, RELAY_JOBS 8)
Two/three slot-shaped trees (<slot>/src + <slot>/build, no build type, like land.py), target relay.

## Before moving RELAY_SOURCE_DIR off the compile line (HEAD 4bd08848 + CompilerCache.cmake)
-- Relay: compiler cache ccache (/usr/bin/ccache), dir /home/elliott/.cache/relay/ccache, base /tmp/claude-1000/v52p-measure/a, max 10G
slot a: exit 0, 33 s
Cacheable calls:   189 /  189 (100.0%)
  Hits:              0 /  189 ( 0.00%)
    Direct:          0
    Preprocessed:    0
  Misses:          189 /  189 (100.0%)
Local storage:
  Cache size (GB): 0.0 / 10.0 ( 0.26%)
  Hits:              0 /  189 ( 0.00%)
  Misses:          189 /  189 (100.0%)
-- Relay: compiler cache ccache (/usr/bin/ccache), dir /home/elliott/.cache/relay/ccache, base /tmp/claude-1000/v52p-measure/b, max 10G
slot b: exit 0, 22 s
Cacheable calls:   189 /  189 (100.0%)
  Hits:            174 /  189 (92.06%)
    Direct:        162 /  174 (93.10%)
    Preprocessed:   12 /  174 ( 6.90%)
  Misses:           15 /  189 ( 7.94%)
Local storage:
  Cache size (GB): 0.0 / 10.0 ( 0.41%)
  Hits:            174 /  189 (92.06%)
  Misses:           15 /  189 ( 7.94%)

slot c (third tree, same sources), warm: 21 s, 174/189 hits; misses were the AppPaths.h includers, e.g. ../src/src/RelayWindowModels.cpp, ../src/src/RelayWindowSettings.cpp — their command line carried -DRELAY_SOURCE_DIR="/tmp/.../c/src".
slot c rebuilt from clean with every object cached: 3.9 s, 189/189 hits (the floor).

## After (as landed in 52ea9245)
slot a (all targets): exit 0 49 s
slot b warm relay: exit 0 3.991715329 s
Cacheable calls:   191 /  191 (100.0%)
  Hits:            190 /  191 (99.48%)
    Direct:        189 /  190 (99.47%)
    Preprocessed:    1 /  190 ( 0.53%)
  Misses:            1 /  191 ( 0.52%)
Local storage:
  Cache size (GB): 0.1 / 10.0 ( 0.94%)


land.py's own verify build of 52ea9245 (slot relay-terminal-1006c7a3-0, incremental) configured with CCACHE_BASEDIR=/tmp/claude-1000/land/verify-slots/relay-terminal-1006c7a3-0; cumulative stats with slot b: 222/224 hits.

## Tests
python3 -m unittest tests.test_compiler_cache tests.test_scratch -> 7 + 16 OK
slot a ctest -R 'theme|filepane|remotepane|remoteshare|settingspane|consolemode|projectpicker|filterpopup' -> 9/9; -R '^board|^settings$|remotesettings' -> 11/11
