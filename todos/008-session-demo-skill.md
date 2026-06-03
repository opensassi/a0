# Todo: Create `skills/local/session-demo` Skill

**Created:** 2026-06-03
**Session:** session-demo skill planning — demos/prompt.md pipeline analysis
**Priority:** high

## Context

Generate demo videos from a a0 session using an 8-stage pipeline with per-stage verification via ffmpeg sync checks. Each stage produces assets in `.artifacts/` tracked by a `plan.json`. Follows the flow from `demos/prompt.md` but re-architected as a skill with staged, verifiable asset generation.

Depends on Todo 007 (a0 infrastructure: user prompt, session query, Playwright record video). Initial version uses workarounds until those land.

---

## Files to Create

```
skills/local/session-demo/
├── skill.json                                 # 12 tools + 1 prompt
├── scripts/
│   ├── 00-demo-video-assets-from-session.cjs  # COPY+MODIFY from demos/scripts/
│   ├── 01-generate-scene-audio.cjs            # COPY from demos/scripts/
│   ├── 02-pad-audio.sh                        # COPY from demos/scripts/
│   ├── 03-fetch-segment.cjs                   # REWRITE — browser.sh bridge
│   ├── 04-record-clip.cjs                     # MODIFY — bridge nav + direct Playwright record
│   ├── 05-stitch-audio.sh                     # COPY from demos/scripts/
│   ├── 06-stitch-video.sh                     # COPY from demos/scripts/
│   ├── 07-mix-final.sh                        # COPY from demos/scripts/
│   ├── render-demo-slide.cjs                  # COPY from demos/scripts/
│   ├── demo-plan.sh                           # NEW — plan.json CRUD
│   ├── demo-verify.sh                         # NEW — ffmpeg/ffprobe per-stage verifier
│   └── demo-prompt.sh                         # NEW — stdin/stdout user prompt
└── prompts/
    └── session_demo.md                        # NEW — stage-by-stage orchestration prompt
```

---

## skill.json — Tool Definitions

### Stage Tools

| Tool | Script | Params (stdin JSON) | Timeout |
|------|--------|---------------------|---------|
| `demo_setup` | `00-...cjs` | `{sessionId, slug, artifactsDir}` | 300s |
| `demo_generate_tts` | `01-...cjs` | `{artifactsDir}` | 600s |
| `demo_pad_audio` | `02-...sh` | `{audioFile}` | 60s |
| `demo_fetch_segment` | `03-...cjs` | `{artifactsDir, index}` | 120s |
| `demo_record_clip` | `04-...cjs` | `{artifactsDir, index}` | 180s |
| `demo_stitch_audio` | `05-...sh` | `{artifactsDir, output, files[]}` | 300s |
| `demo_stitch_video` | `06-...sh` | `{artifactsDir, output, files[]}` | 300s |
| `demo_mix_final` | `07-...sh` | `{artifactsDir, outputPath}` | 300s |

### Utility Tools

| Tool | Script | Params | Timeout | Purpose |
|------|--------|--------|---------|---------|
| `demo_plan` | `demo-plan.sh` | `{action, ...}` | 10s | plan.json CRUD |
| `demo_verify` | `demo-verify.sh` | `{stage, artifactsDir}` | 120s | ffmpeg/ffprobe per-stage check |
| `demo_prompt_user` | `demo-prompt.sh` | `{message, options[]}` | 300s | Read user response from stdin |
| `demo_list_artifacts` | `ls -la` | `{artifactsDir}` | 10s | List files in artifacts dir |

### Skill Args

| Arg | Type | Default | Description |
|-----|------|---------|-------------|
| `session-id` | string | — | **Required.** Session UUID to generate demo from |
| `slug` | string | auto | Demo directory name (default: derived from session) |
| `auto` | boolean | false | Skip all user prompts, auto-confirm stages |
| `rebuild-all` | boolean | false | Clear plan.json and re-run all stages fresh |

Command paths in Docker: `/opt/a0/skills/local/session-demo/scripts/<script>`

---

## Script Modifications (vs `demos/scripts/` Originals)

### `00-demo-video-assets-from-session.cjs`

**Changes:**
- Replace `opencode` binary references with `a0` binary
- Accept `--session-id <uuid>` as alternative to blog filename input
- Keep LLM query generation of title, opening narrative, 5 features, conclusion
- Fallback: if no `session:query` tool yet (Todo 007#2), Stage 00 is handled by the prompt instructing the LLM to write files directly

**Current (line 44):**
```javascript
const cmd = `${opencodePath} run '${escapedPrompt}' -s ${sessionId} --fork 2>/dev/null`;
```

**Target:**
```javascript
const cmd = `${a0Path} session query ${sessionId} '${escapedPrompt}' 2>/dev/null`;
// or fallback: tool returns error, prompting LLM to generate directly
```

### `03-fetch-segment.cjs` — Rewrite

**Current:** Launches its own Chromium via `require('playwright')`, navigates, scrolls, captures text.

**Target:** Use `child_process.execSync()` to call the Playwright bridge via `browser.sh`:
- `bash /opt/a0/scripts/browser.sh navigate` — go to feature URL
- `bash /opt/a0/scripts/browser.sh evaluate` — get page height
- `bash /opt/a0/scripts/browser.sh evaluate` — scroll to position, get center text
- Keep anchor inference via a0 or LLM, but change binary from `opencode`

**Input/output unchanged:** Reads `demo-assets.json` + `audio-timing.json`, writes `anchors-segment-N.json`.

### `04-record-clip.cjs` — Modify

**Changes:**
- Slide opening (index 0) and conclusion (index 6): Render HTML slide, open via `browser.sh navigate file://<html-path>`, record via direct Playwright `recordVideo`
- Feature clips (index 1-5): Navigate via `browser.sh navigate`, scroll via `browser.sh evaluate`, record via direct Playwright
- Keep `require('playwright')` only for `recordVideo` — removed once Todo 007#3 lands
- Remove `browser.newContext()` / `chromium.launch()` — slide HTML rendering stays

---

## plan.json Schema

Managed by `demo-plan.sh`. Tracks artifact approval for partial rebuilds.

```json
{
  "slug": "my-demo",
  "sessionId": "ses_abc123",
  "rebuildAll": false,
  "stages": {
    "00_assets":       "approved",
    "01_tts":          "approved",
    "02_pad":          "failed",
    "03_fetch":        "pending",
    "04_record":       "pending",
    "05_stitch_audio": "pending",
    "06_stitch_video": "pending",
    "07_mix":          "pending"
  },
  "audioSegments": [
    { "index": 0, "label": "opening", "file": "audio-00-opening.aac", "dur": 15.0,
      "paddedFile": "audio-00-opening-padded.aac", "paddedDur": 16.0,
      "included": true, "status": "approved" }
  ],
  "videoClips": [
    { "index": 0, "label": "opening", "file": "clip-00-opening.webm",
      "dur": 16.2, "included": true, "status": "approved" }
  ],
  "stitched": {
    "audio": { "file": "audio-stitched.m4a", "dur": 0, "status": "pending" },
    "video": { "file": "video-stitched.mp4", "dur": 0, "status": "pending" }
  },
  "final": { "file": "../my-demo.mp4", "dur": 0, "status": "pending" }
}
```

### `demo-plan.sh` Operations

```
init <slug> <sessionId>
get
update-stage <name> <status>              # pending/generated/verified/approved/failed
stages-to-run                              # returns JSON array of unfinished stage names
add-segment <index> <file> <dur>          # and sets status=generated
update-segment <index> <field> <value>
add-clip <index> <file> <dur>
update-clip <index> <field> <value>
set-included segment|clip <index> true|false
mark-approved <dotpath>                    # e.g. stitched.audio or stages.00_assets
set-rebuild-all true|false
```

---

## Per-Stage Verification (`demo-verify.sh`)

| Stage | Check | Method | Tolerance |
|-------|-------|--------|-----------|
| 00 | `demo-assets.json` valid JSON, has `title`/`features[]`/`opening`/`conclusion`; `narrative-*.txt` exist | `jq` + `test -f` | — |
| 01 | Each `audio-*.aac` — duration > 0 | `ffmpeg -f null` full decode | — |
| 02 | Each `*-padded.aac` — duration = original + 1.0s | `ffmpeg -f null` both files | ±0.3s |
| 03 | Each `anchors-segment-*.json` exists, `anchors[]` non-empty | `jq` | — |
| 04 | Each `clip-*.webm` — duration ≥ audio segment duration | `ffprobe` + plan.json lookup | — |
| 05 | `audio-stitched.m4a` — duration ≈ sum of included padded segments | `ffmpeg -f null` | ±1s |
| 06 | `video-stitched.mp4` — duration ≈ `audio-stitched.m4a` | `ffprobe` + `ffmpeg -f null` | **±2s** |
| 07 | Final `.mp4` — both audio+video streams, durations match | `ffprobe` streams + duration | ±0.5s |

**Key: Audio uses full decode via `ffmpeg -f null` (ffprobe is inaccurate for AAC).** Pattern:
```bash
dur=$(ffmpeg -i "$file" -f null - 2>&1 | grep -oP 'time=\K[\d:\.]+' | tail -1)
IFS=: read -r h m s <<<"$dur"
dur=$(echo "$h * 3600 + $m * 60 + $s" | bc)
```

Verifier output JSON:
```json
{ "stage": "06_stitch_video", "passed": false, "duration": { "video": 48.2, "audio": 45.0, "delta": 3.2, "tolerance": 2.0 }, "checks": [ { "name": "video exists", "ok": true }, { "name": "audio sync", "ok": false, "reason": "3.2s delta exceeds 2.0s tolerance" } ] }
```

---

## Prompt Flow (`prompts/session_demo.md`)

```
INIT:
  Read session-id, slug, auto, rebuild-all from skill args
  mkdir -p demos/<slug>/.artifacts/
  demo_plan init <slug> <sessionId>
  If rebuild-all: demo_plan set-rebuild-all true

STAGE 00 (Assets — LLM Driven):
  If plan approved → skip
  Generate from session context: title, opening (50-60w), 5 features (headings+URLs+descriptions+70-110w narratives), conclusion (40-50w)
  Write demo-assets.json and narrative-*.txt files
  demo_verify 00
  demo_plan update-stage 00_assets approved|failed
  demo_prompt_user "Stage 00: <summary>. Continue?"

STAGE 01 (TTS):
  demo_generate_tts → demo_verify 01 → plan update → prompt

STAGE 02 (Pad — one file at a time):
  demo_list_artifacts → identify audio-*.aac excluding *-padded
  For each: demo_pad_audio → demo_plan add-segment
  demo_verify 02 → plan update → prompt

STAGE 03 (Fetch anchors — one at a time, indices 1-5):
  For each: demo_fetch_segment N → demo_plan update-segment
  demo_verify 03 → plan update → prompt

STAGE 04 (Record clips — one at a time, indices 0-6):
  For each: demo_record_clip N → demo_plan add-clip
  demo_verify 04 → plan update → prompt

STAGE 05 (Stitch Audio):
  demo_list_artifacts → sorted padded audio files
  demo_prompt_user "Select segments to include:" with file listing
  demo_stitch_audio with selected files
  demo_verify 05 → plan update → prompt

STAGE 06 (Stitch Video):
  demo_list_artifacts → sorted clip files
  demo_prompt_user "Select clips to include:" (match stage 05 selection)
  demo_stitch_video with selected clips
  demo_verify 06 (SYNC CHECK)
  If delta > 2s: warn, offer continue/abort/trim
  → plan update → prompt

STAGE 07 (Mix):
  demo_mix_final → demo_verify 07
  demo_prompt_user "Final: demos/<slug>/<slug>.mp4. Verify?"
```

---

## Docker Changes

Add to `docker/Dockerfile.playwright` Stage 2:

```dockerfile
# Session-demo: ffmpeg for audio/video, edge-tts for TTS
RUN apt-get install -y ffmpeg python3 python3-pip && \
    pip3 install --break-system-packages edge-tts
```

No changes to `COPY` paths — `COPY skills/ /opt/a0/skills/` already picks up `skills/local/session-demo/`.

---

## Dependencies

The prompt declares:

```json
"dependencies": [
  "local:playwright:browser_navigate",
  "local:playwright:browser_evaluate",
  "local:playwright:browser_snapshot",
  "local:playwright:browser_close",
  "system:fs:write",
  "system:fs:read",
  "system:fs:glob",
  "system:bash:bash"
]
```

---

## Workarounds (until Todo 007 lands)

| Missing feature (007) | Workaround |
|-----------------------|------------|
| `system:meta:user_prompt` | `demo-prompt.sh` reads stdin directly — works in REPL mode; in `a0 run` mode, `--skill-arg=session-demo-auto=true` skips prompts |
| `system:session:query` | Stage 00 is LLM-driven: the prompt tells the LLM to generate assets from its session memory. The `00-*.cjs` script is fallback if a0 gets the feature |
| Playwright `record_video` bridge action | `04-record-clip.cjs` keeps `require('playwright')` for video recording only. All other browser interaction (nav, scroll) goes through `browser.sh` bridge |

---

## Implementation Order

1. `demo-plan.sh` — plan.json CRUD (standalone, no dependencies)
2. `demo-verify.sh` — ffmpeg/ffprobe verification (standalone)
3. `demo-prompt.sh` — stdin/stdout user interaction (standalone)
4. `skill.json` — tool definitions, args, prompt reference
5. `prompts/session_demo.md` — orchestration prompt
6. Copy+modify scripts `00-*.cjs` through `07-*.sh` from `demos/scripts/`
7. Rewrite `03-fetch-segment.cjs` for browser.sh bridge
8. Modify `04-record-clip.cjs` for bridge nav + direct Playwright record
9. Update `docker/Dockerfile.playwright` with ffmpeg + edge-tts
10. Test end-to-end: one complete pipeline run with user verification
