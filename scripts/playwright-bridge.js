#!/usr/bin/env node
/**
 * Playwright Bridge — persistent HTTP daemon wrapping Playwright.
 * Started once per session by Docker Compose. Never per-call.
 *
 * Each endpoint receives JSON via POST body:
 *   curl -s http://localhost:3100 -d '{"action":"navigate","url":"..."}'
 */

const http = require('http');
const { chromium } = require('playwright');

const PORT = parseInt(process.env.BRIDGE_PORT || '3100', 10);
let USER_HEADLESS = process.env.BRIDGE_HEADLESS !== 'false';
let UNSAFE_LOCAL = false;  // true = running on host (headed mode allowed)
let browser, page;

async function closeBrowser() {
  if (browser) {
    try { await browser.close(); } catch (_) {}
  }
  browser = null;
  page = null;
}

async function ensurePage() {
  if (!browser) {
    const headless = UNSAFE_LOCAL ? USER_HEADLESS : true;
    if (!headless && !UNSAFE_LOCAL) {
      console.error("Warning: headless=false ignored in Docker mode. Use --skill-arg=playwright-unsafe-local=true for headed browsing on the host.");
    }
    browser = await chromium.launch({
      headless,
      args: ['--no-sandbox', '--ozone-platform=x11']
    });
  }
  if (!page || page.isClosed()) {
    const ctx = await browser.newContext();
    page = await ctx.newPage();
  }
  return page;
}

async function handleAction(action, args) {
  const p = await ensurePage();
  switch (action) {
    // --- Navigation ---
    case 'navigate':
      await p.goto(args.url, { waitUntil: 'domcontentloaded', timeout: (args.timeout || 60) * 1000 });
      return { ok: true, url: p.url(), title: await p.title() };
    case 'navigate_back':
      await p.goBack({ waitUntil: 'domcontentloaded' });
      return { ok: true, url: p.url() };

    // --- Input ---
    case 'click':
      await p.click(args.target, { button: args.button || 'left', clickCount: args.doubleClick ? 2 : 1, modifiers: args.modifiers || [] });
      return { ok: true };
    case 'type':
      await p.fill(args.target, '');
      if (args.slowly) {
        await p.type(args.target, args.text, { delay: 50 });
      } else {
        await p.fill(args.target, args.text);
      }
      if (args.submit) await p.keyboard.press('Enter');
      return { ok: true };
    case 'hover':
      await p.hover(args.target);
      return { ok: true };
    case 'press_key':
      await p.keyboard.press(args.key);
      return { ok: true };
    case 'select_option':
      await p.selectOption(args.target, args.values);
      return { ok: true };
    case 'fill_form':
      for (const field of (args.fields || [])) {
        const el = field.target || field.name;
        if (field.type === 'checkbox') {
          await p.setChecked(el, field.value === 'true' || field.value === true);
        } else if (field.type === 'combobox') {
          await p.selectOption(el, field.value);
        } else if (field.type === 'slider') {
          await p.evaluate(([sel, val]) => { const e = document.querySelector(sel); if (e) e.value = val; }, [el, field.value]);
        } else {
          await p.fill(el, field.value);
        }
      }
      return { ok: true };
    case 'handle_dialog':
      p.on('dialog', async dialog => {
        if (args.accept) await dialog.accept(args.promptText);
        else await dialog.dismiss();
      });
      return { ok: true };
    case 'drag':
      const src = await p.$(args.startTarget);
      const dst = await p.$(args.endTarget);
      if (src && dst) {
        const srcBox = await src.boundingBox();
        const dstBox = await dst.boundingBox();
        if (srcBox && dstBox) {
          await p.mouse.move(srcBox.x + srcBox.width / 2, srcBox.y + srcBox.height / 2);
          await p.mouse.down();
          await p.mouse.move(dstBox.x + dstBox.width / 2, dstBox.y + dstBox.height / 2, { steps: 10 });
          await p.mouse.up();
        }
      }
      return { ok: true };
    case 'drop':
      if (args.paths && args.paths.length > 0) {
        const input = await p.$('input[type=file], ' + (args.target || 'body'));
        if (input) await input.setInputFiles(args.paths);
      }
      return { ok: true };
    case 'file_upload':
      const chooser = await p.waitForEvent('filechooser');
      if (args.paths && args.paths.length > 0) await chooser.setFiles(args.paths);
      else await chooser.cancel();
      return { ok: true };

    // --- Observation ---
    case 'snapshot':
      const snap = await p.evaluate(({ depth: d, boxes: b }) => {
        function walk(el, depth) {
          if (depth > d) return null;
          const tag = el.tagName ? el.tagName.toLowerCase() : el.nodeName;
          const role = el.getAttribute ? (el.getAttribute('role') || tag) : tag;
          const name = el.getAttribute ? (el.getAttribute('aria-label') || el.getAttribute('title') || '') : '';
          const val = el.value !== undefined ? el.value : el.textContent ? el.textContent.substring(0, 120).trim() : '';
          const rect = b ? el.getBoundingClientRect() : null;
          const info = { role, name, value: val };
          if (rect) info.box = [rect.x, rect.y, rect.width, rect.height];
          if (el.selected) info.selected = true;
          if (el === document.activeElement) info.focused = true;
          const children = [];
          for (const child of el.children || []) {
            const sub = walk(child, depth + 1);
            if (sub) children.push(sub);
          }
          if (children.length) info.children = children;
          return info;
        }
        return walk(document.body, 0);
      }, { depth: args.depth || 10, boxes: args.boxes || false });
      return { ok: true, snapshot: formatSnapshot(snap, args) };
    case 'take_screenshot':
      const clip = args.target ? await p.$(args.target).then(el => el?.boundingBox()) : undefined;
      const screenshot = await p.screenshot({ type: args.type || 'png', fullPage: args.fullPage, clip });
      return { ok: true, screenshot: screenshot.toString('base64'), type: args.type || 'png' };
    case 'console_messages': {
      const msgs = args.all ? consoleMessages : consoleMessagesSinceLast;
      consoleMessagesSinceLast = [];
      return { ok: true, messages: msgs.map(m => ({ type: m.type, text: m.text, url: m.url, line: m.line })) };
    }
    case 'network_requests': {
      let list = networkRequests;
      if (!args.static) list = list.filter(r => !r.static);
      if (args.filter) list = list.filter(r => r.url.match(new RegExp(args.filter)));
      return { ok: true, requests: list.map(r => ({ url: r.url, method: r.method, status: r.status, type: r.type })) };
    }
    case 'wait_for':
      if (args.time) {
        await new Promise(r => setTimeout(r, args.time * 1000));
      }
      if (args.text) {
        await p.waitForFunction(text => document.body.innerText.includes(text), args.text, { timeout: (args.time || 30) * 1000 });
      }
      if (args.textGone) {
        await p.waitForFunction(text => !document.body.innerText.includes(text), args.textGone, { timeout: (args.time || 30) * 1000 });
      }
      return { ok: true };
    case 'evaluate':
      const evalResult = await p.evaluate((fn) => {
        try { return eval(fn); } catch { return new Function(fn)(); }
      }, args.function || args.code);
      return { ok: true, result: evalResult };
    case 'resize':
      await p.setViewportSize({ width: args.width, height: args.height });
      return { ok: true };

    // --- Tab management ---
    case 'tabs':
      const ctx = p.context();
      if (args.action === 'list') {
        const tabs = ctx.pages().map((pg, i) => ({ index: i, url: pg.url(), title: pg.title() }));
        return { ok: true, tabs };
      }
      if (args.action === 'new') {
        const newPage = await ctx.newPage();
        if (args.url) await newPage.goto(args.url);
        return { ok: true, index: ctx.pages().length - 1 };
      }
      if (args.action === 'close') {
        if (args.index !== undefined) await ctx.pages()[args.index]?.close();
        else await p.close();
        return { ok: true };
      }
      if (args.action === 'select') {
        page = ctx.pages()[args.index];
        return { ok: true };
      }
      return { error: 'unknown tab action: ' + args.action };

    // --- Lifecycle ---
    case 'launch':
      if (args.unsafe_local !== undefined) UNSAFE_LOCAL = args.unsafe_local !== false;
      if (args.headless !== undefined) USER_HEADLESS = args.headless !== false;
      closeBrowser();
      await ensurePage();
      return { ok: true, browser: 'chromium', mode: UNSAFE_LOCAL ? 'host' : 'docker',
               headless: UNSAFE_LOCAL ? USER_HEADLESS : true };
    case 'close':
      await closeBrowser();
      return { ok: true };
    case 'ping':
      return { ok: true, uptime: process.uptime() };
    case 'set_headless':
      USER_HEADLESS = args.headless !== false;
      return { ok: true, headless: USER_HEADLESS, effectiveHeadless: UNSAFE_LOCAL ? USER_HEADLESS : true };
    case 'set_unsafe_local':
      UNSAFE_LOCAL = args.value !== false;
      return { ok: true, mode: UNSAFE_LOCAL ? 'host' : 'docker' };

    default:
      return { error: 'unknown action: ' + action };
  }
}

// --- Network monitoring ---
let networkRequests = [];
let consoleMessages = [];
let consoleMessagesSinceLast = [];

function setupMonitoring(p) {
  p.on('console', msg => {
    const entry = { type: msg.type(), text: msg.text(), url: msg.location().url, line: msg.location().lineNumber };
    consoleMessages.push(entry);
    consoleMessagesSinceLast.push(entry);
  });
  p.on('request', req => {
    networkRequests.push({ url: req.url(), method: req.method(), type: req.resourceType(), static: isStaticResource(req) });
  });
  p.on('requestfinished', req => {
    const resp = req.response();
    if (resp) {
      const entry = networkRequests.find(r => r.url === req.url());
      if (entry) entry.status = resp.status();
    }
  });
}

function isStaticResource(req) {
  const t = req.resourceType();
  return ['image', 'stylesheet', 'font', 'media'].includes(t);
}

function formatSnapshot(node, args) {
  if (!node) return '';
  const depth = args.depth || 10;
  const lines = [];
  function walk(n, d) {
    if (d > depth || !n) return;
    const indent = '  '.repeat(d);
    let line = indent + (n.role || 'unknown');
    if (n.name) line += ' [' + n.name + ']';
    if (n.value !== undefined && n.value !== '') line += ': ' + String(n.value).substring(0, 200);
    if (n.box) line += ' [box=' + n.box.join(',') + ']';
    if (n.selected) line += ' [selected]';
    if (n.focused) line += ' [active]';
    line += ' [ref=e' + d + ']';
    lines.push(line);
    if (n.children) for (const c of n.children) walk(c, d + 1);
  }
  walk(node, 0);
  return lines.join('\n');
}

// --- Server ---
const server = http.createServer(async (req, res) => {
  res.setHeader('Content-Type', 'application/json');
  if (req.method !== 'POST') {
    res.writeHead(405);
    return res.end(JSON.stringify({ error: 'POST only' }));
  }
  let body = '';
  req.on('data', chunk => body += chunk);
  req.on('end', async () => {
    try {
      const { action, args: rawArgs, ...rest } = JSON.parse(body);
      const result = await handleAction(action || rest.action, rawArgs || rest);
      res.writeHead(result.error ? 400 : 200);
      res.end(JSON.stringify(result));
    } catch (err) {
      res.writeHead(500);
      res.end(JSON.stringify({ error: err.message }));
    }
  });
});

// Auto-launch browser
ensurePage().then(() => {
  const ctx = page.context();
  setupMonitoring(page);
  ctx.on('page', pg => {
    if (!page || page.isClosed()) page = pg;
    setupMonitoring(pg);
  });
}).catch(err => {
  console.error('Failed to launch browser:', err.message);
});

server.listen(PORT, () => {
  console.error('Playwright bridge listening on port ' + PORT);
});
