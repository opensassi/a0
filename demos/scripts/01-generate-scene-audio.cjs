#!/usr/bin/env node

/**
 * generate-scene-audio.cjs
 *
 * Read narrative .txt files from the .artifacts directory, generate TTS
 * audio for each using edge-tts native output (AAC in ADTS, 48kbps,
 * 24kHz mono), measure duration, and output a timing JSON artifact.
 *
 * No format conversion. edge-tts output is saved as-is (native .aac).
 *
 * Input:  .artifacts/narrative-01-opening.txt
 *         .artifacts/narrative-02-*.txt  (features)
 *         .artifacts/narrative-07-conclusion.txt
 *
 * Output: .artifacts/audio-01-opening.aac
 *         .artifacts/audio-02-*.aac
 *         .artifacts/audio-07-conclusion.aac
 *         .artifacts/audio-timing.json
 *
 * Usage:
 *   node generate-scene-audio.cjs <artifacts-dir>
 *     [--voice en-US-AriaNeural]
 */

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

function getAudioDuration(aacPath) {
  const out = execSync(
    `ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 ${aacPath}`,
    { encoding: 'utf-8', timeout: 10000 }
  ).trim();
  return parseFloat(out) || 0;
}

function generateTts(text, voice, outputPath) {
  const escaped = text.replace(/'/g, "'\\''");
  const cmd = `edge-tts --voice ${voice} --text '${escaped}' --write-media ${outputPath} 2>/dev/null`;
  execSync(cmd, { stdio: 'pipe', encoding: 'utf-8', timeout: 120000 });
  return fs.existsSync(outputPath);
}

function main() {
  const args = process.argv.slice(2);
  const artifactsDir = args[0];
  const vi = args.indexOf('--voice');
  const voice = vi !== -1 ? args[vi + 1] : 'en-US-AriaNeural';

  if (!artifactsDir) {
    console.error('Usage: node generate-scene-audio.cjs <artifacts-dir> [--voice en-US-AriaNeural]');
    process.exit(1);
  }

  if (!fs.existsSync(artifactsDir)) {
    console.error(`Artifacts directory not found: ${artifactsDir}`);
    process.exit(1);
  }

  const txtFiles = fs.readdirSync(artifactsDir)
    .filter(f => f.startsWith('narrative-') && f.endsWith('.txt'))
    .sort();

  if (txtFiles.length === 0) {
    console.error(`No narrative-*.txt files found in ${artifactsDir}`);
    process.exit(1);
  }

  const timing = {
    generated_at: new Date().toISOString(),
    voice: voice,
    format: 'edge-tts native AAC (ADTS, 48kbps, 24kHz mono)',
    padding_secs: 0,
    padding_file: '',
    segments: [],
    total_duration_secs: 0,
  };

  console.log(`Generating audio for ${txtFiles.length} narrative segments (voice: ${voice})...\n`);

  for (const txtFile of txtFiles) {
    const txtPath = path.join(artifactsDir, txtFile);
    const text = fs.readFileSync(txtPath, 'utf-8').trim();

    if (!text) {
      console.log(`  ${txtFile}: empty — skipped`);
      continue;
    }

    const baseName = txtFile.replace(/^narrative-/, 'audio-').replace(/\.txt$/, '.aac');
    const aacPath = path.join(artifactsDir, baseName);

    const seg = { segment: txtFile, word_count: text.split(/\s+/).filter(Boolean).length, char_count: text.length, duration_secs: 0, audio_file: baseName };

    try {
      console.log(`  ${txtFile}: generating TTS (${seg.char_count} chars)...`);
      const ttsOk = generateTts(text, voice, aacPath);
      if (!ttsOk) throw new Error('TTS produced no output');
      const fileSize = fs.statSync(aacPath).size;

      const duration = getAudioDuration(aacPath);
      if (duration <= 0) throw new Error(`ffprobe returned ${duration}s`);

      seg.duration_secs = duration;

      const m = Math.floor(duration / 60);
      const s = (duration % 60).toFixed(1);
      console.log(`    → ${m > 0 ? `${m}m ` : ''}${s}s | ${(fileSize / 1024).toFixed(0)} KB`);

    } catch (err) {
      console.error(`    FAILED: ${err.message.slice(0, 120)}`);
      seg.duration_secs = 0;
      seg.error = err.message.slice(0, 200);
    }

    timing.segments.push(seg);
  }

  timing.total_duration_secs = timing.segments.reduce((s, n) => s + n.duration_secs, 0);

  // ── Write timing JSON ──────────────────────────────────────────
  const timingPath = path.join(artifactsDir, 'audio-timing.json');
  fs.writeFileSync(timingPath, JSON.stringify(timing, null, 2));

  const rawMin = Math.floor(timing.total_duration_secs / 60);
  const rawSec = (timing.total_duration_secs % 60).toFixed(1);
  console.log(`\nTotal: ${rawMin > 0 ? `${rawMin}m ` : ''}${rawSec}s`);
  console.log(`Timing: ${timingPath}`);
}

main();
