// The counters rrpcount.js gathers, as {rx: [[type, [count, bytes]], …], tx: …, err: […]}.
(() => {
  const rx = {};
  for (const [kind, size] of window.__rx) {
    rx[kind] = rx[kind] || [0, 0];
    rx[kind][0] += 1;
    rx[kind][1] += size;
  }
  const tx = {};
  for (const kind of window.__ts) tx[kind] = (tx[kind] || 0) + 1;
  return JSON.stringify({
    rx: Object.entries(rx).sort((a, b) => b[1][1] - a[1][1]),
    tx: Object.entries(tx).sort((a, b) => b[1] - a[1]),
    err: window.__err,
  });
})()
