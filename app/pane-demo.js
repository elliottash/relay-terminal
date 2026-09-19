// SPDX-License-Identifier: GPL-3.0-or-later
// The page around the pane view (app/pane-demo.html): it loads a fixture, mounts app/pane.js on
// it, and shows what the view would have sent to the desktop. Nothing here is part of the client;
// the app mounts the same view from app/app.js and sends through the real transport.
//
//   ?fixture=busy_queue   which state to draw (the files in tests/fixtures/pane_state/)
//   ?input=touch|mouse    force the device the view draws for, instead of asking the browser
//   ?theme=relay-light    the generated theme to use (app/pane-theme.css)
//   ?bare=1               hide the page's own chrome, for screenshots
//
// Every message the view emits is appended to the log and counted on the button, which is what
// tests/test_pane_view.py asserts against: the view must send exactly the protocol's messages.

import { mountPane } from './pane.js';
// Sized like the app: to the visible viewport, with the short-viewport caps under 520 px, so a
// window the height of an iPad's strip above its keyboard shows what the iPad shows.
import { trackViewport } from './viewport.js';

trackViewport();

// The fixtures, by name. A static page cannot list a directory, and these are the seven states
// worth looking at: idle, a busy queue, long reasoning, a paused queue, a view-only device (no
// actions, no model choices), a long session list and a Relay Free allowance running low.
const FIXTURES = ['idle', 'busy_queue', 'thinking_long_tail', 'paused_queue', 'view_only', 'sessions_50', 'allowance'];
const FIXTURE_DIR = '../tests/fixtures/pane_state/';

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);

const sent = [];
let view = null;

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function note(text) {
  const node = $('demo-note');
  node.textContent = text || '';
  node.hidden = !text;
}

// A stand-in for the terminal the app draws with app/screen.js, so the view's proportions and its
// theme are the ones a phone sees rather than an empty box.
function terminalStandIn() {
  const pre = el('pre', 'demo-terminal');
  pre.append(el('span', 'demo-prompt', 'elliott@spark ~/relay-terminal$ '), el('span', '', 'ctest\n'));
  pre.append(el('span', 'demo-dim', '100% tests passed, 0 tests failed out of 44\n\n'));
  pre.append(el('span', 'demo-prompt', 'elliott@spark ~/relay-terminal$ '), el('span', 'demo-agent', '✦ please plan this out\n'));
  pre.append(el('span', 'demo-dim', '✦ thought for 20 s  (Ctrl+click)\n'));
  return pre;
}

function renderLog() {
  $('demo-log-toggle').textContent = `Sent: ${sent.length}`;
  $('demo-log').textContent = sent.map((message) => JSON.stringify(message)).join('\n');
}

function mount() {
  if (view) view.destroy();
  sent.length = 0;
  renderLog();
  view = mountPane($('demo-stage'), {
    send: (message) => { sent.push(message); renderLog(); },
    input: params.get('input') || $('demo-input').value || undefined,
    theme: $('demo-theme').value || undefined,
  });
  view.terminalSlot.append(terminalStandIn());
  // What a test reads instead of guessing when the page is ready.
  document.body.dataset.demoReady = '1';
  return view;
}

async function load(name) {
  note('');
  let state;
  try {
    const response = await fetch(`${FIXTURE_DIR}${name}.json`, { cache: 'no-store' });
    if (!response.ok) throw new Error(`${response.status}`);
    state = await response.json();
  } catch (error) {
    // Served from app/ alone the fixtures are a directory up and out of reach; say so rather than
    // drawing an empty pane, and offer the file picker instead.
    note(`Could not load ${name}.json (${error.message}). Serve the repository root, or use “Load JSON…”.`);
    return;
  }
  draw(state);
}

function draw(state) {
  document.body.dataset.demoFixture = String(state.pane || '');
  mount().update(state);
  document.body.dataset.demoSeq = String(state.seq ?? '');
  document.body.dataset.demoState = JSON.stringify(state);
}

// The handle tests/test_pane_view.py drives the page through: the messages the view has sent, and
// a way to push another state at the mounted view without reloading (a later one, or an older one
// it must ignore). A demo page only; the client mounts the same view from app/app.js.
window.paneDemo = {
  sent,
  update: (state) => (view ? view.update(state) : false),
  editText: (message) => { if (view) view.onEditText(message); },
  refuse: (message) => (view ? view.onRefused(message) : false),
  toast: () => { const t = document.querySelector('.rp-toast'); return t && !t.hidden ? t.textContent : ''; },
  state: () => JSON.parse(document.body.dataset.demoState || 'null'),
};

function start() {
  const select = $('demo-fixture');
  for (const name of FIXTURES) select.append(el('option', '', name));
  const wanted = params.get('fixture') || FIXTURES[0];
  select.value = FIXTURES.includes(wanted) ? wanted : FIXTURES[0];
  if (params.get('input')) $('demo-input').value = params.get('input');
  if (params.get('theme')) $('demo-theme').value = params.get('theme');
  if (params.get('bare') === '1') document.body.classList.add('demo-bare');

  select.addEventListener('change', () => load(select.value));
  $('demo-input').addEventListener('change', () => load(select.value));
  $('demo-theme').addEventListener('change', () => {
    if (view) view.setTheme($('demo-theme').value);
  });
  $('demo-log-toggle').addEventListener('click', () => { $('demo-log').hidden = !$('demo-log').hidden; });
  $('demo-file').addEventListener('change', async (event) => {
    const file = event.target.files && event.target.files[0];
    if (!file) return;
    try {
      draw(JSON.parse(await file.text()));
    } catch (error) {
      note(`That file is not a pane_state: ${error.message}`);
    }
  });
  load(select.value);
}

start();
