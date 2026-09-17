# Vendored libvterm

- Upstream: https://www.leonerd.org.uk/code/libvterm/ (Paul Evans), MIT license (see `LICENSE`).
- Version: 0.3.3, from `libvterm-0.3.3.tar.gz`
  (sha256 `09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0`).
- Imported: `LICENSE`, `include/`, `src/` (including the pre-generated `*.inc` tables).
  Not imported: `bin/`, `t/` (perl test harness), `Makefile`.

Relay builds these files into the static library `relay-vterm-c` (see `engine/CMakeLists.txt`)
so every platform gets identical emulator behaviour. Relay's changes are marked in the code
with `RELAY PATCH` comments and listed below; everything else is byte-identical to upstream.

## Relay patches

(none yet)
