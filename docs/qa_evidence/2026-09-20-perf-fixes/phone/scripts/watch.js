(() => {
  const prefix = "PREFIX";
  window.__seen = {};
  window.__watch = true;
  const el = document.getElementById("screen-wrap");
  const tick = () => {
    const text = el.textContent;
    let at = 0;
    for (;;) {
      const i = text.indexOf(prefix, at);
      if (i < 0) break;
      const token = text.slice(i, i + prefix.length + 13);
      if (/^\d{13}$/.test(token.slice(prefix.length)) && !(token in window.__seen))
        window.__seen[token] = Date.now();
      at = i + 1;
    }
    if (window.__watch) requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
  return "watching " + prefix;
})()
