#!/usr/bin/env node

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');
const { chromium } = require('playwright');
const { renderSlide } = require('./render-demo-slide.cjs');

function slugify(s) { return s.toLowerCase().replace(/[^a-z0-9]+/g, '-').slice(0, 40).replace(/^-|-$/g, ''); }

function getDuration(filePath) {
  try {
    const dur = execSync(
      `ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "${filePath}"`,
      { encoding: 'utf-8', timeout: 10000 }
    ).trim();
    return parseFloat(dur) || 0;
  } catch { return 0; }
}

async function recordSlide(artifactsDir, segIdx, scene, durationSecs) {
  const slug = slugify(scene.title || scene.heading || 'slide');
  const out = path.join(artifactsDir, `clip-${String(segIdx).padStart(2, '0')}-${slug}.webm`);
  if (fs.existsSync(out)) {
    const stats = fs.statSync(out);
    const dur = getDuration(out);
    console.log(`  ${path.basename(out)}: ${dur.toFixed(1)}s, ${(stats.size / 1024 / 1024).toFixed(1)} MB (cached)`);
    return out;
  }

  const html = renderSlide(scene);
  const htmlPath = path.join(artifactsDir, `.tmp-slide-${segIdx}.html`);
  fs.writeFileSync(htmlPath, html);

  let browser = null;
  try {
    browser = await chromium.launch({ headless: true, args: ['--no-sandbox', '--disable-setuid-sandbox'] });
    const context = await browser.newContext({
      viewport: { width: 1920, height: 1080 },
      deviceScaleFactor: 1,
      recordVideo: { dir: artifactsDir, size: { width: 1920, height: 1080 } },
    });
    const recordingStart = Date.now();
    const done = new Promise(r => setTimeout(r, durationSecs * 1000 + 300));
    const page = await context.newPage();

    await page.goto('file://' + path.resolve(htmlPath), { waitUntil: 'load', timeout: 15000 });

    await done;
    await context.close();
    await new Promise(r => setTimeout(r, 1000));

    for (const f of fs.readdirSync(artifactsDir)) {
      if (f.startsWith('page@') && f.endsWith('.webm')) {
        const src = path.join(artifactsDir, f);
        if (fs.statSync(src).size > 1024) {
          fs.renameSync(src, out);
        }
        break;
      }
    }
  } catch (err) {
    console.error(`  RECORD FAILED: slide ${segIdx}: ${err.message.slice(0, 150)}`);
  } finally {
    if (browser) await browser.close();
    if (fs.existsSync(htmlPath)) fs.unlinkSync(htmlPath);
  }

  if (fs.existsSync(out)) {
    const stats = fs.statSync(out);
    const dur = getDuration(out);
    console.log(`  ${path.basename(out)}: ${dur.toFixed(1)}s, ${(stats.size / 1024 / 1024).toFixed(1)} MB`);
    return out;
  }
  return null;
}

async function recordDemo(artifactsDir, segIdx, feature, durationSecs, anchorsData) {
  const slug = slugify(feature.heading);
  const out = path.join(artifactsDir, `clip-${String(segIdx).padStart(2, '0')}-${slug}.webm`);
  if (fs.existsSync(out)) {
    const stats = fs.statSync(out);
    const dur = getDuration(out);
    console.log(`  ${path.basename(out)}: ${dur.toFixed(1)}s, ${(stats.size / 1024 / 1024).toFixed(1)} MB (cached)`);
    return out;
  }

  let browser = null;
  try {
    browser = await chromium.launch({ headless: true, args: ['--no-sandbox', '--disable-setuid-sandbox'] });
    const context = await browser.newContext({
      viewport: { width: 1920, height: 1080 },
      deviceScaleFactor: 1,
      recordVideo: { dir: artifactsDir, size: { width: 1920, height: 1080 } },
    });
    const recordingStart = Date.now();
    const done = new Promise(r => setTimeout(r, durationSecs * 1000 + 300));
    const page = await context.newPage();

    // Navigate (best effort — catch errors silently)
    await page.goto(feature.url, { waitUntil: 'load', timeout: 30000 }).catch(() => {});

    // Scroll playback timed precisely from recordingStart
    try {
      const pageHeight = anchorsData.page_height || await page.evaluate(() => document.body.scrollHeight || document.documentElement.scrollHeight);
      const vpH = 1080;
      const maxY = Math.max(0, pageHeight - vpH);

      const anchors = (anchorsData.anchors || []).filter(a => a.time_secs !== undefined);

      for (const anchor of anchors) {
        const targetMs = anchor.time_secs * 1000;
        const elapsed = Date.now() - recordingStart;
        const waitMs = Math.max(0, targetMs - elapsed);
        if (waitMs > 0) await new Promise(r => setTimeout(r, waitMs));

        const y = Math.round(maxY * (anchor.scroll_pct / 100));
        await page.evaluate((sy) => window.scrollTo(0, sy), y);
      }
    } catch (err) {
      console.error(`  Scroll error (continuing recording): ${err.message.slice(0, 150)}`);
    }

    await done;
    await context.close();
    await new Promise(r => setTimeout(r, 1000));

    for (const f of fs.readdirSync(artifactsDir)) {
      if (f.startsWith('page@') && f.endsWith('.webm')) {
        const src = path.join(artifactsDir, f);
        if (fs.statSync(src).size > 1024) {
          fs.renameSync(src, out);
        }
        break;
      }
    }
  } catch (err) {
    console.error(`  RECORD FAILED: demo ${segIdx} (${feature.heading}): ${err.message.slice(0, 150)}`);
  } finally {
    if (browser) await browser.close();
  }

  if (fs.existsSync(out)) {
    const stats = fs.statSync(out);
    const dur = getDuration(out);
    console.log(`  ${path.basename(out)}: ${dur.toFixed(1)}s, ${(stats.size / 1024 / 1024).toFixed(1)} MB`);
    return out;
  }
  return null;
}

async function main() {
  const args = process.argv.slice(2);
  const artifactsDir = args[0];
  const segIdx = parseInt(args[1], 10);

  if (!artifactsDir || isNaN(segIdx) || segIdx < 0 || segIdx > 6) {
    console.error('Usage: node record-clip.cjs <artifacts-dir> <segment-index 0-6>');
    process.exit(1);
  }

  const assetsPath = path.join(artifactsDir, 'demo-assets.json');
  const timingPath = path.join(artifactsDir, 'audio-timing.json');

  if (!fs.existsSync(assetsPath)) { console.error('demo-assets.json not found'); process.exit(1); }
  if (!fs.existsSync(timingPath)) { console.error('audio-timing.json not found'); process.exit(1); }

  const assets = JSON.parse(fs.readFileSync(assetsPath, 'utf-8'));
  const timing = JSON.parse(fs.readFileSync(timingPath, 'utf-8'));

  const segment = timing.segments[segIdx];
  const durationSecs = segment.duration_secs + 1.0;
  const label = segIdx === 0 ? 'Opening' : segIdx === 6 ? 'Conclusion' : assets.features[segIdx - 1].heading;

  console.log(`Recording clip ${segIdx} (${label}): ${durationSecs.toFixed(1)}s`);

  if (segIdx === 0) {
    const scene = { type: 'opening_slide', slideType: 'opening', ...assets.opening, title: assets.title };
    await recordSlide(artifactsDir, segIdx, scene, durationSecs);
  } else if (segIdx === 6) {
    const scene = { type: 'conclusion_slide', slideType: 'conclusion', ...assets.conclusion, title: 'Next Steps' };
    await recordSlide(artifactsDir, segIdx, scene, durationSecs);
  } else {
    const feature = assets.features[segIdx - 1];
    const anchorsPath = path.join(artifactsDir, `anchors-segment-${segIdx}.json`);
    const anchorsData = fs.existsSync(anchorsPath) ? JSON.parse(fs.readFileSync(anchorsPath, 'utf-8')) : { anchors: [] };
    await recordDemo(artifactsDir, segIdx, feature, durationSecs, anchorsData);
  }
}

main();
