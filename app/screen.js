// SPDX-License-Identifier: AGPL-3.0-or-later
// The terminal view: a cell grid painted from screen state, never from PTY bytes.
//
// There is no emulator here on purpose. The desktop parsed the output with Relay's own engine and
// sent rows of styled runs, so this file only has to put text on screen and keep the columns
// aligned. That is what makes the phone immune to the two bugs that spoil Warp's mobile viewer:
// it cannot be handed half an escape sequence, and it never tells the host what size it is.
//
// Every string here comes from program output, so it goes in through textContent.
//
// Scrollback lives in the same scroll container as the live screen, above it, so a drag on a phone
// and a wheel on a trackpad both reach it with no gesture of our own. Pages are named by absolute
// row (docs/REMOTE-PROTOCOL.md section 6.5), which is what lets a page be de-duplicated exactly
// and joined with no hole while the shell is still printing.

const ATTR = {
  BOLD: 1 << 0, ITALIC: 1 << 1, UNDERLINE: 1 << 2, DOUBLE_UNDERLINE: 1 << 3,
  CURLY_UNDERLINE: 1 << 4, BLINK: 1 << 5, REVERSE: 1 << 6, CONCEAL: 1 << 7,
  STRIKE: 1 << 8, FAINT: 1 << 9,
};

// A link in the grid's text. The wire carries no link attribute (section 6's cells are text and
// colour only), so the client finds http(s) URLs itself, at paint time, in one row at a time — a
// URL the terminal wrapped across two rows does not link, on this view or on the desktop's.
const URL_RE = /https?:\/\/[^\s]+|www\.[^\s]+/gi;

// Sentence punctuation ends a URL; a closing bracket ends it only when it closes nothing the URL
// opened — the Wikipedia kind, `https://example.com/x_(1)`, keeps its bracket.
function trimUrl(url) {
  let end = url.length;
  for (;;) {
    const ch = url[end - 1];
    if (!ch) break;
    if (/[.,;:!?]/.test(ch)) { end -= 1; continue; }
    const open = ch === ')' ? '(' : ch === ']' ? '[' : null;
    if (open) {
      const opens = (url.slice(0, end).split(open).length - 1);
      const closes = (url.slice(0, end).split(ch).length - 1);
      if (closes > opens) { end -= 1; continue; }
    }
    break;
  }
  return url.slice(0, end);
}

function linkify(segments) {
  const text = segments.map(([text]) => text).join('');
  URL_RE.lastIndex = 0;
  const found = [];
  for (let match = URL_RE.exec(text); match; match = URL_RE.exec(text)) {
    let url = trimUrl(match[0]);
    if (url.length < 5) continue;
    found.push([match.index, match.index + url.length, url.startsWith('www.') ? `https://${url}` : url]);
  }
  if (!found.length) return segments.map(([text, fg, bg, attrs]) => ({ text, fg, bg, attrs }));
  const pieces = [];
  let at = 0;
  for (const [text, fg, bg, attrs] of segments) {
    let start = at;
    const end = at + text.length;
    for (const [from, to, url] of found) {
      if (to <= start || from >= end) continue;
      if (from > start) pieces.push({ text: text.slice(0, from - start), fg, bg, attrs });
      const a = Math.max(from, start);
      const b = Math.min(to, end);
      pieces.push({ text: text.slice(a - start, b - start), fg, bg, attrs, url });
      start = b;
    }
    if (start < end) pieces.push({ text: text.slice(start - at), fg, bg, attrs });
    at = end;
  }
  return pieces;
}

// xterm's first 16, then the 6x6x6 cube and the greys, built once. These are the fallbacks: the
// first 16 are the theme's, and what the grid actually paints is `THEMED` below.
const PALETTE = (() => {
  const base = ['#000000', '#cd0000', '#00cd00', '#cdcd00', '#1e90ff', '#cd00cd', '#00cdcd',
    '#e5e5e5', '#4c4c4c', '#ff0000', '#00ff00', '#ffff00', '#4682b4', '#ff00ff', '#00ffff',
    '#ffffff'];
  const levels = [0, 95, 135, 175, 215, 255];
  const hex = (n) => n.toString(16).padStart(2, '0');
  for (let r = 0; r < 6; r++) {
    for (let g = 0; g < 6; g++) {
      for (let b = 0; b < 6; b++) {
        base.push(`#${hex(levels[r])}${hex(levels[g])}${hex(levels[b])}`);
      }
    }
  }
  for (let grey = 0; grey < 24; grey++) {
    const value = 8 + grey * 10;
    base.push(`#${hex(value)}${hex(value)}${hex(value)}`);
  }
  return base;
})();

// The sixteen ANSI colours are the theme's, not xterm's: app/pane-theme.css is generated from the
// desktop's own theme files and defines --rt-ansi-0…15 on `.relay-pane`, which the terminal sits
// inside (app/app.js moves #terminal-pane into the pane view). A cell asks for the property and
// keeps xterm's value as the fallback, so a page with no theme in scope — a guest's terminal —
// paints exactly what it painted before, and a theme chosen on the page (pane.js `setTheme`)
// repaints the grid with no message from the desktop. 16-255 are the cube and the greys, which
// are fixed by the escape sequence itself and belong to no theme.
const THEMED = PALETTE.map((hex, n) => (n < 16 ? `var(--rt-ansi-${n}, ${hex})` : hex));

// Packed relay::CellColor: high byte is the kind, low 24 bits the value.
function colorOf(packed) {
  const kind = (packed >>> 24) & 0xff;
  const value = packed & 0xffffff;
  if (kind === 1) return THEMED[value & 0xff] || null;
  if (kind === 2) {
    return `#${(value & 0xffffff).toString(16).padStart(6, '0')}`;
  }
  return null;                 // default: inherit the theme
}

// Rows per request. The protocol caps a page at 200; a phone screen holds far fewer, and a
// smaller page reaches the reader sooner.
const HISTORY_PAGE = 80;
// The protocol's own cap on one page (section 6.5). Closing a seam asks for the whole gap up to
// this, rather than a page at a time: what costs there is the round trip, not the rows.
const HISTORY_CAP = 200;
// Rows kept here. Reaching live drops the lot, so this only bounds one journey upward.
const HISTORY_MAX = 2000;
// Fetch the next page while the top is still this far away, so the rows are there before the
// finger is.
const PREFETCH_PX = 600;
// No more than one `history_get` this often while the reader is up in the scrollback, where the
// seam is in front of them and a page they are waiting for is a page they can see arrive.
const HISTORY_INTERVAL = 250;
// And no more than one this often while they are at the live end. Output scrolling pushes a row
// off the screen on every frame, so the seam reopens on every frame, and asking the moment it is
// open is asking once per frame: on a Pixel 8 watching a 20 000-character reply that was 188
// requests in five seconds, 121 of them refused by the desktop's budget of 120 a minute (#3H5T).
// The seam still closes — section 6.5 does not let a client paint a column with a hole in it, and
// a reader who moves off the bottom gets the short cadence in `scrolled()` — it just stops
// closing once per frame over a handful of rows nobody is looking at.
const HISTORY_LIVE_INTERVAL = 1000;
// After a `rate_limited` — this client asking faster than the desktop will answer, which is the
// client's own bug and never the reader's business — wait this long before asking again.
const HISTORY_BACKOFF = 2000;

export class ScreenView {
  constructor(root) {
    this.root = root;
    this.rows = 24;
    this.cols = 80;
    this.lines = new Map();
    this.cursor = { row: 0, col: 0, visible: true };
    this.alt = false;
    this.grid = document.createElement('div');
    this.grid.className = 'screen-grid';
    // Scrollback sits above the live rows inside the same grid, so it is painted at the same
    // font size by the same run painter and scrolls with the same gesture.
    this.historyBox = document.createElement('div');
    this.historyBox.className = 'screen-history';
    this.grid.append(this.historyBox);
    this.root.replaceChildren(this.grid);
    this.rowNodes = [];
    this.history = [];           // {row, segs}, ascending and contiguous
    this.historyTop = null;      // absolute row of history[0]; null when nothing is held
    this.liveBase = null;        // absolute row of the live block's first line (frame `base`)
    this.more = true;            // is there anything older than historyTop
    this.pending = false;        // one request in flight at a time
    this.askAfter = 0;           // earliest the next one may go out (Date.now), see HISTORY_INTERVAL
    this.askTimer = null;        // a want that arrived inside that window, waiting to be merged
    this.behind = false;
    this.needsFit = true;        // something may have moved under fit(); nothing else measures
    this.fitCols = 0;            // the `cols` the current font size was measured for
    this.onNeedHistory = null;   // (beforeRow | null, count) => void
    this.onBehind = null;        // (behind) => void, for the "new output" affordance
    this.fit();
    this._onResize = () => this.refit();
    this._onScroll = () => this.scrolled();
    // A webfont arriving after the first paint changes the advance width, so the measured ratio
    // goes with it.
    this._onFonts = () => { this.ratio = 0; this.refit(); };
    window.addEventListener('resize', this._onResize);
    window.addEventListener('orientationchange', this._onResize);
    this.root.addEventListener('scroll', this._onScroll, { passive: true });
    // The container can change size with the window standing still — app/viewport.js shrinking
    // the terminal for an on-screen keyboard, the pane view laying out beside it — and before
    // fit() cached anything, re-measuring on every frame hid that. The observer is what replaces
    // it. It also fires once on observe, which is harmless: fit() finds the same width and writes
    // nothing, so there is no loop.
    if (window.ResizeObserver) {
      this._observer = new ResizeObserver(this._onResize);
      this._observer.observe(this.root);
    }
    if (document.fonts) document.fonts.addEventListener('loadingdone', this._onFonts);
  }

  destroy() {
    window.removeEventListener('resize', this._onResize);
    window.removeEventListener('orientationchange', this._onResize);
    this.root.removeEventListener('scroll', this._onScroll);
    if (this._observer) this._observer.disconnect();
    if (document.fonts) document.fonts.removeEventListener('loadingdone', this._onFonts);
    if (this.askTimer) clearTimeout(this.askTimer);
    this.askTimer = null;
  }

  // The host owns the size; we only scale the font so `cols` columns fit the screen.
  //
  // `clientWidth` excludes a vertical scrollbar, and scrollback puts one there, so the width this
  // reads changes the moment history arrives. `scrollbar-gutter: stable` reserves that space from
  // the start (style.css), which is what stops the grid being sized for a width it no longer has
  // and spilling into a horizontal scrollbar. Re-fitting is a no-op unless the size really moved,
  // because changing the font height under somebody who is reading would move their line.
  //
  // It measures only when something might have moved. Both reads below — `clientWidth` and
  // `getComputedStyle` — make the browser resolve layout and style, and fit() runs on every frame
  // the desktop sends, which made it the phone's top JS function at 22 % of non-idle JS through a
  // streamed reply (#3H5T). Nothing it measures changes between two frames: the width moves on a
  // resize, a rotation, a browser zoom or a page-font change, all of which arrive as events, and
  // `refit()` is what they all call. `cols` is the one thing the desktop can change under us, so
  // it is part of the key rather than an event.
  fit() {
    if (!this.needsFit && this.fitCols === this.cols) return;
    const available = this.root.clientWidth || window.innerWidth;
    if (!available || !this.cols) return;
    // clientWidth counts the padding, and the padding is not the same on a phone as on a wider
    // screen (style.css switches it to 16px a side), so it is read rather than assumed — an 8px
    // guess left the grid 25px wider than its container and a horizontal scrollbar under it.
    const style = window.getComputedStyle(this.root);
    const inset = (parseFloat(style.paddingLeft) || 0) + (parseFloat(style.paddingRight) || 0);
    // Two pixels of slack: a browser rounds each glyph's advance, so a grid sized to exactly the
    // content width can still land a fraction of a pixel over it and scroll sideways.
    const exact = ((available - inset - 2) / this.cols) / this.advance();
    // The desktop's columns are the desktop's width; on a phone they do not fit, and fitting them
    // anyway once meant six-pixel text nobody could read (a phone's request, 2026-09-22). So the
    // fit is a floor, not a clamp: the font is never smaller than the reader's choice (12px
    // unless the A−/A+ buttons in the bar moved it), and a grid wider than the screen scrolls
    // sideways in the wrap, which could already scroll either way.
    const fitted = Math.floor(exact * 100) / 100;
    const floor = this.fontFloor();
    const size = Math.min(Math.max(fitted, floor), Math.max(16, floor));
    const text = `${size.toFixed(2)}px`;
    this.needsFit = false;
    this.fitCols = this.cols;
    if (text === this.grid.style.fontSize) return;
    this.grid.style.fontSize = text;
    this.grid.style.lineHeight = `${(size * 1.25).toFixed(2)}px`;
  }

  // Something that fit() reads may have moved: measure again now, and once. Everything that can
  // change the answer goes through here — the window's own resize and orientation events, which
  // is also how a browser zoom and a change of the page's font size arrive, the ResizeObserver
  // for a container the window never hears about, and a webfont finishing.
  refit() {
    this.needsFit = true;
    this.fit();
  }

  // The smallest font the terminal will draw, the reader's own choice (fit() above). Six is the
  // old behaviour — always fit every column, however small that gets — and twelve reads on a
  // phone. A−/A+ in the terminal bar move it in steps of two.
  fontFloor() {
    try {
      const stored = Number(window.localStorage.getItem('relay.term-font-floor'));
      if (Number.isFinite(stored) && stored >= 6 && stored <= 24) return stored;
    } catch { /* private mode: the default stands, and A−/A+ work for the session */ }
    return 12;
  }

  setFontFloor(floor) {
    try {
      window.localStorage.setItem('relay.term-font-floor', String(Math.max(6, Math.min(24, floor))));
    } catch { /* private mode: it holds only until the page goes */ }
    this.refit();
  }

  // The advance width of one monospace glyph as a fraction of the font size, measured in the
  // grid's own font rather than assumed. A guessed 0.6 was a few per cent short of the font the
  // desktop actually gets, which over a hundred columns is enough to push the grid wider than its
  // container and put a horizontal scrollbar under the terminal. Measured once: the family does
  // not change, and the ratio does not depend on the size.
  advance() {
    if (this.ratio) return this.ratio;
    const probe = document.createElement('span');
    probe.style.cssText = 'position:absolute;visibility:hidden;white-space:pre;font-size:100px';
    probe.textContent = '0'.repeat(50);
    this.grid.append(probe);
    const width = probe.getBoundingClientRect().width;
    probe.remove();
    this.ratio = width > 0 ? width / 50 / 100 : 0.6;
    return this.ratio;
  }

  apply(message) {
    // Measured before anything moves: a live row changes in place, so the only thing that can
    // shift the reader is us.
    const wasAtBottom = this.atBottom();
    if (message.t === 'screen_snapshot') {
      const rows = message.rows ?? this.rows;
      const cols = message.cols ?? this.cols;
      const alt = !!message.alt;
      // A snapshot is usually just a full repaint — the desktop sends one whenever the whole
      // grid changed — and it says nothing about what is above. Only a change of geometry, or
      // the alternate screen, which has no scrollback of its own, breaks the join, so only
      // those throw the rows away. Discarding on every snapshot dropped a reader back to the
      // live screen the moment anything redrew.
      const broke = rows !== this.rows || cols !== this.cols || alt !== this.alt;
      this.rows = rows;
      this.cols = cols;
      this.alt = alt;
      this.lines.clear();
      this.rowNodes = [];
      this.grid.replaceChildren(this.historyBox);
      if (broke) this.resetHistory();
      this.fit();
    }
    // A scroll first, then the rows the desktop could not carry over. Order matters: the frame's
    // own lines are what the screen looks like *after* the shift (#3H5T, section 6.5).
    if (message.scroll) this.shiftRows(message.scroll);
    for (const line of message.lines || []) {
      this.lines.set(line.row, line.segs || []);
    }
    if (message.cursor) this.cursor = message.cursor;
    // Where the live block sits in the scrollback. Everything about the seam hangs off this.
    if (Number.isInteger(message.base)) this.liveBase = message.base;
    this.paint(message.t === 'screen_snapshot' ? null : (message.lines || []).map((l) => l.row));
    this.checkSeam();
    if (wasAtBottom) {
      this.toBottom();
    } else if ((message.lines || []).length) {
      // Somebody is reading what scrolled away. Say there is more rather than dragging them to it.
      this.setBehind(true);
    }
    this.requestIfNeeded();
  }

  // Rows `[top, bottom)` moved up by `by` — a negative `by` moves them down — which is what the
  // desktop sends instead of a whole screen when output scrolls (#3H5T). A streamed reply was
  // 166 snapshots of 7,747 B and a grid rebuilt 166 times; it is now a diff of a row or two and
  // this, which moves `by` row nodes and repaints none of them.
  //
  // The nodes are moved rather than repainted because their content did not change: row r shows
  // what row r + by showed. The rows the shift left empty are always in the frame's `lines`, so
  // paint() overwrites them a moment later and their stale nodes are never seen.
  shiftRows(scroll) {
    const by = Math.trunc(scroll.by || 0);
    const top = Math.max(0, Math.trunc(scroll.top || 0));
    const bottom = Math.min(this.rows, Math.trunc(scroll.bottom ?? this.rows));
    const height = bottom - top;
    if (!by || height <= 0) return;

    const moved = new Map();
    for (let row = top; row < bottom; row++) {
      const from = row + by;
      moved.set(row, from >= top && from < bottom ? (this.lines.get(from) || []) : []);
    }
    for (const [row, segs] of moved) this.lines.set(row, segs);

    // The cursor is painted into its row's node, and that node has just moved. paint() repaints
    // wherever it thinks the old one is, so it has to be told, or a second cursor is left behind.
    if (this.lastCursorRow !== undefined && this.lastCursorRow >= top && this.lastCursorRow < bottom) {
      const landed = this.lastCursorRow - by;
      this.lastCursorRow = landed >= top && landed < bottom ? landed : undefined;
    }

    // A shift that empties the region leaves nothing to move; the frame repaints all of it.
    if (Math.abs(by) >= height || this.rowNodes.length < bottom) return;
    const region = this.rowNodes.slice(top, bottom);
    const cut = by > 0 ? by : height + by;
    const rotated = region.slice(cut).concat(region.slice(0, cut));
    // Only the nodes that wrapped round move in the DOM; the rest are already in order.
    const before = this.rowNodes[bottom] || null;
    if (by > 0) {
      for (const node of region.slice(0, cut)) this.grid.insertBefore(node, before);
    } else {
      for (const node of region.slice(cut)) this.grid.insertBefore(node, region[0]);
    }
    this.rowNodes.splice(top, height, ...rotated);
  }

  // ---- scrollback ---------------------------------------------------------------------------
  //
  // The rows on screen are one column: the history buffer, then the live block. It must be
  // contiguous — `historyBottom() === liveBase` — because a hole in it is a lie about what the
  // program printed. Output while somebody is reading further up opens one: lines leave the live
  // screen into the core's scrollback, the live block starts further down, and the rows in
  // between are in neither half until they are fetched.

  atBottom() {
    return this.root.scrollHeight - this.root.scrollTop - this.root.clientHeight <= 4;
  }

  toBottom() {
    this.root.scrollTop = this.root.scrollHeight;
  }

  setBehind(behind) {
    if (behind === this.behind) return;
    this.behind = behind;
    if (this.onBehind) this.onBehind(behind);
  }

  // One past the newest row held, which is where the live block should begin.
  historyBottom() {
    return this.history.length ? this.history[this.history.length - 1].row + 1 : null;
  }

  // How many rows are missing between the buffer and the live block.
  gapRows() {
    const bottom = this.historyBottom();
    if (bottom === null || this.liveBase === null) return 0;
    return Math.max(0, this.liveBase - bottom);
  }

  // The scrollback shrank under what we hold — a clear, a reset, or the alternate screen, which
  // has none of its own. The rows are no longer these rows, so they go.
  checkSeam() {
    const bottom = this.historyBottom();
    if (bottom === null || this.liveBase === null) return;
    if (this.liveBase < bottom) this.resetHistory();
  }

  resetHistory() {
    this.history = [];
    this.historyTop = null;
    this.more = true;
    this.pending = false;
    this.historyBox.replaceChildren();
  }

  // Back to the newest output. The column is contiguous either way, so nothing is thrown away:
  // this only puts the view at the bottom and takes the affordance down.
  toLive() {
    this.toBottom();
    this.setBehind(false);
    this.requestIfNeeded();
  }

  scrolled() {
    // Even at the bottom there may be a seam to close: the gap sits directly above the live
    // block, which is exactly what somebody at the bottom is looking at the edge of.
    if (this.atBottom()) {
      this.setBehind(false);
    } else if (this.askAfter - Date.now() > HISTORY_INTERVAL) {
      // Coming off the live end puts the seam in front of them; the slow cadence that applies
      // while they are down there does not follow them up.
      this.askAfter = Date.now() + HISTORY_INTERVAL;
    }
    this.requestIfNeeded();
  }

  // Is the reader close enough to the seam to see a hole in it? A small gap is closed wherever
  // they are looking above the live end, because it costs one request; a large one waits until
  // they come down towards it, so a chatty shell cannot make the phone fetch rows nobody reads.
  nearSeam() {
    const gap = this.gapRows();
    if (gap === 0) return false;
    if (gap <= HISTORY_PAGE * 2) return true;
    const seam = this.historyBox.offsetHeight;
    return seam - (this.root.scrollTop + this.root.clientHeight) <= PREFETCH_PX;
  }

  requestIfNeeded() {
    if (this.pending || !this.onNeedHistory) return;
    // One request at a time, and the question is only *asked* every HISTORY_INTERVAL. A want that
    // arrives inside the window is not dropped — `askLater` brings it back, and by then the gap
    // it asks about is one request rather than several (#3H5T). Everything below this line reads
    // layout; above it is arithmetic, which is what keeps a frame cheap.
    const wait = this.askAfter - Date.now();
    if (wait > 0) { this.askLater(wait); return; }
    // Watching the live end is the cheap cadence; reading up in the scrollback is the quick one.
    this.askAfter = Date.now()
      + (this.atBottom() ? HISTORY_LIVE_INTERVAL : HISTORY_INTERVAL);
    // The seam comes first: a hole in the middle of the column is worse than being a page short
    // of the top, and closing it is what keeps the row numbers honest.
    if (this.nearSeam()) {
      const bottom = this.historyBottom();
      // The whole gap in one request, not a page of it: the round trip is the cost.
      const want = Math.min(this.gapRows(), HISTORY_CAP);
      this.ask(bottom + want, want);
      return;
    }
    if (!this.more) return;
    // The first page is fetched before it is needed: there has to be something above the live
    // screen for a drag upward to land in. It ends at `liveBase`, not at the newest row the core
    // holds, because the desktop's own view may be sitting back in its scrollback.
    if (this.historyTop !== null && this.root.scrollTop > PREFETCH_PX) return;
    const before = this.historyTop !== null ? this.historyTop : this.liveBase;
    this.ask(before === null ? null : before, HISTORY_PAGE);
  }

  // One request goes out, and nothing else until it has been answered or refused.
  ask(beforeRow, count) {
    this.pending = true;
    this.onNeedHistory(beforeRow, count);
  }

  // A want that turned up inside the interval, kept until it may go. One timer: the wants merge,
  // because what the next request asks for is read off the gap as it stands then, not now.
  askLater(ms) {
    if (this.askTimer) return;
    this.askTimer = setTimeout(() => {
      this.askTimer = null;
      this.requestIfNeeded();
    }, ms);
  }

  // The desktop would not answer a `history_get`. `rate_limited` means this client asked faster
  // than the budget in section 10.1 allows, which is ours to fix and not something to put under
  // the reader's composer: back off and ask again. Any other refusal means the page is not
  // coming, so stop asking until a drag asks for it.
  refused(code) {
    this.pending = false;
    if (code === 'rate_limited') {
      this.askAfter = Date.now() + HISTORY_BACKOFF;
      // Asked for again when the back-off is up rather than whenever the next frame happens to
      // arrive: a refusal at the end of a burst of output would otherwise leave the hole it was
      // closing until something else moved.
      this.askLater(HISTORY_BACKOFF);
      return;
    }
    this.more = false;
  }

  // One `history` reply. Rows carry absolute numbers, so which end it belongs to, and what of it
  // is already painted, are both read off the numbers rather than guessed at.
  applyHistory(message) {
    this.pending = false;
    const lines = (message.lines || []).filter((line) => Number.isInteger(line.row));
    const bottom = this.historyBottom();
    if (bottom === null) {
      this.insertTop(lines, message);
    } else if (lines.length && lines[0].row >= bottom) {
      this.insertBottom(lines.filter((line) => line.row >= bottom));
    } else {
      this.insertTop(lines.filter((line) => line.row < this.historyTop), message);
    }
    this.fit();
    this.requestIfNeeded();
  }

  // Older rows, above what we hold. The reader stays on their line.
  insertTop(fresh, message) {
    if (!fresh.length) {
      if (this.historyTop === null || this.historyTop <= 0) this.more = false;
      return;
    }
    if (this.historyTop !== null && fresh[fresh.length - 1].row + 1 !== this.historyTop) {
      // A hole, which only a scrollback ring evicting underneath us can make. Start from here.
      this.history = [];
      this.historyBox.replaceChildren();
    }
    const before = this.root.scrollHeight;
    const batch = document.createDocumentFragment();
    for (const line of fresh) batch.append(this.historyRow(line));
    this.historyBox.prepend(batch);
    this.history.unshift(...fresh);
    this.historyTop = this.history[0].row;
    // Everything above the viewport grew by exactly this much; keep the reader on their line.
    this.root.scrollTop += this.root.scrollHeight - before;
    this.more = this.historyTop > 0;
    this.trim();
  }

  // The rows that left the live screen while somebody was reading further up. They go below the
  // buffer and above the live block, which is where the program put them; nothing above the
  // viewport changes, so the reader does not move.
  insertBottom(fresh) {
    if (!fresh.length || fresh[0].row !== this.historyBottom()) return;
    const wasAtBottom = this.atBottom();
    const batch = document.createDocumentFragment();
    for (const line of fresh) batch.append(this.historyRow(line));
    this.historyBox.append(batch);
    this.history.push(...fresh);
    // These go in above the live block, so somebody watching the newest output would be pushed
    // off the end of it by rows they have already seen.
    if (wasAtBottom) this.toBottom();
    this.trim();
  }

  // Bounded memory. The oldest rows go — never the newest, which would reopen the seam — and only
  // ones lying entirely above the viewport, so nothing the reader is looking at moves. `more`
  // stays true, so dragging further up fetches them again.
  trim() {
    let excess = this.history.length - HISTORY_MAX;
    if (excess <= 0) return;
    const room = this.root.scrollTop - PREFETCH_PX;
    let height = 0;
    while (excess > 0) {
      const first = this.historyBox.firstElementChild;
      if (!first) break;
      const rowHeight = first.offsetHeight;
      if (height + rowHeight > room) break;
      height += rowHeight;
      first.remove();
      this.history.shift();
      excess -= 1;
    }
    if (!height) return;
    this.historyTop = this.history.length ? this.history[0].row : null;
    this.more = this.historyTop === null || this.historyTop > 0;
    this.root.scrollTop -= height;
  }

  historyRow(line) {
    const node = document.createElement('div');
    node.className = 'screen-row';
    for (const piece of linkify(line.segs || [])) {
      node.append(piece.url ? this.link(piece.text, piece.url)
                            : this.span(piece.text, piece.fg, piece.bg, piece.attrs));
    }
    if (!node.childNodes.length) node.append(document.createTextNode(' '));
    return node;
  }

  paint(onlyRows) {
    while (this.rowNodes.length < this.rows) {
      const node = document.createElement('div');
      node.className = 'screen-row';
      this.grid.append(node);
      this.rowNodes.push(node);
    }
    while (this.rowNodes.length > this.rows) {
      this.rowNodes.pop().remove();
    }
    const rows = onlyRows === null ? [...Array(this.rows).keys()] : onlyRows;
    const touched = new Set(rows);
    touched.add(this.cursor.row);
    if (this.lastCursorRow !== undefined) touched.add(this.lastCursorRow);
    this.lastCursorRow = this.cursor.row;
    for (const row of touched) {
      if (row >= 0 && row < this.rowNodes.length) this.paintRow(row);
    }
  }

  paintRow(row) {
    const node = this.rowNodes[row];
    if (!node) return;
    const segments = this.lines.get(row) || [];
    const showCursor = this.cursor.visible && this.cursor.row === row;
    node.replaceChildren();

    let column = 0;
    for (const piece of linkify(segments)) {
      const text = piece.text;
      const make = (part) => (piece.url ? this.link(part, piece.url) : this.span(part, piece.fg, piece.bg, piece.attrs));
      if (!showCursor || this.cursor.col < column || this.cursor.col >= column + text.length) {
        node.append(make(text));
        column += text.length;
        continue;
      }
      // The cursor falls inside this run: split it so one cell can carry the cursor class.
      const at = this.cursor.col - column;
      if (at > 0) node.append(make(text.slice(0, at)));
      const cell = make(text[at]);
      cell.classList.add('cursor');
      node.append(cell);
      if (at + 1 < text.length) node.append(make(text.slice(at + 1)));
      column += text.length;
    }
    if (showCursor && this.cursor.col >= column) {
      const pad = this.cursor.col - column;
      if (pad > 0) node.append(this.span(' '.repeat(pad), 0, 0, 0));
      const cell = this.span(' ', 0, 0, 0);
      cell.classList.add('cursor');
      node.append(cell);
    }
    if (!node.childNodes.length) node.append(document.createTextNode(' '));
  }

  link(text, url) {
    const node = this.span(text, 0, 0, 0);
    const anchor = document.createElement('a');
    anchor.className = 'screen-link';
    anchor.href = url;
    anchor.target = '_blank';
    anchor.rel = 'noopener noreferrer';
    anchor.append(node);
    return anchor;
  }

  span(text, fg, bg, attrs) {
    const node = document.createElement('span');
    node.textContent = (attrs & ATTR.CONCEAL) ? ' '.repeat(text.length) : text;
    let foreground = colorOf(fg);
    let background = colorOf(bg);
    if (attrs & ATTR.REVERSE) {
      const swap = foreground || 'var(--term-fg)';
      foreground = background || 'var(--term-bg)';
      background = swap;
    }
    if (foreground) node.style.color = foreground;
    if (background) node.style.background = background;
    if (attrs & ATTR.BOLD) node.style.fontWeight = '700';
    if (attrs & ATTR.FAINT) node.style.opacity = '0.65';
    if (attrs & ATTR.ITALIC) node.style.fontStyle = 'italic';
    if (attrs & (ATTR.UNDERLINE | ATTR.DOUBLE_UNDERLINE | ATTR.CURLY_UNDERLINE)) {
      node.style.textDecoration = 'underline';
    }
    if (attrs & ATTR.STRIKE) {
      node.style.textDecoration = `${node.style.textDecoration} line-through`.trim();
    }
    return node;
  }
}

// What the extra-keys row sends. A phone keyboard has none of these.
export const KEYS = {
  Esc: '\x1b', Tab: '\t', Enter: '\r', Backspace: '\x7f',
  Up: '\x1b[A', Down: '\x1b[B', Right: '\x1b[C', Left: '\x1b[D',
  Home: '\x1b[H', End: '\x1b[F', PgUp: '\x1b[5~', PgDn: '\x1b[6~',
  '^C': '\x03', '^D': '\x04', '^Z': '\x1a', '^L': '\x0c', '^A': '\x01', '^E': '\x05',
  '^K': '\x0b', '^U': '\x15', '^W': '\x17', '^R': '\x12',
};

// A key event as the bytes a terminal expects. Returns null for keys we do not send, so the
// browser keeps its own behaviour (tab-switching shortcuts, for one).
export function keyEventBytes(event) {
  const { key, ctrlKey, altKey, metaKey } = event;
  if (metaKey) return null;                 // Cmd is the tablet's, not the terminal's
  const named = {
    Enter: '\r', Tab: '\t', Escape: '\x1b', Backspace: '\x7f', Delete: '\x1b[3~',
    ArrowUp: '\x1b[A', ArrowDown: '\x1b[B', ArrowRight: '\x1b[C', ArrowLeft: '\x1b[D',
    Home: '\x1b[H', End: '\x1b[F', PageUp: '\x1b[5~', PageDown: '\x1b[6~',
    F1: '\x1bOP', F2: '\x1bOQ', F3: '\x1bOR', F4: '\x1bOS',
    F5: '\x1b[15~', F6: '\x1b[17~', F7: '\x1b[18~', F8: '\x1b[19~',
    F9: '\x1b[20~', F10: '\x1b[21~', F11: '\x1b[23~', F12: '\x1b[24~',
  };
  if (named[key]) return altKey ? `\x1b${named[key]}` : named[key];
  if (key.length !== 1) return null;        // Shift, Meta, dead keys and the rest
  if (ctrlKey) {
    const byte = controlByte(key);
    return byte === null ? null : byte;
  }
  return altKey ? `\x1b${key}` : key;
}

export function controlByte(letter) {
  // Ctrl+A..Ctrl+_ are the character minus 64; Ctrl+Space is NUL.
  if (letter === ' ') return '\x00';
  const code = letter.toUpperCase().charCodeAt(0);
  if (code < 64 || code > 95) return null;
  return String.fromCharCode(code - 64);
}
