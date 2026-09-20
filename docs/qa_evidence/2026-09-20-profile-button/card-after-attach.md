---
id: 5WTE
type: work
status: inbox
labels: [feature]
rank: c
created: '2026-09-20'
links: {commits: [], evidence: [docs/qa_evidence/2026-09-20-profile-build], github: null, plans: [], related: []}
---
# Make the build faster

## Issue
the build takes minutes; find out where the time goes

## Profile
### build · 2026-09-20 21:17 · spark-dcc9 · `unknown`

6 steps · 2.8 s wall · 3.1 s of compile time

| Output | Seconds | Share |
|---|---:|---:|
| `CMakeFiles/beta.dir/src/beta.cpp.o` | 2.7 | 85.1% |
| `CMakeFiles/alpha.dir/src/alpha.cpp.o` | 0.3 | 8.1% |
| `libbeta.a` | 0.1 | 2.3% |
| `fixture` | 0.1 | 2.3% |
| `libalpha.a` | 0.1 | 1.7% |
| `CMakeFiles/fixture.dir/src/main.cpp.o` | 0.0 | 0.5% |
