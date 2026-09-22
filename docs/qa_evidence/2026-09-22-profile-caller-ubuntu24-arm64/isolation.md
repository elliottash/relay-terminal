# PF14: live profiler fixture isolation

The preserved Ubuntu 24.04 arm64 failures contain socketserver/selector frames in a workload
that calls only `middle`, `leaf`, `sum`, and its generator. The raw profile records an impossible
`leaf → leaf` caller edge and zero calls on `middle → leaf`. The converter faithfully chooses the
heavier self edge and cuts the resulting cycle; this is not a flattened-name collision.

## Reproduction

Run from the repository root:

```sh
python3 docs/qa_evidence/2026-09-22-profile-caller-ubuntu24-arm64/thread-repro.py
```

The script starts eight idle TCPServer threads with a 5 ms polling interval and invokes the
unchanged roundtrip test. On local ARM64 Python 3.12.3, the original fixture failed at iteration
22 in 0.27 seconds with middle.total=0.000001 versus leaf.self=0.002666. Its raw profile is
preserved in `thread-repro-before.txt`. No full suite or network requests are needed. A simpler
background thread doing periodic arithmetic did not reproduce it in 100 attempts.

After isolating capture in a fresh Python subprocess, all 100 attempts passed under the same
server interference; the ten converter tests passed. The reproducer exits unsuccessfully if
an assertion fails and always shuts down/joins its servers. The original live caller-total
assertion and all converter checks are retained; no converter/product code changed.

## Mechanism and limits

[PEP 669](https://peps.python.org/pep-0669/#specification) specifies that monitoring callbacks
are per interpreter, not per thread. [CPython issue 130377](https://github.com/python/cpython/issues/130377)
records cProfile's move to that mechanism in 3.12. The
[3.12 implementation](https://github.com/python/cpython/blob/3.12/Modules/_lsprof.c)
registers these events in `profiler_enable`, enters/leaves the same profiler context in
`pystart_callback`/`pyreturn_callback`, and stores a single `currentProfilerContext` on the
profiler object. Its Python callbacks have no thread filter. This explains the measured
cross-thread caller contamination; it is not evidence of an ARM compiler or Relay runtime fault.

The change fixes the generated test fixture's scope, not CPython's handling of arbitrary
multithreaded profiles. Real multithreaded cProfile captures can still contain inaccurate
caller relationships; the converter cannot reconstruct information already corrupted in input.
