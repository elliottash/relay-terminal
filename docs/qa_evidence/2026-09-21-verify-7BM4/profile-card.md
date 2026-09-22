---
id: KSNT
type: work
status: inbox
labels: [feature, build]
rank: k
created: '2026-09-21'
links: {commits: [], evidence: [docs/qa_evidence/2026-09-21-profile-build], github: null, plans: [], related: []}
---
# The build got slow last month: where does the time go?

## Issue
a clean build used to be quick. before anyone guesses, measure it.

## Profile
### build · 2026-09-22 01:24 · spark-dcc9 · `267dedc+dirty`

8 steps · 2.7 s wall · 2.8 s of compile time

| Output | Seconds | Share |
|---|---:|---:|
| `CMakeFiles/orders.dir/src/report.cpp.o` | 2.7 | 94.7% |
| `liborders.a` | 0.0 | 1.3% |
| `totals_test` | 0.0 | 0.8% |
| `CMakeFiles/orders.dir/src/totals.cpp.o` | 0.0 | 0.7% |
| `CMakeFiles/orders.dir/src/inventory.cpp.o` | 0.0 | 0.6% |
| `CMakeFiles/totals_test.dir/tests/totals_test.cpp.o` | 0.0 | 0.6% |
| `CMakeFiles/totals_large_order_test.dir/tests/totals_large_order_test.cpp.o` | 0.0 | 0.6% |
| `totals_large_order_test` | 0.0 | 0.6% |
