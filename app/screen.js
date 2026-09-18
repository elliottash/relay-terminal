// SPDX-License-Identifier: GPL-3.0-or-later
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

// xterm's first 16, then the 6x6x6 cube and the greys, built once.
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

// Packed relay::CellColor: high byte is the kind, low 24 bits the value.
function colorOf(packed) {
  const kind = (packed >>> 24) & 0xff;
  const value = packed & 0xffffff;
  if (kind === 1) return PALETTE[value & 0xff] || null;
  if (kind === 2) {
    return `#${(value & 0xffffff).toString(16).padStart(6, '0')}`;
  }
  return null;                 // default: inherit the theme
}

// Rows per request. The protocol caps a page at 200; a phone screen holds far fewer, and a
// smaller page reaches the reader sooner.
const HISTORY_PAGE = 80;
// Rows kept here. Reaching live drops the lot, so this only bounds one journey upward.
const HISTORY_MAX = 2000;
// Fetch the next page while the top is still this far away, so the rows are there before the
// finger is.
const PREFETCH_PX = 600;

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
    this.more = true;            // is there anything older than historyTop
    this.pending = false;        // one request in flight at a time
    this.stale = false;          // output arrived while the reader was back here
    this.behind = false;
    this.onNeedHistory = null;   // (beforeRow | null, count) => void
    this.onBehind = null;        // (behind) => void, for the "new output" affordance
    this.fit();
    this._onResize = () => this.fit();
    this._onScroll = () => this.scrolled();
    window.addEventListener('resize', this._onResize);
    window.addEventListener('orientationchange', this._onResize);
    this.root.addEventListener('scroll', this._onScroll, { passive: true });
  }

  destroy() {
    window.removeEventListener('resize', this._onResize);
    window.removeEventListener('orientationchange', this._onResize);
    this.root.removeEventListener('scroll', this._onScroll);
  }

  // The host owns the size; we only scale the font so `cols` columns fit the screen.
  fit() {
    const available = this.root.clientWidth || window.innerWidth;
    if (!available || !this.cols) return;
    // 0.6 is the advance width of a monospace glyph as a fraction of the font size, near enough
    // for every font in the stack; the -2 leaves room for the padding.
    const size = Math.max(6, Math.min(16, ((available - 8) / this.cols) / 0.6));
    this.grid.style.fontSize = `${size.toFixed(2)}px`;
    this.grid.style.lineHeight = `${(size * 1.25).toFixed(2)}px`;
  }

  apply(message) {
    // Measured before anything moves: a live row changes in place, so the only thing that can
    // shift the reader is us.
    const wasAtBottom = this.atBottom();
    if (message.t === 'screen_snapshot') {
      this.rows = message.rows ?? this.rows;
      this.cols = message.cols ?? this.cols;
      this.alt = !!message.alt;
      this.lines.clear();
      this.rowNodes = [];
      this.grid.replaceChildren(this.historyBox);
      // The geometry may have changed under the rows we hold, and the alternate screen has no
      // scrollback of its own; either way what is above no longer joins on.
      this.resetHistory();
      this.fit();
    }
    for (const line of message.lines || []) {
      this.lines.set(line.row, line.segs || []);
    }
    if (message.cursor) this.cursor = message.cursor;
    this.paint(message.t === 'screen_snapshot' ? null : (message.lines || []).map((l) => l.row));
    if (wasAtBottom) {
      this.toBottom();
    } else if ((message.lines || []).length) {
      // Somebody is reading what scrolled away. Say there is more rather than dragging them to it.
      this.stale = true;
      this.setBehind(true);
    }
    this.requestIfNeeded();
  }

  // ---- scrollback ---------------------------------------------------------------------------

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

  resetHistory() {
    this.history = [];
    this.historyTop = null;
    this.more = true;
    this.pending = false;
    this.historyBox.replaceChildren();
  }

  // Back to the newest output. Rows held from before the output that arrived meanwhile no longer
  // join the live screen, so they go: the next drag upward asks for them again from the end.
  toLive() {
    if (this.stale) this.resetHistory();
    this.stale = false;
    this.toBottom();
    this.setBehind(false);
    this.requestIfNeeded();
  }

  scrolled() {
    if (this.atBottom()) {
      if (this.behind || this.stale) this.toLive();
      return;
    }
    this.requestIfNeeded();
  }

  requestIfNeeded() {
    if (this.pending || !this.more || !this.onNeedHistory) return;
    // The first page is fetched before it is needed: there has to be something above the live
    // screen for a drag upward to land in.
    if (this.historyTop !== null && this.root.scrollTop > PREFETCH_PX) return;
    this.pending = true;
    this.onNeedHistory(this.historyTop, HISTORY_PAGE);
  }

  // One `history` reply. Rows carry absolute numbers, so what we already hold is dropped by
  // number rather than by guesswork, and the page joins exactly onto the top of the buffer.
  applyHistory(message) {
    this.pending = false;
    this.more = !!message.more;
    const lines = (message.lines || []).filter((line) => Number.isInteger(line.row));
    const fresh = this.historyTop === null
      ? lines : lines.filter((line) => line.row < this.historyTop);
    if (!fresh.length) return;
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
    this.trim();
    this.requestIfNeeded();
  }

  // Bounded memory. We are travelling upward, so the rows furthest from the reader are the ones
  // nearest the live screen; those go first, and they are below the viewport, so dropping them
  // moves nothing. The buffer then no longer reaches the live screen, which is what `stale`
  // means: arriving back at the bottom starts again from the newest page.
  trim() {
    if (this.history.length <= HISTORY_MAX) return;
    while (this.history.length > HISTORY_MAX) {
      this.history.pop();
      const last = this.historyBox.lastElementChild;
      if (!last) break;
      last.remove();
    }
    this.stale = true;
  }

  historyRow(line) {
    const node = document.createElement('div');
    node.className = 'screen-row';
    for (const [text, fg, bg, attrs] of line.segs || []) {
      node.append(this.span(text, fg, bg, attrs));
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
    for (const [text, fg, bg, attrs] of segments) {
      if (!showCursor || this.cursor.col < column || this.cursor.col >= column + text.length) {
        node.append(this.span(text, fg, bg, attrs));
        column += text.length;
        continue;
      }
      // The cursor falls inside this run: split it so one cell can carry the cursor class.
      const at = this.cursor.col - column;
      if (at > 0) node.append(this.span(text.slice(0, at), fg, bg, attrs));
      const cell = this.span(text[at], fg, bg, attrs);
      cell.classList.add('cursor');
      node.append(cell);
      if (at + 1 < text.length) node.append(this.span(text.slice(at + 1), fg, bg, attrs));
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
