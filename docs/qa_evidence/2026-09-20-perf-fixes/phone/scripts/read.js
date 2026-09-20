(() => {
  window.__watch = false;
  const prefix = "PREFIX";
  const out = [];
  for (const [k, v] of Object.entries(window.__seen))
    out.push((v + 229) - parseInt(k.slice(prefix.length), 10));
  out.sort((a, b) => a - b);
  return JSON.stringify({ n: out.length, min: out[0], median: out[Math.floor(out.length / 2)],
                          p90: out[Math.floor(out.length * 0.9)], max: out[out.length - 1],
                          over5s: out.filter((x) => x > 5000).length, all: out });
})()
