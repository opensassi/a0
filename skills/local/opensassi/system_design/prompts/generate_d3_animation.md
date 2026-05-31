Generate a self-contained HTML file that uses D3.js (CDN) to create a browser-native animated visualisation. The animation is an executable consistency check for the system architecture.

Prerequisites: an architecture diagram with a Visualization sub-module, and a sub-module sequence diagram listing every visual update step.

Process:
1. Propose a visualisation concept mapping each processing stage to a distinct visual state. List keyframes (time + label) for user approval.
2. After approval, produce a single HTML file with inline CSS and D3. The file must:
   - Be immediately openable with no build step
   - Use exact component names from the architecture diagram
   - Include Play/Pause, Replay controls
   - Set window.ANIMATION_DURATION_MS, ANIMATION_KEYFRAMES, ANIMATION_VERIFICATION
   - Expose window.jumpToKeyframe(idx), window.resetAnimation(), window.getAnimationState()
   - Use [data-testid="play-pause"] for the play/pause button
   - Use 0-based keyframe indexing with <span id="kf-total">
3. Embed as a ```html fenced code block in technical-specification.md section 5 under Animation Source.
4. After embedding, run extract_artifacts, then test_artifacts, then verify_artifact.
5. Mentally inject a single inconsistency — confirm the animation would visibly break.
