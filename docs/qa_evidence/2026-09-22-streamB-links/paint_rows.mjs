// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What `linkify()` makes of a row, for any version of app/screen.js. #NK73.
//
// The function is pure — runs in, pieces out — so it can be lifted out of the file and run
// without a browser: everything from `const ATTR` down to the palette is evaluated here and
// `linkify` is taken from it. What that misses is the DOM half (colour, CONCEAL, the anchors),
// which is why tests/test_web_screen.py drives the real ScreenView in a real Chrome. This is the
// side-by-side table: the same rows through the old painter and the new one.
//
//   git show c4a657ed^:app/screen.js > /tmp/screen-before.js
//   node paint_rows.mjs /tmp/screen-before.js app/screen.js
import { readFileSync } from 'fs';

const ROWS = [
  ['see https://a.example.com and https://b.example.com now', 80],
  ['https://a.example.com https://b.example.com https://c.example.com', 80],
  ['banner: WWW.EXAMPLE.COM is the site', 80],
  ['run curl "https://api.example.com/v1" twice', 80],
  ['see <https://example.com/a> in the RFC', 80],
  ['path /var/www.old/index.html is served', 80],
  ['one https://example.com/x_(1), ok', 80],
  // A row exactly as wide as the screen: the URL may be half of one.
  ['open https://example.com/a/very/long/signed/path', 47],
];
// A URL split across two runs of different colour, which is what an ANSI-coloured line is.
const SPLIT = [['see https://a.exa', 1, 0, 0], ['mple.com and https://b.example.com now', 2, 0, 0]];

function load(path) {
  const src = readFileSync(path, 'utf8');
  const body = src.slice(src.indexOf('const ATTR'), src.indexOf("// xterm's first 16"));
  return new Function(`${body}\nreturn linkify;`)();
}

for (const path of process.argv.slice(2)) {
  const linkify = load(path);
  console.log(`=== ${path}`);
  for (const [text, cols] of ROWS) {
    const pieces = linkify([[text, 0, 0, 0]], cols);
    const out = pieces.map((piece) => piece.text).join('');
    console.log(`in : ${JSON.stringify(text)}`);
    console.log(`out: ${JSON.stringify(out)}${out === text ? '' : '   <-- NOT THE ROW'}`);
    for (const piece of pieces) {
      if (piece.url) console.log(`     link ${JSON.stringify(piece.text)} -> ${piece.url}`);
    }
  }
  const pieces = linkify(SPLIT, 80);
  const whole = SPLIT.map(([text]) => text).join('');
  const out = pieces.map((piece) => piece.text).join('');
  console.log(`in : ${JSON.stringify(whole)}   (two runs)`);
  console.log(`out: ${JSON.stringify(out)}${out === whole ? '' : '   <-- NOT THE ROW'}`);
  for (const piece of pieces) {
    if (piece.url) console.log(`     link ${JSON.stringify(piece.text)} -> ${piece.url}`);
  }
  console.log('');
}
