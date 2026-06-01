#!/usr/bin/env node

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');
const { chromium } = require('playwright');

async function main() {
  const args = process.argv.slice(2);
  const artifactsDir = args[0];
  const segIdx = parseInt(args[1], 10);

  if (!artifactsDir || isNaN(segIdx) || segIdx < 1 || segIdx > 5) {
    console.error('Usage: node fetch-segment.cjs <artifacts-dir> <segment-index 1-5>');
    process.exit(1);
  }

  const assetsPath = path.join(artifactsDir, 'demo-assets.json');
  const timingPath = path.join(artifactsDir, 'audio-timing.json');

  if (!fs.existsSync(assetsPath)) { console.error('demo-assets.json not found'); process.exit(1); }
  if (!fs.existsSync(timingPath)) { console.error('audio-timing.json not found'); process.exit(1); }

  const assets = JSON.parse(fs.readFileSync(assetsPath, 'utf-8'));
  const timing = JSON.parse(fs.readFileSync(timingPath, 'utf-8'));

  const feature = assets.features[segIdx - 1];
  const segDuration = timing.segments[segIdx].duration_secs;
  const outPath = path.join(artifactsDir, `anchors-segment-${segIdx}.json`);

  console.log(`[${segIdx}] ${feature.heading}: ${feature.url}`);

  // ── Fetch scroll stops ──
  const browser = await chromium.launch({ headless: false, args: ['--no-sandbox', '--disable-setuid-sandbox'] });
  const context = await browser.newContext({ viewport: { width: 1920, height: 1080 } });
  const page = await context.newPage();

  let data;
  try {
    await page.goto(feature.url, { waitUntil: 'load', timeout: 30000 });
    await page.waitForTimeout(1000);

    const pageHeight = await page.evaluate(() => document.body.scrollHeight || document.documentElement.scrollHeight);
    const vpH = 1080;
    const scrollStops = [];

    for (let pct = 0; pct <= 90; pct += 10) {
      const y = Math.round((pageHeight - vpH) * (pct / 100));
      await page.evaluate((sy) => window.scrollTo(0, sy), Math.max(0, y));
      await page.waitForTimeout(300);

      const centerText = await page.evaluate(() => {
        const el = document.elementFromPoint(960, 540);
        if (!el) return '';
        const block = el.closest('article, main, section, div, td, li, p, h1, h2, h3, h4');
        return block ? (block.textContent || '').trim().slice(0, 300) : (el.textContent || '').trim().slice(0, 300);
      });

      scrollStops.push({ pct, y: Math.max(0, y), text: centerText });
    }

    data = { segment: segIdx, url: feature.url, page_height: pageHeight, scroll_stops: scrollStops };
    console.log(`    ${scrollStops.length} scroll stops, page height: ${pageHeight}px`);

  } catch (err) {
    console.error(`    FETCH FAILED: ${err.message}`);
    data = { segment: segIdx, url: feature.url, page_height: 0, scroll_stops: [], error: err.message };
  } finally {
    await page.close();
    await browser.close();
  }

  // ── Infer anchors ──
  if (!data.scroll_stops || data.scroll_stops.length === 0 || !data.scroll_stops.some(s => s.text)) {
    console.log(`    No scroll text available — using fallback anchors`);
    data.anchors = [
      { time_secs: 0.5, scroll_pct: 0 },
      { time_secs: segDuration * 0.4, scroll_pct: 50 },
      { time_secs: segDuration - 0.5, scroll_pct: 90 },
    ];
  } else {
    const stopLines = data.scroll_stops
      .map(s => `${s.pct}% y=${s.y} → "${s.text.slice(0, 150)}"`).join('\n');

    const prompt = `Given a demo narrative and scroll stops from a GitHub page, choose 3-5 scroll positions with timing that visually align the narrative to the page content for a recorded demo.

Segment duration: ${segDuration.toFixed(1)}s
Narrative: "${feature.narrative}"
URL: ${feature.url}

Page scroll stops (text at each 10% increment of page height):
${stopLines}

Rules:
- time_secs must be spread across the ${segDuration.toFixed(1)}s duration
- First anchor at time_secs = 0.5
- Last anchor at time_secs = ${(segDuration - 0.5).toFixed(1)} (leave time to view)
- scroll_pct must match one of the provided percentages
- Choose passages whose visible text most relates to the narrative
- No markdown, no backticks

Output ONLY a raw JSON array:
[{"time_secs": 0.5, "scroll_pct": 0}, {"time_secs": 8.0, "scroll_pct": 30}, ...]`;

    const escaped = prompt.replace(/'/g, "'\\''");
    const cmd = `opencode run '${escaped}' 2>/dev/null`;

    console.log(`    Inferring anchors...`);
    try {
      const raw = execSync(cmd, { encoding: 'utf-8', timeout: 120000 }).trim();
      const json = raw.replace(/^```(?:json)?\s*|```\s*$/g, '').trim();
      const anchors = JSON.parse(json);

      if (!Array.isArray(anchors) || anchors.length === 0) throw new Error('Bad response');

      data.anchors = anchors;
      console.log(`    ${anchors.length} anchors: ${anchors.map(a => `${a.time_secs}s@${a.scroll_pct}%`).join(', ')}`);

    } catch (err) {
      console.error(`    Inference failed: ${err.message}`);
      data.anchors = [
        { time_secs: 0.5, scroll_pct: 0 },
        { time_secs: segDuration * 0.4, scroll_pct: 50 },
        { time_secs: segDuration - 0.5, scroll_pct: 90 },
      ];
      console.log(`    Fallback: ${data.anchors.map(a => `${a.time_secs.toFixed(1)}s@${a.scroll_pct}%`).join(', ')}`);
    }
  }

  fs.writeFileSync(outPath, JSON.stringify(data, null, 2));
  console.log(`    Written to ${outPath}`);
}

main();
