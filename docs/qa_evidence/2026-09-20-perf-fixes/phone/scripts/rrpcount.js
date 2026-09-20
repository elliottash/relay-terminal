// Count RRP messages by type inside the phone's live page (#PF4K phone area).
//
//   cdp.py eval @rrpcount.js 42061        install (idempotent) and reset the counters
//   cdp.py eval @rrpread.js  42061        read them back
//
// The RRP layer decodes every frame with JSON.parse and encodes every send with JSON.stringify
// (app/rrp.js:368, :390), so wrapping those two is enough to see the plaintext without touching
// any file the page was served. Only the message's `t` and its length are kept — never its body.
(() => {
  window.__rx = [];
  window.__ts = [];
  window.__err = [];
  if (window.__rrpcount) return "reset";
  window.__rrpcount = true;
  const parse = JSON.parse;
  JSON.parse = function (text) {
    const value = parse.apply(this, arguments);
    try {
      if (value && typeof value === "object" && typeof value.t === "string") {
        window.__rx.push([value.t, typeof text === "string" ? text.length : 0]);
        if (value.t === "error" && window.__err.length < 5) window.__err.push(text.slice(0, 300));
      }
    } catch (error) { /* counting must never break the page */ }
    return value;
  };
  const stringify = JSON.stringify;
  JSON.stringify = function (value) {
    try {
      if (value && typeof value === "object" && typeof value.t === "string")
        window.__ts.push(value.t);
    } catch (error) { /* as above */ }
    return stringify.apply(this, arguments);
  };
  return "installed";
})()
