#!/usr/bin/env node

/**
 * render-demo-slide.cjs
 *
 * Render HTML slides for the blog demo pipeline.
 * Three templates: opening, bullet_intro, conclusion.
 * All Catppuccin Mocha dark theme, self-contained HTML.
 *
 * Usage: node render-demo-slide.cjs <scene-json> [--output slide.html]
 *   or:  node render-demo-slide.cjs --scene-type opening --title "..." ...
 */

const CSS_SHARED = `
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap');
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    width: 1920px; height: 1080px;
    font-family: 'Inter', sans-serif;
    background: linear-gradient(135deg, #1e1e2e 0%, #181825 50%, #11111b 100%);
    color: #cdd6f4;
    display: flex; align-items: center; justify-content: center;
    overflow: hidden;
    position: relative;
  }
  @keyframes bgSweep {
    0% { background-position: 0% 50%; }
    50% { background-position: 100% 50%; }
    100% { background-position: 0% 50%; }
  }
  body.bg-animate {
    background-size: 200% 200%;
    animation: bgSweep 10s ease infinite;
  }
`;

function htmlWrap(title, bodyCss, bodyHtml, extraCss) {
  return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=1920, height=1080">
<title>${title}</title>
<style>
${CSS_SHARED}
${extraCss || ''}
${bodyCss}
</style>
</head>
<body>
${bodyHtml}
</body>
</html>`;
}

function renderOpeningSlide(scene) {
  const { title = '', subtitle = '', bullets = [], footer = '' } = scene;

  const bulletItems = bullets.map((b, i) =>
    `<li style="opacity:0; animation:fadeInUp 0.6s ease forwards; animation-delay:${0.8 + i * 0.3}s;">${b}</li>`
  ).join('\n      ');

  const css = `
    .container { text-align: center; padding: 80px; max-width: 1400px; }
    .title { font-size: 52px; font-weight: 700; color: #cdd6f4; margin-bottom: 16px;
      opacity:0; animation:fadeInUp 0.8s ease forwards; animation-delay:0.2s; }
    .subtitle { font-size: 28px; font-weight: 400; color: #a6adc8; margin-bottom: 48px;
      opacity:0; animation:fadeInUp 0.8s ease forwards; animation-delay:0.5s; }
    .bullets { list-style: none; text-align: left; display: inline-block; }
    .bullets li {
      font-size: 26px; font-weight: 400; color: #bac2de;
      padding: 10px 0 10px 36px;
      background: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='20' height='20' viewBox='0 0 24 24' fill='none' stroke='%2389b4fa' stroke-width='2.5' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpolyline points='20 6 9 17 4 12'%3E%3C/polyline%3E%3C/svg%3E") left center no-repeat;
      background-size: 20px;
    }
    .footer { position: absolute; bottom: 40px; left: 0; right: 0; text-align: center;
      font-size: 16px; color: #585b70;
      opacity:0; animation:fadeIn 1s ease forwards; animation-delay:3s; }
    @keyframes fadeInUp { from { opacity:0; transform:translateY(20px); } to { opacity:1; transform:translateY(0); } }
    @keyframes fadeIn { from { opacity:0; } to { opacity:1; } }
  `;

  const body = `
  <div class="container">
    <div class="title">${escapeHtml(title)}</div>
    <div class="subtitle">${escapeHtml(subtitle)}</div>
    <ul class="bullets">
      ${bulletItems}
    </ul>
  </div>
  <div class="footer">${escapeHtml(footer)}</div>`;

  return htmlWrap(title, css, body);
}

function renderBulletIntroCard(scene) {
  const { heading = '', subtext = '' } = scene;

  const css = `
    .card {
      background: #313244; border: 1px solid #585b70; border-radius: 16px;
      padding: 60px 80px; max-width: 800px; text-align: center;
      transform: scale(0.95); animation: zoomIn 0.5s ease forwards;
    }
    .heading { font-size: 36px; font-weight: 700; color: #cdd6f4; margin-bottom: 16px; }
    .subtext { font-size: 26px; font-weight: 400; color: #a6adc8; line-height: 1.5; }
    .arrow {
      position: absolute; bottom: 80px; font-size: 28px; color: #89b4fa;
      animation: pulse 1.5s ease-in-out infinite;
    }
    @keyframes zoomIn { from { transform:scale(0.85); opacity:0; } to { transform:scale(1); opacity:1; } }
    @keyframes pulse { 0%,100% { opacity:0.4; } 50% { opacity:1; } }
  `;

  const body = `
  <div class="card">
    <div class="heading">${escapeHtml(heading)}</div>
    <div class="subtext">${escapeHtml(subtext)}</div>
  </div>
  <div class="arrow">Let's look →</div>`;

  return htmlWrap(heading, css, body);
}

function renderConclusionSlide(scene) {
  const { title = 'Next Steps', items = [], footer = '' } = scene;

  const itemElements = items.map((item, i) =>
    `<div class="item" style="animation-delay:${0.2 + i * 0.4}s;">
      <span class="dash"></span>
      <span class="text">${escapeHtml(item)}</span>
    </div>`
  ).join('\n    ');

  const css = `
    .container { padding: 80px; max-width: 1000px; }
    .title { font-size: 48px; font-weight: 700; color: #cdd6f4; margin-bottom: 48px;
      opacity:0; animation:fadeInUp 0.6s ease forwards; animation-delay:0.2s; }
    .items { display: flex; flex-direction: column; gap: 20px; }
    .item {
      display: flex; align-items: center; gap: 20px;
      opacity:0; animation:fadeInUp 0.5s ease forwards;
    }
    .dash {
      flex-shrink: 0; width: 4px; height: 32px;
      background: #89b4fa; border-radius: 2px;
    }
    .text { font-size: 28px; font-weight: 400; color: #bac2de; }
    .footer { position: absolute; bottom: 40px; left: 0; right: 0; text-align: center;
      font-size: 16px; color: #585b70; }
    @keyframes fadeInUp { from { opacity:0; transform:translateY(16px); } to { opacity:1; transform:translateY(0); } }
  `;

  const body = `
  <div class="container">
    <div class="title">${escapeHtml(title)}</div>
    <div class="items">
      ${itemElements}
    </div>
  </div>
  <div class="footer">${escapeHtml(footer)}</div>`;

  return htmlWrap(title, css, body);
}

function escapeHtml(s) {
  if (!s) return '';
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function renderSlide(scene) {
  // card_browser_pair scenes embed card data under scene.card
  if (scene.type === 'card_browser_pair' && scene.card) {
    return renderBulletIntroCard({ heading: scene.card.heading, subtext: scene.card.subtext });
  }
  switch (scene.slideType) {
    case 'opening': return renderOpeningSlide(scene);
    case 'bullet_intro': return renderBulletIntroCard(scene);
    case 'conclusion': return renderConclusionSlide(scene);
    default: return renderOpeningSlide(scene);
  }
}

// CLI mode
function main() {
  const args = process.argv.slice(2);

  // Accept scene JSON as file path or inline via --scene-type flags
  if (args[0] && !args[0].startsWith('--')) {
    // First arg is a scene JSON file
    const sceneJson = JSON.parse(fs.readFileSync(args[0], 'utf-8'));
    const outputIndex = args.indexOf('--output');
    const outputPath = outputIndex !== -1 ? args[outputIndex + 1] : 'slide.html';
    fs.mkdirSync(path.dirname(path.resolve(outputPath)), { recursive: true });
    fs.writeFileSync(outputPath, html);
    console.log(`Rendered slide to ${outputPath}`);
    return;
  }

  const typeIndex = args.indexOf('--scene-type');
  const titleIndex = args.indexOf('--title');
  const bulletsIndex = args.indexOf('--bullets');
  const outputIndex = args.indexOf('--output');
  const outputPath = outputIndex !== -1 ? args[outputIndex + 1] : 'slide.html';

  const sceneType = typeIndex !== -1 ? args[typeIndex + 1] : 'opening';

  const scene = { slideType: sceneType };

  if (sceneType === 'opening' || sceneType === 'conclusion') {
    scene.title = titleIndex !== -1 ? args[titleIndex + 1] : 'Demo Title';
    scene.subtitle = 'A demonstration of the blog demo pipeline';
    scene.bullets = bulletsIndex !== -1 ? JSON.parse(args[bulletsIndex + 1]) : ['First deliverable', 'Second deliverable', 'Third deliverable'];
    scene.footer = 'opensassi/a0';
    scene.items = sceneType === 'conclusion' ? ['Next step 1', 'Next step 2', 'Next step 3'] : [];
  } else if (sceneType === 'bullet_intro') {
    scene.heading = titleIndex !== -1 ? args[titleIndex + 1] : 'Feature Demo';
    scene.subtext = 'Details about this feature';
  }

  const html = renderSlide(scene);
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.writeFileSync(outputPath, html);
  console.log(`Rendered ${sceneType} slide to ${outputPath}`);
}

const fs = require('fs');
const path = require('path');

if (require.main === module) main();

module.exports = { renderSlide, renderOpeningSlide, renderBulletIntroCard, renderConclusionSlide, renderScene: renderSlide };
