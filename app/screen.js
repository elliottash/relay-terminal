// SPDX-License-Identifier: GPL-3.0-or-later
// The terminal view: a cell grid painted from screen state, never from PTY bytes.
//
// There is no emulator here on purpose. The desktop parsed the output with Relay's own engine and
// sent rows of styled runs, so this file only has to put text on screen and keep the columns
// aligned. That is what makes the phone immune to the two bugs that spoil Warp's mobile viewer:
// it cannot be handed half an escape sequence, and it never tells the host what size it is.
//
// Every string here comes from program output, so it goes in through textContent.

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
    this.root.replaceChildren(this.grid);
    this.rowNodes = [];
    this.fit();
    this._onResize = () => this.fit();
    window.addEventListener('resize', this._onResize);
    window.addEventListener('orientationchange', this._onResize);
  }

  destroy() {
    window.removeEventListener('resize', this._onResize);
    window.removeEventListener('orientationchange', this._onResize);
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
    if (message.t === 'screen_snapshot') {
      this.rows = message.rows ?? this.rows;
      this.cols = message.cols ?? this.cols;
      this.alt = !!message.alt;
      this.lines.clear();
      this.rowNodes = [];
      this.grid.replaceChildren();
      this.fit();
    }
    for (const line of message.lines || []) {
      this.lines.set(line.row, line.segs || []);
    }
    if (message.cursor) this.cursor = message.cursor;
    this.paint(message.t === 'screen_snapshot' ? null : (message.lines || []).map((l) => l.row));
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

export function controlByte(letter) {
  const code = letter.toUpperCase().charCodeAt(0);
  if (code < 64 || code > 95) return null;
  return String.fromCharCode(code - 64);
}
