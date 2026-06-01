#!/usr/bin/env node

/**
 * demo-video-assets-from-session.cjs
 *
 * Generate all 6 narrative assets for a blog demo video directly from
 * the session context using opencode -s --fork queries. Takes a blog
 * markdown file path as input and extracts the session ID from the
 * filename (the hex segment after the last `-` is prefixed with `ses_`).
 *
 * Blog filename format: <date>-<slug>-<hex>.md
 * Example: 2026-06-01-enterprise-handoff-execution-17cd9677effeV3oA1jQrUNFPb9.md
 *          → session ID: ses_17cd9677effeV3oA1jQrUNFPb9
 *
 * Produces:
 *   1. opening  — session title + overview narrative
 *   2-6. features[0..4] — heading, card_description, narrative, demo URL
 *   7. conclusion — wrap-up narrative
 *
 * Usage:
 *   node demo-video-assets-from-session.cjs <blog-file>
 *     [--output demo-assets.json]
 *     [--opencode-path /usr/local/bin/opencode]
 */

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const MIN_WORDS = 70;
const MAX_WORDS = 110;

function extractSessionId(blogFile) {
  const basename = path.basename(blogFile, '.md');
  const parts = basename.split('-');
  const hex = parts[parts.length - 1];
  return 'ses_' + hex;
}

function runQuery(sessionId, prompt, opencodePath) {
  const escapedPrompt = prompt
    .replace(/'/g, "'\\''");

  const cmd = `${opencodePath} run '${escapedPrompt}' -s ${sessionId} --fork 2>/dev/null`;
  try {
    const out = execSync(cmd, { encoding: 'utf-8', timeout: 300000, maxBuffer: 10 * 1024 * 1024 });
    // Strip any ANSI escape sequences or prompt artifacts
    let text = out.trim();
    // Remove lines that look like prompts or markdown fences
    text = text.replace(/^>\s+\w+\s+[·•]\s+[\w-]+\s*\n?/gm, '');
    text = text.replace(/^```(?:json)?\s*\n?/gm, '');
    text = text.replace(/^```\s*\n?/gm, '');
    text = text.replace(/^\$\s+.*\n?/gm, '');
    text = text.trim();
    return text;
  } catch (err) {
    console.error(`  Query failed: ${err.message.slice(0, 200)}`);
    return null;
  }
}

function countWords(s) {
  return s ? s.split(/\s+/).filter(Boolean).length : 0;
}

async function main() {
  const args = process.argv.slice(2);
  const blogFile = args[0];
  const oi = args.indexOf('--output');
  const outputPath = oi !== -1 ? args[oi + 1] : 'demo-assets.json';
  const oi2 = args.indexOf('--opencode-path');
  const opencodePath = oi2 !== -1 ? args[oi2 + 1] : 'opencode';

  if (!blogFile) {
    console.error('Usage: node demo-video-assets-from-session.cjs <blog-file> [--output demo-assets.json] [--opencode-path opencode]');
    console.error('  Blog file: path to a blog markdown file in blog/<slug>.md format');
    process.exit(1);
  }

  if (!fs.existsSync(blogFile)) {
    console.error(`Blog file not found: ${blogFile}`);
    process.exit(1);
  }

  const sessionId = extractSessionId(blogFile);
  const slug = path.basename(blogFile, '.md');
  console.log(`Blog: ${blogFile}`);
  console.log(`Session ID: ${sessionId}\n`);

  const assets = {
    session_id: sessionId,
    slug: slug,
    generated_at: new Date().toISOString(),
    title: '',
    opening: { narrative: '' },
    features: [],
    conclusion: { narrative: '' },
  };

  // ── Step 1: Generate title ─────────────────────────────────────
  console.log('Step 1/9: Generating demo title...');
  const titleResult = runQuery(sessionId,
    `generate a short title for a demo video of this session. Rules: plain text, no markdown formatting or quotes, max 80 characters. Output ONLY the title, nothing else.`,
    opencodePath);
  assets.title = titleResult || 'Session Demo';
  console.log(`  Title: ${assets.title}`);

  // ── Step 2: Generate opening narrative ───────────────────────────
  console.log('Step 2/9: Generating opening narrative...');
  const openingResult = runQuery(sessionId,
    `from this session, generate an opening narration for a demo video titled "${assets.title}". This plays during the opening slide. Introduce the session and give a 1-2 sentence overview of what the viewer will see. Target 50-60 words (about 15-20 seconds spoken). Natural conversational tone, plain text only, no markdown, no formatting. Output ONLY the narrative.`,
    opencodePath);
  assets.opening.narrative = openingResult || '';
  console.log(`  Opening: ${countWords(assets.opening.narrative)} words`);

  // ── Step 3: Generate 5 feature topics ────────────────────────────
  console.log('Step 3/9: Generating 5 feature topics...');
  const topicsResult = runQuery(sessionId,
    `from this session, for a demo video titled "${assets.title}", list exactly 5 product features to demo. Rules:
- Each heading: 3-5 words, plain text, no markdown
- Each must have a browser-accessible GitHub URL (issue/PR/commit/project board/file page)
- Description: one sentence, plain text, no markdown
Output ONLY as raw JSON, no markdown, no code fences, no backticks, no surrounding text:
[{"heading":"...","url":"...","description":"..."}]`,
    opencodePath);

  let topics = [];
  if (topicsResult) {
    try {
      // Try to extract JSON from the response
      const jsonMatch = topicsResult.match(/\[[\s\S]*\]/);
      topics = JSON.parse(jsonMatch ? jsonMatch[0] : topicsResult);
      if (!Array.isArray(topics)) topics = [];
    } catch (e) {
      console.error(`  Failed to parse topics JSON: ${e.message}`);
    }
  }
  console.log(`  Found ${topics.length} topics`);

  // ── Steps 4-8: Generate narrative per feature ──────────────────
  for (let i = 0; i < topics.length; i++) {
    const t = topics[i];
    const stepNum = 4 + i;
    console.log(`Step ${stepNum}/9: Generating narrative for "${t.heading}"...`);

    const narrativeResult = runQuery(sessionId,
      `from this session, for the feature "${t.heading}" at ${t.url}, generate demo text for a video. Write a short narrative in natural conversational tone for a browser demo voiceover. Explain the feature, how it fits the session context, and why it matters. Also generate a one-sentence card description for the title card slide. Target ${MIN_WORDS}-${MAX_WORDS} words (about 30 seconds spoken). Rules: plain text only, no markdown, no formatting. Output ONLY as raw JSON with no markdown, no code fences, no backticks: {"heading":"${t.heading}","narrative":"...","card_description":"...","url":"${t.url}"}`,
      opencodePath);

    if (narrativeResult) {
      try {
        const jsonMatch = narrativeResult.match(/\{[\s\S]*\}/);
        const parsed = JSON.parse(jsonMatch ? jsonMatch[0] : narrativeResult);
        t.narrative = parsed.narrative || '';
        t.card_description = parsed.card_description || t.description;
        t.url = parsed.url || t.url;
      } catch (e) {
        console.error(`  Failed to parse narrative JSON: ${e.message}`);
        t.narrative = '';
        t.card_description = t.description;
      }
    }
    console.log(`  Narrative: ${countWords(t.narrative)} words`);
  }

  assets.features = topics;

  // ── Step 9: Generate conclusion narrative ─────────────────────────
  console.log('Step 9/9: Generating conclusion narrative...');
  const conclusionResult = runQuery(sessionId,
    `from this session, generate a conclusion narration for a demo video titled "${assets.title}". This plays during the closing "Next Steps" slide. Briefly summarize what was built and point to the next milestones (Open Source June 15, Cloud Beta July 1, Enterprise August 15). Target 40-50 words (about 15 seconds spoken). Natural conversational tone, plain text only, no markdown, no formatting. Output ONLY the narrative.`,
    opencodePath);
  assets.conclusion.narrative = conclusionResult || '';
  console.log(`  Conclusion: ${countWords(assets.conclusion.narrative)} words`);

  const outputDir = path.dirname(path.resolve(outputPath));

  // ── Write JSON output ────────────────────────────────────────
  fs.mkdirSync(outputDir, { recursive: true });
  fs.writeFileSync(outputPath, JSON.stringify(assets, null, 2));
  console.log('');
  console.log(`Assets written to ${outputPath}`);

  // ── Extract individual narrative .txt files ───────────────────
  console.log('');
  console.log('Extracting narrative .txt files...');

  function slugify(s) {
    return s.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 50);
  }

  // 00 — opening
  const openingPath = path.join(outputDir, 'narrative-00-opening.txt');
  fs.writeFileSync(openingPath, (assets.opening.narrative || '') + '\n');
  console.log(`  narrative-00-opening.txt (${countWords(assets.opening.narrative)} words)`);

  // 01-05 — features
  assets.features.forEach((f, i) => {
    const num = String(i + 1).padStart(2, '0');
    const slug = slugify(f.heading);
    const featurePath = path.join(outputDir, `narrative-${num}-${slug}.txt`);
    fs.writeFileSync(featurePath, (f.narrative || '') + '\n');
    console.log(`  narrative-${num}-${slug}.txt (${countWords(f.narrative)} words)`);
  });

  // 06 — conclusion
  const conclusionPath = path.join(outputDir, 'narrative-06-conclusion.txt');
  fs.writeFileSync(conclusionPath, (assets.conclusion.narrative || '') + '\n');
  console.log(`  narrative-06-conclusion.txt (${countWords(assets.conclusion.narrative)} words)`);

  // Summary
  console.log('');
  console.log(`  Title: ${assets.title}`);
  console.log(`  Opening: ${countWords(assets.opening.narrative)} words`);
  console.log(`  Features: ${assets.features.length}`);
  assets.features.forEach((f, i) => console.log(`    ${i + 1}. ${f.heading}: ${countWords(f.narrative)} words`));
  console.log(`  Conclusion: ${countWords(assets.conclusion.narrative)} words`);
}

main();
