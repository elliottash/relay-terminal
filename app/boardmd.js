// SPDX-License-Identifier: AGPL-3.0-or-later
// A card's Markdown, drawn by building DOM nodes (card #SWPH).
//
// A card body and a thread entry are text a model, a collaborator or a merged branch wrote, and
// they reach this phone from the desktop as plain strings. Nothing here ever parses HTML: there
// is no innerHTML, no DOMParser, no template. The renderer reads the text line by line, makes the
// handful of elements Markdown needs — headings, paragraphs, lists, task boxes, code, quotes,
// rules, tables — with createElement, and puts every character in through a text node. So
// `<script>` in a card is the eight characters `<script>`, an `<img onerror>` is a sentence, and
// the worst a hostile card can do is be ugly.
//
// Three things are deliberately not Markdown's defaults:
//
// * **HTML comments are never shown.** `<!-- t:a3 -->` and `<!-- relay:entry … -->` are the
//   board's own bookkeeping (docs/SWITCHBOARD-FORMAT.md 2.5 and 3); GitHub hides them and so does
//   this. Inside a code fence they are code and stay.
// * **A link is text until it is confirmed.** It is drawn as its words followed by the address,
//   and tapping it calls `onLink(url, text)` — the view shows a sheet with the whole address
//   before anything opens. Only http, https and mailto are ever offered; an image is never
//   fetched, it is the words "image" and its address.
// * **A single newline is a line break.** The owner's words are stored verbatim and are typed the
//   way people type on a phone, so folding their lines together would rewrite them.

const FENCE = /^(\s*)(`{3,}|~{3,})\s*([\w+-]*)\s*$/;
const HEADING = /^(#{1,6})\s+(.*?)\s*#*\s*$/;
const RULE = /^\s{0,3}([-*_])(\s*\1){2,}\s*$/;
const ITEM = /^(\s*)([-*+]|\d{1,9}[.)])\s+(.*)$/;
const TASK = /^\[( |x|X)\]\s+(.*)$/;
const QUOTE = /^\s{0,3}>\s?(.*)$/;
const TABLE_RULE = /^\s*\|?\s*:?-{1,}:?\s*(\|\s*:?-{1,}:?\s*)*\|?\s*$/;
const CARD_REF = /^#[0-9A-Z]{4}(?:\.[0-9a-z]{2})?\b/;
const URL_START = /^https?:\/\/[^\s<>()]+/i;

export const OPENABLE = /^(https?:|mailto:)/i;

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

// The text with its HTML comments taken out, across lines, leaving fenced code alone. Returns the
// lines, so the block pass below never sees a comment at all.
export function visibleLines(source) {
  const out = [];
  let fence = null;
  let inComment = false;
  for (const raw of String(source ?? '').replace(/\r\n?/g, '\n').split('\n')) {
    if (fence) {
      out.push(raw);
      const close = raw.match(FENCE);
      if (close && close[2][0] === fence[0] && close[2].length >= fence.length && !close[3]) fence = null;
      continue;
    }
    let line = raw;
    let kept = '';
    let touched = false;
    while (line) {
      if (inComment) {
        const end = line.indexOf('-->');
        touched = true;
        if (end < 0) { line = ''; break; }
        line = line.slice(end + 3);
        inComment = false;
        continue;
      }
      const start = line.indexOf('<!--');
      if (start < 0) { kept += line; break; }
      kept += line.slice(0, start);
      line = line.slice(start + 4);
      inComment = true;
      touched = true;
    }
    // A line that held nothing but a comment disappears; it must not become a blank line that
    // splits the paragraph it sat in the middle of into two.
    if (touched && !kept.trim()) continue;
    kept = touched ? kept.replace(/\s+$/, '') : kept;
    const open = kept.match(FENCE);
    if (open) fence = open[2];
    out.push(kept);
  }
  return out;
}

// ---- inline ------------------------------------------------------------------------------------

function linkNode(text, url, options) {
  const address = String(url).trim();
  const node = el('button', 'rb-md-link');
  node.type = 'button';
  const words = String(text || '').trim();
  if (words && words !== address) {
    node.append(el('span', 'rb-md-link-text', words), el('span', 'rb-md-link-url', ` (${address})`));
  } else {
    node.append(el('span', 'rb-md-link-text', address));
  }
  node.dataset.url = address;
  node.addEventListener('click', (event) => {
    event.preventDefault();
    event.stopPropagation();
    if (typeof options.onLink === 'function') options.onLink(address, words || address);
  });
  return node;
}

function cardRefNode(ref, options) {
  const id = ref.slice(1, 5);
  if (typeof options.onCard !== 'function' || (options.knowsCard && !options.knowsCard(id))) {
    return document.createTextNode(ref);
  }
  const node = el('button', 'rb-md-ref', ref);
  node.type = 'button';
  node.dataset.card = id;
  node.addEventListener('click', (event) => {
    event.preventDefault();
    event.stopPropagation();
    options.onCard(id);
  });
  return node;
}

// Where the closing run of `marker` is, for emphasis opened at `from`. -1 when it never closes,
// in which case the marker is just characters.
function closing(text, marker, from) {
  let at = text.indexOf(marker, from);
  while (at >= 0) {
    if (at > from && !/\s/.test(text[at - 1])) return at;
    at = text.indexOf(marker, at + 1);
  }
  return -1;
}

// Inline Markdown into `parent`, as nodes. `depth` stops a pathological nest from recursing away.
export function renderInline(parent, source, options = {}, depth = 0) {
  const text = String(source ?? '');
  let plain = '';
  const flush = () => {
    if (plain) parent.append(document.createTextNode(plain));
    plain = '';
  };
  let at = 0;
  while (at < text.length) {
    const char = text[at];
    const rest = text.slice(at);
    if (char === '\\' && at + 1 < text.length && /[\\`*_{}[\]()#+\-.!~|<>]/.test(text[at + 1])) {
      plain += text[at + 1];
      at += 2;
      continue;
    }
    if (char === '`') {
      const run = rest.match(/^`+/)[0];
      const end = text.indexOf(run, at + run.length);
      if (end > 0) {
        flush();
        parent.append(el('code', 'rb-md-code', text.slice(at + run.length, end).replace(/^ (.*) $/, '$1')));
        at = end + run.length;
        continue;
      }
    }
    if (depth < 6 && (rest.startsWith('**') || rest.startsWith('__') || rest.startsWith('~~'))) {
      const marker = rest.slice(0, 2);
      const end = closing(text, marker, at + 2);
      if (end > at + 2 && !/\s/.test(text[at + 2])) {
        flush();
        const node = el(marker === '~~' ? 'del' : 'strong');
        renderInline(node, text.slice(at + 2, end), options, depth + 1);
        parent.append(node);
        at = end + 2;
        continue;
      }
    }
    if (depth < 6 && (char === '*' || char === '_') && at + 1 < text.length && !/\s/.test(text[at + 1])
        // snake_case is a name, not emphasis: an underscore opens only at a word's edge.
        && (char === '*' || at === 0 || !/\w/.test(text[at - 1]))) {
      let end = closing(text, char, at + 1);
      while (end >= 0 && char === '_' && end + 1 < text.length && /\w/.test(text[end + 1])) {
        end = closing(text, char, end + 1);
      }
      if (end > at + 1) {
        flush();
        const node = el('em');
        renderInline(node, text.slice(at + 1, end), options, depth + 1);
        parent.append(node);
        at = end + 1;
        continue;
      }
    }
    if (char === '!' && text[at + 1] === '[') {
      const match = rest.match(/^!\[([^\]]*)\]\(\s*<?([^\s)>]+)>?(?:\s+"[^"]*")?\s*\)/);
      if (match) {
        // Never fetched: a picture's address is somebody else's server learning this phone is
        // reading the card. The words say what it is.
        flush();
        parent.append(el('span', 'rb-md-image', `[image: ${match[1] || 'untitled'}] `));
        parent.append(OPENABLE.test(match[2]) ? linkNode('', match[2], options)
          : document.createTextNode(match[2]));
        at += match[0].length;
        continue;
      }
    }
    if (char === '[') {
      const match = rest.match(/^\[([^\]]+)\]\(\s*<?([^\s)>]+)>?(?:\s+"[^"]*")?\s*\)/);
      if (match) {
        flush();
        if (OPENABLE.test(match[2])) parent.append(linkNode(match[1], match[2], options));
        else {
          // A path in the repository, an anchor, a `javascript:` — nothing a phone should open.
          // Its words, then where it pointed, as text.
          const node = el('span', 'rb-md-path');
          renderInline(node, match[1], options, depth + 1);
          node.append(document.createTextNode(` (${match[2]})`));
          parent.append(node);
        }
        at += match[0].length;
        continue;
      }
    }
    if (char === '<') {
      const match = rest.match(/^<((?:https?:\/\/|mailto:)[^\s<>]+)>/i);
      if (match) {
        flush();
        parent.append(linkNode('', match[1], options));
        at += match[0].length;
        continue;
      }
    }
    if ((char === 'h' || char === 'H') && (at === 0 || !/\w/.test(text[at - 1]))) {
      const match = rest.match(URL_START);
      if (match) {
        const address = match[0].replace(/[.,;:!?'"]+$/, '');
        flush();
        parent.append(linkNode('', address, options));
        at += address.length;
        continue;
      }
    }
    if (char === '#' && (at === 0 || !/[\w#&/]/.test(text[at - 1]))) {
      const match = rest.match(CARD_REF);
      if (match) {
        flush();
        parent.append(cardRefNode(match[0], options));
        at += match[0].length;
        continue;
      }
    }
    plain += char;
    at += 1;
  }
  flush();
  return parent;
}

// ---- blocks ------------------------------------------------------------------------------------

function cells(line) {
  let body = line.trim();
  if (body.startsWith('|')) body = body.slice(1);
  if (body.endsWith('|') && !body.endsWith('\\|')) body = body.slice(0, -1);
  return body.split(/(?<!\\)\|/).map((cell) => cell.trim().replace(/\\\|/g, '|'));
}

function renderTable(rows, options) {
  const wrap = el('div', 'rb-md-table-wrap');
  const table = el('table', 'rb-md-table');
  const head = el('thead');
  const headRow = el('tr');
  for (const cell of cells(rows[0])) renderInline(headRow.appendChild(el('th')), cell, options);
  head.append(headRow);
  const body = el('tbody');
  for (const line of rows.slice(2)) {
    const row = el('tr');
    for (const cell of cells(line)) renderInline(row.appendChild(el('td')), cell, options);
    body.append(row);
  }
  table.append(head, body);
  wrap.append(table);
  return wrap;
}

const indentOf = (line) => line.match(/^\s*/)[0].replace(/\t/g, '    ').length;

function renderList(lines, start, options) {
  // Every consecutive item at this indent or deeper, with its continuation lines; one blank line
  // between items is allowed, two ends the list.
  const first = lines[start].match(ITEM);
  const base = indentOf(lines[start]);
  const ordered = /\d/.test(first[2][0]);
  const list = el(ordered ? 'ol' : 'ul', 'rb-md-list');
  if (ordered) {
    const from = parseInt(first[2], 10);
    if (from !== 1 && Number.isFinite(from)) list.start = from;
  }
  let at = start;
  while (at < lines.length) {
    const match = lines[at].match(ITEM);
    if (!match || indentOf(lines[at]) !== base || /\d/.test(match[2][0]) !== ordered) break;
    const item = el('li');
    let words = match[3];
    const task = words.match(TASK);
    if (task) {
      item.classList.add('rb-md-task');
      const box = document.createElement('input');
      box.type = 'checkbox';
      box.disabled = true;               // the phone reads a checklist; the desktop's agent ticks it
      box.checked = task[1] !== ' ';
      item.append(box);
      [, , words] = task;
    }
    const inner = [words];
    at += 1;
    while (at < lines.length) {
      const line = lines[at];
      if (!line.trim()) {
        const next = lines[at + 1];
        if (next !== undefined && next.trim() && indentOf(next) > base) { inner.push(''); at += 1; continue; }
        break;
      }
      if (indentOf(line) <= base) break;
      inner.push(line.slice(Math.min(indentOf(line), base + 2)));
      at += 1;
    }
    const content = el('div', 'rb-md-item');
    renderBlocks(content, inner, options, true);
    item.append(content);
    list.append(item);
    // A single blank line between two items of the same list does not end it.
    if (at < lines.length && !lines[at].trim() && lines[at + 1] !== undefined) {
      const next = lines[at + 1].match(ITEM);
      if (next && indentOf(lines[at + 1]) === base && /\d/.test(next[2][0]) === ordered) at += 1;
    }
  }
  return { node: list, next: at };
}

function renderBlocks(parent, lines, options, tight = false) {
  let at = 0;
  while (at < lines.length) {
    const line = lines[at];
    if (!line.trim()) { at += 1; continue; }

    const fence = line.match(FENCE);
    if (fence) {
      const code = [];
      at += 1;
      while (at < lines.length) {
        const close = lines[at].match(FENCE);
        if (close && close[2][0] === fence[2][0] && close[2].length >= fence[2].length && !close[3]) break;
        code.push(lines[at].slice(Math.min(indentOf(lines[at]), fence[1].length)));
        at += 1;
      }
      at += 1;
      const pre = el('pre', 'rb-md-pre');
      pre.append(el('code', '', code.join('\n')));
      if (fence[3]) pre.dataset.lang = fence[3];
      parent.append(pre);
      continue;
    }

    const heading = line.match(HEADING);
    if (heading) {
      const level = Math.min(6, heading[1].length);
      renderInline(parent.appendChild(el(`h${Math.max(3, level)}`, `rb-md-h rb-md-h${level}`)), heading[2], options);
      at += 1;
      continue;
    }

    if (RULE.test(line) && !ITEM.test(line)) {
      parent.append(el('hr', 'rb-md-rule'));
      at += 1;
      continue;
    }

    if (QUOTE.test(line)) {
      const inner = [];
      while (at < lines.length && QUOTE.test(lines[at])) {
        inner.push(lines[at].match(QUOTE)[1]);
        at += 1;
      }
      renderBlocks(parent.appendChild(el('blockquote', 'rb-md-quote')), inner, options);
      continue;
    }

    if (line.includes('|') && at + 1 < lines.length && TABLE_RULE.test(lines[at + 1])
        && lines[at + 1].includes('-') && lines[at + 1].includes('|')) {
      const rows = [];
      while (at < lines.length && lines[at].trim() && lines[at].includes('|')) {
        rows.push(lines[at]);
        at += 1;
      }
      parent.append(renderTable(rows, options));
      continue;
    }

    if (ITEM.test(line)) {
      const { node, next } = renderList(lines, at, options);
      parent.append(node);
      at = next;
      continue;
    }

    const words = [];
    while (at < lines.length && lines[at].trim() && !FENCE.test(lines[at]) && !HEADING.test(lines[at])
           && !QUOTE.test(lines[at]) && !(ITEM.test(lines[at]) && words.length && indentOf(lines[at]) < 4)
           && !(RULE.test(lines[at]))) {
      words.push(lines[at].trim());
      at += 1;
    }
    if (!words.length) { words.push(line.trim()); at += 1; }
    const paragraph = el(tight ? 'div' : 'p', 'rb-md-p');
    words.forEach((piece, index) => {
      if (index) paragraph.append(document.createElement('br'));
      renderInline(paragraph, piece.replace(/\s{2,}$/, ''), options);
    });
    parent.append(paragraph);
  }
  return parent;
}

// The whole text as one block of nodes.
export function renderMarkdown(source, options = {}) {
  const root = el('div', 'rb-md');
  renderBlocks(root, visibleLines(source), options);
  return root;
}

// A card body, by section (docs/SWITCHBOARD-FORMAT.md 2.7): the `# title` line is the page's own
// heading and is dropped, and each `## Heading` starts a section whose name is handed back beside
// its lines, so the view can draw the tasks from the card's parsed checklist instead of from text.
export function bodySections(source) {
  const sections = [];
  let current = { heading: '', lines: [] };
  let fence = null;
  let seenTitle = false;
  for (const line of visibleLines(source)) {
    const mark = line.match(FENCE);
    if (fence) {
      if (mark && mark[2][0] === fence[0] && mark[2].length >= fence.length && !mark[3]) fence = null;
      current.lines.push(line);
      continue;
    }
    if (mark) { fence = mark[2]; current.lines.push(line); continue; }
    const heading = line.match(HEADING);
    if (heading && heading[1].length === 1 && !seenTitle && !sections.length
        && !current.lines.some((item) => item.trim())) {
      seenTitle = true;
      continue;
    }
    if (heading && heading[1].length === 2) {
      if (current.heading || current.lines.some((item) => item.trim())) sections.push(current);
      current = { heading: heading[2], lines: [] };
      continue;
    }
    current.lines.push(line);
  }
  if (current.heading || current.lines.some((item) => item.trim())) sections.push(current);
  return sections;
}

export function renderLines(lines, options = {}) {
  const root = el('div', 'rb-md');
  renderBlocks(root, lines, options);
  return root;
}

// The numbered choices a question ends on ("1. … 2. …"): the top-level ordered items, in order,
// as `{number, text}`. A reply box offers each as a tap that starts the answer with its number.
export function numberedOptions(source) {
  const options = [];
  let fence = null;
  for (const line of visibleLines(source)) {
    const mark = line.match(FENCE);
    if (fence) { if (mark && !mark[3]) fence = null; continue; }
    if (mark) { fence = mark[2]; continue; }
    const match = line.match(/^(\d{1,2})[.)]\s+(.*)$/);
    if (match) options.push({ number: parseInt(match[1], 10), text: match[2].replace(/[*_`]/g, '').trim() });
  }
  // Only a run that reads 1, 2, 3…: a changelog line starting "2026. " is not a choice.
  const run = [];
  for (const option of options) {
    if (option.number === run.length + 1) run.push(option);
  }
  return run.length >= 2 ? run : [];
}
