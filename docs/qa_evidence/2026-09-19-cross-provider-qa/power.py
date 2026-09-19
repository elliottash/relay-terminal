"""How many review items to tell two reviewers apart on ONE author's bugs?

Paired design: both reviewers judge the same items, so the test is McNemar's on the
discordant pairs. Power is driven by the discordant rate, not by the item count alone —
two reviewers that agree on almost everything need a lot of items to separate.
"""
import random
from math import comb

def mcnemar_p(b, c):
    """Exact two-sided binomial test on discordant pairs."""
    n = b + c
    if n == 0:
        return 1.0
    k = min(b, c)
    tail = sum(comb(n, i) for i in range(0, k + 1)) / 2 ** n
    return min(1.0, 2 * tail)

def power(n_items, p1, p2, rho, trials=4000, alpha=0.05, seed=1):
    """rho = P(both right | at least one right)-ish coupling: how much the two reviewers
    share blind spots. Higher rho = more agreement = fewer discordant pairs = less power."""
    rnd = random.Random(seed)
    hits = 0
    for _ in range(trials):
        b = c = 0
        for _ in range(n_items):
            shared = rnd.random() < rho          # item decided by shared ability
            if shared:
                ok = rnd.random() < (p1 + p2) / 2
                r1 = r2 = ok
            else:
                r1 = rnd.random() < p1
                r2 = rnd.random() < p2
            if r1 and not r2: b += 1
            elif r2 and not r1: c += 1
        if mcnemar_p(b, c) < alpha:
            hits += 1
    return hits / trials

print("Detecting a reviewer difference on ONE author's bugs (paired, alpha=.05)\n")
print(f"{'items':>6} {'.35 vs .45':>12} {'.35 vs .50':>12} {'.35 vs .55':>12}   (rho=0.5)")
for n in (100, 200, 400, 800, 1600):
    row = [power(n, .35, q, .5) for q in (.45, .50, .55)]
    print(f"{n:>6} " + " ".join(f"{p:>12.2f}" for p in row))
print(f"\n{'items':>6} {'rho=0.3':>10} {'rho=0.5':>10} {'rho=0.7':>10} {'rho=0.85':>10}   (.35 vs .50)")
for n in (200, 400, 800, 1600):
    row = [power(n, .35, .50, r) for r in (.3, .5, .7, .85)]
    print(f"{n:>6} " + " ".join(f"{p:>10.2f}" for p in row))
