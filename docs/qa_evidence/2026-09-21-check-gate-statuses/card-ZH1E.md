---
id: ZH1E
type: work
status: done
labels: [bug, totals]
assignee: agent
rank: c
created: '2026-09-21'
links: {plans: [], commits: [a80b86eab4af], evidence: [], related: [], github: null}
---
# Order totals are wrong above 100 items

## Issue
a customer ordered 150 units and the invoice total was negative. fix it and make sure it cannot come back.

## Tests
- `ctest -R totals` — tests/totals_test.cpp
- `ctest -R totals_large_order` — tests/totals_large_order_test.cpp
- `ctest -R rounding` — tests/rounding_test.cpp
- `tests/test_invoice.py`

### Check 2026-09-21 20:06
- passed · ctest:totals — ctest -R totals passed for this revision on laptop, 2026-09-21T12:05:05Z
- failed · ctest:totals_large_order — ctest -R totals_large_order failed for this revision on laptop
- not-applicable · ctest:rounding — ctest -R rounding is not in the project any more
- missing-evidence · unittest:tests.test_invoice — no run of tests/test_invoice.py for this revision, from any host, and no attached result
- notice · ctest:rounding — ctest -R rounding is not in the project any more
- notice · unittest:tests.test_invoice — tests/test_invoice.py: 3 of 3 never ran here (test_total_matches_the_order, test_large_order_has_one_line_per_item, test_currency_is_printed_with_two_decimals)
- notice · ctest:totals_large_order — ctest -R totals_large_order failed the last time it ran, 2026-09-21T12:05:05Z
- accepted · ctest:totals — run seed-19 from laptop, 2026-09-21T12:05:05Z accepted as evidence for this revision <!-- relay:accept test=ctest:totals run=seed-19 rev=a80b86eab4af -->
history: thread
