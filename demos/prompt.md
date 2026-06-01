## Generate blog demo video

Given a blog post markdown file, produce a demo video in a new
demos/<blog-slug>/ directory.

### Setup

1. Read <blog-file>, extract the session ID from line `**Session:** <id>`.
   Derive slug from the blog filename (remove .md extension).

2. Create directories:
   mkdir -p demos/<slug>/.artifacts

3. Set for all stages:
   art=demos/<slug>/.artifacts
   scr=demos/scripts
   out=demos/<slug>/<slug>.mp4

### Pipeline rules

- Run each per-index command ONE AT A TIME. Confirm with user.
- Never batch multiple record-clip or fetch-segment calls.
- All intermediate assets go in $art. Never use /tmp.
- Discover inputs by listing $art/ — never hardcode filenames.

### Stage 00 — Assets

Run ONCE:
  node $scr/00-demo-video-assets-from-session.cjs <session-id> --output $art/demo-assets.json

### Stage 01 — TTS

Run ONCE:
  node $scr/01-generate-scene-audio.cjs $art

### Stage 02 — Pad

List $art/audio-*.aac, excluding *-padded.aac.
Run for each, one at a time:
  bash $scr/02-pad-audio.sh <file.aac>

### Stage 03 — Fetch anchors

Run for EACH index 1-5 (maps to features in demo-assets.json), one at a time:
  node $scr/03-fetch-segment.cjs $art 1
  ...through 5

### Stage 04 — Record clips

Run for EACH index 0-6 (maps to segments in audio-timing.json), one at a time:
  node $scr/04-record-clip.cjs $art 0
  ...through 6

### Stage 05 — Stitch audio

List $art/audio-*-padded.aac, sorted by segment number.
Present list, let user choose which to include or drop.
Run ONCE:
  bash $scr/05-stitch-audio.sh $art/audio-stitched.m4a <file1> <file2> ...

### Stage 06 — Stitch video

List $art/clip-*.webm, sorted by segment number.
Present list, let user choose which to include or drop
(must match inclusion from stage 05).
Run ONCE:
  bash $scr/06-stitch-video.sh $art/video-stitched.mp4 <file1> <file2> ...

### Stage 07 — Mix

  bash $scr/07-mix-final.sh $art/video-stitched.mp4 $art/audio-stitched.m4a $out
