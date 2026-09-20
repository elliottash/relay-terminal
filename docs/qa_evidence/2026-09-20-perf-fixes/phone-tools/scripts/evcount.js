// Deeper than rrpcount.js: also record the inner worker-event name of every `agent` message, so
// "no tool text reached the phone" is a count and not an inference (#3H5T).
(() => {
  window.__rx = []; window.__ev = {}; window.__ts = []; window.__err = [];
  if (window.__evcount) return "reset";
  window.__evcount = true;
  const parse = JSON.parse;
  JSON.parse = function (text) {
    const value = parse.apply(this, arguments);
    try {
      if (value && typeof value === "object" && typeof value.t === "string") {
        window.__rx.push([value.t, typeof text === "string" ? text.length : 0]);
        if (value.t === "agent" && value.event && typeof value.event.event === "string") {
          const k = value.event.event;
          window.__ev[k] = window.__ev[k] || [0, 0];
          window.__ev[k][0] += 1;
          window.__ev[k][1] += typeof text === "string" ? text.length : 0;
        }
      }
    } catch (e) { /* counting must never break the page */ }
    return value;
  };
  const stringify = JSON.stringify;
  JSON.stringify = function (value) {
    try { if (value && typeof value === "object" && typeof value.t === "string") window.__ts.push(value.t); }
    catch (e) { /* as above */ }
    return stringify.apply(this, arguments);
  };
  return "installed";
})()
