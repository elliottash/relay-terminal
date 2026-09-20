(() => {
  const rx = {};
  for (const [k, s] of window.__rx) { rx[k] = rx[k] || [0, 0]; rx[k][0]++; rx[k][1] += s; }
  return JSON.stringify({ rx: Object.entries(rx).sort((a,b)=>b[1][1]-a[1][1]),
                          events: Object.entries(window.__ev).sort((a,b)=>b[1][1]-a[1][1]) });
})()
