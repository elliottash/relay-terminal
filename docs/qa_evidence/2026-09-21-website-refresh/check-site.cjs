// NODE_PATH=<directory containing playwright> node <this file>; Chrome installed locally.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const http = require('node:http');
const root = path.resolve(__dirname, '../../../site');
const server = http.createServer((req, res) => {
  const file = path.join(root, new URL(req.url, 'http://localhost').pathname === '/' ? 'index.html' : new URL(req.url, 'http://localhost').pathname);
  const mime = {'.html':'text/html','.css':'text/css','.js':'text/javascript','.svg':'image/svg+xml','.png':'image/png','.woff2':'font/woff2'};
  fs.readFile(file, (err, data) => { res.writeHead(err ? 404 : 200, {'Content-Type':mime[path.extname(file)] || 'application/octet-stream'}); res.end(err ? 'Not found' : data); });
});
(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  const browser = await chromium.launch({executablePath:'/usr/bin/google-chrome', args:['--no-sandbox']});
  try {
    for (const width of [360, 720, 1280]) for (const colorScheme of ['light','dark']) {
      const context = await browser.newContext({viewport:{width,height:900}, colorScheme});
      const page = await context.newPage(); const errors=[];
      page.on('pageerror', e => errors.push(e.message));
      page.on('response', r => { if (r.status() >= 400) errors.push(`${r.status()} ${r.url()}`); });
      for (const file of ['index.html','free.html']) {
        await page.goto(`${base}/${file}`); await page.evaluate(() => document.fonts.ready);
        assert.equal(await page.locator('h1').count(), 1);
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${file} overflow ${width}`);
        const broken = await page.evaluate(() => [...document.querySelectorAll('a[href^="#"]')].filter(a => a.hash && !document.getElementById(a.hash.slice(1))).map(a => a.hash));
        assert.deepEqual(broken, []);
        assert.equal(await page.locator('.panel-switch button[tabindex="0"]').count(),1);
        await page.locator('.panel-switch button[tabindex="0"]').focus(); await page.keyboard.press('ArrowRight');
        assert.equal(await page.locator('.panel-switch button:focus').getAttribute('aria-checked'),'true');
        if (file === 'index.html') {
          await page.locator('.jack-shell').focus(); await page.keyboard.press('ArrowRight');
          assert.equal(await page.locator('.board').getAttribute('data-line'),'agent');
          assert.equal(await page.locator('.input-text').innerText(),'why does the build fail?');
          await page.locator('.jack-agent').press('ArrowLeft');
          assert.equal(await page.locator('.board').getAttribute('data-line'),'shell');
          const links = await page.locator('[src],link[href],a[href]').evaluateAll(els => els.map(e=>e.getAttribute('src')||e.getAttribute('href')).filter(x=>x && !/^(https?:|#|mailto:)/.test(x)));
          for (const link of links) assert.equal((await page.request.get(`${base}/${link.split('#')[0]}`)).status(),200,link);
          await page.locator('img').evaluateAll(imgs => Promise.all(imgs.map(img => { img.loading = 'eager'; return img.decode(); })));
          if (colorScheme === 'light' && [360,1280].includes(width)) {
            await page.locator('[data-panel="beige"]').click();
            await page.screenshot({path:path.join(__dirname,width === 360 ? 'mobile.png':'desktop.png'),fullPage:true});
          }
        }
      }
      assert.deepEqual(errors, []);
      await page.reload(); assert(await page.evaluate(() => document.documentElement.dataset.theme === localStorage.getItem('relay-panel')));
      await context.close(); console.log(`PASS ${width}px ${colorScheme}: both pages, links, radio keyboard controls, persistence, no errors`);
    }
    const context = await browser.newContext({javaScriptEnabled:false,viewport:{width:360,height:900}});
    const page=await context.newPage();
    for (const file of ['index.html','free.html']) {
      await page.goto(`${base}/${file}`);
      assert.equal(await page.locator('.panel-switch').isVisible(),false);
      assert(await page.locator('main').isVisible());
    }
    await context.close(); console.log('PASS no JavaScript: both pages readable, theme controls hidden');
  } finally { await browser.close(); server.close(); }
})().catch(e => { console.error(e); server.close(); process.exitCode=1; });
